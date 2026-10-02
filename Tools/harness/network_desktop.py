#!/usr/bin/env python3
"""Own packaged listen-host/remote windows and drive only an explicitly focused peer."""

from __future__ import annotations

import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
from typing import cast

from harness.verification_readiness import WindowNotReady, desktop_log_ready
from harness.verify import (
    BINARY,
    DEFAULT_MAP,
    POINTER,
    JsonObject,
    compile_pointer,
    execute,
    identity,
    map_started,
    package_stamp,
)
from x.scopes import map_package


def event(run: Path, action: str, **fields: object) -> None:
    with (run / "actions.jsonl").open("a") as file:
        file.write(
            json.dumps(
                {
                    "time": datetime.datetime.now(datetime.UTC).isoformat(),
                    "action": action,
                    **fields,
                }
            )
            + "\n"
        )


def session(run: Path) -> JsonObject:
    return cast(JsonObject, json.loads((run / "session.json").read_text()))


def windows() -> list[JsonObject]:
    return cast(list[JsonObject], json.loads(execute(["hyprctl", "clients", "-j"])))


def doctor(run: Path, peer: str, *, focused: bool = False) -> JsonObject:
    record = session(run)
    if package_stamp() != record["package"]:
        raise RuntimeError(
            "Package modified/stale since launch; stop session, rebuild, start a fresh run"
        )
    if peer not in record["peers"]:
        raise RuntimeError(f"Unknown peer: {peer}")
    item = record["peers"][peer]
    if identity(item["pid"]) != item["identity"]:
        raise RuntimeError(
            f"{peer} executable/PID/start identity changed; no input sent"
        )
    log_path = run / peer / "game.log"
    log = log_path.read_text(errors="replace") if log_path.exists() else ""
    desktop_log_ready(
        log, peer, record["map"], log_path, map_started(log, record["map"])
    )
    owned = [
        window
        for window in windows()
        if window["pid"] == item["pid"] and window.get("mapped")
    ]
    if not owned:
        raise WindowNotReady(f"{peer} owned window not mapped yet")
    if len(owned) != 1:
        raise RuntimeError(f"{peer} expected one mapped owned window, got {len(owned)}")
    window = owned[0]
    if (
        focused
        and json.loads(execute(["hyprctl", "activewindow", "-j"])).get("address")
        != window["address"]
    ):
        raise RuntimeError(f"{peer} window is not focused; no input or screenshot sent")
    return {
        "peer": peer,
        "pid": item["pid"],
        "map": record["map"],
        "window": window,
        "monitors": json.loads(execute(["hyprctl", "monitors", "-j"])),
    }


def stop_one(run: Path, item: JsonObject) -> None:
    pid = item["pid"]
    if identity(pid) != item["identity"]:
        event(
            run, "stop", pid=pid, result="already absent or identity changed; no signal"
        )
        return
    fd = os.pidfd_open(pid)
    try:
        if identity(pid) != item["identity"]:
            raise RuntimeError("PID identity changed before cleanup")
        signal.pidfd_send_signal(fd, signal.SIGTERM)
        while identity(pid) == item["identity"]:
            time.sleep(0.1)
        event(run, "stop", pid=pid, result="TERM")
    finally:
        os.close(fd)


def stop(run: Path) -> None:
    for item in reversed(list(session(run)["peers"].values())):
        stop_one(run, item)
    print(f"Stopped recorded game processes only; evidence retained in {run}")


