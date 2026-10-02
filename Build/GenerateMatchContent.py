"""Create or update the match content catalogue: unit and building definitions plus DA_MatchContent.

Build/Content/units.json owns all unit stats, including the preserved serialized
combat values. Catalogue order is a replicated contract: units frontline=0,
ranged=1, siege=2; buildings barracks=0, extractor=1, workshop=2.

Run through ./x gen generate-match-content.
"""
import json
from pathlib import Path

import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
role_type = getattr(unreal, "UnitRole", None) or getattr(unreal, "EUnitRole")
armor_type = getattr(unreal, "ArmorClass", None) or getattr(unreal, "EArmorClass")
damage_type = unreal.WeaponDamageType


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def authored_enum(enum_type, name, allowed):
    if name not in allowed:
        raise ValueError("Unsupported authored enum %r; expected one of %s" % (name, ", ".join(allowed)))
    return getattr(enum_type, name)


def mesh(path):
    """Soft reference target; a missing mesh leaves the slot empty so the runtime cube fallback applies."""
    if not assets.does_asset_exist(path):
        unreal.log_warning("MATCH_CONTENT_MISSING_MESH " + path)
        return None
    return require(unreal.load_asset(path), "Could not load " + path)


def data_asset(path, asset_class):
    """Load or create an asset; callers apply the same text values to both."""
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
    unreal.log("MATCH_CONTENT_CREATED " + path)
    return asset


def apply(asset, values):
    for property_name, value in values:
        asset.set_editor_property(property_name, value)
    require(assets.save_loaded_asset(asset), "Could not save " + asset.get_path_name())


unit_art = "/Game/Art/Units/"
units = []
with (Path(__file__).parent / "Content" / "units.json").open(encoding="utf-8") as source:
    unit_definitions = json.load(source)
# Validate the complete source before writing any asset.
unit_tags = [
    (
        authored_enum(role_type, definition["role"], ("FRONTLINE", "RANGED", "SIEGE")),
        authored_enum(armor_type, definition["armor_class"], ("LIGHT", "HEAVY", "SHIELDED", "STRUCTURE")),
        authored_enum(damage_type, definition["damage_type"], ("KINETIC", "PIERCING", "DEMOLITION", "EMP")),
    )
    for definition in unit_definitions
]
for definition, (role, armor, damage) in zip(unit_definitions, unit_tags):
    name = definition["asset_name"]
    unit = data_asset("/Game/Units/DA_" + name, unreal.ArmyUnitDefinition)
    values = [(key, definition[key]) for key in (
        "id", "display_name", "max_health", "attack_damage", "range", "interval",
        "unit_cost", "capacity", "configuration_cost", "unit_duration", "move_speed",
    )]
    values.extend((
        ("role", role),
        ("armor_class", armor),
        ("damage_type", damage),
        ("accent", unreal.LinearColor(*definition["accent"], 1.0)),
        ("human_mesh", mesh(unit_art + "SM_Human_" + name)),
        ("machine_mesh", mesh(unit_art + "SM_Machine_" + name)),
    ))
    apply(unit, values)
    units.append(unit)

building_art = "/Game/Art/Buildings/"
buildings = []
# Extractor repurposes the existing Outpost definition and art assets; filenames remain stable.
for name, asset_name, cost, duration, health, footprint, produces, deposit, research, accent in (
    ("Barracks", "Barracks", 220, 12.0, 500, 125.0, True, False, False, (0.04, 0.50, 1.0)),
    ("Extractor", "Outpost", 160, 9.0, 350, 95.0, False, True, False, (0.16, 0.85, 0.25)),
    ("Workshop", "Workshop", 190, 14.0, 400, 145.0, False, False, True, (0.65, 0.25, 1.0)),
):
    building = data_asset("/Game/Content/DA_" + asset_name, unreal.BuildingDefinition)
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

content = data_asset("/Game/Content/DA_MatchContent", unreal.MatchContent)
apply(content, (("units", units), ("buildings", buildings)))
unreal.log("MATCH_CONTENT_READY units=%d buildings=%d" % (len(units), len(buildings)))
