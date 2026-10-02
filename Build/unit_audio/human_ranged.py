"""Human Ranged, "Offline Ranger": long rifle with antenna pack (Docs/Audio.md "Unit sound sheets").

Attack interval 1.15 s, so one shot plus its bolt cycle fits before the next shot. Direction: dark and
sci-fi. Recorded rifles are pitched down and rolled off above ~6 kHz; designed sci-fi shots, an
electric-arc tail and a servo on the bolt carry the future-tech character.
"""

import numpy as np

from .core import Event, Layer, Unit, clip, dark, event, filt, load, pitch, seconds

SOURCES = [
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part8of14.zip",
        "Pole Position - Springfield 1903A3 bolt-action rifle/M1903A3, Firing, t2, 1m, Right, Above, MKH8060.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part8of14.zip",
        "Pole Position - Springfield 1903A3 bolt-action rifle/M1903A3, Handling, Cycling Bolt, MKH416.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part7of14.zip",
        "Pole Position - Mauser Karabiner 98 kurz K98k bolt-action rifle/K98k, Firing, t2, MKH416.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 5of8/SoundMorph - FUTURE WEAPONS 3/Future Weapons 3 - Assault Rifle - Shot Single 4.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "SoundMorph - Future Weapons/Alliance-AssaultRifle_05-Single_Shot-04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "SoundMorph - Future Weapons/Resistance-AssaultRifle_03-Single_Shot-04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part3of14.zip",
        "David Dumais Audio - Futuristic Guns Sound FX Pack 3/OtherGuns_Rifle_4.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 5of8/Pole Position - The Outdoor Gun Acoustics Library/AK47_big_open_area_2m_above_behind_gun_RSM191_M.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%206of6.zip",
        "Stuart Duffield - Bullet SFX/ShotgunShell_Land_Concrete_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL1_WEAPON_4_4_GUN_GUNSHOT_FUTURISTIC.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL1_WEAPON_8_2_GUN_GUNSHOT_FUTURISTIC.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL2_WEAPON_15_5_GUN_GUNSHOT_FUTURISTIC.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - SCI-FI GunnerySCI-FI Gunnery/PM_SFG_VOL2_WEAPON_41_6_GUN_GUNSHOT_FUTURISTIC.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Electric Arcs and Energy/Electric_Arc_Reverberant_Shock_Long_05.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - Foundation Series SCI-FI vol 2/PM_FSSF2_EXOSKELETON_11_SERVO_MOVEMENT_ROTATION.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - Bullet Bys &Impacts/PM_BBI_Bullet_Impact_Dirt_3.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "Olivier Girardot - Hand Guns Sound Effects Pack/Bullet rock Impact 4.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%204of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 4of8/Olivier Girardot - Guns & Explosions/Bullet Impact 22.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%202of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 2of8/Olivier Girardot - Natural Disasters/Guns & Explosions Album - Bullet Impacts - Multiple 1.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Medieval Armor and Impacts/Plate_Impact_Hard_02.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle8of14.zip",
        "RYK-Sounds - Laser Guns/heavy hit.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle8of14.zip",
        "RYK-Sounds - Laser Guns/hitsound 2.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 5of8/SoundMorph - FUTURE WEAPONS 3/Future Weapons 3 - Grenade Launcher 2 - Hit 2.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Epic Stock Media - AAA Game Character Police Officer/HMNBrth_Police Officer Gasp Vocal Male Shocked Alert 1.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Red Libraries - Bodyfall/RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Red Libraries - Bodyfall/RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part9of14.zip",
        "SmartSoundFX \u2013 Medieval/ARMOR Body Drop Chain Leather Short 02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Gamemaster Audio - Footstep and Foley Sounds/foley_soldier_gear_equipment_metal_cloth_heavy_movement_light_08.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/SoundMorph - Robotic Lifeforms 2/Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
    ),
]


