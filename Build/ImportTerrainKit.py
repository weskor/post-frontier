"""Import the terrain kit (Art/Terrain/*.fbx) into /Game/Art/Terrain and give it materials and collision.

Pipeline (see Docs/World.md "Art pipeline"): Build/GenerateTerrainKit.py (Blender) -> Art/Terrain/SM_*.fbx -> this
script -> Build/GenerateAvailabilityZone.py places the pieces. Run from the project root with the editor closed and
no other Unreal process from this repo running, after Build/BuildSharedMaterial.py has built the MI_SC2_* instances:

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin,GeometryScripting -ExecutePythonScript="$PWD/Build/ImportTerrainKit.py" \
    -unattended -nullrhi -nosplash

Require TERRAIN_KIT_IMPORTED 15 in the log (and no RuntimeError). Reruns replace the meshes and the material variants.
GeometryScripting is needed only to read the vertex colours and positions back, like Build/VerifyMasks.py.

FBX contract (docstring of Build/GenerateTerrainKit.py, mirrored as SPEC in Build/TerrainKit.py): centimetres, +Z up,
origin at the footprint centre, base at z = 0, material slots Shell, Dark, Glow, Accent (only the slots a piece
uses, in that order). Import options are the ones of Build/ImportEnvironmentKit.py (uniform scale 1, Convert Scene
on, Force Front X Axis off, vertex colour Replace), so the checks below (2 cm on footprint, height, centring and
base) fail if that ever stops being true.

Materials: every slot gets an MI_SC2_<Faction>_<Slot>_Env instance of /Game/Art/Materials/M_Shared (the terrain has
no scope of its own; Env is the coarsest, and the kit's masks were baked with the "env" scope). Human pieces are
steel + rock, Machine pieces pearl + cyan, the destructible rocks and the Watchtower as TerrainKit.look_material
says. The kit's earth and rock Dark/Shell slots are three variants of the Env instances that override the colour and
the earth-ish amounts only (TerrainKit.VARIANTS, built here, values from the Terrain and Rock variants of the
Blender script). Vertex colours (the baked SC2Mask: R Edge, G Cavity, B Ground) are imported with Replace and read
back: alpha 1, B within 0.03 of the ground formula for the 120 cm env band, lowest vertices B = 1.

Collision (the docstring's COLLISION INTENT; one simple shape per cell or piece, never per triangle):
  Cliff_Straight, Plateau_Fill, Rocks_Destructible_*   one box of the mesh bounds (full height, solid; the top of a
                                                       Fill or Straight is the walkable plane at z = 300)
  Cliff_CornerOuter / Cliff_CornerInner                one 10-DOP prism hull (the rounded corner / the fillet)
  Ramp_Wide / Ramp_Narrow (both looks)                 convex decomposition: the wedge below the deck and one hull per
                                                       parapet block; every hull vertex over the walkable strip must
                                                       lie on or under the deck plane (checked when the engine
                                                       exposes the hull vertices, see ramp_hulls= in the token)
  Watchtower                                           a 400 x 400 x 28 cm pad box (walkable) and a 180 x 180 x 900
                                                       column box (blocks, the radius-90 column)
Complexity stays "project default"; nothing uses complex-as-simple.

Orientation check: Unreal's importer flips Blender +Y to -Y. The centroid of the upper vertices of Cliff_CornerOuter
must lie away from AvailabilityZoneLayout.CORNER_OUTER_LOW_AT_YAW0 and the top of a ramp must be at local -X; the
script stops if not, because the map generator's yaw rules depend on it.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art
import AvailabilityZoneLayout as layout
import TerrainKit

require = art.require
assets = art.assets

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX_DIR = os.path.join(ROOT, "Art", "Terrain")
MESH_FOLDER = TerrainKit.MESH_FOLDER
PREFIX = TerrainKit.PREFIX
SPEC = TerrainKit.SPEC
TOLERANCE_CM = 2.0
GROUND_BAND_CM = 120.0        # MasterMaterials.bake_masks(obj, "env"): ground splash height for the env scope
GROUND_ERROR_LIMIT = 0.03
meshlib = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
lists = unreal.GeometryScript_List
GEOMETRY_READ = unreal.GeometryScriptCopyMeshFromAssetOptions()
LOD = unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.MAX_AVAILABLE, lod_index=0)

BOX = unreal.ScriptCollisionShapeType.BOX
PRISM = unreal.ScriptCollisionShapeType.NDOP10_Z
BOX_PIECES = ("Cliff_Straight", "Plateau_Fill", "Rocks_Destructible_Small", "Rocks_Destructible_Large")
PRISM_PIECES = ("Cliff_CornerOuter", "Cliff_CornerInner")
RAMP_PIECES = ("Ramp_Wide", "Ramp_Narrow")
RAMP_STRIP_HALF_CM = {"Ramp_Wide": 350.0, "Ramp_Narrow": 150.0}   # walkable strip 700 / 300 between 50 cm parapets
RAMP_HULL_COUNTS = (6, 10, 16)
TOWER_PAD = (400.0, 400.0, 28.0)
TOWER_COLUMN = (180.0, 180.0, 900.0)


def base_name(name):
    return name[:-len("_Machine")] if name.endswith("_Machine") else name


# ---------------------------------------------------------------- material variants
def build_variants():
    for variant, (parent, vectors, scalars) in TerrainKit.VARIANTS.items():
        art._instance(variant, art.shared(parent), vectors, scalars)
    unreal.log("TERRAIN_VARIANTS_BUILT %d" % len(TerrainKit.VARIANTS))


# ---------------------------------------------------------------- collision
def set_boxes(mesh, boxes):
    """Replace the mesh's simple collision with axis-aligned boxes [(x, y, z size, centre z)] centred on the origin."""
    body = mesh.get_editor_property("body_setup")
    geom = body.get_editor_property("agg_geom")
    elems = []
    for sx, sy, sz, cz in boxes:
        box = unreal.KBoxElem()
        box.set_editor_property("center", unreal.Vector(0.0, 0.0, cz))
        box.set_editor_property("x", sx)
        box.set_editor_property("y", sy)
        box.set_editor_property("z", sz)
        elems.append(box)
    geom.set_editor_property("box_elems", elems)
    body.set_editor_property("agg_geom", geom)
    mesh.modify()
    got = body.get_editor_property("agg_geom").get_editor_property("box_elems")
    require(len(got) == len(boxes), "Could not author the collision boxes of " + mesh.get_name())


