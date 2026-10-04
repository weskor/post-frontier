"""A battle ends on a completed hold; an offline HQ at the cap is censored, never a destroyed HQ."""

from harness.simulation_comparison import battle_statistics
from harness.simulation_validation import interpret_outcome, validate_report
import pytest
from test_simulation_reports import finish, telemetry


@pytest.mark.parametrize(
    ("outcome", "winner", "states", "message"),
    [
        ("hold_completed", 0, ("online", "offline"), "team 5 HQ is not lost"),
        ("hold_completed", 0, ("online", "online"), "team 5 HQ is not lost"),
        ("hold_completed", 5, ("online", "lost"), "team 0 HQ is not lost"),
        ("hold_completed", 0, ("lost", "lost"), "team 0 HQ is lost"),
        ("time_cap", None, ("online", "lost"), "team 5 HQ is lost"),
        ("time_cap", None, ("lost", "online"), "team 0 HQ is lost"),
    ],
)
def test_outcome_must_agree_with_the_final_hq_lifecycle(
    outcome: str, winner: int | None, states: tuple[str, str], message: str
) -> None:
    job, report = telemetry()
    finish(report, outcome, winner, states)
    with pytest.raises(ValueError, match=message):
        validate_report(report, job)


@pytest.mark.parametrize(
    ("winner", "states"),
    [
        (0, ("online", "lost")),
        # The winner's own HQ may be offline when the loser's hold completes.
        (0, ("offline", "lost")),
        (5, ("lost", "online")),
        # Simultaneous completions: team 5 wins, as in the game's outcome policy.
        (5, ("lost", "lost")),
    ],
)
def test_a_completed_hold_is_decisive_for_the_side_that_held(
    winner: int, states: tuple[str, str]
) -> None:
    job, report = telemetry()
    finish(report, "hold_completed", winner, states)
    validate_report(report, job)
    outcome = interpret_outcome(report)
    assert outcome.decisive
    assert outcome.winner == winner


@pytest.mark.parametrize(
    "states",
    [
        ("online", "online"),
        ("offline", "online"),
        ("online", "offline"),
        ("offline", "offline"),
    ],
)
def test_an_offline_hq_at_the_cap_is_a_censored_draw(states: tuple[str, str]) -> None:
    job, report = telemetry()
    finish(report, "time_cap", None, states)
    validate_report(report, job)
    outcome = interpret_outcome(report)
    assert not outcome.decisive
    assert outcome.winner is None
    # It counts as a censored match at the cap, never as a decisive length or a victory.
    stats = battle_statistics([report])
    assert (stats["decisive"], stats["censored"]) == (0, 1)
    assert stats["earliest_victory_seconds"] is None


def test_hq_destroyed_is_not_a_result() -> None:
    # An HQ at 0 HP is only offline: the old outcome name no longer exists.
    job, report = telemetry()
    finish(report, "hq_destroyed", 0, ("online", "offline"))
    with pytest.raises(ValueError, match="Unknown outcome"):
        validate_report(report, job)


def test_hq_state_is_required_in_every_snapshot() -> None:
    job, report = telemetry()
    del report["snapshots"][1]["teams"][0]["hq_state"]
    with pytest.raises(ValueError, match="hq_state"):
        validate_report(report, job)


@pytest.mark.parametrize(
    ("state", "health"), [("online", 0), ("offline", 100), ("lost", 100)]
)
def test_only_an_online_hq_has_hit_points(state: str, health: int) -> None:
    job, report = telemetry()
    team = report["snapshots"][1]["teams"][1]
    team["hq_state"] = state
    team["hq_health"] = health
    with pytest.raises(ValueError, match="only an online HQ has hit points"):
        validate_report(report, job)
