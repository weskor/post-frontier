"""Machine Structure: the Cluster (HQ), Fine-Tuning Farm, Edge Node and Alignment Lab, and the Machine's
notifications (Docs/Audio.md "Buildings, sectors and HQs"). "A product launch that wants you dead":
pearl-white floating segments, glass and ceramic, frictionless servos, clean wide tails, deliberate glitches.

Players command buildings and fronts, not units, so these sounds carry much of the feedback and must read
at a glance. Direction: dark and sci-fi. The faction's notification chime (a recorded musical UI phrase)
is the signature: played clean but darkened for notifications, fragmented for placement, reversed for a
cancel, repeated and stuttered into a polite siren when the Cluster is attacked, and sliding down when it
dies. Glass tails are real wine glass and glass tone recordings pitched to the chime's last note, so the
tail rings in key. Construction is a granular spray of recorded glass fragments over servo moves, a
3D printer's head and soft data clicks, all high-passed so several building sites never drone.
Low end only ever comes from short thumps that decay within a second.
"""

from collections.abc import Callable

import numpy as np

from .core import (
    SR,
    Audio,
    Event,
    Layer,
    Unit,
    clip,
    dark,
    event,
    fades,
    filt,
    load,
    pitch,
    seconds,
)

SOURCES = [
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%207of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 7of9/Sound Ex Machina - UI SOUNDS - MUSICAL/Notification_Bridge Operation 01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip",
        "George Karagioules - The Source Collection SFX Pack/Wine Glass Hit Higher Pitch.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle2of9.zip",
        "Mechanical Wave - Glass/GLASTonl_Dark Tone Thrill_07_MWSFX_GL.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Epic Stock Media - Elemental Mutation Whooshes and Impacts/GLASMvmt_Whoosh Glass Crystal Fragments Sharp Shards Dry 05_ESM_EMWI.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Epic Stock Media - Tower Defense Game/ROBTMvmt_Tower Deploy Hitech Robot Motor Dark Thump Servo Whine 04_ESM_TDG.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/SoundMorph - Robotic Lifeforms 2/Robotic Lifeforms 2 - Servos - Variable Nano Machine 12.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%204of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 4of8/MattLightbound - 3D Printer - Lulzbot mini/3DPrinter.Servo.Whine.Sequence2.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Glitchedtones - Granular Textures/Granular Texture UI Glitch 02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part3of14.zip",
        "Effectsworks - Tech Future/Micro UI Readout Sound Mechanical 10.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - NANOTECH/PM_NANOTECH_ONESHOT_56 Robotic, Robot, Cyber, Tech, Nano, Micro, Movement, Mechanical, Servo, On.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%205of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 5of9/MatiasMacSD - THE MACHINES 2_ROBOTIC SOUNDS/Robot_Power On_03.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "CB Sound Design - Cyberdeck - Hologram & Transmissions Sound Effects/CYBERDECK_Outro_2.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip",
        "Mechanical Wave - Hits Whoosh/Heavy Sci-Fi Hit_HW 09.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle8of14.zip",
        "Rogue Waves - Glitch Grains/UIGlitch_Impact_RogueWaves_GlitchGrains_14.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Airborne Sound - Elements Glass/Glass,Plate Glass,Thick,Break,Topple,Schoeps.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Airborne Sound - Elements Glass/Glass,Crush,Debris,Compress,Constant,Heavy,Violent,Uneasy,Neumann.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "Eneas Mentzel - Debris & Rubble/DESTRCrsh_Smashing a ceramics tile on concrete  exterior_Eneas Mentzel_Debris & Rubble_04.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Broken Glass/Bluezone_BC0274_glass_impact_break_002.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Building Collapse/Bluezone_BC0275_building_collapse_debris_impact_glass_short_009.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Shards/SHARDS [LD Mono] 02 - Impact, Glass, Shatter, Small.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Shards/SHARDS [LD Mono] 16 - Drop, Glass, Small Shards.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "DavidDumais - Explosion SFX Pack/DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Broken Robot/Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM/Glitch_Factory_01_Decimated_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%205of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 5of8/Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM/Glitch_Factory_01_Boom_Crackle_04.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "BluezoneCorp - High Voltage/Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav",
    ),
]


