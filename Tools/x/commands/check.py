"""Formatting and changed-file lint, without Unreal execution."""

import argparse
from pathlib import Path

from x import gitinfo, lint
from x.context import Context

NAME = "check"
SUMMARY = "Apply formatting and run blocking lint on changed files."
HELP = """./x check [--all]

Apply formatting fixes, print reformatted paths, then run every enabled lint rule.
By default inspect changes against main, the index, worktree and untracked files.
--all inspects every tracked file. Deleted files are skipped.
Rule scopes and reasoned exceptions live in Tools/x/lint*.toml.
Each rule records its outcome; exit zero means zero findings.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--all", action="store_true", help="lint every tracked file")


def run(args: argparse.Namespace, ctx: Context) -> int:
    paths = (
        [Path(p) for p in lint.repository_files(ctx.repo, tracked=True)]
        if args.all
        else gitinfo.changed_files(ctx.repo)
    )
    return 0 if lint.run(ctx, paths, fix=True) else 1
