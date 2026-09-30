"""Shared building blocks for unit sound recipes and the render pipeline (Build/GenerateUnitAudio.py).

A recipe module (one per unit, e.g. human_ranged.py) defines:
    SOURCES: list[tuple[str, str]]  (bundle zip file name, member path inside the zip); fetched by
             Build/FetchAudioSources.py into Saved/AudioSources/Sonniss/<Library>/<File>
    UNIT:    Unit                   tracks (REAPER session layout) and events (one per output sound set)
"""
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

ROOT = Path(__file__).resolve().parents[2]
SOURCES_DIR = ROOT / "Saved" / "AudioSources" / "Sonniss"
LAYERS = ROOT / "Saved" / "AudioLayers"
RENDERS = ROOT / "Saved" / "AudioRender"
PREVIEW = ROOT / "Saved" / "AudioPreview"
SESSIONS = ROOT / "Art" / "Audio" / "Sessions"
OUT = ROOT / "Art" / "Audio"
SR = 48_000
TRUE_PEAK_CEILING_DBFS = -1.0
REGION_SPACING = 3.0  # seconds between region starts in a session


# ---------------------------------------------------------------- recipe model

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


# ---------------------------------------------------------------- source handling

def seconds(n: float) -> int:
    return int(round(n * SR))


def source_path(member: str) -> Path:
    """Cache location of a bundle member: Saved/AudioSources/Sonniss/<Library>/<File>."""
    library, name = member.split("/")[-2:]
    return SOURCES_DIR / library / name


_cache: dict[str, np.ndarray] = {}


def load(library: str, name: str) -> np.ndarray:
    """A cached source as 48 kHz mono float64."""
    key = f"{library}/{name}"
    if key not in _cache:
        path = SOURCES_DIR / library / name
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


def dark(x: np.ndarray, cutoff: float) -> np.ndarray:
    """Gentle 12 dB/octave roll-off: removes brightness without the boxy sound of a steep filter."""
    return filt(x, "lowpass", cutoff, order=2)


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


# ---------------------------------------------------------------- stage 1: prepare layers

def region_name(unit: Unit, event_name: str, variant: int) -> str:
    return f"SW_{unit.key}_{event_name}_{variant + 1:02d}"


def prepare(unit: Unit) -> list[tuple[str, float, float, Layer, Path]]:
    """Writes every layer clip; returns (region, region start, region length, layer, clip path)."""
    folder = LAYERS / unit.key
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    tracks = {name for name, _ in unit.tracks}
    placed, slot = [], 0
    for ev in unit.events:
        for v in range(ev.variants):
            region = region_name(unit, ev.name, v)
            for layer in ev.build(v):
                if layer.track not in tracks:
                    raise SystemExit(f"{unit.key}: layer track {layer.track!r} is not in UNIT.tracks")
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
    """Renders every region of the session with REAPER and writes the finished files and preview reel."""
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
            data, _rate = sf.read(rendered, dtype="float64", always_2d=True)
            audio, lufs, peak = finish(data.mean(axis=1), ev.loudness)
            sf.write(final / f"{name}.wav", audio, SR, subtype="PCM_24")
            reel += [audio, np.zeros(seconds(0.5))]
            written += 1
            print(f"{(final / name).relative_to(ROOT)}.wav  {len(audio) / SR:.2f}s  {lufs:.1f} LUFS  "
                  f"true peak {peak:.1f} dBFS")
    PREVIEW.mkdir(parents=True, exist_ok=True)
    sf.write(PREVIEW / f"{unit.key}.wav", np.concatenate(reel), SR, subtype="PCM_24")
    return written
