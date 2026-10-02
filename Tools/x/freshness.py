"""Freshness depends on selected file paths and bytes, never mtimes."""

import fnmatch
import hashlib
from datetime import UTC, datetime
from pathlib import Path

from x import gitinfo, jsonio
from x.settings import Settings, load


class Freshness:
    """Worktree-bound version of the public module functions."""

    def __init__(self, repo: Path, settings: Settings) -> None:
        self.repo = repo
        self.settings = settings

    def current_hash(self, kind: str) -> str:
        patterns = self.settings.freshness[kind]
        paths = set(
            gitinfo.query(
                self.repo, "ls-files", "-co", "--exclude-standard", "-z"
            ).split("\0")
        )
        digest = hashlib.sha256()
        for relative in sorted(path for path in paths if path):
            path = self.repo / relative
            if (
                not any(fnmatch.fnmatchcase(relative, pattern) for pattern in patterns)
                or not path.is_file()
            ):
                continue
            name = relative.encode("utf-8", "surrogateescape")
            content = path.read_bytes()
            digest.update(len(name).to_bytes(8, "big"))
            digest.update(name)
            digest.update(len(content).to_bytes(8, "big"))
            digest.update(content)
        return digest.hexdigest()

    def stamp(self, kind: str, run_id: str, input_hash: str | None = None) -> None:
        """Stamp the inputs actually built, or snapshot current inputs if omitted."""
        jsonio.save(
            self.repo / "Intermediate/x-stamps" / f"{kind}.json",
            {
                "hash": self.current_hash(kind) if input_hash is None else input_hash,
                "commit": gitinfo.commit(self.repo),
                "run_id": run_id,
                "time": datetime.now(UTC).isoformat(),
            },
        )

    def is_fresh(self, kind: str) -> bool:
        path = self.repo / "Intermediate/x-stamps" / f"{kind}.json"
        if not path.exists():
            return False
        return bool(jsonio.load(path).get("hash") == self.current_hash(kind))


def current_hash(repo: Path, kind: str) -> str:
    return Freshness(repo, load(repo)).current_hash(kind)


def stamp(repo: Path, kind: str, run_id: str, input_hash: str | None = None) -> None:
    Freshness(repo, load(repo)).stamp(kind, run_id, input_hash)


def is_fresh(repo: Path, kind: str) -> bool:
    return Freshness(repo, load(repo)).is_fresh(kind)
