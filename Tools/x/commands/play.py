"""Launch a content-hash-verified package while holding the exclusive lock."""

import argparse
from pathlib import Path

from x.content.packages import latest_package
from x.context import Context

NAME = "play"
SUMMARY = "Launch the latest fresh package, offline by default."
HELP = (
    "./x play [--shipping] [--map /Game/Maps/Boot] [--steam] [-- extra] "
    "holds the exclusive lock until the game exits. Missing/stale packages refuse "
    "with run ./x package. Without --steam, -nosteam disables Steam. --steam sets "
    "SteamAppId=480, SteamGameId=480 and preloads the documented Steam overlay renderer; "
    "the Steam client must be logged in. Extra arguments can select window size, "
    "listen/client URLs or an explicit SDL backend. For headless smoke use "
    "./x play --map /Game/Maps/Boot -- -nullrhi -ExecCmds=Quit."
)
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--shipping", action="store_true")
    parser.add_argument("--map", dest="map")
    parser.add_argument("--steam", action="store_true")
    parser.add_argument("extra", nargs=argparse.REMAINDER)


def run(args: argparse.Namespace, ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("play requires a recorded run")
    config = "shipping" if args.shipping else "development"
    extra = list(args.extra)
    extra = extra[1:] if extra[:1] == ["--"] else extra
    env = {}
    if args.steam:
        env = {
            "SteamAppId": "480",
            "SteamGameId": "480",
            "LD_PRELOAD": str(
                Path.home() / ".local/share/Steam/ubuntu12_64/gameoverlayrenderer.so"
            ),
        }
    with ctx.locks.exclusive():
        executable = latest_package(ctx.repo, config, ctx.settings.game_target)
        argv: list[str | Path] = [executable]
        if args.map:
            argv.append(args.map)
        if not args.steam:
            argv.append("-nosteam")
        argv.extend([*extra, f"-abslog={ctx.run.dir / 'game.log'}"])
        return ctx.exec(argv, log="play", env=env, cwd=executable.parent)
