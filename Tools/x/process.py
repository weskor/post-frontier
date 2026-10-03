"""Quiet child execution with progress-based stalling and process-group cleanup."""

from collections import deque
from collections.abc import Mapping
from contextlib import suppress
from dataclasses import dataclass
from hashlib import blake2b
import os
from pathlib import Path
import re
import signal
import subprocess
import threading
import time
from typing import IO

# SIGINT reaches only the main thread; it sets this so worker-thread children
# and lock waits stop as if interrupted themselves.
CANCELLED = threading.Event()


class ProcessGroupSurvived(RuntimeError):
    """A terminated child's group still exists after the cleanup bound."""


@dataclass(frozen=True)
class Outcome:
    exit_code: int
    duration_s: float
    peak_rss_mb: float
    stalled: bool = False
    interrupted: bool = False


_TIMESTAMP = re.compile(
    rb"^(?:\[\d{4}[.-]\d\d[.-]\d\d[- T]\d\d[.:]\d\d[.:]\d\d"
    rb"[.:]\d+\]\s*(?:\[\s*\d+\]\s*)?"
    rb"|\[?\d{4}-\d\d-\d\d[T ]\d\d:\d\d:\d\d(?:\.\d+)?Z?\]?\s+)"
)
_PENDING_TIME = re.compile(rb"^(Pending [^\r\n]*?) \(\d+(?:\.\d+)?s\):")
_COUNTER = re.compile(
    rb"(\b(?:response id|poll|attempt|retry|counter)\s*[=:]\s*)\d+\b",
    re.IGNORECASE,
)


def progress_line(line: bytes) -> bytes:
    """Remove log metadata, not state: numeric units, IDs and percentages remain.

    Unreal's leading timestamp/frame pair, ISO timestamps, harness Pending elapsed
    time, response sequence IDs and explicitly labelled polling counters are noise.
    Unlabelled numbers and semantic JSON fields (generation, construction, armies,
    etc.) are deliberately retained, as are simulation times and frame state.
    """
    line = _TIMESTAMP.sub(b"", line.rstrip(b"\r"), count=1)
    line = _PENDING_TIME.sub(rb"\1 (<elapsed>):", line, count=1)
    return _COUNTER.sub(rb"\1<count>", line)


