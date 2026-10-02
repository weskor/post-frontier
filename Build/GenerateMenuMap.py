"""Create the empty frontend world after building CoopRTSEditor; never changes a gameplay map."""
import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
path = "/Game/Maps/Menu"
mode = unreal.load_class(None, "/Script/CoopRTS.CommandMenuGameMode")
if not mode:
    raise RuntimeError("Build CoopRTSEditor before generating Menu")
if assets.does_asset_exist(path):
    if not levels.load_level(path):
        raise RuntimeError("Could not load Menu")
else:
    if not levels.new_level(path, False):
        raise RuntimeError("Could not create Menu")
editor.get_editor_world().get_world_settings().set_editor_property("default_game_mode", mode)
if not levels.save_current_level():
    raise RuntimeError("Could not save Menu")
unreal.log("COMMAND_MENU_GENERATED")
