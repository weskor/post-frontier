"""File-backed probe protocol and supervised snapshot convergence."""

from __future__ import annotations

from collections.abc import Callable, Sequence
import json
import os
from pathlib import Path
import time
from typing import cast

from harness.network_peers import NetworkPeers, require
from harness.verify import JsonObject, map_started
from harness.waits import Deadline, WaitKind, WaitTimeout


class NetworkProbe(NetworkPeers):
    def selected_map_ready(self, name: str) -> bool:
        path = self.peers[name]["folder"] / "game.log"
        if not path.exists():
            return False
        log = path.read_text(errors="replace")
        if map_started(log, self.map_path):
            return True
        require(
            not any(
                "Bringing World " in line and " up for play" in line
                for line in log.splitlines()
            ),
            f"{name} started a different map instead of {self.map_path}; see {path}",
        )
        return False

    def until[T](
        self,
        predicate: Callable[[], T | None],
        explanation: str,
        report_interval: float,
        *,
        names: Sequence[str] | None = None,
        kind: WaitKind = "state",
        seconds: float | None = None,
        snapshot: Callable[[], object] | None = None,
    ) -> T:
        names = list(names if names is not None else self.peers)

        def observed() -> object:
            if snapshot is not None:
                return snapshot()
            return {
                name: {
                    "state": self.latest_states.get(name),
                    "pending": self.pending.get(name),
                    "error": self.latest_errors.get(name),
                    "health": self.connection_health.get(name, "starting"),
                    "identity": self.peers[name]["identity"],
                }
                for name in names
            }

        with Deadline(explanation, kind, seconds=seconds) as deadline:
            next_report = deadline.started
            report_interval = min(report_interval, 5)
            try:
                while True:
                    deadline.check(observed())
                    self.check_artifact()
                    self.check_network_failures()
                    for name in names:
                        entry = self.peers[name]
                        if entry["process"].poll() is not None:
                            raise AssertionError(
                                f"{name} exited ({entry['process'].returncode}) waiting for {explanation}; see logs"
                            )
                    value = predicate()
                    deadline.check(observed())
                    if value:
                        self.check_network_failures()
                        return value
                    now = time.monotonic()
                    if now >= next_report:
                        self.progress(explanation, deadline.elapsed, names)
                        next_report = now + report_interval
                    time.sleep(min(0.15, deadline.remaining))
            except WaitTimeout as error:
                self.event("timeout", detail=explanation, error=str(error))
                raise

    def reply_ready(self, name: str) -> bool:
        return (
            self.live(name)
            and (self.peers[name]["folder"] / "game.log").exists()
            and "Network verification enabled peer="
            in (self.peers[name]["folder"] / "game.log").read_text(errors="replace")
        )

    def request(
        self,
        name: str,
        action: str = "observe",
        *,
        allow_unavailable: bool = False,
        expect_rejection: bool = False,
        **fields: object,
    ) -> JsonObject:
        entry = self.peers[name]
        require(self.live(name), f"{name} process is not owned/live")
        self.sequences[name] += 1
        command_id = self.sequences[name]
        folder = entry["folder"]
        payload = {"id": command_id, "action": action, "owner": 0, "army": 0, **fields}
        temp = folder / "request.tmp"
        temp.write_text(json.dumps(payload))
        os.replace(temp, folder / "request.json")
        self.event("request", peer=name, command=payload)
        self.pending[name] = f"{action} response id={command_id}"
        try:
            response = self.until(
                lambda: self.get_response(name, command_id),
                f"{name} {action} response",
                5,
                names=[name],
                kind="response",
            )
        finally:
            self.pending.pop(name, None)
        with (folder / "responses.jsonl").open("a") as log:
            log.write(json.dumps(response) + "\n")
        self.event("response", peer=name, id=command_id, error=response["error"])
        self.latest_errors[name] = response["error"]
        require(
            response["peer"] == name, f"{name} {action} returned wrong peer identity"
        )
        if expect_rejection:
            require(
                bool(response["error"]),
                f"{name} {action}: unavailable action unexpectedly accepted",
            )
        else:
            require(
                not response["error"]
                or (
                    allow_unavailable and response["error"] == "game world unavailable"
                ),
                f"{name} {action}: {response['error']}",
            )
        return cast(JsonObject, response["state"])

    def get_response(self, name: str, expected: int) -> JsonObject | None:
        return read_response(self.peers[name]["folder"] / "reply.json", expected)

    def observe(
        self, name: str, ready: bool = True, *, allow_unavailable: bool = False
    ) -> JsonObject:
        state = self.request(name, allow_unavailable=allow_unavailable)
        self.latest_states[name] = state
        if state["ready"]:
            self.connection_health[name] = (
                "connected" if name != "host" else "listening"
            )
        elif allow_unavailable:
            self.connection_health[name] = "travel pending; game world unavailable"
        if ready:
            require(state["ready"], f"{name} has no live replicated game world")
        return state

    def all_states(
        self,
        active: Sequence[str],
        *,
        ready: bool = True,
        allow_unavailable: bool = False,
    ) -> dict[str, JsonObject]:
        return {
            name: self.observe(name, ready=ready, allow_unavailable=allow_unavailable)
            for name in active
        }

    def await_states(
        self,
        active: Sequence[str],
        predicate: Callable[[dict[str, JsonObject]], object],
        description: str,
        report_interval: float = 5,
        *,
        allow_loading: bool = False,
        allow_travel: bool = False,
    ) -> dict[str, JsonObject]:
        def check() -> dict[str, JsonObject] | None:
            states = self.all_states(
                active, ready=False, allow_unavailable=allow_travel
            )
            unready = [name for name, state in states.items() if not state["ready"]]
            require(
                not unready or allow_loading or allow_travel,
                f"live game world disappeared outside connection/travel: {unready}",
            )
            return states if not unready and predicate(states) else None

        return self.until(check, description, report_interval, names=active)


def read_response(path: Path, expected: int) -> JsonObject | None:
    try:
        response = cast(JsonObject, json.loads(path.read_text()))
    except (FileNotFoundError, json.JSONDecodeError):
        return None
    return response if response["id"] == expected else None
