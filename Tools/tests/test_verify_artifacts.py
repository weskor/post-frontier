"""Package freshness follows bytes and the published package identity."""

from pathlib import Path

import pytest
from x import freshness, jsonio
from x.verifying.artifacts import package_snapshot


def publish(repo: Path) -> Path:
    root = repo / "Saved/Packages/development/latest"
    binary = root / "CoopRTS/Binaries/Linux/CoopRTS"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"package")
    jsonio.save(
        root / "package.json", {"package_hash": freshness.current_hash(repo, "package")}
    )
    return root


def test_matching_hash_accepts_and_source_edit_refuses(repo: Path) -> None:
    root = publish(repo)
    before = package_snapshot(repo)
    assert before["root"] == str(root.resolve())
    (repo / "Source/rules.cpp").write_text("changed source\n")
    with pytest.raises(RuntimeError, match=r"run ./x package"):
        package_snapshot(repo)


@pytest.mark.parametrize("damage", ["missing", "invalid", "no_hash", "no_binary"])
def test_unusable_package_refuses(repo: Path, damage: str) -> None:
    root = publish(repo)
    record = root / "package.json"
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


def test_switching_latest_changes_session_snapshot(repo: Path) -> None:
    root = publish(repo)
    before = package_snapshot(repo)
    record = jsonio.load(root / "package.json")
    record["run_id"] = "different-package-run"
    jsonio.save(root / "package.json", record)
    assert package_snapshot(repo) != before


def test_package_binary_mutation_changes_snapshot(repo: Path) -> None:
    root = publish(repo)
    before = package_snapshot(repo)
    (root / "CoopRTS/Binaries/Linux/CoopRTS").write_bytes(b"modified-package")
    assert package_snapshot(repo) != before
