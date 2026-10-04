#!/usr/bin/env python3
"""Opt-in real-socket CoopRTS network verification; never reuses single-window sessions."""

from __future__ import annotations

import argparse
import collections
from collections.abc import Sequence
import os
import sys
from typing import TypedDict, cast

from harness.network_peers import require
from harness.network_probe import NetworkProbe
from harness.verify import DEFAULT_MAP, JsonObject
from x.scopes import map_package

# Stable public API shared with the rendered HUD harness.
__all__ = [
    "ATTACK",
    "BARRACKS",
    "MOVE_HOLD",
    "RANGED",
    "RETREAT",
    "SIEGE",
    "WORKSHOP",
    "NetworkRun",
    "alive_units",
    "building",
    "distance2",
    "force",
    "force_arrived",
    "force_counts_match",
    "issue_force_order",
    "minimap_region_point",
    "minimap_world_point",
    "order_destination_matches",
    "order_matches",
    "owned_buildings",
    "region",
    "require",
    "select_order_region",
    "select_producer_force",
    "wallet",
]


class Coordinates(TypedDict):
    x: float
    y: float


class NetworkRun(NetworkProbe):
    def start(self, name: str, *, host: bool = False, rejection: bool = False) -> None:
        self.launch(name, host=host, rejection=rejection)
        # A process is never adopted by name or by a reused PID.
        self.until(
            lambda: self.establish_identity(name),
            f"{name} executable identity",
            30,
            names=[name],
            kind="identity",
        )
        if host:
            self.until(
                lambda: self.selected_map_ready(name),
                f"{name} selected map {self.map_path}",
                5,
                names=[name],
                kind="readiness",
            )
        if not rejection:
            self.until(
                lambda: (
                    self.observe(name, ready=False) if self.reply_ready(name) else None
                ),
                f"{name} first probe response",
                120,
                names=[name],
                kind="readiness",
            )

    def isolate_fresh_host(self, previous_generation: int, site_count: int) -> None:
        states = self.await_states(
            ["host"],
            lambda states: (
                states["host"]["generation"] > previous_generation
                and len(states["host"]["sites"]) == site_count
            ),
            "host exposes the initialized fresh world before autonomous play",
            allow_travel=True,
        )
        state = states["host"]
        require(
            state["result"] == 0
            and state["friendlyHQ"] == 900
            and state["enemyHQ"] == 900
            and not any(a["team"] == 0 for a in state["armies"])
            and not any(b["team"] == 0 for b in state["buildings"])
            and all(
                site["owner"] == -1 and site["progress"] == 0 for site in state["sites"]
            ),
            "new authoritative world did not expose fresh HQs, empty player bases and neutral sectors",
        )
        # Preserve the observed reset while delayed clients replicate. This
        # stops autonomous orders; it does not clear sites, health or progress.
        self.request("host", "isolate")
        self.phase(
            "fresh authoritative reset observed before stopping autonomous play for peer convergence"
        )

    def rejected(self, name: str, text: str) -> None:
        entry = self.peers[name]
        log = entry["folder"] / "game.log"

        def observed_rejection() -> bool:
            if log.exists() and text in log.read_text(errors="replace"):
                return True
            require(
                entry["process"].poll() is None,
                f"{name} exited before expected server rejection {text!r}; see logs",
            )
            return False

        self.until(
            observed_rejection,
            f"{name} expected server rejection {text!r}",
            45,
            names=["host"],
        )
        # Rejection must not leave a controller with an assigned commander/world.
        if self.live(name) and self.reply_ready(name):
            snapshot = self.request(name, allow_unavailable=True)
            require(
                not snapshot["ready"] or snapshot["localIndex"] == -1,
                f"rejected peer {name} received commander identity",
            )
        self.event("rejected", peer=name, reason=text)
        self.stop(name)