CHIME = (
    "Sound Ex Machina - UI SOUNDS - MUSICAL",
    "Notification_Bridge Operation 01.wav",
)
# The chime is two dry phrases: a falling two-note call (0-0.185 s) and a four-note answer ending on A6.
CHIME_END = 0.47
CALL_END = 0.185
LAST_NOTE = 0.365
WINE_GLASS = (
    "George Karagioules - The Source Collection SFX Pack",
    "Wine Glass Hit Higher Pitch.wav",
)  # 5 takes
WINE_HZ = 1425.0  # fundamental of every wine glass take
GLASS_TONE = ("Mechanical Wave - Glass", "GLASTonl_Dark Tone Thrill_07_MWSFX_GL.wav")
GLASS_TONE_HZ = 558.0
CHIME_LAST_HZ = 1760.0
GLASS_SPRAY = (
    "Epic Stock Media - Elemental Mutation Whooshes and Impacts",
    "GLASMvmt_Whoosh Glass Crystal Fragments Sharp Shards Dry 05_ESM_EMWI.wav",
)
NANO_SERVO = (
    "SoundMorph - Robotic Lifeforms 2",
    "Robotic Lifeforms 2 - Servos - Variable Nano Machine 12.wav",
)
SEAT = (
    "Epic Stock Media - Tower Defense Game",
    "ROBTMvmt_Tower Deploy Hitech Robot Motor Dark Thump Servo Whine 04_ESM_TDG.wav",
)
HEAVY_HIT = ("Mechanical Wave - Hits Whoosh", "Heavy Sci-Fi Hit_HW 09.wav")
GLASS_CRUSH = (
    "Airborne Sound - Elements Glass",
    "Glass,Crush,Debris,Compress,Constant,Heavy,Violent,Uneasy,Neumann.wav",
)
DECIMATED = (
    "Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM",
    "Glitch_Factory_01_Decimated_01.wav",
)
PRINTER = (
    "MattLightbound - 3D Printer - Lulzbot mini",
    "3DPrinter.Servo.Whine.Sequence2.wav",
)


def chime(start: float = 0.0, end: float = CHIME_END) -> Audio:
    return load(*CHIME)[seconds(start) : seconds(end)]


def in_key(source_hz: float, target_hz: float) -> float:
    """Semitones that move `source_hz` onto `target_hz`."""
    return float(12.0 * np.log2(target_hz / source_hz))


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


def sequence(parts: list[tuple[Audio, float]]) -> Audio:
    """Several short clips summed onto one track at their offsets (seconds)."""
    out = np.zeros(max(seconds(at) + len(x) for x, at in parts))
    for x, at in parts:
        out[seconds(at) : seconds(at) + len(x)] += x
    return out


def grains(
    source: tuple[str, str],
    start: float,
    end: float,
    length: float,
    rate: float,
    grain: float,
    semitones: tuple[float, float],
    rng: np.random.Generator,
) -> Audio:
    """Granular spray: Hann-windowed grains of a recording at random positions and pitches, `rate` per second."""
    x = load(*source)[seconds(start) : seconds(end)]
    n = seconds(grain)
    parts: list[tuple[Audio, float]] = []
    for at in np.sort(rng.uniform(-grain, length, int(rate * length))):
        g = x[(p := rng.integers(0, len(x) - 2 * n)) : p + 2 * n]
        g = pitch(g, rng.uniform(*semitones))[:n]
        parts.append((g * np.hanning(len(g)) * rng.uniform(0.4, 1.0), max(0.0, at)))
    return sequence(parts)[: seconds(length)]


