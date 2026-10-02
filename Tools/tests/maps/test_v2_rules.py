"""Cheap authoring checks are pure; exhaustive raster/rendering stays in maps."""

from copy import deepcopy
import math

from DrawAvailabilityZoneV2 import (
    WALK_CELL,
    MapData,
    clearance,
    inside,
    quarter_cells,
    region_errors,
    site_errors,
    symmetry_errors,
    tiling_mismatches,
    topology,
    walking,
)
import pytest

from .conftest import change


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        (
            ("regions", 1, "index"),
            lambda d: d["regions"][1]["index"] + 1,
            "Region indices must be 0..14",
        ),
        (("deposits",), [], "Exactly 16 sketch diamonds expected"),
        (("regions", 1, "role"), "tactical", "Region role counts differ"),
        (
            ("regions", 1, "home_team"),
            lambda d: d["regions"][1]["home_team"] + 1,
            "home team mismatch",
        ),
        (("regions", 1, "anchor"), None, "missing anchor"),
        (
            ("regions", 1, "anchor"),
            lambda d: [extent + 1 for extent in d["arena"]["half_extent"]],
            "invalid/outside anchor",
        ),
        (
            ("regions", 0, "anchor"),
            lambda d: d["headquarters"][0]["pos"][:],
            "invalid/outside anchor",
        ),
    ],
)
def test_region_contract(
    v2_map: MapData, path: tuple[str | int, ...], value: object, message: str
) -> None:
    assert region_errors(v2_map) == []
    if callable(value):
        value = value(v2_map)
    change(v2_map, path, value)
    errors = region_errors(v2_map)
    assert len(errors) == 1 and message in errors[0]


@pytest.mark.parametrize("damage", ["winding", "bounds"])
def test_region_geometry(v2_map: MapData, damage: str) -> None:
    assert region_errors(v2_map) == []
    poly = v2_map["regions"][0]["poly"]
    if damage == "winding":
        poly.reverse()
        expected = "0 is not CCW"
    else:
        poly[0][1] = -v2_map["arena"]["half_extent"][1] - 1
        expected = "0 extends beyond arena"
    assert region_errors(v2_map) == [expected]


def border_deposit(data: MapData) -> list[float]:
    """An inside pivot whose extractor crosses an authored main border."""
    poly = data["regions"][0]["poly"]
    home = data["headquarters"][0]["pos"]
    for a, b in zip(poly, poly[1:] + poly[:1], strict=True):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length == 0:
            continue
        point = [
            (a[0] + b[0]) / 2 - 50 * dy / length,
            (a[1] + b[1]) / 2 + 50 * dx / length,
        ]
        if not inside(point, poly) or math.dist(point, home) < 1100:
            continue
        if any(
            inside(point, rock["poly"]) or clearance(point, rock["poly"]) < 90
            for rock in data["blockers"]
        ):
            continue
        if any(
            not inside((point[0] + x, point[1] + y), poly)
            for x in (-95, 95)
            for y in (-95, 95)
        ):
            return point
    raise AssertionError("No isolated extractor-border fixture on the authored main")


@pytest.mark.parametrize(
    "damage",
    [
        "hq",
        "deposit-country",
        "extractor-border",
        "kind",
        "hq-exclusion",
        "anchor-exclusion",
    ],
)
def test_sites(v2_map: MapData, damage: str) -> None:
    assert site_errors(v2_map) == []
    if damage == "hq":
        anchor = v2_map["regions"][1]["anchor"]
        assert anchor is not None
        v2_map["headquarters"][0]["pos"] = anchor[:]
        message = "HQ HQ_H outside main"
    elif damage == "deposit-country":
        deposit = v2_map["deposits"][0]
        deposit["region"] = next(
            r["index"]
            for r in v2_map["regions"]
            if r["index"] != deposit["region"]
            and r["role"] != "reward"
            and not inside(deposit["pos"], r["poly"])
        )
        message = "outside country"
    elif damage == "extractor-border":
        v2_map["deposits"][0]["pos"] = border_deposit(v2_map)
        message = "extractor footprint crosses country border"
    elif damage == "kind":
        v2_map["deposits"][0]["kind"] = "rich"
        message = "deposit kind mismatch"
    elif damage == "hq-exclusion":
        v2_map["deposits"][0]["pos"] = v2_map["headquarters"][0]["pos"][:]
        message = "main deposit too close to HQ exclusion footprint"
    else:
        deposit = next(d for d in v2_map["deposits"] if d["region"] not in (0, 14))
        anchor = v2_map["regions"][deposit["region"]]["anchor"]
        assert anchor is not None
        deposit["pos"] = anchor[:]
        message = "deposit conflicts with capture anchor footprint"
    errors = site_errors(v2_map)
    assert any(message in error for error in errors)
    # A pivot outside necessarily also fails its whole-footprint rule.
    assert all(
        message in error
        or (damage == "deposit-country" and "extractor footprint" in error)
        for error in errors
    )


