"""Invalid attempts, missing samples and censored results never inflate report numbers."""

from copy import deepcopy
import json
from pathlib import Path

from harness.simulation_charts import sample_buckets
from harness.simulation_comparison import compare_pair, symmetry_metadata
from harness.simulation_evidence import committed_plan_evidence, save_json
from harness.simulation_report import group_summary, load_results, summarize
from harness.simulation_validation import TEAM_FIELDS, validate_report
from harness.verify import JsonObject
import pytest


def telemetry_team(index: int) -> JsonObject:
    team: JsonObject = dict.fromkeys(TEAM_FIELDS, 0)
    team.update(
        team=index,
        hq_health=100,
        hq_state="online",
        hq_hold_seconds=0,
        units_alive=12,
        units_by_role=[12, 0, 0, 0, 0],
        units_produced=12,
        attacks_observed=2,
        largest_region_unit_share=0.5,
        units_by_region=[],
        buildings=[],
        forces=[],
        region_indices=[],
    )
    return team


def telemetry() -> tuple[JsonObject, JsonObject]:
    job = dict(
        map="/Game/Maps/Test",
        seed=1,
        economy={"human_baseline": 2, "jev_baseline": 2},
        time_cap=60,
        dilation=1,
    )
    teams = [telemetry_team(index) for index in (0, 5)]
    report = dict(
        job,
        schema_version=1,
        status="complete",
        duration=60,
        wall_duration=6,
        time_cap_seconds=60,
        requested_dilation=1,
        effective_dilation=1,
        max_game_delta_seconds=1 / 60,
        outcome="time_cap",
        winner=None,
        hq_position_team0=[0, 0, 0],
        hq_position_team5=[100, 0, 0],
        regions=[
            dict(index=0, role=0, home_team=0, anchor=[0, 0, 0], neighbours=[1]),
            dict(index=1, role=1, home_team=0, anchor=[30, 40, 0], neighbours=[]),
        ],
        deposits=[{}],
        unit_definitions=[{}],
        snapshots=[
            dict(
                time=t,
                scheduled_time=t,
                teams=deepcopy(teams),
                deposits=[],
                enemy_plans=[],
            )
            for t in (0, 30, 60)
        ],
        events=[
            dict(time=30, kind="hq_damage"),
            dict(time=30, kind="first_capture", team=0),
        ],
    )
    return job, report


def test_group_numbers_include_draws_but_only_observed_depletion() -> None:
    reports: list[JsonObject] = [
        dict(
            winner=0,
            outcome="hold_completed",
            duration=120,
            wall_duration=4,
            events=[
                dict(kind="deposit_depleted", time=90),
                dict(kind="deposit_depleted", time=30),
            ],
        ),
        dict(
            winner=5,
            outcome="hold_completed",
            duration=180,
            wall_duration=8,
            events=[dict(kind="deposit_depleted", time=120)],
        ),
        dict(
            winner=None, outcome="time_cap", duration=300, wall_duration=15, events=[]
        ),
    ]
    line, summary = group_summary(("map", "baseline2", 1), reports)
    assert summary == dict(
        map="map",
        variant="baseline2",
        dilation=1,
        complete=3,
        team0_wins=1,
        team5_wins=1,
        draws=1,
        median_duration_minutes=3,
        median_wall_seconds=8,
        depletion_matches=2,
    )
    assert "1/3 (33%) | 1/3 (33%) | 1 | 3.00 | 8.0 | 1.25 (2/3)" in line


def test_curve_buckets_exclude_terminal_offsets_and_small_force_concentration() -> None:
    _, report = telemetry()
    report["snapshots"][0]["teams"][0]["units_alive"] = 11
    second = deepcopy(report)
    second["snapshots"] = second["snapshots"][:2]
    second["snapshots"][1]["teams"][0]["units_alive"] = 20
    second["snapshots"].append(
        dict(scheduled_time=41, teams=report["snapshots"][0]["teams"])
    )
    assert sample_buckets([report, second], 0, "units_alive") == {
        0: [11, 11],
        30: [12, 20],
        60: [12],
    }
    assert sample_buckets([report, second], 0, "largest_region_unit_share") == {
        30: [0.5, 0.5],
        60: [0.5],
    }


