"""Discover command modules and own each invocation's evidence lifecycle."""

import argparse
import importlib
from pathlib import Path
import pkgutil
import sys
from typing import Protocol, Sequence, cast

from x.context import Context
from x.runs import Run
from x.settings import load


class Command(Protocol):
    NAME: str
    SUMMARY: str
    HELP: str
    RECORD: bool

    def configure(self, parser: argparse.ArgumentParser) -> None: ...
    def run(self, args: argparse.Namespace, ctx: Context) -> int: ...


def discover(package_name: str = "x.commands") -> dict[str, Command]:
    package = importlib.import_module(package_name)
    commands: dict[str, Command] = {}
    for item in sorted(
        pkgutil.iter_modules(package.__path__), key=lambda module: module.name
    ):
        if item.name == "__init__":
            continue
        command = cast(Command, importlib.import_module(f"{package_name}.{item.name}"))
        if command.NAME in commands:
            raise ValueError(f"duplicate command name: {command.NAME}")
        commands[command.NAME] = command
    return commands


def parser_for(command: Command) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=f"./x {command.NAME}", description=command.SUMMARY, allow_abbrev=False
    )
    command.configure(parser)
    return parser


def invoke(
    command: Command, args: argparse.Namespace, repo: Path, argv: list[str]
) -> int:
    settings = load(repo)
    record = (
        Run(repo, settings.runs_root, command.NAME, argv) if command.RECORD else None
    )
    ctx = Context(repo, settings, record, command.NAME)
    interrupted = False
    try:
        code = command.run(args, ctx)
    except KeyboardInterrupt:
        code, interrupted = 1, True
    except Exception as error:
        print(f"{command.NAME}: {type(error).__name__}: {error}", file=sys.stderr)
        code = 1
    code = 0 if code == 0 else 2 if code == 2 else 1
    return record.finish(code, interrupted=interrupted) if record is not None else code


def main(argv: Sequence[str]) -> int:
    repo = Path(__file__).resolve().parents[2]
    arguments = list(argv) or ["help"]
    try:
        commands = discover()
        name = arguments[0]
        if name in ("-h", "--help"):
            name, arguments = "help", ["help"]
        if name not in commands:
            print(f"unknown command: {name}; use ./x help", file=sys.stderr)
            return 2
        try:
            args = parser_for(commands[name]).parse_args(arguments[1:])
        except SystemExit as error:
            return int(error.code) if isinstance(error.code, int) else 2
        return invoke(commands[name], args, repo, arguments)
    except KeyboardInterrupt:
        return 1
    except Exception as error:
        print(f"x: {type(error).__name__}: {error}", file=sys.stderr)
        return 1
