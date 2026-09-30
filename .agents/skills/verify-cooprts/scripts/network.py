#!/usr/bin/env python3
"""Opt-in real-socket CoopRTS network verification; never reuses single-window sessions."""
import argparse
import collections
import datetime
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time

from verify import ROOT, BINARY, DEFAULT_MAP, editor_stamp, package_stamp, identity, map_package, map_started

ENGINE = Path(os.environ.get("UE_ROOT", str(Path.home() / ".local/opt/unreal-engine/5.8.3")))
EDITOR = ENGINE / "Engine/Binaries/Linux/UnrealEditor"


def require(condition, explanation):
    if not condition:
        raise AssertionError(explanation)


class NetworkRun:
    def __init__(self, directory, mode, clients, emulation, rendered=False, max_fps=None, offscreen=None, *,
                 map_path=DEFAULT_MAP):
        self.map_path = map_package(map_path)
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
        (self.run / "run.json").write_text(json.dumps({"map": self.map_path, "mode": mode, "clients": clients, "emulation": emulation,
                                                     "rendered": rendered, "max_fps": max_fps,
                                                     "offscreen": offscreen, "artifact": self.artifact}, indent=2))
        self.events = (self.run / "events.jsonl").open("a", buffering=1)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]

    def event(self, kind, **fields):
        self.events.write(json.dumps({"time": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                      "kind": kind, "map": self.map_path, **fields}) + "\n")
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
                "buildings": [{"index": b["index"], "kind": b["kind"], "owner": b["owner"],
                               "construction": round(b["constructionProgress"], 2),
                               "joined": b["joined"], "travelling": b["travelling"], "state": b["productionState"]}
                              for b in state["buildings"]] if state and "buildings" in state else None,
                "reinforcing": [{"owner": u["owner"], "army": a["army"], "slot": u["slot"],
                                 "at": [round(v) for v in u["position"][:2]]}
                                for a in state["armies"] for u in a["units"]
                                if u["reinforcing"] and u["health"] > 0] if state and "armies" in state else None,
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
                       f"{self.map_path}?listen" if host else f"127.0.0.1:{self.port}",
                       "-game", "-nosound", "-unattended"]
        else:
            command = [str(BINARY), f"{self.map_path}?listen" if host else f"127.0.0.1:{self.port}",
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
        if host:
            self.until(lambda: self.selected_map_ready(name), f"{name} selected map {self.map_path}", 5, names=[name])
        if not rejection:
            self.until(lambda: self.observe(name, ready=False) if self.reply_ready(name) else None,
                       f"{name} first probe response", 120, names=[name])

    def selected_map_ready(self, name):
        path = self.peers[name]["folder"] / "game.log"
        if not path.exists():
            return False
        log = path.read_text(errors="replace")
        if map_started(log, self.map_path):
            return True
        require(not any("Bringing World " in line and " up for play" in line for line in log.splitlines()),
                f"{name} started a different map instead of {self.map_path}; see {path}")
        return False

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


def latched(run, names, predicate, description):
    """Transient evidence (a recruit mid-travel) is latched per peer when first observed;
    peers never have to expose the same transient state in one simultaneous sample."""
    seen = set()

    def check(states):
        seen.update(name for name, state in states.items() if predicate(state))
        return seen >= set(names)
    return run.await_states(names, check, description)


def near(anchor, dx, dy):
    """Map points are offsets from replicated HQ/sector positions, never literal coordinates."""
    return {"x": anchor[0] + dx, "y": anchor[1] + dy}


# Building definition indices (DA_MatchContent order) and EUnitRole recipes used by the probe.
BARRACKS, OUTPOST, WORKSHOP = 0, 1, 2
FRONTLINE, RANGED, SIEGE = 0, 1, 2
Session = collections.namedtuple("Session", "names identities peer owner hq arena")


def connect(run):
    """Fresh host plus clients with isolated enemy, paused income and distinct commanders."""
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
    host = states["host"]
    return Session(names, identities, peer, identities[peer], host["friendlyHQPosition"], host["arenaHalfExtent"])


def fund(run, s, amount, description):
    run.request("host", "fund", owner=s.owner, amount=amount)
    return converged(run, s.names, lambda st: wallet(st, s.owner)["wallet"] == amount, description)


def place_barracks(run, s, count, description):
    candidate = run.request("host", "placement", kind=BARRACKS)["placementCandidate"]
    run.request(s.peer, "build", kind=BARRACKS, x=candidate[0], y=candidate[1])
    return candidate, converged(run, s.names, lambda st: len(owned_buildings(st, s.owner, BARRACKS)) == count, description)


def build_barracks(run, s):
    """Paid remote placement, rejected duplicate/outside/foreign commands, game-time completion."""
    fund(run, s, 1000, "explicit encounter budget replicated")
    candidate, states = place_barracks(run, s, 1, "remote-owned barracks construction replicates")
    index = owned_buildings(states["host"], s.owner, BARRACKS)[0]["index"]
    balance = wallet(states["host"], s.owner)["wallet"]
    require(balance == 780, "barracks placement did not charge exactly 220")
    run.request(s.peer, "build", kind=BARRACKS, x=candidate[0], y=candidate[1])
    run.request(s.peer, "build", kind=BARRACKS, x=2 * s.arena[0], y=0)  # beyond the replicated arena
    if s.peer != "host":
        run.request("host", "production", building=index, recipe=RANGED, enabled=True)
        run.request("host", "cancel", building=index)
    states = converged(run, s.names, lambda st: building(st, index)["constructionProgress"] == 1,
                       "normal game-time building construction")
    require(all(wallet(st, s.owner)["wallet"] == balance and len(owned_buildings(st, s.owner, BARRACKS)) == 1
                and not building(st, index)["enabled"] for st in states.values()),
            "invalid placement or foreign role/cancel command mutated construction or debited a wallet")
    return index


def configure_siege(run, s, index):
    """First Start locks siege for exactly the 180 fee and leaves the force paused and empty."""
    run.request(s.peer, "front", building=index, frontOrder=0, **near(s.hq, 1700, 2300))
    # Exactly the configuration price leaves no funds for a recruit; observe the
    # accepted Start independently of automatic production and replication timing.
    fund(run, s, 180, "siege configuration budget")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(run, s.names, lambda st: building(st, index)["configured"]
                       and building(st, index)["recipe"] == SIEGE and wallet(st, s.owner)["wallet"] == 0,
                       "first Start permanently configures siege for exactly 180")
    producer = building(states["host"], index)
    require(producer["capacity"] == 2 and producer["unitCost"] == 50 and abs(producer["unitTime"] - 20 / 3) < .001,
            "siege per-unit economy/capacity mismatch")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(run, s.names, lambda st: not building(st, index)["enabled"], "configuration pause")
    return producer["forceID"]


def reject_locked_commands(run, s, index, squad):
    before = run.observe("host")
    run.request(s.peer, "production", building=index, recipe=RANGED, enabled=True)
    run.request(s.peer, "production", building=index, recipe=255, enabled=True)
    if s.peer != "host":
        foreign_before = wallet(before, s.identities["host"])["wallet"]
        run.request("host", "production", building=index, recipe=FRONTLINE, enabled=True)
        run.request("host", "front", building=index, frontOrder=2, **near(s.hq, 1700, 600))
        require(wallet(run.observe("host"), s.identities["host"])["wallet"] == foreign_before,
                "foreign RPC debited issuing commander's wallet")
    # Reliable RPC order on the same owning controller supplies a behavioral
    # delivery barrier; do not assert localized feedback wording or sleep for RPCs.
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    converged(run, s.names, lambda st: building(st, index)["enabled"], "owner resume after rejected role RPCs")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(run, s.names, lambda st: not building(st, index)["enabled"], "owner pauses after rejection barrier")
    rejected = run.observe("host")
    require(not building(rejected, index)["enabled"] and building(rejected, index)["recipe"] == SIEGE
            and building(rejected, index)["front"] == building(before, index)["front"]
            and building(rejected, index)["forceID"] == squad
            and building(rejected, index)["productionSeconds"] == building(before, index)["productionSeconds"]
            and wallet(rejected, s.owner)["wallet"] == 0,
            "locked/invalid/foreign commands changed force configuration, progress, front or wallet")
    run.phase("owner RPCs, permanent siege configuration and atomic rejection of locked, invalid and foreign commands")


def recruit(run, s, index, expected, description):
    """Pay for exactly one siege unit and wait until it has physically joined on every peer."""
    fund(run, s, 50, f"one-unit budget for recruit {expected}")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(run, s.names, lambda st: building(st, index)["joined"] == expected
                       and building(st, index)["travelling"] == 0 and force_counts_match(st, s.owner, index)
                       and wallet(st, s.owner)["wallet"] == 0, description)
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(run, s.names, lambda st: not building(st, index)["enabled"], f"recruit {expected} pause")
    return states


def produce_and_replace(run, s, index, squad):
    """Per-unit debit, physical travel/arrival, independent second force and causal casualty replacement."""
    fund(run, s, 50, "one-unit budget")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(run, s.names, lambda st: building(st, index)["joined"] + building(st, index)["travelling"] == 1
                       and force_counts_match(st, s.owner, index) and wallet(st, s.owner)["wallet"] == 0,
                       "one physical siege recruit, not batch production, replicated for exactly 50")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(run, s.names, lambda st: not building(st, index)["enabled"], "single recruit pause")
    first = alive_units(force(states["host"], s.owner, index))[0]
    require(first["reinforcing"] and first["role"] == SIEGE and first["owner"] == s.owner
            and distance2(first["position"], building(states["host"], index)["position"]) < 1200 ** 2
            and distance2(first["position"], building(states["host"], index)["front"]) > 500 ** 2,
            "recruit did not physically leave its producer toward the distant front")
    first_position = first["position"]
    latched(run, s.names, lambda st: distance2(alive_units(force(st, s.owner, index))[0]["position"], first_position) > 200 ** 2,
            "recruit travels on host and remote, not just a count increase")
    converged(run, s.names, lambda st: building(st, index)["joined"] == 1 and building(st, index)["travelling"] == 0
              and force_counts_match(st, s.owner, index), "physical arrival joins the force on all peers")
    run.request("host", "fund", owner=s.owner, amount=50)
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    converged(run, s.names, lambda st: building(st, index)["joined"] == 2 and building(st, index)["travelling"] == 0
              and building(st, index)["productionState"] == "ForceComplete"
              and wallet(st, s.owner)["wallet"] == 0 and force_counts_match(st, s.owner, index),
              "siege force naturally fills two alive slots without repeated configuration charge")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    converged(run, s.names, lambda st: not building(st, index)["enabled"]
              and building(st, index)["productionState"] == "Paused", "full siege force explicitly paused")

    # Same commander, second producer: no shared slots and no global front scope.
    fund(run, s, 1000, "second producer budget")
    place_barracks(run, s, 2, "second owned producer replicates")
    states = converged(run, s.names, lambda st: len(owned_buildings(st, s.owner, BARRACKS)) == 2
                       and all(b["constructionProgress"] == 1 for b in owned_buildings(st, s.owner, BARRACKS)),
                       "second owned producer completes normally")
    second = next(b["index"] for b in owned_buildings(states["host"], s.owner, BARRACKS) if b["index"] != index)
    run.request(s.peer, "front", building=second, frontOrder=1, **near(s.hq, 1700, -1100))
    run.request("host", "fund", owner=s.owner, amount=120)
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=True)
    states = converged(run, s.names, lambda st: building(st, second)["joined"] == 4
                       and building(st, second)["travelling"] == 0 and force_counts_match(st, s.owner, second)
                       and wallet(st, s.owner)["wallet"] == 0, "independent ranged force fills four paid slots")
    require(building(states["host"], second)["forceID"] != squad, "two producers reused the same force identity")
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=False)
    converged(run, s.names, lambda st: not building(st, second)["enabled"], "second producer paused")
    second_front = building(states["host"], second)["front"]
    run.request(s.peer, "front", building=index, frontOrder=1, **near(s.hq, 1700, 3100))
    converged(run, s.names, lambda st: building(st, index)["frontOrder"] == 1
              and building(st, second)["front"] == second_front and building(st, second)["frontOrder"] == 1,
              "building-only front scope replicates without moving other force")
    victim = alive_units(force(run.observe("host"), s.owner, index))[0]
    run.request("host", "kill", owner=s.owner, army=squad, slot=victim["slot"])
    converged(run, s.names, lambda st: building(st, index)["joined"] + building(st, index)["travelling"] == 1
              and force_counts_match(st, s.owner, index), "real hostile damage opens one replicated vacancy")
    run.request("host", "fund", owner=s.owner, amount=50)
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    states = converged(run, s.names, lambda st: building(st, index)["travelling"] == 1
                       and building(st, index)["joined"] == 1 and force_counts_match(st, s.owner, index)
                       and wallet(st, s.owner)["wallet"] == 0, "one causal paid casualty replacement travels on all peers")
    replacement = next(u for u in alive_units(force(states["host"], s.owner, index)) if u["reinforcing"])
    origin = replacement["position"]
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    run.request(s.peer, "front", building=index, frontOrder=1, **near(s.hq, 1700, 1500))
    latched(run, s.names, lambda st: any(u["reinforcing"] and distance2(u["position"], origin) > 200 ** 2
                                         for u in alive_units(force(st, s.owner, index))),
            "replacement retargets while force front moves")
    states = converged(run, s.names, lambda st: building(st, index)["joined"] == 2
                       and building(st, index)["travelling"] == 0 and force_counts_match(st, s.owner, index),
                       "replacement physically arrives and joins moving force")
    require(all(building(st, second)["joined"] == 4 and building(st, second)["front"] == second_front
                and wallet(st, s.owner)["wallet"] == 0 for st in states.values()),
            "replacement stole another force's capacity/front or charged more than one unit")
    run.phase("per-unit debit, independent fronts and causal replacement travel/arrival")