def notify(v: int) -> list[Layer]:
    rng = np.random.default_rng(8100 + v)
    shift = (-2.0, -2.5)[v]
    stretch = 2.0 ** (-shift / 12.0)  # varispeed: pitching down lengthens
    full = chime()
    # The same chime again, later and darker: a glassy slap-back rather than a reverb wash.
    echo = sequence(
        [(pitch(full, shift), 0.0), (0.5 * dark(pitch(full, shift), 2500), 0.15)]
    )
    # The glass tail rings an octave below the chime's last note, so it sustains the phrase in key.
    tail_pitch = in_key(WINE_HZ, CHIME_LAST_HZ * 2.0 ** (shift / 12.0) / 2.0)
    tail_at = LAST_NOTE * stretch - 0.01
    tail = pitch(event(*WINE_GLASS, index=(1, 3)[v], length=0.9), tail_pitch)[
        : seconds(1.2 - tail_at)
    ]
    spray = load(*GLASS_SPRAY)[seconds(1.2) : seconds(2.0)]
    return [
        # High-passed: each chime note starts with a soft low click that would thump on small speakers.
        Layer(
            "Chime",
            clip(filt(dark(pitch(full, shift), 6500), "highpass", 300), fade_out=0.05),
        ),
        Layer(
            "Chime Low",
            clip(
                filt(dark(pitch(full, shift - 12.0), 3000), "highpass", 200),
                fade_out=0.3,
            ),
        ),
        Layer(
            "Echo", clip(filt(dark(echo, 3500), "highpass", 500), fade_out=0.2), at=0.14
        ),
        Layer(
            "Glass Tail",
            clip(filt(dark(tail, 7000), "highpass", 300), fade_out=0.45),
            at=tail_at,
        ),
        Layer(
            "Shimmer",
            clip(
                filt(
                    dark(pitch(spray, rng.uniform(-3.0, -2.0)), 8000), "highpass", 2500
                ),
                fade_in=0.25,
                fade_out=0.5,
            ),
            at=0.25,
        ),
    ]


def place(v: int) -> list[Layer]:
    rng = np.random.default_rng(8200 + v)
    call = chime(0.0, CALL_END + 0.02)
    shimmer = load(*GLASS_SPRAY)[seconds((1.3, 1.7)[v]) : seconds((1.3, 1.7)[v] + 0.45)]
    seat = event(*SEAT, length=0.6)
    servo = load(*NANO_SERVO)[seconds((0.39, 0.81)[v]) : seconds((0.39, 0.81)[v] + 0.3)]
    land = rng.uniform(0.22, 0.26)
    return [
        # The materialise swell rises into the seat.
        Layer(
            "Materialise",
            clip(
                filt(dark(pitch(shimmer, -2.0), 8000), "highpass", 1500),
                fade_in=0.2,
                fade_out=0.1,
            ),
        ),
        Layer(
            "Chime",
            clip(
                filt(dark(pitch(call, (-2.0, -3.0)[v]), 6000), "highpass", 300),
                fade_out=0.08,
            ),
            at=0.02,
        ),
        # High-passed: the tower motor's thump would otherwise boom; the seat only needs its knock.
        Layer(
            "Seat",
            clip(
                filt(dark(pitch(seat, rng.uniform(-3.0, -2.0)), 6000), "highpass", 120),
                fade_out=0.25,
            ),
            at=land - 0.1,
        ),
        Layer(
            "Servo",
            clip(filt(dark(pitch(servo, -2.0), 7000), "highpass", 600), fade_out=0.1),
            at=land,
        ),
    ]


