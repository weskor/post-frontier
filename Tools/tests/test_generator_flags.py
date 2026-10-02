"""Unreal modes must not disable rendering for shader compilation or omit geometry."""

from pathlib import Path

import pytest
from x.content.generating import MCP_DISABLED, unreal_flags
from x.content.registry import Generator


@pytest.mark.parametrize("geometry", [False, True])
@pytest.mark.parametrize("offscreen", [False, True])
def test_generator_modes_are_exclusive_and_mcp_never_autostarts(
    geometry: bool, offscreen: bool, tmp_path: Path
) -> None:
    entry = Generator(
        "fixture",
        "unreal",
        "fixture.py",
        "asset",
        "purpose",
        geometry=geometry,
        offscreen=offscreen,
    )
    flags = unreal_flags(entry, tmp_path / "fixture.py", tmp_path / "unreal.log")
    assert ("-nullrhi" in flags) != ("-RenderOffscreen" in flags)
    assert ("-RenderOffscreen" in flags) == offscreen
    plugins = next(
        flag.split("=", 1)[1].split(",")
        for flag in flags
        if flag.startswith("-EnablePlugins=")
    )
    assert "PythonScriptPlugin" in plugins
    assert ("GeometryScripting" in plugins) == geometry
    assert MCP_DISABLED in flags
    assert "-nosteam" in flags and "-unattended" in flags
