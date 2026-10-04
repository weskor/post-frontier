"""Human Lancer, "Torchbearer": a mining cutter on a shielded powered frame.

A short recorded beam transient rides welding sparks and a heavy servo; its tail clears the
1 s firing interval. ShieldBreak is the shared Human shield-collapse cue, not an impact.
Selection and acknowledgements use the existing shared UI sounds.
"""

from .core import Event, Layer, Unit, clip, dark, event, filt, pitch

SOURCES = [
    (
        "Sonniss.com-GDC2023-GameAudioBundle8of14.zip",
        "Shapeforms Audio - Sci-Fi Weapons Cyberpunk Arsenal/LASRMisc_Beam Transient_04_SFRMS_SCIWPNS.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - High Voltage Electricity - The Essential Collection/Arc Weld Electrical Sparks 3.wav",
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
        "Sonniss.com-GDC2023-GameAudioBundle2of14.zip",
        "BluezoneCorp - Demolisher - Robot/Bluezone_BC0290_demolisher_metal_impact_002.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Gamemaster Audio -  Human Vocalizations/voice_male_b_death_low_09.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip",
        "Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Window,Small,Crack,Medium Impact,Bright.wav",
    ),
]

BEAM = (
    "Shapeforms Audio - Sci-Fi Weapons Cyberpunk Arsenal",
    "LASRMisc_Beam Transient_04_SFRMS_SCIWPNS.wav",
)
ELECTRIC = "Hear and Now Sound - High Voltage Electricity - The Essential Collection"
SERVO = ("MatiasMacSD - THE MACHINES - ROBOTIC SOUNDS", "Robot_Servo_006.wav")
METAL = (
    "BluezoneCorp - Demolisher - Robot",
    "Bluezone_BC0290_demolisher_metal_impact_002.wav",
)
GLASS = (
    "Chris Skyes - Shards Broken Glass",
    "Window,Small,Crack,Medium Impact,Bright.wav",
)
VOICE = ("Gamemaster Audio -  Human Vocalizations", "voice_male_b_death_low_09.wav")


def lancer_fire(v: int) -> list[Layer]:
    beam = event(*BEAM, length=0.5)
    weld = event(
        ELECTRIC, "Arc Weld Electrical Sparks 3.wav", skip=0.12 + v * 0.18, length=0.48
    )
    servo = event(*SERVO, skip=0.12 + v * 0.08, length=0.15)
    return [
        Layer("Cutter beam", clip(dark(pitch(beam, -4.0 + v * 0.25), 6200)), 0.045),
        Layer("Welding crackle", clip(filt(weld, "bandpass", (350, 5500))), 0.045),
        Layer("Frame servo", clip(dark(pitch(servo, -5.0), 2400))),
    ]


def lancer_impact(v: int) -> list[Layer]:
    shock = event(
        ELECTRIC, "Damp Electric Shock 8.wav", skip=0.1 + v * 0.25, length=0.25
    )
    metal = event(*METAL, length=0.35)
    return [
        Layer("Contact arc", clip(dark(pitch(shock, -1.0 + v * 0.3), 5200))),
        Layer("Frame collapse", clip(dark(pitch(metal, -3.0 + v), 3200)), 0.015),
    ]


def lancer_death(v: int) -> list[Layer]:
    voice = event(*VOICE, length=0.85)
    servo = event(*SERVO, skip=0.1 + v * 0.12, length=0.45)
    metal = event(*METAL, length=0.55)
    return [
        Layer("Operator", clip(dark(pitch(voice, -1.0 + v * 0.4), 4000))),
        Layer("Frame servo", clip(dark(pitch(servo[::-1], -7.0), 2300)), 0.1),
        Layer("Frame collapse", clip(dark(pitch(metal, -7.0 + v), 3500)), 0.35),
    ]


def shield_break(v: int) -> list[Layer]:
    shock = event(
        ELECTRIC, "Damp Electric Shock 8.wav", skip=0.1 + v * 0.3, length=0.45
    )
    glass = event(*GLASS, length=0.35)
    return [
        Layer("Shield rupture", clip(dark(pitch(shock, -6.0 + v * 0.5), 4700))),
        Layer("Shield shards", clip(dark(pitch(glass, -4.0 + v * 0.4), 5800)), 0.025),
    ]


UNIT = Unit(
    "Human",
    "Lancer",
    tracks=[
        ("Cutter beam", 0.0),
        ("Welding crackle", -8.0),
        ("Frame servo", -13.0),
        ("Contact arc", -2.0),
        ("Frame collapse", -8.0),
        ("Operator", -7.0),
        ("Shield rupture", 0.0),
        ("Shield shards", -7.0),
    ],
    events=[
        Event("Fire", 4, 0.9, -20.0, lancer_fire),
        Event("Impact", 3, 0.8, -22.0, lancer_impact),
        Event("Death", 3, 1.6, -21.0, lancer_death),
        Event("ShieldBreak", 3, 1.0, -21.0, shield_break),
    ],
)
