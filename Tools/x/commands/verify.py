"""The only executable entry point for slow verification harnesses."""

import argparse
import importlib
import sys
from pathlib import Path
from types import ModuleType

from x.building import ensure_editor
from x.context import Context
from x.verifying.artifacts import package_snapshot
from x.verifying.cleanup import cleanup
from x.verifying.sessions import lease

NAME = "verify"
SUMMARY = "Run network, offscreen HUD or owned native/desktop verification."
HELP = """./x verify network|hud|native|desktop --help
Network proves a selected loopback replicated contract, not OS input, Steam or WAN.
HUD proves offscreen presentation and shared hit geometry, not compositor input.
Native/desktop provide owned packaged input/capture evidence, not automatic assertions
or human coordination. Desktop sessions retain the exclusive lock across commands:
launch, doctor/focus, input/capture, stop. Actions use this worktree's active session.
Every invocation owns a run record; session actions append evidence to the launch run.
"""
RECORD = True
SCRIPTS = {
    "network": "network",
    "hud": "hud_capture",
    "native": "verify",
    "desktop": "network_desktop",
}
LIMITS = {
    "network": "Replicated slice over loopback; not OS input, rendering, Steam or WAN.",
    "hud": "Offscreen HUD/hit geometry; not OS input, focus or unrequested resolutions.",
    "native": "Owned packaged input and captures; not automatic gameplay assertions, replication or WAN.",
    "desktop": "Owned host/client input and captures; not automatic replicated assertions, Steam, WAN or human coordination.",
}


def harness_module(name: str) -> ModuleType:
    directory = Path(__file__).resolve().parents[2] / "harness"
    sys.path.insert(0, str(directory))
    try:
        return importlib.import_module(SCRIPTS[name])
    finally:
        sys.path.remove(str(directory))


def configure(parser: argparse.ArgumentParser) -> None:
    commands = parser.add_subparsers(dest="harness", required=True)
    for name in SCRIPTS:
        child = commands.add_parser(
            name,
            help=LIMITS[name],
            description=LIMITS[name],
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        harness_module(name).configure(child)
        child.epilog = (child.epilog or "") + "\n" + LIMITS[name]


def execute(args: argparse.Namespace, ctx: Context, folder: Path) -> int:
    if ctx.run is None:
        raise RuntimeError("verification requires a recorded command")
    argv = ctx.run.record["argv"][2:]
    script = ctx.repo / "Tools/harness" / f"{SCRIPTS[args.harness]}.py"
    code = 1
    try:
        code = ctx.exec(
            [sys.executable, script, *argv],
            log=f"verify-{args.harness}",
            env={"PYTHONPATH": str(ctx.repo / "Tools"), "X_HARNESS_DIR": str(folder)},
        )
    finally:
        action = getattr(args, "command", getattr(args, "action", ""))
        if code and (args.harness in ("network", "hud") or action == "launch"):
            cleanup(folder)
    ctx.run.add_artifact(folder, f"{args.harness} evidence")
    ctx.run.add_result(f"verify-{args.harness}", code == 0)
    return code


def run(args: argparse.Namespace, ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("verification requires a recorded command")
    folder = ctx.run.dir / "harness"
    if args.harness in ("native", "desktop"):
        action = args.command if args.harness == "native" else args.action
        if action == "launch":
            package_snapshot(ctx.repo)
        with lease(ctx, args.harness, action, folder) as session:
            return execute(args, ctx, session)
    if args.mode == "editor" and not ensure_editor(ctx):
        return 1
    exclusive = args.harness == "network" and args.clients > 0
    with ctx.locks.exclusive() if exclusive else ctx.locks.headless():
        if args.mode == "packaged":
            package_snapshot(ctx.repo)
        return execute(args, ctx, folder)
