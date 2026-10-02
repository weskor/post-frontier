"""Human Siege, "Unplugger": heavy mech with giant bolt-cutter jaws (Docs/Audio.md "Unit sound sheets").

Attack interval 2.6 s: one heavy shot plus the mech settling fits before the next. The Unplugger fires a
charged cable-cutting blast from its jaws: a clamp and shear crunch, a heavy cannon report with designed
sci-fi cannon on top, a big electric discharge (it "unplugs" Machine power) and a low weight that is gone
within ~0.6 s, then a hydraulic vent, servo and a settling clunk. Direction as Human Ranged: recordings
pitched down and rolled off, designed sci-fi layers for the future-tech character.
"""

import numpy as np

from .core import (
    Audio,
    Event,
    Layer,
    Unit,
    clip,
    dark,
    event,
    filt,
    load,
    loudness,
    pitch,
    seconds,
)

SOURCES = [
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Tanks and Jet/TAJ - Tank Battle Cannon Fire [XY Stereo]-001.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part2of14.zip",
        "Bluezone - Tank - Explosion Sound Effects/Bluezone_BC0271_tank_artillery_cannon_shot_012.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Airborne Sound - Battlefield Howitzers/Howitzer,M101,C1,105 mm,Distant,Right Side,Shot,Pound,Thick.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Airborne Sound - Battlefield Howitzers/Howitzer,M101,C3,105 mm,Distant,Right Side,Shot,Explode,Crack,Sweetener.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "David Dumais Audio - Sci-Fi Weapons Pack 1/GUNMech_Heavy Mech Cock 03_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "David Dumais Audio - Sci-Fi Weapons Pack 1/SCIWeap_Heavyweapon12 Shot 04_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip",
        "Fascinated Sound - The Gun Locker SFX Pack/Automatic Cannon - MK44 - 03 - Single Shot with Report 03.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part9of14.zip",
        "SmartSoundFX - Futuristic/CANNON Plasma Shot Tonal High 10.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%205of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 5of9/MatiasMacSD - THE WEAPONS - SCI-FI WEAPONS/Weapon Bomb Explosion Sci-Fi Plasma-04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Gamemaster Audio - Sci-Fi Sounds and Sci-Fi Weapons/sci-fi_weapon_blaster_laser_boom_zap_08.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - High Voltage Electricity - The Essential Collection/Damp Electric Shock 8.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Gamemaster Audio - Magic and Spell Sounds/electric_surge_blast_04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Electric Arcs and Energy/Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%208of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 8of9/SoundMorph - ENERGY/Energy - electric_fuse-08.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%201of6.zip",
        "Alexander Kopeikin - Sci-Fi Metal Elements HD/sci-fi metal impact, steel bulkhead hit, low 01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/BlueZone - Heavy Metal Impact Sound Effects/Bluezone_BC0251_heavy_metal_impact_large_tank_01_03.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/BlueZone - Heavy Metal Impact Sound Effects/Bluezone_BC0251_heavy_metal_impact_metal_plate_medium.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/BlueZone - Heavy Metal Impact Sound Effects/Bluezone_BC0251_heavy_metal_impact_steel_barrel_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%201of6.zip",
        "Alexander Kopeikin - Black Metal/dry rusty metal impact with collapse 08.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%208of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 8of9/SoundMorph - MECHANISM/Mechanism -  Designed Mega Steam Lever-001.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Fascinated Sound - Scene Shop Tool Box/Compressed Air 06 Hiss and Spurt.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Airborne Sound - Tools/Air Compressor,Omega,5 hp,20 Gallon,Twin Cylinder,Air Release,x2.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "SoundMorph - Robotic Lifeforms/footstep_a_mech_mega_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "SoundMorph - Robotic Lifeforms/footstep_a_mech_mega_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "SoundMorph - Robotic Lifeforms/power_move_mech_18.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - High Voltage Electricity - The Essential Collection/Arc Weld Electrical Sparks 3.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 6of8/UberDuo - The Cabin Audio Playset/Switch,ElectricalPanelBreaker,On.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Broken Robot/Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/SoundMorph - Robotic Lifeforms 2/Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%202of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 2of8/Bluezone Corporation - Metal Debris/Bluezone_BC0236_metal_debris_055.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%202of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 2of8/Bluezone Corporation - Metal Debris/Bluezone_BC0236_metal_debris_097.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Undoing-Computer/Hit Impact Hard Metal Scrap Debris_UC 01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Undoing-Computer/Crush Rattle Metal Scrap Debris_UC 04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Gamemaster Audio -  Explosion Sound Pack/explosion_large_no_tail_03.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%208of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 8of9/SoundHolder -  Tractor Case II CX90/tractor Case II CX90 exterior engine on idle and off stereo M10.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Sci-Fi-Invaders/Shut Off Engine_SI 03.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%201of6.zip",
        "Alexander Kopeikin - Black Metal/heavy sheet metal low groan 13.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%201of6.zip",
        "Alexander Kopeikin - Black Metal/metal, heavy creaks, shipwreck, submarine deformation under pressure 07.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 1of8/Airborne Sound - Variety 1 Sound Effects/Steam Blast,Tube or Pipe,Distant.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part6of14.zip",
        "Pole Position - Car Destruction/Car_Destruction_Chassis_Impact_t36_Ext_Drop_on_Ground_Metal_Crash_Bounce_Various_MKH8060.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%206of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 6of9/Pole Position - Car Debris, Impacts & Crashes/mercedes_benz_dropped_1m_on_concrete_ls-5_2.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Junk and Debris/Pile of Scrap Metal - Metal Scrap Pile, Rustle, Drop, Long Tail 03.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Hits Whoosh/Heavy Sci-Fi Hit_HW 09.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Impact/Impact - Tech Debris 12.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "David Dumais Audio - Sci-Fi Weapons Pack 1/DSGNBass_Weapon Power Down 04_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/The Sound Pack Tree - Trains/166000 - Steam Blasts.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "Dramatic Cat - Claas Dominator - Combine Harvester 88 SL Maxi/VEHFarm_Claas Dominator Combine ONBOARD Turn On Start Idle Long Stop Shutoff MIX_DRCA_DOMI_Mix.wav",
    ),
]


