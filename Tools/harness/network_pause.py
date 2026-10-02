"""Remote active pause, shared countdown, host early resume and exhausted budget."""

from __future__ import annotations

from harness.network import NetworkRun, require
from harness.network_session import connect, converged
from harness.verify import JsonObject


def reject_engine_toggle(run: NetworkRun, peer: str, paused: bool) -> None:
    serial = run.observe(peer)["pauseFeedbackSerial"]
    run.request(peer, "enginePause")
    # Reliable command verdict supplies a barrier after the engine RPC.
    run.request(peer, "pause" if paused else "resume")
    state = run.await_states(
        [peer],
        lambda values: values[peer]["pauseFeedbackSerial"] > serial,
        "engine toggle rejection followed by a command-layer verdict",
    )[peer]
    require(not state["pauseAccepted"], "engine toggle changed the command budget")
    host = run.observe("host")
    require(
        host["worldPaused"] == paused and host["activePaused"] == paused,
        "engine toggle bypassed the authoritative pause state",
    )


def pause_scenario(run: NetworkRun) -> None:
    require(run.clients >= 1, "pause proof requires a real remote client")
    s = connect(run)
    for peer in s.names:
        reject_engine_toggle(run, peer, False)
    serial = run.observe(s.peer)["pauseFeedbackSerial"]
    # Exercise the P mapping through real Enhanced Input, not a direct local handler.
    run.request(s.peer, "key", key="P", pressed=True)
    run.request(s.peer, "key", key="P", pressed=False)
    accepted = run.await_states(
        [s.peer],
        lambda values: values[s.peer]["pauseFeedbackSerial"] > serial,
        "remote P receives an authoritative pause verdict",
    )[s.peer]
    require(accepted["pauseAccepted"], "remote pause RPC was not accepted")
    states = converged(
        run,
        s.names,
        lambda st: (
            st["worldPaused"]
            and st["activePaused"]
            and st["coopPauseSpent"]
            and 0 < st["pauseRemaining"] <= 60
        ),
        "remote P pauses every peer and shares the sixty-second countdown",
    )
    frozen = states["host"]["worldTime"]
    remaining = states["host"]["pauseRemaining"]

    def stopped(values: dict[str, JsonObject]) -> bool:
        host = values["host"]
        require(
            host["worldTime"] == frozen, "host simulation advanced during active pause"
        )
        require(
            all(st["activePaused"] and st["worldPaused"] for st in values.values()),
            "peer resumed before the deadline or early-resume command",
        )
        return all(st["pauseRemaining"] < remaining - 2 for st in values.values())

    countdown = run.await_states(
        s.names, stopped, "real countdown advances with frozen host simulation"
    )
    require(
        max(st["pauseRemaining"] for st in countdown.values())
        - min(st["pauseRemaining"] for st in countdown.values())
        < 1,
        "peers disagree on the shared countdown",
    )
    for peer in s.names:
        reject_engine_toggle(run, peer, True)
    # Drop the controller whose P request began the pause. Keep its evidence on
    # disk, but stop supervising the intentionally closed socket as a live peer.
    run.stop(s.peer)
    del run.peers[s.peer]
    names = [name for name in s.names if name != s.peer]
    disconnected = run.await_states(
        names,
        lambda values: len(values["host"]["players"]) == len(names),
        "pausing client's controller is removed by real socket disconnect",
    )
    require(
        disconnected["host"]["worldPaused"]
        and disconnected["host"]["activePaused"]
        and disconnected["host"]["worldTime"] == frozen,
        "pausing client disconnect cleared engine pause or desynchronized the budget",
    )
    remaining = disconnected["host"]["pauseRemaining"]
    run.await_states(
        names, stopped, "team pause keeps counting down after pauser disconnect"
    )
    run.request("host", "resume")
    resumed = converged(
        run,
        names,
        lambda st: (
            not st["worldPaused"]
            and not st["activePaused"]
            and st["coopPauseSpent"]
            and st["pauseRemaining"] == 0
        ),
        "another player ends the team's pause early on every peer",
    )
    require(
        resumed["host"]["worldTime"] >= frozen, "resume moved simulation time backwards"
    )
    serial = run.observe("host")["pauseFeedbackSerial"]
    run.request("host", "pause")
    rejected = run.await_states(
        ["host"],
        lambda values: values["host"]["pauseFeedbackSerial"] > serial,
        "second pause receives an authoritative verdict after early resume",
    )["host"]
    require(not rejected["pauseAccepted"], "second co-op pause was accepted")
    run.await_states(
        names,
        lambda values: (
            all(
                not st["worldPaused"]
                and not st["activePaused"]
                and st["coopPauseSpent"]
                for st in values.values()
            )
            and values["host"]["worldTime"] > frozen + 1
        ),
        "spent pause cannot refreeze the running host simulation",
    )
    run.phase(
        "remote P freezes host; engine bypass rejected; pauser disconnect preserves pause; host resumes; second pause rejected"
    )