def construct_loop(v: int) -> list[Layer]:
    rng = np.random.default_rng(8300 + v)
    region = 4.5
    printer = pitch(
        load(*PRINTER)[seconds((4.0, 20.0)[v]) : seconds((4.0, 20.0)[v] + region)], -2.0
    )
    texture = load(
        "Glitchedtones - Granular Textures", "Granular Texture UI Glitch 02.wav"
    )
    texture = texture[seconds((3.0, 16.5)[v]) : seconds((3.0, 16.5)[v] + region)]
    glass = grains(
        GLASS_SPRAY,
        0.8,
        2.3,
        region,
        rate=28.0,
        grain=0.07,
        semitones=(-5.0, 0.0),
        rng=rng,
    )
    # Servo moves: short bursts of a nano servo at irregular times, each at its own pitch.
    moves = [(0.05, 0.37), (0.39, 0.78), (0.81, 1.2)]
    move_times = np.arange(rng.uniform(0.0, 0.3), region - 0.3, 0.9)
    servo = sequence(
        [
            (
                fades(
                    pitch(
                        load(*NANO_SERVO)[seconds(a) : seconds(b)],
                        rng.uniform(-4.0, -1.0),
                    ),
                    0.02,
                    0.08,
                ),
                at,
            )
            for at in move_times + rng.uniform(0.0, 0.15, len(move_times))
            for a, b in [moves[rng.integers(0, 3)]]
        ]
    )
    # Soft data clicks: the readout's three bursts and the nano grains, scattered.
    readout = load(
        "Effectsworks - Tech Future", "Micro UI Readout Sound Mechanical 10.wav"
    )
    nano = load(
        "PMSFX - NANOTECH",
        "PM_NANOTECH_ONESHOT_56 Robotic, Robot, Cyber, Tech, Nano, Micro, Movement, Mechanical, Servo, On.wav",
    )
    bits = [
        readout[seconds(0.02) : seconds(0.16)],
        readout[seconds(0.24) : seconds(0.33)],
        nano[seconds(0.58) : seconds(0.7)],
        nano[seconds(0.7) : seconds(0.83)],
        nano[seconds(0.84) : seconds(0.98)],
    ]
    clicks = sequence(
        [
            (
                fades(
                    pitch(bits[rng.integers(0, len(bits))], rng.uniform(-3.0, -1.0)),
                    0.002,
                    0.03,
                ),
                at,
            )
            for at in np.cumsum(rng.uniform(0.28, 0.6, 14)) - 0.2
            if 0.0 <= at < region - 0.1
        ]
    )
    loop = dict(fade_in=0.0, fade_out=0.0)  # the region wraps: no fades at its ends

    # Granular and servo tracks may end between bursts; pad the full overlap region, never fade its boundary.
    def full_region(x: Audio) -> Audio:
        return np.pad(x[: seconds(region)], (0, max(0, seconds(region) - len(x))))

    return [
        # The print head: stepper whine high-passed well above its motor hum.
        Layer(
            "Printer",
            clip(full_region(filt(dark(printer, 7000), "highpass", 600)), **loop),
        ),
        Layer(
            "Texture",
            clip(
                full_region(filt(dark(pitch(texture, -2.0), 6000), "highpass", 400)),
                **loop,
            ),
        ),
        Layer(
            "Glass Grains",
            clip(full_region(filt(dark(glass, 9000), "highpass", 2000)), **loop),
        ),
        Layer(
            "Servo Moves",
            clip(full_region(filt(dark(servo, 7000), "highpass", 500)), **loop),
        ),
        Layer(
            "Data",
            clip(full_region(filt(dark(clicks, 7000), "highpass", 1200)), **loop),
        ),
    ]


def complete(v: int) -> list[Layer]:
    rng = np.random.default_rng(8400 + v)
    shift = (-1.0, -2.0)[v]
    # The last half second of a recorded power-on rise; it cuts off where the chime lands.
    cut = (3.9, 9.37)[v]
    rise = load("MatiasMacSD - THE MACHINES 2_ROBOTIC SOUNDS", "Robot_Power On_03.wav")[
        seconds(cut - 0.5) : seconds(cut)
    ]
    rise = pitch(rise, -2.0)
    land = len(rise) / SR
    full = chime()
    bloom = load(*GLASS_SPRAY)[seconds(1.4) : seconds(2.5)]
    tone = event(*GLASS_TONE, length=1.4)
    tone_pitch = in_key(GLASS_TONE_HZ, CHIME_LAST_HZ * 2.0 ** (shift / 12.0) / 4.0)
    tone_at = land - 0.2
    tone = pitch(tone, tone_pitch)[: seconds(2.0 - tone_at)]
    return [
        Layer(
            "Power Rise",
            clip(filt(dark(rise, 7000), "highpass", 200), fade_in=0.25, fade_out=0.01),
        ),
        Layer(
            "Chime",
            clip(filt(dark(pitch(full, shift), 7000), "highpass", 300), fade_out=0.05),
            at=land,
        ),
        Layer(
            "Chime Low",
            clip(
                filt(dark(pitch(full, shift - 12.0), 3000), "highpass", 200),
                fade_out=0.3,
            ),
            at=land,
        ),
        # The power-on blooms into a spray of glass as the chime plays.
        Layer(
            "Bloom",
            clip(
                filt(
                    dark(pitch(bloom, rng.uniform(-3.0, -2.0)), 8000), "highpass", 1500
                ),
                fade_in=0.03,
                fade_out=0.6,
            ),
            at=land,
        ),
        # Glass resonance two octaves below the chime's last note; it swells 0.3 s, so it starts under the rise.
        Layer(
            "Glass Resonance",
            clip(filt(dark(tone, 6500), "highpass", 200), fade_in=0.1, fade_out=0.8),
            at=tone_at,
        ),
    ]


