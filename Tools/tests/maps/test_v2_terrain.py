"""Habitable Zone v2 terrain: T3 constraints, ramps, closed borders, routes and derive round trip."""

from collections.abc import Callable
from copy import deepcopy
import json
from pathlib import Path

import DrawAvailabilityZoneV2
from DrawAvailabilityZoneV2 import SKETCH, MapData, defend_post_errors, derive, topology
import pytest
from TerrainPlan import Terrain
from TerrainWalk import Walker, route_errors, route_report, terrain_errors, walk_errors

from .conftest import change


@pytest.fixture
def terrain_map(v2_map: MapData) -> MapData:
    v2_map["terrain"] = deepcopy(v2_map["terrain"])
    for region in v2_map["regions"]:
        region["neighbours"] = region["neighbours"][:]
    return v2_map


@pytest.fixture(scope="session")
def real_walker(v2_data: MapData) -> Walker:
    return Walker(Terrain(v2_data))


def test_real_map_meets_t3(v2_data: MapData, real_walker: Walker) -> None:
    terrain = Terrain(v2_data)
    assert terrain_errors(terrain) == []
    assert walk_errors(terrain, real_walker) == []
    assert route_errors(terrain, route_report(terrain, real_walker)) == []
    assert topology(v2_data)[2] == []
    assert defend_post_errors(v2_data) == []


def test_routes_cost_different_things(v2_data: MapData, real_walker: Walker) -> None:
    report = {r["name"]: r for r in route_report(Terrain(v2_data), real_walker)}
    north, centre, south = (
        report["North (Ridge)"],
        report["Center (Hub)"],
        report["South (Spur)"],
    )
    assert centre["length"] < south["length"] < north["length"]
    assert south["damage"] > 0 == north["damage"] == centre["damage"]
    assert north["traits"] != centre["traits"]


def test_heights(v2_data: MapData) -> None:
    terrain = Terrain(v2_data)
    assert terrain.ground_z(*v2_data["headquarters"][0]["pos"]) == 0
    ridge = v2_data["regions"][4]["anchor"]
    assert ridge is not None
    assert terrain.ground_z(*ridge) == 300
    ramp = terrain.ramps[0]
    assert terrain.ground_z(*ramp["centre"]) == pytest.approx(150)
    assert terrain.ground_z(ramp["centre"][0] + 1e5, 0) == 0


def mutate(path: tuple[str | int, ...], value: object) -> Callable[[MapData], None]:
    return lambda data: change(data, path, value)


def drop_ramp(data: MapData) -> None:
    data["terrain"]["plateaus"][0]["ramps"].pop()


def move_ramp(data: MapData) -> None:
    data["terrain"]["plateaus"][0]["ramps"][0]["centre"][0] -= 800


def prop_on_plateau(data: MapData) -> None:
    anchor = data["regions"][4]["anchor"]
    assert anchor is not None
    data["terrain"]["props"][0]["pos"] = anchor[:]
    data["terrain"]["props"][0]["region"] = 4


def unreduce_neighbours(data: MapData) -> None:
    data["regions"][3]["neighbours"] = [2, 4, 5, 6]


def swap_route(data: MapData) -> None:
    data["terrain"]["routes"][0]["regions"] = [0, 2, 3, 5, 13, 14]


def shorten_routes(data: MapData) -> None:
    del data["terrain"]["routes"][1:]


@pytest.mark.parametrize(
    ("damage", "message"),
    [
        (mutate(("regions", 0, "trait"), "cover"), "Main region 0 must have no trait"),
        (
            mutate(("regions", 3, "trait"), "hazard"),
            "Reward region 3 must not be hazard",
        ),
        (mutate(("regions", 11, "trait"), None), "Need >= 3 cover regions"),
        (mutate(("regions", 10, "trait"), "open"), "Need >= 1 hazard regions"),
        (mutate(("regions", 9, "trait"), "lava"), "invalid trait"),
        (drop_ramp, "neighbours must be polygon adjacency minus closed borders"),
        (move_ramp, "top edge is not on two plateau cells"),
        (prop_on_plateau, "must stand in a cover region"),
        (
            unreduce_neighbours,
            "neighbours must be polygon adjacency minus closed borders",
        ),
        (mutate(("terrain", "props"), []), "Cover region 2 needs >= 6 props"),
        (
            mutate(("regions", 2, "defend_posts", 2), [-1900, -5100]),
            "Region 2 post 2: formation slot",
        ),
        (
            mutate(("terrain", "open_exceptions"), {}),
            "Open region 8 contains ['plateau_edge', 'ramp'] but declares []",
        ),
    ],
)
def test_terrain_rules_reject(
    terrain_map: MapData, damage: Callable[[MapData], None], message: str
) -> None:
    assert terrain_errors(Terrain(terrain_map)) == []
    damage(terrain_map)
    assert any(message in e for e in terrain_errors(Terrain(terrain_map)))


