"""Owned peer lifecycle, launch options and retained network evidence."""

from __future__ import annotations

from collections.abc import Sequence
import datetime
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import time
from typing import NotRequired, TypedDict

from harness.verify import (
    BINARY,
    DEFAULT_MAP,
    ROOT,
    JsonObject,
    identity,
    module_stamp,
    package_stamp,
)
from harness.waits import Deadline, WaitTimeout
from x.scopes import map_package


def require(condition: object, explanation: str) -> None:
    if not condition:
        raise AssertionError(explanation)


class Peer(TypedDict):
    process: subprocess.Popen[bytes]
    folder: Path
    command: list[str]
    identity: dict[str, str] | None
    rejection: bool
    log_offset: NotRequired[int]
    log_fragment: NotRequired[str]


class NetworkPeers:
    def __init__(
        self,
        directory: Path,
        mode: str,
        clients: int,
        emulation: bool,
        rendered: bool = False,
        max_fps: int | None = None,
        offscreen: tuple[int, int] | None = None,
        *,
        map_path: str = DEFAULT_MAP,
    ) -> None:
        self.map_path = map_package(map_path)
        self.run = directory
        self.mode = mode
        self.clients = clients
        self.emulation = emulation
        self.rendered = rendered
        self.max_fps = max_fps
        # (width, height): real Vulkan rendering into an offscreen viewport; no window is created.
        self.offscreen = offscreen
        self.peers: dict[str, Peer] = {}
        self.sequences: dict[str, int] = {}
        self.latest_states: dict[str, JsonObject] = {}
        self.latest_errors: dict[str, str] = {}
        self.connection_health: dict[str, str] = {}
        self.pending: dict[str, str] = {}
        self.stopped: set[str] = set()
        self.artifact = module_stamp() if mode == "editor" else package_stamp()
        self.run.mkdir(parents=True, exist_ok=False)
        (self.run / "run.json").write_text(
            json.dumps(
                {
                    "map": self.map_path,
                    "mode": mode,
                    "clients": clients,
                    "emulation": emulation,
                    "rendered": rendered,
                    "max_fps": max_fps,
                    "offscreen": offscreen,
                    "artifact": self.artifact,
                },
                indent=2,
            )
        )
        self.events = (self.run / "events.jsonl").open("a", buffering=1)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]

    def event(self, kind: str, **fields: object) -> None:
        self.events.write(
            json.dumps(
                {
                    "time": datetime.datetime.now(datetime.UTC).isoformat(),
                    "kind": kind,
                    "map": self.map_path,
                    **fields,
                }
            )
            + "\n"
        )

    def phase(self, label: str) -> None:
        self.event("phase", label=label)
        print(f"Verified: {label}", flush=True)

    def progress(self, description: str, elapsed: float, names: Sequence[str]) -> None:
        peers = {}
        for name in names:
            state = self.latest_states.get(name)
            peers[name] = {
                "ready": state.get("ready") if state else None,
                "generation": state.get("generation") if state else None,
                "netMode": state.get("netMode") if state else None,
                "localIndex": state.get("localIndex") if state else None,
                "players": len(state["players"])
                if state and "players" in state
                else None,
                "armies": len(state["armies"]) if state and "armies" in state else None,
                "result": state.get("result") if state else None,
                "buildings": [
                    {
                        "index": b["index"],
                        "kind": b["kind"],
                        "owner": b["owner"],
                        "construction": round(b["constructionProgress"], 2),
                        "joined": b["joined"],
                        "travelling": b["travelling"],
                        "state": b["productionState"],
                    }
                    for b in state["buildings"]
                ]
                if state and "buildings" in state
                else None,
                "reinforcing": [
                    {
                        "owner": u["owner"],
                        "army": a["army"],
                        "slot": u["slot"],
                        "at": [round(v) for v in u["position"][:2]],
                    }
                    for a in state["armies"]
                    for u in a["units"]
                    if u["reinforcing"] and u["health"] > 0
                ]
                if state and "armies" in state
                else None,
                "health": self.connection_health.get(name, "starting"),
                "error": self.latest_errors.get(name, ""),
                "pending": self.pending.get(name),
            }
        self.event(
            "waiting", detail=description, elapsed=round(elapsed, 1), peers=peers
        )
        print(
            f"Pending {description} ({elapsed:.1f}s): {json.dumps(peers, separators=(',', ':'))}",
            flush=True,
        )

    def check_artifact(self) -> None:
        require(
            (module_stamp() if self.mode == "editor" else package_stamp())
            == self.artifact,
            "binary/content changed while network session was running",
        )

    def check_network_failures(self) -> None:
        for name, entry in self.peers.items():
            if entry["rejection"]:
                continue
            path = entry["folder"] / "game.log"
            try:
                with path.open(errors="replace") as log:
                    log.seek(entry.get("log_offset", 0))
                    lines = entry.get("log_fragment", "") + log.read()
                    entry["log_offset"] = log.tell()
            except FileNotFoundError:
                continue
            *complete, entry["log_fragment"] = lines.split("\n")
            for line in complete:
                if "LogNet: Browse:" in line and "?Restart" in line:
                    self.connection_health[name] = "browsing restart URL"
                terminal_travel = (
                    "TravelFailure:" in line or "BroadcastTravelFailure" in line
                )
                if terminal_travel or (
                    name != "host"
                    and (
                        "NetworkFailure:" in line
                        or ("LogNet: Browse:" in line and "?closed" in line)
                    )
                ):
                    self.connection_health[name] = f"terminal: {line}"
                    self.event("terminal", peer=name, line=line, log=str(path))
                    raise AssertionError(
                        f"{name} lost its server/travel connection: {line}; see {path}"
                    )

    def establish_identity(self, name: str) -> bool | None:
        entry = self.peers[name]
        info = identity(entry["process"].pid)
        if info and info["exe"] == str(Path(entry["command"][0]).resolve()):
            entry["identity"] = info
            (entry["folder"] / "process.json").write_text(
                json.dumps(
                    {
                        "pid": entry["process"].pid,
                        "identity": info,
                        "command": entry["command"],
                    },
                    indent=2,
                )
            )
            return True
        return None

    def live(self, name: str) -> bool:
        entry = self.peers[name]
        return (
            entry["identity"] is not None
            and identity(entry["process"].pid) == entry["identity"]
        )

    def signal_owned(self, name: str, signum: int) -> None:
        entry = self.peers[name]
        require(
            self.live(name),
            f"{name} PID identity changed before {signal.Signals(signum).name}",
        )
        fd = os.pidfd_open(entry["process"].pid)
        try:
            require(
                self.live(name),
                f"{name} PID identity changed before {signal.Signals(signum).name}",
            )
            signal.pidfd_send_signal(fd, signum)
            self.event(
                "signal",
                peer=name,
                pid=entry["process"].pid,
                identity=entry["identity"],
                signal=signal.Signals(signum).name,
            )
        finally:
            os.close(fd)

    def pause(self, name: str) -> None:
        require(name not in self.stopped, f"{name} already paused")
        self.signal_owned(name, signal.SIGSTOP)
        self.stopped.add(name)

    def resume(self, name: str) -> None:
        if name in self.stopped:
            try:
                if self.live(name):
                    self.signal_owned(name, signal.SIGCONT)
            finally:
                self.stopped.remove(name)

    def stop(self, name: str) -> None:
        self.resume(name)
        entry = self.peers[name]
        process = entry["process"]
        pid = process.pid
        if process.poll() is None:
            fd = os.pidfd_open(pid)
            try:
                # A failed identity wait still owns this freshly spawned Popen.
                require(
                    entry["identity"] is None or self.live(name),
                    f"{name} PID identity changed before stop",
                )
                signal.pidfd_send_signal(fd, signal.SIGTERM)
                deadline = Deadline(f"{name} exits after SIGTERM", "shutdown")
                try:
                    while process.poll() is None:
                        deadline.check({"pid": pid, "identity": identity(pid)})
                        time.sleep(min(0.1, deadline.remaining))
                except WaitTimeout as error:
                    self.event("cleanup-escalation", peer=name, error=str(error))
                    signal.pidfd_send_signal(fd, signal.SIGKILL)
                    deadline = Deadline(f"{name} exits after SIGKILL", "shutdown")
                    while process.poll() is None:
                        deadline.check({"pid": pid, "identity": identity(pid)})
                        time.sleep(min(0.1, deadline.remaining))
            finally:
                os.close(fd)
        self.event("stop", peer=name, pid=pid, exit_code=process.returncode)

    def command(self, name: str, host: bool, folder: Path) -> list[str]:
        if self.mode == "editor":
            EDITOR = Path(os.environ["UE_ROOT"]) / "Engine/Binaries/Linux/UnrealEditor"
            command = [
                str(EDITOR),
                str(ROOT / "CoopRTS.uproject"),
                f"{self.map_path}?listen" if host else f"127.0.0.1:{self.port}",
                "-game",
                "-nosound",
                "-unattended",
            ]
        else:
            command = [
                str(BINARY),
                f"{self.map_path}?listen" if host else f"127.0.0.1:{self.port}",
                "-nosound",
                "-unattended",
            ]
        command.append("-nosteam")
        if self.offscreen:
            command += [
                "-RenderOffScreen",
                "-windowed",
                f"-ResX={self.offscreen[0]}",
                f"-ResY={self.offscreen[1]}",
            ]
        elif self.mode == "editor" or not self.rendered:
            command.append("-nullrhi")
        command += [
            "-log",
            "-stdout",
            "-FullStdOutLogOutput",
            f"-abslog={folder / 'game.log'}",
            f"-CoopRTSNetVerifyDir={folder}",
            f"-CoopRTSNetVerifyPeer={name}",
        ]
        if host:
            command.append("-CoopRTSNetVerifyAuthority")
            command.append(f"-port={self.port}")
        if self.emulation:
            command += ["-PktLag=120", "-PktLoss=8"]
        if self.max_fps:
            command.append(f"-ExecCmds=t.MaxFPS {self.max_fps}")
        return command

    def launch(self, name: str, *, host: bool = False, rejection: bool = False) -> None:
        require(name not in self.peers, f"peer name already used: {name}")
        self.check_artifact()
        folder = self.run / name
        folder.mkdir()
        command = self.command(name, host, folder)
        with (folder / "stdout.log").open("w") as output:
            process = subprocess.Popen(
                command,
                cwd=ROOT if self.mode == "editor" else BINARY.parents[3],
                stdout=output,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        self.peers[name] = {
            "process": process,
            "folder": folder,
            "command": command,
            "identity": None,
            "rejection": rejection,
        }
        self.connection_health[name] = "starting"
        self.sequences[name] = 0
        self.event("launch", peer=name, pid=process.pid, command=command)
