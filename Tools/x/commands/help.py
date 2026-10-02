"""List command modules or display one command's procedure reference."""

import argparse
import sys

from x.context import Context

NAME = "help"
SUMMARY = "List commands or show a command's procedure and arguments."
HELP = "Run ./x help to list all commands. Run ./x help <command> for its procedure and argument usage."
RECORD = False


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("command", nargs="?", help="command whose procedure to display")


def run(args: argparse.Namespace, ctx: Context) -> int:
    from x.cli import discover, parser_for

    commands = discover()
    if args.command is None:
        print("Usage: ./x <command> [args]\n\nCommands:")
        width = max(len(name) for name in commands)
        for name, command in sorted(commands.items()):
            print(f"  {name:<{width}}  {command.SUMMARY}")
        return 0
    if args.command not in commands:
        print(f"unknown command: {args.command}; use ./x help", file=sys.stderr)
        return 2
    command = commands[args.command]
    print(command.HELP)
    print()
    parser_for(command).print_help()
    return 0
