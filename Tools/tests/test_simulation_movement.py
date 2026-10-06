"""Movement-progress report lines: settled units, overrun marches and forces that never held."""

from harness.simulation_movement import movement_section
from harness.verify import JsonObject
from test_simulation_reports import telemetry


def movement_force(**fields: object) -> JsonObject:
    return dict(
        dict(
            team=0,
            marched=True,
            held_after_march=True,
            settled=0,
            overruns=0,
            alive=True,
        ),
        **fields,
    )


def test_movement_section_counts_settled_overrun_and_never_held_forces() -> None:
    _, stuck = telemetry()
    stuck["duration"] = 120
    stuck["movement"] = dict(
        forces={
            "a": movement_force(
                settled=3,
                overruns=1,
                spread_samples=10,
                spread_sum=1500,
                spread_over=2,
                spread_max=400,
            ),
            "b": movement_force(
                held_after_march=False,
                spread_samples=10,
                spread_sum=500,
                spread_over=0,
                spread_max=90,
            ),
            # A force destroyed on the march is not stuck, and one that never marched never held on purpose.
            "c": movement_force(held_after_march=False, alive=False),
            "d": movement_force(marched=False, held_after_march=False),
        }
    )
    _, old = telemetry()
    lines: list[str] = []
    rows = movement_section({("map", "baseline2", 1): [stuck, old]}, lines)
    assert rows == [
        dict(
            matches=2,
            recorded=1,
            minutes=2,
            settled=3,
            settled_per_minute=1.5,
            overruns=1,
            marched=3,
            spread_samples=20,
            spread_over=2,
            spread_mean=100,
            spread_max=400,
            never_holding=1,
            map="map",
            variant="baseline2",
            dilation=1,
        )
    ]
    assert "| map | baseline2 | 1\u00d7 | 1/2 | 3 | 1.50 | 1 | 3 | 1 |" in lines
    assert "| map | baseline2 | 1\u00d7 | 20 | 100 | 400 | 2 |" in lines


def test_movement_section_reports_nothing_for_reports_without_metrics() -> None:
    _, old = telemetry()
    lines: list[str] = []
    rows = movement_section({("map", "baseline2", 1): [old]}, lines)
    assert rows[0]["settled_per_minute"] is None
    assert rows[0]["spread_mean"] is None
    assert "| map | baseline2 | 1\u00d7 | 0 | n/a | 0 | 0 |" in lines
    assert "| map | baseline2 | 1\u00d7 | 0/1 | 0 | n/a | 0 | 0 | 0 |" in lines
