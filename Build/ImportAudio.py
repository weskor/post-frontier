"""Import the world audio library, or only scripted announcer WAVs with -AnnouncerOnly.

Run through ./x gen import-audio; announcer generation invokes the isolated mode.
Reruns replace sound waves without changing source WAVs.
"""
import json
from pathlib import Path
import wave

import unreal

ROOT = Path(__file__).resolve().parents[1]
DEST = "/Game/Audio"
MIX = DEST + "/Mix"
assets = unreal.AssetToolsHelpers.get_asset_tools()
editor = unreal.EditorAssetLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def asset(name, cls, factory):
    path = MIX + "/" + name
    obj = editor.load_asset(path) if editor.does_asset_exist(path) else assets.create_asset(name, MIX, cls, factory())
    require(isinstance(obj, cls), "Wrong or missing audio asset: " + path)
    return obj


def save(obj):
    require(editor.save_loaded_asset(obj, only_if_is_dirty=False), "Failed to save " + obj.get_path_name())


def import_announcer():
    lines = json.loads((ROOT / "Build/Audio/announcer_lines.json").read_text())
    destination = DEST + "/Announcer"
    master = editor.load_asset(MIX + "/SC_Master")
    require(isinstance(master, unreal.SoundClass), "Import the world audio mix first: missing SC_Master")
    tasks = []
    for line in lines:
        name = "VO_" + line["id"]
        source = ROOT / "Art/Audio/Announcer" / (name + ".wav")
        with wave.open(str(source), "rb") as data:
            require(data.getframerate() == 48000 and data.getnchannels() == 1 and data.getsampwidth() == 3,
                    "Expected 48-kHz/24-bit mono: " + str(source))
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        task.set_editor_property("factory", unreal.SoundFactory())
        tasks.append(task)
    assets.import_asset_tasks(tasks)
    for line, task in zip(lines, tasks):
        expected = destination + "/VO_" + line["id"]
        paths = task.get_editor_property("imported_object_paths")
        require(len(paths) == 1, "Expected one announcer wave: " + expected)
        sound = editor.load_asset(expected)
        require(isinstance(sound, unreal.SoundWave), "Not a SoundWave: " + expected)
        sound.set_editor_property("sound_class_object", master)
        sound.set_editor_property("volume", 1.0)
        sound.set_editor_property("pitch", 1.0)
        sound.set_editor_property("priority", 4.0)
        sound.set_editor_property("looping", False)
        sound.set_editor_property("override_concurrency", False)
        sound.set_editor_property("concurrency_set", [])
        save(sound)
        unreal.log("ANNOUNCER_WAVE " + expected)
    unreal.log("ANNOUNCER_IMPORTED waves=" + str(len(lines)))


