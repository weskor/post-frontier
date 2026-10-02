"""Create or replace /Game/Maps/CampusZero, the first themed greybox (see Docs/World.md).

Human scrapyard in the west, data-centre campus in the east. The layout is built around
the match actors Build/MatchLayout.py places (arena, sites, HQs) plus the army homes, so
the existing match plays unchanged on it. Boot and the automated tests are untouched.

Run order (from the project root, editor closed, each step its own editor process, no other Unreal process
from this repo running):
  0. Build/BuildSharedMaterial.py    builds M_Shared and the MI_SC2_* instances (-RenderOffscreen; see its docstring)
  1. Build/ImportEnvironmentKit.py   imports the /Game/Art/Environment kit this map places; require
                                     ENV_KIT_IMPORTED 17 in its log
  2. Build/GenerateCampusZero.py     after compiling CoopRTSEditor:
  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateCampusZero.py" \
    -unattended -nullrhi -nosplash
Require CAMPUS_ZERO_GENERATED in the log. Reruns replace every actor in CampusZero. Without step 1 the
script stops at its first check ("run Build/ImportEnvironmentKit.py first"); there is no primitive fallback.

Halls, towers, chillers, transformers, the pylons, the mast, containers, wrecks, sandbags, barrels, the
generator shack, fences and cable spools are environment-kit pieces (Build/EnvKit.py) placed at the footprint
the old boxes and cylinders had, and still pass footprint_clear() / KEEP_OUT. Their collision is the simple
collision baked into the imported meshes. Glow strips, road/ground/ring decals and lights stay primitives.
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art
import EnvKit
import MatchLayout

require = art.require
assets = art.assets
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

# Mirrors ACapturePoint (Source/CoopRTS). If it moves, update here. Arena extent is read from the placed actor below.
CAPTURE_RADIUS = 430.0
SITES = MatchLayout.SITES
FRIENDLY_HQ = MatchLayout.FRIENDLY_HQ
ENEMY_HQ = MatchLayout.ENEMY_HQ
ENEMY_HOME = (1800, 2300)
HOMES = [(x, y) for x in (-1800, -2800) for y in (0, -850, 850, -1700, 1700)]
# Keep-out circles for blocking geometry: capture ring + margin, formation space, HQ bodies, and the
# milestone 9 finale spots below. The objective itself reuses the two Resource sites' CaptureRadius rings
# (ACapturePoint scan); the shield-down assault hits the enemy HQ body. No M9 actor or position is new.
# Dev-only spots mirrored so fixtures can run here: ArmyNetworkVerification's HQ assault anchor
# (enemy HQ + (900, 700), units spread +-110) and ArmyObjectiveTests' parking points for off-site units.
M9_ASSAULT_ANCHOR = (ENEMY_HQ[0] + 900, ENEMY_HQ[1] + 700)
M9_PARKING = [(4300, 4300), (4100, 4300)]
KEEP_OUT = ([(p, CAPTURE_RADIUS + 120) for p in SITES.values()]
            + [(p, 450) for p in HOMES + [ENEMY_HOME]]
            + [(FRIENDLY_HQ, 420), (ENEMY_HQ, 420)]
            + [(M9_ASSAULT_ANCHOR, 300)] + [(p, 200) for p in M9_PARKING])
# Lighting knobs (dusk mood): tuned from in-game captures, see Saved/Verification/campus-zero-*.
SUN_LUX = 5.0
SKY_INTENSITY = 3.5
EXPOSURE_BIAS = 10.0
POOL_INNER_CONE = 16.0  # campus spot cone, degrees; a narrow inner-to-outer gap keeps the pool edge sharp
POOL_OUTER_CONE = 19.0
POOL_INTENSITY = 320.0
SUN_COLOR = (235, 238, 250)  # near-neutral cool moonlight; the old (255, 200, 160) tinted every lit face tan
SKY_COLOR = (180, 200, 245)

# ---------------------------------------------------------------- materials
floor_mat = art.surface("MI_Asphalt", (0.045, 0.048, 0.055), 0.9)
scrap_ground = art.surface("MI_ScrapGround", (0.088, 0.085, 0.08), 0.95)
campus_ground = art.surface("MI_CampusConcrete", (0.12, 0.13, 0.14), 0.75)
road_line = art.surface("MI_RoadLine", (0.55, 0.45, 0.18), 0.6)
concrete = art.surface("MI_Concrete", (0.22, 0.22, 0.23), 0.85)
steel = art.surface("MI_Steel", (0.18, 0.19, 0.2), 0.4, 0.9)
cyan = art.glow("MI_GlowMachine", art.MACHINE_GLOW, 4.0)
cyan_dim = art.glow("MI_GlowCable", art.MACHINE_GLOW, 1.6)
# Paint jobs for the Human kit pieces (Shell slot override): children of MI_SC2_Human_Shell_Env that override BaseColor
# only, so containers, wrecks and sandbags keep the master's wear, grime, panels and static switches but are not one
# colour. Needs Build/BuildSharedMaterial.py.
rust = art.shared_child("MI_Rust", "MI_SC2_Human_Shell_Env", BaseColor=(0.2, 0.11, 0.07))
olive = art.shared_child("MI_Olive", "MI_SC2_Human_Shell_Env", BaseColor=(0.1, 0.12, 0.09))
container_blue = art.shared_child("MI_ContainerBlue", "MI_SC2_Human_Shell_Env", BaseColor=(0.05, 0.12, 0.2))
sandbag = art.shared_child("MI_Sandbag", "MI_SC2_Human_Shell_Env", BaseColor=(0.13, 0.12, 0.10))
amber = art.glow("MI_GlowHuman", art.HUMAN_GLOW, 5.0)
team_materials = [require(assets.load_asset("/Game/Materials/MI_CommandTeam" + str(i)),
                          "Run Build/GenerateCommandMap.py first: missing team material")
                  for i in range(5)]

# The kit is placed as-is; check before the level is touched so a missing import leaves CampusZero unchanged.
require(assets.does_directory_exist(EnvKit.MESH_FOLDER),
        "%s is missing: run Build/ImportEnvironmentKit.py first" % EnvKit.MESH_FOLDER)
for piece in EnvKit.SPEC:
    require(assets.does_asset_exist(EnvKit.mesh_path(piece)),
            "Missing %s: run Build/ImportEnvironmentKit.py first" % EnvKit.mesh_path(piece))

# ---------------------------------------------------------------- level
map_path = "/Game/Maps/CampusZero"
if assets.does_asset_exist(map_path):
    require(levels.load_level(map_path), "Could not load CampusZero for replacement")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings):
            continue
        if isinstance(actor, unreal.Brush) and not isinstance(actor, unreal.Volume):
            continue
        require(actors.destroy_actor(actor), "Could not remove old actor " + actor.get_name())
else:
    require(levels.new_level(map_path, False), "Could not create CampusZero")

cube = require(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"), "Engine cube is missing")
cylinder = require(unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder"), "Engine cylinder is missing")
blocking = []
blocking_footprints = []


def spawn(actor_class, label, location, rotation=unreal.Rotator()):
    actor = require(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation),
                    "Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


# Blocking geometry is vetted against the arena the game reads, so the match actors go in first.
capture_anchors = {}
ARENA_X, ARENA_Y = MatchLayout.place(spawn, capture_anchors)


def footprint_clear(center, size, yaw):
    """Reject blocking geometry whose rotated footprint enters a gameplay keep-out circle."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    for (px, py), radius in KEEP_OUT:
        # Point in the box's local frame, then distance to the box rectangle.
        dx, dy = px - center[0], py - center[1]
        lx, ly = dx * c + dy * s, -dx * s + dy * c
        ox = max(abs(lx) - size[0] / 2, 0.0)
        oy = max(abs(ly) - size[1] / 2, 0.0)
        if math.hypot(ox, oy) < radius:
            raise RuntimeError("Blocking geometry at %s intrudes on keep-out %s" % (center, (px, py)))
    c_abs, s_abs = abs(c), abs(s)
    ext_x = c_abs * size[0] / 2 + s_abs * size[1] / 2
    ext_y = s_abs * size[0] / 2 + c_abs * size[1] / 2
    if abs(center[0]) + ext_x > ARENA_X or abs(center[1]) + ext_y > ARENA_Y:
        raise RuntimeError("Blocking geometry at %s leaves the arena" % (center,))