TAJ = (
    "Double Trouble Audio - Tanks and Jet",
    "TAJ - Tank Battle Cannon Fire [XY Stereo]-001.wav",
)
HOWITZER_THICK = (
    "Airborne Sound - Battlefield Howitzers",
    "Howitzer,M101,C1,105 mm,Distant,Right Side,Shot,Pound,Thick.wav",
)
REPORTS = [
    (
        "Fascinated Sound - The Gun Locker SFX Pack",
        "Automatic Cannon - MK44 - 03 - Single Shot with Report 03.wav",
        0,
    ),
    (
        "Bluezone - Tank - Explosion Sound Effects",
        "Bluezone_BC0271_tank_artillery_cannon_shot_012.wav",
        0,
    ),
    (
        "Fascinated Sound - The Gun Locker SFX Pack",
        "Automatic Cannon - MK44 - 03 - Single Shot with Report 03.wav",
        1,
    ),
    (
        "Airborne Sound - Battlefield Howitzers",
        "Howitzer,M101,C3,105 mm,Distant,Right Side,Shot,Explode,Crack,Sweetener.wav",
        0,
    ),
]
SCIFI_CANNONS = [
    ("SmartSoundFX - Futuristic", "CANNON Plasma Shot Tonal High 10.wav"),
    (
        "Gamemaster Audio - Sci-Fi Sounds and Sci-Fi Weapons",
        "sci-fi_weapon_blaster_laser_boom_zap_08.wav",
    ),
    (
        "MatiasMacSD - THE WEAPONS - SCI-FI WEAPONS",
        "Weapon Bomb Explosion Sci-Fi Plasma-04.wav",
    ),
    (
        "David Dumais Audio - Sci-Fi Weapons Pack 1",
        "SCIWeap_Heavyweapon12 Shot 04_DDUMAIS_NONE.wav",
    ),
]
# (library, file, onset index, skip)
DISCHARGES = [
    (
        "Gamemaster Audio - Magic and Spell Sounds",
        "electric_surge_blast_04.wav",
        0,
        0.0,
    ),
    (
        "Hear and Now Sound - High Voltage Electricity - The Essential Collection",
        "Damp Electric Shock 8.wav",
        0,
        0.0,
    ),
    ("SoundMorph - ENERGY", "Energy - electric_fuse-08.wav", 0, 0.0),
    (
        "Sound Spark LLC - Electric Arcs and Energy",
        "Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav",
        1,
        0.0,
    ),
]
SCRAP = (
    "Mechanical Wave - Undoing-Computer",
    "Hit Impact Hard Metal Scrap Debris_UC 01.wav",
)
PLATE = (
    "BlueZone - Heavy Metal Impact Sound Effects",
    "Bluezone_BC0251_heavy_metal_impact_metal_plate_medium.wav",
)
STEAM_LEVER = (
    "SoundMorph - MECHANISM",
    "Mechanism -  Designed Mega Steam Lever-001.wav",
)
MECH_MOVE = ("SoundMorph - Robotic Lifeforms", "power_move_mech_18.wav")
MECH_STEPS = [
    ("SoundMorph - Robotic Lifeforms", "footstep_a_mech_mega_01.wav"),
    ("SoundMorph - Robotic Lifeforms", "footstep_a_mech_mega_02.wav"),
]
RECOCK = (
    "David Dumais Audio - Sci-Fi Weapons Pack 1",
    "GUNMech_Heavy Mech Cock 03_DDUMAIS_NONE.wav",
)
VENTS = [
    (
        "Fascinated Sound - Scene Shop Tool Box",
        "Compressed Air 06 Hiss and Spurt.wav",
        0,
    ),
    (
        "Airborne Sound - Tools",
        "Air Compressor,Omega,5 hp,20 Gallon,Twin Cylinder,Air Release,x2.wav",
        0,
    ),
    ("The Sound Pack Tree - Trains", "166000 - Steam Blasts.wav", 0),
    (
        "Airborne Sound - Tools",
        "Air Compressor,Omega,5 hp,20 Gallon,Twin Cylinder,Air Release,x2.wav",
        1,
    ),
]
SPARKS = (
    "Hear and Now Sound - High Voltage Electricity - The Essential Collection",
    "Arc Weld Electrical Sparks 3.wav",
)


