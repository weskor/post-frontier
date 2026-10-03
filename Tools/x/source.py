"""Immutable source identities: HEAD blobs plus actual, nonignored worktree delta."""

from collections.abc import Iterator
from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
from typing import Any

from x import jsonio
from x.source_filters import baseline_filters, lfs_pointer

ALGORITHM = "worktree-blobs-v1"


def git_command(repo: Path, *args: str) -> list[str]:
    # Dirty LFS discovery must not invoke git-lfs clean (which writes its object
    # cache). Cached stat entries still avoid reading unchanged hydrated assets.
    return [
        "git",
        "-C",
        str(repo),
        "-c",
        "filter.lfs.clean=",
        "-c",
        "filter.lfs.process=",
        "-c",
        "filter.lfs.required=false",
        *args,
    ]


def git(repo: Path, *args: str, env: dict[str, str] | None = None) -> bytes:
    return subprocess.check_output(
        git_command(repo, *args),
        env=env,
        stderr=subprocess.PIPE,
    )


def names(data: bytes) -> list[str]:
    return [
        name.decode("utf-8", "surrogateescape") for name in data.split(b"\0") if name
    ]


def retain_diff(
    repo: Path, path: Path, head: str, environment: dict[str, str], added: set[str]
) -> None:
    flags = ("--binary", "--no-renames", "--no-ext-diff", "--no-textconv")
    with path.open("wb") as stream:
        subprocess.run(
            git_command(repo, "diff", *flags, head),
            env=environment,
            stdout=stream,
            stderr=subprocess.PIPE,
            check=True,
        )
        # Staged additions are absent from the private HEAD index. Retain their
        # actual bytes as new-file patches without hash-object -w or git add.
        for relative in sorted(added):
            file = repo / relative
            if not file.exists() and not file.is_symlink():
                continue
            arguments = git_command(
                repo, "diff", "--no-index", *flags, "--", "/dev/null", relative
            )
            result = subprocess.run(
                arguments,
                env=environment,
                stdout=stream,
                stderr=subprocess.PIPE,
                check=False,
            )
            if result.returncode not in (0, 1):
                raise subprocess.CalledProcessError(
                    result.returncode, arguments, stderr=result.stderr
                )


def file_identity(path: Path, object_format: str, lfs: bool) -> tuple[str, str, int]:
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode):
        content = os.fsencode(os.readlink(path))
        size = len(content)
        blob = hashlib.new(object_format, f"blob {size}\0".encode() + content)
        return "120000", blob.hexdigest(), size
    if not stat.S_ISREG(metadata.st_mode):
        raise ValueError(f"unsupported source file: {path}")
    size = metadata.st_size
    blob = hashlib.new(object_format, f"blob {size}\0".encode())
    raw = hashlib.sha256() if lfs else None
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            blob.update(chunk)
            if raw is not None:
                raw.update(chunk)
    mode = "100755" if metadata.st_mode & stat.S_IXUSR else "100644"
    # An unsmudged LFS pointer is actual pointer content, not the hydrated asset.
    pointer = lfs and size < 1024 and lfs_pointer(path.read_bytes()) is not None
    identity = (
        "sha256:" + raw.hexdigest()
        if raw is not None and not pointer
        else blob.hexdigest()
    )
    return mode, identity, size


def baseline_entries(repo: Path, head: str) -> dict[str, tuple[str, str]]:
    flags = git(repo, "ls-files", "-v", "-z")
    if any(
        flag and (flag[:1] == b"S" or flag[:1].islower()) for flag in flags.split(b"\0")
    ):
        raise ValueError(
            "sparse/assume-unchanged index entries make source identity unknown"
        )
    entries: dict[str, tuple[str, str]] = {}
    for tree_item in git(repo, "ls-tree", "-rz", head).split(b"\0"):
        if not tree_item:
            continue
        tree_metadata, tree_name = tree_item.split(b"\t", 1)
        mode, kind, oid = tree_metadata.decode().split()
        if kind != "blob":
            raise ValueError("submodule source identity is unknown")
        entries[tree_name.decode("utf-8", "surrogateescape")] = (mode, oid)
    return entries


@contextmanager
def private_index(repo: Path, head: str) -> Iterator[dict[str, str]]:
    with tempfile.TemporaryDirectory(prefix="x-source-") as temporary:
        index = Path(temporary) / "index"
        original = Path(
            os.fsdecode(git(repo, "rev-parse", "--git-path", "index")).strip()
        )
        if not original.is_absolute():
            original = repo / original
        if original.exists():
            shutil.copyfile(original, index)
        environment = {**os.environ, "GIT_INDEX_FILE": str(index)}
        # Retain unchanged stat entries, but explicitly ignore the worktree when
        # resetting this private index: staged/unstaged differences are expected.
        git(repo, "read-tree", "-m", "-i", head, env=environment)
        yield environment


