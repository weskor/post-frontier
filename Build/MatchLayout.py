"""Shared match actors and pure region/deposit planning for the legacy arenas.

Boot and CampusZero share the prototype HQ/sector coordinates. AvailabilityZone v1 passes its own
JSON coordinates to the same clipped Voronoi planner. Unreal is only needed when placing actors.
"""
import math
from importlib import import_module
from collections.abc import Callable, Iterator, Mapping, Sequence
from itertools import chain
from typing import Any, NotRequired, TypedDict, TypeVar, cast

Point = Sequence[float]
Polygon = Sequence[Point]
GroundClearance = Callable[[Point, float], bool]
Containment = Callable[[Polygon, Point], bool]
T = TypeVar("T")


class GameplayRegion(TypedDict):
    index: int
    name: str
    home_team: int
    seed: tuple[float, ...]
    poly: list[tuple[float, float]]
    neighbours: list[int]
    defend_posts: NotRequired[list[list[float]]]


class Deposit(TypedDict):
    region: int
    pos: Point
    kind: str


def require(value: T, message: str) -> T:
    if not value:
        raise RuntimeError(message)
    return value


# Capture sites retain this order, with SiteIndex 2+ matching their RegionIndex (mains occupy 0 and 1).
SITES = {"Substation7": (-850, -1800), "CoolingPlant": (1450, 1100), "FibreJunction": (600, -2200)}
SITE_Z = 5
FRIENDLY_HQ = (-3500, -600)
ENEMY_HQ = (3200, 2300)
HQ_Z = 110  # AHeadquarters hit box half height: the body stands on the floor
FRIENDLY_TEAM = 0
ENEMY_TEAM = 5
HEADQUARTERS = (("Friendly Main", FRIENDLY_HQ, FRIENDLY_TEAM), ("Enemy Main", ENEMY_HQ, ENEMY_TEAM))
BUILD_GRID = 50
EXTRACTOR_HALF_EXTENT = 100.0  # Four build-grid cells; includes the 95 cm physical footprint.
DEPOSIT_CLEARANCE = 175.0  # Circumscribed footprint plus the 35 cm navigation agent radius.
GEOMETRY_EPSILON = 1e-5

# Authored world XY centimetres, indexed in region_plan's seed order. Keep these
# editable: the one-off coverage estimate is not part of map generation.
BOOT_DEFEND_POSTS = [
    [[-3100, 2200], [-3100, -1400]],
    [[2500, 3600], [4200, 700]],
    [[-2000, -3600], [-900, -900]],
    [[2400, 0], [-800, 2500]],
    [[3100, -2700], [500, -2600]],
]
CAMPUS_ZERO_DEFEND_POSTS = [
    [[-3100, 2200], [-3100, -2300]],
    [[4300, 600], [2500, 3200]],
    [[-2000, -3600], [-900, -900]],
    [[2100, -400], [-500, 2800]],
    [[3100, -2700], [500, -2600]],
]
DEFEND_POST_RANGE = 3500.0
NAV_AGENT_RADIUS = 35.0


def clip_polygon(poly: list[tuple[float, float]], normal: Point, limit: float) -> list[tuple[float, float]]:
    """Intersect a CCW convex polygon with normal.dot(point) <= limit."""
    result = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        da = normal[0] * a[0] + normal[1] * a[1] - limit
        db = normal[0] * b[0] + normal[1] * b[1] - limit
        if da <= 0:
            result.append(a)
        if (da <= 0) != (db <= 0):
            t = da / (da - db)
            result.append((a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])))
    cleaned: list[tuple[float, float]] = []
    for p in result:
        if not cleaned or math.dist(p, cleaned[-1]) > GEOMETRY_EPSILON:
            cleaned.append(p)
    if len(cleaned) > 1 and math.dist(cleaned[0], cleaned[-1]) <= GEOMETRY_EPSILON:
        cleaned.pop()
    return cleaned


def contains(poly: Polygon, point: Point) -> bool:
    """Boundary-inclusive containment for the planner's CCW convex polygons."""
    for a, b in zip(poly, chain(poly[1:], poly[:1])):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx * (point[1] - a[1]) - dy * (point[0] - a[0]) < -GEOMETRY_EPSILON * math.hypot(dx, dy):
            return False
    return len(poly) >= 3


