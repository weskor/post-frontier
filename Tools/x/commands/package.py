"""BuildCookRun archives never overwrite the friends' playtest build."""

import argparse

from x.content.packages import CONFIGS, package_executable, publish
from x.context import Context

NAME = "package"
SUMMARY = "Build and cook a run-specific Development or Shipping package."
HELP = (
    "./x package [development|shipping] runs BuildCookRun under the exclusive lock. "
    "Maps come from Config/DefaultGame.ini MapsToCook. Archives live in "
    "Saved/Packages/<config>/<run-id>/; latest points to the last successful package. "
    "package.json records run_id, config, commit and the input content hash. "
    "Only the two newest successful packages per config are kept. Builds/ is untouched."
)
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("config", nargs="?", choices=CONFIGS, default="development")


def run(args: argparse.Namespace, ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("packaging requires a recorded run")
    directory = ctx.repo / "Saved/Packages" / args.config / ctx.run.id
    maps = ctx.settings.maps()
    if not maps:
        raise ValueError("Config/DefaultGame.ini has no MapsToCook")
    with ctx.locks.exclusive():
        package_hash = ctx.freshness.current_hash("package")
        code = ctx.exec(
            [
                ctx.settings.engine_root / "Engine/Build/BatchFiles/RunUAT.sh",
                "BuildCookRun",
                f"-project={ctx.repo / ctx.settings.project}",
                "-noP4",
                "-platform=Linux",
                f"-clientconfig={args.config.title()}",
                "-build",
                "-cook",
                f"-map={'+'.join(maps)}",
                "-stage",
                "-pak",
                "-iostore",
                "-package",
                "-archive",
                f"-archivedirectory={directory}",
                "-unattended",
                "-utf8output",
            ],
            log="package",
        )
        if code:
            return code
        package_executable(directory, ctx.settings.game_target, args.config)
        if ctx.freshness.current_hash("package") != package_hash:
            raise ValueError(
                "package inputs changed during cooking; run ./x package again"
            )
        publish(ctx.repo, directory, args.config, ctx.run.id, package_hash)
        ctx.run.add_artifact(directory, f"{args.config} package")
        print(f"Package: {directory}")
    return 0
