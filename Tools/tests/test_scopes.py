"""Anchored glob routing must work for absent and deleted paths."""

from pathlib import Path

import pytest
from x.scopes import ScopeMap, load


@pytest.fixture
def mapping(repo: Path) -> ScopeMap:
    (repo / "Tools/x/scopes.toml").write_text(
        '[scopes.rules]\nkind = "automation"\nfilter = "CoopRTS.Rules"\nmap = "default"\n'
        '[scopes.tools]\nkind = "pytest"\npaths = ["Tools/tests"]\n'
        '[scopes.lint]\nkind = "lint"\n'
        "[paths]\n"
        '"Source/**/*.h" = ["rules"]\n'
        '"Tools/**" = ["tools"]\n'
        '"*.md" = ["lint"]\n'
        '"x" = ["tools"]\n'
        '"Source/Rules/*.h" = ["rules", "tools"]\n'
    )
    return load(repo)


@pytest.mark.parametrize(
    "path,expected",
    [
        ("Source/deleted.h", ["rules"]),
        ("Source/Rules/Policy.h", ["rules", "tools"]),
        ("Source/Rules/nested/Policy.h", ["rules"]),
        ("Source/a/b/c/Policy.h", ["rules"]),
        ("Source/Policy.cpp", []),
        ("Tools/deleted.py", ["tools"]),
        ("Tools/a/b/test.py", ["tools"]),
        ("README.md", ["lint"]),
        ("Docs/README.md", []),
        ("x", ["tools"]),
        ("nested/x", []),
    ],
)
def test_glob_boundaries(mapping: ScopeMap, path: str, expected: list[str]) -> None:
    assert mapping.scopes_for([Path(path)]) == expected


def test_sorted_unique_scope_union(mapping: ScopeMap) -> None:
    paths = (Path(path) for path in ["x", "Source/Rules/a.h", "README.md", "x"])
    assert mapping.scopes_for(paths) == ["lint", "rules", "tools"]


def test_unmapped_preserves_deleted_paths(mapping: ScopeMap, repo: Path) -> None:
    missing = Path("Source/deleted.cpp")
    outside = repo.parent / "unrelated.h"
    assert mapping.unmapped(
        [missing, Path("Source/deleted.h"), missing, outside]
    ) == sorted([missing, outside])
    assert mapping.scopes_for([repo / "Source/deleted.h"]) == ["rules"]
