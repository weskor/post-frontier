"""Quiet child execution with progress-based stalling and process-group cleanup."""

from collections import deque
from collections.abc import Mapping
from contextlib import suppress
from dataclasses import dataclass
import os
from pathlib import Path
import signal
import subprocess
import time
from typing import IO


class ProcessGroupSurvived(RuntimeError):
    """A terminated child's group still exists after the cleanup bound."""


@dataclass(frozen=True)
class Outcome:
    exit_code: int
    duration_s: float
    stalled: bool = False
    interrupted: bool = False


def size(path: Path) -> int:
    try:
        return path.stat().st_size
    except FileNotFoundError:
        return 0


def kill_group(child: subprocess.Popen[bytes]) -> None:
    """Reap the child and wait at most 2 s for post-SIGKILL group disappearance."""
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        child.wait()
        return
    # The parent may exit while a descendant ignores TERM. Always kill the group.
    deadline = time.monotonic() + 0.5
    while time.monotonic() < deadline:
        if child.poll() is not None:
            break
        time.sleep(0.02)
    with suppress(ProcessLookupError):
        os.killpg(child.pid, signal.SIGKILL)
    child.wait()
    # SIGKILL delivery is asynchronous; orphaned zombies must also leave the group.
    timeout = 2.0
    deadline = time.monotonic() + timeout
    while True:
        try:
            os.killpg(child.pid, 0)
        except ProcessLookupError:
            return
        if time.monotonic() >= deadline:
            raise ProcessGroupSurvived(
                f"process group {child.pid} survived SIGKILL for {timeout:g} seconds"
            )
        time.sleep(0.02)


def supervise(
    child: subprocess.Popen[bytes], watch: Path, stall_seconds: float, log: IO[bytes]
) -> tuple[int, bool, bool]:
    stalled = interrupted = False
    try:
        try:
            previous = size(watch)
            progress = time.monotonic()
            while child.poll() is None:
                current = size(watch)
                if current > previous:
                    progress = time.monotonic()
                previous = current
                if time.monotonic() - progress >= stall_seconds:
                    stalled = True
                    kill_group(child)
                    return 1, stalled, interrupted
                time.sleep(min(0.05, stall_seconds / 4))
            code = child.wait()
        except KeyboardInterrupt:
            interrupted = True
            kill_group(child)
            code = 130
        except ProcessGroupSurvived:
            raise
        except BaseException:
            kill_group(child)
            raise
    except ProcessGroupSurvived as error:
        log.write(f"process-group cleanup failed: {error}\n".encode())
        code = 1
    return code, stalled, interrupted


def execute(
    argv: list[str],
    log: Path,
    cwd: Path,
    env: Mapping[str, str],
    watch: Path | None,
    stall_seconds: float,
) -> Outcome:
    if stall_seconds <= 0:
        raise ValueError("stall_seconds must be positive")
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    print(f"start {' '.join(argv)}; log {log}", flush=True)
    interrupted = False
    stalled = False
    with log.open("wb") as stream:
        try:
            child = subprocess.Popen(
                argv,
                cwd=cwd,
                env=env,
                stdout=stream,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        except OSError as error:
            stream.write(f"{error}\n".encode())
            code = 1
        else:
            code, stalled, interrupted = supervise(
                child, watch or log, stall_seconds, stream
            )
    duration = time.monotonic() - started
    print(
        f"end exit {code}, {duration:.2f}s, log {log}"
        + (" (stalled)" if stalled else ""),
        flush=True,
    )
    if code:
        with log.open(errors="replace") as source:
            print("".join(deque(source, maxlen=40)), end="")
    return Outcome(code, duration, stalled, interrupted)
