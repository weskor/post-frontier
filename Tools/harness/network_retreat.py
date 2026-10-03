"""Positive network Retreat departure, safe destination and return movement."""

from __future__ import annotations

from harness.network import (
    ATTACK,
    HOLDING,
    MARCHING,
    RETREAT,
    RETREATING,
    NetworkRun,
    distance2,
    force,
    force_arrived,
    force_counts_match,
    issue_force_order,
    order_matches,
    region,
)
from harness.network_session import Session, converged, hold_at, latched


def begin_retreat_home(run: NetworkRun, s: Session, index: int) -> int:
    state = run.observe("host")
    home = int(next(r["index"] for r in state["regions"] if r["homeTeam"] == 0))
    hold_at(run, s, index, home, "establish home as the force's last held safe region")
    converged(
        run,
        s.names,
        lambda st: (
            force_arrived(st, s.owner, index, home)
            and force(st, s.owner, index)["status"] == HOLDING
        ),
        "same force physically holds home before its Retreat departure",
    )
    outbound = next(r["index"] for r in state["regions"] if r["homeTeam"] == 5)
    retreat_from_home(run, s, index, home, outbound)
    return home


def retreat_from_home(
    run: NetworkRun, s: Session, index: int, home: int, outbound: int
) -> None:
    safe_states = {name: run.observe(name) for name in s.names}
    safe_ids = {
        st["localIndex"]: force(st, s.owner, index)["actorId"]
        for st in safe_states.values()
    }
    safe_origin = force(safe_states["host"], s.owner, index)["center"]
    issue_force_order(run, s.peer, index, ATTACK, outbound)
    departed = converged(
        run,
        s.names,
        lambda st: (
            force_counts_match(st, s.owner, index)
            and force(st, s.owner, index)["actorId"] == safe_ids[st["localIndex"]]
            and distance2(force(st, s.owner, index)["center"], safe_origin) > 1000**2
            and order_matches(st, index, ATTACK, outbound)
            and force(st, s.owner, index)["status"] == MARCHING
            and distance2(
                force(st, s.owner, index)["center"], region(st, home)["anchor"]
            )
            > 1000**2
        ),
        "same force physically departs its safe home before Retreat",
    )
    origin = force(departed["host"], s.owner, index)["center"]
    issue_force_order(run, s.peer, index, RETREAT)
    latched(
        run,
        s.names,
        lambda st: (
            force_counts_match(st, s.owner, index)
            and force(st, s.owner, index)["actorId"] == safe_ids[st["localIndex"]]
            and order_matches(st, index, RETREAT, -1)
            and force(st, s.owner, index)["status"] == RETREATING
            and force(st, s.owner, index)["waypointRegionIndex"] == home
            and distance2(
                force(st, s.owner, index)["destination"], region(st, home)["anchor"]
            )
            <= 75**2
            and distance2(force(st, s.owner, index)["center"], origin) > 200**2
            and distance2(
                force(st, s.owner, index)["center"], region(st, home)["anchor"]
            )
            ** 0.5
            < distance2(origin, region(st, home)["anchor"]) ** 0.5 - 200
        ),
        "actual Retreat moves the same force toward its safe home on every peer",
    )
