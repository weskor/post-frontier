"""Real owned children exercise Development readiness and Shipping liveness tiers."""

import os
from pathlib import Path
import signal
import sys
import threading
import time

import pytest
from x.play_smoke import SmokeResult, execute_smoke

_CHILD = """
import os
from pathlib import Path
import signal
import sys
import time

game_log = Path(sys.argv[1])
crashes = Path(sys.argv[2])
exit_code = 143
shutdown_message = 'LogExit: Exiting.'
shutdown_report = False

def emit(message):
    with game_log.open('a') as stream:
        stream.write(message + '\\n')
        stream.flush()

def stop(signum, frame):
    if shutdown_message:
        emit(shutdown_message)
    if shutdown_report:
        crashes.mkdir(parents=True, exist_ok=True)
        (crashes / 'CrashContext.runtime-xml').write_text('crash')
    print('UnixPlatform RequestExitWithStatus(0, 143); Error 143', flush=True)
    raise SystemExit(exit_code)

signal.signal(signal.SIGTERM, stop)
"""
_READY = "emit('LogLoad: Took 0.015 seconds to LoadMap(/Game/Maps/Boot)')"


def run_child(
    tmp_path: Path,
    body: str,
    *,
    shipping: bool = False,
    startup: float = 2.0,
    shutdown: float = 0.4,
    survival: float = 0.5,
) -> SmokeResult:
    script = tmp_path / "child.py"
    script.write_text(_CHILD + "\n" + body + "\ntime.sleep(30)\n")
    return execute_smoke(
        [
            sys.executable,
            str(script),
            str(tmp_path / "game.log"),
            str(tmp_path / "crashes"),
        ],
        requested_map="/Game/Maps/Boot",
        shipping=shipping,
        game_log=tmp_path / "game.log",
        stdout_log=tmp_path / "stdout.log",
        crash_roots=[tmp_path / "crashes"],
        startup_seconds=startup,
        shipping_seconds=survival,
        shutdown_seconds=shutdown,
    )


@pytest.mark.parametrize("exit_code", [0, 143])
def test_development_correct_map_and_normal_owned_shutdown(
    tmp_path: Path, exit_code: int
) -> None:
    body = (
        f"exit_code = {exit_code}\n"
        "emit('LogInit: -crashhandlerstacksize - Allows setting crash handler stack sizes (204800)')\n"
        + _READY
    )
    result = run_child(tmp_path, body)
    assert result.ok
    assert result.ready and result.shutdown and result.term_sent
    assert result.engine_exit_code == exit_code
    assert result.pid is not None
    with pytest.raises(ChildProcessError):
        os.waitpid(result.pid, os.WNOHANG)


def test_unrelated_map_cannot_satisfy_readiness(tmp_path: Path) -> None:
    result = run_child(
        tmp_path,
        "emit('LogLoad: Took 0.015 seconds to LoadMap(/Game/Maps/BootOther)')",
        startup=0.3,
    )
    assert not result.ok and not result.ready and not result.term_sent
    assert result.timed_out


def test_preexisting_requested_map_log_does_not_prove_new_readiness(
    tmp_path: Path,
) -> None:
    (tmp_path / "game.log").write_text(
        "LogLoad: Took 0.015 seconds to LoadMap(/Game/Maps/Boot)\n"
    )
    result = run_child(tmp_path, "pass", startup=0.3)
    assert not result.ok and not result.ready and not result.term_sent


@pytest.mark.parametrize(
    "body",
    [
        "pass",
        "print('runner says LoadMap(/Game/Maps/Boot) complete', flush=True)",
        "emit('LogLoad: Took 0.015 seconds to LoadMap(/Game/Maps/Boot?listen)')",
    ],
)
def test_missing_real_requested_readiness_fails(tmp_path: Path, body: str) -> None:
    result = run_child(tmp_path, body, startup=0.3)
    assert not result.ok and not result.ready and not result.term_sent


def test_incremental_prefixed_stdout_engine_readiness(tmp_path: Path) -> None:
    body = """
sys.stdout.write('[2026.10.03-01.02.03:004][  0]LogLoad: Display: Took 0.015 seconds to ')
sys.stdout.flush()
time.sleep(0.08)
print('LoadMap(/Game/Maps/Boot)', flush=True)
"""
    result = run_child(tmp_path, body)
    assert result.ok and result.ready and result.shutdown


@pytest.mark.parametrize("exit_code", [0, 7, 143])
def test_early_exit_is_never_a_success(tmp_path: Path, exit_code: int) -> None:
    result = run_child(tmp_path, f"raise SystemExit({exit_code})")
    assert not result.ok and not result.term_sent
    assert result.engine_exit_code == exit_code
    assert not result.timed_out


def test_nonzero_non143_after_owned_signal_fails(tmp_path: Path) -> None:
    result = run_child(tmp_path, "exit_code = 7\n" + _READY)
    assert not result.ok and result.term_sent and result.shutdown
    assert result.engine_exit_code == 7


def test_unhandled_signal_exit_is_not_clean_shutdown(tmp_path: Path) -> None:
    body = "signal.signal(signal.SIGTERM, signal.SIG_DFL)\n" + _READY
    result = run_child(tmp_path, body)
    assert not result.ok and result.term_sent and not result.shutdown
    assert result.engine_exit_code == -signal.SIGTERM


