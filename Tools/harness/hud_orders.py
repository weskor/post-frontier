"""Rendered selected-force right-click previews, A targeting and immediate R."""

from __future__ import annotations

from typing import cast

from harness.hud_surface import Capture
from harness.network import (
    ATTACK,
    MOVE_HOLD,
    RETREAT,
    building,
    minimap_region_point,
    minimap_world_point,
    order_matches,
    require,
    select_order_region,
    wallet,
)


def cancel_orders(capture: Capture, barracks: int) -> None:
    capture.select_force(barracks)
    panel_actions_during_attack(capture, barracks)
    for cancellation in ("Escape", "right-click"):
        before = building(capture.state(), barracks)
        capture.begin_attack()
        capture.shot(f"attack-mode-cancel-{cancellation}")
        if cancellation == "Escape":
            capture.key("Escape")
        else:
            x, y = minimap_region_point(capture.state(), before["targetRegionIndex"])
            capture.run.request("host", "orderClick", x=x, y=y)
        cancelled = capture.wait(
            lambda s: not s["assigningOrder"] and s["hudExpanded"],
            f"{cancellation} cancels pending A and restores the deck",
        )
        require(
            order_matches(
                cancelled, barracks, before["forceVerb"], before["targetRegionIndex"]
            ),
            "cancelling A mutated the selected force's existing order",
        )


def panel_actions_during_attack(capture: Capture, barracks: int) -> None:
    before = building(capture.state(), barracks)
    capture.begin_attack()
    capture.hud(43, "Pause stays usable during A targeting")
    capture.wait(
        lambda s: s["activePaused"] and s["assigningOrder"] and not s["hudExpanded"],
        "Pause dispatches without consuming the Attack target",
    )
    capture.hud(43, "Resume stays usable during A targeting")
    capture.wait(
        lambda s: not s["activePaused"] and s["assigningOrder"],
        "Resume dispatches without consuming the Attack target",
    )
    screen = capture.state()["uiScreen"]
    capture.hud(33, "MENU stays usable during A targeting")
    capture.wait(
        lambda s: (
            s["uiScreen"] != screen and not s["assigningOrder"] and s["hudExpanded"]
        ),
        "MENU opens and cancels A, restoring the deck",
    )
    capture.hud(23, "Resume the game after the targeting MENU regression")
    capture.wait(lambda s: s["uiScreen"] == screen, "MENU resumes the game")
    capture.begin_attack()
    state = capture.state()
    owner = state["localIndex"]
    budget = wallet(state, owner)["wallet"]
    capture.run.request("host", "fund", owner=owner, amount=220)
    capture.hud(1, "Construction build choice stays usable during A targeting")
    capture.wait(
        lambda s: s["placing"] and not s["assigningOrder"] and not s["hudExpanded"],
        "Construction build choice replaces A with placement",
    )
    capture.key("Escape")
    restored = capture.wait(
        lambda s: not s["placing"] and not s["assigningOrder"] and s["hudExpanded"],
        "Cancelling construction restores the deck",
    )
    capture.run.request("host", "fund", owner=owner, amount=budget)
    require(
        order_matches(
            restored, barracks, before["forceVerb"], before["targetRegionIndex"]
        ),
        "panel actions during A mutated the selected force's order",
    )


def assign_orders(capture: Capture, barracks: int) -> int:
    state = capture.select_force(barracks)
    target = select_order_region(state, barracks)["index"]
    smart_previews(capture, barracks, target)
    rejected_attack(capture, barracks, target)
    capture.order_region(barracks, MOVE_HOLD, target)
    return cast(int, target)


def smart_previews(capture: Capture, barracks: int, target: int) -> None:
    state = capture.state()
    x, y = minimap_region_point(state, target)
    preview = capture.preview(x, y)
    require(
        preview["allowed"]
        and preview["resolution"] == 1
        and preview["regionIndex"] == target
        and preview["structureId"] == -1
        and preview["label"] == "Move & Hold",
        "region right-click preview does not resolve to Move & Hold",
    )
    capture.shot("cursor-preview-move-hold")
    capture.order_region(barracks, MOVE_HOLD, target)

    state = capture.state()
    enemy_main = next(r["index"] for r in state["regions"] if r["homeTeam"] == 5)
    x, y = minimap_world_point(state, state["enemyHQPosition"])
    preview = capture.preview(x, y)
    require(
        preview["allowed"]
        and preview["resolution"] == 2
        and preview["structureId"] == state["enemyHQId"]
        and preview["label"] == "Attack",
        "hostile headquarters right-click preview does not resolve to Attack",
    )
    capture.shot("cursor-preview-hostile-structure-attack")
    capture.run.request("host", "orderClick", x=x, y=y)
    capture.wait(
        lambda s: (
            order_matches(s, barracks, ATTACK, enemy_main)
            and building(s, barracks)["targetStructureId"] == s["enemyHQId"]
        ),
        "smart right-click matches the hostile-structure Attack preview",
    )
    capture.shot("attack-order")
    capture.key("R")
    capture.wait(
        lambda s: not s["assigningOrder"] and order_matches(s, barracks, RETREAT, -1),
        "R immediately retreats the selected force without region targeting",
    )
    capture.shot("retreat-order")


def rejected_attack(capture: Capture, barracks: int, target: int) -> None:
    capture.begin_attack()
    state = capture.state()
    origin, size = state["minimapOrigin"], state["minimapSize"]
    # Arena corners lie outside the playable regions; resolve an actual map
    # point, never a HUD panel that must continue dispatching its own action.
    for horizontal, vertical in (
        (0.01, 0.01),
        (0.99, 0.01),
        (0.01, 0.99),
        (0.99, 0.99),
    ):
        x, y = origin[0] + size * horizontal, origin[1] + size * vertical
        preview = capture.preview(x, y)
        if not preview["allowed"]:
            break
    else:
        raise AssertionError("fixture has no invalid minimap corner for rejected A")
    require(
        not preview["allowed"]
        and preview["resolution"] == 0
        and preview["rejection"] != 0
        and bool(preview["label"]),
        "rejected cursor preview omits its reason",
    )
    capture.shot("cursor-preview-rejected-with-reason")
    before = building(capture.state(), barracks)
    capture.run.request("host", "hudClick", x=x, y=y)
    rejected = capture.wait(
        lambda s: (
            s["assigningOrder"]
            and s["pendingVerb"] == ATTACK
            and not s["hudExpanded"]
            and bool(s["orderFeedback"])
            and s["feedbackOpacity"] > 0
        ),
        "rejected Attack keeps A open and explains why",
    )
    require(
        rejected["orderFeedback"] == preview["label"]
        and order_matches(
            rejected, barracks, before["forceVerb"], before["targetRegionIndex"]
        ),
        "rejected Attack disagrees with its preview or mutated the force order",
    )
    capture.shot("attack-rejected-mode-open")
    capture.order_region(barracks, ATTACK, target)
    capture.shot("attack-region-assigned")
