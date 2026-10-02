"""Seed real violations in disposable copies; keep scanner boundaries honest."""

from pathlib import Path

import pytest
from x.lint import docs, source
from x.lint.model import ExceptionEntry, Finding, Policy, apply_exceptions, matches


@pytest.mark.parametrize(
    ("rule", "seed"),
    [
        ("suppression", "// NOLINT"),
        ("suppression", "# noqa"),
        ("suppression", "# type: ignore"),
        ("suppression", "# pyright: ignore"),
        ("suppression", "#if 0"),
        ("suppression", "// clang-format off"),
        ("disabled-test", "EAutomationTestFlags::Disabled"),
        ("disabled-test", "pytest.mark.skip"),
        ("disabled-test", "skipif"),
        ("disabled-test", "pytest.mark.xfail"),
        ("disabled-test", "unittest.skip"),
        ("marker", "// TODO"),
        ("marker", "// FIXME"),
        ("marker", "// HACK"),
        ("marker", "// XXX"),
        ("marker", "raise NotImplementedError"),
        ("marker", "unimplemented()"),
        ("direct-engine", "Unreal" + "Editor"),
        ("direct-engine", "Unreal" + "Editor-Cmd"),
        ("direct-engine", "Build" + ".sh"),
        ("direct-engine", "Run" + "UAT"),
        ("direct-engine", "Unreal" + "BuildTool"),
        ("direct-engine", "Generate" + "ProjectFiles"),
    ],
)
def test_seeded_workarounds(tmp_path: Path, rule: str, seed: str) -> None:
    original = Path(__file__).parents[2] / "Source/CoopRTS/Rules/ProductionPolicy.cpp"
    copy = tmp_path / "policy.cpp"
    text = original.read_text() + seed + "\n"
    copy.write_text(text)
    findings = source.scan(rule, "Source/policy.cpp", copy.read_text())
    assert [(item.rule, item.line) for item in findings] == [
        (rule, len(text.splitlines()))
    ]


@pytest.mark.parametrize(
    "command",
    [
        "Unreal" + "Editor map",
        "Unreal" + "Editor-Cmd map",
        "Engine/Build" + ".sh target",
        "Run" + "UAT BuildCookRun",
        "Unreal" + "BuildTool target",
        "python3 Build/GenerateMap.py",
        "uv run pytest",
        "blender -b asset",
        "verify.py regression",
        "network.py launch",
        "hud_capture.py --quick",
        "network_desktop.py launch",
        "SimulateMatches --count 1",
        "flock /tmp/lock ./x build",
    ],
)
@pytest.mark.parametrize("prefix", ["", "$ ", "X=1 ", "true && ", "true; ", "true | "])
def test_seeded_procedures(tmp_path: Path, command: str, prefix: str) -> None:
    path = tmp_path / "guide.md"
    path.write_text(f"Name `verify.py` in prose.\n```sh\n{prefix}{command}\n```\n")
    assert docs.procedure_lines(path.read_text()) == [3]
    path.write_text(f"Run `{prefix}{command}`.\n")
    assert docs.procedure_lines(path.read_text()) == [1]


def test_doc_citations_and_nonprocedural_names(tmp_path: Path) -> None:
    path = tmp_path / "guide.md"
    path.write_text(
        "`verify.py` refuses to run. `Build/Map.py` is a generator.\n"
        "Evidence: [result](../Saved/Verification/result.md).\n"
    )
    assert docs.scan("procedure-text", "guide.md", path.read_text()) == []
    (finding,) = docs.scan("saved-path", "guide.md", path.read_text())
    assert (finding.path, finding.line, finding.rule) == ("guide.md", 2, "saved-path")


def test_exception_is_exact_and_unused_entries_block() -> None:
    marker = Finding("a.py", 2, "marker", "stub")
    suppression = Finding("a.py", 3, "suppression", "suppressed")
    other = Finding("b.py", 2, "marker", "stub")
    policy = Policy({}, [ExceptionEntry("marker", "a.py", "genuine false positive")])
    assert apply_exceptions([marker, suppression, other], policy) == [
        suppression,
        other,
    ]
    (unused,) = apply_exceptions([], policy)
    assert (unused.path, unused.rule) == ("Tools/x/lint-exceptions.toml", "marker")
    assert "unused exception" in unused.message


def test_globs_do_not_accidentally_enable_frozen_audit() -> None:
    assert matches("Tools/x/lint/model.py", "Tools/x/**/*.py")
    assert matches("Tools/x/cli.py", "Tools/x/**/*.py")
    assert not matches("Docs/Engineering/Audit/tests.md", "Docs/Engineering/*.md")


def test_include_layer_boundary(tmp_path: Path) -> None:
    path = tmp_path / "Policy.h"
    path.write_text(
        '#include "CoreMinimal.h"\n#include "Rules/Other.h"\n'
        '#include "Policy.generated.h"\n#include "GameFramework/Actor.h"\n'
        '#include "Rules/../Actor.h"\n#include "Other.generated.h"\n'
    )
    assert [
        item.line
        for item in source.scan("rules-includes", "Policy.h", path.read_text())
    ] == [4, 5, 6]


def test_size_boundaries_and_cpp_nested_braces(tmp_path: Path) -> None:
    path = tmp_path / "Policy.cpp"
    text = 'UFUNCTION()\nint Test::Run() const\n{\n auto f = []() { return "}"; };\n'
    text += ' // { comment }\n auto raw = R"tag({})tag";\n' + "\n" * 55 + "}\n"
    path.write_text(text)
    spans = source.cpp_functions(path.read_text())
    assert spans == [(2, 62)]
    assert [
        item.line for item in source.scan("function-length", "Policy.cpp", text)
    ] == [2]
    assert (
        source.scan("function-length", "Policy.cpp", text.replace("\n\n", "\n", 1))
        == []
    )
    assert source.scan("file-length", "Policy.cpp", "\n" * 500) == []
    assert [
        item.line for item in source.scan("file-length", "Policy.cpp", "\n" * 501)
    ] == [501]


def test_real_source_cpp_and_class_inline_functions() -> None:
    root = Path(__file__).parents[2]
    text = (root / "Source/CoopRTS/Rules/ProductionPolicy.cpp").read_text()
    assert source.cpp_functions(text) == [(5, 28), (31, 41)]
    text = "class Thing\n{\n int Run()\n {\n  if (true) { return 1; }\n  return 0;\n }\n};\n"
    assert source.cpp_functions(text) == [(3, 7)]
    text = "def run() -> None:\n" + "    pass\n" * 60
    assert [
        item.line for item in source.scan("function-length", "policy.py", text)
    ] == [1]


def test_literals_and_comment_include_examples_are_not_workarounds(
    tmp_path: Path,
) -> None:
    path = tmp_path / "Policy.cpp"
    path.write_text(
        'const char* text = "TODO: an input label";\n// TODO\n'
        '/*\n#include "GameFramework/Actor.h"\n*/\n'
    )
    assert [
        item.line for item in source.scan("marker", "Policy.cpp", path.read_text())
    ] == [2]
    assert source.scan("rules-includes", "Policy.cpp", path.read_text()) == []
