"""Serialize checked task-branch fast-forwards into the main worktree."""

import fcntl
import json
import os
from pathlib import Path
import subprocess

from x import gitinfo, jsonio, source
from x.context import Context
from x.scopes import load

GRANT = "x-land-grant.json"
LEDGER = "x-land-ledger.jsonl"
LEDGER_REF = "refs/x/land-ledger"


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


def ledger_anchor(repo: Path) -> tuple[str, int, str] | None:
    result = subprocess.run(
        ["git", "-C", str(repo), "rev-parse", "--verify", "--quiet", LEDGER_REF],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode == 1:
        return None
    if result.returncode:
        raise ValueError("cannot read ledger anchor")
    oid = result.stdout.strip()
    value = json.loads(gitinfo.query(repo, "cat-file", "blob", oid))
    if (
        not isinstance(value, dict)
        or set(value) != {"entries", "new"}
        or type(value["entries"]) is not int
        or value["entries"] < 1
        or not isinstance(value["new"], str)
    ):
        raise ValueError("invalid ledger anchor")
    return oid, value["entries"], value["new"]


def seed_anchor(repo: Path, tip: str) -> None:
    data = json.dumps({"entries": 1, "new": tip}).encode()
    oid = (
        subprocess.check_output(
            ["git", "-C", str(repo), "hash-object", "-w", "--stdin"], input=data
        )
        .decode()
        .strip()
    )
    gitinfo.query(repo, "update-ref", LEDGER_REF, oid, "")


def ledger_entries(
    repo: Path, tip: str, initialize: bool
) -> list[dict[str, str]] | None:
    path = common_dir(repo) / LEDGER
    anchor = ledger_anchor(repo)
    if not path.exists():
        if anchor is not None:
            raise ValueError("ledger missing while its anchor exists")
        entries = None
    else:
        entries = [json.loads(line) for line in path.read_text().splitlines()]
    if anchor is None and (
        entries is None
        or (
            len(entries) == 1
            and isinstance(entries[0], dict)
            and set(entries[0]) == {"seed"}
        )
    ):
        # Check never writes evidence. Only a checked landing gives a seed authority.
        if not initialize:
            return None
        entries = [{"seed": tip}]
        with path.open("w") as stream:
            stream.write(json.dumps(entries[0]) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        seed_anchor(repo, tip)
        anchor = ledger_anchor(repo)
    if anchor is None:
        raise ValueError("ledger anchor missing")
    if (
        not entries
        or len(entries) != anchor[1]
        or entries[-1].get("new", entries[-1].get("seed")) != anchor[2]
    ):
        raise ValueError("ledger entry count or endpoint disagrees with its anchor")
    return entries


def audit_check(ctx: Context) -> None:
    try:
        audit_main(ctx.repo)
        return
    except (ValueError, OSError, subprocess.CalledProcessError):
        # A landing may be between its ref write, ledger append and anchor update.
        with ctx.locks.held(["land.lock"], fcntl.LOCK_EX):
            audit_main(ctx.repo)


def audit_main(repo: Path, *, initialize: bool = False) -> None:
    """Read without locking; initialization requires the caller to hold land.lock."""
    tip = gitinfo.query(repo, "rev-parse", "--verify", "refs/heads/main").strip()
    history: list[str] = []
    positions: dict[str, int] = {}
    endpoint = ""
    try:
        entries = ledger_entries(repo, tip, initialize)
        if entries is None:
            return
        history = gitinfo.query(repo, "rev-list", "--first-parent", tip).splitlines()
        positions = {commit: index for index, commit in enumerate(history)}
        endpoint = entries[0]["seed"]
        frontier = positions[endpoint]
        for entry in entries[1:]:
            if entry["old"] != endpoint or not entry["run_id"] or not entry["time"]:
                raise ValueError("discontinuous landing ledger")
            new_index = positions[entry["new"]]
            if new_index > frontier:
                raise ValueError(
                    "landing range runs backwards on main's first-parent history"
                )
            frontier = new_index
            endpoint = entry["new"]
        if frontier == 0 and endpoint == tip:
            return
        unlanded = list(reversed(history[:frontier]))
        detail = "unlanded commits: " + ", ".join(unlanded)
    except (
        ValueError,
        KeyError,
        IndexError,
        TypeError,
        AttributeError,
        OSError,
    ) as error:
        detail = f"invalid landing history: {error}"
        if history:
            frontier = positions.get(endpoint, len(history))
            detail += "; unlanded commits: " + (
                ", ".join(reversed(history[:frontier])) or "(none; main rewound)"
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


def reusable_check(ctx: Context, branch: str) -> str | None:
    """Return a passed check of this branch that already proved identical content.

    The check must have kept its content unchanged from start to finish (no
    formatter rewrite), match the rebased content exactly and have passed every
    scope the rebased branch now selects. Anything else reruns the check.
    """
    if ctx.run is None:
        raise RuntimeError("landing requires a recorded command")
    snapshots = ctx.run.record["source"]["snapshots"]
    current = snapshots[ctx.run.snapshot("land:rebased")]
    required = set(load(ctx.repo).scopes_for(gitinfo.changed_files(ctx.repo)))
    # Run IDs start with a UTC timestamp, so reverse name order is newest first.
    for path in sorted(
        ctx.settings.runs_root.glob("*-check-*/record.json"), reverse=True
    ):
        try:
            record = jsonio.load(path)
        except (OSError, ValueError):
            continue
        provenance = record.get("source", {})
        completed = provenance.get("completed")
        if (
            record.get("command") != "check"
            or record.get("status") != "passed"
            or record.get("branch") != branch
            or provenance.get("initial_to_completed") != "equal"
            or completed is None
            or source.equality(provenance["snapshots"][completed], current) != "equal"
        ):
            continue
        proven = {result["name"] for result in record["results"] if result["ok"]}
        if required <= proven:
            return str(record["id"])
    return None


def merge_checked(ctx: Context, main: Path, branch: str) -> int:
    if ctx.run is None:
        raise RuntimeError("landing requires a recorded command")
    before = gitinfo.commit(main)
    if gitinfo.is_dirty(main):
        return refuse(
            ctx, "main worktree is dirty; commit or move every local change first"
        )
    after = gitinfo.commit(ctx.repo)
    first_parent_range(ctx.repo, before, after)
    audit_main(ctx.repo, initialize=True)
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
        if gitinfo.commit(main) == before:
            if ctx.exec(
                ["git", "reset", "--hard", before], cwd=main, log="land-restore"
            ):
                return refuse(ctx, "main restoration failed; stop and ask the owner")
        else:
            return refuse(ctx, "failed merge changed main; stop and ask the owner")
        return refuse(
            ctx,
            "main fast-forward refused; restored clean main; inspect land-merge.log",
        )
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
        reused = reusable_check(ctx, branch)
        if reused is not None:
            message = f"reused passed check {reused}: identical content, every selected scope passed"
            print(f"land: {message}")
            ctx.run.add_result("check", True, message)
            return merge_checked(ctx, main, branch)
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
