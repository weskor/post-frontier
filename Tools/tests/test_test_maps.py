"""Scope overrides must launch and require the selected world, not a fallback."""

from collections.abc import Sequence
from dataclasses import replace
from pathlib import Path

import pytest
from x.building import editor_module
from x.cli import parser_for
from x.commands import test as command
from x.context import Context
from x.runs import Run
from x.settings import load
from x.testing import COMPLETE

BOOT = "/Game/Maps/Boot"
CLASSIC = "/Game/Maps/AvailabilityZone"
SELECTED = "/Game/Maps/AvailabilityZoneV2"


def automation_context(
    repo: Path, worlds: tuple[str, str], monkeypatch: pytest.MonkeyPatch
) -> Context:
    settings = replace(
        load(repo),
        engine_root=repo.parent / "engine",
        lock_dir=repo.parent / "locks",
        runs_root=repo.parent / "runs",
    )
    record = Run(repo, settings.runs_root, "test", ["test"])
    ctx = Context(repo, settings, record, "test")
    (repo / "Tools/x/scopes.toml").write_text(
        '[scopes.home]\nkind="automation"\nfilter="CoopRTS.Rules"\nmap="default"\n'
        '[scopes.away]\nkind="automation"\nfilter="CoopRTS.Construction.Lifecycle"\n'
        f'map="{CLASSIC}"\n[paths]\n'
    )
    observations = dict(zip(("home-unreal", "away-unreal"), worlds, strict=True))
    outcomes = (
        "Test Completed. Result={Success} Path={CoopRTS.Rules.Economy.Affordability}\n"
        "Test Completed. Result={Success} Path={CoopRTS.Construction.Lifecycle}\n"
        f"{COMPLETE}\n"
    )

    def observe(
        self: Context, argv: Sequence[str | Path], *, log: str, watch: Path
    ) -> int:
        world = observations[watch.stem]
        watch.write_text(
            f"Bringing World {world}.{world.rsplit('/', 1)[1]} up for play\n" + outcomes
        )
        assert self.run is not None
        self.run.add_exec(
            [str(arg) for arg in argv], self.run.dir / f"{log}.log", 0, 0.0, False
        )
        return 0

    monkeypatch.setattr(Context, "exec", observe)
    module = editor_module(ctx)
    module.parent.mkdir(parents=True)
    module.write_bytes(b"isolated automation module")
    ctx.freshness.stamp("editor", record.id)
    return ctx


@pytest.mark.parametrize(
    "override,worlds,passed",
    [
        (None, (BOOT, CLASSIC), True),
        (SELECTED, (SELECTED, SELECTED), True),
        (SELECTED, (BOOT, CLASSIC), False),
    ],
)
def test_scope_map_precedence_and_fallback_rejection(
    repo: Path,
    override: str | None,
    worlds: tuple[str, str],
    passed: bool,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    ctx = automation_context(repo, worlds, monkeypatch)
    argv = ["home", "away"] + (["--map", override] if override is not None else [])
    args = parser_for(command).parse_args(argv)
    assert command.run(args, ctx) == (0 if passed else 1)
    assert ctx.run is not None
    assert [item["argv"][2] for item in ctx.run.record["execs"]] == (
        [override, override] if override is not None else [BOOT, CLASSIC]
    )
    assert [item["ok"] for item in ctx.run.record["results"]] == [passed, passed]
    if not passed:
        assert all(
            f"requested map {SELECTED} did not start" in item["details"]
            for item in ctx.run.record["results"]
        )


@pytest.mark.parametrize(
    "value",
    [
        "Maps/Boot",
        "/Engine/Maps/Boot",
        "/Game/Maps/Boot.umap",
        "/Game/Maps/Boot?listen",
        "/Game/Maps/../Boot",
        "/Game/Maps/Boot/",
        "/Game/Maps/Boot extra",
    ],
)
def test_map_argument_rejects_non_package_paths(value: str) -> None:
    with pytest.raises(SystemExit) as failure:
        parser_for(command).parse_args(["rules", "--map", value])
    assert failure.value.code == 2
