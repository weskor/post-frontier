"""Machine Ranged, "Autocomplete Drone": small hovering body, oversized sensor head, one long emitter
(Docs/Audio.md "Unit sound sheets"). It finishes your sentences, then you.

Attack interval 1.15 s. Every shot starts with a real laptop keystroke (the autocomplete joke), then a
designed laser shot pitched down and rolled off, a short low pulse for weight and a glass resonance
that rings out cleanly before the next shot. Cleaner and glassier than the Offline grit, still dark:
library sci-fi weapons, glass and glitch recordings, no synthesis. Deaths stutter, power down and warp
the Machine's notification chime (stuttered or reversed, pitched down) before the drone hits the ground.
"""
import numpy as np

from .core import SR, Event, Layer, Unit, clip, dark, event, fades, filt, pitch, seconds

SOURCES = [
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Hear and Now Sound - Office Equipment and Supplies/Laptop Keyboard 5.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip',
     'Digital Rain Lab - Lethal Energies/CK_Blaster_Shot-226.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part2of14.zip',
     'Async Audio - Sci-Fi Blaster/Blast Laser 2.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%205of5.zip',
     'TheLibrarybyEmptySea - Robobiotics/TheLibrarybyMTC_Robo_LaserBlast_Medium_027.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%205of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 5of9/MatiasMacSD - THE WEAPONS - SCI-FI WEAPONS/Weapon Shot Blaster-06.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part9of14.zip',
     'SmartSoundFX - Futuristic/CANNON Plasma Shot Tonal High 10.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle6of9.zip',
     'Rescopic Sound - Sci-Fi Energy Weapons/SCIWeap_Shot Pulse YR 05_RSCPC_SFEW.wav'),
    ('Sonniss.com-GDC2023-GameAudioBundle8of14.zip',
     'Shapeforms Audio - Sci-Fi Weapons Cyberpunk Arsenal/LASRMisc_Beam Transient_04_SFRMS_SCIWPNS.wav'),
    ('Sonniss.com-GDC2026-GameAudioBundle2of5.zip',
     'Cinematic Sound Design - Interface & Infographics/Interface Accept Glassy Snap.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip',
     'George Karagioules - The Source Collection SFX Pack/Wine Glass Hit Higher Pitch.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle6of9.zip',
     'Rescopic Sound - Parallax/SCIMisc_Zap Short 14_RSCPC_PX.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part3of14.zip',
     'David Dumais Audio - Electricity Magic 1/Magic_Spells_Impact_Electricity25.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part1of14.zip',
     'Articulated Sounds - Magic Elements vol.2/CHEMICAL ACID Sizzle, Burn, Short 02.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Data Disruption/Quick Glitch 26.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Data Disruption/Medium Glitch 18.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part5of14.zip',
     'PMSFX - Glitchy Circuits/PM_GC_SPARKLY_SHORT_GLITCH_51.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'Mechanical Wave - Undoing-Computer/Hit Impact Metal Scrap Debris_UC 06.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'Mechanical Wave - Undoing-Computer/Drop Fall Metal Rattle Scrap Debris_UC 10.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%203of5.zip',
     'Mechanical Wave - Undoing-Computer/Bouncing Metal Rattle Scrap Debris_UC 06.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%202of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 2of9/Chris Skyes - Shards Broken Glass/Window,Small,Crack,Medium Impact,Bright.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip',
     'Sound Spark LLC - Broken Robot/Broken_Robot_Morphing_Short_Static_Pitch_Confused_Glitch_02.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part2of14.zip',
     'Async Audio - Trailer Tools/Async_Stutter Pass.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%207of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 7of9/Sound Ex Machina - UI SOUNDS - MUSICAL/Notification_Bridge Operation 01.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip',
     'Sound Spark LLC - Broken Robot/Broken_Robot_Servo_Short_Falling_Pitch_02.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part3of14.zip',
     'Eiravaein Works - 48Kilos/PaverTV,side,CRTTV,concretepaver,drop,impact,obliterate,glass,plastic,metal,shatter,crush,contained,dense.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%203of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 3of8/Glitchedtones - Impact/Impact - Tech Debris 12.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%204of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 4of8/Pole Position - Drone DJI Spreading Wings S900/Drone_S900_t4_ext_startup_steady_rpms_shutdown_stationary_MKH8060.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%201of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 1of8/Airborne Sound - Eclectic Whooshes/Whoosh,Sound Design,Power Down,Groan,Choppy,Very High.wav'),
]


