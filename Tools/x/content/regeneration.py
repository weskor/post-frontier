"""`./x land` step: regenerate binaries whose text sources changed, commit them apart."""

import contextlib
from pathlib import Path
import shlex
import subprocess
import sys

from x import gitinfo
from x.content import generated
from x.context import Context

SUBJECT = "Regenerate binary assets (./x land)"


def committed_changes(repo: Path) -> list[str]:
    diff = gitinfo.query(
        repo, "diff", "--name-only", "--no-renames", "-z", "main", "HEAD"
    )
    return [path for path in diff.split("\0") if path]


def commit_paths(repo: Path, sha: str) -> list[str]:
    output = gitinfo.query(
        repo,
        "diff-tree",
        "--no-commit-id",
        "--name-only",
        "-r",
        "--no-renames",
        "-z",
        sha,
    )
    return [path for path in output.split("\0") if path]


def branch_commits(repo: Path) -> list[str]:
    return gitinfo.query(repo, "rev-list", "--no-merges", "main..HEAD").split()


def is_regeneration(repo: Path, mapping: generated.GeneratedMap, sha: str) -> bool:
    """Land's own commit: its subject, and every path one of the mapped outputs."""
    if gitinfo.query(repo, "log", "-1", "--format=%s", sha).strip() != SUBJECT:
        return False
    outputs = tuple(pattern for entry in mapping.entries for pattern in entry.outputs)
    paths = commit_paths(repo, sha)
    return bool(paths) and all(generated.matches_any(outputs, path) for path in paths)


def foreign_output_changes(repo: Path) -> str | None:
    """Refusal naming branch commits, other than land's, that change generated outputs.

    Only main..HEAD is inspected, so binaries already on main (landed by another
    branch's land) never trip it.
    """
    mapping = generated.load(repo)
    maps = [mapping]
    with contextlib.suppress(subprocess.CalledProcessError):
        main_map = gitinfo.query(repo, "show", f"main:{generated.MAP_PATH}")
        maps.append(generated.parse(main_map))
    found: list[str] = []
    for sha in branch_commits(repo):
        if is_regeneration(repo, mapping, sha):
            continue
        found += [
            f"{sha[:9]}: {message}"
            for message in generated.rejections(maps, commit_paths(repo, sha))
        ]
    if not found:
        return None
    return (
        "branch commits change generated binaries (commit text only; land "
        "regenerates them): " + "; ".join(found)
    )


def rebase_command(repo: Path) -> tuple[list[str], dict[str, str]]:
    """Rebase onto main, dropping regeneration commits a crashed land left behind.

    Land removes its regeneration commit whenever it refuses, so only a crash
    leaves one. A kept commit would replay binaries and conflict when main moved
    the same assets; land makes a fresh one.
    """
    mapping = generated.load(repo)
    stale = [sha for sha in branch_commits(repo) if is_regeneration(repo, mapping, sha)]
    if not stale:
        return ["git", "rebase", "main"], {}
    script = Path(__file__).resolve().with_name("drop_commits.py")
    editor = f"{shlex.quote(sys.executable)} {shlex.quote(str(script))}"
    return ["git", "-c", "rebase.autoSquash=false", "rebase", "-i", "main"], {
        "GIT_SEQUENCE_EDITOR": editor,
        "X_DROP_SHAS": " ".join(stale),
    }


def undo_regeneration(repo: Path, head: str) -> None:
    """Remove land's regeneration commit after a refusal, keeping other local edits.

    The worktree can hold check-applied formatting that the worker must commit;
    stash it around the reset so only the regenerated binaries go away.
    """
    if gitinfo.commit(repo) == head:
        return
    stashed = gitinfo.is_dirty(repo)
    if stashed:
        gitinfo.query(repo, "stash", "push", "--quiet", "--include-untracked")
    gitinfo.query(repo, "reset", "--hard", head)
    if stashed:
        gitinfo.query(repo, "stash", "pop", "--quiet")
    print("land: removed the regeneration commit; the next land regenerates")


def restore(repo: Path) -> None:
    """The worktree was clean before regeneration, so everything dirty is generator output."""
    gitinfo.query(repo, "reset", "--hard", "HEAD")
    gitinfo.query(repo, "clean", "-fdq")


def dirty_paths(repo: Path) -> list[str]:
    status = gitinfo.query(repo, "status", "--porcelain", "-z", "--untracked-files=all")
    return [field[3:] for field in status.split("\0") if field]


def regenerate(ctx: Context) -> str | None:
    """Return a refusal message, or None when nothing is wrong (also when nothing ran)."""
    if ctx.run is None:
        raise RuntimeError("regeneration requires a recorded command")
    mapping = generated.load(ctx.repo)
    entries = generated.triggered(mapping, committed_changes(ctx.repo))
    if not entries:
        ctx.run.add_result("regenerate", True, "no generated sources changed")
        print("land: no generated sources changed; nothing to regenerate")
        return None
    gens = list(dict.fromkeys(name for entry in entries for name in entry.gen))
    # Entries sharing a generator are rewritten together, so all may change.
    writable = tuple(
        pattern
        for entry in mapping.entries
        if set(entry.gen) & set(gens)
        for pattern in entry.outputs
    )
    print(
        f"land: regenerating {', '.join(entry.name for entry in entries)} "
        f"via ./x gen {', '.join(gens)}"
    )
    for name in gens:
        code = ctx.exec(
            [sys.executable, ctx.repo / "x", "gen", name], log=f"regenerate-{name}"
        )
        if code != 0:
            restore(ctx.repo)
            return f"./x gen {name} failed; inspect regenerate-{name}.log"
    changed = dirty_paths(ctx.repo)
    stray = [path for path in changed if not generated.matches_any(writable, path)]
    if stray:
        restore(ctx.repo)
        return "generators wrote outside their declared outputs: " + ", ".join(stray)
    if not changed:
        ctx.run.add_result("regenerate", True, "outputs already match their sources")
        print("land: regenerated outputs are identical; no commit needed")
        return None
    gitinfo.query(ctx.repo, "add", "-A")
    message = f"{SUBJECT}\n\nSources changed for: {', '.join(e.name for e in entries)}\nGenerators: {', '.join(gens)}\n"
    if ctx.exec(["git", "commit", "-m", message], log="regenerate-commit") != 0:
        restore(ctx.repo)
        return "regeneration commit failed; inspect regenerate-commit.log"
    ctx.run.add_result("regenerate", True, f"committed {len(changed)} files")
    print(f"land: committed {len(changed)} regenerated files")
    return None
