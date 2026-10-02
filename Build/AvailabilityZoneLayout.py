"""Availability Zone Phase A geometry, from Build/Maps/AvailabilityZone.json, with no Unreal dependency.

Imported by Build/GenerateAvailabilityZone.py (spawns what this computes), Build/ImportTerrainKit.py (the kit
orientation constant) and Saved/Verification/availability-zone/*.py (expected values); also runnable on its own as a
smoke test of the whole plan without starting Unreal:

    python3 Build/AvailabilityZoneLayout.py            # build the plan, run every check, print the counts
    python3 Build/AvailabilityZoneLayout.py --svg /tmp/az-plan.svg

Everything is centimetres, degrees, Unreal world axes (+X minimap-up, +Y minimap-right, yaw turns +X toward +Y).

Phase A (docs/Maps/AvailabilityZone-implementation.md, "Phase A (flat greybox)"): every playable level is z = 0.
* The playable ground is the JSON regions plus the ramp footprints (open floor). It is rasterised onto the terrain
  kit's 400 cm grid (cell (i, j) covers 400 i +- 200, 400 j +- 200); a cell is playable ("P", with its level), ramp
  floor ("R"), the Slab ("S") or rim ("X").
* A level boundary between two playable cells of different levels is a plain collision wall, 100 cm thick and 300 cm
  tall, centred on the boundary line, one box per straight run, with a gap wherever a ramp floor cell touches it. The
  ramps are those gaps, closed at the sides by 50 cm parapets (65 cm) along the footprint edges: the walkable strip is
  the 700 cm the design measured, the gate's two lanes are split by a 100 cm parapet. The walls are thin so no
  distance of the design changes (the design's clearance ring already covers them).
* The rim (everything else) is kit rock: the ring of cells next to playable ground is kit Cliff_Straight /
  Cliff_CornerOuter / Plateau_Fill standing at z = 0 (300 cm) with the face toward the playable cell, chosen by the
  kit's own contract (docstring of Build/GenerateTerrainKit.py); cells further in are merged into 600 cm boxes. The
  kit's Cliff_CornerInner fillet would sit inside the playable cell and eat walkable ground, so concave corners are
  left sharp (Plateau_Fill in the corner cell), which keeps the design's distances. The kit's own ramp pieces are
  not placed: their deck rises 300 cm, which a flat map cannot land on (Phase B, proposal P1).
* Blocking dressing is checked against a keep-out set built from the same JSON (HQ discs, sector territory discs, bays
  with their exit ring, ramp footprints, and a corridor along every route and every base-to-sector walk); anything
  that would collide with the design's play space is rejected, never moved.
"""

from __future__ import annotations

import ast
from collections.abc import Iterable, Iterator, Mapping, Sequence
from itertools import pairwise
import json
import math
import os
import sys
from typing import ClassVar, NotRequired, TypedDict, TypeVar, cast

import DrawMapLayout
from DrawMapLayout import Cell, MapData, Point, Polygon, WorldPoint
import MatchLayout

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

MAP_JSON = os.path.join(ROOT, "Build", "Maps", "AvailabilityZone.json")

CELL = 400
WALL_THICKNESS = 100.0
WALL_HEIGHT = 300.0
WALL_EXTENSION = 50.0  # each run grows by half a wall thickness so perpendicular walls close their corner
RIM_EDGE_HEIGHT = 300.0  # one kit cliff step
RIM_BLOCK_HEIGHT = 600.0
PARAPET_THICKNESS = 50.0
PARAPET_HEIGHT = 65.0  # the kit's parapet is 55 cm plus a 10 cm cap
RAMP_RUN = 800.0
RAMP_FOOTPRINT = 800.0
ROUTE_CORRIDOR_HALF = 300.0  # design doc: "a 600 cm corridor along each route"
BAY_EXIT_RING = 365.0
FOOTPRINT_MARGIN = 300.0  # keep-out margin around every ramp footprint
PLAYER_START = (-8000.0, -6000.0)
PLAYER_START_KEEP_OUT = 150.0

# Indexed by the generated Voronoi gameplay regions (two mains, then site_index
# order), not by the JSON's eight elevation/terrain polygons.
GAMEPLAY_DEFEND_POSTS: list[list[list[float]]] = [
    [[-7200, -3800], [-7000, -5600]],
    [[7200, 5400], [7000, 3800]],
    [[-3000, -5800], [-4800, -6200]],
    [[-3800, -1200], [-4200, -3000]],
    [[-4800, 1000], [-7000, 800]],
    [[-600, -4200], [1800, -4000]],
    [[-600, 3000], [0, 5400]],
    [[6800, -800], [4600, -1200]],
    [[4000, 2800], [4200, 600]],
    [[3000, 5800], [5000, 6000]],
]

# Where the kit's asymmetric pieces look at yaw 0 in Unreal (Blender +Y is Unreal -Y): a CornerOuter's two low
# neighbours at yaw 0. ImportTerrainKit measures the imported mesh against this.
CORNER_OUTER_LOW_AT_YAW0 = ((1, 0), (0, -1))

DIRS = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}

Rect = tuple[float, float, float, float, float]
KitSizes = Mapping[str, tuple[float, float, float, float, float]]
PlacedProp = tuple[float, float, str, float, str, float]
T = TypeVar("T")


class BoxData(TypedDict):
    label: str
    center: WorldPoint
    size: WorldPoint
    look: str
    height: NotRequired[float]
    axis: NotRequired[str]
    levels: NotRequired[list[str]]
    high: NotRequired[Cell]
    ramp: NotRequired[str]
    yaw: NotRequired[float]


class WallRun(TypedDict):
    lo: int
    hi: int
    levels: set[str]


class RimPieceData(TypedDict):
    piece: str
    look: str
    center: WorldPoint
    yaw: int
    cell: Cell
    low: list[int]


class PropData(TypedDict):
    kit: str
    label: str
    center: WorldPoint
    yaw: float
    round: bool
    size: WorldPoint
    look: str
    region: NotRequired[str]
    base: NotRequired[float]


class HallData(TypedDict):
    label: str
    center: WorldPoint
    size: WorldPoint
    doors: dict[str, int]
    base: NotRequired[float]
    look: NotRequired[str]
    towers: NotRequired[list[WorldPoint]]


class TowerData(TypedDict):
    label: str
    center: WorldPoint
    look: str


class LampData(TowerData):
    base: float


def load(path: str | os.PathLike[str] | None = None) -> MapData:
    with open(path or MAP_JSON) as handle:
        return cast(MapData, json.load(handle))


