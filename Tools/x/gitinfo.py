"""Git queries preserve deleted paths and unusual filenames."""

from pathlib import Path
import subprocess


def query(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args]).decode(
        "utf-8", "surrogateescape"
    )


def branch(repo: Path) -> str:
    return query(repo, "rev-parse", "--abbrev-ref", "HEAD").strip()


def commit(repo: Path) -> str:
    return query(repo, "rev-parse", "HEAD").strip()


def is_dirty(repo: Path) -> bool:
    return bool(query(repo, "status", "--porcelain", "--untracked-files=normal"))


def merge_base(repo: Path, base: str = "main") -> str:
    return query(repo, "merge-base", "HEAD", base).strip()


def changed_files(repo: Path, base: str = "main") -> list[Path]:
    ancestor = merge_base(repo, base)
    paths: set[str] = set()
    for args in (
        ("diff", "--name-only", "--no-renames", "-z", ancestor, "HEAD"),
        ("diff", "--name-only", "--no-renames", "-z", "--cached"),
        ("diff", "--name-only", "--no-renames", "-z"),
        ("ls-files", "--others", "--exclude-standard", "-z"),
    ):
        paths.update(path for path in query(repo, *args).split("\0") if path)
    return [Path(path) for path in sorted(paths)]


def main_worktree(repo: Path) -> Path:
    for record in query(repo, "worktree", "list", "--porcelain", "-z").split("\0\0"):
        fields = record.split("\0")
        if "branch refs/heads/main" in fields:
            return Path(
                next(
                    field.removeprefix("worktree ")
                    for field in fields
                    if field.startswith("worktree ")
                )
            )
    raise ValueError("no worktree has main checked out")
