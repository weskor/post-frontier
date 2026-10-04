"""Rendered pressure HUD: the JEV release timeline, the battle clock, the LINE CUT chip and a stunned Barracks.

Run on the Habitable Zone v2 map (a Drill Rig deposit beyond a neck): ./x verify hud --mode editor
--map /Game/Maps/AvailabilityZoneV2 --quick pressure. JEV is left running, so the empty timeline, the v1.1 release
cell in its last 30 s and the wave it sends are JEV's own; only the cut (region control, a Drill Rig) and the stun are
host fixtures (mapPres*, pressureStun, recorded in events.jsonl). Everything on screen is drawn by the real HUD from
replicated state. Each state is captured at every --res: the viewport changes while the state holds.
"""

from __future__ import annotations

from collections.abc import Sequence
import time

from harness.hud_fortify import viewport
from harness.hud_map_presentation import (
    FAR,
    NECK,
    ZOOM_OUT,
    deck,
    focus,
    set_control,
    zoom,
)
from harness.hud_setup import boot, place_barracks
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import (
    BARRACKS,
    NetworkRun,
    building,
    owned_buildings,
    region,
    require,
)

# JEV's schedule (decisions J1): v1.1 at 120 s shows 30 s ahead, from 90 s.
RELEASE_WINDOW = 95
WAVE_AFTER = 122
# The stun holds 3 s of game time; a tenth-speed clock keeps its chip up across a capture.
STUN_SLOW = 0.1
STUN_SECONDS = 3
ACTIVE_PAUSE = 43
# Real seconds the paused clock is watched.
PAUSE_WATCH = 4


def at_each(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    label: str,
) -> None:
    """The same state at every resolution, then back to the first for the next state."""
    for width, height in resolutions:
        viewport(run, capture, width, height)
        capture.shot(f"{label}-{width}x{height}")
    if len(resolutions) > 1:
        viewport(run, capture, *resolutions[0])


def empty_timeline(
    run: NetworkRun, capture: Capture, resolutions: Sequence[tuple[int, int]]
) -> None:
    state = capture.state()
    require(
        not state["jevIntent"]["entries"] and "timelineRect" in state["jevIntent"],
        "the timeline is not empty or has lost its reserved bar before 90 s",
    )
    at_each(run, capture, resolutions, "empty-timeline")


def stunned_barracks(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
) -> None:
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "owned barracks placed")
    barracks = owned_buildings(state, owner, BARRACKS)[0]["index"]
    capture.wait(
        lambda s: building(s, barracks)["constructionProgress"] == 1,
        "barracks complete",
    )
    # F centres the camera on the selected Barracks so its overlay is on screen.
    capture.key("F")
    run.request("host", "mapPresDilation", factor=STUN_SLOW)
    run.request("host", "pressureStun", seconds=STUN_SECONDS)
    at_each(run, capture, resolutions, "stunned-barracks")
    run.request("host", "mapPresDilation", factor=1)


def line_cut(
    run: NetworkRun, capture: Capture, resolutions: Sequence[tuple[int, int]]
) -> None:
    for index in (NECK, FAR):
        set_control(run, capture, index, 0)
    run.request("host", "mapPresRig", region=FAR)
    set_control(run, capture, NECK, 5)
    at_each(run, capture, resolutions, "line-cut")
    set_control(run, capture, NECK, 0)


def timed(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    seconds: float,
    label: str,
    needs_plan: bool,
) -> None:
    capture.wait(
        lambda s: (
            s["serverTime"] >= seconds and (not needs_plan or bool(s["jevPlans"]))
        ),
        f"{label}: game time {seconds:.0f} s" + (" and a plan" if needs_plan else ""),
    )
    at_each(run, capture, resolutions, label)


