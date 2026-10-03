"""Fresh peer sessions, convergence barriers and paid barracks setup."""

from __future__ import annotations

from collections.abc import Callable, Sequence
from typing import NamedTuple, cast

from harness.network import (
    BARRACKS,
    MOVE_HOLD,
    RANGED,
    SIEGE,
    NetworkRun,
    building,
    force,
    force_counts_match,
    order_matches,
    order_destination_matches,
    owned_buildings,
    require,
    select_order_region,
    wallet,
)
from harness.verify import JsonObject


class Session(NamedTuple):
    names: list[str]
    identities: dict[str, int]
    peer: str
    owner: int
    arena: Sequence[float]


def converged(
    run: NetworkRun,
    names: Sequence[str],
    predicate: Callable[[JsonObject], object],
    description: str,
    report_interval: float = 5,
) -> dict[str, JsonObject]:
    return run.await_states(
        names,
        lambda states: all(predicate(s) for s in states.values()),
        description,
        report_interval,
    )


def latched(
    run: NetworkRun,
    names: Sequence[str],
    predicate: Callable[[JsonObject], object],
    description: str,
) -> dict[str, JsonObject]:
    """Transient evidence (a recruit mid-travel) is latched per peer when first observed;
    peers never have to expose the same transient state in one simultaneous sample."""
    seen: set[str] = set()

    def check(states: dict[str, JsonObject]) -> bool:
        seen.update(name for name, state in states.items() if predicate(state))
        return seen >= set(names)

    return run.await_states(names, check, description)


def hold_at(
    run: NetworkRun, s: Session, index: int, target: int, description: str
) -> dict[str, JsonObject]:
    run.request(
        s.peer, "order", building=index, forceVerb=MOVE_HOLD, targetRegionIndex=target
    )
    return converged(
        run,
        s.names,
        lambda st: (
            order_matches(st, index, MOVE_HOLD, target)
            and order_destination_matches(st, s.owner, index, target)
            and force_counts_match(st, s.owner, index)
            and force(st, s.owner, index)["forceVerb"] == MOVE_HOLD
            and force(st, s.owner, index)["targetRegionIndex"] == target
            and force(st, s.owner, index)["orders"]
            == [
                {
                    "forceVerb": MOVE_HOLD,
                    "targetRegionIndex": target,
                    "targetStructureId": -1,
                }
            ]
        ),
        description,
    )


def connect(run: NetworkRun) -> Session:
    """Fresh host plus clients with isolated enemy, paused income and distinct commanders."""
    names = ["host", *(f"c{i}" for i in range(1, run.clients + 1))]
    run.start("host", host=True)
    run.await_states(
        ["host"],
        lambda s: (
            s["host"]["localIndex"] >= 0
            and bool(s["host"]["sites"])
            and bool(s["host"]["regions"])
            and bool(s["host"]["deposits"])
        ),
        "initialized construction host",
        allow_loading=True,
    )
    run.request("host", "isolate")
    run.request("host", "income", paused=True)
    for name in names[1:]:
        run.start(name)
    states = run.await_states(
        names,
        lambda values: all(
            s["localIndex"] >= 0
            and len(s["players"]) == len(names)
            and len(s["sites"]) == len(values["host"]["sites"])
            and len(s["regions"]) == len(values["host"]["regions"])
            and len(s["deposits"]) == len(values["host"]["deposits"])
            and all(p["index"] >= 0 for p in s["players"])
            for s in values.values()
        ),
        "all independent commander identities",
        allow_loading=True,
    )
    identities = {name: state["localIndex"] for name, state in states.items()}
    require(
        len(set(identities.values())) == len(names),
        "peers do not own distinct commanders",
    )
    for name, state in states.items():
        require(
            state["netMode"] == (2 if name == "host" else 3),
            f"{name}: not a real listen/client world",
        )
        require(
            not any(a["team"] == 0 for a in state["armies"]),
            f"{name}: obsolete fixed starting armies",
        )
        if run.emulation:
            require(
                state.get("pktLag") == 120 and state.get("pktLoss") == 8,
                f"{name}: emulation not active",
            )
    run.phase("fresh real sockets and empty player armies with independent identities")
    peer = names[-1]
    host = states["host"]
    return Session(names, identities, peer, identities[peer], host["arenaHalfExtent"])


def fund(
    run: NetworkRun, s: Session, amount: int, description: str
) -> dict[str, JsonObject]:
    run.request("host", "fund", owner=s.owner, amount=amount)
    return converged(
        run, s.names, lambda st: wallet(st, s.owner)["wallet"] == amount, description
    )