def register_block(label, center, size, yaw, round_shape):
    """Vet a blocking footprint against the keep-outs and record it: one CAMPUS_ZERO_BLOCK line per footprint."""
    footprint_clear(center, size, yaw)
    blocking.append(label)
    blocking_footprints.append((center[0], center[1], size[0], size[1], yaw))
    unreal.log("CAMPUS_ZERO_BLOCK %s cx=%.0f cy=%.0f sx=%.0f sy=%.0f yaw=%.0f cyl=%d" % (
        label, center[0], center[1], size[0], size[1], yaw, round_shape))


def block(label, center, size, surface, yaw=0.0, base=0.0, collision=True, mesh=cube, check=True):
    """Place a box/cylinder by footprint centre, size in cm, and base height."""
    if collision and check:
        register_block(label, center, size, yaw, mesh is cylinder)
    actor = spawn(unreal.StaticMeshActor, label, (center[0], center[1], base + size[2] / 2),
                  unreal.Rotator(yaw=yaw))
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, surface)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    return actor


def kit_footprint(name, size):
    """Fail if kit piece `name` is not the footprint of the primitive it replaces (cm, within 1 cm)."""
    fx, fy, _ = EnvKit.piece_size(name)
    require(abs(fx - size[0]) <= 1.0 and abs(fy - size[1]) <= 1.0,
            "Kit piece %s is %.0f x %.0f cm but the map expects %.0f x %.0f" % (name, fx, fy, size[0], size[1]))


