"""Retire recorded children if the runner interrupts or stalls a harness."""

import os
from pathlib import Path
import select
import signal
from typing import Any

from x import jsonio
from x.verifying.holder import identity


def stop_owned(record: dict[str, Any]) -> None:
    pid = int(record["pid"])
    if identity(pid) != record["identity"]:
        return
    try:
        fd = os.pidfd_open(pid)
    except ProcessLookupError:
        return
    try:
        if identity(pid) != record["identity"]:
            return
        signal.pidfd_send_signal(fd, signal.SIGTERM)
        if not select.select([fd], [], [], 0.5)[0]:
            signal.pidfd_send_signal(fd, signal.SIGKILL)
            select.select([fd], [], [])
    except ProcessLookupError:
        pass
    finally:
        os.close(fd)


def cleanup(folder: Path) -> None:
    session = folder / "session.json"
    if session.exists():
        record = jsonio.load(session)
        for peer in record.get("peers", {"native": record}).values():
            stop_owned(peer)
    for path in folder.glob("*/process.json"):
        stop_owned(jsonio.load(path))
