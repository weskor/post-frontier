"""Rendered proof of active pause, a progressing countdown and the spent action."""

from __future__ import annotations

from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import NetworkRun, require


def pause_hud_scenario(run: NetworkRun) -> None:
    require(
        run.clients == 0 and run.offscreen, "pause-hud requires --clients 0 --offscreen"
    )
    capture = Capture(run)
    pid, _ = boot(run, capture, (1600, 900))
    capture.hud(43, "active pause through the on-screen P button")
    state = capture.wait(
        lambda st: (
            st["activePaused"] and st["worldPaused"] and 0 < st["pauseRemaining"] <= 60
        ),
        "paused listen-host HUD with a shared countdown",
    )
    frozen = state["worldTime"]
    remaining = state["pauseRemaining"]
    capture.shot("paused-countdown")
    capture.key("Escape")
    capture.wait(
        lambda st: (
            st["uiScreen"] == 2
            and st["activePaused"]
            and st["worldPaused"]
            and st["worldTime"] == frozen
            and st["pauseRemaining"] < remaining - 2
        ),
        "Esc menu preserves pause and displays the advancing countdown",
    )
    capture.shot("paused-menu-countdown")
    capture.key("Escape")
    capture.wait(lambda st: st["uiScreen"] == 0, "Esc closes the menu without resuming")
    capture.hud(43, "any player can resume the shared active pause")
    capture.wait(
        lambda st: (
            not st["activePaused"] and not st["worldPaused"] and st["coopPauseSpent"]
        ),
        "spent pause indicator after early resume",
    )
    capture.shot("pause-spent")
    no_compositor_windows(run, pid)
    run.phase("paused/countdown, paused menu and spent action rendered offscreen")