def army(state: JsonObject, commander: int, index: int) -> JsonObject:
    matches = [
        a for a in state["armies"] if a["owner"] == commander and a["army"] == index
    ]
    require(
        len(matches) == 1, f"expected one army {commander}/{index}, got {len(matches)}"
    )
    return cast(JsonObject, matches[0])


def wallet(state: JsonObject, commander: int) -> JsonObject:
    matches = [p for p in state["players"] if p["index"] == commander]
    require(
        len(matches) == 1, f"expected one wallet for {commander}, got {len(matches)}"
    )
    return cast(JsonObject, matches[0])


def owned_buildings(
    state: JsonObject, owner: int, kind: int | None = None
) -> list[JsonObject]:
    return [
        b
        for b in state["buildings"]
        if b["owner"] == owner and (kind is None or b["kind"] == kind)
    ]


def building(state: JsonObject, index: int) -> JsonObject:
    matches = [b for b in state["buildings"] if b["index"] == index]
    require(len(matches) == 1, f"expected living building {index}, got {len(matches)}")
    return cast(JsonObject, matches[0])


def force(state: JsonObject, owner: int, index: int) -> JsonObject:
    producer = building(state, index)
    return army(state, owner, producer["forceID"])


def alive_units(group: JsonObject) -> list[JsonObject]:
    return [u for u in group["units"] if u["health"] > 0]


def distance2(a: Sequence[float], b: Sequence[float]) -> float:
    return sum((a[i] - b[i]) ** 2 for i in (0, 1))


def force_counts_match(state: JsonObject, owner: int, index: int) -> bool:
    producer = building(state, index)
    matches = [
        a
        for a in state["armies"]
        if a["owner"] == owner and a["army"] == producer["forceID"]
    ]
    if len(matches) != 1:
        return False  # Actor references and their fields can replicate in separate updates.
    group = matches[0]
    members = alive_units(group)
    travelling = sum(u["reinforcing"] for u in members)
    slots = [u["slot"] for u in members]
    return (
        producer["configured"]
        and group["producer"] == index
        and producer["travelling"] == travelling
        and producer["joined"] == len(members) - travelling
        and len(members) <= producer["capacity"]
        and len(set(slots)) == len(slots)
        and all(
            0 <= u["slot"] < producer["capacity"]
            and u["owner"] == owner
            and u["role"] == producer["recipe"]
            and u["producer"] == index
            for u in members
        )
    )


def near(anchor: Sequence[float], dx: float, dy: float) -> Coordinates:
    """Map points are offsets from replicated HQ/sector positions, never literal coordinates."""
    return {"x": anchor[0] + dx, "y": anchor[1] + dy}


def minimap_world_point(
    state: JsonObject, point: Sequence[float]
) -> tuple[float, float]:
    """Inverse of the production minimap transform, without screen-size assumptions."""
    half = state["arenaHalfExtent"]
    origin, size = state["minimapOrigin"], state["minimapSize"]
    horizontal = (point[1] + half[1]) / (2 * half[1])
    vertical = (half[0] - point[0]) / (2 * half[0])
    require(
        0 < horizontal < 1 and 0 < vertical < 1,
        "order target is outside the clickable minimap",
    )
    return origin[0] + size * horizontal, origin[1] + size * vertical


def minimap_region_point(state: JsonObject, target: int) -> tuple[float, float]:
    return minimap_world_point(state, region(state, target)["anchor"])


def select_producer_force(run: NetworkRun, peer: str, index: int) -> JsonObject:
    state = run.observe(peer)
    matches = [a for a in state["armies"] if a["producer"] == index]
    require(len(matches) == 1, f"producer {index} has no unique replicated force")
    army = matches[0]
    run.request(
        peer, "select", target="force", owner=army["owner"], number=army["forceNumber"]
    )
    selected = run.observe(peer)
    require(
        selected["selectedForces"] == [army["actorId"]]
        and not selected["buildingSelected"],
        "producer force did not enter owner-only command selection",
    )
    return selected


