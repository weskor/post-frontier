"""Rendered map presentation: region traits, supply cuts at the cut and five seconds later, the Scrambler pulse ring.

Run on the Habitable Zone v2 map (traits and Drill Rig deposits): ./x verify hud --mode editor
--map /Game/Maps/AvailabilityZoneV2 --quick map-presentation. Region state is set by host fixtures (mapPres*
actions, recorded in events.jsonl); everything on screen is drawn from the replicated state by the real HUD.
Game time is slowed with mapPresDilation so a one-second flash and a 0.4 s ring hold still for a capture.
"""

from __future__ import annotations

from collections.abc import Callable, Sequence

from harness.hud_fortify import ability, region_state, viewport
from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import NetworkRun, require
from harness.verify import JsonObject

FORTIFY_FOCUS = "fortifyHudFocus"
# Habitable Zone v2: West Cut is a neck (cover); Uplink beyond it holds rich deposits; the others carry one trait each.
NECK, FAR = 2, 3
TRAIT_REGIONS = {"cover": 2, "high-ground": 7, "open": 8, "hazard": 10}
# A cut flashes for one second; a capture needs the first flash still lit.
CUT_SLOW = 0.1
PULSE_SLOW = 0.05
RING_AGES = (0.12, 0.28, 0.4)


def focus(run: NetworkRun, capture: Capture, region: int) -> None:
    run.request("host", FORTIFY_FOCUS, region=region)
    previous: list[object] = []

    def settled(state: JsonObject) -> bool:
        position = state["cameraPosition"]
        done = bool(previous) and previous[-1] == position
        previous.append(position)
        return done

    capture.wait(settled, f"camera settled on region {region}")


def controller(state: JsonObject, region: int) -> int:
    return int(region_state(state, region)["controller"])


def set_control(run: NetworkRun, capture: Capture, region: int, team: int) -> None:
    run.request("host", "mapPresControl", region=region, team=team)
    capture.wait(
        lambda s: controller(s, region) == team, f"region {region} held by team {team}"
    )


def traits(run: NetworkRun, capture: Capture, suffix: str) -> None:
    for name, region in TRAIT_REGIONS.items():
        focus(run, capture, region)
        capture.shot(f"traits-{name}-region-{region}-{suffix}")


def cut_states(run: NetworkRun, capture: Capture, suffix: str) -> None:
    """The cut, at the moment it lands and after the flash window has closed."""
    for region in (NECK, FAR):
        set_control(run, capture, region, 0)
    focus(run, capture, FAR)
    capture.shot(f"cut-before-connected-{suffix}")
    run.request("host", "mapPresDilation", factor=CUT_SLOW)
    set_control(run, capture, NECK, 5)
    capture.shot(f"cut-moment-{suffix}")
    run.request("host", "mapPresDilation", factor=1)
    first = capture.state()["serverTime"]
    capture.wait(lambda s: s["serverTime"] - first >= 5, "five seconds after the cut")
    capture.shot(f"cut-steady-5s-{suffix}")
    for region in (NECK, FAR):
        set_control(run, capture, region, 0)
    capture.shot(f"cut-reconnected-{suffix}")


def older_than(start: float, seconds: float) -> Callable[[JsonObject], bool]:
    def reached(state: JsonObject) -> bool:
        return bool(state["serverTime"] - start >= seconds)

    return reached


def pulse(run: NetworkRun, capture: Capture, suffix: str) -> None:
    """A Scrambler beside a hostile shielded unit: ring ages sampled on slowed game time."""
    run.request("host", "mapPresDilation", factor=PULSE_SLOW)
    start = float(run.request("host", "mapPresPulse", region=NECK)["serverTime"])
    for age in RING_AGES:
        capture.wait(older_than(start, age), f"pulse ring {age:.2f} s old")
        capture.shot(f"pulse-ring-{int(age * 100)}cs-{suffix}")
    run.request("host", "mapPresDilation", factor=1)
    run.request("host", "mapPresClear")


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, _ = boot(run, capture, resolutions[0])
    run.request("host", "income", paused=True)
    regions = {int(r["index"]): r for r in ability(capture.state())["regions"]}
    require(
        all(index in regions for index in (NECK, FAR, *TRAIT_REGIONS.values())),
        "map presentation needs the Habitable Zone v2 regions",
    )
    run.request("host", "jevPlans", stage="create")
    run.request("host", "jevPlans", stage="replace")
    for position, (width, height) in enumerate(resolutions):
        viewport(run, capture, width, height)
        suffix = f"{width}x{height}"
        traits(run, capture, suffix)
        if position == 0:
            # One finished Drill Rig on Uplink's deposit; it stays for every resolution.
            run.request("host", "mapPresRig", region=FAR)
        cut_states(run, capture, suffix)
        pulse(run, capture, suffix)
    no_compositor_windows(run, pid)
    run.event(
        "PASS",
        captures=capture.count,
        resolutions=resolutions,
        scenario="map-presentation",
    )