def trim(x: Audio, lufs: float) -> Audio:
    """Turns a clip down to at most `lufs`, so every variant of a layer sits at about the same level."""
    return x * min(1.0, 10 ** ((lufs - loudness(x)) / 20))


def siege_fire(v: int) -> list[Layer]:
    rng = np.random.default_rng(3100 + v)
    detune = rng.uniform(-0.4, 0.4)
    # The jaws bite: a steel plate clang and a scrap-metal crunch make the shear transient.
    plate = event(*PLATE, length=0.5)
    shear = event(*SCRAP, index=(0, 2, 4, 1)[v], length=0.35)
    report_lib, report_name, report_index = REPORTS[v]
    report = event(report_lib, report_name, index=report_index, length=1.0)
    scifi = event(*SCIFI_CANNONS[v], length=1.0)
    weight = (
        event(*TAJ, index=(0, 1, 3)[v], length=0.8)
        if v < 3
        else event(*HOWITZER_THICK, length=0.8)
    )
    arc_lib, arc_name, arc_index, arc_skip = DISCHARGES[v]
    arc = event(arc_lib, arc_name, index=arc_index, skip=arc_skip, length=0.8)
    ram = event(*STEAM_LEVER, skip=(0.0, 0.45, 0.2, 0.6)[v], length=0.6)
    vent_lib, vent_name, vent_index = VENTS[v]
    vent = event(vent_lib, vent_name, index=vent_index, length=0.55)
    move = load(*MECH_MOVE)
    servo = move[seconds(0.3 + 0.7 * v) : seconds(0.75 + 0.7 * v)].copy()
    recock = event(*RECOCK, length=0.7)
    settle = rng.uniform(0.95, 1.1)
    return [
        Layer(
            "Clamp",
            clip(
                dark(pitch(plate, detune + (-2.0, -3.0, -1.5, -2.5)[v]), 6000),
                fade_out=0.3,
            ),
        ),
        Layer(
            "Shear",
            trim(clip(dark(pitch(shear, detune - 2.0), 5000), fade_out=0.15), -16.5),
            at=0.005,
        ),
        Layer(
            "Report",
            trim(
                clip(
                    filt(dark(pitch(report, detune - 2.0), 5000), "highpass", 100),
                    fade_out=0.55,
                ),
                -22.0,
            ),
            at=0.015,
        ),
        Layer(
            "Sci-Fi Cannon",
            trim(
                clip(
                    filt(dark(pitch(scifi, -2.0), 8000), "highpass", 100), fade_out=0.45
                ),
                -16.5,
            ),
            at=0.01,
        ),
        # Weight: low end only, faded out fast so nothing below 200 Hz hangs into the next shot.
        Layer(
            "Weight",
            trim(
                clip(filt(pitch(weight, detune - 1.0), "lowpass", 250), fade_out=0.55),
                -24.0,
            ),
            at=0.015,
        ),
        # High-passed so the discharge crackles above the weight instead of adding rumble.
        Layer(
            "Discharge",
            trim(
                clip(
                    filt(dark(pitch(arc, -3.0), 5000), "highpass", 350),
                    fade_in=0.01,
                    fade_out=0.4,
                ),
                -15.0,
            ),
            at=0.03,
        ),
        Layer(
            "Ram",
            clip(
                filt(dark(pitch(ram, -2.0), 6000), "highpass", 150),
                fade_in=0.02,
                fade_out=0.3,
            ),
            at=0.2,
        ),
        Layer(
            "Vent",
            clip(
                filt(dark(pitch(vent, -2.0), 5000), "highpass", 400),
                fade_in=0.01,
                fade_out=0.25,
            ),
            at=settle,
        ),
        Layer(
            "Servo",
            clip(
                filt(dark(pitch(servo, -3.0), 6000), "highpass", 200),
                fade_in=0.03,
                fade_out=0.15,
            ),
            at=settle - 0.12,
        ),
        # The jaws re-cock once the vent has blown off.
        Layer(
            "Recock",
            clip(
                filt(
                    dark(pitch(recock, rng.uniform(-5.0, -4.0)), 6000), "highpass", 200
                ),
                fade_out=0.15,
            ),
            at=settle + 0.25,
        ),
    ]


