"""Dedicated smoke harness: record raw engine exits, return validated tier status."""

import argparse
from collections.abc import Sequence
from contextlib import suppress
from dataclasses import asdict, dataclass, field
import os
from pathlib import Path
import select
import signal
import subprocess
import time
from types import FrameType

from x import jsonio
from x.play_smoke_logs import Evidence, LogReader, crash_snapshot
from x.process import ProcessGroupSurvived, group_rss_bytes, kill_group

STARTUP_SECONDS = 60.0
SHIPPING_SECONDS = 20.0
SHUTDOWN_SECONDS = 15.0


@dataclass
class SmokeResult:
    tier: str
    requested_map: str
    argv: list[str]
    pid: int | None = None
    engine_exit_code: int | None = None
    term_sent: bool = False
    ready: bool = False
    survived: bool = False
    shutdown: bool = False
    crashes: list[str] = field(default_factory=list)
    crash_reports: list[str] = field(default_factory=list)
    failure: str = ""
    timed_out: bool = False
    interrupted: bool = False
    duration_s: float = 0.0
    peak_rss_mb: float = 0.0

    @property
    def ok(self) -> bool:
        return not self.failure


@dataclass
class Supervisor:
    child: subprocess.Popen[bytes]
    evidence: Evidence
    result: SmokeResult
    crash_roots: Sequence[Path]
    baseline: dict[str, tuple[int, int]]
    sample_at: float = 0.0

    def observe(self, *, final: bool = False) -> None:
        if final:
            self.evidence.finish()
        else:
            self.evidence.poll()
        now = time.monotonic()
        if final or now >= self.sample_at:
            snapshot = crash_snapshot(self.crash_roots)
            reports = {
                path
                for path, stamp in snapshot.items()
                if self.baseline.get(path) != stamp
            }
            self.result.crash_reports = sorted(set(self.result.crash_reports) | reports)
            self.result.peak_rss_mb = max(
                self.result.peak_rss_mb, group_rss_bytes(self.child.pid) / (1024 * 1024)
            )
            self.sample_at = now + 0.25
        self.result.ready = self.evidence.ready
        self.result.shutdown = self.evidence.shutdown
        self.result.crashes = list(self.evidence.crashes)

    def crashed(self) -> bool:
        return bool(self.result.crashes or self.result.crash_reports)

    def await_startup(self, deadline: float, shipping: bool) -> bool:
        while True:
            self.observe()
            if self.child.poll() is not None:
                self.result.failure = "Engine exited before the smoke shutdown request"
                return False
            if self.crashed():
                self.result.failure = "Engine crash evidence during startup"
                return False
            now = time.monotonic()
            if shipping and now >= deadline:
                self.result.survived = True
                return True
            if not shipping and now >= deadline:
                self.result.failure = (
                    "Requested map did not finish loading within startup deadline"
                )
                self.result.timed_out = True
                return False
            if not shipping and self.evidence.ready:
                return True
            time.sleep(min(0.02, max(0.0, deadline - now)))

    def terminate(self) -> bool:
        # Drain a fixed pre-signal snapshot before resetting shutdown evidence.
        self.observe(final=True)
        if self.crashed():
            self.result.failure = "Engine crash evidence before smoke shutdown request"
            return False
        # A pidfd targets only this owned child, never a recycled PID or another game.
        pidfd = os.pidfd_open(self.child.pid)
        try:
            if select.select([pidfd], [], [], 0)[0]:
                self.result.failure = "Engine exited before the smoke shutdown request"
                return False
            try:
                signal.pidfd_send_signal(pidfd, signal.SIGTERM)
            except ProcessLookupError:
                self.result.failure = "Engine exited before SIGTERM could be sent"
                return False
        finally:
            os.close(pidfd)
        self.result.term_sent = True
        self.evidence.shutdown = False
        self.result.shutdown = False
        return True

    def await_shutdown(self, deadline: float, shipping: bool) -> None:
        while self.child.poll() is None:
            self.observe()
            if time.monotonic() >= deadline:
                self.result.failure = "Engine did not exit within shutdown deadline"
                self.result.timed_out = True
                return
            time.sleep(0.02)
        self.observe(final=True)
        code = self.child.wait()
        expected = (143,) if shipping else (0, 143)
        if self.crashed():
            self.result.failure = "Engine crash evidence during smoke shutdown"
        elif code not in expected:
            self.result.failure = f"Unexpected engine exit after owned SIGTERM: {code}"
        elif not shipping and not self.evidence.shutdown:
            self.result.failure = "Normal engine shutdown log was not observed"