def test_missing_shutdown_log_fails_despite_exit143(tmp_path: Path) -> None:
    result = run_child(tmp_path, "shutdown_message = ''\n" + _READY)
    assert not result.ok and result.term_sent and not result.shutdown
    assert result.engine_exit_code == 143


def test_pre_signal_shutdown_line_cannot_prove_shutdown(tmp_path: Path) -> None:
    body = "shutdown_message = ''\nemit('LogExit: Exiting.')\n" + _READY
    result = run_child(tmp_path, body)
    assert not result.ok and result.term_sent and not result.shutdown


@pytest.mark.parametrize("shipping", [False, True])
def test_shutdown_timeout_kills_and_reaps_owned_child(
    tmp_path: Path, shipping: bool
) -> None:
    body = "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n" + _READY
    result = run_child(tmp_path, body, shipping=shipping, shutdown=0.2)
    assert not result.ok and result.term_sent and result.timed_out
    assert result.engine_exit_code == -signal.SIGKILL
    assert result.pid is not None
    with pytest.raises(ChildProcessError):
        os.waitpid(result.pid, os.WNOHANG)


def test_unrelated_growth_cannot_extend_absolute_startup_bound(tmp_path: Path) -> None:
    body = """
for index in range(300):
    emit('LogTemp: unrelated work ' + str(index))
    time.sleep(0.01)
"""
    started = time.monotonic()
    result = run_child(tmp_path, body, startup=0.3)
    elapsed = time.monotonic() - started
    assert not result.ok and result.timed_out and not result.term_sent
    assert elapsed < 1.5
    assert "unrelated work" in (tmp_path / "game.log").read_text()


def test_shipping_liveness_accepts_only_owned143_without_readiness(
    tmp_path: Path,
) -> None:
    result = run_child(tmp_path, "shutdown_message = ''", shipping=True)
    assert result.ok and result.survived and result.term_sent
    assert not result.ready and not result.shutdown
    assert result.engine_exit_code == 143
    assert result.duration_s >= 0.5


def test_shipping_logged_map_cannot_shorten_liveness_bound(tmp_path: Path) -> None:
    result = run_child(tmp_path, _READY, shipping=True)
    assert result.ok and result.survived and not result.ready
    assert result.duration_s >= 0.5


@pytest.mark.parametrize("exit_code", [0, 143])
def test_shipping_spontaneous_exit_fails(tmp_path: Path, exit_code: int) -> None:
    result = run_child(tmp_path, f"raise SystemExit({exit_code})", shipping=True)
    assert not result.ok and not result.term_sent and not result.survived
    assert result.engine_exit_code == exit_code


@pytest.mark.parametrize("exit_code", [0, 7, -1])
def test_shipping_other_post_signal_exit_fails(tmp_path: Path, exit_code: int) -> None:
    result = run_child(tmp_path, f"exit_code = {exit_code}", shipping=True)
    assert not result.ok and result.survived and result.term_sent
    assert result.engine_exit_code != 143


@pytest.mark.parametrize(
    "signature",
    [
        "Fatal error: broken",
        "Unhandled Exception: crash",
        "CrashReportClient spawned",
        "Signal 5 caught.",
        "Engine crash handling finished; re-raising signal 11 for the default handler.",
    ],
)
def test_shipping_startup_crash_output_fails(tmp_path: Path, signature: str) -> None:
    result = run_child(tmp_path, f"print({signature!r}, flush=True)", shipping=True)
    assert not result.ok and not result.term_sent and not result.survived
    assert result.crashes


@pytest.mark.parametrize("shipping", [False, True])
def test_crash_signature_during_shutdown_fails(tmp_path: Path, shipping: bool) -> None:
    body = "shutdown_message = 'Fatal error: during shutdown'\n" + _READY
    result = run_child(tmp_path, body, shipping=shipping)
    assert not result.ok and result.term_sent and result.crashes
    assert result.engine_exit_code == 143


def test_shipping_new_crash_report_fails(tmp_path: Path) -> None:
    body = """
crashes.mkdir()
(crashes / 'CrashContext.runtime-xml').write_text('crash')
"""
    result = run_child(tmp_path, body, shipping=True)
    assert not result.ok and not result.term_sent and result.crash_reports


def test_shipping_old_crash_report_does_not_fail(tmp_path: Path) -> None:
    crashes = tmp_path / "crashes"
    crashes.mkdir()
    (crashes / "old.runtime-xml").write_text("old crash")
    result = run_child(tmp_path, "pass", shipping=True)
    assert result.ok and not result.crash_reports


def test_shipping_shutdown_crash_report_fails(tmp_path: Path) -> None:
    result = run_child(tmp_path, "shutdown_report = True", shipping=True)
    assert not result.ok and result.term_sent and result.crash_reports


def test_interrupt_kills_owned_child_and_preserves_outcome(tmp_path: Path) -> None:
    marker = tmp_path / "pid"

    def interrupt() -> None:
        deadline = time.monotonic() + 5
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        if marker.exists():
            os.kill(os.getpid(), signal.SIGINT)

    thread = threading.Thread(target=interrupt)
    thread.start()
    try:
        body = (
            "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
            f"Path({str(marker)!r}).write_text(str(os.getpid()))"
        )
        result = run_child(tmp_path, body)
    finally:
        thread.join()
    assert not result.ok and result.interrupted and not result.term_sent
    assert result.engine_exit_code == -signal.SIGKILL
    assert result.pid == int(marker.read_text())
    assert result.pid is not None
    with pytest.raises(ChildProcessError):
        os.waitpid(result.pid, os.WNOHANG)
