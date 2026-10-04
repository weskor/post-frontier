"""Fixed runtime evidence proves duel acceptance boundaries and failure discipline."""

import argparse
from copy import deepcopy
import json
from pathlib import Path

from harness.simulation_duel_rules import COUNTERS, evaluate_group
from harness.simulation_planning import MAP_V1, MAP_V2, plan_jobs, validate_options
from harness.simulation_report import summarize
from harness.simulation_validation import validate_report
from harness.verify import JsonObject
import pytest
from simulation_duel_support import (
    counter_rule_names,
    legacy_telemetry,
    persist,
    refresh_duration,
    rule_rows,
    set_outcome,
    stalled_telemetry,
    telemetry,
    threshold_reports,
)
from x.content.simulation import configure


def test_counter_inclusive_thresholds() -> None:
    reports = threshold_reports()
    rules = rule_rows(evaluate_group(reports))
    for name, _, relation in counter_rule_names():
        assert rules[name]["win_rate"] == pytest.approx(
            0.65 if relation == "prey" else 0.35
        )
        assert rules[name]["status"] == "pass"
    changed = deepcopy(reports)
    row = next(
        row
        for row in changed[0]["duels"]
        if (row["left"], row["right"]) == ("frontline", "siege")
    )
    set_outcome(changed[0], row, 5)
    rules = rule_rows(evaluate_group(changed))
    assert rules["frontline_prey_siege"]["status"] == "fail"
    assert rules["siege_predator_frontline"]["status"] == "fail"


def mirror_reports(total: int, left_wins: int, draws: int = 0) -> list[JsonObject]:
    reports = []
    for seed in range(total):
        _, report = telemetry(seed)
        winner = 0 if seed < left_wins else 5 if seed < total - draws else None
        for row in report["duels"]:
            if row["left"] == row["right"]:
                set_outcome(report, row, winner)
        refresh_duration(report)
        reports.append(report)
    return reports


@pytest.mark.parametrize(
    ("total", "wins", "expected", "p_value"),
    [
        (39, 19, "fail", 1.0),
        (40, 13, "fail", 0.03847730828420026),
        (40, 14, "pass", 0.0806904677519924),
        (40, 20, "pass", 1.0),
        (40, 26, "pass", 0.0806904677519924),
        (40, 27, "fail", 0.03847730828420026),
    ],
)
def test_mirror_sample_size_and_exact_two_sided_boundary(
    total: int, wins: int, expected: str, p_value: float
) -> None:
    rules = rule_rows(evaluate_group(mirror_reports(total, wins)))
    for unit in COUNTERS:
        mirror = rules[f"{unit}_mirror"]
        assert mirror["status"] == expected
        assert mirror["duels"] == total
        assert mirror["p_values"] == pytest.approx([p_value, p_value])


def test_mirror_draws_count_against_each_side_and_incomplete_evidence_fails() -> None:
    reports = mirror_reports(40, 20, draws=7)
    mirror = rule_rows(evaluate_group(reports))["frontline_mirror"]
    assert mirror["win_rates"] == [0.5, 0.325]
    assert mirror["draws"] == 7
    assert mirror["p_values"] == pytest.approx([1.0, 0.03847730828420026])
    assert mirror["status"] == "fail"
    incomplete = rule_rows(evaluate_group(mirror_reports(40, 20), complete=False))
    assert incomplete["frontline_mirror"]["status"] == "fail"


def test_draws_are_never_half_wins_or_removed_from_denominators() -> None:
    reports = threshold_reports()
    row = next(
        row
        for row in reports[0]["duels"]
        if (row["left"], row["right"]) == ("frontline", "siege")
    )
    set_outcome(reports[0], row, None)
    rules = rule_rows(evaluate_group(reports))
    assert rules["frontline_prey_siege"]["wins"] == 25
    assert rules["frontline_prey_siege"]["duels"] == 40
    assert rules["frontline_prey_siege"]["win_rate"] == 0.625
    assert rules["frontline_prey_siege"]["status"] == "fail"


def test_worth_normalizes_each_sides_actual_unequal_spend() -> None:
    job, report = legacy_telemetry()
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
    assert (
        report["duration"]
        == sum(row["duration"] for row in report["duels"])
        > job["time_cap"]
    )
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
    reports = [legacy_telemetry(seed)[1] for seed in range(3)]
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
        validate_report(report, legacy_telemetry(report["seed"])[0])
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
        dict(leader="frontline", dominated="lancer"),
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
        ("duel", "spawn_first_team", 5),
        ("duel", "spawn_first_team", True),
        ("duel", "spawn_first_team", None),
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


@pytest.mark.parametrize("seed", [0, 1, 2, 3])
@pytest.mark.parametrize("scope", ["pair", "geometry"])
def test_creation_order_must_follow_seed_parity(seed: int, scope: str) -> None:
    job, report = telemetry(seed)
    validate_report(report, job)
    target = report["duels"][0] if scope == "pair" else report["geometry"]
    target["spawn_first_team"] = 5 if seed % 2 else 0
    with pytest.raises(ValueError):
        validate_report(report, job)


