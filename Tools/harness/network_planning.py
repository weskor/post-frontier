"""Planning phase: kits, unit type, Ready and the countdown reach a real remote client; the last Ready unfreezes both."""

from __future__ import annotations

from collections.abc import Callable
import os
from typing import cast

from harness.network import FRONTLINE, RANGED, NetworkRun, require
from harness.verify import JsonObject

# The game process keeps the real planning phase (every other probe skips it) when this is set.
KEEP_PLANNING = "COOPRTS_NET_KEEP_PLANNING"
# What the server answers an edit sent while Ready (PlanningPolicy::EditRejection).
LOCKED_REASON = "Ready locks your kit; press Enter to un-Ready first."


def kit(state: JsonObject, slot: int) -> JsonObject:
    """The commander's kit as this peer replicates it, or an empty one before it arrives."""
    for entry in state["planning"]["kits"]:
        if entry["slot"] == slot:
            return cast(JsonObject, entry)
    return {"ready": False, "role": -1, "power": -1, "barracks": None, "rig": None}


def finished(piece: JsonObject | None) -> bool:
    return piece is not None and bool(piece["complete"])


def same_place(a: JsonObject | None, b: JsonObject | None) -> bool:
    if not a or not b:
        return False
    return all(
        abs(x - y) < 1.0 for x, y in zip(a["location"], b["location"], strict=True)
    )


def await_all(
    run: NetworkRun,
    names: list[str],
    check: Callable[[JsonObject], object],
    description: str,
) -> dict[str, JsonObject]:
    return run.await_states(
        names, lambda states: all(check(s) for s in states.values()), description
    )


def open_session(run: NetworkRun, names: list[str]) -> dict[str, int]:
    run.start("host", host=True)
    run.await_states(
        ["host"],
        lambda s: s["host"]["localIndex"] >= 0 and bool(s["host"]["planning"]["kits"]),
        "host opens in planning with its own kit slot",
        allow_loading=True,
    )
    for name in names[1:]:
        run.start(name)
    states = run.await_states(
        names,
        lambda values: all(
            s["localIndex"] >= 0
            and len(s["players"]) == len(names)
            and s["planning"]["active"]
            and len(s["planning"]["kits"]) == len(names)
            for s in values.values()
        ),
        "every peer sees every commander's kit slot",
        allow_loading=True,
    )
    return {name: state["localIndex"] for name, state in states.items()}


def frozen_and_counting(run: NetworkRun, names: list[str]) -> None:
    states = await_all(
        run,
        names,
        lambda s: s["worldPaused"] and s["planning"]["remaining"] > 0,
        "planning freezes every peer's world",
    )
    require(
        all(
            all(k["power"] == 200 and not k["ready"] for k in s["planning"]["kits"])
            for s in states.values()
        ),
        "kits do not open at 200 Power and unready on every peer",
    )
    start = states[names[-1]]["planning"]["remaining"]

    def counting(values: dict[str, JsonObject]) -> bool:
        remote = values[names[-1]]["planning"]["remaining"]
        host = values["host"]["planning"]["remaining"]
        return bool(remote < start - 2 and abs(host - remote) < 1.5)

    run.await_states(
        names, counting, "the remote client's countdown runs down with the host's"
    )


def place_kit(run: NetworkRun, names: list[str], slot: int, through: str) -> None:
    """Place a commander's kit: `through` is the peer that sends it as its own commander (an RPC), or the host fixture."""
    if through == "host":
        run.request("host", "planningPlace", owner=slot, piece="barracks")
        run.request("host", "planningPlace", owner=slot, piece="rig")
    else:
        run.request(through, "planningClientPlace", piece="barracks")
        run.request(through, "planningClientPlace", piece="rig")
    states = await_all(
        run,
        names,
        lambda s: finished(kit(s, slot)["barracks"]) and finished(kit(s, slot)["rig"]),
        f"commander {slot}'s finished Barracks and Drill Rig appear on every peer",
    )
    host_kit = kit(states["host"], slot)
    for name, state in states.items():
        client_kit = kit(state, slot)
        require(
            same_place(client_kit["barracks"], host_kit["barracks"])
            and same_place(client_kit["rig"], host_kit["rig"]),
            f"{name}: kit layout differs from the host's",
        )
        require(client_kit["power"] == 200, f"{name}: placing the kit cost Power")