class LogProgress:
    """Stream novel completed lines with bounded memory and per-poll work.

    Keep 16,384 recent line fingerprints, read at most 256 KiB per poll and buffer
    at most 64 KiB of an unfinished line. Repeats do not refresh progress, even
    across file replacement/truncation. Compare an EOF boundary to detect common
    truncate-and-regrow writes between polls; never join partial lines across them.
    Once newline output begins, partial writes wait for line completion rather
    than letting slowly streamed repeats refresh the watchdog. Pure unterminated
    output (e.g. progress dots) and oversized lines conservatively use byte growth.
    Fingerprint eviction and oversized lines favor liveness over repeat detection.
    """

    READ_LIMIT = 256 * 1024
    LINE_LIMIT = 64 * 1024
    HISTORY_LIMIT = 16_384

    def __init__(self, path: Path) -> None:
        self.path = path
        self.offset = 0
        self.identity: tuple[int, int] | None = None
        self.modified: int | None = None
        self.boundary = b""
        self.partial = b""
        self.oversized = False
        self.line_output = False
        self.seen: set[bytes] = set()
        self.order: deque[bytes] = deque()
        # Already-written bytes belong to a previous command, not this child's
        # progress. Still retain a small boundary to notice an immediate rewrite.
        try:
            with path.open("rb") as source:
                stat = os.fstat(source.fileno())
                self.identity = (stat.st_dev, stat.st_ino)
                self.modified = stat.st_mtime_ns
                self.offset = stat.st_size
                source.seek(max(0, self.offset - 64))
                self.boundary = source.read(min(64, self.offset))
        except FileNotFoundError:
            pass

    def _novel(self, line: bytes) -> bool:
        normalized = progress_line(line)
        if not normalized.strip():
            return False
        key = blake2b(normalized, digest_size=16).digest()
        if key in self.seen:
            return False
        if len(self.order) == self.HISTORY_LIMIT:
            self.seen.remove(self.order.popleft())
        self.order.append(key)
        self.seen.add(key)
        return True

    def _feed(self, data: bytes) -> bool:
        progress = False
        parts = data.split(b"\n")
        for part in parts[:-1]:
            self.line_output = True
            if self.oversized or len(self.partial) + len(part) > self.LINE_LIMIT:
                progress = True
            else:
                progress = self._novel(self.partial + part) or progress
            self.partial = b""
            self.oversized = False
        fragment = parts[-1]
        if self.oversized or len(self.partial) + len(fragment) > self.LINE_LIMIT:
            self.partial = b""
            self.oversized = True
            progress = bool(fragment) or progress
        else:
            self.partial += fragment
            if not self.line_output and fragment:
                progress = True
        return progress

    def poll(self) -> bool:
        try:
            with self.path.open("rb") as source:
                stat = os.fstat(source.fileno())
                identity = (stat.st_dev, stat.st_ino)
                rewritten = (
                    identity != self.identity
                    or stat.st_size < self.offset
                    or (
                        stat.st_size == self.offset
                        and stat.st_mtime_ns != self.modified
                    )
                )
                if not rewritten and self.boundary:
                    source.seek(self.offset - len(self.boundary))
                    rewritten = source.read(len(self.boundary)) != self.boundary
                if rewritten:
                    self.offset = 0
                    self.partial = b""
                    self.oversized = False
                self.identity = identity
                self.modified = stat.st_mtime_ns
                source.seek(self.offset)
                data = source.read(self.READ_LIMIT)
                self.offset += len(data)
                if not data:
                    return False
                self.boundary = (self.boundary + data)[-64:]
                if rewritten:
                    self.boundary = data[-64:]
                return self._feed(data)
        except FileNotFoundError:
            return False


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


def group_rss_bytes(pgid: int) -> int:
    """Sum the leader's descendants and process group, counting each PID once."""
    resident: dict[int, int] = {}
    children: dict[int, list[int]] = {}
    pending = [pgid]
    with os.scandir("/proc") as entries:
        for entry in entries:
            if not entry.name.isdecimal():
                continue
            try:
                # comm may contain spaces or ')'; fields after its final ')' are fixed.
                fields = (
                    (Path(entry.path) / "stat").read_text().rsplit(")", 1)[1].split()
                )
                pid = int(entry.name)
                resident[pid] = int(fields[21])
                children.setdefault(int(fields[1]), []).append(pid)
                if int(fields[2]) == pgid:
                    pending.append(pid)
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                # Processes can disappear between enumeration and reading stat.
                continue
    visited: set[int] = set()
    pages = 0
    while pending:
        pid = pending.pop()
        if pid in visited:
            continue
        visited.add(pid)
        pages += resident.get(pid, 0)
        pending.extend(children.get(pid, ()))
    return pages * os.sysconf("SC_PAGE_SIZE")


def supervise(
    child: subprocess.Popen[bytes], watch: Path, stall_seconds: float, log: IO[bytes]
) -> tuple[int, bool, bool, float]:
    stalled = interrupted = False
    peak_rss = 0
    sample_at = 0.0
    try:
        try:
            detector = LogProgress(watch)
            progress = time.monotonic()
            while child.poll() is None:
                if CANCELLED.is_set():
                    raise KeyboardInterrupt
                now = time.monotonic()
                if now >= sample_at:
                    peak_rss = max(peak_rss, group_rss_bytes(child.pid))
                    sample_at = now + 0.25
                if detector.poll():
                    progress = time.monotonic()
                if time.monotonic() - progress >= stall_seconds:
                    stalled = True
                    kill_group(child)
                    return 1, stalled, interrupted, peak_rss / (1024 * 1024)
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
    return code, stalled, interrupted, peak_rss / (1024 * 1024)


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
    peak_rss_mb = 0.0
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
            code, stalled, interrupted, peak_rss_mb = supervise(
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
    return Outcome(code, duration, peak_rss_mb, stalled, interrupted)
