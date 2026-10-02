"""Invalid attempts, missing samples and censored results never inflate report numbers."""

from copy import deepcopy
from pathlib import Path

from harness.simulation_charts import sample_buckets
from harness.simulation_comparison import compare_pair, symmetry_metadata
from harness.simulation_evidence import save_json
from harness.simulation_report import group_summary, load_results, summarize
from harness.simulation_validation import TEAM_FIELDS, validate_report
from harness.verify import JsonObject
import pytest


def telemetry_team(index: int) -> JsonObject:
    team: JsonObject = dict.fromkeys(TEAM_FIELDS, 0)
    team.update(
        team=index,
        hq_health=100,
        units_alive=12,
        units_by_role=[12, 0, 0],
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
        map="/Game/Maps/Test", seed=1, economy={"baseline": 2}, time_cap=60, dilation=1
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
            dict(time=t, scheduled_time=t, teams=deepcopy(teams), deposits=[])
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
            outcome="hq_destroyed",
            duration=120,
            wall_duration=4,
            events=[
                dict(kind="deposit_depleted", time=90),
                dict(kind="deposit_depleted", time=30),
            ],
        ),
        dict(
            winner=5,
            outcome="hq_destroyed",
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


@pytest.mark.parametrize("damage", ["sample", "roles", "outcome"])
def test_invalid_telemetry_is_not_a_draw(damage: str) -> None:
    job, report = telemetry()
    if damage == "sample":
        del report["snapshots"][1]
        message = "Missing 30-second sample"
    elif damage == "roles":
        report["snapshots"][1]["teams"][0]["units_by_role"] = [1, 0, 0]
        message = "Role counts disagree"
    else:
        report["winner"] = 0
        message = "Invalid time-cap draw"
    with pytest.raises(ValueError, match=message):
        validate_report(report, job)


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