def test_results_revalidate_relocated_evidence_and_exclude_nonzero_exit(
    tmp_path: Path,
) -> None:
    job, report = telemetry()
    folder = tmp_path / "match"
    folder.mkdir()
    save_json(folder / "match.json", report)
    good = dict(
        job=job, directory="/old/evidence/match", status="complete", returncode=0
    )
    invalid = dict(good, returncode=2)
    pending = dict(status="interrupted")
    valid, failed = load_results(tmp_path, dict(matches=[good, invalid, pending]))
    assert valid == [(good, report)]
    assert failed[0]["error"] == "Recorded process did not exit zero"
    assert failed[1] == pending


@pytest.mark.parametrize("damage", ["sample", "roles", "role-width", "outcome"])
def test_invalid_telemetry_is_not_a_draw(damage: str) -> None:
    job, report = telemetry()
    if damage == "sample":
        del report["snapshots"][1]
        message = "Missing 30-second sample"
    elif damage == "roles":
        report["snapshots"][1]["teams"][0]["units_by_role"] = [1, 0, 0, 0, 0]
        message = "Role counts disagree"
    elif damage == "role-width":
        report["snapshots"][1]["teams"][0]["units_by_role"] = [12, 0, 0]
        message = "Role counts differ"
    else:
        report["winner"] = 0
        message = "Invalid time-cap draw"
    with pytest.raises(ValueError, match=message):
        validate_report(report, job)


@pytest.mark.parametrize(
    "economy",
    [
        {"baseline": 2},
        {"human_baseline": 2},
        {"human_baseline": 1, "jev_baseline": 2},
        {"human_baseline": 2, "jev_baseline": 1},
    ],
)
def test_report_economy_must_carry_both_launched_baselines(
    economy: JsonObject,
) -> None:
    job, report = telemetry()
    report["economy"] = economy
    with pytest.raises(ValueError, match="human and JEV baselines"):
        validate_report(report, job)


@pytest.mark.parametrize(
    ("launched", "recorded"), [("rush", "default"), ("default", "rush"), ("rush", None)]
)
def test_report_scenario_must_match_the_launched_job(
    launched: str, recorded: str | None
) -> None:
    job, report = telemetry()
    job["scenario"] = launched
    if recorded is not None:
        report["scenario"] = recorded
    else:
        report.pop("scenario", None)
    with pytest.raises(ValueError, match="scenario"):
        validate_report(report, job)


def test_a_report_without_a_scenario_is_the_default_autopilot() -> None:
    job, report = telemetry()
    validate_report(report, job)
    job["scenario"] = "rush"
    report["scenario"] = "rush"
    validate_report(report, job)


def finish(
    report: JsonObject,
    outcome: str,
    winner: int | None,
    states: tuple[str, str],
) -> None:
    """End the report with these HQ states; only an online HQ has hit points."""
    report.update(outcome=outcome, winner=winner)
    for team, state in zip(report["snapshots"][-1]["teams"], states, strict=True):
        team["hq_state"] = state
        team["hq_health"] = 100 if state == "online" else 0


@pytest.mark.parametrize("missing", ["snapshot", "creation", "escalation"])
def test_missing_plans_are_excluded_instead_of_crashing_aggregation(
    tmp_path: Path,
    missing: str,
) -> None:
    job, report = telemetry()
    plan = telemetry_plan()
    if missing == "snapshot":
        del report["snapshots"][-1]["enemy_plans"]
    elif missing == "creation":
        report["snapshots"][-1]["enemy_plans"] = [plan]
    else:
        report["events"].append(dict(plan, time=2, kind="plan_created", team=5))
        report["snapshots"][-1]["enemy_plans"] = [dict(plan, escalated=True)]
    folder = tmp_path / "match"
    folder.mkdir()
    save_json(folder / "match.json", report)
    record = dict(job=job, directory=str(folder), status="complete", returncode=0)
    assert not summarize(tmp_path, dict(matches=[record], planned_jobs=[job]))
    evidence = json.loads((tmp_path / "summary.json").read_text())
    assert evidence["groups"] == []
    assert evidence["failures"][0]["status"] == "failed"


