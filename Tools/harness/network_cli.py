"""Network scenario execution, PASS/FAIL recording and unconditional peer cleanup."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

from harness.network import NetworkRun, configure
from harness.network import __doc__ as network_description
from harness.network_scenarios import SCENARIOS


def main() -> None:
    parser = argparse.ArgumentParser(description=network_description)
    configure(parser)
    args = parser.parse_args()
    if args.rendered and args.mode != "packaged":
        parser.error("--rendered is only available for packaged runs")
    if args.max_fps is not None and args.max_fps < 1:
        parser.error("--max-fps must be positive")
    pointer_orders = args.scenario in (
        "ownership", "production", "economy", "restart", "construction"
    )
    run = NetworkRun(
        Path(os.environ["X_HARNESS_DIR"]).resolve(),
        args.mode,
        args.clients,
        args.emulation,
        args.rendered,
        args.max_fps,
        map_path=args.map,
        offscreen=(1600, 900) if args.offscreen or pointer_orders else None,
    )
    scenario, _ = SCENARIOS[args.scenario]
    try:
        scenario(run)
        run.event(
            "PASS",
            peers=list(run.peers),
            mode=run.mode,
            scenario=args.scenario,
            emulation=run.emulation,
        )
        print(
            f"PASS: {args.scenario} {args.mode} host + {args.clients} remote clients; evidence: {run.run}"
        )
    except BaseException as error:
        run.event("FAIL", error=repr(error))
        print(f"FAIL: {error}; evidence: {run.run}", file=sys.stderr)
        raise
    finally:
        for name in reversed(list(run.peers)):
            if run.peers[name]["process"].poll() is None:
                run.stop(name)
        run.events.close()
