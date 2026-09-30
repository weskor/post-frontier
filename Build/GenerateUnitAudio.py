#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "scipy", "soundfile", "pedalboard", "pyloudnorm"]
# ///
"""Build unit one-shots from recorded layers, mixed and rendered in REAPER (Docs/Audio.md).

    uv run Build/FetchAudioSources.py                 # once: recorded sources into Saved/AudioSources/
    uv run Build/GenerateUnitAudio.py                 # prepare layers, render sessions, write Art/Audio
    uv run Build/GenerateUnitAudio.py --new-session   # also rebuild the REAPER sessions from the recipes

Three stages per unit:
1. Prepare: cut single events out of the source recordings (onset detection), convert to 48 kHz mono,
   apply the recipe's pitch/filter/trim and write one clip per layer to Saved/AudioLayers/<Unit>/.
2. Session: Art/Audio/Sessions/<Unit>.rpp holds one track per layer (the mix balance lives in the track
   faders) and one region per output sound. The script writes a session only when it is missing or with
   --new-session, so balance, timing and FX changed by hand in REAPER survive reruns.
3. Render: `reaper -renderproject` renders every region; each render is trimmed, limited and matched to
   its event's loudness target, then written to Art/Audio/<Faction>/<Role>/<Region>.wav (48 kHz, 24-bit,
   mono, true peak <= -1 dBFS). A listening reel per unit goes to Saved/AudioPreview/<Unit>.wav.

Deterministic: fixed seeds and fixed source events, so reruns of an unchanged session give identical files.
"""
import argparse
import shutil
import subprocess
from dataclasses import dataclass
from os.path import relpath
from pathlib import Path
from typing import Callable

import numpy as np
import pyloudnorm
import soundfile as sf
from pedalboard import Limiter, Pedalboard
from scipy.signal import butter, resample_poly, sosfilt

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "Saved" / "AudioSources" / "Sonniss"
LAYERS = ROOT / "Saved" / "AudioLayers"
RENDERS = ROOT / "Saved" / "AudioRender"
PREVIEW = ROOT / "Saved" / "AudioPreview"
SESSIONS = ROOT / "Art" / "Audio" / "Sessions"
OUT = ROOT / "Art" / "Audio"
SR = 48_000
TRUE_PEAK_CEILING_DBFS = -1.0
REGION_SPACING = 3.0  # seconds between region starts in a session


# ---------------------------------------------------------------- source handling

def seconds(n: float) -> int:
    return int(round(n * SR))


_cache: dict[str, np.ndarray] = {}


def load(library: str, name: str) -> np.ndarray:
    key = f"{library}/{name}"
    if key not in _cache:
        path = SOURCES / library / name
        if not path.exists():
            raise SystemExit(f"missing {path}; run uv run Build/FetchAudioSources.py first")
        data, rate = sf.read(path, dtype="float64", always_2d=True)
        mono = data.mean(axis=1)
        _cache[key] = resample_poly(mono, SR, rate) if rate != SR else mono
    return _cache[key]


def onsets(x: np.ndarray, rise_db: float = -18.0, rearm_db: float = -38.0) -> list[int]:
    """Sample positions of separate events: the envelope rises above rise_db after falling below rearm_db."""
    hop = seconds(0.005)
    env = np.array([np.max(np.abs(x[i:i + hop])) for i in range(0, len(x), hop)])
    db = 20 * np.log10(env / (np.max(env) or 1.0) + 1e-9)
    found, armed = [], True
    for i, level in enumerate(db):
        if armed and level > rise_db:
            # Refine to the first sample reaching a quarter of the local peak so layers align tightly.
            window = x[i * hop:(i + 2) * hop]
            peak = np.max(np.abs(window)) or 1.0
            found.append(i * hop + int(np.argmax(np.abs(window) >= 0.25 * peak)))
            armed = False
        elif not armed and level < rearm_db:
            armed = True
    return found


