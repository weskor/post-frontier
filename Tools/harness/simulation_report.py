"""Aggregate only validated simulation matches into numerical and Markdown reports."""

from __future__ import annotations

import collections
import json
from pathlib import Path
import statistics

from harness.simulation_charts import make_charts
from harness.simulation_comparison import (
    PASS,
    battle_statistics,
    compare_pair,
    evaluate_gate_1b,
    rush_evidence,
    symmetry_metadata,
)
from harness.simulation_evidence import (
    PLAN_LABELS,
    PLAN_VERBS,
    GroupKey,
    Groups,
    committed_plan_evidence,
    save_json,
    teams,
)
from harness.simulation_validation import (
    crowd_unchecked_lines,
    interpret_outcome,
    validate_report,
)
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
            validate_report(report, record["job"], legacy=True)
            valid.append((record, report))
        except (OSError, ValueError, KeyError, TypeError) as error:
            failed.append(dict(record, status="failed", error=str(error)))
    return valid, failed


def group_summary(key: GroupKey, reports: list[JsonObject]) -> tuple[str, JsonObject]:
    map_name, variant, dilation = key
    n = len(reports)
    outcomes = [interpret_outcome(row) for row in reports]
    wins0 = sum(outcome.winner == 0 for outcome in outcomes)
    wins5 = sum(outcome.winner == 5 for outcome in outcomes)
    draws = sum(not outcome.decisive for outcome in outcomes)
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


