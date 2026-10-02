"""Scope routing, explanations and formatting execute through the actual CLI."""

from pathlib import Path

from conftest import git
from landing_support import ROOT, commit_file, install_runner, invoke
import pytest
from x import jsonio
from x.scopes import load


@pytest.fixture
def task(repo: Path) -> Path:
    return install_runner(repo)


def test_doc_only_runs_lint_without_test_scopes(repo: Path, task: Path) -> None:
    commit_file(task, "Docs/task.md", "documentation only\n")
    result = invoke(task, "check")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "check scopes: lint" in result.stdout
    assert "lint selected by: Docs/task.md" in result.stdout
    (record_path,) = (repo.parent / "runs").glob("*/record.json")
    record = jsonio.load(record_path)
    assert [
        item["name"]
        for item in record["results"]
        if not item["name"].startswith("lint:")
    ] == ["lint"]
    assert record["execs"] == []


@pytest.mark.parametrize("all_files", [False, True])
def test_rules_use_production_path_mapping_and_print_reasons(
    repo: Path, task: Path, all_files: bool
) -> None:
    path = "Source/CoopRTS/Rules/PlacementPolicy.h"
    commit_file(task, path, "#pragma once\n")
    expected = load(ROOT).scopes_for([Path(path)])
    result = invoke(task, "check", *(["--all"] if all_files else []))
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"check scopes: {', '.join(expected)}" in result.stdout
    for name in expected:
        assert f"{name} selected by: {path}" in result.stdout
        (proof,) = (repo.parent / "runs").glob(f"*/{name}.proof")
        assert proof.read_text() == "executed"
    assert not list((repo.parent / "runs").glob("*/tools.proof"))


def test_failed_scope_does_not_skip_other_selected_scopes(
    repo: Path, task: Path
) -> None:
    commit_file(task, "Source/CoopRTS/Rules/PlacementPolicy.h", "#pragma once\n")
    commit_file(task, "Docs/fail.md", "fail\n")
    result = invoke(task, "check")
    assert result.returncode == 1
    for name in ("construction", "production", "rules"):
        assert f"FAIL {name}:" in result.stdout
        assert list((repo.parent / "runs").glob(f"*/{name}.proof"))


def test_formatting_runs_tests_and_lists_paths_in_summary(
    repo: Path, task: Path
) -> None:
    commit_file(task, "Tools/x/unformatted.py", "answer=1\n")
    result = invoke(task, "check")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS tools:" in result.stdout
    assert "check: PASS; reformatted: Tools/x/unformatted.py" in result.stdout
    assert (task / "Tools/x/unformatted.py").read_text() == "answer = 1\n"


def test_scope_map_checks_unchanged_tracked_and_deleted_files(
    repo: Path, task: Path
) -> None:
    (task / "unmapped.asset").write_text("tracked but deleted\n")
    git(task, "add", "unmapped.asset")
    (task / "unmapped.asset").unlink()
    result = invoke(task, "check")
    assert result.returncode == 1
    assert "unmapped.asset:1: scope-map:" in result.stdout


def test_scope_map_rejects_unchanged_unmapped_file(repo: Path, task: Path) -> None:
    commit_file(repo, "unmapped.asset", "unmapped\n")
    git(task, "rebase", "main")
    commit_file(task, "Docs/task.md", "doc only\n")
    result = invoke(task, "check")
    assert result.returncode == 1
    assert "unmapped.asset:1: scope-map:" in result.stdout
    assert "check scopes: lint" in result.stdout