def supervise(
    supervisor: Supervisor,
    started: float,
    shipping: bool,
    startup: float,
    shutdown: float,
) -> None:
    result = supervisor.result
    child = supervisor.child
    try:
        if supervisor.await_startup(started + startup, shipping) and supervisor.terminate():
            supervisor.await_shutdown(time.monotonic() + shutdown, shipping)
    except KeyboardInterrupt:
        result.interrupted = True
        result.failure = "Smoke interrupted"
        # The outer runner also has a bounded cleanup. Kill immediately so its
        # termination of this harness cannot strand our separate engine group.
        with suppress(ProcessLookupError):
            os.killpg(child.pid, signal.SIGKILL)
    except BaseException:
        kill_group(child)
        raise
    finally:
        if child.poll() is None:
            try:
                kill_group(child)
            except ProcessGroupSurvived as error:
                result.failure = f"Owned process-group cleanup failed: {error}"
        result.engine_exit_code = child.wait()
        supervisor.observe(final=True)
        if supervisor.crashed() and not result.failure:
            result.failure = "Engine crash evidence during smoke shutdown"


def execute_smoke(
    argv: Sequence[str],
    *,
    requested_map: str,
    shipping: bool,
    game_log: Path,
    stdout_log: Path,
    crash_roots: Sequence[Path],
    startup_seconds: float = STARTUP_SECONDS,
    shipping_seconds: float = SHIPPING_SECONDS,
    shutdown_seconds: float = SHUTDOWN_SECONDS,
) -> SmokeResult:
    if min(startup_seconds, shipping_seconds, shutdown_seconds) <= 0:
        raise ValueError("Smoke deadlines must be positive")
    tier = "shipping-liveness" if shipping else "development-map"
    result = SmokeResult(tier, requested_map, list(argv))
    evidence = Evidence(
        None if shipping else requested_map,
        [LogReader(game_log), LogReader(stdout_log)],
    )
    baseline = crash_snapshot(crash_roots)
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with stdout_log.open("wb") as stream:
        try:
            child = subprocess.Popen(
                argv, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True
            )
        except OSError as error:
            result.failure = f"Engine launch failed: {error}"
        else:
            result.pid = child.pid
            supervisor = Supervisor(child, evidence, result, crash_roots, baseline)
            supervise(
                supervisor,
                time.monotonic(),
                shipping,
                shipping_seconds if shipping else startup_seconds,
                shutdown_seconds,
            )
    result.duration_s = time.monotonic() - started
    return result


def interrupted(signum: int, frame: FrameType | None) -> None:
    raise KeyboardInterrupt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shipping", action="store_true")
    parser.add_argument("--map", required=True)
    parser.add_argument("--game-log", type=Path, required=True)
    parser.add_argument("--stdout-log", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--crash-dir", type=Path, action="append", default=[])
    parser.add_argument("argv", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    argv = args.argv[1:] if args.argv[:1] == ["--"] else args.argv
    signal.signal(signal.SIGTERM, interrupted)
    result = execute_smoke(
        argv,
        requested_map=args.map,
        shipping=args.shipping,
        game_log=args.game_log,
        stdout_log=args.stdout_log,
        crash_roots=args.crash_dir,
    )
    jsonio.save(args.report, asdict(result) | {"ok": result.ok})
    status = "passed" if result.ok else result.failure
    print(
        f"{result.tier}: {status}; raw engine exit {result.engine_exit_code}; "
        f"TERM sent {result.term_sent}",
        flush=True,
    )
    return 0 if result.ok else 130 if result.interrupted else 1


if __name__ == "__main__":
    raise SystemExit(main())
