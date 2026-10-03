"""The sole entry point for moving main."""

import argparse

from x.context import Context
from x.landing import land

NAME = "land"
SUMMARY = "Rebase, check and fast-forward main from a clean task worktree."
HELP = """./x land

Run on a committed task/<slug> branch, never main. Landings serialize under
<lock_dir>/land.lock. Rebase conflicts are aborted and their paths printed.
Before rebasing, refuse a branch whose own commits change generated binaries
(agents commit text only; main's history is not inspected).
After the rebase, regenerate binary assets whose text sources changed on the
branch: Tools/x/generated.toml maps sources to outputs and the ./x gen entries
that write them. Gens run serially and their outputs are committed in one
separate "Regenerate binary assets (./x land)" commit; a generator that fails
or writes outside its outputs refuses the landing and leaves the worktree
clean. No changed source: nothing regenerates. Whenever land refuses after
committing regenerated binaries it removes that commit again (other local
edits, such as check formatting, are kept), so a manual `git rebase main`
never replays binaries. Only a crashed land can leave one; the next land
drops commits with that subject whose paths are all mapped outputs.
Then reuse the newest passed ./x check of this branch whose content
stayed unchanged during the check, equals the rebased content exactly and passed
every scope now selected; the land record names it. Otherwise run the scoped
check in-process on the result, regenerated binaries included; commit any
formatting fixes before retrying. Reuse does not rerun flaky scopes or detect
engine/toolchain changes.
Fast-forward main in its own worktree; any local change there blocks landing.
Audit main against the landing ledger; on failure stop and ask the owner.
Print the landed commit range and run ID. Hooks allow main updates only here.
The pre-commit hook rejects any other commit that changes a generated binary
(or adds an unmapped .uasset/.umap): change the text source and land. Hand-made
binaries with no generator are listed as authored in the map and stay allowed.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.description = SUMMARY


def run(args: argparse.Namespace, ctx: Context) -> int:
    return land(ctx)
