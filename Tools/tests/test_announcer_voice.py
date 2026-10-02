"""The script is safe to import; voice finishing preserves the audible asset contract."""

import hashlib
import json
from pathlib import Path
import wave

import numpy as np
import pytest
from RenderAnnouncerVoice import Line, finish_voice, load_lines, verified_file
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
