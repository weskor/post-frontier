"""An input edit after a build must never be stamped as built."""

from pathlib import Path

import pytest
from x import freshness
from x.context import Context
from x.settings import load


@pytest.mark.parametrize("bound", [False, True])
def test_precomputed_stamp_does_not_adopt_later_source_edit(
    repo: Path, bound: bool
) -> None:
    ctx = Context(repo, load(repo))
    built_hash = ctx.freshness.current_hash("editor")
    source = repo / "Source/rules.cpp"
    original = source.read_text()
    source.write_text("edited after build comparison\n")
    if bound:
        ctx.freshness.stamp("editor", "built-snapshot", built_hash)
    else:
        freshness.stamp(repo, "editor", "built-snapshot", built_hash)
    assert not ctx.freshness.is_fresh("editor")
    source.write_text(original)
    assert ctx.freshness.is_fresh("editor")
