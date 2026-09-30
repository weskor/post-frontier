"""Import the construction-loop building meshes into /Game/Art/Buildings and give them the MI_SC2_*_Bld materials.

Pipeline (see Docs/World.md "Art pipeline"): Build/GenerateBuildingMeshes.py (Blender) -> Art/Buildings/SM_*.fbx
-> this script. Run from the project root with the editor closed and no other Unreal process from this repo running
(Build/BuildSharedMaterial.py must have run first):

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/ImportBuildingMeshes.py" \
    -unattended -nullrhi -nosplash

Require BUILDING_MESHES_IMPORTED 15 in the log (and no RuntimeError). Reruns replace the meshes.

Meshes: SM_{Human,Machine}_{Barracks,Barracks_Frontline,Barracks_Ranged,Barracks_Siege,Outpost,Workshop} (a Barracks
is configured once and permanently as Frontline, Ranged or Siege; SM_*_Barracks is the neutral, unconfigured
building) and the faction-neutral scaffolds SM_Construction_{Barracks,Outpost,Workshop}. These are the names
ACommandBuilding::FindBuildingMesh loads.

FBX contract (docstring of Build/GenerateBuildingMeshes.py; ACommandBuilding::GetFootprintRadius): centimetres, +Z up,
door / ramp / dish toward +X, origin at the footprint centre, ground at z = -65 (the actor's footprint box is 130 cm
tall). Footprint half-extent is 125 (Barracks), 95 (Outpost) or 145 (Workshop); the mesh stays inside +10 % of it and
fills at least 80 % of it. Default import axis options match the export, so no rotation or scale is applied; the checks
below fail (2 cm tolerance) if that ever stops being true.

Materials: slots Team, Shell, Dark, Glow, in that order on every mesh. Slot 0 (Team) is the one gameplay tints
through `TeamColor` (blue / red, amber while under construction). Each slot gets MI_SC2_<Faction>_<Slot>_Bld of
/Game/Art/Materials/M_Shared; scaffolds get MI_SC2_Construction_<Slot>_Bld (bare-steel Shell, amber Team). Vertex
colours (the baked SC2Mask: R Edge, G Cavity, B Ground) are imported with Vertex Color Import Option Replace; the
master reads them only when its UseBakedMasks switch is on (Build/VerifyMasks.py checks they arrived).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art  # noqa: E402

require = art.require
assets = art.assets

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX_DIR = os.path.join(ROOT, "Art", "Buildings")
MESH_FOLDER = "/Game/Art/Buildings"
FACTIONS = ("Machine", "Human")
KINDS = ("Barracks", "Barracks_Frontline", "Barracks_Ranged", "Barracks_Siege", "Outpost", "Workshop")
SCAFFOLDS = ("Barracks", "Outpost", "Workshop")
SLOTS = ("Team", "Shell", "Dark", "Glow")
GROUND = -65.0
TOLERANCE_CM = 2.0
OVERHANG = 1.10
HALF = {"Barracks": 125.0, "Outpost": 95.0, "Workshop": 145.0}
# Height above ground in cm: min / max, from HEIGHT_RANGE / SCAFFOLD_HEIGHT_RANGE in Build/GenerateBuildingMeshes.py
HEIGHT = {"Barracks": (170, 230), "Barracks_Frontline": (170, 260), "Barracks_Ranged": (170, 260),
          "Barracks_Siege": (170, 260), "Outpost": (340, 400), "Workshop": (160, 230)}
SCAFFOLD_HEIGHT = {"Barracks": (190, 250), "Outpost": (260, 340), "Workshop": (170, 240)}

# name -> (materials faction set, footprint class, height range)
SPEC = {}
for faction in FACTIONS:
    for kind in KINDS:
        SPEC["SM_%s_%s" % (faction, kind)] = (faction, kind.split("_")[0], HEIGHT[kind])
for kind in SCAFFOLDS:
    SPEC["SM_Construction_" + kind] = ("Construction", kind, SCAFFOLD_HEIGHT[kind])

# ---------------------------------------------------------------- import
tasks = []
for name in SPEC:
    fbx = os.path.join(FBX_DIR, name + ".fbx")
    require(os.path.isfile(fbx), "Missing " + fbx + " (run Build/GenerateBuildingMeshes.py in Blender)")
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
    tasks.append((name, task))

art.tools.import_asset_tasks([task for _name, task in tasks])

rows = []
for name, _task in tasks:
    path = "%s/%s" % (MESH_FOLDER, name)
    mesh = require(assets.load_asset(path), "Import failed for " + name)
    require(isinstance(mesh, unreal.StaticMesh), name + " did not import as a static mesh")
    slots = [str(slot.material_slot_name) for slot in mesh.static_materials]
    require(slots == list(SLOTS), "%s slots are %s, expected %s" % (name, slots, list(SLOTS)))
    faction = SPEC[name][0]
    for index, slot in enumerate(SLOTS):
        mesh.set_material(index, art.shared("MI_SC2_%s_%s_Bld" % (faction, slot)))
    require(assets.save_loaded_asset(mesh), "Could not save " + path)
    box = mesh.get_bounds()
    lo, hi = box.origin - box.box_extent, box.origin + box.box_extent
    rows.append((name, lo, hi))
    unreal.log("BUILDING_MESH_BOUNDS %s min=(%.1f, %.1f, %.1f) max=(%.1f, %.1f, %.1f) size=(%.1f, %.1f, %.1f) "
               "triangles=%d" % (name, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z,
                                 mesh.get_num_triangles(0)))

# Bounds follow check() in Build/GenerateBuildingMeshes.py, in cm: ground at -65, inside +10 % of the footprint
# half-extent (roofs, antennas and ramps may overhang), filling at least 80 % of it, height within the kind's range.
for name, lo, hi in rows:
    _faction, base, (hmin, hmax) = SPEC[name]
    half = HALF[base]
    require(abs(lo.z - GROUND) <= TOLERANCE_CM, "%s ground is z=%.1f, expected %.0f" % (name, lo.z, GROUND))
    limit = half * OVERHANG + TOLERANCE_CM
    require(max(-lo.x, -lo.y, hi.x, hi.y) <= limit, "%s reaches %.1f cm, footprint limit %.1f" % (
        name, max(-lo.x, -lo.y, hi.x, hi.y), limit))
    require(min(-lo.x, -lo.y, hi.x, hi.y) >= 0.80 * half - TOLERANCE_CM,
            "%s does not fill its %.0f cm footprint (x %.1f..%.1f, y %.1f..%.1f)" % (name, half, lo.x, hi.x, lo.y, hi.y))
    height = hi.z - GROUND
    require(hmin - TOLERANCE_CM <= height <= hmax + TOLERANCE_CM,
            "%s is %.1f cm tall, expected %d..%d" % (name, height, hmin, hmax))
unreal.log("BUILDING_MESHES_IMPORTED %d" % len(rows))