def _append_plan_details(
    matches: list[JsonObject], owner_totals: JsonObject, lines: list[str]
) -> None:
    lines += [
        "",
        "### Escalation by source-region owner at event time",
        "",
        "Ownership is captured in each event, not reconstructed from region-control snapshots. Defense at creation is not a mid-commitment transition. Later transitions are split by whether a new command was issued; label-only changes issue no order. Only the first creation and first transition per ticket count.",
        "",
        "| Match | Source owner | Defense at creation | Order-changing transitions | Label-only transitions |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for evidence in matches:
        for owner, categories in evidence["escalation_by_owner"].items():
            lines.append(
                f"| `{evidence['match']}` | {owner} | {categories['creation_defense']} | {categories['order_changing']} | {categories['label_only']} |"
            )
    for owner, categories in owner_totals.items():
        lines.append(
            f"| **Total** | {owner} | **{categories['creation_defense']}** | **{categories['order_changing']}** | **{categories['label_only']}** |"
        )
    lines += [
        "",
        "### Active published plans at each terminal snapshot",
        "",
        "Full sampled plan histories and creation/escalation events remain in each `match.json`; ETA and remaining commitment below are in seconds at the terminal observation.",
    ]
    for evidence in matches:
        lines += [
            "",
            f"#### `{evidence['match']}` — {evidence['active_at_seconds']:.1f}s",
            "",
        ]
        if not evidence["active_plans"]:
            lines.append("No active published plans.")
            continue
        lines += [
            "| Ticket | Force | Verb | Source region | Target region | Size band | ETA s | Commitment s | Escalated | Memo |",
            "| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ]
        for plan in evidence["active_plans"]:
            memo = " ".join(plan["memo"].split()).replace("|", r"\|")
            lines.append(
                f"| {plan['ticket']} | {plan['force']} | {PLAN_LABELS[plan['verb']]} | {plan['source_region']} | {plan['target_region']} | ~{plan['size_band']} units | {plan['eta_seconds']:.1f} | {plan['remaining_commitment_seconds']:.1f} | {'yes' if plan['escalated'] else 'no'} | {memo} |"
            )


def plan_evidence(
    valid: list[tuple[JsonObject, JsonObject]], lines: list[str]
) -> JsonObject:
    lines += [
        "",
        "## JEV committed plans (team 5)",
        "",
        "Counts are unique tickets per match, grouped by their creation verb. Escalated includes defense at creation and later transitions without counting the same ticket twice; repeated publications do not count again. Team 0 autopilot is excluded.",
        "",
        "| Match | Seed | Move & Hold | Attack | Retreat | Total | Escalated | Captures | Attacks observed |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    matches = []
    totals = dict.fromkeys((*PLAN_VERBS, "total", "escalated"), 0)
    owner_totals = {
        owner: dict(creation_defense=0, order_changing=0, label_only=0)
        for owner in ("jev", "neutral", "player")
    }
    for record, report in valid:
        evidence = committed_plan_evidence(report)
        evidence.update(
            match=Path(record["directory"]).name, seed=record["job"]["seed"]
        )
        matches.append(evidence)
        counts = evidence["counts"]
        for name in totals:
            totals[name] += counts[name]
        for owner, categories in evidence["escalation_by_owner"].items():
            for category, count in categories.items():
                owner_totals[owner][category] += count
        lines.append(
            f"| `{evidence['match']}` | {evidence['seed']} | {counts['move_and_hold']} | {counts['attack']} | {counts['retreat']} | {counts['total']} | {counts['escalated']} | {evidence['captures']} | {evidence['attacks_observed']} |"
        )
    lines.append(
        f"| **Total** | | **{totals['move_and_hold']}** | **{totals['attack']}** | **{totals['retreat']}** | **{totals['total']}** | **{totals['escalated']}** | | |"
    )
    _append_plan_details(matches, owner_totals, lines)
    return dict(
        team=5,
        counts=totals,
        escalation_by_owner=owner_totals,
        escalation_counts={
            category: sum(row[category] for row in owner_totals.values())
            for category in ("creation_defense", "order_changing", "label_only")
        },
        matches=matches,
    )


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


def variant_label(job: JsonObject) -> str:
    scenario = job.get("scenario", "default")
    return job["variant"] if scenario == "default" else f"{job['variant']}+{scenario}"


def group_matches(valid: list[tuple[JsonObject, JsonObject]]) -> Groups:
    groups: collections.defaultdict[GroupKey, list[JsonObject]] = (
        collections.defaultdict(list)
    )
    for record, report in valid:
        job = record["job"]
        groups[(job["map"], variant_label(job), job["dilation"])].append(report)
    return groups


def _minutes(seconds: float | None) -> str:
    return "n/a" if seconds is None else f"{seconds / 60:.2f}"


def _median_text(stats: JsonObject) -> str:
    text = _minutes(stats["median_with_censored_seconds"])
    return f"\u2265{text}" if stats["median_is_lower_bound"] else text


def _rush_line(variant: str, rush: JsonObject) -> str:
    median, longest = (
        rush["median_order_delay_seconds"],
        rush["max_order_delay_seconds"],
    )
    delays = (
        "n/a" if median is None else f"median {median:.1f} s, maximum {longest:.1f} s"
    )
    return (
        f"- `{variant}` rush: {rush['forces_seen']} forces seen alive, "
        f"{rush['forces_attacking']} ordered to Attack JEV's objective (its Failover Nodes, then its main); order delay {delays}. "
        f"Casualty-rule cycle under the Attack: {rush['withdrawals']} withdrawals, {rush['resumes']} resumes. "
        f"Retreat events (a defect, expected 0): {rush['retreats']}."
    )


def _earliest_lines(rows: list[JsonObject]) -> list[str]:
    lines = []
    for scenario in sorted({row["scenario"] for row in rows}):
        earliest = [
            row["earliest_victory_seconds"]
            for row in rows
            if row["scenario"] == scenario
            and row["earliest_victory_seconds"] is not None
        ]
        text = f"{min(earliest):.1f} s" if earliest else "none"
        lines.append(f"- Earliest team-0 victory, scenario `{scenario}`: **{text}**.")
    return lines


def battle_length_section(
    valid: list[tuple[JsonObject, JsonObject]], lines: list[str]
) -> list[JsonObject]:
    cells: collections.defaultdict[tuple[str, str, str, float], list[JsonObject]] = (
        collections.defaultdict(list)
    )
    for record, report in valid:
        job = record["job"]
        key = (
            job["map"],
            job["variant"],
            job.get("scenario", "default"),
            job["dilation"],
        )
        cells[key].append(report)
    lines += [
        "",
        "## Battle length",
        "",
        "Decisive matches ended by the outcome rules; censored matches reached the time cap and are not lengths. "
        "The median including censored counts each censored match at its cap; a censored match's true length is longer, so this is a lower bound (shown \u2265 when a censored match sits at a middle rank, where it proves no ceiling).",
        "",
        "| Map | Variant | Scenario | Dilation | Matches | Decisive | Median decisive min | Censored | Median incl. censored min | Earliest victory s |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    rows: list[JsonObject] = []
    evidence: list[str] = []
    for (map_name, variant, scenario, dilation), reports in sorted(cells.items()):
        stats = battle_statistics(reports)
        victory = stats["earliest_victory_seconds"]
        lines.append(
            f"| {map_name} | {variant} | {scenario} | {dilation:g}\u00d7 | {stats['matches']} | {stats['decisive']} "
            f"| {_minutes(stats['decisive_median_seconds'])} | {stats['censored']} "
            f"| {_median_text(stats)} | {'none' if victory is None else f'{victory:.1f}'} |"
        )
        row = dict(
            stats, map=map_name, variant=variant, scenario=scenario, dilation=dilation
        )
        if scenario == "rush":
            row["rush"] = rush = rush_evidence(reports)
            evidence.append(_rush_line(variant, rush))
        rows.append(row)
    lines += ["", *_earliest_lines(rows), *evidence, ""]
    return rows


def gate_section(
    manifest: JsonObject, valid: list[tuple[JsonObject, JsonObject]], lines: list[str]
) -> JsonObject | None:
    if manifest.get("gate") != "1b":
        return None
    result = evaluate_gate_1b(valid)
    lines += [
        f"## Gate 1b: {result['status']}",
        "",
        f"Needs both human baselines on V2, both scenarios, at least {result['seeds_required']} seeds in every cell and a 1200 s cap; fewer seeds never PASS.",
        "",
    ]
    lines += [
        f"- **{row['status']}** {row['check']}: {row['detail']}"
        for row in result["checks"]
    ]
    return result


def _summarize_matches(run: Path, manifest: JsonObject) -> bool:
    valid, failed = load_results(run, manifest)
    groups = group_matches(valid)
    lines = [
        "# AI-vs-AI simulation report",
        "",
        f"Run: `{run.name}`. Complete matches: **{len(valid)}**; failed/interrupted: **{len(failed)}**.",
        "Failures never enter win, draw, duration or curve denominators. JSON, launch identity, stdout and game logs remain beside this report.",
        "",
        *crowd_unchecked_lines(valid),
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
        "Draw durations are censored by the cap, not measured victory times; see the battle-length section for decisive and censored results separately.",
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
    stats_lines: list[str] = []
    length_rows = battle_length_section(valid, stats_lines)
    gate = gate_section(manifest, valid, stats_lines)
    lines += stats_lines
    print("\n".join(stats_lines), flush=True)
    plans = plan_evidence(valid, lines)
    comparisons = paired_comparisons(valid, manifest, lines)
    report_tail(run, manifest, failed, groups, lines)
    save_json(
        run / "summary.json",
        dict(
            groups=summaries,
            layouts=layouts,
            jev_plans=plans,
            battle_length=length_rows,
            gate=gate,
            failures=failed,
            dilation_comparisons=comparisons,
        ),
    )
    (run / "Report.md").write_text("\n".join(lines) + "\n")
    return (
        not failed
        and len(manifest["matches"]) == len(manifest["planned_jobs"])
        and all(row["status"] == "pass" for row in comparisons)
        and (gate is None or gate["status"] == PASS)
    )
