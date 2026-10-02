"""Human Frontline, "Luddite": powered armour, riot shield and sledgehammer (Docs/Audio.md "Unit sound sheets").

Attack interval 0.7 s, so every swing is a short servo wind-up into one hard strike whose low end is gone
well before the next swing. Direction: dark and sci-fi. Recorded hammer hits on thick iron and an oil
barrel are pitched down and rolled off; a designed robot metal impact, an electric snap and the suit's
servos and hydraulics carry the future-tech character. Impacts land on Machine shells, so they add a
ceramic/glass crack; deaths vent hydraulics, power the suit down and drop the riot shield.
"""

import numpy as np

from .core import Event, Layer, Unit, clip, dark, event, filt, pitch

SOURCES = [
    (
        "Sonniss.com-GDC2024-GameAudioBundle6of9.zip",
        "Pole Position - The Metal Hit Sweeteners Library/Iron - Thick - HIT - Hammer.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle6of9.zip",
        "Pole Position - The Metal Hit Sweeteners Library/Oil Barrel - HIT - Hammer - Take 2.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Sound Spark LLC \u2013 Metal Hits, Scrapes and Squeaks/Metal_Sheets_Sledgehammer_Hit_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip",
        "Game Audio Factory - GAFSFX01 - Dark Materials/GAF SFX01 - HORROR - Hammer Impact, huge.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%207of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 7of8/2496SoundEffects - Super Heroes Sound Design/The Captain's Shield Metal Hit 5.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Hits Whoosh/Heavy Sci-Fi Hit_HW 09.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Demolisher - Robot/Bluezone_BC0290_demolisher_metal_impact_002.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Demolisher - Robot/Bluezone_BC0290_demolisher_debris_rubble_texture_004.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - High Voltage Electricity - The Essential Collection/Damp Electric Shock 8.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip",
        "MatiasMacSD - THE MACHINES - ROBOTIC SOUNDS/Robot_Servo_006.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Epic Stock Media - Tower Defense Game/ROBTMvmt_Tower Deploy Hitech Robot Motor Dark Thump Servo Whine 04_ESM_TDG.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Gamemaster Audio - Punch and Combat Sounds/whoosh_weapon_knife_swing_04.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle3of14.zip",
        "Justsoundeffects - Industrial Robot/MECHHydr_Hydraulic Pressure Releasing_JSE_IR_01_Stereo.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Window,Small,Crack,Medium Impact,Bright.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Undoing-Computer/Drop Fall Metal Rattle Scrap Debris_UC 10.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%201of5.zip",
        "Coll Anderson - Car Destruction/EFX EXT Metal Impact Drop 01 A.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%202of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 2of8/Christophe Davaille \u2013 The Gym - Sounds Of Bodybuilding/TG_Weight Metal Plate 10kg_Dropped_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Red Libraries - Bodyfall/RL_bodyfall_Metal_Grid_M3_Close_Stereo_Hard_Impact_07.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Junk and Debris/Pile of Scrap Metal - Metal Scrap Pile, Rustle, Drop, Long Tail 03.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/SoundMorph - Robotic Lifeforms 2/Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Broken Robot/Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Gamemaster Audio -  Human Vocalizations/voice_male_b_death_low_09.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%201of6.zip",
        "Bottle Rocket Fx - Scream/Grunt_Pain_Male_BB_10_SCREAM LIBRARY_BRFX-004.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Articulated Sounds - Fight Vocalizations/EMOTE Joshua, Man, Pain Hurt Grunt Big 03.wav",
    ),
]


POLE_METAL = "Pole Position - The Metal Hit Sweeteners Library"
IRON = (POLE_METAL, "Iron - Thick - HIT - Hammer.wav")
BARREL = (POLE_METAL, "Oil Barrel - HIT - Hammer - Take 2.wav")
HUGE = (
    "Game Audio Factory - GAFSFX01 - Dark Materials",
    "GAF SFX01 - HORROR - Hammer Impact, huge.wav",
)
ROBOT_HIT = (
    "BluezoneCorp - Demolisher - Robot",
    "Bluezone_BC0290_demolisher_metal_impact_002.wav",
)
SHOCK = (
    "Hear and Now Sound - High Voltage Electricity - The Essential Collection",
    "Damp Electric Shock 8.wav",
)
STRIKE = 0.09  # hammer contact, after the servo wind-up


