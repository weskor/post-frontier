"""Full rendered HUD acceptance orchestration and construction/research presentation."""

from __future__ import annotations

from collections.abc import Sequence
from typing import cast

from harness.hud_actions import (
    BUILD_BARRACKS,
    RECIPE_SIEGE,
    RESEARCH_REPAIRS,
    SELECT_FORCE,
    TOGGLE_PRODUCTION,
)
from harness.hud_build import deck_controls
from harness.hud_orders import assign_orders, cancel_orders
from harness.hud_production import (
    fill_force,
    paid_replacement,
    retarget_replacement,
    start_and_starve,
)
from harness.hud_retreat import recall_box_fixture
from harness.hud_setup import boot, place_barracks
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import (
    BARRACKS,
    HOLDING,
    SIEGE,
    WORKSHOP,
    NetworkRun,
    building,
    distance2,
    owned_buildings,
    require,
    wallet,
)
from harness.verify import JsonObject


def primary_barracks(run: NetworkRun, capture: Capture, owner: int) -> int:
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "owned barracks placed")
    require(
        state["buildingSelected"] and state["hudExpanded"] and not state["placing"],
        "successful placement did not select its building and restore construction choices",
    )
    capture.key("Escape")
    paused = capture.wait(
        lambda s: s["uiScreen"] != state["uiScreen"],
        "placement result remains visible after opening the pause menu",
    )
    require(
        paused["orderFeedback"] == state["orderFeedback"]
        and paused["feedbackOpacity"] > 0
        and paused["buildingSelected"]
        and not paused["placing"],
        "pause swallowed the paid placement result or changed selection/mode",
    )
    capture.shot("placement-result-paused")
    capture.key("Escape")
    capture.wait(
        lambda s: s["uiScreen"] == state["uiScreen"], "resume after placement feedback"
    )
    capture.hud(
        BUILD_BARRACKS, "Build choices remain available immediately after placement"
    )
    capture.wait(
        lambda s: s["placing"], "second building choice without F4 or selection"
    )
    capture.key("Escape")
    run.phase(
        "accepted placement reopens build choices; a second placement needs no discovery hotkey"
    )
    barracks = owned_buildings(state, owner, BARRACKS)[0]["index"]
    capture.wait(
        lambda s: (
            s["buildingSelected"]
            and 0.15 < building(s, barracks)["constructionProgress"] < 0.9
        ),
        "barracks visibly under construction",
    )
    capture.shot("barracks-constructing")
    capture.wait(
        lambda s: building(s, barracks)["constructionProgress"] == 1,
        "barracks complete",
    )
    capture.shot("barracks-ready")
    capture.hud(RECIPE_SIEGE, "Preview Siege before the first Start")
    capture.wait(
        lambda s: (
            building(s, barracks)["recipe"] == SIEGE
            and not building(s, barracks)["configured"]
        ),
        "Siege remains a freely selectable unconfigured type",
    )
    capture.shot("barracks-siege-unconfigured")
    return cast(int, barracks)


def siege_producer(
    run: NetworkRun, capture: Capture, owner: int, barracks: int
) -> None:
    run.request("host", "fund", owner=owner, amount=400)
    state = place_barracks(run, capture, owner, 2, "independent Siege producer placed")
    siege = next(
        b["index"]
        for b in owned_buildings(state, owner, BARRACKS)
        if b["index"] != barracks
    )
    run.request("host", "select", target="building", building=siege)
    capture.key("F")
    capture.wait(
        lambda s: building(s, siege)["constructionProgress"] == 1,
        "Siege producer complete",
    )
    capture.hud(RECIPE_SIEGE, "Choose Siege for an independent producer")
    capture.wait(
        lambda s: building(s, siege)["recipe"] == SIEGE,
        "independent Siege type selected",
    )
    capture.shot("siege-first-start-180")
    capture.hud(TOGGLE_PRODUCTION, "Pay one-time Siege configuration and lock")
    capture.wait(
        lambda s: (
            building(s, siege)["configured"]
            and building(s, siege)["enabled"]
            and wallet(s, owner)["wallet"] == 0
            and building(s, siege)["joined"] == 0
            and building(s, siege)["travelling"] == 0
            and building(s, siege)["productionState"] == "InsufficientResources"
        ),
        "exactly 180 configures Siege without an unpaid unit",
    )
    capture.shot("siege-locked-waiting-unit-funds")
    capture.hud(TOGGLE_PRODUCTION, "Pause configured Siege before other purchases")
    capture.wait(
        lambda s: not building(s, siege)["enabled"],
        "Siege paused without changing type",
    )
    run.phase("rendered first-Start Siege fee and locked independent two-slot force")


