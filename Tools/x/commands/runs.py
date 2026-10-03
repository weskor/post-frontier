"""Inspect durable run evidence without creating another run."""

import argparse
from pathlib import Path
import sys
from typing import Any

from x import jsonio, source
from x.context import Context
from x.runs import recent

NAME = "runs"
SUMMARY = "List the last 20 runs or inspect one run's evidence and logs."
HELP = """./x runs [<id>] [--compare-current]

Without an id, list the last 20 runs. With an id, inspect the complete record,
source snapshots, retained tracked diffs, untracked path/content-hash manifests,
logs and artifacts. Archived legacy records remain readable and unchanged.

--compare-current requires an id and compares current source with the completed
snapshot. Exit zero means content equality only, NOT acceptance or PASS. Different
or unknown content exits one; invalid usage exits two. This is read-only: it creates
no run, writes no Git objects and never changes the user's index. A commit/rebase
alone does not invalidate equal content. Check the command, scope results, failed
execs and requested proof tier separately; equality cannot replace missing tests.

Snapshots identify HEAD plus actual tracked modifications/deletions and nonignored
untracked files, regardless of staged/unstaged split. Unchanged content assets use
HEAD blob identities instead of being rehashed. LFS uses pointer content hashes
for hydrated assets. Retained binary tracked diffs include actual dirty content;
LFS baseline blobs are pointers, and no LFS clean filter/object-cache writes occur.
Manifests retain hashes, not untracked contents or ignored files/secrets.

Initial/completed, scope and exec boundaries are observations, not an atomic
filesystem snapshot. Inspect each interval's content and execution input_hashes.
Different boundaries expose mutation (including formatting/generation); an equal
completed snapshot does NOT mean a mutating command tested its final output.
Scope exec indices identify the executions belonging to that result; automation
and builds retain the editor hashes used by their existing mutation guards.
Equal boundaries cannot detect edits reverted between observations. Legacy,
submodule, opaque clean-filter/encoding, noncanonical line-ending snapshots and
sparse/assume-unchanged index entries are unknown, never inferred from commit/dirty
or promoted to a source match.
"""
RECORD = False


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("id", nargs="?", help="run id to inspect")
    parser.add_argument(
        "--compare-current",
        action="store_true",
        help="compare completed source with current content; equality is not PASS",
    )


def compare_current(record: dict[str, Any], repo: Path) -> int:
    provenance = record.get("source", {})
    snapshots = provenance.get("snapshots", [])
    completed = provenance.get("completed")
    baseline = (
        snapshots[completed]
        if isinstance(completed, int) and 0 <= completed < len(snapshots)
        else {}
    )
    current = source.capture(repo) if baseline else {}
    match = source.equality(baseline, current)
    print(f"completed source vs current: {match}")
    print(f"run status: {record['status']} (content equality is not acceptance/PASS)")
    print(f"initial to completed: {provenance.get('initial_to_completed', 'unknown')}")
    for index, snapshot in enumerate(snapshots):
        print(
            f"  source[{index}] {snapshot['label']} vs current: "
            f"{source.equality(snapshot, current)}"
        )
    if match == "unknown":
        print(
            f"source uncertainty: {baseline.get('unknown') or current.get('unknown') or 'legacy or incomplete evidence'}"
        )
    print("Inspect ./x runs <id> for scope/exec inputs, mutations and proof limits.")
    return 0 if match == "equal" else 1


def run(args: argparse.Namespace, ctx: Context) -> int:
    compare = getattr(args, "compare_current", False)
    if compare and args.id is None:
        print("--compare-current requires a run id", file=sys.stderr)
        return 2
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
    if compare:
        return compare_current(record, ctx.repo)
    for key, value in record.items():
        print(f"{key}: {value}")
    print("Logs:")
    for execution in record["execs"]:
        peak = execution.get("peak_rss_mb")
        memory = "unmeasured" if peak is None else f"{peak:.2f} MiB"
        print(f"  {execution['log']} (peak RSS: {memory})")
    return 0
