"""Build /Game/Maps/ArtGallery: every unit, HQ, building, scaffold and environment-kit piece under the Campus Zero dusk.

The gallery is where the MI_SC2_* values of Build/BuildSharedMaterial.py are judged against Art/Materials/Close-*.png
before Campus Zero is regenerated. Run from the project root with the editor closed and no other Unreal process from
this repo running, after Build/BuildSharedMaterial.py and the three importers:

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/BuildArtGallery.py" \
    -unattended -nullrhi -nosplash

Require ART_GALLERY_GENERATED in the log. Reruns replace every actor. The map stores only mesh references, so the
importers and instances can change without regenerating it.

Layout (cm; the floor top is z = 0; every piece's front faces -Y, toward the south camera). Rows from near (-Y) to
far (+Y), each labelled by an actor named `Row_<name>` whose location is the row's focus point; the capture script
Saved/Verification/sc2-art-unreal/gallery_shots.py looks the rows up by that label:

  human-kit         Container, Wreck, SandbagWall, BurnBarrel, GeneratorShack, CableSpool
  human-buildings   Barracks (neutral), Frontline, Ranged, Siege, Outpost, Workshop
  human-units       Frontline, Ranged, Siege, HQ, the three construction scaffolds
  machine-units     Frontline, Ranged, Siege, HQ
  machine-buildings Barracks (neutral), Frontline, Ranged, Siege, Outpost, Workshop
  machine-kit       DataHall Bay / Door / Corner / Roof, Chiller, Transformer, CoolingTower, Pylon, CommsMast,
                    FenceSegment, ClusterPylon
  machine-hall      one assembled data hall (EnvKit.assemble_hall)

Light and exposure copy Build/GenerateCampusZero.py (SUN_LUX, SKY_INTENSITY, EXPOSURE_BIAS). The camera actors
`GalleryCamera` (unit-gallery style overview) is the game's view; the capture script moves it.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art
import EnvKit

require = art.require
assets = art.assets
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

# Lighting: the Campus Zero dusk (Build/GenerateCampusZero.py).
SUN_LUX = 5.0
SKY_INTENSITY = 3.5
EXPOSURE_BIAS = 10.0
SUN_COLOR = (235, 238, 250)
SKY_COLOR = (180, 200, 245)

FRONT_YAW = -90.0   # mesh +X (door, ramp, muzzle) -> world -Y, toward the camera
ROWS = {}           # row name -> focus (x, y)


def piece_z(path):
    """Height that puts the mesh's lowest point on the floor top (z = 0)."""
    box = require(assets.load_asset(path), "Missing " + path).get_bounds()
    return -(box.origin.z - box.box_extent.z)


def spawn(actor_class, label, location, rotation=unreal.Rotator()):
    actor = require(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation),
                    "Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


def place_mesh(path, label, x, y, yaw=FRONT_YAW, hover=0.0):
    actor = spawn(unreal.StaticMeshActor, label, (x, y, piece_z(path) + hover), unreal.Rotator(yaw=yaw))
    component = actor.static_mesh_component
    component.set_static_mesh(require(assets.load_asset(path), "Missing " + path))
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name("NoCollision")
    return actor


def row(name, y, items, spacing, start=None):
    """items: [(mesh path, label, extra x offset)]; the row is centred on x = 0 unless `start` is given."""
    x0 = start if start is not None else -(len(items) - 1) * spacing / 2.0
    for index, (path, label, dx) in enumerate(items):
        place_mesh(path, "%s_%s" % (name, label), x0 + index * spacing + dx, y)
    ROWS[name] = (0.0 if start is None else x0 + (len(items) - 1) * spacing / 2.0, y)


def kit_row(name, y, pieces, spacing):
    x0 = -(len(pieces) - 1) * spacing / 2.0
    for index, piece in enumerate(pieces):
        EnvKit.place_piece(piece, (x0 + index * spacing, y), yaw=FRONT_YAW, label="%s_%s" % (name, piece),
                           collision=False)
    ROWS[name] = (0.0, y)


