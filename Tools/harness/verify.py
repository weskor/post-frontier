#!/usr/bin/env python3
"""Drive only the packaged CoopRTS process owned by a recorded verification run."""

from __future__ import annotations

import argparse
from collections.abc import Sequence
import datetime
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from typing import Any, cast

from harness.verification_readiness import native_log_ready
from x import scopes

# JSON session and compositor payloads are dynamic third-party records.
type JsonObject = dict[str, Any]

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = Path(__file__).resolve().parent
BINARY = ROOT / "Saved/Packages/development/latest/CoopRTS/Binaries/Linux/CoopRTS"
POINTER = ROOT / "Intermediate/x-harness/pointer"
READY = "Bringing up level for play"
DEFAULT_MAP = "/Game/Maps/Boot"


def map_started(text: str, map_path: str) -> bool:
    return f"Bringing World {map_path}.{map_path.rsplit('/', 1)[1]} up for play" in text


def execute(args: Sequence[str | Path]) -> str:
    return subprocess.run(
        [str(a) for a in args], check=True, text=True, capture_output=True
    ).stdout.strip()


def identity(pid: int) -> dict[str, str] | None:
    try:
        stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        if stat[0] == "Z":
            return None
        return {
            "start": stat[19],
            "exe": str(Path(f"/proc/{pid}/exe").resolve(strict=True)),
        }
    except (FileNotFoundError, ProcessLookupError):
        return None


def record(run: Path, kind: str, **data: object) -> None:
    with (run / "actions.jsonl").open("a") as out:
        out.write(
            json.dumps(
                {
                    "time": datetime.datetime.now(datetime.UTC).isoformat(),
                    "kind": kind,
                    **data,
                }
            )
            + "\n"
        )


def package_stamp() -> JsonObject:
    from x.verifying.artifacts import package_snapshot

    return package_snapshot(ROOT)


def module_stamp() -> JsonObject:
    from x.verifying.artifacts import editor_snapshot

    return editor_snapshot(ROOT)


def compile_pointer() -> None:
    POINTER.parent.mkdir(parents=True, exist_ok=True)
    flags = shlex.split(execute(["pkg-config", "--cflags", "--libs", "wayland-client"]))
    execute(
        [
            "cc",
            "-Wall",
            "-Wextra",
            "-Werror",
            SCRIPTS / "pointer.c",
            "-o",
            POINTER,
            *flags,
        ]
    )


def state(run: Path) -> JsonObject:
    return cast(JsonObject, json.loads((run / "session.json").read_text()))


def doctor(run: Path, focused: bool = False) -> JsonObject:
    session = state(run)
    if identity(session["pid"]) != session["identity"]:
        raise RuntimeError(
            "Owned game is not running with its recorded identity; do not drive another instance"
        )
    if package_stamp() != session["package"]:
        raise RuntimeError(
            "Package changed during session; stop and launch a fresh run"
        )
    log = (
        (run / "game.log").read_text(errors="replace")
        if (run / "game.log").exists()
        else ""
    )
    native_log_ready(log, session["map"], map_started(log, session["map"]))
    windows = json.loads(execute(["hyprctl", "clients", "-j"]))
    owned = [w for w in windows if w["pid"] == session["pid"] and w.get("mapped")]
    if len(owned) != 1:
        raise RuntimeError("Expected exactly one mapped window for the owned game")
    window = owned[0]
    if (
        focused
        and json.loads(execute(["hyprctl", "activewindow", "-j"])).get("pid")
        != session["pid"]
    ):
        raise RuntimeError(
            "Owned game is not focused. Focus that window explicitly; no input sent."
        )
    return {
        "pid": session["pid"],
        "window": window,
        "monitors": json.loads(execute(["hyprctl", "monitors", "-j"])),
    }


def stop(run: Path) -> None:
    session = state(run)
    pid = session["pid"]
    if identity(pid) != session["identity"]:
        record(run, "cleanup", result="owned process already absent; no signal sent")
        return
    fd = os.pidfd_open(pid)
    try:
        if identity(pid) != session["identity"]:
            raise RuntimeError("PID identity changed before cleanup; no signal sent")
        signal.pidfd_send_signal(fd, signal.SIGTERM)
        deadline = time.monotonic() + 15
        while identity(pid) == session["identity"] and time.monotonic() < deadline:
            time.sleep(0.1)
        if identity(pid) == session["identity"]:
            signal.pidfd_send_signal(fd, signal.SIGKILL)
            record(run, "cleanup", result="owned process required SIGKILL")
        else:
            record(run, "cleanup", result="owned process exited after SIGTERM")
    finally:
        os.close(fd)
    print(f"Stopped owned instance only. Evidence preserved: {run}")