def event(library: str, name: str, index: int = 0, length: float = 1.0, skip: float = 0.0,
          pre: float = 0.003) -> np.ndarray:
    """One event from a source: `index`-th onset, starting `skip` after it, `length` seconds long."""
    x = load(library, name)
    start = max(0, onsets(x)[index] + seconds(skip) - seconds(pre))
    return x[start:start + seconds(length)].copy()


def pitch(x: np.ndarray, semitones: float) -> np.ndarray:
    """Varispeed pitch shift: higher is shorter."""
    return resample_poly(x, 1000, int(round(1000 * 2.0 ** (semitones / 12.0))))


def filt(x: np.ndarray, kind: str, freq, order: int = 4) -> np.ndarray:
    return sosfilt(butter(order, freq, btype=kind, fs=SR, output="sos"), x)


def fades(x: np.ndarray, fade_in: float = 0.001, fade_out: float = 0.03) -> np.ndarray:
    x = x.copy()
    n_in, n_out = min(seconds(fade_in), len(x)), min(seconds(fade_out), len(x))
    x[:n_in] *= np.linspace(0.0, 1.0, n_in)
    x[len(x) - n_out:] *= np.linspace(1.0, 0.0, n_out)
    return x


def clip(x: np.ndarray, fade_in: float = 0.001, fade_out: float = 0.03) -> np.ndarray:
    """Final layer clip: faded and peak-normalized to -1 dBFS, so track faders alone set the balance."""
    x = fades(x, fade_in, fade_out)
    return x / (np.max(np.abs(x)) or 1.0) * 10 ** (-1 / 20)


# ---------------------------------------------------------------- recipes

@dataclass
class Layer:
    track: str
    audio: np.ndarray
    at: float = 0.0  # offset inside the region, seconds


@dataclass
class Event:
    name: str          # output event, e.g. "Fire"
    variants: int
    length: float      # region length, seconds
    loudness: float    # integrated loudness target, LUFS
    build: Callable[[int], list[Layer]]


@dataclass
class Unit:
    faction: str
    role: str
    tracks: list[tuple[str, float]]  # (track name, initial fader dB) in session order
    events: list[Event]

    @property
    def key(self) -> str:
        return f"{self.faction}_{self.role}"


# Human Ranged, "Offline Ranger": long rifle with antenna pack. Attack interval 1.15 s, so one shot plus
# its bolt cycle fits before the next shot. Direction: dark and sci-fi. Recorded rifles are pitched
# down and rolled off above ~6 kHz; designed sci-fi shots, an electric-arc tail and a servo on the bolt
# carry the future-tech character.
POLE_1903 = "Pole Position - Springfield 1903A3 bolt-action rifle"
POLE_K98 = "Pole Position - Mauser Karabiner 98 kurz K98k bolt-action rifle"
POLE_OUTDOOR = "Pole Position - The Outdoor Gun Acoustics Library"
PMSFX_GUNNERY = "PMSFX - SCI-FI GunnerySCI-FI Gunnery"
ARC = ("Sound Spark LLC - Electric Arcs and Energy", "Electric_Arc_Reverberant_Shock_Long_05.wav")
SCIFI_SHOTS = [
    (PMSFX_GUNNERY, "PM_SFG_VOL1_WEAPON_4_4_GUN_GUNSHOT_FUTURISTIC.wav"),
    (PMSFX_GUNNERY, "PM_SFG_VOL1_WEAPON_8_2_GUN_GUNSHOT_FUTURISTIC.wav"),
    (PMSFX_GUNNERY, "PM_SFG_VOL2_WEAPON_15_5_GUN_GUNSHOT_FUTURISTIC.wav"),
    (PMSFX_GUNNERY, "PM_SFG_VOL2_WEAPON_41_6_GUN_GUNSHOT_FUTURISTIC.wav"),
]
SCIFI_PUNCHES = [
    ("SoundMorph - FUTURE WEAPONS 3", "Future Weapons 3 - Assault Rifle - Shot Single 4.wav"),
    ("SoundMorph - Future Weapons", "Alliance-AssaultRifle_05-Single_Shot-04.wav"),
    ("SoundMorph - Future Weapons", "Resistance-AssaultRifle_03-Single_Shot-04.wav"),
    ("David Dumais Audio - Futuristic Guns Sound FX Pack 3", "OtherGuns_Rifle_4.wav"),
]