def shared_edge(a_poly: Polygon, b_poly: Polygon) -> bool:
    """Neighbours share a positive-length polygon edge, not just a vertex."""
    for a, b in zip(a_poly, chain(a_poly[1:], a_poly[:1])):
        length = math.dist(a, b)
        if length <= GEOMETRY_EPSILON:
            continue
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        for c, d in zip(b_poly, chain(b_poly[1:], b_poly[:1])):
            if any(abs(ux * (p[1] - a[1]) - uy * (p[0] - a[0])) > GEOMETRY_EPSILON for p in (c, d)):
                continue
            lo, hi = sorted(ux * (p[0] - a[0]) + uy * (p[1] - a[1]) for p in (c, d))
            if min(length, hi) - max(0.0, lo) > GEOMETRY_EPSILON:
                return True
    return False


def region_plan(
    half_extent: Point,
    headquarters: Sequence[tuple[str, Point, int]] = HEADQUARTERS,
    sectors: Sequence[tuple[str, Point]] | None = None,
    defend_posts: Sequence[Sequence[Point]] | None = None,
) -> list[GameplayRegion]:
    """Tile the arena around ordered seeds, optionally attaching indexed authored posts."""
    sectors = list(SITES.items()) if sectors is None else list(sectors)
    headquarters = sorted(headquarters, key=lambda h: h[2])
    require([h[2] for h in headquarters] == [FRIENDLY_TEAM, ENEMY_TEAM], "Exactly two home teams required")
    hx, hy = half_extent
    require(hx > 0 and hy > 0, "ArenaBounds has no extent")
    seeds = [(name, tuple(pos[:2]), team) for name, pos, team in headquarters]
    seeds += [(name, tuple(pos[:2]), -1) for name, pos in sectors]
    require(len({p for _, p, _ in seeds}) == len(seeds), "Region seeds must be distinct")
    if defend_posts is not None:
        require(len(defend_posts) == len(seeds), "Every gameplay region requires authored defend posts")
    require(all(abs(p[0]) < hx and abs(p[1]) < hy for _, p, _ in seeds), "Region seeds must be inside arena")
    regions: list[GameplayRegion] = []
    for index, (name, seed, team) in enumerate(seeds):
        poly: list[tuple[float, float]] = [(-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)]
        for other_index, (_, other, _) in enumerate(seeds):
            if other_index == index:
                continue
            distance = math.dist(seed, other)
            normal = ((other[0] - seed[0]) / distance, (other[1] - seed[1]) / distance)
            midpoint = ((other[0] + seed[0]) / 2, (other[1] + seed[1]) / 2)
            poly = clip_polygon(poly, normal, normal[0] * midpoint[0] + normal[1] * midpoint[1])
        require(len(poly) >= 3, "Region %s has an empty polygon" % name)
        regions.append({"index": index, "name": name, "home_team": team, "seed": seed,
                        "poly": poly, "neighbours": []})
        if defend_posts is not None:
            regions[-1]["defend_posts"] = [list(point) for point in defend_posts[index]]
    for a in regions:
        for b in regions[a["index"] + 1:]:
            if shared_edge(a["poly"], b["poly"]):
                a["neighbours"].append(b["index"])
                b["neighbours"].append(a["index"])
    return regions


def clear_of_blockers(point: Point, blockers: Sequence[Sequence[float]], clearance: float = DEPOSIT_CLEARANCE) -> bool:
    """Conservative footprint clearance from oriented (cx, cy, sx, sy, yaw) ground blockers."""
    for cx, cy, sx, sy, yaw in blockers:
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        dx, dy = point[0] - cx, point[1] - cy
        lx, ly = dx * c + dy * s, -dx * s + dy * c
        if math.hypot(max(abs(lx) - sx / 2, 0), max(abs(ly) - sy / 2, 0)) < clearance:
            return False
    return True


