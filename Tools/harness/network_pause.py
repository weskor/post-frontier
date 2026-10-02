"""Remote active pause, shared countdown, host early resume and exhausted budget."""

from __future__ import annotations

from harness.network import NetworkRun, require
from harness.network_session import connect, converged
from harness.verify import JsonObject


def pause_scenario(run: NetworkRun) -> None:
    require(run.clients >= 1, "pause proof requires a real remote client")
    s = connect(run)
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
    run.request("host", "resume")
    resumed = converged(
        run,
        s.names,
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
    serial = run.observe(s.peer)["pauseFeedbackSerial"]
    run.request(s.peer, "pause")
    rejected = run.await_states(
        [s.peer],
        lambda values: values[s.peer]["pauseFeedbackSerial"] > serial,
        "second remote pause receives an authoritative verdict",
    )[s.peer]
    require(not rejected["pauseAccepted"], "second co-op pause was accepted")
    run.await_states(
        s.names,
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
        "remote P freezes host simulation; countdown replicates; host resumes early; second pause rejected"
    )
