"""Paid research, outcome fixtures and connected fresh-world reset assertions."""

from __future__ import annotations

from harness.network import WORKSHOP, NetworkRun, owned_buildings, require, wallet
from harness.network_session import Session, converged
from harness.verify import JsonObject


def research(run: NetworkRun, s: Session) -> None:
    """Paid workshop and one owner-scoped doctrine purchase; a repeat request is issued but never charged."""
    candidate = run.request("host", "placement", kind=WORKSHOP)["placementCandidate"]
    run.request("host", "fund", owner=s.owner, amount=200)
    run.request(s.peer, "build", kind=WORKSHOP, x=candidate[0], y=candidate[1])
    states = converged(
        run,
        s.names,
        lambda st: any(
            b["kind"] == WORKSHOP and b["constructionProgress"] == 1
            for b in owned_buildings(st, s.owner)
        ),
        "workshop construction",
    )
    workshop = owned_buildings(states["host"], s.owner, WORKSHOP)[0]["index"]
    # Budget for this research is an explicit setup action, not claimed income.
    run.request("host", "fund", owner=s.owner, amount=200)
    run.request(s.peer, "research", building=workshop, choice=1)
    converged(
        run,
        s.names,
        lambda st: (
            wallet(st, s.owner)["doctrine"] == 1 and wallet(st, s.owner)["wallet"] == 50
        ),
        "paid owner-scoped workshop research",
    )
    run.request(s.peer, "research", building=workshop, choice=2)


def finish(
    run: NetworkRun, s: Session, squad: int, description: str
) -> dict[str, JsonObject]:
    # Shorten only the remaining outcome fixture; this is RPC/state proof, never native Q proof.
    run.request("host", "finish", owner=s.owner, army=squad, win=True)
    return converged(
        run, s.names, lambda st: st["result"] == 1 and st["enemyHQ"] == 0, description
    )


def reset_matches(
    name: str, state: JsonObject, s: Session, old: dict[str, JsonObject]
) -> bool:
    return (
        state["generation"] > old[name]["generation"]
        and state["localIndex"] == s.identities[name]
        and len(state["players"]) == len(s.names)
        and state["result"] == 0
        and state["enemyHQ"] == 900
        and state["friendlyHQ"] == 900
        and not any(a["team"] == 0 for a in state["armies"])
        and not any(b["team"] == 0 for b in state["buildings"])
        and all(p["doctrine"] == 0 and p["wallet"] >= 600 for p in state["players"])
        and len(state["sites"]) == len(old["host"]["sites"])
        and len(state["deposits"]) == len(old["host"]["deposits"])
        and all(
            not d["occupied"] and d["remaining"] == (3000 if d["rich"] else 2400)
            for d in state["deposits"]
        )
        and all(
            site["owner"] == -1 and site["progress"] == 0 for site in state["sites"]
        )
    )


def restart_and_converge(
    run: NetworkRun, s: Session, old: dict[str, JsonObject]
) -> None:
    """Connected restart: every peer converges on the fresh, fully reset world with preserved sockets."""
    run.request(s.peer, "restart")
    run.isolate_fresh_host(old["host"]["generation"], len(old["host"]["sites"]))
    states = run.await_states(
        s.names,
        lambda values: all(
            reset_matches(name, state, s, old) for name, state in values.items()
        ),
        "same connected commanders converge on the reset construction world",
        allow_travel=True,
    )
    for name, state in states.items():
        require(
            state["gameStateId"] != old[name]["gameStateId"]
            and state["netDriverId"] == old[name]["netDriverId"],
            f"{name}: fresh world or preserved connection missing",
        )
    run.phase(
        "fresh empty bases, all map sectors neutral, research reset and preserved socket identities"
    )
