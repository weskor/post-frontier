"""Guard loss must be a new event for the exact site before any HQ damage."""

from copy import deepcopy

from harness.network_guard_nodes import guard_loss_confirmed
from harness.verify import JsonObject
import pytest


@pytest.fixture
def guard_loss() -> tuple[JsonObject, JsonObject]:
    node = {"id": "EnemyNodeA", "pos": [5000, 8000], "region": 12}
    state = {
        "enemyHQ": 900,
        "objectiveEvents": [
            {
                "id": "enemy_node_lost",
                "sequence": 7,
                "position": [5000, 8000, 150],
                "affectedTeam": 5,
                "damageTier": 1,
            }
        ],
    }
    return node, state


def test_guard_loss_requires_distinct_one_remaining_and_none_remaining_transitions(
    guard_loss: tuple[JsonObject, JsonObject],
) -> None:
    node, state = guard_loss
    assert guard_loss_confirmed(state, node, 1, 6)
    assert not guard_loss_confirmed(state, node, 0, 6)
    last = deepcopy(state)
    last["objectiveEvents"][0]["damageTier"] = 0
    assert guard_loss_confirmed(last, node, 0, 6)
    assert not guard_loss_confirmed(last, node, 1, 6)


@pytest.mark.parametrize(
    ("key", "value"),
    [
        ("id", "region_captured"),
        ("sequence", 6),
        ("position", [9000, 4800, 150]),
        ("position", [5000.001, 8000, 150]),
        ("affectedTeam", 0),
    ],
)
def test_unrelated_stale_or_wrong_site_event_cannot_unlock_hq_acceptance(
    guard_loss: tuple[JsonObject, JsonObject], key: str, value: object
) -> None:
    node, state = guard_loss
    state["objectiveEvents"][0][key] = value
    assert not guard_loss_confirmed(state, node, 1, 6)


def test_missing_loss_or_premature_hq_damage_is_not_guard_completion(
    guard_loss: tuple[JsonObject, JsonObject],
) -> None:
    node, state = guard_loss
    state["enemyHQ"] = 899
    assert not guard_loss_confirmed(state, node, 1, 6)
    state["enemyHQ"] = 900
    state["objectiveEvents"] = []
    assert not guard_loss_confirmed(state, node, 1, 6)