def research(run, s):
    """Paid workshop and one owner-scoped doctrine purchase; a repeat request is issued but never charged."""
    candidate = run.request("host", "placement", kind=WORKSHOP)["placementCandidate"]
    run.request("host", "fund", owner=s.owner, amount=200)
    run.request(s.peer, "build", kind=WORKSHOP, x=candidate[0], y=candidate[1])
    states = converged(run, s.names, lambda st: any(b["kind"] == WORKSHOP and b["constructionProgress"] == 1
                                                    for b in owned_buildings(st, s.owner)), "workshop construction")
    workshop = owned_buildings(states["host"], s.owner, WORKSHOP)[0]["index"]
    # Budget for this research is an explicit setup action, not claimed income.
    run.request("host", "fund", owner=s.owner, amount=200)
    run.request(s.peer, "research", building=workshop, choice=1)
    converged(run, s.names, lambda st: wallet(st, s.owner)["doctrine"] == 1 and wallet(st, s.owner)["wallet"] == 50,
              "paid owner-scoped workshop research")
    run.request(s.peer, "research", building=workshop, choice=2)


def finish(run, s, squad, description):
    # Shorten only the remaining outcome fixture; this is RPC/state proof, never native Q proof.
    run.request("host", "finish", owner=s.owner, army=squad, win=True)
    return converged(run, s.names, lambda st: st["result"] == 1 and st["enemyHQ"] == 0, description)


