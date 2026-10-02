#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "scipy", "soundfile", "pedalboard", "pyloudnorm"]
# ///
"""Render an original 72-BPM dark synth / low-horn listening sketch, not a game import.

    uv run Build/GenerateMusicSample.py

Writes Saved/AudioPreview/Dark_Horn_Sample_v2.wav and .mp3, plus an editable REAPER
session/stems under Saved/AudioMusic/DarkHorn_v2. The first audition is preserved.
No Unreal process or imported assets.
The boat horn is the existing licensed Sonniss recording used by Human Structure;
all musical notes, synth patches and percussion are original deterministic synthesis.
The existing source cache is required (Build/FetchAudioSources.py populates it).
Reruns regenerate stems but preserve an existing session's faders and hand edits.
"""
from pathlib import Path
import json
import shutil
import subprocess

import numpy as np
import pyloudnorm as pyln
import soundfile as sf
from pedalboard import Pedalboard, Reverb
from scipy.signal import correlate, find_peaks, resample_poly

from unit_audio.core import SR, dark, event, fades, filt, pitch, seconds

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "Saved" / "AudioMusic" / "DarkHorn_v2"
PREVIEW = ROOT / "Saved" / "AudioPreview"
SAMPLE_NAME = "Dark_Horn_Sample_v2"
BPM = 72
BEAT = 60.0 / BPM
BAR = 4 * BEAT
DURATION = 32 * BAR + 6.0
N = seconds(DURATION)
RNG = np.random.default_rng(720926)
HORN = ("Pole Position - Narrowboat Gardner 4LW 1960",
        "narrowboat_t23_var_sfx_horn_various_XY_RSM191.wav")
# Eight-bar harmonic blocks: Dm(add9), Bbmaj7, Gm(add9), A7.
CHORDS = ((50, 53, 57, 64), (46, 50, 53, 57), (43, 46, 50, 57), (45, 49, 52, 55))
ROOTS = (38, 34, 31, 33)
HORN_NOTES = ((8, 29), (16, 38), (24, 28))  # F1, D2, E1; darker fifths beneath the chords.


def hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def normalize(x):
    peak = np.max(np.abs(x))
    if peak <= 1e-10:
        raise RuntimeError("Silent composition layer")
    return x / peak


def place(stem, x, at, gain=1.0, pan=0.0):
    start = seconds(at)
    end = min(N, start + len(x))
    if end <= start:
        return
    x = x[:end - start]
    if x.ndim == 1:
        angle = (pan + 1.0) * np.pi / 4
        stem[start:end, 0] += x * (gain * np.cos(angle))
        stem[start:end, 1] += x * (gain * np.sin(angle))
    else:
        stem[start:end] += x * gain


def analog(note, duration, detune=0.0, harmonics=9):
    t = np.arange(seconds(duration), dtype=np.float64) / SR
    phase = 2 * np.pi * hz(note) * 2 ** (detune / 1200) * t
    out = np.zeros_like(t)
    for harmonic in range(1, harmonics + 1):
        out += np.sin(harmonic * phase + 0.19 * harmonic) / harmonic
    return out / 1.75


def space(stem, room, wet):
    return Pedalboard([Reverb(room_size=room, damping=0.66, wet_level=wet,
                             dry_level=0.85, width=1.0)])(stem.T.copy(), SR).T.copy()


def fundamental(x):
    # The original horn's stable body, excluding the breathy onset.
    body = x[seconds(0.35):seconds(0.85)]
    body = filt(body, "bandpass", (90, 1200), order=2)
    ac = correlate(body, body, mode="full", method="fft")[len(body) - 1:]
    lo, hi = int(SR / 800), int(SR / 90)
    peaks, _ = find_peaks(ac[lo:hi])
    if not len(peaks):
        raise RuntimeError("Could not find a periodic horn body to tune")
    peaks += lo
    strong = peaks[ac[peaks] >= 0.88 * np.max(ac[peaks])]
    return SR / strong[0]


