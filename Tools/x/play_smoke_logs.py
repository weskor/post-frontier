"""Incremental engine evidence, without treating runner messages as engine logs."""

from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path
import re

_PREFIX = r"^(?:\[[^\]\r\n]*\])*"
_LOAD = re.compile(
    _PREFIX + r"LogLoad: (?:\w+: )?Took \d+(?:\.\d+)? seconds to LoadMap\(([^)]+)\)\s*$"
)
_EXIT = re.compile(_PREFIX + r"LogExit: (?:\w+: )?Exiting\.\s*$")
_CRASH = re.compile(
    r"Fatal error|Unhandled Exception|Unhandled exception|CrashReportClient|"
    r"Segmentation fault|Assertion failed|"
    r"Signal \d+ caught|Engine crash handling finished|LowLevelFatalError|=== Critical error:|"
    r"CommonLinuxCrashHandler|CommonUnixCrashHandler"
)


@dataclass
class LogReader:
    path: Path
    offset: int = field(init=False)
    pending: bytes = b""

    def __post_init__(self) -> None:
        self.offset = self.path.stat().st_size if self.path.exists() else 0

    def lines(self, *, final: bool = False) -> list[str]:
        try:
            with self.path.open("rb") as source:
                source.seek(self.offset)
                chunk = source.read(65536)
        except FileNotFoundError:
            chunk = b""
        self.offset += len(chunk)
        pieces = (self.pending + chunk).split(b"\n")
        self.pending = pieces.pop()
        if final and self.pending and not chunk:
            pieces.append(self.pending)
            self.pending = b""
        return [piece.decode(errors="replace").rstrip("\r") for piece in pieces]

    def remaining_chunks(self) -> int:
        try:
            remaining = max(0, self.path.stat().st_size - self.offset)
        except FileNotFoundError:
            return 0
        return (remaining + 65535) // 65536


def crash_snapshot(roots: Sequence[Path]) -> dict[str, tuple[int, int]]:
    snapshot: dict[str, tuple[int, int]] = {}
    for root in roots:
        for path in root.rglob("*"):
            try:
                if path.is_file():
                    info = path.stat()
                    snapshot[str(path)] = (info.st_mtime_ns, info.st_size)
            except FileNotFoundError:
                continue
    return snapshot


@dataclass
class Evidence:
    requested_map: str | None
    readers: list[LogReader]
    ready: bool = False
    shutdown: bool = False
    crashes: list[str] = field(default_factory=list)

    def poll(self, *, final: bool = False) -> None:
        for reader in self.readers:
            for line in reader.lines(final=final):
                match = _LOAD.fullmatch(line)
                if match is not None and match[1] == self.requested_map:
                    self.ready = True
                if _EXIT.fullmatch(line):
                    self.shutdown = True
                if _CRASH.search(line) and line not in self.crashes:
                    self.crashes.append(line)

    def finish(self) -> None:
        # Snapshot the drain bound: descendants cannot extend it by log growth.
        rounds = max((reader.remaining_chunks() for reader in self.readers), default=0)
        for _ in range(rounds):
            self.poll()
        self.poll(final=True)
