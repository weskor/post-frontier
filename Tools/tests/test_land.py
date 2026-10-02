"""Prove real landing and refusal paths in separate disposable worktrees."""

from pathlib import Path

from conftest import git
from landing_support import commit_file, git_result, install_runner, invoke
import pytest
from x import jsonio


@pytest.fixture
def task(repo: Path) -> Path:
    return install_runner(repo)


def test_fast_forward_separate_main_with_local_edits(repo: Path, task: Path) -> None:
    commit_file(task, "Docs/task.md", "landed\n")
    before = git(repo, "rev-parse", "HEAD")
    (repo / "Source/rules.cpp").write_text("unrelated local edit\n")
    result = invoke(task, "land")
    assert result.returncode == 0, result.stdout + result.stderr
    assert git(repo, "rev-parse", "HEAD") == git(task, "rev-parse", "HEAD")
    assert f"landed {before}.." in result.stdout and "run " in result.stdout
    assert (repo / "Docs/task.md").read_text() == "landed\n"
    assert (repo / "Source/rules.cpp").read_text() == "unrelated local edit\n"
    (record_path,) = (repo.parent / "runs").glob("*/record.json")
    record = jsonio.load(record_path)
    assert record["status"] == "passed"
    assert record["lock_waits"][0]["lock"] == "land"


@pytest.mark.parametrize("mode", ["main", "dirty", "empty", "detached"])
def test_initial_refusals(repo: Path, task: Path, mode: str) -> None:
    target = task
    if mode == "main":
        target = repo
    elif mode == "dirty":
        (task / "untracked").write_text("dirty")
    elif mode == "detached":
        git(task, "switch", "--detach")
    before = git(repo, "rev-parse", "HEAD")
    result = invoke(target, "land")
    assert result.returncode == 1
    expected = {
        "main": "not main",
        "dirty": "worktree is dirty",
        "empty": "no commits beyond main",
        "detached": "task/<slug> branch",
    }
    assert expected[mode] in result.stdout
    assert git(repo, "rev-parse", "HEAD") == before


def test_rebase_conflict_aborts_and_names_path(repo: Path, task: Path) -> None:
    commit_file(task, "Docs/task.md", "task conflict\n")
    commit_file(repo, "Docs/task.md", "main conflict\n")
    task_commit = git(task, "rev-parse", "HEAD")
    before = git(repo, "rev-parse", "HEAD")
    result = invoke(task, "land")
    assert result.returncode == 1
    assert "rebase conflict; rebase aborted: Docs/task.md" in result.stdout
    assert git(task, "rev-parse", "HEAD") == task_commit
    assert git(repo, "rev-parse", "HEAD") == before
    assert git(task, "status", "--porcelain") == ""


@pytest.mark.parametrize("failure", ["lint", "scope", "format"])
def test_check_blocks_landing(repo: Path, task: Path, failure: str) -> None:
    if failure == "lint":
        commit_file(task, "Tools/x/broken.py", "answer = missing\n")
    elif failure == "scope":
        commit_file(task, "Docs/fail.md", "fail the selected scope\n")
        commit_file(task, "Tools/check.txt", "select tools\n")
    else:
        commit_file(task, "Tools/x/unformatted.py", "answer=1\n")
    before = git(repo, "rev-parse", "HEAD")
    result = invoke(task, "land")
    assert result.returncode == 1, result.stdout + result.stderr
    assert (
        "check reformatted files" if failure == "format" else "check failed"
    ) in result.stdout
    assert git(repo, "rev-parse", "HEAD") == before
    if failure == "format":
        assert (task / "Tools/x/unformatted.py").read_text() == "answer = 1\n"
        assert "PASS tools" in result.stdout


def test_main_conflicting_local_edit_is_preserved(repo: Path, task: Path) -> None:
    commit_file(task, "Docs/task.md", "landed\n")
    (repo / "Docs/task.md").write_text("local main edit\n")
    before = git(repo, "rev-parse", "HEAD")
    result = invoke(task, "land")
    assert result.returncode == 1
    assert "main fast-forward refused" in result.stdout
    assert "local changes" in result.stdout and "Docs/task.md" in result.stdout
    assert git(repo, "rev-parse", "HEAD") == before
    assert (repo / "Docs/task.md").read_text() == "local main edit\n"


def test_rebase_then_land(repo: Path, task: Path) -> None:
    commit_file(task, "Docs/task.md", "task\n")
    commit_file(repo, "Docs/main.md", "main\n")
    old_task = git(task, "rev-parse", "HEAD")
    result = invoke(task, "land")
    assert result.returncode == 0, result.stdout + result.stderr
    assert git(task, "rev-parse", "HEAD") != old_task
    assert git(repo, "rev-parse", "HEAD") == git(task, "rev-parse", "HEAD")
    assert (task / "Docs/main.md").read_text() == "main\n"


def test_hook_blocks_main_commit_merge_and_ref_deletion(repo: Path, task: Path) -> None:
    assert invoke(task, "help").returncode == 0
    commit_file(task, "Docs/task.md", "task\n")
    (repo / "Docs/main.md").write_text("main\n")
    git(repo, "add", "Docs/main.md")
    for args in (("commit", "-m", "blocked"), ("update-ref", "-d", "refs/heads/main")):
        result = git_result(repo, *args)
        assert result.returncode != 0
        assert "main only moves through ./x land" in result.stderr
    git(repo, "restore", "--source=HEAD", "--staged", "--worktree", "Docs/main.md")
    result = git_result(repo, "merge", "--ff-only", "task/acceptance")
    assert result.returncode != 0
    assert "main only moves through ./x land" in result.stderr
    result = git_result(repo, "merge", "--ff-only", "task/acceptance", marker=True)
    assert result.returncode == 0, result.stderr
    other = repo.parent / "other"
    git(task, "worktree", "add", "-b", "task/other", str(other))
    commit_file(other, "Docs/other.md", "other\n")
    git(task, "tag", "allowed-tag")
    git(task, "update-ref", "refs/custom/allowed", "HEAD")
    assert git(repo, "rev-parse", "main") == git(task, "rev-parse", "HEAD")
