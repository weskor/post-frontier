"""Rebuild the flat /Game/Maps/AvailabilityZoneV2 from Maps/AvailabilityZoneV2.json.

Run in UnrealEditor-Cmd with PythonScriptPlugin after building CoopRTSEditor.
Regions, capture anchors and deposits are native replicated match actors; painted
region/deposit markings are non-colliding navigation guides.
"""
import json
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import MatchLayout

MAP_PATH = "/Game/Maps/AvailabilityZoneV2"
JSON_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Maps", "AvailabilityZoneV2.json")
require = MatchLayout.require
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def inside(point, poly):
    """Boundary-inclusive point in polygon (the input polygon is implicitly closed)."""
    x, y = point
    enclosed = False
    for a, b in zip(poly, poly[1:] + poly[:1]):
        ax, ay = a
        bx, by = b
        cross = (x - ax) * (by - ay) - (y - ay) * (bx - ax)
        if abs(cross) < 0.001 and min(ax, bx) - 0.001 <= x <= max(ax, bx) + 0.001 and min(ay, by) - 0.001 <= y <= max(ay, by) + 0.001:
            return True
        if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
            enclosed = not enclosed
    return enclosed


def area(poly):
    return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1])) / 2


def validate(data):
    half = data["arena"]["half_extent"]
    require(len(half) == 2 and min(half) > 0, "Invalid arena half_extent")
    require(data["arena"]["half_height"] == 1000 and data["arena"]["placement_margin"] >= 0,
            "Arena half_height must match AArenaBounds.HalfHeight (1000 cm)")
    headquarters = data["headquarters"]
    require(len(headquarters) == 2 and {hq["team"] for hq in headquarters} == {0, 5},
            "The map requires exactly one HQ each for teams 0 and 5")
    regions = data["regions"]
    require(len(regions) == 15 and sorted(r["index"] for r in regions) == list(range(15)),
            "Expected region indices 0..14 exactly once")
    by_index = {r["index"]: r for r in regions}
    mains = [r for r in regions if r["role"] == "main"]
    require(len(mains) == 2 and {r["home_team"] for r in mains} == {0, 5},
            "Expected one main for each HQ")
    for region in regions:
        index = region["index"]
        poly = region["poly"]
        require(len(poly) >= 3 and area(poly) > 0, "Region %d polygon must be CCW" % index)
        require(region["role"] in ("main", "natural", "reward", "tactical"), "Invalid region role")
        require(region["home_team"] in (0, 5) if region["role"] == "main" else region["home_team"] == -1,
                "Region %d has unexpected home team" % index)
        require(all(abs(x) <= half[0] and abs(y) <= half[1] for x, y in poly),
                "Region %d extends beyond arena" % index)
        anchor = region["anchor"]
        require(anchor is None if region["role"] == "main" else anchor is not None and inside(anchor, poly),
                "Region %d has an invalid capture anchor" % index)
        neighbours = region["neighbours"]
        require(len(neighbours) == len(set(neighbours)) and index not in neighbours and all(
            neighbour in by_index and index in by_index[neighbour]["neighbours"] for neighbour in neighbours),
            "Region %d has invalid/asymmetric neighbours" % index)
    for hq in headquarters:
        require(inside(hq["pos"], next(r["poly"] for r in mains if r["home_team"] == hq["team"])),
                "HQ team %d must be inside its main" % hq["team"])
    for deposit in data["deposits"]:
        require(deposit["region"] in by_index and deposit["kind"] in ("normal", "rich") and
                inside(deposit["pos"], by_index[deposit["region"]]["poly"]),
                "Deposit outside its region or with unknown kind")
    for rock in data["blockers"]:
        require(len(rock["poly"]) >= 3 and area(rock["poly"]) > 0, "Invalid CCW blocker polygon " + rock["id"])
    return by_index


with open(JSON_PATH, encoding="utf-8") as handle:
    data = json.load(handle)
regions = validate(data)
arena_definition = data["arena"]
hx, hy = arena_definition["half_extent"]

