"""Builds serialize shared UBT state without bypassing headless admission."""

from dataclasses import replace
import os
from pathlib import Path
import select
import subprocess
import sys

from x.context import Context
from x.settings import load

CHILD = """
from dataclasses import replace
from pathlib import Path
import sys
from x.context import Context
from x.settings import load
repo, root, mode = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
ctx = Context(repo, replace(load(repo), lock_dir=root, headless_pool_size=2))
print('ready', flush=True)
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
    assert select.select([child.stdout], [], [], 5)[0], "lock child did not respond"
    return child.stdout.readline().decode().strip()


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
            world = start(repo, root, "headless")
            children.append(world)
            assert line(world) == "ready"
            assert line(world) == "entered"
            release(world)
            build = start(repo, root, "build")
            children.append(build)
            assert line(build) == "ready"
            assert line(build).startswith("waiting for build.lock:")
        assert line(build) == "entered"
        release(build)
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
