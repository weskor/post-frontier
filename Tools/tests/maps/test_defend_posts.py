"""Authored post rules on real gameplay regions and damaged map copies."""

from copy import copy, deepcopy
import json
from pathlib import Path

from AvailabilityZoneLayout import Layout, env_sizes, gameplay_regions
import DrawAvailabilityZoneV2 as v2
from DrawAvailabilityZoneV2 import MapData, defend_post_errors
import MatchLayout
import pytest


@pytest.fixture(scope="session")
def dressed_classic(real_layout: Layout) -> Layout:
    layout = copy(real_layout)
    layout.kit_sizes = env_sizes()
    layout.props, layout.rim_props, layout.halls = [], [], []
    layout._dress()
    return layout


def test_real_authored_posts(v2_data: MapData, dressed_classic: Layout) -> None:
    assert defend_post_errors(v2_data) == []
    assert dressed_classic.defend_post_errors() == []


@pytest.mark.parametrize("count", [0, 1, 4])
def test_v2_post_count(v2_map: MapData, count: int) -> None:
    region = v2_map["regions"][0]
    region["defend_posts"] = [region["defend_posts"][0][:] for _ in range(count)]
    assert any(
        "requires 2-3 defend posts" in error for error in defend_post_errors(v2_map)
    )


@pytest.mark.parametrize("coordinate", [float("nan"), float("inf"), -float("inf")])
def test_v2_nonfinite_post(v2_map: MapData, coordinate: float) -> None:
    v2_map["regions"][0]["defend_posts"][0][0] = coordinate
    assert any(
        "finite world XY coordinates" in error for error in defend_post_errors(v2_map)
    )


def test_v2_post_outside_country(v2_map: MapData) -> None:
    v2_map["regions"][0]["defend_posts"][0] = v2_map["regions"][14]["defend_posts"][0][
        :
    ]
    assert any(
        "defend post 0 outside region" in error for error in defend_post_errors(v2_map)
    )


def test_v2_post_on_rock(v2_map: MapData) -> None:
    x, y = v2_map["regions"][0]["defend_posts"][0]
    v2_map["blockers"].append(
        {
            "id": "damaged_rock",
            "kind": "rock",
            "name": "Rock covering an authored defend post",
            "poly": [
                [x - 100, y - 100],
                [x + 100, y - 100],
                [x + 100, y + 100],
                [x - 100, y + 100],
            ],
        }
    )
    assert any(
        "defend post 0 off walkable ground" in error
        for error in defend_post_errors(v2_map)
    )


def test_v2_stretched_region(v2_map: MapData) -> None:
    region = v2_map["regions"][0]
    xs, ys = zip(*region["poly"], strict=False)
    x0, x1, y0, y1 = min(xs), max(xs) + 12000, min(ys), max(ys)
    region["poly"] = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]
    v2_map["regions"] = [region]
    v2_map["arena"]["half_extent"] = [30000, 30000]
    errors = defend_post_errors(v2_map)
    assert len(errors) == 1 and "maximum 3500 cm" in errors[0]


@pytest.mark.parametrize(
    "damage", ["count", "outside", "wall", "rim", "dressing", "coverage"]
)
def test_classic_gameplay_posts(dressed_classic: Layout, damage: str) -> None:
    layout = copy(dressed_classic)
    regions = gameplay_regions(layout.data)
    region = regions[0]
    if damage == "count":
        region["defend_posts"] = region["defend_posts"][:1]
        message = "requires 2-3 defend posts"
    elif damage == "outside":
        region["defend_posts"][0] = regions[1]["defend_posts"][0][:]
        message = "outside region"
    elif damage == "wall":
        wall = layout.walls[0]
        region["defend_posts"][0] = list(wall["center"])
        message = "off walkable ground"
    elif damage == "rim":
        region["defend_posts"][0] = [-9800, -9800]
        message = "off walkable ground"
    elif damage == "dressing":
        layout.halls = deepcopy(layout.halls)
        x, y = region["defend_posts"][0]
        layout.halls.append(
            {"label": "DamagedHall", "center": (x, y), "size": (400, 400), "doors": {}}
        )
        message = "off walkable ground"
    else:
        # The post coordinates still lie inside the gameplay polygon, but can no
        # longer defend buildable ground toward the region's opposite edge.
        region["defend_posts"] = [[-8400, -6400], [-8300, -6400]]
        message = "maximum 3500 cm"
    assert any(message in error for error in layout.defend_post_errors(regions))


def test_exact_stretch_boundary() -> None:
    region: MatchLayout.GameplayRegion = {
        "index": 0,
        "name": "Flat strip",
        "home_team": -1,
        "seed": (0, 0),
        "poly": [(-7100, -100), (7100, -100), (7100, 100), (-7100, 100)],
        "neighbours": [],
        "defend_posts": [[-3500, 0], [3500, 0]],
    }

    def clear_ground(point: MatchLayout.Point, clearance: float) -> bool:
        return MatchLayout.clear_of_blockers(point, [], clearance)

    assert MatchLayout.defend_post_errors([region], (8000, 1000), clear_ground) == []
    region["poly"] = [(-7150, -100), (7150, -100), (7150, 100), (-7150, 100)]
    errors = MatchLayout.defend_post_errors([region], (8000, 1000), clear_ground)
    assert len(errors) == 1 and "3550.0 cm" in errors[0]


def test_rederive_preserves_hand_edited_posts(
    v2_data: MapData, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    data = deepcopy(v2_data)
    data["regions"][0]["defend_posts"][0][0] += 50
    authored = [deepcopy(region["defend_posts"]) for region in data["regions"]]
    path = tmp_path / "AvailabilityZoneV2.json"
    path.write_text(json.dumps(data))
    monkeypatch.setattr(v2, "DATA", path)
    v2.derive()
    derived = json.loads(path.read_text())
    assert [region["defend_posts"] for region in derived["regions"]] == authored
