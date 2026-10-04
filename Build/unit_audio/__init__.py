"""Unit sound recipes. Every module here other than core defines SOURCES and UNIT (see core.py).

A recipe module is named after its unit key in lower case: Human_Ranged -> human_ranged.py.
Lancer and Scrambler modules are discovered automatically, including their faction effect cues.
"""

from __future__ import annotations

import importlib
import pkgutil
from typing import TYPE_CHECKING, Protocol, cast

if TYPE_CHECKING:
    from .core import Unit


class RecipeModule(Protocol):
    SOURCES: list[tuple[str, str]]
    UNIT: Unit


def recipes(keys: list[str] | None = None) -> list[RecipeModule]:
    """Recipe modules for the given unit keys (e.g. "Human_Ranged"), or all of them sorted by name.

    Only the requested modules are imported, so one unit's broken recipe cannot stop work on another."""
    available = sorted(
        info.name for info in pkgutil.iter_modules(__path__) if info.name != "core"
    )
    names = available if keys is None else [key.lower() for key in keys]
    unknown = [name for name in names if name not in available]
    if unknown:
        raise SystemExit(f"no recipe module for {unknown}; available: {available}")
    return [
        cast(RecipeModule, importlib.import_module(f"{__name__}.{name}"))
        for name in names
    ]
