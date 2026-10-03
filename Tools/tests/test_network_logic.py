"""Probe snapshots, partial replication and fresh-world invariants."""

from copy import deepcopy
from pathlib import Path

from harness.network import (
    army,
    force_counts_match,
    reachable_regions,
    select_order_region,
    wallet,
)
from harness.network_outcomes import reset_matches
from harness.network_probe import read_response
from harness.network_session import Session
from harness.verify import JsonObject
import pytest


def force_snapshot() -> JsonObject:
    return {
        "buildings": [
            {
                "index": 8,
                "forceID": 4,
                "configured": True,
                "travelling": 1,
                "joined": 1,
                "capacity": 2,
                "recipe": 2,
            }
        ],
        "armies": [
            {
                "owner": 3,
                "army": 4,
                "producer": 8,
                "units": [
                    {
                        "health": 10,
                        "reinforcing": False,
                        "slot": 0,
                        "owner": 3,
                        "role": 2,
                        "producer": 8,
                    },
                    {
                        "health": 10,
                        "reinforcing": True,
                        "slot": 1,
                        "owner": 3,
                        "role": 2,
                        "producer": 8,
                    },
                ],
            }
        ],
    }


def test_force_counts_ignore_dead_slots_but_reject_overcapacity() -> None:
    state = force_snapshot()
    dead = dict(state["armies"][0]["units"][0], health=0, slot=-1, role=255)
    state["armies"][0]["units"].append(dead)
    assert force_counts_match(state, 3, 8)
    dead["health"] = 1
    assert not force_counts_match(state, 3, 8)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("configured", False),
        ("forceID", 99),
        ("travelling", 0),
        ("joined", 2),
        ("capacity", 1),
        ("recipe", 1),
    ],
)
def test_force_rejects_inconsistent_producer(field: str, value: int | bool) -> None:
    state = force_snapshot()
    state["buildings"][0][field] = value
    assert not force_counts_match(state, 3, 8)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("slot", 0),
        ("slot", -1),
        ("slot", 2),
        ("owner", 4),
        ("role", 1),
        ("producer", 9),
        ("reinforcing", False),
    ],
)
def test_force_rejects_invalid_members(field: str, value: int | bool) -> None:
    state = force_snapshot()
    state["armies"][0]["units"][1][field] = value
    assert not force_counts_match(state, 3, 8)


def test_partial_or_duplicate_army_replication_is_not_convergence() -> None:
    state = force_snapshot()
    group = state["armies"].pop()
    assert not force_counts_match(state, 3, 8)
    state["armies"].append(group)
    assert force_counts_match(state, 3, 8)
    state["armies"].append(deepcopy(group))
    assert not force_counts_match(state, 3, 8)
    with pytest.raises(AssertionError, match="expected one army 3/4, got 2"):
        army(state, 3, 4)


def test_wallet_requires_a_unique_commander() -> None:
    state: JsonObject = {"players": [{"index": 1, "wallet": 600}]}
    assert wallet(state, 1)["wallet"] == 600
    with pytest.raises(AssertionError, match="expected one wallet for 0, got 0"):
        wallet(state, 0)
    state["players"].append({"index": 1, "wallet": 900})
    with pytest.raises(AssertionError, match="expected one wallet for 1, got 2"):
        wallet(state, 1)


def region_snapshot() -> JsonObject:
    return {
        "buildings": [{"index": 8, "team": 0, "position": [0, 0, 999]}],
        "regions": [
            {"index": 0, "homeTeam": 0, "anchor": [0, 0], "neighbours": [20, 99, 10]},
            {"index": 10, "homeTeam": -1, "anchor": [1500, 0], "neighbours": [0, 40]},
            {"index": 20, "homeTeam": -1, "anchor": [2000, 0], "neighbours": [0]},
            {"index": 30, "homeTeam": -1, "anchor": [5000, 0], "neighbours": []},
            {"index": 40, "homeTeam": 5, "anchor": [5000, 0], "neighbours": [10]},
        ],
    }


