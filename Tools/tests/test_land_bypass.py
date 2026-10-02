"""The bypass policy covers executable snippets, docs and non-main boundaries."""

import pytest
from x.lint import source


@pytest.mark.parametrize(
    "path", ["Tools/unsafe.py", "Docs/guide.md", "script.sh", "config.toml"]
)
@pytest.mark.parametrize(
    "snippet",
    [
        "git -c core.hooksPath=/dev/null commit",
        "git commit --no-verify",
        "git update-ref refs/heads/main HEAD",
        "git update-ref main HEAD",
        'subprocess.run(["git", "update-ref", "refs/heads/main", "HEAD"])',
        'subprocess.run(["git", "update-ref",\n "refs/heads/main", "HEAD"])',
        'Path(".git/refs/heads/main").write_text(tip)',
        'echo "$tip" > .git/refs/heads/main',
        'Path("x-land-grant.json").write_text(data)',
        'Path("x-land-ledger.jsonl").unlink()',
    ],
)
def test_bypass_snippets_block(path: str, snippet: str) -> None:
    findings = source.scan("land-bypass", path, "safe\n" + snippet + "\n")
    assert [(item.path, item.rule, item.line) for item in findings] == [
        (path, "land-bypass", 2)
    ]


@pytest.mark.parametrize(
    "snippet",
    [
        "git update-ref refs/heads/task/example HEAD",
        "git update-ref refs/custom/allowed HEAD",
        "git update-ref refs/heads/mainly HEAD",
        "Run ./x land.",
    ],
)
def test_other_refs_and_normal_landing_allowed(snippet: str) -> None:
    assert source.scan("land-bypass", "Docs/guide.md", snippet) == []


@pytest.mark.parametrize(
    "path",
    [
        "Tools/x/landing.py",
        "Tools/hooks/reference-transaction",
        "Tools/tests/test_land_strict.py",
        "Tools/tests/landing_support.py",
    ],
)
def test_enforcement_and_its_tests_are_exempt(path: str) -> None:
    assert (
        source.scan("land-bypass", path, "git -c core.hooksPath=/dev/null commit") == []
    )


@pytest.mark.parametrize(
    "path", ["Tools/test_land_helper.py", "Tools/tests/test_landscape.py"]
)
def test_unrelated_land_named_files_are_not_exempt(path: str) -> None:
    assert source.scan("land-bypass", path, "git commit --no-verify")