KEYBOARD = ("Hear and Now Sound - Office Equipment and Supplies", "Laptop Keyboard 5.wav")
BLASTER = ("Digital Rain Lab - Lethal Energies", "CK_Blaster_Shot-226.wav")
PULSE = ("Rescopic Sound - Sci-Fi Energy Weapons", "SCIWeap_Shot Pulse YR 05_RSCPC_SFEW.wav")
BEAM = ("Shapeforms Audio - Sci-Fi Weapons Cyberpunk Arsenal", "LASRMisc_Beam Transient_04_SFRMS_SCIWPNS.wav")
SNAP = ("Cinematic Sound Design - Interface & Infographics", "Interface Accept Glassy Snap.wav")
WINE_GLASS = ("George Karagioules - The Source Collection SFX Pack", "Wine Glass Hit Higher Pitch.wav")
CHIME = ("Sound Ex Machina - UI SOUNDS - MUSICAL", "Notification_Bridge Operation 01.wav")
LASERS = [
    ("Async Audio - Sci-Fi Blaster", "Blast Laser 2.wav"),
    ("TheLibrarybyEmptySea - Robobiotics", "TheLibrarybyMTC_Robo_LaserBlast_Medium_027.wav"),
    ("MatiasMacSD - THE WEAPONS - SCI-FI WEAPONS", "Weapon Shot Blaster-06.wav"),
    ("SmartSoundFX - Futuristic", "CANNON Plasma Shot Tonal High 10.wav"),
]


def stutter(x: np.ndarray, grain: float, repeats: int) -> np.ndarray:
    """A digital stutter edit: the first `grain` seconds repeated, then the whole sound."""
    head = fades(x[:seconds(grain)], 0.002, 0.004)
    return np.concatenate([head] * repeats + [x])


def ranged_fire(v: int) -> list[Layer]:
    rng = np.random.default_rng(5100 + v)
    detune = rng.uniform(-0.4, 0.4)
    # The "autocomplete" joke: one real laptop keystroke leads every shot.
    key = event(*KEYBOARD, index=(1, 4, 8, 10)[v], length=0.08)
    blaster = event(*BLASTER, length=0.6)
    laser = event(*LASERS[v], length=0.5)
    weight = event(*PULSE, length=0.3)
    beam = event(*BEAM, length=0.3)
    snap = event(*SNAP, length=0.3)
    # Skip the strike: only the glass resonance rings on after the shot.
    ring = event(*WINE_GLASS, index=(1, 2, 3, 0)[v], skip=0.03, length=0.8)
    shot = 0.035  # the emitter fires just after the keystroke, so the click reads as its own event
    return [
        Layer("Key", clip(dark(filt(pitch(key, -2.0 + detune), "highpass", 350), 6000), fade_out=0.03)),
        Layer("Blaster", clip(filt(dark(pitch(blaster, detune - 1.5), 6000), "highpass", 120), fade_out=0.25),
              at=shot),
        Layer("Laser", clip(filt(dark(pitch(laser, -2.0), 6000), "highpass", 200), fade_out=0.2), at=shot),
        Layer("Weight", clip(filt(filt(pitch(weight, detune - 1.0), "lowpass", 500), "highpass", 45),
                             fade_out=0.18), at=shot),
        Layer("Beam", clip(dark(pitch(beam, -4.0 + detune), 5000), fade_out=0.1), at=shot + 0.005),
        Layer("Glass Snap", clip(dark(pitch(snap, -2.0), 8000), fade_out=0.1), at=shot + 0.01),
        Layer("Glass Tail", clip(filt(dark(pitch(ring, (-5.0, -5.5, -4.5, -6.0)[v]), 5000), "highpass", 500),
                                 fade_in=0.01, fade_out=0.45), at=shot + 0.025),
    ]