def gameplay_regions(data: MapData | None = None) -> list[MatchLayout.GameplayRegion]:
    """The generator's complete arena tiling, with editable authored defend posts."""
    data = data or load()
    return MatchLayout.region_plan(
        data["match_actors"]["arena"]["half_extent"],
        [
            (hq["label"], hq["pos"], hq["team_index"])
            for hq in data["match_actors"]["headquarters"]
        ],
        [
            (sector["name"], sector["pos"])
            for sector in sorted(
                data["sectors"], key=lambda sector: sector["site_index"]
            )
        ],
        GAMEPLAY_DEFEND_POSTS,
    )


# ----------------------------------------------------------------------------- small geometry
def look_at(x: float, y: float) -> str:
    """Human kit on the south-west half of the diagonal, Machine on the north-east half (design: themes per area)."""
    return "human" if x + y < 0 else "machine"


def rot(vx: float, vy: float, yaw: float) -> WorldPoint:
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return vx * c - vy * s, vx * s + vy * c


def dist_point_rect(px: float, py: float, rect: Rect) -> float:
    """Distance from a point to an oriented rectangle (cx, cy, sx, sy, yaw); 0 inside."""
    cx, cy, sx, sy, yaw = rect
    dx, dy = px - cx, py - cy
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    lx, ly = dx * c + dy * s, -dx * s + dy * c
    return math.hypot(max(abs(lx) - sx / 2, 0.0), max(abs(ly) - sy / 2, 0.0))


def rects_close(a: Rect, b: Rect, margin: float = 0.0) -> bool:
    """True when two oriented rectangles (cx, cy, sx, sy, yaw) overlap or lie within `margin` (separating axes)."""
    for rect in (a, b):
        c, s = math.cos(math.radians(rect[4])), math.sin(math.radians(rect[4]))
        for ax, ay in ((c, s), (-s, c)):

            def radius(r: Rect, ax: float = ax, ay: float = ay) -> float:
                rc, rs = math.cos(math.radians(r[4])), math.sin(math.radians(r[4]))
                return (r[2] / 2) * abs(ax * rc + ay * rs) + (r[3] / 2) * abs(
                    -ax * rs + ay * rc
                )

            if (
                abs((b[0] - a[0]) * ax + (b[1] - a[1]) * ay)
                > radius(a) + radius(b) + margin
            ):
                return False
    return True