def test_region_traversal_is_deterministic_with_cycles_and_missing_edges() -> None:
    state = region_snapshot()
    assert [r["index"] for r in reachable_regions(state, 0)] == [0, 10, 20, 40]
    with pytest.raises(AssertionError, match="region graph is missing source 99"):
        reachable_regions(state, 99)


def test_order_selection_rejects_boundary_home_excluded_and_unreachable_regions() -> (
    None
):
    state = region_snapshot()
    assert select_order_region(state, 8)["index"] == 20
    assert select_order_region(state, 8, min_distance=1499)["index"] == 10
    with pytest.raises(AssertionError, match="no reachable, non-main region"):
        select_order_region(state, 8, exclude=(20,))


def test_reply_reader_waits_for_complete_current_request(tmp_path: Path) -> None:
    path = tmp_path / "reply.json"
    assert read_response(path, 2) is None
    path.write_text('{"id":')
    assert read_response(path, 2) is None
    path.write_text('{"id":1,"state":{"ready":true}}')
    assert read_response(path, 2) is None
    path.write_text('{"id":2,"state":{"ready":false}}')
    assert read_response(path, 2) == {"id": 2, "state": {"ready": False}}


def reset_snapshot() -> tuple[JsonObject, Session, dict[str, JsonObject]]:
    session = Session(["host", "c1"], {"host": 0, "c1": 1}, "c1", 1, [10000, 10000])
    old: dict[str, JsonObject] = {
        "host": {"generation": 9, "sites": [{}], "deposits": [{}, {}]},
        "c1": {"generation": 9},
    }
    state: JsonObject = {
        "generation": 10,
        "localIndex": 1,
        "result": 0,
        "enemyHQ": 900,
        "friendlyHQ": 900,
        "assigningOrder": False,
        "buildingSelected": False,
        "armies": [{"team": 5}],
        "buildings": [{"team": 5}],
        "players": [
            {"index": 0, "doctrine": 0, "wallet": 600},
            {"index": 1, "doctrine": 0, "wallet": 600},
        ],
        "sites": [{"owner": -1, "progress": 0}],
        "deposits": [
            {"occupied": False, "rich": True, "remaining": 3000},
            {"occupied": False, "rich": False, "remaining": 2400},
        ],
    }
    return state, session, old


def test_fresh_reset_allows_enemy_fixture_and_baseline_wallet() -> None:
    state, session, old = reset_snapshot()
    assert reset_matches("c1", state, session, old)
    state["players"][1]["wallet"] = 601
    assert reset_matches("c1", state, session, old)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("generation", 9),
        ("localIndex", 0),
        ("result", 1),
        ("enemyHQ", 899),
        ("friendlyHQ", 899),
        ("assigningOrder", True),
        ("buildingSelected", True),
    ],
)
def test_reset_rejects_stale_world_identity_and_result(field: str, value: int) -> None:
    state, session, old = reset_snapshot()
    state[field] = value
    assert not reset_matches("c1", state, session, old)


@pytest.mark.parametrize(
    ("collection", "field", "value"),
    [
        ("armies", "team", 0),
        ("buildings", "team", 0),
        ("players", "wallet", 599),
        ("players", "doctrine", 1),
        ("sites", "owner", 0),
        ("sites", "progress", 0.01),
        ("deposits", "occupied", True),
        ("deposits", "remaining", 2999),
    ],
)
def test_reset_rejects_residual_match_state(
    collection: str,
    field: str,
    value: int | float | bool,
) -> None:
    state, session, old = reset_snapshot()
    state[collection][0][field] = value
    assert not reset_matches("c1", state, session, old)


@pytest.mark.parametrize("collection", ["players", "sites", "deposits"])
def test_reset_requires_complete_replicated_collections(collection: str) -> None:
    state, session, old = reset_snapshot()
    state[collection].pop()
    assert not reset_matches("c1", state, session, old)


def test_reset_requires_restored_normal_deposit_reserve() -> None:
    state, session, old = reset_snapshot()
    state["deposits"][1]["remaining"] = 2399
    assert not reset_matches("c1", state, session, old)
