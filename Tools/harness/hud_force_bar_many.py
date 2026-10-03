"""Force bar with several cards: two forces, then five including an orphan, deck beside or pinned."""

from __future__ import annotations

from collections.abc import Callable, Sequence
from typing import cast

from harness.hud_actions import TOGGLE_PRODUCTION
from harness.hud_jev import rects_overlap
from harness.hud_setup import place_barracks
from harness.hud_surface import Capture
from harness.network import BARRACKS, NetworkRun, building, owned_buildings, require
from harness.verify import JsonObject

# ui.md "Force-bar layout": the deck is 720 wide and sits 8 beside the cards, 10 from the edge.
DECK_WIDTH = 720
GAP = 8
MARGIN = 10


def own_cards(state: JsonObject, owner: int) -> list[JsonObject]:
    cards = [
        cast(JsonObject, group)
        for group in state["armies"]
        if group["owner"] == owner
        and "forceCard" in group
        and group["forceCard"]["owned"]
    ]
    return sorted(cards, key=lambda group: group["forceNumber"])


def deck_fits_beside(state: JsonObject, cards: Sequence[JsonObject]) -> bool:
    width, height = state["viewportWidth"], state["viewportHeight"]
    scale = min(1.0, max(0.78, min(width / 1280, height / 720)))
    right = max(c["forceCard"]["rect"][0] + c["forceCard"]["rect"][2] for c in cards)
    return (width - MARGIN * scale) - (right + GAP * scale) >= DECK_WIDTH * scale


def deselect(run: NetworkRun, capture: Capture) -> None:
    run.request("host", "select", target="none")
    capture.wait(
        lambda s: not s["buildingSelected"] and not s["selectedForces"], "no selection"
    )


def add_force(
    run: NetworkRun, capture: Capture, owner: int, known: Sequence[int]
) -> int:
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(
        run, capture, owner, len(known) + 1, f"force bar barracks {len(known) + 1}"
    )
    index = next(
        b["index"]
        for b in owned_buildings(state, owner, BARRACKS)
        if b["index"] not in known
    )
    capture.wait(
        lambda s: building(s, index)["constructionProgress"] == 1,
        f"barracks {index} complete",
    )
    # A selected building opens the deck even where it does not fit beside the cards.
    capture.hud(TOGGLE_PRODUCTION, f"lock force of barracks {index}")
    capture.wait(lambda s: building(s, index)["configured"], f"barracks {index} forced")
    return int(index)


def check_cards(
    state: JsonObject, owner: int, count: int, width: int, height: int
) -> list[JsonObject]:
    cards = own_cards(state, owner)
    require(len(cards) == count, f"expected {count} own force cards, saw {len(cards)}")
    rects = [c["forceCard"]["rect"] for c in cards]
    for card in cards:
        view = card["forceCard"]
        require(view["clearsPanels"], "force card overlaps build bar, deck or minimap")
        x, y, w, h = view["rect"]
        require(
            x >= 0 and y >= 0 and x + w <= width and y + h <= height,
            "force card leaves the viewport",
        )
    require(
        not any(
            rects_overlap(a, b) for i, a in enumerate(rects) for b in rects[i + 1 :]
        ),
        "force cards overlap each other",
    )
    return cards


def at_viewport(
    owner: int, count: int, width: int, height: int
) -> Callable[[JsonObject], bool]:
    def ready(state: JsonObject) -> bool:
        return (state["viewportWidth"], state["viewportHeight"]) == (
            width,
            height,
        ) and len(own_cards(state, owner)) == count

    return ready


def capture_cards(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    label: str,
    owner: int,
    count: int,
) -> None:
    for width, height in resolutions:
        run.request("host", "resolution", width=width, height=height)
        state = capture.wait(
            at_viewport(owner, count, width, height),
            f"{count} force cards at {width}x{height}",
        )
        cards = check_cards(state, owner, count, width, height)
        require(state["centreClear"], "the middle of the screen is HUD by default")
        require(
            state["deckOpen"] == deck_fits_beside(state, cards),
            "the deck must start collapsed exactly where it does not fit beside the cards",
        )
        capture.shot(f"force-bar-{label}-{width}x{height}")


def pinned_deck(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
    count: int,
) -> None:
    for width, height in resolutions:
        run.request("host", "resolution", width=width, height=height)
        state = capture.wait(
            at_viewport(owner, count, width, height),
            f"{count} force cards at {width}x{height}",
        )
        if state["deckOpen"]:
            continue
        capture.key("F4")
        state = capture.wait(lambda s: s["deckOpen"], "F4 opens the collapsed deck")
        check_cards(state, owner, count, width, height)
        capture.shot(f"force-bar-five-cards-deck-pinned-{width}x{height}")
        capture.key("F4")
        capture.wait(lambda s: not s["deckOpen"], "F4 collapses the pinned deck")


def many_states(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
    barracks: int,
) -> None:
    known = [barracks]
    known.append(add_force(run, capture, owner, known))
    deselect(run, capture)
    capture_cards(run, capture, resolutions, "two-cards", owner, 2)
    for _ in range(3):
        known.append(add_force(run, capture, owner, known))
    run.request("host", "destroyBuilding", building=known[2])
    state = capture.wait(
        lambda s: any(
            c["forceCard"]["productionText"].startswith("Orphan")
            for c in own_cards(s, owner)
        ),
        "destroyed producer leaves an orphan force card",
    )
    require(len(own_cards(state, owner)) == 5, "orphan lost its card")
    deselect(run, capture)
    capture_cards(run, capture, resolutions, "five-cards-with-orphan", owner, 5)
    pinned_deck(run, capture, resolutions, owner, 5)