def launch(run: Path, clients: int, probe: bool, map_path: str = DEFAULT_MAP) -> None:
    map_path = map_package(map_path)
    package = package_stamp()
    for program in ("hyprctl", "wtype", "grim", "cc", "pkg-config"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing desktop prerequisite: {program}")
    execute(["hyprctl", "monitors", "-j"])
    run.mkdir(parents=True, exist_ok=False)
    compile_pointer()
    record: JsonObject = {
        "map": map_path,
        "package": package,
        "clients": clients,
        "probe": probe,
        "peers": {},
    }
    (run / "session.json").write_text(json.dumps(record, indent=2))
    # Connect to loopback only; use a freely chosen port to avoid adopting a different listen server.
    import socket

    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    try:
        for index in range(clients + 1):
            name = "host" if index == 0 else f"c{index}"
            folder = run / name
            folder.mkdir()
            travel = f"{map_path}?listen" if index == 0 else f"127.0.0.1:{port}"
            command = [
                str(BINARY),
                travel,
                "-windowed",
                "-ResX=1100",
                "-ResY=720",
                "-log",
                "-stdout",
                "-FullStdOutLogOutput",
                f"-abslog={folder / 'game.log'}",
            ]
            command.append("-nosteam")
            if index == 0:
                command.append(f"-port={port}")
            if probe:
                command += [
                    f"-CoopRTSNetVerifyDir={folder}",
                    f"-CoopRTSNetVerifyPeer={name}",
                ]
                if index == 0:
                    command.append("-CoopRTSNetVerifyAuthority")
            with (folder / "stdout.log").open("w") as output:
                process = subprocess.Popen(
                    command,
                    cwd=BINARY.parents[3],
                    stdout=output,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
            # A launch only fails on an observed process exit, never elapsed wall time.
            while True:
                stamp = identity(process.pid)
                if stamp and stamp["exe"] == str(BINARY.resolve()):
                    break
                if process.poll() is not None:
                    raise RuntimeError(
                        f"{name} failed to establish executable identity; inspect its stdout.log"
                    )
                time.sleep(0.1)
            record["peers"][name] = {
                "pid": process.pid,
                "identity": stamp,
                "command": command,
            }
            (run / "session.json").write_text(json.dumps(record, indent=2))
            event(
                run, "launch", peer=name, pid=process.pid, command=command, map=map_path
            )
            # Wait for an owned mapped window on the selected map or the process's own exit.
            while True:
                try:
                    report = doctor(run, name)
                    print(
                        json.dumps(
                            {
                                "ready": name,
                                "pid": process.pid,
                                "window": report["window"]["address"],
                            }
                        )
                    )
                    break
                except WindowNotReady as error:
                    if process.poll() is not None:
                        raise RuntimeError(
                            f"{name} did not become a mapped window on {map_path}; inspect per-peer logs"
                        ) from error
                    time.sleep(0.3)
    except BaseException as error:
        try:
            event(run, "launch-failed", map=map_path, error=repr(error))
        finally:
            stop(run)
        raise


def fraction(value: str) -> float:
    number = float(value)
    if not 0.05 <= number <= 0.95:
        raise argparse.ArgumentTypeError("Use a fraction between .05 and .95")
    return number


def configure(parser: argparse.ArgumentParser) -> None:
    commands = parser.add_subparsers(dest="action", required=True)
    launch_command = commands.add_parser("launch")
    launch_command.add_argument("--clients", type=int, choices=(1, 4), required=True)
    launch_command.add_argument(
        "--map",
        type=map_package,
        default=DEFAULT_MAP,
        help="world package path (default: %(default)s)",
    )
    launch_command.add_argument(
        "--probe",
        action="store_true",
        help="opt into Development observations and host-only encounter fixtures",
    )
    commands.add_parser("stop")
    for action in ("doctor", "focus", "capture", "key", "click", "point"):
        command = commands.add_parser(action)
        command.add_argument("--peer", required=True)
        if action == "capture":
            command.add_argument("label")
        if action == "key":
            command.add_argument(
                "key",
                choices=(
                    "w",
                    "a",
                    "s",
                    "d",
                    "tab",
                    "space",
                    "h",
                    "r",
                    "q",
                    "escape",
                    "f4",
                    "enter",
                ),
            )
        if action in ("point", "click"):
            command.add_argument("--x", type=fraction, default=0.5)
            command.add_argument("--y", type=fraction, default=0.5)
        if action == "click":
            command.add_argument("button", choices=("left", "right"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    run = Path(os.environ["X_HARNESS_DIR"]).resolve()
    if args.action == "launch":
        launch(run, args.clients, args.probe, map_path=args.map)
        return
    if args.action == "stop":
        stop(run)
        return
    report = doctor(
        run, args.peer, focused=args.action in ("capture", "key", "click", "point")
    )
    if args.action == "doctor":
        print(json.dumps(report, indent=2))
    elif args.action == "focus":
        address = report["window"]["address"]
        execute(
            ["hyprctl", "dispatch", f'hl.dsp.focus({{window="address:{address}"}})']
        )
        doctor(run, args.peer, focused=True)
        event(run, "focus", peer=args.peer, address=address)
    elif args.action == "capture":
        if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
            raise RuntimeError(
                "Capture label must be alphanumeric, hyphen or underscore"
            )
        target = run / f"{args.peer}-{args.label}.png"
        if target.exists():
            raise RuntimeError("Evidence label already exists")
        report["cursor"] = json.loads(execute(["hyprctl", "cursorpos", "-j"]))
        execute(["grim", "-c", target])
        target.with_suffix(".json").write_text(json.dumps(report, indent=2))
        event(run, "capture", peer=args.peer, path=str(target))
        print(target)
    elif args.action == "key":
        key = {"enter": "Return", "tab": "Tab", "escape": "Escape", "f4": "F4"}.get(
            args.key, args.key
        )
        execute(["wtype", "-k", key])
        event(run, "key", peer=args.peer, key=args.key)
    else:
        window = report["window"]
        x = int(window["at"][0] + window["size"][0] * args.x)
        y = int(window["at"][1] + window["size"][1] * args.y)
        execute(["hyprctl", "dispatch", f"hl.dsp.cursor.move({{x={x},y={y}}})"])
        doctor(run, args.peer, focused=True)
        execute(
            [POINTER, "click", "273" if args.button == "right" else "272"]
            if args.action == "click"
            else [POINTER, "scroll", "0"]
        )
        event(
            run,
            args.action,
            peer=args.peer,
            x=args.x,
            y=args.y,
            **({"button": args.button} if args.action == "click" else {}),
        )


if __name__ == "__main__":
    if not os.environ.get("X_RUN_ID"):
        sys.exit("run through ./x verify")
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"Multiplayer desktop verification blocked: {error}", file=sys.stderr)
        sys.exit(1)
