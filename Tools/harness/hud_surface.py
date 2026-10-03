"""Offscreen HUD interaction, capture evidence and minimap coordinate checks."""

from __future__ import annotations

from collections.abc import Callable, Sequence
import json
from pathlib import Path
import shutil
import struct
import subprocess
from typing import cast

from harness.network import NetworkRun, goal_matches, region, require
from harness.verify import JsonObject


def minimap_region_point(state: JsonObject, target: int) -> tuple[float, float]:
    """Inverse of the HUD's arena-to-minimap transform, using a replicated region anchor."""
    anchor = region(state, target)["anchor"]
    half = state["arenaHalfExtent"]
    origin, size = state["minimapOrigin"], state["minimapSize"]
    horizontal = (anchor[1] + half[1]) / (2 * half[1])
    vertical = (half[0] - anchor[0]) / (2 * half[0])
    require(
        0 < horizontal < 1 and 0 < vertical < 1,
        "region anchor is outside the clickable minimap",
    )
    return origin[0] + size * horizontal, origin[1] + size * vertical


def png_size(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:24]
    require(header[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG")
    return cast(tuple[int, int], struct.unpack(">II", header[16:24]))


def capture_dimensions(path: Path, state: JsonObject) -> tuple[int, int]:
    width, height = png_size(path)
    require(
        (width, height) == (state["viewportWidth"], state["viewportHeight"]),
        f"{path.name}: {width}x{height} does not match viewport {state['viewportWidth']}x{state['viewportHeight']}",
    )
    return width, height


def compositor_windows(pid: int) -> list[str] | None:
    """Read-only Hyprland query: windows owned by pid. None when no compositor query is available."""
    if not shutil.which("hyprctl"):
        return None
    try:
        clients = json.loads(
            subprocess.run(
                ["hyprctl", "clients", "-j"], check=True, capture_output=True, text=True
            ).stdout
        )
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return None
    return [c.get("title", "") for c in clients if c.get("pid") == pid]


class Capture:
    def __init__(self, run: NetworkRun) -> None:
        self.run = run
        self.folder = run.run / "captures"
        self.folder.mkdir()
        self.count = 0

    def state(self) -> JsonObject:
        return self.run.observe("host")

    def wait(
        self, predicate: Callable[[JsonObject], object], description: str
    ) -> JsonObject:
        return self.run.await_states(
            ["host"], lambda s: predicate(s["host"]), description
        )["host"]

    def hud(self, action: int, description: str) -> None:
        self.run.request("host", "hud", hudAction=action)
        self.run.phase(f"HUD click via shared geometry: {description}")

    def key(self, name: str) -> None:
        self.run.request("host", "key", key=name, pressed=True)
        self.run.request("host", "key", key=name, pressed=False)

    def box(
        self, start: Sequence[float], end: Sequence[float], *, add: bool = False
    ) -> None:
        self.run.request(
            "host",
            "select",
            target="box",
            add=add,
            x=start[0],
            y=start[1],
            x2=end[0],
            y2=end[1],
        )

    def minimap(self, horizontal: float, vertical: float) -> None:
        before = self.state()
        require(
            not before["assigningGoal"],
            "camera-only minimap check cannot run while picking a goal",
        )
        origin = before["minimapOrigin"]
        size = before["minimapSize"]
        self.run.request(
            "host",
            "hudClick",
            x=origin[0] + size * horizontal,
            y=origin[1] + size * vertical,
        )
        # The minimap spans the replicated arena: top edge is +X, left edge is -Y.
        half = before["arenaHalfExtent"]
        expected = (
            half[0] - 2 * half[0] * vertical,
            -half[1] + 2 * half[1] * horizontal,
            0,
        )
        after = self.wait(
            lambda s: all(
                abs(actual - target) < 1
                for actual, target in zip(s["cameraPosition"], expected, strict=False)
            ),
            f"minimap camera focus at {expected}",
        )
        require(
            (after["placing"], after["assigningGoal"])
            == (before["placing"], before["assigningGoal"]),
            "minimap camera click changed active placement/goal mode",
        )
        require(
            [
                (b["actorId"], b["forceGoal"], b["goalRegionIndex"])
                for b in after["buildings"]
            ]
            == [
                (b["actorId"], b["forceGoal"], b["goalRegionIndex"])
                for b in before["buildings"]
            ],
            "minimap camera click placed a building or reassigned a goal",
        )
        self.run.phase(f"minimap camera-only click at {horizontal:.2f},{vertical:.2f}")

    def pick_region(self, index: int, goal: int, target: int) -> None:
        before = self.state()
        require(
            before["assigningGoal"] and before["pendingGoal"] == goal,
            "requested goal pick mode is inactive",
        )
        x, y = minimap_region_point(before, target)
        self.run.request("host", "hudClick", x=x, y=y)
        self.wait(
            lambda s: (
                not s["assigningGoal"]
                and s["hudExpanded"]
                and goal_matches(s, index, goal, target)
            ),
            "minimap submits selected region goal",
        )
        self.run.phase(f"minimap selected region {target} for goal {goal}")

    def shot(self, label: str) -> Path:
        state = self.state()
        self.count += 1
        path = self.folder / f"{self.count:02d}-{label}.png"
        self.run.request("host", "screenshot", path=str(path))
        previous = -1

        def written() -> bool | None:
            nonlocal previous
            if not path.exists():
                return None
            size = path.stat().st_size
            stable = size > 0 and size == previous
            previous = size
            return stable or None

        self.run.until(
            written,
            f"screenshot {path.name} written",
            5,
            names=["host"],
            kind="capture",
            snapshot=lambda: {"path": str(path), "size": previous},
        )
        width, height = capture_dimensions(path, state)
        self.run.event(
            "capture",
            path=str(path),
            width=width,
            height=height,
            hudExpanded=state["hudExpanded"],
            placing=state["placing"],
            assigningGoal=state["assigningGoal"],
            feedback=state.get("orderFeedback", ""),
        )
        print(f"Captured {path} ({width}x{height})", flush=True)
        return path


def no_compositor_windows(run: NetworkRun, pid: int) -> None:
    windows = compositor_windows(pid)
    run.event("compositor-windows", pid=pid, windows=windows)
    require(not windows, f"offscreen host mapped compositor windows: {windows}")