def cancel(v: int) -> list[Layer]:
    rng = np.random.default_rng(8500 + v)
    backwards = pitch(chime()[::-1], (-3.0, -4.0)[v])
    end = len(backwards) / SR
    unravel = load(
        "CB Sound Design - Cyberdeck - Hologram & Transmissions Sound Effects",
        "CYBERDECK_Outro_2.wav",
    )
    unravel = pitch(unravel, rng.uniform(-3.0, -2.0))[: seconds(1.5 - (end - 0.12))]
    fragments = load(*GLASS_SPRAY)[seconds((1.9, 2.0)[v]) : seconds(2.6)]
    return [
        # The chime reversed: its notes swell backwards and stop on the attack of the first one.
        Layer(
            "Chime Reverse",
            clip(
                filt(dark(backwards, 6000), "highpass", 300),
                fade_in=0.05,
                fade_out=0.01,
            ),
        ),
        Layer(
            "Unravel",
            clip(
                filt(dark(unravel, 7000), "highpass", 300), fade_in=0.02, fade_out=0.4
            ),
            at=end - 0.12,
        ),
        Layer(
            "Glass Fragments",
            clip(
                filt(dark(pitch(fragments, -3.0), 8000), "highpass", 1500),
                fade_in=0.05,
                fade_out=0.3,
            ),
            at=end - 0.05,
        ),
    ]


SHELLS: list[Callable[[], Audio]] = [
    lambda: event(
        "Airborne Sound - Elements Glass",
        "Glass,Plate Glass,Thick,Break,Topple,Schoeps.wav",
        index=1,
        length=1.2,
    ),
    lambda: event(
        "Chris Skyes - Shards Broken Glass",
        "Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
        length=1.0,
    ),
    lambda: event(
        "Eneas Mentzel - Debris & Rubble",
        "DESTRCrsh_Smashing a ceramics tile on concrete  exterior_Eneas Mentzel_Debris & Rubble_04.wav",
        length=1.2,
    ),
]
CRACKS = [
    ("BluezoneCorp - Broken Glass", "Bluezone_BC0274_glass_impact_break_002.wav"),
    (
        "BluezoneCorp - Building Collapse",
        "Bluezone_BC0275_building_collapse_debris_impact_glass_short_009.wav",
    ),
    (
        "Double Trouble Audio - Shards",
        "SHARDS [LD Mono] 02 - Impact, Glass, Shatter, Small.wav",
    ),
]


def power_down(v: int, length: float) -> Audio:
    """Physical printer-head friction slows into a rough servo stop, never a clean oscillator sweep."""
    if v == 2:
        return event(
            "Sound Spark LLC - Broken Robot",
            "Broken_Robot_Servo_Short_Falling_Pitch_02.wav",
            length=length,
        )
    start = (8.0, 23.0)[v]
    head = load(*PRINTER)[seconds(start) : seconds(start + length)]
    return glide(head, -1.0, -7.0)[: seconds(length)]


