"""Simulation evidence identities and atomic JSON persistence."""

from __future__ import annotations

import json
from pathlib import Path

from harness.verify import JsonObject

type GroupKey = tuple[str, str, float]
type Groups = dict[GroupKey, list[JsonObject]]


def save_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def stamp(path: Path) -> JsonObject:
    stat = path.stat()
    return dict(path=str(path), size=stat.st_size, mtime_ns=stat.st_mtime_ns)


def teams(snapshot: JsonObject) -> dict[int, JsonObject]:
    return {team["team"]: team for team in snapshot["teams"]}