def research(run: NetworkRun, capture: Capture, owner: int) -> None:
    run.request("host", "select", target="none")
    candidate = run.request("host", "placement", kind=WORKSHOP)["placementCandidate"]
    run.request("host", "fund", owner=owner, amount=400)
    run.request("host", "build", kind=WORKSHOP, x=candidate[0], y=candidate[1])
    state = capture.wait(
        lambda s: len(owned_buildings(s, owner, WORKSHOP)) == 1, "owned workshop placed"
    )
    workshop = owned_buildings(state, owner, WORKSHOP)[0]["index"]
    run.request("host", "select", target="building", building=workshop)
    capture.key("F")
    capture.wait(
        lambda s: building(s, workshop)["constructionProgress"] == 1,
        "workshop complete",
    )
    capture.shot("workshop-research")
    capture.hud(RESEARCH_REPAIRS, "Buy Field Repairs")
    capture.wait(lambda s: wallet(s, owner)["doctrine"] == 2, "Field Repairs owned")
    capture.shot("workshop-owned")


def other_resolutions(
    run: NetworkRun,
    capture: Capture,
    barracks: int,
    resolutions: Sequence[tuple[int, int]],
) -> None:
    run.request("host", "select", target="building", building=barracks)
    capture.key("F")
    for width, height in resolutions[1:]:
        run.request("host", "resolution", width=width, height=height)

        def viewport_matches(
            s: JsonObject, width: int = width, height: int = height
        ) -> bool:
            return (s["viewportWidth"], s["viewportHeight"]) == (width, height)

        capture.wait(viewport_matches, f"viewport {width}x{height}")
        capture.shot(f"barracks-{width}x{height}")
    if len(resolutions) > 1:
        width, height = resolutions[0]
        run.request("host", "resolution", width=width, height=height)
        capture.wait(
            lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height),
            "primary viewport restored",
        )


def at_alert(state: JsonObject, alert: JsonObject) -> bool:
    return bool(
        state["focusedAlertSequence"] == alert["sequence"]
        and all(
            abs(state["cameraPosition"][axis] - alert["position"][axis]) < 1
            for axis in (0, 1)
        )
    )


def damage_alert(
    run: NetworkRun, capture: Capture, owner: int, squad: int
) -> JsonObject:
    state = capture.state()
    initial_camera = state["cameraPosition"]
    history = state["objectiveEvents"]
    run.request(
        "host",
        "hqDamage",
        owner=owner,
        army=squad,
        damage=state["enemyHQ"] - 450,
    )
    state = capture.wait(
        lambda s: len(s["objectiveEvents"]) > len(history),
        "single HQ hit raises an attributed threshold alert",
    )
    require(
        state["cameraPosition"] == initial_camera, "receiving an alert moved the camera"
    )
    latest: JsonObject = state["objectiveEvents"][-1]
    require(
        latest["id"] == "enemy_hq_half", "HQ half-health transition is not latest alert"
    )
    require(
        state["objectiveEvents"] == [*history, latest],
        "single HQ hit must add exactly one half-health event",
    )
    require(
        any(f["owner"] == owner for f in latest["forces"]),
        "HQ alert omitted the attacking player's force",
    )
    return latest


