"""Paired dilation comparison and layout symmetry evidence."""

from __future__ import annotations

import collections
import math

from harness.simulation_evidence import teams
from harness.simulation_validation import COMPARISON_FIELDS
from harness.verify import JsonObject


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