def destroyed(v: int) -> list[Layer]:
    rng = np.random.default_rng(8600 + v)
    shell = SHELLS[v]()
    crack = event(*CRACKS[v], length=0.5)
    thump = (
        event(*HEAVY_HIT, length=0.5)
        if v != 1
        else event(
            "Rogue Waves - Glitch Grains",
            "UIGlitch_Impact_RogueWaves_GlitchGrains_14.wav",
            length=0.5,
        )
    )
    debris_at = (1.4, 3.3, 5.9)[v]
    debris = load(*GLASS_CRUSH)[seconds(debris_at) : seconds(debris_at + 2.2)]
    dropout = load(*DECIMATED)[
        seconds((0.4, 1.4, 2.2)[v]) : seconds((0.4, 1.4, 2.2)[v] + 0.45)
    ]
    spark = event(
        "BluezoneCorp - High Voltage",
        "Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav",
        skip=(1.1, 1.98, 2.25)[v],
        length=0.4,
    )
    return [
        Layer(
            "Shell Break",
            clip(
                filt(
                    dark(pitch(shell, rng.uniform(-2.5, -1.5)), 7000), "highpass", 250
                ),
                fade_out=0.4,
            ),
        ),
        Layer(
            "Glass Crack",
            clip(
                filt(
                    dark(pitch(crack, rng.uniform(-2.0, -1.0)), 8000), "highpass", 250
                ),
                fade_out=0.15,
            ),
        ),
        # Cut to half a second and low-passed: weight on the hit, decayed long before the debris settles.
        Layer("Thump", clip(filt(pitch(thump, -3.0), "lowpass", 400), fade_out=0.4)),
        Layer(
            "Power Down",
            clip(
                filt(dark(pitch(power_down(v, 1.3), -2.0), 5000), "highpass", 500),
                fade_in=0.02,
                fade_out=0.5,
            ),
            at=0.08,
        ),
        Layer(
            "Debris",
            clip(
                filt(
                    dark(pitch(debris, rng.uniform(-3.0, -2.0)), 6000), "highpass", 400
                ),
                fade_in=0.05,
                fade_out=1.0,
            ),
            at=0.25,
        ),
        Layer(
            "Dropout",
            clip(
                filt(dark(pitch(dropout, -2.0), 5000), "highpass", 300),
                fade_in=0.01,
                fade_out=0.2,
            ),
            at=rng.uniform(0.3, 0.45),
        ),
        Layer(
            "Spark",
            clip(
                filt(dark(pitch(spark, -2.0), 6000), "highpass", 400),
                fade_in=0.005,
                fade_out=0.15,
            ),
            at=0.02,
        ),
    ]


def deploy(v: int) -> list[Layer]:
    rng = np.random.default_rng(8700 + v)
    # Reverse a broadband glass-fragment pass: airy material movement, not a synthesized tonal sweep.
    lift_at = (0.8, 1.3)[v]
    whoosh = load(*GLASS_SPRAY)[seconds(lift_at) : seconds(lift_at + 0.65)][::-1]
    glass = load(*GLASS_SPRAY)[seconds((1.0, 1.5)[v]) : seconds((1.0, 1.5)[v] + 0.6)]
    servo_at = (0.39, 0.81)[v]
    printer = load(*NANO_SERVO)[seconds(servo_at) : seconds(servo_at + 0.3)]
    return [
        # Glass and servo layers stay above the bass range even when pitched down.
        Layer(
            "Whoosh",
            clip(
                filt(
                    dark(pitch(whoosh, rng.uniform(-2.5, -1.5)), 6000), "highpass", 1200
                ),
                fade_in=0.05,
                fade_out=0.35,
            ),
        ),
        Layer(
            "Glass Shimmer",
            clip(
                filt(dark(pitch(glass, -3.0), 8000), "highpass", 2000),
                fade_in=0.15,
                fade_out=0.3,
            ),
            at=0.05,
        ),
        Layer(
            "Print",
            clip(
                filt(
                    dark(pitch(printer[: seconds(0.35)], -2.0), 7000), "highpass", 1000
                ),
                fade_in=0.02,
                fade_out=0.15,
            ),
            at=0.02,
        ),
    ]


