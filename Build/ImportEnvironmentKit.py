"""Import the Campus Zero environment kit into /Game/Art/Environment and give it materials and collision.

Pipeline (see Docs/World.md "Art pipeline"): Build/GenerateEnvironmentKit.py (Blender) ->
Art/Environment/SM_Env_*.fbx -> this script. Run from the project root with the editor closed and no other
Unreal process from this repo running:

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/ImportEnvironmentKit.py" \
    -unattended -nullrhi -nosplash

Require ENV_KIT_IMPORTED 17 in the log (and no RuntimeError). Reruns replace the meshes.

FBX contract (docstring of Build/GenerateEnvironmentKit.py, table mirrored as SPEC in Build/EnvKit.py):
centimetres, +Z up, front +X, origin at the footprint centre, base at z = 0, material slots Shell, Dark, Glow,
Accent (only the slots a piece uses, in that order). The default import axis options match the export, so no
rotation or scale is applied; the checks below fail (2 cm tolerance on footprint, height, centring and base) if
that ever stops being true. Note Unreal's importer flips Y (Blender +Y -> Unreal -Y); see Build/EnvKit.py.

Materials: every slot gets an `MI_Env*` instance under /Game/Art/Materials made with ArtMaterials.surface() and
glow() (StarCraft 2-style art direction, Saved/AgentBriefs/sc2-style.md). Machine pieces (data halls, cooling
tower, chiller, transformer, pylon, comms mast and the campus perimeter FenceSegment) are polished pearl Shell /
near-black navy Dark / cyan Glow / red Accent. Human pieces (the forward-base crates, wreck, barricade, brazier,
generator and cable reel) are painted steel-blue Shell / gunmetal Dark / amber Glow / hazard-yellow Accent
(SandbagWall has no Glow slot, so its floodlight lenses are Accent). The cluster obelisk keeps the Machine Dark,
Glow and Accent with a glossy white Shell (Docs/World.md "Colour language"). The split is explicit in HUMAN /
MACHINE / CLUSTER below and must cover every piece. Values are tuned against Build/GenerateCampusZero.py's dusk
lighting (SUN_LUX 5.0, SKY_INTENSITY 3.5, EXPOSURE_BIAS 10.0) and its existing MI_HallShell / MI_HallDark /
MI_GlowMachine / MI_GlowHuman / MI_GlowMachineEye instances; adjust them in the LOOKS table below.

Collision: the FBX import generates simple collision (auto_generate_collision), which is an 18-DOP convex
hull of the whole mesh. It is then replaced per piece so the map's blocking geometry matches the documented
footprint (COLLISION below): a single box of the mesh bounds for rectangular pieces (so wall modules tile with
no gaps and the parapet keeps its full footprint; the exception is EnvKit.GROUND_FOOTPRINT, whose box is narrower
than the mesh, the pylon's 1.5 m body under a cross-arm 11 m up) and a single 10-DOP prism (octagonal, Z aligned)
convex hull for round pieces. Complexity stays "project default" (simple and complex); nothing uses
complex-as-simple.
Build/GenerateCampusZero.py places the pieces (it needs this import first).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art  # noqa: E402
import EnvKit  # noqa: E402

require = art.require
assets = art.assets

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX_DIR = os.path.join(ROOT, "Art", "Environment")
MESH_FOLDER = EnvKit.MESH_FOLDER
PREFIX = EnvKit.PREFIX
SPEC = EnvKit.SPEC
TOLERANCE_CM = 2.0
meshlib = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

# ---------------------------------------------------------------- materials
# Which look each piece gets (same split as LOOKS in Build/GenerateEnvironmentKit.py's previews).
HUMAN = ("Container", "Wreck", "SandbagWall", "BurnBarrel", "GeneratorShack", "CableSpool")
CLUSTER = ("ClusterPylon",)
MACHINE = ("DataHallBay", "DataHallDoor", "DataHallCorner", "DataHallRoof", "CoolingTower", "Chiller", "Transformer",
           "Pylon", "CommsMast", "FenceSegment")   # the fence stands on the Machine campus perimeter
assert sorted(HUMAN + CLUSTER + MACHINE) == sorted(SPEC), "every kit piece needs exactly one look"
LOOK_OF = {name: ("Human" if name in HUMAN else "Cluster" if name in CLUSTER else "Machine") for name in SPEC}

MACHINE_RED = (1.0, 0.05, 0.03)  # MI_GlowMachineEye in GenerateCampusZero.py
LOOKS = {
    "Machine": {
        "Shell": art.surface("MI_EnvMachineShell", (0.50, 0.55, 0.64), 0.5, 0.3),
        "Dark": art.surface("MI_EnvMachineDark", (0.012, 0.016, 0.028), 0.5, 0.5),
        "Glow": art.glow("MI_EnvMachineGlow", art.MACHINE_GLOW, 4.0),
        "Accent": art.glow("MI_EnvMachineAccent", MACHINE_RED, 1.0),  # the map's eye (8.0) clips to peach on a lens
    },
    "Human": {
        "Shell": art.surface("MI_EnvHumanShell", (0.14, 0.21, 0.33), 0.9, 0.05),
        "Dark": art.surface("MI_EnvHumanDark", (0.06, 0.065, 0.075), 0.7, 0.1),  # metallic .5 mirrored the blue sky
        "Glow": art.glow("MI_EnvHumanGlow", art.HUMAN_GLOW, 5.0),
        "Accent": art.surface("MI_EnvHumanAccent", (0.75, 0.5, 0.03), 0.8, 0.1),
    },
}
LOOKS["Cluster"] = dict(LOOKS["Machine"], Shell=art.surface("MI_EnvClusterShell", (0.88, 0.90, 0.94), 0.18, 0.0))

# ---------------------------------------------------------------- collision
BOX = unreal.ScriptCollisionShapeType.BOX
PRISM = unreal.ScriptCollisionShapeType.NDOP10_Z
ROUND = ("CoolingTower", "Chiller", "BurnBarrel", "CableSpool")
COLLISION = {name: (PRISM if name in ROUND else BOX) for name in SPEC}


def narrow_box(name, mesh):
    """Shrink the single box to EnvKit.GROUND_FOOTPRINT (centred, full height): the pylon's cross-arm is overhead."""
    fx, fy = EnvKit.collision_size(name)
    height = EnvKit.piece_size(name)[2]
    body = mesh.get_editor_property("body_setup")
    geom = body.get_editor_property("agg_geom")
    box = unreal.KBoxElem()
    box.set_editor_property("center", unreal.Vector(0.0, 0.0, height / 2))
    box.set_editor_property("x", fx)
    box.set_editor_property("y", fy)
    box.set_editor_property("z", height)
    geom.set_editor_property("box_elems", [box])
    body.set_editor_property("agg_geom", geom)
    mesh.modify()
    got = body.get_editor_property("agg_geom").get_editor_property("box_elems")
    require(len(got) == 1 and abs(got[0].get_editor_property("x") - fx) < 0.5
            and abs(got[0].get_editor_property("y") - fy) < 0.5, "Could not narrow the collision box of " + name)