def import_world_audio():
    files = sorted((ROOT / "Art" / "Audio").glob("*/*/SW_*.wav"))
    require(len(files) == 115, "Expected 115 rendered source WAVs, found " + str(len(files)))
    for source in files:
        with wave.open(str(source), "rb") as data:
            require(data.getframerate() == 48000 and data.getnchannels() == 1 and data.getsampwidth() == 3,
                    "Expected 48-kHz/24-bit mono: " + str(source))

    editor.make_directory(MIX)
    classes = {}
    for name, volume, ui in (("Master", 1.0, False), ("Combat", 0.65, False),
                             ("Structures", 0.70, False), ("UI", 0.80, True), ("Alerts", 0.90, False),
                             ("Ambience", 0.25, False)):
        cls = asset("SC_" + name, unreal.SoundClass, unreal.SoundClassFactory)
        props = cls.get_editor_property("properties")
        props.set_editor_property("volume", volume)
        props.set_editor_property("pitch", 1.0)
        props.set_editor_property("reverb", name not in ("UI", "Ambience"))
        props.set_editor_property("default2d_reverb_send_amount", 0.0)
        props.set_editor_property("is_ui_sound", ui)
        cls.set_editor_property("properties", props)
        classes[name] = cls
    master = classes["Master"]
    master.set_editor_property("child_classes", [classes[name] for name in ("Combat", "Structures", "UI", "Alerts", "Ambience")])
    for name, cls in classes.items():
        cls.set_editor_property("parent_class", None if name == "Master" else master)
        save(cls)

    mix = asset("SM_Master", unreal.SoundMix, unreal.SoundMixFactory)
    adjust = unreal.SoundClassAdjuster()
    adjust.set_editor_property("sound_class_object", master)
    adjust.set_editor_property("volume_adjuster", 1.0)
    adjust.set_editor_property("pitch_adjuster", 1.0)
    adjust.set_editor_property("apply_to_children", True)
    mix.set_editor_property("sound_class_effects", [adjust])
    mix.set_editor_property("initial_delay", 0.0)
    mix.set_editor_property("fade_in_time", 0.0)
    mix.set_editor_property("fade_out_time", 0.0)
    mix.set_editor_property("duration", -1.0)
    save(mix)

    attenuation = asset("ATT_World", unreal.SoundAttenuation, unreal.SoundAttenuationFactory)
    settings = attenuation.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.LOGARITHMIC)
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(900.0, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", 6100.0)
    settings.set_editor_property("enable_occlusion", False)
    # UE 5.8.3 SoundAttenuation.h: air absorption uses absolute ground-listener distance.
    settings.set_editor_property("attenuate_with_lpf", True)
    settings.set_editor_property("absorption_method", unreal.AirAbsorptionMethod.LINEAR)
    settings.set_editor_property("enable_log_frequency_scaling", True)
    settings.set_editor_property("lpf_radius_min", 900.0)
    settings.set_editor_property("lpf_radius_max", 7000.0)
    settings.set_editor_property("lpf_frequency_at_min", 18000.0)
    settings.set_editor_property("lpf_frequency_at_max", 2500.0)
    settings.set_editor_property("hpf_frequency_at_min", 20.0)
    settings.set_editor_property("hpf_frequency_at_max", 20.0)
    settings.set_editor_property("enable_reverb_send", True)
    settings.set_editor_property("reverb_send_method", unreal.ReverbSendMethod.LINEAR)
    settings.set_editor_property("reverb_distance_min", 900.0)
    settings.set_editor_property("reverb_distance_max", 7000.0)
    settings.set_editor_property("reverb_wet_level_min", 0.08)
    settings.set_editor_property("reverb_wet_level_max", 0.20)
    attenuation.set_editor_property("attenuation", settings)
    save(attenuation)

    # The mixer updates the first master-reverb effect from the active ReverbEffect.
    # Author both with the same settings; runtime activation avoids the no-volume dry default.
    reverb_values = {
        "bypass_early_reflections": False, "bypass_late_reflections": False,
        "reflections_delay": 0.025, "reflections_gain": 0.12, "gain_hf": 0.55,
        "late_delay": 0.035, "decay_time": 0.85, "density": 0.65, "diffusion": 0.70,
        "air_absorption_gain_hf": 0.98, "decay_hf_ratio": 0.60, "late_gain": 0.65, "gain": 0.32,
    }
    reverb = asset("RE_World", unreal.ReverbEffect, unreal.ReverbEffectFactory)
    for name, value in reverb_values.items():
        reverb.set_editor_property(name, value)
    save(reverb)

    def reverb_factory():
        factory = unreal.SoundSubmixEffectFactory()
        factory.set_editor_property("sound_effect_submix_preset_class", unreal.SubmixEffectReverbPreset)
        return factory

    preset = asset("SFX_WorldReverb", unreal.SubmixEffectReverbPreset, reverb_factory)
    reverb_settings = unreal.SubmixEffectReverbSettings()
    for name, value in reverb_values.items():
        reverb_settings.set_editor_property(name, value)
    reverb_settings.set_editor_property("wet_level", 1.0)
    reverb_settings.set_editor_property("dry_level", 0.0)
    preset.set_editor_property("settings", reverb_settings)
    save(preset)
    submix = asset("SMX_WorldReverb", unreal.SoundSubmix, unreal.SoundSubmixFactory)
    submix.set_editor_property("submix_effect_chain", [preset])
    save(submix)

    concurrency = {}
    limits = {"Combat": 24, "Impact": 12, "Death": 8, "Construction": 4, "Structure": 8, "Alerts": 2, "UI": 4, "Ambience": 1}
    for faction in ("Human", "Machine"):
        for role in ("Frontline", "Ranged", "Siege"):
            limits[faction + "_" + role] = 6
    for name, count in limits.items():
        obj = asset("CC_" + name, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory)
        settings = obj.get_editor_property("concurrency")
        settings.set_editor_property("max_count", count)
        settings.set_editor_property("limit_to_owner", False)
        settings.set_editor_property("resolution_rule", unreal.MaxConcurrentResolutionRule.STOP_OLDEST
                                     if name in ("Alerts", "UI") else unreal.MaxConcurrentResolutionRule.STOP_FARTHEST_THEN_OLDEST)
        settings.set_editor_property("voice_steal_release_time", 0.03)
        obj.set_editor_property("concurrency", settings)
        save(obj)
        concurrency[name] = obj

    # Routing decisions are authoring-time asset metadata, not runtime gameplay strings.
    tasks = []
    for source in files:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", DEST + "/" + "/".join(source.parts[-3:-1]))
        task.set_editor_property("destination_name", source.stem)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        task.set_editor_property("factory", unreal.SoundFactory())
        tasks.append(task)
    assets.import_asset_tasks(tasks)

    for source, task in zip(files, tasks):
        paths = task.get_editor_property("imported_object_paths")
        require(len(paths) == 1, "Audio import did not produce one wave: " + str(source))
        sound = editor.load_asset(paths[0])
        require(isinstance(sound, unreal.SoundWave), "Not a SoundWave: " + paths[0])
        faction, role = source.parts[-3:-1]
        event = source.stem.split("_")[-2]
        volume, priority = (0.80 if faction == "Machine" else 1.0), 1.0
        if event in ("Attack", "Fire"):
            category, group = "Combat", "Combat"
            groups = [concurrency[group], concurrency[faction + "_" + role]]
        else:
            if role == "Ambience":
                category, group, priority = "Ambience", "Ambience", 0.25
            elif event == "Impact":
                category, group, volume, priority = "Combat", "Impact", volume * 0.70, 0.75
            elif event == "Death":
                category, group, priority = "Combat", "Death", 1.5
            elif event == "ConstructLoop":
                category, group, priority = "Structures", "Construction", 0.5
            elif event in ("HQAlarm", "HQDestroyed", "Notify", "SectorCaptured", "SectorLost"):
                category, group, priority = "Alerts", "Alerts", 3.0
            elif role == "UI":
                category, group, priority = "UI", "UI", 2.0
            else:
                category, group = "Structures", "Structure"
            groups = [concurrency[group]]
        sound.set_editor_property("sound_class_object", classes[category])
        sound.set_editor_property("volume", volume)
        sound.set_editor_property("pitch", 1.0)
        sound.set_editor_property("priority", priority)
        sound.set_editor_property("looping", event in ("ConstructLoop", "NightLoop"))
        if role == "Ambience":
            sound.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
        sound.set_editor_property("override_concurrency", False)
        sound.set_editor_property("concurrency_set", groups)
        save(sound)

    unreal.log("AUDIO_IMPORTED waves=115 support=25 total=140")


if "-AnnouncerOnly" in unreal.SystemLibrary.get_command_line().split():
    import_announcer()
else:
    import_world_audio()