def hq_alarm(v: int) -> list[Layer]:
    rng = np.random.default_rng(8800 + v)
    call = chime(0.0, CALL_END + 0.02)
    period = 0.6
    # The chime's falling two-note call becomes a siren: repeated, alternating pitch, every other one stuttered.
    pitches = ((-2.0, -4.0, -2.0, -4.0), (-3.0, -1.0, -3.0, -1.0))[v]
    calls = [stutter(call, 0.035, 2) if i % 2 else call for i in range(4)]
    siren = sequence(
        [
            (pitch(c, p), i * period)
            for i, (c, p) in enumerate(zip(calls, pitches, strict=False))
        ]
    )
    low = sequence(
        [
            (pitch(c, p - 12.0), i * period)
            for i, (c, p) in enumerate(zip(calls, pitches, strict=False))
        ]
    )
    # A dark pulse under every call: the head of a heavy hit, low-passed and cut, so it throbs but never drones.
    hit = fades(pitch(event(*HEAVY_HIT, length=0.35), -4.0), 0.002, 0.25)
    pulse = sequence([(hit, i * period) for i in range(4)])
    texture = load(
        "Glitchedtones - Granular Textures", "Granular Texture UI Glitch 02.wav"
    )
    glitch = sequence(
        [
            (
                fades(texture[seconds(s) : seconds(s + 0.12)], 0.005, 0.05),
                i * period + 0.33,
            )
            for i, s in enumerate(rng.uniform(2.0, 20.0, 4))
        ]
    )
    return [
        Layer("Siren", clip(filt(dark(siren, 6000), "highpass", 300), fade_out=0.05)),
        Layer("Siren Low", clip(filt(dark(low, 3000), "highpass", 200), fade_out=0.2)),
        Layer("Pulse", clip(filt(pulse, "lowpass", 500), fade_out=0.25)),
        Layer(
            "Alarm Glitch",
            clip(
                filt(dark(pitch(glitch, -2.0), 6000), "highpass", 500)[: seconds(2.5)],
                fade_out=0.05,
            ),
        ),
    ]


def hq_destroyed(v: int) -> list[Layer]:
    rng = np.random.default_rng(8900 + v)
    land = 0.45
    full = chime()
    # The chime tries to play, hiccups on its first note and slides down an octave and more as the power dies.
    fail = glide(stutter(full, 0.05, 4), -2.0, -18.0)
    last = chime(LAST_NOTE, CHIME_END)
    crash = event(
        "DavidDumais - Explosion SFX Pack",
        "DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav",
        length=2.5,
    )
    shell = SHELLS[0]()
    shards = SHELLS[1]()
    thump = event(
        *HEAVY_HIT, length=0.65
    )  # -4 semitones still leaves the entire bass hit under 0.82 s
    boom = event(
        "Sound Spark LLC \u2013 GLITCH FACTORY 1- BOOM, CRACKLE AND SCREAM",
        "Glitch_Factory_01_Boom_Crackle_04.wav",
        length=1.2,
    )
    debris = load(*GLASS_CRUSH)[seconds(0.3) : seconds(3.8)]
    trickle = event(
        "Double Trouble Audio - Shards",
        "SHARDS [LD Mono] 16 - Drop, Glass, Small Shards.wav",
        index=3,
        length=2.0,
    )
    dropout = load(*DECIMATED)[seconds(1.0) : seconds(1.9)]
    return [
        Layer(
            "Chime",
            clip(
                filt(dark(pitch(chime(0.0, CALL_END), -2.0), 6500), "highpass", 300),
                fade_out=0.03,
            ),
        ),
        Layer(
            "Chime Fail",
            clip(filt(dark(fail, 5000), "highpass", 250), fade_out=0.3),
            at=0.2,
        ),
        # High-passed: the designed crash's own low end rumbles for seconds; the cut thump gives the weight.
        Layer(
            "Crash",
            clip(filt(dark(pitch(crash, -2.0), 6000), "highpass", 300), fade_out=1.2),
            at=land,
        ),
        Layer(
            "Shell Break",
            clip(filt(dark(pitch(shell, -3.0), 7000), "highpass", 200), fade_out=0.5),
            at=land,
        ),
        Layer(
            "Shards",
            clip(filt(dark(pitch(shards, -2.0), 7000), "highpass", 300), fade_out=0.4),
            at=land + 0.02,
        ),
        Layer(
            "Thump",
            clip(filt(pitch(thump, -4.0), "lowpass", 350), fade_out=0.7),
            at=land,
        ),
        Layer(
            "Energy Boom",
            clip(filt(dark(pitch(boom, -3.0), 6000), "highpass", 300), fade_out=0.5),
            at=land,
        ),
        Layer(
            "Power Down",
            clip(
                filt(dark(pitch(power_down(1, 2.2), -3.0), 5000), "highpass", 500),
                fade_in=0.02,
                fade_out=0.9,
            ),
            at=land + 0.05,
        ),
        Layer(
            "Groan",
            clip(
                filt(dark(pitch(power_down(0, 1.8), -2.0), 4000), "highpass", 500),
                fade_in=0.05,
                fade_out=0.8,
            ),
            at=land + 0.1,
        ),
        Layer(
            "Debris",
            clip(
                filt(dark(pitch(debris, -3.0), 6000), "highpass", 400),
                fade_in=0.1,
                fade_out=1.5,
            ),
            at=land + 0.3,
        ),
        Layer(
            "Trickle",
            clip(
                filt(dark(pitch(trickle, -2.0), 7000), "highpass", 500),
                fade_in=0.05,
                fade_out=0.8,
            ),
            at=2.3,
        ),
        Layer(
            "Dropout",
            clip(
                filt(dark(pitch(dropout, -2.0), 5000), "highpass", 300),
                fade_in=0.02,
                fade_out=0.3,
            ),
            at=land + rng.uniform(0.5, 0.6),
        ),
        # One last note, two octaves down, after the dust: the Cluster's final ping.
        Layer(
            "Last Ping",
            clip(
                filt(dark(pitch(last, -26.0), 3000), "highpass", 200),
                fade_in=0.01,
                fade_out=0.3,
            ),
            at=3.6,
        ),
    ]


