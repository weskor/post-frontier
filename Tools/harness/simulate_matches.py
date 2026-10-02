#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["matplotlib>=3.8,<4"]
# ///
"""Run real standalone AI matches, or summarize existing evidence. Never invent draws."""
from __future__ import annotations

import argparse
import collections
import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))

from x.content.generating import MCP_DISABLED
from x.content.packages import latest_package
from x.content.simulation import configure
from x.freshness import is_fresh
from x.locks import Locks
from x.settings import load
MAP_V2 = "/Game/Maps/AvailabilityZoneV2"
MAP_V1 = "/Game/Maps/AvailabilityZone"
DEFAULT_ECONOMY = dict(baseline=2, normal_rate=4, rich_rate=6, normal_amount=2400, rich_amount=3000)
ECONOMY_FLAGS = dict(baseline="SimBaseline", normal_rate="SimNormalRate", rich_rate="SimRichRate",
                     normal_amount="SimNormalAmount", rich_amount="SimRichAmount")
TEAM_FIELDS = ("wallet", "income_per_second", "extractors", "completed_extractors", "deposits_remaining",
               "barracks", "units_alive", "regions_controlled", "hq_health", "units_produced",
               "casualties_observed", "unit_health_loss_observed", "attacks_observed",
               "units_reinforcing", "largest_region_unit_share")
COMPARISON_FIELDS = TEAM_FIELDS + ("units_by_role",)


def save_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def stamp(path: Path) -> dict:
    stat = path.stat()
    return dict(path=str(path), size=stat.st_size, mtime_ns=stat.st_mtime_ns)


def parse_variant(text: str) -> tuple[str, dict]:
    name, separator, overrides = text.partition(":")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        raise argparse.ArgumentTypeError("Variant name must contain only letters, digits, _ or -")
    values = dict(DEFAULT_ECONOMY)
    if not separator:
        if name not in ("baseline2", "baseline3", "baseline4"):
            raise argparse.ArgumentTypeError("Use baseline2/3/4 or NAME:baseline=3,normal_rate=4,...")
        values["baseline"] = int(name[-1])
    else:
        if not overrides:
            raise argparse.ArgumentTypeError("Economy overrides cannot be empty")
        seen = set()
        for part in overrides.split(","):
            key, equals, value = part.partition("=")
            maximum = 10000 if key in ("baseline", "normal_rate", "rich_rate") else 100000000
            if key not in values or key in seen or not equals or not value.isdecimal() or int(value) > maximum:
                raise argparse.ArgumentTypeError(f"Invalid or duplicate economy override: {part}")
            seen.add(key)
            values[key] = int(value)
    return name, values


def number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label} must be a finite number")
    return float(value)


