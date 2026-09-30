#!/usr/bin/env python3
"""Offscreen-rendered HUD captures from one owned Development listen host; no window, focus or OS input.

The host renders with Vulkan into an offscreen viewport (-RenderOffScreen). HUD buttons are clicked
through the controller's real left-click entry point at the centre the HUD's own geometry reports, and
Escape/F4 go through Enhanced Input mappings via PlayerInput. None of this is compositor/OS input.
Host-only fixtures (isolate, fund, capture, finish) shorten setup and are recorded in events.jsonl.

Full run: the whole presentation state sequence (placement, production, starvation, force, casualty,
research, victory) at the first resolution, selected-barracks captures at the others.
--quick <label>: boot, one placed barracks, deck + inspector captures at one resolution, stop. It proves
the deck and a selected building's inspector render; it never proves production, starvation, fronts,
research or victory presentation.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

from network import (NetworkRun, require, owned_buildings, wallet, building, force, alive_units, distance2,
                     force_counts_match, near, BARRACKS, WORKSHOP, RANGED, SIEGE)

# EHUDAction ordinals from Source/CoopRTS/CommandHUD.h.
BUILD_BARRACKS, RECIPE_RANGED, RECIPE_SIEGE, TOGGLE_PRODUCTION, FRONT_SECURE, RESEARCH_REPAIRS = 1, 6, 7, 8, 9, 13
CONSTRUCTION = 15


def png_size(path):
    header = path.read_bytes()[:24]
    require(header[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG")
    return struct.unpack(">II", header[16:24])


def compositor_windows(pid):
    """Read-only Hyprland query: windows owned by pid. None when no compositor query is available."""
    if not shutil.which("hyprctl"):
        return None
    try:
        clients = json.loads(subprocess.run(["hyprctl", "clients", "-j"], check=True, capture_output=True, text=True).stdout)
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return None
    return [c.get("title", "") for c in clients if c.get("pid") == pid]


class Capture:
    def __init__(self, run):
        self.run = run
        self.folder = run.run / "captures"
        self.folder.mkdir()
        self.count = 0

    def state(self):
        return self.run.observe("host")

    def wait(self, predicate, description):
        return self.run.await_states(["host"], lambda s: predicate(s["host"]), description)["host"]

    def hud(self, action, description):
        self.run.request("host", "hud", hudAction=action)
        self.run.phase(f"HUD click via shared geometry: {description}")

    def key(self, name):
        self.run.request("host", "key", key=name, pressed=True)
        self.run.request("host", "key", key=name, pressed=False)

    def minimap(self, horizontal, vertical):
        before = self.state()
        origin = before["minimapOrigin"]
        size = before["minimapSize"]
        self.run.request("host", "hudClick", x=origin[0] + size * horizontal, y=origin[1] + size * vertical)
        # The minimap spans the replicated arena: top edge is +X, left edge is -Y.
        half = before["arenaHalfExtent"]
        expected = (half[0] - 2 * half[0] * vertical, -half[1] + 2 * half[1] * horizontal, 0)
        after = self.wait(lambda s: all(abs(actual - target) < 1
                                       for actual, target in zip(s["cameraPosition"], expected)),
                          f"minimap camera focus at {expected}")
        require((after["placing"], after["assigningFront"]) == (before["placing"], before["assigningFront"]),
                "minimap click changed active placement/front mode")
        require([(b["actorId"], b["front"]) for b in after["buildings"]]
                == [(b["actorId"], b["front"]) for b in before["buildings"]],
                "minimap camera click placed a building or reassigned a front")
        self.run.phase(f"minimap camera-only click at {horizontal:.2f},{vertical:.2f}")

    def shot(self, label):
        state = self.state()
        self.count += 1
        path = self.folder / f"{self.count:02d}-{label}.png"
        self.run.request("host", "screenshot", path=str(path))
        previous = -1

        def written():
            nonlocal previous
            if not path.exists():
                return None
            size = path.stat().st_size
            stable = size > 0 and size == previous
            previous = size
            return stable or None
        self.run.until(written, f"screenshot {path.name} written", 5, names=["host"])
        width, height = png_size(path)
        require((width, height) == (state["viewportWidth"], state["viewportHeight"]),
                f"{path.name}: {width}x{height} does not match viewport {state['viewportWidth']}x{state['viewportHeight']}")
        self.run.event("capture", path=str(path), width=width, height=height, hudExpanded=state["hudExpanded"],
                       placing=state["placing"], assigningFront=state["assigningFront"],
                       feedback=state.get("orderFeedback", ""))
        print(f"Captured {path} ({width}x{height})", flush=True)
        return path


def no_compositor_windows(run, pid):
    windows = compositor_windows(pid)
    run.event("compositor-windows", pid=pid, windows=windows)
    require(not windows, f"offscreen host mapped compositor windows: {windows}")


def boot(run, capture, resolution):
    """Rendered, isolated listen host with a fresh expanded deck at the requested viewport."""
    run.start("host", host=True)
    pid = run.peers["host"]["process"].pid
    state = capture.wait(lambda s: s["localIndex"] >= 0 and len(s["sites"]) == 3 and s["viewportWidth"] > 0,
                         "rendered listen host with commander and sectors")
    no_compositor_windows(run, pid)
    run.request("host", "isolate")
    run.phase("isolated enemy planner for stable presentation states (fixture)")
    capture.wait(lambda s: s["hudExpanded"] and not s["buildingSelected"], "fresh expanded deck")
    # The offscreen null platform ignores -ResX/-ResY; keep that launch size as a small-viewport check.
    launch = (state["viewportWidth"], state["viewportHeight"])
    if launch != resolution:
        capture.shot(f"start-overview-launch-{launch[0]}x{launch[1]}")
        width, height = resolution
        run.request("host", "resolution", width=width, height=height)
        state = capture.wait(lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height), f"viewport {width}x{height}")
    return pid, state


def place_barracks(run, capture, owner, count, description):
    candidate = run.request("host", "placement", kind=BARRACKS)["placementCandidate"]
    run.request("host", "build", kind=BARRACKS, x=candidate[0], y=candidate[1])
    return capture.wait(lambda s: len(owned_buildings(s, owner, BARRACKS)) == count, description)


def quick(run, label, resolution):
    capture = Capture(run)
    pid, state = boot(run, capture, resolution)
    owner = state["localIndex"]
    capture.shot(f"{label}-deck")
    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "owned barracks placed")
    barracks = owned_buildings(state, owner, BARRACKS)[0]["index"]
    run.request("host", "select", target="building", building=barracks)
    state = capture.wait(lambda s: s["buildingSelected"] and building(s, barracks)["constructionProgress"] == 1,
                         "selected barracks complete")
    require(building(state, barracks)["productionState"] == "Unconfigured",
            "fresh completed barracks does not present as unconfigured")
    capture.shot(f"{label}-inspector")
    no_compositor_windows(run, pid)
    run.event("PASS", captures=capture.count, resolutions=[resolution], quick=label)


def scenario(run, resolutions):
    capture = Capture(run)
    pid, state = boot(run, capture, resolutions[0])
    owner = state["localIndex"]
    hq = state["friendlyHQPosition"]
    capture.shot("start-overview")
    capture.minimap(.25, .25)
    capture.shot("minimap-camera-northwest")
    capture.minimap(.99, .01)
    capture.minimap(.5, .5)
    capture.key("SpaceBar")

    capture.hud(BUILD_BARRACKS, "Build Barracks card enters placement")
    capture.wait(lambda s: s["placing"] and not s["hudExpanded"], "placement mode with collapsed deck")
    capture.shot("placement-mode")
    capture.minimap(.75, .75)
    capture.key("SpaceBar")
    capture.key("Escape")
    capture.wait(lambda s: not s["placing"] and s["hudExpanded"], "Escape cancels placement and reopens deck")
    capture.key("F4")
    capture.wait(lambda s: not s["hudExpanded"], "F4 hides deck")
    capture.shot("deck-hidden")
    capture.minimap(.5, .5)
    capture.key("SpaceBar")
    capture.hud(CONSTRUCTION, "Persistent Construction button reopens hidden choices")
    capture.wait(lambda s: s["hudExpanded"], "Construction reopens deck without a hotkey")
    capture.key("F4")
    capture.wait(lambda s: not s["hudExpanded"], "F4 hides reopened deck")
    capture.key("F4")
    capture.wait(lambda s: s["hudExpanded"], "F4 reopens deck")
    run.phase("Escape/F4 mappings and persistent Construction action")

    run.request("host", "fund", owner=owner, amount=1000)
    state = place_barracks(run, capture, owner, 1, "owned barracks placed")
    require(state["hudExpanded"] and not state["placing"],
            "successful placement did not restore construction choices")
    capture.hud(BUILD_BARRACKS, "Build choices remain available immediately after placement")
    capture.wait(lambda s: s["placing"], "second building choice without F4 or selection")
    capture.key("Escape")
    run.phase("accepted placement reopens build choices; a second placement needs no discovery hotkey")
    barracks = owned_buildings(state, owner, BARRACKS)[0]["index"]
    run.request("host", "select", target="building", building=barracks)
    capture.wait(lambda s: s["buildingSelected"] and 0.15 < building(s, barracks)["constructionProgress"] < 0.9,
                 "barracks visibly under construction")
    capture.shot("barracks-constructing")
    capture.wait(lambda s: building(s, barracks)["constructionProgress"] == 1, "barracks complete")
    capture.shot("barracks-ready")
    capture.hud(RECIPE_SIEGE, "Preview Siege before the first Start")
    capture.wait(lambda s: building(s, barracks)["recipe"] == SIEGE and not building(s, barracks)["configured"],
                 "Siege remains a freely selectable unconfigured type")
    capture.shot("barracks-siege-unconfigured")

    run.request("host", "income", paused=True)
    run.request("host", "fund", owner=owner, amount=30)
    capture.hud(RECIPE_RANGED, "Ranged recipe")
    capture.wait(lambda s: building(s, barracks)["recipe"] == RANGED, "ranged recipe replicated")
    capture.hud(TOGGLE_PRODUCTION, "Start production")
    capture.wait(lambda s: building(s, barracks)["enabled"] and building(s, barracks)["configured"]
                 and building(s, barracks)["productionSeconds"] > building(s, barracks)["unitTime"] * .25,
                 "configured force production progressing")
    capture.shot("barracks-producing-locked-type")
    capture.hud(TOGGLE_PRODUCTION, "Pause partially produced unit")
    paused = capture.wait(lambda s: not building(s, barracks)["enabled"], "partial unit paused")
    progress = building(paused, barracks)["productionSeconds"]
    capture.shot("barracks-paused-locked-type")
    run.request("host", "hud", hudAction=RECIPE_SIEGE, expect_rejection=True)
    locked = capture.state()
    require(building(locked, barracks)["recipe"] == RANGED and building(locked, barracks)["configured"]
            and building(locked, barracks)["productionSeconds"] == progress
            and wallet(locked, owner)["wallet"] == wallet(paused, owner)["wallet"],
            "paused configured force exposed a type-changing action or mutated work/wallet")
    capture.hud(TOGGLE_PRODUCTION, "Resume locked ranged force")
    run.request("host", "fund", owner=owner, amount=0)
    capture.wait(lambda s: building(s, barracks)["productionState"] == "InsufficientResources",
                 "enabled production reports insufficient resources")
    capture.shot("barracks-waiting-resources")

    capture.hud(FRONT_SECURE, "Secure front enters ground targeting")
    capture.wait(lambda s: s["assigningFront"] and not s["hudExpanded"], "front targeting mode")
    capture.shot("front-mode")
    capture.minimap(.25, .75)
    capture.key("SpaceBar")
    capture.key("Escape")
    capture.wait(lambda s: not s["assigningFront"] and s["hudExpanded"], "Escape cancels front targeting")
    # Ground clicks need a cursor; the same owning-controller RPC assigns the front.
    run.request("host", "front", building=barracks, frontOrder=0, **near(hq, 1700, 2300))
    before_resume = building(capture.state(), barracks)
    run.request("host", "fund", owner=owner, amount=(4 - before_resume["joined"] - before_resume["travelling"]) * 30)
    capture.wait(lambda s: building(s, barracks)["productionState"] == "Producing",
                 "funding automatically resumes enabled production")
    run.phase("resource starvation and automatic resume without toggling production")
    state = capture.wait(lambda s: building(s, barracks)["travelling"] > 0 and force_counts_match(s, owner, barracks)
                         and building(s, barracks)["frontOrder"] == 0, "paid unit travelling from producer to Secure front")
    squad = building(state, barracks)["forceID"]
    capture.shot("barracks-recruit-travelling")
    state = capture.wait(lambda s: building(s, barracks)["joined"] == 4
                         and building(s, barracks)["travelling"] == 0 and force_counts_match(s, owner, barracks)
                         and building(s, barracks)["productionState"] == "ForceComplete",
                         "four paid ranged units physically join and fill their own force")
    require(building(state, barracks)["capacity"] == 4 and wallet(state, owner)["wallet"] == 0,
            "ranged force capacity or single-unit payment mismatch")
    capture.shot("barracks-force-complete")
    capture.hud(TOGGLE_PRODUCTION, "Pause full force")
    capture.wait(lambda s: not building(s, barracks)["enabled"]
                 and building(s, barracks)["productionState"] == "Paused", "explicit pause takes priority while full")
    capture.shot("barracks-full-paused")
    capture.hud(TOGGLE_PRODUCTION, "Enable full force")
    capture.wait(lambda s: building(s, barracks)["enabled"]
                 and building(s, barracks)["productionState"] == "ForceComplete"
                 and building(s, barracks)["joined"] == 4 and wallet(s, owner)["wallet"] == 0,
                 "enabled full force reports automatic capacity waiting, without charging")
    capture.hud(TOGGLE_PRODUCTION, "Pause full force before casualty")
    capture.wait(lambda s: building(s, barracks)["productionState"] == "Paused", "full force paused again")
    victim = alive_units(force(state, owner, barracks))[0]
    run.request("host", "kill", owner=owner, army=squad, slot=victim["slot"])
    capture.wait(lambda s: building(s, barracks)["joined"] == 3 and building(s, barracks)["travelling"] == 0,
                 "real casualty creates force deficit")
    capture.shot("barracks-casualty-deficit-paused")
    run.request("host", "fund", owner=owner, amount=30)
    capture.hud(TOGGLE_PRODUCTION, "Resume automatic paid replacement")
    state = capture.wait(lambda s: building(s, barracks)["travelling"] == 1
                         and building(s, barracks)["joined"] == 3 and wallet(s, owner)["wallet"] == 0
                         and force_counts_match(s, owner, barracks), "one paid replacement leaves the producer")
    recruit = next(u for u in alive_units(force(state, owner, barracks)) if u["reinforcing"])
    origin = recruit["position"]
    capture.shot("barracks-replacement-travelling")
    run.request("host", "front", building=barracks, frontOrder=1, **near(hq, 1700, 3100))
    capture.wait(lambda s: any(u["reinforcing"] and distance2(u["position"], origin) > 200 ** 2
                              for u in alive_units(force(s, owner, barracks))),
                 "replacement physically tracks moving force")
    capture.wait(lambda s: building(s, barracks)["joined"] == 4 and building(s, barracks)["travelling"] == 0
                 and force_counts_match(s, owner, barracks), "replacement physically arrives")
    capture.shot("barracks-replacement-complete")
    capture.hud(TOGGLE_PRODUCTION, "Pause after replacement proof")
    state = capture.wait(lambda s: not building(s, barracks)["enabled"], "force paused for presentation checks")
    run.phase("permanent type, partial pause, starvation, four-unit force, real casualty and paid physical replacement")
    roster = [(a["army"], a["serial"]) for a in state["armies"] if a["owner"] == owner]
    for key in ("Tab", "Q", "H", "R"):
        capture.key(key)
    state = capture.state()
    require(roster == [(a["army"], a["serial"]) for a in state["armies"] if a["owner"] == owner],
            "removed manual squad keys changed automatic squad orders")
    require(state["buildingSelected"], "removed Tab binding changed building selection")
    run.phase("former squad-control keys preserve automatic fronts and building selection")

    run.request("host", "fund", owner=owner, amount=400)
    state = place_barracks(run, capture, owner, 2, "independent Siege producer placed")
    siege = next(b["index"] for b in owned_buildings(state, owner, BARRACKS) if b["index"] != barracks)
    run.request("host", "select", target="building", building=siege)
    capture.key("SpaceBar")
    capture.wait(lambda s: building(s, siege)["constructionProgress"] == 1, "Siege producer complete")
    capture.hud(RECIPE_SIEGE, "Choose Siege for an independent producer")
    capture.wait(lambda s: building(s, siege)["recipe"] == SIEGE, "independent Siege type selected")
    capture.shot("siege-first-start-180")
    capture.hud(TOGGLE_PRODUCTION, "Pay one-time Siege configuration and lock")
    capture.wait(lambda s: building(s, siege)["configured"] and building(s, siege)["enabled"]
                 and wallet(s, owner)["wallet"] == 0 and building(s, siege)["joined"] == 0
                 and building(s, siege)["travelling"] == 0
                 and building(s, siege)["productionState"] == "InsufficientResources",
                 "exactly 180 configures Siege without an unpaid unit")
    capture.shot("siege-locked-waiting-unit-funds")
    capture.hud(TOGGLE_PRODUCTION, "Pause configured Siege before other purchases")
    capture.wait(lambda s: not building(s, siege)["enabled"], "Siege paused without changing type")
    run.phase("rendered first-Start Siege fee and locked independent two-slot force")

    run.request("host", "select", target="none")
    candidate = run.request("host", "placement", kind=WORKSHOP)["placementCandidate"]
    run.request("host", "fund", owner=owner, amount=400)
    run.request("host", "build", kind=WORKSHOP, x=candidate[0], y=candidate[1])
    state = capture.wait(lambda s: len(owned_buildings(s, owner, WORKSHOP)) == 1, "owned workshop placed")
    workshop = owned_buildings(state, owner, WORKSHOP)[0]["index"]
    run.request("host", "select", target="building", building=workshop)
    capture.key("SpaceBar")
    capture.wait(lambda s: building(s, workshop)["constructionProgress"] == 1, "workshop complete")
    capture.shot("workshop-research")
    capture.hud(RESEARCH_REPAIRS, "Buy Field Repairs")
    capture.wait(lambda s: wallet(s, owner)["doctrine"] == 2, "Field Repairs owned")
    capture.shot("workshop-owned")

    run.request("host", "select", target="building", building=barracks)
    capture.key("SpaceBar")
    for width, height in resolutions[1:]:
        run.request("host", "resolution", width=width, height=height)
        capture.wait(lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height), f"viewport {width}x{height}")
        capture.shot(f"barracks-{width}x{height}")
    if len(resolutions) > 1:
        width, height = resolutions[0]
        run.request("host", "resolution", width=width, height=height)
        capture.wait(lambda s: (s["viewportWidth"], s["viewportHeight"]) == (width, height), "primary viewport restored")

    run.request("host", "finish", owner=owner, army=squad, win=True)
    capture.wait(lambda s: s["result"] == 1, "weapon-caused victory")
    capture.shot("victory")
    no_compositor_windows(run, pid)
    run.event("PASS", captures=capture.count, resolutions=resolutions)


def resolution(text):
    width, height = (int(v) for v in text.lower().split("x"))
    return width, height


def label(text):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", text):
        raise argparse.ArgumentTypeError("use letters, numbers, underscores or hyphens")
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True, type=Path, help="fresh Saved/Verification/<id> directory")
    parser.add_argument("--mode", choices=("editor", "packaged"), required=True)
    parser.add_argument("--res", type=resolution, action="append",
                        help="viewport WxH set with r.SetRes; the first hosts the full state sequence, others "
                             "capture the selected barracks (default 1600x900); --quick uses only the first")
    parser.add_argument("--quick", type=label, metavar="LABEL",
                        help="boot, place and select one barracks, capture <LABEL>-deck and <LABEL>-inspector at the "
                             "first resolution, then stop; no production fill, fronts, research or victory")
    parser.add_argument("--max-fps", type=int, default=30)
    args = parser.parse_args()
    resolutions = args.res or [(1600, 900)]
    run = NetworkRun(args.run.resolve(), args.mode, 0, False, max_fps=args.max_fps, offscreen=resolutions[0])
    try:
        if args.quick:
            quick(run, args.quick, resolutions[0])
            print(f"PASS: quick {args.quick} at {resolutions[0][0]}x{resolutions[0][1]}; evidence: {run.run}")
        else:
            scenario(run, resolutions)
            print(f"PASS: {len(resolutions)} viewport(s); evidence: {run.run}")
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
