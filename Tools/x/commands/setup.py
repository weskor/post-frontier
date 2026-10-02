"""Check local tools and apply the shared engine ShaderPrint patch idempotently."""

import argparse
import shutil
import stat
import subprocess

from x.context import Context

NAME = "setup"
SUMMARY = "Bootstrap Git LFS, check tools and apply the required ShaderPrint patch."
HELP = (
    "Checks the configured engine's UnrealEditor, UnrealEditor-Cmd, Linux Build.sh "
    "and RunUAT.sh, plus git, uv, clang-format and patch on PATH. Under the exclusive "
    "lock, configures local LFS filters with git lfs install --local --skip-repo "
    "(no hook installation; Tools/hooks already chains to Git LFS), then runs "
    "git lfs pull to fetch this checkout's binary content. Repeated setup is safe. "
    "It preserves core.hooksPath and the existing hooks. Next, "
    "checks whether Build/UnrealEngine-5.8.3-ShaderPrint.patch is already applied "
    "with a reverse dry run; otherwise applies it with patch --forward. "
    "This intentionally changes the shared engine shader, never Builds/."
)
RECORD = True


def configure(parser: argparse.ArgumentParser) -> None:
    parser.description = SUMMARY


def run(args: argparse.Namespace, ctx: Context) -> int:
    engine = ctx.settings.engine_root
    for relative in (
        "Engine/Binaries/Linux/UnrealEditor",
        "Engine/Binaries/Linux/UnrealEditor-Cmd",
        "Engine/Build/BatchFiles/Linux/Build.sh",
        "Engine/Build/BatchFiles/RunUAT.sh",
    ):
        if not (engine / relative).is_file():
            raise ValueError(f"missing engine component: {engine / relative}")
    for tool in ("git", "uv", "clang-format", "patch"):
        if shutil.which(tool) is None:
            raise ValueError(f"missing tool on PATH: {tool}")
    print(f"Engine: {engine}; uv and clang-format available")
    patch = ctx.repo / "Build/UnrealEngine-5.8.3-ShaderPrint.patch"
    arguments = ["patch", "--batch", "-d", str(engine), "-p1", "-i", str(patch)]
    with ctx.locks.exclusive():
        code = ctx.exec(
            ["git", "lfs", "install", "--local", "--skip-repo"], log="lfs-install"
        )
        if code != 0:
            return code
        print("Git LFS local filters configured; existing hooks preserved", flush=True)
        code = ctx.exec(["git", "lfs", "pull"], log="lfs-pull")
        if code != 0:
            return code
        print("Git LFS content fetched", flush=True)
        probe = subprocess.run(
            [*arguments, "--reverse", "--dry-run"],
            capture_output=True,
            text=True,
            check=False,
        )
        if probe.returncode == 0:
            print("ShaderPrint patch already applied")
            if ctx.run is not None:
                ctx.run.add_result("shader-patch", True, "already applied")
            return 0
        target = engine / "Engine/Shaders/Private/ShaderPrintCommon.ush"
        target.chmod(target.stat().st_mode | stat.S_IWUSR)
        code = ctx.exec([*arguments, "--forward"], log="shader-patch")
        if code == 0:
            print("ShaderPrint patch applied")
        return code