CRASHES = (
    "Pole Position - Car Destruction",
    "Car_Destruction_Chassis_Impact_t36_Ext_Drop_on_Ground_Metal_Crash_Bounce_Various_MKH8060.wav",
)
SCIFI_HITS = [
    ("Mechanical Wave - Hits Whoosh", "Heavy Sci-Fi Hit_HW 09.wav"),
    ("Glitchedtones - Impact", "Impact - Tech Debris 12.wav"),
    ("Gamemaster Audio -  Explosion Sound Pack", "explosion_large_no_tail_03.wav"),
]
METAL_HITS = [
    (
        "BlueZone - Heavy Metal Impact Sound Effects",
        "Bluezone_BC0251_heavy_metal_impact_large_tank_01_03.wav",
    ),
    (
        "BlueZone - Heavy Metal Impact Sound Effects",
        "Bluezone_BC0251_heavy_metal_impact_steel_barrel_01.wav",
    ),
    (
        "Alexander Kopeikin - Sci-Fi Metal Elements HD",
        "sci-fi metal impact, steel bulkhead hit, low 01.wav",
    ),
]
BREAKER = ("UberDuo - The Cabin Audio Playset", "Switch,ElectricalPanelBreaker,On.wav")
# (library, file, onset index, length)
POWER_CUTS = [
    (
        "Sound Spark LLC - Broken Robot",
        "Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
        0,
        0.8,
    ),
    (
        "David Dumais Audio - Sci-Fi Weapons Pack 1",
        "DSGNBass_Weapon Power Down 04_DDUMAIS_NONE.wav",
        0,
        0.9,
    ),
    (
        "SoundMorph - Robotic Lifeforms 2",
        "Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
        1,
        0.8,
    ),
]
DEBRIS = [
    ("Bluezone Corporation - Metal Debris", "Bluezone_BC0236_metal_debris_055.wav", 0),
    ("Bluezone Corporation - Metal Debris", "Bluezone_BC0236_metal_debris_097.wav", 0),
    (
        "Mechanical Wave - Undoing-Computer",
        "Hit Impact Hard Metal Scrap Debris_UC 01.wav",
        3,
    ),
]


