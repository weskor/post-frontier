"""Order-cost report lines: queries per order and per minute, and reports without the counter."""

from harness.simulation_pathcost import path_cost_section
from test_simulation_reports import telemetry


def test_path_cost_section_totals_orders_and_minutes_over_recorded_matches() -> None:
    _, first = telemetry()
    first["duration"] = 120
    first["path_cost"] = dict(
        path_queries=300, straight_moves=60, orders=100, order_path_queries=250, order_straight_moves=60
    )
    _, second = telemetry()
    second["duration"] = 60
    second["path_cost"] = dict(
        path_queries=50, straight_moves=40, orders=50, order_path_queries=50, order_straight_moves=40
    )
    _, old = telemetry()
    lines: list[str] = []
    rows = path_cost_section({("map", "baseline2", 1): [first, second, old]}, lines)
    assert rows[0]["recorded"] == 2
    assert rows[0]["orders"] == 150
    assert rows[0]["queries_per_order"] == 2.0
    assert rows[0]["straight_per_order"] == 100 / 150
    assert rows[0]["order_queries_per_minute"] == 100.0
    assert rows[0]["queries_per_minute"] == 350 / 3
    assert "| map | baseline2 | 1\u00d7 | 2/3 | 150 | 2.00 | 0.67 | 100.00 | 116.67 |" in lines


def test_path_cost_section_reports_nothing_for_reports_without_the_counter() -> None:
    _, old = telemetry()
    lines: list[str] = []
    rows = path_cost_section({("map", "baseline2", 1): [old]}, lines)
    assert rows[0]["queries_per_order"] is None
    assert "| map | baseline2 | 1\u00d7 | 0/1 | 0 | n/a | n/a | n/a | n/a |" in lines


def test_path_cost_section_has_no_per_order_figure_without_orders() -> None:
    _, quiet = telemetry()
    quiet["path_cost"] = dict(
        path_queries=0, straight_moves=0, orders=0, order_path_queries=0, order_straight_moves=0
    )
    lines: list[str] = []
    rows = path_cost_section({("map", "baseline2", 1): [quiet]}, lines)
    assert rows[0]["queries_per_order"] is None
    assert rows[0]["queries_per_minute"] == 0
