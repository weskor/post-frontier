"""Run the balance harness; the harness acquires a headless slot per match."""

import argparse
import sys

from x.building import ensure_editor
from x.content import simulation
from x.content.packages import latest_package
from x.context import Context

NAME = "sim"
SUMMARY = "Run real standalone AI matches and retain balance reports."
HELP = (
    "Runs Tools/harness/simulate_matches.py with runner-owned UE_ROOT and evidence directory. "
    "Editor mode ensures a fresh module first; --package selects the latest fresh Development "
    "package. Each match independently acquires the shared headless pool so agents can interleave. "
    "Default maps: AvailabilityZoneV2 and AvailabilityZone; baseline2; 10 matches each; "
    "2400 game-second cap; 1x dilation. --matrix runs V2 baseline2/3/4 plus v1 baseline2. "
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
