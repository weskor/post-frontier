"""Formatting, blocking lint and tests selected by changed paths."""

import argparse
from dataclasses import dataclass
from pathlib import Path

from x import gitinfo, lint
from x.context import Context
from x.scopes import load
from x.testing import run_scopes

NAME = "check"
SUMMARY = "Apply formatting, run blocking lint and changed-file test scopes."
HELP = """./x check [--all]

Apply formatting fixes, print reformatted paths, then run every enabled lint rule.
By default inspect changes against main, the index, worktree and untracked files.
--all lints every tracked file; tests still follow changed paths, including deletions.
Print each selected scope and the changed paths that selected it.
Python format, lint and tests use uv run --locked, syncing the locked dev environment.
Exit zero means lint and every selected scope passed.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--all", action="store_true", help="lint every tracked file")


@dataclass(frozen=True)
class CheckResult:
    ok: bool
    reformatted: tuple[Path, ...]


def check(ctx: Context, *, all_files: bool = False) -> CheckResult:
    changed = gitinfo.changed_files(ctx.repo)
    paths = (
        [Path(p) for p in lint.repository_files(ctx.repo, tracked=True)]
        if all_files
        else changed
    )
    result = lint.run(ctx, paths, fix=True)
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
    print(
        f"check: {'PASS' if result.ok and tests_ok else 'FAIL'}; reformatted: "
        + (", ".join(map(str, result.reformatted)) or "(none)")
    )
    return CheckResult(result.ok and tests_ok, result.reformatted)


def run(args: argparse.Namespace, ctx: Context) -> int:
    return 0 if check(ctx, all_files=args.all).ok else 1
