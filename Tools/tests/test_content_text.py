"""Text content sources: one definition per value, deterministic generated outputs."""

from collections.abc import Callable
import json
from pathlib import Path
import shutil
from typing import Any

import ContentText
import GenerateGameplayConstants
from harness.simulation_planning import DEFAULT_ECONOMY
import pytest


@pytest.fixture
def content(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    shutil.copytree(ContentText.CONTENT, tmp_path / "Content")
    monkeypatch.setattr(ContentText, "CONTENT", tmp_path / "Content")
    return tmp_path / "Content"


Edit = Callable[[Any], object]


def rewrite(path: Path, edit: Edit) -> None:
    data = json.loads(path.read_text())
    edit(data)
    path.write_text(json.dumps(data))


def test_header_is_regenerated_identically(tmp_path: Path) -> None:
    first, second = tmp_path / "first.h", tmp_path / "second.h"
    GenerateGameplayConstants.write(first)
    GenerateGameplayConstants.write(second)
    assert first.read_bytes() == second.read_bytes()
    assert first.read_bytes() == GenerateGameplayConstants.OUTPUT.read_bytes()


def test_header_types_follow_json_numbers() -> None:
    declaration = GenerateGameplayConstants.declaration
    assert declaration("start_gold", 600) == "constexpr int32 StartGold = 600;"
    assert (
        declaration("capture_radius", 430.0) == "constexpr float CaptureRadius = 430.f;"
    )
    assert declaration("splash", 2.5) == "constexpr float Splash = 2.5f;"


def test_economy_defaults_follow_shared_constants() -> None:
    shared = ContentText.constants()
    economy = dict(DEFAULT_ECONOMY)
    assert economy == {
        "human_baseline": shared["human_baseline_income"],
        "jev_baseline": shared["jev_baseline_income"],
        "normal_rate": shared["normal_deposit_rate"],
        "rich_rate": shared["rich_deposit_rate"],
        "normal_amount": shared["normal_deposit_amount"],
        "rich_amount": shared["rich_deposit_amount"],
    }


def test_map_constants_come_from_building_and_constants_sources(content: Path) -> None:
    rewrite(
        content / "constants.json",
        lambda data: data.update(
            capture_radius=500.0, placement_z_tolerance=90.0, placement_box_half_z=40.0
        ),
    )
    rewrite(
        content / "buildings.json", lambda data: data[0].update(footprint_radius=130.0)
    )
    values: dict[str, Any] = {}
    ContentText.add_shared_constants(values)
    assert values["capture_radius"] == 500.0
    assert values["placement_z_tolerance"] == 90.0
    assert values["placement_overlap_box"] == {"centre_z_offset": 65.0, "half_z": 40.0}
    assert values["footprint_radius"] == {
        "barracks": 130.0,
        "outpost": 95.0,
        "workshop": 145.0,
    }


def test_map_must_not_redefine_a_shared_value() -> None:
    with pytest.raises(ValueError, match="capture_radius"):
        ContentText.add_shared_constants({"capture_radius": 430.0})


@pytest.mark.parametrize(
    ("name", "edit", "message"),
    [
        ("buildings.json", lambda data: data[0].pop("max_health"), "missing"),
        ("buildings.json", lambda data: data[1].update(typo=1), "unknown"),
        ("buildings.json", lambda data: data.reverse(), "catalogue order"),
        ("units.json", lambda data: data[2].pop("range"), "missing"),
    ],
)
def test_definitions_reject_incomplete_or_reordered_sources(
    content: Path, name: str, edit: Edit, message: str
) -> None:
    rewrite(content / name, edit)
    loader = (
        ContentText.building_definitions
        if name == "buildings.json"
        else ContentText.unit_definitions
    )
    with pytest.raises(ValueError, match=message):
        loader()


def test_constants_reject_non_numbers(content: Path) -> None:
    rewrite(
        content / "constants.json", lambda data: data.update(human_baseline_income="2")
    )
    with pytest.raises(ValueError, match="human_baseline_income"):
        ContentText.constants()


def by_id() -> dict[str, dict[str, Any]]:
    return {unit["id"]: unit for unit in ContentText.unit_definitions()}


def test_branches_carry_the_documented_one_b_effects() -> None:
    units = by_id()
    assert units["warden"]["max_health"] == round(
        units["frontline"]["max_health"] * 1.3
    )
    assert units["marksman"]["range"] == pytest.approx(units["ranged"]["range"] * 1.2)
    assert units["bulwark"]["max_shield"] == round(units["lancer"]["max_shield"] * 1.5)
    assert units["bulwark"]["move_speed"] == pytest.approx(
        units["lancer"]["move_speed"] * 0.9
    )
    assert units["jammer"]["pulse_building_stun_seconds"] == 5.0
    assert units["demolisher"]["structure_damage_multiplier"] == 1.5
    assert all(
        unit["structure_damage_multiplier"] == 1.0
        for unit in units.values()
        if unit["id"] != "demolisher"
    )
    # Each branch changes exactly its documented stats and nothing else about its base.
    changes = {
        "warden": ("frontline", {"max_health"}),
        "marksman": ("ranged", {"range"}),
        "bulwark": ("lancer", {"max_shield", "move_speed"}),
        "jammer": ("scrambler", {"pulse_building_stun_seconds"}),
        "demolisher": ("siege", {"structure_damage_multiplier"}),
    }
    for branch, (base, stats) in changes.items():
        assert units[branch]["branch_of"] == base
        assert units[branch]["branch_summary"]
        identity = {
            "asset_name",
            "id",
            "display_name",
            "accent",
            "branch_of",
            "branch_summary",
        }
        differing = {
            field
            for field in ContentText.UNIT_FIELDS
            if field not in identity and units[branch][field] != units[base][field]
        }
        assert differing == stats, branch


@pytest.mark.parametrize(
    ("edit", "message"),
    [
        (lambda data: data[5].update(unit_cost=99), "keeps its base"),
        (lambda data: data[5].update(capacity=2), "keeps its base"),
        (lambda data: data[5].update(attack_damage=99), "keeps its base"),
        (lambda data: data[6].update(interval=2.0), "keeps its base"),
        (lambda data: data[8].update(pulse_radius=1.0), "keeps its base"),
        (lambda data: data[5].update(branch_summary=""), "needs a summary"),
        (lambda data: data[5].update(branch_of="nobody"), "must name a base unit"),
        (lambda data: data[5].update(branch_of="marksman"), "must name a base unit"),
        (lambda data: data[0].update(branch_summary="+1"), "only a branch"),
        (lambda data: data[6].update(branch_of="frontline"), "one branch"),
    ],
)
def test_branch_rows_must_stay_derived_from_their_base(
    content: Path, edit: Edit, message: str
) -> None:
    rewrite(content / "units.json", edit)
    with pytest.raises(ValueError, match=message):
        ContentText.unit_definitions()