def frontline_attack(v: int) -> list[Layer]:
    rng = np.random.default_rng(2100 + v)
    detune = rng.uniform(-0.4, 0.4)
    servo = event(
        "MatiasMacSD - THE MACHINES - ROBOTIC SOUNDS",
        "Robot_Servo_006.wav",
        skip=(0.12, 0.3, 0.5, 0.2)[v],
        length=0.1,
    )
    hydraulic = event(
        "Epic Stock Media - Tower Defense Game",
        "ROBTMvmt_Tower Deploy Hitech Robot Motor Dark Thump Servo Whine 04_ESM_TDG.wav",
        skip=(0.1, 0.18, 0.26, 0.34)[v],
        length=0.1,
    )
    swing = event(
        "Gamemaster Audio - Punch and Combat Sounds",
        "whoosh_weapon_knife_swing_04.wav",
        length=0.2,
    )
    # Iron takes 1, 3, 4 and 6 are the full-strength blows; the barrel gives the hollow armour body.
    hammer = event(*IRON, index=(1, 3, 4, 6)[v], length=0.35)
    body = event(*BARREL, index=(0, 2, 4, 7)[v], length=0.4)
    weight = event(*HUGE, index=(1, 2, 3, 1)[v], length=0.3)
    robot = event(*ROBOT_HIT, length=0.28)
    snap = event(*SHOCK, skip=0.1 + 0.45 * v, length=0.12)
    ring = event(
        "2496SoundEffects - Super Heroes Sound Design",
        "The Captain's Shield Metal Hit 5.wav",
        length=0.35,
    )
    return [
        Layer(
            "Servo",
            clip(
                filt(dark(pitch(servo, -3.0), 6000), "highpass", 200),
                fade_in=0.02,
                fade_out=0.03,
            ),
        ),
        Layer(
            "Hydraulic",
            clip(
                filt(dark(pitch(hydraulic, -2.0), 5000), "highpass", 250),
                fade_in=0.02,
                fade_out=0.03,
            ),
        ),
        Layer(
            "Swing",
            clip(
                dark(pitch(swing, rng.uniform(-6, -4)), 5000),
                fade_in=0.03,
                fade_out=0.04,
            ),
        ),
        Layer(
            "Hammer",
            clip(dark(pitch(hammer, detune - 2.0), 8000), fade_out=0.2),
            at=STRIKE,
        ),
        Layer(
            "Body",
            clip(
                filt(dark(pitch(body, detune - 1.0), 5000), "highpass", 80),
                fade_out=0.25,
            ),
            at=STRIKE,
        ),
        Layer(
            "Weight",
            clip(
                filt(
                    filt(pitch(weight, (-1, -1, -2, -3)[v]), "lowpass", 1500),
                    "highpass",
                    35,
                ),
                fade_out=0.2,
            ),
            at=STRIKE,
        ),
        Layer(
            "Sci-Fi Hit",
            clip(
                filt(dark(pitch(robot, (-2, -3, -1, -4)[v]), 7000), "highpass", 200),
                fade_out=0.18,
            ),
            at=STRIKE,
        ),
        # High-passed: the shock recording carries a mains hum that would rumble under the next swing.
        Layer(
            "Snap",
            clip(filt(dark(snap, 7000), "highpass", 500), fade_in=0.002, fade_out=0.06),
            at=STRIKE + 0.005,
        ),
        Layer(
            "Ring",
            clip(
                filt(dark(pitch(ring, -3.0 + detune), 5000), "highpass", 300),
                fade_out=0.25,
            ),
            at=STRIKE + 0.01,
        ),
    ]


