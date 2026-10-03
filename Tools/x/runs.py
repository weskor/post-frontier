"""Per-invocation evidence with atomic lifecycle updates."""

from collections.abc import Iterator
from contextlib import contextmanager
from datetime import UTC, datetime
from pathlib import Path
import secrets
import threading
import time
from typing import Any

from x import gitinfo, jsonio, source


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
        self.repo = repo
        # Scopes run on worker threads; every record mutation and save holds this.
        self._lock = threading.RLock()
        self._scope = threading.local()
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
            "source": {"snapshots": []},
        }
        self.record["source"]["initial"] = self.snapshot("initial")
        self.save()

    def save(self) -> None:
        with self._lock:
            jsonio.save(self.dir / "record.json", self.record)

    def snapshot(self, label: str) -> int:
        with self._lock:
            snapshots = self.record["source"]["snapshots"]
            index = len(snapshots)
            snapshot = source.capture(
                self.repo, self.dir / "source" / f"{index:04d}", retained=snapshots
            )
            snapshots.append({**snapshot, "label": label, "time": utc_now()})
            self.save()
            return index

    @contextmanager
    def collecting_execs(self) -> Iterator[list[int]]:
        """Collect the indices of execs recorded by the current thread."""
        indices: list[int] = []
        self._scope.execs = indices
        try:
            yield indices
        finally:
            del self._scope.execs

    def source_interval(self, before: int, after: int) -> dict[str, Any]:
        snapshots = self.record["source"]["snapshots"]
        return {
            "before": before,
            "after": after,
            "content": source.equality(snapshots[before], snapshots[after]),
        }

    def add_exec_inputs(self, log: str, kind: str, before: str, after: str) -> None:
        """Retain the exact hashes used by an existing freshness mutation guard."""
        with self._lock:
            for execution in reversed(self.record["execs"]):
                if Path(execution["log"]).stem == log:
                    execution.setdefault("input_hashes", {})[kind] = {
                        "before": before,
                        "after": after,
                        "equal": before == after,
                    }
                    self.save()
                    return

    def add_result(
        self,
        name: str,
        ok: bool,
        details: str = "",
        duration_s: float | None = None,
        *,
        provenance: dict[str, Any] | None = None,
    ) -> None:
        with self._lock:
            self.record["results"].append(
                {
                    "name": name,
                    "ok": ok,
                    "duration_s": duration_s,
                    "details": details,
                    **({"source": provenance} if provenance is not None else {}),
                }
            )
            self.save()

    def add_artifact(self, path: Path, label: str) -> None:
        with self._lock:
            self.record["artifacts"].append(
                {"path": str(path.resolve()), "label": label}
            )
            self.save()

    def add_exec(
        self,
        argv: list[str],
        log: Path,
        exit_code: int,
        duration_s: float,
        stalled: bool,
        peak_rss_mb: float,
        *,
        source_before: int | None = None,
        source_after: int | None = None,
    ) -> int:
        with self._lock:
            index = len(self.record["execs"])
            self.record["execs"].append(
                {
                    "argv": argv,
                    "log": str(log),
                    "exit_code": exit_code,
                    "duration_s": duration_s,
                    "stalled": stalled,
                    "peak_rss_mb": peak_rss_mb,
                    "source": self.source_interval(source_before, source_after)
                    if source_before is not None and source_after is not None
                    else {"content": "unknown"},
                }
            )
            collector = getattr(self._scope, "execs", None)
            if collector is not None:
                collector.append(index)
            self.save()
            return index

    def add_lock_wait(self, lock: str, duration_s: float) -> None:
        with self._lock:
            self.record["lock_waits"].append({"lock": lock, "duration_s": duration_s})
            self.save()

    def finish(self, exit_code: int, *, interrupted: bool = False) -> int:
        completed = self.snapshot("completed")
        self.record["source"]["completed"] = completed
        self.record["source"]["initial_to_completed"] = self.source_interval(
            self.record["source"]["initial"], completed
        )["content"]
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
