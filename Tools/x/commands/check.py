"""Formatting, blocking lint and tests selected by changed paths."""

import argparse
from dataclasses import dataclass
from pathlib import Path

from x import gitinfo, lint
from x.context import Context
from x.landing import audit_check
from x.scopes import load
from x.testing import run_scopes

NAME = "check"
SUMMARY = "Apply formatting, run blocking lint and changed-file test scopes."
HELP = """./x check [--all]

Apply formatting fixes, print reformatted paths, then run every enabled lint rule.
A lint failure stops the check before any test scope runs.
By default inspect changes against main, the index, worktree and untracked files.
--all lints every tracked file; tests still follow changed paths, including deletions.
Print each selected scope and the changed paths that selected it.
Python format, lint and tests use uv run --locked, syncing the locked dev environment.
Automation builds once, then leases a headless slot per scope; non-Unreal scopes
take no Unreal lock. Scope lease wait times are retained in the run's lock_waits.
Exit zero means lint and every selected scope passed, not every proof tier.
This is the only change proof; use ./x land to land it. Tools/x/scopes.toml owns
selection, so do not hand-pick fewer tests or recreate a feature-to-test table.
Name the changed behavior and observable failure condition before checking.
Start with this scoped proof; move outward only for a contract it cannot express,
and run slow ./x verify extras only when the task brief names them. A routine
change does not authorize a full gameplay, topology or fault acceptance matrix.
Compilation, accepted commands and screenshots do not replace live assertions.
Never weaken assertions to pass. Preserve failed/interrupted runs, separate
product defects from harness/setup failures, then make a targeted correction.
A full check can take tens of minutes: do not rerun it after every fix. Iterate
with ./x test <failed scopes> (and ./x check again only for lint failures, which
stop before tests) until those pass, then run ./x check once as the final proof.
Inspect exact scope results and limits via ./x runs <id> and ./x help test;
do not claim untested native, replicated or Steam behavior.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--all", action="store_true", help="lint every tracked file")


@dataclass(frozen=True)
class CheckResult:
    ok: bool
    reformatted: tuple[Path, ...]


def check(ctx: Context, *, all_files: bool = False, audit: bool = True) -> CheckResult:
    if audit:
        audit_check(ctx)
    changed = gitinfo.changed_files(ctx.repo)
    paths = (
        [Path(p) for p in lint.repository_files(ctx.repo, tracked=True)]
        if all_files
        else changed
    )
    result = lint.run(ctx, paths, fix=True)
    reformatted = ", ".join(map(str, result.reformatted)) or "(none)"
    if not result.ok:
        print(f"check: FAIL (lint; test scopes skipped); reformatted: {reformatted}")
        return CheckResult(False, result.reformatted)
    mapping = load(ctx.repo)
    scopes = mapping.scopes_for(changed)
    reasons: dict[str, list[str]] = {scope: [] for scope in scopes}
    for path in changed:
        for scope in mapping.scopes_for([path]):
            reasons[scope].append(str(path))
    print(f"check scopes: {', '.join(scopes) or '(none)'}")
    for scope, selected in reasons.items():
        print(f"  {scope} selected by: {', '.join(selected)}")
    tests_ok = run_scopes(ctx, scopes)
    print(f"check: {'PASS' if tests_ok else 'FAIL'}; reformatted: {reformatted}")
    return CheckResult(tests_ok, result.reformatted)


def run(args: argparse.Namespace, ctx: Context) -> int:
    return 0 if check(ctx, all_files=args.all).ok else 1