def test_incomplete_batch_and_missing_pair_fail_reporting(tmp_path: Path) -> None:
    manifest = dict(
        matches=[],
        planned_jobs=[dict(map="map", variant="v", seed=7)],
        comparison_dilation=2,
        comparison_tolerance=0.05,
    )
    assert summarize(tmp_path, manifest) is False
    text = (tmp_path / "Report.md").read_text()
    assert "Complete matches: **0**; failed/interrupted: **0**" in text
    assert "Missing valid paired match" in text
    assert "1 planned matches were not attempted" in text


def test_pair_requires_combat_and_compares_maximum_sample_delta() -> None:
    _, one = telemetry()
    high = deepcopy(one)
    high["snapshots"][1]["teams"][0]["wallet"] = 10
    result = compare_pair(one, high, 0.05)
    assert result["status"] == "fail_or_inconclusive"
    assert result["max_absolute_deltas"]["team0.wallet"] == 10
    assert result["issues"] == ["t=30 team0.wallet: 0 vs 10"]
    high = deepcopy(one)
    high["events"] = []
    result = compare_pair(one, high, 0.05)
    assert "Sample has not exercised HQ combat" in result["issues"]
    assert "Sample has not exercised capture" in result["issues"]


def test_layout_uses_planar_distance_and_unreachable_reward() -> None:
    _, report = telemetry()
    report["regions"].append(
        dict(index=2, role=2, home_team=-1, anchor=[0, 0, 100], neighbours=[])
    )
    result = symmetry_metadata(report)
    assert result["0"]["nearest_natural_cm"] == 50
    assert result["0"]["reward_hops"] == {"2": None}
    assert result["5"]["nearest_natural_cm"] == pytest.approx((70**2 + 40**2) ** 0.5)


def telemetry_plan() -> JsonObject:
    return dict(
        ticket=1,
        force=10,
        verb=1,
        source_region=0,
        source_controller=5,
        order_changed=True,
        target_region=1,
        target_structure="EnemyHQ",
        size_band=8,
        eta_seconds=10,
        committed_until_seconds=27,
        remaining_commitment_seconds=25,
        escalated=False,
        memo="Ticket #1 · Attacking with ~8 units",
    )


def test_short_lived_plans_count_without_any_active_snapshot() -> None:
    job, report = telemetry()
    plan = telemetry_plan()
    report["events"] += [
        dict(plan, time=2, kind="plan_created", team=5),
        dict(plan, time=4, kind="plan_escalated", team=5, verb=0, escalated=True),
        dict(plan, time=6, kind="plan_created", team=5, ticket=2, verb=2),
    ]
    validate_report(report, job)
    evidence = committed_plan_evidence(report)
    assert evidence["counts"] == dict(
        move_and_hold=0, attack=1, retreat=1, total=2, escalated=1
    )
    assert evidence["active_plans"] == []


def test_reused_ticket_is_excluded_from_report_counts(tmp_path: Path) -> None:
    job, report = telemetry()
    plan = telemetry_plan()
    report["events"] += [
        dict(plan, time=2, kind="plan_created", team=5),
        dict(plan, time=4, kind="plan_created", team=5, force=11),
    ]
    folder = tmp_path / "match"
    folder.mkdir()
    save_json(folder / "match.json", report)
    record = dict(job=job, directory=str(folder), status="complete", returncode=0)
    valid, failed = load_results(tmp_path, dict(matches=[record]))
    assert valid == []
    assert failed[0]["error"] == "JEV ticket reused by another force"