def issue_force_order(
    run: NetworkRun,
    peer: str,
    index: int,
    verb: int,
    target: int = -1,
    *,
    enemy_hq: bool = False,
    queue: bool = False,
) -> None:
    """Select the real force, then use the controller's right-click/A/R entry points."""
    state = select_producer_force(run, peer, index)
    if verb == RETREAT:
        run.request(peer, "retreat", queue=queue)
        return
    point = (
        minimap_world_point(state, state["enemyHQPosition"])
        if enemy_hq
        else minimap_region_point(state, target)
    )
    if verb == ATTACK and not enemy_hq:
        run.request(peer, "beginAttack")
        run.request(peer, "confirmAttack", x=point[0], y=point[1], queue=queue)
    else:
        require(verb == MOVE_HOLD or enemy_hq, "unsupported selection order verb")
        run.request(peer, "orderClick", x=point[0], y=point[1], queue=queue)


def region(state: JsonObject, index: int) -> JsonObject:
    matches = [r for r in state["regions"] if r["index"] == index]
    require(len(matches) == 1, f"expected region {index}, got {len(matches)}")
    return cast(JsonObject, matches[0])


def reachable_regions(state: JsonObject, source: int) -> list[JsonObject]:
    """Deterministic neighbour traversal; targets come from replicated region data."""
    graph = {r["index"]: r for r in state["regions"]}
    require(source in graph, f"region graph is missing source {source}")
    pending = collections.deque([source])
    seen = {source}
    result = []
    while pending:
        current = pending.popleft()
        result.append(graph[current])
        for neighbour in sorted(graph[current]["neighbours"]):
            if neighbour in graph and neighbour not in seen:
                seen.add(neighbour)
                pending.append(neighbour)
    return result


def select_order_region(
    state: JsonObject,
    index: int,
    exclude: Sequence[int] = (),
    min_distance: float = 1500,
) -> JsonObject:
    """First reachable neutral region far from the producer, in graph order."""
    producer = building(state, index)
    source = next(
        r["index"] for r in state["regions"] if r["homeTeam"] == producer["team"]
    )
    candidates = [
        r
        for r in reachable_regions(state, source)
        if r["homeTeam"] == -1
        and r["index"] not in exclude
        and distance2(r["anchor"], producer["position"]) > min_distance**2
    ]
    require(
        bool(candidates),
        "map has no reachable, non-main region sufficiently far from the producer",
    )
    return candidates[0]


def order_matches(state: JsonObject, index: int, verb: int, target: int) -> bool:
    producer = building(state, index)
    return cast(
        bool, producer["forceVerb"] == verb and producer["targetRegionIndex"] == target
    )


def region_contains(area: JsonObject, point: Sequence[float]) -> bool:
    """Match inclusive polygon boundaries, including degenerate-edge rejection."""
    polygon = area["polygon"]
    if len(polygon) < 3:
        return False
    inside = False
    for previous, current in zip(polygon, [*polygon[1:], polygon[0]], strict=True):
        dx, dy = current[0] - previous[0], current[1] - previous[1]
        ox, oy = point[0] - previous[0], point[1] - previous[1]
        length2 = dx * dx + dy * dy
        dot = ox * dx + oy * dy
        if (
            length2 > 0
            and abs(dx * oy - dy * ox) <= 1e-6 * length2**0.5
            and 0 <= dot <= length2
        ):
            return True
        if (previous[1] > point[1]) != (current[1] > point[1]) and (
            point[0] < previous[0] + oy * dx / dy
        ):
            inside = not inside
    return inside