def validate_report(report: dict, job: dict) -> None:
    if report.get("schema_version") != 1 or report.get("status") != "complete":
        raise ValueError(f"No complete schema-1 result: status={report.get('status')}, error={report.get('error')}")
    for field in ("map", "seed", "economy"):
        if report.get(field) != job[field]:
            raise ValueError(f"Telemetry {field} does not match launched job")
    duration = number(report.get("duration"), "duration")
    if duration <= 0 or duration > job["time_cap"] + .1:
        raise ValueError("Duration outside game-time cap")
    number(report.get("wall_duration"), "wall duration")
    for field in ("regions", "deposits", "unit_definitions"):
        if not isinstance(report.get(field), list) or not report[field]:
            raise ValueError(f"Missing layout/content metadata: {field}")
    for team in (0, 5):
        position = report.get(f"hq_position_team{team}")
        if not isinstance(position, list) or len(position) != 3:
            raise ValueError("Missing HQ position metadata")
        for coordinate in position:
            number(coordinate, "HQ coordinate")
    if abs(number(report.get("time_cap_seconds"), "time cap") - job["time_cap"]) > .001:
        raise ValueError("Telemetry time cap differs from request")
    for field in ("requested_dilation", "effective_dilation"):
        if abs(number(report.get(field), field) - job["dilation"]) > .001:
            raise ValueError(f"{field} differs from request (engine may have clamped it)")
    if number(report.get("max_game_delta_seconds"), "game delta") > 1 / 60 + .0001:
        raise ValueError("Game delta exceeded fixed 60 Hz contract")
    snapshots = report.get("snapshots")
    if not isinstance(snapshots, list) or not snapshots:
        raise ValueError("Missing snapshots")
    previous = -1.0
    for snapshot in snapshots:
        current = number(snapshot.get("time"), "snapshot time")
        if current <= previous or current > duration + .001:
            raise ValueError("Snapshot times not strictly increasing or exceed match duration")
        previous = current
        if not isinstance(snapshot.get("teams"), list) or {team.get("team") for team in snapshot["teams"]} != {0, 5} or len(snapshot["teams"]) != 2:
            raise ValueError("Snapshot must contain exactly teams 0 and 5")
        for team in snapshot["teams"]:
            for field in TEAM_FIELDS:
                if number(team.get(field), field) < 0:
                    raise ValueError(f"Negative telemetry {field}")
            if not isinstance(team.get("units_by_role"), list) or len(team["units_by_role"]) != 3:
                raise ValueError("Missing role counts")
            if sum(number(count, "role count") for count in team["units_by_role"]) != team["units_alive"]:
                raise ValueError("Role counts disagree with living unit count")
            for field in ("units_by_region", "buildings", "forces", "region_indices"):
                if field not in team:
                    raise ValueError(f"Missing concentration/production metadata: {field}")
        if not isinstance(snapshot.get("deposits"), list):
            raise ValueError("Missing per-deposit history")
    if abs(snapshots[0]["time"]) > .001 or abs(snapshots[-1]["time"] - duration) > .001:
        raise ValueError("Missing initial or terminal snapshot")
    sampled = {int(round(snapshot["scheduled_time"] / 30)) for snapshot in snapshots
               if abs(snapshot["scheduled_time"] / 30 - round(snapshot["scheduled_time"] / 30)) < .0001}
    # Outcome checks precede periodic sampling; a terminal state within a game frame
    # of a boundary is that boundary's observation, not a missing historical sample.
    if abs(duration - round(duration / 30) * 30) <= .05:
        sampled.add(int(round(duration / 30)))
    for boundary in range(1, math.ceil(duration / 30)):
        if boundary not in sampled:
            raise ValueError(f"Missing 30-second sample {boundary * 30}")
    if not isinstance(report.get("events"), list):
        raise ValueError("Missing event history")
    for event in report["events"]:
        if not 0 <= number(event.get("time"), "event time") <= duration + .001:
            raise ValueError("Event time outside match")
    final = {team["team"]: team for team in snapshots[-1]["teams"]}
    if report.get("outcome") == "time_cap":
        if report.get("winner") is not None or duration < job["time_cap"] - .02 or any(final[team]["hq_health"] <= 0 for team in (0, 5)):
            raise ValueError("Invalid time-cap draw")
    elif report.get("outcome") == "hq_destroyed":
        winner = report.get("winner")
        if winner not in (0, 5) or final[5 if winner == 0 else 0]["hq_health"] > 0:
            raise ValueError("Winner has no destroyed opposing HQ")
        if final[0]["hq_health"] <= 0 and winner != 5:
            raise ValueError("Simultaneous HQ loss must follow game's team-5 tie precedence")
    else:
        raise ValueError("Unknown outcome, not a result")


def artifact_identity(args: argparse.Namespace) -> list[dict]:
    executable = latest_package(ROOT, "development", load(ROOT).game_target) if args.package else Path(os.environ["UE_ROOT"]) / "Engine/Binaries/Linux/UnrealEditor"
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


def command(args: argparse.Namespace, job: dict, output: Path) -> list[str]:
    if args.package:
        result = [str(latest_package(ROOT, "development", load(ROOT).game_target)), job["map"]]
    else:
        engine = Path(os.environ["UE_ROOT"]) / "Engine/Binaries/Linux/UnrealEditor"
        result = [str(engine), str(ROOT / "CoopRTS.uproject"), job["map"], "-game", MCP_DISABLED]
    result += ["-nullrhi", "-nosound", "-nosplash", "-unattended", "-nosteam", "-autopilot",
               f"-SimSeed={job['seed']}", f"-SimTimeCap={job['time_cap']}", f"-SimDilation={job['dilation']}",
               f"-SimOutput={output}", f"-abslog={output.parent / 'game.log'}", "-ExecCmds=t.MaxFPS 0"]
    result += [f"-{ECONOMY_FLAGS[key]}={value}" for key, value in job["economy"].items()]
    return result


