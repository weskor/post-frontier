"""Formatting paths survive lint failure and include both language formatters."""

from pathlib import Path

from landing_support import install_runner
from test_lint_run import context
from x import lint


def test_reformatted_paths_exclude_unchanged_and_survive_lint_failure(
    repo: Path,
) -> None:
    task = install_runner(repo)
    changed_python = Path("Tools/x/changed.py")
    unchanged_python = Path("Tools/x/unchanged.py")
    changed_cpp = Path("Source/CoopRTS/Rules/EconomyPolicy.cpp")
    (task / changed_python).write_text("answer=missing\n")
    (task / unchanged_python).write_text("answer = 1\n")
    (task / changed_cpp).parent.mkdir(parents=True, exist_ok=True)
    (task / changed_cpp).write_text("int value(){return 1;}\n")
    result = lint.run(
        context(task), [changed_python, unchanged_python, changed_cpp], fix=True
    )
    assert not result.ok
    assert set(result.reformatted) == {changed_python, changed_cpp}
    assert (task / changed_python).read_text() == "answer = missing\n"
    assert (task / unchanged_python).read_text() == "answer = 1\n"
    second = lint.run(
        context(task), [changed_python, unchanged_python, changed_cpp], fix=True
    )
    assert not second.ok
    assert second.reformatted == ()
