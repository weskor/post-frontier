"""Own one standalone simulation process, its progress watchdog and cleanup."""

from __future__ import annotations

import argparse
from contextlib import suppress
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
from harness.waits import DEFAULT_SECONDS, Deadline, WaitTimeout
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
        "-SimDuel" if job.get("mode") == "duel" else "-autopilot",
        f"-SimSeed={job['seed']}",
        f"-SimTimeCap={job['time_cap']}",
        f"-SimDilation={job['dilation']}",
        f"-SimOutput={output}",
        f"-abslog={output.parent / 'game.log'}",
        "-ExecCmds=t.MaxFPS 0",
    ]
    if job.get("mode") != "duel":
        result += [
            f"-{ECONOMY_FLAGS[key]}={value}" for key, value in job["economy"].items()
        ]
    return result


def stop_owned(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    # The unreaped leader owns this private process group, so its PID cannot be reused.
    for signum in (signal.SIGTERM, signal.SIGKILL):
        with suppress(ProcessLookupError):
            os.killpg(process.pid, signum)
        deadline = Deadline(
            f"simulation child {process.pid} exit after {signal.Signals(signum).name}",
            "shutdown",
        )
        try:
            while process.poll() is None:
                deadline.check({"pid": process.pid, "returncode": process.returncode})
                time.sleep(min(0.1, deadline.remaining))
            return
        except WaitTimeout as error:
            if signum == signal.SIGKILL:
                raise
            print(
                f"Escalating simulation shutdown: {error}", file=sys.stderr, flush=True
            )


def completion_seconds(job: JsonObject, pair_count: int) -> float:
    # 80 passing launches measured 73.979s maximum launch-through-exit wall time.
    # Budget startup separately, then allow 3x nominal wall time for every duel pair.
    return 3 * (
        DEFAULT_SECONDS["simulation"]
        + float(job["time_cap"]) / float(job["dilation"]) * pair_count
    )


def monitor(
    process: subprocess.Popen[bytes],
    output: Path,
    directory: Path,
    stall_seconds: float,
    job: JsonObject,
) -> int:
    progress = Deadline(
        "persisted simulation game-time progress (watchdog failure, not draw)",
        "simulation",
        seconds=stall_seconds,
    )
    # Passing duel matrices had at most three runtime units (nine ordered pairs).
    # Cover that roster during startup, then use the actual persisted definitions.
    pair_count = 9 if job.get("mode") == "duel" else 1
    absolute = Deadline(
        f"simulation job {directory.name} completion",
        "simulation",
        seconds=completion_seconds(job, pair_count),
    )
    checkpoint: JsonObject | None = None
    previous_duration = -1.0
    previous_stamp = None
    while process.poll() is None:
        time.sleep(min(0.5, progress.remaining, absolute.remaining))
        if output.exists():
            current_stamp = output.stat().st_mtime_ns
            if current_stamp != previous_stamp:
                checkpoint = json.loads(output.read_text())
                duration = number(checkpoint.get("duration"), "checkpoint duration")
                if job.get("mode") == "duel" and checkpoint.get("unit_definitions"):
                    observed_pairs = len(checkpoint["unit_definitions"]) ** 2
                    if observed_pairs != pair_count:
                        pair_count = observed_pairs
                        absolute.rebudget(completion_seconds(job, pair_count))
                if duration > previous_duration:
                    progress.reset()
                    previous_duration = duration
                    print(
                        f"  {directory.name}: {duration:.1f} game seconds",
                        flush=True,
                    )
                previous_stamp = current_stamp
        snapshot = {
            "pid": process.pid,
            "job": job,
            "pair_count": pair_count,
            "checkpoint": checkpoint,
        }
        absolute.check(snapshot)
        progress.check(snapshot)
    assert process.returncode is not None
    return process.returncode


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
                        process, output, directory, args.stall_seconds, job
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
            try:
                stop_owned(process)
            except (OSError, WaitTimeout) as error:
                record["status"] = "failed"
                record["error"] = str(error)
                print(f"FAIL {directory.name}: {error}", file=sys.stderr, flush=True)
            record["returncode"] = process.returncode
        save_json(directory / "launch.json", record)
    return record
