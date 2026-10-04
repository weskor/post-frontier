"""Machine Scrambler, "Override": precise relay snaps interrupted by static and digital glitches.

Recorded electrical energy and data-disruption layers distinguish its compact EMP shot from
the broad auto-cast Pulse. Selection and acknowledgements remain shared UI cues.
"""

from .core import Event, Layer, Unit, clip, dark, event, filt, pitch

SOURCES = [
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Data Disruption/Medium Glitch 18.wav",
    ),
    (
        "Sonniss.com-GDC2026-GameAudioBundle2of5.zip",
        "Cinematic Sound Design - Interface & Infographics/Interface Accept Glassy Snap.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Electric Arcs and Energy/Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip",
        "Sound Spark LLC - Electric Arcs and Energy/Electric_Arc_Reverberant_Shock_Long_05.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%207of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 7of9/Sound Ex Machina - UI SOUNDS - MUSICAL/Notification_Bridge Operation 01.wav",
    ),
    (
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Demolisher - Robot/Bluezone_BC0290_demolisher_metal_impact_002.wav",
    ),
]

GLITCH = ("Glitchedtones - Data Disruption", "Medium Glitch 18.wav")
SNAP = ("Cinematic Sound Design - Interface & Infographics", "Interface Accept Glassy Snap.wav")
ENERGY = "Sound Spark LLC - Electric Arcs and Energy"
CHARGE = (ENERGY, "Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav")
ARC = (ENERGY, "Electric_Arc_Reverberant_Shock_Long_05.wav")
CHIME = ("Sound Ex Machina - UI SOUNDS - MUSICAL", "Notification_Bridge Operation 01.wav")
METAL = ("BluezoneCorp - Demolisher - Robot", "Bluezone_BC0290_demolisher_metal_impact_002.wav")


def scrambler_fire(v: int) -> list[Layer]:
    snap = event(*SNAP, length=0.15)
    glitch = event(*GLITCH, length=0.3)
    charge = event(*CHARGE, skip=0.05 + v * 0.08, length=0.25)
    return [
        Layer("Override relay", clip(dark(pitch(snap, 1.0 + v * 0.2), 6500))),
        Layer("Override static", clip(filt(pitch(glitch, -1.0 + v * 0.5), "bandpass", (500, 6500))), 0.015),
        Layer("EMP discharge", clip(dark(pitch(charge, -2.0 + v * 0.3), 4800)), 0.025),
    ]


def scrambler_impact(v: int) -> list[Layer]:
    arc = event(*ARC, length=0.25)
    glitch = event(*GLITCH, length=0.2)
    return [
        Layer("Contact arc", clip(filt(pitch(arc, 3.0 + v * 0.4), "bandpass", (800, 6200)))),
        Layer("Override static", clip(dark(pitch(glitch, 2.0 + v * 0.4), 6000)), 0.015),
    ]


def scrambler_death(v: int) -> list[Layer]:
    chime = event(*CHIME, length=0.45)
    glitch = event(*GLITCH, length=0.35)
    metal = event(*METAL, length=0.4)
    return [
        Layer("Power down", clip(dark(pitch(chime[::-1], -4.0 + v * 0.4), 4500))),
        Layer("Override static", clip(dark(pitch(glitch, -5.0 + v * 0.5), 4500)), 0.12),
        Layer("Shell drop", clip(dark(pitch(metal, -1.0 + v * 0.4), 3500)), 0.32),
    ]


def emp_pulse(v: int) -> list[Layer]:
    charge = event(*CHARGE, length=0.3)
    arc = event(*ARC, length=0.55)
    glitch = event(*GLITCH, length=0.35)
    snap = event(*SNAP, length=0.15)
    return [
        Layer("Pulse charge", clip(dark(pitch(charge[::-1], 4.0 + v * 0.3), 5800))),
        Layer("Pulse body", clip(dark(pitch(arc, -6.0 + v * 0.4), 3300)), 0.14),
        Layer("Override static", clip(filt(pitch(glitch, -3.0 + v * 0.5), "bandpass", (450, 6200))), 0.16),
        Layer("Override relay", clip(dark(pitch(snap, -3.0), 5800)), 0.14),
    ]


UNIT = Unit(
    "Machine",
    "Scrambler",
    tracks=[
        ("Override relay", -8.0), ("Override static", -5.0), ("EMP discharge", 0.0),
        ("Contact arc", -2.0), ("Power down", -5.0), ("Shell drop", -9.0),
        ("Pulse charge", -10.0), ("Pulse body", 0.0),
    ],
    events=[
        Event("Fire", 4, 0.8, -20.0, scrambler_fire),
        Event("Impact", 3, 0.8, -22.0, scrambler_impact),
        Event("Death", 3, 1.6, -21.0, scrambler_death),
        Event("Pulse", 3, 1.6, -19.0, emp_pulse),
    ],
)