def buildable_samples(
    region: Mapping[str, object],
    half_extent: Point,
    clear_ground: GroundClearance,
    placement_margin: float = 100.0,
    contains_point: Containment = contains,
    uncovered_by: Sequence[Point] = (),
) -> Iterator[tuple[float, float]]:
    """Build-grid samples with the same whole-footprint/ground clearance as deposit placement.

    `uncovered_by` prunes already-covered samples before expensive ground queries;
    it does not change which uncovered building placements count.
    """
    hx, hy = half_extent
    poly = cast(Polygon, region["poly"])
    x0 = max(min(p[0] for p in poly), -hx + placement_margin + DEPOSIT_CLEARANCE)
    x1 = min(max(p[0] for p in poly), hx - placement_margin - DEPOSIT_CLEARANCE)
    y0 = max(min(p[1] for p in poly), -hy + placement_margin + DEPOSIT_CLEARANCE)
    y1 = min(max(p[1] for p in poly), hy - placement_margin - DEPOSIT_CLEARANCE)
    for i in range(math.ceil(x0 / BUILD_GRID), math.floor(x1 / BUILD_GRID) + 1):
        for j in range(math.ceil(y0 / BUILD_GRID), math.floor(y1 / BUILD_GRID) + 1):
            point = (i * BUILD_GRID, j * BUILD_GRID)
            if uncovered_by and any(math.dist(point, post) <= DEFEND_POST_RANGE for post in uncovered_by):
                continue
            if not contains_point(poly, point):
                continue
            if not all(contains_point(poly, (point[0] + dx, point[1] + dy))
                       for dx in (-EXTRACTOR_HALF_EXTENT, EXTRACTOR_HALF_EXTENT)
                       for dy in (-EXTRACTOR_HALF_EXTENT, EXTRACTOR_HALF_EXTENT)):
                continue
            if clear_ground(point, DEPOSIT_CLEARANCE):
                yield point


def defend_post_errors(
    regions: Sequence[Mapping[str, object]],
    half_extent: Point,
    clear_ground: GroundClearance,
    placement_margin: float = 100.0,
    contains_point: Containment = contains,
) -> list[str]:
    """Reject missing/extra, out-of-region, off-ground or >35m authored defend posts."""
    errors = []
    hx, hy = half_extent
    for region in regions:
        name = cast(str, region["name"])
        poly = cast(Polygon, region["poly"])
        posts = cast(Sequence[Point], region.get("defend_posts", []))
        if not 2 <= len(posts) <= 3:
            errors.append("%s: requires 2-3 defend posts (got %d)" % (name, len(posts)))
            continue
        valid = True
        for index, post in enumerate(posts):
            if len(post) != 2 or not all(math.isfinite(value) for value in post):
                errors.append("%s: defend post %d requires finite world XY coordinates" % (name, index))
                valid = False
                continue
            if not contains_point(poly, post):
                errors.append("%s: defend post %d outside region" % (name, index))
                valid = False
            if abs(post[0]) >= hx or abs(post[1]) >= hy or not clear_ground(post, NAV_AGENT_RADIUS):
                errors.append("%s: defend post %d off walkable ground" % (name, index))
                valid = False
        if not valid:
            continue
        sample = next(buildable_samples(region, half_extent, clear_ground, placement_margin,
                                        contains_point, posts), None)
        if sample is not None:
            distance = min(math.dist(sample, post) for post in posts)
            errors.append("%s: buildable spot %s is %.1f cm from nearest defend post (maximum 3500 cm)"
                          % (name, sample, distance))
    return errors


def deposit_plan(
    regions: Sequence[GameplayRegion], half_extent: Point, clear_ground: Callable[[Point], bool],
    placement_margin: float = 100.0,
) -> list[Deposit]:
    """Find 2 normal deposits per main and 1 per sector on clear, grid-aligned ground.

    Search concentric build-grid rings about each seed. Every footprint corner stays in the same region,
    away from arena walls, HQ bodies and the other deposits, leaving room to build all extractors together.
    `clear_ground(point)` checks the map's finished ground-level collision/navigation plan.
    """
    hx, hy = half_extent
    deposits: list[Deposit] = []
    homes = [r for r in regions if r["home_team"] >= 0]
    for region in regions:
        seed = region["seed"]
        gx, gy = (math.floor(v / BUILD_GRID + 0.5) for v in seed)
        wanted = 2 if region["home_team"] >= 0 else 1
        placed = 0
        max_ring = math.ceil(max(hx + abs(seed[0]), hy + abs(seed[1])) / BUILD_GRID)
        for ring in range(max_ring + 1):
            offsets = [(x, y) for x in range(-ring, ring + 1) for y in (-ring, ring)]
            if ring:
                offsets += [(x, y) for x in (-ring, ring) for y in range(-ring + 1, ring)]
            candidates = {((gx + x) * BUILD_GRID, (gy + y) * BUILD_GRID) for x, y in offsets}
            for point in sorted(candidates, key=lambda p: (math.dist(p, seed), p)):
                if abs(point[0]) + DEPOSIT_CLEARANCE + placement_margin > hx or abs(point[1]) + DEPOSIT_CLEARANCE + placement_margin > hy:
                    continue
                corners = [(point[0] + dx, point[1] + dy) for dx in (-EXTRACTOR_HALF_EXTENT, EXTRACTOR_HALF_EXTENT)
                           for dy in (-EXTRACTOR_HALF_EXTENT, EXTRACTOR_HALF_EXTENT)]
                if not all(contains(region["poly"], p) for p in [point] + corners):
                    continue
                if any(math.dist(point, home["seed"]) < (350 if home is region else 1100) for home in homes):
                    continue
                if region["home_team"] < 0 and math.dist(point, seed) < 250:
                    continue
                if any(math.dist(point, d["pos"]) < 300 for d in deposits) or not clear_ground(point):
                    continue
                deposits.append({"region": region["index"], "pos": point, "kind": "normal"})
                placed += 1
                if placed == wanted:
                    break
            if placed == wanted:
                break
        require(placed == wanted, "No clear deposit footprint in region %s" % region["name"])
    return deposits


