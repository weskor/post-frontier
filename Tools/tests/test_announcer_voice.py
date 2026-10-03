"""The script is safe to import; voice finishing preserves the audible asset contract."""

from collections.abc import Callable
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from typing import cast
import wave

import numpy as np
import pytest
from RenderAnnouncerVoice import (
    Line,
    finish_voice,
    load_lines,
    prune_wavs,
    verified_file,
)
from unit_audio.core import loudness, sf, true_peak


@pytest.mark.parametrize(
    "data",
    [
        {},
        [],
        [{"id": "region_lost"}],
        [{"id": "../escape", "text": "Lost."}],
        [{"id": "UPPER", "text": "Lost."}],
        [{"id": "a__b", "text": "Lost."}],
        [{"id": 3, "text": "Lost."}],
        [{"id": "region_lost", "text": "   "}],
        [{"id": "region_lost", "text": "One\nTwo"}],
        [{"id": "region_lost", "text": 3}],
        [{"id": "region_lost", "text": "Lost.", "extra": True}],
        [
            {"id": "region_lost", "text": "Lost."},
            {"id": "region_lost", "text": "Again."},
        ],
    ],
)
def test_rejects_unsafe_script(data: object, tmp_path: Path) -> None:
    script = tmp_path / "lines.json"
    script.write_text(json.dumps(data))
    with pytest.raises(ValueError):
        load_lines(script)


def test_new_line_requires_no_event_enum(tmp_path: Path) -> None:
    script = tmp_path / "lines.json"
    script.write_text('[{"id":"new_event_2", "text":"New event."}]')
    assert load_lines(script) == [Line("new_event_2", "New event.")]


def test_verified_download_and_offline_cache(tmp_path: Path) -> None:
    source = tmp_path / "source"
    source.write_bytes(b"model bytes")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    cache = tmp_path / "cache/model"
    verified_file(source.as_uri(), digest, cache)
    source.unlink()
    assert verified_file(source.as_uri(), digest, cache).read_bytes() == b"model bytes"
    cache.write_bytes(b"corrupted")
    with pytest.raises(ValueError, match="cache SHA-256 mismatch"):
        verified_file(source.as_uri(), digest, cache)


def test_rejects_changed_model_before_caching(tmp_path: Path) -> None:
    source = tmp_path / "source"
    source.write_bytes(b"different model")
    cache = tmp_path / "cache/model"
    with pytest.raises(ValueError, match="download SHA-256 mismatch"):
        verified_file(source.as_uri(), "0" * 64, cache)
    assert not cache.exists()


def test_voice_finish_determinism_and_loudness(tmp_path: Path) -> None:
    # Varying amplitude and impulses exercise the limiter, not just constant gain.
    time = np.arange(22050 * 2, dtype=np.float64) / 22050
    samples = (
        0.03
        * np.sin(2 * np.pi * 500 * time)
        * (0.6 + 0.4 * np.sin(2 * np.pi * 3 * time))
    )
    samples[500::4000] = 0.9
    first, second = tmp_path / "first.wav", tmp_path / "second.wav"
    finish_voice(samples, 22050, first)
    finish_voice(samples, 22050, second)
    assert first.read_bytes() == second.read_bytes()
    with wave.open(str(first)) as result:
        assert (
            result.getframerate(),
            result.getsampwidth(),
            result.getnchannels(),
        ) == (48000, 3, 1)
    audio, _ = sf.read(first, dtype="float64", always_2d=True)
    assert loudness(audio[:, 0]) == pytest.approx(-18.0, abs=0.2)
    assert 20 * np.log10(true_peak(audio[:, 0])) <= -1.0 + 0.001


def test_script_has_generated_wave_and_imported_asset() -> None:
    root = Path(__file__).resolve().parents[2]
    for line in load_lines(root / "Build/Audio/announcer_lines.json"):
        name = f"VO_{line.id}"
        assert (root / "Art/Audio/Announcer" / f"{name}.wav").is_file(), name
        assert (root / "Content/Audio/Announcer" / f"{name}.uasset").is_file(), name


def test_pruning_removes_only_unlisted_voice_wavs(tmp_path: Path) -> None:
    for name in ("VO_kept.wav", "VO_removed.wav", "SW_unrelated.wav", "VO_kept.txt"):
        (tmp_path / name).write_bytes(b"preserved bytes")
    prune_wavs([Line("kept", "Kept.")], tmp_path)
    assert not (tmp_path / "VO_removed.wav").exists()
    for name in ("VO_kept.wav", "SW_unrelated.wav", "VO_kept.txt"):
        assert (tmp_path / name).read_bytes() == b"preserved bytes"


@pytest.fixture
def source_matches(
    monkeypatch: pytest.MonkeyPatch,
) -> Callable[[Path, str | None], bool]:
    """Import the real digest policy without invoking the Unreal entry point."""
    unreal = SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: None),
        EditorAssetLibrary=SimpleNamespace(),
    )
    monkeypatch.setitem(sys.modules, "unreal", unreal)
    script = Path(__file__).resolve().parents[2] / "Build/ImportAudio.py"
    spec = importlib.util.spec_from_file_location("test_import_audio", script)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return cast(
        Callable[[Path, str | None], bool], module.__dict__["announcer_source_matches"]
    )


def test_announcer_digest_ignores_source_location_and_timestamp(
    tmp_path: Path, source_matches: Callable[[Path, str | None], bool]
) -> None:
    source = tmp_path / "relocated.wav"
    source.write_bytes(b"voice bytes")
    metadata = json.dumps(
        [
            {
                "RelativeFilename": "another/worktree/voice.wav",
                "Timestamp": "9999999999",
                "FileMD5": hashlib.md5(source.read_bytes()).hexdigest().upper(),
            }
        ]
    )
    assert source_matches(source, metadata)


def test_announcer_digest_detects_same_size_same_timestamp_change(
    tmp_path: Path, source_matches: Callable[[Path, str | None], bool]
) -> None:
    source = tmp_path / "voice.wav"
    source.write_bytes(b"voice one")
    metadata = json.dumps([{"FileMD5": hashlib.md5(source.read_bytes()).hexdigest()}])
    before = source.stat()
    source.write_bytes(b"voice two")
    os.utime(source, ns=(before.st_atime_ns, before.st_mtime_ns))
    assert not source_matches(source, metadata)


@pytest.mark.parametrize(
    "metadata",
    [
        None,
        "",
        "not json",
        "null",
        "[]",
        "{}",
        "[null]",
        "[{}]",
        '[{"FileMD5": 12}]',
        '[{"FileMD5": "broken"}]',
        '[{"FileMD5": "gggggggggggggggggggggggggggggggg"}]',
        '[{"FileMD5": "00000000000000000000000000000000"}]',
        '[{"FileMD5": "00000000000000000000000000000000"}, {}]',
    ],
)
def test_announcer_uncertain_digest_requires_import(
    tmp_path: Path,
    source_matches: Callable[[Path, str | None], bool],
    metadata: str | None,
) -> None:
    source = tmp_path / "voice.wav"
    source.write_bytes(b"voice bytes")
    assert not source_matches(source, metadata)
