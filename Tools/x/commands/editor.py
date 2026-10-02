"""Hold the exclusive Unreal lock for the owner's whole GUI session."""

import argparse

from x.building import ensure_editor
from x.context import Context

NAME = "editor"
SUMMARY = "Open the fresh editor GUI and hold the exclusive lock until it exits."
HELP = "./x editor [-- extra args]\nBuilds automatically when stale, then opens the project GUI.\nThe exclusive lock keeps headless commands out until the editor exits."
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("extra", nargs=argparse.REMAINDER)


def run(args: argparse.Namespace, ctx: Context) -> int:
    if not ensure_editor(ctx):
        return 1
    extra = args.extra[1:] if args.extra[:1] == ["--"] else args.extra
    with ctx.locks.exclusive():
        return ctx.exec(
            [
                ctx.settings.engine_root / "Engine/Binaries/Linux/UnrealEditor",
                ctx.repo / ctx.settings.project,
                *extra,
            ],
            log="editor",
            stall_seconds=float("inf"),
        )
