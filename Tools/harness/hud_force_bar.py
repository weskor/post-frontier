"""Force-card shared clicks and live state captures at every requested resolution."""

from __future__ import annotations

from collections.abc import Sequence
from typing import cast

from harness.hud_actions import TOGGLE_PRODUCTION
from harness.hud_jev import check_display, rects_overlap
from harness.hud_setup import boot, place_barracks
from harness.hud_surface import Capture, minimap_region_point, no_compositor_windows
from harness.network import (
    ATTACK,
    BARRACKS,
    NetworkRun,
    alive_units,
    building,
    force,
    owned_buildings,
    require,
    select_order_region,
)
from harness.verify import JsonObject


def card(state: JsonObject, owner: int, barracks: int) -> JsonObject:
    group = force(state, owner, barracks)
    require("forceCard" in group, "force bar card was not visible")
    return cast(JsonObject, group["forceCard"])


def click(run: NetworkRun, owner: int, number: int, control: str) -> None:
    run.request("host", "forceCard", owner=owner, number=number, control=control)
    run.phase(f"force card shared geometry click: {control}")


def capture_states(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    label: str,
    owner: int,
    barracks: int,
) -> None:
    for width, height in resolutions:
        run.request("host", "resolution", width=width, height=height)

        def viewport_matches(
            state: JsonObject, width: int = width, height: int = height
        ) -> bool:
            return (state["viewportWidth"], state["viewportHeight"]) == (width, height)

        state = capture.wait(
            viewport_matches,
            f"force bar viewport {width}x{height}",
        )
        view = card(state, owner, barracks)
        require(
            view["joined"] == building(state, barracks)["joined"]
            and view["capacity"] == building(state, barracks)["capacity"]
            and view["owned"],
            "force card strength/access disagrees with its real force",
        )
        require(view["clearsPanels"], "force card overlaps deck/build bar/minimap")
        capture.shot(f"force-bar-{label}-{width}x{height}")


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0])
    owner = state["localIndex"]
    run.request("host", "income", paused=True)
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "force bar producer placed")
    barracks = owned_buildings(state, owner, BARRACKS)[0]["index"]
    capture.wait(
        lambda s: building(s, barracks)["constructionProgress"] == 1,
        "force bar producer completed",
    )
    capture.hud(TOGGLE_PRODUCTION, "lock initial Frontline force")
    state = capture.wait(
        lambda s: building(s, barracks)["configured"], "force bar force configured"
    )
    number = building(state, barracks)["forceNumber"]
    refill_states(run, capture, resolutions, owner, barracks, number)
    order_states(run, capture, resolutions, owner, barracks, number)
    memo_states(run, capture, resolutions, owner, barracks)
    teammate_states(run, capture, resolutions)
    no_compositor_windows(run, pid)
    run.event(
        "PASS", captures=capture.count, resolutions=resolutions, scenario="force-bar"
    )


def refill_states(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
    barracks: int,
    number: int,
) -> None:
    capture.wait(
        lambda s: (
            0
            < building(s, barracks)["productionSeconds"]
            < building(s, barracks)["unitTime"]
            and building(s, barracks)["joined"] > 0
        ),
        "force bar refill progressing",
    )
    click(run, owner, number, "production")
    paused = capture.wait(
        lambda s: not building(s, barracks)["enabled"], "card pauses paid refill"
    )
    require(
        card(paused, owner, barracks)["productionProgress"] > 0,
        "paused card lost its in-progress refill",
    )
    capture_states(run, capture, resolutions, "paused-refill", owner, barracks)
    click(run, owner, number, "production")
    capture.wait(
        lambda s: (
            building(s, barracks)["joined"] == building(s, barracks)["capacity"]
            and building(s, barracks)["travelling"] == 0
        ),
        "force bar paid members join to full strength",
    )
    click(run, owner, number, "select")
    require(
        force(capture.state(), owner, barracks)["actorId"]
        in capture.state()["selectedForces"],
        "card click did not select its own force",
    )
    capture_states(run, capture, resolutions, "full", owner, barracks)


