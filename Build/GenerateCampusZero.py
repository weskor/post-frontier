"""Create or replace /Game/Maps/CampusZero, the first themed greybox (see Docs/World.md).

Human scrapyard in the west, data-centre campus in the east. The layout is built around
the prototype's hard-coded gameplay coordinates in ACommandGameMode (sites, HQs, army
homes) and the +/-4500 arena bounds, so the existing match plays unchanged on it.
Boot and the automated tests are untouched.

Run after compiling CoopRTSEditor (from the project root, editor closed):
  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateCampusZero.py" \
    -unattended -nullrhi -nosplash
Require CAMPUS_ZERO_GENERATED in the log. Reruns replace every actor in CampusZero.
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art  # noqa: E402

require = art.require
assets = art.assets
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

# Mirrors ACommandGameMode / ACapturePoint / AHeadquarters (Source/CoopRTS). If those move, update here.
ARENA = 4500.0
CAPTURE_RADIUS = 430.0
SITES = {"Substation7": (-850, -1800), "CoolingPlant": (1450, 1100), "FibreJunction": (600, -2200)}
FRIENDLY_HQ = (-3500, -600)
ENEMY_HQ = (3200, 2300)
ENEMY_HOME = (1800, 2300)
HOMES = [(x, y) for x in (-1800, -2800) for y in (0, -850, 850, -1700, 1700)]
# Keep-out circles for blocking geometry: capture ring + margin, formation space, HQ bodies.
KEEP_OUT = ([(p, CAPTURE_RADIUS + 120) for p in SITES.values()]
            + [(p, 450) for p in HOMES + [ENEMY_HOME]]
            + [(FRIENDLY_HQ, 420), (ENEMY_HQ, 420)])
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
hall_shell = art.surface("MI_HallShell", (0.28, 0.30, 0.33), 0.45, 0.3)
hall_dark = art.surface("MI_HallDark", (0.05, 0.055, 0.065), 0.5, 0.4)
tower_mat = art.surface("MI_CoolingTower", (0.26, 0.28, 0.31), 0.8)
rust = art.surface("MI_Rust", (0.2, 0.11, 0.07), 0.9, 0.2)
olive = art.surface("MI_Olive", (0.1, 0.12, 0.09), 0.85)
container_blue = art.surface("MI_ContainerBlue", (0.05, 0.12, 0.2), 0.8, 0.2)
sandbag = art.surface("MI_Sandbag", (0.2, 0.19, 0.16), 0.95)
steel = art.surface("MI_Steel", (0.18, 0.19, 0.2), 0.4, 0.9)
cyan = art.glow("MI_GlowMachine", art.MACHINE_GLOW, 4.0)
cyan_dim = art.glow("MI_GlowCable", art.MACHINE_GLOW, 1.6)
amber = art.glow("MI_GlowHuman", art.HUMAN_GLOW, 5.0)
machine_red = art.glow("MI_GlowMachineEye", (1.0, 0.05, 0.03), 8.0)
team_materials = [require(assets.load_asset("/Game/Materials/MI_CommandTeam" + str(i)),
                          "Run Build/GenerateCommandMap.py first: missing team material")
                  for i in range(5)]

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


def spawn(actor_class, label, location, rotation=unreal.Rotator()):
    actor = require(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation),
                    "Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


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
    if abs(center[0]) + ext_x > ARENA or abs(center[1]) + ext_y > ARENA:
        raise RuntimeError("Blocking geometry at %s leaves the arena" % (center,))


def block(label, center, size, surface, yaw=0.0, base=0.0, collision=True, mesh=cube, check=True):
    """Place a box/cylinder by footprint centre, size in cm, and base height."""
    if collision and check:
        footprint_clear(center, size, yaw)
        blocking.append(label)
        unreal.log("CAMPUS_ZERO_BLOCK %s cx=%.0f cy=%.0f sx=%.0f sy=%.0f yaw=%.0f cyl=%d" % (
            label, center[0], center[1], size[0], size[1], yaw, mesh is cylinder))
    actor = spawn(unreal.StaticMeshActor, label, (center[0], center[1], base + size[2] / 2),
                  unreal.Rotator(yaw=yaw))
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, surface)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    return actor


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


# ---------------------------------------------------------------- ground
block("Ground", (0, 0), (9000, 9000, 100), floor_mat, base=-100, check=False)
block("GroundScrapyard", (-3000, 0), (3000, 9000, 1), scrap_ground, collision=False)
block("GroundCampus", (3100, 0), (2800, 9000, 1), campus_ground, collision=False)
# Road from the Bunker, along the south flank, into the campus gate.
for index, (a, b) in enumerate((((-3500, -1150), (-850, -1150)), ((-850, -1150), (600, -1500)),
                                ((600, -1500), (2600, -1500)), ((2600, -1500), (2600, 1500)))):
    strip("Road%d" % index, a, b, 360, concrete, 1.5)
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    for dash in range(int(length // 400)):
        t0, t1 = (dash * 400 + 100) / length, (dash * 400 + 300) / length
        strip("Road%dDash%d" % (index, dash), (a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0),
              (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1), 18, road_line, 2.5)

# ---------------------------------------------------------------- centre: Data Hall 0
# Same footprint as Boot's CentralObstacle so crossing times stay comparable.
block("DataHall0", (0, 0), (1200, 2000, 520), hall_shell)
block("DataHall0Roof", (0, 0), (1000, 1800, 60), hall_dark, base=520, collision=False)
for side, y in (("South", -1004), ("North", 1004)):
    block("DataHall0Glow" + side, (0, y), (1100, 10, 24), cyan, base=380, collision=False)
for side, x in (("West", -604), ("East", 604)):
    block("DataHall0Glow" + side, (x, 0), (10, 1900, 24), cyan, base=380, collision=False)
    for door, y in enumerate((-600, 0, 600)):
        block("DataHall0Door%s%d" % (side, door), (x, y), (12, 220, 300), hall_dark, collision=False)
for index, (x, y) in enumerate(((-300, -600), (300, -600), (-300, 0), (300, 0), (-300, 600), (300, 600))):
    block("DataHall0Chiller%d" % index, (x, y), (320, 320, 120), steel, base=580, collision=False)
    block("DataHall0Fan%d" % index, (x, y), (220, 220, 8), cyan_dim, base=700, collision=False, mesh=cylinder)

# ---------------------------------------------------------------- east: the campus
halls = (("HallA", (3550, 450), (900, 1300)), ("HallB", (3300, -1650), (1300, 800)),
         ("HallC", (500, 3650), (1700, 700)), ("HallD", (2900, 3750), (1100, 600)))
for name, center, (sx, sy) in halls:
    block(name, center, (sx, sy, 600), hall_shell)
    block(name + "Roof", center, (sx - 120, sy - 120, 50), hall_dark, base=600, collision=False)
    for side, offset in (("S", -1), ("N", 1)):
        block("%sGlow%s" % (name, side), (center[0], center[1] + offset * (sy / 2 + 4)),
              (sx - 100, 10, 20), cyan, base=460, collision=False)
    for side, offset in (("W", -1), ("E", 1)):
        block("%sGlow%s" % (name, side), (center[0] + offset * (sx / 2 + 4), center[1]),
              (10, sy - 100, 20), cyan, base=460, collision=False)
for index, (x, y) in enumerate(((2400, -3200), (3500, -3300), (3950, -2600))):
    block("CoolingTower%d" % index, (x, y), (560, 560, 900), tower_mat, mesh=cylinder)
    block("CoolingTowerRim%d" % index, (x, y), (600, 600, 40), hall_dark, base=900, collision=False, mesh=cylinder)
    block("CoolingTowerPlume%d" % index, (x, y), (380, 380, 6), cyan_dim, base=942, collision=False, mesh=cylinder)
# Campus perimeter fence with wide gates (north and south spans only; the middle stays open).
for index, (x0, y0, y1) in enumerate(((1900, 2850, 4400), (1150, -4400, -2750))):
    block("CampusFence%d" % index, (x0, (y0 + y1) / 2), (30, y1 - y0, 220), steel)
    for post in range(int((y1 - y0) // 400) + 1):
        block("CampusFence%dLamp%d" % (index, post), (x0, y0 + post * 400), (40, 40, 30), cyan,
              base=220, collision=False)
# Machine HQ plaza: a lit ring the Cluster (AHeadquarters) stands in; nothing blocks inside it.
block("ClusterPlaza", ENEMY_HQ, (900, 900, 2), concrete, collision=False, mesh=cylinder)
block("ClusterPlazaRing", ENEMY_HQ, (940, 940, 1.5), cyan, collision=False, mesh=cylinder)
for index, angle in enumerate(range(0, 360, 45)):
    x = ENEMY_HQ[0] + 650 * math.cos(math.radians(angle))
    y = ENEMY_HQ[1] + 650 * math.sin(math.radians(angle))
    if abs(x) < ARENA - 100 and abs(y) < ARENA - 100:
        block("ClusterPylon%d" % index, (x, y), (70, 70, 420), hall_shell)
        block("ClusterPylonEye%d" % index, (x, y), (80, 80, 40), machine_red, base=420, collision=False)

# ---------------------------------------------------------------- capture sites
# Substation 7: transformer yard and a pylon; cables run to the Cluster.
sx, sy = SITES["Substation7"]
for index, (dx, dy) in enumerate(((-600, -550), (600, -450), (650, 250))):
    block("Substation7Transformer%d" % index, (sx + dx, sy + dy), (220, 170, 260), steel)
    block("Substation7Coil%d" % index, (sx + dx, sy + dy), (60, 60, 90), cyan, base=260, collision=False, mesh=cylinder)
for index, (dx, dy) in enumerate(((-60, -60), (60, -60), (-60, 60), (60, 60))):
    block("Substation7PylonLeg%d" % index, (sx + dx, sy - 900 + dy), (30, 30, 1300), steel)
block("Substation7PylonArm", (sx, sy - 900), (700, 40, 40), steel, base=1150, collision=False)
for side in (-1, 1):
    block("Substation7Insulator%d" % (side + 1), (sx + side * 320, sy - 900), (40, 40, 70), cyan,
          base=1080, collision=False)
# Cooling Plant: chiller drums around the Machine side of the ring.
cx, cy = SITES["CoolingPlant"]
for index, (dx, dy) in enumerate(((700, -150), (700, 350), (250, -750))):
    block("CoolingPlantChiller%d" % index, (cx + dx, cy + dy), (260, 260, 300), tower_mat, mesh=cylinder)
    block("CoolingPlantFan%d" % index, (cx + dx, cy + dy), (200, 200, 6), cyan_dim, base=300,
          collision=False, mesh=cylinder)
# Fibre Junction: comms mast and cable spools.
fx, fy = SITES["FibreJunction"]
block("FibreJunctionMast", (fx + 150, fy - 650), (80, 80, 1500), steel)
block("FibreJunctionMastBeacon", (fx + 150, fy - 650), (100, 100, 60), cyan, base=1500, collision=False)
for index, (dx, dy) in enumerate(((-720, -150), (-640, 380), (720, 250))):
    block("FibreJunctionSpool%d" % index, (fx + dx, fy + dy), (200, 200, 140), rust, mesh=cylinder)
# Cyan cables from every site to the Cluster (future hook: tint amber while humans hold the site).
for name, (x, y) in SITES.items():
    corner = (2600, y)
    strip(name + "CableA", (x, y), corner, 24, cyan_dim, 3)
    strip(name + "CableB", corner, (2600, ENEMY_HQ[1]), 24, cyan_dim, 3)
strip("ClusterCableTrunk", (2600, ENEMY_HQ[1]), ENEMY_HQ, 40, cyan_dim, 3)
# Ground rings make each site readable at strategic zoom, outside the native capture marker.
for name, center in SITES.items():
    block(name + "Ring", center, (2 * CAPTURE_RADIUS + 20, 2 * CAPTURE_RADIUS + 20, 1.2), road_line,
          collision=False, mesh=cylinder)
    block(name + "RingInner", center, (2 * CAPTURE_RADIUS - 20, 2 * CAPTURE_RADIUS - 20, 1.6), floor_mat,
          collision=False, mesh=cylinder)

# ---------------------------------------------------------------- west: the scrapyard
for index, (center, yaw, surface_mat) in enumerate((((-3900, 1600), 90, container_blue), ((-3650, 2700), 10, rust),
                                                    ((-2300, 3400), 0, olive), ((-3900, -2500), 80, rust),
                                                    ((-2500, -3350), -8, container_blue), ((-1200, 3500), 20, rust))):
    block("Container%d" % index, center, (600, 245, 260), surface_mat, yaw)
    block("ContainerStack%d" % index, (center[0] + 40, center[1]), (600, 245, 260), olive if index % 2 else rust,
          yaw + 4, base=260, collision=False)
for index, (center, yaw) in enumerate((((-1100, 2600), 30), ((-400, -3500), -20), ((-4000, 3700), 70),
                                       ((-3200, -3900), 5), ((-600, 2300), -60))):
    block("Wreck%d" % index, center, (420, 190, 130), rust, yaw)
    block("WreckCab%d" % index, center, (200, 170, 90), steel, yaw, base=130, collision=False)
# The Bunker's yard: sandbag walls behind and beside the HQ, a flag mast, work lamps.
hx, hy = FRIENDLY_HQ
for index, (center, size) in enumerate((((hx - 520, hy), (120, 900, 110)), ((hx - 150, hy + 560), (600, 120, 110)),
                                        ((hx - 150, hy - 560), (600, 120, 110)))):
    block("BunkerSandbags%d" % index, center, size, sandbag)
block("BunkerMast", (hx - 520, hy + 520), (40, 40, 900), steel)
block("BunkerBanner", (hx - 520, hy + 600), (10, 140, 200), team_materials[0], base=650, collision=False)
for index, (dx, dy) in enumerate(((-520, -400), (-520, 400), (180, 560), (180, -560))):
    block("BunkerLamp%d" % index, (hx + dx, hy + dy), (50, 50, 50), amber, base=110, collision=False)
    light("BunkerLampLight%d" % index, (hx + dx, hy + dy, 220), art.HUMAN_GLOW, 260, 500)
# Army home pads, matching Boot's markers so players can find their formations.
for index, y in enumerate((0, -850, 850, -1700, 1700)):
    block("ArmyHome%d" % index, (-1800, y), (500, 500, 4), team_materials[index], base=0, collision=False, mesh=cylinder)
# Scavenged power: burn barrels and a generator shack beside the scrapyard.
for index, (x, y) in enumerate(((-2300, -700), (-2300, 1300), (-1300, -2500), (-1300, 2300))):
    block("BurnBarrel%d" % index, (x, y), (70, 70, 100), rust, mesh=cylinder)
    block("BurnBarrelFire%d" % index, (x, y), (60, 60, 10), amber, base=100, collision=False, mesh=cylinder)
    light("BurnBarrelLight%d" % index, (x, y, 260), art.HUMAN_GLOW, 100, 380)
block("GeneratorShack", (-4100, 500), (300, 400, 250), olive)
block("GeneratorLamp", (-3945, 500), (10, 120, 40), amber, base=180, collision=False)

# ---------------------------------------------------------------- campus lights
# Wall-mounted spots under each hall's cyan seam; the cone edge makes a small, sharp pool instead of a blob.
for index, (point, outward) in enumerate((
        ((3100, 100), (-1, 0)), ((3100, 800), (-1, 0)),  # HallA west face
        ((3000, -1250), (0, 1)), ((3600, -1250), (0, 1)), ((3300, -2050), (0, -1)),  # HallB
        ((200, 3300), (0, -1)), ((1000, 3300), (0, -1)),  # HallC south face
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
bounds.set_actor_scale3d(unreal.Vector(ARENA / extent.x, ARENA / extent.y, 450 / extent.z))
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
