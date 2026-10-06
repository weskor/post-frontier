"""Extractor acceptance preserves authority precision and replicated wire precision."""

from copy import deepcopy

from harness.network_economy import extractor_ready
from harness.verify import JsonObject
import pytest


@pytest.fixture
def retained_extractor() -> tuple[JsonObject, JsonObject]:
    # Relevant values from economy run 20261006-202459-verify-7002, deposit 8.
    site = {
        "index": 8,
        "position": [-7508.772, 2210.526, 5],
        "rate": 6,
        "occupied": True,
        "complete": True,
        "extractor": 1,
        "owner": 1,
        "team": 0,
    }
    state = {
        "netMode": 2,
        "deposits": [deepcopy(site)],
        "buildings": [
            {"index": 1, "deposit": 8, "position": [-7508.772, 2210.526, 75]}
        ],
        "players": [
            {"index": 0, "wallet": 600, "income": 5},
            {"index": 1, "wallet": 0, "income": 5},
        ],
    }
    return site, state


def test_retained_fractional_host_and_quantized_client_pass(
    retained_extractor: tuple[JsonObject, JsonObject],
) -> None:
    site, host = retained_extractor
    assert extractor_ready(host, 1, site)
    client = deepcopy(host)
    client["netMode"] = 3
    client["buildings"][0]["position"] = [-7509, 2211, 75]
    assert extractor_ready(client, 1, site)


@pytest.mark.parametrize(
    ("source", "wire"),
    [
        (-2.5, -3),
        (-1.5, -2),
        (-0.5, -1),
        (-0.499, 0),
        (0.0, 0),
        (0.499, 0),
        (0.5, 1),
        (1.5, 2),
        (2.5, 3),
    ],
)
def test_client_wire_rounding_including_signed_half_ties(
    retained_extractor: tuple[JsonObject, JsonObject], source: float, wire: int
) -> None:
    site, state = retained_extractor
    site["position"][:2] = [source, -source]
    state["netMode"] = 3
    state["buildings"][0]["position"][:2] = [wire, -wire]
    assert extractor_ready(state, 1, site)
    state["buildings"][0]["position"][0] = wire + 1
    assert not extractor_ready(state, 1, site)


@pytest.mark.parametrize("net_mode", [2, 3])
@pytest.mark.parametrize("axis", [0, 1])
@pytest.mark.parametrize("offset", [-1.0, -0.001, 0.001, 1.0])
def test_misplaced_building_fails_without_coordinate_tolerance(
    retained_extractor: tuple[JsonObject, JsonObject],
    net_mode: int,
    axis: int,
    offset: float,
) -> None:
    site, state = retained_extractor
    state["netMode"] = net_mode
    if net_mode == 3:
        state["buildings"][0]["position"][:2] = [-7509, 2211]
    state["buildings"][0]["position"][axis] += offset
    assert not extractor_ready(state, 1, site)


def test_authority_rejects_wire_precision_and_client_rejects_unquantized_position(
    retained_extractor: tuple[JsonObject, JsonObject],
) -> None:
    site, state = retained_extractor
    state["buildings"][0]["position"][:2] = [-7509, 2211]
    assert not extractor_ready(state, 1, site)
    state["netMode"] = 3
    state["buildings"][0]["position"][:2] = site["position"][:2]
    assert not extractor_ready(state, 1, site)


@pytest.mark.parametrize("net_mode", [2, 3])
@pytest.mark.parametrize(
    ("collection", "index", "key", "value"),
    [
        ("deposits", 0, "occupied", False),
        ("deposits", 0, "complete", False),
        ("deposits", 0, "owner", 0),
        ("deposits", 0, "team", 5),
        ("deposits", 0, "extractor", 9),
        ("buildings", 0, "deposit", 9),
        ("players", 1, "wallet", 1),
        ("players", 0, "income", 4),
        ("players", 1, "income", 4),
    ],
)
def test_exact_coordinates_do_not_mask_other_extractor_failures(
    retained_extractor: tuple[JsonObject, JsonObject],
    net_mode: int,
    collection: str,
    index: int,
    key: str,
    value: int | bool,
) -> None:
    site, state = retained_extractor
    state["netMode"] = net_mode
    if net_mode == 3:
        state["buildings"][0]["position"][:2] = [-7509, 2211]
    state[collection][index][key] = value
    assert not extractor_ready(state, 1, site)
