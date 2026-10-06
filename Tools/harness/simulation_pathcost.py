"""Order cost evidence: synchronous path queries and raycast moves spent by forces, per order and per minute."""

from __future__ import annotations

from harness.simulation_evidence import Groups
from harness.verify import JsonObject


def match_path_cost(report: JsonObject) -> JsonObject | None:
    """One match's path cost; None for a report that predates the counter."""
    cost = report.get("path_cost")
    if not isinstance(cost, dict):
        return None
    return dict(
        minutes=report["duration"] / 60,
        orders=cost["orders"],
        order_path_queries=cost["order_path_queries"],
        order_straight_moves=cost["order_straight_moves"],
        path_queries=cost["path_queries"],
        straight_moves=cost["straight_moves"],
    )


def _ratio(numerator: float, denominator: float) -> float | None:
    return numerator / denominator if denominator else None


def path_cost_evidence(reports: list[JsonObject]) -> JsonObject:
    """Totals over every match that recorded the counter; per-minute uses their game minutes."""
    rows = [row for row in map(match_path_cost, reports) if row]
    minutes = sum(row["minutes"] for row in rows)
    orders = sum(row["orders"] for row in rows)
    order_queries = sum(row["order_path_queries"] for row in rows)
    queries = sum(row["path_queries"] for row in rows)
    return dict(
        matches=len(reports),
        recorded=len(rows),
        minutes=minutes,
        orders=orders,
        order_path_queries=order_queries,
        order_straight_moves=sum(row["order_straight_moves"] for row in rows),
        path_queries=queries,
        straight_moves=sum(row["straight_moves"] for row in rows),
        queries_per_order=_ratio(order_queries, orders),
        straight_per_order=_ratio(sum(row["order_straight_moves"] for row in rows), orders),
        order_queries_per_minute=_ratio(order_queries, minutes),
        queries_per_minute=_ratio(queries, minutes),
    )


def _cell(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def path_cost_section(groups: Groups, lines: list[str]) -> list[JsonObject]:
    lines += [
        "",
        "## Order cost",
        "",
        "Synchronous path queries solved for forces (orders, re-paths, pursuit, hold, joining recruits) and moves a "
        "navmesh raycast answered instead. Per order: queries and straight moves spent planning one order, rejected "
        "orders included. `MoveToLocation` at a pursuit's end and the engine's own re-paths are not counted, so the "
        "per-minute figure is a lower bound. A match with a rejected order retried many times dominates the totals; "
        "compare per-order figures. No gate.",
        "",
        "| Map | Variant | Dilation | Recorded | Orders | Queries per order | Straight moves per order | Order queries per game min | All queries per game min |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    rows: list[JsonObject] = []
    for (map_name, variant, dilation), reports in sorted(groups.items()):
        evidence = path_cost_evidence(reports)
        lines.append(
            f"| {map_name} | {variant} | {dilation:g}\u00d7 | {evidence['recorded']}/{evidence['matches']} "
            f"| {evidence['orders']} | {_cell(evidence['queries_per_order'])} "
            f"| {_cell(evidence['straight_per_order'])} | {_cell(evidence['order_queries_per_minute'])} "
            f"| {_cell(evidence['queries_per_minute'])} |"
        )
        rows.append(dict(evidence, map=map_name, variant=variant, dilation=dilation))
    lines.append("")
    return rows
