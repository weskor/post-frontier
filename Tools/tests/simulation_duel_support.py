"""Deterministic runtime evidence shared by duel report tests."""

from pathlib import Path

from harness.simulation_duel_rules import COUNTERS, SUPPORT
from harness.simulation_evidence import save_json
from harness.simulation_planning import MAP_V2
from harness.verify import JsonObject

# Every unit has 5 HP-plus-shield and 0.5 damage per second per Power, so no unit dominates another.
ROSTER = (
    ("frontline", 20, 0, False),
    ("ranged", 30, 0, False),
    ("siege", 40, 0, False),
    ("lancer", 45, 80, False),
    ("scrambler", 35, 0, True),
)
BUDGET = 120


def unit_definitions() -> list[JsonObject]:
    return [
        dict(
            id=unit,
            cost=cost,
            capacity=BUDGET // cost,
            health=cost * 5 - shield,
            shield=shield,
            support=support,
            damage=cost / 2,
            attack_interval=1,
            range=175,
        )
        for unit, cost, shield, support in ROSTER
    ]


def squads(row: JsonObject) -> list[list[tuple[str, int]]]:
    """The two squads of a pair or composition row as (unit, count) lists."""
    if "left_units" in row:
        return [
            [(part["id"], part["count"]) for part in row[side]]
            for side in ("left_units", "right_units")
        ]
    return [[(row["left"], row["initial_units"][0])], [(row["right"], row["initial_units"][1])]]


