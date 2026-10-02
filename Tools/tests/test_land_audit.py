"""Ledger damage fails closed; consistent check audits never queue behind land."""

import fcntl
import json
from pathlib import Path
import subprocess
import sys

from conftest import git
from landing_support import commit_file, install_runner, invoke, seed_evidence
import pytest
from test_locks import eventually
from x.landing import LEDGER, LEDGER_REF
from x.settings import load


@pytest.fixture
def task(repo: Path) -> Path:
    task = install_runner(repo)
    seed_evidence(repo)
    result = invoke(task, "check")
    assert result.returncode == 0, result.stdout + result.stderr
    return task


def read_anchor(repo: Path) -> dict[str, object]:
    value = json.loads(git(repo, "cat-file", "blob", LEDGER_REF))
    assert isinstance(value, dict)
    return value


def write_anchor(repo: Path, entries: int, new: str) -> None:
    result = subprocess.run(
        ["git", "-C", str(repo), "hash-object", "-w", "--stdin"],
        input=json.dumps({"entries": entries, "new": new}),
        text=True,
        capture_output=True,
        check=True,
    )
    git(repo, "update-ref", LEDGER_REF, result.stdout.strip())


def test_seed_and_committed_ranges_are_anchored(repo: Path, task: Path) -> None:
    assert read_anchor(repo) == {"entries": 1, "new": git(repo, "rev-parse", "main")}
    for count in (2, 3):
        commit_file(task, "Docs/task.md", f"range {count}\n")
        landed = invoke(task, "land")
        assert landed.returncode == 0, landed.stdout + landed.stderr
        assert read_anchor(repo) == {
            "entries": count,
            "new": git(repo, "rev-parse", "main"),
        }
    # Packing the anchor as well as main must preserve the audit.
    git(repo, "pack-refs", "--all")
    git(repo, "gc")
    checked = invoke(task, "check")
    assert checked.returncode == 0, checked.stdout + checked.stderr


@pytest.mark.parametrize(
    "damage",
    [
        "missing",
        "empty",
        "truncate",
        "endpoint",
        "anchor-count",
        "anchor-tip",
        "anchor-missing",
    ],
)
@pytest.mark.parametrize("command", ["check", "land"])
def test_ledger_damage_blocks_audit(
    repo: Path, task: Path, damage: str, command: str
) -> None:
    seed = git(repo, "rev-parse", "main")
    commit_file(task, "Docs/task.md", "recorded range\n")
    landed = invoke(task, "land")
    assert landed.returncode == 0, landed.stdout + landed.stderr
    path = repo / ".git" / LEDGER
    lines = path.read_text().splitlines()
    anchor_before = read_anchor(repo)
    if damage == "missing":
        path.unlink()
    elif damage == "empty":
        path.write_text("")
    elif damage == "truncate":
        path.write_text(lines[0] + "\n")
    elif damage == "endpoint":
        value = json.loads(lines[1])
        value["new"] = seed
        path.write_text(lines[0] + "\n" + json.dumps(value) + "\n")
    elif damage == "anchor-count":
        write_anchor(repo, 1, git(repo, "rev-parse", "main"))
    elif damage == "anchor-tip":
        write_anchor(repo, 2, seed)
    else:
        git(repo, "update-ref", "-d", LEDGER_REF)
    result = invoke(task, command)
    assert result.returncode == 1
    assert (
        "main audit failed" in result.stderr
        and "stop and ask the owner" in result.stderr
    )
    if damage == "missing":
        assert not path.exists()
    if damage in {"missing", "empty", "truncate", "endpoint"}:
        assert read_anchor(repo) == anchor_before


def test_hookless_commit_and_ledger_deletion_are_detected(
    repo: Path, task: Path
) -> None:
    git(
        repo,
        "-c",
        "core.hooksPath=/dev/null",
        "commit",
        "--allow-empty",
        "-m",
        "bypass",
    )
    (repo / ".git" / LEDGER).unlink()
    result = invoke(task, "check")
    assert result.returncode == 1
    assert "ledger missing while its anchor exists" in result.stderr


def test_recreation_still_rejected_after_ledger_deletion(
    repo: Path, task: Path
) -> None:
    tip = git(repo, "rev-parse", "main")
    git(task, "-c", "core.hooksPath=/dev/null", "update-ref", "-d", "refs/heads/main")
    (repo / ".git" / LEDGER).unlink()
    result = subprocess.run(
        ["git", "-C", str(task), "update-ref", "refs/heads/main", tip],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode != 0
    assert "main only moves through ./x land" in result.stderr


@pytest.mark.parametrize("stale", [False, True])
def test_only_land_reseeds_void_pre_cutover_evidence(repo: Path, stale: bool) -> None:
    task = install_runner(repo)
    tip = git(repo, "rev-parse", "main")
    path = repo / ".git" / LEDGER
    seed = git(repo, "rev-parse", "main~1") if stale else tip
    original = json.dumps({"seed": seed}) + "\n"
    path.write_text(original)
    checked = invoke(task, "check")
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert path.read_text() == original
    assert (
        subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "--verify", "--quiet", LEDGER_REF],
            capture_output=True,
            check=False,
        ).returncode
        == 1
    )
    commit_file(task, "Docs/task.md", "cutover establishes authority\n")
    landed = invoke(task, "land")
    assert landed.returncode == 0, landed.stdout + landed.stderr
    entries = [json.loads(line) for line in path.read_text().splitlines()]
    assert entries[0] == {"seed": tip}
    assert read_anchor(repo) == {"entries": 2, "new": git(task, "rev-parse", "HEAD")}


