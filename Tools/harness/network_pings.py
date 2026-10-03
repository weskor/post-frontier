"""Team-only ping delivery, authoritative throttle/expiry and rendered G placement."""

from __future__ import annotations

from collections.abc import Sequence
import time

from harness.hud_setup import boot
from harness.hud_surface import Capture, no_compositor_windows
from harness.network import NetworkRun, require, wallet
from harness.network_session import connect
from harness.verify import JsonObject


def same_position(actual: Sequence[float], expected: Sequence[float]) -> bool:
    return len(actual) == len(expected) and all(
        abs(a - b) < 0.01 for a, b in zip(actual, expected, strict=True)
    )


def event(state: JsonObject, sequence: int) -> JsonObject:
    rows: list[JsonObject] = [
        row for row in state["pingEvents"] if row["sequence"] == sequence
    ]
    require(len(rows) == 1, f"expected retained ping {sequence}, got {len(rows)}")
    return rows[0]


def attribution(row: JsonObject, sender: JsonObject, position: Sequence[float]) -> None:
    require(row["id"] == "ping_look_here", "ground request became a need-help ping")
    require(same_position(row["position"], position), "ping changed its exact spot")
    require(row["sequence"] < 0, "ping feed identity collided with objective events")
    require(row["affectedTeam"] == sender["team"], "ping has the wrong recipient team")
    require(len(row["forces"]) == 1, "ping lost its single sender attribution")
    contributor = row["forces"][0]
    require(
        contributor["owner"] == sender["index"]
        and contributor["team"] == sender["team"]
        and contributor["playerName"] == sender["playerName"]
        and bool(contributor["playerName"].strip())
        and contributor["forceNumber"] == 0,
        "ping did not name the actual remote sender or forged a teammate force",
    )


def no_global_pings(state: JsonObject) -> None:
    require(
        not any(row["id"].startswith("ping_") for row in state["objectiveEvents"]),
        "team communication leaked into the globally replicated objective ring",
    )


def pings_scenario(run: NetworkRun) -> None:
    require(run.clients >= 1, "pings proof requires a real remote client")
    session = connect(run)
    before = run.observe(session.peer)
    sender = wallet(before, session.owner)
    require(
        all(
            wallet(run.observe(name), session.identities[name])["team"]
            == sender["team"]
            for name in session.names
        ),
        "connected peers are not on the sender's team",
    )
    position = [
        before["friendlyHQPosition"][0] + 550,
        before["friendlyHQPosition"][1] - 500,
        0,
    ]
    serial = before["pingFeedbackSerial"]
    require(not before["pingEvents"], "fresh client already has ping history")
    # No intervening convergence wait: both requests reach the authoritative
    # component inside its two-second window, not a local-input throttle.
    run.request(session.peer, "ping", x=position[0], y=position[1], z=position[2])
    run.request(session.peer, "ping", x=position[0] + 100, y=position[1], z=position[2])
    verdict = run.await_states(
        [session.peer],
        lambda values: values[session.peer]["pingFeedbackSerial"] >= serial + 2,
        "two reliable authoritative ping verdicts reach the sending client",
    )[session.peer]
    require(
        not verdict["pingAccepted"], "second remote ping bypassed the server throttle"
    )
    require(
        bool(verdict["orderFeedback"].strip()), "throttled ping supplied no explanation"
    )

    def delivered(values: dict[str, JsonObject]) -> bool:
        for state in values.values():
            no_global_pings(state)
            require(
                len(state["pingEvents"]) <= 1,
                "throttled ping entered a receiver's history",
            )
        return all(len(state["pingEvents"]) == 1 for state in values.values())

    states = run.await_states(
        session.names,
        delivered,
        "remote client's one accepted ping reaches every team peer",
    )
    sequences = {}
    for name, state in states.items():
        row = state["pingEvents"][0]
        attribution(row, sender, position)
        require(
            row["active"], f"{name}: reliable ping arrived after its marker expired"
        )
        require(
            abs(row["serverTime"] - states["host"]["pingEvents"][0]["serverTime"])
            < 0.01,
            f"{name}: ping timestamp differs from the authoritative host event",
        )
        sequences[name] = row["sequence"]
    run.event("ping-delivery", sender=sender, position=position, receivers=states)

    def expired(values: dict[str, JsonObject]) -> bool:
        for name, state in values.items():
            no_global_pings(state)
            require(
                len(state["pingEvents"]) == 1,
                "expiry discarded history or a throttled request later appeared",
            )
            row = event(state, sequences[name])
            attribution(row, sender, position)
            age = state["serverTime"] - row["serverTime"]
            require(
                row["active"] == (age < 6),
                "ping activity does not obey the six-second server-time lifetime",
            )
        return all(
            not event(state, sequences[name])["active"]
            for name, state in values.items()
        )

    run.await_states(
        session.names,
        expired,
        "ping markers expire on all peers after six synchronized server seconds",
    )
    run.phase(
        "remote ping exact spot/name delivered to host team; second RPC explained/rejected; retained history inactive after six seconds"
    )


