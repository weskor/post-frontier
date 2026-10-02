"""Own one standalone simulation process, its progress watchdog and cleanup."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from harness.simulation_evidence import save_json, stamp
from harness.simulation_validation import number, validate_report
from harness.verify import JsonObject
from x.content.packages import latest_package
from x.freshness import is_fresh
from x.locks import Locks
from x.settings import load

ROOT = Path(__file__).resolve().parents[2]
ECONOMY_FLAGS = dict(
    baseline="SimBaseline",
    normal_rate="SimNormalRate",
    rich_rate="SimRichRate",
    normal_amount="SimNormalAmount",
    rich_amount="SimRichAmount",
)


def artifact_identity(args: argparse.Namespace) -> list[JsonObject]:
    executable = (
        latest_package(ROOT, "development", load(ROOT).game_target)
        if args.package
        else Path(os.environ["UE_ROOT"]) / "Engine/Binaries/Linux/UnrealEditor"
    )
    executable = executable.expanduser().resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f"Missing executable: {executable}")
    artifacts = [executable]
    if not args.package:
        artifacts.append(ROOT / "Binaries/Linux/libUnrealEditor-CoopRTS.so")
    binary = artifacts[-1]
    if not binary.is_file():
        raise ValueError(f"Build the current editor module first: {binary}")
    if not args.package and not is_fresh(ROOT, "editor"):
        raise ValueError("Editor module is stale; use ./x sim")
    return [stamp(path) for path in artifacts]


def command(args: argparse.Namespace, job: JsonObject, output: Path) -> list[str]:
    if args.package:
        result = [
            str(latest_package(ROOT, "development", load(ROOT).game_target)),
            job["map"],
        ]
    else:
        engine = Path(os.environ["UE_ROOT"]) / "Engine/Binaries/Linux/UnrealEditor"
        result = [
            str(engine),
            str(ROOT / "CoopRTS.uproject"),
            job["map"],
            "-game",
        ]
    result += [
        "-nullrhi",
        "-nosound",
        "-nosplash",
        "-unattended",
        "-nosteam",
        "-autopilot",
        f"-SimSeed={job['seed']}",
        f"-SimTimeCap={job['time_cap']}",
        f"-SimDilation={job['dilation']}",
        f"-SimOutput={output}",
        f"-abslog={output.parent / 'game.log'}",
        "-ExecCmds=t.MaxFPS 0",
    ]
    result += [
        f"-{ECONOMY_FLAGS[key]}={value}" for key, value in job["economy"].items()
    ]
    return result


def stop_owned(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    # Popen starts a private session. Never select other UE processes by name.
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def monitor(
    process: subprocess.Popen[bytes],
    output: Path,
    directory: Path,
    stall_seconds: float,
) -> int:
    last_progress = time.monotonic()
    previous_duration = -1.0
    previous_stamp = None
    while process.poll() is None:
        time.sleep(0.5)
        if output.exists():
            current_stamp = output.stat().st_mtime_ns
            if current_stamp != previous_stamp:
                checkpoint = json.loads(output.read_text())
                duration = number(checkpoint.get("duration"), "checkpoint duration")
                if duration > previous_duration:
                    last_progress = time.monotonic()
                    previous_duration = duration
                    print(
                        f"  {directory.name}: {duration:.1f} game seconds",
                        flush=True,
                    )
                previous_stamp = current_stamp
        if time.monotonic() - last_progress > stall_seconds:
            raise ValueError(
                f"No persisted game-time progress for {stall_seconds:g}s; watchdog failure, not draw"
            )
    return process.wait()


def complete_result(output: Path, job: JsonObject, record: JsonObject) -> None:
    report = json.loads(output.read_text())
    validate_report(report, job)
    record["status"] = "complete"
    record["outcome"] = report["outcome"]
    record["winner"] = report["winner"]
    record["duration"] = report["duration"]


def launch_record(
    job: JsonObject, directory: Path, launch: list[str], artifacts: list[JsonObject]
) -> JsonObject:
    record: JsonObject = dict(
        job=job,
        directory=str(directory),
        command=launch,
        artifacts=artifacts,
        status="failed",
    )
    save_json(directory / "launch.json", record)
    return record


def run_match(
    args: argparse.Namespace,
    job: JsonObject,
    directory: Path,
    artifacts: list[JsonObject],
) -> JsonObject:
    directory.mkdir()
    output = directory / "match.json"
    launch = command(args, job, output)
    record = launch_record(job, directory, launch, artifacts)
    process = None
    try:
        with Locks(ROOT, load(ROOT), command="sim").headless():
            if args.package:
                latest_package(ROOT, "development", load(ROOT).game_target)
            elif not is_fresh(ROOT, "editor"):
                raise ValueError("Editor inputs changed before match; use ./x sim")
            if [stamp(Path(item["path"])) for item in artifacts] != artifacts:
                raise ValueError("Gameplay artifact changed before launch")
            with (directory / "stdout.log").open("wb") as stdout:
                process = subprocess.Popen(
                    launch,
                    cwd=ROOT,
                    stdout=stdout,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
                try:
                    record["pid"] = process.pid
                    save_json(directory / "launch.json", record)
                    record["returncode"] = monitor(
                        process, output, directory, args.stall_seconds
                    )
                finally:
                    # Hold the shared lock until the owned game has actually stopped.
                    stop_owned(process)
            if record["returncode"] != 0:
                raise ValueError(f"Game exited {record['returncode']}; no valid result")
            if [stamp(Path(item["path"])) for item in artifacts] != artifacts:
                raise ValueError("Gameplay artifact changed during match")
            complete_result(output, job, record)
    except KeyboardInterrupt:
        record["status"] = "interrupted"
        record["error"] = "Operator interrupted run; no match result"
        raise
    except (OSError, ValueError, KeyError, TypeError) as error:
        record["error"] = str(error)
        print(f"FAIL {directory.name}: {error}", file=sys.stderr, flush=True)
    finally:
        if process is not None:
            stop_owned(process)
            record["returncode"] = process.returncode
        save_json(directory / "launch.json", record)
    return record
