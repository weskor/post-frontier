"""Owned desktop/native cleanup and simulation predicates fail without real games."""

from collections.abc import Callable
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from typing import Any, cast

from harness import network_desktop, simulation_execution, verify, waits
from harness.verification_readiness import WindowNotReady
import pytest


class Clock:
    def __init__(self) -> None:
        self.now = 0.0
        self.tick: Callable[[], object] = lambda: None

    def sleep(self, seconds: float) -> None:
        self.now += seconds
        self.tick()


class Child:
    pid = 4242

    def __init__(self) -> None:
        self.returncode: int | None = None

    def poll(self) -> int | None:
        return self.returncode


@pytest.fixture
def clock(monkeypatch: pytest.MonkeyPatch) -> Clock:
    result = Clock()
    monkeypatch.setattr(time, "monotonic", lambda: result.now)
    monkeypatch.setattr(time, "sleep", result.sleep)
    for kind in ("identity", "readiness", "shutdown"):
        monkeypatch.setitem(waits.DEFAULT_SECONDS, kind, 0.2)
    return result


def pidfd_signals(
    monkeypatch: pytest.MonkeyPatch, child: Child, *, exits: bool = True
) -> list[tuple[int, int]]:
    sent: list[tuple[int, int]] = []
    monkeypatch.setattr(os, "pidfd_open", lambda pid: 71)
    monkeypatch.setattr(os, "close", lambda fd: None)

    def send(fd: int, signum: int) -> None:
        sent.append((fd, signum))
        if exits and signum == signal.SIGKILL:
            child.returncode = -signum

    monkeypatch.setattr(signal, "pidfd_send_signal", send)
    return sent


@pytest.mark.parametrize("desktop", [False, True])
def test_shutdown_escalates_only_owned_pidfd_and_confirms_exit(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, clock: Clock, desktop: bool
) -> None:
    child = Child()
    expected = {"start": "1", "exe": "/game"}
    item = {"pid": child.pid, "identity": expected}
    (tmp_path / "session.json").write_text(json.dumps(item))
    monkeypatch.setattr(
        verify, "identity", lambda pid: expected if child.poll() is None else None
    )
    monkeypatch.setattr(network_desktop, "identity", verify.identity)
    signals = pidfd_signals(monkeypatch, child)
    if desktop:
        network_desktop.stop_one(tmp_path, item)
    else:
        verify.stop(tmp_path)
    assert signals == [(71, signal.SIGTERM), (71, signal.SIGKILL)]
    assert child.returncode == -signal.SIGKILL
    assert "last observed snapshot" in (tmp_path / "actions.jsonl").read_text()


def test_changed_recorded_identity_is_never_signalled(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, clock: Clock
) -> None:
    child = Child()
    signals = pidfd_signals(monkeypatch, child)
    monkeypatch.setattr(network_desktop, "identity", lambda pid: {"start": "other"})
    network_desktop.stop_one(
        tmp_path, {"pid": child.pid, "identity": {"start": "original"}}
    )
    assert signals == []
    assert child.returncode is None


def test_child_that_survives_kill_has_bounded_reap_failure(
    monkeypatch: pytest.MonkeyPatch, clock: Clock
) -> None:
    child = Child()
    signals = pidfd_signals(monkeypatch, child, exits=False)
    with pytest.raises(waits.WaitTimeout, match="exit after SIGKILL") as failure:
        verify.reap_owned(cast(subprocess.Popen[bytes], child))
    assert signals == [(71, signal.SIGTERM), (71, signal.SIGKILL)]
    assert '"pid": 4242' in str(failure.value)
    assert '"returncode": null' in str(failure.value)
    assert clock.now == pytest.approx(0.4)


def test_expired_probe_does_not_prevent_owned_child_kill(
    monkeypatch: pytest.MonkeyPatch, clock: Clock
) -> None:
    child = Child()
    sent = pidfd_signals(monkeypatch, child)
    with waits.Deadline("failed probe", "state", seconds=0.1):
        clock.sleep(0.1)
        verify.reap_owned(cast(subprocess.Popen[bytes], child))
    assert sent == [(71, signal.SIGTERM), (71, signal.SIGKILL)]
    assert child.returncode == -signal.SIGKILL
    assert clock.now == pytest.approx(0.3)


