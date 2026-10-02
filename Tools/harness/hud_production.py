"""HUD permanent recipes, resource starvation, force capacity and paid replacement."""

from __future__ import annotations

from harness.hud_actions import RECIPE_RANGED, RECIPE_SIEGE, TOGGLE_PRODUCTION
from harness.hud_surface import Capture
from harness.network import (
    HOLD,
    RANGED,
    NetworkRun,
    alive_units,
    building,
    distance2,
    force,
    force_counts_match,
    goal_matches,
    region,
    require,
    select_goal_region,
    wallet,
)
from harness.verify import JsonObject


def start_and_starve(
    run: NetworkRun, capture: Capture, owner: int, barracks: int
) -> None:
    run.request("host", "income", paused=True)
    run.request("host", "fund", owner=owner, amount=30)
    capture.hud(RECIPE_RANGED, "Ranged recipe")
    capture.wait(
        lambda s: building(s, barracks)["recipe"] == RANGED, "ranged recipe replicated"
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
    run.request("host", "hud", hudAction=RECIPE_SIEGE, expect_rejection=True)
    locked = capture.state()
    require(
        building(locked, barracks)["recipe"] == RANGED
        and building(locked, barracks)["configured"]
        and building(locked, barracks)["productionSeconds"] == progress
        and wallet(locked, owner)["wallet"] == wallet(paused, owner)["wallet"],
        "paused configured force exposed a type-changing action or mutated work/wallet",
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
    before_resume = building(capture.state(), barracks)
    run.request(
        "host",
        "fund",
        owner=owner,
        amount=(4 - before_resume["joined"] - before_resume["travelling"]) * 30,
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
            and goal_matches(s, barracks, HOLD, target)
        ),
        "paid unit travelling from producer to held region",
    )
    squad = building(state, barracks)["forceID"]
    capture.shot("barracks-recruit-travelling")
    state = capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == 4
            and building(s, barracks)["travelling"] == 0
            and force_counts_match(s, owner, barracks)
            and building(s, barracks)["productionState"] == "ForceComplete"
        ),
        "four paid ranged units physically join and fill their own force",
    )
    require(
        building(state, barracks)["capacity"] == 4
        and wallet(state, owner)["wallet"] == 0,
        "ranged force capacity or single-unit payment mismatch",
    )
    capture.shot("barracks-force-complete")
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
            and building(s, barracks)["joined"] == 4
            and wallet(s, owner)["wallet"] == 0
        ),
        "enabled full force reports automatic capacity waiting, without charging",
    )
    return state, squad


def paid_replacement(
    run: NetworkRun,
    capture: Capture,
    owner: int,
    barracks: int,
    squad: int,
    state: JsonObject,
) -> tuple[JsonObject, list[float]]:
    capture.hud(TOGGLE_PRODUCTION, "Pause full force before casualty")
    capture.wait(
        lambda s: building(s, barracks)["productionState"] == "Paused",
        "full force paused again",
    )
    victim = alive_units(force(state, owner, barracks))[0]
    run.request("host", "kill", owner=owner, army=squad, slot=victim["slot"])
    capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == 3
            and building(s, barracks)["travelling"] == 0
        ),
        "real casualty creates force deficit",
    )
    capture.shot("barracks-casualty-deficit-paused")
    run.request("host", "fund", owner=owner, amount=30)
    capture.hud(TOGGLE_PRODUCTION, "Resume automatic paid replacement")
    state = capture.wait(
        lambda s: (
            building(s, barracks)["travelling"] == 1
            and building(s, barracks)["joined"] == 3
            and wallet(s, owner)["wallet"] == 0
            and force_counts_match(s, owner, barracks)
        ),
        "one paid replacement leaves the producer",
    )
    recruit = next(
        u for u in alive_units(force(state, owner, barracks)) if u["reinforcing"]
    )
    origin = recruit["position"]
    capture.shot("barracks-replacement-travelling")
    return recruit, origin


def retarget_replacement(
    run: NetworkRun,
    capture: Capture,
    owner: int,
    barracks: int,
    target: int,
    recruit: JsonObject,
    origin: list[float],
) -> None:
    moved_target = select_goal_region(capture.state(), barracks, exclude=(target,))[
        "index"
    ]
    run.request("host", "goal", building=barracks, goal=HOLD, region=moved_target)
    capture.wait(
        lambda s: (
            goal_matches(s, barracks, HOLD, moved_target)
            and distance2(
                building(s, barracks)["front"], region(s, moved_target)["anchor"]
            )
            < 1
        ),
        "replacement force retargets to the new held region",
    )
    capture.wait(
        lambda s: any(
            u["slot"] == recruit["slot"] and distance2(u["position"], origin) > 200**2
            for u in alive_units(force(s, owner, barracks))
        ),
        "same paid replacement physically tracks the retargeted force",
    )
    capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == 4
            and building(s, barracks)["travelling"] == 0
            and force_counts_match(s, owner, barracks)
        ),
        "replacement physically arrives",
    )
    capture.shot("barracks-replacement-complete")
    capture.hud(TOGGLE_PRODUCTION, "Pause after replacement proof")
    state = capture.wait(
        lambda s: not building(s, barracks)["enabled"],
        "force paused for presentation checks",
    )
    run.phase(
        "permanent type, partial pause, starvation, four-unit force, real casualty and paid physical replacement"
    )
    roster = [(a["army"], a["serial"]) for a in state["armies"] if a["owner"] == owner]
    for key in ("Tab", "Q", "H", "R"):
        capture.key(key)
    state = capture.state()
    require(
        roster
        == [(a["army"], a["serial"]) for a in state["armies"] if a["owner"] == owner],
        "removed manual squad keys changed automatic squad orders",
    )
    require(state["buildingSelected"], "removed Tab binding changed building selection")
    run.phase(
        "former squad-control keys preserve automatic goals and building selection"
    )
