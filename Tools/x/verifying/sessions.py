"""Connect each invocation to the session's persistent exclusive lock owner."""

import fcntl
import os
import socket
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

from x import jsonio
from x.context import Context
from x.verifying.holder import games_alive, identity


def manifest(ctx: Context, harness: str) -> Path:
    return ctx.repo / "Intermediate/x-harness" / f"{harness}.json"


def start(ctx: Context, harness: str, folder: Path) -> dict[str, str | int]:
    if ctx.run is None:
        raise RuntimeError("session launch requires a run")
    path = manifest(ctx, harness)
    if path.exists():
        old = jsonio.load(path)
        if identity(int(old["pid"])) == old["identity"]:
            raise RuntimeError(
                f"{harness} session already active; run ./x verify {harness} stop"
            )
    endpoint = Path(tempfile.mkdtemp(prefix="cooprts-x-session-")) / "holder.sock"
    with (ctx.run.dir / "lock-holder.log").open("wb") as log:
        child = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "x.verifying.holder",
                str(ctx.repo),
                str(endpoint),
                str(folder),
                str(ctx.settings.lock_dir),
                ctx.run.id,
            ],
            cwd=ctx.repo,
            stdout=log,
            stderr=log,
            start_new_session=True,
            env={**os.environ, "PYTHONPATH": str(Path(__file__).resolve().parents[2])},
        )
    while not endpoint.exists():
        if child.poll() is not None:
            raise RuntimeError("lock holder exited; inspect lock-holder.log")
        time.sleep(0.02)
    record = {
        "pid": child.pid,
        "identity": identity(child.pid),
        "endpoint": str(endpoint),
        "folder": str(folder),
    }
    jsonio.save(path, record)
    return {"endpoint": str(endpoint), "folder": str(folder), "pid": child.pid}


@contextmanager
def lease(ctx: Context, harness: str, action: str, folder: Path) -> Iterator[Path]:
    registry = manifest(ctx, harness).with_suffix(".lock")
    registry.parent.mkdir(parents=True, exist_ok=True)
    with registry.open("a+b") as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        with connected(ctx, harness, action, folder) as session:
            yield session


@contextmanager
def connected(ctx: Context, harness: str, action: str, folder: Path) -> Iterator[Path]:
    record = (
        start(ctx, harness, folder)
        if action == "launch"
        else jsonio.load(manifest(ctx, harness))
    )
    endpoint = Path(str(record["endpoint"]))
    if action != "launch" and identity(int(record["pid"])) != record["identity"]:
        with ctx.locks.exclusive():
            if action != "stop":
                raise RuntimeError(
                    f"session holder absent; run ./x verify {harness} stop"
                )
            yield Path(str(record["folder"]))
            manifest(ctx, harness).unlink(missing_ok=True)
        return
    try:
        with socket.socket(socket.AF_UNIX) as connection:
            connection.connect(str(endpoint))
            if connection.recv(16) != b"ready":
                raise RuntimeError("session holder did not grant exclusive lease")
            try:
                yield Path(str(record["folder"]))
            finally:
                connection.sendall(b"stop" if action == "stop" else b"keep")
    finally:
        if action == "stop" and not games_alive(Path(str(record["folder"]))):
            # The holder acknowledges release by removing its endpoint.
            while endpoint.exists() and identity(int(record["pid"])) is not None:
                time.sleep(0.02)
            manifest(ctx, harness).unlink(missing_ok=True)
