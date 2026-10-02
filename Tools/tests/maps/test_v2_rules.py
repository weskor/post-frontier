"""Cheap authoring checks are pure; exhaustive raster/rendering stays in maps."""

from copy import deepcopy
import math

from DrawAvailabilityZoneV2 import (
    MapData,
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
        (("regions", 1, "index"), 2, "Region indices must be 0..14"),
        (("deposits",), [], "Exactly 16 sketch diamonds expected"),
        (("regions", 1, "role"), "tactical", "Region role counts differ"),
        (("regions", 1, "home_team"), 0, "home team mismatch"),
        (("regions", 1, "anchor"), None, "missing anchor"),
        (("regions", 1, "anchor"), [10000, 10000], "invalid/outside anchor"),
        (("regions", 0, "anchor"), [-6807, -6912], "invalid/outside anchor"),
    ],
)
def test_region_contract(
    v2_map: MapData, path: tuple[str | int, ...], value: object, message: str
) -> None:
    assert region_errors(v2_map) == []
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
        poly[0][1] = -10001
        expected = "0 extends beyond arena"
    assert region_errors(v2_map) == [expected]


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
        v2_map["deposits"][0]["region"] = 2
        message = "outside country"
    elif damage == "extractor-border":
        # Main's flat outside edge: pivot inside, only extractor corners outside.
        v2_map["deposits"][0]["pos"] = [-9950, -7000]
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
def test_rock_clearance(v2_map: MapData, site: str) -> None:
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
    v2_map["blockers"].append(
        {
            "id": "fixture",
            "name": "fixture",
            "kind": "rock",
            "poly": [
                [x + 89, y - 1],
                [x + 91, y - 1],
                [x + 91, y + 1],
                [x + 89, y + 1],
            ],
        }
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
        duplicate["index"] = 15
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
    if target == "anchor":
        v2_map["headquarters"][1]["pos"] = v2_map["headquarters"][0]["pos"][:]
        v2_map["regions"] = [
            next(
                r
                for r in v2_map["regions"]
                if r["anchor"] is not None and r["anchor"][0] > 0
            )
        ]
    v2_map["blockers"] = [
        {
            "id": "wall",
            "name": "wall",
            "kind": "rock",
            # A line obstacle's 100cm clearance seals the full walking grid.
            "poly": [[0, -10000], [0, 10000]],
        }
    ]
    with pytest.raises(ValueError, match="Unreachable site"):
        walking(v2_map)
