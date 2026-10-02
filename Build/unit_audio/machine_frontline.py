"""Machine Frontline, "SOL 6000": pearl-white melee shell with one red lens and huge shoulders
(Docs/Audio.md "Unit sound sheets"). The HAL-9000 nod: "I'm afraid I can't let you pass."

Attack interval 0.7 s, so a strike lands about 0.12 s in and has decayed well before the next one.
Direction: dark and sci-fi, but cleaner and glassier than the Offline grit. Designed energy risers and
a mecha energy blade carry the strike; a wine glass, a glassy UI snap and a pitched-down sword shing
make it crystalline; a short designed low hit gives weight that dies within 0.3 s. Deaths use the
faction's notification chime, reversed and pitched down, sucking the shell into its collapse.
"""

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
    filt,
    load,
    pitch,
    seconds,
)

SOURCES = [
    (
        "Sonniss.com-GDC2024-GameAudioBundle6of9.zip",
        "Rogue Waves - Anime Studio/WEAPSwrd_Mecha Energy Sword Cut_RogueWaves_AnimeStudio.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle2of9.zip",
        "Mechanical Wave - Glass/GLASMisc_Reverse Glass Effect_04_MWSFX_GL.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Gamemaster Audio - Magic and Spell Sounds/casting_charge_matter_grow_04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Electric Arcs and Energy/Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip",
        "George Karagioules - The Source Collection SFX Pack/Wine Glass Hit Higher Pitch.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Cinematic Sound Design - Interface & Infographics/Interface Accept Glassy Snap.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Impact/Impact - Tech Debris 12.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "Soundopolis - Blades/Knife_Sword_Shing_Fienup_001.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Window,Small,Crack,Medium Impact,Bright.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "Eneas Mentzel - Debris & Rubble/DESTRCrsh_Smashing a ceramics tile on concrete  exterior_Eneas Mentzel_Debris & Rubble_04.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip",
        "PMSFX - Bullet Bys &Impacts/PM_BBI_Bullet_Impact_Glass_1.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle2of9.zip",
        "Mechanical Wave - Glass/GLASBrk_Glass Break Hit_04_MWSFX_GL.wav",
    ),
    (
        "Sonniss.com-GDC2024-GameAudioBundle1of9.zip",
        "BluezoneCorp - High Voltage/Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Demolisher - Robot/Bluezone_BC0290_demolisher_metal_impact_002.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%207of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 7of9/Sound Ex Machina - UI SOUNDS - MUSICAL/Notification_Bridge Operation 01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Data Disruption/Medium Glitch 18.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%206of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 6of8/Tone Manufacture - Glitch UI/TM_GLITCH UI_Glitches dry_53.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "CB Sound Design - Defect \u2013 Hum, Noise And Glitches/DEFECT_Digital_Glitch_6.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Fox Audio Post-Production - Space Racer 2 \u2013 Turbines & Engines/PodRacer_Turbine_off_05.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 1of8/Airborne Sound - Eclectic Whooshes/Whoosh,Sound Design,Power Down,Groan,Choppy,Very High.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "David Dumais Audio - Sci-Fi Weapons Pack 1/DSGNBass_Weapon Power Down 04_DDUMAIS_NONE.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Broken Glass/Bluezone_BC0274_glass_impact_break_small_014.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/3maze - IMPACTUS/shatter_ice_001.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%204of5.zip",
        "Secret Source - 003 Jaws Of Life/JOL_0753_GlassImpact_07_Senn8020.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%201of8.zip",
        "Sonniss.com - GDC 2019 - Game Audio Bundle Part 1of8/Airborne Sound - Elements Glass/Glass,Plate Glass,Thick,Break,Topple,Schoeps.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%203of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 3of9/Double Trouble Audio - Shards/SHARDS [LD Mono] 16 - Drop, Glass, Small Shards.wav",
    ),
]


WINE_GLASS = (
    "George Karagioules - The Source Collection SFX Pack",
    "Wine Glass Hit Higher Pitch.wav",
)  # 5 takes
METAL_HIT = (
    "BluezoneCorp - Demolisher - Robot",
    "Bluezone_BC0290_demolisher_metal_impact_002.wav",
)
CHIME = (
    "Sound Ex Machina - UI SOUNDS - MUSICAL",
    "Notification_Bridge Operation 01.wav",
)


def charge(v: int) -> Audio:
    """The last ~0.13 s of a designed energy riser, cut where it peaks so the strike takes over."""
    if v == 0:
        x = load(
            "Mechanical Wave - Glass", "GLASMisc_Reverse Glass Effect_04_MWSFX_GL.wav"
        )
        return x[seconds(0.77) : seconds(0.9)]
    if v == 1:
        x = load(
            "Gamemaster Audio - Magic and Spell Sounds",
            "casting_charge_matter_grow_04.wav",
        )
        return x[seconds(1.03) : seconds(1.17)]
    if v == 2:
        x = load(
            "Sound Spark LLC - Electric Arcs and Energy",
            "Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav",
        )
        return filt(x[seconds(1.26) : seconds(1.4)], "highpass", 300)
    # The last wine glass take reversed: a crystalline swell into the blade.
    return event(*WINE_GLASS, index=4, length=0.14)[::-1]


