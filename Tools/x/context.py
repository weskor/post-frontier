"""The worktree, evidence and shared facilities passed to every command."""

from dataclasses import dataclass, field
import os
from pathlib import Path
from typing import Mapping, Sequence

from x.freshness import Freshness
from x.locks import Locks
from x.process import execute
from x.runs import Run
from x.settings import Settings


@dataclass
class Context:
    repo: Path
    settings: Settings
    run: Run | None = None
    command: str = ""
    locks: Locks = field(init=False)
    freshness: Freshness = field(init=False)

    def __post_init__(self) -> None:
        self.locks = Locks(self.repo, self.settings, self.run, self.command)
        self.freshness = Freshness(self.repo, self.settings)

    def exec(
        self,
        argv: Sequence[str | Path],
        *,
        log: str,
        cwd: Path | None = None,
        env: Mapping[str, str] | None = None,
        watch: Path | None = None,
        stall_seconds: float | None = None,
    ) -> int:
        if self.run is None:
            raise RuntimeError("child execution requires a recorded command")
        log_path = self.run.dir / f"{log}.log"
        if not log_path.resolve().is_relative_to(self.run.dir.resolve()):
            raise ValueError("log must be inside the run directory")
        child_env = {
            **os.environ,
            **(env or {}),
            "X_RUN_ID": self.run.id,
            "X_RUN_DIR": str(self.run.dir),
            "UE_ROOT": str(self.settings.engine_root),
        }
        arguments = [str(arg) for arg in argv]
        working = cwd or self.repo
        watched = (
            working / watch if watch is not None and not watch.is_absolute() else watch
        )
        outcome = execute(
            arguments,
            log_path,
            working,
            child_env,
            watched,
            self.settings.stall_seconds if stall_seconds is None else stall_seconds,
        )
        self.run.add_exec(
            arguments, log_path, outcome.exit_code, outcome.duration_s, outcome.stalled
        )
        if outcome.interrupted:
            raise KeyboardInterrupt
        return outcome.exit_code
