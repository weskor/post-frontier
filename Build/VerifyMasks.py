"""Read the baked SC2Mask vertex colours back from every imported art mesh and prove they arrived unchanged.

Run after the three importers (ImportUnitMeshes, ImportBuildingMeshes, ImportEnvironmentKit), editor closed and no other
Unreal process from this repo running:

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin,GeometryScripting -ExecutePythonScript="$PWD/Build/VerifyMasks.py" \
    -unattended -nullrhi -nosplash

Require MASKS_VERIFIED in the log. Only then set USE_BAKED_MASKS in Build/BuildSharedMaterial.py and rerun it.

What is checked, per mesh (Blender wrote R Edge, G Cavity, B Ground, A 1 linear; Unreal must give them back unencoded):
* the mesh has vertex colours at all, and A is 1 (an FBX without the attribute reads as white, which would mean
  "every edge worn, every crevice grimy" in the master);
* B follows the Ground formula of MasterMaterials.bake_masks, (1 - (z - zmin) / height)^2 with height 45 / 80 / 120 cm
  for units / buildings / kit, and equals 1 at the lowest vertex. This is the one channel that can be recomputed from
  the geometry alone. A gamma-encoded import (sRGB round trip) would give sqrt-like values far from the formula;
* R (Edge) and G (Cavity) are not constant: some vertices have Edge, and Cavity varies;
* the number of meshes checked equals the number of Art/{Units,Buildings,Environment}/SM_*.fbx sources, so a new mesh
  that is generated but not imported (or imported but not generated) fails here instead of a hardcoded total.
"""
import os

import unreal

GEOMETRY_READ = unreal.GeometryScriptCopyMeshFromAssetOptions()
LOD = unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.MAX_AVAILABLE, lod_index=0)
GROUND_HEIGHT_CM = {"Units": 45.0, "Buildings": 80.0, "Environment": 120.0}
# HQs are 3 m buildings: scope_of() in Build/MasterMaterials.py maps them to "bld"
HEIGHT_OF = {"SM_Human_HQ": 80.0, "SM_Machine_HQ": 80.0}
GROUND_ERROR_LIMIT = 0.03   # mean |B - formula|; 8-bit storage and split-vertex blending are well below this
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
lists = unreal.GeometryScript_List


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def read_masks(mesh):
    dyn = unreal.new_object(unreal.DynamicMesh)
    dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(mesh, dyn, GEOMETRY_READ, LOD)
    require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, "Could not read " + mesh.get_name())
    dyn, colours, has_colours, _blended = unreal.GeometryScript_VertexColors.get_mesh_per_vertex_colors(dyn)
    if not has_colours:
        return None, None
    dyn, positions, _gaps = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dyn, False)
    return (lists.convert_color_list_to_array(colours), lists.convert_vector_list_to_array(positions))


def verify(folder, key):
    rows = []
    for path in assets.list_assets(folder, recursive=False, include_folder=False):
        mesh = assets.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        name = mesh.get_name()
        colours, positions = read_masks(mesh)
        require(colours is not None, "%s has no vertex colours (imported without Vertex Color Import Option Replace?)" % name)
        require(len(colours) == len(positions), "%s: %d colours for %d vertices" % (name, len(colours), len(positions)))
        height = HEIGHT_OF.get(name, GROUND_HEIGHT_CM[key])
        zmin = min(p.z for p in positions)
        error = sum(abs(c.b - max(0.0, 1.0 - (p.z - zmin) / height) ** 2) for c, p in zip(colours, positions)) / len(colours)
        edge = [c.r for c in colours]
        cavity = [c.g for c in colours]
        alpha = min(c.a for c in colours)
        bottom = max(c.b for c, p in zip(colours, positions) if p.z - zmin < 1.0)
        unreal.log("MASK_STATS %s vertices=%d edge(mean=%.3f max=%.3f nonzero=%.3f) cavity(mean=%.3f max=%.3f) "
                   "ground_error=%.4f bottom_ground=%.3f alpha_min=%.3f" % (
                       name, len(colours), sum(edge) / len(edge), max(edge), sum(1 for v in edge if v > 0.02) / len(edge),
                       sum(cavity) / len(cavity), max(cavity), error, bottom, alpha))
        require(alpha > 0.99, "%s: vertex alpha is %.3f, expected 1" % (name, alpha))
        require(error < GROUND_ERROR_LIMIT, "%s: ground mask deviates from the formula by %.3f (encoded colours?)" % (name, error))
        require(bottom > 0.97, "%s: lowest vertices have ground %.3f, expected 1" % (name, bottom))
        require(max(edge) > 0.3 and max(edge) > min(edge) + 0.3, "%s: edge mask is constant (max %.2f)" % (name, max(edge)))
        require(max(cavity) > min(cavity) + 0.05, "%s: cavity mask is constant" % name)
        rows.append((name, error))
    return rows


rows = verify("/Game/Art/Units", "Units") + verify("/Game/Art/Buildings", "Buildings") + verify("/Game/Art/Environment", "Environment")
# Every FBX the generators wrote is imported by its importer, so the expected count is the number of source FBXs.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
expected = sum(
    len([f for f in os.listdir(os.path.join(ROOT, "Art", folder)) if f.startswith("SM_") and f.endswith(".fbx")])
    for folder in ("Units", "Buildings", "Environment")
)
require(len(rows) == expected, "Expected %d meshes (one per source FBX), checked %d" % (expected, len(rows)))
unreal.log("MASKS_VERIFIED %d worst_ground_error=%.4f" % (len(rows), max(error for _n, error in rows)))
