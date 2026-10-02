"""Every authored v1 rejection gets real-data acceptance and one-rule damage."""

from copy import copy, deepcopy
import math
from typing import cast

from DrawMapLayout import Analysis, ProposalsData
import pytest

from .conftest import change


@pytest.mark.parametrize(
    ("method", "path", "value", "message"),
    [
        (
            "check_grid",
            ("regions", 0, "poly", 0, 0),
            lambda a: a.data["regions"][0]["poly"][0][0] + 1,
            "off the kit grid",
        ),
        (
            "check_grid",
            ("ramps", 0, "pieces", 0, "centre", 0),
            lambda a: a.data["ramps"][0]["pieces"][0]["centre"][0] + 1,
            "not centred",
        ),
        ("check_grid", ("ramps", 0, "pieces", 0, "yaw"), 1, "not a multiple of 90"),
        (
            "check_grid",
            ("ramps", 0, "length"),
            lambda a: a.data["ramps"][0]["length"] + 1,
            "does not match the kit ramp",
        ),
        (
            "check_grid",
            ("ramps", 0, "lane_width"),
            lambda a: a.data["ramps"][0]["lane_width"] + 1,
            "does not match the kit ramp",
        ),
        (
            "check_grid",
            ("blockers", 0, "poly", 0, 0),
            lambda a: a.data["blockers"][0]["poly"][0][0] + 1,
            "off the kit grid",
        ),
        (
            "check_grid",
            ("proposals", "vision_points", 0, "pos", 0),
            lambda a: a.data["proposals"]["vision_points"][0]["pos"][0] + 1,
            "not on a cell centre",
        ),
        (
            "check_ramps",
            ("grid", "step"),
            lambda a: a.data["grid"]["step"] + 1,
            "not one 300 cm step",
        ),
        (
            "check_ramps",
            ("ramps", 0, "lane_width"),
            lambda a: 3 * a.const["force_width"] - 1,
            "under three force widths",
        ),
        (
            "check_sectors",
            ("sectors", 0, "level"),
            lambda a: next(
                z["id"]
                for z in a.data["elevation"]
                if z["id"] != a.data["sectors"][0]["level"]
            ),
            "is on",
        ),
        (
            "check_chokes",
            ("chokes", 0, "width"),
            lambda a: a.data["chokes"][0]["width"] + 500,
            "file says",
        ),
        (
            "check_symmetry",
            ("sectors", 0, "pos", 0),
            lambda a: a.data["sectors"][0]["pos"][0] + 1,
            "not rot180 partners",
        ),
        (
            "check_elevation",
            ("elevation", 0, "z_cm"),
            6,
            "rises into the placement overlap box",
        ),
        ("check_elevation", ("elevation", 0, "z_cm"), -91, "more than 95 cm from z 5"),
        ("check_elevation", ("ramps", 0, "rise_z_cm"), 1, "within the character's"),
        (
            "check_bays",
            ("build_pockets", 0, "bays", 0, "pos"),
            lambda a: [
                a.hq["HQ_H"]["pos"][k]
                + a.data["build_pockets"][0]["half_plane"]["keep"][k]
                for k in (0, 1)
            ],
            "fails the placement rules",
        ),
        (
            "check_bays",
            ("build_pockets", 0, "half_plane", "keep"),
            lambda a: [-v for v in a.data["build_pockets"][0]["half_plane"]["keep"]],
            "wrong pocket half",
        ),
        (
            "intruder_reach",
            ("constants", "jev_intruder_radius"),
            100,
            "intruder radius does not reach",
        ),
        (
            "jev_build_check",
            ("constants", "hq_territory_radius"),
            200,
            "cannot place its",
        ),
        (
            "jev_build_check",
            ("constants", "sector_territory_radius"),
            50,
            "cannot place an outpost",
        ),
    ],
)
def test_authored_rules(
    analysis: Analysis,
    method: str,
    path: tuple[str | int, ...],
    value: object,
    message: str,
) -> None:
    check = getattr(analysis, method)
    check()
    assert analysis.errors == []
    if callable(value):
        value = value(analysis)
    change(analysis.data, path, value)
    check()
    assert analysis.errors and all(message in error for error in analysis.errors)


def clone_grid(
    analysis: Analysis, field: str, i: int, j: int, value: int | bool
) -> None:
    grid = copy(analysis.grid)
    rows = [row[:] for row in getattr(grid, field)]
    rows[i][j] = value
    setattr(grid, field, rows)
    analysis.grid = grid


