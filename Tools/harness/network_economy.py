"""Region capture, finite private/JEV extractors and Assault outcome sequence."""

from __future__ import annotations

from typing import cast

from harness.network import (
    ASSAULT,
    EXPAND,
    EXTRACTOR,
    FALL_BACK,
    HOLD,
    NetworkRun,
    alive_units,
    building,
    distance2,
    force,
    goal_matches,
    near,
    region,
    require,
    select_goal_region,
    wallet,
)
from harness.network_outcomes import finish, research
from harness.network_session import Session, converged
from harness.verify import JsonObject


def event_sequence(state: JsonObject) -> int:
    events = state["objectiveEvents"]
    return int(events[-1]["sequence"]) if events else 0


def objective_event(
    run: NetworkRun, s: Session, event_id: str, target: int,
    owner: int, force_number: int, after: int = 0, damage_tier: int | None = None,
) -> dict[str, JsonObject]:
    def matching(state: JsonObject) -> list[JsonObject]:
        return [
            event for event in state["objectiveEvents"]
            if event["id"] == event_id and event["region"] == target
            and event["sequence"] > after
            and (damage_tier is None or event["damageTier"] == damage_tier)
        ]

    states = converged(
        run, s.names, lambda st: bool(matching(st)),
        f"{event_id} reaches every peer with region {target}",
    )
    expected = matching(states["host"])
    require(len(expected) == 1, f"{event_id}: duplicate transition")
    if event_id == "drill_rig_lost":
        require(
            expected[0]["affectedTeam"] == 0
            and len(expected[0]["forces"]) == 1
            and expected[0]["forces"][0]["team"] == 5
            and expected[0]["forces"][0]["playerName"] == "JEV",
            "Drill Rig loss must affect the friendly team and attribute the lethal JEV force",
        )
    require(
        any(
            contributor["owner"] == owner
            and contributor["forceNumber"] == force_number
            and contributor["playerName"]
            for contributor in expected[0]["forces"]
        ),
        f"{event_id}: wrong causing player or force",
    )
    require(
        all(matching(state) == expected for state in states.values()),
        f"{event_id}: remote event identity, attribution or location differs",
    )
    return states


def deposit(state: JsonObject, index: int) -> JsonObject:
    return next(d for d in state["deposits"] if d["index"] == index)


def jev_baseline(
    run: NetworkRun, payments: int, paid_payments: int = 0
) -> tuple[int, int]:
    """Expected credits and floored rate for the host plus every client."""
    rate_tenths = 2 * (10 + 3 * run.clients)
    # connect pauses income before clients join: earlier natural solo ticks
    # leave no fraction. Keep the carry across this scenario's explicit ticks,
    # including enemyExtractor's wallet reset, which does not reset the carry.
    before_tenths = 2 * rate_tenths * paid_payments
    credits = (before_tenths + 2 * rate_tenths * payments) // 10 - before_tenths // 10
    return credits, rate_tenths // 10


def capture_region(
    run: NetworkRun, s: Session, index: int
) -> tuple[int, dict[str, JsonObject]]:
    state = run.observe("host")
    deposit_regions = {d["region"] for d in state["deposits"] if not d["occupied"]}
    excluded = tuple(
        r["index"]
        for r in state["regions"]
        if r["controller"] == 0 or r["index"] not in deposit_regions
    )
    target = select_goal_region(state, index, exclude=excluded)["index"]
    after = event_sequence(state)
    force_number = building(state, index)["forceNumber"]
    run.request(s.peer, "goal", building=index, goal=EXPAND, region=target)
    states = converged(
        run,
        s.names,
        lambda st: (
            region(st, target)["controller"] == 0
            and goal_matches(st, index, HOLD, target)
            and building(st, index)["frontOrder"] == 1
            and distance2(building(st, index)["front"], region(st, target)["anchor"])
            < 1
        ),
        "produced force completes Expand by securing and holding the target region",
    )
    require(
        all(all(p["income"] == 2 for p in st["players"]) for st in states.values()),
        "bare region capture incorrectly provides income",
    )
    states = objective_event(
        run, s, "region_captured", target, s.owner, force_number, after,
    )
    return target, states


