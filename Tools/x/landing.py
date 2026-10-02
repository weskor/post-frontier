"""Serialize checked task-branch fast-forwards into the main worktree."""

import fcntl
import json
import os
from pathlib import Path
import subprocess

from x import gitinfo, jsonio
from x.context import Context

GRANT = "x-land-grant.json"
LEDGER = "x-land-ledger.jsonl"


def common_dir(repo: Path) -> Path:
    return (
        repo / gitinfo.query(repo, "rev-parse", "--git-common-dir").strip()
    ).resolve()


def ensure_hooks(repo: Path) -> None:
    common = common_dir(repo)
    directory = common.parent / "Tools/hooks"
    hooks = str(directory)
    try:
        names = gitinfo.query(
            repo, "ls-tree", "--name-only", "main:Tools/hooks"
        ).splitlines()
    except subprocess.CalledProcessError as error:
        raise ValueError(
            f"hook integrity failed: {directory}; stop and ask the owner"
        ) from error
    if not directory.is_dir():
        raise ValueError(f"hook integrity failed: {directory}; stop and ask the owner")
    for name in sorted(set(names) | {path.name for path in directory.iterdir()}):
        path = directory / name
        expected = subprocess.run(
            ["git", "-C", str(repo), "show", f"main:Tools/hooks/{name}"],
            capture_output=True,
            check=False,
        )
        if (
            expected.returncode
            or not path.is_file()
            or path.is_symlink()
            or path.read_bytes() != expected.stdout
            or not path.stat().st_mode & 0o111
            or not os.access(path, os.X_OK)
        ):
            raise ValueError(f"hook integrity failed: {path}; stop and ask the owner")
    current = subprocess.run(
        ["git", "-C", str(repo), "config", "--local", "--get", "core.hooksPath"],
        capture_output=True,
        text=True,
        check=False,
    )
    if current.returncode not in (0, 1):
        raise ValueError(current.stderr.strip())
    if current.stdout.strip() != hooks:
        gitinfo.query(repo, "config", "--local", "core.hooksPath", hooks)


def first_parent_range(repo: Path, old: str, new: str) -> list[str]:
    history = gitinfo.query(repo, "rev-list", "--first-parent", new).splitlines()
    if old not in history:
        raise ValueError(f"{old} is not on {new}'s first-parent history")
    return list(reversed(history[: history.index(old)]))


