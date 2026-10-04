"""Habitable Zone v2 terrain checks: T3 constraints, ramp and prop placement, walking over ramps, routes and necks.

`terrain_errors` is pure and quick; `Walker` is the 100 cm raster (cliffs, rock walls, rocks, props, ramps) behind
`walk_errors`, `route_report` and the audit's travel times. Imported by Build/DrawAvailabilityZoneV2.py and
Tools/tests/maps; no Unreal dependency.
"""

from __future__ import annotations

from collections.abc import Collection, Mapping
import heapq
import itertools
import math
from typing import Any, cast

import MatchLayout
import TerrainPlan
from TerrainPlan import (
    CELL,
    RAMP_HALF_STRIP,
    RAMP_RUN,
    Point,
    Terrain,
    cell_of,
    contains,
    polygon_distance,
    rect_distance,
)

WALK_CELL = 100
WALK_N = 200
ROCK_CLEARANCE = 100.0
SPEED = 420.0
OPEN_SPEED = 1.15  # trait magnitude of decisions T1
HAZARD_DPS = 4.0
RAMP_FOOT_BAND = (
    -RAMP_RUN / 2 - 150,
    -RAMP_RUN / 2 + 150,
)  # where a walker may change level, along the ramp


class Walker:
    """8-neighbour Dijkstra on 100 cm cells. Changing level is allowed only in a ramp's top band."""

    def __init__(self, terrain: Terrain) -> None:
        self.terrain = terrain
        self.blocked = bytearray(WALK_N * WALK_N)
        self.owner = [-1] * (WALK_N * WALK_N)
        self.level = bytearray(WALK_N * WALK_N)
        self.ramp = [-1] * (WALK_N * WALK_N)
        self.top = bytearray(WALK_N * WALK_N)
        props = terrain.prop_rects()
        for i in range(WALK_N):
            for j in range(WALK_N):
                x, y = self.xy(i * WALK_N + j)
                n = i * WALK_N + j
                cell = cell_of(x, y)
                self.owner[n] = terrain.owner.get(cell, -1)
                self.level[n] = cell in terrain.plateau
                blocked = cell in terrain.walls
                blocked = blocked or any(
                    rect_distance((x, y), (p[0], p[1]), p[4], p[2], p[3])
                    < ROCK_CLEARANCE
                    for p in props
                )
                blocked = blocked or any(
                    contains(r["poly"], (x, y))
                    or polygon_distance((x, y), r["poly"]) < ROCK_CLEARANCE
                    for r in terrain.data["blockers"]
                    if self._near(r["poly"], x, y)
                )
                self.blocked[n] = blocked
                for k, ramp in enumerate(terrain.ramps):
                    lx, ly = terrain.ramp_frame((x, y), ramp)
                    if abs(ly) <= RAMP_HALF_STRIP and abs(lx) <= RAMP_RUN / 2:
                        self.ramp[n] = k
                    if (
                        abs(ly) <= RAMP_HALF_STRIP
                        and RAMP_FOOT_BAND[0] <= lx <= RAMP_FOOT_BAND[1]
                    ):
                        self.top[n] = 1

    @staticmethod
    def _near(poly: list[list[float]], x: float, y: float) -> bool:
        return (
            min(p[0] for p in poly) - 200 <= x <= max(p[0] for p in poly) + 200
            and min(p[1] for p in poly) - 200 <= y <= max(p[1] for p in poly) + 200
        )

    @staticmethod
    def xy(n: int) -> tuple[float, float]:
        i, j = divmod(n, WALK_N)
        return (i - WALK_N / 2 + 0.5) * WALK_CELL, (j - WALK_N / 2 + 0.5) * WALK_CELL

    @staticmethod
    def index(point: Point) -> int:
        i = min(
            WALK_N - 1, max(0, int((point[0] + WALK_N / 2 * WALK_CELL) // WALK_CELL))
        )
        j = min(
            WALK_N - 1, max(0, int((point[1] + WALK_N / 2 * WALK_CELL) // WALK_CELL))
        )
        return i * WALK_N + j

    def nearest(self, point: Point) -> int:
        start = self.index(point)
        if not self.blocked[start]:
            return start
        i0, j0 = divmod(start, WALK_N)
        options = [
            a * WALK_N + b
            for a in range(max(0, i0 - 5), min(WALK_N, i0 + 6))
            for b in range(max(0, j0 - 5), min(WALK_N, j0 + 6))
            if not self.blocked[a * WALK_N + b]
        ]
        return min(options, key=lambda n: math.dist(self.xy(n), point))

    def search(
        self,
        src: Point,
        dst: Point | None = None,
        allowed: Collection[int] | None = None,
    ) -> tuple[list[float], dict[int, int]]:
        start, goal = self.nearest(src), None if dst is None else self.nearest(dst)
        dist = [math.inf] * (WALK_N * WALK_N)
        dist[start] = 0.0
        prev: dict[int, int] = {}
        todo = [(0.0, start)]
        moves = [
            (a, b, WALK_CELL * math.hypot(a, b))
            for a in (-1, 0, 1)
            for b in (-1, 0, 1)
            if a or b
        ]
        while todo:
            length, n = heapq.heappop(todo)
            if length != dist[n]:
                continue
            if n == goal:
                break
            i, j = divmod(n, WALK_N)
            for a, b, step in moves:
                p, q = i + a, j + b
                if not (0 <= p < WALK_N and 0 <= q < WALK_N):
                    continue
                m = p * WALK_N + q
                if self.blocked[m] or length + step >= dist[m]:
                    continue
                if (
                    a
                    and b
                    and (self.blocked[p * WALK_N + j] or self.blocked[i * WALK_N + q])
                ):
                    continue
                if (
                    allowed is not None
                    and self.owner[m] not in allowed
                    and self.ramp[m] < 0
                ):
                    continue
                if self.level[n] != self.level[m] and not (self.top[n] and self.top[m]):
                    continue
                dist[m] = length + step
                prev[m] = n
                heapq.heappush(todo, (dist[m], m))
        return dist, prev

    def distance(
        self, src: Point, dst: Point, allowed: Collection[int] | None = None
    ) -> float:
        return self.search(src, dst, allowed)[0][self.nearest(dst)]

    def path(
        self, src: Point, dst: Point, allowed: Collection[int] | None = None
    ) -> list[tuple[float, float]]:
        dist, prev = self.search(src, dst, allowed)
        cur = self.nearest(dst)
        if not math.isfinite(dist[cur]):
            return []
        out = [cur]
        while cur in prev:
            cur = prev[cur]
            out.append(cur)
        return [self.xy(n) for n in reversed(out)]


def representative(data: Mapping[str, Any], region: int) -> Point:
    """HQ for a main, the capture anchor otherwise."""
    if region == 0:
        return cast(Point, data["headquarters"][0]["pos"])
    if region == 14:
        return cast(Point, data["headquarters"][1]["pos"])
    return cast(Point, data["regions"][region]["anchor"])


def route_report(terrain: Terrain, walker: Walker) -> list[dict[str, Any]]:
    """Length, time and trait exposure of every authored route, HQ to HQ, along the region sequence."""
    data = terrain.data
    home, enemy = data["headquarters"][0]["pos"], data["headquarters"][1]["pos"]
    out = []
    for route in data["terrain"]["routes"]:
        regions = route["regions"]
        points = walker.path(home, enemy, set(regions))
        hazard = open_ = 0.0
        for a, b in itertools.pairwise(points):
            trait = terrain.trait.get(walker.owner[walker.index(b)])
            hazard += math.dist(a, b) if trait == "hazard" else 0.0
            open_ += math.dist(a, b) if trait == "open" else 0.0
        length = (
            sum(math.dist(a, b) for a, b in itertools.pairwise(points))
            if points
            else math.inf
        )
        out.append(
            {
                "name": route["name"],
                "regions": regions,
                "length": length,
                "hazard": hazard,
                "open": open_,
                "seconds": (length - open_) / SPEED + open_ / (SPEED * OPEN_SPEED),
                "damage": hazard / SPEED * HAZARD_DPS,
                "traits": sorted({t for r in regions if (t := terrain.trait.get(r))}),
                "points": points,
            }
        )
    return out


def route_errors(terrain: Terrain, report: list[dict[str, Any]]) -> list[str]:
    errors = []
    neighbours = terrain.neighbours()
    if not 2 <= len(report) <= 3:
        errors.append(f"Routes: 2-3 authored routes required (got {len(report)})")
    for route in report:
        seq = route["regions"]
        if (
            seq[0] != 0
            or seq[-1] != 14
            or any(b not in neighbours[a] for a, b in itertools.pairwise(seq))
        ):
            errors.append(
                "Route {} is not a neighbour path from region 0 to 14".format(
                    route["name"]
                )
            )
        elif not math.isfinite(route["length"]):
            errors.append("Route {} is not walkable end to end".format(route["name"]))
    for n, first in enumerate(report):
        for second in report[n + 1 :]:
            same = first["regions"][1:-1] == second["regions"][1:-1]
            alike = (
                abs(first["length"] - second["length"]) < 0.05 * first["length"]
                and first["traits"] == second["traits"]
            )
            if same or alike:
                errors.append(
                    "Routes {} and {} do not cost different things".format(
                        first["name"], second["name"]
                    )
                )
    on_route = {r for route in report for r in route["regions"][1:-1]}
    necks = [r for r in on_route if len(neighbours[r]) <= 2]
    if len(necks) < 2:
        errors.append(
            f"Need >= 2 non-main route regions with <= 2 neighbours (got {sorted(necks)})"
        )
    return errors


def walk_errors(terrain: Terrain, walker: Walker) -> list[str]:
    """The walkable region graph must equal the declared neighbours: no walking through a closed border, no
    declared neighbour that cannot be walked to."""
    errors = []
    neighbours = terrain.neighbours()
    data = terrain.data
    for a, near in sorted(terrain.adjacent.items()):
        for b in sorted(n for n in near if n > a):
            walkable = math.isfinite(
                walker.distance(
                    representative(data, a), representative(data, b), {a, b}
                )
            )
            if walkable != (b in neighbours[a]):
                errors.append(
                    f"Regions {a} and {b}: walkable={walkable} but neighbour={b in neighbours[a]}"
                )
    for site, point in _sites(data):
        if walker.blocked[walker.index(point)]:
            errors.append(f"{site} is on blocked ground")
        elif not math.isfinite(walker.distance(data["headquarters"][0]["pos"], point)):
            errors.append(f"{site} is not reachable from the human HQ")
    return errors


def _sites(data: Mapping[str, Any]) -> list[tuple[str, Point]]:
    sites: list[tuple[str, Point]] = [
        ("HQ {}".format(h["id"]), h["pos"]) for h in data["headquarters"]
    ]
    for region in data["regions"]:
        if region["anchor"]:
            sites.append((f"anchor {region['index']}", region["anchor"]))
        sites += [
            (f"post {n} of region {region['index']}", p)
            for n, p in enumerate(region["defend_posts"])
        ]
    return sites + [(f"deposit {n}", d["pos"]) for n, d in enumerate(data["deposits"])]


def terrain_errors(terrain: Terrain) -> list[str]:
    """Traits, plateaus, ramps, closed borders and props against the T3 constraints (no walking)."""
    errors = (
        _trait_errors(terrain)
        + _ramp_errors(terrain)
        + _site_errors(terrain)
        + _prop_errors(terrain)
        + _wall_errors(terrain)
        + _post_errors(terrain)
    )
    stated = {r["index"]: r["neighbours"] for r in terrain.data["regions"]}
    wrong = {i: n for i, n in terrain.neighbours().items() if stated[i] != n}
    if wrong:
        errors.append(
            f"neighbours must be polygon adjacency minus closed borders: {wrong}"
        )
    return errors


def _trait_errors(terrain: Terrain) -> list[str]:
    errors = []
    regions = terrain.regions
    for index, region in regions.items():
        trait = region.get("trait", "missing")
        if trait not in (None, *TerrainPlan.TRAITS):
            errors.append(f"Region {index} has invalid trait {trait!r}")
        if region["role"] == "main" and trait:
            errors.append(f"Main region {index} must have no trait")
        if region["role"] == "reward" and trait == "hazard":
            errors.append(f"Reward region {index} must not be hazard")
    counts = TerrainPlan.trait_counts(terrain)
    for trait, least in (("high_ground", 2), ("cover", 3), ("open", 2), ("hazard", 1)):
        if counts[trait] < least:
            errors.append(f"Need >= {least} {trait} regions (got {counts[trait]})")
    if sum(counts.values()) > 10:
        errors.append(
            f"At most 10 of 15 regions may carry a trait (got {sum(counts.values())})"
        )
    if {p["region"] for p in terrain.data["terrain"]["plateaus"]} != set(
        terrain.plateau_regions
    ):
        errors.append("terrain.plateaus must list exactly the high_ground regions")
    for index in terrain.plateau_regions:
        for near in sorted(terrain.adjacent[index]):
            ramps = [
                r for r in terrain.ramps if (r["plateau"], r["to"]) == (index, near)
            ]
            if len(ramps) > 1 or (
                len(ramps) == 0 and tuple(sorted((index, near))) in terrain.wall_borders
            ):
                errors.append(
                    f"High ground {index}: border with {near} needs at most one ramp and no wall"
                )
    if (
        len(terrain.hazard_plates()) < TerrainPlan.MIN_HAZARD_PLATES
        and counts["hazard"]
    ):
        errors.append(f"Hazard ground needs >= {TerrainPlan.MIN_HAZARD_PLATES} plates")
    return errors


def _ramp_errors(terrain: Terrain) -> list[str]:
    errors = []
    for ramp in terrain.ramps:
        name = f"Ramp {ramp['plateau']}->{ramp['to']}"
        if ramp["to"] not in terrain.adjacent[ramp["plateau"]]:
            errors.append(f"{name}: regions are not neighbours")
            continue
        if ramp["yaw"] not in TerrainPlan.DIRS:
            errors.append(f"{name}: yaw must be a multiple of 90")
            continue
        di, dj = TerrainPlan.DIRS[ramp["yaw"]]
        along = (abs(dj), abs(di))
        edge = (
            ramp["centre"][0] - di * CELL,
            ramp["centre"][1] - dj * CELL,
        )  # midpoint of the top edge
        top = [
            (
                round((edge[0] - di * CELL / 2 + s * along[0] * CELL / 2) / CELL),
                round((edge[1] - dj * CELL / 2 + s * along[1] * CELL / 2) / CELL),
            )
            for s in (-1, 1)
        ]
        if any(terrain.plateau_owner.get(c) != ramp["plateau"] for c in top):
            errors.append(
                f"{name}: top edge is not on two plateau cells of region {ramp['plateau']}"
            )
        front = [
            (
                round(
                    (ramp["centre"][0] + sx * along[0] * CELL / 2 + sd * di * CELL / 2)
                    / CELL
                ),
                round(
                    (ramp["centre"][1] + sx * along[1] * CELL / 2 + sd * dj * CELL / 2)
                    / CELL
                ),
            )
            for sx in (-1, 1)
            for sd in (-1, 1)
        ]
        if any(
            c in terrain.solid or terrain.owner.get(c, -1) != ramp["to"] for c in front
        ):
            errors.append(
                f"{name}: footprint must lie on free ground wholly inside region {ramp['to']}"
            )
        for kind, point, limit in terrain.sites():
            if (
                rect_distance(point, ramp["centre"], ramp["yaw"], RAMP_RUN, RAMP_RUN)
                < limit
            ):
                errors.append(f"{name}: footprint too close to a {kind} at {point}")
        for rock in terrain.data["blockers"]:
            if any(
                rect_distance(p, ramp["centre"], ramp["yaw"], RAMP_RUN, RAMP_RUN)
                < TerrainPlan.RAMP_ROCK_CLEARANCE
                for p in rock["poly"]
            ):
                errors.append("{}: footprint too close to {}".format(name, rock["id"]))
    return errors


def _site_errors(terrain: Terrain) -> list[str]:
    """Sites in a plateau region stand a whole cell inside it; elsewhere they stand on flat ground."""
    errors = []
    for name, point in _sites(terrain.data):
        cell = cell_of(*point)
        if terrain.owner.get(cell) in terrain.plateau_regions:
            if not all(
                (cell[0] + a, cell[1] + b) in terrain.plateau
                for a in (-1, 0, 1)
                for b in (-1, 0, 1)
            ):
                errors.append(f"{name} is too close to a plateau edge")
        elif cell in terrain.solid or any(
            terrain.in_ramp(point, r) is not None for r in terrain.ramps
        ):
            errors.append(f"{name} stands on a plateau, wall or ramp of another region")
    return errors


def _prop_errors(terrain: Terrain) -> list[str]:
    errors = []
    data = terrain.data
    per_region: dict[int, int] = {}
    for n, prop in enumerate(terrain.props):
        name = f"Prop {n} ({prop['kit']})"
        region, pos = prop["region"], prop["pos"]
        per_region[region] = per_region.get(region, 0) + 1
        if prop["kit"] not in TerrainPlan.PROP_SIZE:
            errors.append(f"{name}: unknown kit piece")
            continue
        if (
            terrain.trait.get(region) != "cover"
            or terrain.owner.get(cell_of(*pos)) != region
        ):
            errors.append(f"{name}: must stand in a cover region")
        reach = max(TerrainPlan.PROP_SIZE[prop["kit"]]) * 50
        if any(
            cell_of(pos[0] + dx, pos[1] + dy) in terrain.solid
            for dx in (-reach, 0, reach)
            for dy in (-reach, 0, reach)
        ) or any(
            terrain.in_ramp(pos, r, RAMP_RUN / 2 + 100) is not None
            for r in terrain.ramps
        ):
            errors.append(f"{name}: on a plateau, wall or ramp")
        if any(
            m != n and math.dist(pos, o["pos"]) < 800
            for m, o in enumerate(terrain.props)
        ):
            errors.append(f"{name}: closer than 800 cm to another prop")
        clash = terrain.prop_site_clash(pos, prop["yaw"], prop["kit"])
        if clash:
            errors.append(f"{name}: too close to a {clash}")
        if any(
            contains(r["poly"], pos) or polygon_distance(pos, r["poly"]) < 250
            for r in data["blockers"]
        ):
            errors.append(f"{name}: on a rock")
    for index, trait in terrain.trait.items():
        if trait == "cover" and per_region.get(index, 0) < TerrainPlan.MIN_COVER_PROPS:
            errors.append(
                f"Cover region {index} needs >= {TerrainPlan.MIN_COVER_PROPS} props"
            )
        if trait == "open" and any(
            contains(terrain.regions[index]["poly"], p)
            for r in data["blockers"]
            for p in r["poly"]
        ):
            errors.append(f"Open region {index} must be clear of rocks")
    return errors


def _wall_errors(terrain: Terrain) -> list[str]:
    """Every wall border is covered along its whole shared edge (Open sides are walled from the other side, and the
    walking check proves those sealed), and each Open region declares exactly the pieces standing in it."""
    errors = []
    for a, b in sorted(terrain.wall_borders):
        if "open" in (terrain.trait.get(a), terrain.trait.get(b)):
            continue
        for point in terrain.shared_edge_samples(a, b):
            if not terrain.edge_cells(point) <= terrain.walls:
                errors.append(
                    f"Wall {a}-{b} leaves the shared edge uncovered near {point[0]:.0f}, {point[1]:.0f}"
                )
                break
    declared = terrain.data["terrain"].get("open_exceptions", {})
    for index, trait in terrain.trait.items():
        if trait != "open":
            continue
        if sorted(declared.get(str(index), [])) != sorted(
            terrain.open_intrusions(index)
        ):
            errors.append(
                f"Open region {index} contains {sorted(terrain.open_intrusions(index))} but declares "
                f"{sorted(declared.get(str(index), []))}; list the exceptions in terrain.open_exceptions"
            )
    return errors


def _post_errors(terrain: Terrain) -> list[str]:
    """A holding squad stands in a 6-slot formation around its post (offsets +-220 x +-140 cm); the hold code clamps a
    slot outside the polygon to the border and rejects the move, leaving that unit behind. All six slots must lie
    inside the region on clear ground."""
    errors = []
    for region in terrain.data["regions"]:
        for number, post in enumerate(region["defend_posts"]):
            for dx in (-220, 0, 220):
                for dy in (-140, 140):
                    slot = (post[0] + dx, post[1] + dy)
                    if not contains(region["poly"], slot) or not terrain.clear_ground(
                        slot, MatchLayout.NAV_AGENT_RADIUS
                    ):
                        errors.append(
                            f"Region {region['index']} post {number}: formation slot {slot} is outside the region or on blocked ground"
                        )
    return errors
