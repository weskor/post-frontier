"""Gifting over loopback: a remote client sends through its owning RPC component and every peer reads the same wallets and log."""

from __future__ import annotations

from typing import cast

from harness.network import NetworkRun, require
from harness.network_session import Session, connect
from harness.verify import JsonObject

CLIENT_POWER = 300
CLIENT_DATA = 80
HOST_POWER = 50
HOST_DATA = 0


def team(state: JsonObject) -> JsonObject:
    value = state["team"]
    require(isinstance(value, dict), "peer snapshot has no Team panel state")
    return cast(JsonObject, value)


def commander(state: JsonObject, index: int) -> JsonObject:
    rows = [row for row in team(state)["commanders"] if row["index"] == index]
    require(len(rows) == 1, f"commander {index} is not replicated on this peer")
    return cast(JsonObject, rows[0])


def log(state: JsonObject) -> list[JsonObject]:
    return cast(list[JsonObject], team(state)["log"])


def fund(run: NetworkRun, s: Session, host: int) -> None:
    run.request("host", "giftFund", owner=s.owner, power=CLIENT_POWER, data=CLIENT_DATA)
    run.request("host", "giftFund", owner=host, power=HOST_POWER, data=HOST_DATA)
    run.await_states(
        s.names,
        lambda values: all(
            (commander(v, s.owner)["power"], commander(v, s.owner)["data"])
            == (CLIENT_POWER, CLIENT_DATA)
            and (commander(v, host)["power"], commander(v, host)["data"])
            == (HOST_POWER, HOST_DATA)
            for v in values.values()
        ),
        "both wallets replicate to every peer",
    )


def gift_lands(
    run: NetworkRun,
    s: Session,
    host: int,
    resource: str,
    amount: int,
    balances: tuple[int, int, int, int],
    entries: int,
) -> None:
    """After the client's send: balances are (client power, client data, host power, host data)."""
    states = run.await_states(
        s.names,
        lambda values: all(
            (
                commander(v, s.owner)["power"],
                commander(v, s.owner)["data"],
                commander(v, host)["power"],
                commander(v, host)["data"],
            )
            == balances
            and len(log(v)) == entries
            for v in values.values()
        ),
        f"the client's {amount} {resource} reaches the host on every peer",
    )
    for name, state in states.items():
        newest = log(state)[-1]
        require(
            (
                newest["sender"],
                newest["recipient"],
                newest["resource"],
                newest["amount"],
            )
            == (s.owner, host, resource, amount),
            f"{name}: the newest log entry is not the client's {amount} {resource} gift: {newest}",
        )


def refused(run: NetworkRun, s: Session, host: int, amount: int, text: str) -> None:
    run.request(s.peer, "giftSend", to=host, resource="power", amount=amount)
    run.await_states(
        [s.peer],
        lambda values: team(values[s.peer])["refusal"] == text,
        f"the client is told '{text}'",
    )


def gifts_scenario(run: NetworkRun) -> None:
    require(run.clients >= 1, "Gift proof requires a real remote client")
    s = connect(run)
    host = s.identities["host"]
    fund(run, s, host)

    # The client sends through its controller RPC; Power and Data move atomically, once.
    run.request(s.peer, "giftSend", to=host, resource="power", amount=100)
    gift_lands(run, s, host, "power", 100, (200, CLIENT_DATA, 150, HOST_DATA), 1)
    run.request(s.peer, "giftSend", to=host, resource="data", amount=30)
    gift_lands(run, s, host, "data", 30, (200, 50, 150, 30), 2)
    run.phase("client gift: Power and Data moved once and logged on both peers")
    # Only the recipient has something unseen.
    sent = run.await_states(
        s.names,
        lambda values: team(values["host"])["unseen"],
        "the host's opener marks the client's gifts as unseen",
    )
    require(not team(sent[s.peer])["unseen"], "the sender has an unseen gift")

    # Refusals carry the panel's texts, change no wallet and add no log entry.
    refused(run, s, host, 500, "Not enough Power: you have 200")
    refused(run, s, host, 0, "Pick an amount above 0")
    after = run.await_states(
        s.names,
        lambda values: all(len(log(v)) == 2 for v in values.values()),
        "refused gifts leave the log alone",
    )
    require(
        all(
            commander(v, s.owner)["power"] == 200 and commander(v, host)["power"] == 150
            for v in after.values()
        ),
        "a refused gift moved Power",
    )

    # The host gifts back: each recipient's opener shows a gold dot until it opens the panel with Tab.
    run.request("host", "giftSend", to=s.owner, resource="power", amount=20)
    states = run.await_states(
        s.names,
        lambda values: (
            all(len(log(v)) == 3 for v in values.values())
            and team(values[s.peer])["unseen"]
        ),
        "the host's gift reaches the client and marks an unseen gift",
    )
    for name in s.names:
        run.request(name, "key", key="Tab", pressed=True)
        run.request(name, "key", key="Tab", pressed=False)
    run.await_states(
        s.names,
        lambda values: all(
            team(v)["open"] and not team(v)["unseen"] for v in values.values()
        ),
        "Tab opens each panel and clears its gold dot",
    )
    run.event("gifts", peers=states)
    run.phase(
        "client gift, refusals, host gift back and the Tab-cleared unseen mark agree on every peer"
    )