def neighbour_region(state: JsonObject) -> int:
    """A region next to the friendly home region: reachable from the Barracks for a first order."""
    home = next(r for r in state["regions"] if r["homeTeam"] == 0)
    return int(home["neighbours"][0])


def client_edits(run: NetworkRun, names: list[str], client: str, remote: int) -> None:
    """The remote commander picks a type and a first order through its RPC component, and Ready locks both."""
    run.request(client, "planningClientType", role=RANGED)
    run.request(
        client, "planningClientOrder", region=neighbour_region(run.observe("host"))
    )
    await_all(
        run,
        names,
        lambda s: (
            kit(s, remote)["role"] == RANGED and len(kit(s, remote)["orders"]) == 1
        ),
        "the remote client's unit type and first order, sent over the wire, reach every peer",
    )
    run.request(client, "planningClientReady", ready=True)
    await_all(
        run,
        names,
        lambda s: bool(kit(s, remote)["ready"]) and s["planning"]["active"],
        "the remote client's Ready, sent over the wire, is visible everywhere and planning goes on",
    )
    # Ready locks the kit: this pick is refused, and the server's reason must come back to the remote client over the wire.
    run.request(client, "planningClientType", role=FRONTLINE)
    await_all(
        run,
        [client],
        lambda s: s["planning"]["feedback"] == LOCKED_REASON,
        "the refusal's reason reaches the remote client",
    )
    # The un-Ready is handled later on the same channel, so once it is visible the refused pick has been decided.
    run.request(client, "planningClientReady", ready=False)
    states = await_all(
        run,
        names,
        lambda s: not kit(s, remote)["ready"],
        "the remote client un-readies over the wire",
    )
    require(
        all(kit(s, remote)["role"] == RANGED for s in states.values()),
        "a unit type sent while Ready was accepted",
    )


def planning_scenario(run: NetworkRun) -> None:
    require(run.clients >= 1, "planning proof requires a real remote client")
    names = ["host", *(f"c{i}" for i in range(1, run.clients + 1))]
    os.environ[KEEP_PLANNING] = "1"
    try:
        slots = open_session(run, names)
    finally:
        os.environ.pop(KEEP_PLANNING, None)
    require(
        len(set(slots.values())) == len(names), "peers do not own distinct commanders"
    )
    run.phase("host and remote client open in one frozen planning phase")
    frozen_and_counting(run, names)
    remote = slots[names[-1]]
    # The host's kit first, so the client's placements see it replicated.
    for name in names:
        place_kit(run, names, slots[name], name)
    client_edits(run, names, names[-1], remote)
    for name in names[:-1]:
        run.request("host", "planningReady", owner=slots[name], ready=True)
    await_all(
        run,
        names,
        lambda s: (
            all(kit(s, slots[n])["ready"] for n in names[:-1])
            and not kit(s, remote)["ready"]
            and s["planning"]["active"]
            and s["worldPaused"]
        ),
        "every Ready but the remote client's is visible everywhere and planning goes on",
    )
    before = run.observe("host")["worldTime"]
    run.request(names[-1], "planningClientReady", ready=True)
    states = run.await_states(
        names,
        lambda v: (
            all(
                not s["planning"]["active"] and not s["worldPaused"] for s in v.values()
            )
            and v["host"]["worldTime"] > before + 1
        ),
        "the remote client's Ready, the last one, ends planning once and unfreezes every peer",
    )
    require(
        all(s["planning"]["clockStart"] >= 0 for s in states.values()),
        "the battle clock start is not published after planning",
    )
    run.phase(
        "the remote client placed its kit, picked a type, queued an order and Readied through its RPC component; layouts, orders and Ready replicated; the last Ready unfroze both"
    )
