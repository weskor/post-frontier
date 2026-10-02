"""Repository-relative glob routing, including paths deleted from disk."""

import argparse
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from fnmatch import fnmatchcase
from pathlib import Path
import re
import tomllib
from typing import Any, Literal


@dataclass(frozen=True)
class Scope:
    kind: Literal["automation", "pytest", "script", "lint"]
    filter: str = ""
    map: str = ""
    paths: tuple[str, ...] = ()
    commands: tuple[tuple[str, ...], ...] = ()


def map_package(value: str) -> str:
    if not re.fullmatch(r"/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+", value):
        raise argparse.ArgumentTypeError(
            "Use a /Game/... package path without an extension or URL options"
        )
    return value


def matches(parts: tuple[str, ...], pattern: tuple[str, ...]) -> bool:
    if not pattern:
        return not parts
    if pattern[0] == "**":
        return matches(parts, pattern[1:]) or bool(
            parts and matches(parts[1:], pattern)
        )
    return bool(
        parts and fnmatchcase(parts[0], pattern[0]) and matches(parts[1:], pattern[1:])
    )


@dataclass(frozen=True)
class ScopeMap:
    repo: Path
    definitions: Mapping[str, Scope]
    entries: tuple[tuple[str, tuple[str, ...]], ...]

    def _scopes(self, path: Path) -> set[str]:
        if path.is_absolute():
            try:
                path = path.relative_to(self.repo)
            except ValueError:
                return set()
        return {
            scope
            for pattern, scopes in self.entries
            if matches(path.parts, Path(pattern).parts)
            for scope in scopes
        }

    def scopes_for(self, paths: Iterable[Path]) -> list[str]:
        return sorted({scope for path in paths for scope in self._scopes(path)})

    def unmapped(self, paths: Iterable[Path]) -> list[Path]:
        return sorted({path for path in paths if not self._scopes(path)})

    def names(self) -> list[str]:
        return sorted(self.definitions)


def _scope(name: str, spec: Mapping[str, Any]) -> Scope:
    kind = spec["kind"]
    if kind not in ("automation", "pytest", "script", "lint"):
        raise ValueError(f"scope {name}: unsupported kind {kind}")
    if kind == "automation":
        test_filter = spec.get("filter")
        if not isinstance(test_filter, str) or not test_filter.strip():
            raise ValueError(f"scope {name}: automation requires a non-empty filter")
        map_path = spec.get("map")
        try:
            if not isinstance(map_path, str):
                raise argparse.ArgumentTypeError("Map must be a string")
            if map_path != "default":
                map_package(map_path)
        except argparse.ArgumentTypeError as error:
            raise ValueError(
                f"scope {name}: automation requires default or a /Game/... map"
            ) from error
    if kind == "script":
        commands = spec.get("commands")
        if (
            not isinstance(commands, list)
            or not commands
            or any(
                not isinstance(command, list)
                or len(command) < 2
                or any(not isinstance(arg, str) or not arg.strip() for arg in command)
                for command in commands
            )
        ):
            raise ValueError(
                f"scope {name}: script requires non-empty validator commands"
            )
    if kind == "pytest":
        paths = spec.get("paths")
        if (
            not isinstance(paths, list)
            or not paths
            or any(not isinstance(path, str) or not path.strip() for path in paths)
        ):
            raise ValueError(f"scope {name}: pytest requires non-empty paths")
    return Scope(
        kind=kind,
        filter=spec.get("filter", ""),
        map=spec.get("map", ""),
        paths=tuple(spec.get("paths", [])),
        commands=tuple(tuple(command) for command in spec.get("commands", [])),
    )


def load(repo: Path) -> ScopeMap:
    with (repo / "Tools/x/scopes.toml").open("rb") as source:
        data = tomllib.load(source)
    definitions = {name: _scope(name, spec) for name, spec in data["scopes"].items()}
    entries = tuple(
        (pattern, tuple(scopes)) for pattern, scopes in data["paths"].items()
    )
    for pattern, scopes in entries:
        if not scopes or any(scope not in definitions for scope in scopes):
            raise ValueError(f"path {pattern}: empty or unknown scopes {scopes}")
    return ScopeMap(repo.absolute(), definitions, entries)
