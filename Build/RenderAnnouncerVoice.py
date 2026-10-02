#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "==3.12.*"
# dependencies = ["piper-tts==1.3.0", "onnxruntime==1.30.0", "numpy==2.5.3", "scipy==1.18.1", "soundfile==0.14.0", "pedalboard==0.9.25", "pyloudnorm==0.2.0"]
# ///
"""Render the human dispatcher offline, then import through the existing audio tool."""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from urllib.request import urlopen

import numpy as np
from scipy.signal import resample_poly
from unit_audio.core import SR, Audio, filt, finish, sf

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "Build/Audio/announcer_lines.json"
MANIFEST = ROOT / "Build/Audio/announcer_voice.json"
CACHE = ROOT / "Saved/AudioSources/Piper"
OUTPUT = ROOT / "Art/Audio/Announcer"
TARGET_LUFS = -18.0


@dataclass(frozen=True)
class Line:
    id: str
    text: str


def load_lines(path: Path) -> list[Line]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list) or not data:
        raise ValueError("Announcer script must be a nonempty array")
    result: list[Line] = []
    seen: set[str] = set()
    for item in data:
        if not isinstance(item, dict) or set(item) != {"id", "text"}:
            raise ValueError("Each announcer line requires exactly id and text")
        identifier, text = item["id"], item["text"]
        if not isinstance(identifier, str) or not re.fullmatch(
            r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*", identifier
        ):
            raise ValueError(f"Unsafe announcer id: {identifier!r}")
        if identifier in seen:
            raise ValueError(f"Duplicate announcer id: {identifier}")
        if (
            not isinstance(text, str)
            or not text.strip()
            or "\n" in text
            or "\r" in text
        ):
            raise ValueError(f"Expected one nonempty spoken line: {identifier}")
        seen.add(identifier)
        result.append(Line(identifier, text))
    return result


def verified_file(url: str, digest: str, destination: Path) -> Path:
    """Never use a corrupt cache or an unverified download."""
    if destination.exists():
        with destination.open("rb") as source:
            actual = hashlib.file_digest(source, "sha256").hexdigest()
        if actual != digest:
            raise ValueError(f"Voice cache SHA-256 mismatch: {destination}")
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=destination.parent) as temporary:
        downloaded = Path(temporary) / destination.name
        with urlopen(url, timeout=60) as response, downloaded.open("wb") as target:
            shutil.copyfileobj(response, target)
        with downloaded.open("rb") as source:
            actual = hashlib.file_digest(source, "sha256").hexdigest()
        if actual != digest:
            raise ValueError(f"Voice download SHA-256 mismatch: {url}")
        downloaded.replace(destination)
    return destination


def fetch_voice() -> Path:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    for name, digest in manifest["files"].items():
        verified_file(manifest["base_url"] + name, digest, CACHE / name)
    return CACHE / f"{manifest['name']}.onnx"


def finish_voice(samples: Audio, rate: int, destination: Path) -> tuple[float, float]:
    audio = resample_poly(samples, SR, rate)
    # Light radio bandwidth, without synthetic chimes, distortion or a long reverb tail.
    audio = filt(filt(audio, "highpass", 300.0), "lowpass", 3500.0)
    audio, lufs, peak = finish(audio, TARGET_LUFS)
    if abs(lufs - TARGET_LUFS) > 0.2 or peak > -1.0 + 1e-6:
        raise ValueError(f"Voice mix target missed: {lufs:.2f} LUFS, {peak:.2f} dBTP")
    sf.write(destination, audio, SR, subtype="PCM_24")
    return lufs, peak


def prune_wavs(lines: list[Line], output: Path) -> None:
    expected = {f"VO_{line.id}.wav" for line in lines}
    for source in output.glob("VO_*.wav"):
        if source.is_file() and source.name not in expected:
            source.unlink()
            print(f"ANNOUNCER_PRUNED {source.name}", flush=True)


def render_lines(lines: list[Line], model: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="announcer-") as temporary:
        raw = Path(temporary) / "raw.wav"
        for line in lines:
            # Zero generator and duration noise disables both stochastic model inputs.
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "piper",
                    "--model",
                    str(model),
                    "--output-file",
                    str(raw),
                    "--noise-scale",
                    "0",
                    "--noise-w-scale",
                    "0",
                    "--length-scale",
                    "1.05",
                    "--sentence-silence",
                    "0.12",
                    "--no-normalize",
                ],
                input=line.text + "\n",
                text=True,
                check=True,
            )
            samples, rate = sf.read(raw, dtype="float64", always_2d=True)
            destination = output / f"VO_{line.id}.wav"
            lufs, peak = finish_voice(samples[:, 0], rate, destination)
            print(
                f"ANNOUNCER_RENDERED {destination.name} lufs={lufs:.2f} peak={peak:.2f}",
                flush=True,
            )
    prune_wavs(lines, output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--render-only", action="store_true", help="Render without Unreal import"
    )
    args = parser.parse_args()
    lines = load_lines(SCRIPT)
    model = fetch_voice()
    render_lines(lines, model, OUTPUT)
    reel: list[Audio] = []
    for line in lines:
        audio, _ = sf.read(
            OUTPUT / f"VO_{line.id}.wav", dtype="float64", always_2d=True
        )
        reel.extend((audio[:, 0], np.zeros(SR // 2)))
    preview = ROOT / "Saved/AudioPreview/Announcer.wav"
    preview.parent.mkdir(parents=True, exist_ok=True)
    sf.write(preview, np.concatenate(reel), SR, subtype="PCM_24")
    print(f"ANNOUNCER_PREVIEW {preview}", flush=True)
    if not args.render_only:
        subprocess.run(
            [str(ROOT / "x"), "gen", "import-audio", "--", "-AnnouncerOnly"],
            cwd=ROOT,
            check=True,
        )


if __name__ == "__main__":
    main()
