"""Text sources for gameplay content, read with no Unreal dependency.

Build/Content/units.json and buildings.json hold every tuned definition field;
constants.json holds the gameplay constants that C++ and the map tools share.
GenerateMatchContent.py writes the definitions into the data assets,
GenerateGameplayConstants.py writes the C++ header, and the map tools read the
same files through add_shared_constants.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, cast

CONTENT = Path(__file__).resolve().parent / "Content"

UNIT_FIELDS = (
    "asset_name",
    "id",
    "display_name",
    "role",
    "armor_class",
    "damage_type",
    "move_speed",
    "max_health",
    "attack_damage",
    "range",
    "interval",
    "unit_cost",
    "unit_duration",
    "capacity",
    "configuration_cost",
    "max_shield",
    "pulse_interval",
    "pulse_radius",
    "pulse_building_stun_seconds",
    "branch_of",
    "branch_summary",
    "accent",
)
# A tier-2 branch is derived from its base (forces.md): the price, the force size and the damage identity stay
# the base's, so only combat stats may differ.
BRANCH_SHARED = (
    "role",
    "armor_class",
    "damage_type",
    "unit_cost",
    "unit_duration",
    "capacity",
    "configuration_cost",
)
BUILDING_FIELDS = (
    "asset_name",
    "id",
    "display_name",
    "build_cost",
    "build_duration",
    "max_health",
    "footprint_radius",
    "produces_forces",
    "requires_deposit",
    "offers_research",
    "accent",
)
# Catalogue order is a replicated contract (see MatchContent.h).
BUILDING_ORDER = ("barracks", "extractor", "workshop")
UNIT_ORDER = (
    "frontline",
    "ranged",
    "siege",
    "lancer",
    "scrambler",
    "warden",
    "marksman",
    "bulwark",
    "jammer",
)


def load_json(name: str) -> object:
    with (CONTENT / name).open(encoding="utf-8") as source:
        return json.load(source)


def validated(
    kind: str, name: str, fields: tuple[str, ...], order: tuple[str, ...]
) -> list[dict[str, Any]]:
    definitions = cast(list[dict[str, Any]], load_json(name))
    for definition in definitions:
        missing = [field for field in fields if field not in definition]
        unknown = [field for field in definition if field not in fields]
        if missing or unknown:
            raise ValueError(
                f"{kind} {definition.get('id')!r}: missing {missing}, unknown {unknown}"
            )
    if tuple(definition["id"] for definition in definitions) != order:
        raise ValueError(f"{name} must list {order} in catalogue order")
    return definitions


def unit_definitions() -> list[dict[str, Any]]:
    definitions = validated("unit", "units.json", UNIT_FIELDS, UNIT_ORDER)
    by_id = {definition["id"]: definition for definition in definitions}
    branched = [d["branch_of"] for d in definitions if d["branch_of"]]
    if len(branched) != len(set(branched)):
        raise ValueError("each unit offers one branch in step 1b")
    for definition in definitions:
        base = by_id.get(definition["branch_of"])
        if not definition["branch_of"]:
            if definition["branch_summary"]:
                raise ValueError(
                    f"unit {definition['id']!r}: only a branch has a summary"
                )
            continue
        if base is None or base["branch_of"]:
            raise ValueError(
                f"unit {definition['id']!r}: branch_of must name a base unit"
            )
        if not definition["branch_summary"]:
            raise ValueError(f"unit {definition['id']!r}: a branch needs a summary")
        changed = [f for f in BRANCH_SHARED if definition[f] != base[f]]
        if changed:
            raise ValueError(
                f"unit {definition['id']!r}: a branch keeps its base's {changed}"
            )
    return definitions


def building_definitions() -> list[dict[str, Any]]:
    return validated("building", "buildings.json", BUILDING_FIELDS, BUILDING_ORDER)


def constants() -> dict[str, int | float]:
    values = cast(dict[str, int | float], load_json("constants.json"))
    for name, value in values.items():
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"constants.json {name!r} must be a number")
    return values


def shared_map_constants() -> dict[str, Any]:
    """Values the map tools read from the shared sources, keyed as they read them."""
    shared = constants()
    return {
        "capture_radius": shared["capture_radius"],
        "hq_exclusion_radius": shared["hostile_hq_clearance"],
        "placement_z_tolerance": shared["placement_z_tolerance"],
        "placement_overlap_box": {
            "centre_z_offset": shared["placement_box_centre_z"],
            "half_z": shared["placement_box_half_z"],
        },
        "footprint_radius": {
            definition["asset_name"].lower(): definition["footprint_radius"]
            for definition in building_definitions()
        },
    }


def add_shared_constants(map_constants: dict[str, Any]) -> None:
    """Fill a map's constants block with the shared values; the map file must not carry them."""
    values = shared_map_constants()
    duplicated = [key for key in values if key in map_constants]
    if duplicated:
        raise ValueError(f"map constants duplicate shared values: {duplicated}")
    map_constants.update(values)
