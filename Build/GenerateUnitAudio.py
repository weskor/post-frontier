#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "scipy", "soundfile", "pedalboard", "pyloudnorm"]
# ///
"""Render unit one-shots from synthesis plus CC0 source layers (Docs/Audio.md "Unit sound sheets").

    python3 Build/FetchAudioSources.py      # once: CC0 layers into Saved/AudioSources/
    uv run Build/GenerateUnitAudio.py       # writes Art/Audio/<Faction>/<Role>/SW_*.wav

Deterministic: every variant has a fixed seed, so reruns produce identical files.
Output: 48 kHz, 24-bit, mono. Every variant of an event is matched to that event's integrated
loudness target (LUFS), so round-robin variants sit at the same level; true peak never exceeds
-1 dBFS. A preview reel of every rendered sound (variants separated by gaps) goes to
Saved/AudioPreview/<Faction>_<Role>.wav.

Each sound is built SC2-style from layers: transient + body + low weight + tail.
"""
from pathlib import Path

import numpy as np
import pyloudnorm
import soundfile as sf
from pedalboard import Compressor, Distortion, Limiter, Pedalboard, Reverb
from scipy.signal import butter, resample_poly, sosfilt

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "Saved" / "AudioSources"
OUT = ROOT / "Art" / "Audio"
PREVIEW = ROOT / "Saved" / "AudioPreview"
SR = 48_000
TRUE_PEAK_CEILING_DBFS = -1.0


# ---------------------------------------------------------------- primitives

def seconds(n: float) -> int:
    return int(round(n * SR))


def source(pack: str, name: str) -> np.ndarray:
    path = SOURCES / pack / f"{name}.ogg"
    if not path.exists():
        raise SystemExit(f"missing {path}; run python3 Build/FetchAudioSources.py first")
    data, rate = sf.read(path, dtype="float64", always_2d=True)
    mono = data.mean(axis=1)
    if rate != SR:
        mono = resample_poly(mono, SR, rate)
    return mono / (np.max(np.abs(mono)) or 1.0)


def pitch(x: np.ndarray, semitones: float) -> np.ndarray:
    """Tape-style pitch shift: higher is shorter, like a varispeed sampler."""
    ratio = 2.0 ** (semitones / 12.0)
    up, down = 1000, int(round(1000 * ratio))
    return resample_poly(x, up, down)


def exp_env(n: int, decay_s: float, attack_s: float = 0.0) -> np.ndarray:
    t = np.arange(n) / SR
    env = np.exp(-t / decay_s)
    if attack_s > 0:
        env *= np.clip(t / attack_s, 0.0, 1.0)
    return env


def noise(rng: np.random.Generator, dur: float) -> np.ndarray:
    return rng.standard_normal(seconds(dur))


def filt(x: np.ndarray, kind: str, freq, order: int = 4) -> np.ndarray:
    return sosfilt(butter(order, freq, btype=kind, fs=SR, output="sos"), x)


def sweep(dur: float, f0: float, f1: float, curve: float = 4.0) -> np.ndarray:
    """Sine whose frequency falls exponentially from f0 to f1."""
    t = np.arange(seconds(dur)) / SR
    freq = f1 + (f0 - f1) * np.exp(-curve * t / dur)
    return np.sin(2 * np.pi * np.cumsum(freq) / SR)


def trim(x: np.ndarray, dur: float, fade: float = 0.004) -> np.ndarray:
    x = x[: seconds(dur)].copy()
    n = min(seconds(fade), len(x))
    x[len(x) - n:] *= np.linspace(1.0, 0.0, n)
    return x


def normalize(x: np.ndarray) -> np.ndarray:
    return x / (np.max(np.abs(x)) or 1.0)


class Mix:
    def __init__(self, dur: float):
        self.buf = np.zeros(seconds(dur))

    def add(self, x: np.ndarray, at: float = 0.0, gain: float = 1.0) -> "Mix":
        start = seconds(at)
        end = min(len(self.buf), start + len(x))
        self.buf[start:end] += gain * x[: end - start]
        return self


def master(x: np.ndarray, chain: Pedalboard) -> np.ndarray:
    # Pad first so reverb tails decay naturally instead of being cut at the layer buffer's end.
    x = np.concatenate([normalize(x), np.zeros(seconds(0.4))])
    y = chain(x.astype(np.float32), SR).astype(np.float64)
    # Trim trailing silence below -60 dBFS, then a short fade so nothing clicks.
    loud = np.nonzero(np.abs(y) > 10 ** (-60 / 20) * np.max(np.abs(y)))[0]
    y = trim(y, (loud[-1] + 1) / SR + 0.01, fade=0.01)
    return normalize(y)