def apply_collision(name, mesh):
    require(meshlib.remove_collisions(mesh), "Could not clear collision on " + name)
    shape = COLLISION[name]
    require(meshlib.add_simple_collisions(mesh, shape) >= 0, "Could not generate %s collision on %s" % (shape, name))
    if name in EnvKit.GROUND_FOOTPRINT:
        narrow_box(name, mesh)
    boxes, hulls = meshlib.get_simple_collision_count(mesh), meshlib.get_convex_collision_count(mesh)
    expected = (1, 0) if shape == BOX else (0, 1)
    require((boxes, hulls) == expected, "%s collision is %d boxes + %d convex, expected %s" % (
        name, boxes, hulls, expected))
    require(meshlib.get_collision_complexity(mesh) != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,
            name + " must not use complex collision as simple")


# ---------------------------------------------------------------- import
tasks = []
for name in SPEC:
    fbx = os.path.join(FBX_DIR, PREFIX + name + ".fbx")
    require(os.path.isfile(fbx), "Missing " + fbx + " (run Build/GenerateEnvironmentKit.py in Blender)")
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
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx)
    task.set_editor_property("destination_path", MESH_FOLDER)
    task.set_editor_property("destination_name", PREFIX + name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    tasks.append((name, task))

art.tools.import_asset_tasks([task for _name, task in tasks])

rows = []
for name, _task in tasks:
    path = "%s/%s%s" % (MESH_FOLDER, PREFIX, name)
    mesh = require(assets.load_asset(path), "Import failed for " + name)
    require(isinstance(mesh, unreal.StaticMesh), name + " did not import as a static mesh")
    slots = tuple(str(slot.material_slot_name) for slot in mesh.static_materials)
    require(slots == SPEC[name][3], "%s slots are %s, expected %s" % (name, slots, SPEC[name][3]))
    apply_collision(name, mesh)
    for index, slot in enumerate(slots):
        mesh.set_material(index, LOOKS[LOOK_OF[name]][slot])
    require(assets.save_loaded_asset(mesh), "Could not save " + path)
    box = mesh.get_bounds()
    lo, hi = box.origin - box.box_extent, box.origin + box.box_extent
    rows.append((name, lo, hi))
    unreal.log("ENV_MESH_BOUNDS %s look=%s min=(%.1f, %.1f, %.1f) max=(%.1f, %.1f, %.1f) size=(%.1f, %.1f, %.1f)" % (
        name, LOOK_OF[name], lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z))

# Checks follow the docstring table of Build/GenerateEnvironmentKit.py (metres there, cm here): footprint,
# height, footprint centred on the origin and base on z = 0, all within TOLERANCE_CM.
for name, lo, hi in rows:
    fx, fy, height = EnvKit.piece_size(name)
    got = (hi.x - lo.x, hi.y - lo.y, hi.z)
    require(abs(got[0] - fx) <= TOLERANCE_CM and abs(got[1] - fy) <= TOLERANCE_CM,
            "%s footprint is %.1f x %.1f cm, expected %.0f x %.0f" % (name, got[0], got[1], fx, fy))
    require(abs(got[2] - height) <= TOLERANCE_CM, "%s is %.1f cm tall, expected %.0f" % (name, got[2], height))
    require(abs(lo.x + hi.x) <= 2 * TOLERANCE_CM and abs(lo.y + hi.y) <= 2 * TOLERANCE_CM,
            "%s footprint is not centred on the origin (x %.1f..%.1f, y %.1f..%.1f)" % (
                name, lo.x, hi.x, lo.y, hi.y))
    require(abs(lo.z) <= TOLERANCE_CM, "%s base is z=%.1f, expected 0" % (name, lo.z))
unreal.log("ENV_KIT_IMPORTED %d" % len(rows))
