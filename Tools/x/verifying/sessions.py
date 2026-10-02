"""Connect each invocation to the session's persistent exclusive lock owner."""

from collections.abc import Iterator
from contextlib import contextmanager, suppress
import fcntl
from pathlib import Path
import socket
import tempfile
import time
from typing import Any

from x import jsonio
from x.context import Context
from x.verifying.bootstrap import abort_child, abort_record, remove_endpoint, spawn
from x.verifying.holder import games_alive, identity


def manifest(ctx: Context, harness: str) -> Path:
    return ctx.repo / "Intermediate/x-harness" / f"{harness}.json"


def start(ctx: Context, harness: str, folder: Path) -> dict[str, Any]:
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
    child = None
    try:
        child = spawn(ctx, endpoint, folder)
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
        return record
    except BaseException:
        if child is not None:
            abort_child(child, endpoint, path)
        else:
            remove_endpoint(endpoint)
        raise


@contextmanager
def lease(ctx: Context, harness: str, action: str, folder: Path) -> Iterator[Path]:
    registry = manifest(ctx, harness).with_suffix(".lock")
    registry.parent.mkdir(parents=True, exist_ok=True)
    with registry.open("a+b") as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        with connected(ctx, harness, action, folder) as session:
            yield session


def active_session(ctx: Context, harness: str) -> dict[str, Any]:
    try:
        return jsonio.load(manifest(ctx, harness))
    except FileNotFoundError as error:
        raise RuntimeError(
            f"no active {harness} session; run ./x verify {harness} launch"
        ) from error


@contextmanager
def connected(ctx: Context, harness: str, action: str, folder: Path) -> Iterator[Path]:
    record = (
        start(ctx, harness, folder)
        if action == "launch"
        else active_session(ctx, harness)
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
    acquired = False
    try:
        with socket.socket(socket.AF_UNIX) as connection:
            if action == "launch":
                print(
                    f"waiting for exclusive lease; holders: {ctx.locks._holders(['ue.lock'])}",
                    flush=True,
                )
            connection.connect(str(endpoint))
            if connection.recv(16) != b"ready":
                raise RuntimeError("session holder did not grant exclusive lease")
            acquired = True
            try:
                yield Path(str(record["folder"]))
            finally:
                with suppress(OSError):
                    connection.sendall(b"stop" if action == "stop" else b"keep")
    except BaseException:
        if action == "launch" and not acquired:
            abort_record(record, manifest(ctx, harness))
        raise
    finally:
        if action == "stop" and not games_alive(Path(str(record["folder"]))):
            # The holder acknowledges release by removing its endpoint.
            while endpoint.exists() and identity(int(record["pid"])) is not None:
                time.sleep(0.02)
            manifest(ctx, harness).unlink(missing_ok=True)