def place_barracks(
    run: NetworkRun, s: Session, count: int, description: str
) -> tuple[Sequence[float], dict[str, JsonObject]]:
    candidate = run.request("host", "placement", kind=BARRACKS)["placementCandidate"]
    run.request(s.peer, "key", key="B", pressed=True)
    run.request(s.peer, "key", key="B", pressed=False)
    run.request(s.peer, "key", key="Q", pressed=True)
    run.request(s.peer, "key", key="Q", pressed=False)
    run.request(s.peer, "place", x=candidate[0], y=candidate[1])
    return candidate, converged(
        run,
        s.names,
        lambda st: len(owned_buildings(st, s.owner, BARRACKS)) == count,
        description,
    )


def build_barracks(run: NetworkRun, s: Session) -> int:
    """Paid remote placement, rejected duplicate/outside/foreign commands, game-time completion."""
    fund(run, s, 1000, "explicit encounter budget replicated")
    candidate, states = place_barracks(
        run, s, 1, "remote-owned barracks construction replicates"
    )
    converged(
        run,
        [s.peer],
        lambda st: st["buildingSelected"] and not st["placing"] and st["hudExpanded"],
        "remote placement result selects its building and ends placement",
    )
    index = owned_buildings(states["host"], s.owner, BARRACKS)[0]["index"]
    balance = wallet(states["host"], s.owner)["wallet"]
    require(balance == 780, "barracks placement did not charge exactly 220")
    run.request(s.peer, "build", kind=BARRACKS, x=candidate[0], y=candidate[1])
    run.request(
        s.peer, "build", kind=BARRACKS, x=2 * s.arena[0], y=0
    )  # beyond the replicated arena
    if s.peer != "host":
        run.request("host", "production", building=index, recipe=RANGED, enabled=True)
        run.request("host", "cancel", building=index)
    states = converged(
        run,
        s.names,
        lambda st: building(st, index)["constructionProgress"] == 1,
        "normal game-time building construction",
    )
    require(
        all(
            wallet(st, s.owner)["wallet"] == balance
            and len(owned_buildings(st, s.owner, BARRACKS)) == 1
            and not building(st, index)["enabled"]
            for st in states.values()
        ),
        "invalid placement or foreign role/cancel command mutated construction or debited a wallet",
    )
    return cast(int, index)


def configure_siege(run: NetworkRun, s: Session, index: int) -> int:
    """First Start locks siege for exactly the 180 fee and leaves the force paused and empty."""
    # Exactly the configuration price leaves no funds for a recruit; observe the
    # accepted Start independently of automatic production and replication timing.
    fund(run, s, 180, "siege configuration budget")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["configured"]
            and building(st, index)["recipe"] == SIEGE
            and wallet(st, s.owner)["wallet"] == 0
        ),
        "first Start permanently configures siege for exactly 180",
    )
    producer = building(states["host"], index)
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    states = converged(
        run,
        s.names,
        lambda st: not building(st, index)["enabled"],
        "configuration pause",
    )
    target = select_order_region(states["host"], index)["index"]
    # An empty force cannot physically arrive until a paid recruit joins it.
    run.request(
        s.peer, "order", building=index, forceVerb=MOVE_HOLD, targetRegionIndex=target
    )
    converged(
        run,
        s.names,
        lambda st: (
            order_matches(st, index, MOVE_HOLD, target)
            and force_counts_match(st, s.owner, index)
            and force(st, s.owner, index)["forceVerb"] == MOVE_HOLD
            and force(st, s.owner, index)["targetRegionIndex"] == target
            and force(st, s.owner, index)["orders"]
            == [
                {
                    "forceVerb": MOVE_HOLD,
                    "targetRegionIndex": target,
                    "targetStructureId": -1,
                }
            ]
        ),
        "empty configured force receives its remote Move & Hold target",
    )
    return cast(int, producer["forceID"])


def recruit(
    run: NetworkRun, s: Session, index: int, expected: int, description: str
) -> dict[str, JsonObject]:
    """Pay for exactly one siege unit and wait until it has physically joined on every peer."""
    recipe = building(run.observe("host"), index)
    fund(run, s, recipe["unitCost"], f"one-unit budget for recruit {expected}")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["joined"] == expected
            and building(st, index)["travelling"] == 0
            and force_counts_match(st, s.owner, index)
            and wallet(st, s.owner)["wallet"] == 0
        ),
        description,
    )
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(
        run,
        s.names,
        lambda st: not building(st, index)["enabled"],
        f"recruit {expected} pause",
    )
    return states
