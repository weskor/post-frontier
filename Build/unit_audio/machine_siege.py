"""Machine Siege, "Hallucinator": low wide hovering artillery chassis with one oversized emitter,
confidently wrong 20% of the time (Docs/Audio.md "Unit sound sheets").

Attack interval 2.6 s, so the heaviest Machine shot gets a charged swell, a launch and a tail that has
decayed before the next shot. Direction: dark and sci-fi, but cleaner and glassier than the Offline grit.
Designed sci-fi cannons are pitched down and rolled off; a dark glass tone blooms under every shot and
is doubled a fraction of a semitone off, so the tail beats against itself ("wrong"); noisy granular glitch
grains are the deliberate digital artefact. Deaths play the faction's notification chime cheerfully, then let it
slide down ("task failed") before the chassis crashes and powers down.
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
    pitch,
    seconds,
)

SOURCES = [
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "SoundMorph - Robotic Lifeforms/servo_ice_lense_04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Window,Small,Break,Medium Impact,Large Amount of Clinking Pieces.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%205of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 5of9/MatiasMacSD - THE WEAPONS - SCI-FI WEAPONS/Weapon Bomb Explosion Sci-Fi Plasma-04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%206of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 6of9/Pole Position - Car Debris, Impacts & Crashes/peugeot_106_dropped_10m_on_metal_plates_zaxcom_191_1_S.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%207of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 7of9/Sound Ex Machina - UI SOUNDS - MUSICAL/Notification_Bridge Operation 01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%202of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 2of8/Bluezone Corporation - Metal Debris/Bluezone_BC0236_metal_debris_097.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM/Glitch_Factory_01_Boom_Crackle_04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM/Glitch_Factory_01_Decimated_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part1of14.zip",
        "Articulated Sounds - Magic Elements vol.2/MAGIC GENERIC Texture, Shimmer, Processed Glass, Reversed Layer, Disturbing, High.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part2of14.zip",
        "Async Audio - Sci-Fi Blaster/Big Blast 4.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part2of14.zip",
        "Bluezone - Tank - Explosion Sound Effects/Bluezone_BC0271_tank_artillery_cannon_shot_012.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Broken Robot/Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part9of14.zip",
        "SmartSoundFX - Futuristic/CANNON Plasma Shot Tonal High 10.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Broken Glass/Bluezone_BC0274_glass_impact_break_falling_debris_004.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Building Collapse/Bluezone_BC0275_building_collapse_debris_impact_glass_short_009.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Detonation - Explosion/Bluezone_BC0277_explosion_mortar_002_01.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "BluezoneCorp - High Voltage/Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "David Dumais Audio - Sci-Fi Weapons Pack 1/SCIWeap_Heavyweapon12 Shot 04_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - Mechanical Morphs And Mutations/PM_MMM_Granular_Mechanical_Sequence_Glitchy_Futuristic_24.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle8of14.zip",
        "Rogue Waves - Glitch Grains/UIGlitch_Impact_RogueWaves_GlitchGrains_14.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "BluezoneCorp - Alien Tripod/Bluezone_BC0292_alien_tripod_debris_glass_falling_003.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "BluezoneCorp - Sci Fi Weapon/Bluezone_BC0295_sci_fi_weapon_cannon_shot_002.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "DavidDumais - Explosion SFX Pack/DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "DavidDumais - Explosion SFX Pack/EXPLReal_Medium Realistic Explosion 15_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle2of9.zip",
        "Mechanical Wave - Glass/GLASMisc_Reverse Glass Effect_04_MWSFX_GL.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle2of9.zip",
        "Mechanical Wave - Glass/GLASTonl_Dark Tone Thrill_07_MWSFX_GL.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle1of5.zip",
        "344 Audio - Bass Drops & Downers Vol. 2/DSGNBass_Bass Drop & Downer Fast 12_344 Audio_Bass Drops & Downers Vol 2.wav",
    ),
]


GLASS = "Mechanical Wave - Glass"
CHIME = (
    "Sound Ex Machina - UI SOUNDS - MUSICAL",
    "Notification_Bridge Operation 01.wav",
)
GLASS_TONE = (GLASS, "GLASTonl_Dark Tone Thrill_07_MWSFX_GL.wav")
LAUNCHES = [
    (
        "BluezoneCorp - Sci Fi Weapon",
        "Bluezone_BC0295_sci_fi_weapon_cannon_shot_002.wav",
    ),
    ("SmartSoundFX - Futuristic", "CANNON Plasma Shot Tonal High 10.wav"),
    (
        "David Dumais Audio - Sci-Fi Weapons Pack 1",
        "SCIWeap_Heavyweapon12 Shot 04_DDUMAIS_NONE.wav",
    ),
    ("Async Audio - Sci-Fi Blaster", "Big Blast 4.wav"),
]


def glide(x: Audio, start: float, end: float) -> Audio:
    """Varispeed whose pitch slides linearly (over the source) from `start` to `end` semitones: a slowing tape."""
    rate = 2.0 ** (np.linspace(start, end, len(x)) / 12.0)
    time = np.concatenate(
        [[0.0], np.cumsum(1.0 / rate[:-1])]
    )  # output sample at which each source sample plays
    return np.interp(
        np.interp(np.arange(int(time[-1])), time, np.arange(len(x))),
        np.arange(len(x)),
        x,
    )


def stutter(x: Audio, grain: float, repeats: int) -> Audio:
    """The first `grain` seconds repeated, then the rest: a digital hiccup."""
    n = seconds(grain)
    head = x[:n] * np.linspace(
        1.0, 0.6, n
    )  # soft grain end, so the repeat clicks but does not pop
    return np.concatenate([head] * repeats + [x])


def siege_fire(v: int) -> list[Layer]:
    rng = np.random.default_rng(6100 + v)
    detune = rng.uniform(-0.4, 0.4)
    launch_at = 0.16
    # The reversed glass swell rises to its peak 0.62 s into the file: cut so the peak lands on the launch.
    swell = load(GLASS, "GLASMisc_Reverse Glass Effect_04_MWSFX_GL.wav")[
        seconds(0.44) : seconds(0.63)
    ]
    launch = event(*LAUNCHES[v], length=0.85)
    weight = event(
        "Bluezone - Tank - Explosion Sound Effects",
        "Bluezone_BC0271_tank_artillery_cannon_shot_012.wav",
        length=0.5,
    )
    # The glass tone swells for 0.3 s, then sustains: it starts early so it blooms with the launch, and is cut
    # short with a long fade so it decays instead of droning through the interval.
    tone = event(*GLASS_TONE, length=1.3)
    tone_pitch = (-1.0, -2.0, -0.5, -1.5)[v]
    shimmer = event(
        "Articulated Sounds - Magic Elements vol.2",
        "MAGIC GENERIC Texture, Shimmer, Processed Glass, Reversed Layer, Disturbing, High.wav",
        skip=(2.0, 4.5, 7.0, 9.5)[v],
        length=1.5,
    )
    # Noisy designed granular grains, not a tonal stutter: the artefact must not read as a blip.
    grains = event(
        "PMSFX - Mechanical Morphs And Mutations",
        "PM_MMM_Granular_Mechanical_Sequence_Glitchy_Futuristic_24.wav",
        index=(0, 2, 5, 7)[v],
        length=0.45,
    )
    servo = event(
        "SoundMorph - Robotic Lifeforms", "servo_ice_lense_04.wav", length=0.25
    )
    wrong = (0.35, -0.3, 0.45, -0.4)[
        v
    ]  # the double sits a fraction of a semitone off: slow beating
    return [
        Layer(
            "Charge",
            clip(
                filt(dark(pitch(swell, -2.0), 6000), "highpass", 300),
                fade_in=0.12,
                fade_out=0.02,
            ),
        ),
        Layer(
            "Launch",
            clip(
                filt(dark(pitch(launch, detune - 1.5), 7000), "highpass", 40),
                fade_out=0.45,
            ),
            at=launch_at,
        ),
        Layer(
            "Weight",
            clip(filt(pitch(weight, detune - 2.0), "lowpass", 1500), fade_out=0.3),
            at=launch_at,
        ),
        Layer(
            "Glass Bloom",
            clip(
                filt(pitch(tone, tone_pitch), "highpass", 200),
                fade_in=0.05,
                fade_out=1.0,
            ),
            at=launch_at - 0.2,
        ),
        Layer(
            "Detune",
            clip(
                filt(pitch(tone, tone_pitch + wrong), "highpass", 300),
                fade_in=0.2,
                fade_out=1.0,
            ),
            at=launch_at + 0.05,
        ),
        Layer(
            "Shimmer",
            clip(
                filt(dark(pitch(shimmer, -2.0), 7000), "highpass", 1000),
                fade_in=0.2,
                fade_out=0.8,
            ),
            at=launch_at + 0.1,
        ),
        Layer(
            "Glitch",
            clip(filt(dark(pitch(grains, -3.0), 5000), "highpass", 250), fade_out=0.1),
            at=launch_at + rng.uniform(0.45, 0.6),
        ),
        Layer(
            "Servo",
            clip(dark(pitch(servo, -4.0), 6000), fade_out=0.08),
            at=launch_at + rng.uniform(1.0, 1.1),
        ),
    ]


BLASTS = [
    (
        "MatiasMacSD - THE WEAPONS - SCI-FI WEAPONS",
        "Weapon Bomb Explosion Sci-Fi Plasma-04.wav",
    ),
    (
        "BluezoneCorp - Detonation - Explosion",
        "Bluezone_BC0277_explosion_mortar_002_01.wav",
    ),
    (
        "DavidDumais - Explosion SFX Pack",
        "EXPLReal_Medium Realistic Explosion 15_DDUMAIS_NONE.wav",
    ),
]
GLASS_DEBRIS = [
    (
        "BluezoneCorp - Broken Glass",
        "Bluezone_BC0274_glass_impact_break_falling_debris_004.wav",
    ),
    (
        "BluezoneCorp - Alien Tripod",
        "Bluezone_BC0292_alien_tripod_debris_glass_falling_003.wav",
    ),
    (
        "Chris Skyes - Shards Broken Glass",
        "Window,Small,Break,Medium Impact,Large Amount of Clinking Pieces.wav",
    ),
]


def siege_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(6200 + v)
    blast = event(*BLASTS[v], length=1.0)
    bloom = event(
        "Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM",
        "Glitch_Factory_01_Boom_Crackle_04.wav",
        length=0.8,
    )
    sub = event(
        "344 Audio - Bass Drops & Downers Vol. 2",
        "DSGNBass_Bass Drop & Downer Fast 12_344 Audio_Bass Drops & Downers Vol 2.wav",
        length=0.8,
    )
    crack = event(
        "BluezoneCorp - Building Collapse",
        "Bluezone_BC0275_building_collapse_debris_impact_glass_short_009.wav",
        length=0.45,
    )
    debris = event(*GLASS_DEBRIS[v], length=1.0)
    return [
        # High-passed and cut to 1 s: the sub drop carries the low end and decays on its own.
        Layer(
            "Blast",
            clip(
                filt(dark(pitch(blast, rng.uniform(-2.0, -1.0)), 7000), "highpass", 90),
                fade_out=0.6,
            ),
        ),
        Layer(
            "Energy Bloom",
            clip(filt(dark(pitch(bloom, -2.0), 7000), "highpass", 150), fade_out=0.3),
            at=0.005,
        ),
        # The sub drop is cut short and faded: weight on the hit, nothing left under the next salvo.
        Layer(
            "Sub Drop",
            clip(filt(pitch(sub, (0.0, -1.0, 1.0)[v]), "lowpass", 300), fade_out=0.55),
        ),
        Layer(
            "Glass Crack",
            clip(dark(pitch(crack, (-2, -1, -3)[v]), 8000), fade_out=0.15),
            at=0.003,
        ),
        Layer(
            "Glass Debris",
            clip(
                filt(
                    dark(pitch(debris, rng.uniform(-3.0, -2.0)), 7000), "highpass", 500
                ),
                fade_out=0.35,
            ),
            at=0.03,
        ),
    ]


CRASHES = [
    (
        "Pole Position - Car Debris, Impacts & Crashes",
        "peugeot_106_dropped_10m_on_metal_plates_zaxcom_191_1_S.wav",
    ),
    (
        "DavidDumais - Explosion SFX Pack",
        "DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav",
    ),
    ("Bluezone Corporation - Metal Debris", "Bluezone_BC0236_metal_debris_097.wav"),
]


def siege_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(6300 + v)
    chime = load(*CHIME)[: seconds(0.5)]
    # Cheerful first, then the same chime fails: sliding down, stuttering, or running backwards and down.
    fail = (
        glide(chime, -3.0, -15.0),
        glide(stutter(chime, 0.09, 3), -2.0, -12.0),
        glide(chime[::-1], -5.0, -16.0),
    )[v]
    crash = event(*CRASHES[v], skip=(0.0, 0.46, 0.0)[v], length=1.1)
    shards = event(
        "Chris Skyes - Shards Broken Glass",
        "Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
        length=0.8,
    )
    thump = event(
        "Rogue Waves - Glitch Grains",
        "UIGlitch_Impact_RogueWaves_GlitchGrains_14.wav",
        length=0.7,
    )
    # A recorded servo winding down with a falling pitch: the emitter losing power.
    power = event(
        "Sound Spark LLC - Broken Robot",
        "Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
        length=0.75,
    )
    # One crackling burst of a recorded arc discharge; its mains hum sits below the high-pass.
    spark = event(
        "BluezoneCorp - High Voltage",
        "Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav",
        skip=(1.1, 1.98, 2.25)[v],
        length=0.45,
    )
    # A bit-crushed burst as the systems drop out: the deliberate digital artefact of the death.
    dropout = event(
        "Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM",
        "Glitch_Factory_01_Decimated_01.wav",
        skip=(0.3, 1.3, 2.3)[v],
        length=0.5,
    )
    land = rng.uniform(0.5, 0.58)
    return [
        Layer(
            "Chime", clip(dark(pitch(chime, (-1.0, -2.0, 0.0)[v]), 7000), fade_out=0.05)
        ),
        Layer("Chime Fail", clip(dark(fail, 6000), fade_out=0.15), at=0.3),
        # High-passed: the designed crash's own low end rumbles on; the short low thump gives the weight.
        Layer(
            "Crash",
            clip(
                filt(dark(pitch(crash, (-1.0, -2.0, -1.5)[v]), 6000), "highpass", 110),
                fade_out=0.5,
            ),
            at=land,
        ),
        Layer(
            "Low Thump",
            clip(filt(pitch(thump, -1.0), "lowpass", 400), fade_out=0.35),
            at=land,
        ),
        Layer(
            "Shell Shards",
            clip(
                filt(
                    dark(pitch(shards, rng.uniform(-3.0, -2.0)), 7000), "highpass", 400
                ),
                fade_out=0.3,
            ),
            at=land + 0.01,
        ),
        Layer(
            "Power Down",
            clip(
                filt(dark(pitch(power, -2.0), 5000), "highpass", 150),
                fade_in=0.02,
                fade_out=0.3,
            ),
            at=land + 0.12,
        ),
        Layer(
            "Spark",
            clip(
                filt(dark(pitch(spark, -2.0), 6000), "highpass", 400),
                fade_in=0.005,
                fade_out=0.15,
            ),
            at=land + 0.03,
        ),
        Layer(
            "Dropout",
            clip(
                filt(dark(pitch(dropout, -2.0), 5000), "highpass", 250),
                fade_in=0.01,
                fade_out=0.2,
            ),
            at=land + rng.uniform(0.35, 0.5),
        ),
    ]


UNIT = Unit(
    "Machine",
    "Siege",
    tracks=[
        ("Charge", -6.0),
        ("Launch", 0.0),
        ("Weight", -9.0),
        ("Glass Bloom", -4.0),
        ("Detune", -6.0),
        ("Shimmer", -10.0),
        ("Glitch", -9.0),
        ("Servo", -16.0),
        ("Blast", -2.0),
        ("Energy Bloom", 0.0),
        ("Sub Drop", -6.0),
        ("Glass Crack", -2.0),
        ("Glass Debris", -3.0),
        ("Chime", -4.0),
        ("Chime Fail", -6.0),
        ("Crash", 0.0),
        ("Low Thump", -5.0),
        ("Shell Shards", -4.0),
        ("Power Down", -6.0),
        ("Spark", -8.0),
        ("Dropout", -10.0),
    ],
    events=[
        Event("Fire", 4, 2.4, -19.0, siege_fire),
        Event("Impact", 3, 1.6, -22.0, siege_impact),
        Event("Death", 3, 2.4, -21.0, siege_death),
    ],
)
