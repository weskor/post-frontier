"""Create or update the match content catalogue: unit and building definitions plus DA_MatchContent.

Existing /Game/Units/DA_* assets keep their tuned combat values; only the data-driven
fields (identity, production, meshes) are written. Catalogue order is a replicated
contract: units frontline=0, ranged=1, siege=2; buildings barracks=0, extractor=1, workshop=2.

After building CoopRTSEditor, run UnrealEditor-Cmd with -EnablePlugins=PythonScriptPlugin
-ExecutePythonScript="$PWD/Build/GenerateMatchContent.py" -unattended -nullrhi -nosplash.
"""
import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
role_type = getattr(unreal, "UnitRole", None) or getattr(unreal, "EUnitRole")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def mesh(path):
    """Soft reference target; a missing mesh leaves the slot empty so the runtime cube fallback applies."""
    if not assets.does_asset_exist(path):
        unreal.log_warning("MATCH_CONTENT_MISSING_MESH " + path)
        return None
    return require(unreal.load_asset(path), "Could not load " + path)


def data_asset(path, asset_class, create_defaults):
    """Load the asset at path or create it; create_defaults are applied only on creation."""
    if assets.does_asset_exist(path):
        asset = require(assets.load_asset(path), "Could not load " + path)
        if not isinstance(asset, asset_class):
            raise RuntimeError("Unexpected asset at " + path)
        unreal.log("MATCH_CONTENT_UPDATING " + path)
        return asset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class.static_class())
    folder, name = path.rsplit("/", 1)
    asset = require(tools.create_asset(name, folder, asset_class, factory), "Could not create " + path)
    for property_name, value in create_defaults:
        asset.set_editor_property(property_name, value)
    unreal.log("MATCH_CONTENT_CREATED " + path)
    return asset


def apply(asset, values):
    for property_name, value in values:
        asset.set_editor_property(property_name, value)
    require(assets.save_loaded_asset(asset), "Could not save " + asset.get_path_name())


unit_art = "/Game/Art/Units/"
units = []
for name, role, health, damage, attack_range, interval, cost, duration, capacity, fee, accent in (
    ("Frontline", role_type.FRONTLINE, 140, 14, 175.0, 0.7, 20, 10.0 / 3.0, 6, 0, (0.85, 0.85, 0.85)),
    ("Ranged", role_type.RANGED, 90, 18, 560.0, 1.15, 30, 13.0 / 3.0, 4, 0, (0.55, 0.85, 1.0)),
    ("Siege", role_type.SIEGE, 110, 42, 1150.0, 2.6, 50, 20.0 / 3.0, 2, 180, (1.0, 0.65, 0.25)),
):
    unit = data_asset("/Game/Units/DA_" + name, unreal.ArmyUnitDefinition, (
        ("role", role), ("max_health", health), ("attack_damage", damage),
        ("range", attack_range), ("interval", interval),
    ))
    apply(unit, (
        ("id", name.lower()), ("display_name", name), ("accent", unreal.LinearColor(*accent, 1.0)),
        ("unit_cost", cost), ("capacity", capacity), ("configuration_cost", fee), ("unit_duration", duration),
        ("human_mesh", mesh(unit_art + "SM_Human_" + name)),
        ("machine_mesh", mesh(unit_art + "SM_Machine_" + name)),
    ))
    units.append(unit)

building_art = "/Game/Art/Buildings/"
buildings = []
# Extractor repurposes the existing Outpost definition and art assets; filenames remain stable.
for name, asset_name, cost, duration, health, footprint, produces, deposit, research, accent in (
    ("Barracks", "Barracks", 220, 12.0, 500, 125.0, True, False, False, (0.04, 0.50, 1.0)),
    ("Extractor", "Outpost", 160, 9.0, 350, 95.0, False, True, False, (0.16, 0.85, 0.25)),
    ("Workshop", "Workshop", 190, 14.0, 400, 145.0, False, False, True, (0.65, 0.25, 1.0)),
):
    building = data_asset("/Game/Content/DA_" + asset_name, unreal.BuildingDefinition, ())
    # Producer locked-type variants follow catalogue unit order; other buildings have none.
    role_meshes = {"human": [], "machine": []}
    if produces:
        for faction in role_meshes:
            role_meshes[faction] = [mesh(building_art + "SM_%s_%s_%s" % (faction.capitalize(), asset_name, unit.get_name()[3:]))
                                    for unit in units]
    apply(building, (
        ("id", name.lower()), ("display_name", name), ("accent", unreal.LinearColor(*accent, 1.0)),
        ("build_cost", cost), ("max_health", health), ("build_duration", duration), ("footprint_radius", footprint),
        ("produces_forces", produces), ("requires_deposit", deposit), ("offers_research", research),
        ("human_mesh", mesh(building_art + "SM_Human_" + asset_name)),
        ("machine_mesh", mesh(building_art + "SM_Machine_" + asset_name)),
        ("construction_mesh", mesh(building_art + "SM_Construction_" + asset_name)),
        ("human_role_meshes", role_meshes["human"]),
        ("machine_role_meshes", role_meshes["machine"]),
    ))
    buildings.append(building)

content = data_asset("/Game/Content/DA_MatchContent", unreal.MatchContent, ())
apply(content, (("units", units), ("buildings", buildings)))
unreal.log("MATCH_CONTENT_READY units=%d buildings=%d" % (len(units), len(buildings)))