LOUDNESS = pyloudnorm.Meter(SR)


def true_peak(y: np.ndarray) -> float:
    """Inter-sample peak estimate from 4x oversampling."""
    return float(np.max(np.abs(resample_poly(y, 4, 1))))


def level(y: np.ndarray, target_lufs: float) -> tuple[np.ndarray, float]:
    """Gain to the integrated-loudness target, then pull down if the true-peak ceiling is exceeded."""
    measured = LOUDNESS.integrated_loudness(np.pad(y, (0, max(0, seconds(0.5) - len(y)))))
    y = y * 10 ** ((target_lufs - measured) / 20)
    ceiling = 10 ** (TRUE_PEAK_CEILING_DBFS / 20)
    if true_peak(y) > ceiling:
        y = y * ceiling / true_peak(y)
    return y, 20 * np.log10(true_peak(y))


# ---------------------------------------------------------------- Human Ranged (Offline Ranger)
# Docs/Audio.md: dry rifle crack + mechanical bolt + short outdoor slap; ricochet/armour ping;
# radio static burst cut off + body fall.

RIFLE_BUS = Pedalboard([
    Distortion(drive_db=8),
    Compressor(threshold_db=-14, ratio=5, attack_ms=0.5, release_ms=70),
    Reverb(room_size=0.22, damping=0.6, wet_level=0.07, dry_level=1.0, width=0.0),
    Limiter(threshold_db=-1, release_ms=40),
])

WORLD_BUS = Pedalboard([
    Compressor(threshold_db=-16, ratio=3, attack_ms=2, release_ms=120),
    Reverb(room_size=0.3, damping=0.55, wet_level=0.09, dry_level=1.0, width=0.0),
    Limiter(threshold_db=-1, release_ms=60),
])


def ranged_fire(variant: int) -> np.ndarray:
    rng = np.random.default_rng(1100 + variant)
    mix = Mix(0.55)
    # Transient: a few milliseconds of bright noise.
    crack = filt(noise(rng, 0.02), "highpass", 2500) * exp_env(seconds(0.02), 0.0016)
    mix.add(crack, gain=1.0)
    # Body: band-limited blast, saturated, centre varies per variant.
    centre = rng.uniform(900, 1600)
    body = filt(noise(rng, 0.12), "bandpass", [centre * 0.45, centre * 2.2]) * exp_env(seconds(0.12), 0.022)
    mix.add(np.tanh(3.0 * normalize(body)), gain=0.8)
    # Weight: short falling sine for the chest-hit.
    thump = sweep(0.14, rng.uniform(150, 175), rng.uniform(48, 58)) * exp_env(seconds(0.14), 0.05, 0.001)
    mix.add(thump, gain=0.55)
    # Tail: dark noise decay plus one outdoor slap echo off nearby structures.
    tail = filt(noise(rng, 0.4), "lowpass", 1400) * exp_env(seconds(0.4), 0.09)
    mix.add(tail, at=0.008, gain=0.22)
    slap_at = rng.uniform(0.07, 0.1)
    mix.add(filt(body, "lowpass", 2200), at=slap_at, gain=0.18)
    # Mechanical cycle: two small metal clicks after the shot (bolt back, bolt home).
    bolt = source("KenneyImpact", f"impactMetal_light_00{variant % 5}")
    bolt = filt(trim(pitch(bolt, rng.uniform(7, 9)), 0.06), "highpass", 1800)
    mix.add(bolt, at=rng.uniform(0.17, 0.19), gain=0.16)
    mix.add(trim(pitch(bolt, 2), 0.05), at=rng.uniform(0.25, 0.27), gain=0.12)
    return master(mix.buf, RIFLE_BUS)


