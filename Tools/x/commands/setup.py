"""Check local tools and apply the shared engine ShaderPrint patch idempotently."""

import argparse
import shutil
import stat
import subprocess

from x.context import Context

NAME = "setup"
SUMMARY = "Check the engine and tools; apply the required ShaderPrint patch."
HELP = (
    "Checks the configured engine's UnrealEditor, UnrealEditor-Cmd, Linux Build.sh "
    "and RunUAT.sh, plus uv and clang-format on PATH. Under the exclusive lock, "
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
    for tool in ("uv", "clang-format", "patch"):
        if shutil.which(tool) is None:
            raise ValueError(f"missing tool on PATH: {tool}")
    print(f"Engine: {engine}; uv and clang-format available")
    patch = ctx.repo / "Build/UnrealEngine-5.8.3-ShaderPrint.patch"
    arguments = ["patch", "--batch", "-d", str(engine), "-p1", "-i", str(patch)]
    with ctx.locks.exclusive():
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
