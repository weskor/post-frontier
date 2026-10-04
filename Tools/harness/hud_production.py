"""HUD permanent recipes, resource starvation, force capacity and paid replacement."""

from __future__ import annotations

from harness.hud_actions import RECIPE_RANGED, RECIPE_SIEGE, TOGGLE_PRODUCTION
from harness.hud_surface import Capture
from harness.network import (
    HOLDING,
    MOVE_HOLD,
    RANGED,
    NetworkRun,
    alive_units,
    building,
    force,
    force_arrived,
    force_counts_match,
    order_destination_matches,
    order_matches,
    require,
    select_order_region,
    wallet,
)
from harness.verify import JsonObject


def start_and_starve(
    run: NetworkRun, capture: Capture, owner: int, barracks: int
) -> None:
    run.request("host", "income", paused=True)
    capture.hud(RECIPE_RANGED, "Ranged recipe")
    selected = capture.wait(
        lambda s: building(s, barracks)["recipe"] == RANGED, "ranged recipe replicated"
    )
    run.request(
        "host", "fund", owner=owner, amount=building(selected, barracks)["unitCost"]
    )
    capture.hud(TOGGLE_PRODUCTION, "Start production")
    capture.wait(
        lambda s: (
            building(s, barracks)["enabled"]
            and building(s, barracks)["configured"]
            and building(s, barracks)["productionSeconds"]
            > building(s, barracks)["unitTime"] * 0.25
        ),
        "configured force production progressing",
    )
    capture.shot("barracks-producing-locked-type")
    capture.hud(TOGGLE_PRODUCTION, "Pause partially produced unit")
    paused = capture.wait(
        lambda s: not building(s, barracks)["enabled"], "partial unit paused"
    )
    progress = building(paused, barracks)["productionSeconds"]
    capture.shot("barracks-paused-locked-type")
    capture.hud(
        RECIPE_SIEGE,
        "Blocked locked-type button explains itself without changing production",
    )
    locked = capture.state()
    require(
        building(locked, barracks)["recipe"] == RANGED
        and building(locked, barracks)["configured"]
        and building(locked, barracks)["productionSeconds"] == progress
        and wallet(locked, owner)["wallet"] == wallet(paused, owner)["wallet"],
        "paused configured force exposed a type-changing action or mutated work/wallet",
    )
    require(
        locked["orderFeedback"] == "Force type locked after Start."
        and locked["feedbackOpacity"] > 0,
        "locked-type click did not visibly explain why the force type cannot change",
    )
    capture.hud(TOGGLE_PRODUCTION, "Resume locked ranged force")
    run.request("host", "fund", owner=owner, amount=0)
    capture.wait(
        lambda s: building(s, barracks)["productionState"] == "InsufficientResources",
        "enabled production reports insufficient resources",
    )
    capture.shot("barracks-waiting-resources")


def fill_force(
    run: NetworkRun, capture: Capture, owner: int, barracks: int, target: int
) -> tuple[JsonObject, int]:
    run.request("host", "select", target="building", building=barracks)
    capture.key("F")
    before_resume = building(capture.state(), barracks)
    capacity = before_resume["capacity"]
    run.request(
        "host",
        "fund",
        owner=owner,
        amount=(capacity - before_resume["joined"] - before_resume["travelling"])
        * before_resume["unitCost"],
    )
    capture.wait(
        lambda s: building(s, barracks)["productionState"] == "Producing",
        "funding automatically resumes enabled production",
    )
    run.phase("resource starvation and automatic resume without toggling production")
    state = capture.wait(
        lambda s: (
            building(s, barracks)["travelling"] > 0
            and force_counts_match(s, owner, barracks)
            and order_matches(s, barracks, MOVE_HOLD, target)
        ),
        "paid unit travelling from producer to held region",
    )
    squad = building(state, barracks)["forceID"]
    capture.shot("barracks-recruit-travelling")
    state = capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == capacity
            and building(s, barracks)["travelling"] == 0
            and force_counts_match(s, owner, barracks)
            and building(s, barracks)["productionState"] == "ForceComplete"
            and building(s, barracks)["capacity"] == capacity
            and wallet(s, owner)["wallet"] == 0
            and building(s, barracks)["status"] == HOLDING
            and order_matches(s, barracks, MOVE_HOLD, target)
            and force_arrived(s, owner, barracks, target)
        ),
        "paid ranged units physically join and fill their own force",
    )
    capture.shot("barracks-force-complete")
    check_full_capacity_controls(capture, owner, barracks)
    return state, squad