def densify(points: Sequence[Point], step: float) -> list[WorldPoint]:
    out = [cast(WorldPoint, tuple(points[0]))]
    for (ax, ay), (bx, by) in pairwise(points):
        n = max(1, int(math.hypot(bx - ax, by - ay) // step))
        out.extend(
            (ax + (bx - ax) * k / n, ay + (by - ay) * k / n) for k in range(1, n + 1)
        )
    return out


class Rng:
    """Tiny deterministic generator so the same JSON always gives the same dressing."""

    def __init__(self, seed: int) -> None:
        self.state = (seed * 2654435761 + 1013904223) & 0xFFFFFFFF

    def next(self) -> float:
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.state >> 8) / float(1 << 24)

    def uniform(self, lo: float, hi: float) -> float:
        return lo + (hi - lo) * self.next()

    def pick(self, items: Sequence[T]) -> T:
        return items[int(self.next() * len(items)) % len(items)]


# ----------------------------------------------------------------------------- the layout
class Layout:
    def __init__(
        self, data: MapData | None = None, kit_sizes: KitSizes | None = None
    ) -> None:
        """`kit_sizes` maps an Environment-kit piece name to (footprint X, footprint Y, height, blocking X, blocking Y)
        in cm (Build/EnvKit.py: piece_size + collision_size); without it no dressing is planned."""
        self.data = data or load()
        d = self.data
        self.const = d["constants"]
        self.hx, self.hy = (float(v) for v in d["arena"]["half_extent"])
        self.kit_sizes = kit_sizes or {}
        self.analysis = DrawMapLayout.Analysis(d)
        self.analysis_ok = self.analysis.run()
        self.hq = {h["id"]: h for h in d["headquarters"]}
        self.regions = {r["id"]: r for r in d["regions"]}
        self.ramps = {r["id"]: r for r in d["ramps"]}
        self.sectors = d["sectors"]
        self.issues: list[
            str
        ] = []  # design or plan problems found while building (empty = plan is sound)
        self._classify()
        self._walls()
        self._parapets()
        self._rim()
        self._keep_out()
        self.props: list[PropData] = []  # in-play blocking dressing
        self.halls: list[HallData] = []  # assembled data halls (rim or Slab)
        self.rim_props: list[
            PropData
        ] = []  # backdrop dressing standing on the rim blocks
        self.slab: HallData | None = None
        self._towers()
        self._lamps()
        if self.kit_sizes:
            self._dress()
        self._check()
        self.check_seal()
        self.issues.extend(self.defend_post_errors())

    # ------------------------------------------------------------------ grid
    def cell_of(self, x: float, y: float) -> Cell:
        return round(x / CELL), round(y / CELL)

    @staticmethod
    def centre_of(cell: Cell) -> WorldPoint:
        return cell[0] * CELL, cell[1] * CELL

    def _classify(self) -> None:
        d = self.data
        extent = math.ceil((max(self.hx, self.hy) - CELL / 2) / CELL)
        self.extent = extent
        self.kind: dict[Cell, str] = {}
        self.level: dict[Cell, str] = {}
        self.region_of: dict[Cell, str] = {}
        self.ramp_of: dict[Cell, str] = {}
        polys = [(r["id"], r["level"], r["poly"]) for r in d["regions"]]
        for i in range(-extent, extent + 1):
            for j in range(-extent, extent + 1):
                x, y = i * CELL, j * CELL
                kind = "X"
                for rid, level, poly in polys:
                    if DrawMapLayout.inside(x, y, poly):
                        kind = "P"
                        self.level[(i, j)] = level
                        self.region_of[(i, j)] = rid
                        break
                for blocker in d["blockers"]:
                    if DrawMapLayout.inside(x, y, blocker["poly"]):
                        kind = "S"
                for ramp in d["ramps"]:
                    if any(DrawMapLayout.inside(x, y, fp) for fp in ramp["footprints"]):
                        kind = "R"
                        self.ramp_of[(i, j)] = ramp["id"]
                self.kind[(i, j)] = kind

    def kind_at(self, cell: Cell) -> str:
        return self.kind.get(cell, "X")

    def walkable(self, cell: Cell) -> bool:
        return self.kind_at(cell) in ("P", "R")

    # ------------------------------------------------------------------ walls between levels
    def _walls(self) -> None:
        edges: dict[tuple[str, int, str, Cell], list[tuple[int, int, str, str]]] = {}
        mid: WorldPoint
        for (i, j), kind in self.kind.items():
            if kind != "P":
                continue
            for di, dj, axis in ((1, 0, "v"), (0, 1, "h")):
                other = (i + di, j + dj)
                if (
                    self.kind_at(other) == "P"
                    and self.level[other] != self.level[(i, j)]
                ):
                    if axis == "v":
                        line, lo = i * CELL + CELL // 2, j * CELL - CELL // 2
                        mid = (line, lo + CELL / 2)
                    else:
                        line, lo = j * CELL + CELL // 2, i * CELL - CELL // 2
                        mid = (lo + CELL / 2, line)
                    la, lb = self.level[(i, j)], self.level[other]
                    high = (
                        (-di, -dj) if la > lb else (di, dj)
                    )  # level ids sort L0 < L1 < L2
                    edges.setdefault((axis, line, look_at(*mid), high), []).append(
                        (lo, lo + CELL, la, lb)
                    )
        self.walls: list[BoxData] = []
        for (axis, line, look, high), spans in sorted(edges.items()):
            spans.sort()
            runs: list[WallRun] = []
            for lo, hi, la, lb in spans:
                if runs and runs[-1]["hi"] == lo:
                    runs[-1]["hi"] = hi
                    runs[-1]["levels"].add(la)
                    runs[-1]["levels"].add(lb)
                else:
                    runs.append({"lo": lo, "hi": hi, "levels": {la, lb}})
            for run in runs:
                lo, hi, levels = run["lo"], run["hi"], run["levels"]
                length = hi - lo + 2 * WALL_EXTENSION
                midpoint = (lo + hi) / 2
                cx, cy = (line, midpoint) if axis == "v" else (midpoint, line)
                size = (
                    (WALL_THICKNESS, length)
                    if axis == "v"
                    else (length, WALL_THICKNESS)
                )
                self.walls.append(
                    {
                        "label": f"Wall{len(self.walls):02d}",
                        "center": (cx, cy),
                        "size": size,
                        "height": WALL_HEIGHT,
                        "look": look,
                        "axis": axis,
                        "levels": sorted(levels),
                        "high": high,
                    }
                )

    def _parapets(self) -> None:
        """A parapet along each side of every ramp piece (the shared side of a two-lane gate gets two = 100 cm)."""
        self.parapets: list[BoxData] = []
        self.decks: list[BoxData] = []
        for ramp in self.data["ramps"]:
            for index, piece in enumerate(ramp["pieces"]):
                cx, cy = piece["centre"]
                yaw = piece["yaw"] % 360
                require(
                    yaw % 90 == 0,
                    f"{ramp['id']!s} piece {index:d} yaw {yaw!s} is not a multiple of 90",
                )
                offset = RAMP_FOOTPRINT / 2 - PARAPET_THICKNESS / 2
                size = (
                    (RAMP_RUN, PARAPET_THICKNESS)
                    if yaw % 180 == 0
                    else (PARAPET_THICKNESS, RAMP_RUN)
                )
                look = "machine" if ramp["kit_piece"].endswith("_Machine") else "human"
                for side in (-1, 1):
                    ox, oy = rot(0.0, side * offset, yaw)
                    self.parapets.append(
                        {
                            "label": f"Parapet_{ramp['id']!s}_{index:d}_{'L' if side < 0 else 'R'}",
                            "center": (cx + ox, cy + oy),
                            "size": size,
                            "height": PARAPET_HEIGHT,
                            "look": look,
                            "ramp": ramp["id"],
                        }
                    )
                lane = (
                    (RAMP_RUN, ramp["lane_width"])
                    if yaw % 180 == 0
                    else (ramp["lane_width"], RAMP_RUN)
                )
                self.decks.append(
                    {
                        "label": f"Deck_{ramp['id']!s}_{index:d}",
                        "center": (cx, cy),
                        "size": lane,
                        "yaw": yaw,
                        "look": look,
                        "ramp": ramp["id"],
                    }
                )

    # ------------------------------------------------------------------ rim
    def _rim(self) -> None:
        self.rim_pieces: list[RimPieceData] = []
        interior: set[Cell] = set()
        self.rim_counts = {
            "Cliff_Straight": 0,
            "Cliff_CornerOuter": 0,
            "Plateau_Fill": 0,
        }
        for cell, kind in self.kind.items():
            if kind != "X":
                continue
            low = [
                a
                for a, (di, dj) in DIRS.items()
                if self.walkable((cell[0] + di, cell[1] + dj))
            ]
            diagonal = any(
                self.walkable((cell[0] + di, cell[1] + dj))
                for di in (-1, 0, 1)
                for dj in (-1, 0, 1)
            )
            if not low and not diagonal:
                interior.add(cell)
                continue
            x, y = self.centre_of(cell)
            look = look_at(x, y)
            if len(low) == 1:
                piece, yaw = "Cliff_Straight", low[0]
            elif (
                len(low) == 2
                and (low[1] - low[0]) % 360 in (90, 270)
                and self._same_level(cell, low)
            ):
                first = low[0] if (low[1] - low[0]) % 360 == 90 else low[1]
                piece, yaw = "Cliff_CornerOuter", (first + 90) % 360
            else:
                piece, yaw = "Plateau_Fill", 0
            self.rim_counts[piece] += 1
            self.rim_pieces.append(
                {
                    "piece": piece,
                    "look": look,
                    "center": (x, y),
                    "yaw": yaw,
                    "cell": cell,
                    "low": low,
                }
            )
        self.interior = interior
        # Merge the interior cells into rectangles: runs along j, then identical runs of neighbouring i.
        runs: dict[tuple[int, int], list[int]] = {}
        for i in sorted({c[0] for c in interior}):
            js = sorted(c[1] for c in interior if c[0] == i)
            start = prev = js[0]
            for next_j in [*js[1:], None]:
                if next_j is not None and next_j == prev + 1:
                    prev = next_j
                    continue
                runs.setdefault((start, prev), []).append(i)
                if next_j is not None:
                    start = prev = next_j
        self.rim_blocks: list[BoxData] = []
        for (j0, j1), is_ in sorted(runs.items()):
            is_.sort()
            group = [is_[0]]
            for next_i in [*is_[1:], None]:
                if next_i is not None and next_i == group[-1] + 1:
                    group.append(next_i)
                    continue
                cx = (group[0] + group[-1]) / 2 * CELL
                cy = (j0 + j1) / 2 * CELL
                size = ((group[-1] - group[0] + 1) * CELL, (j1 - j0 + 1) * CELL)
                self.rim_blocks.append(
                    {
                        "label": f"Rim{len(self.rim_blocks):03d}",
                        "center": (cx, cy),
                        "size": size,
                        "height": RIM_BLOCK_HEIGHT,
                        "look": look_at(cx, cy),
                    }
                )
                if next_i is not None:
                    group = [next_i]

    def _same_level(self, cell: Cell, directions: Iterable[int]) -> bool:
        """The rounded corner of Cliff_CornerOuter opens its cut-away corner onto both neighbours; between two
        different levels that would be a gap around the end of a level wall, so those corners stay full cells."""
        levels = {
            self.level.get((cell[0] + DIRS[a][0], cell[1] + DIRS[a][1])) or "ramp"
            for a in directions
        }
        return len(levels) == 1

    def slab_rects(self) -> list[Rect]:
        return [self.rect_of_poly(b["poly"]) for b in self.data["blockers"]]

    @staticmethod
    def rect_of_poly(poly: Polygon) -> Rect:
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        return (
            (min(xs) + max(xs)) / 2,
            (min(ys) + max(ys)) / 2,
            max(xs) - min(xs),
            max(ys) - min(ys),
            0.0,
        )

    # ------------------------------------------------------------------ keep-out (the footprint_clear idea)
    def _keep_out(self) -> None:
        c = self.const
        self.keep_circles: list[tuple[str, Point, float]] = []
        for hq in self.data["headquarters"]:
            self.keep_circles.append(
                ("hq " + hq["id"], tuple(hq["pos"]), float(c["hq_exclusion_radius"]))
            )
        for s in self.sectors:
            self.keep_circles.append(
                (
                    f"sector S{s['site_index'] + 1:d}",
                    tuple(s["pos"]),
                    float(c["sector_territory_radius"]),
                )
            )
        for pocket in self.data["build_pockets"]:
            for bay in pocket["bays"]:
                self.keep_circles.append(
                    ("bay " + bay["id"], tuple(bay["pos"]), BAY_EXIT_RING)
                )
        self.keep_circles.append(("player start", PLAYER_START, PLAYER_START_KEEP_OUT))
        self.keep_rects: list[tuple[str, Rect]] = []
        for ramp in self.data["ramps"]:
            for fp in ramp["footprints"]:
                r = self.rect_of_poly(fp)
                self.keep_rects.append(
                    (
                        "ramp " + ramp["id"],
                        (
                            r[0],
                            r[1],
                            r[2] + 2 * FOOTPRINT_MARGIN,
                            r[3] + 2 * FOOTPRINT_MARGIN,
                            0.0,
                        ),
                    )
                )
        self.corridors: list[tuple[str, list[WorldPoint]]] = []
        an = self.analysis
        for label in ("open", "no_door", "no_gate"):
            route = an.routes.get(label)
            if route:
                self.corridors.append(
                    ("route " + label, densify(route[1], ROUTE_CORRIDOR_HALF / 4))
                )
        for key in ("HQ_H", "HQ_J"):
            for name, (_length, pts) in an.table.get(key, {}).items():
                if name.startswith("S"):
                    self.corridors.append(
                        (
                            f"walk {key!s}>{name!s}",
                            densify(pts, ROUTE_CORRIDOR_HALF / 4),
                        )
                    )
        self.corridor_points = [p for _label, pts in self.corridors for p in pts]

    def keep_out_reason(self, rect: Rect, exempt: Sequence[str] = ()) -> str | None:
        """None when the blocking footprint (cx, cy, sx, sy, yaw) is clear of every gameplay keep-out, else why not."""
        for label, centre, radius in self.keep_circles:
            if label.split()[0] in exempt:
                continue
            if dist_point_rect(centre[0], centre[1], rect) < radius:
                return "keep-out " + label
        for label, keep in self.keep_rects:
            if label.split()[0] in exempt:
                continue
            if rects_close(rect, keep, 0.0):
                return "keep-out " + label
        if "route" not in exempt:
            for px, py in self.corridor_points:
                if (
                    abs(px - rect[0]) < 1500
                    and abs(py - rect[1]) < 1500
                    and dist_point_rect(px, py, rect) < ROUTE_CORRIDOR_HALF
                ):
                    return "keep-out corridor"
        if (
            abs(rect[0]) + max(rect[2], rect[3]) / 2 > self.hx
            or abs(rect[1]) + max(rect[2], rect[3]) / 2 > self.hy
        ):
            return "outside the arena"
        return None

    def obstacle_distance(self, x: float, y: float) -> float:
        """Distance from a point to the nearest wall, parapet, rim cell or Slab (cm)."""
        best = 1e9
        ci, cj = self.cell_of(x, y)
        for i in range(ci - 4, ci + 5):
            for j in range(cj - 4, cj + 5):
                if self.kind_at((i, j)) in ("X", "S"):
                    best = min(
                        best,
                        dist_point_rect(x, y, (i * CELL, j * CELL, CELL, CELL, 0.0)),
                    )
        for box in self.walls + self.parapets:
            if abs(box["center"][0] - x) < 4000 and abs(box["center"][1] - y) < 4000:
                best = min(
                    best,
                    dist_point_rect(
                        x,
                        y,
                        (
                            box["center"][0],
                            box["center"][1],
                            box["size"][0],
                            box["size"][1],
                            0.0,
                        ),
                    ),
                )
        return best

    def deposit_clear(self, point: Point, clearance: float) -> bool:
        """Clear navigable ground for a deposit/extractor, including all planned blocking dressing."""
        x, y = point
        if (
            not self.walkable(self.cell_of(x, y))
            or self.obstacle_distance(x, y) < clearance
        ):
            return False
        for prop in self.props + self.rim_props:
            rect = (
                prop["center"][0],
                prop["center"][1],
                prop["size"][0],
                prop["size"][1],
                prop["yaw"],
            )
            if dist_point_rect(x, y, rect) < clearance:
                return False
        for tower in self.towers:
            if (
                dist_point_rect(
                    x, y, (tower["center"][0], tower["center"][1], 400, 400, 0)
                )
                < clearance
            ):
                return False
        for hall in self.halls:
            if (
                dist_point_rect(
                    x,
                    y,
                    (
                        hall["center"][0],
                        hall["center"][1],
                        hall["size"][0],
                        hall["size"][1],
                        0,
                    ),
                )
                < clearance
            ):
                return False
        return True

    def defend_post_errors(
        self, regions: list[MatchLayout.GameplayRegion] | None = None
    ) -> list[str]:
        """Coverage uses gameplay regions and all finished blocking dressing."""
        return MatchLayout.defend_post_errors(
            gameplay_regions(self.data) if regions is None else regions,
            self.data["match_actors"]["arena"]["half_extent"],
            self.deposit_clear,
            self.data["match_actors"]["arena"]["placement_margin"],
        )

    # ------------------------------------------------------------------ landmarks and light masts
    def _towers(self) -> None:
        """The design's four watchtowers (proposals.vision_points) stand where the JSON puts them, as landmarks. They
        may sit on a route corridor (the pad is walkable, the column is 180 cm across); every other keep-out holds."""
        self.towers: list[TowerData] = []
        for point in self.data["proposals"].get("vision_points", []):
            x, y = point["pos"]
            rect = (x, y, 400.0, 400.0, 0.0)
            reason = self.keep_out_reason(rect, exempt=("route",))
            if reason:
                self.issues.append(f"tower {point['id']!s}: {reason!s}")
            self.towers.append(
                {"label": point["id"], "center": (x, y), "look": look_at(x, y)}
            )

    LAMP_SPACING = 1500.0

    def _lamps(self) -> None:
        """Wall lamps: a small glow box and a point light on the top of every level wall (amber on the human half,
        cyan on the Machine half), plus one on each ramp parapet head at the top of the ramp. They sit on top of
        geometry that already blocks, collide with nothing and stand in no keep-out."""
        self.lamps: list[LampData] = []
        for wall in self.walls:
            cx, cy = wall["center"]
            length = wall["size"][1] if wall["axis"] == "v" else wall["size"][0]
            span = length - 2 * WALL_EXTENSION
            count = max(1, int(span // self.LAMP_SPACING))
            for k in range(count):
                along = -span / 2 + (k + 0.5) * span / count
                px, py = (cx, cy + along) if wall["axis"] == "v" else (cx + along, cy)
                self.lamps.append(
                    {
                        "label": f"Lamp{len(self.lamps):02d}",
                        "center": (px, py),
                        "base": WALL_HEIGHT,
                        "look": look_at(px, py),
                    }
                )
        for ramp in self.data["ramps"]:
            for piece in ramp["pieces"]:
                yaw = piece["yaw"] % 360
                for side in (-1, 1):
                    # parapet head at the top end of the ramp: 400 cm back from the piece centre against the fall line
                    ox, oy = rot(
                        -(RAMP_RUN / 2 - 60.0),
                        side * (RAMP_FOOTPRINT / 2 - PARAPET_THICKNESS / 2),
                        yaw,
                    )
                    px, py = piece["centre"][0] + ox, piece["centre"][1] + oy
                    if any(
                        math.hypot(px - lamp["center"][0], py - lamp["center"][1])
                        < 100.0
                        for lamp in self.lamps
                    ):
                        continue  # the shared parapet of a two-lane gate
                    self.lamps.append(
                        {
                            "label": f"Lamp{len(self.lamps):02d}",
                            "center": (px, py),
                            "base": PARAPET_HEIGHT,
                            "look": look_at(px, py),
                        }
                    )

    # ------------------------------------------------------------------ dressing
    ROUND = ("CoolingTower", "Chiller", "BurnBarrel", "CableSpool")
    HUMAN_KIT = (
        "Container",
        "Wreck",
        "SandbagWall",
        "BurnBarrel",
        "GeneratorShack",
        "CableSpool",
    )
    # Per decoration entry: how many in-play pieces to try (the sampler stops when it runs out of clear ground).
    PLAY_DENSITY: ClassVar[dict[str, int]] = {
        "main_H": 10,
        "terrace_H": 14,
        "pocket_SE": 14,
        "pass_W": 10,
        "main_J": 8,
        "terrace_J": 14,
        "pocket_NW": 14,
        "pass_E": 10,
    }

    def _prop_rect(
        self, name: str, centre: Point, yaw: float, scale: float = 1.0
    ) -> Rect:
        _fx, _fy, _h, bx, by = self.kit_sizes[name]
        return (centre[0], centre[1], bx * scale, by * scale, yaw)

    def _dress(self) -> None:
        d = self.data
        placed: list[PlacedProp] = []
        deco = {e["region"]: e for e in d["decoration"]}
        # In-play scatter: only pieces the Environment kit has, never a hall (halls are rim / Slab dressing).
        for rid, region in self.regions.items():
            entry = next((e for e in d["decoration"] if e["region"] == rid), None)
            if entry is None and rid.startswith("pass"):
                entry = deco["pass_W"]
            kits = [
                k
                for k in (entry["kit"] if entry else [])
                if k in self.kit_sizes
                and not k.startswith("DataHall")
                and k not in ("ClusterPylon", "CommsMast")
            ]
            if not kits:
                continue
            rng = Rng(sum(ord(ch) for ch in rid) * 977)
            xs, ys = [p[0] for p in region["poly"]], [p[1] for p in region["poly"]]
            want = self.PLAY_DENSITY.get(rid, 8)
            count = tries = 0
            while count < want and tries < want * 400:
                tries += 1
                x, y = rng.uniform(min(xs), max(xs)), rng.uniform(min(ys), max(ys))
                if not DrawMapLayout.inside(x, y, region["poly"]):
                    continue
                name = kits[count % len(kits)]
                yaw = rng.pick((0.0, 90.0, 180.0, 270.0)) + rng.uniform(-12, 12)
                rect = self._prop_rect(name, (x, y), yaw)
                radius = math.hypot(rect[2], rect[3]) / 2
                if self.obstacle_distance(x, y) < radius + 350:
                    continue
                if self.keep_out_reason(rect):
                    continue
                if any(
                    math.hypot(x - o[0], y - o[1]) < radius + o[5] + 450 for o in placed
                ):
                    continue
                placed.append((x, y, name, yaw, rid, radius))
                self.props.append(
                    {
                        "kit": name,
                        "label": f"{rid!s}_{name!s}{count:d}",
                        "center": (x, y),
                        "yaw": yaw,
                        "round": name in self.ROUND,
                        "size": rect[2:4],
                        "region": rid,
                        "look": look_at(x, y),
                    }
                )
                count += 1
        self._dress_rim(placed)
        self._dress_slab()

    def _dress_rim(self, placed: list[PlacedProp]) -> None:
        """Backdrop massing on the 600 cm rim blocks: Machine campus (halls, towers) north-east, forward base
        (containers, wrecks, sandbags, shacks) south-west. Out of play, so no keep-out applies; the halls must sit
        wholly on interior rim cells."""
        sizes = self.kit_sizes
        halls_wanted = {
            "machine": [
                (1200, 800),
                (1000, 1000),
                (1600, 800),
                (1200, 1000),
                (1400, 800),
                (1000, 800),
            ],
            "human": [],
        }
        rng = Rng(4242)
        candidates = sorted(self.interior, key=lambda c: (round(rng.next() * 1000), c))
        self._rim_taken: list[Rect] = []

        def free(rect: Rect, margin: float = 200.0) -> bool:
            if not self._rim_footprint_ok(rect):
                return False
            return not any(rects_close(rect, o, margin) for o in self._rim_taken)

        for look, halls in halls_wanted.items():
            for size in halls:
                for cell in candidates:
                    x, y = self.centre_of(cell)
                    if look_at(x, y) != look or self._distance_to_play(x, y) > 2600:
                        continue
                    rect: Rect = (x, y, size[0], size[1], 0.0)
                    if not free(rect, 300.0):
                        continue
                    doors = {"N": 1} if rng.next() < 0.5 else {"S": 1}
                    self._rim_taken.append(rect)
                    self.halls.append(
                        {
                            "label": f"RimHall{len(self.halls):d}",
                            "center": (x, y),
                            "size": size,
                            "doors": doors,
                            "base": RIM_BLOCK_HEIGHT,
                            "look": look,
                        }
                    )
                    break
        wanted = {
            "machine": ["CoolingTower"] * 10
            + ["Chiller"] * 8
            + ["Transformer"] * 8
            + ["Pylon"] * 4,
            "human": ["Container"] * 12
            + ["Wreck"] * 6
            + ["GeneratorShack"] * 4
            + ["SandbagWall"] * 6
            + ["BurnBarrel"] * 6
            + ["CableSpool"] * 4,
        }
        for look, names in wanted.items():
            for index, name in enumerate(names):
                if name not in sizes:
                    continue
                for cell in candidates:
                    x, y = self.centre_of(cell)
                    x += rng.uniform(-120, 120)
                    y += rng.uniform(-120, 120)
                    if look_at(x, y) != look or self._distance_to_play(x, y) > 2600:
                        continue
                    yaw = rng.pick((0.0, 90.0, 180.0, 270.0)) + rng.uniform(-10, 10)
                    rect = self._prop_rect(name, (x, y), yaw)
                    if not free(rect, 150.0):
                        continue
                    self._rim_taken.append(rect)
                    self.rim_props.append(
                        {
                            "kit": name,
                            "label": f"Rim_{name!s}{index:d}",
                            "center": (x, y),
                            "yaw": yaw,
                            "round": name in self.ROUND,
                            "size": rect[2:4],
                            "base": RIM_BLOCK_HEIGHT,
                            "look": look,
                        }
                    )
                    break

    def _rim_footprint_ok(self, rect: Rect) -> bool:
        """The footprint stands wholly on interior rim cells (so on a 600 cm block, clear of the 300 cm edge ring)."""
        half = math.hypot(rect[2], rect[3]) / 2
        i0, i1 = self.cell_of(rect[0] - half, 0)[0], self.cell_of(rect[0] + half, 0)[0]
        j0, j1 = self.cell_of(0, rect[1] - half)[1], self.cell_of(0, rect[1] + half)[1]
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                if (
                    dist_point_rect(i * CELL, j * CELL, rect) <= CELL / 2 * 1.42
                    and (i, j) not in self.interior
                    and rects_close(rect, (i * CELL, j * CELL, CELL, CELL, 0.0), 0.0)
                ):
                    return False
        return True

    def _distance_to_play(self, x: float, y: float) -> float:
        best = 1e9
        ci, cj = self.cell_of(x, y)
        for i in range(ci - 7, ci + 8):
            for j in range(cj - 7, cj + 8):
                if self.walkable((i, j)):
                    best = min(best, math.hypot(x - i * CELL, y - j * CELL))
        return best

    def _dress_slab(self) -> None:
        """Data Hall 0: the whole blocker rectangle is one hall (doors two per long side), cooling towers on the roof."""
        for blocker in self.data["blockers"]:
            if blocker["id"] != "slab":
                continue
            cx, cy, sx, sy, _yaw = self.rect_of_poly(blocker["poly"])
            long_x = sx >= sy
            self.slab = {
                "label": "DataHall0",
                "center": (cx, cy),
                "size": (sx, sy),
                "doors": {"N": 2, "S": 2} if long_x else {"E": 2, "W": 2},
            }
            towers = []
            if "CoolingTower" in self.kit_sizes:
                for tx in (-1800, 0, 1800):
                    for ty in (-700, 700):
                        towers.append((cx + tx, cy + ty))
            self.slab["towers"] = towers

    # ------------------------------------------------------------------ checks
    def _check(self) -> None:
        issues = self.issues
        if not self.analysis_ok:
            issues.extend("DrawMapLayout: " + e for e in self.analysis.errors)
        for hall in ([self.slab] if self.slab else []) + self.halls:
            for value in hall["size"]:
                if abs(value / 100.0 - round(value / 100.0)) > 1e-6:
                    issues.append(
                        f"{hall['label']!s} size {hall['size']!s} is not a whole number of metres"
                    )
        # Level walls and parapets must not touch a ramp's walkable strip.
        for ramp in self.data["ramps"]:
            for strip in ramp["strips"]:
                srect = self.rect_of_poly(strip)
                for box in self.walls + self.parapets:
                    brect = (
                        box["center"][0],
                        box["center"][1],
                        box["size"][0],
                        box["size"][1],
                        0.0,
                    )
                    if rects_close(srect, brect, -1.0):
                        issues.append(
                            f"{box['label']!s} touches the walkable strip of {ramp['id']!s}"
                        )
        # Blocking level geometry must not enter a territory disc.
        for label, centre, radius in self.keep_circles:
            if label.startswith(("hq", "sector")):
                for box in self.walls + self.parapets + self.rim_blocks:
                    brect = (
                        box["center"][0],
                        box["center"][1],
                        box["size"][0],
                        box["size"][1],
                        0.0,
                    )
                    if dist_point_rect(centre[0], centre[1], brect) < radius - 100.0:
                        issues.append(f"{box['label']!s} enters {label!s}")
                for piece in self.rim_pieces:
                    prect = (piece["center"][0], piece["center"][1], CELL, CELL, 0.0)
                    if dist_point_rect(centre[0], centre[1], prect) < radius - 100.0:
                        issues.append(
                            f"rim {piece['piece']!s} at {piece['center']!s} enters {label!s}"
                        )
        # Every rim cell with a playable neighbour is covered by a kit piece; every playable cell of a level touching
        # a different level (or a ramp) has a wall or a gap on that edge (constructed, so count them).
        expected_edges = 0
        for (i, j), kind in self.kind.items():
            if kind == "P":
                for di, dj in ((1, 0), (0, 1)):
                    o = (i + di, j + dj)
                    if self.kind_at(o) == "P" and self.level[o] != self.level[(i, j)]:
                        expected_edges += 1
        got = sum(
            round(
                (w["size"][1] if w["axis"] == "v" else w["size"][0])
                - 2 * WALL_EXTENSION
            )
            // CELL
            for w in self.walls
        )
        if got != expected_edges:
            issues.append(
                f"wall runs cover {got:d} cell edges, expected {expected_edges:d}"
            )
        # Territory discs sit on one level (the design checks this too; re-assert on the raster).
        for label, centre, radius in self.keep_circles:
            if not label.startswith(("hq", "sector")):
                continue
            levels = set()
            for (i, j), lv in self.level.items():
                if (
                    dist_point_rect(
                        centre[0], centre[1], (i * CELL, j * CELL, CELL, CELL, 0.0)
                    )
                    < radius - 200
                ):
                    levels.add(lv)
            if len(levels) > 1:
                issues.append(f"{label!s} spans levels {sorted(levels)!s}")

    # ------------------------------------------------------------------ raster proof that the levels are sealed
    RES = 50

    def _solid_raster(self) -> tuple[bytearray, int, int]:
        """50 cm raster of everything that blocks, exactly as generated: kit pieces with their real outline (a
        Cliff_CornerOuter is a quarter disc of radius 400 about the cell corner opposite its low neighbours), walls,
        parapets, rim blocks, the Slab and the ramp footprints when `close_ramps`."""
        res = self.RES
        half = self.extent * CELL + CELL // 2
        n = int(2 * half // res)
        solid = bytearray(n * n)
        origin = -half

        def cells_of(cx: float, cy: float, sx: float, sy: float) -> Iterator[Cell]:
            i0, i1 = (
                int((cx - sx / 2 - origin) // res),
                math.ceil((cx + sx / 2 - origin) / res),
            )
            j0, j1 = (
                int((cy - sy / 2 - origin) // res),
                math.ceil((cy + sy / 2 - origin) / res),
            )
            for i in range(max(i0, 0), min(i1, n)):
                for j in range(max(j0, 0), min(j1, n)):
                    yield i, j

        def fill(cx: float, cy: float, sx: float, sy: float) -> None:
            for i, j in cells_of(cx, cy, sx, sy):
                solid[i * n + j] = 1

        for box in self.walls + self.parapets + self.rim_blocks:
            fill(box["center"][0], box["center"][1], box["size"][0], box["size"][1])
        for rect in self.slab_rects():
            fill(rect[0], rect[1], rect[2], rect[3])
        for piece in self.rim_pieces:
            cx, cy = piece["center"]
            if piece["piece"] != "Cliff_CornerOuter":
                fill(cx, cy, CELL, CELL)
                continue
            sx = sum(DIRS[a][0] for a in piece["low"])
            sy = sum(DIRS[a][1] for a in piece["low"])
            ox, oy = cx - sx * CELL / 2, cy - sy * CELL / 2
            for i, j in cells_of(cx, cy, CELL, CELL):
                x, y = origin + (i + 0.5) * res, origin + (j + 0.5) * res
                if math.hypot(x - ox, y - oy) <= CELL:
                    solid[i * n + j] = 1
        return solid, n, origin

    def _flood(
        self,
        solid: bytearray,
        n: int,
        origin: float,
        start: Point,
        blocked_extra: Sequence[Rect] = (),
    ) -> bytearray | None:
        res = self.RES
        # the agent (radius ~35) cannot use a free cell within one raster cell of a solid one
        free = bytearray(n * n)
        for i in range(1, n - 1):
            row = i * n
            for j in range(1, n - 1):
                if not (
                    solid[row + j]
                    or solid[row + j - 1]
                    or solid[row + j + 1]
                    or solid[row - n + j]
                    or solid[row + n + j]
                    or solid[row - n + j - 1]
                    or solid[row - n + j + 1]
                    or solid[row + n + j - 1]
                    or solid[row + n + j + 1]
                ):
                    free[row + j] = 1
        for rect in blocked_extra:
            i0, i1 = (
                int((rect[0] - rect[2] / 2 - origin) // res),
                math.ceil((rect[0] + rect[2] / 2 - origin) / res),
            )
            j0, j1 = (
                int((rect[1] - rect[3] / 2 - origin) // res),
                math.ceil((rect[1] + rect[3] / 2 - origin) / res),
            )
            for i in range(i0, i1):
                for j in range(j0, j1):
                    free[i * n + j] = 0
        si, sj = int((start[0] - origin) // res), int((start[1] - origin) // res)
        seen = bytearray(n * n)
        if not free[si * n + sj]:
            return None
        stack = [si * n + sj]
        seen[stack[0]] = 1
        while stack:
            c = stack.pop()
            for d in (1, -1, n, -n):
                m = c + d
                if 0 <= m < n * n and free[m] and not seen[m]:
                    seen[m] = 1
                    stack.append(m)
        return seen

    def check_seal(self) -> None:
        """Flood the generated geometry: with every ramp footprint closed each group of regions of one level reaches
        only itself (so no wall end, rim corner or parapet leaks), and with the ramps open the bases connect."""
        solid, n, origin = self._solid_raster()
        res = self.RES
        groups: dict[str, list[Cell]] = {}
        for cell, rid in self.region_of.items():
            groups.setdefault(rid, []).append(cell)
        # regions of one level touching without a wall (the pocket mouths) form one group
        parent = {rid: rid for rid in groups}

        def find(a: str) -> str:
            while parent[a] != a:
                a = parent[a]
            return a

        for (i, j), rid in self.region_of.items():
            for di, dj in ((1, 0), (0, 1)):
                other = self.region_of.get((i + di, j + dj))
                if (
                    other
                    and other != rid
                    and self.level[(i, j)] == self.level[(i + di, j + dj)]
                ):
                    parent[find(rid)] = find(other)
        classes: dict[str, set[str]] = {}
        for rid in groups:
            classes.setdefault(find(rid), set()).add(rid)
        footprints = [
            self.rect_of_poly(fp)
            for ramp in self.data["ramps"]
            for fp in ramp["footprints"]
        ]
        self.seal: dict[str, list[str]] = {}
        for _root, members in sorted(classes.items()):
            first = sorted(members)[0]
            cells = groups[first]
            start = self.centre_of(cells[len(cells) // 2])
            seen = self._flood(solid, n, origin, start, footprints)
            if seen is None:
                self.issues.append(f"seal check: start of {first!s} is not free")
                continue
            reached = set()
            for cell, rid in self.region_of.items():
                x, y = self.centre_of(cell)
                if seen[int((x - origin) // res) * n + int((y - origin) // res)]:
                    reached.add(rid)
            self.seal[first] = sorted(reached)
            if reached != members:
                self.issues.append(
                    f"seal check: with the ramps closed, {first!s} reaches {sorted(reached)!s} "
                    f"but should reach only {sorted(members)!s}"
                )
        start = self.centre_of(
            next(c for c, rid in self.region_of.items() if rid == "main_H")
        )
        seen = self._flood(solid, n, origin, start)
        reached = {
            rid
            for cell, rid in self.region_of.items()
            if seen
            and seen[
                int((self.centre_of(cell)[0] - origin) // res) * n
                + int((self.centre_of(cell)[1] - origin) // res)
            ]
        }
        self.seal["open"] = sorted(reached)
        if reached != set(self.regions):
            self.issues.append(
                f"seal check: with the ramps open the Bunker main reaches {sorted(reached)!s}, not every region"
            )

    # ------------------------------------------------------------------ summary
    def counts(self) -> dict[str, int | dict[str, int]]:
        return {
            "cells playable": sum(1 for k in self.kind.values() if k == "P"),
            "cells ramp floor": sum(1 for k in self.kind.values() if k == "R"),
            "cells rim edge": len(self.rim_pieces),
            "cells rim interior": len(self.interior),
            "rim kit pieces": dict(self.rim_counts),
            "rim blocks": len(self.rim_blocks),
            "level walls": len(self.walls),
            "parapets": len(self.parapets),
            "ramp decks": len(self.decks),
            "in-play props": len(self.props),
            "towers": len(self.towers),
            "lamps": len(self.lamps),
            "rim halls": len(self.halls),
            "rim props": len(self.rim_props),
            "slab towers": len(self.slab["towers"]) if self.slab else 0,
        }


def require(value: T, message: str) -> T:
    if not value:
        raise ValueError(message)
    return value


# ----------------------------------------------------------------------------- stand-alone smoke test
def env_sizes() -> dict[str, tuple[float, float, float, float, float]]:
    """Environment-kit footprints (name -> footprint X, Y, height, blocking X, Y; cm) read from the literals of
    Build/EnvKit.py without importing it (it needs Unreal), so plain Python and Unreal's -game Python get the same
    plan the generator builds from EnvKit.piece_size / collision_size."""
    with open(os.path.join(HERE, "EnvKit.py")) as handle:
        tree = ast.parse(handle.read())
    literals: dict[str, object] = {}
    for node in tree.body:
        if (
            isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
        ):
            if node.targets[0].id == "SPEC":
                literals["SPEC"] = ast.literal_eval(node.value)
            elif node.targets[0].id == "GROUND_FOOTPRINT":
                literals["GROUND_FOOTPRINT"] = ast.literal_eval(node.value)
    spec = cast(
        dict[str, tuple[float, float, float, tuple[str, ...]]], literals["SPEC"]
    )
    footprints = cast(dict[str, tuple[float, float]], literals["GROUND_FOOTPRINT"])
    out = {}
    for name, (fx, fy, height, _slots) in spec.items():
        bx, by = footprints.get(name, (fx, fy))
        out[name] = (fx * 100.0, fy * 100.0, height * 100.0, bx * 100.0, by * 100.0)
    return out


def write_svg(layout: Layout, path: str | os.PathLike[str]) -> None:
    """Debug plan: rim, walls, parapets, props, keep-outs. +X is up, +Y is right."""
    scale, pad = 0.04, 20
    w = h = int(layout.hx * 2 * scale) + 2 * pad

    def P(x: float, y: float) -> WorldPoint:
        return (y + layout.hy) * scale + pad, (layout.hx - x) * scale + pad

    def rect(
        cx: float,
        cy: float,
        sx: float,
        sy: float,
        yaw: float,
        fill: str,
        opacity: float = 1.0,
        stroke: str = "none",
    ) -> str:
        pts = []
        for ox, oy in (
            (-sx / 2, -sy / 2),
            (sx / 2, -sy / 2),
            (sx / 2, sy / 2),
            (-sx / 2, sy / 2),
        ):
            rx, ry = rot(ox, oy, yaw)
            x, y = P(cx + rx, cy + ry)
            pts.append(f"{x:.1f},{y:.1f}")
        return (
            f'<polygon points="{" ".join(pts)!s}" fill="{fill!s}" '
            f'fill-opacity="{opacity!s}" stroke="{stroke!s}"/>'
        )

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:d}" height="{h:d}"><rect width="100%" height="100%" '
        'fill="#15181d"/>'
    ]
    for (i, j), kind in layout.kind.items():
        colour = {"P": "#3a4452", "R": "#59432a", "S": "#2b3d4d", "X": "#0d0f12"}[kind]
        if kind == "P":
            colour = {"L0": "#37424f", "L1": "#3d4a5a", "L2": "#465669"}[
                layout.level[(i, j)]
            ]
        out.append(rect(i * CELL, j * CELL, CELL, CELL, 0, colour))
    for piece in layout.rim_pieces:
        colour = {
            "Cliff_Straight": "#7a5c3a",
            "Cliff_CornerOuter": "#9a6a3a",
            "Plateau_Fill": "#5a4a3a",
        }[piece["piece"]]
        out.append(
            rect(
                piece["center"][0], piece["center"][1], CELL - 20, CELL - 20, 0, colour
            )
        )
        dx, dy = DIRS[piece["low"][0]] if piece["low"] else (0, 0)
        out.append(
            rect(
                piece["center"][0] + dx * 150,
                piece["center"][1] + dy * 150,
                60,
                60,
                0,
                "#ffd070",
            )
        )
    for box in layout.walls:
        out.append(
            rect(
                box["center"][0],
                box["center"][1],
                box["size"][0],
                box["size"][1],
                0,
                "#ff5050",
            )
        )
    for box in layout.parapets:
        out.append(
            rect(
                box["center"][0],
                box["center"][1],
                box["size"][0],
                box["size"][1],
                0,
                "#ffa000",
            )
        )
    for _l, centre, radius in layout.keep_circles:
        x, y = P(*centre)
        out.append(
            f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius * scale:.1f}" fill="none" stroke="#4aa0ff" stroke-width="1"/>'
        )
    for _label, pts in layout.corridors:
        points = " ".join(f"{x:.1f},{y:.1f}" for x, y in (P(*p) for p in pts[::4]))
        out.append(
            f'<polyline points="{points!s}" fill="none" stroke="#40ff90" '
            f'stroke-opacity="0.25" stroke-width="{2 * ROUTE_CORRIDOR_HALF * scale:.1f}"/>'
        )
    for prop in layout.props:
        out.append(
            rect(
                prop["center"][0],
                prop["center"][1],
                prop["size"][0],
                prop["size"][1],
                prop["yaw"],
                "#e0e0e0",
            )
        )
    for hall in layout.halls:
        out.append(
            rect(
                hall["center"][0],
                hall["center"][1],
                hall["size"][0],
                hall["size"][1],
                0,
                "#40d0ff",
                0.7,
            )
        )
    for prop in layout.rim_props:
        out.append(
            rect(
                prop["center"][0],
                prop["center"][1],
                prop["size"][0],
                prop["size"][1],
                prop["yaw"],
                "#c0ffc0",
            )
        )
    out.append("</svg>")
    with open(path, "w") as handle:
        handle.write("\n".join(out))


def main() -> int:
    layout = Layout(kit_sizes=env_sizes())
    for key, value in layout.counts().items():
        print(f"{key!s:<20} {value!s}")
    if "--svg" in sys.argv:
        path = sys.argv[sys.argv.index("--svg") + 1]
        write_svg(layout, path)
        print("wrote " + path)
    for issue in layout.issues:
        print("ISSUE " + issue)
    print(f"Issues: {len(layout.issues) or 'none'!s}")
    return 1 if layout.issues else 0


if __name__ == "__main__":
    sys.exit(main())
