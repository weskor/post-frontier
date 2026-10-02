"""Run the balance harness; the harness acquires a headless slot per match."""

import argparse
import sys

from x.building import ensure_editor
from x.content import simulation
from x.content.packages import latest_package
from x.context import Context

NAME = "sim"
SUMMARY = (
    "Run standalone AI matches or combat duel matrices and retain balance reports."
)
HELP = (
    "Runs Tools/harness/simulate_matches.py with runner-owned UE_ROOT and evidence directory. "
    "Editor mode ensures a fresh module first; --package selects the latest fresh Development "
    "package. Each match independently acquires the shared headless pool so agents can interleave. "
    "Default maps: AvailabilityZoneV2 and AvailabilityZone; baseline2; 10 matches each; "
    "2400 game-second cap; 1x dilation. --matrix runs V2 baseline2/3/4 plus v1 baseline2. "
    "--duel runs the full ordered runtime combat roster, including mirrors, in each seed process; "
    "default V2 only, 120 Power per side in whole units, no configuration fees or autopilot. "
    "--matches counts matrix seeds; --time-cap defaults to 300 game-seconds separately per pair. "
    "Duel economy variants, --matrix and --compare-dilation are incompatible. "
    "Duel reports evaluate Docs/Design/units.md rules; balance failures are evidence, not runtime failures. "
    "Stalled duels are invalid runtime failures, never draws or a balance baseline. "
    "Use --compare-dilation for paired telemetry comparison; --report-only RUN regenerates "
    "charts/reports without a game. Reports live in the recorded run directory, never Saved/Simulation."
)
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    simulation.configure(parser)


def run(args: argparse.Namespace, ctx: Context) -> int:
    if not args.report_only:
        if args.package:
            latest_package(ctx.repo, "development", ctx.settings.game_target)
        elif not ensure_editor(ctx):
            return 1
    if ctx.run is None:
        raise RuntimeError("simulation requires a recorded run")
    code = ctx.exec(
        [
            "uv",
            "run",
            "--script",
            ctx.repo / "Tools/harness/simulate_matches.py",
            *sys.argv[2:],
        ],
        log="simulation",
        env={"PYTHONPATH": str(ctx.repo / "Tools")},
        stall_seconds=max(ctx.settings.stall_seconds, args.stall_seconds + 30),
    )
    ctx.run.add_artifact(ctx.run.dir / "Report.md", "simulation report")
    print(f"Simulation log: {ctx.run.dir / 'simulation.log'}")
    return code