def ramp_hulls_clear(name, mesh):
    """True when no collision hull rises over the walkable strip; None when the engine does not expose the vertices."""
    geom = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    strip = RAMP_STRIP_HALF_CM[base_name(name)] - 5.0
    try:
        hulls = [list(hull.get_editor_property("vertex_data")) for hull in geom.get_editor_property("convex_elems")]
    except Exception as exc:  # the property is not Blueprint-visible in every engine build
        unreal.log_warning("TERRAIN_RAMP_HULLS %s vertices not readable: %r" % (name, exc))
        return None
    for hull in hulls:
        for v in hull:
            deck = 300.0 * (400.0 - v.x) / 800.0
            if abs(v.y) < strip and v.z > deck + 2.0:
                unreal.log_warning("TERRAIN_RAMP_HULLS %s bridged: vertex (%.0f, %.0f, %.0f) over the strip, deck %.0f"
                                   % (name, v.x, v.y, v.z, deck))
                return False
    return True


def apply_collision(name, mesh):
    """Author the kit's collision; returns the ramp hull status ('verified', 'unverified', 'bridged') or ''."""
    require(meshlib.remove_collisions(mesh), "Could not clear collision on " + name)
    base = base_name(name)
    status = ""
    if base in BOX_PIECES:
        require(meshlib.add_simple_collisions(mesh, BOX) >= 0, "Could not generate box collision on " + name)
        expected = (1, 0)
    elif base in PRISM_PIECES:
        require(meshlib.add_simple_collisions(mesh, PRISM) >= 0, "Could not generate prism collision on " + name)
        expected = (0, 1)
    elif base in RAMP_PIECES:
        expected = None
        clear = None
        for hull_count in RAMP_HULL_COUNTS:
            meshlib.set_convex_decomposition_collisions(mesh, hull_count, 32, 400000)
            clear = ramp_hulls_clear(name, mesh)
            if clear is not False:
                break
        hulls = meshlib.get_convex_collision_count(mesh)
        require(hulls >= 2, "%s decomposed into %d hulls; a wedge and the parapet blocks need at least 2" % (name, hulls))
        status = {True: "verified", None: "unverified", False: "bridged"}[clear]
    elif base == "Watchtower":
        set_boxes(mesh, [(TOWER_PAD[0], TOWER_PAD[1], TOWER_PAD[2], TOWER_PAD[2] / 2),
                         (TOWER_COLUMN[0], TOWER_COLUMN[1], TOWER_COLUMN[2], TOWER_COLUMN[2] / 2)])
        expected = (2, 0)
    else:
        raise RuntimeError("No collision rule for " + name)
    if expected:
        boxes, hulls = meshlib.get_simple_collision_count(mesh), meshlib.get_convex_collision_count(mesh)
        require((boxes, hulls) == expected, "%s collision is %d boxes + %d convex, expected %s" % (
            name, boxes, hulls, expected))
    require(meshlib.get_collision_complexity(mesh) != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,
            name + " must not use complex collision as simple")
    return status


