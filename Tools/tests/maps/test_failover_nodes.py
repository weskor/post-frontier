"""Authored guard sites and the native/mesh generator contract, without Unreal."""

import ast
from collections.abc import Callable
from pathlib import Path
from typing import Any

import DrawAvailabilityZoneV2 as drawing
from DrawAvailabilityZoneV2 import MapData, failover_node_errors
import pytest
import TerrainPlan
import TerrainWalk

ROOT = Path(__file__).resolve().parents[3]


def functions(path: str, names: set[str], namespace: dict[str, Any]) -> dict[str, Any]:
    tree = ast.parse((ROOT / path).read_text())
    tree.body = [
        n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names
    ]
    exec(compile(tree, path, "exec"), namespace)
    return namespace


def require(value: object, message: str) -> object:
    if not value:
        raise ValueError(message)
    return value


@pytest.mark.parametrize(
    ("damage", "message"),
    [
        ("missing", "exactly two"),
        ("team", "exactly two"),
        ("id", "IDs must be unique"),
        ("region", "two distinct regions"),
        ("not-neighbour", "must neighbour"),
        ("outside", "footprint outside"),
        ("deposit", "deposit/anchor/post"),
        ("anchor", "deposit/anchor/post"),
        ("post", "deposit/anchor/post"),
        ("rock", "blocker/cover"),
        ("prop", "blocker/cover"),
        ("ramp", "within 800 cm of ramp"),
        ("hq", "HQ/other node"),
        ("node", "HQ/other node"),
        ("floor", "flat navigable ground"),
    ],
)
def test_failover_site_rejections(v2_map: MapData, damage: str, message: str) -> None:
    assert failover_node_errors(v2_map) == []
    nodes = v2_map["failover_nodes"]
    node = nodes[0]
    x, y = node["pos"]
    if damage == "missing":
        nodes.pop()
    elif damage == "team":
        node["team"] = 7
    elif damage == "id":
        node["id"] = nodes[1]["id"]
    elif damage == "region":
        node["region"] = nodes[1]["region"]
    elif damage == "not-neighbour":
        node["region"] = 14
    elif damage == "outside":
        node["pos"] = [0, 0]
    elif damage == "deposit":
        v2_map["deposits"][0]["pos"] = [x + 799, y]
    elif damage == "anchor":
        v2_map["regions"][1]["anchor"] = [x + 799, y]
    elif damage == "post":
        v2_map["regions"][1]["defend_posts"][0] = [x + 799, y]
    elif damage == "rock":
        v2_map["blockers"].append(
            {
                "id": "guard-rock",
                "kind": "rock",
                "name": "Guard rock",
                "poly": [
                    [x + 599, y - 100],
                    [x + 700, y - 100],
                    [x + 700, y + 100],
                    [x + 599, y + 100],
                ],
            }
        )
    elif damage == "prop":
        v2_map["terrain"]["props"][0]["pos"] = [x + 200, y]
    elif damage == "ramp":
        v2_map["terrain"]["plateaus"][0]["ramps"][0]["centre"] = [x, y]
    elif damage == "hq":
        v2_map["headquarters"][0]["pos"] = [x + 1999, y]
    elif damage == "node":
        nodes[1]["pos"] = [x + 1999, y]
    else:
        v2_map["regions"][1]["trait"] = "high_ground"
    assert any(message in error for error in failover_node_errors(v2_map))


def test_native_validator_uses_site_audit(v2_map: MapData) -> None:
    namespace = functions(
        "Build/GenerateAvailabilityZoneV2.py",
        {"validate", "inside", "area"},
        {
            "require": require,
            "DrawAvailabilityZoneV2": drawing,
            "TerrainPlan": TerrainPlan,
            "TerrainWalk": TerrainWalk,
        },
    )
    validate: Callable[[MapData], object] = namespace["validate"]
    assert validate(v2_map)
    v2_map["failover_nodes"].pop()
    with pytest.raises(ValueError, match="Invalid failover nodes"):
        validate(v2_map)


def assignment(path: str, name: str) -> object:
    tree = ast.parse((ROOT / path).read_text())
    value = next(
        n.value
        for n in tree.body
        if isinstance(n, ast.Assign)
        and any(isinstance(t, ast.Name) and t.id == name for t in n.targets)
    )
    return ast.literal_eval(value)


def test_mesh_registry_contract() -> None:
    generator = "Build/GenerateBuildingMeshes.py"
    importer = "Build/ImportBuildingMeshes.py"
    kinds = assignment(importer, "KINDS")
    assert isinstance(kinds, tuple) and "FailoverNode" in kinds
    for path, key, expected in (
        (importer, "HALF", 150),
        (importer, "HEIGHT", (300, 350)),
        (generator, "HALF", 1.5),
        (generator, "HEIGHT_RANGE", (3.0, 3.5)),
    ):
        values = assignment(path, key)
        assert isinstance(values, dict) and values["FailoverNode"] == expected
    namespace = functions(generator, {"kind_of", "ground_of"}, {})
    for faction in ("Human", "Machine"):
        obj = type("MeshName", (), {"name": f"SM_{faction}_FailoverNode"})()
        assert namespace["ground_of"](obj) == 0
