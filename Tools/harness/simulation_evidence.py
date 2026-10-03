"""Simulation evidence identities and atomic JSON persistence."""

from __future__ import annotations

import json
from pathlib import Path

from harness.verify import JsonObject

type GroupKey = tuple[str, str, float]
type Groups = dict[GroupKey, list[JsonObject]]

PLAN_VERBS = ("move_and_hold", "attack", "retreat")
PLAN_LABELS = ("Move & Hold", "Attack", "Retreat")


def save_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def stamp(path: Path) -> JsonObject:
    stat = path.stat()
    return dict(path=str(path), size=stat.st_size, mtime_ns=stat.st_mtime_ns)


def teams(snapshot: JsonObject) -> dict[int, JsonObject]:
    return {team["team"]: team for team in snapshot["teams"]}


def committed_plan_evidence(report: JsonObject) -> JsonObject:
    """Count ticket lifetimes, not evaluations, snapshots or changed verbs."""
    created: dict[int, int] = {}
    escalated: set[int] = set()
    transitions: set[int] = set()
    breakdown = {
        owner: dict(creation_defense=0, order_changing=0, label_only=0)
        for owner in ("jev", "neutral", "player")
    }
    for event in report["events"]:
        if event.get("team") != 5:
            continue
        ticket = event.get("ticket")
        if event["kind"] == "plan_created":
            if ticket in created:
                continue
            created[ticket] = event["verb"]
            if not event["escalated"]:
                continue
            category = "creation_defense"
        elif event["kind"] == "plan_escalated":
            if ticket not in created or ticket in transitions:
                continue
            transitions.add(ticket)
            category = "order_changing" if event["order_changed"] else "label_only"
        else:
            continue
        escalated.add(ticket)
        owner = {5: "jev", -1: "neutral", 0: "player"}[event["source_controller"]]
        breakdown[owner][category] += 1
    counts = {
        name: sum(verb == index for verb in created.values())
        for index, name in enumerate(PLAN_VERBS)
    }
    terminal = report["snapshots"][-1]
    return dict(
        team=5,
        counts=dict(
            counts,
            total=len(created),
            escalated=len(escalated.intersection(created)),
        ),
        escalation_by_owner=breakdown,
        escalation_counts={
            category: sum(row[category] for row in breakdown.values())
            for category in ("creation_defense", "order_changing", "label_only")
        },
        captures=sum(
            event["kind"] == "region_control" and event.get("team") == 5
            for event in report["events"]
        ),
        attacks_observed=teams(terminal)[5]["attacks_observed"],
        active_at_seconds=terminal["time"],
        active_plans=terminal["enemy_plans"],
    )
