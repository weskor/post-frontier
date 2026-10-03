"""Discovery, invocation failures, usage errors and evidence lifecycle."""

import argparse
from dataclasses import replace
import importlib
import json
from pathlib import Path
import sys
from types import ModuleType
from typing import cast

from conftest import git
import pytest
from test_content_packages import make_package
from x import cli, jsonio, source
from x.commands import play, runs
from x.context import Context
from x.runs import Run, recent
from x.settings import load
from x.testing import run_scopes


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

    monkeypatch.setattr(module, "run", execute, raising=False)
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
    record.add_exec(["child"], record.dir / "child.log", 0, 0.25, False, 12.5)
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


@pytest.mark.parametrize("shipping", [False, True])
@pytest.mark.parametrize(
    "extra",
    [
        ["-ExecCmds=Quit"],
        ["-eXeCcMdS=Quit"],
        ["-ExecCmds", "Quit"],
        ["-EXECCMDS", "Quit"],
    ],
)
def test_smoke_console_commands_refuse_before_locks_or_package_lookup(
    repo: Path,
    monkeypatch: pytest.MonkeyPatch,
    shipping: bool,
    extra: list[str],
) -> None:
    settings = replace(
        load(repo), runs_root=repo.parent / "runs", lock_dir=repo.parent / "locks"
    )
    monkeypatch.setattr(cli, "load", lambda root: settings)
    argv = ["--smoke", *(["--shipping"] if shipping else []), "--", *extra]
    command = cast(cli.Command, play)
    args = cli.parser_for(command).parse_args(argv)
    # Neither tier has a package: invalid smoke input must win over that error.
    assert cli.invoke(command, args, repo, ["play", *argv]) == 1
    assert not settings.lock_dir.exists()
    record = recent(settings.runs_root)[0]
    assert record["status"] == "failed" and record["execs"] == []


def test_smoke_console_commands_refuse_a_fresh_package_without_launching(
    repo: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    settings = replace(
        load(repo), runs_root=repo.parent / "runs", lock_dir=repo.parent / "locks"
    )
    monkeypatch.setattr(cli, "load", lambda root: settings)
    directory = make_package(repo, "20261003-100000-package-play")
    executable = directory / "Linux/CoopRTS/Binaries/Linux/CoopRTS"
    marker = repo.parent / "launched"
    executable.write_text(
        f"#!{sys.executable}\n"
        "from pathlib import Path\n"
        f"Path({str(marker)!r}).write_text('launched')\n"
    )
    executable.chmod(0o755)
    command = cast(cli.Command, play)
    argv = ["--smoke", "--", "-ExecCmds=Quit"]
    args = cli.parser_for(command).parse_args(argv)
    assert cli.invoke(command, args, repo, ["play", *argv]) == 1
    assert not marker.exists()
    assert not settings.lock_dir.exists()


@pytest.mark.parametrize("exit_code", [0, 1])
def test_run_content_comparison_is_not_pass_or_new_evidence(
    repo: Path, capsys: pytest.CaptureFixture[str], exit_code: int
) -> None:
    settings = replace(load(repo), runs_root=repo.parent / "runs")
    ctx = Context(repo, settings)
    (repo / "Source/rules.cpp").write_text("tested dirty bytes\n")
    recorded = Run(repo, settings.runs_root, "check", ["check"])
    recorded.finish(exit_code)
    before = (recorded.dir / "record.json").read_bytes()
    git(repo, "add", "Source/rules.cpp")
    git(repo, "commit", "-m", "tested bytes")
    args = argparse.Namespace(id=recorded.id, compare_current=True)
    assert runs.run(args, ctx) == 0
    output = capsys.readouterr().out
    assert "completed source vs current: equal" in output
    assert "content equality is not acceptance/PASS" in output
    assert f"run status: {'passed' if exit_code == 0 else 'failed'}" in output
    (repo / "Source/rules.cpp").write_text("different input\n")
    assert runs.run(args, ctx) == 1
    assert "completed source vs current: different" in capsys.readouterr().out
    assert len(recent(settings.runs_root)) == 1
    assert (recorded.dir / "record.json").read_bytes() == before


def test_legacy_comparison_remains_unknown_and_inspectable(
    repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    settings = replace(load(repo), runs_root=repo.parent / "runs")
    ctx = Context(repo, settings)
    recorded = Run(repo, settings.runs_root, "old", ["old"])
    recorded.finish(0)
    recorded.record.pop("source")
    recorded.save()
    before = (recorded.dir / "record.json").read_bytes()
    assert runs.run(argparse.Namespace(id=recorded.id, compare_current=True), ctx) == 1
    assert "completed source vs current: unknown" in capsys.readouterr().out
    assert runs.run(argparse.Namespace(id=recorded.id, compare_current=False), ctx) == 0
    assert "status: passed" in capsys.readouterr().out
    assert (recorded.dir / "record.json").read_bytes() == before
    assert runs.run(argparse.Namespace(id=None, compare_current=True), ctx) == 2


def test_mutating_execution_does_not_test_completed_tree(
    repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    settings = replace(load(repo), runs_root=repo.parent / "runs")
    recorded = Run(repo, settings.runs_root, "gen", ["gen"])
    ctx = Context(repo, settings, recorded)
    assert (
        ctx.exec(
            [
                sys.executable,
                "-c",
                "from pathlib import Path; Path('Source/rules.cpp').write_text('generated input\\n')",
            ],
            log="generate",
        )
        == 0
    )
    recorded.finish(0)
    interval = recorded.record["execs"][0]["source"]
    assert interval["content"] == "different"
    assert recorded.record["source"]["initial_to_completed"] == "different"
    assert runs.run(argparse.Namespace(id=recorded.id, compare_current=True), ctx) == 0
    output = capsys.readouterr().out
    assert "initial to completed: different" in output
    assert "exec:generate:before vs current: different" in output
    assert "exec:generate:after vs current: equal" in output


def test_scope_provenance_starts_after_prior_formatting(repo: Path) -> None:
    command = [
        sys.executable,
        "-c",
        "from pathlib import Path; assert Path('Source/rules.cpp').read_text() == 'formatted'",
    ]
    (repo / "Tools/x/scopes.toml").write_text(
        '[scopes.probe]\nkind="script"\ncommands='
        + json.dumps([command])
        + "\n[paths]\n"
    )
    settings = replace(load(repo), runs_root=repo.parent / "runs")
    path = repo / "Source/rules.cpp"
    path.write_text("unformatted")
    recorded = Run(repo, settings.runs_root, "check", ["check"])
    ctx = Context(repo, settings, recorded)
    path.write_text("formatted")
    assert run_scopes(ctx, ["probe"])
    recorded.finish(0)
    provenance = recorded.record["source"]
    snapshots = provenance["snapshots"]
    result = recorded.record["results"][0]
    assert result["source"]["execs"] == [0]
    assert result["source"]["content"] == "equal"
    assert (
        source.equality(
            snapshots[provenance["initial"]], snapshots[result["source"]["before"]]
        )
        == "different"
    )
    assert (
        source.equality(
            snapshots[provenance["completed"]], snapshots[result["source"]["before"]]
        )
        == "equal"
    )
