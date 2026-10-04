"""Rendered capture of the Human Lancer and Scrambler produced at real Barracks."""

from __future__ import annotations

from harness.hud_setup import boot, place_barracks
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import BARRACKS, NetworkRun, building, owned_buildings, require

# EUnitRole ordinals appended by combat-shields-traits.
ASSAULT, SUPPORT = 3, 4


def produce_squad(
    run: NetworkRun, capture: Capture, owner: int, count: int, role: int, label: str
) -> int:
    """Place the count-th Barracks, lock it to `role` through the production RPC and wait for a full squad."""
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, count, f"{label} barracks placed")
    index = owned_buildings(state, owner, BARRACKS)[-1]["index"]
    capture.wait(
        lambda s: building(s, index)["constructionProgress"] == 1,
        f"{label} barracks complete",
    )
    run.request("host", "production", building=index, recipe=role, enabled=True)
    state = capture.wait(
        lambda s: (
            building(s, index)["configured"] and building(s, index)["recipe"] == role
        ),
        f"{label} recipe locked",
    )
    capacity = building(state, index)["capacity"]
    require(capacity == 3, f"{label} squad capacity is {capacity}, expected 3")
    run.request(
        "host",
        "fund",
        owner=owner,
        amount=capacity * building(state, index)["unitCost"],
    )
    capture.wait(
        lambda s: (
            building(s, index)["joined"] == capacity
            and building(s, index)["travelling"] == 0
        ),
        f"{label} squad of {capacity} joined",
    )
    capture.shot(f"{label}-squad")
    return int(index)


def scenario(run: NetworkRun, resolution: tuple[int, int], support_first: bool) -> None:
    """Force 1 stands in the open and force 2 behind the second Barracks, so each order shows one squad clearly."""
    capture = Capture(run)
    pid, state = boot(run, capture, resolution)
    owner = state["localIndex"]
    order = [(SUPPORT, "scrambler"), (ASSAULT, "lancer")] if support_first else [(ASSAULT, "lancer"), (SUPPORT, "scrambler")]
    for count, (role, label) in enumerate(order, start=1):
        produce_squad(run, capture, owner, count, role, label)
    capture.shot("both-squads")
    final = capture.state()
    run.event(
        "UNIT_POSITIONS",
        units=[
            dict(role=unit["role"], position=unit["position"])
            for army in final["armies"]
            for unit in army["units"]
            if unit["health"] > 0
        ],
    )
    no_compositor_windows(run, pid)
    quick = "new-units-support-first" if support_first else "new-units"
    run.event("PASS", captures=capture.count, resolutions=[resolution], quick=quick)
