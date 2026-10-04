"""Habitable Zone v2 terrain geometry from Build/Maps/AvailabilityZoneV2.json, with no Unreal dependency.

Imported by Build/GenerateAvailabilityZoneV2.py (spawns what this computes), Build/DrawAvailabilityZoneV2.py (audits,
render) and Tools/tests/maps. Everything is centimetres and degrees on Unreal world axes (+X minimap-up, +Y
minimap-right, yaw turns +X toward +Y).

The terrain kit's 400 cm grid (Build/GenerateTerrainKit.py "GRID CONTRACT"): cell (i, j) covers 400 i +- 200 and
400 j +- 200 and belongs to the region whose polygon holds the cell centre. A High-ground region is a plateau of every
cell its polygon touches (so no ground-level strip is left inside a plateau region's polygon, and units walking at a
cliff foot never count as standing in it), at z = `plateau_height`; its border toward a neighbour is a cliff except where an authored Ramp_Wide (800 x 800 cm,
standing on the lower neighbour's ground, top edge on the plateau edge) climbs it. A border without a ramp is therefore
closed, as is every `terrain.closed_borders` pair (a two-cell rock wall of plateau pieces). Closed borders are not
neighbours: `neighbours` is the polygon adjacency minus them, so supply follows what units can walk.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Collection, Iterator, Mapping, Sequence
import itertools
import json
import math
from pathlib import Path
import random
from typing import Any

import MatchLayout

CELL = 400
RAMP_RUN = 800.0
RAMP_HALF_STRIP = 350.0  # Ramp_Wide walkable strip is 700 cm between its parapets
DIRS = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}
TRAITS = ("high_ground", "cover", "open", "hazard")
# Env-kit footprints (X, Y metres) of the cover props; GenerateAvailabilityZoneV2 asserts they match EnvKit.SPEC.
PROP_SIZE = {
    "SandbagWall": (1.2, 3.0),
    "Container": (6.0, 2.45),
    "Wreck": (4.2, 1.9),
    "FenceSegment": (4.0, 0.3),
    "Chiller": (2.6, 2.6),
    "Transformer": (2.2, 1.7),
    "CoolingTower": (5.6, 5.6),
    "CableSpool": (2.0, 2.0),
}
# Footprint clearance a ramp keeps from sites (cm from the 800 x 800 footprint edge) and from rocks.
RAMP_SITE_CLEARANCE = {"anchor": 130.0, "post": 50.0, "deposit": 200.0, "hq": 700.0}
RAMP_ROCK_CLEARANCE = 150.0
# Footprint clearance a cover prop keeps from hold posts and capture anchors (formations stand around them) and
# from deposits, in cm from the prop's footprint edge.
PROP_SITE_CLEARANCE = {"anchor": 800.0, "post": 800.0, "deposit": 500.0}
MIN_COVER_PROPS = 6
MIN_HAZARD_PLATES = 8
Cell = tuple[int, int]
Point = Sequence[float]


def polygon_distance(point: Point, poly: Sequence[Point]) -> float:
    """Distance from a point to the boundary of a polygon."""
    best = math.inf
    for a, b in zip(poly, [*poly[1:], poly[0]], strict=True):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = dx * dx + dy * dy
        t = (
            0.0
            if length == 0
            else max(
                0.0,
                min(1.0, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length),
            )
        )
        best = min(best, math.hypot(point[0] - a[0] - t * dx, point[1] - a[1] - t * dy))
    return best


def contains(poly: Sequence[Point], point: Point) -> bool:
    """Boundary-inclusive point in an arbitrary simple polygon."""
    return polygon_distance(point, poly) < 1e-6 or ray_inside(poly, point)


def ray_inside(poly: Sequence[Point], point: Point) -> bool:
    """Half-open ray cast: a point on a shared edge belongs to exactly one side (single cell ownership)."""
    inside = False
    for a, b in zip(poly, [*poly[1:], poly[0]], strict=True):
        if (a[1] > point[1]) != (b[1] > point[1]) and point[0] < (b[0] - a[0]) * (
            point[1] - a[1]
        ) / (b[1] - a[1]) + a[0]:
            inside = not inside
    return inside


def clipped_area(
    poly: Sequence[Point], x0: float, x1: float, y0: float, y1: float
) -> float:
    """Area of a polygon inside an axis-aligned box (Sutherland-Hodgman; the polygon may be concave)."""
    points = [(p[0], p[1]) for p in poly]
    for axis, limit, keep_above in (
        (0, x0, True),
        (0, x1, False),
        (1, y0, True),
        (1, y1, False),
    ):
        out: list[tuple[float, float]] = []
        for a, b in zip(points, [*points[1:], points[0]], strict=True):
            a_in = a[axis] >= limit if keep_above else a[axis] <= limit
            b_in = b[axis] >= limit if keep_above else b[axis] <= limit
            if a_in != b_in:
                s = (limit - a[axis]) / (b[axis] - a[axis])
                out.append((a[0] + s * (b[0] - a[0]), a[1] + s * (b[1] - a[1])))
            if b_in:
                out.append(b)
        points = out
        if not points:
            return 0.0
    return (
        abs(
            sum(
                a[0] * b[1] - b[0] * a[1]
                for a, b in zip(points, [*points[1:], points[0]], strict=True)
            )
        )
        / 2
    )


def rect_distance(
    point: Point, centre: Point, yaw: float, size_x: float, size_y: float
) -> float:
    """Distance from a point to an oriented rectangle (0 inside)."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    dx, dy = point[0] - centre[0], point[1] - centre[1]
    lx, ly = dx * c + dy * s, -dx * s + dy * c
    return math.hypot(max(abs(lx) - size_x / 2, 0.0), max(abs(ly) - size_y / 2, 0.0))


