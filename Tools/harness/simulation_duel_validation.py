"""Admit only complete, internally consistent runtime combat matrices."""

from __future__ import annotations

import math

from harness.simulation_validation import number
from harness.verify import JsonObject

DUEL_BUDGET = 120


def nonnegative(value: object, label: str) -> float:
    result = number(value, label)
    if result < 0:
        raise ValueError(f"{label} must be nonnegative")
    return result


def count(value: object, label: str) -> int:
    result = nonnegative(value, label)
    if not result.is_integer():
        raise ValueError(f"{label} must be a whole count")
    return int(result)


def pair_values(row: JsonObject, field: str) -> list[float]:
    values = row.get(field)
    if not isinstance(values, list) or len(values) != 2:
        raise ValueError(f"Duel {field} must contain both sides")
    return [nonnegative(value, field) for value in values]


def definitions(report: JsonObject) -> dict[str, JsonObject]:
    rows = report.get("unit_definitions")
    if not isinstance(rows, list) or not rows:
        raise ValueError("Missing runtime unit definitions")
    roster: dict[str, JsonObject] = {}
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("Malformed runtime unit definition")
        unit_id = row.get("id")
        if not isinstance(unit_id, str) or not unit_id or unit_id in roster:
            raise ValueError("Runtime unit ids must be nonempty and unique")
        for field in ("cost", "health", "damage", "attack_interval"):
            if number(row.get(field), field) <= 0:
                raise ValueError(f"Runtime {field} must be positive")
        if count(row.get("capacity"), "capacity") <= 0:
            raise ValueError("Runtime capacity must be positive")
        nonnegative(row.get("range"), "range")
        if row["cost"] > DUEL_BUDGET:
            raise ValueError("Runtime combat unit is unaffordable at duel budget")
        roster[unit_id] = row
    return roster


def validate_sides(
    row: JsonObject,
    roster: dict[str, JsonObject],
    values: dict[str, list[float]],
) -> None:
    for side, unit_id in enumerate((row["left"], row["right"])):
        unit = roster[unit_id]
        initial = count(values["initial_units"][side], "initial units")
        survivors = count(values["survivors"][side], "survivors")
        attacks = count(values["attacks"][side], "attacks")
        if initial != math.floor(DUEL_BUDGET / unit["cost"]):
            raise ValueError(
                "Initial squad does not spend the whole-unit 120 Power budget"
            )
        if not math.isclose(
            values["spent"][side], initial * unit["cost"], abs_tol=0.001
        ):
            raise ValueError("Spent Power disagrees with initial whole units")
        if survivors > initial:
            raise ValueError("Survivors exceed initial units")
        if not math.isclose(
            values["survivor_power"][side], survivors * unit["cost"], abs_tol=0.001
        ):
            raise ValueError("Survivor Power disagrees with living units")
        damage = values["damage_dealt"][side]
        opponent = roster[row["right"] if side == 0 else row["left"]]
        enemy_initial = values["initial_units"][1 - side]
        killed = enemy_initial - values["survivors"][1 - side]
        if damage > enemy_initial * opponent["health"] + 0.01:
            raise ValueError("Effective damage exceeds initial enemy health")
        if damage + 0.01 < killed * opponent["health"] or (damage > 0 and attacks == 0):
            raise ValueError("Damage/casualties lack actual combat telemetry")


def validate_pair(row: JsonObject, roster: dict[str, JsonObject], cap: float) -> None:
    if not isinstance(row, dict) or any(
        not isinstance(row.get(key), str) or row[key] not in roster
        for key in ("left", "right")
    ):
        raise ValueError("Duel references unknown runtime unit")
    values = {
        field: pair_values(row, field)
        for field in (
            "spent",
            "initial_units",
            "survivors",
            "survivor_power",
            "damage_dealt",
            "attacks",
        )
    }
    validate_sides(row, roster, values)
    duration = number(row.get("duration"), "duel duration")
    if duration <= 0 or duration > cap + 0.1:
        raise ValueError("Duel duration outside per-pair cap")
    winner = row.get("winner")
    if "winner" not in row or isinstance(winner, bool) or winner not in (None, 0, 5):
        raise ValueError("Invalid duel winner")
    alive = values["survivors"]
    if row.get("outcome") == "wiped":
        expected = 0 if alive[0] > 0 else 5 if alive[1] > 0 else None
        if min(alive) != 0 or winner != expected:
            raise ValueError("Wipe outcome disagrees with survivors")
    elif row.get("outcome") == "time_cap":
        if min(alive) <= 0 or winner is not None or duration < cap - 0.1:
            raise ValueError("Invalid duel time-cap draw")
    else:
        raise ValueError("Unknown duel outcome")


def validate_duel_report(report: JsonObject, job: JsonObject) -> None:
    if not isinstance(report, dict):
        raise ValueError("Duel telemetry must be an object")
    if (
        type(report.get("schema_version")) is not int
        or report.get("schema_version") != 1
        or report.get("status") != "complete"
    ):
        raise ValueError("No complete schema-1 duel result")
    if (
        report.get("mode") != "duel"
        or report.get("outcome") != "matrix_complete"
        or "winner" not in report
        or report["winner"] is not None
    ):
        raise ValueError("No terminal duel matrix outcome")
    for field in ("map", "seed"):
        if report.get(field) != job[field]:
            raise ValueError(f"Telemetry {field} does not match launched job")
    count(report.get("seed"), "seed")
    for field, expected in (
        ("time_cap_seconds", job["time_cap"]),
        ("requested_dilation", job["dilation"]),
        ("effective_dilation", job["dilation"]),
    ):
        if abs(number(report.get(field), field) - expected) > 0.001:
            raise ValueError(f"Telemetry {field} differs from request")
    nonnegative(report.get("wall_duration"), "wall duration")
    delta = nonnegative(report.get("max_game_delta_seconds"), "game delta")
    if delta > 1 / 60 + 0.0001:
        raise ValueError("Game delta exceeded fixed 60 Hz contract")
    roster = definitions(report)
    rows = report.get("duels")
    if not isinstance(rows, list):
        raise ValueError("Missing duel telemetry")
    seen = set()
    for row in rows:
        validate_pair(row, roster, job["time_cap"])
        key = (row["left"], row["right"])
        if key in seen:
            raise ValueError("Duplicate ordered duel pair")
        seen.add(key)
    expected = {(left, right) for left in roster for right in roster}
    if seen != expected:
        raise ValueError("Incomplete ordered duel matrix, including mirrors")
    duration = number(report.get("duration"), "matrix duration")
    if duration <= 0 or not math.isclose(
        duration, sum(row["duration"] for row in rows), abs_tol=0.1 * len(rows)
    ):
        raise ValueError("Matrix duration disagrees with completed pair durations")
