"""Aggregate only validated simulation matches into numerical and Markdown reports."""

from __future__ import annotations

import collections
import json
from pathlib import Path
import statistics

from harness.simulation_charts import make_charts
from harness.simulation_comparison import compare_pair, symmetry_metadata
from harness.simulation_evidence import GroupKey, Groups, save_json, teams
from harness.simulation_validation import validate_report
from harness.verify import JsonObject


def load_results(
    run: Path, manifest: JsonObject
) -> tuple[list[tuple[JsonObject, JsonObject]], list[JsonObject]]:
    valid, failed = [], []
    for record in manifest["matches"]:
        if record.get("status") != "complete":
            failed.append(record)
            continue
        try:
            directory = Path(record["directory"])
            # Evidence directories may be moved together after the run.
            report = json.loads((run / directory.name / "match.json").read_text())
            if record.get("returncode") != 0:
                raise ValueError("Recorded process did not exit zero")
            validate_report(report, record["job"])
            valid.append((record, report))
        except (OSError, ValueError, KeyError, TypeError) as error:
            failed.append(dict(record, status="failed", error=str(error)))
    return valid, failed


def group_summary(key: GroupKey, reports: list[JsonObject]) -> tuple[str, JsonObject]:
    map_name, variant, dilation = key
    n = len(reports)
    wins0 = sum(row["winner"] == 0 for row in reports)
    wins5 = sum(row["winner"] == 5 for row in reports)
    draws = sum(row["outcome"] == "time_cap" for row in reports)
    depleted = [
        min(
            event["time"]
            for event in row["events"]
            if event["kind"] == "deposit_depleted"
        )
        / 60
        for row in reports
        if any(event["kind"] == "deposit_depleted" for event in row["events"])
    ]
    median_duration = statistics.median(row["duration"] for row in reports) / 60
    wall = statistics.median(row["wall_duration"] for row in reports)
    depletion = (
        f"{statistics.median(depleted):.2f} ({len(depleted)}/{n})"
        if depleted
        else "not reached"
    )
    line = f"| {map_name} | {variant} | {dilation:g}\u00d7 | {n} | {wins0}/{n} ({wins0 / n:.0%}) | {wins5}/{n} ({wins5 / n:.0%}) | {draws} | {median_duration:.2f} | {wall:.1f} | {depletion} |"
    summary = dict(
        map=map_name,
        variant=variant,
        dilation=dilation,
        complete=n,
        team0_wins=wins0,
        team5_wins=wins5,
        draws=draws,
        median_duration_minutes=median_duration,
        median_wall_seconds=wall,
        depletion_matches=len(depleted),
    )
    return line, summary


def layout_evidence(groups: Groups, lines: list[str]) -> JsonObject:
    layouts = {}
    for key, reports in sorted(groups.items()):
        layout = symmetry_metadata(reports[0])
        layouts[key[0]] = layout
        for team in (0, 5):
            metadata = layout[str(team)]
            natural = metadata["nearest_natural_cm"]
            concentration = [
                teams(snapshot)[team]["largest_region_unit_share"]
                for row in reports
                for snapshot in row["snapshots"]
                if teams(snapshot)[team]["units_alive"] >= 12
            ]
            share = (
                f"{statistics.median(concentration):.1%}"
                if concentration
                else "not reached"
            )
            lines.append(
                f"- `{key[0]}` `{key[1]}` {key[2]:g}\u00d7 team {team}: HQ `{metadata['hq']}`, nearest natural **{natural:.0f} cm**"
                if natural is not None
                else f"- `{key[0]}` `{key[1]}` team {team}: no natural region metadata"
            )
            lines.append(
                f"  Reward graph hops: `{metadata['reward_hops']}`; median ≥12-unit largest-region share: **{share}**."
            )
    return layouts


def paired_comparisons(
    valid: list[tuple[JsonObject, JsonObject]], manifest: JsonObject, lines: list[str]
) -> list[JsonObject]:
    comparisons = []
    if manifest.get("comparison_dilation"):
        pairs: collections.defaultdict[
            tuple[str, str, int], dict[float, JsonObject]
        ] = collections.defaultdict(dict)
        for record, report in valid:
            job = record["job"]
            pairs[(job["map"], job["variant"], job["seed"])][job["dilation"]] = report
        expected_pairs = {
            (job["map"], job["variant"], job["seed"])
            for job in manifest["planned_jobs"]
        }
        for key in sorted(expected_pairs):
            pair = pairs[key]
            if 1.0 not in pair or manifest["comparison_dilation"] not in pair:
                comparison = dict(
                    status="fail_or_inconclusive", issues=["Missing valid paired match"]
                )
            else:
                comparison = compare_pair(
                    pair[1.0],
                    pair[manifest["comparison_dilation"]],
                    manifest["comparison_tolerance"],
                )
            comparisons.append(
                dict(map=key[0], variant=key[1], seed=key[2], **comparison)
            )
        lines += [
            "",
            "## Paired dilation sample",
            "",
            "Comparison requires paid production, attacks on both sides, HQ damage and capture. Zero-combat samples are inconclusive, never passes. Uses shared 30s boundaries, 5% relative +1 count tolerance by default, exact capture transition sequence and ≤2s first-event timing.",
        ]
        for item in comparisons:
            lines.append(
                f"- `{item['map']}` `{item['variant']}` seed {item['seed']}: **{item['status']}**"
            )
            lines.extend(f"  - {issue}" for issue in item["issues"][:12])
    return comparisons