def siege_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(3200 + v)
    detune = rng.uniform(-0.5, 0.5)
    crash = event(*CRASHES, index=(0, 2, 4)[v], length=1.0)
    scifi = event(*SCIFI_HITS[v], length=0.8)
    metal = event(*METAL_HITS[v], length=0.7)
    sparks = event(*SPARKS, index=(3, 5, 9)[v], length=0.45)
    breaker = event(*BREAKER, length=0.3)
    # The target's power drops out: a falling designed power-down under the breaker thunk.
    power_lib, power_name, power_index, power_length = POWER_CUTS[v]
    power = event(power_lib, power_name, index=power_index, length=power_length)
    debris_lib, debris_name, debris_index = DEBRIS[v]
    debris = event(debris_lib, debris_name, index=debris_index, length=0.9)
    cut = rng.uniform(0.1, 0.14)
    return [
        Layer(
            "Crunch",
            clip(
                filt(dark(pitch(crash, detune - 2.0), 5000), "highpass", 70),
                fade_out=0.5,
            ),
        ),
        Layer(
            "Sci-Fi Hit",
            clip(filt(dark(pitch(scifi, -2.0), 7000), "highpass", 60), fade_out=0.4),
        ),
        Layer(
            "Metal",
            clip(
                filt(dark(pitch(metal, detune - 1.0), 6000), "highpass", 150),
                fade_out=0.35,
            ),
            at=0.004,
        ),
        Layer(
            "Sparks",
            trim(
                clip(
                    filt(dark(pitch(sparks, -2.0), 7000), "highpass", 400), fade_out=0.2
                ),
                -20.0,
            ),
            at=0.01,
        ),
        Layer(
            "Breaker",
            clip(dark(pitch(breaker, (-5, -6, -4)[v]), 5000), fade_out=0.1),
            at=cut,
        ),
        Layer(
            "Power Cut",
            clip(
                filt(dark(pitch(power, (-3, -1, -5)[v]), 5000), "highpass", 180),
                fade_out=0.25,
            ),
            at=cut + 0.01,
        ),
        Layer(
            "Debris",
            clip(filt(dark(debris, 5000), "highpass", 300), fade_in=0.01, fade_out=0.4),
            at=0.05,
        ),
    ]


TRACTOR = (
    "SoundHolder -  Tractor Case II CX90",
    "tractor Case II CX90 exterior engine on idle and off stereo M10.wav",
)
COMBINE = (
    "Dramatic Cat - Claas Dominator - Combine Harvester 88 SL Maxi",
    "VEHFarm_Claas Dominator Combine ONBOARD Turn On Start Idle Long Stop Shutoff MIX_DRCA_DOMI_Mix.wav",
)
SHUTOFF = ("Mechanical Wave - Sci-Fi-Invaders", "Shut Off Engine_SI 03.wav")
GROAN = ("Alexander Kopeikin - Black Metal", "heavy sheet metal low groan 13.wav")
CREAKS = (
    "Alexander Kopeikin - Black Metal",
    "metal, heavy creaks, shipwreck, submarine deformation under pressure 07.wav",
)
HISSES = [
    (
        "Fascinated Sound - Scene Shop Tool Box",
        "Compressed Air 06 Hiss and Spurt.wav",
        0,
    ),
    ("The Sound Pack Tree - Trains", "166000 - Steam Blasts.wav", 1),
    (
        "Airborne Sound - Variety 1 Sound Effects",
        "Steam Blast,Tube or Pipe,Distant.wav",
        0,
    ),
]
COLLAPSES = [
    (
        "Pole Position - Car Destruction",
        "Car_Destruction_Chassis_Impact_t36_Ext_Drop_on_Ground_Metal_Crash_Bounce_Various_MKH8060.wav",
        1,
    ),
    (
        "Pole Position - Car Debris, Impacts & Crashes",
        "mercedes_benz_dropped_1m_on_concrete_ls-5_2.wav",
        0,
    ),
    (
        "Alexander Kopeikin - Black Metal",
        "dry rusty metal impact with collapse 08.wav",
        0,
    ),
]
SCRAP_PILE = [
    (
        "Double Trouble Audio - Junk and Debris",
        "Pile of Scrap Metal - Metal Scrap Pile, Rustle, Drop, Long Tail 03.wav",
        0,
    ),
    (
        "Mechanical Wave - Undoing-Computer",
        "Crush Rattle Metal Scrap Debris_UC 04.wav",
        5,
    ),
    ("Bluezone Corporation - Metal Debris", "Bluezone_BC0236_metal_debris_055.wav", 0),
]