def dark(x: np.ndarray, cutoff: float) -> np.ndarray:
    """Gentle 12 dB/octave roll-off: removes brightness without the boxy sound of a steep filter."""
    return filt(x, "lowpass", cutoff, order=2)


def ranged_fire(v: int) -> list[Layer]:
    rng = np.random.default_rng(1100 + v)
    detune = rng.uniform(-0.4, 0.4)
    rifle = event(POLE_1903, "M1903A3, Firing, t2, 1m, Right, Above, MKH8060.wav", index=v, length=1.1)
    # Some K98k takes register a second onset (the slap off the range wall); these indices are the shots.
    low = event(POLE_K98, "K98k, Firing, t2, MKH416.wav", index=(2, 3, 4, 6)[v], length=0.5)
    shot = event(*SCIFI_SHOTS[v], length=0.8)
    punch = event(*SCIFI_PUNCHES[v], length=0.3)
    arc = event(*ARC, skip=0.12 * v, length=0.6)
    tail = event(POLE_OUTDOOR, "AK47_big_open_area_2m_above_behind_gun_RSM191_M.wav", index=v, skip=0.04, length=1.0)
    bolt = load(POLE_1903, "M1903A3, Handling, Cycling Bolt, MKH416.wav")
    bolt = bolt[seconds(0.55):seconds(1.05)]  # bolt back and home, without the handle lift and lock
    servo = event("PMSFX - Foundation Series SCI-FI vol 2", "PM_FSSF2_EXOSKELETON_11_SERVO_MOVEMENT_ROTATION.wav",
                  length=0.4)
    casing = event("Stuart Duffield - Bullet SFX", "ShotgunShell_Land_Concrete_02.wav", length=0.35)
    cycle = rng.uniform(0.34, 0.4)
    return [
        Layer("Rifle", clip(dark(pitch(rifle, detune - 1.0), 6000), fade_out=0.25)),
        Layer("Rifle Low", clip(filt(pitch(low, detune - 1.0), "lowpass", 900), fade_out=0.2)),
        Layer("Sci-Fi Shot", clip(filt(dark(pitch(shot, -2.0), 8000), "highpass", 100), fade_out=0.25)),
        Layer("Sci-Fi Punch", clip(filt(dark(pitch(punch, -1.5), 8000), "highpass", 180), fade_out=0.08)),
        # High-passed: the arc recording's deep hum would otherwise rumble through the whole attack interval.
        Layer("Energy Tail", clip(filt(dark(pitch(arc, -5.0), 4000), "highpass", 350), fade_in=0.03, fade_out=0.35),
              at=0.02),
        Layer("Outdoor Tail", clip(dark(tail, 3000), fade_in=0.02, fade_out=0.4), at=0.03),
        Layer("Bolt", clip(dark(pitch(bolt, rng.uniform(-2.3, -1.7)), 5000), fade_out=0.05), at=cycle),
        Layer("Servo", clip(dark(pitch(servo, -3.0), 6000), fade_out=0.08), at=cycle + 0.02),
        Layer("Casing", clip(dark(pitch(casing, rng.uniform(-4, -2)), 6000)), at=rng.uniform(0.62, 0.7)),
    ]


