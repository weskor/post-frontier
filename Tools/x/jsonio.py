"""Atomic JSON replacement for evidence and freshness records."""

import json
import os
from pathlib import Path
import tempfile
from typing import Any


def save(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", dir=path.parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def load(path: Path) -> dict[str, Any]:
    with path.open() as stream:
        value: object = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value
