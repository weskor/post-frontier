"""Freshness stamps track successful, stable builds and surviving artifacts."""

import sys
from dataclasses import replace
from pathlib import Path

import pytest
from x.building import editor_module, ensure_editor
from x.context import Context
from x.freshness import Freshness
from x.runs import Run
from x.settings import load


def context(repo: Path, mode: str = "success") -> Context:
    settings = replace(
        load(repo),
        engine_root=repo.parent / "engine",
        lock_dir=repo.parent / "locks",
        runs_root=repo.parent / "runs",
    )
    build = settings.engine_root / "Engine/Build/BatchFiles/Linux/Build.sh"
    build.parent.mkdir(parents=True, exist_ok=True)
    build.write_text(
        f"#!{sys.executable}\n"
        "from pathlib import Path\n"
        "root = Path.cwd()\n"
        "artifact = root / 'Binaries/Linux/libUnrealEditor-CoopRTS.so'\n"
        "artifact.parent.mkdir(parents=True, exist_ok=True)\n"
        "artifact.write_bytes(b'module')\n"
        + (
            "(root / 'Source/rules.cpp').write_text('changed during build')\n"
            if mode == "change"
            else ""
        )
        + ("raise SystemExit(1)\n" if mode == "fail" else "")
    )
    build.chmod(0o755)
    record = Run(repo, settings.runs_root, "build", ["build"])
    return Context(repo, settings, record, "build")


def test_build_source_change_and_deleted_artifact(repo: Path) -> None:
    ctx = context(repo)
    assert ensure_editor(ctx)
    assert ctx.freshness.is_fresh("editor")
    assert ensure_editor(ctx)
    assert ctx.run is not None
    assert len(ctx.run.record["execs"]) == 1
    (repo / "Source/rules.cpp").write_text("consumer change\n")
    assert not ctx.freshness.is_fresh("editor")
    assert ensure_editor(ctx)
    assert len(ctx.run.record["execs"]) == 2
    editor_module(ctx).unlink()
    assert ensure_editor(ctx)
    assert len(ctx.run.record["execs"]) == 3


@pytest.mark.parametrize("mode", ["fail", "change"])
def test_unsuccessful_or_mutating_build_does_not_stamp(repo: Path, mode: str) -> None:
    ctx = context(repo, mode)
    assert not ensure_editor(ctx)
    assert not ctx.freshness.is_fresh("editor")
    assert not (repo / "Intermediate/x-stamps/editor.json").exists()


def test_edit_between_build_comparison_and_stamp_stays_stale(
    repo: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    ctx = context(repo)
    original_stamp = Freshness.stamp

    def edit_then_stamp(
        self: Freshness, kind: str, run_id: str, input_hash: str | None = None
    ) -> None:
        (repo / "Source/rules.cpp").write_text("edit immediately before stamp\n")
        original_stamp(self, kind, run_id, input_hash)

    monkeypatch.setattr(Freshness, "stamp", edit_then_stamp)
    assert ensure_editor(ctx)
    assert not ctx.freshness.is_fresh("editor")
