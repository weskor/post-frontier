"""Human Scrambler, "Killswitch": an EMP pack with rough electrical crackle.

Fire is a compact discharge within the 1 s attack interval; the larger auto-cast Pulse has a
charge, bass release and crackling tail. Selection/acknowledgements use the shared UI set.
"""

from .core import Event, Layer, Unit, clip, dark, event, filt, pitch

SOURCES = [
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - High Voltage Electricity - The Essential Collection/Damp Electric Shock 8.wav",
    ),
    (
        "Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip",
        "Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - High Voltage Electricity - The Essential Collection/Arc Weld Electrical Sparks 3.wav",
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
]

ELECTRIC = "Hear and Now Sound - High Voltage Electricity - The Essential Collection"
ENERGY = "Sound Spark LLC - Electric Arcs and Energy"
ARC = (ENERGY, "Electric_Arc_Reverberant_Shock_Long_05.wav")
CHARGE = (ENERGY, "Electric_Energy_Bit_Erosion_Blast_Powerup_01.wav")
SERVO = ("MatiasMacSD - THE MACHINES - ROBOTIC SOUNDS", "Robot_Servo_006.wav")
METAL = ("BluezoneCorp - Demolisher - Robot", "Bluezone_BC0290_demolisher_metal_impact_002.wav")
VOICE = ("Gamemaster Audio -  Human Vocalizations", "voice_male_b_death_low_09.wav")


def scrambler_fire(v: int) -> list[Layer]:
    shock = event(ELECTRIC, "Damp Electric Shock 8.wav", skip=0.1 + v * 0.18, length=0.3)
    weld = event(ELECTRIC, "Arc Weld Electrical Sparks 3.wav", skip=0.1 + v * 0.2, length=0.25)
    servo = event(*SERVO, skip=0.1 + v * 0.1, length=0.12)
    return [
        Layer("EMP discharge", clip(dark(pitch(shock, -3.0 + v * 0.4), 5000))),
        Layer("Pack crackle", clip(filt(weld, "bandpass", (600, 6500))), 0.03),
        Layer("Pack relay", clip(dark(pitch(servo, 2.0), 3000))),
    ]


def scrambler_impact(v: int) -> list[Layer]:
    arc = event(*ARC, length=0.3)
    shock = event(ELECTRIC, "Damp Electric Shock 8.wav", skip=0.2 + v * 0.2, length=0.18)
    return [
        Layer("Contact arc", clip(filt(pitch(arc, 2.0 + v * 0.4), "bandpass", (700, 6000)))),
        Layer("Pack crackle", clip(dark(shock, 5000)), 0.02),
    ]


def scrambler_death(v: int) -> list[Layer]:
    voice = event(*VOICE, length=0.7)
    charge = event(*CHARGE, length=0.45)
    metal = event(*METAL, length=0.45)
    return [
        Layer("Operator", clip(dark(pitch(voice, 1.0 + v * 0.4), 4000))),
        Layer("EMP discharge", clip(dark(pitch(charge[::-1], -5.0), 4000)), 0.1),
        Layer("Pack drop", clip(dark(pitch(metal, -2.0 + v * 0.4), 3500)), 0.3),
    ]


def emp_pulse(v: int) -> list[Layer]:
    charge = event(*CHARGE, length=0.35)
    arc = event(*ARC, length=0.6)
    weld = event(ELECTRIC, "Arc Weld Electrical Sparks 3.wav", skip=0.15 + v * 0.3, length=0.55)
    return [
        Layer("Pulse charge", clip(dark(pitch(charge[::-1], 2.0 + v * 0.3), 4500))),
        Layer("Pulse body", clip(dark(pitch(arc, -8.0 + v * 0.4), 2600)), 0.18),
        Layer("Pack crackle", clip(filt(weld, "bandpass", (400, 6200))), 0.2),
    ]


UNIT = Unit(
    "Human",
    "Scrambler",
    tracks=[
        ("EMP discharge", 0.0), ("Pack crackle", -7.0), ("Pack relay", -13.0),
        ("Contact arc", -2.0), ("Operator", -6.0), ("Pack drop", -9.0),
        ("Pulse charge", -9.0), ("Pulse body", 0.0),
    ],
    events=[
        Event("Fire", 4, 0.8, -20.0, scrambler_fire),
        Event("Impact", 3, 0.8, -22.0, scrambler_impact),
        Event("Death", 3, 1.6, -21.0, scrambler_death),
        Event("Pulse", 3, 1.6, -19.0, emp_pulse),
    ],
)
