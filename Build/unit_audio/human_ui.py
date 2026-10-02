"""Human UI: the player's HUD and sector feedback (Docs/Audio.md "UI", "Buildings, sectors and HQs").

The player is the Offline, so the interface sounds like their night-shift switch room: chunky toggle
switches, relays and contactors, a detent knob on a field radio, a buzzer, breakers and transformer hum.
Direction: dark and sci-fi. Recordings are pitched down 1-3 semitones and rolled off; small designed
sci-fi clicks and a designed lever give the hardware its future-tech edge. Clicks stay short and dry.

Sector feedback shares one tonal element, a struck anvil ring: the capture ticks climb a major triad
(root, third, fifth at 25/50/75 %), a captured sector resolves it up to the octave, a lost sector falls
below the root. Hums are high-passed and die within a second, so several never stack into a drone.
"""
import numpy as np

from .core import SR, Event, Layer, Unit, clip, dark, event, filt, load, onsets, pitch, seconds

SOURCES = [
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'BluezoneCorp - Industrial Lever Switch/Bluezone_BC0302_industrial_lever_switch_small_003.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'BluezoneCorp - Industrial Lever Switch/Bluezone_BC0302_industrial_lever_switch_014.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'BluezoneCorp - Industrial Lever Switch/Bluezone_BC0302_industrial_lever_switch_039.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%201of6.zip',
     '3maze -  Buttons and Switches/light_switch_019.wav'),
    ('Sonniss.com-GDC2026-GameAudioBundle3of5.zip',
     'InMotionAudio - USA Hotel/MECHClik_USALightSwitch_On05_InMotionAudio_USAHotel.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle6of9.zip',
     'Pole Position - The Vehicle Doors and More Library/Black Hawk - VAR SFX - Stabilizer Manual Slew Switch - MKH418S.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%204of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 4of8/Omar Alvarado - Household, kitchen and hotel sounds/Electric power surge protector turned on with beep and loud relay clicks, pause, relay click with buzz2.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%206of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 6of8/UberDuo - The Home Barista/Coffee, Grinder, Knob, Turn, Click X3.wav'),
    ('Sonniss.com%20-%20GDC%202020%20-%20Game%20Audio%20Bundle%20Part12of14.zip',
     'Soundholder - Renault Master 2.3 dci/renault master 2.3 dci foley interior radio knob turn.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%204of6.zip',
     'Sergey Eybog - Handheld Tranceivers/Walkie Talkie,Cobra MicroTalk,Radio Communication,End,Noise Squelch,Beep,Series.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%204of6.zip',
     'Sergey Eybog - Handheld Tranceivers/Walkie Talkie,Kenwood TK-3107 FM,Radio Communication,No Speech,Static,Feedback Squelch,Manipulations.wav'),
    ('Sonniss.com%20-%20GDC%202016-%20Game%20Audio%20Bundle%20Part%202of6.zip',
     'MatiasMacSD - NOISE/OldRadioTuningStaticNoise_001.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%202of5.zip',
     'Detunized - HumBuzz/HumBuzz-Buzz-03.wav'),
    ('Sonniss.com%20-%20GDC%20-%20Game%20Audio%20Bundle%202of5.zip',
     'Detunized - HumBuzz/HumBuzz-Hum-17.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%206of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 6of8/Tone Manufacture - Glitch UI/TM_GLITCH UI_Clicks dry_11.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%205of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 5of9/Mononeshot - MEDIEVAL, Smithy/MEDIEVAL, Smithy - 08 - Anvil hits - Tree hits.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%208of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 8of9/SoundMorph - ENERGY/Energy - electric_fuse-08.wav'),
    ('Sonniss.com%20-%20GDC%202018%20-%20Game%20Audio%20Bundle%20Part%206of8.zip',
     'Sonniss.com - GDC 2018 - Game Audio Bundle Part 6of8/UberDuo - The Cabin Audio Playset/Switch,ElectricalPanelBreaker,On.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%208of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 8of9/SoundMorph - MECHANISM/Mechanism -  Designed Mega Steam Lever-001.wav'),
    ('Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%206of8.zip',
     'Sonniss.com - GDC 2019 - Game Audio Bundle Part 6of8/SoundMorph - Robotic Lifeforms 2/Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav'),
    ('Sonniss.com-GDC2024-GameAudioBundle1of9.zip',
     'BluezoneCorp - High Voltage/Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav'),
]

