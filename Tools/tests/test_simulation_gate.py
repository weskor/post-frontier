"""Decisive vs censored battle statistics, the 1b gate's sample discipline and the rush scenario's plan."""

import argparse
from pathlib import Path

from harness.simulation_comparison import (
    FAIL,
    INSUFFICIENT,
    PASS,
    battle_statistics,
    evaluate_gate_1b,
    rush_evidence,
)
from harness.simulation_execution import command
from harness.simulation_planning import (
    DEFAULT_ECONOMY,
    GATE_SEEDS,
    GATE_TIME_CAP,
    MAP_V2,
    plan_jobs,
    validate_options,
)
from harness.simulation_report import group_matches
from harness.simulation_validation import interpret_outcome
from harness.verify import JsonObject
import pytest
from x.content.simulation import configure

CAP = 1200


def match(
    variant: str = "baseline2",
    scenario: str = "default",
    seconds: float = 600,
    winner: int | None = 0,
    cap: float = CAP,
    baseline: int | None = None,
) -> tuple[JsonObject, JsonObject]:
    """A (record, report) pair as load_results yields it; winner None is a time-cap draw."""
    human = baseline if baseline is not None else int(variant[-1])
    job: JsonObject = dict(
        map=MAP_V2,
        variant=variant,
        scenario=scenario,
        economy=dict(DEFAULT_ECONOMY, human_baseline=human),
        time_cap=cap,
        dilation=1,
    )
    report: JsonObject = dict(
        outcome="time_cap" if winner is None else "hq_destroyed",
        winner=winner,
        duration=cap if winner is None else seconds,
        time_cap_seconds=cap,
        events=[],
    )
    return dict(job=job), report


def cell(
    count: int = GATE_SEEDS,
    variant: str = "baseline2",
    scenario: str = "default",
    seconds: float = 600,
) -> list[tuple[JsonObject, JsonObject]]:
    return [match(variant, scenario, seconds) for _ in range(count)]


def passing_gate(
    rush_seconds: float = 700,
) -> list[tuple[JsonObject, JsonObject]]:
    rows = []
    for variant in ("baseline2", "baseline1"):
        rows += cell(variant=variant, seconds=600)
        rows += cell(variant=variant, scenario="rush", seconds=rush_seconds)
    return rows


def checks(result: JsonObject) -> dict[str, JsonObject]:
    return {row["check"]: row for row in result["checks"]}


def test_statistics_keep_decisive_and_censored_apart() -> None:
    reports = [
        match(seconds=400)[1],
        match(seconds=600)[1],
        match(seconds=800, winner=5)[1],
        match(winner=None)[1],
        match(winner=None)[1],
    ]
    stats = battle_statistics(reports)
    assert (stats["matches"], stats["decisive"], stats["censored"]) == (5, 3, 2)
    assert stats["decisive_median_seconds"] == 600
    # Censored matches count at the cap: [400, 600, 800, 1200, 1200].
    assert stats["median_with_censored_seconds"] == 800
    assert not stats["median_is_lower_bound"]
    # JEV's win at 800 s is a decisive length but not a victory.
    assert stats["earliest_victory_seconds"] == 400


def test_all_censored_has_no_decisive_length_and_no_victory() -> None:
    stats = battle_statistics([match(winner=None)[1] for _ in range(3)])
    assert stats["decisive"] == 0
    assert stats["decisive_median_seconds"] is None
    assert stats["earliest_victory_seconds"] is None
    assert stats["median_with_censored_seconds"] == CAP


def test_unknown_outcome_is_not_a_result_and_decisive_needs_a_winner() -> None:
    with pytest.raises(ValueError, match="Unknown outcome"):
        interpret_outcome(dict(outcome="hq_overrun", duration=10, winner=0))
    for kind in ("hq_destroyed", "hold_completed"):
        with pytest.raises(ValueError, match="without a winning team"):
            interpret_outcome(dict(outcome=kind, duration=10, winner=None))
    # A completed hold is a decisive result for the side that held the main.
    assert interpret_outcome(dict(outcome="hold_completed", duration=10, winner=5)).decisive
    assert not interpret_outcome(
        dict(outcome="time_cap", duration=10, winner=None)
    ).decisive


def test_a_censored_value_at_the_middle_rank_is_a_bound_not_a_median() -> None:
    # Ten JEV wins at 240 s and ten stalemates: the mean of the middle ranks is 720 s,
    # exactly the 12 min ceiling, yet half the battles outlast the cap.
    reports = [match(seconds=240, winner=5)[1] for _ in range(10)]
    reports += [match(winner=None)[1] for _ in range(10)]
    stats = battle_statistics(reports)
    assert stats["median_with_censored_seconds"] == 720
    assert stats["median_is_lower_bound"]