def stop_owned(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    # Popen starts a private session. Never select other UE processes by name.
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def run_match(args: argparse.Namespace, job: dict, directory: Path, artifacts: list[dict]) -> dict:
    directory.mkdir()
    output = directory / "match.json"
    launch = command(args, job, output)
    record = dict(job=job, directory=str(directory), command=launch, artifacts=artifacts, status="failed")
    save_json(directory / "launch.json", record)
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
                process = subprocess.Popen(launch, cwd=ROOT, stdout=stdout, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    record["pid"] = process.pid
                    save_json(directory / "launch.json", record)
                    last_progress = time.monotonic()
                    previous_duration = -1.0
                    previous_stamp = None
                    while process.poll() is None:
                        time.sleep(.5)
                        if output.exists():
                            current_stamp = output.stat().st_mtime_ns
                            if current_stamp != previous_stamp:
                                checkpoint = json.loads(output.read_text())
                                duration = number(checkpoint.get("duration"), "checkpoint duration")
                                if duration > previous_duration:
                                    last_progress = time.monotonic()
                                    previous_duration = duration
                                    print(f"  {directory.name}: {duration:.1f} game seconds", flush=True)
                                previous_stamp = current_stamp
                        if time.monotonic() - last_progress > args.stall_seconds:
                            raise ValueError(f"No persisted game-time progress for {args.stall_seconds:g}s; watchdog failure, not draw")
                    record["returncode"] = process.wait()
                finally:
                    # Hold the shared lock until the owned game has actually stopped.
                    stop_owned(process)
            if record["returncode"] != 0:
                raise ValueError(f"Game exited {record['returncode']}; no valid result")
            if [stamp(Path(item["path"])) for item in artifacts] != artifacts:
                raise ValueError("Gameplay artifact changed during match")
            report = json.loads(output.read_text())
            validate_report(report, job)
            record["status"] = "complete"
            record["outcome"] = report["outcome"]
            record["winner"] = report["winner"]
            record["duration"] = report["duration"]
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


def load_results(run: Path, manifest: dict) -> tuple[list[tuple[dict, dict]], list[dict]]:
    valid, failed = [], []
    for record in manifest["matches"]:
        if record.get("status") != "complete":
            failed.append(record)
            continue
        try:
            directory = Path(record["directory"])
            # Evidence directories may be moved together after the run.
            report = json.loads((run / directory.name / "match.json").read_text())
            if record.get("returncode") != 0:
                raise ValueError("Recorded process did not exit zero")
            validate_report(report, record["job"])
            valid.append((record, report))
        except (OSError, ValueError, KeyError, TypeError) as error:
            failed.append(dict(record, status="failed", error=str(error)))
    return valid, failed


def teams(snapshot: dict) -> dict[int, dict]:
    return {team["team"]: team for team in snapshot["teams"]}


def compare_pair(one: dict, high: dict, tolerance: float) -> dict:
    issues, deltas = [], {}
    one_samples = {round(row["scheduled_time"], 3): row for row in one["snapshots"]}
    high_samples = {round(row["scheduled_time"], 3): row for row in high["snapshots"]}
    # Terminal timestamps can differ slightly; comparison uses the fixed scheduled boundaries.
    common = sorted(key for key in one_samples.keys() & high_samples.keys() if key % 30 == 0)
    if len(common) < 2:
        issues.append("Insufficient shared 30-second samples")
    sample_pairs = [(str(sample), one_samples[sample], high_samples[sample]) for sample in common]
    sample_pairs.append(("terminal", one["snapshots"][-1], high["snapshots"][-1]))
    if abs(one["duration"] - high["duration"]) > 2 + tolerance * max(one["duration"], high["duration"]):
        issues.append("Terminal duration differs beyond comparison tolerance")
    for sample, one_snapshot, high_snapshot in sample_pairs:
        a, b = teams(one_snapshot), teams(high_snapshot)
        for team in (0, 5):
            for field in COMPARISON_FIELDS:
                left = a[team][field]
                right = b[team][field]
                left = left if isinstance(left, list) else [left]
                right = right if isinstance(right, list) else [right]
                for index, (x, y) in enumerate(zip(left, right)):
                    delta = abs(x - y)
                    key = f"team{team}.{field}" + (f"[{index}]" if len(left) > 1 else "")
                    deltas[key] = max(deltas.get(key, 0), delta)
                    absolute = .02 if field == "largest_region_unit_share" else 1
                    if delta > absolute + tolerance * max(abs(x), abs(y)):
                        issues.append(f"t={sample} {key}: {x} vs {y}")
    for report in (one, high):
        final = teams(report["snapshots"][-1])
        if any(final[team]["units_produced"] == 0 or final[team]["attacks_observed"] == 0 for team in (0, 5)):
            issues.append("Sample has not exercised paid production and combat on both sides")
        if not any(event["kind"] == "hq_damage" for event in report["events"]):
            issues.append("Sample has not exercised HQ combat")
        if not any(event["kind"] == "first_capture" for event in report["events"]):
            issues.append("Sample has not exercised capture")
    captures = [[(event["team"], event["region"]) for event in report["events"] if event["kind"] == "region_control"] for report in (one, high)]
    if captures[0] != captures[1]:
        issues.append("Region-control transition sequence differs")
    kinds = {"first_extractor", "first_extractor_complete", "first_barracks", "first_barracks_complete", "first_capture"}
    first_events = [{(event["kind"], event["team"]): event["time"] for event in report["events"] if event["kind"] in kinds} for report in (one, high)]
    if first_events[0].keys() != first_events[1].keys():
        issues.append("First economy/production/capture events differ")
    for key in first_events[0].keys() & first_events[1].keys():
        if abs(first_events[0][key] - first_events[1][key]) > 2:
            issues.append(f"First event {key} differs by more than 2 game seconds")
    if one["winner"] != high["winner"] or one["outcome"] != high["outcome"]:
        issues.append("Sample outcome differs")
    return dict(status="pass" if not issues else "fail_or_inconclusive", issues=issues, max_absolute_deltas=deltas,
                shared_samples=len(common), relative_tolerance=tolerance, absolute_count_tolerance=1)


def symmetry_metadata(report: dict) -> dict:
    regions = {row["index"]: row for row in report["regions"]}
    result = {}
    for team in (0, 5):
        hq = report[f"hq_position_team{team}"]
        distances = [math.dist(hq[:2], row["anchor"][:2]) for row in regions.values()
                     if row["role"] == 1 and row["home_team"] == team]
        main = next((row["index"] for row in regions.values() if row["role"] == 0 and row["home_team"] == team), None)
        # Some legacy natural regions have no home-team tag; nearest natural is explicit instead.
        if not distances:
            distances = [math.dist(hq[:2], row["anchor"][:2]) for row in regions.values() if row["role"] == 1]
        hops = {main: 0} if main is not None else {}
        queue = collections.deque(hops)
        while queue:
            current = queue.popleft()
            for neighbour in regions[current]["neighbours"]:
                if neighbour in regions and neighbour not in hops:
                    hops[neighbour] = hops[current] + 1
                    queue.append(neighbour)
        rewards = {str(index): hops.get(index) for index, row in regions.items() if row["role"] == 2}
        result[str(team)] = dict(hq=hq, nearest_natural_cm=min(distances) if distances else None, reward_hops=rewards)
    return result


def summarize(run: Path, manifest: dict) -> bool:
    valid, failed = load_results(run, manifest)
    groups = collections.defaultdict(list)
    for record, report in valid:
        key = (record["job"]["map"], record["job"]["variant"], record["job"]["dilation"])
        groups[key].append(report)
    lines = ["# AI-vs-AI simulation report", "", f"Run: `{run.name}`. Complete matches: **{len(valid)}**; failed/interrupted: **{len(failed)}**.",
             "Failures never enter win, draw, duration or curve denominators. JSON, launch identity, stdout and game logs remain beside this report.", "",
             "Fixed 60 Hz game steps, unlimited headless wall-clock throughput; dilation is not permitted to coarsen combat/production ticks.",
             "Seeds initialize UE global random streams; asynchronous navigation and actor ordering are not guaranteed deterministic. Identical outcomes across seeds are not independent statistical evidence.", "",
             "| Map | Variant | Dilation | Complete | Team 0 wins | Team 5 wins | Draws | Median game min | Median wall s | Median first depletion min |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    summaries = []
    for (map_name, variant, dilation), reports in sorted(groups.items()):
        n = len(reports)
        wins0 = sum(row["winner"] == 0 for row in reports)
        wins5 = sum(row["winner"] == 5 for row in reports)
        draws = sum(row["outcome"] == "time_cap" for row in reports)
        depleted = [min(event["time"] for event in row["events"] if event["kind"] == "deposit_depleted") / 60
                    for row in reports if any(event["kind"] == "deposit_depleted" for event in row["events"])]
        median_duration = statistics.median(row["duration"] for row in reports) / 60
        wall = statistics.median(row["wall_duration"] for row in reports)
        depletion = f"{statistics.median(depleted):.2f} ({len(depleted)}/{n})" if depleted else "not reached"
        lines.append(f"| {map_name} | {variant} | {dilation:g}× | {n} | {wins0}/{n} ({wins0/n:.0%}) | {wins5}/{n} ({wins5/n:.0%}) | {draws} | {median_duration:.2f} | {wall:.1f} | {depletion} |")
        summaries.append(dict(map=map_name, variant=variant, dilation=dilation, complete=n, team0_wins=wins0,
                              team5_wins=wins5, draws=draws, median_duration_minutes=median_duration,
                              median_wall_seconds=wall, depletion_matches=len(depleted)))
    lines += ["", "The pacing target is 12–18 game minutes. Draw durations are censored by the cap, not measured victory times.",
              "~50% side wins is an expectation only for actually symmetric maps and identical conditions. V2's HQ positions, travel distances and region geometry are asymmetric; do not label a side bias an AI bug without inspecting these metadata and planner tick ordering.", "",
              "## Layout and concentration evidence", "",
              "Largest-region unit share (only snapshots with ≥12 living units) is a spatial concentration proxy, not proof that deathballs win or that human co-op is readable.",
              "`deposits_remaining` is reserve in currently controlled regions (ownership changes can move it); per-deposit histories and `deposit_depleted` events are the depletion evidence.",
              "Observed unit health loss is a lower bound: damage and repairs within a tick, or a fatal hit before actor removal, may not be visible. Casualties/births are observed each tick. Attack counters count shots, not successful damage.", ""]
    layouts = {}
    for key, reports in sorted(groups.items()):
        layout = symmetry_metadata(reports[0])
        layouts[key[0]] = layout
        for team in (0, 5):
            metadata = layout[str(team)]
            natural = metadata["nearest_natural_cm"]
            concentration = [teams(snapshot)[team]["largest_region_unit_share"] for row in reports for snapshot in row["snapshots"]
                             if teams(snapshot)[team]["units_alive"] >= 12]
            share = f"{statistics.median(concentration):.1%}" if concentration else "not reached"
            lines.append(f"- `{key[0]}` `{key[1]}` {key[2]:g}× team {team}: HQ `{metadata['hq']}`, nearest natural **{natural:.0f} cm**" if natural is not None else f"- `{key[0]}` `{key[1]}` team {team}: no natural region metadata")
            lines.append(f"  Reward graph hops: `{metadata['reward_hops']}`; median ≥12-unit largest-region share: **{share}**.")
    comparisons = []
    if manifest.get("comparison_dilation"):
        pairs = collections.defaultdict(dict)
        for record, report in valid:
            job = record["job"]
            pairs[(job["map"], job["variant"], job["seed"])][job["dilation"]] = report
        expected_pairs = {(job["map"], job["variant"], job["seed"]) for job in manifest["planned_jobs"]}
        for key in sorted(expected_pairs):
            pair = pairs[key]
            if 1.0 not in pair or manifest["comparison_dilation"] not in pair:
                comparison = dict(status="fail_or_inconclusive", issues=["Missing valid paired match"])
            else:
                comparison = compare_pair(pair[1.0], pair[manifest["comparison_dilation"]], manifest["comparison_tolerance"])
            comparisons.append(dict(map=key[0], variant=key[1], seed=key[2], **comparison))
        lines += ["", "## Paired dilation sample", "",
                  "Comparison requires paid production, attacks on both sides, HQ damage and capture. Zero-combat samples are inconclusive, never passes. Uses shared 30s boundaries, 5% relative +1 count tolerance by default, exact capture transition sequence and ≤2s first-event timing."]
        for item in comparisons:
            lines.append(f"- `{item['map']}` `{item['variant']}` seed {item['seed']}: **{item['status']}**")
            lines.extend(f"  - {issue}" for issue in item["issues"][:12])
    if failed:
        lines += ["", "## Failed or interrupted attempts", ""]
        lines.extend(f"- `{Path(row['directory']).name}`: {row.get('error', row.get('status'))}" for row in failed)
    if len(manifest["matches"]) < len(manifest["planned_jobs"]):
        lines += ["", f"**Incomplete batch:** {len(manifest['planned_jobs']) - len(manifest['matches'])} planned matches were not attempted."]
    if valid:
        make_charts(run, groups)
        lines += ["", "## Curves", "", "Curves average surviving matches at each scheduled sample; later samples have fewer contributors after HQ endings (survivorship bias).", "",
                  "![Income](income.png)", "", "![Units](units.png)", "", "![Concentration](concentration.png)", "", "![Duration](duration.png)"]
    save_json(run / "summary.json", dict(groups=summaries, layouts=layouts, failures=failed, dilation_comparisons=comparisons))
    (run / "Report.md").write_text("\n".join(lines) + "\n")
    return not failed and len(manifest["matches"]) == len(manifest["planned_jobs"]) and all(row["status"] == "pass" for row in comparisons)


def make_charts(run: Path, groups: dict) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    for filename, field, label in (("income", "income_per_second", "Power / game second"),
                                   ("units", "units_alive", "Living units"),
                                   ("concentration", "largest_region_unit_share", "Largest-region unit share")):
        fig, axis = plt.subplots(figsize=(11, 6))
        for (map_name, variant, dilation), reports in sorted(groups.items()):
            for team in (0, 5):
                buckets = collections.defaultdict(list)
                for report in reports:
                    for snapshot in report["snapshots"]:
                        scheduled = snapshot["scheduled_time"]
                        if abs(scheduled / 30 - round(scheduled / 30)) > .0001:
                            continue
                        summary = teams(snapshot)[team]
                        if field == "largest_region_unit_share" and summary["units_alive"] < 12:
                            continue
                        buckets[scheduled].append(summary[field])
                times = sorted(buckets)
                axis.plot([value / 60 for value in times], [statistics.mean(buckets[value]) for value in times],
                          label=f"{map_name.rsplit('/', 1)[-1]} {variant} {dilation:g}× T{team}", linestyle="-" if team == 0 else "--")
        axis.set(xlabel="Game minutes", ylabel=label, title=f"{label} (active-match mean)")
        axis.grid(alpha=.25)
        axis.legend(fontsize=7)
        fig.tight_layout()
        fig.savefig(run / f"{filename}.png", dpi=140)
        plt.close(fig)
    fig, axis = plt.subplots(figsize=(11, 6))
    labels, medians = [], []
    for (map_name, variant, dilation), reports in sorted(groups.items()):
        labels.append(f"{map_name.rsplit('/', 1)[-1]}\n{variant} {dilation:g}×")
        medians.append(statistics.median(row["duration"] for row in reports) / 60)
    axis.bar(labels, medians)
    axis.axhspan(12, 18, alpha=.15, color="green", label="12–18 min target")
    axis.set(ylabel="Median game minutes (draws censored)", title="Match duration")
    axis.legend()
    fig.tight_layout()
    fig.savefig(run / "duration.png", dpi=140)
    plt.close(fig)


def main() -> int:
    if not os.environ.get("X_RUN_ID"):
        raise ValueError("use ./x sim")
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    if args.variant:
        args.variant = [parse_variant(text) for text in args.variant]
    if args.report_only:
        run = args.report_only.resolve()
        return 0 if summarize(run, json.loads((run / "run.json").read_text())) else 1
    if args.matches <= 0 or not 0 <= args.seed <= 2147483647 - args.matches + 1:
        parser.error("matches must be positive and seeds within signed int32")
    if any(not math.isfinite(value) for value in (args.time_cap, args.sample_seconds, args.dilation, args.stall_seconds, args.comparison_tolerance)):
        parser.error("Numeric parameters must be finite")
    if not 1 <= args.time_cap <= 86400 or not 1 <= args.sample_seconds <= 86400 or not 1 <= args.dilation <= 32:
        parser.error("time caps must be 1..86400s and dilation 1..32")
    if args.compare_dilation is not None and (not math.isfinite(args.compare_dilation) or not 1 < args.compare_dilation <= 32):
        parser.error("comparison dilation must be >1 and <=32")
    if args.stall_seconds < 60 or not 0 <= args.comparison_tolerance <= 1:
        parser.error("stall interval must be >=60s; comparison tolerance must be 0..1")
    if args.matrix and (args.maps or args.variant):
        parser.error("--matrix already defines maps and variants")
    maps = args.maps or [MAP_V2, MAP_V1]
    if len(set(maps)) != len(maps) or any(not re.fullmatch(r"/Game/[A-Za-z0-9_/]+", name) for name in maps):
        parser.error("Maps must be unique /Game/... package paths without URL options or extensions")
    variants = args.variant or [parse_variant("baseline2")]
    if len({name for name, _ in variants}) != len(variants):
        parser.error("Variant names must be unique")
    combinations = [(MAP_V2, parse_variant(name)) for name in ("baseline2", "baseline3", "baseline4")] + [(MAP_V1, parse_variant("baseline2"))] if args.matrix else [(map_name, variant) for map_name in maps for variant in variants]
    dilation_values = [1.0, args.compare_dilation] if args.compare_dilation else [args.dilation]
    cap = args.sample_seconds if args.compare_dilation else args.time_cap
    jobs = [dict(map=map_name, variant=name, economy=economy, seed=seed, dilation=dilation, time_cap=cap)
            for map_name, (name, economy) in combinations for seed in range(args.seed, args.seed + args.matches) for dilation in dilation_values]
    artifacts = artifact_identity(args)
    run = Path(os.environ["X_RUN_DIR"]).resolve()
    manifest = dict(schema_version=1, created=dt.datetime.now(dt.timezone.utc).isoformat(), artifacts=artifacts,
                    planned_jobs=jobs, matches=[], comparison_dilation=args.compare_dilation,
                    comparison_tolerance=args.comparison_tolerance, lock=str(load(ROOT).lock_dir))
    save_json(run / "run.json", manifest)
    interrupted = False
    try:
        for index, job in enumerate(jobs):
            map_id = job["map"].rsplit("/", 1)[-1]
            directory = run / f"{index + 1:03d}-{map_id}-{job['variant']}-seed{job['seed']}-x{job['dilation']:g}"
            print(f"MATCH {index + 1}/{len(jobs)} {directory.name}", flush=True)
            try:
                record = run_match(args, job, directory, artifacts)
            except KeyboardInterrupt:
                record = json.loads((directory / "launch.json").read_text())
                manifest["matches"].append(record)
                interrupted = True
                break
            manifest["matches"].append(record)
            save_json(run / "run.json", manifest)
    finally:
        save_json(run / "run.json", manifest)
    success = summarize(run, manifest)
    print(f"Report: {run / 'Report.md'}", flush=True)
    return 130 if interrupted else 0 if success else 1


if __name__ == "__main__":
    def interrupted(signum: int, frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Simulation runner failed: {error}", file=sys.stderr)
        raise SystemExit(1)