LEVERS = "BluezoneCorp - Industrial Lever Switch"
SWITCHES = [
    (LEVERS, "Bluezone_BC0302_industrial_lever_switch_small_003.wav"),
    ("3maze -  Buttons and Switches", "light_switch_019.wav"),
    ("InMotionAudio - USA Hotel", "MECHClik_USALightSwitch_On05_InMotionAudio_USAHotel.wav"),
]
SLEW = ("Pole Position - The Vehicle Doors and More Library",
        "Black Hawk - VAR SFX - Stabilizer Manual Slew Switch - MKH418S.wav")
RELAYS = ("Omar Alvarado - Household, kitchen and hotel sounds",
          "Electric power surge protector turned on with beep and loud relay clicks, pause, relay click with buzz2.wav")
KNOB = ("Soundholder - Renault Master 2.3 dci", "renault master 2.3 dci foley interior radio knob turn.wav")
GRINDER_KNOB = ("UberDuo - The Home Barista", "Coffee, Grinder, Knob, Turn, Click X3.wav")
COBRA = ("Sergey Eybog - Handheld Tranceivers",
         "Walkie Talkie,Cobra MicroTalk,Radio Communication,End,Noise Squelch,Beep,Series.wav")
KENWOOD = ("Sergey Eybog - Handheld Tranceivers",
           "Walkie Talkie,Kenwood TK-3107 FM,Radio Communication,No Speech,Static,Feedback Squelch,Manipulations.wav")
STATIC = ("MatiasMacSD - NOISE", "OldRadioTuningStaticNoise_001.wav")
CRACKLE = ("Detunized - HumBuzz", "HumBuzz-Buzz-03.wav")
HUM = ("Detunized - HumBuzz", "HumBuzz-Hum-17.wav")
TICK = ("Tone Manufacture - Glitch UI", "TM_GLITCH UI_Clicks dry_11.wav")
ANVIL = ("Mononeshot - MEDIEVAL, Smithy", "MEDIEVAL, Smithy - 08 - Anvil hits - Tree hits.wav")
FUSE = ("SoundMorph - ENERGY", "Energy - electric_fuse-08.wav")
BREAKER = ("UberDuo - The Cabin Audio Playset", "Switch,ElectricalPanelBreaker,On.wav")
STEAM_LEVER = ("SoundMorph - MECHANISM", "Mechanism -  Designed Mega Steam Lever-001.wav")
DISENGAGE = ("SoundMorph - Robotic Lifeforms 2", "Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav")
SURGE = ("BluezoneCorp - High Voltage", "Bluezone_BC0299_electricity_surge_discharge_electrical_arc_crackling_002_01.wav")

# Relay clacks in the surge protector take (seconds): switching on, the held relay pulling in, releasing.
RELAY_CLACKS = (0.326, 1.286, 9.712)
# Walkie-talkie squelch bursts: the noise after each roger beep, without the beep tones.
SQUELCH_BURSTS = ((1.638, 0.15), (4.958, 0.045))
# Semitone offsets of the anvil ring (~2.38 kHz): a major triad on the capture ticks, pitched down to
# sit darker. Captured resolves to the octave, lost falls below the root.
ROOT = -7.0
TRIAD = (ROOT, ROOT + 4.0, ROOT + 7.0)


def cut(source: tuple[str, str], start: float, length: float) -> np.ndarray:
    """A slice of a source at an absolute time, for takes whose onsets the detector cannot separate."""
    x = load(*source)
    return x[seconds(start):seconds(start + length)].copy()


def ring(index: int, semitones: float, length: float) -> np.ndarray:
    """One anvil ring without the hammer thump or the next strike in the recording."""
    starts = onsets(load(*ANVIL))
    if index + 1 < len(starts):
        length = min(length, (starts[index + 1] - starts[index]) / SR - 0.005)
    strike = event(*ANVIL, index=index, length=length)
    return filt(dark(pitch(strike, semitones), 8000), "highpass", 500 * 2 ** (semitones / 12))


def click(v: int) -> list[Layer]:
    rng = np.random.default_rng(9100 + v)
    body = event(*SWITCHES[v], length=0.12)
    chunk = event(*SLEW, index=(0, 1, 3)[v], length=0.09)
    tick = event(*TICK, length=0.05)
    return [
        Layer("Switch", clip(filt(dark(pitch(body, rng.uniform(-2.0, -1.0)), 7000), "highpass", 120),
                             fade_out=0.04)),
        Layer("Switch Low", clip(filt(dark(pitch(chunk, -2.0), 4000), "highpass", 120), fade_out=0.04)),
        Layer("Tick", clip(filt(dark(pitch(tick, -3.0), 9000), "highpass", 1500), fade_out=0.02), at=0.002),
    ]


