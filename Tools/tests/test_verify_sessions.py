"""Real holder locks survive invocation exit and release only with owned games."""

from collections.abc import Callable
from dataclasses import replace
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import time

import pytest
from x import jsonio
from x.context import Context
from x.runs import Run
from x.settings import load
from x.verifying.holder import identity
from x.verifying.sessions import lease, manifest


def eventually(predicate: Callable[[], bool]) -> None:
    deadline = time.monotonic() + 8
    while not predicate():
        if time.monotonic() > deadline:
            raise AssertionError("holder lifecycle did not converge")
        time.sleep(0.02)


def blocked(path: Path) -> bool:
    with path.open("a+b") as stream:
        try:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return True
    return False


def context(repo: Path, root: Path, action: str) -> Context:
    settings = replace(load(repo), lock_dir=root / "locks", runs_root=root / "runs")
    record = Run(repo, settings.runs_root, "verify", ["verify", "native", action])
    return Context(repo, settings, record, "verify")


@pytest.mark.parametrize("ending", ["stop", "exit"])
def test_lock_survives_launch_and_actions_until_game_ends(
    repo: Path, tmp_path: Path, ending: str
) -> None:
    ctx = context(repo, tmp_path, "launch")
    assert ctx.run is not None
    folder = ctx.run.dir / "harness"
    game = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    holder_pid = 0
    try:
        with lease(ctx, "native", "launch", folder) as session:
            session.mkdir()
            eventually(lambda: identity(game.pid) is not None)
            jsonio.save(
                session / "session.json",
                {"pid": game.pid, "identity": identity(game.pid)},
            )
        holder_pid = int(jsonio.load(manifest(ctx, "native"))["pid"])
        ctx.run.finish(0)
        assert blocked(ctx.settings.lock_dir / "ue.lock")
        with lease(
            context(repo, tmp_path, "capture"), "native", "capture", folder
        ) as session:
            assert session == folder
            assert blocked(ctx.settings.lock_dir / "ue.lock")
        assert blocked(ctx.settings.lock_dir / "ue.lock")
        if ending == "stop":
            with lease(context(repo, tmp_path, "stop"), "native", "stop", folder):
                game.terminate()
                game.wait(timeout=8)
        else:
            game.terminate()
            game.wait(timeout=8)
        eventually(lambda: not blocked(ctx.settings.lock_dir / "ue.lock"))
        eventually(lambda: identity(holder_pid) is None)
    finally:
        if game.poll() is None:
            game.terminate()
            game.wait(timeout=8)
        if holder_pid:
            os.waitpid(holder_pid, 0)


def test_failed_launch_does_not_leave_exclusive_lock(
    repo: Path, tmp_path: Path
) -> None:
    ctx = context(repo, tmp_path, "launch")
    assert ctx.run is not None
    with (
        pytest.raises(RuntimeError, match="launch failure"),
        lease(ctx, "native", "launch", ctx.run.dir / "harness"),
    ):
        raise RuntimeError("launch failure")
    pid = int(jsonio.load(manifest(ctx, "native"))["pid"])
    eventually(lambda: identity(pid) is None)
    assert not blocked(ctx.settings.lock_dir / "ue.lock")
    os.waitpid(pid, 0)


def test_action_without_session_reports_launch_prerequisite(
    repo: Path, tmp_path: Path
) -> None:
    ctx = context(repo, tmp_path, "capture")
    with (
        pytest.raises(
            RuntimeError,
            match=r"no active native session; run ./x verify native launch",
        ),
        lease(ctx, "native", "capture", tmp_path / "unused"),
    ):
        raise AssertionError("missing session should not admit an action")


def test_holder_exit_does_not_mask_harness_failure(repo: Path, tmp_path: Path) -> None:
    from x.verifying.cleanup import stop_owned

    ctx = context(repo, tmp_path, "launch")
    assert ctx.run is not None
    pid = 0
    try:
        with (
            pytest.raises(RuntimeError, match="original harness failure"),
            lease(ctx, "native", "launch", ctx.run.dir / "harness"),
        ):
            record = jsonio.load(manifest(ctx, "native"))
            pid = int(record["pid"])
            stop_owned(record)
            raise RuntimeError("original harness failure")
    finally:
        if pid:
            os.waitpid(pid, 0)
