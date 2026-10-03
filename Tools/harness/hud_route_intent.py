"""Selected force path/queue, pre-confirm hover and teammate intent captures."""

from __future__ import annotations

from harness.hud_surface import Capture, minimap_region_point
from harness.network import (
    ATTACK,
    MOVE_HOLD,
    NetworkRun,
    force,
    region_contains,
    require,
)
from harness.verify import JsonObject


def frame_route(
    run: NetworkRun, capture: Capture, owner: int, barracks: int
) -> tuple[JsonObject, int, int]:
    before = capture.state()
    original = force(before, owner, barracks)
    source = next(
        r for r in before["regions"] if region_contains(r, original["center"])
    )
    target = next(
        r
        for r in before["regions"]
        if r["index"] in source["neighbours"] and r["homeTeam"] != 5
    )
    home, destination = source["index"], target["index"]
    run.request(
        "host", "select", target="force", owner=owner, number=original["forceNumber"]
    )
    capture.key("F")
    capture.key("F4")
    capture.wait(lambda s: not s["hudExpanded"], "route surface inspector collapsed")
    for _ in range(4):
        capture.key("MouseScrollDown")
    frame = capture.state()
    midpoint = [
        (a + b) / 2 for a, b in zip(original["center"], target["anchor"], strict=True)
    ]
    half = frame["arenaHalfExtent"]
    origin, size = frame["minimapOrigin"], frame["minimapSize"]
    run.request(
        "host",
        "hudClick",
        x=origin[0] + size * (midpoint[1] + half[1]) / (2 * half[1]),
        y=origin[1] + size * (half[0] - midpoint[0]) / (2 * half[0]),
    )
    capture.wait(
        lambda s: all(abs(s["cameraPosition"][i] - midpoint[i]) < 1 for i in (0, 1)),
        "route endpoints framed without inspector occlusion",
    )
    return original, home, destination


def publish_queue(
    run: NetworkRun,
    capture: Capture,
    owner: int,
    barracks: int,
    home: int,
    destination: int,
) -> JsonObject:
    run.request(
        "host",
        "order",
        building=barracks,
        forceVerb=ATTACK,
        targetRegionIndex=destination,
        targetEnemyHQ=False,
        queue=False,
    )
    run.request(
        "host",
        "order",
        building=barracks,
        forceVerb=MOVE_HOLD,
        targetRegionIndex=home,
        targetEnemyHQ=False,
        queue=True,
    )
    state = capture.wait(
        lambda s: (
            len(force(s, owner, barracks)["intentRoutes"]) == 2
            and force(s, owner, barracks)["intentRoutes"][0]["regions"][-1]
            == destination
            and force(s, owner, barracks)["intentRoutes"][1]["regions"][-1] == home
        ),
        "active path and successive queued return leg",
    )
    return state


def route_intent(run: NetworkRun, capture: Capture, owner: int, barracks: int) -> None:
    original, home, destination = frame_route(run, capture, owner, barracks)
    state = publish_queue(run, capture, owner, barracks, home, destination)
    orders = force(state, owner, barracks)["orders"]
    capture.shot("route-selected-path-queue")
    preview_target = home
    require(preview_target != destination, "preview target duplicates the active target")
    x, y = minimap_region_point(state, preview_target)
    run.request("host", "cursor", x=x, y=y)
    preview = capture.wait(
        lambda s: (
            s["routePreview"]
            and s["previewRegion"] == preview_target
            and s["previewVerb"] == MOVE_HOLD
        ),
        "smart right-click route preview before confirmation",
    )
    require(
        force(preview, owner, barracks)["orders"] == orders,
        "hover preview committed an order",
    )
    capture.shot("route-smart-hover-preview")
    run.request("host", "routeTeammate", targetRegionIndex=destination, enabled=True)
    state = capture.wait(
        lambda s: any(
            a["owner"] != owner
            and a["team"] == 0
            and a["intentRoutes"]
            and a["intentRoutes"][0]["regions"][-1] == destination
            for a in s["armies"]
        ),
        "second commander's live order intent",
    )
    capture.shot("route-teammate-intent-map-minimap")
    run.request("host", "routeTeammate", targetRegionIndex=destination, enabled=False)
    capture.key("F4")
    for _ in range(4):
        capture.key("MouseScrollUp")
    run.request(
        "host",
        "order",
        building=barracks,
        forceVerb=original["forceVerb"],
        targetRegionIndex=original["targetRegionIndex"],
        targetEnemyHQ=False,
        queue=False,
    )
    run.request("host", "select", target="building", building=barracks)
    run.phase(
        "rendered selected route/queue, smart hover preview, commander-coloured teammate world/minimap intent; controlled second-commander fixture, not remote replication"
    )
