"""Paired dilation comparison, layout symmetry evidence, battle-length statistics and the 1b gate."""

from __future__ import annotations

import collections
import math
import statistics

from harness.simulation_evidence import teams
from harness.simulation_planning import GATE_SEEDS, GATE_TIME_CAP, MAP_V2
from harness.simulation_validation import COMPARISON_FIELDS, interpret_outcome
from harness.verify import JsonObject

# Decision G1: median battle length 8-12 min, and no rush victory before 360 s.
LENGTH_BAND_MINUTES = (8.0, 12.0)
RUSH_FLOOR_SECONDS = 360.0
REQUIRED_HUMAN_BASELINES = (2, 1)
PASS, FAIL, INSUFFICIENT = "PASS", "FAIL", "INSUFFICIENT"


def compare_pair(one: JsonObject, high: JsonObject, tolerance: float) -> JsonObject:
    issues: list[str] = []
    deltas: dict[str, float] = {}
    one_samples = {round(row["scheduled_time"], 3): row for row in one["snapshots"]}
    high_samples = {round(row["scheduled_time"], 3): row for row in high["snapshots"]}
    # Terminal timestamps can differ slightly; comparison uses the fixed scheduled boundaries.
    common = sorted(
        key for key in one_samples.keys() & high_samples.keys() if key % 30 == 0
    )
    if len(common) < 2:
        issues.append("Insufficient shared 30-second samples")
    sample_pairs = [
        (str(sample), one_samples[sample], high_samples[sample]) for sample in common
    ]
    sample_pairs.append(("terminal", one["snapshots"][-1], high["snapshots"][-1]))
    if abs(one["duration"] - high["duration"]) > 2 + tolerance * max(
        one["duration"], high["duration"]
    ):
        issues.append("Terminal duration differs beyond comparison tolerance")
    compare_samples(sample_pairs, tolerance, issues, deltas)
    compare_events(one, high, issues)
    if one["winner"] != high["winner"] or one["outcome"] != high["outcome"]:
        issues.append("Sample outcome differs")
    return dict(
        status="pass" if not issues else "fail_or_inconclusive",
        issues=issues,
        max_absolute_deltas=deltas,
        shared_samples=len(common),
        relative_tolerance=tolerance,
        absolute_count_tolerance=1,
    )


def compare_samples(
    sample_pairs: list[tuple[str, JsonObject, JsonObject]],
    tolerance: float,
    issues: list[str],
    deltas: dict[str, float],
) -> None:
    for sample, one_snapshot, high_snapshot in sample_pairs:
        a, b = teams(one_snapshot), teams(high_snapshot)
        for team in (0, 5):
            for field in COMPARISON_FIELDS:
                left = a[team][field]
                right = b[team][field]
                left = left if isinstance(left, list) else [left]
                right = right if isinstance(right, list) else [right]
                for index, (x, y) in enumerate(zip(left, right, strict=False)):
                    delta = abs(x - y)
                    key = f"team{team}.{field}" + (
                        f"[{index}]" if len(left) > 1 else ""
                    )
                    deltas[key] = max(deltas.get(key, 0), delta)
                    absolute = 0.02 if field == "largest_region_unit_share" else 1
                    if delta > absolute + tolerance * max(abs(x), abs(y)):
                        issues.append(f"t={sample} {key}: {x} vs {y}")