def test_check_never_creates_evidence_before_cutover(repo: Path) -> None:
    task = install_runner(repo)
    checked = invoke(task, "check")
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert not (repo / ".git" / LEDGER).exists()
    assert (
        subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "--verify", "--quiet", LEDGER_REF],
            capture_output=True,
            check=False,
        ).returncode
        == 1
    )


def test_consistent_check_does_not_wait_on_landing_lock(repo: Path, task: Path) -> None:
    lock = load(task).lock_dir / "land.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    with lock.open("a+b") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        result = subprocess.run(
            [sys.executable, str(task / "x"), "check"],
            cwd=task,
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "waiting for land.lock" not in result.stdout


def test_inconsistent_snapshot_retries_under_landing_lock(
    repo: Path, task: Path
) -> None:
    path = repo / ".git" / LEDGER
    valid = path.read_text()
    lock = load(task).lock_dir / "land.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    log = repo.parent / "audit-retry.log"
    with lock.open("a+b") as stream, log.open("wb") as output:
        fcntl.flock(stream, fcntl.LOCK_EX)
        path.write_text(valid + '{"old":')
        child = subprocess.Popen(
            [sys.executable, str(task / "x"), "check"],
            cwd=task,
            stdout=output,
            stderr=output,
        )
        try:
            eventually(
                lambda: "waiting for land.lock" in log.read_text(),
                "inconsistent audit did not retry under land.lock",
            )
            assert child.poll() is None
            path.write_text(valid)
        except BaseException:
            child.kill()
            child.wait(timeout=5)
            raise
    try:
        assert child.wait(timeout=10) == 0, log.read_text()
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=5)


@pytest.mark.parametrize("damage", ["gap", "backwards"])
def test_audit_rejects_nonconcatenated_ranges(
    repo: Path, task: Path, damage: str
) -> None:
    for count in (1, 2, 3):
        commit_file(task, "Docs/task.md", f"recorded {count}\n")
        landed = invoke(task, "land")
        assert landed.returncode == 0, landed.stdout + landed.stderr
    path = repo / ".git" / LEDGER
    entries = [json.loads(line) for line in path.read_text().splitlines()]
    if damage == "gap":
        entries[2]["old"] = entries[0]["seed"]
    else:
        first, second = entries[1]["new"], entries[2]["new"]
        entries[1]["new"] = second
        entries[2]["old"], entries[2]["new"] = second, first
        entries[3]["old"] = first
    path.write_text("".join(json.dumps(entry) + "\n" for entry in entries))
    checked = invoke(task, "check")
    assert checked.returncode == 1 and "main audit failed" in checked.stderr


def test_audit_reports_only_unrecorded_commits_after_ranges(
    repo: Path, task: Path
) -> None:
    recorded = []
    for count in (1, 2):
        commit_file(task, "Docs/task.md", f"recorded {count}\n")
        landed = invoke(task, "land")
        assert landed.returncode == 0, landed.stdout + landed.stderr
        recorded.append(git(repo, "rev-parse", "main"))
    unrecorded = []
    for count in (1, 2):
        git(
            repo,
            "-c",
            "core.hooksPath=/dev/null",
            "commit",
            "--allow-empty",
            "-m",
            f"unrecorded {count}",
        )
        unrecorded.append(git(repo, "rev-parse", "main"))
    checked = invoke(task, "check")
    assert checked.returncode == 1
    assert all(commit in checked.stderr for commit in unrecorded)
    assert all(commit not in checked.stderr for commit in recorded)


def test_audit_rejects_endpoint_outside_first_parent_chain(
    repo: Path, task: Path
) -> None:
    git(task, "switch", "-c", "task/side")
    commit_file(task, "Docs/side.md", "side-parent commit\n")
    side = git(task, "rev-parse", "HEAD")
    git(task, "switch", "task/acceptance")
    commit_file(task, "Docs/task.md", "first-parent commit\n")
    git(task, "merge", "--no-ff", "task/side", "-m", "merge side")
    landed = invoke(task, "land")
    assert landed.returncode == 0, landed.stdout + landed.stderr
    path = repo / ".git" / LEDGER
    entries = [json.loads(line) for line in path.read_text().splitlines()]
    entries[1]["new"] = side
    path.write_text("".join(json.dumps(entry) + "\n" for entry in entries))
    write_anchor(repo, 2, side)
    checked = invoke(task, "check")
    assert checked.returncode == 1 and "main audit failed" in checked.stderr
