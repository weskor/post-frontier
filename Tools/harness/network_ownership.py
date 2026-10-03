"""Atomic rejection of locked, invalid and foreign production/order commands."""

from __future__ import annotations

from harness.network import (
    ATTACK,
    FRONTLINE,
    MOVE_HOLD,
    RANGED,
    RETREAT,
    SIEGE,
    NetworkRun,
    building,
    force,
    order_matches,
    require,
    wallet,
)
from harness.network_session import Session, converged
from harness.verify import JsonObject


def issue_rejected_commands(run: NetworkRun, s: Session, index: int) -> JsonObject:
    before = run.observe("host")
    run.request(s.peer, "production", building=index, recipe=RANGED, enabled=True)
    run.request(s.peer, "production", building=index, recipe=255, enabled=True)
    invalid_region = max(r["index"] for r in before["regions"]) + 1
    run.request(
        s.peer,
        "forceOrderRPC",
        building=index,
        forceVerb=255,
        targetRegionIndex=building(before, index)["targetRegionIndex"],
    )
    for verb in (MOVE_HOLD, ATTACK):
        run.request(
            s.peer,
            "forceOrderRPC",
            building=index,
            forceVerb=verb,
            targetRegionIndex=invalid_region,
        )
        run.request(
            s.peer,
            "forceOrderRPC",
            building=index,
            forceVerb=verb,
            targetRegionIndex=-1,
        )
    run.request(
        s.peer,
        "forceOrderRPC",
        building=index,
        forceVerb=RETREAT,
        targetRegionIndex=invalid_region,
    )
    if s.peer != "host":
        foreign_before = wallet(before, s.identities["host"])["wallet"]
        run.request(
            "host", "production", building=index, recipe=FRONTLINE, enabled=True
        )
        run.request(
            "host",
            "forceOrderRPC",
            building=index,
            forceVerb=RETREAT,
            targetRegionIndex=-1,
        )
        require(
            wallet(run.observe("host"), s.identities["host"])["wallet"]
            == foreign_before,
            "foreign RPC debited issuing commander's wallet",
        )
    return before


def verify_force_ownership(run: NetworkRun, s: Session, index: int) -> None:
    state = run.observe(s.peer)
    army = next(a for a in state["armies"] if a["producer"] == index)
    camera = state["cameraPosition"]
    run.request(
        s.peer, "select", target="force", owner=s.owner, number=army["forceNumber"]
    )
    selected = run.observe(s.peer)
    require(
        selected["selectedForces"] == [army["actorId"]]
        and not selected["buildingSelected"]
        and selected["cameraPosition"] == camera,
        "owner force selection failed or moved camera",
    )
    if s.peer != "host":
        host = run.observe("host")
        foreign = next(a for a in host["armies"] if a["producer"] == index)
        camera = host["cameraPosition"]
        run.request(
            "host",
            "select",
            target="force",
            owner=s.owner,
            number=foreign["forceNumber"],
        )
        inspected = run.observe("host")
        require(
            inspected["selectedForces"] == []
            and inspected["inspectedForce"] == foreign["actorId"]
            and not inspected["buildingSelected"]
            and inspected["cameraPosition"] == camera,
            "teammate force entered command selection or inspection moved camera",
        )
    run.phase("replicated owner-only selection; teammate read-only force inspection")


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
            and force(st, s.owner, index)["orders"]
            == force(before, s.owner, index)["orders"]
            and force(st, s.owner, index)["status"]
            == force(before, s.owner, index)["status"]
            and force(st, s.owner, index)["waypointRegionIndex"]
            == force(before, s.owner, index)["waypointRegionIndex"]
            and order_matches(
                st,
                index,
                building(before, index)["forceVerb"],
                building(before, index)["targetRegionIndex"],
            )
            and building(st, index)["forceID"] == squad
            and building(st, index)["productionSeconds"]
            == building(before, index)["productionSeconds"]
            and wallet(st, s.owner)["wallet"] == 0
            for st in states.values()
        ),
        "locked/invalid/foreign commands changed force configuration, progress, order, waypoint or wallet",
    )
    run.phase(
        "owner RPCs, permanent siege configuration and atomic rejection of invalid/foreign orders and locked types"
    )
    verify_force_ownership(run, s, index)
