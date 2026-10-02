"""Boot an isolated rendered host and place capture fixtures."""

from __future__ import annotations

from harness.hud_actions import BUILD_BARRACKS
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import BARRACKS, NetworkRun, building, owned_buildings, require
from harness.verify import JsonObject


def boot(
    run: NetworkRun, capture: Capture, resolution: tuple[int, int]
) -> tuple[int, JsonObject]:
    """Rendered, isolated listen host with a fresh expanded deck at the requested viewport."""
    run.start("host", host=True)
    pid = run.peers["host"]["process"].pid
    state = capture.wait(
        lambda s: (
            s["localIndex"] >= 0
            and bool(s["sites"])
            and bool(s["regions"])
            and s["viewportWidth"] > 0
        ),
        "rendered listen host with commander and regions",
    )
    no_compositor_windows(run, pid)
    run.request("host", "isolate")
    run.phase("isolated enemy planner for stable presentation states (fixture)")
    capture.wait(
        lambda s: s["hudExpanded"] and not s["buildingSelected"], "fresh expanded deck"
    )
    # The offscreen null platform ignores -ResX/-ResY; keep that launch size as a small-viewport check.
    launch = (state["viewportWidth"], state["viewportHeight"])
    if launch != resolution:
        capture.shot(f"start-overview-launch-{launch[0]}x{launch[1]}")
        width, height = resolution
        run.request("host", "resolution", width=width, height=height)
        state = capture.wait(
            lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height),
            f"viewport {width}x{height}",
        )
    return pid, state


def place_barracks(
    run: NetworkRun, capture: Capture, owner: int, count: int, description: str
) -> JsonObject:
    candidate = run.request("host", "placement", kind=BARRACKS)["placementCandidate"]
    if not run.observe("host")["placing"]:
        capture.hud(BUILD_BARRACKS, "Barracks on the always-visible build bar")
    run.request("host", "place", x=candidate[0], y=candidate[1])
    return capture.wait(
        lambda s: len(owned_buildings(s, owner, BARRACKS)) == count, description
    )


def quick(run: NetworkRun, label: str, resolution: tuple[int, int]) -> None:
    if label == "pings":
        from harness.network_pings import pings_hud_scenario

        pings_hud_scenario(run, resolution)
        run.event("PASS", quick=label, resolutions=[resolution])
        return
    capture = Capture(run)
    pid, state = boot(run, capture, resolution)
    owner = state["localIndex"]
    capture.shot(f"{label}-deck")
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "owned barracks placed")
    barracks = owned_buildings(state, owner, BARRACKS)[0]["index"]
    run.request("host", "select", target="building", building=barracks)
    state = capture.wait(
        lambda s: (
            s["buildingSelected"] and building(s, barracks)["constructionProgress"] == 1
        ),
        "selected barracks complete",
    )
    require(
        building(state, barracks)["productionState"] == "Unconfigured",
        "fresh completed barracks does not present as unconfigured",
    )
    capture.shot(f"{label}-inspector")
    no_compositor_windows(run, pid)
    run.event("PASS", captures=capture.count, resolutions=[resolution], quick=label)
