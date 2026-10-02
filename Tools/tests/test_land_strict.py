"""Bypass probes run only in disposable repositories, through real Git and CLI."""

import fcntl
from itertools import pairwise
import json
import os
from pathlib import Path

from conftest import git
from landing_support import commit_file, git_result, install_runner, invoke
import pytest
from x.landing import GRANT, LEDGER
from x.settings import load


@pytest.fixture
def task(repo: Path) -> Path:
    task = install_runner(repo)
    result = invoke(task, "check")
    assert result.returncode == 0, result.stdout + result.stderr
    return task


@pytest.mark.parametrize("marker", [False, True])
def test_empty_main_commit_rejected(repo: Path, task: Path, marker: bool) -> None:
    before = git(repo, "rev-parse", "main")
    result = git_result(repo, "commit", "--allow-empty", "-m", "bypass", marker=marker)
    assert result.returncode != 0
    assert git(repo, "rev-parse", "main") == before


@pytest.mark.parametrize(
    "mode", ["unlocked", "wrong-old", "wrong-new", "dead", "not-land"]
)
def test_forged_grants_rejected(repo: Path, task: Path, mode: str) -> None:
    commit_file(task, "Docs/task.md", "target\n")
    old, new = git(repo, "rev-parse", "main"), git(task, "rev-parse", "HEAD")
    grant = {"old": old, "new": new, "run_id": "forged", "pid": os.getpid()}
    if mode == "wrong-old":
        grant["old"] = new
    elif mode == "wrong-new":
        grant["new"] = old
    elif mode == "dead":
        grant["pid"] = 2147483647
    (repo / ".git" / GRANT).write_text(json.dumps(grant))
    lock = load(task).lock_dir / "land.lock"
    with lock.open("a+b") as stream:
        if mode != "unlocked":
            fcntl.flock(stream, fcntl.LOCK_EX)
        result = git_result(task, "update-ref", "refs/heads/main", new, old)
    assert result.returncode != 0
    assert git(repo, "rev-parse", "main") == old


@pytest.mark.parametrize("packed", [False, True])
def test_main_deletion_and_recreation_rejected(
    repo: Path, task: Path, packed: bool
) -> None:
    before = git(repo, "rev-parse", "main")
    if packed:
        git(repo, "pack-refs", "--all")
    result = git_result(task, "update-ref", "-d", "refs/heads/main")
    assert result.returncode != 0
    assert git(repo, "rev-parse", "main") == before
    # Deliberately bypass deletion to prove the persistent ledger blocks recreation.
    git(task, "-c", "core.hooksPath=/dev/null", "update-ref", "-d", "refs/heads/main")
    result = git_result(task, "update-ref", "refs/heads/main", before)
    assert result.returncode != 0
    assert git_result(task, "rev-parse", "--verify", "main").returncode != 0


@pytest.mark.parametrize("mode", ["hookless-commit", "direct-ref", "rewind"])
@pytest.mark.parametrize("command", ["check", "land"])
def test_audit_detects_unlanded_main(
    repo: Path, task: Path, mode: str, command: str
) -> None:
    seed = git(repo, "rev-parse", "main")
    commit_file(task, "Docs/task.md", "unlanded\n")
    expected = git(task, "rev-parse", "HEAD")
    if mode == "rewind":
        landed = invoke(task, "land")
        assert landed.returncode == 0, landed.stdout + landed.stderr
    ledger_before = (repo / ".git" / LEDGER).read_text()
    if mode == "hookless-commit":
        git(
            repo,
            "-c",
            "core.hooksPath=/dev/null",
            "commit",
            "--allow-empty",
            "-m",
            "bypass",
        )
        expected = git(repo, "rev-parse", "main")
    elif mode == "direct-ref":
        (repo / ".git/refs/heads/main").write_text(expected + "\n")
    else:
        (repo / ".git/refs/heads/main").write_text(seed + "\n")
    result = invoke(task, command)
    assert result.returncode == 1
    assert (
        "main audit failed" in result.stderr
        and "stop and ask the owner" in result.stderr
    )
    if mode != "rewind":
        assert expected in result.stderr
    assert (repo / ".git" / LEDGER).read_text() == ledger_before


@pytest.mark.parametrize("mode", ["modified", "not-executable", "missing", "extra"])
@pytest.mark.parametrize("command", ["help", "check", "land"])
def test_hook_integrity_refuses_every_command(
    repo: Path, task: Path, mode: str, command: str
) -> None:
    path = repo / "Tools/hooks/reference-transaction"
    if mode == "modified":
        path.write_text(path.read_text() + "\n# altered\n")
    elif mode == "not-executable":
        path.chmod(0o644)
    elif mode == "missing":
        path.unlink()
    else:
        path = repo / "Tools/hooks/pre-commit"
        path.write_text("#!/bin/sh\nexit 0\n")
        path.chmod(0o755)
    result = invoke(task, command)
    assert result.returncode == 1
    assert "hook integrity failed" in result.stderr and str(path) in result.stderr


def test_two_landings_audit_concatenated_ranges(repo: Path, task: Path) -> None:
    tips = [git(repo, "rev-parse", "main")]
    for content in ("first\n", "second\n"):
        commit_file(task, "Docs/task.md", content)
        result = invoke(task, "land")
        assert result.returncode == 0, result.stdout + result.stderr
        tips.append(git(repo, "rev-parse", "main"))
    entries = [
        json.loads(line) for line in (repo / ".git" / LEDGER).read_text().splitlines()
    ]
    assert [(entry["old"], entry["new"]) for entry in entries[1:]] == list(
        pairwise(tips)
    )
    assert entries[1]["run_id"] != entries[2]["run_id"]
    result = invoke(task, "check")
    assert result.returncode == 0, result.stdout + result.stderr


def test_failed_merge_removes_grant(repo: Path, task: Path) -> None:
    commit_file(task, "Docs/task.md", "target\n")
    before = git(repo, "rev-parse", "main")
    # A real Git merge failure after checks, without dirtying either worktree.
    (repo / ".git/index.lock").write_text("held by probe\n")
    result = invoke(task, "land")
    assert result.returncode == 1
    assert git(repo, "rev-parse", "main") == before
    assert not (repo / ".git" / GRANT).exists()
    assert json.loads((repo / ".git" / LEDGER).read_text()) == {"seed": before}


def test_landing_can_replace_legacy_marker_hook(repo: Path) -> None:
    task = install_runner(repo)
    hook = "Tools/hooks/reference-transaction"
    replacement = (task / hook).read_text()
    legacy = (
        '#!/bin/sh\n[ "$1" = prepared ] || exit 0\n'
        '[ "$X_LAND" = 1 ] && exit 0\n'
        "while read -r old new ref; do\n"
        ' [ "$ref" != refs/heads/main ] || exit 1\n'
        "done\n"
    )
    commit_file(repo, hook, legacy)
    git(task, "rebase", "main")
    commit_file(task, hook, replacement)
    result = invoke(task, "land")
    assert result.returncode == 0, result.stdout + result.stderr
    assert git(repo, "rev-parse", "main") == git(task, "rev-parse", "HEAD")
    assert (repo / hook).read_text() == replacement
