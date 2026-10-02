"""List or execute named test scopes, building Unreal automatically."""

import argparse

from x.context import Context
from x.scopes import load
from x.testing import run_scopes

NAME = "test"
SUMMARY = "Run named test scopes, building the editor automatically when stale."
HELP = "./x test <scope> [<scope>...] [--map /Game/Maps/Boot]\n./x test --list\n--map overrides the world of every automation scope in this invocation; other scope kinds are unchanged.\nOnly /Game/... package paths without extensions or URL options are accepted; the engine resolves existence.\nWithout --map, each scope retains its configured map. ./x check never supplies an override.\nRuns every requested scope and retains per-scope results and logs.\nAutomation requires explicit Success, zero exit, SoftQuit and the requested map.\nMap validator images go to the run directory; lint-only scopes need ./x check."
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    from harness.verify import map_package

    parser.add_argument("scopes", nargs="*")
    parser.add_argument("--list", action="store_true", dest="list_scopes")
    parser.add_argument(
        "--map",
        type=map_package,
        dest="map_path",
        help="override every automation scope's world package; default: scope-configured map",
    )


def run(args: argparse.Namespace, ctx: Context) -> int:
    mapping = load(ctx.repo)
    if args.list_scopes:
        if args.scopes or args.map_path is not None:
            print("--list does not accept scopes or --map")
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
    return 0 if run_scopes(ctx, args.scopes, map_path=args.map_path) else 1
