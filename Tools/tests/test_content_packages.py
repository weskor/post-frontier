"""Package retention and launch freshness protect consumers, not argv spelling."""

from pathlib import Path

import pytest
from x import freshness, jsonio
from x.content.packages import latest_package, publish, retain_packages


def make_package(repo: Path, run_id: str) -> Path:
    directory = repo / "Saved/Packages/development" / run_id
    executable = directory / "Linux/CoopRTS/Binaries/Linux/CoopRTS"
    executable.parent.mkdir(parents=True)
    executable.write_text("game")
    publish(
        repo, directory, "development", run_id, freshness.current_hash(repo, "package")
    )
    return directory


def test_retention_keeps_two_newest_successful_packages(repo: Path) -> None:
    first = make_package(repo, "20261002-100000-package-a")
    second = make_package(repo, "20261002-110000-package-b")
    third = make_package(repo, "20261002-120000-package-c")
    root = third.parent
    unfinished = root / "20261002-130000-package-failed"
    unfinished.mkdir()
    (unfinished / "cook.log").write_text("failure")
    retain_packages(root)
    assert not first.exists()
    assert second.is_dir() and third.is_dir()
    assert (root / "latest").resolve() == third
    assert (unfinished / "cook.log").read_text() == "failure"
    assert jsonio.load(third / "package.json")["run_id"] == third.name


def test_source_edit_refuses_package_and_revert_restores_it(repo: Path) -> None:
    directory = make_package(repo, "20261002-100000-package-a")
    expected = directory / "Linux/CoopRTS/Binaries/Linux/CoopRTS"
    assert latest_package(repo, "development", "CoopRTS") == expected
    source = repo / "Source/rules.cpp"
    original = source.read_text()
    source.write_text("changed source\n")
    with pytest.raises(ValueError, match="stale development package; run ./x package"):
        latest_package(repo, "development", "CoopRTS")
    source.write_text(original)
    assert latest_package(repo, "development", "CoopRTS") == expected


def test_missing_package_refuses_with_actionable_command(repo: Path) -> None:
    with pytest.raises(ValueError, match="run ./x package shipping"):
        latest_package(repo, "shipping", "CoopRTS")
