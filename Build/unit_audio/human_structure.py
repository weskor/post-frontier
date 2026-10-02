"""Human Structure: night-shift steel, hydraulic prefab anchors and military machinery.

Recorded tools and vehicle mechanisms provide the technology, not UI oscillators. The alarm is a
real tank siren / boat horn with isolated radio static. Construction keeps its welding bed alive
through the whole 4.5-second region; the renderer overlaps the final 0.5 seconds into the start.
"""
import numpy as np

from .core import Event, Layer, Unit, clip, dark, event, filt, load, pitch, seconds

SOURCES = [
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Fascinated Sound - Scene Shop Tool Box/Impact Nail Gun 01 Single Shot Compressed Air and Action.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle2of9.zip',
     'Jake Fielding - Drill - Foley & Bursts/TOOLPowr_Drill Burst, Rev, Mechanical Tool, Electric,_Jake Fielding_Drill_09.wav'),
    ('Sonniss.com-GDC2023-GameAudioBundle1of14.zip',
     '344 Audio - Nuts And Bolts/Ratchet Clicking, Spin Once To Slow, Spin Twice To Slow, Spin Intermittent.wav'),
    ('Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%202of8.zip',
     'Sonniss.com - GDC 2019 - Game Audio Bundle Part 2of8/Cfry – Hand Tools/socket wrench 5.wav'),
    ('Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%202of8.zip',
     'Sonniss.com - GDC 2019 - Game Audio Bundle Part 2of8/Cfry – Hand Tools/toolbox shuffling 3.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%207of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 7of8/Wakerone - Electric Deluxe/70-Welding-Electric-Micro1.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part13of14.zip',
     'The Sound Pack Tree - Construction & Tools/270901 - Rotary Hammer Drill in Wall 01.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Tools and Wood/Sawblade Drop.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'RDGSFX008 - The Metal Shelf/Metal Shelf Hit 02.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'RDGSFX008 - The Metal Shelf/Metal Shelf Low Movement 02.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'Mechanical Wave - Foley Session 01/Chain Metal Drop On Floor_VS 02.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'BluezoneCorp - Industrial Lever Switch/Bluezone_BC0302_industrial_lever_switch_014.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'BluezoneCorp - Industrial Lever Switch/Bluezone_BC0302_industrial_lever_switch_039.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part4of14.zip',
     'Ivo Vicic - Studer A80 Master recorder analog tape machine/03 Studer A80_power switch on_mono_C.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip',
     'Sound Sower - Electric Field/Electric, Switch ON OFF, power, turn-on, medium frequency, motor, pulse, hum.wav'),
    ('Sonniss.com-GDC2023-GameAudioBundle8of14.zip',
     'SanojSounds - Essentials Vol.1/AIRMisc_MetalCanisterPressureQuickAirReleaseSuckIn_SanojSounds_MKH416-2496-012.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%206of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 6of9/Pole Position - Hydraulics & Pneumatics Library/Robot_1_axle_1_speed_-1_MKH8060.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Hammer & Spark Audio Production - Warrior IFV Custom Vehicle Recording/WR_EXT_Troop Door Open_Mono.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip',
     'Levan Nadashvili - Soldier Footsteps/FS Metal Soldier Walk N01.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip',
     'Levan Nadashvili - Soldier Footsteps/FS Metal Soldier Walk N05.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%205of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 5of8/Pole Position - Sherman M4A1 Medium Tank/Sherman_M4A1_t11_foley_siren_long_CMC6.wav'),
    ('Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%204of8.zip',
     'Sonniss.com - GDC 2019 - Game Audio Bundle Part 4of8/Pole Position - Narrowboat Gardner 4LW 1960/narrowboat_t23_var_sfx_horn_various_XY_RSM191.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%204of6.zip',
     'Sergey Eybog - Handheld Tranceivers/Walkie Talkie,Kenwood TK-3107 FM,Radio Communication,No Speech,Static,Feedback Squelch,Manipulations.wav'),
    ('Sonniss.com-GDC2023-GameAudioBundle2of14.zip',
     'BluezoneCorp - Building Collapse/Bluezone_BC0275_building_collapse_debris_falling_rock_rubble_008.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'RDGSFX008 - The Metal Shelf/Metal Moans 04.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'RDGSFX008 - The Metal Shelf/Falling Rocks on Metal 04.wav'),
    ('Sonniss.com-GDC2023-GameAudioBundle1of14.zip',
     '344 Audio - Nuts And Bolts/Hitting Chisel With Hammer Close.wav'),
    ('Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip',
     'Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Sound Spark LLC – Metal Hits, Scrapes and Squeaks/Metal_Barrel_Bolt_Lock_Loose_Handling_03.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'DavidDumais - Explosion SFX Pack/EXPLReal_Medium Realistic Explosion 15_DDUMAIS_NONE.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'DavidDumais - Explosion SFX Pack/DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav'),
    ('Sonniss.com-GDC2023-GameAudioBundle2of14.zip',
     'BluezoneCorp - Detonation - Explosion/Bluezone_BC0277_explosion_urban_004_02.wav'),
]