def launch(run: Path, map_path: str = DEFAULT_MAP) -> None:
    map_path = scopes.map_package(map_path)
    stamp = package_stamp()
    for program in ("hyprctl", "wtype", "grim", "cc", "pkg-config"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing dependency: {program}")
    execute(["hyprctl", "monitors", "-j"])
    run.mkdir(parents=True, exist_ok=False)
    command = [
        str(BINARY),
        map_path,
        "-windowed",
        "-ResX=1600",
        "-ResY=900",
        "-log",
        "-stdout",
        "-FullStdOutLogOutput",
        f"-abslog={run / 'game.log'}",
    ]
    compile_pointer()
    with (run / "stdout.log").open("w") as out:
        process = subprocess.Popen(
            command,
            cwd=BINARY.parents[3],
            stdout=out,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            current = identity(process.pid)
            if current and current["exe"] == str(BINARY.resolve()):
                break
            if process.poll() is not None:
                raise RuntimeError("Game exited during launch; inspect stdout.log")
            time.sleep(0.05)
        else:
            raise RuntimeError("Could not establish game process identity")
        (run / "session.json").write_text(
            json.dumps(
                {
                    "pid": process.pid,
                    "identity": current,
                    "package": stamp,
                    "map": map_path,
                    "command": command,
                },
                indent=2,
            )
        )
        record(run, "launch", map=map_path, command=command)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(
                    "Game exited before readiness; inspect evidence logs"
                )
            try:
                report = doctor(run)
                print(json.dumps(report, indent=2))
                return
            except RuntimeError:
                time.sleep(0.25)
        raise RuntimeError("Game did not become ready within 90 seconds")
    except BaseException as error:
        record(run, "launch-failure", map=map_path, command=command, error=repr(error))
        if (run / "session.json").exists():
            stop(run)
        elif process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        raise


def capture(run: Path, label: str) -> None:
    if not re.fullmatch(r"[A-Za-z0-9_-]+", label):
        raise RuntimeError(
            "Screenshot label must contain only letters, numbers, underscores or hyphens"
        )
    target = run / f"{label}.png"
    if target.exists():
        raise RuntimeError("Evidence label already exists; use a new label")
    report = doctor(run, focused=True)
    report["cursor"] = json.loads(execute(["hyprctl", "cursorpos", "-j"]))
    execute(["grim", "-c", target])
    (run / f"{label}.json").write_text(json.dumps(report, indent=2))
    record(run, "capture", path=str(target))
    print(target)


def drive(run: Path, args: argparse.Namespace) -> None:
    report = doctor(run, focused=True)
    record(
        run,
        "input",
        action=args.command,
        parameters={k: v for k, v in vars(args).items() if k != "run"},
    )
    if args.command == "key":
        key = {"enter": "Return", "tab": "Tab", "escape": "Escape", "f4": "F4"}.get(
            args.key, args.key
        )
        if args.hold:
            execute(["wtype", "-P", key, "-s", str(args.hold), "-p", key])
        else:
            execute(["wtype", "-k", key])
    elif args.command == "move":
        execute([POINTER, "move", str(args.dx), str(args.dy)])
    else:
        if args.command == "point" or not args.here:
            window = report["window"]
            x = int(window["at"][0] + window["size"][0] * args.x)
            y = int(window["at"][1] + window["size"][1] * args.y)
            execute(["hyprctl", "dispatch", f"hl.dsp.cursor.move({{x={x},y={y}}})"])
            doctor(run, focused=True)

        if args.command == "point":
            command = ["scroll", "0"]  # Motion without a button or wheel tick.
        elif args.command == "click":
            command = ["click", "272" if args.button == "left" else "273"]
        elif args.command == "scroll":
            command = ["scroll", str(args.steps)]
        else:
            command = ["drag", str(args.dx), str(args.dy)]
        execute([POINTER, *command])
    record(run, "input-complete", action=args.command)


def fraction(value: str) -> float:
    number = float(value)
    if not 0.05 <= number <= 0.95:
        raise argparse.ArgumentTypeError("Use a window fraction between .05 and .95")
    return number


def configure(parser: argparse.ArgumentParser) -> None:
    commands = parser.add_subparsers(dest="command", required=True)
    launch_parser = commands.add_parser("launch")
    launch_parser.add_argument(
        "--map",
        type=scopes.map_package,
        default=DEFAULT_MAP,
        help="level package path (default /Game/Maps/Boot); existence checked by the engine",
    )
    for name in ("doctor", "focus", "stop"):
        commands.add_parser(name)
    snap = commands.add_parser("capture")
    snap.add_argument("label")
    key = commands.add_parser("key")
    key.add_argument(
        "key",
        choices=[
            "w",
            "a",
            "s",
            "d",
            "h",
            "r",
            "q",
            "tab",
            "space",
            "enter",
            "escape",
            "f4",
        ],
    )
    key.add_argument(
        "--hold",
        type=int,
        choices=range(0, 2001),
        default=0,
        metavar="0..2000",
        help="Hold milliseconds, zero taps",
    )
    for name in ("click", "scroll", "drag", "move", "point"):
        sub = commands.add_parser(name)
        if name != "move":
            sub.add_argument("--x", type=fraction, default=0.5)
            sub.add_argument("--y", type=fraction, default=0.5)
            if name != "point":
                sub.add_argument(
                    "--here",
                    action="store_true",
                    help="Use the current pointer position without warping",
                )
        if name == "click":
            sub.add_argument("button", choices=["left", "right"])
        elif name == "scroll":
            sub.add_argument(
                "steps",
                type=int,
                choices=range(-12, 13),
                metavar="-12..12",
                help="Negative zooms in, positive zooms out",
            )
        elif name in ("drag", "move"):
            sub.add_argument(
                "dx", type=int, choices=range(-200, 201), metavar="-200..200"
            )
            sub.add_argument(
                "dy", type=int, choices=range(-200, 201), metavar="-200..200"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    run = Path(os.environ["X_HARNESS_DIR"]).resolve()
    if args.command == "launch":
        launch(run, args.map)
    elif args.command == "doctor":
        print(json.dumps(doctor(run), indent=2))
    elif args.command == "focus":
        report = doctor(run)
        address = report["window"]["address"]
        execute(
            ["hyprctl", "dispatch", f'hl.dsp.focus({{window="address:{address}"}})']
        )
        doctor(run, focused=True)
        record(run, "focus", address=address)
    elif args.command == "stop":
        stop(run)
    elif args.command == "capture":
        capture(run, args.label)
    else:
        drive(run, args)


if __name__ == "__main__":
    if not os.environ.get("X_RUN_ID"):
        sys.exit("run through ./x verify")
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"Verification blocked: {error}", file=sys.stderr)
        sys.exit(1)
