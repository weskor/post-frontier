"""Rendered planning phase (ui.md surface 10): clock, roster chips, READY slot, KIT bar, panel and default-spot ghosts."""

from __future__ import annotations

from collections.abc import Sequence
import os
from typing import cast

from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import RANGED, NetworkRun, require
from harness.network_planning import KEEP_PLANNING, kit
from harness.verify import JsonObject

# EHUDAction ordinals from Source/CoopRTS/CommandHUD.h.
PLAN_UNIT_RANGED = 79
# The phase lasts 60 real seconds from the match start and the captures cannot hold it open, so the run needs this much
# of it left when the first capture is taken.
MINIMUM_REMAINING = 22.0


def viewport(run: NetworkRun, capture: Capture, width: int, height: int) -> None:
    run.request("host", "resolution", width=width, height=height)
    capture.wait(
        lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height),
        f"planning viewport {width}x{height}",
    )
    # Resizing an offscreen viewport can strand its cursor on an edge and engage real edge-pan.
    run.request("host", "cursor", x=width / 2, y=height / 2)


def mine(state: JsonObject) -> JsonObject:
    return kit(state, int(state["localIndex"]))


def teammates(state: JsonObject) -> list[int]:
    own = int(state["localIndex"])
    rows = state["team"]["commanders"]
    return [int(c["index"]) for c in rows if c["team"] == 0 and c["index"] != own]


def neighbour_region(state: JsonObject) -> int:
    """A region next to the friendly home region: reachable from the Barracks for a first order."""
    regions = cast(list[JsonObject], state["regions"])
    home = next(r for r in regions if r["homeTeam"] == 0)
    return int(home["neighbours"][0])


def fill_mate(run: NetworkRun, slot: int) -> None:
    """A teammate with a finished kit who is already Ready (the roster chip reads `C2 ✓ READY`)."""
    run.request("host", "planningPlace", owner=slot, piece="barracks")
    run.request("host", "planningPlace", owner=slot, piece="rig")
    run.request("host", "planningReady", owner=slot, ready=True)


def place_barracks(run: NetworkRun, capture: Capture) -> None:
    """B then Q arms the KIT bar's Barracks placement; the click on valid ground is the controller's own placement entry."""
    capture.key("B")
    capture.key("Q")
    capture.wait(lambda s: s["placing"], "B Q arms the Barracks placement mode")
    candidate = run.request("host", "placement", kind=0)["placementCandidate"]
    capture.shot("planning-placement-armed")
    run.request("host", "place", x=candidate[0], y=candidate[1])
    capture.wait(
        lambda s: mine(s)["barracks"] is not None and not s["placing"],
        "the kit Barracks stands finished and the mode ends",
    )


def edit_kit(run: NetworkRun, capture: Capture, own: int, region: int) -> None:
    # The Rig first: the Barracks candidate the probe finds then clears it.
    run.request("host", "planningPlace", owner=own, piece="rig")
    place_barracks(run, capture)
    capture.hud(PLAN_UNIT_RANGED, "the Rifle chip picks the unit type")
    run.request("host", "planningOrder", owner=own, region=region)
    capture.wait(
        lambda s: (
            mine(s)["rig"] is not None
            and mine(s)["role"] == RANGED
            and bool(mine(s)["orders"])
        ),
        "Rig placed, unit type picked, first order queued",
    )


def open_phase(
    run: NetworkRun, capture: Capture, resolution: tuple[int, int]
) -> tuple[int, int, list[int], JsonObject]:
    """The match in its planning phase with three humans: the commander, one Ready teammate and one still placing."""
    os.environ[KEEP_PLANNING] = "1"
    try:
        pid, state = boot(run, capture, resolution, isolate=False)
    finally:
        os.environ.pop(KEEP_PLANNING, None)
    # An offscreen cursor left on a viewport edge engages real edge-pan and drifts the camera off the map (the black
    # captures of verify-bff3 and verify-095e); park it in the middle before anything is captured.
    run.request("host", "cursor", x=state["viewportWidth"] / 2, y=state["viewportHeight"] / 2)
    capture.wait(
        lambda s: s["planning"]["active"] and bool(s["planning"]["kits"]),
        "the match opens in the planning phase with the commander's kit slot",
    )
    run.request("host", "giftHudTeammates", count=2, power=200, data=0)
    state = capture.wait(
        lambda s: len(s["planning"]["kits"]) == 3, "three humans have a kit slot"
    )
    mates = teammates(state)
    fill_mate(run, mates[0])
    # JEV's own first plans, published on the frozen world, must show muted on the timeline.
    state = capture.wait(
        lambda s: (
            bool(kit(s, mates[0])["ready"])
            and s["planning"]["active"]
            and bool(s["jevIntent"]["entries"])
        ),
        "the first teammate is Ready; planning goes on",
    )
    require(
        state["planning"]["remaining"] > MINIMUM_REMAINING,
        f"only {state['planning']['remaining']:.0f} s of planning are left: run the capture again",
    )
    return pid, int(state["localIndex"]), mates, state


def camera(state: JsonObject) -> list[float]:
    return [float(v) for v in state["cameraPosition"]]


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    require(len(resolutions) == 2, "the planning capture needs two viewports")
    wide, narrow = resolutions
    capture = Capture(run)
    pid, own, mates, state = open_phase(run, capture, wide)
    home = camera(state)
    capture.shot(f"planning-unplaced-{wide[0]}x{wide[1]}")
    viewport(run, capture, *narrow)
    capture.shot(f"planning-unplaced-{narrow[0]}x{narrow[1]}")
    edit_kit(run, capture, own, neighbour_region(state))
    capture.shot(f"planning-kit-{narrow[0]}x{narrow[1]}")
    viewport(run, capture, *wide)
    capture.shot(f"planning-kit-{wide[0]}x{wide[1]}")
    require(
        camera(capture.state()) == home,
        "the camera drifted during the capture: a cursor on an edge pans it",
    )
    capture.key("Enter")
    capture.wait(lambda s: bool(mine(s)["ready"]), "Enter readies the finished kit")
    capture.shot(f"planning-ready-{wide[0]}x{wide[1]}")
    capture.key("Enter")
    capture.wait(
        lambda s: not mine(s)["ready"] and s["planning"]["active"],
        "Enter un-readies until 0:00",
    )
    # No deposit is left for a second Rig; the end of planning pays its Power back.
    run.request("host", "planningReady", owner=mates[1], ready=True)
    capture.key("Enter")
    capture.wait(
        lambda s: not s["planning"]["active"],
        "the last Ready ends planning and the battle clock starts",
    )
    capture.shot(f"planning-over-{wide[0]}x{wide[1]}")
    no_compositor_windows(run, pid)
    run.event(
        "PASS",
        captures=capture.count,
        resolutions=list(resolutions),
        scenario="planning",
    )
