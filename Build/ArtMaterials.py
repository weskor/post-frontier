"""Shared Unreal editor helpers for the themed art pass (materials under /Game/Art/Materials).

Imported by Build/BuildSharedMaterial.py, Build/GenerateCampusZero.py and the mesh import scripts; not run directly.
`_instance` writes MaterialInstanceConstants (vectors, scalars, static switches, textures) and is reset and read back on
every call; `shared()` loads an MI_SC2_* instance built by BuildSharedMaterial.py and `shared_child()` makes a paint-job
child of one. Parent materials for surface() / glow() are created once and preserved on reruns.
"""
import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
FOLDER = "/Game/Art/Materials"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def _parent(name, build):
    path = FOLDER + "/" + name
    if assets.does_asset_exist(path):
        return require(assets.load_asset(path), "Could not load " + path)
    material = require(tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew()),
                       "Could not create " + path)
    build(material)
    library.recompile_material(material)
    require(assets.save_loaded_asset(material), "Could not save " + path)
    return material


def _parameter(material, kind, name, default, x, y):
    node = library.create_material_expression(material, kind, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def _build_surface(material):
    color = _parameter(material, unreal.MaterialExpressionVectorParameter, "Color",
                       unreal.LinearColor(0.2, 0.2, 0.2, 1.0), -400, 0)
    roughness = _parameter(material, unreal.MaterialExpressionScalarParameter, "Roughness", 0.8, -400, 200)
    metallic = _parameter(material, unreal.MaterialExpressionScalarParameter, "Metallic", 0.0, -400, 300)
    for node, target in ((color, unreal.MaterialProperty.MP_BASE_COLOR),
                         (roughness, unreal.MaterialProperty.MP_ROUGHNESS),
                         (metallic, unreal.MaterialProperty.MP_METALLIC)):
        require(library.connect_material_property(node, "", target), "Could not connect surface input")


def _build_glow(material):
    color = _parameter(material, unreal.MaterialExpressionVectorParameter, "Color",
                       unreal.LinearColor(0.3, 0.9, 1.0, 1.0), -600, 0)
    intensity = _parameter(material, unreal.MaterialExpressionScalarParameter, "Intensity", 8.0, -600, 200)
    multiply = library.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 100)
    require(library.connect_material_expressions(color, "", multiply, "A"), "Could not wire glow colour")
    require(library.connect_material_expressions(intensity, "", multiply, "B"), "Could not wire glow intensity")
    require(library.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
            "Could not connect emissive")
    black = library.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, -150)
    black.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    require(library.connect_material_property(black, "", unreal.MaterialProperty.MP_BASE_COLOR),
            "Could not connect glow base colour")


def _instance(name, parent, vectors, scalars, switches=None, textures=None):
    """Create or update MaterialInstanceConstant `name` under FOLDER: parent and every override are reset, then set
    and read back (UE 5.8.3's setters return False unconditionally, so the stored value is what is checked)."""
    path = FOLDER + "/" + name
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    require(instance, "Could not create " + path)
    library.set_material_instance_parent(instance, parent)
    library.clear_all_material_instance_parameters(instance)
    for key, rgb in vectors.items():
        value = unreal.LinearColor(*rgb, 1.0)
        library.set_material_instance_vector_parameter_value(instance, key, value)
        actual = library.get_material_instance_vector_parameter_value(instance, key)
        require(all(abs(getattr(actual, c) - getattr(value, c)) < 1e-4 for c in "rgb"),
                "Could not set " + key + " on " + name)
    for key, value in scalars.items():
        library.set_material_instance_scalar_parameter_value(instance, key, value)
        require(abs(library.get_material_instance_scalar_parameter_value(instance, key) - value) < 1e-4,
                "Could not set " + key + " on " + name)
    for key, value in (switches or {}).items():
        library.set_material_instance_static_switch_parameter_value(instance, key, bool(value))
        require(library.get_material_instance_static_switch_parameter_value(instance, key) == bool(value),
                "Could not set static switch " + key + " on " + name)
    for key, texture in (textures or {}).items():
        library.set_material_instance_texture_parameter_value(instance, key, texture)
        require(library.get_material_instance_texture_parameter_value(instance, key) == texture,
                "Could not set texture " + key + " on " + name)
    library.update_material_instance(instance)
    require(assets.save_loaded_asset(instance), "Could not save " + path)
    return instance


def shared(name):
    """MI_SC2_* instance built by Build/BuildSharedMaterial.py."""
    path = FOLDER + "/" + name
    return require(assets.load_asset(path) if assets.does_asset_exist(path) else None,
                   "Missing " + path + ": run Build/BuildSharedMaterial.py first")


def shared_child(name, parent_name, **vectors):
    """Child of an MI_SC2_* instance that overrides colour vectors only (e.g. BaseColor): a paint job that stays in
    the master. Static switches and every other parameter are inherited."""
    return _instance(name, shared(parent_name), vectors, {})


def surface(name, rgb, roughness=0.8, metallic=0.0):
    return _instance(name, _parent("M_Surface", _build_surface), {"Color": rgb},
                     {"Roughness": roughness, "Metallic": metallic})


def glow(name, rgb, intensity):
    return _instance(name, _parent("M_Glow", _build_glow), {"Color": rgb}, {"Intensity": intensity})


# Palette from Docs/World.md "Colour language".
MACHINE_GLOW = (0.35, 0.85, 1.0)
HUMAN_GLOW = (1.0, 0.55, 0.15)