def select(v: int) -> list[Layer]:
    rng = np.random.default_rng(9200 + v)
    relay = cut(RELAYS, RELAY_CLACKS[1 + v] - 0.003, 0.2)
    contactor = event(*SLEW, index=(1, 3)[v], length=0.12)
    spark = cut(CRACKLE, (0.112, 1.306)[v], 0.03)
    return [
        Layer("Relay", clip(filt(dark(pitch(relay, rng.uniform(-2.0, -1.0)), 7000), "highpass", 150),
                            fade_out=0.1)),
        Layer("Contactor", clip(filt(dark(pitch(contactor, -3.0), 4000), "highpass", 120), fade_out=0.05)),
        Layer("Spark", clip(filt(dark(spark, 8000), "highpass", 2000), fade_out=0.015), at=0.004),
    ]


def front(v: int) -> list[Layer]:
    rng = np.random.default_rng(9300 + v)
    detent = cut(KNOB, (0.228, 2.04)[v], 0.2)
    grinder = event(*GRINDER_KNOB, index=v, length=0.08)
    start, length = SQUELCH_BURSTS[v]
    squelch = cut(COBRA, start - 0.002, length + 0.01)
    static = cut(STATIC, (10.9, 8.1)[v], 0.16)
    ptt = event(*KENWOOD, index=0, length=0.03)
    key = 0.07 + rng.uniform(0.0, 0.01)
    return [
        Layer("Detent", clip(filt(dark(pitch(detent, -1.5), 7000), "highpass", 150), fade_out=0.05)),
        Layer("Detent Low", clip(filt(dark(pitch(grinder, -3.0), 5000), "highpass", 150), fade_out=0.03)),
        Layer("PTT", clip(filt(dark(pitch(ptt, -1.0), 7000), "highpass", 300), fade_out=0.01), at=key - 0.01),
        Layer("Squelch", clip(filt(dark(pitch(squelch, -1.0), 5000), "highpass", 350), fade_in=0.002,
                              fade_out=0.03), at=key),
        Layer("Static", clip(filt(dark(static, 4500), "highpass", 400), fade_in=0.004, fade_out=0.08), at=key),
    ]


def reject(v: int) -> list[Layer]:
    rng = np.random.default_rng(9400 + v)
    relay = cut(RELAYS, RELAY_CLACKS[0] - 0.003, 0.12)
    chatter = cut(RELAYS, 0.6, 0.06)
    layers = [
        Layer("Relay", clip(filt(dark(pitch(relay, -3.0), 6000), "highpass", 150), fade_out=0.05)),
        Layer("Relay", clip(filt(dark(pitch(chatter, -2.0), 6000), "highpass", 150), fade_out=0.02), at=0.13),
    ]
    # Two short pulses of a mains buzz: the classic "denied" double buzz, low and rounded.
    for n, at in enumerate((0.0, 0.14)):
        buzz = cut(HUM, 0.6 + 0.4 * n + 0.9 * v, 0.1)
        grit = cut(CRACKLE, (0.12, 1.31)[n], 0.1)
        layers += [
            Layer("Buzzer", clip(filt(dark(pitch(buzz, (-3.0, -4.0)[v] + rng.uniform(-0.1, 0.1)), 4000),
                                      "highpass", 150), fade_in=0.004, fade_out=0.02), at=at + 0.004),
            Layer("Grit", clip(filt(dark(grit, 7000), "highpass", 1500), fade_in=0.004, fade_out=0.02),
                  at=at + 0.004),
        ]
    return layers


def hover(v: int) -> list[Layer]:
    tick = event(*TICK, length=0.05) if v == 0 else event(*SWITCHES[1], length=0.03)
    crackle = cut(CRACKLE, (1.9, 0.846)[v], 0.02)
    return [
        Layer("Hover", clip(filt(dark(pitch(tick, (-4.0, 2.0)[v]), 7000), "highpass", (1200, 800)[v]),
                            fade_out=0.015)),
        Layer("Phosphor", clip(filt(dark(crackle, 9000), "highpass", 3000), fade_out=0.01)),
    ]


def capture_tick(v: int) -> list[Layer]:
    rng = np.random.default_rng(9500 + v)
    relay = cut(RELAYS, RELAY_CLACKS[0] - 0.003, 0.15)
    hum = cut(HUM, 1.2 + 0.3 * v, 0.3)
    return [
        Layer("Relay", clip(filt(dark(pitch(relay, rng.uniform(-2.0, -1.0)), 7000), "highpass", 150),
                            fade_out=0.06)),
        Layer("Ping", clip(ring(1, TRIAD[v], 0.6), fade_in=0.002, fade_out=0.3), at=0.01),
        # The transformer hum engaging, following the ping up.
        Layer("Hum", clip(filt(dark(pitch(hum, TRIAD[v] + 4.0), 5000), "highpass", 200), fade_in=0.03,
                          fade_out=0.15), at=0.01),
    ]


