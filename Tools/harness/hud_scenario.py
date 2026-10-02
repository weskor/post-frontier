"""Full rendered HUD acceptance orchestration and construction/research presentation."""

from __future__ import annotations

from collections.abc import Sequence
from typing import cast

from harness.hud_actions import (
    BUILD_BARRACKS,
    CONSTRUCTION,
    RECIPE_SIEGE,
    RESEARCH_REPAIRS,
    TOGGLE_PRODUCTION,
)
from harness.hud_goals import assign_goals, cancel_goals
from harness.hud_production import (
    fill_force,
    paid_replacement,
    retarget_replacement,
    start_and_starve,
)
from harness.hud_setup import boot, place_barracks
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import (
    BARRACKS,
    SIEGE,
    WORKSHOP,
    NetworkRun,
    building,
    owned_buildings,
    require,
    wallet,
)
from harness.verify import JsonObject


def deck_controls(run: NetworkRun, capture: Capture) -> None:
    capture.shot("start-overview")
    capture.minimap(0.25, 0.25)
    capture.shot("minimap-camera-northwest")
    capture.minimap(0.99, 0.01)
    capture.minimap(0.5, 0.5)
    capture.key("SpaceBar")

    capture.hud(BUILD_BARRACKS, "Build Barracks card enters placement")
    capture.wait(
        lambda s: s["placing"] and not s["hudExpanded"],
        "placement mode with collapsed deck",
    )
    capture.shot("placement-mode")
    capture.minimap(0.75, 0.75)
    capture.key("SpaceBar")
    capture.key("Escape")
    capture.wait(
        lambda s: not s["placing"] and s["hudExpanded"],
        "Escape cancels placement and reopens deck",
    )
    capture.key("F4")
    capture.wait(lambda s: not s["hudExpanded"], "F4 hides deck")
    capture.shot("deck-hidden")
    capture.minimap(0.5, 0.5)
    capture.key("SpaceBar")
    capture.hud(CONSTRUCTION, "Persistent Construction button reopens hidden choices")
    capture.wait(
        lambda s: s["hudExpanded"], "Construction reopens deck without a hotkey"
    )
    capture.key("F4")
    capture.wait(lambda s: not s["hudExpanded"], "F4 hides reopened deck")
    capture.key("F4")
    capture.wait(lambda s: s["hudExpanded"], "F4 reopens deck")
    run.phase("Escape/F4 mappings and persistent Construction action")


def primary_barracks(run: NetworkRun, capture: Capture, owner: int) -> int:
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "owned barracks placed")
    require(
        state["hudExpanded"] and not state["placing"],
        "successful placement did not restore construction choices",
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
    run.request("host", "select", target="building", building=barracks)
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
    capture.key("SpaceBar")
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
    capture.key("SpaceBar")
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
    capture.key("SpaceBar")
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


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0])
    owner = state["localIndex"]
    deck_controls(run, capture)
    barracks = primary_barracks(run, capture, owner)
    start_and_starve(run, capture, owner, barracks)
    cancel_goals(capture, barracks)
    target = assign_goals(capture, barracks)
    state, squad = fill_force(run, capture, owner, barracks, target)
    recruit, origin = paid_replacement(run, capture, owner, barracks, squad, state)
    retarget_replacement(run, capture, owner, barracks, target, recruit, origin)
    siege_producer(run, capture, owner, barracks)
    research(run, capture, owner)
    other_resolutions(run, capture, barracks, resolutions)
    run.request("host", "finish", owner=owner, army=squad, win=True)
    capture.wait(lambda s: s["result"] == 1, "weapon-caused victory")
    capture.shot("victory")
    no_compositor_windows(run, pid)
    run.event("PASS", captures=capture.count, resolutions=resolutions)
