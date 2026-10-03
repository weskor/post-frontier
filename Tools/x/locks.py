"""Crash-safe FIFO admission, readers/writer exclusion and a bounded process pool."""

from collections.abc import Iterator
from contextlib import ExitStack, contextmanager
from datetime import UTC, datetime
import fcntl
import hashlib
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
        self.module_lock = (
            "module-"
            + hashlib.sha256(str(repo.resolve()).encode()).hexdigest()
            + ".lock"
        )
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
    def _queued(self, names: list[str]) -> Iterator[tuple[Path, Path]]:
        """Publish ordered tickets while flock, not PID reuse, proves liveness."""
        key = hashlib.sha256("\0".join(sorted(names)).encode()).hexdigest()
        queue = self.root / f"queue-{key}"
        queue.mkdir(parents=True, exist_ok=True)
        ticket: Path | None = None
        stream: IO[bytes] | None = None
        try:
            with (queue / "guard").open("a+b") as guard:
                fcntl.flock(guard, fcntl.LOCK_EX)
                number = 1 + max(
                    (int(path.name.split(".")[0]) for path in queue.glob("*.ticket")),
                    default=0,
                )
                ticket = queue / f"{number:020d}.{uuid.uuid4().hex}.ticket"
                stream = ticket.open("x+b")
                fcntl.flock(stream, fcntl.LOCK_EX)
            yield queue, ticket
        finally:
            if ticket is not None:
                ticket.unlink(missing_ok=True)
            if stream is not None:
                stream.close()

    def _first(self, queue: Path, ticket: Path) -> bool:
        with (queue / "guard").open("a+b") as guard:
            fcntl.flock(guard, fcntl.LOCK_EX)
            for candidate in sorted(queue.glob("*.ticket")):
                if candidate == ticket:
                    return True
                try:
                    with candidate.open("r+b") as stream:
                        try:
                            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
                        except BlockingIOError:
                            return False
                        candidate.unlink(missing_ok=True)
                except FileNotFoundError:
                    continue
        return False

    @contextmanager
    def held(self, names: list[str], mode: int, *, count: int = 1) -> Iterator[None]:
        """Acquire count locks atomically, in FIFO order for this resource group."""
        if not 1 <= count <= len(names):
            raise ValueError("lock count must fit the resource group")
        self.root.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        streams: list[tuple[str, IO[bytes]]] = []
        selected: list[tuple[str, IO[bytes]]] = []
        holders: list[Path] = []
        waiting = False
        try:
            for name in names:
                streams.append((name, (self.root / name).open("a+b")))
            with self._queued(names) as (queue, ticket):
                while len(selected) != count:
                    if self._first(queue, ticket):
                        for name, stream in streams:
                            try:
                                fcntl.flock(stream.fileno(), mode | fcntl.LOCK_NB)
                            except BlockingIOError:
                                continue
                            selected.append((name, stream))
                            if len(selected) == count:
                                break
                        if len(selected) != count:
                            for _, stream in selected:
                                fcntl.flock(stream, fcntl.LOCK_UN)
                            selected.clear()
                    if len(selected) != count:
                        if not waiting:
                            self._waiting(names)
                            waiting = True
                        time.sleep(0.02)
            for name, _ in selected:
                holder = (
                    self.root / f"{name}.{os.getpid()}.{uuid.uuid4().hex}.holder.json"
                )
                holders.append(holder)
                jsonio.save(
                    holder,
                    {**self.identity, "since": datetime.now(UTC).isoformat()},
                )
            if waiting:
                self._record_wait(", ".join(name for name, _ in selected), started)
            yield
        finally:
            for holder in holders:
                holder.unlink(missing_ok=True)
            for _, stream in streams:
                stream.close()

    @contextmanager
    def headless(self, peers: int = 1, *, label: str = "") -> Iterator[None]:
        if peers < 1:
            raise ValueError("headless peers must be positive")
        started = time.monotonic()
        with ExitStack() as held:
            # Keep admission until all slots are owned: later requests cannot pass.
            with self.held(["turnstile.lock"], fcntl.LOCK_EX):
                held.enter_context(self.held(["ue.lock"], fcntl.LOCK_SH))
                held.enter_context(
                    self.held(
                        [f"pool-{index}.lock" for index in range(self.pool_size)],
                        fcntl.LOCK_EX,
                        count=min(peers, self.pool_size),
                    )
                )
            if label:
                self._record_wait(label, started)
            yield

    @contextmanager
    def module(self) -> Iterator[None]:
        """Prevent a same-worktree editor build while a scope uses its module."""
        with self.held([self.module_lock], fcntl.LOCK_SH):
            yield

    @contextmanager
    def build(self) -> Iterator[None]:
        """Wait for UBT's shared state before consuming a headless pool slot."""
        with (
            self.held(["build.lock"], fcntl.LOCK_EX),
            self.held([self.module_lock], fcntl.LOCK_EX),
            self.headless(),
        ):
            yield

    @contextmanager
    def exclusive(self) -> Iterator[None]:
        with ExitStack() as held:
            with self.held(["turnstile.lock"], fcntl.LOCK_EX):
                held.enter_context(self.held(["ue.lock"], fcntl.LOCK_EX))
            yield