def ranged_impact(variant: int) -> np.ndarray:
    rng = np.random.default_rng(1200 + variant)
    mix = Mix(0.45)
    plate = source("KenneyImpact", f"impactPlate_light_00{(variant * 2) % 5}")
    mix.add(trim(pitch(plate, rng.uniform(-1, 2)), 0.25), gain=0.9)
    # Sparks: sparse bright grains over ~60 ms.
    for _ in range(rng.integers(6, 11)):
        grain = filt(noise(rng, 0.004), "highpass", 4500) * exp_env(seconds(0.004), 0.0012)
        mix.add(grain, at=rng.uniform(0.0, 0.06), gain=rng.uniform(0.2, 0.45))
    if variant < 2:
        # Ricochet whine: falling, slightly wobbling tone.
        whine = sweep(0.28, rng.uniform(3400, 4200), rng.uniform(1500, 1900), curve=2.0)
        whine *= 1 + 0.15 * np.sin(2 * np.pi * rng.uniform(25, 40) * np.arange(len(whine)) / SR)
        mix.add(trim(whine * exp_env(len(whine), 0.1, 0.006), 0.28, fade=0.04), at=0.012, gain=0.22)
    else:
        punch = source("KenneyImpact", "impactPunch_medium_001")
        mix.add(filt(trim(punch, 0.2), "lowpass", 2500), gain=0.6)
    return master(mix.buf, WORLD_BUS)


RADIO_BUS = Pedalboard([Distortion(drive_db=14)])


def ranged_death(variant: int) -> np.ndarray:
    rng = np.random.default_rng(1300 + variant)
    mix = Mix(1.1)
    # Radio static burst that cuts off abruptly: the operator stops keying the mic.
    static = source("KenneySciFi", f"computerNoise_00{variant % 4}")
    static = filt(static, "bandpass", [350, 3400])
    cut = rng.uniform(0.22, 0.32)
    static = trim(static, cut, fade=0.002) * exp_env(seconds(cut), 1.0, 0.004)
    static = RADIO_BUS(normalize(static).astype(np.float32), SR).astype(np.float64)
    mix.add(static, gain=0.28)
    # Squelch tail after the cut.
    squelch = filt(noise(rng, 0.03), "bandpass", [1500, 3000]) * exp_env(seconds(0.03), 0.008)
    mix.add(squelch, at=cut, gain=0.35)
    # Gear rattle as the antenna pack hits the ground.
    for i, name in enumerate(("impactMetal_medium_00", "impactPlate_light_00")):
        gear = source("KenneyImpact", f"{name}{(variant + i) % 5}")
        mix.add(trim(pitch(gear, rng.uniform(1, 4)), 0.25), at=0.16 + 0.07 * i + rng.uniform(0, 0.03), gain=0.35)
    # Body fall: soft heavy thud plus a low sine for weight.
    fall_at = rng.uniform(0.34, 0.42)
    thud = source("KenneyImpact", f"impactSoft_heavy_00{variant % 5}")
    mix.add(filt(trim(thud, 0.4), "lowpass", 2000), at=fall_at, gain=0.9)
    mix.add(sweep(0.25, 95, 42) * exp_env(seconds(0.25), 0.08, 0.002), at=fall_at, gain=0.35)
    return master(mix.buf, WORLD_BUS)


# ---------------------------------------------------------------- output

SOUNDS = [
    # (faction, role, event, render, variant count, integrated loudness target in LUFS)
    ("Human", "Ranged", "Fire", ranged_fire, 4, -20.0),
    ("Human", "Ranged", "Impact", ranged_impact, 3, -22.0),
    ("Human", "Ranged", "Death", ranged_death, 3, -20.0),
]


def main() -> None:
    PREVIEW.mkdir(parents=True, exist_ok=True)
    reels: dict[tuple[str, str], list[np.ndarray]] = {}
    written = 0
    for faction, role, event, render, count, target in SOUNDS:
        folder = OUT / faction / role
        folder.mkdir(parents=True, exist_ok=True)
        for variant in range(count):
            audio, peak_db = level(render(variant), target)
            path = folder / f"SW_{faction}_{role}_{event}_{variant + 1:02d}.wav"
            sf.write(path, audio, SR, subtype="PCM_24")
            reels.setdefault((faction, role), []).extend([audio, np.zeros(seconds(0.45))])
            written += 1
            print(f"{path.relative_to(ROOT)}  {len(audio) / SR:.2f}s  true peak {peak_db:.1f} dBFS")
    for (faction, role), parts in reels.items():
        sf.write(PREVIEW / f"{faction}_{role}.wav", np.concatenate(parts), SR, subtype="PCM_24")
    print(f"UNIT_AUDIO_RENDERED {written}")


if __name__ == "__main__":
    main()
