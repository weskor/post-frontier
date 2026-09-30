#!/usr/bin/env python3
"""Opt-in real-socket CoopRTS network verification; never reuses single-window sessions."""
import argparse
import datetime
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time

from verify import ROOT, BINARY, editor_stamp, package_stamp, identity

ENGINE = Path(os.environ.get("UE_ROOT", str(Path.home() / ".local/opt/unreal-engine/5.8.3")))
EDITOR = ENGINE / "Engine/Binaries/Linux/UnrealEditor"


def require(condition, explanation):
    if not condition:
        raise AssertionError(explanation)


class NetworkRun:
    def __init__(self, directory, mode, clients, emulation, rendered=False, max_fps=None, offscreen=None):
        self.run = directory
        self.mode = mode
        self.clients = clients
        self.emulation = emulation
        self.rendered = rendered
        self.max_fps = max_fps
        # (width, height): real Vulkan rendering into an offscreen viewport; no window is created.
        self.offscreen = offscreen
        self.peers = {}
        self.sequences = {}
        self.latest_states = {}
        self.latest_errors = {}
        self.connection_health = {}
        self.pending = {}
        self.stopped = set()
        self.artifact = editor_stamp() if mode == "editor" else package_stamp()
        self.run.mkdir(parents=True, exist_ok=False)
        (self.run / "run.json").write_text(json.dumps({"mode": mode, "clients": clients, "emulation": emulation,
                                                     "rendered": rendered, "max_fps": max_fps,
                                                     "offscreen": offscreen, "artifact": self.artifact}, indent=2))
        self.events = (self.run / "events.jsonl").open("a", buffering=1)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]

    def event(self, kind, **fields):
        self.events.write(json.dumps({"time": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                      "kind": kind, **fields}) + "\n")
    def phase(self, label):
        self.event("phase", label=label)
        print(f"Verified: {label}", flush=True)

    def progress(self, description, elapsed, names):
        peers = {}
        for name in names:
            state = self.latest_states.get(name)
            peers[name] = {
                "ready": state.get("ready") if state else None,
                "generation": state.get("generation") if state else None,
                "netMode": state.get("netMode") if state else None,
                "localIndex": state.get("localIndex") if state else None,
                "players": len(state["players"]) if state and "players" in state else None,
                "armies": len(state["armies"]) if state and "armies" in state else None,
                "result": state.get("result") if state else None,
                "health": self.connection_health.get(name, "starting"),
                "error": self.latest_errors.get(name, ""),
                "pending": self.pending.get(name),
            }
        self.event("waiting", detail=description, elapsed=round(elapsed, 1), peers=peers)
        print(f"Pending {description} ({elapsed:.1f}s): {json.dumps(peers, separators=(',', ':'))}", flush=True)


    def check_artifact(self):
        require((editor_stamp() if self.mode == "editor" else package_stamp()) == self.artifact,
                "binary/content changed while network session was running")

    def check_network_failures(self):
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
                terminal_travel = "TravelFailure:" in line or "BroadcastTravelFailure" in line
                if (terminal_travel or (name != "host" and
                        ("NetworkFailure:" in line or
                         ("LogNet: Browse:" in line and "?closed" in line)))):
                    self.connection_health[name] = f"terminal: {line}"
                    self.event("terminal", peer=name, line=line, log=str(path))
                    raise AssertionError(f"{name} lost its server/travel connection: {line}; see {path}")


    def start(self, name, *, host=False, rejection=False):
        require(name not in self.peers, f"peer name already used: {name}")
        self.check_artifact()
        folder = self.run / name
        folder.mkdir()
        if self.mode == "editor":
            command = [str(EDITOR), str(ROOT / "CoopRTS.uproject"),
                       "/Game/Maps/Boot?listen" if host else f"127.0.0.1:{self.port}",
                       "-game", "-nosound", "-unattended"]
        else:
            command = [str(BINARY), "/Game/Maps/Boot?listen" if host else f"127.0.0.1:{self.port}",
                       "-nosound", "-unattended"]
        if self.offscreen:
            command += ["-RenderOffScreen", "-windowed", f"-ResX={self.offscreen[0]}", f"-ResY={self.offscreen[1]}"]
        elif self.mode == "editor" or not self.rendered:
            command.append("-nullrhi")
        command += ["-log", "-stdout", "-FullStdOutLogOutput", f"-abslog={folder / 'game.log'}",
                    f"-CoopRTSNetVerifyDir={folder}", f"-CoopRTSNetVerifyPeer={name}"]
        if host:
            command.append("-CoopRTSNetVerifyAuthority")
            command.append(f"-port={self.port}")
        if self.emulation:
            command += ["-PktLag=120", "-PktLoss=8"]
        if self.max_fps:
            command.append(f"-ExecCmds=t.MaxFPS {self.max_fps}")
        with (folder / "stdout.log").open("w") as output:
            process = subprocess.Popen(command, cwd=ROOT if self.mode == "editor" else ROOT / "Builds/Linux",
                                       stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        self.peers[name] = {"process": process, "folder": folder, "command": command,
                            "identity": None, "rejection": rejection}
        self.connection_health[name] = "starting"
        self.sequences[name] = 0
        self.event("launch", peer=name, pid=process.pid, command=command)
        # A process is never adopted by name or by a reused PID.
        self.until(lambda: self.establish_identity(name), f"{name} executable identity", 30, names=[name])
        if not rejection:
            self.until(lambda: self.observe(name, ready=False) if self.reply_ready(name) else None,
                       f"{name} first probe response", 120, names=[name])

    def establish_identity(self, name):
        entry = self.peers[name]
        info = identity(entry["process"].pid)
        if info and info["exe"] == str(EDITOR if self.mode == "editor" else BINARY):
            entry["identity"] = info
            (entry["folder"] / "process.json").write_text(json.dumps({"pid": entry["process"].pid,
                "identity": info, "command": entry["command"]}, indent=2))
            return True
        return None

    def live(self, name):
        entry = self.peers[name]
        return entry["identity"] is not None and identity(entry["process"].pid) == entry["identity"]

    def signal_owned(self, name, signum):
        entry = self.peers[name]
        require(self.live(name), f"{name} PID identity changed before {signal.Signals(signum).name}")
        fd = os.pidfd_open(entry["process"].pid)
        try:
            require(self.live(name), f"{name} PID identity changed before {signal.Signals(signum).name}")
            signal.pidfd_send_signal(fd, signum)
            self.event("signal", peer=name, pid=entry["process"].pid,
                       identity=entry["identity"], signal=signal.Signals(signum).name)
        finally:
            os.close(fd)

    def pause(self, name):
        require(name not in self.stopped, f"{name} already paused")
        self.signal_owned(name, signal.SIGSTOP)
        self.stopped.add(name)

    def resume(self, name):
        if name in self.stopped:
            try:
                if self.live(name):
                    self.signal_owned(name, signal.SIGCONT)
            finally:
                self.stopped.remove(name)

    def until(self, predicate, explanation, report_interval, *, names=None):
        names = list(names if names is not None else self.peers)
        started = time.monotonic()
        next_report = started
        # Older call sites pass larger reporting intervals, not deadlines. Keep
        # every pending predicate visible even when a peer stops responding.
        report_interval = min(report_interval, 5)
        while True:
            self.check_artifact()
            self.check_network_failures()
            for name in names:
                entry = self.peers[name]
                if entry["process"].poll() is not None:
                    raise AssertionError(f"{name} exited ({entry['process'].returncode}) waiting for {explanation}; see logs")
            value = predicate()
            if value:
                self.check_network_failures()
                return value
            now = time.monotonic()
            if now >= next_report:
                self.progress(explanation, now - started, names)
                next_report = now + report_interval
            time.sleep(.15)

    def reply_ready(self, name):
        return self.live(name) and (self.peers[name]["folder"] / "game.log").exists() and "Network verification enabled peer=" in (
            self.peers[name]["folder"] / "game.log").read_text(errors="replace")

    def request(self, name, action="observe", *, allow_unavailable=False, expect_rejection=False, **fields):
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
            response = self.until(lambda: self.get_response(name, command_id),
                                  f"{name} {action} response", 5, names=[name])
        finally:
            self.pending.pop(name, None)
        with (folder / "responses.jsonl").open("a") as log:
            log.write(json.dumps(response) + "\n")
        self.event("response", peer=name, id=command_id, error=response["error"])
        self.latest_errors[name] = response["error"]
        require(response["peer"] == name, f"{name} {action} returned wrong peer identity")
        if expect_rejection:
            require(bool(response["error"]), f"{name} {action}: unavailable action unexpectedly accepted")
        else:
            require(not response["error"] or (allow_unavailable and response["error"] == "game world unavailable"),
                    f"{name} {action}: {response['error']}")
        return response["state"]

    def get_response(self, name, expected):
        path = self.peers[name]["folder"] / "reply.json"
        try:
            response = json.loads(path.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return None
        return response if response["id"] == expected else None

    def observe(self, name, ready=True, *, allow_unavailable=False):
        state = self.request(name, allow_unavailable=allow_unavailable)
        self.latest_states[name] = state
        if state["ready"]:
            self.connection_health[name] = "connected" if name != "host" else "listening"
        elif allow_unavailable:
            self.connection_health[name] = "travel pending; game world unavailable"
        if ready:
            require(state["ready"], f"{name} has no live replicated game world")
        return state

    def all_states(self, active, *, ready=True, allow_unavailable=False):
        return {name: self.observe(name, ready=ready, allow_unavailable=allow_unavailable)
                for name in active}

    def await_states(self, active, predicate, description, report_interval=5, *,
                     allow_loading=False, allow_travel=False):
        def check():
            states = self.all_states(active, ready=False, allow_unavailable=allow_travel)
            unready = [name for name, state in states.items() if not state["ready"]]
            require(not unready or allow_loading or allow_travel,
                    f"live game world disappeared outside connection/travel: {unready}")
            return states if not unready and predicate(states) else None
        return self.until(check, description, report_interval, names=active)

    def isolate_fresh_host(self, previous_generation):
        states = self.await_states(["host"], lambda states:
            states["host"]["generation"] > previous_generation and len(states["host"]["sites"]) == 3,
            "host exposes the initialized fresh world before autonomous play", allow_travel=True)
        state = states["host"]
        require(state["result"] == 0 and state["friendlyHQ"] == 900 and state["enemyHQ"] == 900
                and not any(a["team"] == 0 for a in state["armies"])
                and not any(b["team"] == 0 for b in state["buildings"])
                and all(site["owner"] == -1 and site["progress"] == 0 for site in state["sites"]),
                "new authoritative world did not expose fresh HQs, empty player bases and neutral sectors")
        # Preserve the observed reset while delayed clients replicate. This
        # stops autonomous orders; it does not clear sites, health or progress.
        self.request("host", "isolate")
        self.phase("fresh authoritative reset observed before stopping autonomous play for peer convergence")

    def stop(self, name):
        self.resume(name)
        entry = self.peers[name]
        pid = entry["process"].pid
        if self.live(name):
            fd = os.pidfd_open(pid)
            try:
                require(self.live(name), f"{name} PID identity changed before stop")
                signal.pidfd_send_signal(fd, signal.SIGTERM)
                while self.live(name):
                    time.sleep(.1)
            finally:
                os.close(fd)
        entry["process"].wait()
        self.event("stop", peer=name, pid=pid, exit_code=entry["process"].returncode)

    def rejected(self, name, text):
        entry = self.peers[name]
        log = entry["folder"] / "game.log"
        def observed_rejection():
            if log.exists() and text in log.read_text(errors="replace"):
                return True
            require(entry["process"].poll() is None,
                    f"{name} exited before expected server rejection {text!r}; see logs")
            return False
        self.until(observed_rejection, f"{name} expected server rejection {text!r}", 45, names=["host"])
        # Rejection must not leave a controller with an assigned commander/world.
        if self.live(name) and self.reply_ready(name):
            snapshot = self.request(name, allow_unavailable=True)
            require(not snapshot["ready"] or snapshot["localIndex"] == -1,
                    f"rejected peer {name} received commander identity")
        self.event("rejected", peer=name, reason=text)
        self.stop(name)


def army(state, commander, index):
    matches = [a for a in state["armies"] if a["owner"] == commander and a["army"] == index]
    require(len(matches) == 1, f"expected one army {commander}/{index}, got {len(matches)}")
    return matches[0]


def wallet(state, commander):
    matches = [p for p in state["players"] if p["index"] == commander]
    require(len(matches) == 1, f"expected one wallet for {commander}, got {len(matches)}")
    return matches[0]


def owned_buildings(state, owner, kind=None):
    return [b for b in state["buildings"] if b["owner"] == owner and (kind is None or b["kind"] == kind)]


def building(state, index):
    matches = [b for b in state["buildings"] if b["index"] == index]
    require(len(matches) == 1, f"expected living building {index}, got {len(matches)}")
    return matches[0]


def force(state, owner, index):
    producer = building(state, index)
    return army(state, owner, producer["forceID"])


def alive_units(group):
    return [u for u in group["units"] if u["health"] > 0]


def distance2(a, b):
    return sum((a[i] - b[i]) ** 2 for i in (0, 1))


def force_counts_match(state, owner, index):
    producer = building(state, index)
    matches = [a for a in state["armies"] if a["owner"] == owner and a["army"] == producer["forceID"]]
    if len(matches) != 1:
        return False  # Actor references and their fields can replicate in separate updates.
    group = matches[0]
    members = alive_units(group)
    travelling = sum(u["reinforcing"] for u in members)
    slots = [u["slot"] for u in members]
    return (producer["configured"] and group["producer"] == index
            and producer["travelling"] == travelling and producer["joined"] == len(members) - travelling
            and len(members) <= producer["capacity"] and len(set(slots)) == len(slots)
            and all(0 <= u["slot"] < producer["capacity"] and u["owner"] == owner
                    and u["role"] == producer["recipe"] and u["producer"] == index for u in members))


def converged(run, names, predicate, description, report_interval=5):
    return run.await_states(names, lambda states: all(predicate(s) for s in states.values()),
                            description, report_interval)


def construction_scenario(run):
    names = ["host", *(f"c{i}" for i in range(1, run.clients + 1))]
    run.start("host", host=True)
    run.await_states(["host"], lambda s: s["host"]["localIndex"] >= 0 and len(s["host"]["sites"]) == 3,
                     "initialized construction host", allow_loading=True)
    run.request("host", "isolate")
    run.request("host", "income", paused=True)
    for name in names[1:]:
        run.start(name)
    states = run.await_states(names, lambda values: all(
        s["localIndex"] >= 0 and len(s["players"]) == len(names)
        and all(p["index"] >= 0 for p in s["players"]) for s in values.values()),
        "all independent commander identities", allow_loading=True)
    identities = {name: state["localIndex"] for name, state in states.items()}
    require(len(set(identities.values())) == len(names), "peers do not own distinct commanders")
    for name, state in states.items():
        require(state["netMode"] == (2 if name == "host" else 3), f"{name}: not a real listen/client world")
        require(not any(a["team"] == 0 for a in state["armies"]), f"{name}: obsolete fixed starting armies")
        if run.emulation:
            require(state.get("pktLag") == 120 and state.get("pktLoss") == 8, f"{name}: emulation not active")
    run.phase("fresh real sockets and empty player armies with independent identities")
    peer = names[-1]
    owner = identities[peer]
    run.request("host", "fund", owner=owner, amount=1000)
    converged(run, names, lambda s: wallet(s, owner)["wallet"] == 1000, "explicit encounter budget replicated")

    candidate = run.request("host", "placement", kind=0)["placementCandidate"]
    run.request(peer, "build", kind=0, x=candidate[0], y=candidate[1])
    states = converged(run, names, lambda s: len(owned_buildings(s, owner, 0)) == 1,
                       "remote-owned barracks construction replicates")
    producer = owned_buildings(states["host"], owner, 0)[0]
    index = producer["index"]
    balance = wallet(states["host"], owner)["wallet"]
    require(balance == 780, "barracks placement did not charge exactly 220")
    run.request(peer, "build", kind=0, x=candidate[0], y=candidate[1])
    run.request(peer, "build", kind=0, x=100000, y=0)
    if peer != "host":
        run.request("host", "production", building=index, recipe=1, enabled=True)
        run.request("host", "cancel", building=index)
    states = converged(run, names, lambda s: owned_buildings(s, owner, 0)[0]["construction"] == 1,
                       "normal game-time building construction")
    require(all(wallet(s, owner)["wallet"] == balance and len(owned_buildings(s, owner, 0)) == 1
                and not owned_buildings(s, owner, 0)[0]["enabled"] for s in states.values()),
            "invalid placement or foreign role/cancel command mutated construction or debited a wallet")
    run.request(peer, "front", building=index, frontOrder=0, x=-1800, y=1700)
    # Exactly the configuration price leaves no funds for a recruit; observe the
    # accepted Start independently of automatic production and replication timing.
    run.request("host", "fund", owner=owner, amount=180)
    converged(run, names, lambda s: wallet(s, owner)["wallet"] == 180, "siege configuration budget")
    run.request(peer, "production", building=index, recipe=2, enabled=True)
    states = converged(run, names, lambda s: building(s, index)["configured"]
                       and building(s, index)["recipe"] == 2 and wallet(s, owner)["wallet"] == 0,
                       "first Start permanently configures siege for exactly 180")
    squad_index = building(states["host"], index)["forceID"]
    require(building(states["host"], index)["capacity"] == 2
            and building(states["host"], index)["unitCost"] == 50
            and abs(building(states["host"], index)["unitTime"] - 20 / 3) < .001,
            "siege per-unit economy/capacity mismatch")
    run.request(peer, "production", building=index, recipe=2, enabled=False)
    converged(run, names, lambda s: not building(s, index)["enabled"], "configuration pause")
    before = run.observe("host")
    run.request(peer, "production", building=index, recipe=1, enabled=True)
    run.request(peer, "production", building=index, recipe=255, enabled=True)
    if peer != "host":
        foreign_before = wallet(before, identities["host"])["wallet"]
        run.request("host", "production", building=index, recipe=0, enabled=True)
        run.request("host", "front", building=index, frontOrder=2, x=-1800, y=0)
        require(wallet(run.observe("host"), identities["host"])["wallet"] == foreign_before,
                "foreign RPC debited issuing commander's wallet")
    # Reliable RPC order on the same owning controller supplies a behavioral
    # delivery barrier; do not assert localized feedback wording or sleep for RPCs.
    run.request(peer, "production", building=index, recipe=2, enabled=True)
    converged(run, names, lambda s: building(s, index)["enabled"], "owner resume after rejected role RPCs")
    run.request(peer, "production", building=index, recipe=2, enabled=False)
    converged(run, names, lambda s: not building(s, index)["enabled"], "owner pauses after rejection barrier")
    rejected = run.observe("host")
    require(not building(rejected, index)["enabled"] and building(rejected, index)["recipe"] == 2
            and building(rejected, index)["front"] == building(before, index)["front"]
            and building(rejected, index)["forceID"] == squad_index
            and building(rejected, index)["productionSeconds"] == building(before, index)["productionSeconds"]
            and wallet(rejected, owner)["wallet"] == 0,
            "locked/invalid/foreign commands changed force configuration, progress, front or wallet")

    run.request("host", "fund", owner=owner, amount=50)
    converged(run, names, lambda s: wallet(s, owner)["wallet"] == 50, "one-unit budget")
    run.request(peer, "production", building=index, recipe=2, enabled=True)
    states = converged(run, names, lambda s: building(s, index)["joined"] + building(s, index)["travelling"] == 1
                       and force_counts_match(s, owner, index) and wallet(s, owner)["wallet"] == 0,
                       "one physical siege recruit, not batch production, replicated for exactly 50")
    run.request(peer, "production", building=index, recipe=2, enabled=False)
    converged(run, names, lambda s: not building(s, index)["enabled"], "single recruit pause")
    first = alive_units(force(states["host"], owner, index))[0]
    require(first["reinforcing"] and first["role"] == 2 and first["owner"] == owner
            and distance2(first["position"], building(states["host"], index)["position"]) < 1200 ** 2
            and distance2(first["position"], building(states["host"], index)["front"]) > 500 ** 2,
            "recruit did not physically leave its producer toward the distant front")
    first_position = first["position"]
    converged(run, names, lambda s: distance2(alive_units(force(s, owner, index))[0]["position"], first_position) > 200 ** 2,
              "recruit travels on host and remote, not just a count increase")
    converged(run, names, lambda s: building(s, index)["joined"] == 1 and building(s, index)["travelling"] == 0
              and force_counts_match(s, owner, index), "physical arrival joins the force on all peers")
    run.request("host", "fund", owner=owner, amount=50)
    run.request(peer, "production", building=index, recipe=2, enabled=True)
    converged(run, names, lambda s: building(s, index)["joined"] == 2 and building(s, index)["travelling"] == 0
              and building(s, index)["productionStatus"] == "FORCE COMPLETE"
              and wallet(s, owner)["wallet"] == 0 and force_counts_match(s, owner, index),
              "siege force naturally fills two alive slots without repeated configuration charge")
    run.request(peer, "production", building=index, recipe=2, enabled=False)
    converged(run, names, lambda s: not building(s, index)["enabled"]
              and building(s, index)["productionStatus"] == "PAUSED", "full siege force explicitly paused")

    # Same commander, second producer: no shared slots and no global front scope.
    run.request("host", "fund", owner=owner, amount=1000)
    candidate = run.request("host", "placement", kind=0)["placementCandidate"]
    run.request(peer, "build", kind=0, x=candidate[0], y=candidate[1])
    states = converged(run, names, lambda s: len(owned_buildings(s, owner, 0)) == 2
                       and all(b["construction"] == 1 for b in owned_buildings(s, owner, 0)),
                       "second owned producer completes normally")
    second = next(b["index"] for b in owned_buildings(states["host"], owner, 0) if b["index"] != index)
    run.request(peer, "front", building=second, frontOrder=1, x=-1800, y=-1700)
    run.request("host", "fund", owner=owner, amount=120)
    run.request(peer, "production", building=second, recipe=1, enabled=True)
    states = converged(run, names, lambda s: building(s, second)["joined"] == 4
                       and building(s, second)["travelling"] == 0 and force_counts_match(s, owner, second)
                       and wallet(s, owner)["wallet"] == 0, "independent ranged force fills four paid slots")
    require(building(states["host"], second)["forceID"] != squad_index,
            "two producers reused the same force identity")
    run.request(peer, "production", building=second, recipe=1, enabled=False)
    converged(run, names, lambda s: not building(s, second)["enabled"], "second producer paused")
    second_front = building(states["host"], second)["front"]
    run.request(peer, "front", building=index, frontOrder=1, x=-1800, y=2500)
    converged(run, names, lambda s: building(s, index)["frontOrder"] == 1
              and building(s, second)["front"] == second_front and building(s, second)["frontOrder"] == 1,
              "building-only front scope replicates without moving other force")
    victim = alive_units(force(run.observe("host"), owner, index))[0]
    run.request("host", "kill", owner=owner, army=squad_index, slot=victim["slot"])
    converged(run, names, lambda s: building(s, index)["joined"] + building(s, index)["travelling"] == 1
              and force_counts_match(s, owner, index), "real hostile damage opens one replicated vacancy")
    run.request("host", "fund", owner=owner, amount=50)
    run.request(peer, "production", building=index, recipe=2, enabled=True)
    states = converged(run, names, lambda s: building(s, index)["travelling"] == 1
                       and building(s, index)["joined"] == 1 and force_counts_match(s, owner, index)
                       and wallet(s, owner)["wallet"] == 0, "one causal paid casualty replacement travels on all peers")
    recruit = next(u for u in alive_units(force(states["host"], owner, index)) if u["reinforcing"])
    origin = recruit["position"]
    run.request(peer, "production", building=index, recipe=2, enabled=False)
    run.request(peer, "front", building=index, frontOrder=1, x=-1800, y=900)
    converged(run, names, lambda s: any(u["reinforcing"] and distance2(u["position"], origin) > 200 ** 2
              for u in alive_units(force(s, owner, index))), "replacement retargets while force front moves")
    states = converged(run, names, lambda s: building(s, index)["joined"] == 2
                       and building(s, index)["travelling"] == 0 and force_counts_match(s, owner, index),
                       "replacement physically arrives and joins moving force")
    require(all(building(s, second)["joined"] == 4 and building(s, second)["front"] == second_front
                and wallet(s, owner)["wallet"] == 0 for s in states.values()),
            "replacement stole another force's capacity/front or charged more than one unit")
    run.phase("owner RPCs, permanent siege config, per-unit debit, independent fronts and causal replacement travel/arrival")

    # Natural force movement/capture, rather than teleporting an expected occupant.
    site_position = next(site["position"] for site in run.observe("host")["sites"] if site["index"] == 0)
    run.request(peer, "front", building=index, frontOrder=1, x=site_position[0], y=site_position[1])
    states = converged(run, names, lambda s: next(site for site in s["sites"] if site["index"] == 0)["owner"] == 0,
                       "produced force naturally reaches and secures a sector")
    require(all(s["resourceSites"] == 0 for s in states.values()), "bare capture incorrectly provides income")
    run.request("host", "fund", owner=owner, amount=160)
    run.request(peer, "build", kind=1, x=-450, y=-1800)
    converged(run, names, lambda s: any(b["kind"] == 1 and b["construction"] == 1 for b in owned_buildings(s, owner)),
              "paid outpost establishes secured sector")
    run.request(peer, "front", building=index, frontOrder=1, x=-1800, y=1700)
    converged(run, names, lambda s: s["resourceSites"] == 1
              and next(site for site in s["sites"] if site["index"] == 0)["established"]
              and not next(site for site in s["sites"] if site["index"] == 0)["friendlyPresent"],
              "outpost holds territory after real force departure")
    run.phase("natural capture and paid persistent outpost across peers")

    candidate = run.request("host", "placement", kind=2)["placementCandidate"]
    run.request("host", "fund", owner=owner, amount=200)
    run.request(peer, "build", kind=2, x=candidate[0], y=candidate[1])
    states = converged(run, names, lambda s: any(b["kind"] == 2 and b["construction"] == 1 for b in owned_buildings(s, owner)),
                       "workshop construction")
    workshop = owned_buildings(states["host"], owner, 2)[0]["index"]
    # Budget for this research is an explicit setup action, not claimed income.
    run.request("host", "fund", owner=owner, amount=200)
    run.request(peer, "research", building=workshop, choice=1)
    converged(run, names, lambda s: wallet(s, owner)["doctrine"] == 1 and wallet(s, owner)["wallet"] == 50,
              "paid owner-scoped workshop research")
    run.request(peer, "research", building=workshop, choice=2)
    hq_position = run.observe("host")["enemyHQPosition"]
    run.request(peer, "front", building=index, frontOrder=0, x=hq_position[0], y=hq_position[1])
    converged(run, names, lambda s: s["enemyHQ"] < 900, "automatic force front causes actual HQ weapon damage")
    # Shorten only the remaining outcome fixture; this is RPC/state proof, never native Q proof.
    run.request("host", "finish", owner=owner, army=squad_index, win=True)
    states = converged(run, names, lambda s: s["result"] == 1 and s["enemyHQ"] == 0, "weapon-caused victory replicates")
    require(all(wallet(s, owner)["doctrine"] == 1 and wallet(s, owner)["wallet"] == 50 for s in states.values()),
            "repeat research changed choice or charged twice")
    run.phase("research, automatic-front weapon damage and victory")
    old = states
    run.request(peer, "restart")
    run.isolate_fresh_host(old["host"]["generation"])
    states = run.await_states(names, lambda values: all(
        s["generation"] > old[name]["generation"] and s["localIndex"] == identities[name]
        and len(s["players"]) == len(names) for name, s in values.items()),
        "same connected commanders enter fresh construction world", allow_travel=True)
    for name, state in states.items():
        require(state["gameStateId"] != old[name]["gameStateId"]
                and state["netDriverId"] == old[name]["netDriverId"], f"{name}: fresh world or preserved connection missing")
        require(state["result"] == 0 and state["enemyHQ"] == 900 and state["friendlyHQ"] == 900
                and not any(a["team"] == 0 for a in state["armies"])
                and not any(b["team"] == 0 for b in state["buildings"])
                and all(p["doctrine"] == 0 and p["wallet"] >= 600 for p in state["players"]),
                f"{name}: stale army/building/research/economy after restart")
    run.phase("fresh empty bases, research reset and preserved socket identities")
    run.event("PASS", peers=names, mode=run.mode, scenario="construction", emulation=run.emulation)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, type=Path, help="fresh Saved/Verification/<id> directory")
    parser.add_argument("--mode", choices=("editor", "packaged"), required=True)
    parser.add_argument("--clients", type=int, choices=(0, 1, 4), required=True)
    parser.add_argument("--scenario", choices=("construction",), default="construction",
                        help="paid building/production/front/expansion/research and connected restart")
    parser.add_argument("--emulation", action="store_true", help="require observed native PktLag=120/PktLoss=8 on every peer")
    parser.add_argument("--rendered", action="store_true",
                        help="packaged Vulkan fallback if the real artifact rejects -nullrhi; no automatic visual proof")
    parser.add_argument("--max-fps", type=int, help="bound each verification peer's render/game frame rate")
    args = parser.parse_args()
    if args.rendered and args.mode != "packaged":
        parser.error("--rendered is only available for packaged runs")
    if args.max_fps is not None and args.max_fps < 1:
        parser.error("--max-fps must be positive")
    run = NetworkRun(args.run.resolve(), args.mode, args.clients, args.emulation,
                     args.rendered, args.max_fps)
    try:
        construction_scenario(run)
        print(f"PASS: {args.scenario} {args.mode} host + {args.clients} remote clients; evidence: {run.run}")
    except BaseException as error:
        run.event("FAIL", error=repr(error))
        print(f"FAIL: {error}; evidence: {run.run}", file=sys.stderr)
        raise
    finally:
        for name in reversed(list(run.peers)):
            if run.peers[name]["process"].poll() is None:
                run.stop(name)
        run.events.close()


if __name__ == "__main__":
    main()
