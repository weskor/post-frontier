"""Rendered capture of the Human Lancer and Scrambler produced at real Barracks."""

from __future__ import annotations

from harness.hud_jev_units import ZOOM_OUT, focus
from harness.hud_map_presentation import zoom
from harness.hud_setup import boot, place_barracks
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import (
    BARRACKS,
    NetworkRun,
    alive_units,
    building,
    force,
    owned_buildings,
    require,
)
from harness.verify import JsonObject

# EUnitRole ordinals appended by combat-shields-traits.
ASSAULT, SUPPORT = 3, 4


def squad_centre(state: JsonObject, owner: int, index: int) -> list[float]:
    """Ground centre of the living units of the force produced by Barracks `index`."""
    units = alive_units(force(state, owner, index))
    require(len(units) == 3, f"{len(units)} living units in force {index}, expected 3")
    return [sum(unit["position"][axis] for unit in units) / 3 for axis in (0, 1)]


def produce_squad(
    run: NetworkRun, capture: Capture, owner: int, count: int, role: int, label: str
) -> list[float]:
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
    centre = squad_centre(capture.state(), owner, index)
    focus(run, capture, centre, f"camera on the {label} squad")
    capture.shot(f"{label}-squad")
    return centre


def scenario(run: NetworkRun, resolution: tuple[int, int], support_first: bool) -> None:
    """Force 1 stands in the open and force 2 behind the second Barracks, so each order shows one squad clearly."""
    capture = Capture(run)
    pid, state = boot(run, capture, resolution)
    owner = state["localIndex"]
    order = (
        [(SUPPORT, "scrambler"), (ASSAULT, "lancer")]
        if support_first
        else [(ASSAULT, "lancer"), (SUPPORT, "scrambler")]
    )
    squads = [
        produce_squad(run, capture, owner, count, role, label)
        for count, (role, label) in enumerate(order, start=1)
    ]
    middle = [sum(centre[axis] for centre in squads) / len(squads) for axis in (0, 1)]
    focus(run, capture, middle, "camera between the squads")
    zoom(run, capture, ZOOM_OUT.get(resolution[1], -7))
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
