"""BuildCookRun archives never overwrite the friends' playtest build."""

import argparse
from pathlib import Path
import shutil
from typing import Any

from x import gitinfo
from x.content import playtest
from x.content.packages import CONFIGS, package_executable, publish
from x.context import Context

NAME = "package"
SUMMARY = "Build a Development, Shipping or distributable Steam playtest package."
HELP = (
    "./x package [development|shipping] [--playtest] runs BuildCookRun under the "
    "exclusive lock. Normal maps come from Config/DefaultGame.ini MapsToCook. "
    "Archives live in Saved/Packages/<config>/<run-id>/; latest points to the "
    "last successful package. package.json records run_id, config, commit and "
    "the input content hash. Only the two newest successful packages per variant "
    "are kept; unsuccessful archives are removed. Builds/ is untouched. "
    "--playtest always selects Shipping, cooks Menu, AvailabilityZoneV2 and the "
    "classic AvailabilityZone menu option, and writes a commit-named tar.gz under "
    "Saved/Packages/playtest/<run-id>/ with PLAYTEST.txt controls generated from "
    "README.md and PLAYTEST.sh for test Steam App 480. It passes the supported "
    "UBT ProjectDefine override for project modules; installed precompiled Steam "
    "engine modules are not rebuilt. The staged steam_appid.txt and helper supply "
    "the actual test identity. This is not a release identity or Steam acceptance "
    "proof. Ordinary Development/Shipping latest pointers are unchanged. "
    "./x play --shipping selects the newest fresh Shipping/playtest variant; "
    "a playtest's default smoke map is AvailabilityZoneV2."
)
RECORD = True
UAT_SCRIPT = Path("Engine/Build/BatchFiles/RunUAT.sh")


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("config", nargs="?", choices=CONFIGS, default="development")
    parser.add_argument("--playtest", action="store_true")


def cook(
    ctx: Context, directory: Path, config: str, maps: list[str], is_playtest: bool
) -> int:
    return ctx.exec(
        [
            ctx.settings.engine_root / UAT_SCRIPT,
            "BuildCookRun",
            f"-project={ctx.repo / ctx.settings.project}",
            "-noP4",
            "-platform=Linux",
            f"-clientconfig={config.title()}",
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
            *([playtest.STEAM_UBT_ARGS] if is_playtest else []),
        ],
        log="package",
    )


def prepare_playtest(
    ctx: Context, directory: Path, executable: Path, commit: str, maps: list[str]
) -> tuple[Path, dict[str, Any]]:
    archive = playtest.prepare(
        ctx.repo, directory, executable, ctx.settings.game_target, commit
    )
    return archive, {
        "variant": "playtest",
        "commit": commit,
        "maps": maps,
        "default_map": playtest.DEFAULT_MAP,
        "archive": archive.name,
        "steam_app_id": 480,
        "steam_identity": "staged-appid-and-project-definition",
    }


def remove_unpublished(directory: Path, previous: Path | None) -> None:
    latest = directory.parent / "latest"
    if latest.is_symlink() and latest.readlink() == Path(directory.name):
        latest.unlink()
        if previous is not None:
            latest.symlink_to(previous, target_is_directory=True)
    (directory.parent / ".latest-new").unlink(missing_ok=True)
    if directory.is_dir():
        shutil.rmtree(directory)


def run(args: argparse.Namespace, ctx: Context) -> int:
    if ctx.run is None:
        raise RuntimeError("packaging requires a recorded run")
    is_playtest = getattr(args, "playtest", False)
    config = "shipping" if is_playtest else args.config
    variant = "playtest" if is_playtest else config
    directory = ctx.repo / "Saved/Packages" / variant / ctx.run.id
    maps = list(playtest.MAPS) if is_playtest else ctx.settings.maps()
    if not maps:
        raise ValueError("Config/DefaultGame.ini has no MapsToCook")
    published = False
    latest = directory.parent / "latest"
    with ctx.locks.exclusive():
        previous = latest.readlink() if latest.is_symlink() else None
        try:
            package_hash = ctx.freshness.current_hash("package")
            commit = gitinfo.commit(ctx.repo)
            if is_playtest:
                # Validate authored instructions before launching an expensive cook.
                playtest.controls(ctx.repo)
                package_hash = playtest.input_hash(ctx.repo, package_hash)
            code = cook(ctx, directory, config, maps, is_playtest)
            if code:
                return code
            executable = package_executable(directory, ctx.settings.game_target, config)
            details: dict[str, Any] = {}
            archive: Path | None = None
            if is_playtest:
                archive, details = prepare_playtest(
                    ctx, directory, executable, commit, maps
                )
            current_hash = ctx.freshness.current_hash("package")
            if is_playtest:
                current_hash = playtest.input_hash(ctx.repo, current_hash)
            if current_hash != package_hash or gitinfo.commit(ctx.repo) != commit:
                raise ValueError(
                    "package inputs changed during cooking; run ./x package again"
                )
            publish(
                ctx.repo,
                directory,
                config,
                ctx.run.id,
                package_hash,
                details=details,
                archive=archive,
            )
            published = True
            ctx.run.add_artifact(directory, f"{variant} package")
            if archive is not None:
                ctx.run.add_artifact(archive, "playtest distribution archive")
            print(f"Package: {directory}")
        finally:
            if not published:
                remove_unpublished(directory, previous)
    return 0