CRACKS = [
    (
        "Chris Skyes - Shards Broken Glass",
        "Window,Small,Crack,Medium Impact,Bright.wav",
    ),
    (
        "Chris Skyes - Shards Broken Glass",
        "Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
    ),
    (
        "Chris Skyes - Shards Broken Glass",
        "Window,Small,Crack,Medium Impact,Bright.wav",
    ),
]


def frontline_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(2200 + v)
    detune = rng.uniform(-0.5, 0.5)
    clang = event(*BARREL, index=(1, 5, 8)[v], length=0.45)
    plate = event(
        "Sound Spark LLC \u2013 Metal Hits, Scrapes and Squeaks",
        "Metal_Sheets_Sledgehammer_Hit_02.wav",
        length=0.5,
    )
    crunch = event(
        "Coll Anderson - Car Destruction",
        "EFX EXT Metal Impact Drop 01 A.wav",
        index=(0, 1, 2)[v],
        length=0.4,
    )
    # The targets are Machine shells: pearl-white ceramic and glass that cracks under the hammer.
    crack = event(*CRACKS[v], length=0.4)
    scifi = event(
        "Mechanical Wave - Hits Whoosh", "Heavy Sci-Fi Hit_HW 09.wav", length=0.3
    )
    debris = (
        event(
            "Mechanical Wave - Undoing-Computer",
            "Drop Fall Metal Rattle Scrap Debris_UC 10.wav",
            length=0.5,
        ),
        event(
            "BluezoneCorp - Demolisher - Robot",
            "Bluezone_BC0290_demolisher_debris_rubble_texture_004.wav",
            length=0.5,
        ),
        event(
            "Mechanical Wave - Undoing-Computer",
            "Drop Fall Metal Rattle Scrap Debris_UC 10.wav",
            skip=0.1,
            length=0.45,
        ),
    )[v]
    return [
        Layer(
            "Clang",
            clip(
                filt(dark(pitch(clang, detune - 3.0), 5000), "highpass", 80),
                fade_out=0.25,
            ),
        ),
        Layer(
            "Plate",
            clip(
                filt(dark(pitch(plate, (-1, -2, 0)[v]), 5000), "highpass", 90),
                fade_out=0.3,
            ),
        ),
        Layer(
            "Crunch",
            clip(
                filt(dark(pitch(crunch, detune - 1.0), 6000), "highpass", 150),
                fade_out=0.2,
            ),
            at=0.003,
        ),
        Layer(
            "Crack",
            clip(
                filt(dark(pitch(crack, (-2, -2, -4)[v]), 7000), "highpass", 600),
                fade_out=0.2,
            ),
            at=0.006,
        ),
        Layer(
            "Impact Sci-Fi",
            clip(
                filt(dark(pitch(scifi, (-1, -2, -3)[v]), 7000), "highpass", 120),
                fade_out=0.2,
            ),
        ),
        Layer(
            "Debris",
            clip(dark(filt(debris, "highpass", 300), 5000), fade_out=0.2),
            at=0.02,
        ),
    ]