def test_four_match_plan_counts_ignore_republication_and_autopilot(
    tmp_path: Path,
) -> None:
    records = []
    for seed in range(1, 5):
        job, report = telemetry()
        job.update(seed=seed, variant="baseline2")
        report["seed"] = seed
        plan = telemetry_plan()
        # One ticket persists across evaluation publications and changes verb
        # during escalation. Its creation verb still counts only once.
        report["events"] += [
            dict(plan, time=time, kind="plan_created", team=5) for time in (2, 4, 6)
        ]
        report["events"] += [
            dict(plan, time=time, kind="plan_escalated", team=5, verb=0, escalated=True)
            for time in (8, 10)
        ]
        report["events"] += [
            dict(plan, time=12, kind="plan_created", team=5, verb=0, escalated=True),
            dict(plan, time=14, kind="plan_created", team=5, ticket=2, verb=2),
            dict(plan, time=16, kind="plan_created", team=5, ticket=3, verb=0),
            dict(plan, time=18, kind="plan_created", team=0, ticket=4),
            dict(plan, time=20, kind="plan_escalated", team=0, ticket=4),
            dict(time=22, kind="region_control", team=5, region=1, previous_team=0),
        ]
        report["events"].sort(key=lambda event: event["time"])
        for snapshot in report["snapshots"][1:]:
            snapshot["enemy_plans"] = [dict(plan, verb=0, escalated=True)]
        directory = tmp_path / f"match-{seed}"
        directory.mkdir()
        save_json(directory / "match.json", report)
        records.append(
            dict(job=job, directory=str(directory), status="complete", returncode=0)
        )
    manifest = dict(matches=records, planned_jobs=[row["job"] for row in records])
    assert summarize(tmp_path, manifest)
    evidence = json.loads((tmp_path / "summary.json").read_text())["jev_plans"]
    assert evidence["counts"] == dict(
        move_and_hold=4, attack=4, retreat=4, total=12, escalated=4
    )
    for match in evidence["matches"]:
        assert match["counts"] == dict(
            move_and_hold=1, attack=1, retreat=1, total=3, escalated=1
        )
        assert match["captures"] == 1
        assert match["attacks_observed"] == 2
    text = (tmp_path / "Report.md").read_text()
    assert "| **Total** | | **4** | **4** | **4** | **12** | **4** |" in text
    assert "Escalation by source-region owner at event time" in text
    assert "| **Total** | jev | **0** | **4** | **0** |" in text


def test_escalation_uses_captured_owner_and_creation_is_deduplicated() -> None:
    job, report = telemetry()
    plan = telemetry_plan()
    report["events"] += [
        dict(plan, time=1, kind="plan_created", team=5, verb=0, escalated=True),
        dict(plan, time=2, kind="plan_created", team=5, verb=0, escalated=True),
        dict(plan, time=3, kind="plan_created", team=5, ticket=2),
        dict(time=3.5, kind="region_control", team=5, region=0, previous_team=-1),
        dict(
            plan,
            time=4,
            kind="plan_escalated",
            team=5,
            ticket=2,
            escalated=True,
            source_controller=-1,
            order_changed=False,
        ),
        dict(
            plan,
            time=5,
            kind="plan_escalated",
            team=5,
            ticket=2,
            escalated=True,
            source_controller=5,
            order_changed=False,
        ),
        dict(plan, time=6, kind="plan_created", team=5, ticket=3),
        dict(
            plan,
            time=7,
            kind="plan_escalated",
            team=5,
            ticket=3,
            escalated=True,
            source_controller=0,
        ),
        dict(plan, time=8, kind="plan_created", team=0, ticket=4, escalated=True),
    ]
    report["snapshots"][-1]["enemy_plans"] = [dict(plan, verb=0, escalated=True)]
    validate_report(report, job)
    evidence = committed_plan_evidence(report)
    assert evidence["counts"] == dict(
        move_and_hold=1, attack=2, retreat=0, total=3, escalated=3
    )
    assert evidence["escalation_by_owner"] == {
        "jev": dict(creation_defense=1, order_changing=0, label_only=0),
        "neutral": dict(creation_defense=0, order_changing=0, label_only=1),
        "player": dict(creation_defense=0, order_changing=1, label_only=0),
    }
    assert evidence["escalation_counts"] == dict(
        creation_defense=1, order_changing=1, label_only=1
    )


@pytest.mark.parametrize("kind", ["plan_created", "plan_escalated"])
@pytest.mark.parametrize("field", ["source_controller", "order_changed"])
def test_plan_events_require_exact_escalation_evidence(kind: str, field: str) -> None:
    job, report = telemetry()
    plan = telemetry_plan()
    if kind == "plan_escalated":
        report["events"].append(dict(plan, time=1, kind="plan_created", team=5))
    event = dict(plan, time=2, kind=kind, team=5, escalated=True)
    del event[field]
    report["events"].append(event)
    with pytest.raises(ValueError):
        validate_report(report, job)
