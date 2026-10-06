"""Movement-progress evidence: units settled, marches over twice their ETA, forces that never held."""

from __future__ import annotations

from harness.simulation_evidence import Groups
from harness.verify import JsonObject


def match_movement(report: JsonObject) -> JsonObject | None:
    """One match's movement metrics; None for a report that predates them."""
    movement = report.get("movement")
    if not isinstance(movement, dict):
        return None
    forces = list(movement["forces"].values())
    marched = [force for force in forces if force["marched"]]
    return dict(
        minutes=report["duration"] / 60,
        settled=sum(force["settled"] for force in forces),
        overruns=sum(force["overruns"] for force in forces),
        marched=len(marched),
        never_holding=sum(
            1 for force in marched if force["alive"] and not force["held_after_march"]
        ),
    )


def movement_evidence(reports: list[JsonObject]) -> JsonObject:
    """Totals over every match that recorded movement; per-minute uses their game minutes."""
    rows = [row for row in map(match_movement, reports) if row]
    minutes = sum(row["minutes"] for row in rows)
    settled = sum(row["settled"] for row in rows)
    return dict(
        matches=len(reports),
        recorded=len(rows),
        minutes=minutes,
        settled=settled,
        settled_per_minute=settled / minutes if minutes else None,
        overruns=sum(row["overruns"] for row in rows),
        marched=sum(row["marched"] for row in rows),
        never_holding=sum(row["never_holding"] for row in rows),
    )


def movement_section(groups: Groups, lines: list[str]) -> list[JsonObject]:
    lines += [
        "",
        "## Movement progress",
        "",
        "Units settled: members stopped for a goal after 6 s without progress. Over 2x ETA: Marching legs "
        "that outlasted twice the force card's ETA at the start of the leg. Never held: forces alive at the "
        "end that marched and were not seen Holding afterwards. Observed once per game second; no gate.",
        "",
        "| Map | Variant | Dilation | Recorded | Units settled | Settled per game min | Over 2x ETA | Marched forces | Never held |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    rows: list[JsonObject] = []
    for (map_name, variant, dilation), reports in sorted(groups.items()):
        evidence = movement_evidence(reports)
        rate = evidence["settled_per_minute"]
        lines.append(
            f"| {map_name} | {variant} | {dilation:g}\u00d7 | {evidence['recorded']}/{evidence['matches']} "
            f"| {evidence['settled']} | {'n/a' if rate is None else f'{rate:.2f}'} "
            f"| {evidence['overruns']} | {evidence['marched']} | {evidence['never_holding']} |"
        )
        rows.append(dict(evidence, map=map_name, variant=variant, dilation=dilation))
    lines.append("")
    return rows