def kit(name, label, center, size, yaw=0.0, base=0.0, collision=True, round_shape=False, materials=None):
    """Place one kit piece where block() put a box/cylinder: footprint centre, size (x, y) cm, yaw, base height.
    `materials` overrides kit slots for this actor (see EnvKit.place_piece)."""
    kit_footprint(name, size)
    if collision:
        register_block(label, center, EnvKit.collision_size(name), yaw, round_shape)
    return EnvKit.place_piece(name, center, yaw, base, label=label, collision=collision, materials=materials)


def kit_hall(label, center, size, doors):
    """Assemble a data hall from kit modules; the whole outer rectangle is one blocking footprint.
    `doors` counts door modules per Unreal world side (N = +Y)."""
    register_block(label, center, size, 0.0, False)
    return EnvKit.assemble_hall(label, center, size, doors)


def kit_run(name, label, center, size, along_local_x, materials=None):
    """Tile a straight wall footprint (x, y size in cm) with kit segments; the run is one blocking footprint.
    Segments are long along their local X (fences) or local Y (sandbags) and stretch to fill the run."""
    fx, fy, _ = EnvKit.piece_size(name)
    piece_length, piece_width = (fx, fy) if along_local_x else (fy, fx)
    run_along_y = size[1] >= size[0]
    length, width = (size[1], size[0]) if run_along_y else (size[0], size[1])
    require(abs(width - piece_width) <= 1.0,
            "Kit piece %s is %.0f cm thick but the run %s is %.0f" % (name, piece_width, label, width))
    count = max(1, int(round(length / piece_length)))
    stretch = length / (count * piece_length)
    yaw = 90.0 if run_along_y == along_local_x else 0.0
    scale = (stretch, 1.0, 1.0) if along_local_x else (1.0, stretch, 1.0)
    register_block(label, center, size, 0.0, False)
    for index in range(count):
        offset = (index + 0.5) * length / count - length / 2
        point = (center[0], center[1] + offset) if run_along_y else (center[0] + offset, center[1])
        EnvKit.place_piece(name, point, yaw, 0.0, scale, "%sSeg%d" % (label, index), True, materials=materials)


def strip(label, a, b, width, surface, height=2.0, base=0.0):
    """Flat decorative strip (cables, road paint) between two points; never blocks."""
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    yaw = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    return block(label, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), (length, width, height), surface,
                 yaw, base, collision=False)


def light(label, location, rgb, intensity, radius):
    actor = spawn(unreal.PointLight, label, location)
    component = actor.get_component_by_class(unreal.PointLightComponent)
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("intensity", intensity)
    component.set_editor_property("attenuation_radius", radius)
    r, g, b = (int(v * 255) for v in rgb)
    component.set_editor_property("light_color", unreal.Color(r=r, g=g, b=b, a=255))  # positional args are B,G,R,A
    component.set_editor_property("cast_shadows", False)