def check_full_capacity_controls(capture: Capture, owner: int, barracks: int) -> None:
    capacity = building(capture.state(), barracks)["capacity"]
    capture.hud(TOGGLE_PRODUCTION, "Pause full force")
    capture.wait(
        lambda s: (
            not building(s, barracks)["enabled"]
            and building(s, barracks)["productionState"] == "Paused"
        ),
        "explicit pause takes priority while full",
    )
    capture.shot("barracks-full-paused")
    capture.hud(TOGGLE_PRODUCTION, "Enable full force")
    capture.wait(
        lambda s: (
            building(s, barracks)["enabled"]
            and building(s, barracks)["productionState"] == "ForceComplete"
            and building(s, barracks)["joined"] == capacity
            and wallet(s, owner)["wallet"] == 0
        ),
        "enabled full force reports automatic capacity waiting, without charging",
    )


def paid_replacement(
    run: NetworkRun,
    capture: Capture,
    owner: int,
    barracks: int,
    squad: int,
    state: JsonObject,
) -> int:
    recipe = building(state, barracks)
    deficit = recipe["capacity"] - 1
    capture.hud(TOGGLE_PRODUCTION, "Pause full force before casualty")
    capture.wait(
        lambda s: building(s, barracks)["productionState"] == "Paused",
        "full force paused again",
    )
    victim = alive_units(force(state, owner, barracks))[0]
    run.request("host", "kill", owner=owner, army=squad, slot=victim["slot"])
    capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == deficit
            and building(s, barracks)["travelling"] == 0
        ),
        "real casualty creates force deficit",
    )
    capture.shot("barracks-casualty-deficit-paused")
    run.request("host", "fund", owner=owner, amount=recipe["unitCost"])
    capture.hud(TOGGLE_PRODUCTION, "Resume automatic paid replacement")
    state = capture.wait(
        lambda s: (
            building(s, barracks)["travelling"] == 1
            and building(s, barracks)["joined"] == deficit
            and wallet(s, owner)["wallet"] == 0
            and force_counts_match(s, owner, barracks)
        ),
        "one paid replacement is queued on the supply chain",
    )
    held = {u["slot"] for u in alive_units(force(state, owner, barracks))}
    vacant = next(slot for slot in range(recipe["capacity"]) if slot not in held)
    capture.shot("barracks-replacement-travelling")
    return vacant


def retarget_replacement(
    run: NetworkRun,
    capture: Capture,
    owner: int,
    barracks: int,
    target: int,
    vacant: int,
) -> None:
    moved_target = select_order_region(capture.state(), barracks, exclude=(target,))[
        "index"
    ]
    capture.select_force(barracks)
    capture.order_region(barracks, MOVE_HOLD, moved_target)
    capture.wait(
        lambda s: order_destination_matches(s, owner, barracks, moved_target),
        "replacement force retargets to the new held region",
    )
    capture.wait(
        lambda s: (
            order_destination_matches(s, owner, barracks, moved_target)
            and any(
                u["slot"] == vacant
                for u in alive_units(force(s, owner, barracks))
            )
        ),
        "paid replacement appears in the vacated slot of the retargeted force",
    )
    capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == building(s, barracks)["capacity"]
            and building(s, barracks)["travelling"] == 0
            and force_counts_match(s, owner, barracks)
            and building(s, barracks)["status"] == HOLDING
            and order_matches(s, barracks, MOVE_HOLD, moved_target)
            and order_destination_matches(s, owner, barracks, moved_target)
            and force_arrived(s, owner, barracks, moved_target)
        ),
        "replacement physically arrives",
    )
    run.request("host", "select", target="building", building=barracks)
    capture.key("F")
    capture.shot("barracks-replacement-complete")
    capture.hud(TOGGLE_PRODUCTION, "Pause after replacement proof")
    state = capture.wait(
        lambda s: not building(s, barracks)["enabled"],
        "force paused for presentation checks",
    )
    run.phase(
        "permanent type, partial pause, starvation, full force, real casualty and paid physical replacement"
    )
    check_removed_squad_keys(run, capture, owner, state)


def check_removed_squad_keys(
    run: NetworkRun, capture: Capture, owner: int, state: JsonObject
) -> None:
    roster = [(a["army"], a["serial"]) for a in state["armies"] if a["owner"] == owner]
    for key in ("Tab", "Q", "H"):
        capture.key(key)
    state = capture.state()
    require(
        roster
        == [(a["army"], a["serial"]) for a in state["armies"] if a["owner"] == owner],
        "removed manual squad keys changed automatic squad orders",
    )
    require(state["buildingSelected"], "removed Tab binding changed building selection")
    run.phase("former squad-control keys preserve force orders and building selection")
