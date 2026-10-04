"""Create or update the match content catalogue: unit and building definitions plus DA_MatchContent.

Build/Content/units.json and buildings.json own every unit and building field;
Build/ContentText.py validates them before any asset is written. Catalogue order
is a replicated contract: units frontline=0, ranged=1, siege=2, lancer=3, scrambler=4; buildings
barracks=0, extractor=1, workshop=2. The output is deterministic: assets are
written only from those files and the art paths derived from their asset names.

Run through ./x gen generate-match-content.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ContentText

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


def build_units():
    unit_art = "/Game/Art/Units/"
    definitions = ContentText.unit_definitions()
    # Validate the complete source before writing any asset.
    tags = [
        (
            authored_enum(role_type, definition["role"], ("FRONTLINE", "RANGED", "SIEGE", "ASSAULT", "SUPPORT")),
            authored_enum(armor_type, definition["armor_class"], ("LIGHT", "HEAVY", "SHIELDED", "STRUCTURE")),
            authored_enum(damage_type, definition["damage_type"], ("KINETIC", "PIERCING", "DEMOLITION", "EMP")),
        )
        for definition in definitions
    ]
    units = []
    art_names = {definition["id"]: definition["asset_name"] for definition in definitions}
    for definition, (role, armor, damage) in zip(definitions, tags):
        name = definition["asset_name"]
        # A branch wears its base's art.
        art = art_names.get(definition["branch_of"], name)
        unit = data_asset("/Game/Units/DA_" + name, unreal.ArmyUnitDefinition)
        values = [(key, definition[key]) for key in (
            "id", "display_name", "max_health", "attack_damage", "range", "interval",
            "unit_cost", "capacity", "configuration_cost", "unit_duration", "move_speed",
            "max_shield", "pulse_interval", "pulse_radius", "pulse_building_stun_seconds",
            "branch_of", "branch_summary",
        )]
        values.extend((
            ("role", role),
            ("armor_class", armor),
            ("damage_type", damage),
            ("accent", unreal.LinearColor(*definition["accent"], 1.0)),
            ("human_mesh", mesh(unit_art + "SM_Human_" + art)),
            ("machine_mesh", mesh(unit_art + "SM_Machine_" + art)),
        ))
        apply(unit, values)
        units.append(unit)
    return units


def producer_art(unit, units):
    """Asset stem of the producer variant for a unit; a branch uses its base's."""
    base = str(unit.get_editor_property("branch_of"))
    for other in units:
        if base != "None" and str(other.get_editor_property("id")) == base:
            return other.get_name()[3:]
    return unit.get_name()[3:]


def role_meshes(definition, units):
    """Producer locked-type variants follow catalogue unit order; other buildings have none."""
    building_art = "/Game/Art/Buildings/"
    result = {"human": [], "machine": []}
    if definition["produces_forces"]:
        for faction in result:
            result[faction] = [
                mesh(building_art + "SM_%s_%s_%s" % (faction.capitalize(), definition["asset_name"], producer_art(unit, units)))
                for unit in units
            ]
    return result


def build_buildings(units):
    building_art = "/Game/Art/Buildings/"
    buildings = []
    # Extractor repurposes the existing Outpost definition and art assets; filenames remain stable.
    for definition in ContentText.building_definitions():
        asset_name = definition["asset_name"]
        building = data_asset("/Game/Content/DA_" + asset_name, unreal.BuildingDefinition)
        variants = role_meshes(definition, units)
        values = [(key, definition[key]) for key in (
            "id", "display_name", "build_cost", "max_health", "build_duration", "footprint_radius",
            "produces_forces", "requires_deposit", "offers_research",
        )]
        values.extend((
            ("accent", unreal.LinearColor(*definition["accent"], 1.0)),
            ("human_mesh", mesh(building_art + "SM_Human_" + asset_name)),
            ("machine_mesh", mesh(building_art + "SM_Machine_" + asset_name)),
            ("construction_mesh", mesh(building_art + "SM_Construction_" + asset_name)),
            ("human_role_meshes", variants["human"]),
            ("machine_role_meshes", variants["machine"]),
        ))
        apply(building, values)
        buildings.append(building)
    return buildings


def main():
    units = build_units()
    buildings = build_buildings(units)
    content = data_asset("/Game/Content/DA_MatchContent", unreal.MatchContent)
    apply(content, (("units", units), ("buildings", buildings)))
    unreal.log("MATCH_CONTENT_READY units=%d buildings=%d" % (len(units), len(buildings)))


main()