FIZZ = ("Articulated Sounds - Magic Elements vol.2", "CHEMICAL ACID Sizzle, Burn, Short 02.wav")
ZAP = ("Rescopic Sound - Parallax", "SCIMisc_Zap Short 14_RSCPC_PX.wav")
SHOCK = ("David Dumais Audio - Electricity Magic 1", "Magic_Spells_Impact_Electricity25.wav")
GLITCHES = [
    (("Glitchedtones - Data Disruption", "Quick Glitch 26.wav"), 0),
    (("Glitchedtones - Data Disruption", "Medium Glitch 18.wav"), 3),
    (("PMSFX - Glitchy Circuits", "PM_GC_SPARKLY_SHORT_GLITCH_51.wav"), 0),
]
UNDOING = "Mechanical Wave - Undoing-Computer"
DEBRIS = [
    ((UNDOING, "Hit Impact Metal Scrap Debris_UC 06.wav"), 0),
    ((UNDOING, "Hit Impact Metal Scrap Debris_UC 06.wav"), 1),
    ((UNDOING, "Drop Fall Metal Rattle Scrap Debris_UC 10.wav"), 0),
]


def ranged_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(5200 + v)
    zap = event(*ZAP, length=0.35)
    shock = event(*SHOCK, length=0.4)
    fizz = event(*FIZZ, skip=0.1 + 0.25 * v, length=0.45)
    glitch_source, glitch_index = GLITCHES[v]
    glitch = event(*glitch_source, index=glitch_index, length=0.3)
    # The targets are Offline steel: the burn throws off small metal debris.
    debris_source, debris_index = DEBRIS[v]
    debris = event(*debris_source, index=debris_index, length=0.5)
    return [
        Layer("Energy Hit", clip(filt(dark(pitch(zap, (-2.0, -3.0, -1.5)[v]), 5000), "highpass", 90),
                                 fade_out=0.2)),
        Layer("Shock", clip(filt(dark(pitch(shock, rng.uniform(-2.5, -1.5)), 5000), "highpass", 200),
                            fade_out=0.25)),
        Layer("Fizz", clip(filt(dark(pitch(fizz, -3.0), 4000), "highpass", 500), fade_in=0.004, fade_out=0.3),
              at=0.01),
        Layer("Glitch", clip(filt(dark(pitch(glitch, -3.0), 5000), "highpass", 300), fade_out=0.08),
              at=rng.uniform(0.03, 0.06)),
        Layer("Debris", clip(filt(dark(pitch(debris, rng.uniform(-2.5, -1.0)), 5000), "highpass", 250),
                             fade_out=0.2), at=0.015),
    ]


CRACK = ("Chris Skyes - Shards Broken Glass", "Window,Small,Crack,Medium Impact,Bright.wav")
DEATH_GLITCHES = [
    ("Sound Spark LLC - Broken Robot", "Broken_Robot_Morphing_Short_Static_Pitch_Confused_Glitch_02.wav"),
    ("Async Audio - Trailer Tools", "Async_Stutter Pass.wav"),
    ("Sound Spark LLC - Broken Robot", "Broken_Robot_Morphing_Short_Static_Pitch_Confused_Glitch_02.wav"),
]
DRONE = ("Pole Position - Drone DJI Spreading Wings S900",
         "Drone_S900_t4_ext_startup_steady_rpms_shutdown_stationary_MKH8060.wav")
# (source, skip after the first onset, length, semitones). The drone take's only onset is its startup;
# 13 s later its rotors are winding down, falling from ~230 Hz to ~160 Hz over the next second.
POWER_DOWNS = [
    (DRONE, 13.05, 1.1, 0.0),
    (("Airborne Sound - Eclectic Whooshes", "Whoosh,Sound Design,Power Down,Groan,Choppy,Very High.wav"),
     0.0, 1.2, -1.0),
    (DRONE, 13.2, 0.95, -2.0),
]
SERVO_FALL = ("Sound Spark LLC - Broken Robot", "Broken_Robot_Servo_Short_Falling_Pitch_02.wav")
CRT_DROP = ("Eiravaein Works - 48Kilos",
            "PaverTV,side,CRTTV,concretepaver,drop,impact,obliterate,glass,plastic,metal,shatter,crush,contained,"
            "dense.wav")
