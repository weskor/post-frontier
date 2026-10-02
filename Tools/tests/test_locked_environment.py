"""Dependency changes must sync before the first strict Python lint pass."""

from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile

from conftest import git
from landing_support import install_runner, invoke


def uv(repo: Path, *args: str) -> None:
    result = subprocess.run(
        ["uv", *args], cwd=repo, capture_output=True, text=True, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr


def dependency_wheel(repo: Path) -> Path:
    wheel = repo / "locked_dependency-1.0.0-py3-none-any.whl"
    metadata = "locked_dependency-1.0.0.dist-info"
    with ZipFile(wheel, "w") as archive:
        archive.writestr(
            "locked_dependency/__init__.py", "def answer() -> int:\n    return 42\n"
        )
        archive.writestr("locked_dependency/py.typed", "")
        archive.writestr(
            f"{metadata}/METADATA",
            "Metadata-Version: 2.1\nName: locked-dependency\nVersion: 1.0.0\n",
        )
        archive.writestr(
            f"{metadata}/WHEEL",
            "Wheel-Version: 1.0\nRoot-Is-Purelib: true\nTag: py3-none-any\n",
        )
        archive.writestr(f"{metadata}/RECORD", "")
    return wheel


def test_check_syncs_new_locked_dependency_before_mypy(repo: Path) -> None:
    task = install_runner(repo)
    uv(task, "sync", "--locked", "--python", sys.executable)
    wheel = dependency_wheel(task)
    uv(task, "add", "--group", "dev", "--no-sync", str(wheel))
    source = task / "Tools/x/locked_probe.py"
    source.write_text("from locked_dependency import answer\n\nvalue: int = answer()\n")
    stale = subprocess.run(
        [str(task / ".venv/bin/python"), "-c", "import locked_dependency"],
        cwd=task,
        capture_output=True,
        text=True,
        check=False,
    )
    assert stale.returncode != 0
    assert "ModuleNotFoundError" in stale.stderr
    git(task, "add", "pyproject.toml", "uv.lock", str(source.relative_to(task)))
    result = invoke(task, "check")
    assert result.returncode == 0, result.stdout + result.stderr
    synced = subprocess.run(
        [
            str(task / ".venv/bin/python"),
            "-c",
            "from locked_dependency import answer; print(answer())",
        ],
        cwd=task,
        capture_output=True,
        text=True,
        check=False,
    )
    assert synced.returncode == 0, synced.stderr
    assert synced.stdout.strip() == "42"