def units(faction):
    return [("/Game/Art/Units/SM_%s_%s" % (faction, role), role, 0.0) for role in ("Frontline", "Ranged", "Siege")]


def buildings(faction):
    return [("/Game/Art/Buildings/SM_%s_%s" % (faction, kind), kind, 0.0) for kind in (
        "Barracks", "Barracks_Frontline", "Barracks_Ranged", "Barracks_Siege", "Outpost", "Workshop", "FailoverNode")]


# The kit must be imported before the gallery is built; check before the level is touched.
for piece in EnvKit.SPEC:
    require(assets.does_asset_exist(EnvKit.mesh_path(piece)), "Missing %s: run Build/ImportEnvironmentKit.py first" %
            EnvKit.mesh_path(piece))

map_path = "/Game/Maps/ArtGallery"
if assets.does_asset_exist(map_path):
    require(levels.load_level(map_path), "Could not load ArtGallery for replacement")
    for actor in actors.get_all_level_actors():
        require(actors.destroy_actor(actor), "Could not remove old actor " + actor.get_name())
else:
    require(levels.new_level(map_path, False), "Could not create ArtGallery")

cube = require(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"), "Engine cube is missing")
floor_material = art.surface("MI_GalleryFloor", (0.045, 0.048, 0.055), 0.9)
floor = spawn(unreal.StaticMeshActor, "Floor", (0, 0, -50))
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_material(0, floor_material)
floor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
floor.set_actor_scale3d(unreal.Vector(300, 300, 1))   # 3 km square, top face at z = 0

# ---------------------------------------------------------------- rows
kit_row("human-kit", -2500, ["Container", "Wreck", "SandbagWall", "BurnBarrel", "GeneratorShack", "CableSpool"], 750.0)
row("human-buildings", -1600, buildings("Human"), 520.0)
human_units = units("Human") + [("/Game/Art/Units/SM_Human_HQ", "HQ", 100.0)] + [
    ("/Game/Art/Buildings/SM_Construction_" + kind, "Scaffold" + kind, 100.0) for kind in ("Barracks", "Outpost", "Workshop")]
row("human-units", -800, human_units, 420.0)
row("machine-units", 800, units("Machine") + [("/Game/Art/Units/SM_Machine_HQ", "HQ", 100.0)], 420.0)
row("machine-buildings", 1600, buildings("Machine"), 520.0)
kit_row("machine-kit", 2600, ["DataHallBay", "DataHallDoor", "DataHallCorner", "DataHallRoof", "Chiller", "Transformer",
                              "CoolingTower", "Pylon", "CommsMast", "FenceSegment", "ClusterPylon"], 650.0)
EnvKit.assemble_hall("Hall", (0, 3900), (1400, 1000), {"S": 1}, collision=False)
ROWS["machine-hall"] = (0.0, 3900)
for name, (x, y) in ROWS.items():
    spawn(unreal.TargetPoint, "Row_" + name, (x, y, 0))

# ---------------------------------------------------------------- sky, lights, camera
spawn(unreal.PlayerStart, "GalleryStart", (0, -20000, 500))
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

camera = spawn(unreal.CameraActor, "GalleryCamera", (0, -3000, 3000))
camera.set_actor_rotation(unreal.Rotator(roll=0, pitch=-50, yaw=90), False)
camera.camera_component.set_editor_property("field_of_view", 50.0)
camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)

world = editor.get_editor_world()
world.get_world_settings().set_editor_property(
    "default_game_mode", require(unreal.load_class(None, "/Script/Engine.GameModeBase"), "GameModeBase missing"))
require(levels.save_current_level(), "Could not save ArtGallery")
unreal.log("ART_GALLERY_GENERATED %s rows=%d actors=%d" % (map_path, len(ROWS), len(actors.get_all_level_actors())))
