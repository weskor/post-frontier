"""Rendered capture of the Machine-faction Lancer and Scrambler produced at real JEV Barracks."""

from __future__ import annotations

from harness.hud_map_presentation import settle, zoom
from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import BARRACKS, NetworkRun, building, require
from harness.verify import JsonObject

# EUnitRole ordinals appended by combat-shields-traits.
ASSAULT, SUPPORT = 3, 4
JEV_TEAM = 5
# Wheel steps out so both squads, possibly a region apart, fit in one frame.
ZOOM_OUT = {900: -7, 720: -10}


def jev_barracks(state: JsonObject) -> list[int]:
    return [
        int(b["index"])
        for b in state["buildings"]
        if b["team"] == JEV_TEAM and b["kind"] == BARRACKS
    ]


def squad_centre(state: JsonObject, role: int) -> list[float]:
    """Ground centre of the living JEV units of `role`."""
    units = [
        unit
        for army in state["armies"]
        if army["team"] == JEV_TEAM
        for unit in army["units"]
        if unit["health"] > 0 and unit["role"] == role
    ]
    require(
        len(units) == 3, f"{len(units)} living JEV units of role {role}, expected 3"
    )
    return [sum(unit["position"][axis] for unit in units) / 3 for axis in (0, 1)]


def focus(
    run: NetworkRun, capture: Capture, centre: list[float], description: str
) -> None:
    run.request("host", "jevFocus", x=centre[0], y=centre[1])
    settle(capture, description)


def produce_squad(
    run: NetworkRun, capture: Capture, role: int, label: str
) -> list[float]:
    """The isolated host places a completed JEV Barracks, locks it to `role`, and waits for a full squad."""
    before = set(jev_barracks(capture.state()))
    run.request("host", "jevProduction", role=role)
    state = capture.wait(
        lambda s: len(set(jev_barracks(s)) - before) == 1,
        f"{label} JEV barracks placed",
    )
    index = (set(jev_barracks(state)) - before).pop()
    state = capture.wait(
        lambda s: (
            building(s, index)["configured"]
            and building(s, index)["recipe"] == role
            and building(s, index)["capacity"] > 0
        ),
        f"{label} JEV recipe locked",
    )
    capacity = building(state, index)["capacity"]
    require(capacity == 3, f"{label} squad capacity is {capacity}, expected 3")
    capture.wait(
        lambda s: (
            building(s, index)["joined"] == capacity
            and building(s, index)["travelling"] == 0
        ),
        f"{label} squad of {capacity} joined",
    )
    centre = squad_centre(capture.state(), role)
    focus(run, capture, centre, f"camera on the {label} squad")
    capture.shot(f"{label}-squad")
    return centre


def scenario(run: NetworkRun, resolution: tuple[int, int]) -> None:
    """Machine faction: the Lancer squad, the Scrambler squad, then both with the camera between them."""
    capture = Capture(run)
    pid, _ = boot(run, capture, resolution)
    squads = [
        produce_squad(run, capture, ASSAULT, "jev-lancer"),
        produce_squad(run, capture, SUPPORT, "jev-scrambler"),
    ]
    middle = [sum(centre[axis] for centre in squads) / len(squads) for axis in (0, 1)]
    focus(run, capture, middle, "camera between the squads")
    zoom(run, capture, ZOOM_OUT.get(resolution[1], -7))
    capture.shot("jev-both-squads")
    final = capture.state()
    units = [
        unit
        for army in final["armies"]
        if army["team"] == JEV_TEAM
        for unit in army["units"]
        if unit["health"] > 0
    ]
    for role, label in ((ASSAULT, "Lancer"), (SUPPORT, "Scrambler")):
        count = sum(1 for unit in units if unit["role"] == role)
        require(count == 3, f"JEV fields {count} {label}s, expected one squad of 3")
    run.event(
        "UNIT_POSITIONS",
        units=[dict(role=unit["role"], position=unit["position"]) for unit in units],
    )
    no_compositor_windows(run, pid)
    run.event(
        "PASS", captures=capture.count, resolutions=[resolution], quick="jev-units"
    )
