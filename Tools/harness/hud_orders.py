"""HUD force-order targeting, cancellation and immediate Retreat acceptance."""

from __future__ import annotations

from typing import cast

from harness.hud_actions import ORDER_ATTACK, ORDER_MOVE_HOLD, ORDER_RETREAT
from harness.hud_surface import Capture
from harness.network import (
    ATTACK,
    MOVE_HOLD,
    RETREAT,
    building,
    order_matches,
    require,
    select_order_region,
)
from harness.verify import JsonObject


def cancel_orders(capture: Capture, barracks: int) -> None:
    for action, verb, label in (
        (ORDER_MOVE_HOLD, MOVE_HOLD, "move-hold"),
        (ORDER_ATTACK, ATTACK, "attack"),
    ):
        before_order = building(capture.state(), barracks)
        capture.hud(action, f"{label.title()} enters region targeting")

        def picking_order(s: JsonObject, verb: int = verb) -> object:
            return (
                s["assigningOrder"]
                and s["pendingVerb"] == verb
                and not s["hudExpanded"]
            )

        capture.wait(picking_order, f"{label} region targeting mode")
        capture.shot(f"{label}-region-mode")
        capture.key("F")
        capture.key("Escape")
        cancelled = capture.wait(
            lambda s: not s["assigningOrder"] and s["hudExpanded"],
            f"Escape cancels {label} region targeting",
        )
        require(
            order_matches(
                cancelled,
                barracks,
                before_order["forceVerb"],
                before_order["targetRegionIndex"],
            ),
            "cancelling a region pick mutated the existing force order",
        )


def assign_orders(capture: Capture, barracks: int) -> int:
    enemy_main = next(
        r["index"] for r in capture.state()["regions"] if r["homeTeam"] == 5
    )
    capture.hud(ORDER_ATTACK, "Attack requires a region pick")
    capture.wait(
        lambda s: s["assigningOrder"] and s["pendingVerb"] == ATTACK,
        "Attack region targeting",
    )
    capture.pick_region(barracks, ATTACK, enemy_main)
    capture.shot("attack-order")
    capture.hud(ORDER_RETREAT, "Retreat applies immediately")
    capture.wait(
        lambda s: (
            not s["assigningOrder"] and order_matches(s, barracks, RETREAT, -1)
        ),
        "Retreat order without targeting",
    )
    capture.shot("retreat-order")
    target = select_order_region(capture.state(), barracks)["index"]
    capture.hud(ORDER_ATTACK, "Attack picks a region through the minimap")
    capture.wait(
        lambda s: s["assigningOrder"] and s["pendingVerb"] == ATTACK,
        "Attack minimap pick mode",
    )
    capture.pick_region(barracks, ATTACK, target)
    capture.shot("attack-region-assigned")
    capture.hud(ORDER_MOVE_HOLD, "Move + Hold replaces Attack on the selected region")
    capture.wait(
        lambda s: s["assigningOrder"] and s["pendingVerb"] == MOVE_HOLD,
        "Move + Hold minimap pick mode",
    )
    capture.pick_region(barracks, MOVE_HOLD, target)
    return cast(int, target)