def compare_events(one: JsonObject, high: JsonObject, issues: list[str]) -> None:
    for report in (one, high):
        final = teams(report["snapshots"][-1])
        if any(
            final[team]["units_produced"] == 0 or final[team]["attacks_observed"] == 0
            for team in (0, 5)
        ):
            issues.append(
                "Sample has not exercised paid production and combat on both sides"
            )
        if not any(event["kind"] == "hq_damage" for event in report["events"]):
            issues.append("Sample has not exercised HQ combat")
        if not any(event["kind"] == "first_capture" for event in report["events"]):
            issues.append("Sample has not exercised capture")
    captures = [
        [
            (event["team"], event["region"])
            for event in report["events"]
            if event["kind"] == "region_control"
        ]
        for report in (one, high)
    ]
    if captures[0] != captures[1]:
        issues.append("Region-control transition sequence differs")
    kinds = {
        "first_extractor",
        "first_extractor_complete",
        "first_barracks",
        "first_barracks_complete",
        "first_capture",
    }
    first_events = [
        {
            (event["kind"], event["team"]): event["time"]
            for event in report["events"]
            if event["kind"] in kinds
        }
        for report in (one, high)
    ]
    if first_events[0].keys() != first_events[1].keys():
        issues.append("First economy/production/capture events differ")
    for event_key in first_events[0].keys() & first_events[1].keys():
        if abs(first_events[0][event_key] - first_events[1][event_key]) > 2:
            issues.append(
                f"First event {event_key} differs by more than 2 game seconds"
            )


def symmetry_metadata(report: JsonObject) -> JsonObject:
    regions = {row["index"]: row for row in report["regions"]}
    result = {}
    for team in (0, 5):
        hq = report[f"hq_position_team{team}"]
        distances = [
            math.dist(hq[:2], row["anchor"][:2])
            for row in regions.values()
            if row["role"] == 1 and row["home_team"] == team
        ]
        main = next(
            (
                row["index"]
                for row in regions.values()
                if row["role"] == 0 and row["home_team"] == team
            ),
            None,
        )
        # Some legacy natural regions have no home-team tag; nearest natural is explicit instead.
        if not distances:
            distances = [
                math.dist(hq[:2], row["anchor"][:2])
                for row in regions.values()
                if row["role"] == 1
            ]
        hops = {main: 0} if main is not None else {}
        queue = collections.deque(hops)
        while queue:
            current = queue.popleft()
            for neighbour in regions[current]["neighbours"]:
                if neighbour in regions and neighbour not in hops:
                    hops[neighbour] = hops[current] + 1
                    queue.append(neighbour)
        rewards = {
            str(index): hops.get(index)
            for index, row in regions.items()
            if row["role"] == 2
        }
        result[str(team)] = dict(
            hq=hq,
            nearest_natural_cm=min(distances) if distances else None,
            reward_hops=rewards,
        )
    return result


def battle_statistics(reports: list[JsonObject]) -> JsonObject:
    """Decisive and censored (time-cap) results, never blended without saying so."""
    outcomes = [(interpret_outcome(report), report) for report in reports]
    decisive = [outcome.seconds for outcome, _ in outcomes if outcome.decisive]
    capped = [
        float(report["time_cap_seconds"])
        for outcome, report in outcomes
        if not outcome.decisive
    ]
    victories = [
        outcome.seconds
        for outcome, _ in outcomes
        if outcome.decisive and outcome.winner == 0
    ]
    return dict(
        matches=len(reports),
        decisive=len(decisive),
        decisive_median_seconds=statistics.median(decisive) if decisive else None,
        censored=len(capped),
        # Censored matches count at their cap: a lower bound on their true length.
        median_with_censored_seconds=statistics.median(decisive + capped)
        if reports
        else None,
        earliest_victory_seconds=min(victories) if victories else None,
    )


def rush_evidence(reports: list[JsonObject]) -> JsonObject:
    """How fast rush forces got their Attack order toward JEV's HQ after first living."""
    seen = 0
    delays: list[float] = []
    for report in reports:
        first: dict[str, float] = {}
        ordered: dict[str, float] = {}
        for event in report["events"]:
            if event["kind"] == "rush_force_seen":
                first.setdefault(event["id"], event["time"])
            elif event["kind"] == "rush_force_attacking":
                ordered.setdefault(event["id"], event["time"])
        seen += len(first)
        delays += [ordered[key] - first[key] for key in first if key in ordered]
    return dict(
        forces_seen=seen,
        forces_attacking=len(delays),
        median_order_delay_seconds=statistics.median(delays) if delays else None,
        max_order_delay_seconds=max(delays) if delays else None,
    )