def test_wall_must_cover_its_whole_shared_edge(terrain_map: MapData) -> None:
    terrain = Terrain(terrain_map)
    assert not any("Wall" in e for e in terrain_errors(terrain))
    samples = terrain.shared_edge_samples(2, 6)
    assert samples
    for point in samples:
        terrain.walls -= terrain.edge_cells(point)
    assert any(
        "Wall 2-6 leaves the shared edge uncovered" in e
        for e in terrain_errors(terrain)
    )


@pytest.mark.parametrize("cell", [(10, 5), (12, 5), (13, 6)])
def test_open_side_wall_must_cover_its_edge(
    terrain_map: MapData, cell: tuple[int, int]
) -> None:
    terrain = Terrain(terrain_map)
    assert not any("Wall 5-12" in e for e in terrain_errors(terrain))
    terrain.walls.discard(cell)
    assert any(
        "Wall 5-12 leaves the shared edge uncovered" in e
        for e in terrain_errors(terrain)
    )


def test_routes_must_be_neighbour_paths_and_distinct(
    terrain_map: MapData, real_walker: Walker
) -> None:
    swap_route(terrain_map)
    terrain = Terrain(terrain_map)
    errors = route_errors(terrain, route_report(terrain, real_walker))
    assert any("not a neighbour path" in e for e in errors)
    shorten_routes(terrain_map)
    terrain = Terrain(terrain_map)
    assert any(
        "2-3 authored routes" in e
        for e in route_errors(terrain, route_report(terrain, real_walker))
    )


def reduce_neighbours(data: MapData) -> None:
    near = Terrain(data).neighbours()
    for region in data["regions"]:
        region["neighbours"] = near[region["index"]]


def test_rock_wall_seals_its_border(terrain_map: MapData) -> None:
    terrain_map["terrain"]["closed_borders"].append([1, 9])
    reduce_neighbours(terrain_map)
    terrain = Terrain(terrain_map)
    assert 9 not in terrain.neighbours()[1]
    assert walk_errors(terrain, Walker(terrain)) == []


def test_plateau_without_ramps_cannot_be_entered(terrain_map: MapData) -> None:
    terrain_map["terrain"]["plateaus"][1]["ramps"] = []
    reduce_neighbours(terrain_map)
    terrain = Terrain(terrain_map)
    assert terrain.neighbours()[7] == []
    assert any(
        "anchor 7 is not reachable" in e for e in walk_errors(terrain, Walker(terrain))
    )


def test_stretch_rule_on_raised_ground(terrain_map: MapData) -> None:
    assert defend_post_errors(terrain_map) == []
    ramp = terrain_map["terrain"]["plateaus"][0]["ramps"][0]
    terrain_map["regions"][ramp["to"]]["defend_posts"][0] = ramp["centre"][:]
    assert any("off walkable ground" in e for e in defend_post_errors(terrain_map))
    ridge = terrain_map["regions"][4]
    ridge["defend_posts"] = ridge["defend_posts"][:2]
    assert any(
        "North Ridge: buildable spot" in e for e in defend_post_errors(terrain_map)
    )


def test_derive_keeps_authored_terrain(
    v2_data: MapData, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    copy = tmp_path / "AvailabilityZoneV2.json"
    copy.write_text(json.dumps(v2_data))
    monkeypatch.setattr(DrawAvailabilityZoneV2, "DATA", copy)
    assert SKETCH.exists()
    derive()
    derived = json.loads(copy.read_text())
    assert derived == v2_data
    edited = deepcopy(v2_data)
    edited["regions"][4]["trait"] = "cover"
    copy.write_text(json.dumps(edited))
    derive()
    assert json.loads(copy.read_text())["regions"][4]["trait"] == "cover"
