"""Shared match actors and pure region/deposit planning for the legacy arenas.

Boot and CampusZero share the prototype HQ/sector coordinates. AvailabilityZone v1 passes its own
JSON coordinates to the same clipped Voronoi planner. Unreal is only needed when placing actors.
"""
import math


def require(value, message):
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


def clip_polygon(poly, normal, limit):
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
    cleaned = []
    for p in result:
        if not cleaned or math.dist(p, cleaned[-1]) > GEOMETRY_EPSILON:
            cleaned.append(p)
    if len(cleaned) > 1 and math.dist(cleaned[0], cleaned[-1]) <= GEOMETRY_EPSILON:
        cleaned.pop()
    return cleaned


def contains(poly, point):
    """Boundary-inclusive containment for the planner's CCW convex polygons."""
    for a, b in zip(poly, poly[1:] + poly[:1]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx * (point[1] - a[1]) - dy * (point[0] - a[0]) < -GEOMETRY_EPSILON * math.hypot(dx, dy):
            return False
    return len(poly) >= 3


def shared_edge(a_poly, b_poly):
    """Neighbours share a positive-length polygon edge, not just a vertex."""
    for a, b in zip(a_poly, a_poly[1:] + a_poly[:1]):
        length = math.dist(a, b)
        if length <= GEOMETRY_EPSILON:
            continue
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        for c, d in zip(b_poly, b_poly[1:] + b_poly[:1]):
            if any(abs(ux * (p[1] - a[1]) - uy * (p[0] - a[0])) > GEOMETRY_EPSILON for p in (c, d)):
                continue
            lo, hi = sorted(ux * (p[0] - a[0]) + uy * (p[1] - a[1]) for p in (c, d))
            if min(length, hi) - max(0.0, lo) > GEOMETRY_EPSILON:
                return True
    return False


def region_plan(half_extent, headquarters=HEADQUARTERS, sectors=None):
    """Tile the full arena rectangle around HQs and ordered sectors; mains are always indices 0/1."""
    sectors = list(SITES.items()) if sectors is None else list(sectors)
    headquarters = sorted(headquarters, key=lambda h: h[2])
    require([h[2] for h in headquarters] == [FRIENDLY_TEAM, ENEMY_TEAM], "Exactly two home teams required")
    hx, hy = half_extent
    require(hx > 0 and hy > 0, "ArenaBounds has no extent")
    seeds = [(name, tuple(pos[:2]), team) for name, pos, team in headquarters]
    seeds += [(name, tuple(pos[:2]), -1) for name, pos in sectors]
    require(len({p for _, p, _ in seeds}) == len(seeds), "Region seeds must be distinct")
    require(all(abs(p[0]) < hx and abs(p[1]) < hy for _, p, _ in seeds), "Region seeds must be inside arena")
    regions = []
    for index, (name, seed, team) in enumerate(seeds):
        poly = [(-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)]
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
    for a in regions:
        for b in regions[a["index"] + 1:]:
            if shared_edge(a["poly"], b["poly"]):
                a["neighbours"].append(b["index"])
                b["neighbours"].append(a["index"])
    return regions


def clear_of_blockers(point, blockers, clearance=DEPOSIT_CLEARANCE):
    """Conservative footprint clearance from oriented (cx, cy, sx, sy, yaw) ground blockers."""
    for cx, cy, sx, sy, yaw in blockers:
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        dx, dy = point[0] - cx, point[1] - cy
        lx, ly = dx * c + dy * s, -dx * s + dy * c
        if math.hypot(max(abs(lx) - sx / 2, 0), max(abs(ly) - sy / 2, 0)) < clearance:
            return False
    return True


def deposit_plan(regions, half_extent, clear_ground, placement_margin=100.0):
    """Find 2 normal deposits per main and 1 per sector on clear, grid-aligned ground.

    Search concentric build-grid rings about each seed. Every footprint corner stays in the same region,
    away from arena walls, HQ bodies and the other deposits, leaving room to build all extractors together.
    `clear_ground(point)` checks the map's finished ground-level collision/navigation plan.
    """
    hx, hy = half_extent
    deposits = []
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


def place_regions(spawn, regions, deposits, anchors):
    """Place the binding MapRegion/DepositSite reflected properties after the map's blockers are known."""
    import unreal

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
        actor.set_editor_property("neighbours", region["neighbours"])
        if index >= 2:
            anchor = require(anchors.get(index), "Region %d has no capture anchor" % index)
            require(anchor.get_editor_property("site_index") == index, "Capture anchor index differs from region")
            actor.set_editor_property("anchor", anchor)
    for index, deposit in enumerate(deposits):
        actor = spawn(deposit_class, "Deposit%d" % index, (*deposit["pos"], SITE_Z))
        actor.set_editor_property("region_index", deposit["region"])
        actor.set_editor_property("rich", False)
    unreal.log("MATCH_REGIONS_GENERATED regions=%d deposits=%d" % (len(regions), len(deposits)))


def native_class(name):
    import unreal

    return require(unreal.load_class(None, "/Script/CoopRTS." + name),
                   "Build CoopRTSEditor before generating the map")


def place(spawn, anchors):
    """Place arena, HQs and indexed capture anchors with `spawn(actor_class, label, location)`.

    Fill `anchors` keyed by region index. The caller places regions/deposits after recording its blockers.
    Return the arena half extent (x, y) used by floor, navigation and region planning.
    """
    import unreal

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
