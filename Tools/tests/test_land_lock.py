"""Queued landings identify their holder and retain shared lock-wait evidence."""

import fcntl
import os
from pathlib import Path
import subprocess
import sys

from landing_support import commit_file, install_runner
from test_locks import eventually
from x import jsonio
from x.context import Context
from x.runs import Run
from x.settings import load


def test_contended_land_reports_holder_and_records_wait(repo: Path) -> None:
    task = install_runner(repo)
    commit_file(task, "Docs/task.md", "queued landing\n")
    settings = load(task)
    holder = Run(task, settings.runs_root, "hold-land", [])
    ctx = Context(task, settings, holder, "hold-land")
    log = repo.parent / "queued-land.log"
    env = {key: value for key, value in os.environ.items() if key != "X_LAND"}
    with log.open("wb") as stream, ctx.locks.held(["land.lock"], fcntl.LOCK_EX):
        child = subprocess.Popen(
            [sys.executable, str(task / "x"), "land"],
            cwd=task,
            env=env,
            stdout=stream,
            stderr=stream,
        )
        try:
            eventually(
                lambda: "waiting for land.lock:" in log.read_text(),
                "land did not report its queued holder",
            )
            assert holder.id in log.read_text() and "hold-land" in log.read_text()
            assert child.poll() is None
        except BaseException:
            child.kill()
            child.wait(timeout=10)
            raise
    try:
        assert child.wait(timeout=20) == 0, log.read_text()
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=10)
    holder.finish(0)
    records = (jsonio.load(path) for path in settings.runs_root.glob("*/record.json"))
    (record,) = [item for item in records if item["command"] == "land"]
    assert record["status"] == "passed"
    assert record["lock_waits"][0]["lock"] == "land.lock"
    assert record["lock_waits"][0]["duration_s"] > 0
    assert not list(settings.lock_dir.glob("*.holder.json"))
