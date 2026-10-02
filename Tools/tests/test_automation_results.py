"""Explicit Unreal results, not process survival or unrelated successes, prove a pass."""

import pytest
from x.testing import COMPLETE, parse_automation

# Verbatim excerpts from the 2026-10-02 unity-off rules/construction logs.
WORLD = "[2026.10.02-10.18.18:091][  0]LogWorld: Bringing World /Game/Maps/Boot.Boot up for play (max tick rate 0) at 2026.10.02-12.18.18"
SUCCESS = "[2026.10.02-10.18.23:644][383]LogAutomationController: Display: Test Completed. Result={Success} Name={Affordability} Path={CoopRTS.Rules.Economy.Affordability}"
SECOND = "[2026.10.02-10.18.23:645][385]LogAutomationController: Display: Test Completed. Result={Success} Name={ExtractorDepletionAndOwner} Path={CoopRTS.Rules.Economy.ExtractorDepletionAndOwner}"
FINISH = "[2026.10.02-10.18.23:653][418]LogAutomationCommandLine: Display: **** TEST COMPLETE. EXIT CODE: 0 ****"
CONSTRUCTION = "[2026.10.02-10.19.01:228][527]LogAutomationController: Display: Test Completed. Result={Success} Name={Lifecycle} Path={CoopRTS.Construction.Lifecycle}"


def test_success_includes_every_test_beneath_filter() -> None:
    result = parse_automation(
        f"{WORLD}\n{SUCCESS}\n{SECOND}\n{FINISH}",
        "CoopRTS.Rules",
        "/Game/Maps/Boot",
        0,
    )
    assert result.ok
    assert result.completed == (
        "CoopRTS.Rules.Economy.Affordability",
        "CoopRTS.Rules.Economy.ExtractorDepletionAndOwner",
    )
    assert result.failed == ()


@pytest.mark.parametrize(
    "text,filter,map_path,code,problem",
    [
        (
            "\n".join([WORLD, SUCCESS, SECOND.replace("{Success}", "{Fail}"), FINISH]),
            "CoopRTS.Rules",
            "/Game/Maps/Boot",
            0,
            "non-Success",
        ),
        (
            f"{WORLD}\n{SUCCESS}",
            "CoopRTS.Rules",
            "/Game/Maps/Boot",
            0,
            "missing zero-exit",
        ),
        (
            f"{WORLD}\n{SUCCESS}\n{FINISH}",
            "CoopRTS.Rules",
            "/Game/Maps/AvailabilityZoneV2",
            0,
            "did not start",
        ),
        (
            f"{WORLD}\n{CONSTRUCTION}\n{FINISH}",
            "CoopRTS.Rules",
            "/Game/Maps/Boot",
            0,
            "zero tests",
        ),
        (
            f"{WORLD}\n{SUCCESS}\n{FINISH}",
            "CoopRTS.Rules",
            "/Game/Maps/Boot",
            1,
            "process exit 1",
        ),
        (
            "\n".join(
                [
                    WORLD,
                    SUCCESS,
                    FINISH.replace(COMPLETE, "**** TEST COMPLETE. EXIT CODE: 1 ****"),
                ]
            ),
            "CoopRTS.Rules",
            "/Game/Maps/Boot",
            0,
            "missing zero-exit",
        ),
        (
            "\n".join(
                [
                    WORLD,
                    SUCCESS.replace("CoopRTS.Rules.", "CoopRTS.RulesExtra."),
                    FINISH,
                ]
            ),
            "CoopRTS.Rules",
            "/Game/Maps/Boot",
            0,
            "zero tests",
        ),
    ],
)
def test_failure_conditions(
    text: str, filter: str, map_path: str, code: int, problem: str
) -> None:
    result = parse_automation(text, filter, map_path, code)
    assert not result.ok
    assert problem in result.details


def test_exact_world_filter_does_not_accept_sibling() -> None:
    text = f"{WORLD}\n{CONSTRUCTION}\n{FINISH}"
    assert parse_automation(
        text, "CoopRTS.Construction.Lifecycle", "/Game/Maps/Boot", 0
    ).ok
    assert not parse_automation(
        text, "CoopRTS.Construction.Production", "/Game/Maps/Boot", 0
    ).ok
