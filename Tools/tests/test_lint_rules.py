"""Seed real violations in disposable copies; keep scanner boundaries honest."""

from pathlib import Path

import pytest
from x.lint import docs, source
from x.lint.model import (
    ExceptionEntry,
    Finding,
    Policy,
    PolicyError,
    apply_exceptions,
    load,
    matches,
    parse_test_only_symbols,
)
from x.lint.model import (
    TestOnlySymbols as SymbolPolicy,
)

CPP_FIXTURE = """namespace
{
int Classify(int value)
{
    return value;
}
}

int Policy::Evaluate(int value)
{
    return Classify(value);
}
"""


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
        ("disabled-test", 'pytest.skip("not available")'),
        ("disabled-test", 'pytest.xfail("expected failure")'),
        ("marker", "// TODO"),
        ("marker", "// FIXME"),
        ("marker", "// HACK"),
        ("marker", "// XXX"),
        ("marker", "raise NotImplementedError"),
        ("marker", "unimplemented()"),
        ("direct-engine", "UnrealEditor"),
        ("direct-engine", "UnrealEditor-Cmd"),
        ("direct-engine", "Build.sh"),
        ("direct-engine", "RunUAT"),
        ("direct-engine", "UnrealBuildTool"),
        ("direct-engine", "GenerateProjectFiles"),
    ],
)
@pytest.mark.parametrize("suffix", [".cpp", ".py"])
def test_seeded_workarounds(tmp_path: Path, rule: str, seed: str, suffix: str) -> None:
    original = (
        CPP_FIXTURE if suffix == ".cpp" else "def policy() -> int:\n    return 1\n"
    )
    copy = tmp_path / f"policy{suffix}"
    text = original + seed + "\n"
    copy.write_text(text)
    findings = source.scan(rule, f"Source/policy{suffix}", copy.read_text())
    assert [(item.rule, item.line) for item in findings] == [
        (rule, len(text.splitlines()))
    ]


