"""Pure log-readiness decisions; desktop ownership and input stay in their drivers."""

from __future__ import annotations

from pathlib import Path

READY = "Bringing up level for play"


class WindowNotReady(RuntimeError):
    pass


def native_log_ready(log: str, map_path: str, selected_map: bool) -> None:
    if READY not in log or "5.8.3" not in log:
        raise RuntimeError(
            "Expected UE 5.8.3 game readiness not found in this run's log"
        )
    if not selected_map:
        raise RuntimeError(
            f"Launch failure: requested map {map_path} has not started; inspect game.log"
        )


def desktop_log_ready(
    log: str, peer: str, map_path: str, log_path: Path, selected_map: bool
) -> None:
    if "TravelFailure:" in log or "BroadcastTravelFailure" in log:
        raise RuntimeError(f"{peer} map travel failed; inspect {log_path}")
    if (
        peer == "host"
        and not selected_map
        and any(
            "Bringing World " in line and " up for play" in line
            for line in log.splitlines()
        )
    ):
        raise RuntimeError(
            f"{peer} started a different map instead of {map_path}; inspect {log_path}"
        )
    if not selected_map or READY not in log or "5.8.3" not in log:
        raise WindowNotReady(f"{peer} Unreal 5.8.3 readiness for {map_path} missing")
