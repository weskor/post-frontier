"""Builds serialize shared UBT state without bypassing headless admission."""

from dataclasses import replace
import os
from pathlib import Path
import select
import subprocess
import sys

import pytest
from x.building import editor_module
from x.context import Context
from x.settings import load

CHILD = """
from dataclasses import replace
from pathlib import Path
import sys
from x.building import ensure_editor
from x.context import Context
from x.runs import Run
from x.settings import load
repo, root, mode = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
settings = replace(load(repo), lock_dir=root, headless_pool_size=2,
                   engine_root=root / 'missing-engine', runs_root=root.parent / 'runs')
ctx = Context(repo, settings, Run(repo, settings.runs_root, 'probe', ['probe']))
print('ready', flush=True)
if mode == 'ensure':
    sys.exit(0 if ensure_editor(ctx) else 1)
with getattr(ctx.locks, mode)():
    print('entered', flush=True)
    sys.stdin.readline()
"""


def start(repo: Path, root: Path, mode: str) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        [sys.executable, "-c", CHILD, str(repo), str(root), mode],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        env={**os.environ, "PYTHONPATH": str(Path(__file__).parents[1])},
        bufsize=0,
    )


def line(child: subprocess.Popen[bytes]) -> str:
    assert child.stdout is not None
    while True:
        assert select.select([child.stdout], [], [], 5)[0], "lock child did not respond"
        value = child.stdout.readline().decode().strip()
        if not value.startswith("waiting for"):
            return value


def release(child: subprocess.Popen[bytes]) -> None:
    assert child.stdin is not None
    child.stdin.write(b"release\n")
    child.stdin.flush()
    assert child.wait(timeout=5) == 0


def test_build_serializes_but_world_can_coexist(repo: Path) -> None:
    root = repo.parent / "locks"
    ctx = Context(repo, replace(load(repo), lock_dir=root, headless_pool_size=2))
    children = []
    try:
        with ctx.locks.build():
            build = start(repo, root, "build")
            children.append(build)
            assert line(build) == "ready"
            world = start(repo, root, "headless")
            children.append(world)
            assert line(world) == "ready"
            assert line(world) == "entered"
            release(world)
        assert line(build) == "entered"
        release(build)
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)


@pytest.mark.parametrize("initially_fresh", [False, True])
def test_fresh_editor_skips_build_lock_and_rechecks_after_wait(
    repo: Path, initially_fresh: bool
) -> None:
    root = repo.parent / "locks"
    ctx = Context(repo, replace(load(repo), lock_dir=root, headless_pool_size=2))
    module = editor_module(ctx)
    module.parent.mkdir(parents=True)
    if initially_fresh:
        module.write_bytes(b"editor")
        ctx.freshness.stamp("editor", "owner")
    children = []
    try:
        with ctx.locks.build():
            child = start(repo, root, "ensure")
            children.append(child)
            assert line(child) == "ready"
            if initially_fresh:
                assert line(child) == "editor up to date"
                assert child.wait(timeout=5) == 0
            else:
                module.write_bytes(b"editor")
                ctx.freshness.stamp("editor", "owner")
        if not initially_fresh:
            assert line(child) == "editor up to date"
            assert child.wait(timeout=5) == 0
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
