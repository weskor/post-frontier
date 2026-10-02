"""Atomic rejection of locked, invalid and foreign production/goal commands."""

from __future__ import annotations

from harness.network import (
    ASSAULT,
    EXPAND,
    FALL_BACK,
    FRONTLINE,
    HOLD,
    RANGED,
    SIEGE,
    NetworkRun,
    building,
    goal_matches,
    require,
    wallet,
)
from harness.network_session import Session, converged
from harness.verify import JsonObject


def issue_rejected_commands(run: NetworkRun, s: Session, index: int) -> JsonObject:
    before = run.observe("host")
    run.request(s.peer, "production", building=index, recipe=RANGED, enabled=True)
    run.request(s.peer, "production", building=index, recipe=255, enabled=True)
    enemy_main = next(r["index"] for r in before["regions"] if r["homeTeam"] == 5)
    invalid_region = max(r["index"] for r in before["regions"]) + 1
    run.request(
        s.peer,
        "goal",
        building=index,
        goal=255,
        region=building(before, index)["goalRegionIndex"],
    )
    for goal in (HOLD, EXPAND):
        run.request(s.peer, "goal", building=index, goal=goal, region=invalid_region)
        run.request(s.peer, "goal", building=index, goal=goal, region=enemy_main)
    for goal in (ASSAULT, FALL_BACK):
        run.request(s.peer, "goal", building=index, goal=goal, region=enemy_main)
    if s.peer != "host":
        foreign_before = wallet(before, s.identities["host"])["wallet"]
        run.request(
            "host", "production", building=index, recipe=FRONTLINE, enabled=True
        )
        run.request("host", "goal", building=index, goal=FALL_BACK, region=-1)
        require(
            wallet(run.observe("host"), s.identities["host"])["wallet"]
            == foreign_before,
            "foreign RPC debited issuing commander's wallet",
        )
    return before


def reject_locked_commands(run: NetworkRun, s: Session, index: int, squad: int) -> None:
    before = issue_rejected_commands(run, s, index)
    # Reliable RPC order on the same owning controller supplies a behavioral
    # delivery barrier; do not assert localized feedback wording or sleep for RPCs.
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    converged(
        run,
        s.names,
        lambda st: building(st, index)["enabled"],
        "owner resume after rejected role RPCs",
    )
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    states = converged(
        run,
        s.names,
        lambda st: not building(st, index)["enabled"],
        "owner pauses after rejection barrier",
    )
    require(
        all(
            not building(st, index)["enabled"]
            and building(st, index)["recipe"] == SIEGE
            and building(st, index)["front"] == building(before, index)["front"]
            and building(st, index)["frontOrder"]
            == building(before, index)["frontOrder"]
            and goal_matches(
                st,
                index,
                building(before, index)["forceGoal"],
                building(before, index)["goalRegionIndex"],
            )
            and building(st, index)["forceID"] == squad
            and building(st, index)["productionSeconds"]
            == building(before, index)["productionSeconds"]
            and wallet(st, s.owner)["wallet"] == 0
            for st in states.values()
        ),
        "locked/invalid/foreign commands changed force configuration, progress, goal, waypoint or wallet",
    )
    run.phase(
        "owner RPCs, permanent siege configuration and atomic rejection of invalid/foreign goals and locked types"
    )