def order_states(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
    barracks: int,
    number: int,
) -> None:
    state = capture.state()
    target = select_order_region(state, barracks)["index"]
    click(run, owner, number, "attack")
    state = capture.state()
    require(state["assigningOrder"], "card did not open selected-force Attack mode")
    x, y = minimap_region_point(state, target)
    run.request("host", "hudClick", x=x, y=y)
    capture.wait(
        lambda s: (
            force(s, owner, barracks)["forceVerb"] == ATTACK
            and force(s, owner, barracks)["targetRegionIndex"] == target
            and force(s, owner, barracks)["status"] == 0
        ),
        "card Attack marches on confirmed region",
    )
    capture_states(run, capture, resolutions, "marching", owner, barracks)
    click(run, owner, number, "production")
    click(run, owner, number, "60")
    state = capture.state()
    group = force(state, owner, barracks)
    for victim in alive_units(group)[:3]:
        run.request(
            "host", "kill", owner=owner, army=group["army"], slot=victim["slot"]
        )
    capture.wait(
        lambda s: (
            card(s, owner, barracks)["presentationState"] == 2
            and card(s, owner, barracks)["joined"] == 3
            and force(s, owner, barracks)["resumeCount"] == 5
        ),
        "three casualties trigger fighting withdrawal that resumes at five joined",
    )
    capture_states(run, capture, resolutions, "withdrawing-3-of-6", owner, barracks)
    click(run, owner, number, "retreat")
    capture.wait(
        lambda s: force(s, owner, barracks)["forceVerb"] == 2,
        "card Retreat replaces retained Attack",
    )
    capture_states(run, capture, resolutions, "manual-retreat", owner, barracks)


def inspected_teammate(state: JsonObject) -> JsonObject:
    return cast(
        JsonObject,
        next(
            group
            for group in state["armies"]
            if "forceCard" in group and not group["forceCard"]["owned"]
        ),
    )


def teammate_states(
    run: NetworkRun, capture: Capture, resolutions: Sequence[tuple[int, int]]
) -> None:
    state = run.request("host", "forceCardTeammate")
    run.phase("teammate card inspection using real fixture player/force actors")
    require(not state["selectedForces"], "inspection selected a teammate for commands")
    group = inspected_teammate(state)
    before = len(state["pingEvents"])
    click(run, group["owner"], group["forceNumber"], "ping")
    state = capture.state()
    require(len(state["pingEvents"]) == before + 1, "teammate card did not emit a ping")
    for width, height in resolutions:
        run.request("host", "resolution", width=width, height=height)

        def viewport_matches(state: JsonObject, width: int = width, height: int = height) -> bool:
            return (state["viewportWidth"], state["viewportHeight"]) == (width, height)

        state = capture.wait(viewport_matches, f"teammate card viewport {width}x{height}")
        view = inspected_teammate(state)["forceCard"]
        require(not view["owned"] and not state["selectedForces"], "teammate card became commandable")
        require(view["clearsPanels"], "teammate card overlaps build bar/minimap")
        capture.shot(f"force-bar-teammate-read-only-{width}x{height}")


def memo_states(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
    barracks: int,
) -> None:
    run.request("host", "jevPlans", stage="create")
    run.phase("published JEV plans alongside the expanded deck and force bar")
    for width, height in resolutions:
        run.request("host", "resolution", width=width, height=height)

        def ready(state: JsonObject, width: int = width, height: int = height) -> bool:
            return (
                (state["viewportWidth"], state["viewportHeight"]) == (width, height)
                and len(state["jevIntent"]["memos"]) >= 2
            )

        state = capture.wait(ready, f"two memo rows with expanded deck at {width}x{height}")
        require(state["hudExpanded"], "memo acceptance requires the expanded deck")
        check_display(state, f"force bar expanded deck {width}x{height}")
        view = card(state, owner, barracks)
        require(view["clearsPanels"], "force bar overlaps the expanded deck")
        require(
            all(
                not rects_overlap(view["rect"], memo["rect"])
                for memo in state["jevIntent"]["memos"]
            ),
            "JEV memos overlap the force card",
        )
        capture.shot(f"force-bar-expanded-deck-two-memos-{width}x{height}")
