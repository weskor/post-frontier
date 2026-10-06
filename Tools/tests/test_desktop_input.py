"""Client-area boundaries and ownership refusals for real desktop input."""

import argparse
import json
from pathlib import Path
import sys
from typing import Any

from harness import network_desktop as desktop
from harness import verify
import pytest


@pytest.mark.parametrize("point", [-0.001, 1.0, 1.001, float("nan"), float("inf")])
def test_outside_client_coordinates_are_rejected(point: float) -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        verify.window_point({"at": [-100, -200], "size": [100, 200]}, point, 0.5)


def test_client_edge_pixels_work_with_negative_monitor_origin() -> None:
    window = {"at": [-100, -200], "size": [100, 200]}
    assert verify.window_point(window, 0, 0) == (-100, -200)
    assert verify.window_point(window, 0.9999999999999999, 0.9999999999999999) == (
        -1,
        -1,
    )
    assert verify.window_point(window, 0.02, 0.98) == (-98, -4)
    with pytest.raises(RuntimeError, match="no client area"):
        verify.window_point({"at": [0, 0], "size": [0, 200]}, 0.5, 0.5)


@pytest.mark.parametrize("action", [["key", "up"], ["scroll", "3"], ["click", "left"]])
@pytest.mark.parametrize("fault", ["identity", "focus", "package"])
def test_input_refuses_changed_ownership_before_delivery(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, action: list[str], fault: str
) -> None:
    stamp = {"start": "1", "exe": "/game"}
    record = {
        "package": {},
        "map": "/Game/Maps/Boot",
        "peers": {"c1": {"pid": 123, "identity": stamp}},
    }
    (tmp_path / "session.json").write_text(json.dumps(record))
    monkeypatch.setenv("X_HARNESS_DIR", str(tmp_path))
    monkeypatch.setattr(sys, "argv", ["desktop", *action, "--peer", "c1"])
    monkeypatch.setattr(
        desktop,
        "package_stamp",
        lambda: {"changed": True} if fault == "package" else {},
    )
    monkeypatch.setattr(
        desktop,
        "identity",
        lambda pid: {"start": "2"} if fault == "identity" else stamp,
    )
    monkeypatch.setattr(desktop, "desktop_log_ready", lambda *args: None)
    monkeypatch.setattr(
        desktop, "windows", lambda: [{"pid": 123, "mapped": True, "address": "0x123"}]
    )
    calls: list[list[Any]] = []

    def execute(command: list[Any]) -> str:
        calls.append(command)
        return json.dumps({"address": "0xother"})

    monkeypatch.setattr(desktop, "execute", execute)
    with pytest.raises(
        RuntimeError, match=r"identity changed|not focused|modified/stale"
    ):
        desktop.main()
    assert all(command[:2] == ["hyprctl", "activewindow"] for command in calls)


def test_isolation_refuses_workspace_with_unowned_window(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    record = {"desktop": {"target": "temporary"}, "peers": {"host": {"pid": 123}}}
    (tmp_path / "session.json").write_text(json.dumps(record))
    monkeypatch.setattr(
        desktop, "windows", lambda: [{"pid": 456, "workspace": {"name": "temporary"}}]
    )
    with pytest.raises(RuntimeError, match="unowned window"):
        desktop.isolate_peer(tmp_path, "host")


@pytest.mark.parametrize("fault", ["reused-pid", "hidden-prior", "moved-prior"])
def test_restore_never_refocuses_stale_prior_window(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, fault: str
) -> None:
    saved = {
        "workspace": {"monitorID": 1, "name": "7"},
        "cursor": {"x": 100, "y": 200},
        "window": {
            "pid": 123,
            "address": "0x123",
            "workspace": {"name": "2" if fault == "hidden-prior" else "7"},
        },
        "identity": {"start": "old"},
    }
    (tmp_path / "session.json").write_text(json.dumps({"desktop": saved}))
    monkeypatch.setattr(
        desktop,
        "identity",
        lambda pid: {"start": "new" if fault == "reused-pid" else "old"},
    )
    monkeypatch.setattr(
        desktop,
        "windows",
        lambda: [{"pid": 123, "address": "0x123", "workspace": {"name": "2"}}],
    )
    calls: list[list[Any]] = []

    def execute(command: list[Any]) -> str:
        calls.append(command)
        return "ok"

    monkeypatch.setattr(desktop, "execute", execute)
    desktop.restore_workspace(tmp_path)
    assert not any("window=" in command[-1] for command in calls)
    assert json.loads((tmp_path / "session.json").read_text())["desktop"]["restored"]
    count = len(calls)
    desktop.restore_workspace(tmp_path)
    assert len(calls) == count
