"""Build-bar input and placement feedback acceptance."""

from __future__ import annotations

from harness.hud_actions import BUILD_BARRACKS
from harness.hud_surface import Capture
from harness.network import NetworkRun, require, wallet


def deck_controls(run: NetworkRun, capture: Capture) -> None:
    capture.shot("start-overview")
    capture.minimap(0.25, 0.25)
    capture.shot("minimap-camera-northwest")
    capture.minimap(0.99, 0.01)
    capture.minimap(0.5, 0.5)
    capture.key("F")

    capture.hud(BUILD_BARRACKS, "Build bar Barracks button enters placement")
    capture.wait(
        lambda s: s["placing"] and not s["hudExpanded"],
        "placement mode with collapsed deck",
    )
    capture.shot("placement-mode")
    capture.minimap(0.75, 0.75)
    capture.key("F")
    capture.key("Escape")
    capture.wait(
        lambda s: not s["placing"] and s["hudExpanded"],
        "Escape cancels placement and reopens deck",
    )
    capture.key("F4")
    capture.wait(lambda s: not s["hudExpanded"], "F4 hides deck")
    capture.shot("deck-hidden")
    capture.minimap(0.5, 0.5)
    capture.key("F")
    capture.hud(BUILD_BARRACKS, "Build bar remains clickable with the deck hidden")
    capture.wait(lambda s: s["placing"], "hidden-deck build bar enters placement")
    capture.key("RightMouseButton")
    capture.wait(lambda s: not s["placing"], "right-click cancels placement")
    capture.key("B")
    capture.shot("build-hotkey-pending")
    # Clear before re-arming: another B cancels an unexpired pending sequence.
    capture.key("RightMouseButton")
    capture.key("B")
    capture.key("Q")
    capture.wait(lambda s: s["placing"], "B Q starts Barracks placement")
    capture.key("Escape")
    capture.wait(
        lambda s: not s["placing"] and s["hudExpanded"],
        "Escape cancels hotkey placement",
    )
    run.phase("B Q, Escape/right-click cancellation and always-visible build bar")
    blocked_build_feedback(run, capture)


def blocked_build_feedback(run: NetworkRun, capture: Capture) -> None:
    state = capture.state()
    owner = state["localIndex"]
    income_paused = state["incomePaused"]
    run.request("host", "income", paused=True)
    run.request("host", "fund", owner=owner, amount=0)
    capture.wait(
        lambda s: s["feedbackOpacity"] == 0 and not s["orderFeedback"],
        "previous message expires before checking blocked-click feedback",
    )
    capture.hud(BUILD_BARRACKS, "Greyed-out Barracks button explains its shortfall")
    state = capture.wait(
        lambda s: not s["placing"] and s["feedbackOpacity"] == 1,
        "blocked build button feedback visible without entering placement",
    )
    require(wallet(state, owner)["wallet"] == 0, "blocked build click spent Power")
    capture.shot("blocked-build-feedback-full")
    capture.wait(
        lambda s: 0.25 < s["feedbackOpacity"] < 0.75,
        "message fades after its hold",
    )
    capture.shot("blocked-build-feedback-fading")
    capture.wait(
        lambda s: s["feedbackOpacity"] == 0 and not s["orderFeedback"],
        "expired message clears",
    )
    capture.shot("blocked-build-feedback-expired")
    run.phase("blocked build click explains itself; feedback fades and expires")
    run.request("host", "income", paused=income_paused)
