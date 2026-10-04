"""Machine Lancer, "Corrector": a clean corrective tone followed by a piercing beam.

Recorded glass/interface transients stay precise, without the Human cutter's gritty weld layer.
ShieldBreak supplies the shared Machine shield-collapse cue. Selection uses shared UI audio.
"""

from .core import Event, Layer, Unit, clip, dark, event, filt, pitch

SOURCES = [
    (
        "Sonniss.com-GDC2023-GameAudioBundle8of14.zip",
        "Shapeforms Audio - Sci-Fi Weapons Cyberpunk Arsenal/LASRMisc_Beam Transient_04_SFRMS_SCIWPNS.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Cinematic Sound Design - Interface & Infographics/Interface Accept Glassy Snap.wav",
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
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Window,Small,Crack,Medium Impact,Bright.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Electric Arcs and Energy/Electric_Arc_Reverberant_Shock_Long_05.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Demolisher - Robot/Bluezone_BC0290_demolisher_metal_impact_002.wav",
    ),
]

BEAM = (
    "Shapeforms Audio - Sci-Fi Weapons Cyberpunk Arsenal",
    "LASRMisc_Beam Transient_04_SFRMS_SCIWPNS.wav",
)
SNAP = ("Cinematic Sound Design - Interface & Infographics", "Interface Accept Glassy Snap.wav")
CHIME = ("Sound Ex Machina - UI SOUNDS - MUSICAL", "Notification_Bridge Operation 01.wav")
GLITCH = ("Glitchedtones - Data Disruption", "Medium Glitch 18.wav")
GLASS = ("Chris Skyes - Shards Broken Glass", "Window,Small,Crack,Medium Impact,Bright.wav")
ARC = ("Sound Spark LLC - Electric Arcs and Energy", "Electric_Arc_Reverberant_Shock_Long_05.wav")
METAL = ("BluezoneCorp - Demolisher - Robot", "Bluezone_BC0290_demolisher_metal_impact_002.wav")


def lancer_fire(v: int) -> list[Layer]:
    snap = event(*SNAP, length=0.2)
    beam = event(*BEAM, length=0.5)
    return [
        Layer("Correction tone", clip(dark(pitch(snap, -2.0 + v * 0.2), 6500))),
        Layer("Precision beam", clip(dark(pitch(beam, 1.0 + v * 0.2), 7200)), 0.025),
    ]


def lancer_impact(v: int) -> list[Layer]:
    arc = event(*ARC, length=0.35)
    glass = event(*GLASS, length=0.25)
    return [
        Layer("Contact arc", clip(filt(pitch(arc, 1.0 + v * 0.4), "bandpass", (450, 6500)))),
        Layer("Glass fracture", clip(dark(pitch(glass, -1.0 + v * 0.3), 5500)), 0.02),
    ]


def lancer_death(v: int) -> list[Layer]:
    chime = event(*CHIME, length=0.5)
    glitch = event(*GLITCH, length=0.3)
    metal = event(*METAL, length=0.5)
    return [
        Layer("Power down", clip(dark(pitch(chime[::-1], -6.0 + v * 0.5), 4000))),
        Layer("Fault", clip(dark(pitch(glitch, -2.0 + v), 4800)), 0.12),
        Layer("Shell collapse", clip(dark(pitch(metal, -4.0 + v), 3500)), 0.32),
    ]


def shield_break(v: int) -> list[Layer]:
    arc = event(*ARC, length=0.45)
    glass = event(*GLASS, length=0.4)
    snap = event(*SNAP, length=0.18)
    return [
        Layer("Shield rupture", clip(dark(pitch(arc, -4.0 + v * 0.4), 5500))),
        Layer("Glass fracture", clip(dark(pitch(glass, -5.0 + v * 0.4), 6200)), 0.02),
        Layer("Power down", clip(dark(pitch(snap[::-1], -7.0), 4000)), 0.12),
    ]


UNIT = Unit(
    "Machine",
    "Lancer",
    tracks=[
        ("Correction tone", -5.0), ("Precision beam", 0.0), ("Contact arc", -3.0),
        ("Glass fracture", -8.0), ("Power down", -5.0), ("Fault", -12.0),
        ("Shell collapse", -7.0), ("Shield rupture", 0.0),
    ],
    events=[
        Event("Fire", 4, 0.9, -20.0, lancer_fire),
        Event("Impact", 3, 0.8, -22.0, lancer_impact),
        Event("Death", 3, 1.6, -21.0, lancer_death),
        Event("ShieldBreak", 3, 1.0, -21.0, shield_break),
    ],
)
