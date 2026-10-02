"""A surviving-group probe must fail the recorded exec, not escape as RuntimeError."""

import os
from pathlib import Path
import sys

import pytest
from test_lint_run import context
from x import jsonio


@pytest.mark.parametrize("mode", ["stall", "interrupt"])
def test_cleanup_timeout_retains_exec_failure(
    repo: Path, monkeypatch: pytest.MonkeyPatch, mode: str
) -> None:
    ctx = context(repo)
    assert ctx.run is not None
    original_killpg = os.killpg

    def surviving_probe(pgid: int, signal: int) -> None:
        if signal != 0:
            original_killpg(pgid, signal)

    monkeypatch.setattr(os, "killpg", surviving_probe)
    pid_path = repo / "child.pid"
    code = "import os,signal,sys,time; from pathlib import Path; "
    code += "Path(sys.argv[1]).write_text(str(os.getpid())); "
    if mode == "interrupt":
        code += "os.kill(int(sys.argv[2]),signal.SIGINT); "
    code += "time.sleep(30)"
    argv = [sys.executable, "-c", code, str(pid_path), str(os.getpid())]
    if mode == "interrupt":
        with pytest.raises(KeyboardInterrupt):
            ctx.exec(argv, log="cleanup", stall_seconds=0.2)
    else:
        assert ctx.exec(argv, log="cleanup", stall_seconds=0.2) == 1
    with pytest.raises(ProcessLookupError):
        original_killpg(int(pid_path.read_text()), 0)
    failure = ctx.run.record["execs"][0]
    assert failure["exit_code"] == 1
    assert failure["stalled"] is (mode == "stall")
    assert "process-group cleanup failed:" in (ctx.run.dir / "cleanup.log").read_text()
    assert "survived SIGKILL for 2 seconds" in (ctx.run.dir / "cleanup.log").read_text()
    assert ctx.run.finish(1, interrupted=mode == "interrupt") == 1
    assert jsonio.load(ctx.run.dir / "record.json")["status"] == (
        "stalled" if mode == "stall" else "interrupted"
    )