NAIL = ('Fascinated Sound - Scene Shop Tool Box',
        'Impact Nail Gun 01 Single Shot Compressed Air and Action.wav')
DRILL = ('Jake Fielding - Drill - Foley & Bursts',
         'TOOLPowr_Drill Burst, Rev, Mechanical Tool, Electric,_Jake Fielding_Drill_09.wav')
RATCHET = ('344 Audio - Nuts And Bolts',
           'Ratchet Clicking, Spin Once To Slow, Spin Twice To Slow, Spin Intermittent.wav')
WRENCH = ('Cfry – Hand Tools', 'socket wrench 5.wav')
TOOLBOX = ('Cfry – Hand Tools', 'toolbox shuffling 3.wav')
WELD = ('Wakerone - Electric Deluxe', '70-Welding-Electric-Micro1.wav')
HAMMER_DRILL = ('The Sound Pack Tree - Construction & Tools', '270901 - Rotary Hammer Drill in Wall 01.wav')
SAWBLADE = ('Double Trouble Audio - Tools and Wood', 'Sawblade Drop.wav')
SHELF_HIT = ('RDGSFX008 - The Metal Shelf', 'Metal Shelf Hit 02.wav')
SCAFFOLD = ('RDGSFX008 - The Metal Shelf', 'Metal Shelf Low Movement 02.wav')
CHAIN = ('Mechanical Wave - Foley Session 01', 'Chain Metal Drop On Floor_VS 02.wav')
LEVERS = [('BluezoneCorp - Industrial Lever Switch', f'Bluezone_BC0302_industrial_lever_switch_{n}.wav')
          for n in ('014', '039')]
RELAY = ('Ivo Vicic - Studer A80 Master recorder analog tape machine', '03 Studer A80_power switch on_mono_C.wav')
MOTOR = ('Sound Sower - Electric Field',
         'Electric, Switch ON OFF, power, turn-on, medium frequency, motor, pulse, hum.wav')
AIR = ('SanojSounds - Essentials Vol.1',
       'AIRMisc_MetalCanisterPressureQuickAirReleaseSuckIn_SanojSounds_MKH416-2496-012.wav')
HYDRAULIC = ('Pole Position - Hydraulics & Pneumatics Library', 'Robot_1_axle_1_speed_-1_MKH8060.wav')
DOOR = ('Hammer & Spark Audio Production - Warrior IFV Custom Vehicle Recording', 'WR_EXT_Troop Door Open_Mono.wav')
BOOTS = [('Levan Nadashvili - Soldier Footsteps', f'FS Metal Soldier Walk N{n}.wav') for n in ('01', '05')]
SIREN = ('Pole Position - Sherman M4A1 Medium Tank', 'Sherman_M4A1_t11_foley_siren_long_CMC6.wav')
HORN = ('Pole Position - Narrowboat Gardner 4LW 1960', 'narrowboat_t23_var_sfx_horn_various_XY_RSM191.wav')
RADIO = ('Sergey Eybog - Handheld Tranceivers',
         'Walkie Talkie,Kenwood TK-3107 FM,Radio Communication,No Speech,Static,Feedback Squelch,Manipulations.wav')
COLLAPSE = ('BluezoneCorp - Building Collapse', 'Bluezone_BC0275_building_collapse_debris_falling_rock_rubble_008.wav')
MOAN = ('RDGSFX008 - The Metal Shelf', 'Metal Moans 04.wav')
ROCKS = ('RDGSFX008 - The Metal Shelf', 'Falling Rocks on Metal 04.wav')
CHISEL = ('344 Audio - Nuts And Bolts', 'Hitting Chisel With Hammer Close.wav')
LOCK = ('Sound Spark LLC – Metal Hits, Scrapes and Squeaks', 'Metal_Barrel_Bolt_Lock_Loose_Handling_03.wav')
BLASTS = [
    ('DavidDumais - Explosion SFX Pack', 'EXPLReal_Medium Realistic Explosion 15_DDUMAIS_NONE.wav'),
    ('DavidDumais - Explosion SFX Pack',
     'DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav'),
    ('BluezoneCorp - Detonation - Explosion', 'Bluezone_BC0277_explosion_urban_004_02.wav'),
]


