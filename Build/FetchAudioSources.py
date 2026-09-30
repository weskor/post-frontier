#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["remotezip"]
# ///
"""Fetch the recorded source layers used by Build/GenerateUnitAudio.py.

    uv run Build/FetchAudioSources.py

Sources are individual files from the Sonniss #GameAudioGDC bundles (https://sonniss.com/gameaudiogdc),
pulled out of the multi-GB bundle ZIPs with HTTP range requests so only the listed files download.
They unpack into the Git-ignored cache Saved/AudioSources/Sonniss/<Library>/<File>.wav and are never
committed: the bundle licence forbids passing the sounds on as sound effects, even modified. Only the
finished game sounds in Art/Audio are kept. Rerunnable: files already present are skipped.

The list is the single record of what is used; Art/Audio/SOURCES.md mirrors it.
"""
from pathlib import Path

from remotezip import RemoteZip

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "Saved" / "AudioSources" / "Sonniss"
BASE = "https://downloads.sonniss.com/"
# The download host rejects requests without a browser user agent and the bundle page as referrer.
HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/140 Safari/537.36",
    "Referer": "https://gdc.sonniss.com/",
}

GDC2016_6 = "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%206of6.zip"
GDC2017_3 = "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip"
GDC2018_3 = "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip"
GDC2018_4 = "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%204of8.zip"
GDC2018_5 = "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%205of8.zip"
GDC2019_2 = "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%202of8.zip"
GDC2019_5 = "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip"
GDC2019_6 = "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip"
GDC2020_3 = "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part3of14.zip"
GDC2020_5 = "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip"
GDC2020_7 = "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part7of14.zip"
GDC2020_8 = "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part8of14.zip"
GDC2020_9 = "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part9of14.zip"
GDC2020_12 = "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip"
GDC2023_8 = "Sonniss.com-GDC2023-GameAudioBundle8of14.zip"
GDC2026_2 = "Sonniss.com-GDC2026-GameAudioBundle2of5.zip"
GDC_ORIG_4 = "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip"

P2017_3 = "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/"
P2018_3 = "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/"
P2018_4 = "Sonniss.com - GDC 2018 - Game Audio Bundle Part 4of8/"
P2018_5 = "Sonniss.com - GDC 2018 - Game Audio Bundle Part 5of8/"
P2019_2 = "Sonniss.com - GDC 2019 - Game Audio Bundle Part 2of8/"
P2019_5 = "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/"
P2019_6 = "Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/"

# (bundle zip, member path inside the zip)
FILES = [
    # Human Ranged fire: long-rifle shots, bolt cycling, sci-fi sweeteners, outdoor tail, casing.
    (GDC2020_8, "Pole Position - Springfield 1903A3 bolt-action rifle/M1903A3, Firing, t2, 1m, Right, Above, MKH8060.wav"),
    (GDC2020_8, "Pole Position - Springfield 1903A3 bolt-action rifle/M1903A3, Handling, Cycling Bolt, MKH416.wav"),
    (GDC2020_7, "Pole Position - Mauser Karabiner 98 kurz K98k bolt-action rifle/K98k, Firing, t2, MKH416.wav"),
    (GDC2018_5, P2018_5 + "SoundMorph - FUTURE WEAPONS 3/Future Weapons 3 - Assault Rifle - Shot Single 4.wav"),
    (GDC_ORIG_4, "SoundMorph - Future Weapons/Alliance-AssaultRifle_05-Single_Shot-04.wav"),
    (GDC_ORIG_4, "SoundMorph - Future Weapons/Resistance-AssaultRifle_03-Single_Shot-04.wav"),
    (GDC2020_3, "David Dumais Audio - Futuristic Guns Sound FX Pack 3/OtherGuns_Rifle_4.wav"),
    (GDC2018_5, P2018_5 + "Pole Position - The Outdoor Gun Acoustics Library/AK47_big_open_area_2m_above_behind_gun_RSM191_M.wav"),
    (GDC2016_6, "Stuart Duffield - Bullet SFX/ShotgunShell_Land_Concrete_02.wav"),
    (GDC2020_5, "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL1_WEAPON_4_4_GUN_GUNSHOT_FUTURISTIC.wav"),
    (GDC2020_5, "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL1_WEAPON_8_2_GUN_GUNSHOT_FUTURISTIC.wav"),
    (GDC2020_5, "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL2_WEAPON_15_5_GUN_GUNSHOT_FUTURISTIC.wav"),
    (GDC2020_5, "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL2_WEAPON_41_6_GUN_GUNSHOT_FUTURISTIC.wav"),
    (GDC2020_12, "Sound Spark LLC - Electric Arcs and Energy/Electric_Arc_Reverberant_Shock_Long_05.wav"),
    (GDC2020_5, "PMSFX - Foundation Series SCI-FI vol 2/PM_FSSF2_EXOSKELETON_11_SERVO_MOVEMENT_ROTATION.wav"),
    # Human Ranged impact: bullet hits on dirt and rock, sci-fi hits, a debris burst and armour plate.
    (GDC2020_5, "PMSFX - Bullet Bys &Impacts/PM_BBI_Bullet_Impact_Dirt_3.wav"),
    (GDC2020_5, "Olivier Girardot - Hand Guns Sound Effects Pack/Bullet rock Impact 4.wav"),
    (GDC2018_4, P2018_4 + "Olivier Girardot - Guns & Explosions/Bullet Impact 22.wav"),
    (GDC2019_2, P2019_2 + "Olivier Girardot - Natural Disasters/Guns & Explosions Album - Bullet Impacts - Multiple 1.wav"),
    (GDC2017_3, P2017_3 + "Double Trouble Audio - Medieval Armor and Impacts/Plate_Impact_Hard_02.wav"),
    (GDC2023_8, "RYK-Sounds - Laser Guns/heavy hit.wav"),
    (GDC2023_8, "RYK-Sounds - Laser Guns/hitsound 2.wav"),
    (GDC2018_5, P2018_5 + "SoundMorph - FUTURE WEAPONS 3/Future Weapons 3 - Grenade Launcher 2 - Hit 2.wav"),
    # Human Ranged death: gasp, suit power-down, armoured body drop, gear, body fall.
    (GDC2026_2, "Epic Stock Media - AAA Game Character Police Officer/HMNBrth_Police Officer Gasp Vocal Male Shocked Alert 1.wav"),
    (GDC2019_5, P2019_5 + "Red Libraries - Bodyfall/RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02.wav"),
    (GDC2019_5, P2019_5 + "Red Libraries - Bodyfall/RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10.wav"),
    (GDC2020_9, "SmartSoundFX – Medieval/ARMOR Body Drop Chain Leather Short 02.wav"),
    (GDC2018_3, P2018_3 + "Gamemaster Audio - Footstep and Foley Sounds/foley_soldier_gear_equipment_metal_cloth_heavy_movement_light_08.wav"),
    (GDC2019_6, P2019_6 + "SoundMorph - Robotic Lifeforms 2/Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav"),
]


def local_path(member: str) -> Path:
    library, name = member.split("/")[-2:]
    return CACHE / library / name


def main() -> None:
    by_zip: dict[str, list[str]] = {}
    for bundle, member in FILES:
        if not local_path(member).exists():
            by_zip.setdefault(bundle, []).append(member)
    for bundle, members in by_zip.items():
        with RemoteZip(BASE + bundle, headers=HEADERS) as archive:
            for member in members:
                out = local_path(member)
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(archive.read(member))
                print(f"fetched {out.relative_to(ROOT)}")
    print(f"AUDIO_SOURCES_READY {len(FILES)}")


if __name__ == "__main__":
    main()