def expand_and_research(run, s, index, squad):
    """Natural capture, paid persistent outpost, research and automatic-front HQ damage to victory."""
    site_position = next(site["position"] for site in run.observe("host")["sites"] if site["index"] == 0)
    run.request(s.peer, "front", building=index, frontOrder=1, x=site_position[0], y=site_position[1])
    states = converged(run, s.names, lambda st: next(site for site in st["sites"] if site["index"] == 0)["owner"] == 0,
                       "produced force naturally reaches and secures a sector")
    require(all(st["resourceSites"] == 0 for st in states.values()), "bare capture incorrectly provides income")
    run.request("host", "fund", owner=s.owner, amount=160)
    run.request(s.peer, "build", kind=OUTPOST, **near(site_position, 400, 0))
    converged(run, s.names, lambda st: any(b["kind"] == OUTPOST and b["constructionProgress"] == 1
                                           for b in owned_buildings(st, s.owner)), "paid outpost establishes secured sector")
    run.request(s.peer, "front", building=index, frontOrder=1, **near(s.hq, 1700, 2300))
    converged(run, s.names, lambda st: st["resourceSites"] == 1
              and next(site for site in st["sites"] if site["index"] == 0)["established"]
              and not next(site for site in st["sites"] if site["index"] == 0)["friendlyPresent"],
              "outpost holds territory after real force departure")
    run.phase("natural capture and paid persistent outpost across peers")
    research(run, s)
    hq_position = run.observe("host")["enemyHQPosition"]
    run.request(s.peer, "front", building=index, frontOrder=0, x=hq_position[0], y=hq_position[1])
    converged(run, s.names, lambda st: st["enemyHQ"] < 900, "automatic force front causes actual HQ weapon damage")
    states = finish(run, s, squad, "weapon-caused victory replicates")
    require(all(wallet(st, s.owner)["doctrine"] == 1 and wallet(st, s.owner)["wallet"] == 50 for st in states.values()),
            "repeat research changed choice or charged twice")
    run.phase("research, automatic-front weapon damage and victory")
    return states


