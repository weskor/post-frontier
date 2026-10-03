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
        help="'force-bar' exercises force cards at every --res; 'pings' captures G ground/minimap "
        "markers and six-second expiry; 'jev-intent' captures the JEV timeline, badges and memos "
        "through plan creation, escalation and replacement; other labels boot, place and select "
        "one barracks and capture <LABEL>-deck and <LABEL>-inspector at the first resolution, "
        "then stop; no production fill, fronts, research or victory",
    )
    parser.add_argument("--max-fps", type=int, default=30)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    resolutions = args.res or [(1600, 900)]
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
        if args.quick == "force-bar":
            from harness.hud_force_bar import scenario as force_bar_scenario

            force_bar_scenario(run, resolutions)
            print(
                f"PASS: force bar at {len(resolutions)} viewports; evidence: {run.run}"
            )
        elif args.quick:
            quick(run, args.quick, resolutions[0])
            print(
                f"PASS: quick {args.quick} at {resolutions[0][0]}x{resolutions[0][1]}; evidence: {run.run}"
            )
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
    if not os.environ.get("X_RUN_ID"):
        sys.exit("run through ./x verify")
    main()