def awareness(
    run: NetworkRun, capture: Capture, owner: int, squad: int, barracks: int
) -> None:
    latest = damage_alert(run, capture, owner, squad)
    capture.shot("objective-strip-and-alert-feed")
    capture.minimap(0.25, 0.25)
    before = capture.state()["cameraPosition"]
    run.request("host", "select", target="building", building=barracks)
    require(capture.state()["cameraPosition"] == before, "selecting moved the camera")
    capture.key("SpaceBar")
    state = capture.wait(
        lambda s: at_alert(s, latest),
        "Space actually focuses newest objective alert",
    )
    previous = state["objectiveEvents"][-2]
    capture.key("SpaceBar")
    capture.wait(
        lambda s: at_alert(s, previous),
        "second Space steps to the preceding alert",
    )
    row = next(
        row
        for row in capture.state()["uiAlerts"]
        if row["sequence"] == latest["sequence"]
    )
    capture.minimap(0.5, 0.5)
    run.request("host", "hudClick", x=row["x"], y=row["y"])
    capture.wait(
        lambda s: at_alert(s, latest),
        "clicking the rendered feed row actually focuses its location",
    )
    capture.shot("objective-feed-click-camera")
    alert_expiry(capture, latest)
    run.phase(
        "objective strip, attributed feed, selection stability, Space history and feed click"
    )


def alert_expiry(capture: Capture, latest: JsonObject) -> None:
    state = capture.wait(
        lambda s: (
            not any(row["sequence"] == latest["sequence"] for row in s["uiAlerts"])
        ),
        "alert feed expires without removing objective history",
    )
    require(
        state["objectiveEvents"][-1] == latest, "fading feed removed objective history"
    )
    capture.shot("objective-strip-after-feed-expiry")


def force_selection(
    run: NetworkRun, capture: Capture, owner: int, barracks: int
) -> None:
    state = capture.state()
    army = next(a for a in state["armies"] if a["producer"] == barracks)
    number, actor = army["forceNumber"], army["actorId"]
    before = state["cameraPosition"]
    run.request("host", "select", target="unit", owner=owner, number=number)
    selected = capture.wait(
        lambda s: s["selectedForces"] == [actor] and not s["buildingSelected"],
        "unit selects its force instead of its producer",
    )
    require(selected["cameraPosition"] == before, "unit selection moved camera")
    capture.key("F")
    state = capture.wait(
        lambda s: any(a["actorId"] == actor and "badge" in a for a in s["armies"]),
        "selected force badge is visible after explicit focus",
    )
    run.request("host", "select", target="none")
    before = capture.state()["cameraPosition"]
    run.request("host", "select", target="badge", owner=owner, number=number)
    selected = capture.wait(
        lambda s: (
            s["selectedForces"] == [actor]
            and any(a["actorId"] == actor and a["highlighted"] for a in s["armies"])
        ),
        "shared badge geometry selects and highlights force",
    )
    require(selected["cameraPosition"] == before, "badge selection moved camera")
    require(
        not selected["selectionDragging"],
        "badge click left a selection rectangle without a held pointer",
    )
    capture.shot("force-selected-badge")
    run.request("host", "select", target="building", building=barracks)
    capture.hud(SELECT_FORCE, "Production panel Select force")
    capture.wait(
        lambda s: s["selectedForces"] == [actor] and not s["buildingSelected"],
        "building Select force button switches selection",
    )
    capture.shot("force-selected-panel-button")
    run.request("host", "select", target="building", building=barracks)
    run.phase(
        "unit, badge and building-panel force selection without implicit camera motion"
    )


def focus_box_badges(run: NetworkRun, capture: Capture, owner: int) -> JsonObject:
    def visible(state: JsonObject) -> bool:
        return all("badge" in a for a in state["armies"] if a["owner"] == owner)

    capture.key("F")
    state = capture.state()
    if visible(state):
        return state
    # Focus centres the force set, not the HUD's unobstructed map area.
    run.request("host", "key", key="A", pressed=True)
    try:
        capture.wait(visible, "pan the box fixture badges clear of HUD panels")
    finally:
        run.request("host", "key", key="A", pressed=False)
    return capture.wait(visible, "box fixture badges remain visible after pan stops")


