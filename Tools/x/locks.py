"""Flock readers/writer turnstile plus a bounded headless process pool."""

from collections.abc import Iterator
from contextlib import ExitStack, contextmanager
from datetime import UTC, datetime
import fcntl
import json
import os
from pathlib import Path
import time
from typing import IO
import uuid

from x import jsonio
from x.runs import Run
from x.settings import Settings


class Locks:
    def __init__(
        self, repo: Path, settings: Settings, run: Run | None = None, command: str = ""
    ) -> None:
        self.root = settings.lock_dir
        self.pool_size = settings.headless_pool_size
        self.run = run
        self.identity = {
            "pid": os.getpid(),
            "run_id": run.id if run else "",
            "command": command,
            "worktree": str(repo),
        }

    def _holders(self, names: list[str]) -> str:
        holders: list[str] = []
        for name in names:
            for path in self.root.glob(f"{name}.*.holder.json"):
                try:
                    holder = jsonio.load(path)
                    os.kill(int(holder["pid"]), 0)
                except ProcessLookupError:
                    path.unlink(missing_ok=True)
                    continue
                except (OSError, ValueError, KeyError, json.JSONDecodeError):
                    continue
                holders.append(
                    f"pid {holder['pid']} run {holder['run_id']} {holder['command']} ({holder['worktree']})"
                )
        return "; ".join(holders) or "unknown holder (possibly external flock)"

    def _waiting(self, names: list[str]) -> None:
        print(f"waiting for {', '.join(names)}: {self._holders(names)}", flush=True)

    def _record_wait(self, name: str, started: float) -> None:
        if self.run is not None:
            self.run.add_lock_wait(name, time.monotonic() - started)

    @contextmanager
    def held(self, names: list[str], mode: int) -> Iterator[None]:
        """Acquire one named lock, reporting its holder and recording contention."""
        self.root.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        streams: list[tuple[str, IO[bytes]]] = []
        selected: tuple[str, IO[bytes]] | None = None
        holder: Path | None = None
        waiting = False
        try:
            for name in names:
                streams.append((name, (self.root / name).open("a+b")))
            while selected is None:
                for name, stream in streams:
                    try:
                        fcntl.flock(stream.fileno(), mode | fcntl.LOCK_NB)
                    except BlockingIOError:
                        continue
                    selected = (name, stream)
                    break
                if selected is None:
                    if not waiting:
                        self._waiting(names)
                        waiting = True
                    time.sleep(0.02)
            name, _ = selected
            holder = self.root / f"{name}.{os.getpid()}.{uuid.uuid4().hex}.holder.json"
            jsonio.save(
                holder,
                {**self.identity, "since": datetime.now(UTC).isoformat()},
            )
            if waiting:
                self._record_wait(name, started)
            yield
        finally:
            if holder is not None:
                holder.unlink(missing_ok=True)
            for _, stream in streams:
                stream.close()

    @contextmanager
    def headless(self) -> Iterator[None]:
        with ExitStack() as held:
            with self.held(["turnstile.lock"], fcntl.LOCK_EX):
                held.enter_context(self.held(["ue.lock"], fcntl.LOCK_SH))
            with self.held(
                [f"pool-{index}.lock" for index in range(self.pool_size)], fcntl.LOCK_EX
            ):
                yield

    @contextmanager
    def build(self) -> Iterator[None]:
        """Wait for UBT's shared state before consuming a headless pool slot."""
        with self.held(["build.lock"], fcntl.LOCK_EX), self.headless():
            yield

    @contextmanager
    def exclusive(self) -> Iterator[None]:
        with ExitStack() as held:
            with self.held(["turnstile.lock"], fcntl.LOCK_EX):
                held.enter_context(self.held(["ue.lock"], fcntl.LOCK_EX))
            yield
