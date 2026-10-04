"""Report complete duel matrices and design-rule evidence, never synthetic wins."""

from __future__ import annotations

from collections import Counter, defaultdict
import json
from pathlib import Path

from harness.simulation_duel_rules import COUNTERS, DESIGN_SOURCE, evaluate_group
from harness.simulation_duel_validation import DUEL_BUDGET, definitions, is_stalled
from harness.simulation_evidence import save_json
from harness.simulation_report import load_results
from harness.verify import JsonObject


def job_identity(job: JsonObject) -> str:
    return json.dumps(job, sort_keys=True, allow_nan=False)


def group_identity(job: JsonObject) -> tuple[str, float, float]:
    return job["map"], job["dilation"], job["time_cap"]


def report_group(lines: list[str], group: JsonObject) -> None:
    lines += [
        "",
        f"## {group['map']} — {group['dilation']:g}x",
        "",
        f"Seeds: {group['complete_seeds']} complete / {group['requested_seeds']} requested. "
        f"Balance: **{group['balance_status'].upper()}**.",
        "",
        "### Runtime definitions",
        "",
        "| Id / design name | Unit cost | Capacity | HP | Shield | Damage | Interval | Range | Role |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in group["unit_definitions"]:
        name = COUNTERS[row["id"]].name if row["id"] in COUNTERS else "unmapped"
        lines.append(
            f"| {row['id']} / {name} | {row['cost']:g} | {row['capacity']:g} | "
            f"{row['health']:g} | {row.get('shield', 0):g} | {row['damage']:g} | {row['attack_interval']:g} | "
            f"{row['range']:g} | {'support' if row.get('support') else 'combat'} |"
        )
    lines += ["", "### Runtime geometry", ""]
    for geometry in group["geometry"]:
        lines.append(f"- `{json.dumps(geometry, sort_keys=True)}`")
    report_matrix(lines, group)
    report_rules(lines, group)
    report_compositions(lines, group)
    report_combat(lines, group)


def report_matrix(lines: list[str], group: JsonObject) -> None:
    lines += [
        "",
        "### Ordered win-rate matrix",
        "",
        "Each cell reports left/team 0 and right/team 5 separately; wins only, draws in denominator.",
        "Creation order alternates by seed parity (odd: team 0 first; even: team 5 first). "
        "Only equal measured first-spawn counts balance creation order; an odd seed budget remains unbalanced.",
        "",
        "| Left | Right | Duels | Team 0 wins | Team 5 wins | Draws | Team 0 - team 5 | First-spawn counts [0, 5] | Order balanced |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in group["matrix"]:
        rates = [
            f"{value:.1%}" if value is not None else "unmeasured"
            for value in row["win_rates"]
        ]
        bias = (
            f"{row['side_bias']:+.1%}" if row["side_bias"] is not None else "unmeasured"
        )
        lines.append(
            f"| {row['left']} | {row['right']} | {row['duels']} | "
            f"{row['wins'][0]} ({rates[0]}) | {row['wins'][1]} ({rates[1]}) | {row['draws']} | {bias} | "
            f"{row['spawn_first_counts']} | {'yes' if row['spawn_order_balanced'] else 'no'} |"
        )


def report_rules(lines: list[str], group: JsonObject) -> None:
    lines += [
        "",
        "### Acceptance rules",
        "",
        "Prey/predator rates combine the unit's two ordered sides against that opponent; a unit with several prey "
        "or predators has one rule per opponent, and support units have none. Worth and dominance cover combat "
        "units only, with durability HP plus shield. The composition rule compares the partner squad with and "
        "without the support unit against the same target at the same budget. "
        "Mirror rules require at least 40 fights and a two-sided exact binomial p-value "
        "of at least 0.05 for each side against 50%; the matrix shows mirror side bias. "
        "Missing requested seeds invalidate all rule passes.",
        "",
    ]
    for row in group["rules"]:
        evidence = {
            key: value for key, value in row.items() if key not in ("rule", "status")
        }
        lines.append(
            f"- **{row['status'].upper()}** `{row['rule']}`: `{json.dumps(evidence, sort_keys=True)}`"
        )


def report_combat(lines: list[str], group: JsonObject) -> None:
    lines += [
        "",
        "### Per-seed combat evidence",
        "",
        "Pairs list [team 0, team 5]. Spent excludes configuration fees; damage is effective HP removed; "
        "shield damage is shield points removed from the opponent, by any source including the pulse.",
        "",
        "| Seed | Left / right | Spawn first team | Spent | Initial units | Survivors | Survivor Power | Damage | Shield damage | Attacks | Seconds | Outcome / winner |",
        "| ---: | --- | ---: | --- | --- | --- | --- | --- | --- | --- | ---: | --- |",
    ]
    for row in group["telemetry"]:
        lines.append(
            f"| {row['seed']} | {row['left']} / {row['right']} | {row['spawn_first_team']} | {row['spent']} | "
            f"{row['initial_units']} | {row['survivors']} | {row['survivor_power']} | "
            f"{row['damage_dealt']} | {row.get('shield_damage_dealt', [0, 0])} | {row['attacks']} | "
            f"{row['duration']:g} | {row['outcome']} / {row['winner']} |"
        )


def report_compositions(lines: list[str], group: JsonObject) -> None:
    if not group["compositions"]:
        return
    lines += [
        "",
        "### Support composition fights",
        "",
        "Subject = the squad with or without the support unit; pairs list [team 0, team 5].",
        "",
        "| Seed | Scenario | Subject team | Left units | Right units | Spent | Survivors | Survivor Power | Damage | Shield damage | Attacks | Seconds | Outcome / winner |",
        "| ---: | --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- | ---: | --- |",
    ]
    for row in group["compositions"]:
        units = [
            "+".join(f"{part['count']}x{part['id']}" for part in row[side])
            for side in ("left_units", "right_units")
        ]
        lines.append(
            f"| {row['seed']} | {row['scenario']} ({row['support']}+{row['partner']} vs {row['target']}) | "
            f"{row['subject_team']} | {units[0]} | {units[1]} | {row['spent']} | {row['survivors']} | "
            f"{row['survivor_power']} | {row['damage_dealt']} | {row['shield_damage_dealt']} | {row['attacks']} | "
            f"{row['duration']:g} | {row['outcome']} / {row['winner']} |"
        )


def collect_results(
    run: Path,
    manifest: JsonObject,
) -> tuple[dict[tuple[str, float, float], list[JsonObject]], list[JsonObject], int]:
    valid, failed = load_results(run, manifest)
    for index, record in enumerate(failed):
        try:
            directory = Path(record["directory"])
            report = json.loads((run / directory.name / "match.json").read_text())
        except (OSError, ValueError, KeyError, TypeError):
            continue
        if isinstance(report, dict) and is_stalled(report):
            failed[index] = dict(
                record,
                status="failed",
                outcome="stalled",
                error=f"Stalled duel is invalid, not a draw: {report.get('error', 'no effective damage')}",
                invalid_duel=report.get("invalid_duel"),
            )
    planned = manifest["planned_jobs"]
    expected = Counter(job_identity(job) for job in planned)
    accepted: Counter[str] = Counter()
    groups: dict[tuple[str, float, float], list[JsonObject]] = defaultdict(list)
    if not planned or any(n != 1 for n in expected.values()):
        failed.append(dict(status="failed", error="No unique planned duel seed jobs"))
    for record, report in valid:
        identity = job_identity(record["job"])
        key = group_identity(record["job"])
        if expected[identity] != 1 or accepted[identity]:
            failed.append(
                dict(
                    record,
                    status="failed",
                    error="Unexpected or duplicate duel seed job",
                )
            )
            continue
        if groups[key] and definitions(groups[key][0]) != definitions(report):
            failed.append(
                dict(
                    record,
                    status="failed",
                    error="Runtime definitions changed between seed processes",
                )
            )
            continue
        accepted[identity] += 1
        groups[key].append(report)
    missing = sum((expected - accepted).values())
    return groups, failed, missing


def report_header(complete: int, failed: int, missing: int) -> list[str]:
    return [
        "# Combat duel matrix report",
        "",
        f"Design source: `{DESIGN_SOURCE}` — Who beats whom and Acceptance check.",
        f"Budget: **{DUEL_BUDGET} Power per side**, whole units; configuration fees excluded.",
        "Support units are judged by the composition rule (+20 points with the support unit at equal budget), never one-on-one.",
        "Worth = (1 + own surviving Power / own spent - enemy surviving Power / enemy spent) / 2; "
        "unit worth is the mean across nonmirror combat opponents (support excluded), ordered sides and seeds. Maximum/minimum must be ≤1.25.",
        "Maps are summarized separately, never pooled. Seed variation is not proof of independent statistical samples.",
        "Balance failures are measurements and do not invalidate successful engine runs. "
        "Failed/nonzero-exit/incomplete telemetry never counts as a draw. "
        "Stalled fights invalidate the matrix and are excluded from all completed-duel denominators.",
        "",
        f"Complete seed processes: **{complete}**; failed/interrupted: **{failed}**; "
        f"missing requested seed processes: **{missing}**.",
    ]


def summarize_duels(run: Path, manifest: JsonObject) -> bool:
    groups, failed, missing = collect_results(run, manifest)
    planned = manifest["planned_jobs"]
    requests: dict[tuple[str, float, float], list[int]] = defaultdict(list)
    for job in planned:
        requests[group_identity(job)].append(job["seed"])
    lines = report_header(
        sum(len(rows) for rows in groups.values()), len(failed), missing
    )
    summaries = []
    for key, seeds in sorted(requests.items()):
        reports = groups[key]
        measured_seeds = sorted(report["seed"] for report in reports)
        complete = measured_seeds == sorted(seeds) and len(set(seeds)) == len(seeds)
        group = dict(
            map=key[0],
            dilation=key[1],
            time_cap=key[2],
            requested_seeds=sorted(seeds),
            complete_seeds=measured_seeds,
            total_duration=sum(report["duration"] for report in reports),
            **evaluate_group(reports, complete),
        )
        summaries.append(group)
        report_group(lines, group)
    if failed or missing:
        lines += ["", "## Runtime failures", ""]
        for row in failed:
            lines.append(
                f"- {row.get('directory', 'run')}: {row.get('error', row['status'])}"
            )
            if row.get("outcome") == "stalled":
                lines.append(
                    f"  - Invalid stalled combat evidence: `{json.dumps(row.get('invalid_duel'), sort_keys=True)}`"
                )
        if missing:
            lines.append(
                f"- {missing} planned seed processes have no valid complete matrix."
            )
    success = not failed and not missing and bool(planned)
    save_json(
        run / "summary.json",
        dict(
            mode="duel",
            source=DESIGN_SOURCE,
            runtime_status="pass" if success else "fail",
            support_compositions="measured",
            groups=summaries,
            failures=failed,
            missing_seed_processes=missing,
        ),
    )
    (run / "Report.md").write_text("\n".join(lines) + "\n")
    return success