def cell_of(x: float, y: float) -> Cell:
    return round(x / CELL), round(y / CELL)


def closed_borders(
    data: Mapping[str, Any], adjacent: Mapping[int, Collection[int]]
) -> set[tuple[int, int]]:
    """Pairs that are polygon neighbours but not walkable: the authored rock walls plus every plateau border that
    has no ramp (a cliff)."""
    spec = data["terrain"]
    ramps = {(p["region"], r["to"]) for p in spec["plateaus"] for r in p["ramps"]}
    plateaus = [r["index"] for r in data["regions"] if r.get("trait") == "high_ground"]
    closed = {(min(p), max(p)) for p in spec["closed_borders"]}
    return closed | {
        (min(h, n), max(h, n))
        for h in plateaus
        for n in adjacent[h]
        if (h, n) not in ramps
    }


def look_at(x: float, y: float) -> str:
    """Human half (x + y < 0) or Machine half of the map, like the v1 rim."""
    return "human" if x + y < 0 else "machine"


class Terrain:
    """Cells, plateaus, walls, ramps and props of one map; every query is pure."""

    def __init__(self, data: Mapping[str, Any]) -> None:
        self.data = data
        spec: Mapping[str, Any] = data["terrain"]
        self.height = float(spec["plateau_height"])
        self.regions: dict[int, Mapping[str, Any]] = {
            r["index"]: r for r in data["regions"]
        }
        self.trait = {i: r.get("trait") for i, r in self.regions.items()}
        self.plateau_regions = [
            i for i, t in sorted(self.trait.items()) if t == "high_ground"
        ]
        self.ramps: list[dict[str, Any]] = []
        for plateau in spec["plateaus"]:
            for ramp in plateau["ramps"]:
                self.ramps.append(
                    {
                        "plateau": plateau["region"],
                        "to": ramp["to"],
                        "centre": tuple(ramp["centre"]),
                        "yaw": int(ramp["yaw"]),
                    }
                )
        self.props: list[Mapping[str, Any]] = list(spec["props"])
        self.owner = self._owner()
        self.plateau_owner: dict[Cell, int] = {}
        for index in self.plateau_regions:
            for cell in self._touched_cells(self.regions[index]["poly"]):
                self.plateau_owner.setdefault(cell, index)
        self.plateau = set(self.plateau_owner)
        self.ramp_pairs = {(r["plateau"], r["to"]) for r in self.ramps}
        self.adjacent = self.polygon_adjacency()
        self.wall_borders = {tuple(sorted(p)) for p in spec["closed_borders"]}
        self.closed = closed_borders(data, self.adjacent)
        self.walls = self._walls()
        self.solid = self.plateau | self.walls
        self.bounds = {i: self._bounds(r["poly"]) for i, r in self.regions.items()}

    # ---- topology
    @staticmethod
    def _bounds(poly: Sequence[Point]) -> tuple[float, float, float, float]:
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        return min(xs), max(xs), min(ys), max(ys)

    @staticmethod
    def _touched_cells(poly: Sequence[Point]) -> Iterator[Cell]:
        """Cells whose box shares positive area with the polygon."""
        x0, x1, y0, y1 = Terrain._bounds(poly)
        for i in range(
            math.floor((x0 - CELL / 2) / CELL), math.ceil((x1 + CELL / 2) / CELL) + 1
        ):
            for j in range(
                math.floor((y0 - CELL / 2) / CELL),
                math.ceil((y1 + CELL / 2) / CELL) + 1,
            ):
                if (
                    clipped_area(
                        poly,
                        i * CELL - CELL / 2,
                        i * CELL + CELL / 2,
                        j * CELL - CELL / 2,
                        j * CELL + CELL / 2,
                    )
                    > 1.0
                ):
                    yield i, j

    def _owner(self) -> dict[Cell, int]:
        half = self.data["arena"]["half_extent"]
        reach = int(max(half) // CELL) + 1
        bounds = {i: self._bounds(r["poly"]) for i, r in self.regions.items()}
        owner: dict[Cell, int] = {}
        for i in range(-reach, reach + 1):
            for j in range(-reach, reach + 1):
                x, y = i * CELL, j * CELL
                found = -1
                if abs(x) < half[0] and abs(y) < half[1]:
                    for k, region in self.regions.items():
                        x0, x1, y0, y1 = bounds[k]
                        if (
                            x0 <= x <= x1
                            and y0 <= y <= y1
                            and ray_inside(region["poly"], (x, y))
                        ):
                            found = k
                            break
                owner[(i, j)] = found
        return owner

    def polygon_adjacency(self) -> dict[int, set[int]]:
        """Regions sharing a polygon edge (before any border is closed)."""
        users: dict[tuple[tuple[float, ...], ...], set[int]] = {}
        for index, region in self.regions.items():
            poly = region["poly"]
            for a, b in zip(poly, [*poly[1:], poly[0]], strict=True):
                users.setdefault(tuple(sorted((tuple(a), tuple(b)))), set()).add(index)
        adjacent: dict[int, set[int]] = {i: set() for i in self.regions}
        for owners in users.values():
            if len(owners) == 2:
                a, b = sorted(owners)
                adjacent[a].add(b)
                adjacent[b].add(a)
        return adjacent

    def neighbours(self) -> dict[int, list[int]]:
        """Polygon adjacency minus closed borders: the supply graph."""
        return {
            i: sorted(n for n in near if tuple(sorted((i, n))) not in self.closed)
            for i, near in self.adjacent.items()
        }

    def shared_edge_samples(self, a: int, b: int) -> list[tuple[float, float]]:
        """Points every <= 50 cm along the polygon edges regions a and b share."""
        edges = {
            tuple(sorted((tuple(p), tuple(q))))
            for p, q in itertools.pairwise(
                [*self.regions[a]["poly"], self.regions[a]["poly"][0]]
            )
        }
        other = {
            tuple(sorted((tuple(p), tuple(q))))
            for p, q in itertools.pairwise(
                [*self.regions[b]["poly"], self.regions[b]["poly"][0]]
            )
        }
        samples: list[tuple[float, float]] = []
        for p, q in sorted(edges & other):
            steps = max(1, math.ceil(math.dist(p, q) / 50))
            samples += [
                (p[0] + (q[0] - p[0]) * k / steps, p[1] + (q[1] - p[1]) * k / steps)
                for k in range(steps + 1)
            ]
        return samples

    def edge_cells(self, point: Point) -> set[Cell]:
        """The cell(s) holding a point; a point on a cell boundary counts for both sides."""
        return {
            cell_of(point[0] + dx, point[1] + dy) for dx in (-1, 1) for dy in (-1, 1)
        }

    def _walls(self) -> set[Cell]:
        """Rock-wall cells of the closed borders: every cell within one cell of the other region plus every cell the
        shared edge touches, so even a corner contact is covered. A border with an Open region is walled from the
        other side only, which keeps Open ground clear."""
        walls: set[Cell] = set()
        for a, b in self.wall_borders:
            sides = (
                {c for c, o in self.owner.items() if o == a},
                {c for c, o in self.owner.items() if o == b},
            )
            for (mine, other), own in zip((sides, sides[::-1]), (a, b), strict=True):
                if self.trait.get(own) == "open":
                    continue
                walls |= {
                    c
                    for c in mine
                    if any(
                        (c[0] + di, c[1] + dj) in other
                        for di in (-1, 0, 1)
                        for dj in (-1, 0, 1)
                    )
                }
            for point in self.shared_edge_samples(a, b):
                walls |= {
                    c
                    for c in self.edge_cells(point)
                    if self.trait.get(self.owner.get(c, -1)) != "open"
                }
        return walls

    def open_intrusions(self, region: int) -> list[str]:
        """Plateau, ramp and wall pieces standing in a region (the kinds an Open region must declare)."""
        kinds = []
        if any(o == region for c, o in self.owner.items() if c in self.plateau):
            kinds.append("plateau_edge")
        if any(r["to"] == region for r in self.ramps):
            kinds.append("ramp")
        if any(self.owner.get(c) == region for c in self.walls):
            kinds.append("wall")
        return kinds

    # ---- heights
    @staticmethod
    def ramp_frame(point: Point, ramp: Mapping[str, Any]) -> tuple[float, float]:
        """(along, across) of a point in the ramp's frame: along grows downhill from the footprint centre."""
        c, s = math.cos(math.radians(ramp["yaw"])), math.sin(math.radians(ramp["yaw"]))
        dx, dy = point[0] - ramp["centre"][0], point[1] - ramp["centre"][1]
        return dx * c + dy * s, -dx * s + dy * c

    def in_ramp(
        self, point: Point, ramp: Mapping[str, Any], half_strip: float = RAMP_HALF_STRIP
    ) -> float | None:
        """Distance run down the ramp (0 at the plateau edge .. RAMP_RUN), or None outside its walkable strip."""
        lx, ly = self.ramp_frame(point, ramp)
        if abs(ly) <= half_strip and abs(lx) <= RAMP_RUN / 2:
            return lx + RAMP_RUN / 2
        return None

    def ground_z(self, x: float, y: float) -> float:
        for ramp in self.ramps:
            run = self.in_ramp((x, y), ramp)
            if run is not None:
                return self.height * (1.0 - run / RAMP_RUN)
        return self.height if cell_of(x, y) in self.solid else 0.0

    def region_height(self, index: int) -> float:
        return self.height if index in self.plateau_regions else 0.0

    # ---- kit pieces
    def _low(self, cell: Cell, direction: tuple[int, int]) -> bool:
        """True where the neighbour in `direction` is ground (not plateau, wall or a ramp footprint)."""
        near = (cell[0] + direction[0], cell[1] + direction[1])
        if near in self.solid:
            return False
        x, y = near[0] * CELL, near[1] * CELL
        return not any(self.in_ramp((x, y), r, CELL) is not None for r in self.ramps)

    def solid_pieces(self) -> list[dict[str, Any]]:
        """Kit piece, yaw and centre for every plateau and wall cell, by the kit's own rim rule."""
        pieces = []
        for cell in sorted(self.solid):
            low = [yaw for yaw, d in DIRS.items() if self._low(cell, d)]
            piece, yaw = "Plateau_Fill", 0
            if len(low) == 1:
                piece, yaw = "Cliff_Straight", low[0]
            elif len(low) == 2 and (low[1] - low[0]) % 360 in (90, 270):
                first = low[0] if (low[1] - low[0]) % 360 == 90 else low[1]
                piece, yaw = "Cliff_CornerOuter", (first + 90) % 360
            x, y = cell[0] * CELL, cell[1] * CELL
            pieces.append(
                {
                    "piece": piece,
                    "look": look_at(x, y),
                    "yaw": yaw,
                    "centre": (x, y),
                    "cell": cell,
                    "wall": cell in self.walls,
                }
            )
        return pieces

    def ramp_pieces(self) -> list[dict[str, Any]]:
        return [
            {
                "piece": "Ramp_Wide",
                "look": look_at(*r["centre"]),
                "yaw": r["yaw"],
                "centre": r["centre"],
            }
            for r in self.ramps
        ]

    # ---- ground effects
    def hazard_plates(self) -> list[tuple[float, float]]:
        """Centres of the 360 cm hazard plates: a fixed 40% pattern of the hazard regions' interior cells."""
        plates = []
        for cell, o in sorted(self.owner.items()):
            if self.trait.get(o) != "hazard" or (cell[0] * 7 + cell[1] * 13) % 5 >= 2:
                continue
            x, y = cell[0] * CELL, cell[1] * CELL
            if all(
                contains(self.regions[o]["poly"], (x + dx, y + dy))
                for dx in (-200, 200)
                for dy in (-200, 200)
            ):
                plates.append((float(x), float(y)))
        return plates

    def apron_runs(self) -> list[tuple[int, int, int, int]]:
        """(i, j0, j1, region) rows of whole cells (corners inside) in Open regions, for the paved apron."""
        rows: dict[tuple[int, int], list[int]] = {}
        for (i, j), o in sorted(self.owner.items()):
            if self.trait.get(o) != "open":
                continue
            x, y = i * CELL, j * CELL
            if all(
                contains(self.regions[o]["poly"], (x + dx, y + dy))
                for dx in (-200, 200)
                for dy in (-200, 200)
            ):
                rows.setdefault((i, o), []).append(j)
        runs = []
        for (i, o), js in rows.items():
            start = prev = js[0]
            for nxt in [*js[1:], None]:
                if nxt is not None and nxt == prev + 1:
                    prev = nxt
                    continue
                runs.append((i, start, prev, o))
                if nxt is not None:
                    start = prev = nxt
        return runs

    # ---- placement predicates
    def prop_rects(self) -> list[tuple[float, float, float, float, float]]:
        return [
            (
                p["pos"][0],
                p["pos"][1],
                PROP_SIZE[p["kit"]][0] * 100,
                PROP_SIZE[p["kit"]][1] * 100,
                p["yaw"],
            )
            for p in self.props
        ]

    def clear_ground(self, point: Point, margin: float) -> bool:
        """Walkable, flat ground for a footprint of half extent `margin`: off rocks and props, wholly on one level,
        off ramps and rock walls."""
        for rock in self.data["blockers"]:
            if (
                contains(rock["poly"], point)
                or polygon_distance(point, rock["poly"]) < margin
            ):
                return False
        if not MatchLayout.clear_of_blockers(point, self.prop_rects(), margin):
            return False
        level = cell_of(*point) in self.plateau
        for dx in (-margin, margin):
            for dy in (-margin, margin):
                cell = cell_of(point[0] + dx, point[1] + dy)
                if (cell in self.plateau) != level or cell in self.walls:
                    return False
        return not any(
            rect_distance(point, r["centre"], r["yaw"], RAMP_RUN, RAMP_RUN) < margin
            for r in self.ramps
        )

    def prop_site_clash(self, centre: Point, yaw: float, kit: str) -> str | None:
        """The first anchor, post or deposit whose clearance the prop's footprint breaks, else None."""
        size_x, size_y = (v * 100 for v in PROP_SIZE[kit])
        sites = [("anchor", r["anchor"]) for r in self.data["regions"] if r["anchor"]]
        sites += [("post", p) for r in self.data["regions"] for p in r["defend_posts"]]
        sites += [("deposit", d["pos"]) for d in self.data["deposits"]]
        for kind, point in sites:
            if (
                rect_distance(point, centre, yaw, size_x, size_y)
                < PROP_SITE_CLEARANCE[kind]
            ):
                return f"{kind} at {point}"
        return None

    def sites(self) -> Iterator[tuple[str, Point, float]]:
        for region in self.data["regions"]:
            if region["anchor"]:
                yield "anchor", region["anchor"], RAMP_SITE_CLEARANCE["anchor"]
            for post in region["defend_posts"]:
                yield "post", post, RAMP_SITE_CLEARANCE["post"]
        for deposit in self.data["deposits"]:
            yield "deposit", deposit["pos"], RAMP_SITE_CLEARANCE["deposit"]
        for hq in self.data["headquarters"]:
            yield "hq", hq["pos"], RAMP_SITE_CLEARANCE["hq"]


def load(path: Path) -> dict[str, Any]:
    return dict(json.loads(path.read_text()))


def scatter_props(
    terrain: Terrain,
    theme: Mapping[int, tuple[Sequence[str], int]],
    seed: int,
    keep_out: Sequence[Sequence[Point]],
) -> list[dict[str, Any]]:
    """Seeded cover-prop records for the Cover regions of `theme` (region -> (kit cycle, count)). Props keep off
    sites, rocks, ramps, walls, region borders and `keep_out` polylines (the routes)."""
    rng = random.Random(seed)
    out: list[dict[str, Any]] = []
    for region, (kits, count) in theme.items():
        poly = terrain.regions[region]["poly"]
        x0, x1, y0, y1 = terrain.bounds[region]
        placed: list[dict[str, Any]] = []
        for _ in range(20000):
            if len(placed) >= count:
                break
            x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
            kit = kits[len(placed) % len(kits)]
            yaw = rng.choice([0, 20, 45, 70, 90, 135])
            if terrain.owner.get(cell_of(x, y)) != region or not contains(poly, (x, y)):
                continue
            corners = [
                terrain.owner.get(cell_of(x + dx, y + dy)) == region
                and cell_of(x + dx, y + dy) not in terrain.solid
                for dx in (-300, 300)
                for dy in (-300, 300)
            ]
            if not all(corners) or polygon_distance((x, y), poly) < 350:
                continue
            if any(math.dist((x, y), q["pos"]) < 800 for q in placed):
                continue
            if terrain.prop_site_clash((x, y), yaw, kit):
                continue
            if any(
                polygon_distance((x, y), r["poly"]) < 350 or contains(r["poly"], (x, y))
                for r in terrain.data["blockers"]
            ):
                continue
            if any(
                rect_distance((x, y), r["centre"], r["yaw"], RAMP_RUN, RAMP_RUN) < 500
                for r in terrain.ramps
            ):
                continue
            if any(_near_polyline((x, y), line, 550) for line in keep_out):
                continue
            placed.append(
                {
                    "region": region,
                    "kit": kit,
                    "pos": [round(x), round(y)],
                    "yaw": yaw,
                }
            )
        out += placed
    return out


def _near_polyline(point: Point, line: Sequence[Point], limit: float) -> bool:
    return any(
        polygon_distance(point, [a, b]) < limit for a, b in itertools.pairwise(line)
    )


def trait_counts(terrain: Terrain) -> Counter[str]:
    return Counter(t for t in terrain.trait.values() if t)