def frontline_attack(v: int) -> list[Layer]:
    rng = np.random.default_rng(4100 + v)
    detune = rng.uniform(-0.4, 0.4)
    strike = rng.uniform(0.11, 0.13)
    blade = event(
        "Rogue Waves - Anime Studio",
        "WEAPSwrd_Mecha Energy Sword Cut_RogueWaves_AnimeStudio.wav",
        skip=0.01 * v,
        length=0.32,
    )
    crystal = event(*WINE_GLASS, index=v, length=0.3)
    snap = event(
        "Cinematic Sound Design - Interface & Infographics",
        "Interface Accept Glassy Snap.wav",
        length=0.3,
    )
    pulse = event("Glitchedtones - Impact", "Impact - Tech Debris 12.wav", length=0.28)
    ring = event(
        "Soundopolis - Blades",
        "Knife_Sword_Shing_Fienup_001.wav",
        index=v % 2,
        length=0.45,
    )
    return [
        Layer(
            "Charge",
            clip(
                filt(dark(pitch(charge(v), -2.0 + detune), 6000), "highpass", 200),
                fade_in=0.09,
                fade_out=0.01,
            ),
        ),
        # High-passed: the blade recording hums below 200 Hz for most of a second after the cut.
        Layer(
            "Blade",
            clip(
                filt(dark(pitch(blade, detune - 1.5), 7000), "highpass", 150),
                fade_out=0.12,
            ),
            at=strike,
        ),
        Layer(
            "Snap", clip(dark(pitch(snap, detune - 1.5), 8000), fade_out=0.1), at=strike
        ),
        Layer(
            "Crystal",
            clip(
                filt(dark(pitch(crystal, detune - 3.0), 7000), "highpass", 300),
                fade_out=0.15,
            ),
            at=strike + 0.003,
        ),
        Layer(
            "Pulse",
            clip(filt(pitch(pulse, -2.0 + detune), "lowpass", 400), fade_out=0.15),
            at=strike,
        ),
        Layer(
            "Ring",
            clip(
                filt(dark(pitch(ring, detune - 3.0), 6000), "highpass", 300),
                fade_out=0.25,
            ),
            at=strike + 0.01,
        ),
    ]


GLASS_HITS = [
    (
        "Chris Skyes - Shards Broken Glass",
        "Window,Small,Crack,Medium Impact,Bright.wav",
    ),
    (
        "Eneas Mentzel - Debris & Rubble",
        "DESTRCrsh_Smashing a ceramics tile on concrete  exterior_Eneas Mentzel_Debris & Rubble_04.wav",
    ),
    ("PMSFX - Bullet Bys &Impacts", "PM_BBI_Bullet_Impact_Glass_1.wav"),
]


def frontline_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(4200 + v)
    hit = event(*GLASS_HITS[v], length=0.45)
    if v == 0:
        ring = event(*WINE_GLASS, index=4, length=0.4)
    else:
        ring = event(
            "Mechanical Wave - Glass",
            "GLASBrk_Glass Break Hit_04_MWSFX_GL.wav",
            index=v - 1,
            length=0.4,
        )
    crackle = event(
        "BluezoneCorp - High Voltage",
        "Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav",
        skip=0.05 + 0.4 * v,
        length=0.3,
    )
    thump = event(*METAL_HIT, length=0.28)
    return [
        Layer(
            "Glass Hit",
            clip(dark(pitch(hit, rng.uniform(-2.5, -1.5)), 4500), fade_out=0.15),
        ),
        Layer(
            "Crystal Ring",
            clip(
                filt(dark(pitch(ring, (-4, -3, -5)[v]), 7000), "highpass", 300),
                fade_out=0.2,
            ),
            at=0.003,
        ),
        # High-passed: the arc's mains hum would otherwise sit under every hit.
        Layer(
            "Crackle",
            clip(
                filt(dark(pitch(crackle, -2.0), 7000), "highpass", 400),
                fade_in=0.005,
                fade_out=0.12,
            ),
            at=0.005,
        ),
        Layer(
            "Thump",
            clip(dark(pitch(thump, rng.uniform(-3.0, -2.0)), 3000), fade_out=0.12),
        ),
    ]


def glitch(v: int) -> Audio:
    """A recorded digital stutter: the shell's firmware skipping as it fails."""
    if v == 0:
        return load("Glitchedtones - Data Disruption", "Medium Glitch 18.wav")[
            seconds(0.96) : seconds(1.36)
        ]
    if v == 1:
        return load("Tone Manufacture - Glitch UI", "TM_GLITCH UI_Glitches dry_53.wav")
    return load(
        "CB Sound Design - Defect \u2013 Hum, Noise And Glitches",
        "DEFECT_Digital_Glitch_6.wav",
    )[: seconds(0.35)]