# Check all dependencies before replacing an existing level. No kit meshes or new material assets needed.
cube = require(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"), "Missing engine cube")
cylinder = require(unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder"), "Missing engine cylinder")
materials = {}
for name in ("MI_AZ_GravelDark", "MI_AZ_Concrete", "MI_AZ_MachineConcrete", "MI_AZ_PaintBlue",
             "MI_AZ_PaintAmber", "MI_AZ_Steel"):
    materials[name] = require(unreal.load_asset("/Game/Art/Materials/" + name), "Missing existing material " + name)
for name in ("ArenaBounds", "Headquarters", "CapturePoint", "MapRegion", "DepositSite", "CommandGameMode"):
    MatchLayout.native_class(name)

if assets.does_asset_exist(MAP_PATH):
    require(levels.load_level(MAP_PATH), "Could not load AvailabilityZoneV2 for replacement")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings) or (isinstance(actor, unreal.Brush) and not isinstance(actor, unreal.Volume)):
            continue
        require(actors.destroy_actor(actor), "Could not remove previous map actor " + actor.get_name())
else:
    require(levels.new_level(MAP_PATH, False), "Could not create AvailabilityZoneV2")


def spawn(actor_class, label, location, rotation=None):
    actor = require(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator()),
                    "Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


def block(label, center, size, material, mesh=cube, yaw=0.0, base=0.0, collision=False, folder="AZV2"):
    """Primitive 100-cm source mesh scaled to desired footprint; base is bottom Z."""
    require(min(size) > 0, "Nonpositive mesh size for " + label)
    actor = spawn(unreal.StaticMeshActor, label, (center[0], center[1], base + size[2] / 2),
                  unreal.Rotator(yaw=yaw))
    actor.set_folder_path(folder)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, material)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(*(dimension / 100 for dimension in size)))
    return actor


def line(label, a, b, width, material, base=0.0, folder="AZV2/Markings"):
    length = math.dist(a, b)
    if length < 1:
        return
    angle = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    block(label, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), (length, width, 4),
          material, yaw=angle, base=base, folder=folder)


def rectangle(poly):
    """Return exact oriented box dimensions for a four-corner rectangular blocker."""
    if len(poly) != 4:
        return None
    edges = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(poly, poly[1:] + poly[:1])]
    lengths = [math.hypot(*edge) for edge in edges]
    if min(lengths) < 1 or any(abs(edges[i][0] * edges[(i + 1) % 4][0] +
                                  edges[i][1] * edges[(i + 1) % 4][1]) > 0.001 * lengths[i] * lengths[(i + 1) % 4]
                               for i in range(4)):
        return None
    return (sum(p[0] for p in poly) / 4, sum(p[1] for p in poly) / 4, lengths[0], lengths[1],
            math.degrees(math.atan2(edges[0][1], edges[0][0])))


def place_rock(rock):
    """A single oriented cube follows the exact four-corner measured rock mark."""
    box = require(rectangle(rock["poly"]), "Blocker must be a rotated rectangle: " + rock["id"])
    x, y, sx, sy, yaw = box
    block("Rock_" + rock["id"], (x, y), (sx, sy, 360), materials["MI_AZ_MachineConcrete"],
          yaw=yaw, collision=True, folder="AZV2/Rocks")


def simplified_outline(poly, tolerance=80.0):
    """Remove the sketch's 35-cm pixel stair-steps from visual-only borders."""
    start = max(range(len(poly)), key=lambda i: poly[i][0])
    closed = poly[start:] + poly[:start + 1]
    midpoint = max(range(len(closed)), key=lambda i: math.dist(closed[0], closed[i]))

    def reduce(points):
        a, b = points[0], points[-1]
        length = math.dist(a, b)
        if length == 0:
            return [a, b]
        errors = [abs((b[1] - a[1]) * (p[0] - a[0]) - (b[0] - a[0]) * (p[1] - a[1])) / length
                  for p in points[1:-1]]
        if errors and max(errors) > tolerance:
            split = errors.index(max(errors)) + 1
            return reduce(points[:split + 1])[:-1] + reduce(points[split:])
        return [a, b]

    return reduce(closed[:midpoint + 1])[:-1] + reduce(closed[midpoint:])[:-1]


