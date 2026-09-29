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
    def __init__(self, directory, mode, clients, emulation, rendered=False, max_fps=None):
        self.run = directory
        self.mode = mode
        self.clients = clients
        self.emulation = emulation
        self.rendered = rendered
        self.max_fps = max_fps
        self.peers = {}
        self.sequences = {}
        self.latest_states = {}
        self.latest_errors = {}
        self.connection_health = {}
        self.pending = {}
        self.artifact = editor_stamp() if mode == "editor" else package_stamp()
        self.run.mkdir(parents=True, exist_ok=False)
        (self.run / "run.json").write_text(json.dumps({"mode": mode, "clients": clients, "emulation": emulation,
                                                     "rendered": rendered, "max_fps": max_fps,
                                                     "artifact": self.artifact}, indent=2))
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
                       "-game", "-nullrhi", "-nosound", "-unattended"]
        else:
            command = [str(BINARY), "/Game/Maps/Boot?listen" if host else f"127.0.0.1:{self.port}",
                       "-nosound", "-unattended"]
            if not self.rendered:
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

    def request(self, name, action="observe", *, allow_unavailable=False, **fields):
        entry = self.peers[name]
        require(self.live(name), f"{name} process is not owned/live")
        self.sequences[name] += 1
        command_id = self.sequences[name]
        folder = entry["folder"]
        payload = {"id": command_id, "action": action, "owner": 0, "army": 0, **fields}
        temp = folder / "request.tmp"
        temp.write_text(json.dumps(payload))
        os.replace(temp, folder / "request.json")
        self.event("request", peer=name, **payload)
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
        require(response["peer"] == name
                and (not response["error"] or (allow_unavailable and response["error"] == "game world unavailable")),
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

    def stop(self, name):
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
        self.until(lambda: text in log.read_text(errors="replace") if log.exists() else False,
                   f"{name} expected server rejection {text!r}", 45, names=["host"])
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


def complete(states, count):
    return all(state["localIndex"] >= 0 and len(state["players"]) == count
               and len(state["armies"]) == count * 2 + 1
               and all(len(group["units"]) == 6 and
                       (group["owner"] in range(5) or group["team"] == 5)
                       for group in state["armies"])
               for state in states.values())


def roster(states, count):
    for name, state in states.items():
        require(state["ready"] and state["netMode"] == (2 if name == "host" else 3),
                f"{name}: expected independent listen/client network world")
        require(len(state["players"]) == count, f"{name}: player roster not {count}")
        indices = [p["index"] for p in state["players"]]
        require(len(set(indices)) == count and set(indices) <= set(range(5)), f"{name}: invalid/duplicate identities {indices}")
        for index in indices:
            for group_number in (0, 1):
                group = army(state, index, group_number)
                require(group["team"] == 0 and len(group["units"]) == 6, f"{name}: wrong owned army composition")
                require(sorted((u["slot"], u["role"], u["owner"]) for u in group["units"]) == [
                    (s, s // 2, index) for s in range(6)], f"{name}: missing, duplicated or foreign unit roles")
        require(len([a for a in state["armies"] if a["team"] == 5]) == 1, f"{name}: enemy spawned more than once")


def converged(run, names, predicate, description, report_interval=5):
    return run.await_states(names, lambda states: all(predicate(s) for s in states.values()),
                            description, report_interval)

def verify_live_income(run, commanders):
    before = run.observe("host")

    def earned(states):
        current = states["host"]
        deltas = [wallet(current, index)["wallet"] - wallet(before, index)["wallet"]
                  for index in commanders]
        if not any(delta > 0 for delta in deltas):
            return False
        require(len(set(deltas)) == 1 and deltas[0] > 0 and deltas[0] % 32 == 0,
                f"each separate wallet must receive identical live +32 captured-site ticks: {deltas}")
        run.event("live_income", commanders=commanders, deltas=deltas)
        return True

    run.await_states(["host"], earned, "authoritative captured-site income reaches every wallet", 35)



def main_scenario(run):
    run.start("host", host=True)
    run.await_states(["host"], lambda s: s["host"]["netMode"] == 2
                     and s["host"]["localIndex"] >= 0 and len(s["host"]["armies"]) == 3,
                     "listen server bound and spawned before remote connects", 90, allow_loading=True)
    run.start("c1")
    peers = ["host", "c1"]
    states = run.await_states(peers, lambda s: complete(s, 2),
                              "two distinct connected worlds with full replicated roster", 75,
                              allow_loading=True)
    roster(states, 2)
    a, b = states["host"]["localIndex"], states["c1"]["localIndex"]
    require(a != b, "host/client identity collision")
    run.phase("two live peer worlds agree on complete owned rosters")
    run.request("host", "isolate")
    run.request("host", "kill", owner=a, army=0, slot=4)
    converged(run, peers, lambda s: len(army(s, a, 0)["units"]) == 5,
              "server-caused casualty replicated before foreign purchase")
    before = run.all_states(peers)
    foreign = army(before["host"], a, 0)
    run.request("c1", "order", owner=a, army=0, order=1, x=-850, y=-1800)
    run.await_states(["c1"], lambda s: s["c1"].get("orderFeedback", "").startswith("Order rejected"),
                     "foreign move RPC rejected by server with owning-client feedback", 15)
    run.request("c1", "buy", owner=a, army=0, repeat=12)
    run.await_states(["c1"], lambda s: s["c1"].get("orderFeedback", "").startswith("Recovery rejected"),
                     "foreign paid request rejected by server", 15)
    run.request("c1", "attack", owner=a, army=0)
    run.await_states(["c1"], lambda s: s["c1"].get("orderFeedback", "").startswith("Attack rejected"),
                     "foreign targeted attack RPC rejected by server", 15)
    run.request("c1", "doctrine", choice=255)
    run.request("c1", "doctrine", choice=2)
    run.request("host", "doctrine", choice=1)
    run.request("c1", "doctrine", choice=3)
    converged(run, peers, lambda s: wallet(s, a)["doctrine"] == 1 and wallet(s, b)["doctrine"] == 2
              and all(army(s, i, n)["doctrine"] == (1 if i == a else 2)
                      for i in (a, b) for n in (0, 1)), "owner-isolated irreversible doctrine replication")
    # The client's later doctrine RPC is a reliable channel barrier after its foreign requests.
    converged(run, peers, lambda s: army(s, a, 0)["serial"] == foreign["serial"]
              and len(army(s, a, 0)["units"]) == 5, "foreign order/attack/purchase preserve owner state")
    require(wallet(run.observe("host"), a)["wallet"] >= wallet(before["host"], a)["wallet"],
            "foreign client spent or removed server-owned resources")
    run.request("host", "capture", owner=a, army=1, site=0)
    converged(run, peers, lambda s: next(t for t in s["sites"] if t["index"] == 0)["owner"] == 0
              and s["resourceSites"] == 1, "actual ticking capture agrees across worlds", 30)
    verify_live_income(run, (a, b))
    run.request("host", "income", paused=True)
    require(run.observe("host")["incomePaused"], "authority fixture did not pause further income")
    run.phase("capture and live shared income agree before payment audit")
    run.request("host", "kill", owner=b, army=0, slot=4)
    converged(run, peers, lambda s: len(army(s, b, 0)["units"]) == 5
              and 4 not in [u["slot"] for u in army(s, b, 0)["units"]], "hostile weapon death resolves once")
    # Clear prior foreign-order feedback and wait for this owning Hold RPC
    # before checking a new purchase's exact server rejection.
    prior = army(run.observe("host"), b, 0)["serial"]
    run.request("c1", "order", owner=b, army=0, order=0, x=0, y=0)
    run.await_states(["c1"], lambda s: army(s["c1"], b, 0)["serial"] > prior
                     and s["c1"].get("orderFeedback", "") == "",
                     "owning Hold clears stale feedback before insufficient-funds probe")
    run.request("host", "fund", owner=b, army=0, amount=0)
    converged(run, peers, lambda s: wallet(s, b)["wallet"] == 0,
              "paused zero-resource wallet replicates to both worlds")
    barrier = army(run.observe("host"), b, 0)["serial"]
    run.request("c1", "buy", owner=b, army=0, repeat=4)
    def insufficient(states):
        feedback = states["c1"].get("orderFeedback", "")
        if feedback.startswith("Recovery rejected:") and "INSUFFICIENT RESOURCES" not in feedback:
            raise AssertionError(f"expected insufficient funds, got contradictory server response {feedback!r}")
        return "INSUFFICIENT RESOURCES" in feedback
    run.await_states(["c1"], insufficient, "client receives server insufficient-funds rejection")
    run.request("c1", "order", owner=b, army=0, order=0, x=0, y=0)
    converged(run, peers, lambda s: army(s, b, 0)["serial"] > barrier
              and len(army(s, b, 0)["units"]) == 5 and wallet(s, b)["wallet"] == 0,
              "insufficient paid spam does not grant units or debit wallet")
    run.request("host", "fund", owner=b, army=0, amount=360)
    converged(run, peers, lambda s: wallet(s, b)["wallet"] == 360,
              "paused paid fixture wallet replicates exactly")
    run.phase("paused zero wallet rejects insufficient-funds RPCs")
    pre_buy = run.all_states(peers)
    run.request("c1", "buy", owner=b, army=0, repeat=24)
    # A subsequent reliable RPC on the same owning controller is a barrier for every
    # purchase attempt; observing only the first replacement would miss late overspend.
    run.request("c1", "order", owner=b, army=0, order=0, x=0, y=0)
    def paid_barrier(states):
        if states["c1"].get("orderFeedback", "").startswith("Order rejected"):
            raise AssertionError("owning Hold rejected; cannot use it as a paid-RPC processing barrier")
        return army(states["host"], b, 0)["serial"] > army(pre_buy["host"], b, 0)["serial"]
    run.await_states(["host", "c1"], paid_barrier, "owning Hold processed after all paid RPCs")
    charged = run.observe("host")
    debit = ((wallet(charged, a)["wallet"] - wallet(pre_buy["host"], a)["wallet"])
             - (wallet(charged, b)["wallet"] - wallet(pre_buy["host"], b)["wallet"]))
    require(debit == 80 and len(army(charged, b, 0)["units"]) == 6,
            f"owning RPCs must buy exactly one 80-resource siege role, observed debit={debit}")
    converged(run, peers, lambda s: len(army(s, b, 0)["units"]) == 6
              and army(s, b, 0)["doctrine"] == 2, "paid army and owner doctrine agree across peers")
    post_buy = run.all_states(peers)
    require(all(sorted(u["slot"] for u in army(s, b, 0)["units"]) == list(range(6))
                for s in post_buy.values()), "paid spam exceeded exact role roster")
    run.phase("paid replacement charged exactly once; owner doctrines replicated")
    serial = army(run.observe("host"), b, 0)["serial"]
    run.request("c1", "order", owner=b, army=0, order=1, x=-1550, y=-1600)
    converged(run, peers, lambda s: army(s, b, 0)["order"] == 1
              and abs(army(s, b, 0)["destination"][1] + 1600) < 80,
              "first remote movement order accepted", 20)
    run.request("c1", "order", owner=b, army=0, order=0, x=0, y=0)
    converged(run, peers, lambda s: army(s, b, 0)["order"] == 0,
              "remote Hold replaces movement", 15)
    run.request("c1", "order", owner=b, army=0, order=1, x=-1600, y=-2200)
    converged(run, peers, lambda s: army(s, b, 0)["order"] == 1
              and army(s, b, 0)["serial"] >= serial + 3
              and abs(army(s, b, 0)["destination"][1] + 2200) < 80,
              "latest replacement move accepted and replicated", 20)
    def arrived(s):
        return all((u["position"][0] - (-1600 + (1 - u["slot"] // 2) * 220)) ** 2
                   + (u["position"][1] - (-2200 + (140 if u["slot"] % 2 else -140))) ** 2 < 250 ** 2
                   for u in army(s, b, 0)["units"])
    converged(run, peers, arrived, "all six distinct slots, including purchased siege, arrive", 85)
    run.phase("remote orders remain usable and every purchased unit reaches its formation slot")
    for win, result in ((True, 1), (False, 2)):
        initial = run.all_states(peers)
        run.request("host", "finish", owner=a, army=0, win=win)
        converged(run, peers, lambda s: s["result"] == result
                  and s["enemyHQ" if win else "friendlyHQ"] == 0,
                  "weapon-caused HQ result and replicated victory/defeat", 20)
        terminal = run.all_states(peers)
        run.request("c1", "order", owner=b, army=0, order=1, x=-800, y=-800)
        run.await_states(["c1"], lambda s: s["c1"].get("orderFeedback", "").startswith("Order rejected"),
                         "terminal move RPC explicitly rejected by server", 15)
        converged(run, peers, lambda s: army(s, b, 0)["serial"] == army(terminal["host"], b, 0)["serial"],
                  "terminal rejection preserves order serial")
        generation = {name: initial[name]["generation"] for name in peers}
        run.request("c1", "restart")
        states = run.await_states(peers, lambda s: complete(s, 2) and all(
                    v["generation"] > generation[name] and v["result"] == 0
                    for name, v in s.items()), "all connected clients travel to fresh ongoing Boot world", 90,
                    allow_travel=True)
        roster(states, 2)
        require(all(p["doctrine"] == 0 for v in states.values() for p in v["players"]),
                "restart must reset each doctrine")
        a, b = states["host"]["localIndex"], states["c1"]["localIndex"]
        require(a != b, "connected restart merged host/client identities")
        run.phase(f"{'victory' if win else 'defeat'} and connected two-peer fresh restart")
        run.request("host", "isolate")
    if run.clients == 4:
        for name in ("c2", "c3", "c4"):
            run.start(name)
            peers.append(name)
            states = run.await_states(peers, lambda s: complete(s, len(peers)),
                                      f"{name} joined ongoing match and all worlds agree", 75,
                                      allow_loading=True)
            roster(states, len(peers))
        indices = [states[name]["localIndex"] for name in peers]
        require(len(set(indices)) == 5, "five client identities collide")
        run.phase("five independent player worlds agree on ownership and roster")
        commander = states["c4"]["localIndex"]
        host_index = states["host"]["localIndex"]
        run.request("c4", "doctrine", choice=3)
        run.request("host", "doctrine", choice=1)
        converged(run, peers, lambda s: wallet(s, commander)["doctrine"] == 3
                  and wallet(s, host_index)["doctrine"] == 1,
                  "five-world doctrine ownership separation")
        run.request("host", "capture", owner=host_index, army=1, site=0)
        converged(run, peers, lambda s: s["resourceSites"] == 1
                  and next(t for t in s["sites"] if t["index"] == 0)["owner"] == 0,
                  "one captured site replicated across all five worlds", 35)
        verify_live_income(run, indices)
        run.request("host", "income", paused=True)
        require(run.observe("host")["incomePaused"], "five-player income fixture not paused")
        run.request("host", "kill", owner=commander, army=0, slot=5)
        converged(run, peers, lambda s: len(army(s, commander, 0)["units"]) == 5
                  and 5 not in [u["slot"] for u in army(s, commander, 0)["units"]],
                  "one actual death replicated across five worlds")
        run.request("host", "fund", owner=commander, army=0, amount=360)
        converged(run, peers, lambda s: wallet(s, commander)["wallet"] == 360,
                  "five peers observe controlled paid wallet")
        payment_before = run.observe("host")
        run.request("c4", "buy", owner=commander, army=0, repeat=20)
        run.request("c4", "order", owner=commander, army=0, order=0, x=0, y=0)
        def five_barrier(states):
            if states["c4"].get("orderFeedback", "").startswith("Order rejected"):
                raise AssertionError("five-player owning Hold rejected before purchase audit")
            return army(states["host"], commander, 0)["serial"] > army(payment_before, commander, 0)["serial"]
        run.await_states(["host", "c4"], five_barrier, "all five-world paid RPCs precede owning Hold")
        paid = run.observe("host")
        debit = (wallet(paid, host_index)["wallet"] - wallet(payment_before, host_index)["wallet"]
                 - wallet(paid, commander)["wallet"] + wallet(payment_before, commander)["wallet"])
        require(debit == 80 and len(army(paid, commander, 0)["units"]) == 6,
                f"five-player purchase must charge one 80-resource siege role, debit={debit}")
        converged(run, peers, lambda v: len(army(v, commander, 0)["units"]) == 6
                  and army(v, commander, 0)["doctrine"] == 3,
                  "five peers agree on one paid role replacement", 35)
        run.phase("five peers observe capture, death and one exact paid replacement")
        run.start("c5", rejection=True)
        run.rejected("c5", "Match full (five commanders maximum)")
        roster(run.all_states(peers), 5)
        run.phase("sixth connection rejected without an orphan army")
        left = states["c3"]["localIndex"]
        run.stop("c3")
        peers.remove("c3")
        run.await_states(peers, lambda s: all(len(v["players"]) == 4
                    and not any(a["owner"] == left for a in v["armies"]) for v in s.values()),
                    "leave removes owner armies and roster entry", 35)
        run.phase("disconnect removes two armies and frees the commander slot")
        run.start("c6")
        peers.append("c6")
        states = run.await_states(peers, lambda s: complete(s, 5),
                                  "late replacement reuses freed commander slot", 75, allow_loading=True)
        roster(states, 5)
        require(states["c6"]["localIndex"] == left, "leave slot not reused")
        run.phase("late joining client reuses the freed slot")
        generations = {name: state["generation"] for name, state in states.items()}
        run.request("host", "finish", owner=states["host"]["localIndex"], army=0, win=True)
        converged(run, peers, lambda s: s["result"] == 1 and s["enemyHQ"] == 0,
                  "five connected peer worlds agree on replicated HQ victory")
        run.request("c6", "restart")
        states = run.await_states(peers, lambda s: complete(s, 5) and all(
                    v["generation"] > generations[name] and v["result"] == 0
                    and all(p["doctrine"] == 0 for p in v["players"]) for name, v in s.items()),
                    "five connected clients travel to a fresh shared match", 100, allow_travel=True)
        roster(states, 5)
        run.phase("five connected worlds restart after shared HQ victory")
    else:
        run.request("host", "finish", owner=a, army=0, win=True)
        converged(run, peers, lambda s: s["result"] == 1, "terminal match before late join")
        run.start("late", rejection=True)
        run.rejected("late", "Match finished; rejoin after restart")
        roster(run.all_states(peers), 2)
    if run.emulation:
        for name, state in run.all_states(peers).items():
            require(state.get("pktLag") == 120 and state.get("pktLoss") == 8,
                    f"{name} network emulation not active on native net driver: {state.get('pktLag')}/{state.get('pktLoss')}")
    run.event("PASS", peers=peers, mode=run.mode, emulation=run.emulation)


def focused_restart_scenario(run):
    names = ["host", *(f"c{i}" for i in range(1, run.clients + 1))]
    run.start("host", host=True)
    run.await_states(["host"], lambda s: complete(s, 1), "listen host ready", 35,
                     allow_loading=True)
    for name in names[1:]:
        run.start(name)
        states = run.await_states(names[:names.index(name) + 1],
                                  lambda s: complete(s, len(s)),
                                  f"connected {name} enters one shared match", 35,
                                  allow_loading=True)
        roster(states, len(states))
        run.phase(f"{name} joined focused restart match ({len(states)} worlds)")
    for number in range(3):
        states = run.all_states(names)
        generations = {name: state["generation"] for name, state in states.items()}
        owner = states["host"]["localIndex"]
        run.request("host", "finish", owner=owner, army=0, win=True)
        converged(run, names, lambda s: s["result"] == 1 and s["enemyHQ"] == 0,
                  f"restart cycle {number + 1} replicates actual HQ victory")
        run.request(names[-1], "restart")
        states = run.await_states(names, lambda s: complete(s, len(names)) and all(
                    state["generation"] > generations[name] and state["result"] == 0
                    and all(player["doctrine"] == 0 for player in state["players"])
                    for name, state in s.items()),
                    f"restart cycle {number + 1} travels all connected peers", 30,
                    allow_travel=True)
        roster(states, len(names))
        run.phase(f"connected restart {number + 1}/3 across {len(names)} actual worlds")
    run.event("PASS", peers=names, mode=run.mode, scenario="restart")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, type=Path, help="fresh Saved/Verification/<id> directory")
    parser.add_argument("--mode", choices=("editor", "packaged"), required=True)
    parser.add_argument("--clients", type=int, choices=(1, 4), required=True)
    parser.add_argument("--scenario", choices=("full", "restart"), default="full",
                        help="full acceptance run, or focused three-cycle connected restart repro")
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
        (focused_restart_scenario if args.scenario == "restart" else main_scenario)(run)
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
