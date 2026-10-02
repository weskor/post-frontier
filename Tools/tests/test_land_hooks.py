"""Automatic shared hook configuration and local LFS clean/smudge round trip."""

from pathlib import Path

from conftest import git
from landing_support import commit_file, install_runner, invoke


def test_every_cli_invocation_repairs_shared_hooks(repo: Path) -> None:
    task = install_runner(repo)
    for args in (("help",), ("unknown",), ("check", "--invalid")):
        git(task, "config", "core.hooksPath", "wrong/hooks")
        invoke(task, *args)
        assert git(repo, "config", "--get", "core.hooksPath") == "Tools/hooks"
    git(task, "config", "--unset", "core.hooksPath")
    assert invoke(task, "help").returncode == 0
    assert git(repo, "config", "--get", "core.hooksPath") == "Tools/hooks"


def test_lfs_checkout_smudges_with_custom_hooks(repo: Path) -> None:
    task = install_runner(repo)
    assert invoke(task, "help").returncode == 0
    git(task, "lfs", "install", "--local", "--skip-repo")
    git(task, "lfs", "track", "*.dat")
    payload = b"local LFS round trip\x00\x01\n" * 50
    (task / "asset.dat").write_bytes(payload)
    git(task, "add", ".gitattributes", "asset.dat")
    git(task, "commit", "-m", "LFS asset")
    assert "version https://git-lfs.github.com/spec/v1" in git(
        task, "show", "HEAD:asset.dat"
    )
    (task / "asset.dat").unlink()
    git(task, "checkout", "HEAD", "--", "asset.dat")
    assert (task / "asset.dat").read_bytes() == payload
    environment = git(task, "lfs", "env")
    assert 'git config filter.lfs.process = "git-lfs filter-process"' in environment
    assert git(task, "config", "core.hooksPath") == "Tools/hooks"
    commit_file(task, "Docs/lfs.md", "hooks active\n")
    git(task, "switch", "--detach", "HEAD~1")
    git(task, "switch", "task/acceptance")
    assert (task / "asset.dat").read_bytes() == payload