def restart_and_converge(run, s, old):
    """Connected restart: every peer converges on the fresh, fully reset world with preserved sockets."""
    run.request(s.peer, "restart")
    run.isolate_fresh_host(old["host"]["generation"])

    def reset(name, state):
        return (state["generation"] > old[name]["generation"] and state["localIndex"] == s.identities[name]
                and len(state["players"]) == len(s.names)
                and state["result"] == 0 and state["enemyHQ"] == 900 and state["friendlyHQ"] == 900
                and not any(a["team"] == 0 for a in state["armies"])
                and not any(b["team"] == 0 for b in state["buildings"])
                and all(p["doctrine"] == 0 and p["wallet"] >= 600 for p in state["players"])
                and len(state["sites"]) == 3
                and all(site["owner"] == -1 and site["progress"] == 0 for site in state["sites"]))
    states = run.await_states(s.names, lambda values: all(reset(name, state) for name, state in values.items()),
                              "same connected commanders converge on the reset construction world", allow_travel=True)
    for name, state in states.items():
        require(state["gameStateId"] != old[name]["gameStateId"] and state["netDriverId"] == old[name]["netDriverId"],
                f"{name}: fresh world or preserved connection missing")
    run.phase("fresh empty bases, three neutral sectors, research reset and preserved socket identities")


