"""Execution logs, environment, watch progress, stalls and group interruption."""

from dataclasses import replace
import os
from pathlib import Path
import signal
import sys
import threading
import time

import pytest

from x import jsonio
from x.context import Context
from x.runs import Run
from x.settings import load


@pytest.fixture
def ctx(repo: Path) -> Context:
    settings = replace(
        load(repo),
        runs_root=repo.parent / "runs",
        lock_dir=repo.parent / "locks",
        stall_seconds=0.2,
    )
    return Context(
        repo, settings, Run(repo, settings.runs_root, "probe", ["probe"]), "probe"
    )


def test_success_environment_and_quiet_output(
    ctx: Context, capsys: pytest.CaptureFixture[str]
) -> None:
    assert ctx.run is not None
    code = ctx.exec(
        [
            sys.executable,
            "-c",
            'import os; print("child-only"); print(os.environ["X_RUN_ID"]); '
            'print(os.environ["X_RUN_DIR"]); print(os.environ["UE_ROOT"]); print(os.environ["CUSTOM"])',
        ],
        log="environment",
        env={"CUSTOM": "value", "X_RUN_ID": "must-not-override"},
        stall_seconds=2,
    )
    assert code == 0
    console = capsys.readouterr().out
    assert "child-only\n" not in console
    assert "start" in console and "end exit 0" in console
    lines = (ctx.run.dir / "environment.log").read_text().splitlines()
    assert lines == [
        "child-only",
        ctx.run.id,
        str(ctx.run.dir),
        str(ctx.settings.engine_root),
        "value",
    ]
    assert ctx.run.record["execs"][0]["exit_code"] == 0


def test_failure_prints_last_forty_lines(
    ctx: Context, capsys: pytest.CaptureFixture[str]
) -> None:
    assert (
        ctx.exec(
            [
                sys.executable,
                "-c",
                'import sys; print("\\n".join(f"row:{i}" for i in range(60))); sys.exit(7)',
            ],
            log="failed",
            stall_seconds=2,
        )
        == 7
    )
    output = capsys.readouterr().out
    assert "row:19\n" not in output and "row:20\n" in output and "row:59\n" in output
    assert ctx.run is not None
    assert ctx.run.finish(0) == 1
    assert jsonio.load(ctx.run.dir / "record.json")["status"] == "failed"


def test_missing_executable_is_recorded(ctx: Context) -> None:
    assert ctx.exec(["/nonexistent/cooprts-test-command"], log="missing") == 1
    assert ctx.run is not None
    assert ctx.run.record["execs"][0]["exit_code"] == 1


def test_watch_growth_prevents_stall_and_console_growth_does_not(ctx: Context) -> None:
    watch = ctx.repo / "progress"
    code = (
        "from pathlib import Path; import sys,time; p=Path(sys.argv[1]); "
        '[(p.open("a").write("progress\\n"), time.sleep(0.08)) for _ in range(8)]'
    )
    assert (
        ctx.exec(
            [sys.executable, "-c", code, watch],
            log="progress",
            watch=Path("progress"),
            stall_seconds=0.3,
        )
        == 0
    )
    (ctx.repo / "quiet").touch()
    code = 'import time; [(print("no watch progress", flush=True), time.sleep(0.05)) for _ in range(100)]'
    assert (
        ctx.exec(
            [sys.executable, "-c", code], log="no-progress", watch=ctx.repo / "quiet"
        )
        != 0
    )
    assert ctx.run is not None
    assert ctx.run.finish(0) == 1
    assert jsonio.load(ctx.run.dir / "record.json")["status"] == "stalled"


def alive(pid: int) -> bool:
    try:
        return Path(f"/proc/{pid}/stat").read_text().split(") ")[1].split()[0] != "Z"
    except FileNotFoundError:
        return False


def test_stall_kills_descendants_ignoring_term(ctx: Context) -> None:
    pid_path = ctx.repo / "descendant.pid"
    descendant = "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(30)"
    parent = 'import subprocess,sys,time; from pathlib import Path; child=subprocess.Popen([sys.executable,"-c",sys.argv[2]]); '
    parent += 'Path(sys.argv[1]).write_text(str(child.pid)); print("spawned",flush=True); time.sleep(30)'
    assert (
        ctx.exec(
            [sys.executable, "-c", parent, pid_path, descendant],
            log="stall",
            stall_seconds=0.5,
        )
        != 0
    )
    pid = int(pid_path.read_text())
    try:
        assert not alive(pid)
        assert ctx.run is not None and ctx.run.record["execs"][0]["stalled"]
    finally:
        if alive(pid):
            os.kill(pid, signal.SIGKILL)


def test_ctrl_c_kills_child_group_and_records_interrupt(ctx: Context) -> None:
    pid_path = ctx.repo / "interrupt.pid"
    code = "import os,sys,time; from pathlib import Path; Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(30)"

    def interrupt() -> None:
        deadline = time.monotonic() + 5
        while not pid_path.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        os.kill(os.getpid(), signal.SIGINT)

    thread = threading.Thread(target=interrupt)
    thread.start()
    try:
        with pytest.raises(KeyboardInterrupt):
            ctx.exec(
                [sys.executable, "-c", code, pid_path],
                log="interrupt",
                stall_seconds=10,
            )
    finally:
        thread.join()
    assert not alive(int(pid_path.read_text()))
    assert ctx.run is not None
    assert ctx.run.record["execs"][0]["exit_code"] == 130
    assert ctx.run.finish(130, interrupted=True) == 130
    assert jsonio.load(ctx.run.dir / "record.json")["status"] == "interrupted"
