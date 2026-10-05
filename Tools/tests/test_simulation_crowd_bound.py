from harness.simulation_validation import crowd_unchecked_lines, validate_report
import pytest
from test_simulation_reports import telemetry


def test_peak_living_units_above_the_crowd_cap_is_rejected() -> None:
    # Units beyond UCrowdManager::MaxAgents stand still, which once silently ruined a whole gate run.
    job, report = telemetry()
    report.update(peak_living_units=301, crowd_max_agents=300)
    with pytest.raises(ValueError, match="crowd cap"):
        validate_report(report, job)
    report.update(peak_living_units=300)
    validate_report(report, job)


def test_report_without_the_unit_peak_is_rejected() -> None:
    job, report = telemetry()
    del report["peak_living_units"]
    with pytest.raises(ValueError, match="peak living units"):
        validate_report(report, job)


def test_a_legacy_report_without_the_fields_is_valid_only_when_revalidating() -> None:
    job, report = telemetry()
    del report["peak_living_units"], report["crowd_max_agents"]
    validate_report(report, job, legacy=True)
    with pytest.raises(ValueError, match="peak living units"):
        validate_report(report, job)


def test_a_half_recorded_or_overfull_report_is_rejected_even_when_legacy() -> None:
    job, report = telemetry()
    del report["crowd_max_agents"]
    with pytest.raises(ValueError, match="crowd agent cap"):
        validate_report(report, job, legacy=True)
    job, report = telemetry()
    report.update(peak_living_units=400)
    with pytest.raises(ValueError, match="crowd cap"):
        validate_report(report, job, legacy=True)


def test_matches_without_the_record_are_counted_in_a_warning() -> None:
    _, current = telemetry()
    _, legacy = telemetry()
    del legacy["peak_living_units"], legacy["crowd_max_agents"]
    assert crowd_unchecked_lines([({}, current)]) == []
    warning = crowd_unchecked_lines([({}, current), ({}, legacy)])
    assert "1 of 2 matches predate" in warning[0]