TECH_DEBRIS = ("Glitchedtones - Impact", "Impact - Tech Debris 12.wav")
RATTLES = [
    ((UNDOING, "Drop Fall Metal Rattle Scrap Debris_UC 10.wav"), 0),
    ((UNDOING, "Bouncing Metal Rattle Scrap Debris_UC 06.wav"), 1),
    ((UNDOING, "Bouncing Metal Rattle Scrap Debris_UC 06.wav"), 0),
]


def warped_chime(v: int) -> np.ndarray:
    """The Machine's notification chime, broken three ways: stuttered low, reversed lower, stuttered fast."""
    if v == 0:
        return fades(stutter(pitch(event(*CHIME, length=0.55), -5.0), 0.07, 3), 0.001, 0.2)
    if v == 1:
        return fades(pitch(event(*CHIME, length=0.35), -7.0)[::-1], 0.15, 0.01)
    return fades(stutter(pitch(event(*CHIME, length=0.55), -3.0), 0.045, 4), 0.001, 0.25)


def ranged_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(5300 + v)
    land = rng.uniform(0.58, 0.66)
    crack = event(*CRACK, length=0.3)
    glitch = event(*DEATH_GLITCHES[v], skip=(0.0, 0.1, 0.45)[v], length=0.4)
    power_source, power_skip, power_length, power_pitch = POWER_DOWNS[v]
    power = event(*power_source, skip=power_skip, length=power_length)
    fall = event(*SERVO_FALL, length=0.7)
    crt = event(*CRT_DROP, length=0.8)
    thump = event(*TECH_DEBRIS, length=0.45)
    rattle_source, rattle_index = RATTLES[v]
    rattle = event(*rattle_source, index=rattle_index, length=0.7)
    chime = warped_chime(v)
    # The reversed chime swells into the crash; the stuttered ones break up as the drone starts to fall.
    chime_at = land - len(chime) / SR if v == 1 else 0.03
    return [
        Layer("Crack", clip(filt(dark(pitch(crack, -2.0), 4500), "highpass", 200), fade_out=0.12)),
        Layer("Death Glitch", clip(filt(dark(pitch(glitch, -2.0), 5000), "highpass", 250), fade_out=0.12)),
        Layer("Chime", clip(filt(dark(chime, 7000), "highpass", 250)), at=chime_at),
        # High-passed above the rotor fundamental: the falling harmonics carry the power-down, the landing
        # alone carries the low end.
        Layer("Power Down", clip(filt(dark(pitch(power, power_pitch), 5000), "highpass", 250), fade_out=0.4),
              at=0.05),
        Layer("Servo Fall", clip(dark(pitch(fall, rng.uniform(-3.0, -2.0)), 6000), fade_out=0.15), at=0.1),
        Layer("Shell Crash", clip(filt(dark(pitch(crt, (-3.0, -2.0, -4.0)[v]), 6000), "highpass", 120),
                                  fade_out=0.3), at=land),
        Layer("Crash Weight", clip(filt(pitch(thump, -1.0), "lowpass", 900), fade_out=0.25), at=land),
        Layer("Rattle", clip(filt(dark(pitch(rattle, rng.uniform(-3.0, -1.5)), 6000), "highpass", 250),
                             fade_out=0.25), at=land + 0.02),
    ]


UNIT = Unit(
    "Machine", "Ranged",
    tracks=[("Key", 0.0), ("Blaster", 0.0), ("Laser", -5.0), ("Weight", -6.0), ("Beam", -14.0),
            ("Glass Snap", -12.0), ("Glass Tail", -12.0),
            ("Energy Hit", -3.0), ("Shock", -4.0), ("Fizz", -9.0), ("Glitch", -5.0), ("Debris", -5.0),
            ("Crack", -6.0), ("Death Glitch", -8.0), ("Chime", -7.0), ("Power Down", -8.0), ("Servo Fall", -5.0),
            ("Shell Crash", -2.0), ("Crash Weight", -4.0), ("Rattle", -8.0)],
    events=[
        Event("Fire", 4, 1.15, -20.0, ranged_fire),
        Event("Impact", 3, 0.9, -22.0, ranged_impact),
        Event("Death", 3, 1.6, -21.0, ranged_death),
    ],
)
