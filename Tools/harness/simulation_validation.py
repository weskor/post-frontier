"""Validate complete simulation telemetry before admitting a match to reports."""

from __future__ import annotations

import math

from harness.verify import JsonObject

TEAM_FIELDS = (
    "wallet",
    "income_per_second",
    "extractors",
    "completed_extractors",
    "deposits_remaining",
    "barracks",
    "units_alive",
    "regions_controlled",
    "hq_health",
    "units_produced",
    "casualties_observed",
    "unit_health_loss_observed",
    "attacks_observed",
    "units_reinforcing",
    "largest_region_unit_share",
)
COMPARISON_FIELDS = (*TEAM_FIELDS, "units_by_role")


def number(value: object, label: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
    ):
        raise ValueError(f"{label} must be a finite number")
    return float(value)


def validate_report(report: JsonObject, job: JsonObject) -> None:
    if job.get("mode") == "duel":
        from harness.simulation_duel_validation import validate_duel_report

        validate_duel_report(report, job)
        return
    if report.get("schema_version") != 1 or report.get("status") != "complete":
        raise ValueError(
            f"No complete schema-1 result: status={report.get('status')}, error={report.get('error')}"
        )
    for field in ("map", "seed", "economy"):
        if report.get(field) != job[field]:
            raise ValueError(f"Telemetry {field} does not match launched job")
    duration = number(report.get("duration"), "duration")
    if duration <= 0 or duration > job["time_cap"] + 0.1:
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
    if (
        abs(number(report.get("time_cap_seconds"), "time cap") - job["time_cap"])
        > 0.001
    ):
        raise ValueError("Telemetry time cap differs from request")
    for field in ("requested_dilation", "effective_dilation"):
        if abs(number(report.get(field), field) - job["dilation"]) > 0.001:
            raise ValueError(
                f"{field} differs from request (engine may have clamped it)"
            )
    if number(report.get("max_game_delta_seconds"), "game delta") > 1 / 60 + 0.0001:
        raise ValueError("Game delta exceeded fixed 60 Hz contract")
    validate_snapshots(report, duration)
    validate_outcome(report, job, duration)


def validate_snapshots(report: JsonObject, duration: float) -> None:
    snapshots = report.get("snapshots")
    if not isinstance(snapshots, list) or not snapshots:
        raise ValueError("Missing snapshots")
    previous = -1.0
    for snapshot in snapshots:
        current = number(snapshot.get("time"), "snapshot time")
        if current <= previous or current > duration + 0.001:
            raise ValueError(
                "Snapshot times not strictly increasing or exceed match duration"
            )
        previous = current
        if (
            not isinstance(snapshot.get("teams"), list)
            or {team.get("team") for team in snapshot["teams"]} != {0, 5}
            or len(snapshot["teams"]) != 2
        ):
            raise ValueError("Snapshot must contain exactly teams 0 and 5")
        validate_teams(snapshot)
        if not isinstance(snapshot.get("deposits"), list):
            raise ValueError("Missing per-deposit history")
    if (
        abs(snapshots[0]["time"]) > 0.001
        or abs(snapshots[-1]["time"] - duration) > 0.001
    ):
        raise ValueError("Missing initial or terminal snapshot")
    sampled = {
        round(snapshot["scheduled_time"] / 30)
        for snapshot in snapshots
        if abs(snapshot["scheduled_time"] / 30 - round(snapshot["scheduled_time"] / 30))
        < 0.0001
    }
    # Outcome checks precede periodic sampling; a terminal state within a game frame
    # of a boundary is that boundary's observation, not a missing historical sample.
    if abs(duration - round(duration / 30) * 30) <= 0.05:
        sampled.add(round(duration / 30))
    for boundary in range(1, math.ceil(duration / 30)):
        if boundary not in sampled:
            raise ValueError(f"Missing 30-second sample {boundary * 30}")
    if not isinstance(report.get("events"), list):
        raise ValueError("Missing event history")
    for event in report["events"]:
        if not 0 <= number(event.get("time"), "event time") <= duration + 0.001:
            raise ValueError("Event time outside match")


def validate_teams(snapshot: JsonObject) -> None:
    for team in snapshot["teams"]:
        for field in TEAM_FIELDS:
            if number(team.get(field), field) < 0:
                raise ValueError(f"Negative telemetry {field}")
        if (
            not isinstance(team.get("units_by_role"), list)
            or len(team["units_by_role"]) != 3
        ):
            raise ValueError("Missing role counts")
        if (
            sum(number(count, "role count") for count in team["units_by_role"])
            != team["units_alive"]
        ):
            raise ValueError("Role counts disagree with living unit count")
        for field in ("units_by_region", "buildings", "forces", "region_indices"):
            if field not in team:
                raise ValueError(f"Missing concentration/production metadata: {field}")


def validate_outcome(report: JsonObject, job: JsonObject, duration: float) -> None:
    snapshots = report["snapshots"]
    final = {team["team"]: team for team in snapshots[-1]["teams"]}
    if report.get("outcome") == "time_cap":
        if (
            report.get("winner") is not None
            or duration < job["time_cap"] - 0.02
            or any(final[team]["hq_health"] <= 0 for team in (0, 5))
        ):
            raise ValueError("Invalid time-cap draw")
    elif report.get("outcome") == "hq_destroyed":
        winner = report.get("winner")
        if winner not in (0, 5) or final[5 if winner == 0 else 0]["hq_health"] > 0:
            raise ValueError("Winner has no destroyed opposing HQ")
        if final[0]["hq_health"] <= 0 and winner != 5:
            raise ValueError(
                "Simultaneous HQ loss must follow game's team-5 tie precedence"
            )
    else:
        raise ValueError("Unknown outcome, not a result")
