"""Shield telemetry, support composition and multi-relation counters in the duel report."""

from copy import deepcopy
import json
from pathlib import Path

from harness.simulation_duel_rules import evaluate_group
from harness.simulation_report import summarize
from harness.simulation_validation import validate_report
from harness.verify import JsonObject
import pytest
from simulation_duel_support import (
    legacy_telemetry,
    persist,
    refresh_duration,
    rule_rows,
    set_composition_outcome,
    set_outcome,
    telemetry,
    threshold_reports,
)


def composition_reports(
    seeds: int, baseline_wins: int, supported_wins: int, draws: int = 0
) -> list[JsonObject]:
    """Every seed fights each scenario on both sides; the subject wins in the first N such fights."""
    reports = [telemetry(seed)[1] for seed in range(seeds)]
    for kind, wins in (("baseline", baseline_wins), ("with_support", supported_wins)):
        rows = [
            row
            for report in reports
            for row in report["compositions"]
            if row["scenario"] == kind
        ]
        for index, row in enumerate(rows):
            roster = {unit["id"]: unit for unit in reports[0]["unit_definitions"]}
            if index < wins:
                outcome = row["subject_team"]
            elif index < len(rows) - draws:
                outcome = 0 if row["subject_team"] == 5 else 5
            else:
                outcome = None
            set_composition_outcome(roster, row, outcome)
    for report in reports:
        refresh_duration(report)
    return reports


@pytest.mark.parametrize(
    ("supported_wins", "expected"), [(17, "fail"), (18, "pass"), (30, "pass")]
)
def test_support_composition_needs_twenty_points_at_equal_budget(
    supported_wins: int, expected: str
) -> None:
    reports = composition_reports(20, 10, supported_wins)
    for report in reports:
        validate_report(report, telemetry(report["seed"])[0])
    rule = rule_rows(evaluate_group(reports))["scrambler_composition"]
    assert rule["baseline"]["fights"] == rule["with_support"]["fights"] == 40
    assert rule["baseline"]["win_rate"] == 0.25
    assert rule["gain_points"] == pytest.approx(supported_wins / 40 * 100 - 25)
    assert (rule["partner"], rule["target"]) == ("ranged", "lancer")
    assert rule["status"] == expected


def test_support_composition_draws_stay_in_the_denominator() -> None:
    rule = rule_rows(evaluate_group(composition_reports(20, 10, 18, draws=4)))[
        "scrambler_composition"
    ]
    assert rule["with_support"]["wins"] == 18
    assert rule["with_support"]["fights"] == 40
    assert rule["status"] == "pass"


def test_support_composition_needs_forty_fights_per_scenario() -> None:
    reports = composition_reports(19, 0, 30)
    rule = rule_rows(evaluate_group(reports))["scrambler_composition"]
    assert rule["gain_points"] > 20
    assert rule["baseline"]["fights"] == 38
    assert rule["status"] == "fail"


def test_support_units_are_judged_only_by_composition() -> None:
    reports = threshold_reports()
    before = evaluate_group(reports)
    for report in reports:
        for row in report["duels"]:
            if (
                "scrambler" in (row["left"], row["right"])
                and row["left"] != row["right"]
            ):
                set_outcome(report, row, 0 if row["left"] == "lancer" else 5)
        refresh_duration(report)
    group = evaluate_group(reports)
    rules = rule_rows(group)
    assert not [name for name in rules if name.startswith("scrambler_p")]
    assert "scrambler" not in group["worth"]
    # The Scrambler losing or winning every one-on-one changes no combat unit's worth.
    assert group["worth"] == before["worth"]
    assert rules["no_hp_and_dps_per_power_dominance"]["status"] == "pass"
    assert rules["counter_table_coverage"]["status"] == "pass"


def test_dominance_counts_shield_as_durability() -> None:
    _, report = telemetry()
    lancer = next(unit for unit in report["unit_definitions"] if unit["id"] == "lancer")
    lancer["shield"] = 0
    # Without the shield the Lancer is the least durable per Power and dominates nobody; with it, ties hold.
    assert (
        rule_rows(evaluate_group([report]))["no_hp_and_dps_per_power_dominance"][
            "status"
        ]
        == "pass"
    )
    lancer["health"] += 100
    lancer["shield"] = 100
    efficiency = rule_rows(evaluate_group([report]))[
        "no_hp_and_dps_per_power_dominance"
    ]["efficiencies"]
    assert efficiency["lancer"]["hp_per_power"] == pytest.approx(
        (lancer["health"] + 100) / 45
    )


@pytest.mark.parametrize(
    "fault",
    [
        "killed_shields_not_removed",
        "shield_damage_without_shields",
        "shield_telemetry_missing",
    ],
)
def test_shield_telemetry_is_checked(fault: str) -> None:
    job, report = telemetry()
    row = next(
        row
        for row in report["duels"]
        if (row["left"], row["right"]) == ("frontline", "lancer")
    )
    set_outcome(report, row, 0)
    validate_report(report, job)
    if fault == "killed_shields_not_removed":
        row["shield_damage_dealt"][0] = 0
    elif fault == "shield_damage_without_shields":
        row["shield_damage_dealt"][1] = 5
    else:
        del row["shield_damage_dealt"]
    with pytest.raises(ValueError):
        validate_report(report, job)


@pytest.mark.parametrize(
    "fault",
    [
        "missing_set",
        "duplicate",
        "wrong_squad",
        "unknown_scenario",
        "over_budget",
        "creation_order",
    ],
)
def test_composition_set_must_be_complete_and_match_the_scenario(fault: str) -> None:
    job, report = telemetry()
    validate_report(report, job)
    rows = report["compositions"]
    if fault == "missing_set":
        rows.pop()
    elif fault == "duplicate":
        rows.append(deepcopy(rows[0]))
    elif fault == "wrong_squad":
        rows[2]["left_units"][0]["count"] += 1
    elif fault == "unknown_scenario":
        rows[0]["scenario"] = "alone"
    elif fault == "over_budget":
        rows[0]["spent"][0] = 121
    else:
        rows[0]["spawn_first_team"] = 5
    with pytest.raises(ValueError):
        validate_report(report, job)


def test_runs_before_shields_still_validate_and_report(tmp_path: Path) -> None:
    job, report = legacy_telemetry()
    validate_report(report, job)
    manifest = persist(tmp_path, [report])
    assert summarize(tmp_path, manifest) is True
    assert "Shield damage" in (tmp_path / "Report.md").read_text()


def test_report_shows_shields_and_composition_fights(tmp_path: Path) -> None:
    manifest = persist(tmp_path, [telemetry(1)[1]])
    assert summarize(tmp_path, manifest) is True
    text = (tmp_path / "Report.md").read_text()
    assert "### Support composition fights" in text
    assert "with_support (scrambler+ranged vs lancer)" in text
    assert "| lancer / Lancer | 45 | 2 | 145 | 80 |" in text
    group = json.loads((tmp_path / "summary.json").read_text())["groups"][0]
    assert len(group["compositions"]) == 4
    assert {row["rule"] for row in group["rules"]} >= {
        "scrambler_composition",
        "lancer_prey_frontline",
        "frontline_predator_lancer",
    }