def audit_main(repo: Path) -> None:
    """Call only under land.lock, including when seeding the append-only ledger."""
    path = common_dir(repo) / LEDGER
    tip = gitinfo.query(repo, "rev-parse", "--verify", "refs/heads/main").strip()
    if not path.exists():
        with path.open("x") as stream:
            stream.write(json.dumps({"seed": tip}) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        return
    expected: list[str] = []
    seed = ""
    endpoint = ""
    try:
        entries = [json.loads(line) for line in path.read_text().splitlines()]
        seed = endpoint = entries[0]["seed"]
        for entry in entries[1:]:
            if entry["old"] != endpoint or not entry["run_id"] or not entry["time"]:
                raise ValueError("discontinuous landing ledger")
            expected.extend(first_parent_range(repo, endpoint, entry["new"]))
            endpoint = entry["new"]
        actual = first_parent_range(repo, seed, tip)
        if actual == expected and endpoint == tip:
            return
        expected_set = set(expected)
        unlanded = [commit for commit in actual if commit not in expected_set]
        detail = "unlanded commits: " + (", ".join(unlanded) or "(none; main rewound)")
    except (ValueError, KeyError, IndexError, TypeError) as error:
        detail = f"invalid landing history: {error}"
        if seed:
            history = gitinfo.query(
                repo, "rev-list", "--first-parent", tip
            ).splitlines()
            detail += "; unlanded commits: " + ", ".join(
                commit
                for commit in history
                if commit != seed and commit not in expected
            )
    raise ValueError(f"main audit failed: {detail}; stop and ask the owner")


def refuse(ctx: Context, message: str) -> int:
    print(f"land: {message}")
    if ctx.run is not None:
        ctx.run.add_result("land", False, message)
    return 1


def rebase(ctx: Context) -> bool:
    if ctx.exec(["git", "rebase", "main"], log="land-rebase") == 0:
        return True
    conflicts = gitinfo.query(ctx.repo, "diff", "--name-only", "--diff-filter=U", "-z")
    paths = [path for path in conflicts.split("\0") if path]
    active = any(
        (
            ctx.repo / gitinfo.query(ctx.repo, "rev-parse", "--git-path", state).strip()
        ).exists()
        for state in ("rebase-merge", "rebase-apply")
    )
    if active:
        ctx.exec(["git", "rebase", "--abort"], log="land-rebase-abort")
    if paths:
        refuse(ctx, f"rebase conflict; rebase aborted: {', '.join(paths)}")
    else:
        refuse(ctx, "rebase failed; inspect land-rebase.log")
    return False


def merge_checked(ctx: Context, main: Path, branch: str) -> int:
    if ctx.run is None:
        raise RuntimeError("landing requires a recorded command")
    before = gitinfo.commit(main)
    if gitinfo.is_dirty(main):
        return refuse(
            ctx, "main worktree is dirty; commit or move every local change first"
        )
    audit_main(ctx.repo)
    after = gitinfo.commit(ctx.repo)
    first_parent_range(ctx.repo, before, after)
    grant = common_dir(ctx.repo) / GRANT
    jsonio.save(
        grant, {"old": before, "new": after, "run_id": ctx.run.id, "pid": os.getpid()}
    )
    try:
        merged = (
            ctx.exec(["git", "merge", "--ff-only", branch], cwd=main, log="land-merge")
            == 0
        )
    finally:
        grant.unlink(missing_ok=True)
        grant.with_suffix(".json.prepared").unlink(missing_ok=True)
    if not merged:
        return refuse(ctx, "main fast-forward refused; inspect land-merge.log")
    if gitinfo.commit(main) != after:
        return refuse(ctx, "main changed unexpectedly; stop and ask the owner")
    audit_main(ctx.repo)
    landed = f"{before}..{after}"
    ctx.run.add_result("land", True, landed)
    print(f"landed {landed}; run {ctx.run.id}")
    return 0


def land(ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("landing requires a recorded command")
    with ctx.locks.held(["land.lock"], fcntl.LOCK_EX):
        audit_main(ctx.repo)
        branch = gitinfo.branch(ctx.repo)
        if branch == "main":
            return refuse(ctx, "run ./x land from a task worktree, not main")
        if not branch.startswith("task/"):
            return refuse(ctx, "run ./x land on a task/<slug> branch")
        if gitinfo.is_dirty(ctx.repo):
            return refuse(ctx, "task worktree is dirty; commit changes first")
        if gitinfo.query(ctx.repo, "rev-list", "--count", "main..HEAD").strip() == "0":
            return refuse(ctx, "branch has no commits beyond main")
        try:
            main = gitinfo.main_worktree(ctx.repo)
        except ValueError as error:
            return refuse(ctx, str(error))
        if gitinfo.is_dirty(main):
            return refuse(
                ctx, "main worktree is dirty; commit or move every local change first"
            )
        if not rebase(ctx):
            return 1
        from x.commands.check import check

        result = check(ctx, audit=False)
        if result.reformatted:
            return refuse(
                ctx,
                "check reformatted files; commit them before landing: "
                + ", ".join(map(str, result.reformatted)),
            )
        if not result.ok:
            return refuse(ctx, "check failed; fix and commit before landing")
        if gitinfo.is_dirty(ctx.repo):
            return refuse(ctx, "check left task worktree dirty; commit changes first")
        return merge_checked(ctx, main, branch)
