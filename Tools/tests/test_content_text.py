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
        "baseline": shared["baseline_income"],
        "normal_rate": shared["normal_deposit_rate"],
        "rich_rate": shared["rich_deposit_rate"],
        "normal_amount": shared["normal_deposit_amount"],
        "rich_amount": shared["rich_deposit_amount"],
    }


def test_map_constants_come_from_building_and_constants_sources(content: Path) -> None:
    rewrite(content / "constants.json", lambda data: data.update(capture_radius=500.0))
    rewrite(
        content / "buildings.json", lambda data: data[0].update(footprint_radius=130.0)
    )
    values: dict[str, Any] = {}
    ContentText.add_shared_constants(values)
    assert values["capture_radius"] == 500.0
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
    rewrite(content / "constants.json", lambda data: data.update(baseline_income="2"))
    with pytest.raises(ValueError, match="baseline_income"):
        ContentText.constants()
