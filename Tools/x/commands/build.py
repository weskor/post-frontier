"""Build the editor module using shared hash freshness and locks."""

import argparse

from x.building import ensure_editor
from x.context import Context

NAME = "build"
SUMMARY = "Build the editor target when its inputs have changed."
HELP = "./x build\nBuilds Linux Development with -WaitMutex under the headless lock.\nUnchanged inputs print 'editor up to date'; tests build automatically."
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.set_defaults()


def run(args: argparse.Namespace, ctx: Context) -> int:
    return 0 if ensure_editor(ctx) else 1