def test_censored_below_the_middle_ranks_leave_the_median_exact() -> None:
    reports = [match(seconds=240, winner=5)[1] for _ in range(11)]
    reports += [match(winner=None)[1] for _ in range(9)]
    stats = battle_statistics(reports)
    assert stats["median_with_censored_seconds"] == 240
    assert not stats["median_is_lower_bound"]


def test_gate_never_passes_a_median_that_only_bounds_the_ceiling() -> None:
    rows = passing_gate()
    for variant in ("baseline2", "baseline1"):
        defaults = [
            row
            for row in rows
            if row[0]["job"]["variant"] == variant
            and row[0]["job"]["scenario"] == "default"
        ]
        for index, (_, report) in enumerate(defaults):
            if index < 10:
                report.update(winner=5, duration=240)
            else:
                report.update(outcome="time_cap", winner=None, duration=CAP)
    result = evaluate_gate_1b(rows)
    length = checks(result)["baseline2: median battle length 8-12 min"]
    assert length["status"] == FAIL
    assert "middle rank" in length["detail"]
    assert result["status"] == FAIL


def test_a_draw_cannot_name_a_winner() -> None:
    with pytest.raises(ValueError, match="Invalid time-cap draw"):
        interpret_outcome(dict(outcome="time_cap", duration=9, winner=0))


def test_gate_passes_only_at_full_sample() -> None:
    assert evaluate_gate_1b(passing_gate())["status"] == PASS


def test_gate_refuses_pass_on_fewer_seeds() -> None:
    rows = passing_gate()
    # Drop one baseline2 default seed: 19 of 20.
    first = next(
        row
        for row in rows
        if row[0]["job"]["variant"] == "baseline2"
        and row[0]["job"]["scenario"] == "default"
    )
    rows.remove(first)
    result = evaluate_gate_1b(rows)
    assert result["status"] == INSUFFICIENT
    length = checks(result)["baseline2: median battle length 8-12 min"]
    assert length["status"] == INSUFFICIENT
    assert "19 seeds" in length["detail"]


def test_four_seed_run_in_band_is_not_a_pass() -> None:
    rows = []
    for variant in ("baseline2", "baseline1"):
        rows += cell(count=4, variant=variant, seconds=600)
        rows += cell(count=4, variant=variant, scenario="rush", seconds=700)
    assert evaluate_gate_1b(rows)["status"] == INSUFFICIENT


def test_gate_requires_the_gate_cap() -> None:
    rows = passing_gate()
    for record, _ in rows:
        record["job"]["time_cap"] = 2400
    result = evaluate_gate_1b(rows)
    assert result["status"] == INSUFFICIENT
    assert "1200" in " ".join(row["detail"] for row in result["checks"])


def test_gate_requires_both_baselines() -> None:
    rows = cell(variant="baseline2") + cell(variant="baseline2", scenario="rush")
    result = evaluate_gate_1b(rows)
    assert result["status"] == INSUFFICIENT
    assert checks(result)["1/s baseline measured"]["status"] == INSUFFICIENT


@pytest.mark.parametrize("seconds", [480, 720])
def test_length_band_edges_are_inclusive(seconds: float) -> None:
    rows = passing_gate()
    for record, report in rows:
        if record["job"]["scenario"] == "default":
            report["duration"] = seconds
    assert evaluate_gate_1b(rows)["status"] == PASS


@pytest.mark.parametrize("seconds", [479, 721])
def test_length_outside_band_fails(seconds: float) -> None:
    rows = passing_gate()
    for record, report in rows:
        if record["job"]["scenario"] == "default":
            report["duration"] = seconds
    result = evaluate_gate_1b(rows)
    assert result["status"] == FAIL
    assert checks(result)["baseline2: median battle length 8-12 min"]["status"] == FAIL


def test_censored_matches_count_at_the_cap_not_as_short_battles() -> None:
    rows = passing_gate()
    defaults = [row for row in rows if row[0]["job"]["scenario"] == "default"]
    # Ten of twenty baseline2 matches time out; the median is then (600 + 1200) / 2 = 900 s.
    capped = [row for row in defaults if row[0]["job"]["variant"] == "baseline2"][:10]
    for _, report in capped:
        report.update(outcome="time_cap", winner=None, duration=CAP)
    result = evaluate_gate_1b(rows)
    row = checks(result)["baseline2: median battle length 8-12 min"]
    assert row["status"] == FAIL
    assert "10 censored" in row["detail"]


def test_rush_victory_before_the_floor_fails_even_on_four_seeds() -> None:
    rows = cell(count=3, scenario="rush", seconds=900) + cell(
        count=1, scenario="rush", seconds=359
    )
    result = evaluate_gate_1b(rows)
    assert result["status"] == FAIL
    rush = checks(result)["baseline2: no rush victory before 360 s"]
    assert rush["status"] == FAIL and "359.0" in rush["detail"]