def marker_visible(state: JsonObject, row: JsonObject) -> None:
    require(
        row["active"] and row["mapProjected"],
        "capture has no active projected ground marker",
    )
    require(
        25 < row["mapX"] < state["viewportWidth"] - 25
        and 25 < row["mapY"] < state["viewportHeight"] * 0.7,
        "ground ping is outside the unobscured map capture area",
    )
    origin, size = state["minimapOrigin"], state["minimapSize"]
    require(
        origin[0] + 10 < row["minimapX"] < origin[0] + size - 10
        and origin[1] + 10 < row["minimapY"] < origin[1] + size - 10,
        "ping diamond is outside the visible minimap capture area",
    )


def pings_hud_scenario(
    run: NetworkRun, resolution: tuple[int, int] = (1600, 900)
) -> None:
    require(
        run.clients == 0 and run.offscreen, "pings-hud requires --clients 0 --offscreen"
    )
    capture = Capture(run)
    pid, _ = boot(run, capture, resolution)
    # Center the camera on map-derived arena geometry, away from either HQ.
    capture.minimap(0.5, 0.5)
    state = capture.state()
    origin, size = state["minimapOrigin"], state["minimapSize"]
    serial = state["pingFeedbackSerial"]
    state = run.request(
        "host",
        "pingAtScreenPosition",
        x=origin[0] + size * 0.5,
        y=origin[1] + size * 0.5,
        withoutPlayerState=True,
    )
    require(
        state["pingFeedbackSerial"] == serial and not state["pingEvents"],
        "minimap input before PlayerState arrives submitted a command or spent ping budget",
    )
    x, y = state["viewportWidth"] // 2, round(state["viewportHeight"] * 0.35)
    state = run.request("host", "cursor", x=x, y=y)
    require(
        "cursorScreen" in state and "cursorWorld" in state,
        "offscreen viewport did not expose its ground cursor",
    )
    require(
        abs(state["cursorScreen"][0] - x) < 1 and abs(state["cursorScreen"][1] - y) < 1,
        "probe cursor did not reach the requested map pixel",
    )
    expected = state["cursorWorld"]
    serial = state["pingFeedbackSerial"]
    capture.key("G")
    ground = capture.wait(
        lambda st: st["pingFeedbackSerial"] > serial,
        "actual Enhanced Input G binding receives a server verdict for the ground cursor",
    )
    require(
        ground["pingAccepted"] and len(ground["pingEvents"]) == 1,
        "actual G failed to place one ground ping",
    )
    first = ground["pingEvents"][0]
    attribution(first, wallet(ground, ground["localIndex"]), expected)
    marker_visible(ground, first)
    path = capture.shot("g-ground-map-and-minimap-ping")
    require(
        event(capture.state(), first["sequence"])["active"],
        "ground ping expired before capture finished",
    )
    run.event("ping-marker-proof", path=str(path), event=first)

    deadline = time.monotonic() + 2.1
    state = capture.wait(
        lambda st: time.monotonic() >= deadline,
        "real two-second G cooldown before minimap placement",
    )
    origin, size = state["minimapOrigin"], state["minimapSize"]
    horizontal, vertical = 0.55, 0.45
    x, y = origin[0] + size * horizontal, origin[1] + size * vertical
    half = state["arenaHalfExtent"]
    expected = [half[0] * (1 - 2 * vertical), half[1] * (2 * horizontal - 1), 0]
    serial = state["pingFeedbackSerial"]
    run.request("host", "pingAtScreenPosition", x=x, y=y)
    minimap = capture.wait(
        lambda st: st["pingFeedbackSerial"] > serial,
        "shared G placement resolves an exact minimap ground point",
    )
    require(
        minimap["pingAccepted"] and len(minimap["pingEvents"]) == 2,
        "minimap placement did not create exactly one additional ping",
    )
    second = minimap["pingEvents"][-1]
    attribution(second, wallet(minimap, minimap["localIndex"]), expected)
    require(
        second["sequence"] != first["sequence"],
        "two local feed pings share one identity",
    )
    marker_visible(minimap, second)
    path = capture.shot("minimap-placement-map-and-minimap-ping")
    require(
        event(capture.state(), second["sequence"])["active"],
        "minimap ping expired before capture finished",
    )
    run.event("ping-marker-proof", path=str(path), event=second)
    expired = capture.wait(
        lambda st: st["serverTime"] - second["serverTime"] >= 6,
        "both retained ping markers finish their six-second lifetime",
    )
    require(
        len(expired["pingEvents"]) == 2
        and all(not row["active"] for row in expired["pingEvents"]),
        "expired ping markers remain active or lose feed history",
    )
    capture.shot("six-second-ping-markers-expired")
    no_compositor_windows(run, pid)
    run.phase(
        "actual G ground and shared minimap placement captured with active map/minimap geometry; expiry capture retains history without markers"
    )
