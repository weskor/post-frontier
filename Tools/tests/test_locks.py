"""Real-process lock admission, fairness, crash release and external flock."""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Callable, Iterator

import pytest

from x import jsonio

WORKER = """
from dataclasses import replace
from pathlib import Path
import sys
import time
from x.context import Context
from x.runs import Run
from x.settings import load
repo, root, token, mode, slots = sys.argv[1:]
repo, root = Path(repo), Path(root)
settings = replace(load(repo), lock_dir=root / "locks", runs_root=root / "runs", headless_pool_size=int(slots))
record = Run(repo, settings.runs_root, mode, [mode])
ctx = Context(repo, settings, record, mode)
with getattr(ctx.locks, mode)():
    (root / (token + ".entered")).write_text(record.id)
    while not (root / (token + ".release")).exists():
        time.sleep(0.01)
record.finish(0)
"""


def eventually(predicate: Callable[[], bool], explanation: str) -> None:
    deadline = time.monotonic() + 8
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError(explanation)
        time.sleep(0.01)


@dataclass
class Workers:
    repo: Path
    root: Path
    children: list[subprocess.Popen[bytes]] = field(default_factory=list)

    def start(self, token: str, mode: str, slots: int = 2) -> subprocess.Popen[bytes]:
        env = {**os.environ, "PYTHONPATH": str(Path(__file__).parents[1])}
        with (self.root / f"{token}.log").open("wb") as log:
            child = subprocess.Popen(
                [
                    sys.executable,
                    "-c",
                    WORKER,
                    str(self.repo),
                    str(self.root),
                    token,
                    mode,
                    str(slots),
                ],
                stdout=log,
                stderr=log,
                env=env,
            )
        self.children.append(child)
        return child

    def entered(self, token: str) -> bool:
        return (self.root / f"{token}.entered").exists()

    def await_entry(self, token: str) -> None:
        eventually(
            lambda: self.entered(token), f"{token} did not acquire: {self.log(token)}"
        )

    def log(self, token: str) -> str:
        return (self.root / f"{token}.log").read_text()

    def waiting(self, token: str, lock: str) -> None:
        eventually(
            lambda: f"waiting for {lock}" in self.log(token),
            f"{token} did not wait for {lock}",
        )
        assert not self.entered(token)

    def release(self, token: str, child: subprocess.Popen[bytes]) -> None:
        (self.root / f"{token}.release").touch()
        assert child.wait(timeout=8) == 0


@pytest.fixture
def workers(repo: Path, tmp_path: Path) -> Iterator[Workers]:
    pool = Workers(repo, tmp_path)
    yield pool
    for child in pool.children:
        if child.poll() is None:
            child.kill()
        child.wait(timeout=8)


def test_shared_coexist_and_pool_bounds(workers: Workers) -> None:
    first = workers.start("first", "headless")
    workers.await_entry("first")
    second = workers.start("second", "headless")
    workers.await_entry("second")
    third = workers.start("third", "headless")
    workers.waiting("third", "pool-0.lock, pool-1.lock")
    assert "pid" in workers.log("third") and "headless" in workers.log("third")
    workers.release("first", first)
    workers.await_entry("third")
    workers.release("second", second)
    workers.release("third", third)
    run_id = (workers.root / "third.entered").read_text()
    record = jsonio.load(workers.root / "runs" / run_id / "record.json")
    assert record["lock_waits"][0]["duration_s"] > 0
    assert not list((workers.root / "locks").glob("*.holder.json"))


def test_exclusive_waiter_blocks_new_readers(workers: Workers) -> None:
    reader = workers.start("reader", "headless")
    workers.await_entry("reader")
    writer = workers.start("writer", "exclusive")
    workers.waiting("writer", "ue.lock")
    later = workers.start("later", "headless")
    workers.waiting("later", "turnstile.lock")
    workers.release("reader", reader)
    workers.await_entry("writer")
    workers.waiting("later", "ue.lock")
    workers.release("writer", writer)
    workers.await_entry("later")
    workers.release("later", later)


def test_exclusive_excludes_readers_and_other_writer(workers: Workers) -> None:
    writer = workers.start("writer", "exclusive")
    workers.await_entry("writer")
    reader = workers.start("reader", "headless")
    workers.waiting("reader", "ue.lock")
    another = workers.start("another", "exclusive")
    workers.waiting("another", "turnstile.lock")
    workers.release("writer", writer)
    workers.await_entry("reader")
    workers.waiting("another", "ue.lock")
    workers.release("reader", reader)
    workers.await_entry("another")
    workers.release("another", another)


@pytest.mark.parametrize("mode", ["headless", "exclusive"])
def test_plain_flock_interoperates_both_directions(workers: Workers, mode: str) -> None:
    holder = workers.start("holder", mode)
    workers.await_entry("holder")
    lock = workers.root / "locks/ue.lock"
    assert subprocess.run(["flock", "-n", str(lock), "true"]).returncode == 1
    workers.release("holder", holder)
    with (workers.root / "external.log").open("wb") as log:
        external = subprocess.Popen(
            [
                "flock",
                str(lock),
                sys.executable,
                "-c",
                "from pathlib import Path; import sys,time; Path(sys.argv[1]).touch(); time.sleep(30)",
                str(workers.root / "external.entered"),
            ],
            stdout=log,
            stderr=log,
            start_new_session=True,
        )
    workers.children.append(external)
    eventually(
        lambda: (workers.root / "external.entered").exists(),
        "external flock did not enter",
    )
    blocked = workers.start("blocked", mode)
    workers.waiting("blocked", "ue.lock")
    assert "possibly external flock" in workers.log("blocked")
    os.killpg(external.pid, 9)
    external.wait(timeout=8)
    workers.await_entry("blocked")
    workers.release("blocked", blocked)


@pytest.mark.parametrize("mode", ["headless", "exclusive"])
def test_killed_holder_releases_and_dead_metadata_ignored(
    workers: Workers, mode: str
) -> None:
    holder = workers.start("killed", mode)
    workers.await_entry("killed")
    dead_metadata = next(
        (workers.root / "locks").glob(f"ue.lock.{holder.pid}.*.holder.json")
    )
    holder.kill()
    holder.wait(timeout=8)
    following = workers.start("following", "exclusive")
    workers.await_entry("following")
    reader = workers.start("reader", "headless")
    workers.waiting("reader", "ue.lock")
    assert f"pid {holder.pid} " not in workers.log("reader")
    assert not dead_metadata.exists()
    workers.release("following", following)
    workers.await_entry("reader")
    workers.release("reader", reader)