@pytest.mark.parametrize(
    "command",
    [
        "UnrealEditor map",
        "UnrealEditor-Cmd map",
        "Engine/Build.sh target",
        "RunUAT BuildCookRun",
        "UnrealBuildTool target",
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
    policy = Policy(
        {}, [ExceptionEntry("marker", "a.py", "genuine false positive")], None
    )
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


def test_cpp_namespace_and_class_inline_functions() -> None:
    assert source.cpp_functions(CPP_FIXTURE) == [(3, 6), (9, 12)]
    text = "class Thing\n{\n int Run()\n {\n  if (true) { return 1; }\n  return 0;\n }\n};\n"
    assert source.cpp_functions(text) == [(3, 7)]
    text = "def run() -> None:\n" + "    pass\n" * 60
    assert [
        item.line for item in source.scan("function-length", "policy.py", text)
    ] == [1]


def test_literals_are_markers_but_commented_includes_are_not_dependencies(
    tmp_path: Path,
) -> None:
    path = tmp_path / "Policy.cpp"
    path.write_text(
        'const char* text = "TODO: an input label";\n// TODO\n'
        '/*\n#include "GameFramework/Actor.h"\n*/\n'
    )
    assert [
        item.line for item in source.scan("marker", "Policy.cpp", path.read_text())
    ] == [1, 2]
    assert source.scan("rules-includes", "Policy.cpp", path.read_text()) == []


SYMBOL_POLICY = SymbolPolicy(
    ("FSimulationSettings", "VerifySession"),
    ("autopilot", "SimSeed", "CoopSteam.Verify"),
)
DEV_GUARD = "WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING"


def symbol_lines(text: str) -> list[int]:
    return [
        finding.line
        for finding in source.scan(
            "test-only-symbol",
            "Source/Policy.cpp",
            text,
            test_only_symbols=SYMBOL_POLICY,
        )
    ]


@pytest.mark.parametrize(
    ("guard", "unsafe"),
    [
        (DEV_GUARD, False),
        ("(WITH_DEV_AUTOMATION_TESTS) && !(UE_BUILD_SHIPPING)", False),
        ("!(!WITH_DEV_AUTOMATION_TESTS || UE_BUILD_SHIPPING)", False),
        (f"({DEV_GUARD}) && PLATFORM_WINDOWS", False),
        (f"({DEV_GUARD}) || 0", False),
        ("WITH_DEV_AUTOMATION_TESTS", True),
        ("!UE_BUILD_SHIPPING", True),
        ("WITH_DEV_AUTOMATION_TESTS || !UE_BUILD_SHIPPING", True),
        (f"({DEV_GUARD}) || PLATFORM_WINDOWS", True),
        ("defined(WITH_DEV_AUTOMATION_TESTS) && !UE_BUILD_SHIPPING", True),
        ("defined WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING", True),
        ("WITH_DEV_AUTOMATION_TESTS && !defined(UE_BUILD_SHIPPING)", True),
        ("WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING == 1", True),
        ("WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING + 0", True),
        ("WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING garbage", True),
        ("WITH_DEV_AUTOMATION_TESTS && (!UE_BUILD_SHIPPING", True),
        ("", True),
    ],
)
def test_test_only_guard_implication(guard: str, unsafe: bool) -> None:
    text = f"#if {guard}\nFSimulationSettings Settings;\n#endif\n"
    assert symbol_lines(text) == ([2] if unsafe else [])


def test_nested_branches_and_sibling_guards_do_not_leak() -> None:
    text = (
        "#if WITH_DEV_AUTOMATION_TESTS\n"  # 1
        "#if UE_BUILD_SHIPPING\n"
        "VerifySession();\n"  # 3: shipping
        "#elif !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"  # 5: protected
        "#else\n"
        "VerifySession();\n"  # 7: unreachable
        "#endif\n"
        "VerifySession();\n"  # 9: dev alone
        "#else\n"
        "#if !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"  # 12: not dev
        "#endif\n"
        "#endif\n"
        "VerifySession();\n"  # 15: no guard
        f"#if {DEV_GUARD}\n"
        "VerifySession();\n"
        "#endif\n"
        "#if !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"  # 20: sibling dev guard is gone
        "#endif\n"
    )
    assert symbol_lines(text) == [3, 9, 12, 15, 20]


def test_elif_and_else_use_prior_branch_negations() -> None:
    text = (
        "#if UE_BUILD_SHIPPING\n"
        "VerifySession();\n"
        "#elif !WITH_DEV_AUTOMATION_TESTS\n"
        "VerifySession();\n"
        "#else\n"
        "VerifySession();\n"
        "#endif\n"
        "#if PLATFORM_WINDOWS\n"
        "VerifySession();\n"
        f"#elif {DEV_GUARD}\n"
        "VerifySession();\n"
        "#else\n"
        "VerifySession();\n"
        "#endif\n"
    )
    assert symbol_lines(text) == [2, 4, 9, 13]


@pytest.mark.parametrize("directive", ["ifdef", "ifndef"])
def test_macro_definedness_does_not_prove_value(directive: str) -> None:
    text = (
        f"#{directive} WITH_DEV_AUTOMATION_TESTS\n"
        "#if !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"
        "#endif\n"
        "#else\n"
        "#if !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"
        "#endif\n"
        "#endif\n"
    )
    assert symbol_lines(text) == [3, 7]


@pytest.mark.parametrize("directive", ["elifdef", "elifndef"])
def test_elif_definedness_transitions_and_directive_uses(directive: str) -> None:
    text = (
        f"#if {DEV_GUARD}\n"
        "VerifySession();\n"
        f"#{directive} VerifySession\n"
        'TEXT("-SimSeed=1");\n'
        "#if PLATFORM_WINDOWS\n"
        "VerifySession();\n"
        "#endif\n"
        f"#elif {DEV_GUARD}\n"
        "VerifySession();\n"
        "#else\n"
        "VerifySession();\n"
        "#endif\n"
        "VerifySession();\n"
        f"#if {DEV_GUARD}\n"
        "#if PLATFORM_WINDOWS\n"
        "VerifySession();\n"
        f"#{directive} WITH_DEV_AUTOMATION_TESTS\n"
        'TEXT("-autopilot");\n'
        "#else\n"
        "VerifySession();\n"
        "#endif\n"
        "#endif\n"
    )
    assert symbol_lines(text) == [3, 4, 6, 11, 13]


@pytest.mark.parametrize("directive", ["elifdef", "elifndef"])
def test_elif_definedness_does_not_prove_value(directive: str) -> None:
    text = (
        "#if 0\n"
        f"#{directive} WITH_DEV_AUTOMATION_TESTS\n"
        "#if !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"
        "#endif\n"
        "#else\n"
        "#if !UE_BUILD_SHIPPING\n"
        "VerifySession();\n"
        "#endif\n"
        "#endif\n"
    )
    assert symbol_lines(text) == [4, 8]


def test_comments_literals_and_token_boundaries() -> None:
    text = (
        f"// #if {DEV_GUARD}\n"
        "VerifySession();\n"
        f"/* #if {DEV_GUARD}\n"
        "VerifySession(); */\n"
        'const char* label = "FSimulationSettings VerifySession";\n'
        'const char* raw = u8R"guard(\n'
        f"#if {DEV_GUARD}\n"
        'VerifySession();\n)guard";\n'
        "FSimulationSettingsSuffix Settings;\n"
        "OtherVerifySession();\n"
        "VerifySession2();\n"
        "VerifySession();\n"
        f'"literal" #if {DEV_GUARD}\n'
        "VerifySession();\n"
    )
    assert symbol_lines(text) == [2, 13, 15]


def test_logical_lines_keep_physical_diagnostic_lines() -> None:
    text = (
        "#if WITH_DEV_AUTOMATION_TESTS && \\\n"
        "!UE_BUILD_SHIPPING\n"
        "VerifySession();\n"
        "#endif\n"
        "// continued comment \\\n"
        f"#if {DEV_GUARD}\n"
        "VerifySession();\n"
        "Verify\\\nSession();\n"
    )
    assert symbol_lines(text) == [7, 8]


@pytest.mark.parametrize(
    ("number", "lines"),
    [("1'000", [4, 5]), ("1'\\\n000", [5, 6])],
)
def test_digit_separator_does_not_hide_guard_end(number: str, lines: list[int]) -> None:
    text = (
        f"#if {DEV_GUARD}\n"
        f"const auto Count = {number};\n"
        "#endif\n"
        "VerifySession();\n"
        'TEXT("-SimSeed=1");\n'
        r"const auto Quote = '\'';"
        "\n"
        f"#if {DEV_GUARD}\n"
        "VerifySession();\n"
        'TEXT("-autopilot");\n'
        "#endif\n"
    )
    assert symbol_lines(text) == lines


@pytest.mark.parametrize("literal", [r"'\''", r"'\\'", r"'\n'", "'\\\nn'"])
def test_escaped_character_literals_preserve_guard_boundaries(literal: str) -> None:
    text = (
        f"const auto Character = {literal};\n"
        "VerifySession();\n"
        f"#if {DEV_GUARD}\n"
        f"const auto Character = {literal};\n"
        "VerifySession();\n"
        "#endif\n"
        'TEXT("-autopilot");\n'
    )
    continued_lines = literal.count("\n")
    assert symbol_lines(text) == [2 + continued_lines, 7 + 2 * continued_lines]


@pytest.mark.parametrize(
    ("literal", "lines"),
    [
        ('TEXT("autopilot")', [1]),
        ('TEXT("-autopilot")', [1]),
        ('TEXT("SimSeed=")', [1]),
        ('TEXT("-SimSeed=123")', [1]),
        ('TEXT("-CoopSteam.Verify=1")', [1]),
        ('u8R"key(-SimSeed=1)key"', [1]),
        ('R"autopilot(ordinary content)autopilot"', []),
        ('TEXT("autopilotMode")', []),
        ('TEXT("NotSimSeed=")', []),
        ('TEXT("Other.CoopSteam.Verify")', []),
        ('TEXT("CoopSteam.VerifyExtra")', []),
        ('TEXT("autopilot-extra")', []),
        ('// TEXT("autopilot")', []),
        ('/* TEXT("SimSeed=") */', []),
        ("autopilot;", []),
    ],
)
def test_configured_flag_boundaries(literal: str, lines: list[int]) -> None:
    assert symbol_lines(literal + "\n") == lines
    assert symbol_lines(f"#if {DEV_GUARD}\n{literal}\n#endif\n") == []


@pytest.mark.parametrize(
    "spec",
    [
        None,
        [],
        {},
        {"identifiers": ["VerifySession"]},
        {"identifiers": ["VerifySession"], "flags": ["autopilot"], "extra": []},
        {"identifiers": [], "flags": ["autopilot"]},
        {"identifiers": "VerifySession", "flags": ["autopilot"]},
        {"identifiers": [1], "flags": ["autopilot"]},
        {"identifiers": ["VerifySession()"], "flags": ["autopilot"]},
        {"identifiers": ["VerifySession", "VerifySession"], "flags": ["autopilot"]},
        {"identifiers": ["VerifySession"], "flags": []},
        {"identifiers": ["VerifySession"], "flags": [False]},
        {"identifiers": ["VerifySession"], "flags": ["-autopilot"]},
        {"identifiers": ["VerifySession"], "flags": ["SimSeed="]},
        {"identifiers": ["VerifySession"], "flags": ["autopilot", "autopilot"]},
    ],
)
def test_invalid_test_only_symbol_settings(spec: object) -> None:
    with pytest.raises(ValueError, match="test-only-symbol"):
        parse_test_only_symbols(spec)


def test_invalid_symbol_settings_surface_as_policy_error(tmp_path: Path) -> None:
    root = Path(__file__).parents[2]
    config = (root / "Tools/x/lint.toml").read_text()
    target = tmp_path / "Tools/x/lint.toml"
    target.parent.mkdir(parents=True)
    target.write_text(config.replace('"FSimulationSettings"', '"not an identifier"'))
    with pytest.raises(PolicyError) as caught:
        load(tmp_path)
    assert caught.value.path == "Tools/x/lint.toml"


def test_condition_symbols_require_an_enclosing_guard() -> None:
    assert symbol_lines(f"#if ({DEV_GUARD}) && VerifySession\n#endif\n") == [1]
    text = (
        f"#if {DEV_GUARD}\n"
        "#if VerifySession\n"
        "FSimulationSettings Settings;\n"
        "#endif\n"
        "#endif\n"
    )
    assert symbol_lines(text) == []
