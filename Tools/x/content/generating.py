"""Canonical Unreal generator invocation, including explicit MCP auto-start override."""

from pathlib import Path

from x.content.registry import Generator
from x.context import Context

MCP_DISABLED = (
    "-ini:EditorPerProjectUserSettings:"
    "[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False"
)


def unreal_flags(entry: Generator, script: Path, log: Path) -> list[str]:
    plugins = "PythonScriptPlugin" + (",GeometryScripting" if entry.geometry else "")
    return [
        f"-EnablePlugins={plugins}",
        f"-ExecutePythonScript={script}",
        "-unattended",
        "-nosplash",
        "-nosound",
        "-nosteam",
        "-RenderOffscreen" if entry.offscreen else "-nullrhi",
        MCP_DISABLED,
        f"-abslog={log}",
    ]


def invocation(entry: Generator, ctx: Context, extra: list[str]) -> list[str | Path]:
    script = ctx.repo / "Build" / entry.script
    arguments = [*entry.arguments, *extra]
    if entry.runtime == "unreal":
        if ctx.run is None:
            raise RuntimeError("generator requires a recorded run")
        return [
            ctx.settings.engine_root / "Engine/Binaries/Linux/UnrealEditor-Cmd",
            ctx.repo / ctx.settings.project,
            *unreal_flags(entry, script, ctx.run.dir / "unreal.log"),
            *arguments,
        ]
    if entry.runtime == "blender":
        return ["blender", "-b", "--factory-startup", "-P", script, "--", *arguments]
    if entry.runtime in ("uv", "uv/reaper"):
        return ["uv", "run", "--script", script, *arguments]
    return ["python3", script, *arguments]