UNIT = Unit(
    "Machine",
    "Structure",
    tracks=[
        ("Chime", -2.0),
        ("Chime Low", -12.0),
        ("Echo", -12.0),
        ("Glass Tail", -12.0),
        ("Shimmer", -18.0),
        ("Materialise", -10.0),
        ("Seat", -8.0),
        ("Servo", -10.0),
        ("Printer", -12.0),
        ("Texture", -10.0),
        ("Glass Grains", -10.0),
        ("Servo Moves", -8.0),
        ("Data", -10.0),
        ("Power Rise", -6.0),
        ("Bloom", -12.0),
        ("Glass Resonance", -10.0),
        ("Chime Reverse", -2.0),
        ("Unravel", -8.0),
        ("Glass Fragments", -12.0),
        ("Shell Break", -2.0),
        ("Glass Crack", -4.0),
        ("Thump", -5.0),
        ("Power Down", -7.0),
        ("Debris", -8.0),
        ("Dropout", -12.0),
        ("Spark", -12.0),
        ("Whoosh", -2.0),
        ("Glass Shimmer", -12.0),
        ("Print", -14.0),
        ("Siren", -2.0),
        ("Siren Low", -10.0),
        ("Pulse", -4.0),
        ("Alarm Glitch", -14.0),
        ("Chime Fail", -4.0),
        ("Crash", 0.0),
        ("Shards", -5.0),
        ("Energy Boom", -6.0),
        ("Groan", -8.0),
        ("Trickle", -12.0),
        ("Last Ping", -10.0),
    ],
    events=[
        Event("Notify", 2, 1.2, -22.0, notify),
        Event("Place", 2, 1.2, -22.0, place),
        Event("ConstructLoop", 2, 4.5, -27.0, construct_loop, loop=True),
        Event("Complete", 2, 2.0, -20.0, complete),
        Event("Cancel", 2, 1.5, -22.0, cancel),
        Event("Destroyed", 3, 3.0, -18.0, destroyed),
        Event("Deploy", 2, 1.2, -23.0, deploy),
        Event("HQAlarm", 2, 2.5, -20.0, hq_alarm),
        Event("HQDestroyed", 1, 5.0, -17.0, hq_destroyed),
    ],
)
