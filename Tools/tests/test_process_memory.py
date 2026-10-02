"""Resident memory evidence covers the entire exec process group."""

from dataclasses import replace
from pathlib import Path
import sys

from x import jsonio
from x.context import Context
from x.runs import Run
from x.settings import load


def test_exec_peak_sums_child_and_grandchild(repo: Path) -> None:
    settings = replace(
        load(repo), runs_root=repo.parent / "runs", lock_dir=repo.parent / "locks"
    )
    run = Run(repo, settings.runs_root, "memory", ["memory"])
    ctx = Context(repo, settings, run, "memory")
    script = repo / "memory.py"
    script.write_text(
        "import os, pathlib, subprocess, sys, time\n"
        "memory = bytearray(24 * 1024 * 1024)\n"
        "depth = int(sys.argv[1])\n"
        "child = subprocess.Popen([sys.executable, __file__, str(depth - 1)]) if depth else None\n"
        "pages = int(pathlib.Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[21])\n"
        "print(pages * os.sysconf('SC_PAGE_SIZE'), flush=True)\n"
        "if child is not None:\n"
        "    child.wait()\n"
        "else:\n"
        "    time.sleep(1)\n"
    )
    assert ctx.exec([sys.executable, script, "2"], log="memory") == 0
    expected_mb = sum(map(int, (run.dir / "memory.log").read_text().splitlines())) / (
        1024 * 1024
    )
    recorded = jsonio.load(run.dir / "record.json")["execs"][0]["peak_rss_mb"]
    # Summing only the leader, or only its immediate child, cannot meet this bound.
    assert expected_mb * 0.95 <= recorded <= expected_mb + 8
