"""List or execute named test scopes, building Unreal automatically."""

import argparse

from x.context import Context
from x.scopes import load, map_package
from x.testing import run_scopes

NAME = "test"
SUMMARY = "Run named test scopes, building the editor automatically when stale."
HELP = """./x test <scope> [<scope>...] [--map /Game/Maps/Boot]
./x test --list
--map overrides the world of every automation scope in this invocation; other
scope kinds are unchanged. Only /Game/... package paths without extensions or
URL options are accepted; the engine resolves existence. Without --map, each
scope retains its configured map; only ./x test accepts this override.

Use ./x check for change proof: it selects scopes from Tools/x/scopes.toml.
Use named tests for a diagnosed failure, to iterate on scopes a check failed
(then run ./x check once as final proof), or for a requested baseline;
--list prints the available scope names, kinds and automation filters. Do not
invent scenarios or duplicate the path-to-test map in documentation.
Runs every requested scope once and retains per-scope results and logs. Automation
builds the editor once up front when its configured source hashes are stale,
then runs scopes on one thread per headless_pool_size slot; each fresh scope
world leases and releases its own headless slot, so results finish out of order.
Exclusive waiters and other runs can interleave between scopes; Python/scripts
take no Unreal lock. Each automation lease records scope:<name>:headless in
lock_waits, including uncontended acquisition. Same-worktree builds cannot
replace its module.

Proof limits:
- Python/tool and map-validator scopes prove only their checks; validator images
  go to the run directory. Lint-only scopes need ./x check.
- Rules assertions are world-free even though today's runner starts the editor:
  they cannot prove actors, navigation, replication, rendering or native input.
- World assertions can exercise real actors/navigation/payment/arrival, only
  where asserted. Controlled fixtures are not an unaided new-match playthrough;
  a second controller in one world is not a remote client. Standalone Success
  cannot prove client ownership, replication, hit testing or presentation.

Automation requires the requested map to start, at least one completed test
under the requested filter, every matching result Success, zero process exit
and **** TEST COMPLETE. EXIT CODE: 0 **** from SoftQuit. Unrelated successes,
accepted-order logs or timed exits do not pass. Module/input mutation fails.
Freshness covers configured hash inputs, not arbitrary engine/toolchain changes.
The runner stops a child process group when its watched log is silent or repeats
previous lines for the configured stall interval, ignoring timestamps and polling
counters. Novel log lines are not proof of gameplay progress. Harness predicate
waits have independent deadlines with predicate, elapsed-time and snapshot failures.
Inspect failures with ./x runs <id>, diagnose and rerun the affected scope.
Never weaken assertions or product behavior merely to get green.
"""
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
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
