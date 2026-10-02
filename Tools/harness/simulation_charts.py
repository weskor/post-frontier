"""Render active-match simulation curves without counting missing samples as zero."""

from __future__ import annotations

import collections
from pathlib import Path
import statistics

from harness.simulation_evidence import Groups, teams
from harness.verify import JsonObject


def sample_buckets(
    reports: list[JsonObject], team: int, field: str
) -> dict[float, list[float]]:
    buckets: collections.defaultdict[float, list[float]] = collections.defaultdict(list)
    for report in reports:
        for snapshot in report["snapshots"]:
            scheduled = snapshot["scheduled_time"]
            if abs(scheduled / 30 - round(scheduled / 30)) > 0.0001:
                continue
            summary = teams(snapshot)[team]
            if field == "largest_region_unit_share" and summary["units_alive"] < 12:
                continue
            buckets[scheduled].append(summary[field])
    return buckets


def make_charts(run: Path, groups: Groups) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    for filename, field, label in (
        ("income", "income_per_second", "Power / game second"),
        ("units", "units_alive", "Living units"),
        ("concentration", "largest_region_unit_share", "Largest-region unit share"),
    ):
        fig, axis = plt.subplots(figsize=(11, 6))
        for (map_name, variant, dilation), reports in sorted(groups.items()):
            for team in (0, 5):
                buckets = sample_buckets(reports, team, field)
                times = sorted(buckets)
                axis.plot(
                    [value / 60 for value in times],
                    [statistics.mean(buckets[value]) for value in times],
                    label=f"{map_name.rsplit('/', 1)[-1]} {variant} {dilation:g}\u00d7 T{team}",
                    linestyle="-" if team == 0 else "--",
                )
        axis.set(
            xlabel="Game minutes", ylabel=label, title=f"{label} (active-match mean)"
        )
        axis.grid(alpha=0.25)
        axis.legend(fontsize=7)
        fig.tight_layout()
        fig.savefig(run / f"{filename}.png", dpi=140)
        plt.close(fig)
    fig, axis = plt.subplots(figsize=(11, 6))
    labels, medians = [], []
    for (map_name, variant, dilation), reports in sorted(groups.items()):
        labels.append(f"{map_name.rsplit('/', 1)[-1]}\n{variant} {dilation:g}\u00d7")
        medians.append(statistics.median(row["duration"] for row in reports) / 60)
    axis.bar(labels, medians)
    axis.axhspan(12, 18, alpha=0.15, color="green", label="12\u201318 min target")
    axis.set(ylabel="Median game minutes (draws censored)", title="Match duration")
    axis.legend()
    fig.tight_layout()
    fig.savefig(run / "duration.png", dpi=140)
    plt.close(fig)
