"""Package freshness follows bytes and the published package identity."""

import hashlib
import os
from pathlib import Path

import pytest
from x import freshness, jsonio
from x.content import playtest
from x.verifying.artifacts import package_snapshot


def publish(
    repo: Path, variant: str = "development", *, nested: bool = True
) -> tuple[Path, Path]:
    directory = repo / "Saved/Packages" / variant / "package-run"
    root = directory / "Linux" if nested else directory
    binary = root / "CoopRTS/Binaries/Linux/CoopRTS"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"package")
    content = root / "CoopRTS/Content/Paks"
    content.mkdir(parents=True)
    (content / "CoopRTS.pak").write_bytes(b"cooked-content")
    if variant == "playtest":
        for relative in playtest.INPUTS:
            path = repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(relative)
    input_hash = (
        playtest.input_hash(repo)
        if variant == "playtest"
        else freshness.current_hash(repo, "package")
    )
    jsonio.save(
        directory / "package.json",
        {
            "config": "development",
            "variant": variant,
            "run_id": "package-run",
            "published_at": "20261006-120000"
            if variant == "playtest"
            else "20261006-110000",
            "package_hash": input_hash,
        },
    )
    (directory.parent / "latest").symlink_to(directory.name, target_is_directory=True)
    return directory, root


@pytest.fixture(params=["development", "development-flat", "playtest"])
def published(repo: Path, request: pytest.FixtureRequest) -> tuple[Path, Path]:
    variant = "playtest" if request.param == "playtest" else "development"
    return publish(repo, variant, nested=request.param != "development-flat")


def test_matching_hash_accepts_and_source_edit_refuses(
    repo: Path, published: tuple[Path, Path]
) -> None:
    directory, root = published
    before = package_snapshot(repo)
    assert before["root"] == str(root.resolve())
    assert before["record"] == jsonio.load(directory / "package.json")
    assert before["artifacts"] == {
        "CoopRTS/Binaries/Linux/CoopRTS": hashlib.sha256(b"package").hexdigest(),
        "CoopRTS/Content/Paks/CoopRTS.pak": hashlib.sha256(
            b"cooked-content"
        ).hexdigest(),
    }
    (repo / "Source/rules.cpp").write_text("changed source\n")
    with pytest.raises(RuntimeError, match=r"run ./x package"):
        package_snapshot(repo)


@pytest.mark.parametrize("damage", ["missing", "invalid", "no_hash", "no_binary"])
def test_unusable_package_refuses(
    repo: Path, published: tuple[Path, Path], damage: str
) -> None:
    directory, root = published
    record = directory / "package.json"
    if damage == "missing":
        record.unlink()
    elif damage == "invalid":
        record.write_text("not json")
    elif damage == "no_hash":
        jsonio.save(record, {})
    else:
        (root / "CoopRTS/Binaries/Linux/CoopRTS").unlink()
    with pytest.raises(RuntimeError, match=r"run ./x package"):
        package_snapshot(repo)


def test_switching_latest_changes_session_snapshot(
    repo: Path, published: tuple[Path, Path]
) -> None:
    directory, _ = published
    before = package_snapshot(repo)
    record = jsonio.load(directory / "package.json")
    record["run_id"] = "different-package-run"
    jsonio.save(directory / "package.json", record)
    assert package_snapshot(repo) != before


@pytest.mark.parametrize(
    "relative", ["CoopRTS/Binaries/Linux/CoopRTS", "CoopRTS/Content/Paks/CoopRTS.pak"]
)
def test_package_mutation_with_preserved_size_and_mtime_changes_snapshot(
    repo: Path, published: tuple[Path, Path], relative: str
) -> None:
    _, root = published
    before = package_snapshot(repo)
    path = root / relative
    original = path.read_bytes()
    stat = path.stat()
    modified = bytes(byte ^ 0xFF for byte in original)
    path.write_bytes(modified)
    os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
    after = package_snapshot(repo)
    assert after["artifacts"][relative] == hashlib.sha256(modified).hexdigest()
    assert after != before


@pytest.mark.parametrize("directory_only", [False, True])
def test_package_without_content_refuses(
    repo: Path, published: tuple[Path, Path], directory_only: bool
) -> None:
    _, root = published
    content = root / "CoopRTS/Content/Paks"
    (content / "CoopRTS.pak").unlink()
    if directory_only:
        (content / "empty-directory").mkdir()
    with pytest.raises(RuntimeError, match=r"content missing; run ./x package"):
        package_snapshot(repo)


def test_newest_fresh_variant_and_stale_playtest_fallback(repo: Path) -> None:
    _, ordinary_root = publish(repo)
    _, playtest_root = publish(repo, "playtest")
    assert package_snapshot(repo)["root"] == str(playtest_root)
    (repo / "README.md").write_text("changed playtest controls")
    assert package_snapshot(repo)["root"] == str(ordinary_root)
    (repo / "Source/rules.cpp").write_text("changed game source")
    with pytest.raises(RuntimeError, match=r"run ./x package"):
        package_snapshot(repo)


def test_playtest_only_refuses_changed_distribution_inputs(repo: Path) -> None:
    publish(repo, "playtest")
    (repo / "README.md").write_text("changed playtest controls")
    with pytest.raises(RuntimeError, match=r"run ./x package"):
        package_snapshot(repo)