def order_destination_matches(
    state: JsonObject, owner: int, index: int, target: int
) -> bool:
    if not force_counts_match(state, owner, index):
        return False
    group = force(state, owner, index)
    if not (
        order_matches(state, index, MOVE_HOLD, target)
        and group["forceVerb"] == MOVE_HOLD
        and group["targetRegionIndex"] == target
        and group["waypointRegionIndex"] == target
    ):
        return False
    area = region(state, target)
    if group["status"] == HOLDING and group["holdRegionIndex"] != -1:
        if group["holdRegionIndex"] != target or group["holdPostIndex"] < 0:
            return False
        if not region_contains(area, group["holdPostLocation"]):
            return False
        if group["bHoldResponding"]:
            # A missing threat during commitment grace cannot prove a live endpoint.
            return bool(
                group["holdThreatId"] != -1
                and distance2(group["destination"], group["holdThreatPosition"]) <= 1
            )
        return distance2(group["destination"], group["holdPostLocation"]) <= 1
    # March/refill and neutral capture retain the complete-formation anchor bound.
    return distance2(group["destination"], area["anchor"]) <= 75**2


def force_arrived(state: JsonObject, owner: int, index: int, target: int) -> bool:
    """Physical arrival, not merely accepted intent or an early Holding status."""
    if not force_counts_match(state, owner, index):
        return False
    group = force(state, owner, index)
    area = region(state, target)
    destination_valid = (
        group["holdRegionIndex"] == target
        and order_destination_matches(state, owner, index, target)
    ) or (
        order_matches(state, index, RETREAT, -1)
        and group["forceVerb"] == RETREAT
        and group["targetRegionIndex"] == -1
        and group["status"] == REFILLING
        and distance2(group["destination"], area["anchor"]) <= 75**2
    )
    return (
        destination_valid
        and group["status"] in (HOLDING, REFILLING)
        and not group["bHoldResponding"]
        and group["waypointRegionIndex"] == target
        and region_contains(area, group["center"])
        and distance2(group["center"], group["destination"]) < 150**2
    )


# Building definition indices (DA_MatchContent order) and EUnitRole recipes used by the probe.
BARRACKS, EXTRACTOR, WORKSHOP = 0, 1, 2
FRONTLINE, RANGED, SIEGE = 0, 1, 2
MOVE_HOLD, ATTACK, RETREAT = 0, 1, 2
# EForceStatus ordinals used for physical order-completion assertions.
MARCHING, HOLDING, WITHDRAWING, RETREATING, REFILLING = 0, 1, 2, 3, 4


def configure(parser: argparse.ArgumentParser) -> None:
    from harness.network_scenarios import SCENARIOS

    parser.epilog = (
        "Slices (each starts its own host and clients):\n"
        + "\n".join(f"  {name}: {summary}" for name, (_, summary) in SCENARIOS.items())
        + "\nProves one replicated contract through the loopback in-process probe. "
        "Cannot prove other slices, OS input, rendering, Steam or WAN. Construction "
        "runs the continuous acceptance chain; human coordination remains unproved."
    )
    parser.epilog += (
        "\nSelected-force order scenarios render offscreen at 1600x900 so their "
        "ground/minimap pointer input uses a real viewport; rendering alone adds no visual proof."
    )
    parser.add_argument("--mode", choices=("editor", "packaged"), required=True)
    parser.add_argument(
        "--map",
        type=map_package,
        default=DEFAULT_MAP,
        help="world package path (default: %(default)s)",
    )
    parser.add_argument("--clients", type=int, choices=(0, 1, 4), required=True)
    parser.add_argument(
        "--offscreen",
        action="store_true",
        help="render Vulkan offscreen at 1600x900 for HUD captures; not compositor/OS input",
    )
    parser.add_argument(
        "--scenario",
        choices=list(SCENARIOS),
        default="construction",
        help="independent slice, or the full construction acceptance chain (default)",
    )
    parser.add_argument(
        "--emulation",
        action="store_true",
        help="require observed native PktLag=120/PktLoss=8 on every peer",
    )
    parser.add_argument(
        "--rendered",
        action="store_true",
        help="packaged Vulkan fallback if the real artifact rejects -nullrhi; no automatic visual proof",
    )
    parser.add_argument(
        "--max-fps",
        type=int,
        help="bound each verification peer's render/game frame rate",
    )


if __name__ == "__main__":
    if not os.environ.get("X_RUN_ID"):
        sys.exit("run through ./x verify")
    from harness.network_cli import main

    main()
