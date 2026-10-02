"""Per-invocation evidence with atomic lifecycle updates."""

from datetime import UTC, datetime
from pathlib import Path
import secrets
import time
from typing import Any

from x import gitinfo, jsonio


def utc_now() -> str:
    return datetime.now(UTC).isoformat()


class Run:
    def __init__(
        self, repo: Path, runs_root: Path, command: str, argv: list[str]
    ) -> None:
        self.id = f"{datetime.now(UTC):%Y%m%d-%H%M%S}-{command}-{secrets.token_hex(2)}"
        self.dir = runs_root / self.id
        self.dir.mkdir(parents=True)
        self._clock = time.monotonic()
        self.record: dict[str, Any] = {
            "id": self.id,
            "command": command,
            "argv": argv,
            "status": "running",
            "exit_code": None,
            "started": utc_now(),
            "finished": None,
            "duration_s": None,
            "worktree": str(repo),
            "branch": gitinfo.branch(repo),
            "commit": gitinfo.commit(repo),
            "dirty": gitinfo.is_dirty(repo),
            "results": [],
            "execs": [],
            "lock_waits": [],
            "artifacts": [],
        }
        self.save()

    def save(self) -> None:
        jsonio.save(self.dir / "record.json", self.record)

    def add_result(
        self, name: str, ok: bool, details: str = "", duration_s: float | None = None
    ) -> None:
        self.record["results"].append(
            {"name": name, "ok": ok, "duration_s": duration_s, "details": details}
        )
        self.save()

    def add_artifact(self, path: Path, label: str) -> None:
        self.record["artifacts"].append({"path": str(path.resolve()), "label": label})
        self.save()

    def add_exec(
        self,
        argv: list[str],
        log: Path,
        exit_code: int,
        duration_s: float,
        stalled: bool,
        peak_rss_mb: float,
    ) -> None:
        self.record["execs"].append(
            {
                "argv": argv,
                "log": str(log),
                "exit_code": exit_code,
                "duration_s": duration_s,
                "stalled": stalled,
                "peak_rss_mb": peak_rss_mb,
            }
        )
        self.save()

    def add_lock_wait(self, lock: str, duration_s: float) -> None:
        self.record["lock_waits"].append({"lock": lock, "duration_s": duration_s})
        self.save()

    def finish(self, exit_code: int, *, interrupted: bool = False) -> int:
        status = "passed"
        if interrupted:
            status = "interrupted"
        elif any(item["stalled"] for item in self.record["execs"]):
            status = "stalled"
        elif (
            exit_code
            or any(not item["ok"] for item in self.record["results"])
            or any(item["exit_code"] for item in self.record["execs"])
        ):
            status = "failed"
        if status != "passed" and exit_code == 0:
            exit_code = 1
        self.record.update(
            status=status,
            exit_code=exit_code,
            finished=utc_now(),
            duration_s=time.monotonic() - self._clock,
        )
        self.save()
        print(f"run {self.id}: {status}")
        return exit_code


def recent(root: Path, limit: int = 20) -> list[dict[str, Any]]:
    if not root.exists():
        return []
    paths = sorted(root.glob("*/record.json"), reverse=True)
    return [jsonio.load(path) for path in paths[:limit]]
