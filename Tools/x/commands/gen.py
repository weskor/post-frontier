"""Run one registered content tool under the shared exclusive lock."""

import argparse

from x.building import ensure_editor
from x.content.generating import invocation
from x.content.registry import BY_NAME, GENERATORS
from x.context import Context

NAME = "gen"
SUMMARY = "List or run content generators, importers and asset tools."
HELP = (
    "./x gen --list lists all executable Build tools (library modules are not entries). "
    "./x gen <name> runs its canonical arguments; optional script arguments follow --. "
    "./x gen <name> --describe shows its runtime, outputs and ordered pipeline. "
    "All tools hold the exclusive lock. Unreal tools ensure a fresh editor module, "
    "disable MCP auto-start, and use one headless/offscreen flag builder.\n\n"
    + "\n".join(f"{entry.name}: {entry.HELP or entry.purpose}" for entry in GENERATORS)
)
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--list", action="store_true", help="list registry entries")
    parser.add_argument(
        "--describe", action="store_true", help="describe selected entry"
    )
    parser.add_argument("name", nargs="?", choices=sorted(BY_NAME))
    parser.add_argument(
        "extra", nargs=argparse.REMAINDER, help="script arguments after --"
    )


def describe(name: str) -> None:
    entry = BY_NAME[name]
    print(f"{entry.name}: {entry.purpose}")
    print(f"  runtime={entry.runtime} script=Build/{entry.script}")
    print(
        f"  args={entry.arguments} geometry={entry.geometry} offscreen={entry.offscreen}"
    )
    print(f"  outputs={entry.outputs}")
    if entry.HELP:
        print(f"  {entry.HELP}")


def run(args: argparse.Namespace, ctx: Context) -> int:
    if args.list:
        for entry in sorted(GENERATORS, key=lambda item: item.name):
            describe(entry.name)
        return 0
    if args.name is None:
        raise ValueError("select a generator or use ./x gen --list")
    extra = list(args.extra)
    if extra and extra[0] == "--describe":
        args.describe = True
        extra.pop(0)
    describe(args.name)
    if args.describe:
        return 0
    entry = BY_NAME[args.name]
    if entry.runtime == "unreal" and not ensure_editor(ctx):
        return 1
    extra = extra[1:] if extra[:1] == ["--"] else extra
    with ctx.locks.exclusive():
        code = ctx.exec(invocation(entry, ctx, extra), log="generate")
        if entry.runtime == "unreal" and ctx.run is not None:
            text = (ctx.run.dir / "unreal.log").read_text()
            ok = (
                code == 0
                and "LogEditorPythonExecuter: Error:" not in text
                and "LogPython: Error:" not in text
            )
            ctx.run.add_result("generator-script", ok)
            if not ok:
                print(f"Generator failed; inspect {ctx.run.dir / 'unreal.log'}")
                return 1
        return code
