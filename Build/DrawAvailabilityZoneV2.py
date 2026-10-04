#!/usr/bin/env python3
"""Derive AvailabilityZoneV2.json from the country outlines and validate/render it.

    python3 Build/DrawAvailabilityZoneV2.py --derive
    python3 Build/DrawAvailabilityZoneV2.py [--out Art/Maps/AvailabilityZoneV2-layout.png]

Only --derive reads the Excalidraw source. Raster ownership (2 source pixels per
cell) reconciles the independently sampled, slightly overlapping country paths;
shared cell edges become IDENTICAL polygon edges, not approximate near-matches.
The nearest adjacent country owns the narrow unpainted gaps and the square arena
margin. Default validation uses only the resulting JSON (no sketch dependency).

Import region_errors, site_errors, topology, symmetry_errors and
tiling_mismatches for non-rendering rule checks. audit retains exhaustive
quarter-cell sampling and the original CLI report; walking never draws.
"""

import argparse
from collections import Counter, defaultdict, deque
from collections.abc import Callable, Iterable, Iterator, Sequence
from itertools import chain
import json
import math
from pathlib import Path
import subprocess
from typing import Any, NotRequired, TypedDict, cast

import MatchLayout
import TerrainPlan
from TerrainPlan import CELL
import TerrainWalk

Point = list[float]
Cell = tuple[int, int]
Polygon = list[Point]
Edge = tuple[tuple[float, ...], ...]


class SketchElement(TypedDict):
    type: str
    x: float
    y: float
    points: NotRequired[Polygon]
    width: NotRequired[float]
    height: NotRequired[float]


class SketchDocument(TypedDict):
    elements: list[SketchElement]


class Region(TypedDict):
    index: int
    name: str
    role: str
    home_team: int
    poly: Polygon
    anchor: list[float] | None
    neighbours: list[int]
    defend_posts: list[list[float]]
    trait: str | None


class Deposit(TypedDict):
    region: int
    pos: list[float]
    kind: str


class Headquarters(TypedDict):
    id: str
    team: int
    name: str
    pos: list[float]


class Blocker(TypedDict):
    id: str
    kind: str
    name: str
    poly: Polygon


class Arena(TypedDict):
    half_extent: list[int]
    placement_margin: int
    half_height: int


class BoundsVolume(TypedDict):
    centre: list[int]
    half_extent: list[int]


class Navigation(TypedDict):
    bounds_volume: BoundsVolume


class MapData(TypedDict):
    name: str
    title: str
    unreal_map: str
    schema: int
    units: str
    provenance: str
    symmetry: str
    arena: Arena
    headquarters: list[Headquarters]
    regions: list[Region]
    deposits: list[Deposit]
    blockers: list[Blocker]
    navigation: Navigation
    terrain: dict[str, Any]


ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Build/Maps/AvailabilityZoneV2.json"
SKETCH = ROOT / "Art/Maps/AvailabilityZone-v2-extractor-sites.excalidraw"
PNG = ROOT / "Art/Maps/AvailabilityZoneV2-layout.png"
SCALE = 20000 / 1140  # one source pixel in world centimetres
STEP = 2  # raster reconciliation resolution in source pixels
L = 570  # exact 200 x 200 m arena: 570 * 2 pixels
ORIGIN_X, ORIGIN_Y = 64, 102  # upper-left sketch corner of the arena square
WALK_CELL = 100  # world-centimetre walking grid
SPEED = 420  # source unit movement speed in cm/s
NAMES = (
    "Human Main",
    "Human Near",
    "West Cut",
    "Uplink",
    "North Ridge",
    "Relay Plant",
    "Interchange",
    "Switchback",
    "Power Yard",
    "South Outcrop",
    "East Spur",
    "Cooling",
    "Canal Walk",
    "Machine Near",
    "Machine Main",
)
ROLES = (
    "main",
    "natural",
    "tactical",
    "reward",
    "tactical",
    "reward",
    "tactical",
    "tactical",
    "reward",
    "tactical",
    "tactical",
    "reward",
    "tactical",
    "natural",
    "main",
)
# Source sketch coordinates: visually chosen well inside each country, clear of dark rocks.
ANCHORS = (
    None,
    (480, 895),
    (210, 805),
    (300, 495),
    (250, 240),
    (565, 290),
    (605, 575),
    (700, 785),
    (880, 1050),
    (510, 1130),
    (1000, 865),
    (1050, 630),
    (900, 455),
    (855, 275),
    None,
)
HQ_SITES = (
    (240, 1060),
    (1080, 200),
)  # leave >11 m from main deposits for future extractors


def world(point: Sequence[float]) -> list[float]:
    px, py = point
    return [
        round((ORIGIN_Y + L * STEP / 2 - py) * SCALE, 3),
        round((px - ORIGIN_X - L * STEP / 2) * SCALE, 3),
    ]


