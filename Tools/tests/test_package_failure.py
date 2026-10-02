"""Failed cooks and invalid archives must not consume package storage indefinitely."""

import argparse
from dataclasses import replace
from pathlib import Path

import pytest
from x.commands import package
from x.context import Context
from x.runs import Run
from x.settings import load


@pytest.mark.parametrize("exit_code", [3, 0])
def test_failed_archive_is_removed_without_touching_published_package(
    repo: Path, tmp_path: Path, exit_code: int
) -> None:
    settings = replace(
        load(repo),
        engine_root=tmp_path / "engine",
        lock_dir=tmp_path / "locks",
        runs_root=tmp_path / "runs",
    )
    cooker = settings.engine_root / package.UAT_SCRIPT
    cooker.parent.mkdir(parents=True)
    cooker.write_text(
        "#!/bin/sh\nfor argument do\n"
        '  case "$argument" in -archivedirectory=*) directory=${argument#*=};; esac\n'
        'done\nmkdir -p "$directory/Linux"\n'
        'printf partial > "$directory/Linux/partial.pak"\n'
        'echo "Cooker produced an incomplete archive"\n'
        f"exit {exit_code}\n"
    )
    cooker.chmod(0o755)
    root = repo / "Saved/Packages/development"
    previous = root / "previous-success"
    previous.mkdir(parents=True)
    (previous / "package.json").write_text('{"run_id": "previous-success"}\n')
    (previous / "game.pak").write_text("published content")
    (root / "latest").symlink_to(previous.name, target_is_directory=True)
    record = Run(repo, settings.runs_root, "package", ["package", "development"])
    ctx = Context(repo, settings, record, "package")
    args = argparse.Namespace(config="development")
    if exit_code:
        assert package.run(args, ctx) == exit_code
    else:
        with pytest.raises(ValueError, match="no unique CoopRTS executable"):
            package.run(args, ctx)
    assert not (root / record.id).exists()
    assert (previous / "game.pak").read_text() == "published content"
    assert (root / "latest").resolve() == previous
