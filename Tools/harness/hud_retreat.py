"""Rendered positive Retreat movement and arrival for the force-box fixture."""

from __future__ import annotations

import time

from harness.hud_actions import ORDER_ATTACK, ORDER_MOVE_HOLD, ORDER_RETREAT
from harness.hud_surface import Capture
from harness.network import (
    ATTACK,
    HOLDING,
    MARCHING,
    MOVE_HOLD,
    RETREAT,
    RETREATING,
    NetworkRun,
    distance2,
    force,
    force_counts_match,
    order_destination_matches,
    order_matches,
    require,
)
from harness.verify import JsonObject


def at_home(state: JsonObject, army: JsonObject, home: JsonObject, deadline: float) -> bool:
    require(
        time.monotonic() < deadline,
        "box fixture force failed to return to its home region",
    )
    current = next(a for a in state["armies"] if a["actorId"] == army["actorId"])
    return distance2(current["center"], home["anchor"]) < 150**2


def prepare_home(
    run: NetworkRun, capture: Capture, owner: int
) -> tuple[JsonObject, JsonObject]:
    army = next(
        a
        for a in capture.state()["armies"]
        if a["owner"] == owner and any(u["health"] > 0 for u in a["units"])
    )
    home = next(r for r in capture.state()["regions"] if r["homeTeam"] == 0)
    index = army["producer"]
    run.request("host", "select", target="building", building=index)
    capture.hud(ORDER_MOVE_HOLD, "Establish the box fixture's last held safe region")
    capture.pick_region(index, MOVE_HOLD, home["index"])
    deadline = time.monotonic() + 90
    capture.wait(
        lambda s: (
            order_destination_matches(s, owner, index, home["index"])
            and force(s, owner, index)["status"] == HOLDING
            and at_home(s, army, home, deadline)
        ),
        "force holds home before the positive Retreat fixture",
    )
    return army, home


def recall_box_fixture(run: NetworkRun, capture: Capture, owner: int) -> None:
    army, home = prepare_home(run, capture, owner)
    index = army["producer"]
    enemy_main = next(r["index"] for r in capture.state()["regions"] if r["homeTeam"] == 5)
    capture.hud(ORDER_ATTACK, "Send the fixture force physically away from safe home")
    capture.pick_region(index, ATTACK, enemy_main)
    departed = capture.wait(
        lambda s: (
            force_counts_match(s, owner, index)
            and order_matches(s, index, ATTACK, enemy_main)
            and force(s, owner, index)["status"] == MARCHING
            and distance2(force(s, owner, index)["center"], home["anchor"]) > 1000**2
        ),
        "same force physically departs home before rendered Retreat",
    )
    origin = force(departed, owner, index)["center"]
    capture.hud(ORDER_RETREAT, "Retreat immediately returns the marching force to safety")
    capture.wait(
        lambda s: (
            not s["assigningOrder"]
            and order_matches(s, index, RETREAT, -1)
            and force(s, owner, index)["status"] == RETREATING
            and force(s, owner, index)["waypointRegionIndex"] == home["index"]
            and distance2(force(s, owner, index)["destination"], home["anchor"]) <= 75**2
            and distance2(force(s, owner, index)["center"], origin) > 200**2
            and distance2(force(s, owner, index)["center"], home["anchor"]) ** 0.5
            < distance2(origin, home["anchor"]) ** 0.5 - 200
        ),
        "rendered Retreat drives real movement to the safe home destination",
    )
    capture.shot("retreat-return-marching")
    deadline = time.monotonic() + 90
    capture.wait(
        lambda s: (
            force_counts_match(s, owner, index)
            and force(s, owner, index)["status"] != RETREATING
            and (
                order_matches(s, index, RETREAT, -1)
                or order_destination_matches(s, owner, index, home["index"])
            )
            and force(s, owner, index)["waypointRegionIndex"] == home["index"]
            and distance2(force(s, owner, index)["destination"], home["anchor"]) <= 75**2
            and at_home(s, army, home, deadline)
        ),
        "Retreat physically arrives home without an intervening replacement order",
    )
    capture.shot("retreat-arrived-safe")
    run.phase("rendered Retreat button, safe destination, same-force return movement and arrival")
