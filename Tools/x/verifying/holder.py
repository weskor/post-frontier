"""A detached lock owner serializes desktop actions until recorded games exit."""

import select
import socket
import sys
from dataclasses import replace
from pathlib import Path

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
                    connection.sendall(b"ready")
                    message = connection.recv(16)
                    if not launched:
                        launched = games_alive(folder)
                        if not launched:
                            return
                    if message == b"stop" and not games_alive(folder):
                        return


def main() -> None:
    repo, endpoint, folder, lock_dir, run_id = sys.argv[1:]
    root = Path(repo)
    settings = replace(load(root), lock_dir=Path(lock_dir))
    ctx = Context(root, settings, command="verify desktop session")
    ctx.locks.identity["run_id"] = run_id
    try:
        serve(ctx, Path(endpoint), Path(folder))
    finally:
        Path(endpoint).unlink(missing_ok=True)
        Path(endpoint).parent.rmdir()


if __name__ == "__main__":
    main()
