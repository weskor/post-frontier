#!/usr/bin/env python3
"""Validate a map data file and draw its top-down layout plan.

    python3 Build/DrawMapLayout.py [Build/Maps/AvailabilityZone.json] [--out Art/Maps/AvailabilityZone-layout.png]
                                   [--svg PATH] [--report] [--quiet]

The single source of truth is Build/Maps/<Map>.json (arena, HQs, sectors, elevation regions, ramps, blockers,
chokes, decoration zones, proposals); the Unreal generator is meant to read the same file. This script:

  1. rasterises the regions onto a 100 cm grid (X = minimap-up, Y = minimap-right, like the game's minimap),
  2. runs the design checks the docs rely on (level overlap, terrain-kit grid alignment, ramp slopes and lane
     widths, sectors on one level, ramps and chokes outside every territory disc, choke widths, rot180 symmetry,
     reachability, Phase A heights against the placement rules),
  3. measures real walking distances at the unit speed in the file (16-neighbour Dijkstra with a 100 cm
     clearance ring, then line-of-sight smoothing; within about 1 % of a nav path),
  4. counts building placements with the 2D rules of ACommandGameState::ValidateBuildingPlacement,
  5. writes an SVG and rasterises it to PNG with rsvg-convert (or ImageMagick). No Python packages are needed.

Exit status is 1 if any check fails. `--report` prints the tables the design docs quote.
"""

from __future__ import annotations

import argparse
from collections.abc import Iterable, Iterator, Sequence
import heapq
from itertools import pairwise
import json
import math
import os
import shutil
import subprocess
import sys
from typing import Literal, NotRequired, TypedDict, Unpack, cast

Point = Sequence[float]
Polygon = Sequence[Point]
WorldPoint = tuple[float, float]
Cell = tuple[int, int]
DistanceGrid = list[list[float]]
Parents = dict[Cell, Cell]
Route = tuple[float, list[WorldPoint]]
HeightKey = Literal["z_cm", "z_sc2_cm"]
RadiusKey = Literal["hq_territory_radius", "capture_radius", "sector_territory_radius"]


class ArenaData(TypedDict):
    half_extent: list[float]
    placement_margin: float
    half_height: float


class KitGridData(TypedDict):
    cell: int
    step: int
    ramp_run: int
    ramp_walkable_width: int
    ramp_footprint_width: int
    note: str


class PlacementBoxData(TypedDict):
    centre_z_offset: float
    half_z: float


class ConstantsData(TypedDict):
    source: str
    unit_speed_cm_s: float
    capsule_radius: float
    capsule_half_height: float
    formation_column_spacing: float
    formation_row_spacing: float
    force_width: float
    force_depth_frontline: float
    capture_radius: float
    capture_seconds: float
    sector_territory_radius: float
    hq_territory_radius: float
    hq_exclusion_radius: float
    hq_box: list[float]
    jev_intruder_radius: float
    jev_defend_offset: list[float]
    footprint_radius: dict[str, float]
    weapon_range: dict[str, float]
    placement_z_tolerance: float
    nav_project_extent: list[float]
    click_plane_z: float
    jev_build_z: float
    placement_overlap_box: PlacementBoxData
    character_max_step_height: float
    nav_agent_max_step_height: float
    jev_ring: list[float]
    starting_resources: float
    baseline_income: float
    sector_income: float
    jev_assault_established_sites: int
    jev_assault_force: int


class ElevationData(TypedDict):
    id: str
    name: str
    z_cm: float
    z_sc2_cm: float
    note: str


class RegionData(TypedDict):
    id: str
    name: str
    level: str
    poly: list[list[float]]
    side: str
    role: str
    theme: str


class RampPieceData(TypedDict):
    centre: list[float]
    yaw: float


class RampData(TypedDict):
    id: str
    name: str
    from_level: str
    to_level: str
    kit_piece: str
    pieces: list[RampPieceData]
    top: list[float]
    foot: list[float]
    lanes: int
    lane_width: float
    length: float
    footprints: list[list[list[float]]]
    strips: list[list[list[float]]]
    side: str
    role: str
    note: str
    rise_z_cm: NotRequired[float]
    rise_z_sc2_cm: NotRequired[float]
    slope_z_cm: NotRequired[float]
    slope_z_sc2_cm: NotRequired[float]


class HeadquartersData(TypedDict):
    id: str
    team: int
    name: str
    pos: list[float]
    level: str
    side: str


class SectorData(TypedDict):
    id: str
    site_index: int
    name: str
    pos: list[float]
    level: str
    side: str
    role: str
    site_kind: str
    why: str
    pair: int


class BlockerData(TypedDict):
    id: str
    kind: str
    name: str
    poly: list[list[float]]
    status: str
    note: str


class PlugData(TypedDict):
    id: str
    kind: str
    poly: list[list[float]]
    centre: list[float]
    yaw: float
    status: str


class BreachData(TypedDict):
    note: str
    poly: list[list[float]]
    plugs: list[PlugData]


class VisionPointData(TypedDict):
    id: str
    name: str
    pos: list[float]
    radius: float
    status: str


class PlayerStartData(TypedDict):
    id: str
    slot: int
    pos: list[float]
    status: str


class ProposalsData(TypedDict):
    breach: BreachData
    vision_points: list[VisionPointData]
    per_player_start: list[PlayerStartData]


class ChokeData(TypedDict):
    id: str
    name: str
    center: list[float]
    across: list[float]
    width: float
    side: str


class HalfPlaneData(TypedDict):
    point: list[float]
    keep: list[float]


class BayData(TypedDict):
    id: str
    pos: list[float]
    kind: str


class BuildPocketData(TypedDict):
    id: str
    name: str
    owner_hint: str
    hq: str
    half_plane: HalfPlaneData
    bays: list[BayData]


class DecorationData(TypedDict):
    id: str
    region: str
    theme: str
    name: str
    terrain_kit: list[str]
    ground: list[str]
    trim: str
    kit: list[str]
    new: list[str]


class OpenFieldData(TypedDict):
    id: str
    name: str
    center: list[float]
    size: list[float]


class MapData(TypedDict):
    name: str
    title: str
    unreal_map: str
    schema: int
    units: str
    symmetry: str
    grid: KitGridData
    arena: ArenaData
    constants: ConstantsData
    elevation: list[ElevationData]
    elevation_note: str
    regions: list[RegionData]
    ramps: list[RampData]
    headquarters: list[HeadquartersData]
    sectors: list[SectorData]
    blockers: list[BlockerData]
    proposals: ProposalsData
    chokes: list[ChokeData]
    open_fields: list[OpenFieldData]
    build_pockets: list[BuildPocketData]
    decoration: list[DecorationData]


class TextOptions(TypedDict, total=False):
    size: int
    fill: str
    anchor: str
    weight: str
    italic: bool
    opacity: float
    halo: bool


CELL = 100.0
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQ2 = math.sqrt(2.0)
MOVES = (
    [
        (1, 0, 1.0),
        (-1, 0, 1.0),
        (0, 1, 1.0),
        (0, -1, 1.0),
        (1, 1, SQ2),
        (1, -1, SQ2),
        (-1, 1, SQ2),
        (-1, -1, SQ2),
    ]
    + [(a, b, math.sqrt(5.0)) for a in (-2, 2) for b in (-1, 1)]
    + [(b, a, math.sqrt(5.0)) for a in (-2, 2) for b in (-1, 1)]
)


# ----------------------------------------------------------------------------- geometry
def bbox(poly: Polygon) -> tuple[float, float, float, float]:
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    return min(xs), max(xs), min(ys), max(ys)


def inside(x: float, y: float, poly: Polygon) -> bool:
    """Even-odd ray cast. Points exactly on an edge may go either way; the design keeps clear of that."""
    hit = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit


def seg_dist(px: float, py: float, a: Point, b: Point) -> float:
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = (
        0.0
        if dx == dy == 0
        else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    )
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def poly_dist(px: float, py: float, poly: Polygon) -> float:
    """Distance from a point to the polygon (0 inside)."""
    if inside(px, py, poly):
        return 0.0
    return min(
        seg_dist(px, py, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly))
    )


def centroid(poly: Polygon) -> WorldPoint:
    x0, x1, y0, y1 = bbox(poly)
    return (x0 + x1) / 2.0, (y0 + y1) / 2.0


def rot180(p: Point) -> list[float]:
    return [-p[0], -p[1]]


