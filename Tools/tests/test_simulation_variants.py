"""Economy variants: separate human and JEV baselines, end to end to the game's flags."""

import argparse

from harness.simulation_execution import ECONOMY_FLAGS
from harness.simulation_planning import DEFAULT_ECONOMY, parse_variant
import pytest


def test_custom_variant_overrides_only_the_human_baseline() -> None:
    name, values = parse_variant("h1:human_baseline=1")
    assert name == "h1"
    assert values["human_baseline"] == 1
    assert values["jev_baseline"] == DEFAULT_ECONOMY["jev_baseline"]


def test_jev_baseline_is_independent_of_the_human_preset() -> None:
    for preset in ("baseline2", "baseline3", "baseline4"):
        _, values = parse_variant(preset)
        assert values["human_baseline"] == int(preset[-1])
        assert values["jev_baseline"] == DEFAULT_ECONOMY["jev_baseline"]
    _, values = parse_variant("j:jev_baseline=3")
    assert (values["human_baseline"], values["jev_baseline"]) == (2, 3)


def test_single_baseline_key_is_gone() -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        parse_variant("old:baseline=3")


def test_every_economy_key_has_a_game_flag() -> None:
    assert set(ECONOMY_FLAGS) == set(DEFAULT_ECONOMY)
    assert ECONOMY_FLAGS["human_baseline"] == "SimHumanBaseline"
    assert ECONOMY_FLAGS["jev_baseline"] == "SimJevBaseline"


def test_runtime_overrides_stay_within_bounds() -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        parse_variant("big:jev_baseline=10001")
    with pytest.raises(argparse.ArgumentTypeError):
        parse_variant("twice:human_baseline=1,human_baseline=2")


def test_network_pool_credit_conserves_exactly() -> None:
    from harness.network_economy import pool_credit

    for recipients in range(1, 6):
        whole, carry = 0, 0
        for _ in range(30):
            credit, carry = pool_credit(4 * recipients + 8, recipients, carry)
            whole += credit
        assert whole * 60 + carry == (4 * recipients + 8) * 30 * 60 // recipients
