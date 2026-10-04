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
        # Reports written before shields existed carry neither field: no shield, not support.
        count(row.get("shield", 0), "shield")
        if not isinstance(row.get("support", False), bool):
            raise ValueError("Runtime support flag must be a boolean")
        if row["cost"] > DUEL_BUDGET:
            raise ValueError("Runtime combat unit is unaffordable at duel budget")
        roster[unit_id] = row
    return roster


def is_stalled(report: JsonObject) -> bool:
    invalid = report.get("invalid_duel")
    rows = report.get("duels")
    return (
        report.get("outcome") == "stalled"
        or (isinstance(invalid, dict) and invalid.get("outcome") == "stalled")
        or (
            isinstance(rows, list)
            and any(
                isinstance(row, dict) and row.get("outcome") == "stalled"
                for row in rows
            )
        )
    )


def validate_spawn_order(row: JsonObject, seed: int) -> None:
    expected = 0 if seed % 2 else 5
    if (
        type(row.get("spawn_first_team")) is not int
        or row["spawn_first_team"] != expected
    ):
        raise ValueError("Duel creation order does not match seed parity")


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
        shield_damage = values["shield_damage_dealt"][side]
        opponent = roster[row["right"] if side == 0 else row["left"]]
        enemy_initial = values["initial_units"][1 - side]
        killed = enemy_initial - values["survivors"][1 - side]
        enemy_shield = opponent.get("shield", 0)
        if damage > enemy_initial * opponent["health"] + 0.01:
            raise ValueError("Effective damage exceeds initial enemy health")
        if damage + 0.01 < killed * opponent["health"] or (damage > 0 and attacks == 0):
            raise ValueError("Damage/casualties lack actual combat telemetry")
        if enemy_shield == 0 and shield_damage > 0:
            raise ValueError("Shield damage reported against a squad without shields")
        if shield_damage + 0.01 < killed * enemy_shield:
            raise ValueError("Casualties lack the removal of their shields")


def pair_telemetry(
    row: JsonObject, roster: dict[str, JsonObject]
) -> dict[str, list[float]]:
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
    shielded = any(roster[row[side]].get("shield", 0) for side in ("left", "right"))
    if "shield_damage_dealt" in row or shielded:
        values["shield_damage_dealt"] = pair_values(row, "shield_damage_dealt")
    else:
        values["shield_damage_dealt"] = [0.0, 0.0]
    return values


def validate_outcome(row: JsonObject, alive: list[float], cap: float) -> None:
    duration = number(row.get("duration"), "duel duration")
    if duration <= 0 or duration > cap + 0.1:
        raise ValueError("Duel duration outside per-pair cap")
    winner = row.get("winner")
    if "winner" not in row or isinstance(winner, bool) or winner not in (None, 0, 5):
        raise ValueError("Invalid duel winner")
    if row.get("outcome") == "wiped":
        expected = 0 if alive[0] > 0 else 5 if alive[1] > 0 else None
        if min(alive) != 0 or winner != expected:
            raise ValueError("Wipe outcome disagrees with survivors")
    elif row.get("outcome") == "time_cap":
        if min(alive) <= 0 or winner is not None or duration < cap - 0.1:
            raise ValueError("Invalid duel time-cap draw")
    else:
        raise ValueError("Unknown duel outcome")


def validate_pair(row: JsonObject, roster: dict[str, JsonObject], cap: float) -> None:
    if not isinstance(row, dict) or any(
        not isinstance(row.get(key), str) or row[key] not in roster
        for key in ("left", "right")
    ):
        raise ValueError("Duel references unknown runtime unit")
    values = pair_telemetry(row, roster)
    validate_sides(row, roster, values)
    validate_outcome(row, values["survivors"], cap)


COMPOSITION_KINDS = ("baseline", "with_support")


def squad_values(
    row: JsonObject, field: str, roster: dict[str, JsonObject]
) -> list[tuple[str, int]]:
    parts = row.get(field)
    if not isinstance(parts, list) or not parts:
        raise ValueError(f"Composition {field} must list its units")
    squad = []
    for part in parts:
        if not isinstance(part, dict) or part.get("id") not in roster:
            raise ValueError("Composition references unknown runtime unit")
        squad.append((part["id"], count(part.get("count"), "composition count")))
    return squad


