"""Rendered Fortify surfaces, real dock/cast input and explicitly recorded host fixtures."""

from __future__ import annotations

from collections.abc import Sequence
from typing import cast

from harness.hud_force_bar_many import add_force, check_cards, deselect
from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import NetworkRun, require
from harness.verify import JsonObject

FORTIFY = 53


def ability(state: JsonObject) -> JsonObject:
    return cast(JsonObject, state["fortify"])


def region_state(state: JsonObject, region: int) -> JsonObject:
    return cast(
        JsonObject, next(r for r in ability(state)["regions"] if r["index"] == region)
    )


def ready(run: NetworkRun, owner: int, data: int = 80) -> None:
    run.request("host", "fortifyFund", owner=owner, data=data)
    run.request("host", "fortifyCooldown", owner=owner, seconds=0)


def target(run: NetworkRun, capture: Capture, region: int, expected: str) -> None:
    run.request("host", "fortifyHudFocus", region=region)
    capture.state()
    if not ability(capture.state())["targeting"]:
        capture.hud(FORTIFY, "Fortify [H] dock arms targeting")
    capture.wait(
        lambda s: ability(s)["targeting"] and not s["hudExpanded"],
        "Fortify targeting mode",
    )
    run.request("host", "fortifyHudAim", region=region, expected=expected)


def viewport(run: NetworkRun, capture: Capture, width: int, height: int) -> None:
    run.request("host", "resolution", width=width, height=height)
    capture.wait(
        lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height),
        f"Fortify viewport {width}x{height}",
    )
    # Resizing an offscreen viewport can strand its cursor on an edge and engage real edge-pan.
    run.request("host", "cursor", x=width / 2, y=height / 2)


def surface_states(
    run: NetworkRun,
    capture: Capture,
    owner: int,
    valid: int,
    rejected: int,
    width: int,
    height: int,
) -> None:
    suffix = f"{width}x{height}"
    ready(run, owner)
    run.request("host", "fortifyHudFocus", region=valid)
    run.request("host", "fortifyHudLayout")
    capture.shot(f"fortify-dock-ready-{suffix}")
    run.request("host", "fortifyCooldown", owner=owner, seconds=47)
    capture.shot(f"fortify-dock-cooldown-{suffix}")
    ready(run, owner, data=28)
    capture.shot(f"fortify-dock-data-short-{suffix}")
    ready(run, owner)
    target(run, capture, rejected, "rejected")
    capture.shot(f"fortify-target-rejected-{suffix}")
    target(run, capture, valid, "allowed")
    capture.shot(f"fortify-target-valid-{suffix}")
    # The owning-controller RPC path, not a region fixture, performs the initial cast.
    run.request("host", "fortifyCast", region=valid)
    capture.wait(
        lambda s: region_state(s, valid)["team"] == 0 and not ability(s)["targeting"],
        "Fortify cast accepted through controller RPC",
    )
    capture.shot(f"fortify-active-badge-ring-{suffix}")
    ready(run, owner)
    run.request("host", "fortifyHudRemaining", region=valid, seconds=41)
    target(run, capture, valid, "refresh")
    capture.shot(f"fortify-target-refresh-{suffix}")
    capture.key("Escape")
    capture.wait(lambda s: not ability(s)["targeting"], "Escape cancels Fortify")
    run.request("host", "fortifyHudRemaining", region=valid, seconds=9)
    capture.shot(f"fortify-active-warning-{suffix}")
    # A real fixture teammate wallet casts through the shared authority service and team feed.
    run.request("host", "fortifyHudTeammate", region=valid)
    run.phase("fixture teammate authority cast posts the real team ability feed")
    capture.wait(
        lambda s: ability(s)["events"][-1]["id"] == "fortify_cast",
        "teammate Fortify feed delivered",
    )
    capture.shot(f"fortify-teammate-feed-{suffix}")
    run.request("host", "fortifyHudFeedFocus")
    run.request("host", "fortifyHudLost", region=valid)
    capture.wait(
        lambda s: ability(s)["events"][-1]["id"] == "fortify_ended",
        "Fortify region-lost feed delivered",
    )
    capture.shot(f"fortify-ended-feed-{suffix}")
    run.request("host", "fortifyHudFeedFocus")


def crowded_footer(
    run: NetworkRun,
    capture: Capture,
    resolutions: Sequence[tuple[int, int]],
    owner: int,
) -> None:
    known: list[int] = []
    for _ in range(5):
        known.append(add_force(run, capture, owner, known))
    deselect(run, capture)
    for width, height in resolutions:
        viewport(run, capture, width, height)
        run.request("host", "jevPlans", stage="create")
        run.request("host", "jevPlans", stage="replace")
        if not capture.state()["deckOpen"]:
            capture.key("F4")
        state = capture.wait(
            lambda s: s["deckOpen"], "five force cards and pinned deck"
        )
        check_cards(state, owner, 5, width, height)
        ready(run, owner)
        run.request("host", "fortifyHudLayout")
        capture.shot(f"fortify-five-cards-deck-memo-clamp-{width}x{height}")
        capture.key("F4")


def scenario(run: NetworkRun, resolutions: Sequence[tuple[int, int]]) -> None:
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0])
    owner = int(state["localIndex"])
    run.request("host", "income", paused=True)
    regions = ability(capture.state())["regions"]
    friendly = [r["index"] for r in regions if r["controller"] == 0]
    hostile = [r["index"] for r in regions if r["controller"] == 5]
    require(
        bool(friendly) and bool(hostile),
        "Fortify needs controlled friendly and JEV regions",
    )
    valid, rejected = int(friendly[0]), int(hostile[0])
    crowded_footer(run, capture, resolutions, owner)
    for width, height in resolutions:
        viewport(run, capture, width, height)
        surface_states(run, capture, owner, valid, rejected, width, height)
    no_compositor_windows(run, pid)
    run.event(
        "PASS", captures=capture.count, resolutions=resolutions, scenario="fortify"
    )
