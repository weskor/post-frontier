"""Read tiny LFS pointers, not unchanged content assets; reject opaque transforms."""

import os
from pathlib import Path
import subprocess

# Paths whose worktree bytes git last reported canonical, keyed by the stat that
# was observed before the check. Any write changes ctime, forcing a recheck.
_canonical: dict[Path, tuple[int, int, int, int]] = {}


def lfs_pointer(content: bytes) -> tuple[str, int] | None:
    try:
        lines = content.decode().splitlines()
        if not lines or lines[0] != "version https://git-lfs.github.com/spec/v1":
            return None
        oid = next(
            (
                line.removeprefix("oid sha256:")
                for line in lines
                if line.startswith("oid sha256:")
            ),
            "",
        )
        length = int(
            next(
                (
                    line.removeprefix("size ")
                    for line in lines
                    if line.startswith("size ")
                ),
                "-1",
            )
        )
        if (
            length < 0
            or len(oid) != 64
            or any(character not in "0123456789abcdef" for character in oid)
        ):
            return None
        return oid, length
    except (UnicodeError, ValueError):
        return None


def lfs_paths(
    repo: Path, entries: dict[str, tuple[str, str]], added: set[str]
) -> set[str]:
    paths = sorted(entries.keys() | added)
    attributes = subprocess.check_output(
        [
            "git",
            "-C",
            str(repo),
            "check-attr",
            "-z",
            "--stdin",
            "filter",
            "working-tree-encoding",
            "ident",
        ],
        input=b"".join(os.fsencode(path) + b"\0" for path in paths),
        stderr=subprocess.PIPE,
    ).split(b"\0")
    lfs: set[str] = set()
    for index in range(0, len(attributes) - 1, 3):
        attribute_path, attribute, value = attributes[index : index + 3]
        name = os.fsdecode(attribute_path)
        if entries.get(name, ("", ""))[0] == "120000":
            continue
        if value in (b"unspecified", b"unset"):
            continue
        if attribute == b"filter" and value == b"lfs":
            lfs.add(name)
        else:
            raise ValueError(
                f"unsupported content transform: {name}: {os.fsdecode(attribute)}={os.fsdecode(value)}"
            )
    return lfs


def check_line_endings(repo: Path, text_paths: list[str]) -> None:
    # LFS assets are explicitly binary. Scanning their hydrated bytes for line
    # endings on every exec would turn metadata-only provenance into a full read.
    pending: dict[str, tuple[Path, tuple[int, int, int, int]]] = {}
    for name in text_paths:
        path = repo / name
        try:
            metadata = path.lstat()
        except FileNotFoundError:
            continue
        key = (
            metadata.st_ino,
            metadata.st_size,
            metadata.st_mtime_ns,
            metadata.st_ctime_ns,
        )
        if _canonical.get(path) != key:
            pending[name] = (path, key)
    if not pending:
        return
    endings = subprocess.check_output(
        ["git", "-C", str(repo), "ls-files", "--eol", "-z", "--", *pending],
        stderr=subprocess.PIPE,
    )
    for eol_item in endings.split(b"\0"):
        if not eol_item:
            continue
        eol_metadata, eol_path = eol_item.split(b"\t", 1)
        if b"w/crlf" in eol_metadata or b"w/mixed" in eol_metadata:
            raise ValueError(
                f"noncanonical worktree line endings: {os.fsdecode(eol_path)}"
            )
    for path, key in pending.values():
        _canonical[path] = key


def hydrate_baseline(
    repo: Path, entries: dict[str, tuple[str, str]], baseline: list[str]
) -> None:
    if not baseline:
        return
    process = subprocess.run(
        ["git", "-C", str(repo), "cat-file", "--batch"],
        input=b"".join(entries[name][1].encode() + b"\n" for name in baseline),
        capture_output=True,
        check=True,
    )
    offset = 0
    for name in baseline:
        end = process.stdout.index(b"\n", offset)
        size = int(process.stdout[offset:end].split()[-1])
        pointer = process.stdout[end + 1 : end + 1 + size]
        offset = end + size + 2
        identity = lfs_pointer(pointer)
        if identity is None:
            raise ValueError(f"invalid HEAD LFS pointer: {name}")
        oid, length = identity
        file_path = repo / name
        # Metadata avoids reading unchanged large assets. Probe only tiny files
        # where hydrated size could coincide with pointer size.
        hydrated = file_path.is_file() and file_path.stat().st_size == length
        if (
            hydrated
            and length < 1024
            and lfs_pointer(file_path.read_bytes()) is not None
        ):
            hydrated = False
        if hydrated:
            mode, _ = entries[name]
            entries[name] = (mode, "sha256:" + oid)


def baseline_filters(
    repo: Path, entries: dict[str, tuple[str, str]], added: set[str]
) -> set[str]:
    lfs = lfs_paths(repo, entries, added)
    check_line_endings(repo, sorted(entries.keys() - lfs))
    hydrate_baseline(repo, entries, sorted(name for name in lfs if name in entries))
    return lfs
