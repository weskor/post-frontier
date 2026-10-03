"""Repeated log metadata must not hide stalls or erase meaningful state changes."""

import os
from pathlib import Path
import sys

import pytest
from x.process import LogProgress, execute, progress_line


def append(path: Path, data: bytes) -> None:
    with path.open("ab") as stream:
        stream.write(data)


def test_repeating_child_is_killed(tmp_path: Path) -> None:
    code = (
        "import time; "
        "[(print(f'[2026.10.03-00.00.00:{i:03}][{i:3}] "
        "Pending host observe response ({i}.0s): "
        "response id={i} construction=0.25', flush=True), time.sleep(.04)) "
        "for i in range(100)]"
    )
    outcome = execute(
        [sys.executable, "-c", code],
        tmp_path / "repeating.log",
        tmp_path,
        os.environ,
        None,
        0.4,
    )
    assert outcome.stalled
    assert outcome.exit_code != 0


def test_numeric_progress_child_survives(tmp_path: Path) -> None:
    code = (
        "import time; "
        "[(print(f'[2026.10.03-00.00.00:{i:03}][{i:3}] "
        "Pending host observe response ({i}.0s): "
        "response id={i} construction={i / 20}', flush=True), time.sleep(.04)) "
        "for i in range(20)]"
    )
    outcome = execute(
        [sys.executable, "-c", code],
        tmp_path / "progress.log",
        tmp_path,
        os.environ,
        None,
        0.4,
    )
    assert outcome.exit_code == 0
    assert not outcome.stalled


@pytest.mark.parametrize(
    ("before", "after"),
    [
        (
            b"[2026.10.03-00.00.00:001][  1] Pending host observe response (0.2s): response id=1",
            b"[2026.10.03-01.02.03:777][999] Pending host observe response (7.8s): response id=100",
        ),
        (
            b"2026-10-03T00:00:00.001Z waiting poll=1 attempt:2 counter=3 retry=4",
            b"2026-10-03T00:00:09.002Z waiting poll=11 attempt:12 counter=13 retry=14",
        ),
    ],
)
def test_timestamps_and_poll_counters_repeat(before: bytes, after: bytes) -> None:
    assert progress_line(before) == progress_line(after)


@pytest.mark.parametrize(
    "field",
    [
        "construction",
        "generation",
        "armies",
        "health",
        "joined",
        "travelling",
        "result",
        "frame",
        "tick",
        "simulationSeconds",
        "id",
    ],
)
def test_semantic_numbers_are_progress(tmp_path: Path, field: str) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    for value in (1, 2):
        append(path, f'Pending state (1.0s): {{"{field}":{value}}}\n'.encode())
        assert detector.poll()
    append(path, f'Pending state (2.0s): {{"{field}":2}}\n'.encode())
    assert not detector.poll()


def test_repeats_interleaved_with_other_seen_lines_are_not_progress(
    tmp_path: Path,
) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    append(path, b"waiting for host\nwaiting for client\n")
    assert detector.poll()
    append(path, b"waiting for client\nwaiting for host\nwaiting for client\n")
    assert not detector.poll()
    assert not detector.poll()


def test_partial_repeats_wait_for_completion(tmp_path: Path) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    append(path, b"Pending state (0.0s): construction=0.25\n")
    assert detector.poll()
    for fragment in (b"Pending state (", b"5.0s): construction=", b"0.25", b"\r\n"):
        append(path, fragment)
        assert not detector.poll()
    append(path, b"Pending state (6.0s): construction=0.")
    assert not detector.poll()
    append(path, b"50\n")
    assert detector.poll()


def test_unterminated_dot_growth_keeps_child_alive(tmp_path: Path) -> None:
    code = (
        "import time; "
        "[(print('.', end='', flush=True), time.sleep(.04)) for _ in range(20)]"
    )
    outcome = execute(
        [sys.executable, "-c", code],
        tmp_path / "dots.log",
        tmp_path,
        os.environ,
        None,
        0.4,
    )
    assert outcome.exit_code == 0
    assert not outcome.stalled


@pytest.mark.parametrize("replace", [False, True])
def test_rewrite_does_not_reset_repeat_history(tmp_path: Path, replace: bool) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    append(path, b"state=1\nold fragment")
    assert detector.poll()
    # Include the old fragment in a first completed line so replacement cannot
    # invent progress by concatenating it with the next file's bytes.
    append(path, b"\n")
    assert detector.poll()
    if replace:
        path.rename(tmp_path / "previous.log")
    path.write_bytes(b"state=1\nold fragment\nstate=1\n")
    assert not detector.poll()
    append(path, b"state=2\n")
    assert detector.poll()


def test_short_truncation_and_missing_file_are_safe(tmp_path: Path) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    assert not detector.poll()
    append(path, b"state=1\nstate=2\npartial")
    assert detector.poll()
    path.write_bytes(b"state=1\n")
    assert not detector.poll()
    path.unlink()
    assert not detector.poll()
    append(path, b"state=2\n")
    assert not detector.poll()


def test_oversized_lines_allow_growth_then_resume_repeat_detection(
    tmp_path: Path,
) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    append(path, b"state=1\n")
    assert detector.poll()
    append(path, b"x" * (LogProgress.LINE_LIMIT + 1))
    assert detector.poll()
    append(path, b"x\nstate=1\n")
    assert detector.poll()
    append(path, b"state=1\n")
    assert not detector.poll()


def test_same_size_rewrite_with_unchanged_tail_reports_new_state(
    tmp_path: Path,
) -> None:
    path = tmp_path / "watched.log"
    detector = LogProgress(path)
    tail = b"unchanged log suffix " * 8 + b"\n"
    append(path, b"state=1\n" + tail)
    assert detector.poll()
    modified = path.stat().st_mtime_ns
    path.write_bytes(b"state=2\n" + tail)
    os.utime(path, ns=(modified + 1, modified + 1))
    assert detector.poll()
    assert not detector.poll()


def test_previous_command_bytes_are_not_current_progress(tmp_path: Path) -> None:
    path = tmp_path / "watched.log"
    path.write_bytes(b"previous command\n")
    detector = LogProgress(path)
    assert not detector.poll()
    append(path, b"current command\n")
    assert detector.poll()
