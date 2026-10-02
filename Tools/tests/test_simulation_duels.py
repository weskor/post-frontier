"""Fixed runtime evidence proves duel acceptance boundaries and failure discipline."""

import argparse
from copy import deepcopy
import json
from pathlib import Path

from harness.simulation_duel_rules import COUNTERS, evaluate_group
from harness.simulation_evidence import save_json
from harness.simulation_planning import MAP_V1, MAP_V2, plan_jobs, validate_options
from harness.simulation_report import summarize
from harness.simulation_validation import validate_report
from harness.verify import JsonObject
import pytest
from x.content.simulation import configure


def telemetry(seed: int = 1, map_name: str = MAP_V2) -> tuple[JsonObject, JsonObject]:
    job: JsonObject = dict(
        mode="duel",
        map=map_name,
        variant="duel",
        seed=seed,
        dilation=1,
        time_cap=60,
    )
    units: list[JsonObject] = [
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
        duels=[],
    )
    for left in units:
        for right in units:
            row: JsonObject = dict(
                left=left["id"],
                right=right["id"],
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


def rule_rows(group: JsonObject) -> dict[str, JsonObject]:
    return {row["rule"]: row for row in group["rules"]}


def threshold_reports() -> list[JsonObject]:
    reports = []
    for seed in range(20):
        _, report = telemetry(seed)
        for row in report["duels"]:
            if row["left"] == row["right"]:
                winner = 0 if seed < 9 else 5
            else:
                prey_on_left = COUNTERS[row["left"]]["prey"] == row["right"]
                winner = 0 if prey_on_left == (seed < 13) else 5
            set_outcome(report, row, winner)
        refresh_duration(report)
        reports.append(report)
    return reports


def test_counter_and_mirror_inclusive_thresholds() -> None:
    reports = threshold_reports()
    rules = rule_rows(evaluate_group(reports))
    for unit in COUNTERS:
        assert rules[f"{unit}_prey"]["win_rate"] == pytest.approx(0.65)
        assert rules[f"{unit}_predator"]["win_rate"] == pytest.approx(0.35)
        assert rules[f"{unit}_mirror"]["win_rates"] == [0.45, 0.55]
        assert all(
            rules[f"{unit}_{name}"]["status"] == "pass"
            for name in ("prey", "predator", "mirror")
        )
    changed = deepcopy(reports)
    row = next(
        row
        for row in changed[0]["duels"]
        if (row["left"], row["right"]) == ("frontline", "siege")
    )
    set_outcome(changed[0], row, 5)
    rules = rule_rows(evaluate_group(changed))
    assert rules["frontline_prey"]["status"] == "fail"
    assert rules["siege_predator"]["status"] == "fail"


def test_draws_are_never_half_wins_or_removed_from_denominators() -> None:
    reports = threshold_reports()
    row = next(
        row
        for row in reports[0]["duels"]
        if (row["left"], row["right"]) == ("frontline", "siege")
    )
    set_outcome(reports[0], row, None)
    rules = rule_rows(evaluate_group(reports))
    assert rules["frontline_prey"]["wins"] == 25
    assert rules["frontline_prey"]["duels"] == 40
    assert rules["frontline_prey"]["win_rate"] == 0.625
    assert rules["frontline_prey"]["status"] == "fail"
    for seed in (9, 10, 11):
        row = next(
            row
            for row in reports[seed]["duels"]
            if row["left"] == row["right"] == "frontline"
        )
        set_outcome(reports[seed], row, None)
    rules = rule_rows(evaluate_group(reports))
    assert rules["frontline_mirror"]["win_rates"] == [0.45, 0.4]
    assert rules["frontline_mirror"]["draws"] == 3
    assert rules["frontline_mirror"]["status"] == "fail"


def test_worth_normalizes_each_sides_actual_unequal_spend() -> None:
    job, report = telemetry()
    report["unit_definitions"][1]["cost"] = 35
    for row in report["duels"]:
        for side, unit in enumerate((row["left"], row["right"])):
            if unit == "ranged":
                row["initial_units"][side] = 3
                row["spent"][side] = 105
        set_outcome(report, row, None)
    row = next(
        row
        for row in report["duels"]
        if (row["left"], row["right"]) == ("frontline", "ranged")
    )
    row["survivors"][0] = 3
    row["survivor_power"][0] = 60
    row["damage_dealt"][1] = 300
    refresh_duration(report)
    assert report["duration"] == 540 > job["time_cap"]
    validate_report(report, job)
    group = evaluate_group([report])
    assert group["worth"] == pytest.approx(
        dict(frontline=0.4375, ranged=0.5625, siege=0.5)
    )
    worth = rule_rows(group)["roster_worth_ratio"]
    assert worth["ratio"] == pytest.approx(9 / 7)
    assert worth["status"] == "fail"
    set_outcome(report, row, None)
    worth = rule_rows(evaluate_group([report]))["roster_worth_ratio"]
    assert worth["ratio"] == 1
    assert worth["status"] == "pass"


def test_worth_ratio_includes_exact_one_point_two_five_boundary() -> None:
    reports = [telemetry(seed)[1] for seed in range(3)]
    for report in reports:
        for row in report["duels"]:
            set_outcome(report, row, None)
    lost = next(
        row
        for row in reports[0]["duels"]
        if (row["left"], row["right"]) == ("frontline", "ranged")
    )
    set_outcome(reports[0], lost, 5)
    lost["survivors"][1] = 4
    lost["survivor_power"][1] = 120
    lost["damage_dealt"][0] = 0
    partial = next(
        row
        for row in reports[1]["duels"]
        if (row["left"], row["right"]) == ("frontline", "ranged")
    )
    partial["survivors"][0] = 4
    partial["survivor_power"][0] = 80
    partial["damage_dealt"][1] = 200
    for report in reports:
        refresh_duration(report)
        validate_report(report, telemetry(report["seed"])[0])
    worth = rule_rows(evaluate_group(reports))["roster_worth_ratio"]
    assert worth["ratio"] == pytest.approx(1.25)
    assert worth["status"] == "pass"
    partial["survivors"][0] = 3
    partial["survivor_power"][0] = 60
    partial["damage_dealt"][1] = 300
    assert rule_rows(evaluate_group(reports))["roster_worth_ratio"]["status"] == "fail"


def test_zero_minimum_worth_fails_without_infinity_json() -> None:
    _, report = telemetry()
    for row in report["duels"]:
        if row["left"] != row["right"] and "frontline" in (row["left"], row["right"]):
            winner = 5 if row["left"] == "frontline" else 0
            set_outcome(report, row, winner)
            side = 1 if winner == 5 else 0
            row["survivors"][side] = row["initial_units"][side]
            row["survivor_power"][side] = row["spent"][side]
        else:
            set_outcome(report, row, None)
    worth = rule_rows(evaluate_group([report]))["roster_worth_ratio"]
    assert worth["worth"]["frontline"] == 0
    assert worth["ratio"] is None
    assert worth["status"] == "fail"
    json.dumps(worth, allow_nan=False)


def test_dominance_requires_strictly_greater_hp_and_dps_per_power() -> None:
    _, report = telemetry()
    frontline = report["unit_definitions"][0]
    frontline["health"] *= 2
    rules = rule_rows(evaluate_group([report]))
    assert rules["no_hp_and_dps_per_power_dominance"]["status"] == "pass"
    frontline["damage"] *= 2
    rules = rule_rows(evaluate_group([report]))
    dominance = rules["no_hp_and_dps_per_power_dominance"]
    assert dominance["status"] == "fail"
    assert dominance["dominance"] == [
        dict(leader="frontline", dominated="ranged"),
        dict(leader="frontline", dominated="siege"),
    ]


@pytest.mark.parametrize(
    ("scope", "field", "value"),
    [
        ("report", "seed", 2),
        ("report", "map", MAP_V1),
        ("report", "effective_dilation", 2),
        ("report", "mode", "match"),
        ("report", "status", "running"),
        ("report", "duration", 999),
        ("report", "unit_definitions", []),
        ("definition", "id", None),
        ("definition", "attack_interval", 0),
        ("definition", "health", float("nan")),
        ("duel", "damage_dealt", None),
        ("duel", "attacks", None),
        ("duel", "survivor_power", None),
        ("duel", "spent", [121, 120]),
        ("duel", "initial_units", [5.5, 6]),
        ("duel", "survivors", [10, 0]),
        ("duel", "damage_dealt", [100000, 0]),
        ("duel", "attacks", [0, 0]),
        ("duel", "winner", 0),
        ("duel", "survivors", [1]),
    ],
)
def test_malformed_telemetry_is_not_a_result(
    scope: str, field: str, value: object
) -> None:
    job, report = telemetry()
    targets = dict(
        report=report, definition=report["unit_definitions"][0], duel=report["duels"][0]
    )
    targets[scope][field] = value
    with pytest.raises(ValueError):
        validate_report(report, job)


@pytest.mark.parametrize(
    "fault", ["missing_pair", "duplicate_pair", "missing_mirror", "draw_before_cap"]
)
def test_incomplete_matrix_or_premature_draw_is_not_a_result(fault: str) -> None:
    job, report = telemetry()
    if fault == "missing_pair":
        report["duels"].pop()
    elif fault == "duplicate_pair":
        report["duels"].append(deepcopy(report["duels"][0]))
    elif fault == "missing_mirror":
        report["duels"] = [
            row for row in report["duels"] if row["left"] != row["right"]
        ]
    else:
        set_outcome(report, report["duels"][0], None)
        report["duels"][0]["duration"] = 5
    with pytest.raises(ValueError):
        validate_report(report, job)


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


def test_report_artifact_exposes_runtime_costs_and_rules_without_balance_exit_failure(
    tmp_path: Path,
) -> None:
    _, report = telemetry()
    manifest = persist(tmp_path, [report])
    assert summarize(tmp_path, manifest) is True
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["runtime_status"] == "pass"
    group = summary["groups"][0]
    assert group["balance_status"] == "fail"  # One mirror observation is not 50%.
    assert len(group["matrix"]) == 9
    assert group["telemetry"][0]["spent"] == [120, 120]
    assert group["unit_definitions"][0]["cost"] == 20
    text = (tmp_path / "Report.md").read_text()
    assert "Docs/Design/units.md" in text
    assert summary["support_compositions"] == "out_of_scope"
    assert "frontline / Brawler" in text
    assert "frontline_mirror" in text and "FAIL" in text


@pytest.mark.parametrize("unattempted", [False, True])
def test_missing_requested_seed_never_produces_rule_passes(
    tmp_path: Path, unattempted: bool
) -> None:
    reports = [] if unattempted else threshold_reports()
    manifest = persist(tmp_path, reports)
    manifest["planned_jobs"].append(dict(telemetry()[0], seed=99))
    assert summarize(tmp_path, manifest) is False
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["missing_seed_processes"] == 1
    assert summary["groups"][0]["balance_status"] == "fail"
    assert all(row["status"] == "fail" for row in summary["groups"][0]["rules"])
    assert (
        "1 planned seed processes have no valid complete matrix"
        in (tmp_path / "Report.md").read_text()
    )


@pytest.mark.parametrize(
    "fault", ["exit", "duplicate", "unexpected", "definition_change"]
)
def test_report_rejects_invalid_process_identity_or_changed_definitions(
    tmp_path: Path, fault: str
) -> None:
    _, one = telemetry(1)
    _, two = telemetry(2)
    if fault == "definition_change":
        two["unit_definitions"][0]["range"] += 1
    manifest = persist(tmp_path, [one, two])
    if fault == "exit":
        manifest["matches"][0]["returncode"] = 2
    elif fault == "duplicate":
        manifest["matches"].append(deepcopy(manifest["matches"][0]))
    elif fault == "unexpected":
        manifest["planned_jobs"] = manifest["planned_jobs"][:1]
    assert summarize(tmp_path, manifest) is False
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["runtime_status"] == "fail"
    assert summary["failures"]


def test_different_maps_have_separate_summaries(tmp_path: Path) -> None:
    _, one = telemetry(1, MAP_V2)
    _, two = telemetry(1, MAP_V1)
    manifest = persist(tmp_path, [one, two])
    assert summarize(tmp_path, manifest) is True
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert {group["map"] for group in summary["groups"]} == {MAP_V1, MAP_V2}
    assert all(group["complete_seeds"] == [1] for group in summary["groups"])
    assert all(
        row["duels"] == 1 for group in summary["groups"] for row in group["matrix"]
    )


def options(*arguments: str) -> tuple[argparse.ArgumentParser, argparse.Namespace]:
    parser = argparse.ArgumentParser()
    configure(parser)
    return parser, parser.parse_args(arguments)


def test_duel_seed_jobs_use_v2_only_but_preserve_legacy_map_defaults() -> None:
    parser, args = options("--duel", "--matches", "2", "--seed", "7")
    validate_options(parser, args)
    jobs = plan_jobs(parser, args)
    assert [(job["map"], job["seed"]) for job in jobs] == [(MAP_V2, 7), (MAP_V2, 8)]
    parser, args = options("--matches", "2")
    jobs = plan_jobs(parser, args)
    assert [(job["map"], job["seed"]) for job in jobs] == [
        (MAP_V2, 1),
        (MAP_V2, 2),
        (MAP_V1, 1),
        (MAP_V1, 2),
    ]


@pytest.mark.parametrize(
    "incompatible",
    [("--variant", "baseline2"), ("--matrix",), ("--compare-dilation", "2")],
)
def test_duel_rejects_incompatible_match_modes(incompatible: tuple[str, ...]) -> None:
    parser, args = options("--duel", *incompatible)
    with pytest.raises(SystemExit) as exit:
        validate_options(parser, args)
    assert exit.value.code == 2
