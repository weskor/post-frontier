#!/usr/bin/env python3
"""Offscreen-rendered HUD captures from one owned Development listen host; no window, focus or OS input.

The host renders with Vulkan into an offscreen viewport (-RenderOffScreen). HUD buttons are clicked
through the controller's real left-click entry point at the centre the HUD's own geometry reports, and
A/R/Escape/F4 go through Enhanced Input mappings via PlayerInput. Selected forces order
through shared right-click and Attack-confirm controller entry points; cursor snapshots
record the same resolver used by rendered previews. None of this is compositor/OS input.
Host-only fixtures (isolate, fund, capture, finish) shorten setup and are recorded in events.jsonl.

Full run: the whole presentation state sequence (placement, production, starvation, force, casualty,
research, victory) at the first resolution, selected-barracks captures at the others.
--quick pings: real G ground placement, shared minimap placement, active map/minimap marker captures,
then a six-second expiry capture. --quick jev-intent: publishes controlled JEV plans (create, escalate, replace) on an
isolated host and captures the timeline bar, region badge and memo feed after each.
--quick force-bar: real card selection, production pause/resume, Attack, withdrawal and Retreat,
with state captures at every --res. Other --quick labels boot one placed barracks and capture the
deck + inspector at one resolution; they do not prove production, orders, research or victory.
--quick new-units: locks two real Barracks to the Lancer and the Scrambler, waits for each full squad of 3 and
captures both squads and the two together at the default camera (Human faction only); new-units-support-first
produces the Scrambler first, which puts it in the open instead of behind the second Barracks.
--quick jev-units: the Machine-faction counterpart on an isolated host: the host fixture places two completed JEV
Barracks locked to the Lancer and the Scrambler, and the capture shows each full squad of 3, then both with the camera between them.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import sys

from harness.hud_scenario import scenario
from harness.hud_setup import quick
from harness.network import NetworkRun
from harness.verify import DEFAULT_MAP
from x.scopes import map_package


def resolution(text: str) -> tuple[int, int]:
    width, height = (int(v) for v in text.lower().split("x"))
    return width, height


def label(text: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_-]+", text):
        raise argparse.ArgumentTypeError("use letters, numbers, underscores or hyphens")
    return text


def configure(parser: argparse.ArgumentParser) -> None:
    parser.description = __doc__
    parser.add_argument("--mode", choices=("editor", "packaged"), required=True)
    parser.add_argument(
        "--map",
        type=map_package,
        default=DEFAULT_MAP,
        help="world package path (default: %(default)s)",
    )
    parser.add_argument(
        "--res",
        type=resolution,
        action="append",
        help="viewport WxH set with r.SetRes; the first hosts the full state sequence, others "
        "capture the selected barracks (default 1600x900); --quick uses only the first",
    )
    parser.add_argument(
        "--quick",
        type=label,
        metavar="LABEL",
        help="'fortify' captures dock, targeting, active badges and team feed at every --res; "
        "'pressure' captures the empty JEV timeline, a stunned Barracks, the LINE CUT chip, the v1.1 release cell "
        "and the wave it sends at every --res (run it on --map /Game/Maps/AvailabilityZoneV2; takes about "
        "two minutes of game time); "
        "'team' captures the Team panel (opener, rows, toggle, presets, Send, a refusal, the log, the teammate card's Gift entry) at every --res; "
        "'force-bar' exercises force cards at every --res; 'map-presentation' captures region traits, a "
        "supply cut at the moment it lands and five seconds later, and the Scrambler pulse ring at every --res "
        "(run it on --map /Game/Maps/AvailabilityZoneV2); 'pings' captures G ground/minimap "
        "markers and six-second expiry; 'jev-intent' captures the JEV timeline, badges and memos "
        "through plan creation, escalation and replacement; other labels boot, place and select "
        "one barracks and capture <LABEL>-deck and <LABEL>-inspector at the first resolution, "
        "then stop; 'new-units' and 'new-units-support-first' produce a Lancer and a Scrambler squad and capture them; 'jev-units' "
        "does the same for JEV (Machine faction); no "
        "production fill, fronts, research or victory otherwise",
    )
    parser.add_argument("--max-fps", type=int, default=30)


def execute(
    run: NetworkRun, label: str | None, resolutions: list[tuple[int, int]]
) -> None:
    """Run the scenario the --quick label names (or the full sequence) and print its PASS line."""
    count = len(resolutions)
    if label == "force-bar":
        from harness.hud_force_bar import scenario as force_bar_scenario

        force_bar_scenario(run, resolutions)
        print(f"PASS: force bar at {count} viewports; evidence: {run.run}")
    elif label == "map-presentation":
        from harness.hud_map_presentation import scenario as map_scenario

        map_scenario(run, resolutions)
        print(f"PASS: map presentation at {count} viewports; evidence: {run.run}")
    elif label == "pressure":
        from harness.hud_pressure import scenario as pressure_scenario

        pressure_scenario(run, resolutions)
        print(f"PASS: pressure HUD at {count} viewports; evidence: {run.run}")
    elif label == "team":
        from harness.hud_team import scenario as team_scenario

        team_scenario(run, resolutions)
        print(f"PASS: Team panel; evidence: {run.run}")
    elif label == "planning":
        from harness.hud_planning import scenario as planning_scenario

        planning_scenario(run, resolutions)
        print(f"PASS: planning phase at {count} viewports; evidence: {run.run}")
    elif label == "fortify":
        from harness.hud_fortify import scenario as fortify_scenario

        fortify_scenario(run, resolutions)
        print(f"PASS: Fortify at {count} viewports; evidence: {run.run}")
    elif label:
        quick(run, label, resolutions[0])
        print(
            f"PASS: quick {label} at {resolutions[0][0]}x{resolutions[0][1]}; evidence: {run.run}"
        )
    else:
        scenario(run, resolutions)
        print(f"PASS: {count} viewport(s); evidence: {run.run}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    resolutions = args.res or (
        [(1600, 900), (1280, 720)]
        if args.quick in ("fortify", "map-presentation", "pressure", "team", "planning")
        else [(1600, 900)]
    )
    run = NetworkRun(
        Path(os.environ["X_HARNESS_DIR"]).resolve(),
        args.mode,
        0,
        False,
        max_fps=args.max_fps,
        offscreen=resolutions[0],
        map_path=args.map,
    )
    try:
        execute(run, args.quick, resolutions)
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
    if not os.environ.get("X_RUN_ID"):
        sys.exit("run through ./x verify")
    main()