def power_down(v: int) -> Audio:
    """A noisy, falling power-down with no steady tone or clean sweep; high-passed because all three
    recordings put most of their energy below 200 Hz, which would rumble under the collapse."""
    if v == 0:
        x = load(
            "Airborne Sound - Eclectic Whooshes",
            "Whoosh,Sound Design,Power Down,Groan,Choppy,Very High.wav",
        )
        x = x[: seconds(1.2)]
    elif v == 1:
        # The turbine's last spin-down, after its steady running section.
        x = load(
            "Fox Audio Post-Production - Space Racer 2 \u2013 Turbines & Engines",
            "PodRacer_Turbine_off_05.wav",
        )
        x = x[seconds(4.9) : seconds(6.2)]
    else:
        x = load(
            "David Dumais Audio - Sci-Fi Weapons Pack 1",
            "DSGNBass_Weapon Power Down 04_DDUMAIS_NONE.wav",
        )
        x = x[seconds(0.05) : seconds(0.9)]
    return filt(x, "highpass", 250)


SHELL_CRACKS = [
    ("BluezoneCorp - Broken Glass", "Bluezone_BC0274_glass_impact_break_small_014.wav"),
    ("3maze - IMPACTUS", "shatter_ice_001.wav"),
    ("Secret Source - 003 Jaws Of Life", "JOL_0753_GlassImpact_07_Senn8020.wav"),
]


def shell_break(v: int) -> Audio:
    """Shell segments giving way: a thick glass topple, a smash of large shards, a ceramic tile bouncing apart."""
    if v == 0:
        return event(
            "Airborne Sound - Elements Glass",
            "Glass,Plate Glass,Thick,Break,Topple,Schoeps.wav",
            index=1,
            length=1.0,
        )
    if v == 1:
        return event(
            "Chris Skyes - Shards Broken Glass",
            "Glass,Shards,Smash,Medium Impact,Lots of Large Shards.wav",
            length=0.9,
        )
    return event(*GLASS_HITS[1], index=0, length=1.2)


def frontline_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(4300 + v)
    # The faction chime, reversed and pitched down: it swells backwards into the collapse.
    chime = pitch(load(*CHIME)[: seconds(0.46)][::-1], (-5.0, -7.0, -4.0)[v])
    land = 0.05 + len(chime) / SR - 0.02
    crack = event(*SHELL_CRACKS[v], length=0.4)
    scatter = event(
        "Double Trouble Audio - Shards",
        "SHARDS [LD Mono] 16 - Drop, Glass, Small Shards.wav",
        index=(1, 3, 5)[v],
        length=0.8,
    )
    thud = event(*METAL_HIT, length=0.4)
    return [
        # High-passed: the shell only cracks here; the thud on landing carries the weight.
        Layer(
            "Shell Crack",
            clip(
                filt(
                    dark(pitch(crack, rng.uniform(-3.0, -2.0)), 6000), "highpass", 300
                ),
                fade_out=0.15,
            ),
        ),
        Layer(
            "Glitch", clip(filt(dark(glitch(v), 7000), "highpass", 250), fade_out=0.03)
        ),
        Layer(
            "Power Down",
            clip(
                dark(pitch(power_down(v), (-2, -1, -2)[v]), 5000),
                fade_in=0.02,
                fade_out=0.3,
            ),
            at=0.03,
        ),
        Layer("Chime", clip(dark(chime, 6000), fade_in=0.12, fade_out=0.01), at=0.05),
        Layer(
            "Shell Break",
            clip(
                dark(pitch(shell_break(v), rng.uniform(-2.5, -1.5)), 6000), fade_out=0.3
            ),
            at=land,
        ),
        Layer(
            "Scatter",
            clip(filt(dark(pitch(scatter, -3.0), 5000), "highpass", 250), fade_out=0.3),
            at=land + rng.uniform(0.06, 0.1),
        ),
        Layer(
            "Shell Thud",
            clip(dark(pitch(thud, (-5, -6, -4)[v]), 2500), fade_out=0.2),
            at=land,
        ),
    ]


UNIT = Unit(
    "Machine",
    "Frontline",
    tracks=[
        ("Charge", -7.0),
        ("Blade", 0.0),
        ("Snap", -5.0),
        ("Crystal", -9.0),
        ("Pulse", -7.0),
        ("Ring", -12.0),
        ("Glass Hit", 0.0),
        ("Crystal Ring", -9.0),
        ("Crackle", -12.0),
        ("Thump", -5.0),
        ("Shell Crack", -6.0),
        ("Glitch", -11.0),
        ("Power Down", -8.0),
        ("Chime", -4.0),
        ("Shell Break", -2.0),
        ("Scatter", -11.0),
        ("Shell Thud", -3.0),
    ],
    events=[
        Event("Attack", 4, 0.7, -20.0, frontline_attack),
        Event("Impact", 3, 0.8, -22.0, frontline_impact),
        Event("Death", 3, 1.8, -21.0, frontline_death),
    ],
)