def place_match_actors(region_defs, deposit_defs):
    """Place the v2 gameplay actors from the same JSON used for the visual plan."""
    arena = spawn(MatchLayout.native_class("ArenaBounds"), "Arena", (0, 0, 0))
    arena.set_editor_property("half_extent", unreal.Vector2D(float(hx), float(hy)))
    arena.set_editor_property("placement_margin", float(arena_definition["placement_margin"]))
    require(abs(arena.get_editor_property("half_extent").x - hx) < 0.5 and
            abs(arena.get_editor_property("half_extent").y - hy) < 0.5, "Arena extent did not apply")
    for hq in data["headquarters"]:
        actor = spawn(MatchLayout.native_class("Headquarters"),
                      "FriendlyHeadquarters" if hq["team"] == 0 else "EnemyHeadquarters",
                      (hq["pos"][0], hq["pos"][1], 110))
        actor.set_editor_property("team_index", hq["team"])

    anchors = {}
    for index, region in sorted(region_defs.items()):
        if region["role"] == "main":
            continue
        x, y = region["anchor"]
        anchor = spawn(MatchLayout.native_class("CapturePoint"),
                       "Region%02d_%s" % (index, region["name"]), (x, y, 5))
        anchor.set_editor_property("site_kind", unreal.CaptureSiteKind.RESOURCE)
        anchor.set_editor_property("site_index", index)
        anchor.set_editor_property("tags", [unreal.Name(region["name"])])
        anchors[index] = anchor
    require(len(anchors) == 13 and sorted(anchors) == sorted(
        r["index"] for r in region_defs.values() if r["role"] != "main"),
        "Expected 13 CapturePoints whose SiteIndex equals non-main RegionIndex")
    role_type = {"main": unreal.RegionRole.MAIN, "natural": unreal.RegionRole.NATURAL,
                 "reward": unreal.RegionRole.REWARD, "tactical": unreal.RegionRole.TACTICAL}
    hq_by_team = {hq["team"]: hq["pos"] for hq in data["headquarters"]}
    for index, region in sorted(region_defs.items()):
        point = hq_by_team[region["home_team"]] if region["role"] == "main" else region["anchor"]
        actor = spawn(MatchLayout.native_class("MapRegion"), "Region%02d_%s" % (index, region["name"]),
                      (point[0], point[1], 0))
        actor.set_folder_path("AZV2/Regions")
        actor.set_editor_property("region_index", index)
        actor.set_editor_property("display_name", unreal.Text(region["name"]))
        actor.set_editor_property("region_role", role_type[region["role"]])
        actor.set_editor_property("home_team", region["home_team"])
        actor.set_editor_property("polygon", [unreal.Vector2D(*point) for point in region["poly"]])
        actor.set_editor_property("neighbours", region["neighbours"])
        if index in anchors:
            actor.set_editor_property("anchor", anchors[index])
    for index, deposit in enumerate(deposit_defs):
        x, y = deposit["pos"]
        actor = spawn(MatchLayout.native_class("DepositSite"), "Deposit%02d" % index, (x, y, 5))
        actor.set_folder_path("AZV2/Deposits")
        actor.set_editor_property("region_index", deposit["region"])
        actor.set_editor_property("rich", deposit["kind"] == "rich")
    return anchors


anchors = place_match_actors(regions, data["deposits"])

# One continuous solid, perfectly level ground slab. Its upper surface is z=0.
block("FlatGround", (0, 0), (2 * (hx + 200), 2 * (hy + 200), 100), materials["MI_AZ_GravelDark"],
      base=-100, collision=True, folder="AZV2/Ground")
for rock in data["blockers"]:
    place_rock(rock)

# Borders are planning lines, not walls. Simplify the rasterized source
# polygons only for the visuals; native region data keeps its exact vertices.
# Neither border paint nor deposit/anchor indicators have collision.
seen_edges = set()
role_material = {"main": materials["MI_AZ_PaintBlue"], "natural": materials["MI_AZ_PaintBlue"],
                 "reward": materials["MI_AZ_PaintAmber"], "tactical": materials["MI_AZ_Concrete"]}
for index, region in sorted(regions.items()):
    poly = simplified_outline(region["poly"])
    for edge, (a, b) in enumerate(zip(poly, poly[1:] + poly[:1])):
        key = tuple(sorted((tuple(a), tuple(b))))
        if key not in seen_edges:
            seen_edges.add(key)
            line("Border_%02d_%02d" % (index, edge), a, b, 16, role_material[region["role"]], base=0.5)
    if region["anchor"] is not None:
        x, y = region["anchor"]
        block("AnchorPlate_%02d" % index, (x, y), (280, 280, 2), role_material[region["role"]],
              mesh=cylinder, base=0.5, folder="AZV2/Anchors")