def sector_captured(v: int) -> list[Layer]:
    rng = np.random.default_rng(9600 + v)
    lever = event(LEVERS, ("Bluezone_BC0302_industrial_lever_switch_014.wav",
                           "Bluezone_BC0302_industrial_lever_switch_039.wav")[v], length=0.7)
    steam = event(*STEAM_LEVER, length=0.8)
    snap = event(*BREAKER, length=0.3)
    surge = event(*SURGE, length=0.35)
    hum = cut(HUM, 0.4 + 0.5 * v, 1.3)
    swell = 0.12 + rng.uniform(0.0, 0.02)
    return [
        Layer("Breaker", clip(filt(dark(pitch(lever, -2.0), 6000), "highpass", 80), fade_out=0.3)),
        Layer("Sci-Fi Lever", clip(filt(dark(pitch(steam, -2.0), 7000), "highpass", 150), fade_out=0.3)),
        Layer("Snap", clip(filt(dark(pitch(snap, -1.0), 8000), "highpass", 200), fade_out=0.1)),
        Layer("Arc", clip(filt(dark(pitch(surge, -2.0), 7000), "highpass", 500), fade_out=0.15), at=0.01),
        Layer("Hum", clip(filt(dark(pitch(hum, -1.0), 5000), "highpass", 180), fade_in=0.3, fade_out=0.7),
              at=0.03),
        Layer("Ping", clip(ring((0, 2)[v], TRIAD[2], 0.9), fade_in=0.002, fade_out=0.4), at=swell),
        Layer("Ping High", clip(ring((2, 0)[v], ROOT + 12.0, 1.2), fade_in=0.002, fade_out=0.6), at=swell + 0.22),
    ]


def sector_lost(v: int) -> list[Layer]:
    lever = event(LEVERS, ("Bluezone_BC0302_industrial_lever_switch_039.wav",
                           "Bluezone_BC0302_industrial_lever_switch_014.wav")[v], length=0.6)
    fuse = event(*FUSE, length=0.9)
    # The hum take ends in the transformer spinning down; start just before so it dies ~0.7 s in.
    dying = cut(HUM, 3.05 + 0.1 * v, 1.25)
    power = event(*DISENGAGE, length=1.0)
    return [
        Layer("Breaker", clip(filt(dark(pitch(lever, -3.0), 5000), "highpass", 80), fade_out=0.25)),
        Layer("Fuse", clip(filt(dark(pitch(fuse, -2.0), 6000), "highpass", 150), fade_out=0.4)),
        Layer("Hum", clip(filt(dark(pitch(dying, -1.0), 5000), "highpass", 150), fade_in=0.005, fade_out=0.2)),
        Layer("Power Down", clip(filt(dark(pitch(power, (-3.0, -4.0)[v]), 6000), "highpass", 150),
                                 fade_out=0.3), at=0.05),
        Layer("Ping", clip(ring(v, ROOT + 3.0, 0.8), fade_in=0.002, fade_out=0.4), at=0.2),
        Layer("Ping Low", clip(ring(2 - v, ROOT - 5.0, 1.2), fade_in=0.002, fade_out=0.6), at=0.45),
    ]


UNIT = Unit(
    "Human", "UI",
    tracks=[("Switch", 0.0), ("Switch Low", -6.0), ("Tick", -14.0),
            ("Relay", 0.0), ("Contactor", -6.0), ("Spark", -16.0),
            ("Detent", 0.0), ("Detent Low", -8.0), ("PTT", -10.0), ("Squelch", -6.0), ("Static", -10.0),
            ("Buzzer", -2.0), ("Grit", -18.0),
            ("Hover", 0.0), ("Phosphor", -16.0),
            ("Ping", -4.0), ("Ping High", -6.0), ("Hum", -10.0),
            ("Breaker", 0.0), ("Sci-Fi Lever", -6.0), ("Snap", -8.0), ("Arc", -16.0),
            ("Fuse", -4.0), ("Power Down", -8.0), ("Ping Low", -5.0)],
    events=[
        Event("Click", 3, 0.25, -24.0, click),
        Event("Select", 2, 0.3, -24.0, select),
        Event("Front", 2, 0.4, -23.0, front),
        Event("Reject", 2, 0.4, -23.0, reject),
        Event("Hover", 2, 0.12, -32.0, hover),
        Event("CaptureTick", 3, 0.6, -24.0, capture_tick),
        Event("SectorCaptured", 2, 2.0, -21.0, sector_captured),
        Event("SectorLost", 2, 2.0, -21.0, sector_lost),
    ],
)