@pytest.mark.parametrize(
    "rule", ["overlap", "ramp-top", "ramp-foot", "capture-ring", "hq-disc", "symmetry"]
)
def test_raster_rules(analysis: Analysis, rule: str) -> None:
    methods = {
        "overlap": "check_levels",
        "ramp-top": "check_ramps",
        "ramp-foot": "check_ramps",
        "capture-ring": "check_sectors",
        "hq-disc": "check_sectors",
        "symmetry": "check_symmetry",
    }
    check = getattr(analysis, methods[rule])
    check()
    assert analysis.errors == []
    if rule == "overlap":
        clone_grid(analysis, "cover", 0, 0, 3)
    elif rule.startswith("ramp-"):
        ramp = analysis.data["ramps"][0]
        sign = -1 if rule == "ramp-top" else 1
        dx, dy = (b - a for a, b in zip(ramp["top"], ramp["foot"], strict=True))
        norm = math.hypot(dx, dy)
        x, y = ramp["pieces"][0]["centre"]
        probe = sign * (ramp["length"] / 2 + 100)
        i, j = analysis.grid.cell(x + dx / norm * probe, y + dy / norm * probe)
        clone_grid(analysis, "walk", i, j, False)
    elif rule == "capture-ring":
        i, j = analysis.grid.cell(*analysis.data["sectors"][0]["pos"])
        clone_grid(analysis, "walk", i + 1, j, False)
    elif rule == "hq-disc":
        i, j = analysis.grid.cell(*analysis.hq["HQ_H"]["pos"])
        clone_grid(analysis, "walk", i, j, False)
    else:
        clone_grid(analysis, "walk", 0, 0, True)
    check()
    messages = {
        "overlap": "different levels overlap",
        "ramp-top": "beyond its top",
        "ramp-foot": "beyond its foot",
        "capture-ring": "capture ring leaves",
        "hq-disc": "territory disc leaves",
        "symmetry": "not rot180 symmetric",
    }
    assert analysis.errors and all(messages[rule] in error for error in analysis.errors)


@pytest.mark.parametrize("kind", ["ramp", "choke"])
def test_wall_off(analysis: Analysis, kind: str) -> None:
    analysis.check_wall_off()
    assert analysis.errors == []
    # Keep just the feature under test, so territory intrusion has one cause.
    if kind == "ramp":
        analysis.data["ramps"] = [analysis.data["ramps"][0]]
        analysis.data["chokes"] = []
        analysis.hq["HQ_H"]["pos"] = list(
            analysis.data["ramps"][0]["pieces"][0]["centre"]
        )
    else:
        analysis.data["ramps"] = []
        analysis.data["chokes"] = [analysis.data["chokes"][0]]
        analysis.hq["HQ_H"]["pos"] = list(analysis.data["chokes"][0]["center"])
    analysis.check_wall_off()
    assert len(analysis.errors) == 1
    assert "lies inside" in analysis.errors[0]


def test_bay_spacing(analysis: Analysis) -> None:
    analysis.check_bays()
    assert analysis.errors == []
    bays = analysis.data["build_pockets"][0]["bays"]
    bays[1]["pos"] = bays[0]["pos"][:]
    analysis.check_bays()
    assert analysis.errors == ["two bays are only 0 cm apart"]


def test_ramp_slope(analysis: Analysis) -> None:
    analysis.check_ramps()
    assert analysis.errors == []
    ramp = analysis.data["ramps"][0]
    dx, dy = (b - a for a, b in zip(ramp["top"], ramp["foot"], strict=True))
    norm = math.hypot(dx, dy)
    shift = ramp["length"] / 4
    ramp["length"] /= 2
    ramp["pieces"][0]["centre"] = [
        ramp["pieces"][0]["centre"][0] - dx / norm * shift,
        ramp["pieces"][0]["centre"][1] - dy / norm * shift,
    ]
    analysis.check_ramps()
    assert analysis.errors == ["ramp_main_H: kit ramp slope limit is 30 degrees"]


def two_route_map(analysis: Analysis) -> Analysis:
    data = deepcopy(analysis.data)
    data["arena"]["half_extent"] = [1000, 1000]
    data["regions"] = [deepcopy(data["regions"][0])]
    data["regions"][0]["poly"] = [
        [-1000, -1000],
        [1000, -1000],
        [1000, 1000],
        [-1000, 1000],
    ]
    data["regions"][0]["level"] = "L0"
    data["blockers"], data["sectors"], data["ramps"] = [], [], []
    data["proposals"] = cast(ProposalsData, {})
    data["headquarters"][0]["pos"] = [-650, -50]
    data["headquarters"][1]["pos"] = [650, 50]
    result = Analysis(data)
    for i in (9, 10):
        for j in range(result.grid.ny):
            result.grid.nav[i][j] = j in (4, 5, 14, 15)
    for rid, cy in (("ramp_door_H", -500), ("ramp_gate_H", 500)):
        ramp = deepcopy(analysis.ramps[rid])
        ramp["strips"] = [
            [[-100, cy - 100], [100, cy - 100], [100, cy + 100], [-100, cy + 100]]
        ]
        result.ramps[rid] = ramp
    result.measure()
    assert result.errors == []
    return result


@pytest.mark.parametrize(
    "damage", ["unreachable", "single-entrance", "route-gap", "unsealed"]
)
def test_attack_routes(analysis: Analysis, damage: str) -> None:
    assert (
        analysis.routes["no_door"] is not None
        and analysis.routes["no_gate"] is not None
    )
    assert analysis.route_gap <= 0.03
    assert analysis.routes["no_gate_no_door"] is None
    analysis = two_route_map(analysis)
    if damage == "unreachable":
        for i, j in analysis.closed_cells(["ramp_door_H", "ramp_gate_H"]):
            analysis.grid.nav[i][j] = False
    elif damage == "single-entrance":
        analysis.ramps["ramp_door_H"]["strips"] += analysis.ramps["ramp_gate_H"][
            "strips"
        ]
    elif damage == "route-gap":
        analysis.hq["HQ_H"]["pos"][1] += 300
    else:
        analysis.ramps["ramp_gate_H"]["strips"] = []
    analysis.measure()
    messages = {
        "unreachable": "cannot reach",
        "single-entrance": "disconnects the bases",
        "route-gap": "attack routes differ",
        "unsealed": "reachable without the gate",
    }
    assert any(messages[damage] in error for error in analysis.errors)