def inside(point: Sequence[float], poly: Sequence[Sequence[float]]) -> bool:
    x, y = point
    odd = False
    for a, b in zip(poly, chain(poly[1:], poly[:1]), strict=False):
        if (a[1] > y) != (b[1] > y) and x < a[0] + (y - a[1]) * (b[0] - a[0]) / (
            b[1] - a[1]
        ):
            odd = not odd
    return odd


def contains_point(point: Sequence[float], poly: Sequence[Sequence[float]]) -> bool:
    """Game containment includes polygon edges; tiling's inside() stays strict."""
    if len(poly) < 3:
        return False
    x, y = point
    odd = False
    for a, b in zip(poly, chain(poly[1:], poly[:1]), strict=False):
        dx, dy = b[0] - a[0], b[1] - a[1]
        ox, oy = x - a[0], y - a[1]
        length_squared = dx * dx + dy * dy
        dot = dx * ox + dy * oy
        if (
            length_squared > 0
            and abs(dx * oy - dy * ox) <= 1e-6 * math.sqrt(length_squared)
            and 0 <= dot <= length_squared
        ):
            return True
        if (a[1] > y) != (b[1] > y) and x < a[0] + (y - a[1]) * dx / dy:
            odd = not odd
    return odd


def distance_segment(
    point: Sequence[float], a: Sequence[float], b: Sequence[float]
) -> float:
    vx, vy = b[0] - a[0], b[1] - a[1]
    t = (
        max(
            0.0,
            min(
                1.0,
                ((point[0] - a[0]) * vx + (point[1] - a[1]) * vy) / (vx * vx + vy * vy),
            ),
        )
        if vx or vy
        else 0
    )
    return math.hypot(point[0] - a[0] - t * vx, point[1] - a[1] - t * vy)


def clearance(point: Sequence[float], poly: Sequence[Sequence[float]]) -> float:
    return min(
        distance_segment(point, a, b)
        for a, b in zip(poly, chain(poly[1:], poly[:1]), strict=False)
    )


def area(poly: Sequence[Sequence[float]]) -> float:
    return (
        sum(
            a[0] * b[1] - b[0] * a[1]
            for a, b in zip(poly, chain(poly[1:], poly[:1]), strict=False)
        )
        / 2
    )


