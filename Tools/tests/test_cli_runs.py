"""Discovery, invocation failures, usage errors and evidence lifecycle."""

import argparse
from dataclasses import replace
import importlib
from pathlib import Path
from types import ModuleType
from typing import cast

import pytest

from x import cli, jsonio
from x.commands import runs
from x.context import Context
from x.runs import Run, recent
from x.settings import load


def command() -> ModuleType:
    module = ModuleType("test_command")
    for name, value in (
        ("NAME", "probe"),
        ("SUMMARY", "summary"),
        ("HELP", "procedure"),
        ("RECORD", True),
    ):
        setattr(module, name, value)
    return module


def test_discovery_adds_module_without_registry_edit(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    package = tmp_path / "temporary_commands"
    package.mkdir()
    (package / "__init__.py").write_text("")
    monkeypatch.syspath_prepend(str(tmp_path))
    assert cli.discover("temporary_commands") == {}
    (package / "extra.py").write_text(
        'NAME = "extra"\nSUMMARY = "extra summary"\nHELP = "extra procedure"\nRECORD = False\n'
        'def configure(parser):\n    parser.add_argument("value")\n'
        "def run(args, ctx):\n    return int(args.value)\n"
    )
    importlib.invalidate_caches()
    added = cli.discover("temporary_commands")["extra"]
    args = cli.parser_for(added).parse_args(["0"])
    assert added.NAME == "extra" and args.value == "0"


@pytest.mark.parametrize(
    "mode,expected,code",
    [
        ("pass", "passed", 0),
        ("fail", "failed", 1),
        ("error", "failed", 1),
        ("exit", "failed", 1),
        ("interrupt", "interrupted", 1),
    ],
)
def test_invocation_lifecycle(
    repo: Path, monkeypatch: pytest.MonkeyPatch, mode: str, expected: str, code: int
) -> None:
    settings = replace(
        load(repo), runs_root=repo.parent / "runs", lock_dir=repo.parent / "locks"
    )
    monkeypatch.setattr(cli, "load", lambda root: settings)
    module = command()

    def execute(args: argparse.Namespace, ctx: Context) -> int:
        assert ctx.run is not None
        running = jsonio.load(ctx.run.dir / "record.json")
        assert running["status"] == "running" and running["finished"] is None
        ctx.run.add_artifact(repo / "CoopRTS.uproject", "project")
        if mode == "interrupt":
            raise KeyboardInterrupt
        if mode == "error":
            raise RuntimeError("command failure")
        if mode == "exit":
            return 7
        ctx.run.add_result("observed", mode == "pass", "result detail", 0.1)
        return 0

    setattr(module, "run", execute)
    assert (
        cli.invoke(cast(cli.Command, module), argparse.Namespace(), repo, ["probe"])
        == code
    )
    record = recent(settings.runs_root)[0]
    assert record["status"] == expected and record["exit_code"] == code
    assert record["finished"] is not None and record["duration_s"] >= 0
    assert record["branch"] == "main" and not record["dirty"]
    assert record["artifacts"] == [
        {"path": str(repo / "CoopRTS.uproject"), "label": "project"}
    ]


def test_runs_history_and_detail(
    repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    settings = replace(
        load(repo), runs_root=repo.parent / "runs", lock_dir=repo.parent / "locks"
    )
    ctx = Context(repo, settings)
    assert runs.run(argparse.Namespace(id=None), ctx) == 0
    assert "No runs recorded." in capsys.readouterr().out
    record = Run(repo, settings.runs_root, "sample", ["sample"])
    record.add_exec(["child"], record.dir / "child.log", 0, 0.25, False)
    record.finish(0)
    assert runs.run(argparse.Namespace(id=record.id), ctx) == 0
    output = capsys.readouterr().out
    assert "status: passed" in output and str(record.dir / "child.log") in output
    assert runs.run(argparse.Namespace(id="../escape"), ctx) == 2
    assert runs.run(argparse.Namespace(id="missing"), ctx) == 1


def test_usage_errors_do_not_record(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(
        cli, "load", lambda repo: (_ for _ in ()).throw(AssertionError("must not load"))
    )
    assert cli.main(["unknown"]) == 2
    assert cli.main(["runs", "--invalid"]) == 2