# ---------------------------------------------------------------- read-back
def read_mesh(mesh):
    """(positions, colours or None) of LOD 0 via GeometryScripting."""
    dyn = unreal.new_object(unreal.DynamicMesh)
    dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(mesh, dyn, GEOMETRY_READ, LOD)
    require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, "Could not read " + mesh.get_name())
    dyn, colours, has_colours, _blended = unreal.GeometryScript_VertexColors.get_mesh_per_vertex_colors(dyn)
    dyn, positions, _gaps = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dyn, False)
    positions = lists.convert_vector_list_to_array(positions)
    return positions, (lists.convert_color_list_to_array(colours) if has_colours else None)


def verify_masks(name, positions, colours):
    require(colours is not None, "%s has no vertex colours (imported without Vertex Color Import Option Replace?)" % name)
    require(len(colours) == len(positions), "%s: %d colours for %d vertices" % (name, len(colours), len(positions)))
    zmin = min(p.z for p in positions)
    error = sum(abs(c.b - max(0.0, 1.0 - (p.z - zmin) / GROUND_BAND_CM) ** 2)
                for c, p in zip(colours, positions)) / len(colours)
    bottom = max(c.b for c, p in zip(colours, positions) if p.z - zmin < 1.0)
    alpha = min(c.a for c in colours)
    edge, cavity = [c.r for c in colours], [c.g for c in colours]
    unreal.log("TERRAIN_MASK_STATS %s vertices=%d edge(max=%.3f) cavity(max=%.3f min=%.3f) ground_error=%.4f "
               "bottom_ground=%.3f alpha_min=%.3f" % (name, len(colours), max(edge), max(cavity), min(cavity), error,
                                                      bottom, alpha))
    require(alpha > 0.99, "%s: vertex alpha is %.3f, expected 1" % (name, alpha))
    require(error < GROUND_ERROR_LIMIT, "%s: ground mask deviates from the formula by %.3f (encoded colours?)" % (name, error))
    require(bottom > 0.97, "%s: lowest vertices have ground %.3f, expected 1" % (name, bottom))
    if max(edge) < min(edge) + 0.05 and max(cavity) < min(cavity) + 0.05:
        unreal.log_warning("TERRAIN_MASK_FLAT %s: edge and cavity masks are constant" % name)
    return error


def verify_orientation(name, positions):
    base = base_name(name)
    if base == "Cliff_CornerOuter":
        # The quarter-disc plateau reaches three of the four cell corners; the fourth, where the two low neighbours
        # meet, is cut away by the arc.
        top = [p for p in positions if p.z > 250.0]
        gaps = {}
        for sx in (-1, 1):
            for sy in (-1, 1):
                gaps[(sx, sy)] = min(((p.x - 200.0 * sx) ** 2 + (p.y - 200.0 * sy) ** 2) ** 0.5 for p in top)
        missing = max(gaps, key=gaps.get)
        low = (sum(d[0] for d in layout.CORNER_OUTER_LOW_AT_YAW0), sum(d[1] for d in layout.CORNER_OUTER_LOW_AT_YAW0))
        unreal.log("TERRAIN_CORNER_ORIENTATION %s cut corner %s gaps=%s" % (
            name, missing, {k: round(v) for k, v in gaps.items()}))
        require(missing == low and gaps[missing] > 100.0 and all(v < 80.0 for k, v in gaps.items() if k != missing),
                "%s: the corner cut away by the arc is %s but AvailabilityZoneLayout.CORNER_OUTER_LOW_AT_YAW0 = %s "
                "puts the low neighbours toward %s; update the constant and the yaw rule in Layout._rim" % (
                    name, missing, layout.CORNER_OUTER_LOW_AT_YAW0, low))
    elif base in RAMP_PIECES:
        top = [p for p in positions if p.z > 340.0]
        mean_x = sum(p.x for p in top) / len(top)
        require(mean_x < -100.0, "%s: the top of the ramp is at x=%.0f, expected local -X (downhill is +X)" % (name, mean_x))


