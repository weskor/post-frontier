"""A completed hold is the decisive outcome: the loser's HQ is lost, the winner's is not."""

from harness.simulation_validation import validate_report
from harness.verify import JsonObject
import pytest
from test_simulation_reports import finish, telemetry


def finish_hold(
    report: JsonObject, winner: int, states: tuple[str, str], hq: tuple[int, int]
) -> None:
    finish(report, "hold_completed", winner, hq)
    for team, state in zip(report["snapshots"][-1]["teams"], states, strict=True):
        team["hq_state"] = state


@pytest.mark.parametrize(
    ("winner", "states", "hq", "message"),
    [
        (0, ("online", "offline"), (100, 0), "team 5 HQ is not lost"),
        (0, ("online", "online"), (100, 100), "team 5 HQ stands"),
        (5, ("lost", "lost"), (0, 0), "team 5 HQ is lost"),
    ],
)
def test_a_completed_hold_needs_the_loser_lost_and_the_winner_not(
    winner: int, states: tuple[str, str], hq: tuple[int, int], message: str
) -> None:
    job, report = telemetry()
    finish_hold(report, winner, states, hq)
    with pytest.raises(ValueError, match=message):
        validate_report(report, job)


def test_a_completed_hold_without_hq_states_is_not_a_result() -> None:
    job, report = telemetry()
    finish(report, "hold_completed", 0, (100, 0))
    with pytest.raises(ValueError, match="has no hq_state"):
        validate_report(report, job)


@pytest.mark.parametrize(
    ("winner", "states", "hq"),
    [
        (0, ("online", "lost"), (100, 0)),
        # The winner's own HQ may be offline when the loser's hold completes.
        (0, ("offline", "lost"), (0, 0)),
        (5, ("lost", "online"), (0, 100)),
    ],
)
def test_consistent_completed_holds_validate(
    winner: int, states: tuple[str, str], hq: tuple[int, int]
) -> None:
    job, report = telemetry()
    finish_hold(report, winner, states, hq)
    validate_report(report, job)
