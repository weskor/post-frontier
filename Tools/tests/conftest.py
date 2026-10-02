"""All tests use disposable repositories, locks and evidence roots."""

from pathlib import Path
import shutil
import subprocess

import pytest


def git(repo: Path, *args: str) -> str:
    return (
        subprocess.check_output(
            ["git", "-C", str(repo), *args], stderr=subprocess.STDOUT
        )
        .decode()
        .strip()
    )


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    root.mkdir()
    git(root, "init", "-b", "main")
    git(root, "config", "user.name", "Runner Test")
    git(root, "config", "user.email", "runner@example.test")
    (root / "Tools/x").mkdir(parents=True)
    shutil.copyfile(
        Path(__file__).parents[1] / "x/settings.toml", root / "Tools/x/settings.toml"
    )
    (root / "Config").mkdir()
    (root / "Config/DefaultGame.ini").write_text(
        '[/Script/UnrealEd.ProjectPackagingSettings]\n+MapsToCook=(FilePath="/Game/Maps/Boot")\n'
    )
    (root / "Source").mkdir()
    (root / "Source/rules.cpp").write_text("initial\n")
    (root / "CoopRTS.uproject").write_text("{}\n")
    (root / ".gitignore").write_text("Intermediate/\nignored\n")
    git(root, "add", ".")
    git(root, "commit", "-m", "initial")
    return root
