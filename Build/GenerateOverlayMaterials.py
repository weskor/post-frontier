"""Generate the cooked, depth-tested world-overlay material with RGBA instance data."""
import unreal

FOLDER = "/Game/Materials/Overlay"
NAME = "M_WorldOverlay"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
library = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


path = FOLDER + "/" + NAME
material = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
    NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
require(material, "Could not create " + path)
# ConstructorHelpers roots this asset and its expressions at module startup.
# Reuse its canonical nodes: deleting a rooted expression asserts in Unreal.
existing = {
    (node.get_class().get_name(), node.get_editor_property("material_expression_editor_x"),
     node.get_editor_property("material_expression_editor_y")): node
    for node in library.get_material_expressions(material)
}


def expression(kind, x, y):
    key = (kind.static_class().get_name(), x, y)
    if key in existing:
        return existing.pop(key)
    return require(library.create_material_expression(material, kind, x, y),
                   "Missing expression " + str(key))


material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)
material.set_editor_property("disable_depth_test", False)
material.set_editor_property("used_with_instanced_static_meshes", True)

channels = []
for index in range(4):
    custom = expression(unreal.MaterialExpressionPerInstanceCustomData, -700, index * 150)
    custom.set_editor_property("data_index", index)
    custom.set_editor_property("const_default_value", 1.0)
    # Explicit interpolation supports the pixel-stage emissive and opacity inputs.
    interpolator = expression(unreal.MaterialExpressionVertexInterpolator, -450, index * 150)
    require(library.connect_material_expressions(custom, "", interpolator, ""), "Could not interpolate channel")
    channels.append(interpolator)

rg = expression(unreal.MaterialExpressionAppendVector, -200, 0)
rgb = expression(unreal.MaterialExpressionAppendVector, 0, 0)
require(not existing, "Unexpected overlay expressions: " + str(list(existing)))
for source, target, pin in ((channels[0], rg, "A"), (channels[1], rg, "B"),
                            (rg, rgb, "A"), (channels[2], rgb, "B")):
    require(library.connect_material_expressions(source, "", target, pin), "Could not append colour")
require(library.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "Missing emissive")
require(library.connect_material_property(channels[3], "", unreal.MaterialProperty.MP_OPACITY), "Missing opacity")
require(library.get_num_material_expressions(material) == 10, "Overlay must contain exactly 10 expressions")
library.recompile_material(material)
require(assets.save_loaded_asset(material), "Could not save " + path)
require(assets.does_asset_exist(path), "Saved overlay asset missing")
require(material.get_editor_property("used_with_instanced_static_meshes"), "Instancing usage missing")
require(not material.get_editor_property("disable_depth_test"), "Depth test disabled")
unreal.log("WORLD_OVERLAY_MATERIAL_READY path=" + path + " channels=RGBA blend=Translucent shading=Unlit depth_test=on")
