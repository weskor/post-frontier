"""The sole entry point for moving main."""

import argparse

from x.context import Context
from x.landing import land

NAME = "land"
SUMMARY = "Rebase, check and fast-forward main from a clean task worktree."
HELP = """./x land

Run on a committed task/<slug> branch, never main. Landings serialize under
<lock_dir>/land.lock. Rebase conflicts are aborted and their paths printed.
After the rebase, reuse the newest passed ./x check of this branch whose content
stayed unchanged during the check, equals the rebased content exactly and passed
every scope now selected; the land record names it. Otherwise run the scoped
check in-process; commit any formatting fixes before retrying. Reuse does not
rerun flaky scopes or detect engine/toolchain changes.
Fast-forward main in its own worktree; any local change there blocks landing.
Audit main against the landing ledger; on failure stop and ask the owner.
Print the landed commit range and run ID. Hooks allow main updates only here.
Binary regeneration and the binary-commit hook switch on together in phase 4.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.description = SUMMARY


def run(args: argparse.Namespace, ctx: Context) -> int:
    return land(ctx)