for index, deposit in enumerate(data["deposits"]):
    x, y = deposit["pos"]
    material = materials["MI_AZ_PaintAmber"] if deposit["kind"] == "rich" else materials["MI_AZ_PaintBlue"]
    block("DepositPlate_%02d" % index, (x, y), (190, 190, 2), material, mesh=cylinder,
          base=0.5, folder="AZV2/Deposits")
    block("DepositCore_%02d" % index, (x, y), (95, 95, 3), materials["MI_AZ_Steel"],
          yaw=45, base=2.5, folder="AZV2/Deposits")

# Start the camera beside the friendly HQ, towards the interior; no origin start.
friendly = next(hq["pos"] for hq in data["headquarters"] if hq["team"] == 0)
start_distance = math.hypot(*friendly)
require(start_distance > 400, "Friendly HQ too close to map centre to orient the player start")
start = (friendly[0] - 400 * friendly[0] / start_distance,
         friendly[1] - 400 * friendly[1] / start_distance, 150)
spawn(unreal.PlayerStart, "CommanderStart", start)

sun = spawn(unreal.DirectionalLight, "DuskSun", (0, 0, 3000), unreal.Rotator(pitch=-35, yaw=-28))
sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun_component.set_editor_property("intensity", 5.0)
sun_component.set_editor_property("light_color", unreal.Color(r=235, g=238, b=250, a=255))
sun_component.set_editor_property("atmosphere_sun_light", True)
spawn(unreal.SkyAtmosphere, "DuskAtmosphere", (0, 0, 0))
sky = spawn(unreal.SkyLight, "DuskSky", (0, 0, 2000))
sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_component.set_editor_property("intensity", 3.5)
sky_component.set_editor_property("real_time_capture", True)
sky_component.set_editor_property("light_color", unreal.Color(r=180, g=200, b=245, a=255))
fog = spawn(unreal.ExponentialHeightFog, "DuskFog", (0, 0, 0))
fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
fog_component.set_editor_property("fog_density", 0.02)
fog_component.set_editor_property("fog_height_falloff", 0.15)
fog_component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.02, 0.035, 0.07, 1.0))
grade = spawn(unreal.PostProcessVolume, "DuskGrade", (0, 0, 0))
grade.set_editor_property("unbound", True)
settings = grade.get_editor_property("settings")
for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                    ("auto_exposure_bias", 10.0), ("bloom_intensity", 0.35), ("bloom_threshold", 1.0),
                    ("vignette_intensity", 0.35)):
    settings.set_editor_property("override_" + name, True)
    settings.set_editor_property(name, value)
grade.set_editor_property("settings", settings)

navigation = data.get("navigation", {}).get("bounds_volume", {})
nav_centre = navigation.get("centre", [0, 0, 0])
nav_half = navigation.get("half_extent", [hx + 200, hy + 200, max(1400, arena_definition["half_height"] + 400)])
require(nav_half[0] >= hx and nav_half[1] >= hy and nav_half[2] >= 500,
        "Navigation bounds must cover the arena and flat ground")
bounds = spawn(unreal.NavMeshBoundsVolume, "ArenaNavigationBounds", tuple(nav_centre))
_, original_extent = bounds.get_actor_bounds(False)
require(min(original_extent.x, original_extent.y, original_extent.z) > 0,
        "Navigation brush factory produced an empty volume")
bounds.set_actor_scale3d(unreal.Vector(nav_half[0] / original_extent.x, nav_half[1] / original_extent.y,
                                       nav_half[2] / original_extent.z))
_, actual_extent = bounds.get_actor_bounds(False)
require(all(abs(value - desired) < 1 for value, desired in zip(
    (actual_extent.x, actual_extent.y, actual_extent.z), nav_half)), "Navigation bounds did not resize")
nav_meshes = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.RecastNavMesh)]
if not nav_meshes:
    nav_meshes = [spawn(unreal.RecastNavMesh, "RecastNavMesh-Default", (0, 0, 0))]
for nav_mesh in nav_meshes:
    nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    nav_mesh.set_editor_property("force_rebuild_on_load", True)

world = editor.get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", MatchLayout.native_class("CommandGameMode"))
require(levels.save_current_level(), "Could not save AvailabilityZoneV2")
unreal.log("AVAILABILITY_ZONE_V2_GENERATED %s regions=%d capture=%d deposits=%d rocks=%d "
           "borders=%d nav_half=%s actors=%d" %
           (MAP_PATH, len(regions), len(anchors), len(data["deposits"]), len(data["blockers"]),
            len(seen_edges), nav_half, len(actors.get_all_level_actors())))