def section(source: tuple[str, str], start: float, length: float) -> np.ndarray:
    """Explicit inspected windows, rather than false onsets in continuous machinery/static."""
    return load(*source)[seconds(start):seconds(start + length)].copy()


def place(v: int) -> list[Layer]:
    rng = np.random.default_rng(7100 + v)
    land = event(*SHELF_HIT, length=0.85)
    anchor = event(*NAIL, length=0.35)
    return [
        Layer('Prefab', clip(dark(filt(pitch(land, -4.0 - v), 'highpass', 95), 3800)[:seconds(1.1)], fade_out=0.25)),
        Layer('Landing Low', clip(filt(pitch(land[:seconds(0.18)], -5.0), 'lowpass', 210), fade_out=0.12)),
        Layer('Locks', clip(dark(pitch(event(*LOCK, index=v, length=0.4), -3.0), 4200), fade_out=0.1), at=0.22),
        Layer('Anchors', clip(dark(pitch(anchor, rng.uniform(-3.0, -2.0)), 4800), fade_out=0.08), at=0.40),
        Layer('Anchors', clip(dark(pitch(anchor, rng.uniform(-3.5, -2.5)), 4800), fade_out=0.08), at=0.73),
        Layer('Air', clip(filt(dark(pitch(event(*AIR, index=2 + v, length=0.23), -2.0), 3500), 'highpass', 300)), at=0.84),
    ]


def construct_loop(v: int) -> list[Layer]:
    rng = np.random.default_rng(7200 + v)
    # These windows avoid the welder's break at 14-15 s and keep all beds alive at the seam.
    welding = pitch(section(WELD, (1.3, 7.4)[v], 4.5), -2.0)[:seconds(4.5)]
    hydraulic = pitch(section(HYDRAULIC, (0.82, 6.9)[v], 4.0), -3.0)[:seconds(4.5)]
    drilling = pitch(section(HAMMER_DRILL, (0.5, 2.0)[v], 4.5), -3.0)[:seconds(4.5)]
    layers = [
        Layer('Welding', clip(filt(dark(welding, 4300), 'highpass', 200), fade_in=0.0, fade_out=0.0)),
        Layer('Hydraulics', clip(filt(dark(hydraulic, 2400), 'highpass', 200), fade_in=0.0, fade_out=0.0)),
        Layer('Drilling', clip(filt(dark(drilling, 3200), 'highpass', 220), fade_in=0.0, fade_out=0.0)),
    ]
    for i, at in enumerate((0.24, 1.04, 1.94, 2.80, 3.82)):
        rivet = event(*NAIL, length=0.29)
        tool = event(*CHISEL, index=2 * i + v, length=0.22)
        layers.extend([
            Layer('Rivets', clip(filt(dark(pitch(rivet, rng.uniform(-3.8, -2.2)), 4800), 'highpass', 200), fade_out=0.07), at=at + rng.uniform(-0.035, 0.035)),
            Layer('Tools', clip(filt(dark(pitch(tool, -2.0), 4200), 'highpass', 200), fade_out=0.06), at=at + 0.31),
        ])
    return layers


def complete(v: int) -> list[Layer]:
    rng = np.random.default_rng(7300 + v)
    motor = event(*MOTOR, length=1.2)
    return [
        Layer('Relay', clip(dark(pitch(event(*RELAY, length=0.42), rng.uniform(-2.5, -1.5)), 4500), fade_out=0.06)),
        Layer('Locks', clip(dark(pitch(event(*LEVERS[v], length=0.7), -2.5), 4000), fade_out=0.12), at=0.08),
        Layer('Motor', clip(filt(dark(pitch(motor, -3.5 - v), 3500), 'highpass', 180)[:seconds(1.65)], fade_in=0.012, fade_out=0.35), at=0.18),
        Layer('Air', clip(filt(dark(pitch(event(*AIR, index=4 + v, length=0.24), -3.0), 3500), 'highpass', 350), fade_out=0.07), at=1.48),
        Layer('Relay', clip(dark(event(*LOCK, index=4, length=0.18), 3800)), at=1.78),
    ]


