"""`./x land` step: regenerate binaries whose text sources changed, commit them apart."""

import fcntl
from pathlib import Path
import shlex
import sys

from x import gitinfo
from x.content import generated
from x.context import Context

SUBJECT = "Regenerate binary assets (./x land)"


def committed_changes(repo: Path) -> list[str]:
    diff = gitinfo.query(repo, "diff", "--name-only", "--no-renames", "-z", "main", "HEAD")
    return [path for path in diff.split("\0") if path]


def has_regeneration_commits(repo: Path) -> bool:
    subjects = gitinfo.query(repo, "log", "--format=%s", "main..HEAD").splitlines()
    return SUBJECT in subjects


def rebase_command(repo: Path) -> tuple[list[str], dict[str, str]]:
    """Rebase onto main, dropping earlier regeneration commits: land makes fresh ones.

    A kept commit would replay binaries and conflict when main moved the same assets.
    """
    if not has_regeneration_commits(repo):
        return ["git", "rebase", "main"], {}
    script = Path(__file__).resolve().with_name("drop_commits.py")
    editor = f"{shlex.quote(sys.executable)} {shlex.quote(str(script))}"
    return ["git", "rebase", "-i", "main"], {
        "GIT_SEQUENCE_EDITOR": editor,
        "X_DROP_SUBJECT": SUBJECT,
    }


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
    with ctx.locks.held(["generator.lock"], fcntl.LOCK_EX):
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

