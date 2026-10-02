"""Runner hash/stamp snapshots used by slow harnesses, never mtime heuristics."""

from functools import lru_cache
import hashlib
from pathlib import Path
from typing import Any

from x import freshness, jsonio


@lru_cache(maxsize=32)
def _digest(path: Path, identity: tuple[int, int, int, int]) -> str:
    # Identity is a cache key only. Freshness/mutation decisions compare hashes.
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def digest(path: Path) -> str:
    stat = path.stat()
    return _digest(path, (stat.st_dev, stat.st_ino, stat.st_size, stat.st_ctime_ns))


def package_snapshot(repo: Path) -> dict[str, Any]:
    root = repo / "Saved/Packages/development/latest"
    try:
        record = jsonio.load(root / "package.json")
    except (OSError, ValueError) as error:
        raise RuntimeError("package missing or invalid; run ./x package") from error
    if record.get("package_hash") != freshness.current_hash(repo, "package"):
        raise RuntimeError("package stale; run ./x package")
    if not (root / "CoopRTS/Binaries/Linux/CoopRTS").is_file():
        raise RuntimeError("package binary missing; run ./x package")
    content = sorted(
        path for path in (root / "CoopRTS/Content/Paks").glob("*") if path.is_file()
    )
    if not content:
        raise RuntimeError("packaged content missing; run ./x package")
    artifacts = [root / "CoopRTS/Binaries/Linux/CoopRTS", *content]
    hashes = {
        str(path.relative_to(root)): digest(path)
        for path in artifacts
        if path.is_file()
    }
    return {"root": str(root.resolve()), "record": record, "artifacts": hashes}


def editor_snapshot(repo: Path) -> dict[str, Any]:
    record = jsonio.load(repo / "Intermediate/x-stamps/editor.json")
    if record.get("hash") != freshness.current_hash(repo, "editor"):
        raise RuntimeError("editor inputs changed during run; run ./x build")
    return {
        "stamp": record,
        "module": digest(repo / "Binaries/Linux/libUnrealEditor-CoopRTS.so"),
    }