def siege_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(3300 + v)
    # Engine stall: a diesel running, choking and dying (varispeed down, so it sounds bigger).
    engine = load(*(TRACTOR, COMBINE, TRACTOR)[v])
    start = (13.6, 16.9, 13.9)[v]
    stall = engine[seconds(start) : seconds(start + 1.7)].copy()
    shutoff = load(*SHUTOFF)
    power = shutoff[seconds((4.3, 4.1, 4.5)[v]) : seconds(5.95)].copy()
    groan = (
        load(*GROAN)[seconds(0.8) : seconds(2.4)]
        if v != 1
        else load(*CREAKS)[seconds(7.7) : seconds(9.3)]
    ).copy()
    hiss_lib, hiss_name, hiss_index = HISSES[v]
    hiss = event(hiss_lib, hiss_name, index=hiss_index, length=1.0)
    sparks = event(*SPARKS, index=(3, 5, 8)[v], length=0.8)
    col_lib, col_name, col_index = COLLAPSES[v]
    collapse = event(col_lib, col_name, index=col_index, length=(0.95, 1.5, 1.5)[v])
    step = event(*MECH_STEPS[(v + 1) % 2], length=0.6)
    pile_lib, pile_name, pile_index = SCRAP_PILE[v]
    pile = event(pile_lib, pile_name, index=pile_index, length=1.0)
    fall = rng.uniform(0.75, 0.9)
    # Hydraulics burst and arcs sputter as the mech fails; the engine dies, the frame groans and it collapses.
    return [
        Layer(
            "Hydraulic Hiss",
            clip(filt(dark(pitch(hiss, -2.0), 6000), "highpass", 300), fade_out=0.4),
        ),
        Layer(
            "Arc Sputter",
            clip(filt(dark(pitch(sparks, -3.0), 6000), "highpass", 400), fade_out=0.35),
            at=0.03,
        ),
        Layer(
            "Engine Stall",
            clip(
                filt(dark(pitch(stall, -2.0), 4000), "highpass", 80),
                fade_in=0.03,
                fade_out=0.3,
            ),
            at=0.0,
        ),
        Layer(
            "Power Down",
            clip(
                filt(dark(pitch(power, -2.0), 5000), "highpass", 120),
                fade_in=0.05,
                fade_out=0.2,
            ),
            at=0.05,
        ),
        Layer(
            "Groan",
            clip(
                filt(dark(pitch(groan, -1.0), 5000), "highpass", 250),
                fade_in=0.08,
                fade_out=0.4,
            ),
            at=0.25,
        ),
        Layer(
            "Collapse",
            trim(
                clip(
                    filt(
                        dark(pitch(collapse, rng.uniform(-3, -2)), 5000),
                        "highpass",
                        (90, 90, 150)[v],
                    ),
                    fade_out=0.6,
                ),
                -21.0,
            ),
            at=fall,
        ),
        Layer(
            "Collapse Weight",
            clip(
                filt(filt(pitch(step, -3.0), "lowpass", 300), "highpass", 40),
                fade_out=0.3,
            ),
            at=fall - 0.01,
        ),
        Layer(
            "Scrap",
            clip(filt(dark(pile, 5000), "highpass", 250), fade_in=0.01, fade_out=0.4),
            at=fall + 0.08,
        ),
    ]


UNIT = Unit(
    "Human",
    "Siege",
    tracks=[
        ("Clamp", -9.0),
        ("Shear", -5.0),
        ("Report", 0.0),
        ("Sci-Fi Cannon", -3.0),
        ("Weight", 0.0),
        ("Discharge", -3.0),
        ("Ram", -12.0),
        ("Vent", -14.0),
        ("Servo", -16.0),
        ("Recock", -16.0),
        ("Crunch", 0.0),
        ("Sci-Fi Hit", -3.0),
        ("Metal", -6.0),
        ("Sparks", 3.0),
        ("Breaker", 0.0),
        ("Power Cut", -9.0),
        ("Debris", -8.0),
        ("Hydraulic Hiss", -9.0),
        ("Arc Sputter", -2.0),
        ("Engine Stall", -4.0),
        ("Power Down", -8.0),
        ("Groan", -7.0),
        ("Collapse", 0.0),
        ("Collapse Weight", -9.0),
        ("Scrap", -9.0),
    ],
    events=[
        Event("Fire", 4, 2.3, -19.0, siege_fire),
        Event("Impact", 3, 1.3, -22.0, siege_impact),
        Event("Death", 3, 2.6, -21.0, siege_death),
    ],
)