def worktree_delta(
    repo: Path,
    entries: dict[str, tuple[str, str]],
    changed: set[str],
    untracked: list[str],
    object_format: str,
    lfs: set[str],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    manifest: list[dict[str, Any]] = []
    delta: list[dict[str, Any]] = []
    for relative in sorted(changed | set(untracked)):
        file_path = repo / relative
        if not file_path.exists() and not file_path.is_symlink():
            entries.pop(relative, None)
            delta.append({"path": relative, "deleted": True})
            continue
        mode, identity, size = file_identity(file_path, object_format, relative in lfs)
        entries[relative] = (mode, identity)
        delta_item = {
            "path": relative,
            "mode": mode,
            "content_hash": identity,
            "size": size,
        }
        delta.append(delta_item)
        if relative in untracked:
            manifest.append(delta_item)
    return delta, manifest


def fingerprint(repo: Path, entries: dict[str, tuple[str, str]]) -> str:
    digest = hashlib.sha256()
    for relative, (_, oid) in sorted(entries.items()):
        file_metadata = (repo / relative).lstat()
        mode = (
            "120000"
            if stat.S_ISLNK(file_metadata.st_mode)
            else "100755"
            if file_metadata.st_mode & stat.S_IXUSR
            else "100644"
        )
        encoded_name = relative.encode("utf-8", "surrogateescape")
        digest.update(len(encoded_name).to_bytes(8, "big"))
        digest.update(encoded_name)
        digest.update(f"{mode}\0{oid}\0".encode())
    return digest.hexdigest()


def reuse_artifacts(
    snapshot: dict[str, Any],
    retained: list[dict[str, Any]] | None,
    untracked: list[str],
) -> bool:
    for previous in retained or []:
        if (
            equality(snapshot, previous) == "equal"
            and previous.get("head") == snapshot["head"]
            and previous.get("delta") == snapshot["delta"]
            and previous.get("untracked_paths") == untracked
            and previous.get("tracked_diff")
        ):
            snapshot.update(
                tracked_diff=previous["tracked_diff"],
                untracked_manifest=previous["untracked_manifest"],
                untracked_paths=untracked,
            )
            return True
    return False


def capture_content(
    repo: Path,
    snapshot: dict[str, Any],
    directory: Path | None,
    retained: list[dict[str, Any]] | None,
) -> None:
    head = snapshot["head"]
    entries = baseline_entries(repo, head)
    untracked = names(git(repo, "ls-files", "--others", "--exclude-standard", "-z"))
    added = set(names(git(repo, "ls-files", "--cached", "-z"))) - entries.keys()
    lfs = baseline_filters(repo, entries, set(untracked) | added)
    object_format = git(repo, "rev-parse", "--show-object-format").decode().strip()
    snapshot["object_format"] = object_format
    with private_index(repo, head) as environment:
        changed = set(
            names(
                git(
                    repo,
                    "diff",
                    "--name-only",
                    "--no-renames",
                    "--no-ext-diff",
                    "--no-textconv",
                    "-z",
                    head,
                    env=environment,
                )
            )
        )
        delta, manifest = worktree_delta(
            repo, entries, changed | added, untracked, object_format, lfs
        )
        snapshot.update(content_id=fingerprint(repo, entries), delta=delta)
        if directory is not None and not reuse_artifacts(snapshot, retained, untracked):
            directory.mkdir(parents=True, exist_ok=False)
            retain_diff(repo, directory / "tracked.diff", head, environment, added)
            jsonio.save(directory / "untracked.json", manifest)
            snapshot.update(
                tracked_diff=str((directory / "tracked.diff").resolve()),
                untracked_manifest=str((directory / "untracked.json").resolve()),
                untracked_paths=untracked,
            )


def capture(
    repo: Path,
    directory: Path | None = None,
    *,
    retained: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Observe HEAD plus delta without updating the user's index or Git objects.

    Unchanged blobs are read from HEAD, not rehashed from the worktree. Git's stat
    cache detects the delta. Boundaries are not an atomic filesystem transaction
    or evidence that a process tested its final output.
    """
    snapshot: dict[str, Any] = {"algorithm": ALGORITHM, "content_id": None}
    try:
        snapshot["head"] = git(repo, "rev-parse", "HEAD").decode().strip()
        capture_content(repo, snapshot, directory, retained)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        reason = str(error)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            reason += ": " + os.fsdecode(error.stderr).strip()
        snapshot.update(content_id=None, unknown=reason)
    return snapshot


def equality(left: dict[str, Any], right: dict[str, Any]) -> str:
    if (
        left.get("algorithm") != ALGORITHM
        or right.get("algorithm") != ALGORITHM
        or not left.get("content_id")
        or not right.get("content_id")
    ):
        return "unknown"
    return "equal" if left["content_id"] == right["content_id"] else "different"
