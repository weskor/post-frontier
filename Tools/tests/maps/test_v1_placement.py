"""Isolate the 2D placement gates on a real authored build bay."""

from copy import copy

from DrawMapLayout import Analysis
import pytest

from .test_v1_rules import clone_grid


@pytest.mark.parametrize(
    "rule",
    [
        "arena-x",
        "arena-y",
        "hostile-exclusion",
        "territory",
        "home-hq",
        "hostile-hq",
        "building-gap",
        "grid-range",
        "bare-ramp",
        "sample-range",
        "sample-blocked",
        "sample-ramp",
        "sample-level",
        "edge-clearance",
    ],
)
def test_placement_gates(analysis: Analysis, rule: str) -> None:
    x, y = analysis.data["build_pockets"][0]["bays"][0]["pos"]
    radius = analysis.const["footprint_radius"]["barracks"]
    home = analysis.hq["HQ_H"]["pos"][:]
    hostile = analysis.hq["HQ_J"]["pos"][:]
    zone = home[:]
    zr = analysis.const["hq_territory_radius"]
    placed: list[tuple[float, float, float]] = []
    assert analysis.placement_ok(x, y, radius, home, hostile, zone, zr)
    if rule.startswith("arena-"):
        axis = 0 if rule == "arena-x" else 1
        analysis.data["arena"]["half_extent"][axis] = (
            abs((x, y)[axis]) + analysis.data["arena"]["placement_margin"] - 1
        )
    elif rule == "hostile-exclusion":
        hostile = [x + radius + analysis.const["hq_exclusion_radius"], y]
    elif rule == "territory":
        zr = radius - 1
        zone = [x, y]
    elif rule == "home-hq":
        home = [x + radius + 210, y]
    elif rule == "hostile-hq":
        analysis.const["hq_exclusion_radius"] = 0
        hostile = [x + radius + 210, y]
    elif rule == "building-gap":
        placed = [(x + 2 * radius + 55, y, radius)]
    else:
        grid = analysis.grid
        ci, cj = grid.cell(x, y)
        si, sj = grid.cell(x + radius + 65, y)
        if rule.endswith("range"):
            analysis.grid = copy(grid)
            analysis.grid.nx = ci if rule == "grid-range" else si
        elif rule == "bare-ramp":
            clone_grid(analysis, "ramp", ci, cj, 0)
        elif rule == "sample-blocked":
            clone_grid(analysis, "walk", si, sj, False)
        elif rule == "sample-ramp":
            clone_grid(analysis, "ramp", si, sj, 0)
        elif rule == "sample-level":
            clone_grid(analysis, "cover", si, sj, 1)
        else:
            analysis.edge = [row[:] for row in analysis.edge]
            analysis.edge[si][sj] = 59
    assert not analysis.placement_ok(x, y, radius, home, hostile, zone, zr, placed)