def test_missing_pair_creation_order_is_invalid() -> None:
    job, report = telemetry()
    del report["duels"][0]["spawn_first_team"]
    with pytest.raises(ValueError):
        validate_report(report, job)


@pytest.mark.parametrize("location", ["matrix", "invalid_duel", "completed_pair"])
def test_stalls_cannot_masquerade_as_complete_matrices(location: str) -> None:
    job, report = telemetry()
    _, stalled = stalled_telemetry()
    if location == "matrix":
        report["outcome"] = "stalled"
    elif location == "invalid_duel":
        report["invalid_duel"] = stalled["invalid_duel"]
    else:
        report["duels"][2] = stalled["invalid_duel"]
    with pytest.raises(ValueError):
        validate_report(report, job)
    group = evaluate_group([report])
    assert group["telemetry"] == []
    assert group["matrix"] == []
    assert all(row["status"] == "fail" for row in group["rules"])
    assert all(not row["evidence_complete"] for row in group["rules"])


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
    assert len(group["matrix"]) == 25
    assert group["telemetry"][0]["spent"] == [120, 120]
    assert group["unit_definitions"][0]["cost"] == 20
    assert group["telemetry"][0]["spawn_first_team"] == 0
    assert all(row["spawn_first_counts"] == [1, 0] for row in group["matrix"])
    assert all(not row["spawn_order_balanced"] for row in group["matrix"])
    assert summary["support_compositions"] == "measured"


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
    assert all(not row["evidence_complete"] for row in summary["groups"][0]["rules"])


@pytest.mark.parametrize(
    ("status", "returncode"), [("failed", 1), ("complete", 1), ("complete", 0)]
)
def test_stalled_runtime_is_preserved_as_invalid_and_invalidates_all_rules(
    tmp_path: Path, status: str, returncode: int
) -> None:
    reports = threshold_reports()
    _, reports[0] = stalled_telemetry(0)
    manifest = persist(tmp_path, reports)
    manifest["matches"][0].update(status=status, returncode=returncode)
    assert summarize(tmp_path, manifest) is False
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["runtime_status"] == "fail"
    assert summary["missing_seed_processes"] == 1
    failure = summary["failures"][0]
    assert failure["outcome"] == "stalled"
    assert failure["invalid_duel"]["winner"] is None
    assert failure["invalid_duel"]["survivors"] == [6, 3]
    assert failure["invalid_duel"]["no_damage_seconds"] == 30
    assert failure["invalid_duel"]["stall_timeout_seconds"] == 30
    group = summary["groups"][0]
    assert group["complete_seeds"] == list(range(1, 20))
    assert all(row["seed"] != 0 for row in group["telemetry"])
    assert all(row["duels"] == 19 for row in group["matrix"])
    assert all(row["draws"] == 0 for row in group["matrix"])
    assert group["balance_status"] == "fail"
    assert all(row["status"] == "fail" for row in group["rules"])
    assert all(not row["evidence_complete"] for row in group["rules"])


@pytest.mark.parametrize(
    ("seeds", "counts", "balanced"),
    [([1, 2], [1, 1], True), ([1, 2, 3], [2, 1], False), ([1, 3], [2, 0], False)],
)
def test_report_creation_order_balance_uses_observed_parities_not_seed_count(
    tmp_path: Path, seeds: list[int], counts: list[int], balanced: bool
) -> None:
    reports = [telemetry(seed)[1] for seed in seeds]
    manifest = persist(tmp_path, reports)
    assert summarize(tmp_path, manifest) is True
    summary = json.loads((tmp_path / "summary.json").read_text())
    group = summary["groups"][0]
    assert all(row["spawn_first_counts"] == counts for row in group["matrix"])
    assert all(row["spawn_order_balanced"] is balanced for row in group["matrix"])
    mirrors = [row for row in group["rules"] if row["rule"].endswith("_mirror")]
    assert all(row["spawn_first_counts"] == counts for row in mirrors)
    assert all(row["spawn_order_balanced"] is balanced for row in mirrors)


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
    ("arguments", "expected"),
    [
        ((), 2400),
        (("--duel",), 300),
        (("--time-cap", "17"), 17),
        (("--duel", "--time-cap", "17"), 17),
        (("--duel", "--time-cap", "2400"), 2400),
        (("--compare-dilation", "2"), 600),
    ],
)
def test_cap_defaults_and_explicit_overrides_reach_planned_jobs(
    arguments: tuple[str, ...], expected: float
) -> None:
    parser, args = options(*arguments)
    assert all(job["time_cap"] == expected for job in plan_jobs(parser, args))
    validate_options(parser, args)
    assert all(job["time_cap"] == expected for job in plan_jobs(parser, args))


@pytest.mark.parametrize(
    "incompatible",
    [("--variant", "baseline2"), ("--matrix",), ("--compare-dilation", "2")],
)
def test_duel_rejects_incompatible_match_modes(incompatible: tuple[str, ...]) -> None:
    parser, args = options("--duel", *incompatible)
    with pytest.raises(SystemExit) as exit:
        validate_options(parser, args)
    assert exit.value.code == 2