def report_tail(
    run: Path,
    manifest: JsonObject,
    failed: list[JsonObject],
    groups: Groups,
    lines: list[str],
) -> None:
    if failed:
        lines += ["", "## Failed or interrupted attempts", ""]
        lines.extend(
            f"- `{Path(row['directory']).name}`: {row.get('error', row.get('status'))}"
            for row in failed
        )
    if len(manifest["matches"]) < len(manifest["planned_jobs"]):
        lines += [
            "",
            f"**Incomplete batch:** {len(manifest['planned_jobs']) - len(manifest['matches'])} planned matches were not attempted.",
        ]
    if groups:
        make_charts(run, groups)
        lines += [
            "",
            "## Curves",
            "",
            "Curves average surviving matches at each scheduled sample; later samples have fewer contributors after HQ endings (survivorship bias).",
            "",
            "![Income](income.png)",
            "",
            "![Units](units.png)",
            "",
            "![Concentration](concentration.png)",
            "",
            "![Duration](duration.png)",
        ]


def summarize(run: Path, manifest: JsonObject) -> bool:
    if manifest.get("mode") == "duel" or any(
        job.get("mode") == "duel" for job in manifest["planned_jobs"]
    ):
        from harness.simulation_duels import summarize_duels

        return summarize_duels(run, manifest)
    return _summarize_matches(run, manifest)


def _summarize_matches(run: Path, manifest: JsonObject) -> bool:
    valid, failed = load_results(run, manifest)
    groups: collections.defaultdict[GroupKey, list[JsonObject]] = (
        collections.defaultdict(list)
    )
    for record, report in valid:
        key = (
            record["job"]["map"],
            record["job"]["variant"],
            record["job"]["dilation"],
        )
        groups[key].append(report)
    lines = [
        "# AI-vs-AI simulation report",
        "",
        f"Run: `{run.name}`. Complete matches: **{len(valid)}**; failed/interrupted: **{len(failed)}**.",
        "Failures never enter win, draw, duration or curve denominators. JSON, launch identity, stdout and game logs remain beside this report.",
        "",
        "Fixed 60 Hz game steps, unlimited headless wall-clock throughput; dilation is not permitted to coarsen combat/production ticks.",
        "Seeds initialize UE global random streams; asynchronous navigation and actor ordering are not guaranteed deterministic. Identical outcomes across seeds are not independent statistical evidence.",
        "",
        "| Map | Variant | Dilation | Complete | Team 0 wins | Team 5 wins | Draws | Median game min | Median wall s | Median first depletion min |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    summaries = []
    for key, reports in sorted(groups.items()):
        line, summary = group_summary(key, reports)
        lines.append(line)
        summaries.append(summary)
    lines += [
        "",
        "The pacing target is 12\u201318 game minutes. Draw durations are censored by the cap, not measured victory times.",
        "~50% side wins is an expectation only for actually symmetric maps and identical conditions. V2's HQ positions, travel distances and region geometry are asymmetric; do not label a side bias an AI bug without inspecting these metadata and planner tick ordering.",
        "",
        "## Layout and concentration evidence",
        "",
        "Largest-region unit share (only snapshots with ≥12 living units) is a spatial concentration proxy, not proof that deathballs win or that human co-op is readable.",
        "`deposits_remaining` is reserve in currently controlled regions (ownership changes can move it); per-deposit histories and `deposit_depleted` events are the depletion evidence.",
        "Observed unit health loss is a lower bound: damage and repairs within a tick, or a fatal hit before actor removal, may not be visible. Casualties/births are observed each tick. Attack counters count shots, not successful damage.",
        "",
    ]
    layouts = layout_evidence(groups, lines)
    comparisons = paired_comparisons(valid, manifest, lines)
    report_tail(run, manifest, failed, groups, lines)
    save_json(
        run / "summary.json",
        dict(
            groups=summaries,
            layouts=layouts,
            failures=failed,
            dilation_comparisons=comparisons,
        ),
    )
    (run / "Report.md").write_text("\n".join(lines) + "\n")
    return (
        not failed
        and len(manifest["matches"]) == len(manifest["planned_jobs"])
        and all(row["status"] == "pass" for row in comparisons)
    )