@pytest.mark.parametrize("desktop", [False, True])
@pytest.mark.parametrize("predicate", ["identity", "readiness"])
def test_failed_launch_reaps_child_even_before_session_identity(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    clock: Clock,
    desktop: bool,
    predicate: str,
) -> None:
    module: Any = network_desktop if desktop else verify
    child = Child()
    root = tmp_path / "package"
    binary = root / "CoopRTS/Binaries/Linux/CoopRTS"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"package")
    expected = {"start": "1", "exe": str(binary.resolve())}
    monkeypatch.setattr(module, "package_stamp", lambda: {"root": str(root)})
    monkeypatch.setattr(module.shutil, "which", lambda program: "/fake/program")
    monkeypatch.setattr(module, "compile_pointer", lambda: None)
    monkeypatch.setattr(module, "execute", lambda command: "[]")
    monkeypatch.setattr(module.subprocess, "Popen", lambda *args, **kwargs: child)
    current = expected if predicate == "readiness" else {"exe": "/wrong", "start": "1"}
    monkeypatch.setattr(
        verify, "identity", lambda pid: current if child.poll() is None else None
    )
    monkeypatch.setattr(network_desktop, "identity", verify.identity)

    def not_ready(*args: object, **kwargs: object) -> None:
        if desktop:
            raise WindowNotReady("owned window still unmapped")
        raise RuntimeError("owned window still unmapped")

    monkeypatch.setattr(module, "doctor", not_ready)
    signals = pidfd_signals(monkeypatch, child)
    run = tmp_path / "run"
    with pytest.raises(waits.WaitTimeout) as failure:
        if desktop:
            network_desktop.launch(run, 1, False)
        else:
            verify.launch(run)
    message = str(failure.value)
    predicate_name = (
        "executable identity" if predicate == "identity" else "mapped window"
    )
    assert predicate_name in message
    assert '"pid": 4242' in message
    assert ("/wrong" if predicate == "identity" else "still unmapped") in message
    assert child.returncode == -signal.SIGKILL
    assert signals[-1] == (71, signal.SIGKILL)


def test_simulation_checkpoint_rewrites_do_not_reset_game_time_watchdog(
    tmp_path: Path, clock: Clock
) -> None:
    child = Child()
    output = tmp_path / "match.json"
    clock.tick = lambda: output.write_text(json.dumps({"duration": 0}))
    job = {"time_cap": 120, "dilation": 1}
    with pytest.raises(
        waits.WaitTimeout, match="persisted simulation game-time"
    ) as failure:
        simulation_execution.monitor(
            cast(subprocess.Popen[bytes], child), output, tmp_path, 0.3, job
        )
    assert '"duration": 0' in str(failure.value)
    assert '"pid": 4242' in str(failure.value)


def test_advancing_simulation_still_has_absolute_completion_deadline(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, clock: Clock
) -> None:
    child = Child()
    output = tmp_path / "match.json"
    monkeypatch.setitem(waits.DEFAULT_SECONDS, "simulation", 0.1)
    clock.tick = lambda: output.write_text(json.dumps({"duration": clock.now}))
    job = {"time_cap": 1, "dilation": 1}
    with pytest.raises(
        waits.WaitTimeout, match=r"simulation job .* completion"
    ) as failure:
        simulation_execution.monitor(
            cast(subprocess.Popen[bytes], child), output, tmp_path, 1, job
        )
    assert clock.now == pytest.approx(3.3)
    assert '"checkpoint"' in str(failure.value)


def test_duel_completion_uses_actual_ordered_roster_and_dilation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, clock: Clock
) -> None:
    child = Child()
    output = tmp_path / "match.json"
    monkeypatch.setitem(waits.DEFAULT_SECONDS, "simulation", 0.1)

    def checkpoint() -> None:
        output.write_text(
            json.dumps(
                {
                    "duration": clock.now,
                    "unit_definitions": [{"id": n} for n in range(4)],
                }
            )
        )
        if clock.now >= 20:
            child.returncode = 0

    clock.tick = checkpoint
    job = {"mode": "duel", "time_cap": 1, "dilation": 2}
    assert (
        simulation_execution.monitor(
            cast(subprocess.Popen[bytes], child), output, tmp_path, 1, job
        )
        == 0
    )
    assert clock.now == pytest.approx(20)


def test_simulation_kill_confirmation_is_bounded(
    monkeypatch: pytest.MonkeyPatch, clock: Clock
) -> None:
    child = Child()
    sent: list[tuple[int, int]] = []
    monkeypatch.setattr(
        os,
        "killpg",
        lambda pid, signum: sent.append((pid, signum)),
    )
    with pytest.raises(waits.WaitTimeout, match="exit after SIGKILL") as failure:
        simulation_execution.stop_owned(cast(subprocess.Popen[bytes], child))
    assert sent == [(child.pid, signal.SIGTERM), (child.pid, signal.SIGKILL)]
    assert '"returncode": null' in str(failure.value)
    assert clock.now == pytest.approx(0.4)


@pytest.mark.parametrize(("dilation", "expires"), [(1, 48.3), (2, 24.3)])
def test_duel_timeout_is_anchored_to_launch_not_each_checkpoint(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    clock: Clock,
    dilation: int,
    expires: float,
) -> None:
    child = Child()
    output = tmp_path / "match.json"
    monkeypatch.setitem(waits.DEFAULT_SECONDS, "simulation", 0.1)

    def checkpoint() -> None:
        output.write_text(
            json.dumps(
                {
                    "duration": clock.now,
                    "unit_definitions": [{"id": n} for n in range(4)],
                }
            )
        )

    clock.tick = checkpoint
    job = {"mode": "duel", "time_cap": 1, "dilation": dilation}
    with pytest.raises(
        waits.WaitTimeout, match=r"simulation job .* completion"
    ) as failure:
        simulation_execution.monitor(
            cast(subprocess.Popen[bytes], child), output, tmp_path, 1, job
        )
    assert clock.now == pytest.approx(expires)
    assert '"pair_count": 16' in str(failure.value)
