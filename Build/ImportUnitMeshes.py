"""Import the placeholder unit/HQ meshes into /Game/Art/Units and build /Game/Maps/UnitGallery.

Pipeline (see Docs/World.md "Art pipeline"): Build/GenerateUnitMeshes.py (Blender) -> Art/Units/SM_*.fbx
-> this script -> gallery map. Run from the project root with the editor closed:

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/ImportUnitMeshes.py" \
    -unattended -nullrhi -nosplash

Require UNIT_MESHES_IMPORTED and UNIT_GALLERY_GENERATED in the log. Reruns replace the meshes and
every actor in the gallery. FBX contract (Build/GenerateUnitMeshes.py): centimetres, forward +X, +Z up,
origin at the capsule centre (ground z = -60 for units, -100 for HQs). Default import axis options match
it, so no rotation or scale is applied; the bounds checks below fail if that ever stops being true.

Materials: every slot gets its MI_SC2_<Faction>_<Slot>_<Scope> instance of /Game/Art/Materials/M_Shared (built by
Build/BuildSharedMaterial.py, which must have run first): scope Unit for the six units, Bld for the two HQs (they are
3 m buildings). The Team slot's `TeamColor` default is the faction colour (Human blue, Machine red); gameplay tints it
per team through a dynamic instance. Vertex colours (the baked SC2Mask: R Edge, G Cavity, B Ground) are imported
with Vertex Color Import Option Replace and read back in Build/VerifyMasks.py.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art

require = art.require
assets = art.assets
library = art.library
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX_DIR = os.path.join(ROOT, "Art", "Units")
MESH_FOLDER = "/Game/Art/Units"
ROLES = ("Frontline", "Ranged", "Siege", "HQ")
FACTIONS = ("Machine", "Human")
SLOTS = ("Team", "Shell", "Dark", "Glow")

# Lighting knobs for the gallery (dusk mood like CampusZero; tuned from in-game captures).
SUN_LUX = 6.0
SKY_INTENSITY = 9.0
EXPOSURE_BIAS = 10.5
# Gallery layout, cm.
SPACING = 400.0
ROW_GAP = 700.0
HQ_OFFSET = 450.0  # HQ centre distance behind its row

# ---------------------------------------------------------------- materials
def scope_of(role):
    return "Bld" if role == "HQ" else "Unit"


FACTION_MATERIALS = {(faction, role): {slot: art.shared("MI_SC2_%s_%s_%s" % (faction, slot, scope_of(role)))
                                        for slot in SLOTS}
                     for faction in FACTIONS for role in ROLES}

# ---------------------------------------------------------------- import
tasks = []
for faction in FACTIONS:
    for role in ROLES:
        name = "SM_%s_%s" % (faction, role)
        fbx = os.path.join(FBX_DIR, name + ".fbx")
        require(os.path.isfile(fbx), "Missing " + fbx)
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("import_animations", False)
        options.set_editor_property("automated_import_should_detect_type", False)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        data = options.static_mesh_import_data
        data.set_editor_property("combine_meshes", True)
        data.set_editor_property("convert_scene", True)
        data.set_editor_property("force_front_x_axis", False)
        data.set_editor_property("convert_scene_unit", False)
        data.set_editor_property("import_uniform_scale", 1.0)
        data.set_editor_property("import_translation", unreal.Vector(0, 0, 0))
        data.set_editor_property("import_rotation", unreal.Rotator(0, 0, 0))
        data.set_editor_property("generate_lightmap_u_vs", False)
        data.set_editor_property("auto_generate_collision", True)
        data.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", fbx)
        task.set_editor_property("destination_path", MESH_FOLDER)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("options", options)
        tasks.append((faction, role, name, task))

art.tools.import_asset_tasks([t for *_, t in tasks])

meshes = {}
rows = []
for faction, role, name, task in tasks:
    path = "%s/%s" % (MESH_FOLDER, name)
    mesh = require(assets.load_asset(path), "Import failed for " + name)
    require(isinstance(mesh, unreal.StaticMesh), name + " did not import as a static mesh")
    slot_names = [str(slot.material_slot_name) for slot in mesh.static_materials]
    require(slot_names == list(SLOTS), "%s slots are %s, expected %s" % (name, slot_names, list(SLOTS)))
    for index, slot in enumerate(SLOTS):
        mesh.set_material(index, FACTION_MATERIALS[(faction, role)][slot])
    require(assets.save_loaded_asset(mesh), "Could not save " + path)
    box = mesh.get_bounds()
    lo = box.origin - box.box_extent
    hi = box.origin + box.box_extent
    rows.append((name, lo, hi))
    meshes[(faction, role)] = mesh
    unreal.log("UNIT_MESH_BOUNDS %s min=(%.1f, %.1f, %.1f) max=(%.1f, %.1f, %.1f) size=(%.1f, %.1f, %.1f)" % (
        name, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z))

# Checks follow the FBX contract in Build/GenerateUnitMeshes.py (check()): every unit stands on the capsule
# base at z = -60 and every HQ on z = -100, except the Machine Ranged drone, which hovers (its lowest point is
# above -35 in the generator). Units stay inside the capsule envelope (top <= +60) plus a few cm of weapon.
for name, lo, hi in rows:
    hq = name.endswith("_HQ")
    hover = name == "SM_Machine_Ranged"
    ground = -100.0 if hq else -60.0
    if hover:
        require(-60.0 < lo.z <= -30.0, "%s should hover between z=-60 and -30, lowest point is %.1f" % (name, lo.z))
    else:
        require(abs(lo.z - ground) <= 2.0, "%s ground is z=%.1f, expected %.0f" % (name, lo.z, ground))
    if hq:
        require(abs((hi.x - lo.x) - 300) <= 30 and abs((hi.y - lo.y) - 300) <= 30,
                "%s footprint is %.0f x %.0f, expected ~300 x 300" % (name, hi.x - lo.x, hi.y - lo.y))
        require(hi.z <= 185.0, "%s is taller than the contract allows (top z=%.1f)" % (name, hi.z))
    else:
        require(hi.z <= 66.0, "%s exceeds the capsule envelope (top z=%.1f)" % (name, hi.z))
        require(max(hi.x - lo.x, hi.y - lo.y) <= 135.0, "%s is wider than expected" % name)
    if name.endswith("_Frontline"):
        require(abs((hi.z - lo.z) - 120) <= 6, "%s is %.0f tall, expected ~120" % (name, hi.z - lo.z))
    # Forward is +X: barrels reach further forward than the body reaches back. SM_Human_Siege is exempt: its
    # wheels and rear mount make the box rear-heavy (-66.0 / +62.8, identical to the Blender source); its jaws
    # point +X, which the gallery screenshots confirm.
    if name.endswith(("_Siege", "_Ranged")) and name != "SM_Human_Siege":
        require(hi.x > abs(lo.x), "%s forward is not +X (max x %.1f, min x %.1f)" % (name, hi.x, lo.x))
unreal.log("UNIT_MESHES_IMPORTED %d" % len(meshes))

# ---------------------------------------------------------------- gallery level
map_path = "/Game/Maps/UnitGallery"
if assets.does_asset_exist(map_path):
    require(levels.load_level(map_path), "Could not load UnitGallery for replacement")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings):
            continue
        if isinstance(actor, unreal.Brush) and not isinstance(actor, unreal.Volume):
            continue
        require(actors.destroy_actor(actor), "Could not remove old actor " + actor.get_name())
else:
    require(levels.new_level(map_path, False), "Could not create UnitGallery")

cube = require(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"), "Engine cube is missing")


def spawn(actor_class, label, location, rotation=unreal.Rotator()):
    actor = require(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation),
                    "Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


floor_material = art.surface("MI_GalleryFloor", (0.04, 0.042, 0.048), 0.9)
floor = spawn(unreal.StaticMeshActor, "Floor", (0, 0, -50))
floor.static_mesh_component.set_static_mesh(cube)
floor.static_mesh_component.set_material(0, floor_material)
floor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
floor.set_actor_scale3d(unreal.Vector(3000, 3000, 1))  # 3 km square (edge stays below the horizon), top face at z = 0

# Floor top is z = 0. Meshes are placed by contract ground (-60 units, -100 HQs), so the drone hovers as designed.


def place(faction, role, x, y, yaw):
    name = "SM_%s_%s" % (faction, role)
    actor = spawn(unreal.StaticMeshActor, "%s_%s" % (faction, role), (x, y, 100.0 if role == "HQ" else 60.0),
                  unreal.Rotator(yaw=yaw))
    actor.static_mesh_component.set_static_mesh(meshes[(faction, role)])
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
    return actor


# Machine row on +Y facing -Y (yaw -90); Human row on -Y facing +Y (yaw +90). Forward is +X in mesh space.
# Units stand in a line along X with the HQs behind them (Machine HQ at +Y, Human HQ at -Y).
unit_roles = ROLES[:3]
for faction, y, yaw in (("Machine", ROW_GAP / 2, -90.0), ("Human", -ROW_GAP / 2, 90.0)):
    for index, role in enumerate(unit_roles):
        place(faction, role, (index - 1) * SPACING, y, yaw)
    place(faction, "HQ", 0.0, y + (1 if faction == "Machine" else -1) * HQ_OFFSET, yaw)

# GameModeBase still spawns a DefaultPawn (a visible sphere); park it far outside the frame. The camera is the view.
spawn(unreal.PlayerStart, "GalleryStart", (0, -20000, 500))
sun = spawn(unreal.DirectionalLight, "DuskSun", (0, 0, 3000), unreal.Rotator(pitch=-25, yaw=-28))
sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun_component.set_editor_property("intensity", SUN_LUX)
sun_component.set_editor_property("light_color", unreal.Color(r=255, g=214, b=185, a=255))
sun_component.set_editor_property("atmosphere_sun_light", True)
spawn(unreal.SkyAtmosphere, "DuskAtmosphere", (0, 0, 0))
sky = spawn(unreal.SkyLight, "DuskSky", (0, 0, 2000))
sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_component.set_editor_property("intensity", SKY_INTENSITY)
sky_component.set_editor_property("real_time_capture", True)
sky_component.set_editor_property("light_color", unreal.Color(r=175, g=190, b=255, a=255))
fog = spawn(unreal.ExponentialHeightFog, "DuskFog", (0, 0, 0))
fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
fog_component.set_editor_property("fog_density", 0.02)
fog_component.set_editor_property("fog_height_falloff", 0.15)
fog_component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.02, 0.035, 0.07, 1.0))
grade = spawn(unreal.PostProcessVolume, "DuskGrade", (0, 0, 0))
grade.set_editor_property("unbound", True)
grade_settings = grade.get_editor_property("settings")
for key, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                   ("auto_exposure_bias", EXPOSURE_BIAS),
                   ("bloom_intensity", 0.35),
                   ("bloom_threshold", 1.0),
                   ("vignette_intensity", 0.35)):
    grade_settings.set_editor_property("override_" + key, True)
    grade_settings.set_editor_property(key, value)
grade.set_editor_property("settings", grade_settings)

camera = spawn(unreal.CameraActor, "GalleryCamera", (0, 0, 0))
CAMERA_LOCATION = unreal.Vector(0.0, -2700.0, 3100.0)  # Human side looking +Y, so Machine is far
camera.set_actor_location(CAMERA_LOCATION, False, False)
camera.set_actor_rotation(unreal.Rotator(roll=0, pitch=-50, yaw=90), False)
camera.camera_component.set_editor_property("field_of_view", 50.0)

world = editor.get_editor_world()
settings = world.get_world_settings()
settings.set_editor_property("default_game_mode",
                             require(unreal.load_class(None, "/Script/Engine.GameModeBase"), "GameModeBase missing"))
camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
require(levels.save_current_level(), "Could not save UnitGallery")
unreal.log("UNIT_GALLERY_GENERATED %s actors=%d" % (map_path, len(actors.get_all_level_actors())))