def clear_deposit(
    run: NetworkRun, s: Session, index: int, target: int, states: dict[str, JsonObject]
) -> JsonObject:
    free = next(
        d
        for d in states["host"]["deposits"]
        if d["region"] == target and not d["occupied"]
    )
    rate = 6 if free["rich"] else 4
    total = 3000 if free["rich"] else 2400
    require(
        free["rate"] == rate and free["remaining"] == total,
        "deposit kind has incorrect rate or finite reserve",
    )
    run.request(s.peer, "goal", building=index, goal=FALL_BACK, region=-1)
    states = converged(
        run,
        s.names,
        lambda st: (
            region(st, target)["controller"] == 0
            and not next(site for site in st["sites"] if site["index"] == target)[
                "friendlyPresent"
            ]
            and building(st, index)["forceGoal"] == FALL_BACK
            and all(
                distance2(u["position"], free["position"]) > 500**2
                for u in alive_units(force(st, s.owner, index))
            )
        ),
        "capturing force physically clears deposit footprint while retaining controlled region",
    )
    return cast(JsonObject, free)


def build_extractor(
    run: NetworkRun, s: Session, free: JsonObject
) -> tuple[int, dict[str, JsonObject]]:
    deposit_index = free["index"]
    rate = free["rate"]
    run.request("host", "fund", owner=s.owner, amount=160)
    run.request(s.peer, "build", kind=EXTRACTOR, **near(free["position"], 71, -63))
    states = converged(
        run,
        s.names,
        lambda st: (
            deposit(st, deposit_index)["occupied"]
            and deposit(st, deposit_index)["complete"]
            and deposit(st, deposit_index)["owner"] == s.owner
            and deposit(st, deposit_index)["team"] == 0
            and any(
                b["index"] == deposit(st, deposit_index)["extractor"]
                and b["deposit"] == deposit_index
                and b["position"][:2] == free["position"][:2]
                for b in st["buildings"]
            )
            and wallet(st, s.owner)["wallet"] == 0
            and wallet(st, s.owner)["income"] == 2 + rate
            and all(
                p["income"] == (2 + rate if p["index"] == s.owner else 2)
                for p in st["players"]
            )
        ),
        "paid extractor snaps exact deposit XY, reserves it, and belongs only to builder",
    )
    extractor = deposit(states["host"], deposit_index)["extractor"]
    return extractor, states


def pay_private_income(
    run: NetworkRun, s: Session, free: JsonObject, states: dict[str, JsonObject]
) -> dict[str, JsonObject]:
    deposit_index, rate, total = free["index"], free["rate"], free["remaining"]
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    enemy_before = states["host"]["enemyResources"]
    enemy_payment, _ = jev_baseline(run, payments=1)
    run.request("host", "incomeTick")
    states = converged(
        run,
        s.names,
        lambda st: (
            deposit(st, deposit_index)["remaining"] == total - 2 * rate
            and st["enemyResources"] == enemy_before + enemy_payment
            and all(
                p["wallet"]
                == before[p["index"]] + 4 + (2 * rate if p["index"] == s.owner else 0)
                for p in st["players"]
            )
            and st["income"] == wallet(st, st["localIndex"])["income"]
        ),
        "normal payment tick drains once, pays only builder bonus and baseline to every other wallet",
    )
    return states


