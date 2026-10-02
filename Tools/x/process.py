"""Quiet child execution with progress-based stalling and process-group cleanup."""

from collections import deque
from dataclasses import dataclass
import os
from pathlib import Path
import signal
import subprocess
import time
from typing import Mapping


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
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    # The parent may exit while a descendant ignores TERM. Always kill the group.
    deadline = time.monotonic() + 0.5
    while time.monotonic() < deadline:
        child.poll()
        time.sleep(0.02)
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    child.wait()


def supervise(
    child: subprocess.Popen[bytes], watch: Path, stall_seconds: float
) -> tuple[int, bool]:
    previous = size(watch)
    progress = time.monotonic()
    while child.poll() is None:
        current = size(watch)
        if current > previous:
            progress = time.monotonic()
        previous = current
        if time.monotonic() - progress >= stall_seconds:
            kill_group(child)
            return 1, True
        time.sleep(min(0.05, stall_seconds / 4))
    return child.wait(), False


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
            try:
                code, stalled = supervise(child, watch or log, stall_seconds)
            except KeyboardInterrupt:
                kill_group(child)
                code, interrupted = 130, True
            except BaseException:
                kill_group(child)
                raise
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