def raster_countries(elements: list[SketchElement]) -> list[list[int]]:
    """Take the actual fifteen Excalidraw paths, preserving their irregular edges."""
    outlines = [
        [(elt["x"] + x, elt["y"] + y) for x, y in elt["points"]]
        for elt in elements[:15]
    ]
    assert len(outlines) == 15 and all(len(poly) >= 80 for poly in outlines)
    labels = [[-1] * L for _ in range(L)]
    overlaps = 0
    for k, poly in enumerate(outlines):
        lo_y = max(0, int((min(y for _, y in poly) - ORIGIN_Y) // STEP))
        hi_y = min(L - 1, int((max(y for _, y in poly) - ORIGIN_Y) // STEP))
        for j in range(lo_y, hi_y + 1):
            scan_y = ORIGIN_Y + (j + 0.5) * STEP
            xs = sorted(
                a[0] + (scan_y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
                for a, b in zip(poly, poly[1:] + poly[:1], strict=False)
                if (a[1] > scan_y) != (b[1] > scan_y)
            )
            for left, right in zip(xs[::2], xs[1::2], strict=False):
                lo = max(0, math.ceil((left - ORIGIN_X) / STEP - 0.5))
                hi = min(L - 1, math.ceil((right - ORIGIN_X) / STEP - 0.5) - 1)
                for i in range(lo, hi + 1):
                    prev = labels[j][i]
                    if prev >= 0:
                        overlaps += 1
                        p = (ORIGIN_X + (i + 0.5) * STEP, scan_y)
                        if clearance(p, outlines[prev]) >= clearance(p, poly):
                            continue
                    labels[j][i] = k
    # Unpainted slivers of independently drawn borders, and the thin peripheral
    # arena margin: propagate ownership outward from the real painted outlines.
    queue = deque((i, j) for j in range(L) for i in range(L) if labels[j][i] >= 0)
    uncovered = L * L - len(queue)
    while queue:
        i, j = queue.popleft()
        for di, dj in ((1, 0), (0, 1), (-1, 0), (0, -1)):
            a, b = i + di, j + dj
            if 0 <= a < L and 0 <= b < L and labels[b][a] == -1:
                labels[b][a] = labels[j][i]
                queue.append((a, b))
    # A raster contour is non-manifold when two cells of the same country meet
    # only diagonally. Two such single-pixel junctions occur in this sketch.
    diagonal_contacts = 0
    for j in range(L - 1):
        for i in range(L - 1):
            a, b = labels[j][i], labels[j][i + 1]
            c, d = labels[j + 1][i], labels[j + 1][i + 1]
            if a == d and a != b and a != c:
                labels[j + 1][i + 1] = c
                diagonal_contacts += 1
            elif b == c and b != a and b != d:
                labels[j + 1][i] = d
                diagonal_contacts += 1
    # Preserve the large real country, absorb only detached edge pixels into
    # their bordering neighbour (never silently discard a substantive island).
    slivers = 0
    for region in range(15):
        seen: set[Cell] = set()
        components: list[list[Cell]] = []
        for j in range(L):
            for i in range(L):
                if labels[j][i] != region or (i, j) in seen:
                    continue
                component = [(i, j)]
                seen.add((i, j))
                queue = deque(component)
                while queue:
                    x, y = queue.popleft()
                    for a, b in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                        if (
                            0 <= a < L
                            and 0 <= b < L
                            and labels[b][a] == region
                            and (a, b) not in seen
                        ):
                            seen.add((a, b))
                            component.append((a, b))
                            queue.append((a, b))
                components.append(component)
        largest = max(components, key=len)
        for component in components:
            if component is largest:
                continue
            if len(component) > 20:
                raise ValueError(
                    f"Country {region} has substantive detached component of {len(component)} cells"
                )
            for x, y in component:
                neighbours = [
                    labels[b][a]
                    for a, b in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                    if 0 <= a < L and 0 <= b < L and labels[b][a] != region
                ]
                labels[y][x] = Counter(neighbours).most_common(1)[0][0]
                slivers += 1
    if any(
        labels[j][i] == labels[j + 1][i + 1] != labels[j][i + 1]
        and labels[j][i] != labels[j + 1][i]
        for j in range(L - 1)
        for i in range(L - 1)
    ):
        raise ValueError("Unresolved diagonal country contact")
    print(
        f"Sketch contour cleanup: {diagonal_contacts} corner contacts, {slivers} detached edge cells"
    )
    print(
        f"Sketch reconciliation: {overlaps} overlapping, {uncovered} uncovered 2px cells; nearest painted country fills gaps/margin"
    )
    return labels


def cell_rings(labels: list[list[int]], region: int) -> list[list[Cell]]:
    # Directed cell edges have the owner's inside to their LEFT in sketch pixels.
    # Their orientation flips after the vertical world-axis inversion below.
    following: dict[Cell, Cell] = {}
    for j, row in enumerate(labels):
        for i, k in enumerate(row):
            if k != region:
                continue
            if j == 0 or labels[j - 1][i] != k:
                following[(i, j)] = (i + 1, j)
            if i == L - 1 or row[i + 1] != k:
                following[(i + 1, j)] = (i + 1, j + 1)
            if j == L - 1 or labels[j + 1][i] != k:
                following[(i + 1, j + 1)] = (i, j + 1)
            if i == 0 or row[i - 1] != k:
                following[(i, j + 1)] = (i, j)
    rings: list[list[Cell]] = []
    while following:
        first = next(iter(following))
        cur = first
        cell_chain: list[Cell] = []
        while True:
            cell_chain.append(cur)
            cur = following.pop(cur)
            if cur == first:
                break
        rings.append(cell_chain)
    return rings


def rock_poly(element: SketchElement) -> Polygon:
    path = [(element["x"] + x, element["y"] + y) for x, y in element["points"]]
    # Four-vertex oriented footprint enclosing the actual short, curved stroke.
    # Keep 4 sketch pixels (~70 cm) of rock thickness on either side.
    dx, dy = path[-1][0] - path[0][0], path[-1][1] - path[0][1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    vx, vy = -uy, ux
    along = [x * ux + y * uy for x, y in path]
    across = [x * vx + y * vy for x, y in path]
    lo_a, hi_a = min(along) - 4, max(along) + 4
    lo_b, hi_b = min(across) - 4, max(across) + 4
    poly = [
        (a * ux + b * vx, a * uy + b * vy)
        for a, b in ((lo_a, lo_b), (hi_a, lo_b), (hi_a, hi_b), (lo_a, hi_b))
    ]
    converted = [world(p) for p in poly]
    return converted if area(converted) > 0 else converted[::-1]


def derive() -> None:
    elements = cast(SketchDocument, json.loads(SKETCH.read_text()))["elements"]
    assert [e["type"] for e in elements[:15]] == ["line"] * 15
    # Posts are authored world data, not another sketch-derived/runtime placement.
    # Preserve hand edits when re-deriving the region outlines and deposit sites.
    previous = cast(MapData, json.loads(DATA.read_text()))
    authored_posts = {
        region["index"]: region["defend_posts"] for region in previous["regions"]
    }
    authored_traits = {
        region["index"]: region["trait"] for region in previous["regions"]
    }
    assert len([e for e in elements if e["type"] == "diamond"]) == 16
    labels = raster_countries(elements)
    # Simplify only after collecting bends from ALL rings. A corner on one
    # country's border must split its neighbour's otherwise straight edge.
    raw = [cell_rings(labels, k) for k in range(15)]
    bends: set[Cell] = set()
    for rings in raw:
        if len(rings) != 1:
            raise ValueError(
                f"Country has {len(rings)} components/holes; source needs reconciliation"
            )
        ring = rings[0]
        bends.update(
            p
            for n, p in enumerate(ring)
            if (ring[n - 1][0] - p[0]) * (ring[(n + 1) % len(ring)][1] - p[1])
            != (ring[n - 1][1] - p[1]) * (ring[(n + 1) % len(ring)][0] - p[0])
        )
    regions: list[Region] = []
    for k, rings in enumerate(raw):
        poly = [
            world((ORIGIN_X + i * STEP, ORIGIN_Y + j * STEP))
            for i, j in rings[0]
            if (i, j) in bends
        ]
        if area(poly) < 0:
            poly.reverse()
        anchor = ANCHORS[k]
        regions.append(
            Region(
                index=k,
                name=NAMES[k],
                role=ROLES[k],
                home_team=0 if k == 0 else 5 if k == 14 else -1,
                poly=poly,
                anchor=world(anchor) if anchor else None,
                neighbours=[],
                defend_posts=authored_posts[k],
                trait=authored_traits[k],
            )
        )
    # Shared-edge incidence is the source of truth for neighbour relations.
    edges: defaultdict[Edge, set[int]] = defaultdict(set)
    for region in regions:
        p = region["poly"]
        for a, b in zip(p, p[1:] + p[:1], strict=False):
            edges[tuple(sorted((tuple(a), tuple(b))))].add(region["index"])
    for owners in edges.values():
        if len(owners) == 2:
            owner_a, owner_b = sorted(owners)
            regions[owner_a]["neighbours"].append(owner_b)
            regions[owner_b]["neighbours"].append(owner_a)
    for r in regions:
        r["neighbours"] = sorted(set(r["neighbours"]))
    # Index deposits against their source country, not nearest label centre.
    deposits: list[Deposit] = []
    for elt in elements:
        if elt["type"] != "diamond":
            continue
        px, py = elt["x"] + elt["width"] / 2, elt["y"] + elt["height"] / 2
        i, j = int((px - ORIGIN_X) / STEP), int((py - ORIGIN_Y) / STEP)
        k = labels[j][i]
        deposits.append(
            Deposit(
                region=k,
                pos=world((px, py)),
                kind="rich" if ROLES[k] == "reward" else "normal",
            )
        )
    data: MapData = dict(
        name="AvailabilityZoneV2",
        title="Availability Zone V2",
        unreal_map="/Game/Maps/AvailabilityZoneV2",
        schema=2,
        units="Centimetres. X is minimap-up, Y is minimap-right, Z is up. Point = [x, y].",
        provenance="15 Excalidraw country outlines and 16 diamonds, 2 source-pixel ownership reconciliation; sketch-space arena x=64..1204 y=102..1242, world X=(672-sketch_y)*20000/1140, world Y=(sketch_x-634)*20000/1140. Peripheral unpainted margin/gaps belong to nearest painted region.",
        symmetry="asymmetric: drawn outlines, rock marks, deposits, and the seven tactical regions do not admit 180-degree rotation",
        arena=dict(half_extent=[10000, 10000], placement_margin=100, half_height=1000),
        headquarters=[
            dict(id="HQ_H", team=0, name="The Bunker", pos=world(HQ_SITES[0])),
            dict(id="HQ_J", team=5, name="The Cluster", pos=world(HQ_SITES[1])),
        ],
        regions=regions,
        deposits=deposits,
        blockers=[
            dict(
                id=f"rock_{n + 1}",
                kind="rock",
                name=f"Partial Rock Cover {n + 1}",
                poly=rock_poly(elt),
            )
            for n, elt in enumerate(elements[16:21])
        ],
        navigation=dict(
            bounds_volume=dict(centre=[0, 0, 0], half_extent=[10200, 10200, 1400])
        ),
        terrain=previous["terrain"],
    )
    # Closed borders (plateau cliffs without a ramp, rock walls) are not neighbours.
    reduced = TerrainPlan.Terrain(data).neighbours()
    for r in regions:
        r["neighbours"] = reduced[r["index"]]
    DATA.write_text(json.dumps(data, indent=2) + "\n")
    print(
        f"Derived {len(regions)} regions, {len(deposits)} deposits, {len(data['blockers'])} partial rock marks -> {DATA}"
    )


def region_errors(data: MapData) -> list[str]:
    """Check authored region identities, roles, winding, bounds and anchors."""
    errors: list[str] = []
    regions = data["regions"]
    if len(regions) != 15 or [r["index"] for r in regions] != list(range(15)):
        errors.append("Region indices must be 0..14")
    if len(data["deposits"]) != 16:
        errors.append("Exactly 16 sketch diamonds expected")
    if Counter(r["role"] for r in regions) != {
        "main": 2,
        "natural": 2,
        "reward": 4,
        "tactical": 7,
    }:
        errors.append("Region role counts differ from sketch")
    hx, hy = data["arena"]["half_extent"]
    for r in regions:
        p = r["poly"]
        if area(p) <= 0:
            errors.append(f"{r['index']} is not CCW")
        if not all(-hx <= x <= hx and -hy <= y <= hy for x, y in p):
            errors.append(f"{r['index']} extends beyond arena")
        if r["home_team"] != (0 if r["index"] == 0 else 5 if r["index"] == 14 else -1):
            errors.append(f"{r['index']} home team mismatch")
        if r["anchor"] is None:
            if r["role"] != "main":
                errors.append(f"{r['index']} missing anchor")
        elif r["role"] == "main" or not inside(r["anchor"], p):
            errors.append(f"{r['index']} invalid/outside anchor")
    return errors


def site_errors(data: MapData) -> list[str]:
    """Check HQs and buildable deposit footprints against countries and rocks."""
    errors: list[str] = []
    regions = data["regions"]
    for h in data["headquarters"]:
        main = regions[0 if h["team"] == 0 else 14]
        if not inside(h["pos"], main["poly"]):
            errors.append(f"HQ {h['id']} outside main")
    for d in data["deposits"]:
        if not inside(d["pos"], regions[d["region"]]["poly"]):
            errors.append(f"deposit {d} outside country")
        # An extractor resolves to the deposit position, then needs its complete
        # 95 cm half-extent inside the controlled region, not just its pivot.
        if any(
            not inside(
                (d["pos"][0] + dx, d["pos"][1] + dy), regions[d["region"]]["poly"]
            )
            for dx in (-95, 95)
            for dy in (-95, 95)
        ):
            errors.append(f"deposit extractor footprint crosses country border: {d}")
        if d["kind"] != (
            "rich" if regions[d["region"]]["role"] == "reward" else "normal"
        ):
            errors.append(f"deposit kind mismatch in region {d['region']}")
        if d["region"] in (0, 14):
            hq = next(
                h
                for h in data["headquarters"]
                if h["team"] == (0 if d["region"] == 0 else 5)
            )
            if math.dist(d["pos"], hq["pos"]) < 1100:
                errors.append(f"main deposit too close to HQ exclusion footprint: {d}")
        else:
            anchor = regions[d["region"]]["anchor"]
            if anchor is not None and math.dist(d["pos"], anchor) < 530:
                errors.append(f"deposit conflicts with capture anchor footprint: {d}")
    sites: list[tuple[str, list[float]]] = [
        (f"HQ {h['id']}", h["pos"]) for h in data["headquarters"]
    ]
    sites += [
        (f"anchor {r['index']}", r["anchor"])
        for r in regions
        if r["anchor"] is not None
    ]
    sites += [(f"deposit {n + 1}", d["pos"]) for n, d in enumerate(data["deposits"])]
    for name, pos in sites:
        for b in data["blockers"]:
            if inside(pos, b["poly"]) or clearance(pos, b["poly"]) < 90:
                errors.append(f"{name} within 90 cm of {b['id']}")
    return errors


def defend_post_errors(data: MapData) -> list[str]:
    """Validate authored posts against gameplay countries and the terrain: rocks, props, ramps, walls and levels."""
    return MatchLayout.defend_post_errors(
        data["regions"],
        data["arena"]["half_extent"],
        TerrainPlan.Terrain(data).clear_ground,
        data["arena"]["placement_margin"],
        lambda poly, point: contains_point(point, poly),
        headquarters=[hq["pos"] for hq in data["headquarters"]],
    )


def topology(data: MapData) -> tuple[int, float, list[str]]:
    """Check exact opposing shared edges, neighbour metadata and arena coverage."""
    errors: list[str] = []
    regions = data["regions"]
    hx, hy = data["arena"]["half_extent"]
    # EXACT topology: every internal edge has exactly two opposing users. Counts
    # are computed from polygon edges, not from the stated neighbour metadata.
    incidence: defaultdict[
        Edge, list[tuple[int, tuple[float, ...], tuple[float, ...]]]
    ] = defaultdict(list)
    for r in regions:
        p = r["poly"]
        for edge_a, edge_b in zip(p, p[1:] + p[:1], strict=False):
            key = tuple(sorted((tuple(edge_a), tuple(edge_b))))
            incidence[key].append((r["index"], tuple(edge_a), tuple(edge_b)))
    actual: list[set[int]] = [set() for _ in regions]
    boundary_length = 0.0
    for key, users in incidence.items():
        if len(users) == 1:
            point_a, point_b = key
            boundary_length += math.dist(point_a, point_b)
            if not (
                (abs(point_a[0]) == hx and abs(point_b[0]) == hx)
                or (abs(point_a[1]) == hy and abs(point_b[1]) == hy)
            ):
                errors.append(f"Unmatched interior edge: {key}")
        elif len(users) == 2:
            user_a, user_b = users
            if user_a[1] != user_b[2] or user_a[2] != user_b[1]:
                errors.append(f"Edge direction not opposed: {key}")
            actual[user_a[0]].add(user_b[0])
            actual[user_b[0]].add(user_a[0])
        else:
            errors.append(f"Edge has {len(users)} users: {key}")
    closed = TerrainPlan.closed_borders(data, dict(enumerate(actual)))
    for r in regions:
        n = r["index"]
        expected = sorted(k for k in actual[n] if tuple(sorted((n, k))) not in closed)
        if expected != sorted(r["neighbours"]):
            errors.append(
                f"Neighbour metadata mismatch for {n}: {r['neighbours']} vs {expected}"
            )
        if any(n not in regions[k]["neighbours"] for k in r["neighbours"]):
            errors.append(f"Neighbour incidence not symmetric for {n}")
    total_area = sum(area(r["poly"]) for r in regions)
    if (
        abs(total_area - 4 * hx * hy) > 0.01
        or abs(boundary_length - 4 * (hx + hy)) > 0.01
    ):
        errors.append(
            f"Not a perfect arena tiling: area {total_area}, boundary {boundary_length}"
        )
    return len(incidence), boundary_length, errors


def tiling_mismatches(
    regions: Sequence[Region], points: Iterable[Sequence[float]]
) -> int:
    """Count samples not owned by exactly one country."""
    return sum(sum(inside(point, r["poly"]) for r in regions) != 1 for point in points)


def quarter_cells(data: MapData) -> Iterator[tuple[float, float]]:
    """Independent 100cm raster; four offsets avoid border-only ties."""
    hx, hy = data["arena"]["half_extent"]
    for ix in range(-hx // WALK_CELL, hx // WALK_CELL):
        for iy in range(-hy // WALK_CELL, hy // WALK_CELL):
            for dx, dy in ((0.25, 0.25), (0.25, 0.75), (0.75, 0.25), (0.75, 0.75)):
                yield (ix + dx) * WALK_CELL, (iy + dy) * WALK_CELL


def symmetry_errors(data: MapData) -> list[str]:
    hq_h, hq_j = (h["pos"] for h in data["headquarters"])
    if math.dist(hq_h, [-hq_j[0], -hq_j[1]]) < 1:
        return [
            "HQ sites unexpectedly mirror despite asymmetric sketch; review symmetry claim"
        ]
    return []


def audit(data: MapData) -> None:
    errors = region_errors(data) + site_errors(data) + defend_post_errors(data)
    errors += TerrainWalk.terrain_errors(TerrainPlan.Terrain(data))
    regions = data["regions"]
    edge_count, boundary_length, edge_errors = topology(data)
    errors.extend(edge_errors)
    total_area = sum(area(r["poly"]) for r in regions)
    mismatches = tiling_mismatches(regions, quarter_cells(data))
    if mismatches:
        errors.append(
            f"100cm quarter-cell samples with gaps/overlap: {mismatches} / 160000"
        )
    print(
        f"Exact shared-edge topology: {edge_count} unique edges; {boundary_length:.2f} cm perimeter"
    )
    print(
        f"100cm quarter-cell tiling: {160000 - mismatches}/160000 singly owned; area {total_area / 1e10:.6f} km²"
    )
    for r in regions:
        print(
            f"  {r['index']:2} {r['name']:<17} {area(r['poly']) / 1e10:8.6f} km²  {area(r['poly']) / 1e4:10.4f} m²  neighbours {r['neighbours']}"
        )
    counts = Counter(d["region"] for d in data["deposits"])
    print("Deposits by region:", dict(sorted(counts.items())))
    hq_h, hq_j = (h["pos"] for h in data["headquarters"])
    miss = math.dist(hq_h, [-hq_j[0], -hq_j[1]])
    errors.extend(symmetry_errors(data))
    print(
        f"Sketch symmetry: NOT rot180; HQ mirror offset {miss:.1f} cm; main areas {area(regions[0]['poly']) / 1e4:.1f}/{area(regions[14]['poly']) / 1e4:.1f} m²"
    )
    if errors:
        for error in errors[:30]:
            print("FAIL:", error)
        if len(errors) > 30:
            print(f"... {len(errors) - 30} additional errors")
        raise ValueError(f"{len(errors)} validation errors")
    print(
        "VALID: exact shared topology, CCW/roles, grid tiling, symmetric neighbours, 13 anchors, 16 buildable deposits, HQs, rock clearance"
    )


def walking(data: MapData) -> None:
    """Height-aware 8-neighbour Dijkstra on 100cm cells: rocks, props, rock walls, cliffs and ramps."""
    terrain = TerrainPlan.Terrain(data)
    walker = TerrainWalk.Walker(terrain)
    home, enemy = (h["pos"] for h in data["headquarters"])
    from_home, from_enemy = walker.search(home)[0], walker.search(enemy)[0]

    def report(dist: Sequence[float], point: Sequence[float]) -> str:
        length = dist[walker.nearest(point)]
        if not math.isfinite(length):
            raise ValueError(f"Unreachable site {point}")
        return f"{length / 100:.1f} m / {length / SPEED:.1f} s"

    print(f"Walking HQ_H -> HQ_J: {report(from_home, enemy)}")
    for r in data["regions"]:
        if r["anchor"] is not None:
            print(
                f"  anchor {r['index']:2} {r['name']:<17}: H {report(from_home, r['anchor'])}; J {report(from_enemy, r['anchor'])}"
            )
    routes = TerrainWalk.route_report(terrain, walker)
    for route in routes:
        print(
            f"  route {route['name']:<14} {route['length'] / 100:6.1f} m  {route['seconds']:5.1f} s  "
            f"hazard {route['hazard'] / 100:3.0f} m ({route['damage']:.0f} damage)  open {route['open'] / 100:3.0f} m  "
            f"regions {route['regions']}"
        )
    errors = TerrainWalk.walk_errors(terrain, walker) + TerrainWalk.route_errors(
        terrain, routes
    )
    if errors:
        for error in errors:
            print("FAIL:", error)
        raise ValueError(f"{len(errors)} walking errors")


ROUTE_COLOURS = ("#1f6fd0", "#c0392b", "#14866d")


def terrain_layers(
    data: MapData, pt: Callable[[Sequence[float]], str]
) -> tuple[list[str], list[str]]:
    """SVG for ground under the rocks (apron, hazard, plateau, walls) and over them (props, ramps, routes)."""
    terrain = TerrainPlan.Terrain(data)

    def xy(point: Sequence[float]) -> tuple[float, float]:
        x, y = map(float, pt(point).split(","))
        return x, y

    def box(
        centre: Sequence[float], size_x: float, size_y: float, yaw: float, style: str
    ) -> str:
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        corners = [
            (centre[0] + lx * c - ly * s, centre[1] + lx * s + ly * c)
            for lx, ly in (
                (-size_x / 2, -size_y / 2),
                (size_x / 2, -size_y / 2),
                (size_x / 2, size_y / 2),
                (-size_x / 2, size_y / 2),
            )
        ]
        return f'<polygon points="{" ".join(pt(c) for c in corners)}" {style}/>'

    under = [
        '<defs><pattern id="hazard" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="4" height="8" fill="#c0392b"/><rect x="4" width="4" height="8" fill="#f4d03f"/></pattern></defs>'
    ]
    over: list[str] = []
    for i, j0, j1, _ in terrain.apron_runs():
        under.append(
            box(
                (i * CELL, (j0 + j1) / 2 * CELL),
                CELL,
                (j1 - j0 + 1) * CELL,
                0,
                'fill="#ffffff" fill-opacity="0.75"',
            )
        )
    under += [
        box(p, 360, 360, 0, 'fill="url(#hazard)"') for p in terrain.hazard_plates()
    ]
    for piece in terrain.solid_pieces():
        colour = "#3a3a3a" if piece["wall"] else "#8c6a4a"
        under.append(
            box(
                piece["centre"],
                CELL,
                CELL,
                0,
                f'fill="{colour}" fill-opacity="0.7" stroke="#5a4028" stroke-width="0.4"',
            )
        )
    for prop in terrain.props:
        size_x, size_y = TerrainPlan.PROP_SIZE[prop["kit"]]
        over.append(
            box(
                prop["pos"],
                size_x * 100,
                size_y * 100,
                prop["yaw"],
                'fill="#6b7b3a" stroke="#222" stroke-width="0.8"',
            )
        )
    for ramp in terrain.ramps:
        over.append(
            box(
                ramp["centre"],
                TerrainPlan.RAMP_RUN,
                TerrainPlan.RAMP_RUN,
                ramp["yaw"],
                'fill="#f08c00" fill-opacity="0.85" stroke="#7a3d00" stroke-width="2"',
            )
        )
        dx, dy = TerrainPlan.DIRS[ramp["yaw"]]
        (x0, y0), (x1, y1) = (
            xy((ramp["centre"][0] + k * dx * 300, ramp["centre"][1] + k * dy * 300))
            for k in (-1, 1)
        )
        over.append(
            f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="#fff" stroke-width="3"/><circle cx="{x1:.1f}" cy="{y1:.1f}" r="4" fill="#fff"/>'
        )
    walker = TerrainWalk.Walker(terrain)
    for route, colour in zip(
        TerrainWalk.route_report(terrain, walker), ROUTE_COLOURS, strict=False
    ):
        points = " ".join(pt(p) for p in route["points"][::4])
        over.append(
            f'<polyline points="{points}" fill="none" stroke="{colour}" stroke-width="4" stroke-opacity="0.8" stroke-linejoin="round"/>'
        )
    return under, over


def render(data: MapData, out: Path) -> None:
    hx, hy = data["arena"]["half_extent"]

    # SVG's horizontal screen axis is world Y, vertical screen axis is -world X.
    def pt(p: Sequence[float]) -> str:
        return f"{(p[1] + hy) / 20:.2f},{(hx - p[0]) / 20:.2f}"

    def polygon(poly: Sequence[Sequence[float]]) -> str:
        return " ".join(pt(p) for p in poly)

    fill = {
        "main": "#a5cae8",
        "natural": "#c9e4ee",
        "reward": "#f6d891",
        "tactical": "#c6dfc6",
    }
    under, over = terrain_layers(data, pt)
    out_svg = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1640" viewBox="-60 -65 1120 1150">',
        '<rect x="-60" y="-65" width="1120" height="1150" fill="#f1f3f0"/>',
        '<text x="500" y="-30" text-anchor="middle" fill="#273c48" font-size="22" font-family="sans-serif">AVAILABILITY ZONE V2 — regions, traits, plateaus, ramps and routes</text>',
        '<rect width="1000" height="1000" fill="#ffffff"/>',
    ]
    for r in data["regions"]:
        color = "#e7b6bc" if r["index"] in (13, 14) else fill[r["role"]]
        out_svg.append(
            f'<polygon points="{polygon(r["poly"])}" fill="{color}" stroke="#53686a" stroke-width="1.2" stroke-linejoin="round"/>'
        )
    out_svg += under
    for b in data["blockers"]:
        out_svg.append(
            f'<polygon points="{polygon(b["poly"])}" fill="#394844" stroke="#1b2928" stroke-width="2"/>'
        )
    out_svg += over
    for r in data["regions"]:
        if r["anchor"]:
            x, y = map(float, pt(r["anchor"]).split(","))
            out_svg.append(
                f'<circle cx="{x}" cy="{y}" r="8" fill="#f7fff2" stroke="#264d34" stroke-width="2"/><circle cx="{x}" cy="{y}" r="2.5" fill="#264d34"/>'
            )
        xs = [p[0] for p in r["poly"]]
        ys = [p[1] for p in r["poly"]]
        mid = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
        # Region labels are visually offset from the anchor and retain index/role.
        x, y = map(float, pt(mid).split(","))
        out_svg.append(
            f'<text x="{x:.1f}" y="{y - 12:.1f}" font-family="sans-serif" font-size="15" font-weight="bold" fill="#26383b" stroke="#fff" stroke-width="3" paint-order="stroke" text-anchor="middle">{r["index"]:02} {r["name"]}</text>'
        )
        if r["trait"]:
            out_svg.append(
                f'<text x="{x:.1f}" y="{y + 4:.1f}" font-family="sans-serif" font-size="13" fill="#7a1f00" stroke="#fff" stroke-width="3" paint-order="stroke" text-anchor="middle">[{r["trait"].replace("_", " ")}]</text>'
            )
    for d in data["deposits"]:
        x, y = map(float, pt(d["pos"]).split(","))
        color = "#a46700" if d["kind"] == "rich" else "#286687"
        out_svg.append(
            f'<polygon points="{x:.2f},{y - 8:.2f} {x + 8:.2f},{y:.2f} {x:.2f},{y + 8:.2f} {x - 8:.2f},{y:.2f}" fill="#fff8df" stroke="{color}" stroke-width="2.6"/>'
        )
    for h in data["headquarters"]:
        x, y = map(float, pt(h["pos"]).split(","))
        out_svg.append(
            f'<circle cx="{x}" cy="{y}" r="16" fill="{("#2274a8" if h["team"] == 0 else "#b3475a")}" stroke="#fff" stroke-width="3"/><text x="{x + 20}" y="{y + 29}" font-family="sans-serif" font-size="17" fill="#273c48" stroke="#fff" stroke-width="3" paint-order="stroke">{h["id"]}</text>'
        )
    out_svg += [
        '<text x="500" y="1040" text-anchor="middle" fill="#344b54" font-family="sans-serif" font-size="15">◇ deposits (blue normal, amber rich)  ◎ anchors  ● HQs  brown 300 cm plateau  orange 8 m ramp (arrow downhill)  dark rock wall  olive cover prop</text>',
        '<text x="500" y="1069" text-anchor="middle" fill="#53616b" font-family="sans-serif" font-size="15">200 \u00d7 200 m | X points up | Y points right | borders mark ownership, not walls | routes: blue north, red centre, green south</text>',
        "</svg>",
    ]
    svg = "\n".join(out_svg).encode()
    subprocess.run(["rsvg-convert", "-f", "png", "-o", str(out)], input=svg, check=True)
    print(f"Rendered {out}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--derive",
        action="store_true",
        help="rebuild JSON from source Excalidraw paths",
    )
    parser.add_argument("--out", type=Path, default=PNG)
    args = parser.parse_args()
    if args.derive:
        derive()
    data = cast(MapData, json.loads(DATA.read_text()))
    audit(data)
    walking(data)
    render(data, args.out)


if __name__ == "__main__":
    main()
