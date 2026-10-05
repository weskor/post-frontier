from harness.simulation_validation import validate_report
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
