"""Create or replace /Game/Maps/AvailabilityZone, Phase A (flat) of the first real map (Docs/Maps/AvailabilityZone*.md).

Everything comes from Build/Maps/AvailabilityZone.json through Build/AvailabilityZoneLayout.py (pure Python, which
this script asks for the plan and refuses to run without a clean one). Human forward base south-west, the Machine
campus north-east, the shared Bunker for one or two players, the two attack routes as designed. Phase A: every
playable level is z = 0; level changes are walls, the ramps are the gaps in them (no kit ramp mesh: its deck rises
300 cm). Boot, CampusZero and the automated tests are untouched.

Run order (from the project root, editor closed, each step its own editor process, no other Unreal process from this
repo running; Saved/Verification/availability-zone/run_all.sh does all of it):
  -1. python3 Build/AvailabilityZoneLayout.py   plan smoke test, no Unreal; must print "Issues: none"
   0. Build/BuildSharedMaterial.py              M_Shared and the MI_SC2_* instances (-RenderOffscreen; its docstring)
   1. Build/ImportEnvironmentKit.py             /Game/Art/Environment (ENV_KIT_IMPORTED 17); the Slab and the dressing
   2. Build/ImportTerrainKit.py                 /Game/Art/Terrain (TERRAIN_KIT_IMPORTED 15), -EnablePlugins=PythonScriptPlugin,
                                                GeometryScripting
   3. Build/GenerateAvailabilityZone.py         after compiling CoopRTSEditor (the match actors are native classes):
  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateAvailabilityZone.py" \
    -unattended -nullrhi -nosplash
Require AVAILABILITY_ZONE_GENERATED in the log (it prints the blocking count). Reruns replace every actor of the level.
Without steps 1 and 2 the script stops at its first check; there is no primitive fallback.

What is placed
* Match actors from the JSON's match_actors, mirroring Build/MatchLayout.py (which hard-codes Boot): one AArenaBounds
  (HalfExtent 10000 x 10000, PlacementMargin 100), AHeadquarters team 0 (the Bunker) and team 5 (the Cluster), eight
  ACapturePoint with SiteIndex 2-9, matching their generated region indices. Two HQ main regions and eight
  sector regions tile the arena, with two normal deposits per main and one per sector.
* Level edges: 100 x 300 cm collision walls between levels (a coping strip and lamps on top), 50 x 65 cm parapets
  and a painted deck on every ramp gap, kit Cliff_Straight / Cliff_CornerOuter / Plateau_Fill (Human or Machine look
  by the x + y diagonal) as the rim wall around every playable edge, 600 cm boxes behind them. See the layout
  module's docstring for the rules; every wall run is proven not to touch a ramp's walkable strip.
* The Slab: EnvKit.assemble_hall 52 x 36 m, two doors per long side, cooling towers on the roof. Four kit Watchtowers.
* Ground: asphalt, concrete, gravel, deck plate, Machine tile and concrete zones are MI_AZ_* children of the MI_SC2_*
  instances with UseBakedMasks off (the engine cube has no vertex colours), haul roads along both attack routes,
  numbered bay marks, capture rings, HQ plazas, cyan cables to every Machine sector.
* Dressing: in-play kit props (containers, wrecks, transformers ...) only where AvailabilityZoneLayout.keep_out_reason
  allows: clear of every HQ disc, sector territory disc, bay and its exit ring, ramp footprint and route/walk corridor.
  Backdrop halls, cooling towers and forward-base clutter stand on the out-of-play rim blocks.
* Lighting: CampusZero's dusk (SUN_LUX, SKY_INTENSITY, EXPOSURE_BIAS and the fixed-exposure grade), amber lamps on the
  human half, cyan spot pools at the hall doors and cyan lamps on the Machine half.
* Navigation: NavMeshBoundsVolume sized from navigation.bounds_volume, RecastNavMesh with Dynamic runtime generation.
* World settings game mode CommandGameMode.
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art
import AvailabilityZoneLayout as az
import EnvKit
import MatchLayout
import TerrainKit

require = art.require
assets = art.assets
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

MAP_PATH = "/Game/Maps/AvailabilityZone"
# Lighting knobs: copied from Build/GenerateCampusZero.py (v8+, the dusk "Neon Nightfall" mood); change them there first.
SUN_LUX = 5.0
SKY_INTENSITY = 3.5
EXPOSURE_BIAS = 10.0
POOL_INNER_CONE = 16.0  # hall-door spot cone, degrees; a narrow inner-to-outer gap keeps the pool edge sharp
POOL_OUTER_CONE = 19.0
POOL_INTENSITY = 320.0
SUN_COLOR = (235, 238, 250)  # near-neutral cool moonlight
SKY_COLOR = (180, 200, 245)
CAPTURE_RADIUS = 430.0

# ---------------------------------------------------------------- the plan (pure Python, proven before Unreal is touched)
kit_sizes = {}
for _name in EnvKit.SPEC:
    _fx, _fy, _height = EnvKit.piece_size(_name)
    _bx, _by = EnvKit.collision_size(_name)
    kit_sizes[_name] = (_fx, _fy, _height, _bx, _by)
# The verification scripts rebuild this plan in Unreal's -game Python, where EnvKit cannot be imported: they read the
# same footprints from EnvKit's literals (az.env_sizes). Fail here if the two ever disagree.
require(all(abs(a - b) < 0.01 for name in kit_sizes for a, b in zip(kit_sizes[name], az.env_sizes()[name])),
        "AvailabilityZoneLayout.env_sizes() no longer matches EnvKit.piece_size / collision_size")
layout = az.Layout(kit_sizes=kit_sizes)
require(not layout.issues, "The Availability Zone plan has problems, nothing was generated:\n  " + "\n  ".join(layout.issues))
data = layout.data
HX, HY = layout.hx, layout.hy
unreal.log("AVAILABILITY_ZONE_PLAN %s" % layout.counts())

# ---------------------------------------------------------------- prerequisites (checked before the level is touched)
require(assets.does_directory_exist(EnvKit.MESH_FOLDER), "%s is missing: run Build/ImportEnvironmentKit.py first" % EnvKit.MESH_FOLDER)
for piece in EnvKit.SPEC:
    require(assets.does_asset_exist(EnvKit.mesh_path(piece)), "Missing %s: run Build/ImportEnvironmentKit.py first" % EnvKit.mesh_path(piece))
terrain_used = {"Watchtower"}
for rim in layout.rim_pieces:
    terrain_used.add(rim["piece"] + ("_Machine" if rim["look"] == "machine" else ""))
require(assets.does_directory_exist(TerrainKit.MESH_FOLDER), "%s is missing: run Build/ImportTerrainKit.py first" % TerrainKit.MESH_FOLDER)
for piece in sorted(terrain_used):
    require(assets.does_asset_exist(TerrainKit.mesh_path(piece)), "Missing %s: run Build/ImportTerrainKit.py first" % TerrainKit.mesh_path(piece))
for variant in TerrainKit.VARIANTS:
    require(assets.does_asset_exist(art.FOLDER + "/" + variant), "Missing %s: run Build/ImportTerrainKit.py first" % variant)

# ---------------------------------------------------------------- materials
FLAT = {"Metallic": 0.0, "EdgeWear": 0.0, "EdgeHighlight": 0.0, "PanelStrength": 0.0, "PanelLine": 0.0, "ClearCoat": 0.0}


def prim(name, parent, rgb, roughness=0.85, **scalars):
    """MI_AZ_* child of an MI_SC2_* instance for engine primitives: the cube and cylinder carry no vertex colours,
    which the master would read as white masks (everything worn and grimy), so UseBakedMasks goes off."""
    values = dict(FLAT, Roughness=roughness)
    values.update(scalars)
    return art._instance(name, art.shared(parent), {"BaseColor": rgb}, values, {"UseBakedMasks": False})


asphalt = prim("MI_AZ_Asphalt", "MI_SC2_Human_Dark_Env", (0.045, 0.048, 0.055), 0.9)
concrete = prim("MI_AZ_Concrete", "MI_SC2_Human_Dark_Env", (0.17, 0.175, 0.18), 0.85)
gravel = prim("MI_AZ_Gravel", "MI_SC2_Human_Dark_Env", (0.115, 0.10, 0.085), 0.95)
gravel_dark = prim("MI_AZ_GravelDark", "MI_SC2_Human_Dark_Env", (0.075, 0.068, 0.06), 0.95)
deck_plate = prim("MI_AZ_DeckPlate", "MI_SC2_Human_Dark_Env", (0.10, 0.115, 0.135), 0.55, Metallic=0.3,
                  PanelStrength=0.6, PanelLine=0.6)
steel = prim("MI_AZ_Steel", "MI_SC2_Human_Dark_Env", (0.18, 0.19, 0.2), 0.4, Metallic=0.9)
paint_amber = prim("MI_AZ_PaintAmber", "MI_SC2_Human_Dark_Env", (0.55, 0.42, 0.12), 0.6)
paint_blue = prim("MI_AZ_PaintBlue", "MI_SC2_Human_Dark_Env", (0.05, 0.22, 0.38), 0.6)
ramp_human = prim("MI_AZ_RampHuman", "MI_SC2_Human_Dark_Env", (0.07, 0.07, 0.075), 0.9)
m_tile = prim("MI_AZ_MachineTile", "MI_SC2_Machine_Dark_Env", (0.075, 0.085, 0.10), 0.3, Metallic=0.5,
              PanelStrength=0.5, PanelLine=0.5)
m_concrete = prim("MI_AZ_MachineConcrete", "MI_SC2_Machine_Dark_Env", (0.13, 0.145, 0.165), 0.8)
m_rock = prim("MI_AZ_MachineRock", "MI_SC2_Machine_Dark_Env", (0.05, 0.055, 0.07), 0.7)
# Level walls and rim blocks: the kit's earth rock on the human half, pearl panels on the Machine half.
wall_human = art._instance("MI_AZ_WallHuman", art.shared("MI_SC2_Human_Dark_Terrain"), {}, {}, {"UseBakedMasks": False})
wall_machine = art._instance("MI_AZ_WallMachine", art.shared("MI_SC2_Machine_Shell_Env"), {}, {},
                             {"UseBakedMasks": False})
hazard = art._instance("MI_AZ_Hazard", art.shared("MI_SC2_Human_Accent_Env"), {}, {}, {"UseBakedMasks": False})
cyan = art.glow("MI_AZ_GlowMachine", art.MACHINE_GLOW, 4.0)
cyan_dim = art.glow("MI_AZ_GlowCable", art.MACHINE_GLOW, 1.6)
amber = art.glow("MI_AZ_GlowHuman", art.HUMAN_GLOW, 5.0)
# Paint jobs for the Human kit props (children of MI_SC2_Human_Shell_Env overriding BaseColor only).
rust = art.shared_child("MI_AZ_Rust", "MI_SC2_Human_Shell_Env", BaseColor=(0.2, 0.11, 0.07))
olive = art.shared_child("MI_AZ_Olive", "MI_SC2_Human_Shell_Env", BaseColor=(0.1, 0.12, 0.09))
container_blue = art.shared_child("MI_AZ_ContainerBlue", "MI_SC2_Human_Shell_Env", BaseColor=(0.05, 0.12, 0.2))
sandbag = art.shared_child("MI_AZ_Sandbag", "MI_SC2_Human_Shell_Env", BaseColor=(0.13, 0.12, 0.10))

# Ground per region (design "Themes per area": ground and trim columns).
REGION_GROUND = {"main_H": deck_plate, "terrace_H": concrete, "pocket_SE": gravel, "pass_W": gravel,
                 "main_J": m_tile, "terrace_J": m_concrete, "pocket_NW": m_concrete, "pass_E": gravel}
WALL_OF = {"human": wall_human, "machine": wall_machine}
RIM_OF = {"human": wall_human, "machine": m_rock}
LIP_OF = {"human": hazard, "machine": cyan_dim}
DECK_OF = {"human": ramp_human, "machine": m_tile}
PAINTS = {"Container": (container_blue, rust, olive), "Wreck": (rust, olive), "SandbagWall": (sandbag,),
          "GeneratorShack": (), "BurnBarrel": (), "CableSpool": ()}

# ---------------------------------------------------------------- level
if assets.does_asset_exist(MAP_PATH):
    require(levels.load_level(MAP_PATH), "Could not load AvailabilityZone for replacement")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings):
            continue
        if isinstance(actor, unreal.Brush) and not isinstance(actor, unreal.Volume):
            continue
        require(actors.destroy_actor(actor), "Could not remove old actor " + actor.get_name())
else:
    require(levels.new_level(MAP_PATH, False), "Could not create AvailabilityZone")

cube = require(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"), "Engine cube is missing")
cylinder = require(unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder"), "Engine cylinder is missing")
blocking = []
counts = {"wall": 0, "parapet": 0, "rim_piece": 0, "rim_block": 0, "prop": 0, "hall": 0, "tower": 0, "other": 0}


def spawn(actor_class, label, location, rotation=unreal.Rotator()):
    actor = require(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation),
                    "Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


# ---------------------------------------------------------------- match actors (JSON-driven twin of MatchLayout.place)
capture_anchors = {}


def place_match_actors():
    m = data["match_actors"]
    arena_def = m["arena"]
    arena = spawn(MatchLayout.native_class("ArenaBounds"), "Arena", tuple(arena_def["pos"]))
    arena.set_editor_property("half_extent", unreal.Vector2D(float(arena_def["half_extent"][0]), float(arena_def["half_extent"][1])))
    arena.set_editor_property("placement_margin", float(arena_def["placement_margin"]))
    extent = arena.get_editor_property("half_extent")
    require(abs(extent.x - HX) < 0.5 and abs(extent.y - HY) < 0.5, "ArenaBounds did not take the JSON half extent")
    require(len(m["headquarters"]) == 2 and {h["team_index"] for h in m["headquarters"]} == {0, 5},
            "match_actors must place exactly the team 0 and team 5 Headquarters")
    for hq in m["headquarters"]:
        actor = spawn(MatchLayout.native_class("Headquarters"), hq["label"], tuple(hq["pos"]))
        actor.set_editor_property("team_index", hq["team_index"])
    by_index = {s["site_index"]: s for s in data["sectors"]}
    require(sorted(by_index) == list(range(len(data["sectors"]))) and len(m["sectors"]) == len(by_index),
            "sector site_index values must be 0..n-1 and match match_actors")
    for site_def in m["sectors"]:
        sector = by_index[site_def["site_index"]]
        require(abs(sector["pos"][0] - site_def["pos"][0]) < 0.5 and abs(sector["pos"][1] - site_def["pos"][1]) < 0.5
                and sector["site_kind"] == site_def["site_kind"],
                "sector %s differs between sectors[] and match_actors[]" % sector["id"])
        actor = spawn(MatchLayout.native_class("CapturePoint"), site_def["label"], tuple(site_def["pos"]))
        actor.set_editor_property("site_kind", getattr(unreal.CaptureSiteKind, site_def["site_kind"].upper()))
        region_index = site_def["site_index"] + 2  # Keep the v1 JSON's historical sector order unchanged.
        actor.set_editor_property("site_index", region_index)
        capture_anchors[region_index] = actor
        actor.set_editor_property("tags", [unreal.Name(sector["name"])])
    return float(extent.x), float(extent.y)


# Blocking geometry is vetted against the arena the game reads, so the match actors go in first.
ARENA_X, ARENA_Y = place_match_actors()
regions = az.gameplay_regions(data)
post_errors = layout.defend_post_errors()
require(not post_errors, "Invalid defend posts: " + "; ".join(post_errors))
deposits = MatchLayout.deposit_plan(
    regions, (ARENA_X, ARENA_Y),
    lambda point: layout.deposit_clear(point, MatchLayout.DEPOSIT_CLEARANCE),
    data["match_actors"]["arena"]["placement_margin"])
MatchLayout.place_regions(spawn, regions, deposits, capture_anchors)


# ---------------------------------------------------------------- placement helpers
def register_block(kind, label, center, size, yaw, round_shape):
    blocking.append(label)
    counts[kind] += 1
    unreal.log("AVAILABILITY_ZONE_BLOCK %s kind=%s cx=%.0f cy=%.0f sx=%.0f sy=%.0f yaw=%.0f cyl=%d" % (
        label, kind, center[0], center[1], size[0], size[1], yaw, round_shape))


def block(label, center, size, surface, yaw=0.0, base=0.0, collision=False, mesh=cube, kind="other", folder=None):
    """Place a box/cylinder by footprint centre, size in cm, and base height. Collision registers a blocking footprint."""
    if collision:
        register_block(kind, label, center, size, yaw, mesh is cylinder)
    actor = spawn(unreal.StaticMeshActor, label, (center[0], center[1], base + size[2] / 2), unreal.Rotator(yaw=yaw))
    if folder:
        actor.set_folder_path(folder)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, surface)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    return actor


def strip(label, a, b, width, surface, height=2.0, base=0.0, folder=None):
    """Flat decorative strip (roads, cables, paint) between two points; never blocks."""
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    yaw = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    return block(label, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), (length, width, height), surface, yaw, base, folder=folder)


def polyline(label, points, width, surface, height, folder, only=None):
    """Strips along a polyline; `only` (a look) limits it to segments whose midpoint is on that half of the map."""
    for index, (a, b) in enumerate(zip(points, points[1:])):
        if math.hypot(b[0] - a[0], b[1] - a[1]) < 1.0:
            continue
        if only and az.look_at((a[0] + b[0]) / 2, (a[1] + b[1]) / 2) != only:
            continue
        strip("%s%d" % (label, index), a, b, width, surface, height, folder=folder)


def light(label, location, rgb, intensity, radius):
    actor = spawn(unreal.PointLight, label, location)
    actor.set_folder_path("AZ/Lights")
    component = actor.get_component_by_class(unreal.PointLightComponent)
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("intensity", intensity)
    component.set_editor_property("attenuation_radius", radius)
    r, g, b = (int(v * 255) for v in rgb)
    component.set_editor_property("light_color", unreal.Color(r=r, g=g, b=b, a=255))  # positional args are B,G,R,A
    component.set_editor_property("cast_shadows", False)


def spot(label, wall_point, outward, height, reach, rgb, intensity, base=0.0):
    """Hard-edged pool: a spot on a hall wall, aimed down and out to a point `reach` cm from the wall."""
    location = (wall_point[0] + outward[0] * 10, wall_point[1] + outward[1] * 10, base + height)
    target = (wall_point[0] + outward[0] * reach, wall_point[1] + outward[1] * reach, base)
    dx, dy, dz = (target[i] - location[i] for i in range(3))
    rotation = unreal.Rotator(pitch=math.degrees(math.atan2(dz, math.hypot(dx, dy))), yaw=math.degrees(math.atan2(dy, dx)))
    actor = spawn(unreal.SpotLight, label, location, rotation)
    actor.set_folder_path("AZ/Lights")
    component = actor.get_component_by_class(unreal.SpotLightComponent)
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("intensity", intensity)
    component.set_editor_property("attenuation_radius", 900.0)
    component.set_editor_property("inner_cone_angle", POOL_INNER_CONE)
    component.set_editor_property("outer_cone_angle", POOL_OUTER_CONE)
    component.set_editor_property("source_radius", 0.0)
    component.set_editor_property("soft_source_radius", 0.0)
    r, g, b = (int(v * 255) for v in rgb)
    component.set_editor_property("light_color", unreal.Color(r=r, g=g, b=b, a=255))
    component.set_editor_property("cast_shadows", False)


def hall(label, center, size, doors, base, folder):
    """Assemble a data hall (the whole outer rectangle is one blocking footprint) and light its doors with cyan pools."""
    register_block("hall", label, center, size, 0.0, False)
    EnvKit.assemble_hall(label, center, size, doors, base=base, folder=folder)
    index = 0
    for piece, x, y, _yaw, _sx, _sy in EnvKit.hall_placements(size, doors):
        if piece != "DataHallDoor":
            continue
        outward = (math.copysign(1, x), 0) if abs(x) / size[0] > abs(y) / size[1] else (0, math.copysign(1, y))
        face = (center[0] + x + outward[0] * 150, center[1] + y + outward[1] * 150)
        tangent = (-outward[1], outward[0])
        for side in (-1, 1):
            point = (face[0] + tangent[0] * 300 * side, face[1] + tangent[1] * 300 * side)
            spot("%sSpot%d" % (label, index), point, outward, 440, 260, art.MACHINE_GLOW, POOL_INTENSITY, base)
            index += 1


# ---------------------------------------------------------------- ground
block("Ground", (0, 0), (2 * (HX + 200), 2 * (HY + 200), 100), gravel_dark, base=-100, collision=True, folder="AZ/Ground")
for rid, region in layout.regions.items():
    cx, cy, sx, sy, _ = az.Layout.rect_of_poly(region["poly"])
    block("Ground_" + rid, (cx, cy), (sx, sy, 1.0), REGION_GROUND[rid], folder="AZ/Ground")

# Haul roads along both attack routes (the two routes the design measures), dashed centre line on the human half.
for label in ("no_door", "no_gate"):
    points = layout.analysis.routes[label][1]
    polyline("Road_%s_" % label, points, 520, asphalt, 1.6, "AZ/Ground")
    dashes = 0
    for (a, b) in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        for k in range(int(length // 500)):
            t0, t1 = (k * 500 + 100) / length, (k * 500 + 300) / length
            p0 = (a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0)
            p1 = (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)
            if az.look_at(*p0) == "human":
                strip("RoadLine_%s_%d" % (label, dashes), p0, p1, 20, paint_amber, 1.9, folder="AZ/Ground")
                dashes += 1
# Gravel004-style patches on the passes and pockets (darker, coarser); pure paint, so no keep-out applies.
rng = az.Rng(9001)
for rid in ("pass_W", "pass_E", "pocket_SE", "pocket_NW"):
    cx, cy, sx, sy, _ = az.Layout.rect_of_poly(layout.regions[rid]["poly"])
    for index in range(10):
        px, py = cx + rng.uniform(-sx / 2 + 500, sx / 2 - 500), cy + rng.uniform(-sy / 2 + 500, sy / 2 - 500)
        block("Patch_%s_%d" % (rid, index), (px, py), (rng.uniform(500, 1400), rng.uniform(400, 900), 1.2), gravel_dark,
              yaw=rng.uniform(0, 180), folder="AZ/Ground")

# HQ plazas and the numbered build bays of the shared Bunker (design: "the pockets are a convention made legible by paint").
for hq in data["headquarters"]:
    machine = hq["side"] == "J"
    x, y = hq["pos"]
    block(hq["id"] + "Plaza", (x, y), (900, 900, 2.0), m_concrete if machine else concrete, mesh=cylinder, folder="AZ/Ground")
    block(hq["id"] + "PlazaRing", (x, y), (940, 940, 1.5), cyan_dim if machine else paint_amber, mesh=cylinder, folder="AZ/Ground")
for pocket in data["build_pockets"]:
    square = paint_amber if pocket["id"].endswith("_A") else paint_blue
    for bay in pocket["bays"]:
        x, y = bay["pos"]
        block("Bay" + bay["id"], (x, y), (250, 250, 2.2), square, folder="AZ/Ground")
        number = int(bay["id"][1:])
        for tick in range(number):
            block("Bay%sTick%d" % (bay["id"], tick), (x + (tick - (number - 1) / 2) * 50, y), (16, 120, 2.8), asphalt,
                  folder="AZ/Ground")
# Capture rings make each sector readable at strategic zoom, outside the native capture marker.
for sector in data["sectors"]:
    x, y = sector["pos"]
    machine = sector["side"] == "J"
    block("Sector%dRing" % (sector["site_index"] + 1), (x, y), (2 * CAPTURE_RADIUS + 20, 2 * CAPTURE_RADIUS + 20, 1.2),
          cyan_dim if machine else paint_amber, mesh=cylinder, folder="AZ/Ground")
    block("Sector%dRingInner" % (sector["site_index"] + 1), (x, y), (2 * CAPTURE_RADIUS - 20, 2 * CAPTURE_RADIUS - 20, 1.6),
          m_concrete if machine else concrete, mesh=cylinder, folder="AZ/Ground")
# Cyan cables from the Cluster to every Machine sector (the walks the layout measured).
for sector in data["sectors"]:
    if sector["side"] == "J":
        name = "S%d" % (sector["site_index"] + 1)
        polyline("Cable_" + name + "_", layout.analysis.table["HQ_J"][name][1], 24, cyan_dim, 3.0, "AZ/Ground")

# ---------------------------------------------------------------- level edges: walls, ramp gaps, rim
for wall in layout.walls:
    block(wall["label"], wall["center"], wall["size"] + (wall["height"],), WALL_OF[wall["look"]], collision=True,
          kind="wall", folder="AZ/Walls")
    coping = (wall["size"][0] + 30, wall["size"][1]) if wall["axis"] == "v" else (wall["size"][0], wall["size"][1] + 30)
    block(wall["label"] + "Lip", wall["center"], coping + (8.0,), LIP_OF[wall["look"]], base=wall["height"], folder="AZ/Walls")
for parapet in layout.parapets:
    block(parapet["label"], parapet["center"], parapet["size"] + (parapet["height"],), WALL_OF[parapet["look"]],
          collision=True, kind="parapet", folder="AZ/Ramps")
for deck in layout.decks:
    block(deck["label"], deck["center"], deck["size"] + (2.0,), DECK_OF[deck["look"]], folder="AZ/Ramps")
    along_x = deck["yaw"] % 180 == 0
    for side in (-1, 1):
        offset = side * (deck["size"][1 if along_x else 0] / 2 - 15)
        centre = (deck["center"][0], deck["center"][1] + offset) if along_x else (deck["center"][0] + offset, deck["center"][1])
        length = deck["size"][0 if along_x else 1]
        block("%sEdge%d" % (deck["label"], side), centre, (length, 30, 2.6) if along_x else (30, length, 2.6),
              hazard if deck["look"] == "human" else cyan_dim, folder="AZ/Ramps")

TERRAIN_YAW = {"Cliff_Straight": lambda piece: piece["yaw"], "Cliff_CornerOuter": lambda piece: piece["yaw"],
               "Plateau_Fill": lambda piece: 0.0}
for index, rim in enumerate(layout.rim_pieces):
    name = rim["piece"] + ("_Machine" if rim["look"] == "machine" else "")
    register_block("rim_piece", "Rim_%s_%d" % (name, index), rim["center"], (az.CELL, az.CELL), rim["yaw"], False)
    TerrainKit.place_piece(name, rim["center"], TERRAIN_YAW[rim["piece"]](rim), 0.0, label="Rim_%s_%d" % (name, index),
                           collision=True, folder="AZ/Rim")
for rim in layout.rim_blocks:
    block(rim["label"], rim["center"], rim["size"] + (rim["height"],), RIM_OF[rim["look"]], collision=True, kind="rim_block",
          folder="AZ/Rim")

# ---------------------------------------------------------------- the Slab, watchtowers, dressing
slab = layout.slab
require(slab is not None, "The JSON has no Slab blocker")
hall(slab["label"], slab["center"], slab["size"], slab["doors"], 0.0, "AZ/Slab")
for index, (tx, ty) in enumerate(slab["towers"]):
    EnvKit.place_piece("CoolingTower", (tx, ty), 0.0, 580.0, label="SlabCoolingTower%d" % index, collision=False,
                       folder="AZ/Slab")
for tower in layout.towers:
    register_block("tower", tower["label"], tower["center"], (400, 400), 0.0, True)
    TerrainKit.place_piece("Watchtower", tower["center"], 0.0, 0.0, label=tower["label"], collision=True, folder="AZ/Towers")

for index, prop in enumerate(layout.props):
    rect = (prop["center"][0], prop["center"][1], prop["size"][0], prop["size"][1], prop["yaw"])
    require(layout.keep_out_reason(rect) is None, "Prop %s intrudes on a keep-out" % prop["label"])
    register_block("prop", prop["label"], prop["center"], prop["size"], prop["yaw"], prop["round"])
    paints = PAINTS.get(prop["kit"], ())
    materials = {"Shell": paints[index % len(paints)]} if paints and prop["look"] == "human" else None
    EnvKit.place_piece(prop["kit"], prop["center"], prop["yaw"], 0.0, label=prop["label"], collision=True,
                       folder="AZ/Props", materials=materials)
for rim_hall in layout.halls:
    hall(rim_hall["label"], rim_hall["center"], rim_hall["size"], rim_hall["doors"], rim_hall["base"], "AZ/Backdrop")
for index, prop in enumerate(layout.rim_props):
    paints = PAINTS.get(prop["kit"], ())
    materials = {"Shell": paints[index % len(paints)]} if paints and prop["look"] == "human" else None
    register_block("prop", prop["label"], prop["center"], prop["size"], prop["yaw"], prop["round"])
    EnvKit.place_piece(prop["kit"], prop["center"], prop["yaw"], prop["base"], label=prop["label"], collision=True,
                       folder="AZ/Backdrop", materials=materials)

# ---------------------------------------------------------------- lights on top of the level edges and around the bases
for lamp in layout.lamps:
    machine = lamp["look"] == "machine"
    x, y = lamp["center"]
    block(lamp["label"], (x, y), (40, 40, 40), cyan if machine else amber, base=lamp["base"], folder="AZ/Lights")
    light(lamp["label"] + "Light", (x, y, lamp["base"] + 90), art.MACHINE_GLOW if machine else art.HUMAN_GLOW, 150, 500)
for hq in data["headquarters"]:
    x, y = hq["pos"]
    if hq["side"] == "J":
        light("ClusterLight", (x, y, 450), (1.0, 0.15, 0.1), 1100, 800)
    else:
        for index, dy in enumerate((-500, 500)):
            light("BunkerLampLight%d" % index, (x, y + dy, 350), art.HUMAN_GLOW, 150, 500)
for sector in data["sectors"]:
    machine = sector["side"] == "J"
    light("Sector%dLight" % (sector["site_index"] + 1), (sector["pos"][0], sector["pos"][1], 320),
          art.MACHINE_GLOW if machine else art.HUMAN_GLOW, 120, 600)

# ---------------------------------------------------------------- sky, player start, navigation
spawn(unreal.PlayerStart, "CommanderStart", (az.PLAYER_START[0], az.PLAYER_START[1], 150))
sun = spawn(unreal.DirectionalLight, "DuskSun", (0, 0, 3000), unreal.Rotator(pitch=-25, yaw=-28))
sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun_component.set_editor_property("intensity", SUN_LUX)
sun_component.set_editor_property("light_color", unreal.Color(r=SUN_COLOR[0], g=SUN_COLOR[1], b=SUN_COLOR[2], a=255))
sun_component.set_editor_property("atmosphere_sun_light", True)
spawn(unreal.SkyAtmosphere, "DuskAtmosphere", (0, 0, 0))
sky = spawn(unreal.SkyLight, "DuskSky", (0, 0, 2000))
sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_component.set_editor_property("intensity", SKY_INTENSITY)
sky_component.set_editor_property("real_time_capture", True)
sky_component.set_editor_property("light_color", unreal.Color(r=SKY_COLOR[0], g=SKY_COLOR[1], b=SKY_COLOR[2], a=255))
fog = spawn(unreal.ExponentialHeightFog, "DuskFog", (0, 0, 0))
fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
fog_component.set_editor_property("fog_density", 0.02)
fog_component.set_editor_property("fog_height_falloff", 0.15)
fog_component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.02, 0.035, 0.07, 1.0))
# Fixed exposure: auto-exposure brightens a dark scene until every emissive blows out.
grade = spawn(unreal.PostProcessVolume, "DuskGrade", (0, 0, 0))
grade.set_editor_property("unbound", True)
grade_settings = grade.get_editor_property("settings")
for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                    ("auto_exposure_bias", EXPOSURE_BIAS),
                    ("bloom_intensity", 0.35),
                    ("bloom_threshold", 1.0),
                    ("vignette_intensity", 0.35)):
    grade_settings.set_editor_property("override_" + name, True)
    grade_settings.set_editor_property(name, value)
grade.set_editor_property("settings", grade_settings)

nav = data["navigation"]["bounds_volume"]
bounds = spawn(unreal.NavMeshBoundsVolume, "ArenaNavigationBounds", tuple(nav["centre"]))
_, extent = bounds.get_actor_bounds(False)
require(min(extent.x, extent.y, extent.z) > 0.0,
        "Editor factory did not construct a navigation brush; cannot scale an empty volume")
bounds.set_actor_scale3d(unreal.Vector(nav["half_extent"][0] / extent.x, nav["half_extent"][1] / extent.y,
                                       nav["half_extent"][2] / extent.z))
_, nav_extent = bounds.get_actor_bounds(False)
require(all(abs(got - want) < 1.0 for got, want in zip((nav_extent.x, nav_extent.y, nav_extent.z), nav["half_extent"])),
        "Navigation bounds did not resize to %s" % (nav["half_extent"],))
require(nav_extent.x >= ARENA_X and nav_extent.y >= ARENA_Y, "Navigation bounds do not cover the arena")


def nav_value(nav_mesh, key):
    """Logging only: the agent properties are what the design assumed (radius 35, height 144, slope 44.8 at most)."""
    try:
        return nav_mesh.get_editor_property(key)
    except Exception:
        return "n/a"


nav_meshes = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.RecastNavMesh)]
if not nav_meshes:
    nav_meshes = [spawn(unreal.RecastNavMesh, "RecastNavMesh-Default", (0, 0, 0))]
for nav_mesh in nav_meshes:
    nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    nav_mesh.set_editor_property("force_rebuild_on_load", True)
    unreal.log("AVAILABILITY_ZONE_NAV %s" % {key: nav_value(nav_mesh, key) for key in ("agent_radius", "agent_height",
                                                                                     "agent_max_slope")})

world = editor.get_editor_world()
world.get_world_settings().set_editor_property(
    "default_game_mode", require(unreal.load_class(None, "/Script/CoopRTS.CommandGameMode"),
                                 "Build CoopRTSEditor before generating the map"))
require(levels.save_current_level(), "Could not save AvailabilityZone")
unreal.log("AVAILABILITY_ZONE_GENERATED %s blocking=%d (walls=%d parapets=%d rim_pieces=%d rim_blocks=%d props=%d "
           "halls=%d towers=%d) actors=%d" % (
               MAP_PATH, len(blocking), counts["wall"], counts["parapet"], counts["rim_piece"], counts["rim_block"],
               counts["prop"], counts["hall"], counts["tower"], len(actors.get_all_level_actors())))