# ---------------------------------------------------------------- import
build_variants()
fbx_names = sorted(f[:-4] for f in os.listdir(FBX_DIR) if f.endswith(".fbx"))
require(fbx_names == sorted(PREFIX + n for n in SPEC),
        "Art/Terrain FBX files %s differ from TerrainKit.SPEC %s" % (fbx_names, sorted(PREFIX + n for n in SPEC)))

tasks = []
for name in SPEC:
    fbx = os.path.join(FBX_DIR, PREFIX + name + ".fbx")
    require(os.path.isfile(fbx), "Missing " + fbx + " (run Build/GenerateTerrainKit.py in Blender)")
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
    task.set_editor_property("destination_name", PREFIX + name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    tasks.append((name, task))

art.tools.import_asset_tasks([task for _name, task in tasks])

rows = []
worst_error = 0.0
ramp_status = []
for name, _task in tasks:
    path = TerrainKit.mesh_path(name)
    mesh = require(assets.load_asset(path), "Import failed for " + name)
    require(isinstance(mesh, unreal.StaticMesh), name + " did not import as a static mesh")
    slots = tuple(str(slot.material_slot_name) for slot in mesh.static_materials)
    require(slots == SPEC[name][3], "%s slots are %s, expected %s" % (name, slots, SPEC[name][3]))
    status = apply_collision(name, mesh)
    if status:
        ramp_status.append(status)
    for index, slot in enumerate(slots):
        mesh.set_material(index, art.shared(TerrainKit.look_material(name, slot)))
    require(assets.save_loaded_asset(mesh), "Could not save " + path)
    positions, colours = read_mesh(mesh)
    worst_error = max(worst_error, verify_masks(name, positions, colours))
    verify_orientation(name, positions)
    box = mesh.get_bounds()
    lo, hi = box.origin - box.box_extent, box.origin + box.box_extent
    rows.append((name, lo, hi))
    unreal.log("TERRAIN_MESH_BOUNDS %s look=%s min=(%.1f, %.1f, %.1f) max=(%.1f, %.1f, %.1f) size=(%.1f, %.1f, %.1f) "
               "slots=%s collision=%d box+%d convex" % (
                   name, SPEC[name][4], lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z,
                   "/".join(slots), meshlib.get_simple_collision_count(mesh), meshlib.get_convex_collision_count(mesh)))

# Checks follow the piece table of Build/GenerateTerrainKit.py (metres there, cm here): footprint, height, footprint
# centred on the origin and base on z = 0, all within TOLERANCE_CM.
for name, lo, hi in rows:
    fx, fy, height = TerrainKit.piece_size(name)
    got = (hi.x - lo.x, hi.y - lo.y, hi.z)
    require(abs(got[0] - fx) <= TOLERANCE_CM and abs(got[1] - fy) <= TOLERANCE_CM,
            "%s footprint is %.1f x %.1f cm, expected %.0f x %.0f" % (name, got[0], got[1], fx, fy))
    require(abs(got[2] - height) <= TOLERANCE_CM, "%s is %.1f cm tall, expected %.0f" % (name, got[2], height))
    require(abs(lo.x + hi.x) <= 2 * TOLERANCE_CM and abs(lo.y + hi.y) <= 2 * TOLERANCE_CM,
            "%s footprint is not centred on the origin (x %.1f..%.1f, y %.1f..%.1f)" % (name, lo.x, hi.x, lo.y, hi.y))
    require(abs(lo.z) <= TOLERANCE_CM, "%s base is z=%.1f, expected 0" % (name, lo.z))

unreal.log("TERRAIN_MASKS_VERIFIED %d worst_ground_error=%.4f" % (len(rows), worst_error))
verdict = "bridged" if "bridged" in ramp_status else "unverified" if "unverified" in ramp_status else "verified"
unreal.log("TERRAIN_KIT_IMPORTED %d ramp_hulls=%s" % (len(rows), verdict))