def cancel(v: int) -> list[Layer]:
    rng = np.random.default_rng(7400 + v)
    return [
        Layer('Scaffold', clip(filt(dark(pitch(section(SCAFFOLD, (0.22, 5.3)[v], 0.9), -3.0), 3500), 'highpass', 140)[:seconds(1.15)], fade_out=0.25)),
        Layer('Falling Tools', clip(filt(dark(pitch(event(*SAWBLADE, length=0.7), rng.uniform(-4.0, -2.0)), 4800), 'highpass', 180), fade_out=0.2), at=0.21),
        Layer('Chains', clip(filt(dark(pitch(event(*CHAIN, length=0.5), -2.5 - v), 4500), 'highpass', 200), fade_out=0.15), at=0.72),
        Layer('Tools', clip(dark(pitch(section(TOOLBOX, (2.8, 6.0)[v], 0.3), -2.0), 4000), fade_out=0.07), at=1.06),
    ]


def destroyed(v: int) -> list[Layer]:
    rng = np.random.default_rng(7500 + v)
    blast = event(*BLASTS[v], length=2.1)
    return [
        Layer('Blast', clip(filt(dark(pitch(blast, -3.0 - 0.5 * v), 5000), 'highpass', 300)[:seconds(2.6)], fade_out=0.5)),
        # Only the first impact carries sub energy; collapse, debris and sparks cannot sustain rumble.
        Layer('Blast Low', clip(filt(pitch(blast[:seconds(0.24)], -4.0), 'lowpass', 180), fade_out=0.18)),
        Layer('Collapse', clip(filt(dark(pitch(section(COLLAPSE, 0.6 * v, 1.65), -2.0), 3800), 'highpass', 190), fade_out=0.35), at=0.30),
        Layer('Metal Debris', clip(filt(dark(pitch(section(ROCKS, (0.29, 2.7, 4.1)[v], 1.4), rng.uniform(-4.0, -2.0)), 4200), 'highpass', 180), fade_out=0.35), at=0.69),
        Layer('Sparks', clip(filt(dark(section(WELD, 8.0 + v, 0.55), 5000), 'highpass', 700), fade_in=0.02, fade_out=0.16), at=1.55),
        Layer('Sparks', clip(filt(dark(section(WELD, 10.5 + v, 0.4), 4400), 'highpass', 700), fade_in=0.01, fade_out=0.18), at=2.40),
    ]


def deploy(v: int) -> list[Layer]:
    rng = np.random.default_rng(7600 + v)
    return [
        Layer('Blast Door', clip(filt(dark(pitch(event(*DOOR, index=(0, 3)[v], length=0.44), -2.5), 4000), 'highpass', 130), fade_out=0.09)),
        Layer('Hydraulics', clip(filt(dark(pitch(section(HYDRAULIC, 1.3 + 6.0 * v, 0.5), -3.5), 2600), 'highpass', 200), fade_in=0.03, fade_out=0.12), at=0.07),
        Layer('Boots', clip(filt(dark(pitch(event(*BOOTS[v], length=0.27), rng.uniform(-3.5, -2.5)), 4500), 'highpass', 120), fade_out=0.08), at=0.45),
        Layer('Boots', clip(filt(dark(pitch(event(*BOOTS[1 - v], length=0.27), -3.0), 4500), 'highpass', 120), fade_out=0.08), at=0.82),
    ]


def research(v: int) -> list[Layer]:
    rng = np.random.default_rng(7700 + v)
    return [
        Layer('Toolbox', clip(filt(dark(pitch(section(TOOLBOX, (1.7, 4.5)[v], 0.45), -2.0), 4500), 'highpass', 160), fade_out=0.1)),
        Layer('Wrench', clip(filt(dark(pitch(section(WRENCH, (0.56, 2.34)[v], 0.55), rng.uniform(-2.5, -1.5)), 5000), 'highpass', 220), fade_out=0.08), at=0.48),
        Layer('Ratchet', clip(filt(dark(pitch(section(RATCHET, (0.68, 3.17)[v], 0.32), -2.0), 4200), 'highpass', 220), fade_out=0.06), at=1.06),
        Layer('Tools', clip(filt(dark(pitch(event(*DRILL, length=0.16), -3.0), 3500), 'highpass', 200), fade_out=0.05), at=1.25),
    ]


