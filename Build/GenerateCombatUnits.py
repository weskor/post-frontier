"""Create the three fixed milestone-four unit Data Assets; never replace tuned assets.

After building CoopRTSEditor, run UnrealEditor-Cmd with -EnablePlugins=PythonScriptPlugin
-ExecutePythonScript="$PWD/Build/GenerateCombatUnits.py" -unattended -nullrhi -nosplash.
"""
import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
role_type = getattr(unreal, "UnitRole", None) or getattr(unreal, "EUnitRole")

for name, role, health, damage, attack_range, interval in (
    ("Frontline", role_type.FRONTLINE, 140, 14, 175.0, 0.7),
    ("Ranged", role_type.RANGED, 90, 18, 560.0, 1.15),
    ("Siege", role_type.SIEGE, 110, 42, 1150.0, 2.6),
):
    path = "/Game/Units/DA_" + name
    if assets.does_asset_exist(path):
        definition = assets.load_asset(path)
        if not isinstance(definition, unreal.ArmyUnitDefinition):
            raise RuntimeError("Unexpected asset at " + path)
        unreal.log("COMBAT_UNIT_EXISTING " + path)
        continue
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.ArmyUnitDefinition.static_class())
    definition = tools.create_asset("DA_" + name, "/Game/Units", unreal.ArmyUnitDefinition, factory)
    if not definition:
        raise RuntimeError("Could not create " + path)
    for property_name, value in (
        ("role", role), ("max_health", health), ("attack_damage", damage),
        ("range", attack_range), ("interval", interval),
    ):
        definition.set_editor_property(property_name, value)
    if not assets.save_loaded_asset(definition):
        raise RuntimeError("Could not save " + path)
    unreal.log("COMBAT_UNIT_CREATED " + path)

unreal.log("COMBAT_UNITS_READY")