def deplete_private_deposit(
    run: NetworkRun,
    s: Session,
    target: int,
    free: JsonObject,
    extractor: int,
    states: dict[str, JsonObject],
) -> None:
    deposit_index = free["index"]
    run.request("host", "depositRemaining", deposit=deposit_index, remaining=3)
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    enemy_before = states["host"]["enemyResources"]
    enemy_payment, _ = jev_baseline(run, payments=2, paid_payments=1)
    run.request("host", "incomeTick")
    run.request("host", "incomeTick")
    states = converged(
        run,
        s.names,
        lambda st: (
            deposit(st, deposit_index)["remaining"] == 0
            and wallet(st, s.owner)["income"] == 2
            and st["enemyResources"] == enemy_before + enemy_payment
            and all(
                p["wallet"]
                == before[p["index"]] + 8 + (3 if p["index"] == s.owner else 0)
                and p["income"] == 2
                for p in st["players"]
            )
        ),
        "finite final partial payment is capped and depleted extractor stops bonus on following tick",
    )
    after = event_sequence(states["host"])
    run.request("host", "destroyExtractor", building=extractor)
    converged(
        run,
        s.names,
        lambda st: (
            not deposit(st, deposit_index)["occupied"]
            and deposit(st, deposit_index)["extractor"] == -1
            and region(st, target)["controller"] == 0
            and wallet(st, s.owner)["income"] == 2
        ),
        "real lethal attack frees depleted deposit, while anchor control retains polygon rights",
    )
    objective_event(run, s, "drill_rig_lost", target, -1, 0, after)
    run.phase(
        "natural polygon capture, builder-only finite income, depletion and destruction freeing across peers"
    )


def build_enemy_extractor(
    run: NetworkRun, s: Session
) -> tuple[JsonObject, dict[str, JsonObject]]:
    # JEV fixture uses the same paid placement and economy paths; only budget and completion are accelerated.
    run.request("host", "enemyExtractor")
    states = converged(
        run,
        s.names,
        lambda st: (
            any(
                d["occupied"] and d["complete"] and d["team"] == 5
                for d in st["deposits"]
            )
            and st["enemyResources"] == 0
        ),
        "JEV spends exactly 160 from its controllerless wallet for its own deposit extractor",
    )
    enemy_deposit = next(
        d for d in states["host"]["deposits"] if d["complete"] and d["team"] == 5
    )
    enemy_rate, enemy_total = (
        enemy_deposit["rate"],
        enemy_deposit["remaining"],
    )
    require(
        enemy_rate == (6 if enemy_deposit["rich"] else 4)
        and enemy_total == (3000 if enemy_deposit["rich"] else 2400),
        "JEV deposit finite defaults incorrect",
    )
    return enemy_deposit, states


def pay_enemy_income(
    run: NetworkRun,
    s: Session,
    enemy_deposit: JsonObject,
    states: dict[str, JsonObject],
) -> dict[str, JsonObject]:
    enemy_index, enemy_rate, enemy_total = (
        enemy_deposit["index"],
        enemy_deposit["rate"],
        enemy_deposit["remaining"],
    )
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    enemy_payment, enemy_income = jev_baseline(run, payments=1, paid_payments=3)
    run.request("host", "incomeTick")
    states = converged(
        run,
        s.names,
        lambda st: (
            st["enemyIncome"] == enemy_income + enemy_rate
            and st["enemyResources"] == enemy_payment + 2 * enemy_rate
            and deposit(st, enemy_index)["remaining"] == enemy_total - 2 * enemy_rate
            and all(
                p["wallet"] == before[p["index"]] + 4 and p["income"] == 2
                for p in st["players"]
            )
        ),
        "JEV-owned extractor pays only enemy wallet and drains its finite deposit once",
    )
    return states


