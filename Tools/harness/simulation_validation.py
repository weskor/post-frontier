"""Validate complete simulation telemetry before admitting a match to reports."""

from __future__ import annotations

from dataclasses import dataclass
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


@dataclass(frozen=True)
class Outcome:
    """How a battle ended: a decisive result has a winner, a censored one only the cap.

    `hq_down` and `hq_up` are the final-snapshot HQ health this outcome requires (0 HP, not
    0 HP); `hq_lost` and `hq_standing` are the lifecycle states it requires (`hq_state`
    is "lost", is not "lost"). The validator checks them without knowing which outcome
    kinds exist.
    """

    kind: str
    decisive: bool
    winner: int | None
    seconds: float
    hq_down: tuple[int, ...]
    hq_up: tuple[int, ...]
    hq_lost: tuple[int, ...] = ()
    hq_standing: tuple[int, ...] = ()


def interpret_outcome(report: JsonObject) -> Outcome:
    """The one place that says which recorded outcomes end a battle.

    A battle ends on a completed hold: an HQ at 0 HP is only offline, and the attackers must hold
    its main until the HQ is lost. The simulation still reports that as `hq_destroyed` until its
    outcome resolver moves to `hold_completed`; both mean the same decisive result, but only
    `hold_completed` carries (and is checked against) the HQ lifecycle states. Every statistic,
    the gate and the outcome validator read outcomes only through this function.
    """
    kind = report.get("outcome")
    seconds = number(report.get("duration"), "duration")
    if kind == "hold_completed":
        winner = report.get("winner")
        if winner not in (0, 5):
            raise ValueError("Decisive outcome without a winning team")
        loser = 5 if winner == 0 else 0
        # The loser's hold completed: its HQ is lost (0 HP) and the winner's is not.
        return Outcome(kind, True, winner, seconds, (loser,), (), (loser,), (winner,))
    if kind == "hq_destroyed":
        winner = report.get("winner")
        if winner not in (0, 5):
            raise ValueError("Decisive outcome without a winning team")
        # Simultaneous HQ loss follows the game's team-5 tie precedence.
        return Outcome(
            kind,
            True,
            winner,
            seconds,
            (5,) if winner == 0 else (0,),
            (0,) if winner == 0 else (),
        )
    if kind == "time_cap":
        if report.get("winner") is not None:
            raise ValueError("Invalid time-cap draw: a winner is recorded")
        return Outcome(kind, False, None, seconds, (), (0, 5))
    raise ValueError("Unknown outcome, not a result")


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
    for field in ("map", "seed"):
        if report.get(field) != job[field]:
            raise ValueError(f"Telemetry {field} does not match launched job")
    if report.get("economy") != job["economy"]:
        raise ValueError(
            "Telemetry economy (human and JEV baselines, rates, reserves) "
            "does not match launched job"
        )
    if report.get("scenario", "default") != job.get("scenario", "default"):
        raise ValueError("Telemetry scenario does not match launched job")
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
    validate_plans(report)


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
        if not isinstance(snapshot.get("enemy_plans"), list):
            raise ValueError("Missing per-force JEV plan history")
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


def validate_plan(plan: JsonObject) -> None:
    for field in (
        "ticket",
        "force",
        "verb",
        "source_region",
        "target_region",
        "size_band",
    ):
        value = number(plan.get(field), f"plan {field}")
        if not value.is_integer():
            raise ValueError(f"Plan {field} must be an integer")
    if plan["ticket"] <= 0 or plan["force"] < 0:
        raise ValueError("Plan must identify a ticket and force")
    if plan["verb"] not in (0, 1, 2):
        raise ValueError("Unknown plan verb")
    if plan["size_band"] < 2 or plan["size_band"] % 2:
        raise ValueError("Invalid plan size band")
    for field in ("eta_seconds", "remaining_commitment_seconds"):
        if number(plan.get(field), f"plan {field}") < 0:
            raise ValueError(f"Negative plan {field}")
    if not isinstance(plan.get("escalated"), bool) or not isinstance(
        plan.get("memo"), str
    ):
        raise ValueError("Missing plan escalation or memo")


def validate_plans(report: JsonObject) -> None:
    created: dict[int, JsonObject] = {}
    escalated: dict[int, float] = {}
    for event in report["events"]:
        if event.get("team") != 5 or event["kind"] not in (
            "plan_created",
            "plan_escalated",
        ):
            continue
        validate_plan(event)
        if event.get("source_controller") not in (-1, 0, 5) or isinstance(
            event.get("source_controller"), bool
        ):
            raise ValueError("Missing or invalid JEV plan source ownership")
        if not isinstance(event.get("order_changed"), bool):
            raise ValueError("Missing JEV plan order change evidence")
        if event["kind"] == "plan_escalated" and not event["escalated"]:
            raise ValueError("Escalation event must mark plan escalated")
        ticket = event["ticket"]
        if event["kind"] == "plan_created":
            if ticket in created and created[ticket]["force"] != event["force"]:
                raise ValueError("JEV ticket reused by another force")
            created.setdefault(ticket, event)
            if created[ticket] is event and event["escalated"]:
                escalated.setdefault(ticket, event["time"])
        else:
            if ticket not in created:
                raise ValueError("Missing JEV plan creation before escalation")
            if created[ticket]["force"] != event["force"]:
                raise ValueError("Escalated ticket changed force")
            escalated.setdefault(ticket, event["time"])
    for snapshot in report["snapshots"]:
        tickets: set[int] = set()
        for plan in snapshot["enemy_plans"]:
            validate_plan(plan)
            ticket = plan["ticket"]
            if ticket in tickets:
                raise ValueError("Duplicate active JEV ticket")
            tickets.add(ticket)
            if ticket not in created or created[ticket]["time"] > snapshot["time"]:
                raise ValueError("Missing JEV plan creation history")
            if created[ticket]["force"] != plan["force"]:
                raise ValueError("Published ticket changed force")
            if plan["escalated"] and (
                ticket not in escalated or escalated[ticket] > snapshot["time"]
            ):
                raise ValueError("Missing JEV plan escalation history")


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
    final = {team["team"]: team for team in report["snapshots"][-1]["teams"]}
    outcome = interpret_outcome(report)
    if not outcome.decisive and duration < job["time_cap"] - 0.02:
        raise ValueError("Invalid time-cap draw: ended before the cap")
    for team in outcome.hq_down:
        if final[team]["hq_health"] > 0:
            raise ValueError(f"Invalid {outcome.kind} result: team {team} HQ stands")
    for team in outcome.hq_up:
        if final[team]["hq_health"] <= 0:
            raise ValueError(
                f"Invalid {outcome.kind} result: team {team} HQ is destroyed"
            )
    for team in (*outcome.hq_lost, *outcome.hq_standing):
        if "hq_state" not in final[team]:
            raise ValueError(f"Invalid {outcome.kind} result: team {team} has no hq_state")
    for team in outcome.hq_lost:
        if final[team]["hq_state"] != "lost":
            raise ValueError(
                f"Invalid {outcome.kind} result: team {team} HQ is not lost"
            )
    for team in outcome.hq_standing:
        if final[team]["hq_state"] == "lost":
            raise ValueError(f"Invalid {outcome.kind} result: team {team} HQ is lost")
