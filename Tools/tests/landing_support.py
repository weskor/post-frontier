"""Disposable full CLI repositories; no test writes to the project repository."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from conftest import git
from x.lint.model import load as load_policy
from x.scopes import load

ROOT = Path(__file__).parents[2]


def copy_exceptions(repo: Path) -> None:
    entries = [
        "[[exceptions]]\n"
        + f"rule = {json.dumps(entry.rule)}\n"
        + f"path = {json.dumps(entry.path)}\n"
        + f"reason = {json.dumps(entry.reason)}\n"
        for entry in load_policy(ROOT).exceptions
        if (repo / entry.path).is_file()
    ]
    (repo / "Tools/x/lint-exceptions.toml").write_text("".join(entries))


def install_runner(repo: Path) -> Path:
    shutil.copytree(
        ROOT / "Tools/x",
        repo / "Tools/x",
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("__pycache__"),
    )
    shutil.copytree(ROOT / "Tools/hooks", repo / "Tools/hooks")
    for name in ("x", "pyproject.toml", "uv.lock", ".clang-format"):
        shutil.copyfile(ROOT / name, repo / name)
    (repo / ".venv").symlink_to(ROOT / ".venv", target_is_directory=True)
    with (repo / ".gitignore").open("a") as ignore:
        ignore.write(".venv/\n__pycache__/\n.mypy_cache/\n.ruff_cache/\n")
    (repo / "Source/rules.cpp").write_text("int initial = 0;\n")
    copy_exceptions(repo)
    settings = repo / "Tools/x/settings.toml"
    settings.write_text(
        settings.read_text()
        .replace("~/.local/state/cooprts/runs", str(repo.parent / "runs"))
        .replace(
            'lock_dir = "/tmp/cooprts-work"', f'lock_dir = "{repo.parent / "locks"}"'
        )
    )
    mapping = load(ROOT)
    definitions = []
    for name in mapping.names():
        if name == "lint":
            definitions.append('[scopes.lint]\nkind = "lint"\n')
        else:
            command = [sys.executable, "validator.py", "{run}", name]
            definitions.append(
                f'[scopes.{name}]\nkind = "script"\n'
                f"commands = [{json.dumps(command)}]\n"
            )
    paths = (ROOT / "Tools/x/scopes.toml").read_text().split("[paths]\n", 1)[1]
    (repo / "Tools/x/scopes.toml").write_text(
        "".join(definitions)
        + "[paths]\n"
        + paths
        + '\n"validator.py" = ["lint"]\n"Source/rules.cpp" = ["lint"]\n'
    )
    (repo / "validator.py").write_text(
        "from pathlib import Path\nimport sys\n"
        "Path(sys.argv[1], sys.argv[2] + '.proof').write_text('executed')\n"
        "sys.exit(int(Path('Docs/fail.md').exists()))\n"
    )
    (repo / "Docs").mkdir()
    (repo / "Docs/task.md").write_text("base\n")
    git(repo, "add", ".")
    git(repo, "commit", "-m", "runner setup")
    task = repo.parent / "task"
    git(repo, "worktree", "add", "-b", "task/acceptance", str(task))
    return task


def seed_evidence(repo: Path) -> None:
    """Arrange an already-cut-over disposable repository without invoking check."""
    from x.landing import LEDGER, LEDGER_REF

    tip = git(repo, "rev-parse", "main")
    (repo / ".git" / LEDGER).write_text(json.dumps({"seed": tip}) + "\n")
    result = subprocess.run(
        ["git", "-C", str(repo), "hash-object", "-w", "--stdin"],
        input=json.dumps({"entries": 1, "new": tip}),
        capture_output=True,
        text=True,
        check=True,
    )
    git(repo, "update-ref", LEDGER_REF, result.stdout.strip())


def invoke(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    environment = {key: value for key, value in os.environ.items() if key != "X_LAND"}
    return subprocess.run(
        [sys.executable, str(repo / "x"), *args],
        cwd=repo,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )


def commit_file(repo: Path, path: str, content: str) -> None:
    target = repo / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)
    git(repo, "add", path)
    git(repo, "commit", "-m", "change " + path)


def git_result(
    repo: Path, *args: str, marker: bool = False
) -> subprocess.CompletedProcess[str]:
    environment = {key: value for key, value in os.environ.items() if key != "X_LAND"}
    if marker:
        environment["X_LAND"] = "1"
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
