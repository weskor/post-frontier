"""Per-unit production, independent forces and physical casualty replacement."""

from __future__ import annotations

from collections.abc import Sequence
from typing import cast

from harness.network import (
    BARRACKS,
    HOLDING,
    MOVE_HOLD,
    RANGED,
    SIEGE,
    NetworkRun,
    alive_units,
    building,
    distance2,
    force,
    force_arrived,
    force_counts_match,
    order_matches,
    owned_buildings,
    require,
    select_order_region,
    wallet,
)
from harness.network_session import (
    Session,
    converged,
    fund,
    hold_at,
    latched,
    place_barracks,
)
from harness.verify import JsonObject


def start_first_recruit(run: NetworkRun, s: Session, index: int) -> Sequence[float]:
    recipe = building(run.observe("host"), index)
    fund(run, s, recipe["unitCost"], "one-unit budget")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["joined"] + building(st, index)["travelling"] == 1
            and force_counts_match(st, s.owner, index)
            and wallet(st, s.owner)["wallet"] == 0
        ),
        "one physical siege recruit, not batch production, replicated for exactly one unit cost",
    )
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(
        run,
        s.names,
        lambda st: not building(st, index)["enabled"],
        "single recruit pause",
    )
    first = alive_units(force(states["host"], s.owner, index))[0]
    require(
        first["reinforcing"]
        and first["role"] == SIEGE
        and first["owner"] == s.owner
        and distance2(first["position"], building(states["host"], index)["position"])
        < 1200**2
        and distance2(first["position"], building(states["host"], index)["front"])
        > 500**2,
        "recruit did not physically leave its producer toward the distant front",
    )
    first_position = first["position"]
    return cast(Sequence[float], first_position)


def await_first_arrival(
    run: NetworkRun, s: Session, index: int, first_position: Sequence[float]
) -> None:
    latched(
        run,
        s.names,
        lambda st: (
            distance2(
                alive_units(force(st, s.owner, index))[0]["position"], first_position
            )
            > 200**2
        ),
        "recruit travels on host and remote, not just a count increase",
    )
    converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["joined"] == 1
            and building(st, index)["travelling"] == 0
            and force_counts_match(st, s.owner, index)
        ),
        "physical arrival joins the force on all peers",
    )


def fill_siege_force(run: NetworkRun, s: Session, index: int) -> None:
    recipe = building(run.observe("host"), index)
    remaining = recipe["capacity"] - recipe["joined"] - recipe["travelling"]
    run.request("host", "fund", owner=s.owner, amount=remaining * recipe["unitCost"])
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["joined"] == recipe["capacity"]
            and building(st, index)["travelling"] == 0
            and building(st, index)["productionState"] == "ForceComplete"
            and wallet(st, s.owner)["wallet"] == 0
            and force_counts_match(st, s.owner, index)
        ),
        "siege force naturally fills its alive slots without repeated configuration charge",
    )
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(
        run,
        s.names,
        lambda st: (
            not building(st, index)["enabled"]
            and building(st, index)["productionState"] == "Paused"
        ),
        "full siege force explicitly paused",
    )


def create_second_force(run: NetworkRun, s: Session, index: int, squad: int) -> int:
    # Same commander, second producer: no shared slots and no global order scope.
    fund(run, s, 1000, "second producer budget")
    place_barracks(run, s, 2, "second owned producer replicates")
    states = converged(
        run,
        s.names,
        lambda st: (
            len(owned_buildings(st, s.owner, BARRACKS)) == 2
            and all(
                b["constructionProgress"] == 1
                for b in owned_buildings(st, s.owner, BARRACKS)
            )
        ),
        "second owned producer completes normally",
    )
    second = next(
        b["index"]
        for b in owned_buildings(states["host"], s.owner, BARRACKS)
        if b["index"] != index
    )
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=False)
    recipes = converged(
        run,
        s.names,
        lambda st: building(st, second)["recipe"] == RANGED,
        "second producer selects ranged recipe before funding",
    )
    recipe = building(recipes["host"], second)
    run.request(
        "host", "fund", owner=s.owner, amount=recipe["capacity"] * recipe["unitCost"]
    )
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=True)
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, second)["joined"] == recipe["capacity"]
            and building(st, second)["forceVerb"] == MOVE_HOLD
            and building(st, second)["travelling"] == 0
            and force_counts_match(st, s.owner, second)
            and wallet(st, s.owner)["wallet"] == 0
        ),
        "independent ranged force fills its paid slots",
    )
    require(
        building(states["host"], second)["forceID"] != squad,
        "two producers reused the same force identity",
    )
    return cast(int, second)


