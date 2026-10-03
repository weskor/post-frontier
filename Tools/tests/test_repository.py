"""Consumer-visible settings, hashing and changed-path boundaries."""

from dataclasses import FrozenInstanceError, replace
import json
import os
from pathlib import Path
import shlex
import sys

from conftest import git
import pytest
from x import freshness, gitinfo, source
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


def test_source_identity_survives_commit_and_preserves_index(repo: Path) -> None:
    path = repo / "Source/rules.cpp"
    path.write_text("staged intermediate\n")
    git(repo, "add", "Source/rules.cpp")
    path.write_text("actual input\n")
    (repo / "Config/DefaultGame.ini").unlink()
    (repo / "untracked\nname").write_bytes(b"new source")
    (repo / "ignored").write_bytes(b"secret")
    (repo / "staged-new").write_text("added-source-bytes\n")
    git(repo, "add", "staged-new")
    index = repo / ".git/index"
    original_index = index.read_bytes()
    archived = repo.parent / "snapshot"
    dirty = source.capture(repo, archived)
    assert dirty["content_id"] and index.read_bytes() == original_index
    assert "actual input" in Path(dirty["tracked_diff"]).read_text()
    assert "staged intermediate" not in Path(dirty["tracked_diff"]).read_text()
    assert "added-source-bytes" in Path(dirty["tracked_diff"]).read_text()
    manifest = json.loads(Path(dirty["untracked_manifest"]).read_text())
    assert [item["path"] for item in manifest] == ["untracked\nname"]
    assert manifest[0]["content_hash"] == git(
        repo, "hash-object", "--no-filters", "untracked\nname"
    )
    assert {"path": "Config/DefaultGame.ini", "deleted": True} in dirty["delta"]
    git(repo, "add", "-A")
    git(repo, "commit", "-m", "same source")
    committed = source.capture(repo)
    assert dirty["head"] != committed["head"]
    assert source.equality(dirty, committed) == "equal"
    git(repo, "commit", "--allow-empty", "-m", "new history same source")
    assert source.equality(dirty, source.capture(repo)) == "equal"


def test_source_delta_content_deletion_and_untracked_identity(repo: Path) -> None:
    base = source.capture(repo)
    assert base["content_id"]
    path = repo / "Source/rules.cpp"
    path.write_text("new bytes\n")
    modified = source.capture(repo)
    assert source.equality(base, modified) == "different"
    git(repo, "add", "Source/rules.cpp")
    assert source.equality(modified, source.capture(repo)) == "equal"
    path.write_text("initial\n")
    assert source.equality(base, source.capture(repo)) == "equal"
    git(repo, "reset", "--", "Source/rules.cpp")
    path.unlink()
    assert source.equality(base, source.capture(repo)) == "different"
    path.write_text("initial\n")
    untracked = repo / "new-source"
    untracked.write_text("first")
    first = source.capture(repo)
    assert source.equality(base, first) == "different"
    untracked.write_text("other")
    assert source.equality(first, source.capture(repo)) == "different"
    untracked.unlink()
    (repo / "ignored").write_text("not source")
    assert source.equality(base, source.capture(repo)) == "equal"


def test_source_does_not_rehash_unchanged_files(
    repo: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    original = source.file_identity
    hashed = []

    def observe(path: Path, object_format: str, lfs: bool) -> tuple[str, str, int]:
        hashed.append(path.relative_to(repo))
        return original(path, object_format, lfs)

    monkeypatch.setattr(source, "file_identity", observe)
    assert source.capture(repo)["content_id"]
    assert hashed == []
    (repo / "Source/rules.cpp").write_text("modified\n")
    assert source.capture(repo)["content_id"]
    assert hashed == [Path("Source/rules.cpp")]


def test_lfs_identity_uses_pointer_hash_across_commit(repo: Path) -> None:
    clean = repo.parent / "lfs-clean.py"
    clean.write_text(
        "import hashlib, sys\n"
        "data = sys.stdin.buffer.read()\n"
        "if data.startswith(b'version https://git-lfs.github.com/spec/v1\\n'):\n"
        "    sys.stdout.buffer.write(data)\n"
        "else:\n"
        "    print('version https://git-lfs.github.com/spec/v1')\n"
        "    print('oid sha256:' + hashlib.sha256(data).hexdigest())\n"
        "    print('size ' + str(len(data)))\n"
    )
    git(
        repo,
        "config",
        "filter.lfs.clean",
        f"{shlex.quote(sys.executable)} {shlex.quote(str(clean))}",
    )
    (repo / ".gitattributes").write_text("*.asset filter=lfs -text\n")
    asset = repo / "content.asset"
    asset.write_bytes(b"large binary content")
    initial = source.capture(repo)
    assert initial["content_id"]
    git(repo, "add", ".")
    git(repo, "commit", "-m", "LFS pointer")
    assert source.equality(initial, source.capture(repo)) == "equal"
    asset.write_bytes(b"changed asset content")
    dirty = source.capture(repo)
    assert source.equality(initial, dirty) == "different"
    git(repo, "add", "content.asset")
    git(repo, "commit", "-m", "changed LFS pointer")
    assert source.equality(dirty, source.capture(repo)) == "equal"


@pytest.mark.parametrize(
    "cause", ["opaque-filter", "assume-unchanged", "noncanonical-line-endings"]
)
def test_source_uncertainty_never_fabricates_identity(repo: Path, cause: str) -> None:
    if cause == "opaque-filter":
        (repo / ".gitattributes").write_text("Source/* filter=opaque\n")
    elif cause == "assume-unchanged":
        git(repo, "update-index", "--assume-unchanged", "Source/rules.cpp")
        (repo / "Source/rules.cpp").write_text("hidden mutation\n")
    else:
        (repo / ".gitattributes").write_text("Source/* text eol=lf\n")
        (repo / "Source/rules.cpp").write_bytes(b"initial\r\n")
    snapshot = source.capture(repo)
    assert snapshot["content_id"] is None and snapshot["unknown"]
    assert source.equality(snapshot, snapshot) == "unknown"


def test_source_ident_expansions_never_claim_equivalence(repo: Path) -> None:
    path = repo / "Source/rules.cpp"
    (repo / ".gitattributes").write_text("Source/* ident\n")
    path.write_text("$Id$\n")
    git(repo, "add", ".gitattributes", "Source/rules.cpp")
    git(repo, "commit", "-m", "ident source")
    path.write_text("$Id: first expansion $\n")
    before = source.capture(repo)
    path.write_text("$Id: second expansion $\n")
    assert source.equality(before, source.capture(repo)) == "unknown"