@pytest.mark.parametrize("site", ["hq", "anchor", "deposit"])
@pytest.mark.parametrize("mode", ["near-edge", "inside"])
def test_rock_clearance(v2_map: MapData, site: str, mode: str) -> None:
    assert site_errors(v2_map) == []
    if site == "hq":
        point = v2_map["headquarters"][0]["pos"]
    elif site == "anchor":
        anchor = v2_map["regions"][1]["anchor"]
        assert anchor is not None
        point = anchor
    else:
        point = v2_map["deposits"][0]["pos"]
    x, y = point
    poly = (
        [[x + 89, y - 1], [x + 91, y - 1], [x + 91, y + 1], [x + 89, y + 1]]
        if mode == "near-edge"
        else [[x - 91, y - 91], [x + 91, y - 91], [x + 91, y + 91], [x - 91, y + 91]]
    )
    if mode == "inside":
        assert inside(point, poly) and clearance(point, poly) > 90
    v2_map["blockers"].append(
        {"id": "fixture", "name": "fixture", "kind": "rock", "poly": poly}
    )
    errors = site_errors(v2_map)
    assert len(errors) == 1 and "within 90 cm of fixture" in errors[0]


@pytest.mark.parametrize(
    "damage", ["unmatched", "direction", "users", "metadata", "incidence", "area"]
)
def test_exact_topology(v2_map: MapData, damage: str) -> None:
    assert topology(v2_map)[2] == []
    if damage == "unmatched":
        v2_map["regions"][0]["poly"][0][0] += 1
        message = "Unmatched interior edge"
    elif damage == "direction":
        v2_map["regions"][0]["poly"].reverse()
        message = "Edge direction not opposed"
    elif damage == "users":
        duplicate = deepcopy(v2_map["regions"][0])
        duplicate["index"] = len(v2_map["regions"])
        v2_map["regions"].append(duplicate)
        message = "Edge has 3 users"
    elif damage in ("metadata", "incidence"):
        neighbours = v2_map["regions"][0]["neighbours"]
        other = neighbours.pop()
        if damage == "metadata":
            v2_map["regions"][other]["neighbours"].remove(0)
        message = (
            "Neighbour metadata mismatch"
            if damage == "metadata"
            else "Neighbour incidence not symmetric"
        )
    else:
        v2_map["arena"]["half_extent"][0] += 1
        message = "Not a perfect arena tiling"
    assert any(message in error for error in topology(v2_map)[2])


@pytest.mark.parametrize("damage", ["gap", "overlap"])
def test_sampled_tiling(v2_map: MapData, damage: str) -> None:
    # Quarter-cell samples around every authored gameplay site. The maps scope
    # separately exhausts all 160000 samples; these keep the rule tests fast.
    sites = (
        [h["pos"] for h in v2_map["headquarters"]]
        + [r["anchor"] for r in v2_map["regions"] if r["anchor"] is not None]
        + [d["pos"] for d in v2_map["deposits"]]
    )
    points = [
        (math.floor(p[0] / 100) * 100 + dx, math.floor(p[1] / 100) * 100 + dy)
        for p in sites
        for dx in (25, 75)
        for dy in (25, 75)
    ]
    assert tiling_mismatches(v2_map["regions"], points) == 0
    point = v2_map["headquarters"][0]["pos"]
    if damage == "gap":
        v2_map["regions"][0]["poly"] = []
    else:
        v2_map["regions"].append(deepcopy(v2_map["regions"][0]))
    assert tiling_mismatches(v2_map["regions"], [point]) == 1


def test_quarter_cell_coverage(v2_data: MapData) -> None:
    hx, hy = v2_data["arena"]["half_extent"]
    points = list(quarter_cells(v2_data))
    expected = 4 * (2 * hx // WALK_CELL) * (2 * hy // WALK_CELL)
    assert len(points) == expected
    assert len(set(points)) == expected
    assert all(
        -hx < x < hx
        and -hy < y < hy
        and x % WALK_CELL in (WALK_CELL / 4, 3 * WALK_CELL / 4)
        and y % WALK_CELL in (WALK_CELL / 4, 3 * WALK_CELL / 4)
        for x, y in points
    )


def test_asymmetric_sketch(v2_map: MapData) -> None:
    assert symmetry_errors(v2_map) == []
    v2_map["headquarters"][1]["pos"] = [-v for v in v2_map["headquarters"][0]["pos"]]
    assert symmetry_errors(v2_map) == [
        "HQ sites unexpectedly mirror despite asymmetric sketch; review symmetry claim"
    ]


@pytest.fixture(scope="session")
def real_walking(v2_data: MapData) -> None:
    walking(v2_data)


@pytest.mark.parametrize("target", ["hq", "anchor"])
def test_walking_connectivity(v2_map: MapData, real_walking: None, target: str) -> None:
    home = v2_map["headquarters"][0]["pos"]
    destination = v2_map["headquarters"][1]["pos"]
    if target == "anchor":
        region = max(
            (r for r in v2_map["regions"] if r["anchor"] is not None),
            key=lambda r: math.dist(home, r["anchor"] or home),
        )
        assert region["anchor"] is not None
        destination = region["anchor"]
        v2_map["headquarters"][1]["pos"] = home[:]
        v2_map["regions"] = [region]
    axis = max((0, 1), key=lambda k: abs(home[k] - destination[k]))
    middle = (home[axis] + destination[axis]) / 2
    hx, hy = v2_map["arena"]["half_extent"]
    barrier = (
        [[middle, -hy], [middle, hy]] if axis == 0 else [[-hx, middle], [hx, middle]]
    )
    v2_map["blockers"] = [
        {
            "id": "wall",
            "name": "wall",
            "kind": "rock",
            # A line obstacle's 100cm clearance seals the full walking grid.
            "poly": barrier,
        }
    ]
    with pytest.raises(ValueError, match="Unreachable site"):
        walking(v2_map)
