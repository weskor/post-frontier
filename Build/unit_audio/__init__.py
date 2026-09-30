"""Unit sound recipes. Every module here other than core defines SOURCES and UNIT (see core.py).

A recipe module is named after its unit key in lower case: Human_Ranged -> human_ranged.py.
"""
import importlib
import pkgutil
from types import ModuleType


def recipes(keys: list[str] | None = None) -> list[ModuleType]:
    """Recipe modules for the given unit keys (e.g. "Human_Ranged"), or all of them sorted by name.

    Only the requested modules are imported, so one unit's broken recipe cannot stop work on another."""
    available = sorted(info.name for info in pkgutil.iter_modules(__path__) if info.name != "core")
    names = available if keys is None else [key.lower() for key in keys]
    unknown = [name for name in names if name not in available]
    if unknown:
        raise SystemExit(f"no recipe module for {unknown}; available: {available}")
    return [importlib.import_module(f"{__name__}.{name}") for name in names]