IMPACT_HITS = [
    ("Olivier Girardot - Guns & Explosions", "Bullet Impact 22.wav"),
    ("PMSFX - Bullet Bys &Impacts", "PM_BBI_Bullet_Impact_Dirt_3.wav"),
    ("Olivier Girardot - Hand Guns Sound Effects Pack", "Bullet rock Impact 4.wav"),
]
SCIFI_HITS = [
    ("RYK-Sounds - Laser Guns", "heavy hit.wav"),
    ("SoundMorph - FUTURE WEAPONS 3", "Future Weapons 3 - Grenade Launcher 2 - Hit 2.wav"),
    ("RYK-Sounds - Laser Guns", "hitsound 2.wav"),
]


def ranged_impact(v: int) -> list[Layer]:
    rng = np.random.default_rng(1200 + v)
    hit = event(*IMPACT_HITS[v], length=0.7)
    scifi = event(*SCIFI_HITS[v], length=0.6)
    # The targets are Machine shells: every hit rings armour plate and crackles with energy.
    plate = event("Double Trouble Audio - Medieval Armor and Impacts", "Plate_Impact_Hard_02.wav", length=0.5)
    arc = event(*ARC, skip=0.05 + 0.1 * v, length=0.3)
    debris = event("Olivier Girardot - Natural Disasters", "Guns & Explosions Album - Bullet Impacts - Multiple 1.wav",
                   index=v, length=0.6)
    return [
        Layer("Hit", clip(dark(pitch(hit, rng.uniform(-2.0, -1.0)), 6000), fade_out=0.15)),
        Layer("Sci-Fi Hit", clip(dark(pitch(scifi, -2.0), 8000), fade_out=0.2)),
        Layer("Armour", clip(dark(pitch(plate, (0, 2, -1)[v]), 7000), fade_out=0.15), at=0.004),
        Layer("Energy", clip(filt(dark(pitch(arc, -2.0), 6000), "highpass", 400), fade_in=0.005, fade_out=0.1),
              at=0.01),
        Layer("Debris", clip(dark(filt(debris, "highpass", 300), 5000), fade_out=0.2), at=0.01),
    ]


def ranged_death(v: int) -> list[Layer]:
    rng = np.random.default_rng(1300 + v)
    gasp = event("Epic Stock Media - AAA Game Character Police Officer",
                 "HMNBrth_Police Officer Gasp Vocal Male Shocked Alert 1.wav", length=0.45)
    # The antenna pack and suit electronics shut down as the Ranger drops.
    power = event("SoundMorph - Robotic Lifeforms 2", "Robotic Lifeforms 2 - Power - Autobot Disengage 08.wav",
                  length=1.0)
    gear = event("Gamemaster Audio - Footstep and Foley Sounds",
                 "foley_soldier_gear_equipment_metal_cloth_heavy_movement_light_08.wav", index=v % 2, length=0.5)
    drop = event("SmartSoundFX – Medieval", "ARMOR Body Drop Chain Leather Short 02.wav", length=1.2)
    fall_name = ("RL_bodyfall_Concrete_Generic_Feet_Mid_Mono_Med_Impact_02.wav",
                 "RL_bodyfall_Dirt_M4_Close_Stereo_Hard_Impact_10.wav")[v % 2]
    fall = event("Red Libraries - Bodyfall", fall_name, length=0.9)
    land = rng.uniform(0.34, 0.42)
    return [
        Layer("Vox", clip(dark(pitch(gasp, (-1, -2.5, 0)[v]), 5000), fade_out=0.08)),
        Layer("Power Down", clip(dark(pitch(power, (-3, -4, -2)[v]), 6000), fade_out=0.3), at=0.05),
        Layer("Gear", clip(dark(pitch(gear, rng.uniform(-2, -1)), 6000), fade_out=0.1), at=0.12),
        Layer("Armour Drop", clip(pitch(drop, (-1, -2, 0)[v]), fade_out=0.3), at=land - 0.01),
        Layer("Body Fall", clip(pitch(fall, (-1, -1, -3)[v]), fade_out=0.25), at=land),
    ]


