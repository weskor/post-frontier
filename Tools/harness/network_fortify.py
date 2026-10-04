"""Fortify over loopback: a remote client casts and every peer reads the same replicated state."""

from __future__ import annotations

from typing import cast

from harness.network import NetworkRun, require
from harness.network_session import Session, connect
from harness.verify import JsonObject

DATA_COST = 40
DURATION = 60.0
COOLDOWN = 90.0
FUNDED = 100


def fortify(state: JsonObject) -> JsonObject:
    value = state["fortify"]
    require(isinstance(value, dict), "peer snapshot has no Fortify state")
    return cast(JsonObject, value)


def commander(state: JsonObject, index: int) -> JsonObject:
    rows = [row for row in fortify(state)["commanders"] if row["index"] == index]
    require(len(rows) == 1, f"commander {index} is not replicated on this peer")
    return cast(JsonObject, rows[0])


def protected(state: JsonObject, region: int) -> JsonObject:
    rows = [row for row in fortify(state)["regions"] if row["index"] == region]
    require(len(rows) == 1, f"region {region} is not replicated on this peer")
    return cast(JsonObject, rows[0])


def own_main(state: JsonObject) -> int:
    mains = [r["index"] for r in state["regions"] if r["homeTeam"] == 0]
    require(len(mains) == 1, "the commanders' main region is ambiguous")
    return int(mains[0])


def remaining(state: JsonObject, region: int) -> float:
    return float(protected(state, region)["expiresAt"]) - float(
        fortify(state)["serverNow"]
    )


def fund(run: NetworkRun, s: Session, owner: int) -> None:
    run.request("host", "fortifyFund", owner=owner, data=FUNDED)
    run.await_states(
        s.names,
        lambda values: all(
            commander(v, owner)["data"] == FUNDED for v in values.values()
        ),
        f"commander {owner}'s Data replicates to every peer",
    )


def cast_replicates(run: NetworkRun, s: Session, region: int, caster: int) -> None:
    states = run.await_states(
        s.names,
        lambda values: all(
            protected(v, region)["team"] == 0
            and protected(v, region)["caster"] == caster
            for v in values.values()
        ),
        f"commander {caster}'s Fortify is visible on every peer",
    )
    expiry = {n: float(protected(v, region)["expiresAt"]) for n, v in states.items()}
    require(
        max(expiry.values()) - min(expiry.values()) < 0.01,
        f"peers disagree on the Fortify expiry: {expiry}",
    )
    for name, state in states.items():
        left = remaining(state, region)
        require(
            0 < left <= DURATION + 0.5,
            f"{name}: {left:.1f} s left is not within a {DURATION:.0f} s Fortify",
        )
        row = commander(state, caster)
        require(
            row["data"] == FUNDED - DATA_COST,
            f"{name}: the cast did not spend exactly {DATA_COST} Data once ({row['data']})",
        )
        ready = float(row["readyAt"]) - float(fortify(state)["serverNow"])
        require(
            0 < ready <= COOLDOWN + 0.5,
            f"{name}: cooldown {ready:.1f} s is not within {COOLDOWN:.0f} s",
        )


def fortify_scenario(run: NetworkRun) -> None:
    require(run.clients >= 1, "Fortify proof requires a real remote client")
    s = connect(run)
    host = s.identities["host"]
    first = run.observe("host")
    region = own_main(first)
    require(protected(first, region)["team"] == -1, "the main starts Fortified")
    fund(run, s, s.owner)
    fund(run, s, host)

    # The remote client casts through its controller RPC on the team's main.
    run.request(s.peer, "fortifyCast", region=region)
    cast_replicates(run, s, region, s.owner)
    run.phase(
        "remote client's cast: 40 Data once, 60 s expiry, 90 s cooldown on every peer"
    )

    # A second cast is on cooldown: refused with the chip's reason and free.
    run.request(s.peer, "fortifyCast", region=region)
    run.await_states(
        [s.peer],
        lambda values: values[s.peer]["orderFeedback"].startswith("Cooldown"),
        "the client is told its cooldown",
    )
    after = run.observe(s.peer)
    require(
        commander(after, s.owner)["data"] == FUNDED - DATA_COST,
        "a refused cast spent Data",
    )

    # The teammate (the host's commander) recasts later: expiry moves, nothing stacks.
    before_expiry = float(protected(run.observe("host"), region)["expiresAt"])
    run.await_states(
        ["host"],
        lambda values: (
            float(fortify(values["host"])["serverNow"]) > before_expiry - DURATION + 2.0
        ),
        "two server seconds pass before the teammate's recast",
    )
    run.request("host", "fortifyCast", region=region)
    cast_replicates(run, s, region, host)
    states = run.await_states(
        s.names,
        lambda values: all(
            float(protected(v, region)["expiresAt"]) > before_expiry + 1.0
            for v in values.values()
        ),
        "the teammate's recast refreshes the expiry on every peer",
    )
    require(
        all(protected(v, region)["team"] == 0 for v in states.values()),
        "a recast changed the protected team",
    )
    peer_events = fortify(run.observe(s.peer))["events"]
    require(
        [e["id"] for e in peer_events] == ["fortify_cast"]
        and peer_events[0]["commander"] == host
        and peer_events[0]["region"] == region,
        "the remote client's feed did not receive exactly the teammate's cast row",
    )
    # The host saw the client's cast as a teammate's row, and never a row for its own cast.
    host_events = fortify(run.observe("host"))["events"]
    require(
        [(e["id"], e["commander"]) for e in host_events] == [("fortify_cast", s.owner)],
        "the host's feed is not exactly the client's cast row, without its own cast",
    )
    run.event("fortify", region=region, peers=states)
    run.phase(
        "client cast, rejected recast, teammate refresh and the teammate's feed row agree on every peer"
    )
