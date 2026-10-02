"""Inspect durable run evidence without creating another run."""

import argparse
from pathlib import Path
import sys

from x import jsonio
from x.context import Context
from x.runs import recent

NAME = "runs"
SUMMARY = "List the last 20 runs or inspect one run's evidence and logs."
HELP = "Run ./x runs to list the last 20 runs. Run ./x runs <id> to print its record, log paths and artifacts."
RECORD = False


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("id", nargs="?", help="run id to inspect")


def run(args: argparse.Namespace, ctx: Context) -> int:
    if args.id is None:
        records = recent(ctx.settings.runs_root)
        if not records:
            print("No runs recorded.")
        for record in records:
            duration = record["duration_s"]
            elapsed = f"{duration:.2f}s" if duration is not None else "-"
            print(
                f"{record['id']}  {record['status']}  {elapsed}  {record['branch']}  {record['command']}"
            )
        return 0
    if Path(args.id).name != args.id or args.id in (".", ".."):
        print("invalid run id", file=sys.stderr)
        return 2
    path = ctx.settings.runs_root / args.id / "record.json"
    if not path.is_file():
        print(f"run not found: {args.id}", file=sys.stderr)
        return 1
    record = jsonio.load(path)
    for key, value in record.items():
        print(f"{key}: {value}")
    print("Logs:")
    for execution in record["execs"]:
        print(f"  {execution['log']}")
    return 0