UNITS = [
    Unit("Human", "Ranged",
         tracks=[("Rifle", 0.0), ("Rifle Low", -4.0), ("Sci-Fi Shot", -4.0), ("Sci-Fi Punch", -8.0),
                 ("Energy Tail", -16.0), ("Outdoor Tail", -14.0), ("Bolt", -17.0), ("Servo", -20.0),
                 ("Casing", -24.0),
                 ("Hit", -1.0), ("Sci-Fi Hit", -5.0), ("Armour", -9.0), ("Energy", -14.0), ("Debris", -13.0),
                 ("Vox", -6.0), ("Power Down", -9.0), ("Gear", -11.0), ("Armour Drop", -3.0),
                 ("Body Fall", 0.0)],
         events=[
             Event("Fire", 4, 1.15, -20.0, ranged_fire),
             Event("Impact", 3, 0.9, -22.0, ranged_impact),
             Event("Death", 3, 1.6, -21.0, ranged_death),
         ]),
]


# ---------------------------------------------------------------- stage 1: prepare layers

def region_name(unit: Unit, event_name: str, variant: int) -> str:
    return f"SW_{unit.key}_{event_name}_{variant + 1:02d}"


def prepare(unit: Unit) -> list[tuple[str, float, float, Layer, Path]]:
    """Writes every layer clip; returns (region, region start, region length, layer, clip path)."""
    folder = LAYERS / unit.key
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    placed, slot = [], 0
    for ev in unit.events:
        for v in range(ev.variants):
            region = region_name(unit, ev.name, v)
            for layer in ev.build(v):
                path = folder / f"{region}__{layer.track.replace(' ', '')}.wav"
                sf.write(path, layer.audio, SR, subtype="PCM_24")
                placed.append((region, slot * REGION_SPACING, ev.length, layer, path))
            slot += 1
    return placed


# ---------------------------------------------------------------- stage 2: REAPER session

def write_session(unit: Unit, placed, session: Path) -> None:
    session.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        '<REAPER_PROJECT 0.1 "7.79/linux-x86_64" 0',
        "  SAMPLERATE 48000 0 0",
        f'  RENDER_FILE "{relpath(RENDERS / unit.key, session.parent)}"',
        "  RENDER_PATTERN $region",
        "  RENDER_FMT 0 1 48000",  # mono, 48 kHz
        "  RENDER_1X 0",
        "  RENDER_RANGE 3 0 0 0 1000",  # all project regions
        "  RENDER_STEMS 0",
        "  <RENDER_CFG",
        "    ZXZhdxgAAQ==",  # WAV, 24-bit PCM
        "  >",
    ]
    regions: dict[str, tuple[float, float]] = {}
    for region, start, length, _layer, _path in placed:
        regions[region] = (start, length)
    for index, (region, (start, length)) in enumerate(regions.items(), start=1):
        lines += [f'  MARKER {index} {start:.6f} "{region}" 1', f'  MARKER {index} {start + length:.6f} "" 1']
    for track, fader_db in unit.tracks:
        lines += ["  <TRACK", f'    NAME "{track}"', f"    VOLPAN {10 ** (fader_db / 20):.6f} 0 -1 -1 1"]
        for region, start, length, layer, path in placed:
            if layer.track != track:
                continue
            item_length = min(len(layer.audio) / SR, length - layer.at)
            lines += [
                "    <ITEM",
                f"      POSITION {start + layer.at:.6f}",
                f"      LENGTH {item_length:.6f}",
                "      FADEOUT 1 0.005 0 1 0 0 0",
                f'      NAME "{region} {track}"',
                "      <SOURCE WAVE",
                f'        FILE "{relpath(path, session.parent)}"',
                "      >",
                "    >",
            ]
        lines.append("  >")
    lines.append(">")
    session.write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------- stage 3: render and finish

