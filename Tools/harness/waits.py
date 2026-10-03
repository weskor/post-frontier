"""Bound predicate waits independently of polling output or apparent child activity.

Passing events in the runs root (557 records measured on 2026-10-03) had
maximum readiness/response/state/capture waits of 6.964/1.400/16.054/0.621 s.
Defaults leave more than 3x that headroom. Identity, shutdown and native/desktop
readiness defaults are unmeasured: no historical timing evidence was available
for those ownership/window operations. Cold packaged launches exceeding the
readiness bound remain an untested risk. Simulation uses its default for no
persisted game-time progress, with a job-derived absolute completion deadline.
"""

from __future__ import annotations

from contextvars import ContextVar, Token
import json
import math
import time
from typing import Literal

type WaitKind = Literal[
    "identity", "readiness", "response", "state", "capture", "shutdown", "simulation"
]

DEFAULT_SECONDS: dict[WaitKind, float] = {
    "identity": 30,
    "readiness": 90,
    "response": 30,
    "state": 120,
    "capture": 30,
    "shutdown": 15,
    "simulation": 120,
}


class WaitTimeout(ValueError):
    """A named predicate did not converge before its deadline."""


_active: ContextVar[Deadline | None] = ContextVar("harness_deadline", default=None)


class Deadline:
    def __init__(
        self, description: str, kind: WaitKind, *, seconds: float | None = None
    ) -> None:
        self.description = description
        self.parent: Deadline | None = None
        self.snapshot: object = None
        self.token: Token[Deadline | None] | None = None
        self.started = time.monotonic()
        self.rebudget(DEFAULT_SECONDS[kind] if seconds is None else seconds)

    def reset(self) -> None:
        self.started = time.monotonic()
        self.expires = self.started + self.seconds

    def rebudget(self, seconds: float) -> None:
        """Change the total allowance without restarting the original wait."""
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError("wait deadline must be finite and positive")
        self.seconds = seconds
        self.expires = self.started + seconds

    @property
    def elapsed(self) -> float:
        return time.monotonic() - self.started

    @property
    def remaining(self) -> float:
        bound = self.expires
        if self.parent is not None:
            bound = min(bound, time.monotonic() + self.parent.remaining)
        return max(0, bound - time.monotonic())

    def check(self, snapshot: object) -> None:
        self.snapshot = snapshot
        if self.parent is not None:
            self.parent.check(self.parent.snapshot)
        if time.monotonic() >= self.expires:
            raise WaitTimeout(
                f"Timed out waiting for {self.description} after {self.elapsed:.3f}s "
                f"(deadline {self.seconds:g}s); last observed snapshot: "
                f"{json.dumps(snapshot, default=str, sort_keys=True)}"
            )

    def __enter__(self) -> Deadline:
        self.parent = _active.get()
        self.token = _active.set(self)
        return self

    def __exit__(self, *exception: object) -> None:
        if self.token is not None:
            _active.reset(self.token)
            self.token = None
            self.parent = None
