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
        command.append("-nosteam")
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

    def isolate_fresh_host(self, previous_generation, site_count):
        states = self.await_states(["host"], lambda states:
            states["host"]["generation"] > previous_generation and len(states["host"]["sites"]) == site_count,
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


def region(state, index):
    matches = [r for r in state["regions"] if r["index"] == index]
    require(len(matches) == 1, f"expected region {index}, got {len(matches)}")
    return matches[0]


def reachable_regions(state, source):
    """Deterministic neighbour traversal; targets come from replicated region data."""
    graph = {r["index"]: r for r in state["regions"]}
    require(source in graph, f"region graph is missing source {source}")
    pending = collections.deque([source])
    seen = {source}
    result = []
    while pending:
        current = pending.popleft()
        result.append(graph[current])
        for neighbour in sorted(graph[current]["neighbours"]):
            if neighbour in graph and neighbour not in seen:
                seen.add(neighbour)
                pending.append(neighbour)
    return result


def select_goal_region(state, index, exclude=(), min_distance=1500):
    producer = building(state, index)
    source = next(r["index"] for r in state["regions"] if r["homeTeam"] == producer["team"])
    candidates = [r for r in reachable_regions(state, source)
                  if r["homeTeam"] == -1 and r["index"] not in exclude
                  and distance2(r["anchor"], producer["position"]) > min_distance ** 2]
    require(bool(candidates), "map has no reachable, non-main region sufficiently far from the producer")
    return candidates[0]


def goal_matches(state, index, goal, target):
    producer = building(state, index)
    return producer["forceGoal"] == goal and producer["goalRegionIndex"] == target


def hold_at(run, s, index, target, description):
    run.request(s.peer, "goal", building=index, goal=HOLD, region=target)
    return converged(run, s.names, lambda st: goal_matches(st, index, HOLD, target)
                     and building(st, index)["frontOrder"] == 1
                     and force_counts_match(st, s.owner, index)
                     and force(st, s.owner, index)["frontOrder"] == 1
                     and distance2(force(st, s.owner, index)["front"], region(st, target)["anchor"]) < 1
                     and distance2(building(st, index)["front"], region(st, target)["anchor"]) < 1,
                     description)


# Building definition indices (DA_MatchContent order) and EUnitRole recipes used by the probe.
BARRACKS, EXTRACTOR, WORKSHOP = 0, 1, 2
FRONTLINE, RANGED, SIEGE = 0, 1, 2
HOLD, EXPAND, ASSAULT, FALL_BACK = 0, 1, 2, 3
Session = collections.namedtuple("Session", "names identities peer owner arena")


def connect(run):
    """Fresh host plus clients with isolated enemy, paused income and distinct commanders."""
    names = ["host", *(f"c{i}" for i in range(1, run.clients + 1))]
    run.start("host", host=True)
    run.await_states(["host"], lambda s: s["host"]["localIndex"] >= 0 and bool(s["host"]["sites"])
                     and bool(s["host"]["regions"]) and bool(s["host"]["deposits"]),
                     "initialized construction host", allow_loading=True)
    run.request("host", "isolate")
    run.request("host", "income", paused=True)
    for name in names[1:]:
        run.start(name)
    states = run.await_states(names, lambda values: all(
        s["localIndex"] >= 0 and len(s["players"]) == len(names)
        and len(s["sites"]) == len(values["host"]["sites"])
        and len(s["regions"]) == len(values["host"]["regions"])
        and len(s["deposits"]) == len(values["host"]["deposits"])
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
    return Session(names, identities, peer, identities[peer], host["arenaHalfExtent"])


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
    states = converged(run, s.names, lambda st: not building(st, index)["enabled"], "configuration pause")
    target = select_goal_region(states["host"], index)["index"]
    hold_at(run, s, index, target, "owned Hold goal and region anchor replicate after configuration")
    return producer["forceID"]


def reject_locked_commands(run, s, index, squad):
    before = run.observe("host")
    run.request(s.peer, "production", building=index, recipe=RANGED, enabled=True)
    run.request(s.peer, "production", building=index, recipe=255, enabled=True)
    enemy_main = next(r["index"] for r in before["regions"] if r["homeTeam"] == 5)
    invalid_region = max(r["index"] for r in before["regions"]) + 1
    run.request(s.peer, "goal", building=index, goal=255, region=building(before, index)["goalRegionIndex"])
    for goal in (HOLD, EXPAND):
        run.request(s.peer, "goal", building=index, goal=goal, region=invalid_region)
        run.request(s.peer, "goal", building=index, goal=goal, region=enemy_main)
    for goal in (ASSAULT, FALL_BACK):
        run.request(s.peer, "goal", building=index, goal=goal, region=enemy_main)
    if s.peer != "host":
        foreign_before = wallet(before, s.identities["host"])["wallet"]
        run.request("host", "production", building=index, recipe=FRONTLINE, enabled=True)
        run.request("host", "goal", building=index, goal=FALL_BACK, region=-1)
        require(wallet(run.observe("host"), s.identities["host"])["wallet"] == foreign_before,
                "foreign RPC debited issuing commander's wallet")
    # Reliable RPC order on the same owning controller supplies a behavioral
    # delivery barrier; do not assert localized feedback wording or sleep for RPCs.
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=True)
    converged(run, s.names, lambda st: building(st, index)["enabled"], "owner resume after rejected role RPCs")
    run.request(s.peer, "production", building=index, recipe=SIEGE, enabled=False)
    states = converged(run, s.names, lambda st: not building(st, index)["enabled"],
                       "owner pauses after rejection barrier")
    require(all(not building(st, index)["enabled"] and building(st, index)["recipe"] == SIEGE
                and building(st, index)["front"] == building(before, index)["front"]
                and building(st, index)["frontOrder"] == building(before, index)["frontOrder"]
                and goal_matches(st, index, building(before, index)["forceGoal"],
                                 building(before, index)["goalRegionIndex"])
                and building(st, index)["forceID"] == squad
                and building(st, index)["productionSeconds"] == building(before, index)["productionSeconds"]
                and wallet(st, s.owner)["wallet"] == 0 for st in states.values()),
            "locked/invalid/foreign commands changed force configuration, progress, goal, waypoint or wallet")
    run.phase("owner RPCs, permanent siege configuration and atomic rejection of invalid/foreign goals and locked types")


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

    # Same commander, second producer: no shared slots and no global goal scope.
    fund(run, s, 1000, "second producer budget")
    place_barracks(run, s, 2, "second owned producer replicates")
    states = converged(run, s.names, lambda st: len(owned_buildings(st, s.owner, BARRACKS)) == 2
                       and all(b["constructionProgress"] == 1 for b in owned_buildings(st, s.owner, BARRACKS)),
                       "second owned producer completes normally")
    second = next(b["index"] for b in owned_buildings(states["host"], s.owner, BARRACKS) if b["index"] != index)
    run.request("host", "fund", owner=s.owner, amount=120)
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=True)
    states = converged(run, s.names, lambda st: building(st, second)["joined"] == 4
                       and building(st, second)["forceGoal"] == HOLD
                       and building(st, second)["travelling"] == 0 and force_counts_match(st, s.owner, second)
                       and wallet(st, s.owner)["wallet"] == 0, "independent ranged force fills four paid slots")
    require(building(states["host"], second)["forceID"] != squad, "two producers reused the same force identity")
    states = converged(run, s.names, lambda st: building(st, index)["forceNumber"] == 1
                       and building(st, second)["forceNumber"] == 2
                       and force(st, s.owner, index)["forceNumber"] == 1
                       and force(st, s.owner, second)["forceNumber"] == 2,
                       "producer numbers 1/2 and matching group numbers observed on host and remote")
    run.request(s.peer, "production", building=second, recipe=RANGED, enabled=False)
    converged(run, s.names, lambda st: not building(st, second)["enabled"], "second producer paused")
    second_front = building(states["host"], second)["front"]
    second_target = building(states["host"], second)["goalRegionIndex"]
    original_target = building(states["host"], index)["goalRegionIndex"]
    moved_target = select_goal_region(states["host"], index, exclude=(original_target,))["index"]
    hold_at(run, s, index, moved_target, "replacement Hold target region replicates")
    converged(run, s.names, lambda st: goal_matches(st, second, HOLD, second_target)
              and building(st, second)["front"] == second_front and building(st, second)["frontOrder"] == 1,
              "building-only goal scope replicates without moving other force")
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
    run.request(s.peer, "goal", building=index, goal=HOLD, region=original_target)
    # Arrival may precede a delayed peer's next sample; retain the same replacement
    # slot's movement evidence rather than requiring another transient reinforcing flag.
    latched(run, s.names, lambda st: goal_matches(st, index, HOLD, original_target)
            and distance2(force(st, s.owner, index)["front"], region(st, original_target)["anchor"]) < 1
            and any(u["slot"] == replacement["slot"] and distance2(u["position"], origin) > 200 ** 2
                    for u in alive_units(force(st, s.owner, index))),
            "same paid replacement moves after the force retargets to another region")
    states = converged(run, s.names, lambda st: building(st, index)["joined"] == 2
                       and building(st, index)["travelling"] == 0 and force_counts_match(st, s.owner, index),
                       "replacement physically arrives and joins moving force")
    require(all(building(st, second)["joined"] == 4 and building(st, second)["front"] == second_front
                and goal_matches(st, index, HOLD, original_target)
                and goal_matches(st, second, HOLD, second_target)
                and wallet(st, s.owner)["wallet"] == 0 for st in states.values()),
            "replacement stole another force's capacity/goal or charged more than one unit")
    run.phase("per-unit debit, independent region goals and causal replacement travel/arrival")


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
    """Natural polygon capture, private finite extractors, research and Assault-goal HQ damage."""
    state = run.observe("host")
    deposit_regions = {d["region"] for d in state["deposits"] if not d["occupied"]}
    excluded = tuple(r["index"] for r in state["regions"] if r["controller"] == 0 or r["index"] not in deposit_regions)
    target = select_goal_region(state, index, exclude=excluded)["index"]
    run.request(s.peer, "goal", building=index, goal=EXPAND, region=target)
    states = converged(run, s.names, lambda st: region(st, target)["controller"] == 0
                       and goal_matches(st, index, HOLD, target)
                       and building(st, index)["frontOrder"] == 1
                       and distance2(building(st, index)["front"], region(st, target)["anchor"]) < 1,
                       "produced force completes Expand by securing and holding the target region")
    require(all(all(p["income"] == 2 for p in st["players"]) for st in states.values()),
            "bare region capture incorrectly provides income")
    free = next(d for d in states["host"]["deposits"] if d["region"] == target and not d["occupied"])
    deposit_index = free["index"]
    rate = 6 if free["rich"] else 4
    total = 3000 if free["rich"] else 2400
    require(free["rate"] == rate and free["remaining"] == total, "deposit kind has incorrect rate or finite reserve")

    def deposit(st):
        return next(d for d in st["deposits"] if d["index"] == deposit_index)

    run.request(s.peer, "goal", building=index, goal=FALL_BACK, region=-1)
    states = converged(run, s.names, lambda st: region(st, target)["controller"] == 0
                       and not next(site for site in st["sites"] if site["index"] == target)["friendlyPresent"]
                       and building(st, index)["forceGoal"] == FALL_BACK
                       and all(distance2(u["position"], free["position"]) > 500 ** 2
                               for u in alive_units(force(st, s.owner, index))),
                       "capturing force physically clears deposit footprint while retaining controlled region")

    run.request("host", "fund", owner=s.owner, amount=160)
    run.request(s.peer, "build", kind=EXTRACTOR, **near(free["position"], 71, -63))
    states = converged(run, s.names, lambda st: deposit(st)["occupied"] and deposit(st)["complete"]
                       and deposit(st)["owner"] == s.owner and deposit(st)["team"] == 0
                       and any(b["index"] == deposit(st)["extractor"] and b["deposit"] == deposit_index
                               and b["position"][:2] == free["position"][:2] for b in st["buildings"])
                       and wallet(st, s.owner)["wallet"] == 0 and wallet(st, s.owner)["income"] == 2 + rate
                       and all(p["income"] == (2 + rate if p["index"] == s.owner else 2) for p in st["players"]),
                       "paid extractor snaps exact deposit XY, reserves it, and belongs only to builder")
    extractor = deposit(states["host"])["extractor"]
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    enemy_before = states["host"]["enemyResources"]
    run.request("host", "incomeTick")
    states = converged(run, s.names, lambda st: deposit(st)["remaining"] == total - 2 * rate
                       and st["enemyResources"] == enemy_before + 4
                       and all(p["wallet"] == before[p["index"]] + 4 + (2 * rate if p["index"] == s.owner else 0)
                               for p in st["players"])
                       and st["income"] == wallet(st, st["localIndex"])["income"],
                       "normal payment tick drains once, pays only builder bonus and baseline to every other wallet")
    run.request("host", "depositRemaining", deposit=deposit_index, remaining=3)
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    enemy_before = states["host"]["enemyResources"]
    run.request("host", "incomeTick")
    run.request("host", "incomeTick")
    states = converged(run, s.names, lambda st: deposit(st)["remaining"] == 0 and wallet(st, s.owner)["income"] == 2
                       and st["enemyResources"] == enemy_before + 8
                       and all(p["wallet"] == before[p["index"]] + 8 + (3 if p["index"] == s.owner else 0)
                               and p["income"] == 2 for p in st["players"]),
                       "finite final partial payment is capped and depleted extractor stops bonus on following tick")
    run.request("host", "destroyExtractor", building=extractor)
    converged(run, s.names, lambda st: not deposit(st)["occupied"] and deposit(st)["extractor"] == -1
              and region(st, target)["controller"] == 0 and wallet(st, s.owner)["income"] == 2,
              "real lethal attack frees depleted deposit, while anchor control retains polygon rights")
    run.phase("natural polygon capture, builder-only finite income, depletion and destruction freeing across peers")

    # JEV fixture uses the same paid placement and economy paths; only budget and completion are accelerated.
    run.request("host", "enemyExtractor")
    states = converged(run, s.names, lambda st: any(d["occupied"] and d["complete"] and d["team"] == 5
                                                    for d in st["deposits"]) and st["enemyResources"] == 0,
                       "JEV spends exactly 160 from its controllerless wallet for its own deposit extractor")
    enemy_deposit = next(d for d in states["host"]["deposits"] if d["complete"] and d["team"] == 5)
    enemy_index, enemy_rate, enemy_total = enemy_deposit["index"], enemy_deposit["rate"], enemy_deposit["remaining"]
    require(enemy_rate == (6 if enemy_deposit["rich"] else 4)
            and enemy_total == (3000 if enemy_deposit["rich"] else 2400), "JEV deposit finite defaults incorrect")

    def jev_deposit(st):
        return next(d for d in st["deposits"] if d["index"] == enemy_index)

    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    run.request("host", "incomeTick")
    states = converged(run, s.names, lambda st: st["enemyIncome"] == 2 + enemy_rate
                       and st["enemyResources"] == 4 + 2 * enemy_rate
                       and jev_deposit(st)["remaining"] == enemy_total - 2 * enemy_rate
                       and all(p["wallet"] == before[p["index"]] + 4 and p["income"] == 2 for p in st["players"]),
                       "JEV-owned extractor pays only enemy wallet and drains its finite deposit once")
    run.request("host", "depositRemaining", deposit=enemy_index, remaining=3)
    enemy_before = states["host"]["enemyResources"]
    before = {p["index"]: p["wallet"] for p in states["host"]["players"]}
    run.request("host", "incomeTick")
    run.request("host", "incomeTick")
    states = converged(run, s.names, lambda st: st["enemyResources"] == enemy_before + 11 and st["enemyIncome"] == 2
                       and jev_deposit(st)["remaining"] == 0
                       and all(p["wallet"] == before[p["index"]] + 8 and p["income"] == 2 for p in st["players"]),
                       "JEV depletion caps partial bonus then stops it, without affecting friendly private income")
    run.request("host", "destroyExtractor", building=jev_deposit(states["host"])["extractor"])
    converged(run, s.names, lambda st: not jev_deposit(st)["occupied"]
              and region(st, jev_deposit(st)["region"])["controller"] == 5,
              "JEV extractor destruction frees deposit without altering enemy main ownership")
    run.phase("JEV wallet-only finite extractor payment, depletion and deposit freeing across peers")
    research(run, s)
    enemy_main = next(r["index"] for r in run.observe("host")["regions"] if r["homeTeam"] == 5)
    run.request(s.peer, "goal", building=index, goal=ASSAULT, region=-1)
    converged(run, s.names, lambda st: goal_matches(st, index, ASSAULT, enemy_main),
              "Assault resolves the enemy main server-side on every peer")
    converged(run, s.names, lambda st: st["enemyHQ"] < 900, "Assault goal causes actual HQ weapon damage")
    states = finish(run, s, squad, "weapon-caused victory replicates")
    require(all(wallet(st, s.owner)["doctrine"] == 1 and wallet(st, s.owner)["wallet"] == 50 for st in states.values()),
            "repeat research changed choice or charged twice")
    run.phase("research, Assault-goal weapon damage and victory")
    return states


def restart_and_converge(run, s, old):
    """Connected restart: every peer converges on the fresh, fully reset world with preserved sockets."""
    run.request(s.peer, "restart")
    run.isolate_fresh_host(old["host"]["generation"], len(old["host"]["sites"]))

    def reset(name, state):
        return (state["generation"] > old[name]["generation"] and state["localIndex"] == s.identities[name]
                and len(state["players"]) == len(s.names)
                and state["result"] == 0 and state["enemyHQ"] == 900 and state["friendlyHQ"] == 900
                and not any(a["team"] == 0 for a in state["armies"])
                and not any(b["team"] == 0 for b in state["buildings"])
                and all(p["doctrine"] == 0 and p["wallet"] >= 600 for p in state["players"])
                and len(state["sites"]) == len(old["host"]["sites"])
                and len(state["deposits"]) == len(old["host"]["deposits"])
                and all(not d["occupied"] and d["remaining"] == (3000 if d["rich"] else 2400) for d in state["deposits"])
                and all(site["owner"] == -1 and site["progress"] == 0 for site in state["sites"]))
    states = run.await_states(s.names, lambda values: all(reset(name, state) for name, state in values.items()),
                              "same connected commanders converge on the reset construction world", allow_travel=True)
    for name, state in states.items():
        require(state["gameStateId"] != old[name]["gameStateId"] and state["netDriverId"] == old[name]["netDriverId"],
                f"{name}: fresh world or preserved connection missing")
    run.phase("fresh empty bases, all map sectors neutral, research reset and preserved socket identities")


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
    "economy": (economy_scenario, "polygon capture, builder/JEV-only finite extractors, depletion/freeing, research, HQ damage and victory"),
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