def settle_box_members(capture: Capture, owner: int) -> None:
    previous: dict[int, Sequence[float]] = {}

    def settled(state: JsonObject) -> bool:
        nonlocal previous
        armies = [a for a in state["armies"] if a["owner"] == owner]
        current: dict[int, Sequence[float]] = {
            u["actorId"]: u["position"]
            for army in armies
            for u in army["units"]
            if u["health"] > 0
        }
        stable = current.keys() == previous.keys() and all(
            distance2(position, previous[actor]) <= 0.25**2
            for actor, position in current.items()
        )
        previous = current
        return (
            bool(current)
            and stable
            and all(
                army["status"] == HOLDING and not army["bHoldResponding"]
                for army in armies
                if any(u["health"] > 0 for u in army["units"])
            )
        )

    capture.wait(
        settled,
        "living members settle at quiet posts before precision box geometry",
    )


def force_box_selection(run: NetworkRun, capture: Capture, owner: int) -> None:
    recall_box_fixture(run, capture, owner)
    settle_box_members(capture, owner)
    armies = [a for a in capture.state()["armies"] if a["owner"] == owner]
    require(len(armies) == 2, "box fixture needs the existing two configured forces")
    for index, army in enumerate(armies):
        run.request(
            "host",
            "select",
            target="force",
            owner=owner,
            number=army["forceNumber"],
            toggle=index > 0,
        )
    state = focus_box_badges(run, capture, owner)
    require(
        all("badge" in a for a in state["armies"] if a["owner"] == owner),
        f"nearby box fixture badges are not visible after focus: camera={state['cameraPosition']}",
    )
    badges = [a["badge"] for a in state["armies"] if a["owner"] == owner]
    expected = {a["actorId"] for a in armies}
    camera = state["cameraPosition"]
    start = [min(p[i] for p in badges) - 2 for i in range(2)]
    end = [max(p[i] for p in badges) + 2 for i in range(2)]
    run.request("host", "select", target="none")
    capture.box(end, start)
    state = capture.state()
    require(
        set(state["selectedForces"]) == expected,
        "reverse-drag box did not select both visible force badges",
    )
    require(state["cameraPosition"] == camera, "box selection moved the camera")
    run.request(
        "host", "select", target="force", owner=owner, number=armies[0]["forceNumber"]
    )
    second = next(
        a["badge"] for a in state["armies"] if a["actorId"] == armies[1]["actorId"]
    )
    capture.box(
        [second[0] - 2, second[1] - 2], [second[0] + 2, second[1] + 2], add=True
    )
    state = capture.state()
    require(
        set(state["selectedForces"]) == expected,
        "additive box did not keep the previously selected force",
    )
    require(state["cameraPosition"] == camera, "additive box selection moved camera")
    capture.shot("forces-box-selected")
    run.phase("rendered reverse and additive force-badge box geometry")


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0])
    owner = state["localIndex"]
    deck_controls(run, capture)
    barracks = primary_barracks(run, capture, owner)
    start_and_starve(run, capture, owner, barracks)
    cancel_orders(capture, barracks)
    target = assign_orders(capture, barracks)
    state, squad = fill_force(run, capture, owner, barracks, target)
    force_selection(run, capture, owner, barracks)
    recruit, origin = paid_replacement(run, capture, owner, barracks, squad, state)
    retarget_replacement(run, capture, owner, barracks, target, recruit, origin)
    siege_producer(run, capture, owner, barracks)
    force_box_selection(run, capture, owner)
    research(run, capture, owner)
    other_resolutions(run, capture, barracks, resolutions)
    awareness(run, capture, owner, squad, barracks)
    run.request("host", "finish", owner=owner, army=squad, win=True)
    capture.wait(lambda s: s["result"] == 1, "weapon-caused victory")
    capture.shot("victory")
    no_compositor_windows(run, pid)
    run.event("PASS", captures=capture.count, resolutions=resolutions)