def make_stems():
    stems = {}
    # Brief, quiet swells with long gaps instead of a continuous synth bed.
    pad = np.zeros((N, 2), dtype=np.float32)
    for bar in range(0, 32, 4):
        chord = CHORDS[bar // 8]
        duration = 4.0
        for voice, note in enumerate(chord):
            for channel, cents in ((0, -3.0 - voice), (1, 3.0 + voice)):
                tone = dark(analog(note, duration, cents, harmonics=4), 650)
                tone = fades(tone, 0.9, 2.5)
                start = seconds(bar * BAR)
                end = start + len(tone)
                pad[start:end, channel] += tone * 0.009
    stems["01 Atmosphere Swells"] = space(pad, 0.72, 0.15)

    bass = np.zeros((N, 2), dtype=np.float32)
    for bar in range(32):
        root = ROOTS[bar // 8]
        # Rounded, audible bass notes with real gaps; no bright saw-wave buzz.
        for step, length in ((0, 0.85), (1.25, 0.65), (2.5, 0.55), (3.5, 0.42)):
            note = root + (7 if step == 3.5 and bar % 4 == 3 else 0)
            duration = length * BEAT
            t = np.arange(seconds(duration)) / SR
            phase = 2 * np.pi * hz(note) * t
            body = (0.75 * np.sin(phase) + 0.28 * np.sin(2 * phase)
                    + 0.14 * np.sin(3 * phase) + 0.08 * np.sin(4 * phase))
            body += 0.14 * np.sin(phase / 2)
            body = dark(np.tanh(body * 1.5), 340) * np.exp(-t / 0.55)
            place(bass, fades(body, 0.008, 0.10), bar * BAR + step * BEAT, 0.32)
    stems["02 Bass Pulse"] = bass

    kick_stem = np.zeros((N, 2), dtype=np.float32)
    snare_stem = np.zeros((N, 2), dtype=np.float32)
    metal = np.zeros((N, 2), dtype=np.float32)
    for bar in range(2, 32):
        for beat in (0.0, 2.75 if bar % 2 == 0 else 3.5):
            t = np.arange(seconds(0.58)) / SR
            phase = 2 * np.pi * (39 * t + 92 * 0.027 * (1 - np.exp(-t / 0.027)))
            kick = np.sin(phase) * np.exp(-t / 0.16)
            kick += dark(RNG.normal(0, 1, len(t)), 4200) * np.exp(-t / 0.004) * 0.20
            place(kick_stem, fades(kick, 0.002, 0.06), bar * BAR + beat * BEAT, 0.24)
        t = np.arange(seconds(0.38)) / SR
        noise = filt(RNG.normal(0, 1, len(t)), "bandpass", (900, 8500), order=2)
        snare = noise * np.exp(-t / 0.075) * 0.62
        phase = 2 * np.pi * (155 * t + 40 * 0.022 * (1 - np.exp(-t / 0.022)))
        snare += np.sin(phase) * np.exp(-t / 0.10) * 0.48
        place(snare_stem, fades(snare, 0.001, 0.035), bar * BAR + 2 * BEAT, 0.23)
        for beat in (0, 1, 2, 3):
            t = np.arange(seconds(0.16)) / SR
            hiss = filt(RNG.normal(0, 1, len(t)), "highpass", 5200, order=2)
            ring = np.sin(2 * np.pi * 3780 * t) + 0.5 * np.sin(2 * np.pi * 5423 * t)
            hat = (hiss * 0.3 + ring * 0.15) * np.exp(-t / 0.032)
            place(metal, fades(hat, 0.002, 0.012), bar * BAR + beat * BEAT,
                  0.027 if beat % 2 else 0.019, -0.27 if beat % 2 else 0.27)
    stems["03 Kick"] = kick_stem
    stems["04 Snare"] = snare_stem
    stems["05 Metal and Air"] = space(metal, 0.55, 0.10)

    motif = np.zeros((N, 2), dtype=np.float32)
    for bar in range(14, 30, 4):
        root = ROOTS[bar // 8]
        third = 4 if bar // 8 in (1, 3) else 3
        for step, interval in ((0.0, 19), (2.5, 12 + third)):
            t = np.arange(seconds(1.2)) / SR
            tone = dark(analog(root + interval, 1.2, -3.0, harmonics=3), 900)
            tone *= np.exp(-t / 0.40)
            tone = fades(tone, 0.035, 0.30)
            at = bar * BAR + step * BEAT
            pan = -0.19 if step in (0.0, 3.0) else 0.19
            place(motif, tone, at, 0.025, pan)
            place(motif, tone, at + 0.75 * BEAT, 0.008, -pan)
    stems["06 Sparse Motif"] = space(motif, 0.70, 0.18)

    recorded = event(*HORN, length=2.0, skip=0.35)
    source_hz = fundamental(recorded)
    print(f"Recorded horn body fundamental: {source_hz:.1f} Hz", flush=True)
    horn = np.zeros((N, 2), dtype=np.float32)
    for bar, note in HORN_NOTES:
        shifted = pitch(recorded, 12 * np.log2(hz(note) / source_hz))
        shifted = shifted[:seconds(7.5)]
        shifted = normalize(filt(dark(shifted, 330), "highpass", 28, order=2))
        # A strong low fundamental prevents the recorded horn's upper harmonics
        # from making the lower pitch sound deceptively bright.
        t = np.arange(len(shifted)) / SR
        phase = 2 * np.pi * hz(note) * t
        brass = np.sin(phase) + 0.18 * np.sin(2 * phase) + 0.07 * np.sin(3 * phase)
        brass = dark(np.tanh(brass * 1.35), 300)
        body = dark(np.tanh(1.10 * (0.45 * shifted + 0.55 * brass)), 350)
        body *= 0.96 + 0.04 * np.sin(2 * np.pi * 0.7 * t)
        place(horn, fades(body, 0.90, 2.4), bar * BAR, 0.24)
    stems["07 Lower Horn"] = space(horn, 0.87, 0.30)

    # One shared arrangement envelope; leave the final reverb to dissolve, not an abrupt cut.
    envelope = np.ones(N, dtype=np.float32)
    envelope[:seconds(1.5)] = np.linspace(0, 1, seconds(1.5))
    tail = seconds(8)
    envelope[-tail:] = np.linspace(1, 0, tail) ** 1.3
    for stem in stems.values():
        stem *= envelope[:, None]
    return stems


def write_session(stems):
    session = WORK / (SAMPLE_NAME + ".rpp")
    if session.exists():
        return session
    lines = ['<REAPER_PROJECT 0.1 "7.79/linux-x86_64" 0', "  SAMPLERATE 48000 0 0",
             f"  TEMPO {BPM}", f'  RENDER_FILE "{WORK / "Render"}"',
             "  RENDER_PATTERN $region", "  RENDER_FMT 0 2 48000", "  RENDER_1X 0",
             "  RENDER_RANGE 3 0 0 0 1000", "  RENDER_STEMS 0", "  <RENDER_CFG",
             "    ZXZhdxgAAQ==", "  >", f'  MARKER 1 0 "{SAMPLE_NAME}" 1',
             f'  MARKER 1 {DURATION:.6f} "" 1']
    for name in stems:
        lines += ["  <TRACK", f'    NAME "{name}"', "    VOLPAN 1 0 -1 -1 1", "    <ITEM",
                  "      POSITION 0", f"      LENGTH {DURATION:.6f}", f'      NAME "{name}"',
                  "      <SOURCE WAVE", f'        FILE "Stems/{name}.wav"', "      >", "    >", "  >"]
    lines += [">"]
    session.write_text("\n".join(lines) + "\n")
    return session


def main():
    for binary in ("reaper", "ffmpeg"):
        if not shutil.which(binary):
            raise SystemExit(f"Required audio tool unavailable: {binary}")
    (WORK / "Stems").mkdir(parents=True, exist_ok=True)
    (WORK / "Render").mkdir(exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    stems = make_stems()
    for name, stem in stems.items():
        sf.write(WORK / "Stems" / (name + ".wav"), stem, SR, subtype="PCM_24")
    session = write_session(stems)
    # Use the same headless REAPER path as the existing audio pipeline.
    result = subprocess.run(["reaper", "-nosplash", "-new", "-renderproject", str(session)],
                            capture_output=True, text=True, timeout=300)
    if result.returncode:
        raise RuntimeError("REAPER render failed: " + result.stderr[-2000:])
    rendered = WORK / "Render" / (SAMPLE_NAME + ".wav")
    audio, rate = sf.read(rendered, dtype="float32", always_2d=True)
    if rate != SR or audio.shape[1] != 2 or not np.isfinite(audio).all():
        raise RuntimeError("Invalid rendered stereo audio")
    meter = pyln.Meter(SR)
    measured = meter.integrated_loudness(audio)
    true_peak = np.max(np.abs(resample_poly(audio, 4, 1, axis=0)))
    if not np.isfinite(measured) or true_peak <= 0:
        raise RuntimeError("Silent rendered music")
    gain = min(10 ** ((-19.0 - measured) / 20), 10 ** (-1.2 / 20) / true_peak)
    audio *= gain
    wav = PREVIEW / (SAMPLE_NAME + ".wav")
    mp3 = PREVIEW / (SAMPLE_NAME + ".mp3")
    sf.write(wav, audio, SR, subtype="PCM_24")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                    "-codec:a", "libmp3lame", "-b:a", "256k", str(mp3)], check=True)
    # Measure the actual saved PCM, not just the pre-write floating-point buffer.
    saved, rate = sf.read(wav, dtype="float32", always_2d=True)
    actual_peak = float(np.max(np.abs(resample_poly(saved, 4, 1, axis=0))))
    report = {"file": str(wav.relative_to(ROOT)), "bpm": BPM, "seconds": len(saved) / rate,
              "sample_rate": rate, "channels": saved.shape[1], "lufs": meter.integrated_loudness(saved),
              "true_peak_dbfs": float(20 * np.log10(actual_peak)),
              "horn_entries_seconds": [bar * BAR for bar, _ in HORN_NOTES],
              "horn_notes": ["F1", "D2", "E1"],
              "horn_fundamentals_hz": [hz(note) for _, note in HORN_NOTES],
              "session": str(session.relative_to(ROOT)),
              "source": "/".join(HORN), "seed": 720926}
    (PREVIEW / (SAMPLE_NAME + ".json")).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print(f"MUSIC_SAMPLE_RENDERED {wav.relative_to(ROOT)} {mp3.relative_to(ROOT)}", flush=True)


if __name__ == "__main__":
    main()