POLE_1903 = "Pole Position - Springfield 1903A3 bolt-action rifle"
POLE_K98 = "Pole Position - Mauser Karabiner 98 kurz K98k bolt-action rifle"
POLE_OUTDOOR = "Pole Position - The Outdoor Gun Acoustics Library"
PMSFX_GUNNERY = "PMSFX - SCI-FI GunnerySCI-FI Gunnery"
ARC = (
    "Sound Spark LLC - Electric Arcs and Energy",
    "Electric_Arc_Reverberant_Shock_Long_05.wav",
)
SCIFI_SHOTS = [
    (PMSFX_GUNNERY, "PM_SFG_VOL1_WEAPON_4_4_GUN_GUNSHOT_FUTURISTIC.wav"),
    (PMSFX_GUNNERY, "PM_SFG_VOL1_WEAPON_8_2_GUN_GUNSHOT_FUTURISTIC.wav"),
    (PMSFX_GUNNERY, "PM_SFG_VOL2_WEAPON_15_5_GUN_GUNSHOT_FUTURISTIC.wav"),
    (PMSFX_GUNNERY, "PM_SFG_VOL2_WEAPON_41_6_GUN_GUNSHOT_FUTURISTIC.wav"),
]
SCIFI_PUNCHES = [
    (
        "SoundMorph - FUTURE WEAPONS 3",
        "Future Weapons 3 - Assault Rifle - Shot Single 4.wav",
    ),
    ("SoundMorph - Future Weapons", "Alliance-AssaultRifle_05-Single_Shot-04.wav"),
    ("SoundMorph - Future Weapons", "Resistance-AssaultRifle_03-Single_Shot-04.wav"),
    ("David Dumais Audio - Futuristic Guns Sound FX Pack 3", "OtherGuns_Rifle_4.wav"),
]


def ranged_fire(v: int) -> list[Layer]:
    rng = np.random.default_rng(1100 + v)
    detune = rng.uniform(-0.4, 0.4)
    rifle = event(
        POLE_1903,
        "M1903A3, Firing, t2, 1m, Right, Above, MKH8060.wav",
        index=v,
        length=1.1,
    )
    # Some K98k takes register a second onset (the slap off the range wall); these indices are the shots.
    low = event(
        POLE_K98, "K98k, Firing, t2, MKH416.wav", index=(2, 3, 4, 6)[v], length=0.5
    )
    shot = event(*SCIFI_SHOTS[v], length=0.8)
    punch = event(*SCIFI_PUNCHES[v], length=0.3)
    arc = event(*ARC, skip=0.12 * v, length=0.6)
    tail = event(
        POLE_OUTDOOR,
        "AK47_big_open_area_2m_above_behind_gun_RSM191_M.wav",
        index=v,
        skip=0.04,
        length=1.0,
    )
    bolt = load(POLE_1903, "M1903A3, Handling, Cycling Bolt, MKH416.wav")
    bolt = bolt[
        seconds(0.55) : seconds(1.05)
    ]  # bolt back and home, without the handle lift and lock
    servo = event(
        "PMSFX - Foundation Series SCI-FI vol 2",
        "PM_FSSF2_EXOSKELETON_11_SERVO_MOVEMENT_ROTATION.wav",
        length=0.4,
    )
    casing = event(
        "Stuart Duffield - Bullet SFX", "ShotgunShell_Land_Concrete_02.wav", length=0.35
    )
    cycle = rng.uniform(0.34, 0.4)
    return [
        Layer("Rifle", clip(dark(pitch(rifle, detune - 1.0), 6000), fade_out=0.25)),
        Layer(
            "Rifle Low",
            clip(filt(pitch(low, detune - 1.0), "lowpass", 900), fade_out=0.2),
        ),
        Layer(
            "Sci-Fi Shot",
            clip(filt(dark(pitch(shot, -2.0), 8000), "highpass", 100), fade_out=0.25),
        ),
        Layer(
            "Sci-Fi Punch",
            clip(filt(dark(pitch(punch, -1.5), 8000), "highpass", 180), fade_out=0.08),
        ),
        # High-passed: the arc recording's deep hum would otherwise rumble through the whole attack interval.
        Layer(
            "Energy Tail",
            clip(
                filt(dark(pitch(arc, -5.0), 4000), "highpass", 350),
                fade_in=0.03,
                fade_out=0.35,
            ),
            at=0.02,
        ),
        Layer(
            "Outdoor Tail", clip(dark(tail, 3000), fade_in=0.02, fade_out=0.4), at=0.03
        ),
        Layer(
            "Bolt",
            clip(dark(pitch(bolt, rng.uniform(-2.3, -1.7)), 5000), fade_out=0.05),
            at=cycle,
        ),
        Layer(
            "Servo",
            clip(dark(pitch(servo, -3.0), 6000), fade_out=0.08),
            at=cycle + 0.02,
        ),
        Layer(
            "Casing",
            clip(dark(pitch(casing, rng.uniform(-4, -2)), 6000)),
            at=rng.uniform(0.62, 0.7),
        ),
    ]


IMPACT_HITS = [
    ("Olivier Girardot - Guns & Explosions", "Bullet Impact 22.wav"),
    ("PMSFX - Bullet Bys &Impacts", "PM_BBI_Bullet_Impact_Dirt_3.wav"),
    ("Olivier Girardot - Hand Guns Sound Effects Pack", "Bullet rock Impact 4.wav"),
]
SCIFI_HITS = [
    ("RYK-Sounds - Laser Guns", "heavy hit.wav"),
    (
        "SoundMorph - FUTURE WEAPONS 3",
        "Future Weapons 3 - Grenade Launcher 2 - Hit 2.wav",
    ),
    ("RYK-Sounds - Laser Guns", "hitsound 2.wav"),
]


