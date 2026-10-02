"""List or execute named test scopes, building Unreal automatically."""

import argparse

from x.context import Context
from x.scopes import load
from x.testing import run_scopes

NAME = "test"
SUMMARY = "Run named test scopes, building the editor automatically when stale."
HELP = "./x test <scope> [<scope>...]\n./x test --list\nRuns every requested scope and retains per-scope results and logs.\nAutomation requires explicit Success, zero exit, SoftQuit and the requested map.\nMap validator images go to the run directory; lint-only scopes need ./x check."
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("scopes", nargs="*")
    parser.add_argument("--list", action="store_true", dest="list_scopes")


def run(args: argparse.Namespace, ctx: Context) -> int:
    mapping = load(ctx.repo)
    if args.list_scopes:
        if args.scopes:
            print("--list does not accept scopes")
            return 2
        for name in mapping.names():
            scope = mapping.definitions[name]
            print(
                f"{name}: {scope.kind}" + (f" ({scope.filter})" if scope.filter else "")
            )
        return 0
    if not args.scopes:
        print("provide at least one scope; use ./x test --list")
        return 2
    unknown = sorted(set(args.scopes) - set(mapping.names()))
    if unknown:
        print(f"unknown scopes: {', '.join(unknown)}; use ./x test --list")
        return 2
    return 0 if run_scopes(ctx, args.scopes) else 1