# ----------------------------------------------------------------------------- grid
class Grid:
    """i indexes X (south to north), j indexes Y (west to east); cell centres sit on multiples of CELL plus half."""

    def __init__(self, data: MapData, breach_open: bool = False) -> None:
        hx, hy = data["arena"]["half_extent"]
        self.hx, self.hy = hx, hy
        self.nx, self.ny = int(2 * hx // CELL), int(2 * hy // CELL)
        self.levels = [lv["id"] for lv in data["elevation"]]
        nx, ny = self.nx, self.ny
        self.cover = [[0] * ny for _ in range(nx)]  # bit per level id
        self.ramp = [
            [-1] * ny for _ in range(nx)
        ]  # ramp index of a walkable strip cell
        self.block = [[False] * ny for _ in range(nx)]
        for region in data["regions"]:
            bit = 1 << self.levels.index(region["level"])
            for i, j in self.cells_in(region["poly"]):
                self.cover[i][j] |= bit
        # A ramp piece's footprint is closed by its parapets except along the walkable strip.
        for index, ramp in enumerate(data["ramps"]):
            for footprint in ramp["footprints"]:
                for i, j in self.cells_in(footprint):
                    self.block[i][j] = True
            for strip in ramp["strips"]:
                for i, j in self.cells_in(strip):
                    self.block[i][j] = False
                    self.ramp[i][j] = index
                    self.cover[i][j] |= 0  # ramp cells keep whatever region covers them
        for blocker in data["blockers"]:
            for i, j in self.cells_in(blocker["poly"]):
                self.block[i][j] = True
        if breach_open:
            for i, j in self.cells_in(data["proposals"]["breach"]["poly"]):
                self.block[i][j] = False
                self.cover[i][j] |= 1 << self.levels.index("L0")
        self.walk = [
            [
                (self.cover[i][j] != 0 or self.ramp[i][j] >= 0) and not self.block[i][j]
                for j in range(ny)
            ]
            for i in range(nx)
        ]
        # lvl: level index, -1 on a ramp strip (joins any level), -2 off the map.
        self.lvl = [[-2] * ny for _ in range(nx)]
        for i in range(nx):
            for j in range(ny):
                if self.ramp[i][j] >= 0:
                    self.lvl[i][j] = -1
                elif self.cover[i][j]:
                    self.lvl[i][j] = (
                        self.cover[i][j] & -self.cover[i][j]
                    ).bit_length() - 1
        # Two walkable cells of different levels may never be neighbours: the boundary is a cliff.
        self.boundary = [[False] * ny for _ in range(nx)]
        for i in range(nx):
            for j in range(ny):
                a = self.lvl[i][j]
                if a >= 0 and self.walk[i][j]:
                    for di, dj in (
                        (1, 0),
                        (-1, 0),
                        (0, 1),
                        (0, -1),
                        (1, 1),
                        (1, -1),
                        (-1, 1),
                        (-1, -1),
                    ):
                        ni, nj = i + di, j + dj
                        if (
                            self.in_range(ni, nj)
                            and self.walk[ni][nj]
                            and self.lvl[ni][nj] >= 0
                            and self.lvl[ni][nj] != a
                        ):
                            self.boundary[i][j] = True
        self.nav = [
            [
                self.walk[i][j]
                and not self.boundary[i][j]
                and all(
                    self.is_walk(i + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1)
                )
                for j in range(ny)
            ]
            for i in range(nx)
        ]

    def centre(self, i: int, j: int) -> WorldPoint:
        return -self.hx + (i + 0.5) * CELL, -self.hy + (j + 0.5) * CELL

    def cell(self, x: float, y: float) -> Cell:
        return int((x + self.hx) // CELL), int((y + self.hy) // CELL)

    def in_range(self, i: int, j: int) -> bool:
        return 0 <= i < self.nx and 0 <= j < self.ny

    def is_walk(self, i: int, j: int) -> bool:
        return self.in_range(i, j) and self.walk[i][j]

    def cells_in(self, poly: Polygon) -> Iterator[Cell]:
        x0, x1, y0, y1 = bbox(poly)
        i0, j0 = self.cell(x0, y0)
        i1, j1 = self.cell(x1, y1)
        for i in range(max(0, i0), min(self.nx - 1, i1) + 1):
            for j in range(max(0, j0), min(self.ny - 1, j1) + 1):
                x, y = self.centre(i, j)
                # Sample a hair outward from the arena centre so rot180 partners get mirrored samples and polygon
                # edges that pass through cell centres resolve symmetrically.
                if inside(
                    x + math.copysign(0.011, x), y + math.copysign(0.017, y), poly
                ):
                    yield i, j

    def level_of(self, i: int, j: int) -> str | None:
        bits = self.cover[i][j]
        for k, name in enumerate(self.levels):
            if bits & (1 << k):
                return name
        return None

    def nearest_nav(self, x: float, y: float, limit: int = 12) -> Cell:
        i0, j0 = self.cell(x, y)
        best = None
        for r in range(limit):
            for i in range(i0 - r, i0 + r + 1):
                for j in range(j0 - r, j0 + r + 1):
                    if self.in_range(i, j) and self.nav[i][j]:
                        d = math.hypot(
                            *(
                                a - b
                                for a, b in zip(self.centre(i, j), (x, y), strict=False)
                            )
                        )
                        if best is None or d < best[0]:
                            best = (d, i, j)
            if best:
                return best[1], best[2]
        raise RuntimeError(f"No navigable cell near ({x:.0f}, {y:.0f})")

    def edge_distance(self) -> DistanceGrid:
        """Approximate distance (cm) from each walkable cell to the nearest non-walkable cell (two-pass chamfer)."""
        big = 1e9
        d = [
            [
                0.0 if (not self.walk[i][j] or self.boundary[i][j]) else big
                for j in range(self.ny)
            ]
            for i in range(self.nx)
        ]
        fwd = [(-1, -1, 1.4142), (-1, 0, 1.0), (-1, 1, 1.4142), (0, -1, 1.0)]
        bwd = [(1, 1, 1.4142), (1, 0, 1.0), (1, -1, 1.4142), (0, 1, 1.0)]
        for i in range(self.nx):
            for j in range(self.ny):
                for a, b, w in fwd:
                    if self.in_range(i + a, j + b):
                        d[i][j] = min(d[i][j], d[i + a][j + b] + w)
                    else:
                        d[i][j] = min(d[i][j], 1.0 + 0 * w)
        for i in range(self.nx - 1, -1, -1):
            for j in range(self.ny - 1, -1, -1):
                for a, b, w in bwd:
                    if self.in_range(i + a, j + b):
                        d[i][j] = min(d[i][j], d[i + a][j + b] + w)
                    else:
                        d[i][j] = min(d[i][j], 1.0)
        return [[v * CELL for v in row] for row in d]

    # ------------------------------------------------------------------------- paths
    def dijkstra(
        self, source: Cell, closed: set[Cell] | None = None
    ) -> tuple[DistanceGrid, Parents]:
        closed = closed or set()
        inf = float("inf")
        dist = [[inf] * self.ny for _ in range(self.nx)]
        parent: Parents = {}
        si, sj = source
        dist[si][sj] = 0.0
        heap = [(0.0, si, sj)]
        nav = self.nav
        while heap:
            d, i, j = heapq.heappop(heap)
            if d > dist[i][j]:
                continue
            for a, b, w in MOVES:
                ni, nj = i + a, j + b
                if (
                    0 <= ni < self.nx
                    and 0 <= nj < self.ny
                    and nav[ni][nj]
                    and (ni, nj) not in closed
                ):
                    # Knight moves must not clip a blocked corner.
                    if abs(a) == 2 and not (nav[i + a // 2][j] and nav[i + a // 2][nj]):
                        continue
                    if abs(b) == 2 and not (nav[i][j + b // 2] and nav[ni][j + b // 2]):
                        continue
                    if abs(a) == 1 and abs(b) == 1 and not (nav[ni][j] and nav[i][nj]):
                        continue
                    la, lb = self.lvl[i][j], self.lvl[ni][nj]
                    if la >= 0 and lb >= 0 and la != lb:
                        continue
                    nd = d + w * CELL
                    if nd < dist[ni][nj]:
                        dist[ni][nj] = nd
                        parent[(ni, nj)] = (i, j)
                        heapq.heappush(heap, (nd, ni, nj))
        return dist, parent

    def line_clear(self, a: Point, b: Point, closed: set[Cell]) -> bool:
        """Straight segment stays on navigable cells; a level boundary is a non-navigable cliff cell, so it cannot be crossed."""
        (ax, ay), (bx, by) = a, b
        n = max(1, int(math.hypot(bx - ax, by - ay) // (CELL / 2)))
        for k in range(n + 1):
            x, y = ax + (bx - ax) * k / n, ay + (by - ay) * k / n
            i, j = self.cell(x, y)
            if not (self.in_range(i, j) and self.nav[i][j]) or (i, j) in closed:
                return False
        return True

    def path(
        self,
        parent: Parents,
        source: Cell,
        target: Cell,
        closed: set[Cell] | None = None,
    ) -> list[WorldPoint]:
        """Cell chain from source to target, string-pulled into world points."""
        closed = closed or set()
        chain = [target]
        while chain[-1] != source:
            chain.append(parent[chain[-1]])
        chain.reverse()
        pts = [self.centre(*c) for c in chain]
        out = [pts[0]]
        anchor = 0
        while anchor < len(pts) - 1:
            far = anchor + 1
            for k in range(len(pts) - 1, anchor, -1):
                if self.line_clear(pts[anchor], pts[k], closed):
                    far = k
                    break
            out.append(pts[far])
            anchor = far
        return out


def path_length(pts: Sequence[Point]) -> float:
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in pairwise(pts))


# ----------------------------------------------------------------------------- analysis
class Analysis:
    def __init__(self, data: MapData) -> None:
        self.data = data
        self.errors: list[str] = []
        self.notes: list[str] = []
        self.const = data["constants"]
        self.speed = float(self.const["unit_speed_cm_s"])
        self.grid = Grid(data)
        self.hq = {h["id"]: h for h in data["headquarters"]}
        self.ramps = {r["id"]: r for r in data["ramps"]}
        self.regions = {r["id"]: r for r in data["regions"]}
        self.z = {lv["id"]: lv for lv in data["elevation"]}

    def err(self, message: str) -> None:
        self.errors.append(message)

    # ---- structure
    def check_levels(self) -> None:
        """Regions of different levels may touch (the boundary is a cliff) but never overlap."""
        g = self.grid
        overlap = 0
        for i in range(g.nx):
            for j in range(g.ny):
                bits = g.cover[i][j]
                if bits & (bits - 1):
                    overlap += 1
        if overlap:
            self.err(f"regions of different levels overlap on {overlap:d} cells")

    def check_grid(self) -> None:
        """Everything the terrain kit places must sit on its 400 cm grid (Build/GenerateTerrainKit.py)."""
        grid = self.data["grid"]
        cell = grid["cell"]
        half = cell // 2

        def on_line(v: float) -> bool:
            return (v - half) % cell == 0

        for region in self.data["regions"]:
            for x, y in region["poly"]:
                if not (on_line(x) and on_line(y)):
                    self.err(
                        f"region {region['id']!s} has a vertex ({x!s}, {y!s}) off the kit grid"
                    )
        for ramp in self.data["ramps"]:
            for piece in ramp["pieces"]:
                x, y = piece["centre"]
                if not (on_line(x) and on_line(y)):
                    self.err(
                        f"{ramp['id']!s} piece at ({x!s}, {y!s}) is not centred on a cell corner"
                    )
                if piece["yaw"] % 90:
                    self.err(
                        f"{ramp['id']!s} piece yaw {piece['yaw']!s} is not a multiple of 90"
                    )
            if (
                ramp["length"] != grid["ramp_run"]
                or ramp["lane_width"] != grid["ramp_walkable_width"]
            ):
                self.err(
                    f"{ramp['id']!s} does not match the kit ramp (run {grid['ramp_run']:d}, walkable {grid['ramp_walkable_width']:d})"
                )
        for blocker in self.data["blockers"]:
            for x, y in blocker["poly"]:
                if not (on_line(x) and on_line(y)):
                    self.err(f"blocker {blocker['id']!s} has a vertex off the kit grid")
        for tower in self.data["proposals"]["vision_points"]:
            x, y = tower["pos"]
            if x % cell or y % cell:
                self.err(f"{tower['id']!s} is not on a cell centre")

    def check_ramps(self) -> None:
        g = self.grid
        for ramp in self.data["ramps"]:
            rid = ramp["id"]
            ux, uy = ramp["foot"][0] - ramp["top"][0], ramp["foot"][1] - ramp["top"][1]
            norm = math.hypot(ux, uy)
            ux, uy = ux / norm, uy / norm
            for end, level, sign in (
                ("top", ramp["from_level"], -1),
                ("foot", ramp["to_level"], 1),
            ):
                found = False
                for lane in ramp["pieces"]:
                    # Probe 100 cm past the end of the ramp, on the lane centre line.
                    cx, cy = lane["centre"]
                    px = cx + ux * sign * (ramp["length"] / 2 + 100)
                    py = cy + uy * sign * (ramp["length"] / 2 + 100)
                    i, j = g.cell(px, py)
                    if g.in_range(i, j) and g.level_of(i, j) == level and g.walk[i][j]:
                        found = True
                if not found:
                    self.err(
                        f"{rid!s}: nothing walkable on level {level!s} beyond its {end!s}"
                    )
            height_keys: tuple[Literal["z_cm", "z_sc2_cm"], ...] = ("z_cm", "z_sc2_cm")
            for name in height_keys:
                rise = abs(
                    self.z[ramp["from_level"]][name] - self.z[ramp["to_level"]][name]
                )
                slope = math.degrees(math.atan2(rise, ramp["length"]))
                if name == "z_cm":
                    ramp["rise_z_cm"] = rise
                    ramp["slope_z_cm"] = slope
                else:
                    ramp["rise_z_sc2_cm"] = rise
                    ramp["slope_z_sc2_cm"] = slope
            if ramp["rise_z_sc2_cm"] != self.data["grid"]["step"]:
                self.err(
                    f"{rid!s}: SC2 rise {int(ramp['rise_z_sc2_cm']):d} is not one 300 cm step"
                )
            if ramp["slope_z_sc2_cm"] > 30:
                self.err(f"{rid!s}: kit ramp slope limit is 30 degrees")
            if ramp["lane_width"] < 3 * self.const["force_width"]:
                self.err(
                    f"{rid!s}: lane width {int(ramp['lane_width']):d} is under three force widths"
                )

    def territory_discs(self) -> list[tuple[str, Point, float, str]]:
        discs: list[tuple[str, Point, float, str]] = []
        for h in self.data["headquarters"]:
            discs.append(
                (h["id"], h["pos"], self.const["hq_territory_radius"], h["side"])
            )
        for s in self.data["sectors"]:
            discs.append(
                (
                    f"S{s['site_index'] + 1:d}",
                    s["pos"],
                    self.const["sector_territory_radius"],
                    s["side"],
                )
            )
        return discs

    def check_wall_off(self) -> None:
        """No ramp or named choke may sit inside a territory disc: buildings cannot plug it."""
        self.margins: list[tuple[str, str, float]] = []
        for name, pos, radius, _side in self.territory_discs():
            for ramp in self.data["ramps"]:
                d = min(poly_dist(pos[0], pos[1], fp) for fp in ramp["footprints"])
                self.margins.append((ramp["id"], name, d - radius))
                if d - radius < 0:
                    self.err(
                        f"{ramp['id']!s} lies inside the {radius:.0f} territory of {name!s} ({d - radius:.0f} cm)"
                    )
            for choke in self.data["chokes"]:
                half = choke["width"] / 2.0
                a = (
                    choke["center"][0] - choke["across"][0] * half,
                    choke["center"][1] - choke["across"][1] * half,
                )
                b = (
                    choke["center"][0] + choke["across"][0] * half,
                    choke["center"][1] + choke["across"][1] * half,
                )
                d = seg_dist(pos[0], pos[1], a, b)
                self.margins.append((choke["id"], name, d - radius))
                if d - radius < 0:
                    self.err(f"{choke['id']!s} lies inside the territory of {name!s}")

    def check_sectors(self) -> None:
        g = self.grid
        cap = self.const["capture_radius"]
        terr = self.const["sector_territory_radius"]
        self.sector_stats: dict[str, float] = {}
        for s in self.data["sectors"]:
            x, y = s["pos"]
            ci, cj = g.cell(x, y)
            level = g.level_of(ci, cj)
            if level != s["level"]:
                self.err(f"sector {s['name']!s} is on {level!s}, not {s['level']!s}")
                continue
            bad = 0
            total = 0
            usable = 0
            for i, j in g.cells_in(
                [
                    [x - terr, y - terr],
                    [x + terr, y - terr],
                    [x + terr, y + terr],
                    [x - terr, y + terr],
                ]
            ):
                cx, cy = g.centre(i, j)
                d = math.hypot(cx - x, cy - y)
                if d <= terr:
                    total += 1
                    same = (
                        g.walk[i][j] and g.level_of(i, j) == level and g.ramp[i][j] < 0
                    )
                    if same:
                        usable += 1
                    if d <= cap and not same:
                        bad += 1
            if bad:
                self.err(
                    f"sector {s['name']!s}: capture ring leaves its level ({bad:d} cells)"
                )
            self.sector_stats[s["id"]] = usable / float(total)
        for h in self.data["headquarters"]:
            x, y = h["pos"]
            radius = self.const["hq_territory_radius"]
            total = usable = 0
            for i, j in g.cells_in(
                [
                    [x - radius, y - radius],
                    [x + radius, y - radius],
                    [x + radius, y + radius],
                    [x - radius, y + radius],
                ]
            ):
                cx, cy = g.centre(i, j)
                if math.hypot(cx - x, cy - y) <= radius:
                    total += 1
                    if (
                        g.walk[i][j]
                        and g.level_of(i, j) == h["level"]
                        and g.ramp[i][j] < 0
                    ):
                        usable += 1
            self.sector_stats[h["id"]] = usable / float(total)
            if usable != total:
                self.err(
                    f"{h['id']!s} territory disc leaves its plateau ({usable:d}/{total:d} cells)"
                )

    def check_chokes(self) -> None:
        """A choke's width is twice the clearance (distance to the nearest non-walkable cell) at its centre."""
        g = self.grid
        edge = g.edge_distance()
        self.edge = edge
        self.choke_widths: dict[str, float] = {}
        for c in self.data["chokes"]:
            i, j = g.cell(*c["center"])
            measured = 2.0 * edge[i][j]
            self.choke_widths[c["id"]] = measured
            if abs(measured - c["width"]) > 150:
                self.err(
                    f"choke {c['id']!s} measures about {measured:.0f} cm across, file says {int(c['width']):d}"
                )

    def check_symmetry(self) -> None:
        g = self.grid
        bad = 0
        for i in range(g.nx):
            for j in range(g.ny):
                if g.walk[i][j] != g.walk[g.nx - 1 - i][g.ny - 1 - j] or g.level_of(
                    i, j
                ) != g.level_of(g.nx - 1 - i, g.ny - 1 - j):
                    bad += 1
        self.asymmetric_cells = bad
        if bad:
            self.err(f"layout is not rot180 symmetric ({bad:d} cells)")
        for s in self.data["sectors"]:
            partner = next(
                t for t in self.data["sectors"] if t["site_index"] == s["pair"]
            )
            if [-s["pos"][0], -s["pos"][1]] != partner["pos"] or partner["level"] != s[
                "level"
            ]:
                self.err(
                    f"sector {s['name']!s} and {partner['name']!s} are not rot180 partners"
                )

    # ---- routes
    def closed_cells(self, ramp_ids: Iterable[str]) -> set[Cell]:
        g = self.grid
        cells: set[Cell] = set()
        for rid in ramp_ids:
            for strip in self.ramps[rid]["strips"]:
                cells.update(g.cells_in(strip))
        return cells

    def measure(self) -> None:
        g = self.grid
        sources = {}
        for h in self.data["headquarters"]:
            sources[h["id"]] = g.nearest_nav(h["pos"][0], h["pos"][1])
        self.sources = sources
        self.dist: dict[str, DistanceGrid] = {}
        self.parent: dict[str, Parents] = {}
        for key, src in sources.items():
            self.dist[key], self.parent[key] = g.dijkstra(src)
        self.targets = {}
        for h in self.data["headquarters"]:
            self.targets[h["id"]] = sources[h["id"]]
        for s in self.data["sectors"]:
            self.targets[f"S{s['site_index'] + 1}"] = g.nearest_nav(
                s["pos"][0], s["pos"][1]
            )
        self.table: dict[str, dict[str, Route]] = {}
        for key in ("HQ_H", "HQ_J"):
            row: dict[str, Route] = {}
            for name, cell in self.targets.items():
                d = self.dist[key][cell[0]][cell[1]]
                if d == float("inf"):
                    self.err(f"{key!s} cannot reach {name!s}")
                    continue
                pts = g.path(self.parent[key], sources[key], cell)
                row[name] = (path_length(pts), pts)
            self.table[key] = row
        # Routes from the Bunker to the Cluster with one human entrance closed at a time.
        self.routes: dict[str, Route | None] = {}
        for label, closed_ids in (
            ("open", []),
            ("no_door", ["ramp_door_H"]),
            ("no_gate", ["ramp_gate_H"]),
            ("no_gate_no_door", ["ramp_gate_H", "ramp_door_H"]),
        ):
            closed = self.closed_cells(closed_ids)
            dist, parent = g.dijkstra(sources["HQ_H"], closed)
            t = sources["HQ_J"]
            if dist[t[0]][t[1]] == float("inf"):
                self.routes[label] = None
                continue
            pts = g.path(parent, sources["HQ_H"], t, closed)
            self.routes[label] = (path_length(pts), pts)
        # The optional breach, opened: the route that goes through the Slab's centre.
        if "proposals" in self.data and "breach" in self.data["proposals"]:
            g2 = Grid(self.data, breach_open=True)
            a2, b2, m2 = (
                g2.nearest_nav(self.hq["HQ_H"]["pos"][0], self.hq["HQ_H"]["pos"][1]),
                g2.nearest_nav(self.hq["HQ_J"]["pos"][0], self.hq["HQ_J"]["pos"][1]),
                g2.nearest_nav(0, 0),
            )
            _, par_a = g2.dijkstra(a2)
            _, par_b = g2.dijkstra(b2)
            first = g2.path(par_a, a2, m2)
            second = g2.path(par_b, b2, m2)
            self.routes["breach_open"] = (
                path_length(first) + path_length(second),
                first + second[::-1],
            )
        no_door = self.routes["no_door"]
        no_gate = self.routes["no_gate"]
        if not no_door or not no_gate:
            self.err("closing one human entrance disconnects the bases")
            return
        a, b = no_door[0], no_gate[0]
        self.route_gap = abs(a - b) / min(a, b)
        # Two attack routes must both exist and be within 3 % of each other.
        if self.route_gap > 0.03:
            self.err(f"attack routes differ by {100 * self.route_gap:.1f} %")
        if self.routes["no_gate_no_door"] is not None:
            self.err("the human terrace is reachable without the gate and the door")

    # ---- placement (2D rules of ValidateBuildingPlacement)
    def placement_ok(
        self,
        x: float,
        y: float,
        radius: float,
        home: Point,
        hostile: Point,
        zone: Point,
        zone_radius: float,
        placed: Sequence[tuple[float, float, float]] = (),
    ) -> bool:
        """The 2D checks of ACommandGameState::ValidateBuildingPlacement for a building centred at (x, y).

        home / hostile are the placing team's and the other team's HQ positions; (zone, zone_radius) is the HQ or
        sector territory being built in; `placed` lists (x, y, radius) of existing buildings. The 3D checks
        (collision overlap, nav projection within 110 cm) become: every sample point is walkable ground of the same
        level and at least 60 cm from an edge, and never on a bare ramp.
        """
        g = self.grid
        hx, hy = self.data["arena"]["half_extent"]
        margin = self.data["arena"]["placement_margin"]
        if abs(x) > hx - margin or abs(y) > hy - margin:
            return False
        if (
            math.hypot(x - hostile[0], y - hostile[1])
            <= self.const["hq_exclusion_radius"] + radius
        ):
            return False
        if math.hypot(x - zone[0], y - zone[1]) > zone_radius - radius:
            return False
        if (
            math.hypot(x - home[0], y - home[1]) <= radius + 210
            or math.hypot(x - hostile[0], y - hostile[1]) <= radius + 210
        ):
            return False
        for bx, by, br in placed:
            if math.hypot(x - bx, y - by) <= radius + br + 55:
                return False
        i, j = g.cell(x, y)
        if not g.in_range(i, j) or g.ramp[i][j] >= 0:
            return False
        level = g.level_of(i, j)
        for a, b in (
            (0, 0),
            (1, 0),
            (-1, 0),
            (0, 1),
            (0, -1),
            (0.707, 0.707),
            (-0.707, 0.707),
            (0.707, -0.707),
            (-0.707, -0.707),
        ):
            si, sj = g.cell(x + a * (radius + 65), y + b * (radius + 65))
            if (
                not (
                    g.in_range(si, sj)
                    and g.walk[si][sj]
                    and g.ramp[si][sj] < 0
                    and g.level_of(si, sj) == level
                )
                or self.edge[si][sj] < 60
            ):
                return False
        return True

    def placement(self) -> None:
        g = self.grid
        friendly = self.hq["HQ_H"]["pos"]
        enemy = self.hq["HQ_J"]["pos"]
        hq_r = self.const["hq_territory_radius"]
        sec_r = self.const["sector_territory_radius"]
        out: dict[tuple[str, str], list[WorldPoint]] = {}
        for kind, radius in self.const["footprint_radius"].items():
            if kind == "outpost":
                continue
            zones: list[tuple[str, list[float], float]] = [("HQ_H", friendly, hq_r)] + [
                (f"S{s['site_index'] + 1:d}", s["pos"], sec_r)
                for s in self.data["sectors"]
                if s["side"] == "H"
            ]
            for zone, centre, zr in zones:
                cells = []
                for i, j in g.cells_in(
                    [
                        [centre[0] - zr, centre[1] - zr],
                        [centre[0] + zr, centre[1] - zr],
                        [centre[0] + zr, centre[1] + zr],
                        [centre[0] - zr, centre[1] + zr],
                    ]
                ):
                    x, y = g.centre(i, j)
                    if self.placement_ok(x, y, radius, friendly, enemy, centre, zr):
                        cells.append((x, y))
                out[(kind, zone)] = cells
        self.placements = out
        # Greedy packing: hard rule spacing (footprints + 55) and a practical spacing that keeps an exit lane open.
        self.packing: dict[tuple[str, str, str], list[WorldPoint]] = {}
        for (kind, zone), cells in out.items():
            radius = self.const["footprint_radius"][kind]
            for label, spacing in (
                ("hard", 2 * radius + 55),
                ("practical", 2 * radius + 300),
            ):
                chosen: list[WorldPoint] = []
                for x, y in sorted(cells, key=lambda c: (round(c[0] / 50), c[1])):
                    if all(math.hypot(x - a, y - b) >= spacing for a, b in chosen):
                        chosen.append((x, y))
                self.packing[(kind, zone, label)] = chosen
        # Pocket halves of the Bunker disc.
        self.pockets: dict[tuple[str, str], tuple[int, int]] = {}
        for pocket in self.data["build_pockets"]:
            px, py = pocket["half_plane"]["point"]
            kx, ky = pocket["half_plane"]["keep"]
            for kind in ("barracks", "workshop"):
                cells = [
                    c
                    for c in out[(kind, "HQ_H")]
                    if (c[0] - px) * kx + (c[1] - py) * ky > 0
                ]
                radius = self.const["footprint_radius"][kind]
                chosen = []
                for x, y in sorted(cells, key=lambda c: (round(c[0] / 50), c[1])):
                    if all(
                        math.hypot(x - a, y - b) >= 2 * radius + 300 for a, b in chosen
                    ):
                        chosen.append((x, y))
                self.pockets[(pocket["id"], kind)] = (len(cells), len(chosen))

    def check_bays(self) -> None:
        """Authored bays must pass the placement rules, sit in their pocket half and leave every neighbour 450+ cm."""
        friendly = self.hq["HQ_H"]["pos"]
        enemy = self.hq["HQ_J"]["pos"]
        radius = self.const["footprint_radius"]["barracks"]
        allbays = []
        for pocket in self.data["build_pockets"]:
            px, py = pocket["half_plane"]["point"]
            kx, ky = pocket["half_plane"]["keep"]
            for bay in pocket.get("bays", []):
                x, y = bay["pos"]
                if not self.placement_ok(
                    x,
                    y,
                    radius,
                    friendly,
                    enemy,
                    friendly,
                    self.const["hq_territory_radius"],
                ):
                    self.err(f"bay {bay['id']!s} fails the placement rules")
                if (x - px) * kx + (y - py) * ky <= 0:
                    self.err(f"bay {bay['id']!s} is in the wrong pocket half")
                allbays.append(bay)
        self.bay_min_gap = min(
            [
                math.hypot(a["pos"][0] - b["pos"][0], a["pos"][1] - b["pos"][1])
                for k, a in enumerate(allbays)
                for b in allbays[k + 1 :]
            ]
            or [0]
        )
        if self.bay_min_gap < 450:
            self.err(f"two bays are only {self.bay_min_gap:.0f} cm apart")

    def jev_build_check(self) -> None:
        """Replay AEnemyCommander::BuildNear: 4 rings (360 + 150 k) x 12 directions around the Cluster, then outposts."""
        enemy = self.hq["HQ_J"]["pos"]
        friendly = self.hq["HQ_H"]["pos"]
        rings = [360 + 150 * k for k in range(4)]
        fp = self.const["footprint_radius"]
        placed: list[tuple[float, float, float]] = []
        self.jev_builds: list[tuple[str, WorldPoint | None]] = []
        for kind in ("barracks", "barracks", "barracks", "workshop"):
            spot = None
            for ring in rings:
                for d in range(12):
                    ang = d * math.pi / 6
                    x, y = (
                        enemy[0] + math.cos(ang) * ring,
                        enemy[1] + math.sin(ang) * ring,
                    )
                    if self.placement_ok(
                        x,
                        y,
                        fp[kind],
                        enemy,
                        friendly,
                        enemy,
                        self.const["hq_territory_radius"],
                        placed,
                    ):
                        spot = (x, y)
                        break
                if spot:
                    break
            self.jev_builds.append((kind, spot))
            if spot:
                placed.append((spot[0], spot[1], fp[kind]))
        for kind, spot in self.jev_builds:
            if spot is None:
                self.err(
                    f"JEV cannot place its {kind!s} inside the Cluster ring (BuildNear candidates)"
                )
        # Outposts: any sector, first valid ring candidate.
        self.jev_outposts: dict[str, WorldPoint | None] = {}
        for s in self.data["sectors"]:
            spot = None
            for ring in rings:
                for d in range(12):
                    ang = d * math.pi / 6
                    x, y = (
                        s["pos"][0] + math.cos(ang) * ring,
                        s["pos"][1] + math.sin(ang) * ring,
                    )
                    if self.placement_ok(
                        x,
                        y,
                        fp["outpost"],
                        enemy,
                        friendly,
                        s["pos"],
                        self.const["sector_territory_radius"],
                    ):
                        spot = (x, y)
                        break
                if spot:
                    break
            self.jev_outposts[f"S{s['site_index'] + 1:d}"] = spot
            if spot is None:
                self.err(f"JEV cannot place an outpost for {s['name']!s}")

    def check_elevation(self) -> None:
        """Height of buildable ground against ACommandGameState::ValidateBuildingPlacement's rules.

        1. The overlap box spans Location.Z + 10 .. + 120 (centre +65, half height 55), so a surface higher than
           Location.Z + 10 blocks the footprint; Location.Z is 0 for a click (CursorGround) and 5 for JEV (BuildNear).
        2. Nav samples must project within 110 cm of Location.Z.
        Phase A (`z_cm`) must satisfy both, with 5 cm and 15 cm margins (the navmesh cell height is about 10 cm).
        Phase B (`z_sc2_cm`) is reported, not enforced: it is what proposal P1 has to make legal.
        """
        tol = self.const["placement_z_tolerance"]
        box = self.const["placement_overlap_box"]
        top = box["centre_z_offset"] - box["half_z"]
        self.phase_b_violations: list[str] = []
        for lv in self.data["elevation"]:
            for key, enforce in (("z_cm", True), ("z_sc2_cm", False)):
                for ref_name in ("click_plane_z", "jev_build_z"):
                    ref = self.const[ref_name]
                    z = lv[cast(HeightKey, key)]
                    bad = None
                    if z > ref + top - 5:
                        bad = f"rises into the placement overlap box (z {int(ref):d})"
                    elif abs(z - ref) > tol - 15:
                        bad = f"is more than {int(tol - 15):d} cm from z {int(ref):d}"
                    if bad and enforce:
                        self.err(f"level {lv['id']!s} at {int(z):+d} cm {bad!s}")
                    elif bad:
                        self.phase_b_violations.append(
                            f"{lv['id']!s} {int(z):+d} cm {bad!s}"
                        )
        step = self.const["character_max_step_height"]
        for ramp in self.data["ramps"]:
            rise = ramp["rise_z_cm"]
            if 0 < rise <= step:
                self.err(
                    f"{ramp['id']!s}: a {int(rise):d} cm step is within the character's {int(step):d} cm step height"
                )

    def intruder_reach(self) -> None:
        """Distance from the Cluster to its terrace lip (JEV switches to Defend when humans stand within 1500)."""
        hq = self.hq["HQ_J"]["pos"]
        best = min(
            poly_dist(hq[0], hq[1], r["poly"])
            for r in self.data["regions"]
            if r["side"] == "J" and r["level"] == "L1"
        )
        self.intruder_edge = best
        if best > self.const["jev_intruder_radius"]:
            self.err(
                f"terrace lip is {best:.0f} cm from the Cluster; JEV's 1500 intruder radius does not reach it"
            )

    def run(self) -> bool:
        self.check_levels()
        self.check_grid()
        self.check_ramps()
        self.check_wall_off()
        self.check_sectors()
        self.check_chokes()
        self.check_symmetry()
        self.measure()
        self.edge = self.grid.edge_distance()
        self.check_elevation()
        self.placement()
        self.check_bays()
        self.jev_build_check()
        self.intruder_reach()
        return not self.errors

    # ---- text report
    def secs(self, cm: float) -> float:
        return cm / self.speed

    def report(self) -> str:
        d = self.data
        lines: list[str] = []
        add = lines.append
        hx, hy = d["arena"]["half_extent"]
        add(
            f"Arena {int(2 * hx):d} x {int(2 * hy):d} cm ({2 * hx / 100:.0f} x {2 * hy / 100:.0f} m), unit speed {self.speed:.0f} cm/s"
        )
        add(f"Symmetry mismatch cells: {self.asymmetric_cells:d}")
        add("")
        add("Walking distance from each Headquarters (cm, seconds at unit speed)")
        names_by_key = {f"S{s['site_index'] + 1:d}": s for s in d["sectors"]}
        add(
            f"{''!s:<4} {'sector'!s:<16} {'role'!s:<16} {'from Bunker'!s:>14} {'from Cluster'!s:>14}"
        )
        for key, s in names_by_key.items():
            a = cast(Route, self.table["HQ_H"].get(key))
            b = cast(Route, self.table["HQ_J"].get(key))
            add(
                f"{key!s:<4} {s['name']!s:<16} {s['role']!s:<16} {a[0]:7.0f} {self.secs(a[0]):5.1f}s {b[0]:7.0f} {self.secs(b[0]):5.1f}s"
            )
        a = self.table["HQ_H"]["HQ_J"]
        add(f"HQ to HQ: {a[0]:.0f} cm = {self.secs(a[0]):.1f} s (open)")
        add("")
        add(
            "JEV target score, AEnemyCommander::EvaluatePlan: (5 neutral or Machine-held, 3 human-held) - straight 2D distance / 1200"
        )
        jev = self.hq["HQ_J"]["pos"]
        ranked = []
        for s in d["sectors"]:
            dist = math.hypot(s["pos"][0] - jev[0], s["pos"][1] - jev[1])
            ranked.append((5.0 - dist / 1200.0, 3.0 - dist / 1200.0, dist, s))
        for neutral, human, dist, s in sorted(
            ranked, key=lambda r: (-r[0], r[3]["site_index"])
        ):
            add(
                f"  S{s['site_index'] + 1:d} {s['name']!s:<16} straight {dist:6.0f} cm   neutral {neutral:6.2f}   human-held {human:6.2f}"
            )
        add("")
        add("Attack routes (Bunker to Cluster, one human entrance closed)")
        for label in ("open", "no_door", "no_gate", "breach_open"):
            route = self.routes.get(label)
            if route:
                add(f"  {label!s:<12} {route[0]:7.0f} cm  {self.secs(route[0]):5.1f} s")
        add(
            f"  route gap {100 * self.route_gap:.2f} % (breach_open = through the opened Slab centre; a proposal)"
        )
        add("")
        add("Chokes")
        for c in d["chokes"]:
            add(
                f"  {c['id']!s:<16} declared {int(c['width']):5d}  measured {self.choke_widths[c['id']]:5.0f}"
            )
        add("")
        add("Ramps")
        for r in d["ramps"]:
            add(
                f"  {r['id']!s:<14} {r['lanes']:d} x {r['kit_piece']!s} ({int(r['lane_width']):d} wide lanes) x {int(r['length']):d} long  slope {r['slope_z_cm']:.1f} deg (Phase A)  {r['slope_z_sc2_cm']:.1f} deg (Phase B, rise {int(r['rise_z_sc2_cm']):d})"
            )
        add("")
        add(
            "Tightest ramp/choke clearance from any territory disc (cm beyond the disc edge)"
        )
        worst = sorted(self.margins, key=lambda m: m[2])[:6]
        for what, disc, m in worst:
            add(f"  {what!s:<16} {disc!s:<6} {m:6.0f}")
        add(
            f"Terrace edge to Cluster: {self.intruder_edge:.0f} cm (JEV intruder radius {int(self.const['jev_intruder_radius']):d})"
        )
        add("")
        add(
            "Placement coverage in 100 cm cells (1 cell = 1 m2) and greedy packing (hard / practical)"
        )
        for zone in ["HQ_H"] + [
            f"S{s['site_index'] + 1:d}" for s in d["sectors"] if s["side"] == "H"
        ]:
            row = []
            for kind in ("barracks", "workshop"):
                row.append(
                    f"{kind!s} {len(self.placements[kind, zone]):4d} m2  pack {len(self.packing[kind, zone, 'hard']):2d} / {len(self.packing[kind, zone, 'practical']):2d}"
                )
            add(f"  {zone!s:<5} {'   '.join(row)!s}")
        for (pid, kind), (cells, packed) in sorted(self.pockets.items()):
            add(f"  {pid!s:<11} {kind!s:<8} {cells:4d} m2  practical pack {packed:d}")
        add("")
        add(
            f"Build bays authored: {sum(len(p.get('bays', [])) for p in d['build_pockets']):d}, closest pair {self.bay_min_gap:.0f} cm"
        )
        add(
            f"""JEV BuildNear replay: {", ".join((f"{k!s} {('ok' if sp else 'FAIL')!s}" for k, sp in self.jev_builds))!s}"""
        )
        add(
            f"""JEV outpost candidates: {", ".join((f"{k!s} {('ok' if v else 'FAIL')!s}" for k, v in self.jev_outposts.items()))!s}"""
        )
        add(
            f"Phase B levels above today's buildable ceiling (proposal P1): {', '.join(sorted(set(v.split(' rises')[0].split(' is more')[0] for v in self.phase_b_violations))) or 'none'!s}"
        )
        add("")
        add(f"Errors: {('none' if not self.errors else '')!s}")
        for e in self.errors:
            add("  - " + e)
        return "\n".join(lines)


# ----------------------------------------------------------------------------- drawing
FONT = "DejaVu Sans"
LEVEL_FILL = {"L0": "#33475c", "L1": "#557391", "L2": "#93b0cc"}
CLIFF = "#090b0e"
HUMAN = "#3aa0ff"
MACHINE = "#ff5252"
NEUTRAL = "#f2c14e"
ROUTE_COLORS = {"no_door": "#ff9f1c", "no_gate": "#f051b5"}


def esc(text: object) -> str:
    return str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, data: MapData, an: Analysis) -> None:
        self.d = data
        self.a = an
        self.hx, self.hy = data["arena"]["half_extent"]
        self.map_px = 1400.0
        self.s = self.map_px / (2 * max(self.hx, self.hy))
        self.ml, self.mt = 120.0, 120.0
        self.panel_x = self.ml + 2 * self.hy * self.s + 50.0
        self.width = self.panel_x + 760.0
        self.height = self.mt + 2 * self.hx * self.s + 140.0
        self.out: list[str] = []

    def P(self, x: float, y: float) -> WorldPoint:
        return self.ml + (y + self.hy) * self.s, self.mt + (self.hx - x) * self.s

    def pts(self, poly: Polygon) -> str:
        return " ".join(
            f"{px:.1f},{py:.1f}" for px, py in (self.P(x, y) for x, y in poly)
        )

    def add(self, text: str) -> None:
        self.out.append(text)

    def text(
        self,
        x: float,
        y: float,
        text: object,
        size: int = 16,
        fill: str = "#e8eef5",
        anchor: str = "start",
        weight: str = "normal",
        italic: bool = False,
        opacity: float = 1.0,
        halo: bool = True,
    ) -> None:
        style = ' font-style="italic"' if italic else ""
        halo_attr = (
            f' stroke="#0a0d12" stroke-width="{size / 5.0:.1f}" stroke-linejoin="round" paint-order="stroke"'
            if halo
            else ""
        )
        self.add(
            f'<text x="{x:.1f}" y="{y:.1f}" font-family="{FONT!s}" font-size="{size:d}" fill="{fill!s}" text-anchor="{anchor!s}" font-weight="{weight!s}"{style!s} opacity="{opacity:.2f}"{halo_attr!s}>{esc(text)!s}</text>'
        )

    def world_text(
        self, x: float, y: float, text: object, **kw: Unpack[TextOptions]
    ) -> None:
        px, py = self.P(x, y)
        self.text(px, py, text, **kw)

    def draw(self) -> str:
        d = self.d
        s = self.s
        add = self.add
        add(
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{int(self.width):d}" height="{int(self.height):d}" viewBox="0 0 {int(self.width):d} {int(self.height):d}">'
        )
        add(
            "<defs>"
            '<pattern id="rim" width="12" height="12" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
            '<rect width="12" height="12" fill="#14171c"/><line x1="0" y1="0" x2="0" y2="12" stroke="#1e232b" stroke-width="4"/></pattern>'
            '<pattern id="slab" width="14" height="14" patternUnits="userSpaceOnUse" patternTransform="rotate(-45)">'
            '<rect width="14" height="14" fill="#1b2430"/><line x1="0" y1="0" x2="0" y2="14" stroke="#2c3b4d" stroke-width="5"/></pattern>'
            '<pattern id="plug" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
            '<rect width="10" height="10" fill="#3a2a1a"/><line x1="0" y1="0" x2="0" y2="10" stroke="#a5713a" stroke-width="3"/></pattern>'
            '<marker id="arrow" viewBox="0 0 10 10" refX="7" refY="5" markerWidth="4" markerHeight="4" orient="auto-start-reverse">'
            '<path d="M0,0 L10,5 L0,10 z" fill="context-stroke"/></marker>'
        )
        for ramp in d["ramps"]:
            t = self.P(*ramp["top"])
            f = self.P(*ramp["foot"])
            add(
                f'''<linearGradient id="g_{ramp["id"]!s}" gradientUnits="userSpaceOnUse" x1="{t[0]:.1f}" y1="{t[1]:.1f}" x2="{f[0]:.1f}" y2="{f[1]:.1f}"><stop offset="0" stop-color="{LEVEL_FILL[ramp["from_level"]]!s}"/><stop offset="1" stop-color="{LEVEL_FILL[ramp["to_level"]]!s}"/></linearGradient>'''
            )
        add("</defs>")
        add(
            f'<rect width="{int(self.width):d}" height="{int(self.height):d}" fill="#0d1014"/>'
        )
        x0, y0 = self.P(self.hx, -self.hy)
        add(
            f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{2 * self.hy * s:.1f}" height="{2 * self.hx * s:.1f}" fill="url(#rim)" stroke="#39414d" stroke-width="3"/>'
        )
        # Fills by level, then a dark cliff line and a light lip line on every region that stands above a neighbour.
        order = {lv["id"]: k for k, lv in enumerate(d["elevation"])}
        regions = sorted(d["regions"], key=lambda r: order[r["level"]])
        for region in regions:
            add(
                f'''<polygon points="{self.pts(region["poly"])!s}" fill="{LEVEL_FILL[region["level"]]!s}" stroke="#0b0e12" stroke-width="2" stroke-linejoin="round"/>'''
            )
        for region in regions:
            if order[region["level"]] > 0:
                add(
                    f'''<polygon points="{self.pts(region["poly"])!s}" fill="none" stroke="{CLIFF!s}" stroke-width="9" stroke-linejoin="round" opacity="0.95"/>'''
                )
                add(
                    f'''<polygon points="{self.pts(region["poly"])!s}" fill="none" stroke="#d9e4f0" stroke-width="1.6" stroke-linejoin="round" opacity="0.55" transform="translate(0 0)"/>'''
                )
        # HQ territory pockets
        for h in d["headquarters"]:
            if h["side"] != "H":
                continue
            cx, cy = self.P(*h["pos"])
            radius = self.const_r("hq_territory_radius") * s
            for k, pocket in enumerate(d["build_pockets"]):
                keep = pocket["half_plane"]["keep"]
                # Half disc on the +/-Y side of the HQ. Screen right is +Y.
                sign = keep[1]
                add(
                    f'''<path d="M {cx:.1f} {cy - radius:.1f} A {radius:.1f} {radius:.1f} 0 0 {(1 if sign > 0 else 0):d} {cx:.1f} {cy + radius:.1f} Z" fill="{("#7fd0ff" if k == 0 else "#ffd27f")!s}" opacity="0.28"/>'''
                )
        for _k, pocket in enumerate(d["build_pockets"]):
            for bay in pocket.get("bays", []):
                bx, by = self.P(*bay["pos"])
                half = 125 * s
                add(
                    f'''<rect x="{bx - half:.1f}" y="{by - half:.1f}" width="{2 * half:.1f}" height="{2 * half:.1f}" fill="none" stroke="{"#ffffff"!s}" stroke-width="1.6" stroke-dasharray="3 2"/>'''
                )
                self.text(bx, by + 4, bay["id"], 10, "#ffffff", "middle", halo=False)
        # Slab and proposals
        for b in d["blockers"]:
            add(
                f'''<polygon points="{self.pts(b["poly"])!s}" fill="url(#slab)" stroke="#67809b" stroke-width="3"/>'''
            )
        br = d["proposals"]["breach"]
        add(
            f'''<polygon points="{self.pts(br["poly"])!s}" fill="none" stroke="#a5713a" stroke-width="2" stroke-dasharray="8 6"/>'''
        )
        for plug in br["plugs"]:
            add(
                f'''<polygon points="{self.pts(plug["poly"])!s}" fill="url(#plug)" stroke="#d99a5b" stroke-width="2" stroke-dasharray="6 4" opacity="0.9"/>'''
            )
        # Grid
        for k in range(-int(self.hx // 1000), int(self.hx // 1000) + 1):
            px0, py0 = self.P(k * 1000, -self.hy)
            px1, _ = self.P(k * 1000, self.hy)
            major = k % 5 == 0
            add(
                f'<line x1="{px0:.1f}" y1="{py0:.1f}" x2="{px1:.1f}" y2="{py0:.1f}" stroke="#ffffff" stroke-width="{(1.4 if major else 0.7)!s}" opacity="{(0.22 if major else 0.09)!s}"/>'
            )
            if k % 2 == 0:
                self.text(
                    self.ml - 8,
                    py0 + 5,
                    f"{k * 10:+d}",
                    14,
                    "#9aa7b6",
                    "end",
                    halo=False,
                )
        for k in range(-int(self.hy // 1000), int(self.hy // 1000) + 1):
            px0, py0 = self.P(self.hx, k * 1000)
            _, py1 = self.P(-self.hx, k * 1000)
            major = k % 5 == 0
            add(
                f'<line x1="{px0:.1f}" y1="{py0:.1f}" x2="{px0:.1f}" y2="{py1:.1f}" stroke="#ffffff" stroke-width="{(1.4 if major else 0.7)!s}" opacity="{(0.22 if major else 0.09)!s}"/>'
            )
            if k % 2 == 0:
                self.text(
                    px0, py1 + 20, f"{k * 10:+d}", 14, "#9aa7b6", "middle", halo=False
                )
        self.text(
            12, self.mt - 22, "X (m)  N = +X, up", 14, "#9aa7b6", "start", halo=False
        )
        self.text(
            self.ml + 2 * self.hy * s,
            self.mt + 2 * self.hx * s + 42,
            "Y (m)  E = +Y, right",
            14,
            "#9aa7b6",
            "end",
            halo=False,
        )
        # Ramps: each kit piece footprint (parapet outline) with its walkable strip on top.
        for ramp in d["ramps"]:
            tx, ty = ramp["top"]
            fx, fy = ramp["foot"]
            length = math.hypot(fx - tx, fy - ty)
            ux, uy = (fx - tx) / length, (fy - ty) / length
            nx, ny = -uy, ux
            for footprint, strip, piece in zip(
                ramp["footprints"], ramp["strips"], ramp["pieces"], strict=False
            ):
                add(
                    f'<polygon points="{self.pts(footprint)!s}" fill="#161b22" stroke="#e8eef5" stroke-width="2"/>'
                )
                add(
                    f'''<polygon points="{self.pts(strip)!s}" fill="url(#g_{ramp["id"]!s})" stroke="none"/>'''
                )
                cx, cy = piece["centre"]
                lane_half = ramp["lane_width"] / 2.0
                for k in range(-3, 4):
                    px = cx + ux * k * 100
                    py = cy + uy * k * 100
                    a_ = self.P(px + nx * lane_half, py + ny * lane_half)
                    b_ = self.P(px - nx * lane_half, py - ny * lane_half)
                    add(
                        f'<line x1="{a_[0]:.1f}" y1="{a_[1]:.1f}" x2="{b_[0]:.1f}" y2="{b_[1]:.1f}" stroke="#0b0e12" stroke-width="1" opacity="0.4"/>'
                    )
                up0 = self.P(cx + ux * 260, cy + uy * 260)
                up1 = self.P(cx - ux * 260, cy - uy * 260)
                flat = ramp["rise_z_cm"] == 0
                add(
                    f'''<line x1="{up0[0]:.1f}" y1="{up0[1]:.1f}" x2="{up1[0]:.1f}" y2="{up1[1]:.1f}" stroke="#ffffff" stroke-width="3.5" marker-end="url(#arrow)"{(' marker-start="url(#arrow)"' if flat else "")!s} opacity="0.95"/>'''
                )
        # Territory rings and markers
        cap = self.const_r("capture_radius") * s
        terr = self.const_r("sector_territory_radius") * s
        for sec in d["sectors"]:
            cx, cy = self.P(*sec["pos"])
            col = HUMAN if sec["side"] == "H" else MACHINE
            add(
                f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{terr:.1f}" fill="none" stroke="{col!s}" stroke-width="1.6" stroke-dasharray="7 6" opacity="0.8"/>'
            )
            add(
                f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{cap:.1f}" fill="{NEUTRAL!s}" fill-opacity="0.22" stroke="{NEUTRAL!s}" stroke-width="3"/>'
            )
            add(
                f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="14" fill="{col!s}" stroke="#0a0d12" stroke-width="2"/>'
            )
            self.text(
                cx,
                cy + 5,
                str(sec["site_index"] + 1),
                16,
                "#ffffff",
                "middle",
                "bold",
                halo=False,
            )
            self.text(cx, cy - cap - 10, sec["name"], 17, "#ffffff", "middle", "bold")
            self.text(
                cx,
                cy + cap + 20,
                sec["role"].replace("_", " "),
                14,
                "#dfe7f1",
                "middle",
                italic=True,
            )
        for h in d["headquarters"]:
            cx, cy = self.P(*h["pos"])
            col = HUMAN if h["side"] == "H" else MACHINE
            half = 150 * s
            add(
                f'<rect x="{cx - half * 1.3:.1f}" y="{cy - half * 1.3:.1f}" width="{half * 2.6:.1f}" height="{half * 2.6:.1f}" fill="{col!s}" stroke="#ffffff" stroke-width="3"/>'
            )
            self.text(cx, cy + 6, "HQ", 15, "#ffffff", "middle", "bold", halo=False)
            self.text(
                cx, cy + half * 1.3 + 24, h["name"], 18, "#ffffff", "middle", "bold"
            )
            radius = self.const_r("hq_territory_radius") * s
            add(
                f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{radius:.1f}" fill="none" stroke="{col!s}" stroke-width="1.6" stroke-dasharray="7 6"/>'
            )
        for prop in d["proposals"]["per_player_start"]:
            cx, cy = self.P(*prop["pos"])
            add(
                f'<rect x="{cx - 7:.1f}" y="{cy - 7:.1f}" width="14" height="14" fill="none" stroke="{HUMAN!s}" stroke-width="2.5" stroke-dasharray="3 3"/>'
            )
            add(
                f'''<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{self.const_r("hq_territory_radius") * s:.1f}" fill="none" stroke="{HUMAN!s}" stroke-width="1.2" stroke-dasharray="2 5" opacity="0.8"/>'''
            )
        # Routes
        for label, col in ROUTE_COLORS.items():
            route = self.a.routes.get(label)
            if not route:
                continue
            pts = self.pts(route[1])
            add(
                f'<polyline points="{pts!s}" fill="none" stroke="#0a0d12" stroke-width="10" stroke-linejoin="round" stroke-linecap="round" opacity="0.6"/>'
            )
            add(
                f'<polyline points="{pts!s}" fill="none" stroke="{col!s}" stroke-width="5.5" stroke-linejoin="round" stroke-linecap="round" marker-mid="none" opacity="0.95"/>'
            )
            # Direction chevrons every ~1000 cm of path.
            total = 0.0
            nextmark = 700.0
            for (ax_, ay_), (bx_, by_) in pairwise(route[1]):
                seg = math.hypot(bx_ - ax_, by_ - ay_)
                while total + seg >= nextmark:
                    fraction = (nextmark - total) / seg
                    mx, my = ax_ + (bx_ - ax_) * fraction, ay_ + (by_ - ay_) * fraction
                    p0 = self.P(mx, my)
                    p1 = self.P(
                        mx + (bx_ - ax_) / seg * 40, my + (by_ - ay_) / seg * 40
                    )
                    add(
                        f'<line x1="{p0[0]:.1f}" y1="{p0[1]:.1f}" x2="{p1[0]:.1f}" y2="{p1[1]:.1f}" stroke="#0a0d12" stroke-width="5" marker-end="url(#arrow)"/>'
                    )
                    nextmark += 1200.0
                total += seg
        # JEV expansion walks (Cluster to its two nearest sectors)
        for key in ("S7", "S8"):
            row = self.a.table["HQ_J"].get(key)
            if row:
                pts = self.pts(row[1])
                add(
                    f'<polyline points="{pts!s}" fill="none" stroke="#7df9ff" stroke-width="3.5" stroke-dasharray="4 7" stroke-linecap="round" opacity="0.95"/>'
                )
        # Chokes
        for c in d["chokes"]:
            half = c["width"] / 2.0
            a_ = self.P(
                c["center"][0] - c["across"][0] * half,
                c["center"][1] - c["across"][1] * half,
            )
            b_ = self.P(
                c["center"][0] + c["across"][0] * half,
                c["center"][1] + c["across"][1] * half,
            )
            add(
                f'<line x1="{a_[0]:.1f}" y1="{a_[1]:.1f}" x2="{b_[0]:.1f}" y2="{b_[1]:.1f}" stroke="#ffffff" stroke-width="2" stroke-dasharray="3 3" opacity="0.9"/>'
            )
            for e in (a_, b_):
                add(f'<circle cx="{e[0]:.1f}" cy="{e[1]:.1f}" r="3.5" fill="#ffffff"/>')
            mx, my = (a_[0] + b_[0]) / 2, (a_[1] + b_[1]) / 2
            side = 1 if c["center"][1] < 0 else -1
            offx, offy = (24 * side, 5) if c["across"][1] == 0 else (0, -12)
            anchor = (
                ("start" if side > 0 else "end") if c["across"][1] == 0 else "middle"
            )
            if c["name"]:
                self.text(
                    mx + offx,
                    my + offy,
                    c["name"]
                    if "lanes" in c["name"]
                    else f"{c['name']!s} {int(c['width']):d}",
                    14,
                    "#ffffff",
                    anchor,
                    "bold",
                )
        # Vision proposals
        for v in d["proposals"]["vision_points"]:
            cx, cy = self.P(*v["pos"])
            add(
                f'''<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{v["radius"] * s:.1f}" fill="none" stroke="#b6f28a" stroke-width="1.2" stroke-dasharray="2 6" opacity="0.6"/>'''
            )
            add(
                f'<polygon points="{cx:.1f},{cy - 12:.1f} {cx + 12:.1f},{cy:.1f} {cx:.1f},{cy + 12:.1f} {cx - 12:.1f},{cy:.1f}" fill="none" stroke="#b6f28a" stroke-width="3"/>'
            )
        # Slab and region labels
        self.world_text(
            0,
            0,
            "DATA HALL 0 · THE SLAB",
            size=20,
            anchor="middle",
            weight="bold",
            fill="#a9c1da",
        )
        self.world_text(
            -300,
            0,
            "full-depth dam · breach = proposal",
            size=14,
            anchor="middle",
            italic=True,
            fill="#8ea6bf",
        )
        for region in d["regions"]:
            lv = self.a.z[region["level"]]
            top = region["name"].upper()
            sub = f"{region['level']!s}  {int(lv['z_cm']):+d} / {int(lv['z_sc2_cm']):+d} cm"
            spots = {
                "pass_W": (1500, -5450),
                "pass_E": (-1500, 5450),
                "terrace_H": (-5750, -2900),
                "terrace_J": (5750, 2900),
                "main_H": (-6900, -5800),
                "main_J": (8250, 5450),
                "pocket_SE": (-7600, 300),
                "pocket_NW": (7600, -300),
            }
            x, y = spots[region["id"]]
            dark = region["level"] == "L2"
            self.world_text(
                x,
                y,
                top,
                size=15,
                anchor="middle",
                weight="bold",
                fill="#0b0e12" if dark else "#e3ecf6",
                halo=not dark,
            )
            self.world_text(
                x - 260,
                y,
                sub,
                size=14,
                anchor="middle",
                italic=True,
                fill="#0b0e12" if dark else "#c9d6e4",
                halo=not dark,
            )
        self.world_text(
            1500 - 520,
            -5450,
            "open field 52 x 48 m",
            size=13,
            anchor="middle",
            italic=True,
            fill="#c2cfdd",
        )
        self.world_text(
            -1500 + 520,
            5450,
            "open field 52 x 48 m",
            size=13,
            anchor="middle",
            italic=True,
            fill="#c2cfdd",
        )
        self.world_text(
            -9350,
            0,
            "HUMAN FORWARD BASE (SOUTH)",
            size=22,
            anchor="middle",
            weight="bold",
            fill=HUMAN,
        )
        self.world_text(
            9150,
            0,
            "MACHINE CAMPUS (NORTH)",
            size=22,
            anchor="middle",
            weight="bold",
            fill=MACHINE,
        )
        self.world_text(
            -9350,
            5500,
            "rock rim · out of play",
            size=14,
            anchor="middle",
            italic=True,
            fill="#7d8896",
        )
        self.world_text(
            9150,
            -5500,
            "rock rim · out of play",
            size=14,
            anchor="middle",
            italic=True,
            fill="#7d8896",
        )
        self.scale_bar()
        self.title()
        self.panel()
        add("</svg>")
        return "\n".join(self.out)

    def const_r(self, key: RadiusKey) -> float:
        return float(self.a.const[key])

    def scale_bar(self) -> None:
        s = self.s
        x = self.ml
        y = self.mt + 2 * self.hx * s + 78
        self.text(x, y - 14, "SCALE", 14, "#9aa7b6", halo=False)
        for k in range(5):
            col = "#e8eef5" if k % 2 == 0 else "#39414d"
            self.add(
                f'<rect x="{x + k * 1000 * s:.1f}" y="{y:.1f}" width="{1000 * s:.1f}" height="12" fill="{col!s}" stroke="#e8eef5" stroke-width="1.5"/>'
            )
        for k in range(0, 6):
            self.text(
                x + k * 1000 * s,
                y + 32,
                f"{k * 10:d} m",
                14,
                "#c9d3df",
                "middle",
                halo=False,
            )
        # Time bar: distance walked in 10 s at unit speed.
        span = self.a.speed * 10
        y2 = y
        x2 = x + 6300 * s
        self.text(
            x2,
            y2 - 14,
            f"TRAVEL TIME  ({self.a.speed:.0f} cm/s)",
            14,
            "#9aa7b6",
            halo=False,
        )
        for k in range(4):
            col = "#ffd27f" if k % 2 == 0 else "#39414d"
            self.add(
                f'<rect x="{x2 + k * span * s:.1f}" y="{y2:.1f}" width="{span * s:.1f}" height="12" fill="{col!s}" stroke="#ffd27f" stroke-width="1.5"/>'
            )
        for k in range(0, 5):
            self.text(
                x2 + k * span * s,
                y2 + 32,
                f"{k * 10:d} s",
                14,
                "#ffe2ad",
                "middle",
                halo=False,
            )

    def title(self) -> None:
        d = self.d
        self.text(
            self.ml,
            52,
            d["title"].upper() + "  ·  1 human (or 2) vs JEV",
            34,
            "#ffffff",
            weight="bold",
            halo=False,
        )
        self.text(
            self.ml,
            84,
            f"Top-down layout plan · 200 x 200 m arena · rot180 symmetric · source Build/Maps/{d['name']!s}.json",
            17,
            "#9aa7b6",
            halo=False,
        )

    def panel(self) -> None:
        d, a = self.d, self.a
        x = self.panel_x
        y = self.mt - 10
        add = self.add
        self.text(
            x,
            y,
            "LEVELS (Phase A z / Phase B z, cm)",
            17,
            "#ffffff",
            weight="bold",
            halo=False,
        )
        y += 12
        for lv in reversed(d["elevation"]):
            y += 34
            add(
                f'''<rect x="{x:.1f}" y="{y - 18:.1f}" width="46" height="24" fill="{LEVEL_FILL[lv["id"]]!s}" stroke="#e8eef5" stroke-width="1.5"/>'''
            )
            self.text(
                x + 60,
                y,
                f"{lv['id']!s} {lv['name']!s}: {int(lv['z_cm']):+d} / {int(lv['z_sc2_cm']):+d} cm  ({lv['note']!s})",
                16,
                "#dfe7f1",
                halo=False,
            )
        y += 34
        add(
            f'<rect x="{x:.1f}" y="{y - 18:.1f}" width="46" height="24" fill="{CLIFF!s}" stroke="#e8eef5" stroke-width="1.5"/>'
        )
        self.text(
            x + 60,
            y,
            "Cliff line: Cliff_* pieces on the higher level's edge",
            16,
            "#dfe7f1",
            halo=False,
        )
        y += 44
        self.text(x, y, "SYMBOLS", 17, "#ffffff", weight="bold", halo=False)
        items = [
            (
                "circle",
                NEUTRAL,
                "Sector: capture ring 430 (solid), territory 1000 (dashed)",
            ),
            (
                "ring",
                HUMAN,
                "Human HQ, 900 territory disc; halves = pockets A/B with bays A1-B4",
            ),
            ("ring", MACHINE, "JEV HQ (The Cluster) and its 900 territory disc"),
            (
                "ramp",
                "#e8eef5",
                "Ramp_Wide piece (arrow uphill in Phase B); walkable lane 700 cm",
            ),
            (
                "route1",
                ROUTE_COLORS["no_door"],
                "Route A (orange): Cluster > West door > NW pocket > West Pass > North gate",
            ),
            (
                "route2",
                ROUTE_COLORS["no_gate"],
                "Route B (magenta): Cluster > South gate > East Pass > SE pocket > East door",
            ),
            ("dots", "#7df9ff", "JEV expansion walks (its two nearest sectors)"),
            ("diamond", "#b6f28a", "Vision tower (proposal, no code support yet)"),
            ("plug", "#d99a5b", "Destructible rock plug / breach (proposal)"),
            ("square", HUMAN, "Per-slot start HQ (proposal, dashed)"),
        ]
        for kind, col, text in items:
            y += 30
            if kind == "circle":
                add(
                    f'<circle cx="{x + 23:.1f}" cy="{y - 5:.1f}" r="9" fill="{col!s}" fill-opacity="0.3" stroke="{col!s}" stroke-width="3"/>'
                )
            elif kind == "ring":
                add(
                    f'<circle cx="{x + 23:.1f}" cy="{y - 5:.1f}" r="9" fill="none" stroke="{col!s}" stroke-width="2" stroke-dasharray="4 3"/>'
                )
            elif kind == "ramp":
                add(
                    f'''<rect x="{x + 6:.1f}" y="{y - 14:.1f}" width="34" height="16" fill="{LEVEL_FILL["L1"]!s}" stroke="#e8eef5" stroke-width="2"/>'''
                )
            elif kind in ("route1", "route2"):
                add(
                    f'<line x1="{x + 4:.1f}" y1="{y - 5:.1f}" x2="{x + 44:.1f}" y2="{y - 5:.1f}" stroke="{col!s}" stroke-width="6" stroke-linecap="round"/>'
                )
            elif kind == "dots":
                add(
                    f'<line x1="{x + 4:.1f}" y1="{y - 5:.1f}" x2="{x + 44:.1f}" y2="{y - 5:.1f}" stroke="{col!s}" stroke-width="4" stroke-dasharray="4 7" stroke-linecap="round"/>'
                )
            elif kind == "diamond":
                add(
                    f'<polygon points="{x + 23:.1f},{y - 16:.1f} {x + 34:.1f},{y - 5:.1f} {x + 23:.1f},{y + 6:.1f} {x + 12:.1f},{y - 5:.1f}" fill="none" stroke="{col!s}" stroke-width="3"/>'
                )
            elif kind == "plug":
                add(
                    f'<rect x="{x + 6:.1f}" y="{y - 14:.1f}" width="34" height="16" fill="url(#plug)" stroke="{col!s}" stroke-width="2"/>'
                )
            elif kind == "square":
                add(
                    f'<rect x="{x + 15:.1f}" y="{y - 14:.1f}" width="16" height="16" fill="none" stroke="{col!s}" stroke-width="2.5" stroke-dasharray="3 3"/>'
                )
            self.text(x + 60, y, text, 15, "#dfe7f1", halo=False)
        y += 46
        self.text(
            x,
            y,
            "KEY NUMBERS (measured from this file)",
            17,
            "#ffffff",
            weight="bold",
            halo=False,
        )
        rows = []
        hq = a.table["HQ_H"]["HQ_J"][0]
        rows.append(
            f"HQ to HQ walk: {hq / 100:.0f} m  =  {a.secs(hq):.0f} s at {a.speed:.0f} cm/s"
        )
        ra, rb = (
            cast(Route, a.routes["no_door"])[0],
            cast(Route, a.routes["no_gate"])[0],
        )
        rows.append(
            f"Route A (via North gate): {ra / 100:.0f} m, {a.secs(ra):.0f} s;  Route B (via East door): {rb / 100:.0f} m, {a.secs(rb):.0f} s"
        )
        rows.append(
            f"Route gap {100 * a.route_gap:.2f} %; opened breach (proposal) = third route, {cast(Route, a.routes['breach_open'])[0] / 100:.0f} m"
        )
        s1 = a.table["HQ_H"]["S1"][0]
        s2 = a.table["HQ_H"]["S2"][0]
        s3 = a.table["HQ_H"]["S3"][0]
        s4 = a.table["HQ_H"]["S4"][0]
        rows.append(
            f"Bunker to sectors 1-4: {a.secs(s1):.0f}, {a.secs(s2):.0f}, {a.secs(s3):.0f}, {a.secs(s4):.0f} s"
        )
        rows.append(
            "Cluster to sectors 8, 7, 6, 5: "
            + ", ".join(
                f"{a.secs(a.table['HQ_J'][f'S{k}'][0]):.0f}" for k in (8, 7, 6, 5)
            )
            + " s"
        )
        rows.append("Narrowest lane: one Ramp_Wide strip, 700 cm = 3.9 force widths")
        rows.append(
            f"Terrace edge to Cluster: {a.intruder_edge:.0f} cm (JEV intruder ring 1500)"
        )
        for row in rows:
            y += 27
            self.text(x, y, row, 15, "#dfe7f1", halo=False)
        y += 44
        self.text(
            x,
            y,
            "SECTORS (SiteIndex order = HUD SECTOR n)",
            17,
            "#ffffff",
            weight="bold",
            halo=False,
        )
        y += 28
        self.text(x, y, "#", 14, "#9aa7b6", halo=False)
        self.text(x + 34, y, "name", 14, "#9aa7b6", halo=False)
        self.text(x + 250, y, "role", 14, "#9aa7b6", halo=False)
        self.text(x + 470, y, "from Bunker", 14, "#9aa7b6", "start", halo=False)
        self.text(x + 610, y, "from Cluster", 14, "#9aa7b6", "start", halo=False)
        for sec in d["sectors"]:
            y += 26
            key = f"S{sec['site_index'] + 1:d}"
            col = HUMAN if sec["side"] == "H" else MACHINE
            add(f'<circle cx="{x + 8:.1f}" cy="{y - 5:.1f}" r="8" fill="{col!s}"/>')
            self.text(x + 34, y, sec["name"], 15, "#ffffff", halo=False)
            self.text(
                x + 250, y, sec["role"].replace("_", " "), 15, "#dfe7f1", halo=False
            )
            self.text(
                x + 470,
                y,
                f"{a.secs(a.table['HQ_H'][key][0]):.0f} s",
                15,
                "#dfe7f1",
                halo=False,
            )
            self.text(
                x + 610,
                y,
                f"{a.secs(a.table['HQ_J'][key][0]):.0f} s",
                15,
                "#dfe7f1",
                halo=False,
            )
            self.text(
                x + 8,
                y,
                str(sec["site_index"] + 1),
                12,
                "#ffffff",
                "middle",
                "bold",
                halo=False,
            )
        y += 46
        self.text(
            x,
            y,
            "RAMPS (identical on both sides)",
            17,
            "#ffffff",
            weight="bold",
            halo=False,
        )
        y += 28
        for column, head in (
            (0, "ramp"),
            (250, "kit pieces"),
            (450, "phase A slope (rise)"),
            (610, "SC2 slope (rise)"),
        ):
            self.text(x + column, y, head, 14, "#9aa7b6", halo=False)
        for r in d["ramps"]:
            if r["side"] != "H":
                continue
            y += 26
            self.text(x, y, r["name"].replace("Human ", ""), 15, "#ffffff", halo=False)
            self.text(
                x + 250,
                y,
                f"{r['lanes']:d} x {r['kit_piece'].replace('_Machine', '')!s}",
                15,
                "#dfe7f1",
                halo=False,
            )
            rise_a = abs(
                self.a.z[r["from_level"]]["z_cm"] - self.a.z[r["to_level"]]["z_cm"]
            )
            rise_b = abs(
                self.a.z[r["from_level"]]["z_sc2_cm"]
                - self.a.z[r["to_level"]]["z_sc2_cm"]
            )
            self.text(
                x + 450,
                y,
                f"{r['slope_z_cm']:.1f} deg ({int(rise_a):d} cm)",
                15,
                "#dfe7f1",
                halo=False,
            )
            self.text(
                x + 610,
                y,
                f"{r['slope_z_sc2_cm']:.1f} deg ({int(rise_b):d} cm)",
                15,
                "#dfe7f1",
                halo=False,
            )


def rasterise(svg_path: str, png_path: str, width: int) -> None:
    if shutil.which("rsvg-convert"):
        subprocess.run(
            ["rsvg-convert", "-w", str(width), "-o", png_path, svg_path], check=True
        )
    elif shutil.which("magick"):
        subprocess.run(["magick", "-density", "96", svg_path, png_path], check=True)
    else:
        raise RuntimeError("Install rsvg-convert or ImageMagick to rasterise the SVG")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "map",
        nargs="?",
        default=os.path.join(ROOT, "Build", "Maps", "AvailabilityZone.json"),
    )
    parser.add_argument("--out", help="PNG path (default Art/Maps/<Map>-layout.png)")
    parser.add_argument("--svg", help="also keep the SVG here")
    parser.add_argument(
        "--report", action="store_true", help="print the measured tables"
    )
    parser.add_argument("--quiet", action="store_true", help="print errors only")
    args = parser.parse_args()
    with open(args.map) as handle:
        data = cast(MapData, json.load(handle))
    an = Analysis(data)
    ok = an.run()
    if args.report:
        print(an.report())
    elif not args.quiet:
        print(f"checks: {('all passed' if ok else 'FAILED')!s}")
        for e in an.errors:
            print("  - " + e)
    if not args.report and args.quiet:
        for e in an.errors:
            print(e)
    out = args.out or os.path.join(ROOT, "Art", "Maps", data["name"] + "-layout.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    svg = Svg(data, an).draw()
    svg_path = args.svg or out[:-4] + ".svg.tmp"
    with open(svg_path, "w") as handle:
        handle.write(svg)
    try:
        rasterise(svg_path, out, int(Svg(data, an).width))
    finally:
        if not args.svg and os.path.exists(svg_path):
            os.remove(svg_path)
    print("wrote " + os.path.relpath(out, ROOT))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