def ranged_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(1200 + v)
    hit = event(*IMPACT_HITS[v], length=0.7)
    scifi = event(*SCIFI_HITS[v], length=0.6)
    # The targets are Machine shells: every hit rings armour plate and crackles with energy.
    plate = event(
        "Double Trouble Audio - Medieval Armor and Impacts",
        "Plate_Impact_Hard_02.wav",
        length=0.5,
    )
    arc = event(*ARC, skip=0.05 + 0.1 * v, length=0.3)
    debris = event(
        "Olivier Girardot - Natural Disasters",
        "Guns & Explosions Album - Bullet Impacts - Multiple 1.wav",
        index=v,
        length=0.6,
    )
    return [
        Layer(
            "Hit", clip(dark(pitch(hit, rng.uniform(-2.0, -1.0)), 6000), fade_out=0.15)
        ),
        Layer("Sci-Fi Hit", clip(dark(pitch(scifi, -2.0), 8000), fade_out=0.2)),
        Layer(
            "Armour",
            clip(dark(pitch(plate, (0, 2, -1)[v]), 7000), fade_out=0.15),
            at=0.004,
        ),
        Layer(
            "Energy",
            clip(
                filt(dark(pitch(arc, -2.0), 6000), "highpass", 400),
                fade_in=0.005,
                fade_out=0.1,
            ),
            at=0.01,
        ),
        Layer(
            "Debris",
            clip(dark(filt(debris, "highpass", 300), 5000), fade_out=0.2),
            at=0.01,
        ),
    ]


def ranged_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(1300 + v)
    gasp = event(
        "Epic Stock Media - AAA Game Character Police Officer",
        "HMNBrth_Police Officer Gasp Vocal Male Shocked Alert 1.wav",
        length=0.45,
    )
    # The antenna pack and suit electronics shut down as the Ranger drops.
    power = event(
        "SoundMorph - Robotic Lifeforms 2",
        "Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
        length=1.0,
    )
    gear = event(
        "Gamemaster Audio - Footstep and Foley Sounds",
        "foley_soldier_gear_equipment_metal_cloth_heavy_movement_light_08.wav",
        index=v % 2,
        length=0.5,
    )
    drop = event(
        "SmartSoundFX \u2013 Medieval",
        "ARMOR Body Drop Chain Leather Short 02.wav",
        length=1.2,
    )
    fall_name = (
        "RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02.wav",
        "RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10.wav",
    )[v % 2]
    fall = event("Red Libraries - Bodyfall", fall_name, length=0.9)
    land = rng.uniform(0.34, 0.42)
    return [
        Layer("Vox", clip(dark(pitch(gasp, (-1, -2.5, 0)[v]), 5000), fade_out=0.08)),
        Layer(
            "Power Down",
            clip(dark(pitch(power, (-3, -4, -2)[v]), 6000), fade_out=0.3),
            at=0.05,
        ),
        Layer(
            "Gear",
            clip(dark(pitch(gear, rng.uniform(-2, -1)), 6000), fade_out=0.1),
            at=0.12,
        ),
        Layer(
            "Armour Drop",
            clip(pitch(drop, (-1, -2, 0)[v]), fade_out=0.3),
            at=land - 0.01,
        ),
        Layer("Body Fall", clip(pitch(fall, (-1, -1, -3)[v]), fade_out=0.25), at=land),
    ]


UNIT = Unit(
    "Human",
    "Ranged",
    tracks=[
        ("Rifle", 0.0),
        ("Rifle Low", -4.0),
        ("Sci-Fi Shot", -4.0),
        ("Sci-Fi Punch", -8.0),
        ("Energy Tail", -16.0),
        ("Outdoor Tail", -14.0),
        ("Bolt", -17.0),
        ("Servo", -20.0),
        ("Casing", -24.0),
        ("Hit", -1.0),
        ("Sci-Fi Hit", -5.0),
        ("Armour", -9.0),
        ("Energy", -14.0),
        ("Debris", -13.0),
        ("Vox", -6.0),
        ("Power Down", -9.0),
        ("Gear", -11.0),
        ("Armour Drop", -3.0),
        ("Body Fall", 0.0),
    ],
    events=[
        Event("Fire", 4, 1.15, -20.0, ranged_fire),
        Event("Impact", 3, 0.9, -22.0, ranged_impact),
        Event("Death", 3, 1.6, -21.0, ranged_death),
    ],
)