def retarget_and_open_vacancy(
    run: NetworkRun, s: Session, index: int, squad: int, second: int
) -> tuple[Sequence[float], int, int]:
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["forceNumber"] == 1
            and building(st, second)["forceNumber"] == 2
            and force(st, s.owner, index)["forceNumber"] == 1
            and force(st, s.owner, second)["forceNumber"] == 2
            and force_arrived(
                st, s.owner, second, building(st, second)["targetRegionIndex"]
            )
        ),
        "producer numbers 1/2 and matching group numbers observed on host and remote",
    )
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=False)
    converged(
        run,
        s.names,
        lambda st: not building(st, second)["enabled"],
        "second producer paused",
    )
    second_front = building(states["host"], second)["front"]
    second_target = building(states["host"], second)["targetRegionIndex"]
    original_target = building(states["host"], index)["targetRegionIndex"]
    moved_target = select_order_region(
        states["host"], index, exclude=(original_target,)
    )["index"]
    hold_at(run, s, index, moved_target, "replacement Hold target region replicates")
    converged(
        run,
        s.names,
        lambda st: (
            order_matches(st, second, MOVE_HOLD, second_target)
            and building(st, second)["front"] == second_front
            and building(st, second)["status"] == HOLDING
        ),
        "force-only order scope replicates without moving other force",
    )
    victim = alive_units(force(run.observe("host"), s.owner, index))[0]
    capacity = building(run.observe("host"), index)["capacity"]
    run.request("host", "kill", owner=s.owner, army=squad, slot=victim["slot"])
    converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["joined"] + building(st, index)["travelling"]
            == capacity - 1
            and force_counts_match(st, s.owner, index)
        ),
        "real hostile damage opens one replicated vacancy",
    )
    return second_front, second_target, original_target


def replace_vacancy(
    run: NetworkRun, s: Session, index: int, original_target: int
) -> dict[str, JsonObject]:
    recipe = building(run.observe("host"), index)
    run.request("host", "fund", owner=s.owner, amount=recipe["unitCost"])
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["travelling"] == 1
            and building(st, index)["joined"] == recipe["capacity"] - 1
            and force_counts_match(st, s.owner, index)
            and wallet(st, s.owner)["wallet"] == 0
        ),
        "one causal paid casualty replacement travels on all peers",
    )
    replacement = next(
        u
        for u in alive_units(force(states["host"], s.owner, index))
        if u["reinforcing"]
    )
    origin = replacement["position"]
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    run.request(
        s.peer,
        "order",
        building=index,
        forceVerb=MOVE_HOLD,
        targetRegionIndex=original_target,
    )
    # Arrival may precede a delayed peer's next sample; retain the same replacement
    # slot's movement evidence rather than requiring another transient reinforcing flag.
    latched(
        run,
        s.names,
        lambda st: (
            order_matches(st, index, MOVE_HOLD, original_target)
            and any(
                u["slot"] == replacement["slot"]
                and distance2(u["position"], origin) > 200**2
                for u in alive_units(force(st, s.owner, index))
            )
        ),
        "same paid replacement moves after the force retargets to another region",
    )
    states = converged(
        run,
        s.names,
        lambda st: (
            building(st, index)["joined"] == recipe["capacity"]
            and building(st, index)["travelling"] == 0
            and force_counts_match(st, s.owner, index)
            and building(st, index)["status"] == HOLDING
            and order_matches(st, index, MOVE_HOLD, original_target)
            and force_arrived(st, s.owner, index, original_target)
        ),
        "replacement physically arrives and joins moving force",
    )
    return states


def produce_and_replace(run: NetworkRun, s: Session, index: int, squad: int) -> None:
    """Per-unit debit, physical travel/arrival, independent second force and causal casualty replacement."""
    first_position = start_first_recruit(run, s, index)
    await_first_arrival(run, s, index, first_position)
    fill_siege_force(run, s, index)
    second = create_second_force(run, s, index, squad)
    second_front, second_target, original_target = retarget_and_open_vacancy(
        run, s, index, squad, second
    )
    states = replace_vacancy(run, s, index, original_target)
    require(
        all(
            building(st, second)["joined"] == building(st, second)["capacity"]
            and building(st, second)["front"] == second_front
            and order_matches(st, index, MOVE_HOLD, original_target)
            and order_matches(st, second, MOVE_HOLD, second_target)
            and wallet(st, s.owner)["wallet"] == 0
            for st in states.values()
        ),
        "replacement stole another force's capacity/order or charged more than one unit",
    )
    run.phase(
        "per-unit debit, independent region orders and causal replacement travel/arrival"
    )
