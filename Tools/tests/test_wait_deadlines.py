"""Impossible and nested predicates fail with the last observed probe state."""

from collections.abc import Sequence
import time

from harness.network_probe import NetworkProbe
from harness.verify import JsonObject
from harness.waits import Deadline, WaitTimeout
import pytest


class Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


@pytest.fixture
def clock(monkeypatch: pytest.MonkeyPatch) -> Clock:
    clock = Clock()
    monkeypatch.setattr(time, "monotonic", clock.monotonic)
    monkeypatch.setattr(time, "sleep", clock.sleep)
    return clock


class FakeProbe(NetworkProbe):
    def __init__(self) -> None:
        self.peers = {}
        self.latest_states = {}
        self.pending = {}
        self.latest_errors = {}
        self.connection_health = {}
        self.events_seen: list[tuple[str, dict[str, object]]] = []

    def check_artifact(self) -> None:
        pass

    def check_network_failures(self) -> None:
        pass

    def progress(self, description: str, elapsed: float, names: Sequence[str]) -> None:
        pass

    def event(self, kind: str, **fields: object) -> None:
        self.events_seen.append((kind, fields))


def test_impossible_probe_predicate_reports_elapsed_and_snapshot(clock: Clock) -> None:
    probe = FakeProbe()
    snapshot = {"ready": True, "joined": 3, "required": 4}
    with pytest.raises(WaitTimeout) as raised:
        probe.until(
            lambda: snapshot["joined"] == snapshot["required"],
            "four units physically joined",
            5,
            seconds=0.4,
            snapshot=lambda: snapshot,
        )
    message = str(raised.value)
    assert "four units physically joined after 0.400s" in message
    assert '"joined": 3' in message
    assert '"required": 4' in message
    assert clock.now == pytest.approx(0.4)
    assert probe.events_seen[-1][0] == "timeout"


def test_nested_unanswered_probe_does_not_extend_outer_deadline(clock: Clock) -> None:
    probe = FakeProbe()

    def observe() -> JsonObject:
        return probe.until(
            lambda: None,
            "host observe response",
            5,
            kind="response",
            snapshot=lambda: {"reply_id": 7},
        )

    with pytest.raises(WaitTimeout) as raised:
        probe.until(
            observe,
            "barracks completed",
            5,
            seconds=0.2,
            snapshot=lambda: {"construction": 0.5},
        )
    assert "barracks completed after 0.200s" in str(raised.value)
    assert '"construction": 0.5' in str(raised.value)
    assert clock.now == pytest.approx(0.2)


def test_expired_wait_cannot_accept_a_late_true_predicate(clock: Clock) -> None:
    probe = FakeProbe()

    def late() -> bool:
        clock.sleep(0.3)
        return True

    with pytest.raises(WaitTimeout, match="late readiness"):
        probe.until(late, "late readiness", 5, seconds=0.2)


def test_wait_context_restored_after_timeout(clock: Clock) -> None:
    with (
        pytest.raises(WaitTimeout),
        Deadline("expired", "state", seconds=0.1) as first,
    ):
        clock.sleep(0.1)
        first.check({"phase": "loading"})
    with Deadline("next", "state", seconds=0.2) as next_wait:
        clock.sleep(0.1)
        next_wait.check({"phase": "ready"})
    assert next_wait.remaining == pytest.approx(0.1)
