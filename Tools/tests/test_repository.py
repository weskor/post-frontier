"""Consumer-visible settings, hashing and changed-path boundaries."""

from dataclasses import FrozenInstanceError, replace
import os
from pathlib import Path

from conftest import git
import pytest
from x import freshness, gitinfo
from x.context import Context
from x.settings import load


def test_settings_and_cook_maps(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("UE_ROOT", "/wrong/environment/override")
    settings = load(repo)
    assert settings.engine_root == Path("~/.local/opt/unreal-engine/5.8.3").expanduser()
    assert settings.freshness["editor"] == (
        "Source/**",
        "CoopRTS.uproject",
        "Config/**",
    )
    frozen_field = "headless_pool_size"
    with pytest.raises(FrozenInstanceError):
        setattr(settings, frozen_field, 2)
    (repo / "Config/DefaultGame.ini").write_text(
        ';+MapsToCook=(FilePath="/Ignored")\n+MapsToCook=(FilePath="/Game/Maps/One")\n'
        '+MapsToCook=(FilePath="/Game/Maps/Two")\n'
    )
    assert settings.maps() == ["/Game/Maps/One", "/Game/Maps/Two"]


def test_hash_content_not_timestamp_and_ignore_boundaries(repo: Path) -> None:
    path = repo / "Source/rules.cpp"
    original = freshness.current_hash(repo, "editor")
    os.utime(path, (1, 1))
    assert freshness.current_hash(repo, "editor") == original
    freshness.stamp(repo, "editor", "test-run")
    assert freshness.is_fresh(repo, "editor")
    path.write_text("changed same path\n")
    assert not freshness.is_fresh(repo, "editor")
    path.write_text("initial\n")
    assert freshness.is_fresh(repo, "editor")
    (repo / "Source/untracked.h").write_text("new")
    assert freshness.current_hash(repo, "editor") != original
    (repo / "Source/untracked.h").unlink()
    (repo / "ignored").mkdir()
    (repo / "ignored/file").write_text("ignored")
    (repo / "Content").mkdir()
    before_package = freshness.current_hash(repo, "package")
    (repo / "Content/map.umap").write_bytes(b"map")
    assert freshness.current_hash(repo, "editor") == original
    assert freshness.current_hash(repo, "package") != before_package
    path.unlink()
    assert freshness.current_hash(repo, "editor") != original


def test_changed_files_include_all_states_and_deletions(repo: Path) -> None:
    for name in ("deleted", "staged", "unstaged", "rename-old"):
        (repo / name).write_text("base")
    git(repo, "add", ".")
    git(repo, "commit", "-m", "base paths")
    base = gitinfo.commit(repo)
    git(repo, "switch", "-c", "task/example")
    (repo / "committed").write_text("committed")
    (repo / "deleted").unlink()
    git(repo, "add", ".")
    git(repo, "commit", "-m", "branch work")
    (repo / "staged").write_text("staged")
    git(repo, "add", "staged")
    (repo / "unstaged").write_text("unstaged")
    (repo / "untracked\nname").write_text("untracked")
    (repo / "ignored").write_text("ignored")
    git(repo, "mv", "rename-old", "rename-new")
    assert gitinfo.changed_files(repo) == sorted(
        map(
            Path,
            (
                "committed",
                "deleted",
                "staged",
                "unstaged",
                "untracked\nname",
                "rename-old",
                "rename-new",
            ),
        )
    )
    assert gitinfo.branch(repo) == "task/example"
    assert gitinfo.merge_base(repo, "main") == base
    assert gitinfo.is_dirty(repo)
    main = repo.parent / "main-tree"
    git(repo, "worktree", "add", str(main), "main")
    assert gitinfo.main_worktree(repo) == main


def test_context_freshness_uses_passed_settings(repo: Path) -> None:
    settings = replace(load(repo), freshness={"editor": ("Content/**",)})
    ctx = Context(repo, settings)
    (repo / "Tools/x/settings.toml").unlink()
    (repo / "Content").mkdir()
    content = repo / "Content/map.umap"
    content.write_bytes(b"initial")
    original = ctx.freshness.current_hash("editor")
    ctx.freshness.stamp("editor", "settings-run")
    (repo / "Source/rules.cpp").write_text("excluded source change")
    assert ctx.freshness.is_fresh("editor")
    content.write_bytes(b"changed")
    assert ctx.freshness.current_hash("editor") != original
    assert not ctx.freshness.is_fresh("editor")
