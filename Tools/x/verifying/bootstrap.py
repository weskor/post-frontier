"""Spawn and retire unattached desktop-session lock owners."""

from contextlib import suppress
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

from x.context import Context
from x.verifying.cleanup import stop_owned


def spawn(ctx: Context, endpoint: Path, folder: Path) -> subprocess.Popen[bytes]:
    if ctx.run is None:
        raise RuntimeError("session launch requires a run")
    with (ctx.run.dir / "lock-holder.log").open("wb") as log:
        return subprocess.Popen(
            [
                sys.executable,
                "-m",
                "x.verifying.holder",
                str(ctx.repo),
                str(endpoint),
                str(folder),
                str(ctx.settings.lock_dir),
                ctx.run.id,
                str(os.getpid()),
            ],
            cwd=ctx.repo,
            stdout=log,
            stderr=log,
            start_new_session=True,
            env={**os.environ, "PYTHONPATH": str(Path(__file__).resolve().parents[2])},
        )


def remove_endpoint(endpoint: Path) -> None:
    endpoint.unlink(missing_ok=True)
    if endpoint.parent.exists():
        endpoint.parent.rmdir()


def abort_child(child: subprocess.Popen[bytes], endpoint: Path, manifest: Path) -> None:
    child.terminate()
    try:
        child.wait(timeout=0.5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()
    manifest.unlink(missing_ok=True)
    remove_endpoint(endpoint)


def abort_record(record: dict[str, Any], manifest: Path) -> None:
    stop_owned(record)
    with suppress(ChildProcessError):
        os.waitpid(int(record["pid"]), 0)
    manifest.unlink(missing_ok=True)
    remove_endpoint(Path(record["endpoint"]))