def composition_rows(roster: dict[str, JsonObject], first_team: int) -> list[JsonObject]:
    rows = []
    for kind in ("baseline", "with_support"):
        for team in (0, 5):
            subject = [("ranged", BUDGET // roster["ranged"]["cost"])]
            if kind == "with_support":
                left_over = BUDGET - roster["scrambler"]["cost"]
                subject = [("scrambler", 1), ("ranged", left_over // roster["ranged"]["cost"])]
            enemy = [("lancer", BUDGET // roster["lancer"]["cost"])]
            sides = [subject, enemy] if team == 0 else [enemy, subject]
            row: JsonObject = dict(
                scenario=kind,
                support="scrambler",
                partner="ranged",
                target="lancer",
                subject_team=team,
                left_units=[dict(id=unit, count=amount) for unit, amount in sides[0]],
                right_units=[dict(id=unit, count=amount) for unit, amount in sides[1]],
                spawn_first_team=first_team,
            )
            row["spent"] = [sum(amount * roster[unit]["cost"] for unit, amount in side) for side in sides]
            row["initial_units"] = [sum(amount for _, amount in side) for side in sides]
            set_composition_outcome(roster, row, None)
            rows.append(row)
    return rows


def telemetry(seed: int = 1, map_name: str = MAP_V2) -> tuple[JsonObject, JsonObject]:
    job: JsonObject = dict(
        mode="duel",
        map=map_name,
        variant="duel",
        seed=seed,
        dilation=1,
        time_cap=60,
    )
    units = unit_definitions()
    roster = {unit["id"]: unit for unit in units}
    first_team = 0 if seed % 2 else 5
    report: JsonObject = dict(
        schema_version=1,
        status="complete",
        mode="duel",
        map=map_name,
        seed=seed,
        duration=0,
        wall_duration=3,
        time_cap_seconds=60,
        requested_dilation=1,
        effective_dilation=1,
        max_game_delta_seconds=1 / 60,
        outcome="matrix_complete",
        winner=None,
        unit_definitions=units,
        geometry=dict(
            spawn_spacing=160,
            spawn_jitter=40,
            squad_center_separation=1000,
            spawn_first_team=first_team,
        ),
        duels=[],
    )
    for left in units:
        for right in units:
            row: JsonObject = dict(
                left=left["id"],
                right=right["id"],
                spawn_first_team=first_team,
                spent=[BUDGET // left["cost"] * left["cost"], BUDGET // right["cost"] * right["cost"]],
                initial_units=[BUDGET // left["cost"], BUDGET // right["cost"]],
                survivors=[0, 0],
                survivor_power=[0, 0],
                damage_dealt=[0, 0],
                shield_damage_dealt=[0, 0],
                attacks=[20, 20],
                duration=5,
                outcome="wiped",
                winner=None,
            )
            report["duels"].append(row)
            winner = 0 if right["id"] in COUNTERS[left["id"]]["prey"] else 5
            set_outcome(report, row, winner)
    report["compositions"] = composition_rows(roster, first_team)
    refresh_duration(report)
    return job, report


def legacy_telemetry(seed: int = 1) -> tuple[JsonObject, JsonObject]:
    """The three-unit roster of the first duel runs: no shields, no support, no compositions."""
    job, report = telemetry(seed)
    legacy = [unit["id"] for unit in report["unit_definitions"][:3]]
    report["unit_definitions"] = [
        {key: value for key, value in unit.items() if key not in ("shield", "support")}
        for unit in report["unit_definitions"][:3]
    ]
    report["duels"] = [
        {key: value for key, value in row.items() if key != "shield_damage_dealt"}
        for row in report["duels"]
        if row["left"] in legacy and row["right"] in legacy
    ]
    del report["compositions"]
    refresh_duration(report)
    return job, report


def set_outcome(report: JsonObject, row: JsonObject, winner: int | None) -> None:
    roster = {unit["id"]: unit for unit in report["unit_definitions"]}
    initial = row["initial_units"]
    row["winner"] = winner
    row["survivors"] = (
        initial.copy()
        if winner is None
        else [1 if winner == 0 else 0, 1 if winner == 5 else 0]
    )
    row["survivor_power"] = [
        row["survivors"][side] * roster[unit]["cost"]
        for side, unit in enumerate((row["left"], row["right"]))
    ]
    row["damage_dealt"] = [
        (initial[1 - side] - row["survivors"][1 - side]) * roster[unit]["health"]
        for side, unit in enumerate((row["right"], row["left"]))
    ]
    # Every killed unit lost its whole shield first.
    row["shield_damage_dealt"] = [
        (initial[1 - side] - row["survivors"][1 - side]) * roster[unit].get("shield", 0)
        for side, unit in enumerate((row["right"], row["left"]))
    ]
    row["outcome"] = "time_cap" if winner is None else "wiped"
    row["duration"] = 60 if winner is None else 5


def set_composition_outcome(
    roster: dict[str, JsonObject], row: JsonObject, winner: int | None
) -> None:
    sides = squads(row)
    row["winner"] = winner
    row["survivors"] = [
        row["initial_units"][side] if winner is None else 1 if winner == (0 if side == 0 else 5) else 0
        for side in (0, 1)
    ]
    # One survivor of a mixed squad is its cheapest unit; the fixture only needs consistent Power.
    row["survivor_power"] = [
        0
        if not row["survivors"][side]
        else row["spent"][side]
        if winner is None
        else min(roster[unit]["cost"] for unit, _ in sides[side])
        for side in (0, 1)
    ]
    row["damage_dealt"] = [
        sum(amount * roster[unit]["health"] for unit, amount in sides[1 - side])
        if winner == (0 if side == 0 else 5)
        else 0
        for side in (0, 1)
    ]
    row["shield_damage_dealt"] = [
        sum(amount * roster[unit]["shield"] for unit, amount in sides[1 - side])
        if winner == (0 if side == 0 else 5)
        else 0
        for side in (0, 1)
    ]
    row["attacks"] = [20, 20]
    row["outcome"] = "time_cap" if winner is None else "wiped"
    row["duration"] = 60 if winner is None else 5


def refresh_duration(report: JsonObject) -> None:
    report["duration"] = sum(
        row["duration"] for row in report["duels"] + report.get("compositions", [])
    )


def stalled_telemetry(seed: int = 1) -> tuple[JsonObject, JsonObject]:
    job, report = telemetry(seed)
    invalid = report["duels"][2]
    set_outcome(report, invalid, None)
    invalid.update(
        outcome="stalled",
        duration=30,
        no_damage_seconds=30,
        stall_timeout_seconds=30,
        attacks=[0, 0],
    )
    report.update(
        status="failed",
        outcome="stalled",
        error="No effective damage for the stall timeout while both sides remain alive",
        invalid_duel=invalid,
        duels=report["duels"][:2],
        compositions=[],
    )
    report["duration"] = (
        sum(row["duration"] for row in report["duels"]) + invalid["duration"]
    )
    return job, report


def persist(tmp_path: Path, reports: list[JsonObject]) -> JsonObject:
    records = []
    jobs = []
    for index, report in enumerate(reports):
        job = dict(
            mode="duel",
            map=report["map"],
            variant="duel",
            seed=report["seed"],
            dilation=1,
            time_cap=60,
        )
        folder = tmp_path / f"duel-{index}"
        folder.mkdir()
        save_json(folder / "match.json", report)
        jobs.append(job)
        records.append(
            dict(
                job=job,
                directory=f"/moved/original/{folder.name}",
                status="complete",
                returncode=0,
            )
        )
    return dict(mode="duel", matches=records, planned_jobs=jobs)


def rule_rows(group: JsonObject) -> dict[str, JsonObject]:
    return {row["rule"]: row for row in group["rules"]}


def counter_rule_names() -> list[tuple[str, str, str]]:
    """(rule name, unit, relation) for every one-on-one prey and predator pair the design table lists."""
    return [
        (f"{unit}_{relation}_{opponent}", unit, relation)
        for unit, target in COUNTERS.items()
        if unit not in SUPPORT
        for relation in ("prey", "predator")
        for opponent in target[relation]
    ]


def threshold_reports() -> list[JsonObject]:
    reports = []
    for seed in range(20):
        _, report = telemetry(seed)
        for row in report["duels"]:
            if row["left"] == row["right"]:
                winner = 0 if seed < 9 else 5
            else:
                prey_on_left = row["right"] in COUNTERS[row["left"]]["prey"]
                winner = 0 if prey_on_left == (seed < 13) else 5
            set_outcome(report, row, winner)
        refresh_duration(report)
        reports.append(report)
    return reports