def place_regions(
    spawn: Callable[..., Any], regions: Sequence[GameplayRegion], deposits: Sequence[Deposit],
    anchors: Mapping[int, Any],
) -> None:
    """Place the binding MapRegion/DepositSite reflected properties after the map's blockers are known."""
    unreal = import_module("unreal")

    region_class = native_class("MapRegion")
    deposit_class = native_class("DepositSite")
    for region in regions:
        index = region["index"]
        actor = spawn(region_class, "Region%d" % index, (*region["seed"], 0))
        actor.set_editor_property("region_index", index)
        actor.set_editor_property("display_name", unreal.Text(region["name"]))
        actor.set_editor_property("region_role", unreal.RegionRole.MAIN if index < 2 else unreal.RegionRole.TACTICAL)
        actor.set_editor_property("home_team", region["home_team"])
        actor.set_editor_property("polygon", [unreal.Vector2D(*p) for p in region["poly"]])
        actor.set_editor_property("defend_posts", [unreal.Vector(p[0], p[1], 0) for p in region["defend_posts"]])
        actor.set_editor_property("neighbours", region["neighbours"])
        if index >= 2:
            anchor = anchors.get(index)
            if not anchor:
                raise RuntimeError("Region %d has no capture anchor" % index)
            require(anchor.get_editor_property("site_index") == index, "Capture anchor index differs from region")
            actor.set_editor_property("anchor", anchor)
    for index, deposit in enumerate(deposits):
        actor = spawn(deposit_class, "Deposit%d" % index, (*deposit["pos"], SITE_Z))
        actor.set_editor_property("region_index", deposit["region"])
        actor.set_editor_property("rich", False)
    unreal.log("MATCH_REGIONS_GENERATED regions=%d deposits=%d" % (len(regions), len(deposits)))


def native_class(name: str) -> Any:
    unreal = import_module("unreal")

    return require(unreal.load_class(None, "/Script/CoopRTS." + name),
                   "Build CoopRTSEditor before generating the map")


def place(spawn: Callable[..., Any], anchors: dict[int, Any]) -> tuple[float, float]:
    """Place arena, HQs and indexed capture anchors with `spawn(actor_class, label, location)`.

    Fill `anchors` keyed by region index. The caller places regions/deposits after recording its blockers.
    Return the arena half extent (x, y) used by floor, navigation and region planning.
    """
    unreal = import_module("unreal")

    arena = spawn(native_class("ArenaBounds"), "Arena", (0, 0, 0))
    for index, (name, (x, y)) in enumerate(SITES.items(), start=2):
        site = spawn(native_class("CapturePoint"), name, (x, y, SITE_Z))
        site.set_editor_property("site_kind", unreal.CaptureSiteKind.RESOURCE)
        site.set_editor_property("site_index", index)
        anchors[index] = site
    for label, (x, y), team in (("FriendlyHeadquarters", FRIENDLY_HQ, FRIENDLY_TEAM),
                                ("EnemyHeadquarters", ENEMY_HQ, ENEMY_TEAM)):
        spawn(native_class("Headquarters"), label, (x, y, HQ_Z)).set_editor_property("team_index", team)
    extent = arena.get_editor_property("half_extent")
    require(extent.x > 0 and extent.y > 0, "ArenaBounds has no extent")
    return float(extent.x), float(extent.y)