def spot(label, wall_point, outward, height, reach, rgb, intensity):
    """Hard-edged pool: a spot on a hall wall, aimed down and out to a point `reach` cm from the wall."""
    location = (wall_point[0] + outward[0] * 10, wall_point[1] + outward[1] * 10, height)
    target = (wall_point[0] + outward[0] * reach, wall_point[1] + outward[1] * reach, 0.0)
    dx, dy, dz = (target[i] - location[i] for i in range(3))
    rotation = unreal.Rotator(pitch=math.degrees(math.atan2(dz, math.hypot(dx, dy))),
                              yaw=math.degrees(math.atan2(dy, dx)))
    actor = spawn(unreal.SpotLight, label, location, rotation)
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


# The same authored geometry is emitted by the offline defend-post checks.
MatchLayout.place_campus_zero_geometry(
    (ARENA_X, ARENA_Y), block=block, kit=kit, kit_hall=kit_hall, kit_run=kit_run,
    strip=strip, light=light,
    surfaces={
        "floor_mat": floor_mat, "scrap_ground": scrap_ground, "campus_ground": campus_ground,
        "road_line": road_line, "concrete": concrete, "steel": steel, "cyan": cyan,
        "cyan_dim": cyan_dim, "rust": rust, "olive": olive, "container_blue": container_blue,
        "sandbag": sandbag, "amber": amber, "team_materials": team_materials,
        "cylinder": cylinder, "human_glow": art.HUMAN_GLOW, "capture_radius": CAPTURE_RADIUS,
    })

regions = MatchLayout.region_plan((ARENA_X, ARENA_Y), defend_posts=MatchLayout.CAMPUS_ZERO_DEFEND_POSTS)
post_errors = MatchLayout.defend_post_errors(
    regions, (ARENA_X, ARENA_Y),
    lambda point, clearance: MatchLayout.clear_of_blockers(point, blocking_footprints, clearance),
    headquarters=[home[1] for home in MatchLayout.HEADQUARTERS])
require(not post_errors, "Invalid defend posts: " + "; ".join(post_errors))
deposits = MatchLayout.deposit_plan(regions, (ARENA_X, ARENA_Y),
                                   lambda point: MatchLayout.clear_of_blockers(point, blocking_footprints))
MatchLayout.place_regions(spawn, regions, deposits, capture_anchors)

# ---------------------------------------------------------------- campus lights
# Wall-mounted spots beside each hall's glow seam; the cone edge makes a small, sharp pool instead of a blob.
for index, (point, outward) in enumerate((
        ((3100, 100), (-1, 0)), ((3100, 800), (-1, 0)),  # HallA west face
        ((3000, -1250), (0, 1)), ((3600, -1250), (0, 1)), ((3300, -2050), (0, -1)),  # HallB
        ((200, 3300), (0, -1)), ((800, 3300), (0, -1)),  # HallC south face, flanking its door (x 300..700)
        ((2900, 3450), (0, -1)), ((2350, 3750), (-1, 0)))):  # HallD
    spot("CampusSpot%d" % index, point, outward, 440, 260, art.MACHINE_GLOW, POOL_INTENSITY)
light("ClusterLight", (ENEMY_HQ[0], ENEMY_HQ[1], 450), (1.0, 0.15, 0.1), 1100, 800)

# ---------------------------------------------------------------- sky, player start, navigation
spawn(unreal.PlayerStart, "CommanderStart", (-1800, 0, 150))
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

bounds = spawn(unreal.NavMeshBoundsVolume, "ArenaNavigationBounds", (0, 0, 150))
_, extent = bounds.get_actor_bounds(False)
require(min(extent.x, extent.y, extent.z) > 0.0,
        "Editor factory did not construct a navigation brush; cannot scale an empty volume")
bounds.set_actor_scale3d(unreal.Vector(ARENA_X / extent.x, ARENA_Y / extent.y, 450 / extent.z))
nav_meshes = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.RecastNavMesh)]
if not nav_meshes:
    nav_meshes = [spawn(unreal.RecastNavMesh, "RecastNavMesh-Default", (0, 0, 0))]
for nav_mesh in nav_meshes:
    nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    nav_mesh.set_editor_property("force_rebuild_on_load", True)

world = editor.get_editor_world()
world.get_world_settings().set_editor_property(
    "default_game_mode", require(unreal.load_class(None, "/Script/CoopRTS.CommandGameMode"),
                                 "Build CoopRTSEditor before generating the map"))
require(levels.save_current_level(), "Could not save CampusZero")
unreal.log("CAMPUS_ZERO_GENERATED %s blocking=%d actors=%d" % (
    map_path, len(blocking), len(actors.get_all_level_actors())))
