"""Interrupted launchers must not strand a detached exclusive lock owner."""

import fcntl
import os
from pathlib import Path
import subprocess
import sys
import time

import pytest
from x import jsonio
from x.verifying.cleanup import stop_owned
from x.verifying.holder import identity

LAUNCHER = """
from dataclasses import replace
from pathlib import Path
import os, signal, socket, subprocess, sys, time
from x import jsonio
from x.context import Context
from x.runs import Run
from x.settings import load
from x.verifying import sessions
from x.verifying.holder import identity
repo, root, phase = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
settings = replace(load(repo), lock_dir=root / 'locks', runs_root=root / 'runs')
run = Run(repo, settings.runs_root, 'verify', ['verify', 'native', 'launch'])
ctx = Context(repo, settings, run, 'verify')
original_spawn, original_exists, original_save = subprocess.Popen, Path.exists, jsonio.save

def interrupt():
    if phase in ('death', 'queued-death'):
        os._exit(0)
    os.kill(os.getpid(), signal.SIGINT)

def spawn(*args, **kwargs):
    child = original_spawn(*args, **kwargs)
    original_save(root / 'holder.json', {'pid': child.pid, 'identity': identity(child.pid)})
    return child

def exists(path):
    if path.name == 'holder.sock' and phase in ('endpoint', 'death', 'queued-death'):
        if phase == 'queued-death':
            while not list(settings.lock_dir.glob('turnstile.lock.*.holder.json')):
                time.sleep(0.01)
        interrupt()
    return original_exists(path)

def save(path, value):
    if path.name == 'native.json' and phase == 'manifest':
        interrupt()
    original_save(path, value)

class Connection(socket.socket):
    def recv(self, *args, **kwargs):
        if phase == 'ready':
            interrupt()
        return super().recv(*args, **kwargs)

subprocess.Popen, Path.exists, jsonio.save = spawn, exists, save
sessions.socket.socket = Connection
try:
    with sessions.lease(ctx, 'native', 'launch', run.dir / 'harness'):
        raise AssertionError('interruption did not fire')
except KeyboardInterrupt:
    pass
"""


@pytest.mark.parametrize(
    ("phase", "queued"),
    [
        ("endpoint", False),
        ("manifest", False),
        ("ready", False),
        ("death", False),
        ("queued-death", True),
    ],
)
def test_interrupted_launcher_retires_unattached_holder(
    repo: Path, tmp_path: Path, phase: str, queued: bool
) -> None:
    locks = tmp_path / "locks"
    locks.mkdir()
    with (locks / "ue.lock").open("a+b") as admission:
        if queued:
            fcntl.flock(admission.fileno(), fcntl.LOCK_EX)
        result = subprocess.run(
            [sys.executable, "-c", LAUNCHER, str(repo), str(tmp_path), phase],
            env={**os.environ, "PYTHONPATH": str(Path(__file__).parents[1])},
            check=False,
            capture_output=True,
            text=True,
            timeout=8,
        )
        assert result.returncode == 0, result.stderr
        holder = jsonio.load(tmp_path / "holder.json")
        pid = int(holder["pid"])
        try:
            deadline = time.monotonic() + 8
            while identity(pid) is not None and time.monotonic() < deadline:
                time.sleep(0.02)
            assert identity(pid) is None, (
                "interrupted launcher orphaned an exclusive holder"
            )
            assert not list(locks.glob("ue.lock.*.holder.json"))
            assert not (repo / "Intermediate/x-harness/native.json").exists()
            fcntl.flock(admission.fileno(), fcntl.LOCK_UN)
            fcntl.flock(admission.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            stop_owned(holder)