def frontline_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(2300 + v)
    vox = (
        event(
            "Articulated Sounds - Fight Vocalizations",
            "EMOTE Joshua, Man, Pain Hurt Grunt Big 03.wav",
            length=0.5,
        ),
        # Only the first cry: the rest of the take is a long moan that would sit over the fall.
        event(
            "Gamemaster Audio -  Human Vocalizations",
            "voice_male_b_death_low_09.wav",
            length=0.4,
        ),
        event(
            "Bottle Rocket Fx - Scream",
            "Grunt_Pain_Male_BB_10_SCREAM LIBRARY_BRFX-004.wav",
            length=0.45,
        ),
    )[v]
    # The suit's servos wind down as it loses power.
    servo = (
        "Sound Spark LLC - Broken Robot",
        "Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
    )
    power = (
        event(*servo, length=0.8),
        event(*servo, skip=0.08, length=0.7),
        event(
            "SoundMorph - Robotic Lifeforms 2",
            "Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
            skip=0.52,
            length=0.8,
        ),
    )[v]
    hiss = event(
        "Justsoundeffects - Industrial Robot",
        "MECHHydr_Hydraulic Pressure Releasing_JSE_IR_01_Stereo.wav",
        index=(1, 2, 3)[v],
        length=0.7,
    )
    collapse = event(
        "Double Trouble Audio - Junk and Debris",
        "Pile of Scrap Metal - Metal Scrap Pile, Rustle, Drop, Long Tail 03.wav",
        skip=(0.4, 0.5, 0.45)[v],
        length=0.9,
    )
    fall = event(
        "Red Libraries - Bodyfall",
        "RL_bodyfall_Metal_Grid_M3_Close_Stereo_Hard_Impact_07.wav",
        length=0.7,
    )
    weight = event(*HUGE, index=(2, 3, 1)[v], length=0.3)
    # A 10 kg plate hitting the floor and settling: the riot shield falling from the Luddite's arm.
    shield = event(
        "Christophe Davaille \u2013 The Gym - Sounds Of Bodybuilding",
        "TG_Weight Metal Plate 10kg_Dropped_01.wav",
        index=(0, 3, 4)[v],
        length=0.65,
    )
    land = rng.uniform(0.42, 0.5)
    return [
        # Rolled off hard: the voice is heard from inside the helmet.
        Layer("Vox", clip(dark(pitch(vox, (-2, -1, -2)[v]), 3000), fade_out=0.1)),
        Layer(
            "Power Down",
            clip(
                filt(dark(pitch(power, (-3, -5, -4)[v]), 5000), "highpass", 150),
                fade_out=0.25,
            ),
            at=0.03,
        ),
        Layer(
            "Hiss",
            clip(
                filt(dark(pitch(hiss, -2.0), 5000), "highpass", 300),
                fade_in=0.01,
                fade_out=0.3,
            ),
            at=0.1,
        ),
        Layer(
            "Collapse",
            clip(
                filt(dark(pitch(collapse, (-1, -2, -1.5)[v]), 6000), "highpass", 120),
                fade_out=0.3,
            ),
            at=land - 0.12,
        ),
        Layer(
            "Body Fall",
            clip(dark(pitch(fall, (-2, -3, -1)[v]), 6000), fade_out=0.3),
            at=land,
        ),
        Layer(
            "Fall Weight",
            clip(filt(pitch(weight, -2.0), "lowpass", 1200), fade_out=0.2),
            at=land,
        ),
        Layer(
            "Shield",
            clip(
                filt(
                    dark(pitch(shield, rng.uniform(-2.0, -1.0)), 5000), "highpass", 90
                ),
                fade_out=0.3,
            ),
            at=land + rng.uniform(0.18, 0.26),
        ),
    ]


UNIT = Unit(
    "Human",
    "Frontline",
    tracks=[
        ("Servo", -14.0),
        ("Hydraulic", -16.0),
        ("Swing", -14.0),
        ("Hammer", 0.0),
        ("Body", -4.0),
        ("Weight", -7.0),
        ("Sci-Fi Hit", -9.0),
        ("Snap", -9.0),
        ("Ring", -16.0),
        ("Clang", 0.0),
        ("Plate", -3.0),
        ("Crunch", -6.0),
        ("Crack", -9.0),
        ("Impact Sci-Fi", -7.0),
        ("Debris", -12.0),
        ("Vox", -8.0),
        ("Power Down", -10.0),
        ("Hiss", -12.0),
        ("Collapse", -8.0),
        ("Body Fall", -2.0),
        ("Fall Weight", -5.0),
        ("Shield", -3.0),
    ],
    events=[
        Event("Attack", 4, 0.7, -20.0, frontline_attack),
        Event("Impact", 3, 0.8, -22.0, frontline_impact),
        Event("Death", 3, 1.6, -21.0, frontline_death),
    ],
)
