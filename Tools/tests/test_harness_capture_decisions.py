"""Capture evidence and readiness distinguish wrong maps, pending peers and invalid images."""

from pathlib import Path
import struct

from harness.hud_surface import capture_dimensions, minimap_region_point
from harness.verification_readiness import (
    WindowNotReady,
    desktop_log_ready,
    native_log_ready,
)
from harness.verify import JsonObject, map_started
import pytest


def test_capture_dimensions_accept_viewport_and_reject_wrong_size(
    tmp_path: Path,
) -> None:
    image = tmp_path / "capture.png"
    image.write_bytes(
        b"\x89PNG\r\n\x1a\n" + b"\x00" * 8 + struct.pack(">II", 1600, 900)
    )
    assert capture_dimensions(image, dict(viewportWidth=1600, viewportHeight=900)) == (
        1600,
        900,
    )
    with pytest.raises(
        AssertionError, match="1600x900 does not match viewport 1280x720"
    ):
        capture_dimensions(image, dict(viewportWidth=1280, viewportHeight=720))
    image.write_bytes(b"not a PNG")
    with pytest.raises(AssertionError, match="is not a PNG"):
        capture_dimensions(image, dict(viewportWidth=1600, viewportHeight=900))


def test_minimap_transform_uses_both_arena_axes_and_rejects_edge() -> None:
    state: JsonObject = dict(
        regions=[dict(index=4, anchor=[50, -100, 80])],
        arenaHalfExtent=[100, 200],
        minimapOrigin=[10, 20],
        minimapSize=80,
    )
    assert minimap_region_point(state, 4) == (30, 40)
    state["regions"][0]["anchor"] = [100, 0, 0]
    with pytest.raises(AssertionError, match="outside the clickable minimap"):
        minimap_region_point(state, 4)


def test_native_requires_engine_readiness_before_requested_map() -> None:
    with pytest.raises(RuntimeError, match=r"Expected UE 5\.8\.3"):
        native_log_ready("", "/Game/Maps/Test", False)
    with pytest.raises(
        RuntimeError, match="requested map /Game/Maps/Test has not started"
    ):
        native_log_ready("5.8.3 Bringing up level for play", "/Game/Maps/Test", False)
    text = "5.8.3 Bringing up level for play\nBringing World /Game/Maps/Test.Test up for play"
    native_log_ready(text, "/Game/Maps/Test", map_started(text, "/Game/Maps/Test"))
    assert map_started(text, "/Game/Maps/Test2") is False


@pytest.mark.parametrize("failure", ["TravelFailure:", "BroadcastTravelFailure"])
def test_desktop_travel_failure_wins_over_pending_or_wrong_map(failure: str) -> None:
    log = failure + "\nBringing World /Game/Maps/Other.Other up for play"
    with pytest.raises(RuntimeError, match="host map travel failed") as caught:
        desktop_log_ready(log, "host", "/Game/Maps/Test", "host/game.log", False)
    assert not isinstance(caught.value, WindowNotReady)


def test_desktop_wrong_host_map_is_terminal_but_remote_travel_is_pending() -> None:
    log = "5.8.3 Bringing up level for play\nBringing World /Game/Maps/Other.Other up for play"
    with pytest.raises(RuntimeError, match="started a different map") as caught:
        desktop_log_ready(log, "host", "/Game/Maps/Test", "host/game.log", False)
    assert not isinstance(caught.value, WindowNotReady)
    with pytest.raises(WindowNotReady, match="readiness for /Game/Maps/Test missing"):
        desktop_log_ready(log, "c1", "/Game/Maps/Test", "c1/game.log", False)
    with pytest.raises(WindowNotReady):
        desktop_log_ready("", "host", "/Game/Maps/Test", "host/game.log", True)
    desktop_log_ready(log, "c1", "/Game/Maps/Other", "c1/game.log", True)