def ownership_scenario(run):
    s = connect(run)
    index = build_barracks(run, s)
    reject_locked_commands(run, s, index, configure_siege(run, s, index))


def production_scenario(run):
    s = connect(run)
    index = build_barracks(run, s)
    produce_and_replace(run, s, index, configure_siege(run, s, index))


def economy_scenario(run):
    s = connect(run)
    index = build_barracks(run, s)
    squad = configure_siege(run, s, index)
    recruit(run, s, index, 1, "first siege recruit joins on all peers")
    recruit(run, s, index, 2, "second siege recruit joins on all peers")
    expand_and_research(run, s, index, squad)


def restart_scenario(run):
    s = connect(run)
    index = build_barracks(run, s)
    squad = configure_siege(run, s, index)
    recruit(run, s, index, 1, "one siege recruit joins on all peers")
    research(run, s)
    restart_and_converge(run, s, finish(run, s, squad, "fixture victory replicates before restart"))


def construction_scenario(run):
    """Acceptance chain: every slice in one connected session and one continuous world."""
    s = connect(run)
    index = build_barracks(run, s)
    squad = configure_siege(run, s, index)
    reject_locked_commands(run, s, index, squad)
    produce_and_replace(run, s, index, squad)
    restart_and_converge(run, s, expand_and_research(run, s, index, squad))


SCENARIOS = {
    "ownership": (ownership_scenario, "paid placement, rejected duplicate/foreign commands, siege fee, locked-type rejections"),
    "production": (production_scenario, "per-unit debit, physical travel/arrival, second force, casualty replacement"),
    "economy": (economy_scenario, "natural capture, paid outpost income rights, research, HQ damage and victory"),
    "restart": (restart_scenario, "connected restart converging on a fully reset world with preserved sockets"),
    "construction": (construction_scenario, "acceptance chain: ownership, production, economy and restart in one world"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog="Slices (each starts its own host and clients; one stall never blocks another):\n"
                                     + "\n".join(f"  {name}: {summary}" for name, (_, summary) in SCENARIOS.items())
                                     + "\nLimit: state is observed through the in-process probe on loopback sockets; "
                                     "no rendering, OS input or real network path is proved.")
    parser.add_argument("--run", required=True, type=Path, help="fresh Saved/Verification/<id> directory")
    parser.add_argument("--mode", choices=("editor", "packaged"), required=True)
    parser.add_argument("--map", type=map_package, default=DEFAULT_MAP, help="world package path (default: %(default)s)")
    parser.add_argument("--clients", type=int, choices=(0, 1, 4), required=True)
    parser.add_argument("--scenario", choices=list(SCENARIOS), default="construction",
                        help="independent slice, or the full construction acceptance chain (default)")
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
                     args.rendered, args.max_fps, map_path=args.map)
    scenario, _ = SCENARIOS[args.scenario]
    try:
        scenario(run)
        run.event("PASS", peers=list(run.peers), mode=run.mode, scenario=args.scenario, emulation=run.emulation)
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