def _cell_problem(cell: list[tuple[JsonObject, JsonObject]]) -> str | None:
    if not cell:
        return "no valid matches"
    caps = {job["time_cap"] for job, _ in cell}
    if caps != {GATE_TIME_CAP}:
        listed = ", ".join(f"{cap:g}" for cap in sorted(caps))
        return f"time cap {listed} s, the gate needs {GATE_TIME_CAP:g} s"
    if len(cell) < GATE_SEEDS:
        return f"{len(cell)} seeds, the gate needs at least {GATE_SEEDS}"
    return None


def _check(name: str, status: str, detail: str) -> JsonObject:
    return dict(check=name, status=status, detail=detail)


def _length_check(
    variant: str, cell: list[tuple[JsonObject, JsonObject]]
) -> JsonObject:
    name = f"{variant}: median battle length {LENGTH_BAND_MINUTES[0]:g}-{LENGTH_BAND_MINUTES[1]:g} min"
    problem = _cell_problem(cell)
    if problem:
        return _check(name, INSUFFICIENT, problem)
    stats = battle_statistics([report for _, report in cell])
    minutes = stats["median_with_censored_seconds"] / 60
    within = LENGTH_BAND_MINUTES[0] <= minutes <= LENGTH_BAND_MINUTES[1]
    detail = (
        f"median {minutes:.2f} min over {stats['matches']} seeds "
        f"({stats['censored']} censored at the cap counted at {GATE_TIME_CAP:g} s)"
    )
    return _check(name, PASS if within else FAIL, detail)


def _rush_check(variant: str, cell: list[tuple[JsonObject, JsonObject]]) -> JsonObject:
    name = f"{variant}: no rush victory before {RUSH_FLOOR_SECONDS:g} s"
    earliest = battle_statistics([report for _, report in cell])[
        "earliest_victory_seconds"
    ]
    # One early victory refutes the gate at any sample size; only a clean result needs the seeds.
    if earliest is not None and earliest < RUSH_FLOOR_SECONDS:
        return _check(name, FAIL, f"rush victory at {earliest:.1f} s")
    problem = _cell_problem(cell)
    if problem:
        return _check(name, INSUFFICIENT, problem)
    detail = (
        f"earliest rush victory {earliest:.1f} s over {len(cell)} seeds"
        if earliest is not None
        else f"no rush victory in {len(cell)} seeds"
    )
    return _check(name, PASS, detail)


def evaluate_gate_1b(valid: list[tuple[JsonObject, JsonObject]]) -> JsonObject:
    """PASS needs both baselines, both scenarios, every cell at the G1 sample size and cap."""
    cells: dict[tuple[str, str], list[tuple[JsonObject, JsonObject]]] = (
        collections.defaultdict(list)
    )
    baselines: set[int] = set()
    for record, report in valid:
        job = record["job"]
        if job["map"] != MAP_V2:
            continue
        scenario = job.get("scenario", "default")
        cells[(job["variant"], scenario)].append((job, report))
        baselines.add(job["economy"]["human_baseline"])
    checks: list[JsonObject] = []
    for baseline in REQUIRED_HUMAN_BASELINES:
        if baseline not in baselines:
            checks.append(
                _check(
                    f"{baseline}/s baseline measured",
                    INSUFFICIENT,
                    "no valid matches on V2 for this human baseline",
                )
            )
    for variant in sorted({variant for variant, _ in cells}):
        checks.append(_length_check(variant, cells.get((variant, "default"), [])))
        checks.append(_rush_check(variant, cells.get((variant, "rush"), [])))
    if not cells:
        checks.append(_check("V2 matches", INSUFFICIENT, "no valid matches on V2"))
    statuses = {row["status"] for row in checks}
    status = (
        FAIL if FAIL in statuses else INSUFFICIENT if INSUFFICIENT in statuses else PASS
    )
    return dict(gate="1b", status=status, seeds_required=GATE_SEEDS, checks=checks)