def test_rush_victory_at_the_floor_passes_and_jev_wins_do_not_count() -> None:
    assert evaluate_gate_1b(passing_gate(rush_seconds=360))["status"] == PASS
    rows = passing_gate()
    rows += [match(scenario="rush", seconds=100, winner=5)]
    assert evaluate_gate_1b(rows)["status"] == PASS


def test_gate_ignores_other_maps() -> None:
    rows = passing_gate()
    other, report = match(seconds=30)
    other["job"]["map"] = "/Game/Maps/AvailabilityZone"
    assert evaluate_gate_1b([*rows, (other, report)])["status"] == PASS


def test_rush_scenario_groups_apart_from_default() -> None:
    groups = group_matches([match(), match(scenario="rush")])
    assert {key[1] for key in groups} == {"baseline2", "baseline2+rush"}


def test_rush_evidence_measures_the_delay_from_first_living_to_attack() -> None:
    def events(*rows: tuple[str, str, float]) -> JsonObject:
        return dict(
            events=[dict(kind=kind, id=name, time=time) for kind, name, time in rows]
        )

    evidence = rush_evidence(
        [
            events(
                ("rush_force_seen", "a", 10),
                ("rush_force_attacking", "a", 12),
                ("rush_force_seen", "b", 40),
                ("rush_force_attacking", "b", 46),
                ("rush_force_seen", "c", 50),
            ),
            events(
                ("rush_force_seen", "a", 5),
                ("rush_force_attacking", "a", 5),
                ("rush_force_withdrawing", "a", 60),
                ("rush_force_resumed", "a", 90),
                ("rush_force_withdrawing", "a", 120),
                ("rush_force_retreat", "a", 130),
            ),
        ]
    )
    assert evidence == dict(
        forces_seen=4,
        forces_attacking=3,
        median_order_delay_seconds=2,
        max_order_delay_seconds=6,
        withdrawals=2,
        resumes=1,
        retreats=1,
    )


def test_a_baseline_needs_the_default_economy_apart_from_the_human_income() -> None:
    rows = cell(variant="baseline2") + cell(variant="baseline2", scenario="rush")
    for scenario in ("default", "rush"):
        for _ in range(GATE_SEEDS):
            record, report = match("baseline1", scenario)
            record["job"]["variant"] = "custom"
            record["job"]["economy"]["jev_baseline"] = 9
            rows.append((record, report))
    result = evaluate_gate_1b(rows)
    assert result["status"] == INSUFFICIENT
    assert checks(result)["1/s baseline measured"]["status"] == INSUFFICIENT
    assert not any(row["check"].startswith("custom") for row in result["checks"])


def options(*arguments: str) -> tuple[argparse.ArgumentParser, argparse.Namespace]:
    parser = argparse.ArgumentParser()
    configure(parser)
    return parser, parser.parse_args(arguments)


def test_gate_defaults_plan_the_full_sample() -> None:
    parser, args = options("--gate", "1b")
    validate_options(parser, args)
    jobs = plan_jobs(parser, args)
    assert len(jobs) == 2 * 2 * GATE_SEEDS
    assert {job["map"] for job in jobs} == {MAP_V2}
    assert {job["time_cap"] for job in jobs} == {GATE_TIME_CAP}
    assert {job["scenario"] for job in jobs} == {"default", "rush"}
    assert {job["economy"]["human_baseline"] for job in jobs} == {1, 2}


def test_scenario_defaults_to_the_current_autopilot() -> None:
    parser, args = options("--matches", "1", "--map", MAP_V2)
    assert {job["scenario"] for job in plan_jobs(parser, args)} == {"default"}
    parser, args = options("--matches", "2", "--scenario", "rush", "--map", MAP_V2)
    jobs = plan_jobs(parser, args)
    assert [(job["scenario"], job["seed"]) for job in jobs] == [
        ("rush", 1),
        ("rush", 2),
    ]


@pytest.mark.parametrize(
    "arguments",
    [
        ("--gate", "1b", "--matrix"),
        ("--gate", "1b", "--duel"),
        ("--duel", "--scenario", "rush"),
        ("--scenario", "rush", "--scenario", "rush"),
    ],
)
def test_incompatible_scenario_and_gate_options_are_rejected(
    arguments: tuple[str, ...],
) -> None:
    parser, args = options(*arguments)
    with pytest.raises(SystemExit) as exit:
        validate_options(parser, args)
    assert exit.value.code == 2


def test_the_game_is_launched_with_the_jobs_scenario(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("UE_ROOT", str(tmp_path))
    parser, args = options("--scenario", "rush", "--matches", "1", "--map", MAP_V2)
    (job,) = plan_jobs(parser, args)
    assert "-SimScenario=rush" in command(args, job, tmp_path / "match.json")
    job["scenario"] = "default"
    assert "-SimScenario=default" in command(args, job, tmp_path / "match.json")