def badge_beside_deposit(
    run: NetworkRun, capture: Capture, resolutions: Sequence[tuple[int, int]]
) -> None:
    """A JEV badge over the Uplink region, whose Drill Rig label sits right beside it, at each viewport."""
    capture.wait(
        lambda s: any(b["region"] == FAR for b in s["jevIntent"]["badges"]),
        "a JEV plan targets the Uplink region",
    )
    deck(capture, False)
    for width, height in resolutions:
        viewport(run, capture, width, height)
        tall = height >= 900
        focus(run, capture, FAR, 0.5 if tall else 0.8)
        steps = ZOOM_OUT[900 if tall else 720]
        zoom(run, capture, steps)
        capture.shot(f"badge-beside-deposit-{width}x{height}")
        zoom(run, capture, -steps)
    deck(capture, True)
    viewport(run, capture, *resolutions[0])


def clock_frozen_by_pause(run: NetworkRun, capture: Capture) -> None:
    """Pause stops the game clock the battle clock reads: two captures apart show the same time."""
    capture.hud(ACTIVE_PAUSE, "active pause through the on-screen P button")
    paused = capture.wait(lambda s: s["activePaused"] and s["worldPaused"], "paused")
    capture.shot("clock-paused-first")
    time.sleep(PAUSE_WATCH)
    later = capture.state()
    require(
        later["serverTime"] == paused["serverTime"],
        f"game time moved while paused: {paused['serverTime']} -> {later['serverTime']}",
    )
    capture.shot("clock-paused-later")
    capture.hud(ACTIVE_PAUSE, "resume")
    capture.wait(lambda s: not s["worldPaused"], "resumed")


def near(position: Sequence[float], anchor: Sequence[float]) -> bool:
    return abs(position[0] - anchor[0]) < 100 and abs(position[1] - anchor[1]) < 100


def click_focus(run: NetworkRun, capture: Capture) -> None:
    """A click on the LINE CUT chip and on a plan cell moves the camera to what each points at."""
    state = capture.state()
    require(
        state["viewportWidth"] >= 1280 and state["viewportHeight"] >= 720,
        "click positions assume HUD scale 1.0",
    )
    set_control(run, capture, NECK, 5)
    anchor = region(capture.state(), FAR)["anchor"]
    # The chip sits at the top bar's fixed slot (Top.X + Pad + 400 + Gap), its full 32 px height clickable.
    run.request("host", "hudClick", x=553, y=26)
    capture.wait(
        lambda s: near(s["cameraPosition"], anchor), "camera on the cut region"
    )
    capture.shot("click-cut-chip")
    set_control(run, capture, NECK, 0)
    state = capture.state()
    entries = state["jevIntent"]["entries"]
    cell = next(i for i, entry in enumerate(entries) if entry["target"] >= 0)
    bar = state["jevIntent"]["timelineRect"]
    cell_width = (bar[2] - 88) / 4
    x = bar[0] + 84 + (cell + 0.5) * cell_width
    run.request("host", "hudClick", x=x, y=bar[1] + bar[3] / 2)
    target = region(state, entries[cell]["target"])["anchor"]
    capture.wait(
        lambda s: near(s["cameraPosition"], target), "camera on the plan's target"
    )
    capture.shot("click-plan-cell")


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0], isolate=False)
    owner = state["localIndex"]
    empty_timeline(run, capture, resolutions)
    stunned_barracks(run, capture, resolutions, owner)
    line_cut(run, capture, resolutions)
    # The release cell holds from 90 s to the release at 120 s; capture it in the second half of that window.
    timed(run, capture, resolutions, RELEASE_WINDOW, "release-cell", False)
    # The v1.1 wave is an ordinary plan cell after the release has gone.
    timed(run, capture, resolutions, WAVE_AFTER, "wave-cell", True)
    badge_beside_deposit(run, capture, resolutions)
    clock_frozen_by_pause(run, capture)
    click_focus(run, capture)
    no_compositor_windows(run, pid)
    run.event(
        "PASS",
        captures=capture.count,
        resolutions=resolutions,
        scenario="pressure",
    )
