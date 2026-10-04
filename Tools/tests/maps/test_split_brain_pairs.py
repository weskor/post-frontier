"""Split-Brain Cut target pairs: authored map data audited against the territory graph, without Unreal."""

from copy import deepcopy

from DrawAvailabilityZoneV2 import (
    MapData,
    hop_distances,
    human_supply_necks,
    split_brain_pair_errors,
)
import pytest


def authored(v2_map: MapData) -> MapData:
    """The shared fixture copies shallowly; these tests edit pairs and neighbours."""
    result = deepcopy(v2_map)
    assert split_brain_pair_errors(result) == []
    return result


def test_necks_are_the_regions_human_supply_must_cross(v2_map: MapData) -> None:
    # Human Near, West Cut, Uplink and Power Yard lie on every shortest path to something behind them; Ridge, Spur and
    # the centre are nearer JEV or have an alternative of equal length.
    assert human_supply_necks(v2_map) == [1, 2, 3, 8]


def test_every_authored_pair_is_two_non_adjacent_necks(v2_map: MapData) -> None:
    pairs = v2_map["split_brain_pairs"]
    neighbours = {r["index"]: set(r["neighbours"]) for r in v2_map["regions"]}
    necks = set(human_supply_necks(v2_map))
    assert len(pairs) >= 2
    for first, second in pairs:
        assert {first, second} <= necks
        assert second not in neighbours[first]


@pytest.mark.parametrize(
    ("pairs", "message"),
    [
        ([], "at least one authored neck pair"),
        ([[2, 2]], "two distinct regions"),
        ([[2]], "two distinct regions"),
        ([[2, 99]], "two distinct regions"),
        ([[1, 2]], "is adjacent"),
        ([[2, 3]], "is adjacent"),
        ([[2, 4]], "region 4 is not a human supply neck"),
        ([[0, 8]], "region 0 is not a human supply neck"),
        ([[14, 8]], "region 14 is not a human supply neck"),
        ([[2, 8], [8, 2]], "is listed twice"),
    ],
)
def test_pair_rejections(v2_map: MapData, pairs: list[list[int]], message: str) -> None:
    data = authored(v2_map)
    data["split_brain_pairs"] = pairs
    assert any(message in error for error in split_brain_pair_errors(data))


def test_a_region_stops_being_a_neck_when_an_alternative_path_matches(
    v2_map: MapData,
) -> None:
    # Joining Human Main to Uplink directly leaves West Cut off every shortest path to Uplink and Ridge.
    data = authored(v2_map)
    by_index = {r["index"]: r for r in data["regions"]}
    by_index[0]["neighbours"].append(3)
    by_index[3]["neighbours"].append(0)
    assert 2 not in human_supply_necks(data)
    data["split_brain_pairs"] = [[2, 8]]
    assert any("region 2 is not" in error for error in split_brain_pair_errors(data))


def test_hop_distances_never_enter_the_banned_region() -> None:
    graph = {0: {1}, 1: {0, 2}, 2: {1}}
    assert hop_distances(graph, 0) == {0: 0, 1: 1, 2: 2}
    assert hop_distances(graph, 0, banned=1) == {0: 0}
