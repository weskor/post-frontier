"""Deterministic runtime evidence shared by duel report tests."""

from pathlib import Path

from harness.simulation_duel_rules import COUNTERS
from harness.simulation_evidence import save_json
from harness.simulation_planning import MAP_V2
from harness.verify import JsonObject


def unit_definitions() -> list[JsonObject]:
    return [
        dict(
            id=unit,
            cost=cost,
            capacity=120 // cost,
            health=cost * 5,
            damage=cost / 2,
            attack_interval=1,
            range=175,
        )
        for unit, cost in (("frontline", 20), ("ranged", 30), ("siege", 40))
    ]


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
            spawn_first_team=0 if seed % 2 else 5,
        ),
        duels=[],
    )
    for left in units:
        for right in units:
            row: JsonObject = dict(
                left=left["id"],
                right=right["id"],
                spawn_first_team=0 if seed % 2 else 5,
                spent=[120, 120],
                initial_units=[120 // left["cost"], 120 // right["cost"]],
                survivors=[0, 0],
                survivor_power=[0, 0],
                damage_dealt=[0, 0],
                attacks=[20, 20],
                duration=5,
                outcome="wiped",
                winner=None,
            )
            report["duels"].append(row)
            winner = 0 if COUNTERS[left["id"]]["prey"] == right["id"] else 5
            set_outcome(report, row, winner)
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
    row["outcome"] = "time_cap" if winner is None else "wiped"
    row["duration"] = 60 if winner is None else 5


def refresh_duration(report: JsonObject) -> None:
    report["duration"] = sum(row["duration"] for row in report["duels"])


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