def expected_squads(
    row: JsonObject, roster: dict[str, JsonObject]
) -> list[list[tuple[str, int]]]:
    """The two squads a composition fight must field: subject on its side, the target on the other."""
    support, partner, target = row["support"], row["partner"], row["target"]
    partner_cost = roster[partner]["cost"]
    if row["scenario"] == "baseline":
        subject = [(partner, math.floor(DUEL_BUDGET / partner_cost))]
    else:
        left_over = DUEL_BUDGET - roster[support]["cost"]
        subject = [(support, 1), (partner, math.floor(left_over / partner_cost))]
    enemy = [(target, math.floor(DUEL_BUDGET / roster[target]["cost"]))]
    return [subject, enemy] if row["subject_team"] == 0 else [enemy, subject]


def validate_composition(
    row: JsonObject, roster: dict[str, JsonObject], cap: float
) -> None:
    if not isinstance(row, dict) or row.get("scenario") not in COMPOSITION_KINDS:
        raise ValueError("Unknown composition scenario")
    if any(row.get(key) not in roster for key in ("support", "partner", "target")):
        raise ValueError("Composition references unknown runtime unit")
    if not roster[row["support"]].get("support") or row.get("subject_team") not in (
        0,
        5,
    ):
        raise ValueError("Composition subject is not a support unit on a valid team")
    squads = [
        squad_values(row, field, roster) for field in ("left_units", "right_units")
    ]
    if squads != expected_squads(row, roster):
        raise ValueError("Composition squads do not match the equal-budget scenario")
    values = {
        field: pair_values(row, field)
        for field in (
            "spent",
            "initial_units",
            "survivors",
            "survivor_power",
            "damage_dealt",
            "shield_damage_dealt",
            "attacks",
        )
    }
    for side, squad in enumerate(squads):
        units = sum(amount for _, amount in squad)
        spent = sum(amount * roster[unit]["cost"] for unit, amount in squad)
        enemy_health = sum(
            amount * roster[unit]["health"] for unit, amount in squads[1 - side]
        )
        if values["initial_units"][side] != units or not math.isclose(
            values["spent"][side], spent, abs_tol=0.001
        ):
            raise ValueError(
                "Composition spent Power or members disagree with its squad"
            )
        if (
            spent > DUEL_BUDGET
            or values["survivors"][side] > units
            or values["survivor_power"][side] > spent + 0.001
        ):
            raise ValueError("Composition survivors exceed the squad")
        if (values["survivors"][side] == 0) != (values["survivor_power"][side] == 0):
            raise ValueError("Composition survivor Power disagrees with living units")
        if values["damage_dealt"][side] > enemy_health + 0.01 or (
            values["damage_dealt"][side] > 0 and values["attacks"][side] == 0
        ):
            raise ValueError("Composition damage lacks actual combat telemetry")
    validate_outcome(row, values["survivors"], cap)


def validate_compositions(
    report: JsonObject, roster: dict[str, JsonObject], seed: int, cap: float
) -> list[JsonObject]:
    rows = report.get("compositions", [])
    if not isinstance(rows, list):
        raise ValueError("Malformed composition telemetry")
    seen = set()
    for row in rows:
        validate_composition(row, roster, cap)
        validate_spawn_order(row, seed)
        key = (row["support"], row["scenario"], row["subject_team"])
        if key in seen:
            raise ValueError("Duplicate composition fight")
        seen.add(key)
    supports = [unit for unit, row in roster.items() if row.get("support")]
    if seen != {
        (unit, kind, team)
        for unit in supports
        for kind in COMPOSITION_KINDS
        for team in (0, 5)
    }:
        raise ValueError("Incomplete support composition set")
    return rows


def validate_duel_report(report: JsonObject, job: JsonObject) -> None:
    if not isinstance(report, dict):
        raise ValueError("Duel telemetry must be an object")
    if is_stalled(report):
        raise ValueError("Stalled duel is invalid, not a draw or complete matrix")
    if "invalid_duel" in report:
        raise ValueError("Invalid duel cannot belong to a complete matrix")
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
    seed = count(report.get("seed"), "seed")
    geometry = report.get("geometry")
    if isinstance(geometry, dict) and "spawn_first_team" in geometry:
        validate_spawn_order(geometry, seed)
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
        validate_spawn_order(row, seed)
        key = (row["left"], row["right"])
        if key in seen:
            raise ValueError("Duplicate ordered duel pair")
        seen.add(key)
    expected = {(left, right) for left in roster for right in roster}
    if seen != expected:
        raise ValueError("Incomplete ordered duel matrix, including mirrors")
    rows = rows + validate_compositions(report, roster, seed, job["time_cap"])
    duration = number(report.get("duration"), "matrix duration")
    if duration <= 0 or not math.isclose(
        duration, sum(row["duration"] for row in rows), abs_tol=0.1 * len(rows)
    ):
        raise ValueError("Matrix duration disagrees with completed pair durations")
