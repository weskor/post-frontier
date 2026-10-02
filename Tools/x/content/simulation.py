"""Argument definitions shared by the CLI and its standalone match harness."""

import argparse
from pathlib import Path


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--duel",
        action="store_true",
        help="equal-Power combat roster matrix per seed, without AI/economy (default V2)",
    )
    parser.add_argument(
        "--map",
        action="append",
        dest="maps",
        help="repeat /Game/... packages; default V2 and v1 (duel: V2 only)",
    )
    parser.add_argument(
        "--variant",
        action="append",
        help="baseline2/3/4 or NAME:baseline=3,normal_rate=4,...",
    )
    parser.add_argument(
        "--matches",
        type=int,
        default=10,
        help="matches per map/variant (duel: matrix seeds per map)",
    )
    parser.add_argument(
        "--matrix", action="store_true", help="V2 baseline2/3/4 plus v1 baseline2"
    )
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--time-cap",
        type=float,
        default=2400,
        help="game-second cap per match (duel: per ordered pair, not entire matrix)",
    )
    parser.add_argument("--dilation", type=float, default=1)
    parser.add_argument("--compare-dilation", type=float)
    parser.add_argument("--sample-seconds", type=float, default=600)
    parser.add_argument("--comparison-tolerance", type=float, default=0.05)
    parser.add_argument(
        "--package",
        action="store_true",
        help="use the latest fresh development package",
    )
    parser.add_argument(
        "--stall-seconds",
        type=float,
        default=180,
        help="fail without persisted game-time progress",
    )
    parser.add_argument(
        "--report-only",
        type=Path,
        help="regenerate report from an existing run without Unreal",
    )
