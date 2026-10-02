"""Serialize checked task-branch fast-forwards into the main worktree."""

import fcntl
from pathlib import Path
import subprocess

from x import gitinfo
from x.commands.check import check
from x.context import Context

MARKER = "X_LAND"


def ensure_hooks(repo: Path) -> None:
    common = (
        repo / gitinfo.query(repo, "rev-parse", "--git-common-dir").strip()
    ).resolve()
    hooks = str(common.parent / "Tools/hooks")
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


def land(ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("landing requires a recorded command")
    with ctx.locks.held(["land.lock"], fcntl.LOCK_EX):
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
        if not rebase(ctx):
            return 1
        result = check(ctx)
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
        before = gitinfo.commit(main)
        if ctx.exec(
            ["git", "merge", "--ff-only", branch],
            cwd=main,
            env={MARKER: "1"},
            log="land-merge",
        ):
            details = (ctx.run.dir / "land-merge.log").read_text(errors="replace")
            return refuse(
                ctx,
                "main fast-forward refused; local changes that would "
                "be overwritten must be committed or moved first:\n" + details,
            )
        landed = f"{before}..{gitinfo.commit(main)}"
        ctx.run.add_result("land", True, landed)
        print(f"landed {landed}; run {ctx.run.id}")
        return 0
