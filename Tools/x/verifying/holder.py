"""A detached lock owner serializes desktop actions until recorded games exit."""

from dataclasses import replace
import os
from pathlib import Path
import select
import signal
import socket
import sys
from threading import Thread

from x import jsonio
from x.context import Context
from x.settings import load


def identity(pid: int) -> dict[str, str] | None:
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        if fields[0] == "Z":
            return None
        return {
            "start": fields[19],
            "exe": str(Path(f"/proc/{pid}/exe").resolve(strict=True)),
        }
    except (OSError, ValueError):
        return None


def games_alive(folder: Path) -> bool:
    try:
        record = jsonio.load(folder / "session.json")
    except (OSError, ValueError):
        return False
    peers = record.get("peers", {"native": record})
    return any(
        identity(int(peer["pid"])) == peer["identity"] for peer in peers.values()
    )


def serve(ctx: Context, endpoint: Path, folder: Path) -> None:
    with socket.socket(socket.AF_UNIX) as server:
        server.bind(str(endpoint))
        endpoint.chmod(0o600)
        server.listen()
        with ctx.locks.exclusive():
            launched = False
            while True:
                if launched and not games_alive(folder):
                    return
                if not select.select([server], [], [], 0.2)[0]:
                    continue
                connection, _ = server.accept()
                with connection:
                    try:
                        connection.sendall(b"ready")
                        message = connection.recv(16)
                    except OSError:
                        message = b""
                    if not launched:
                        launched = games_alive(folder)
                        if not launched:
                            return
                    if message == b"stop" and not games_alive(folder):
                        return


def watch_creator(creator: int, folder: Path) -> None:
    # Monitor outside lock admission too: a dead queued launcher must not keep
    # the turnstile locked until the current desktop session finishes.
    select.select([creator], [], [])
    if not games_alive(folder):
        os.kill(os.getpid(), signal.SIGTERM)


def terminate(signum: int, frame: object) -> None:
    raise SystemExit(0)


def main() -> None:
    repo, endpoint, folder, lock_dir, run_id, creator_pid = sys.argv[1:]
    root = Path(repo)
    settings = replace(load(root), lock_dir=Path(lock_dir))
    ctx = Context(root, settings, command="verify desktop session")
    ctx.locks.identity["run_id"] = run_id
    signal.signal(signal.SIGTERM, terminate)
    creator = -1
    try:
        creator = os.pidfd_open(int(creator_pid))
        Thread(target=watch_creator, args=(creator, Path(folder)), daemon=True).start()
        serve(ctx, Path(endpoint), Path(folder))
    except ProcessLookupError:
        pass
    finally:
        if creator >= 0:
            os.close(creator)
        Path(endpoint).unlink(missing_ok=True)
        Path(endpoint).parent.rmdir()


if __name__ == "__main__":
    main()