def deplete_enemy_deposit(
    run: NetworkRun,
    s: Session,
    enemy_deposit: JsonObject,
    states: dict[str, JsonObject],
) -> None:
    enemy_index = enemy_deposit["index"]
    run.request("host", "depositRemaining", deposit=enemy_index, remaining=3)
    enemy_before = states["host"]["enemyResources"]
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    enemy_payment, enemy_income = jev_baseline(run, payments=2, paid_payments=4)
    run.request("host", "incomeTick")
    run.request("host", "incomeTick")
    states = converged(
        run,
        s.names,
        lambda st: (
            st["enemyResources"] == enemy_before + enemy_payment + 3
            and st["enemyIncome"] == enemy_income
            and deposit(st, enemy_index)["remaining"] == 0
            and all(
                p["wallet"] == before[p["index"]] + 8 and p["income"] == 2
                for p in st["players"]
            )
        ),
        "JEV depletion caps partial bonus then stops it, without affecting friendly private income",
    )
    after = event_sequence(states["host"])
    run.request(
        "host",
        "destroyExtractor",
        building=deposit(states["host"], enemy_index)["extractor"],
    )
    states = converged(
        run,
        s.names,
        lambda st: (
            not deposit(st, enemy_index)["occupied"]
            and deposit(st, enemy_index)["extractor"] == -1
            and region(st, deposit(st, enemy_index)["region"])["controller"] == 5
        ),
        "JEV extractor destruction frees deposit without altering enemy main ownership",
    )
    require(
        all(
            not any(
                event["id"] == "drill_rig_lost" and event["sequence"] > after
                for event in state["objectiveEvents"]
            )
            for state in states.values()
        ),
        "JEV Drill Rig death incorrectly raised a loss event on host or remote",
    )
    run.phase(
        "JEV wallet-only finite extractor payment, depletion and deposit freeing across peers"
    )


def assault_and_finish(
    run: NetworkRun, s: Session, index: int, squad: int
) -> dict[str, JsonObject]:
    research(run, s)
    enemy_main = next(
        r["index"] for r in run.observe("host")["regions"] if r["homeTeam"] == 5
    )
    state = run.observe("host")
    after = event_sequence(state)
    force_number = building(state, index)["forceNumber"]
    run.request(s.peer, "goal", building=index, goal=ASSAULT, region=-1)
    converged(
        run,
        s.names,
        lambda st: goal_matches(st, index, ASSAULT, enemy_main),
        "Assault resolves the enemy main server-side on every peer",
    )
    objective_event(
        run, s, "enemy_hq_under_attack", enemy_main, s.owner, force_number,
        after, damage_tier=0,
    )
    converged(
        run, s.names, lambda st: st["enemyHQ"] <= 225,
        "Assault crosses both HQ damage tiers through actual weapon damage",
    )
    for event_id in ("enemy_hq_half", "enemy_hq_critical"):
        objective_event(run, s, event_id, enemy_main, s.owner, force_number)
    for damage_tier in (1, 2):
        objective_event(
            run, s, "enemy_hq_under_attack", enemy_main, s.owner, force_number,
            after, damage_tier=damage_tier,
        )
    states = finish(run, s, squad, "weapon-caused victory replicates")
    states = objective_event(
        run, s, "enemy_hq_offline", enemy_main, s.owner, force_number, after,
    )
    require(
        all(
            sorted(
                event["damageTier"] for event in state["objectiveEvents"]
                if event["id"] == "enemy_hq_under_attack"
                and event["region"] == enemy_main and event["sequence"] > after
            ) == [0, 1, 2]
            for state in states.values()
        ),
        "Assault must announce each enemy HQ damage tier exactly once on every peer",
    )
    require(
        all(
            wallet(st, s.owner)["doctrine"] == 1 and wallet(st, s.owner)["wallet"] == 50
            for st in states.values()
        ),
        "repeat research changed choice or charged twice",
    )
    run.phase("research, Assault-goal weapon damage and victory")
    return states


def expand_and_research(
    run: NetworkRun, s: Session, index: int, squad: int
) -> dict[str, JsonObject]:
    """Natural polygon capture, private finite extractors, research and Assault-goal HQ damage."""
    target, states = capture_region(run, s, index)
    free = clear_deposit(run, s, index, target, states)
    extractor, states = build_extractor(run, s, free)
    states = pay_private_income(run, s, free, states)
    deplete_private_deposit(run, s, target, free, extractor, states)
    enemy_deposit, states = build_enemy_extractor(run, s)
    states = pay_enemy_income(run, s, enemy_deposit, states)
    deplete_enemy_deposit(run, s, enemy_deposit, states)
    return assault_and_finish(run, s, index, squad)
