"""HUD goal targeting, cancellation and immediate-goal acceptance."""

from __future__ import annotations

from typing import cast

from harness.hud_actions import GOAL_ASSAULT, GOAL_EXPAND, GOAL_FALL_BACK, GOAL_HOLD
from harness.hud_surface import Capture
from harness.network import (
    ASSAULT,
    EXPAND,
    FALL_BACK,
    HOLD,
    building,
    goal_matches,
    require,
    select_goal_region,
)
from harness.verify import JsonObject


def cancel_goals(capture: Capture, barracks: int) -> None:
    for action, goal, label in (
        (GOAL_HOLD, HOLD, "hold"),
        (GOAL_EXPAND, EXPAND, "expand"),
    ):
        before_goal = building(capture.state(), barracks)
        capture.hud(action, f"{label.title()} enters region targeting")

        def picking_goal(s: JsonObject, goal: int = goal) -> object:
            return (
                s["assigningGoal"] and s["pendingGoal"] == goal and not s["hudExpanded"]
            )

        capture.wait(picking_goal, f"{label} region targeting mode")
        capture.shot(f"{label}-region-mode")
        capture.key("F")
        capture.key("Escape")
        cancelled = capture.wait(
            lambda s: not s["assigningGoal"] and s["hudExpanded"],
            f"Escape cancels {label} region targeting",
        )
        require(
            goal_matches(
                cancelled,
                barracks,
                before_goal["forceGoal"],
                before_goal["goalRegionIndex"],
            ),
            "cancelling a region pick mutated the existing goal",
        )


def assign_goals(capture: Capture, barracks: int) -> int:
    enemy_main = next(
        r["index"] for r in capture.state()["regions"] if r["homeTeam"] == 5
    )
    capture.hud(GOAL_ASSAULT, "Assault applies immediately without a region pick")
    capture.wait(
        lambda s: (
            not s["assigningGoal"] and goal_matches(s, barracks, ASSAULT, enemy_main)
        ),
        "Assault resolves the enemy main",
    )
    capture.shot("assault-goal")
    capture.hud(GOAL_FALL_BACK, "Fall Back applies immediately")
    capture.wait(
        lambda s: (
            not s["assigningGoal"] and building(s, barracks)["forceGoal"] == FALL_BACK
        ),
        "Fall Back goal without targeting",
    )
    capture.shot("fall-back-goal")
    target = select_goal_region(capture.state(), barracks)["index"]
    capture.hud(GOAL_EXPAND, "Expand picks a region through the minimap")
    capture.wait(
        lambda s: s["assigningGoal"] and s["pendingGoal"] == EXPAND,
        "Expand minimap pick mode",
    )
    capture.pick_region(barracks, EXPAND, target)
    capture.shot("expand-region-assigned")
    capture.hud(GOAL_HOLD, "Hold replaces Expand on the selected region")
    capture.wait(
        lambda s: s["assigningGoal"] and s["pendingGoal"] == HOLD,
        "Hold minimap pick mode",
    )
    capture.pick_region(barracks, HOLD, target)
    return cast(int, target)