LOUDNESS = pyloudnorm.Meter(SR)
CEILING = 10 ** (TRUE_PEAK_CEILING_DBFS / 20)
# JUCE's limiter (pedalboard.Limiter) adds make-up gain up to 0 dBFS; finish() rescales after it.
LIMITER = Pedalboard([Limiter(threshold_db=-6.0, release_ms=60)])


def true_peak(y: np.ndarray) -> float:
    return float(np.max(np.abs(resample_poly(y, 4, 1))))


def loudness(y: np.ndarray) -> float:
    return LOUDNESS.integrated_loudness(np.pad(y, (0, max(0, seconds(0.5) - len(y)))))


def finish(y: np.ndarray, target_lufs: float) -> tuple[np.ndarray, float, float]:
    """Trim the silent end, match loudness, limit peaks; returns audio, LUFS and true peak dBFS.

    Gunshots have a large crest factor, so reaching the loudness target can push the true peak past the
    ceiling. Then the limiter shaves the transient and the result is scaled back under the ceiling;
    a few passes converge on the target wherever limiting allows."""
    loud = np.nonzero(np.abs(y) > 10 ** (-60 / 20) * (np.max(np.abs(y)) or 1.0))[0]
    y = fades(y[: loud[-1] + seconds(0.01)] if len(loud) else y, fade_in=0.0, fade_out=0.01)
    for _ in range(4):
        y = y * 10 ** ((target_lufs - loudness(y)) / 20)
        if true_peak(y) <= CEILING:
            break
        y = LIMITER(y.astype(np.float32), SR).astype(np.float64)
        y = y * CEILING / true_peak(y)
    return y, loudness(y), 20 * np.log10(true_peak(y))


def render(unit: Unit, session: Path) -> int:
    out_dir = RENDERS / unit.key
    shutil.rmtree(out_dir, ignore_errors=True)
    out_dir.mkdir(parents=True)
    result = subprocess.run(["reaper", "-nosplash", "-new", "-renderproject", str(session)],
                            capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        raise SystemExit(f"REAPER render failed ({result.returncode}): {result.stderr[-2000:]}")
    final = OUT / unit.faction / unit.role
    final.mkdir(parents=True, exist_ok=True)
    reel, written = [], 0
    for ev in unit.events:
        for v in range(ev.variants):
            name = region_name(unit, ev.name, v)
            rendered = out_dir / f"{name}.wav"
            if not rendered.exists():
                raise SystemExit(f"REAPER did not render region {name}; is it still in {session}?")
            data, rate = sf.read(rendered, dtype="float64", always_2d=True)
            audio, lufs, peak = finish(data.mean(axis=1), ev.loudness)
            sf.write(final / f"{name}.wav", audio, SR, subtype="PCM_24")
            reel += [audio, np.zeros(seconds(0.5))]
            written += 1
            print(f"{(final / name).relative_to(ROOT)}.wav  {len(audio) / SR:.2f}s  {lufs:.1f} LUFS  "
                  f"true peak {peak:.1f} dBFS")
    PREVIEW.mkdir(parents=True, exist_ok=True)
    sf.write(PREVIEW / f"{unit.key}.wav", np.concatenate(reel), SR, subtype="PCM_24")
    return written


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--new-session", action="store_true",
                        help="rewrite the REAPER sessions from the recipes, discarding manual changes")
    args = parser.parse_args()
    if not shutil.which("reaper"):
        raise SystemExit("reaper not found on PATH (sudo pacman -S reaper)")
    total = 0
    for unit in UNITS:
        placed = prepare(unit)
        session = SESSIONS / f"{unit.key}.rpp"
        if args.new_session or not session.exists():
            write_session(unit, placed, session)
            print(f"wrote session {session.relative_to(ROOT)}")
        total += render(unit, session)
    print(f"UNIT_AUDIO_RENDERED {total}")


if __name__ == "__main__":
    main()
