#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = ["matplotlib==3.11.2"]
# ///
"""Run real standalone AI matches, or summarize existing evidence. Never invent draws."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import signal
import sys

from harness.simulation_evidence import save_json
from harness.simulation_execution import ROOT, artifact_identity, run_match
from harness.simulation_planning import parse_variant, plan_jobs, validate_options
from harness.simulation_report import summarize
from harness.verify import JsonObject
from x.content.simulation import configure
from x.settings import load


def main() -> int:
    if not os.environ.get("X_RUN_ID"):
        raise ValueError("use ./x sim")
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    if args.variant:
        args.variant = [parse_variant(text) for text in args.variant]
    if args.report_only:
        run = args.report_only.resolve()
        return 0 if summarize(run, json.loads((run / "run.json").read_text())) else 1
    validate_options(parser, args)
    jobs = plan_jobs(parser, args)
    artifacts = artifact_identity(args)
    run = Path(os.environ["X_RUN_DIR"]).resolve()
    manifest: JsonObject = dict(
        schema_version=1,
        created=dt.datetime.now(dt.UTC).isoformat(),
        artifacts=artifacts,
        planned_jobs=jobs,
        matches=[],
        comparison_dilation=args.compare_dilation,
        comparison_tolerance=args.comparison_tolerance,
        lock=str(load(ROOT).lock_dir),
    )
    save_json(run / "run.json", manifest)
    interrupted = False
    try:
        for index, job in enumerate(jobs):
            map_id = job["map"].rsplit("/", 1)[-1]
            directory = (
                run
                / f"{index + 1:03d}-{map_id}-{job['variant']}-seed{job['seed']}-x{job['dilation']:g}"
            )
            print(f"MATCH {index + 1}/{len(jobs)} {directory.name}", flush=True)
            try:
                record = run_match(args, job, directory, artifacts)
            except KeyboardInterrupt:
                record = json.loads((directory / "launch.json").read_text())
                manifest["matches"].append(record)
                interrupted = True
                break
            manifest["matches"].append(record)
            save_json(run / "run.json", manifest)
    finally:
        save_json(run / "run.json", manifest)
    success = summarize(run, manifest)
    print(f"Report: {run / 'Report.md'}", flush=True)
    return 130 if interrupted else 0 if success else 1


if __name__ == "__main__":

    def interrupted(signum: int, frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Simulation runner failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
