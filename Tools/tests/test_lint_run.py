"""Exercise actual check formatting, type scope and evidence in temp repos."""

from dataclasses import replace
import json
from pathlib import Path
import shutil
import subprocess
import sys

from conftest import git
import pytest
from x import jsonio, lint
from x.context import Context
from x.lint.model import RULES
from x.runs import Run
from x.settings import load


@pytest.fixture
def lint_repo(repo: Path) -> Path:
    root = Path(__file__).parents[2]
    shutil.copytree(root / "Tools/x", repo / "Tools/x", dirs_exist_ok=True)
    shutil.copyfile(root / "x", repo / "x")
    shutil.copyfile(root / "pyproject.toml", repo / "pyproject.toml")
    (repo / ".venv").symlink_to(root / ".venv", target_is_directory=True)
    settings = repo / "Tools/x/settings.toml"
    settings.write_text(
        settings.read_text().replace(
            "~/.local/state/cooprts/runs", str(repo.parent / "runs")
        )
    )
    return repo


def configure(
    repo: Path, scopes: dict[str, list[str]], exceptions: str = "exceptions = []\n"
) -> None:
    (repo / "Tools/x/lint.toml").write_text(
        "[rules]\n"
        + "".join(f"{rule} = {json.dumps(scopes.get(rule, []))}\n" for rule in RULES)
    )
    (repo / "Tools/x/lint-exceptions.toml").write_text(exceptions)


def context(repo: Path) -> Context:
    settings = replace(
        load(repo), runs_root=repo.parent / "runs", lock_dir=repo.parent / "locks"
    )
    return Context(repo, settings, Run(repo, settings.runs_root, "check", []), "check")


def test_actual_check_formats_and_records_every_rule(lint_repo: Path) -> None:
    configure(
        lint_repo,
        {"format": ["sample.py"], "ruff": ["sample.py"], "mypy": ["sample.py"]},
    )
    file = lint_repo / "sample.py"
    file.write_text("def result() -> int:\n    return 1\n")
    git(lint_repo, "add", ".")
    git(lint_repo, "commit", "-m", "lint setup")
    file.write_text("def result( )->int:\n return 1\n")
    result = subprocess.run(
        [sys.executable, str(lint_repo / "x"), "check"],
        cwd=lint_repo,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "reformatted sample.py" in result.stdout
    assert file.read_text() == "def result() -> int:\n    return 1\n"
    (record_path,) = (lint_repo.parent / "runs").glob("*/record.json")
    record = jsonio.load(record_path)
    assert record["status"] == "passed"
    assert {item["name"] for item in record["results"]} == {
        f"lint:{rule}" for rule in RULES
    }


def test_cross_file_types_and_deleted_files(
    lint_repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    configure(lint_repo, {"mypy": ["*.py"]})
    (lint_repo / "provider.py").write_text("def value() -> int:\n    return 1\n")
    (lint_repo / "consumer.py").write_text(
        "from provider import value\nresult: int = value()\n"
    )
    git(lint_repo, "add", ".")
    git(lint_repo, "commit", "-m", "typed pair")
    (lint_repo / "provider.py").write_text(
        'def value() -> str:\n    return "changed"\n'
    )
    ctx = context(lint_repo)
    assert not lint.run(ctx, [Path("provider.py"), Path("deleted.py")], fix=False)
    assert "consumer.py:2: mypy:" in capsys.readouterr().out


def test_unused_exception_on_unchanged_path_blocks(
    lint_repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    configure(
        lint_repo,
        {"marker": ["*.py"]},
        '[[exceptions]]\nrule="marker"\npath="old.py"\nreason="legitimate marker text"\n',
    )
    (lint_repo / "old.py").write_text("# TODO\n")
    ctx = context(lint_repo)
    assert lint.run(ctx, [], fix=False)
    (lint_repo / "old.py").write_text("# clean\n")
    assert not lint.run(ctx, [], fix=False)
    assert "unused exception for old.py" in capsys.readouterr().out


def test_ruff_and_format_failures_block(
    lint_repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    configure(lint_repo, {"ruff": ["bad.py"], "format": ["bad.py"]})
    (lint_repo / "bad.py").write_text("answer=unknown_name\n")
    ctx = context(lint_repo)
    assert not lint.run(ctx, [Path("bad.py")], fix=False)
    output = capsys.readouterr().out
    assert "bad.py:1: ruff: F821" in output
    assert "bad.py:1: format:" in output


def test_exact_clang_version_is_required(
    lint_repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    configure(lint_repo, {"format": ["test.cpp"]})
    (lint_repo / ".clang-format").write_text(
        "# clang-format 0.0.0\nBasedOnStyle: LLVM\n"
    )
    (lint_repo / "test.cpp").write_text("int value() { return 1; }\n")
    ctx = context(lint_repo)
    assert not lint.run(ctx, [Path("test.cpp")], fix=True)
    assert "exact formatter version required: 0.0.0" in capsys.readouterr().out
    assert (lint_repo / "test.cpp").read_text() == "int value() { return 1; }\n"


def test_deleted_strict_module_rechecks_its_consumers(
    lint_repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    configure(lint_repo, {"mypy": ["*.py"]})
    (lint_repo / "provider.py").write_text("def value() -> int:\n    return 1\n")
    (lint_repo / "consumer.py").write_text(
        "from provider import value\nresult = value()\n"
    )
    git(lint_repo, "add", ".")
    git(lint_repo, "commit", "-m", "typed pair")
    (lint_repo / "provider.py").unlink()
    assert not lint.run(context(lint_repo), [Path("provider.py")], fix=False)
    output = capsys.readouterr().out
    assert "consumer.py:1: mypy:" in output and "[import-not-found]" in output