def hq_alarm(v: int) -> list[Layer]:
    rng = np.random.default_rng(7800 + v)
    horn = event(*(SIREN if v == 0 else HORN), length=(1.48, 1.65)[v])
    # Kenwood 5-8 s is broadband squelch/static: exclude its feedback tones at 1.5-3.2 s.
    static = section(RADIO, rng.uniform(5.05, 5.35), 0.62)
    return [
        Layer('Relay', clip(dark(pitch(event(*LEVERS[v], length=0.2), -3.0), 4000))),
        Layer('Horn', clip(filt(dark(pitch(horn, (-4.0, -3.5)[v]), 4200), 'highpass', 180), fade_in=0.025, fade_out=0.3), at=0.08),
        Layer('Radio', clip(filt(dark(static, 3200), 'highpass', 750), fade_in=0.018, fade_out=0.08), at=0.22),
        Layer('Radio', clip(filt(dark(section(RADIO, 6.8 + 0.2 * v, 0.36), 3200), 'highpass', 750), fade_in=0.02, fade_out=0.09), at=2.10),
    ]


def hq_destroyed(v: int) -> list[Layer]:
    rng = np.random.default_rng(7900 + v)
    blast = event(*BLASTS[1], length=2.6)
    # Reverse a real motor's acceleration, not a synthetic falling-pitch UI power-down.
    power_down = event(*MOTOR, length=1.15)[::-1].copy()
    return [
        Layer('Blast', clip(filt(dark(pitch(blast, -5.5), 4500), 'highpass', 300)[:seconds(3.5)], fade_out=0.6)),
        Layer('Blast Low', clip(filt(pitch(event(*BLASTS[2], length=0.30), -6.0), 'lowpass', 180), fade_out=0.24)),
        Layer('Collapse', clip(filt(dark(pitch(section(COLLAPSE, 0.0, 3.6), -3.0), 3500), 'highpass', 190)[:seconds(4.45)], fade_out=0.5), at=0.24),
        Layer('Steel Strain', clip(filt(dark(pitch(event(*MOAN, length=2.25), -5.0), 2800), 'highpass', 190), fade_in=0.02, fade_out=0.5), at=0.68),
        Layer('Metal Debris', clip(filt(dark(pitch(section(ROCKS, 0.29, 2.3), -3.0), 3800), 'highpass', 190), fade_out=0.5), at=1.62),
        Layer('Sparks', clip(filt(dark(section(WELD, rng.uniform(7.0, 9.0), 0.65), 4500), 'highpass', 700), fade_in=0.01, fade_out=0.2), at=2.62),
        Layer('Power Down', clip(filt(dark(pitch(power_down, -4.0), 2800), 'highpass', 250), fade_in=0.09, fade_out=0.28), at=3.38),
    ]


UNIT = Unit(
    'Human', 'Structure',
    tracks=[('Prefab', 0.0), ('Landing Low', -6.0), ('Locks', -10.0), ('Anchors', -8.0),
            ('Air', -17.0), ('Welding', -11.0), ('Hydraulics', -17.0), ('Drilling', -16.0),
            ('Rivets', -7.0), ('Tools', -15.0), ('Relay', -8.0), ('Motor', -10.0),
            ('Scaffold', -3.0), ('Falling Tools', -7.0), ('Chains', -10.0),
            ('Blast', 0.0), ('Blast Low', -6.0), ('Collapse', -9.0), ('Metal Debris', -11.0),
            ('Sparks', -21.0), ('Blast Door', -2.0), ('Boots', -8.0), ('Toolbox', -5.0),
            ('Wrench', -8.0), ('Ratchet', -12.0), ('Horn', -1.0), ('Radio', -19.0),
            ('Steel Strain', -12.0), ('Power Down', -15.0)],
    events=[
        Event('Place', 2, 1.2, -22.0, place),
        Event('ConstructLoop', 2, 4.5, -27.0, construct_loop, loop=True, crossfade=0.5),
        Event('Complete', 2, 2.0, -20.0, complete),
        Event('Cancel', 2, 1.5, -22.0, cancel),
        Event('Destroyed', 3, 3.0, -18.0, destroyed),
        Event('Deploy', 2, 1.2, -23.0, deploy),
        Event('Research', 2, 1.5, -22.0, research),
        Event('HQAlarm', 2, 2.5, -20.0, hq_alarm),
        Event('HQDestroyed', 1, 5.0, -17.0, hq_destroyed),
    ],
)
