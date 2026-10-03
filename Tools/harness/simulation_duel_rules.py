"""Evaluate the combat-only acceptance rules from runtime duel measurements."""

from __future__ import annotations

from collections import defaultdict
import math
import statistics

from harness.simulation_duel_validation import definitions, is_stalled
from harness.verify import JsonObject

DESIGN_SOURCE = "Docs/Design/units.md"
# Today's runtime ids and the design's Who beats whom table are one mapping.
COUNTERS = {
    "frontline": dict(name="Brawler", prey="siege", predator="ranged"),
    "ranged": dict(name="Rifle", prey="frontline", predator="siege"),
    "siege": dict(name="Artillery", prey="ranged", predator="frontline"),
}
MIRROR_MIN_DUELS = 40
MIRROR_SIGNIFICANCE = 0.05


def rule(name: str, passed: bool, complete: bool, **evidence: object) -> JsonObject:
    return dict(
        rule=name,
        status="pass" if passed and complete else "fail",
        evidence_complete=complete,
        **evidence,
    )


def pair_summary(left: str, right: str, rows: list[JsonObject]) -> JsonObject:
    wins = [sum(row["winner"] == team for row in rows) for team in (0, 5)]
    total = len(rows)
    spawn_first_counts = [
        sum(row.get("spawn_first_team") == team for row in rows) for team in (0, 5)
    ]
    return dict(
        left=left,
        right=right,
        duels=total,
        wins=wins,
        draws=sum(row["winner"] is None for row in rows),
        win_rates=[win / total if total else None for win in wins],
        side_bias=(wins[0] - wins[1]) / total if total else None,
        spawn_first_counts=spawn_first_counts,
        spawn_order_balanced=bool(rows)
        and spawn_first_counts == [total / 2, total / 2],
        mean_duration=statistics.mean(row["duration"] for row in rows)
        if rows
        else None,
    )


def counter_rules(
    by_pair: dict[tuple[str, str], list[JsonObject]],
    complete: bool,
) -> list[JsonObject]:
    rules = []
    for unit, target in COUNTERS.items():
        for relation, bound in (("prey", 0.65), ("predator", 0.35)):
            opponent = target[relation]
            appearances = [
                (row, team)
                for pair, team in (((unit, opponent), 0), ((opponent, unit), 5))
                for row in by_pair[pair]
            ]
            wins = sum(row["winner"] == team for row, team in appearances)
            n = len(appearances)
            rate = wins / n if n else None
            passed = rate is not None and (
                rate >= bound if relation == "prey" else rate <= bound
            )
            rules.append(
                rule(
                    f"{unit}_{relation}",
                    passed,
                    complete,
                    unit=unit,
                    opponent=opponent,
                    wins=wins,
                    duels=n,
                    win_rate=rate,
                    requirement=f"{'at least' if relation == 'prey' else 'at most'} {bound:.0%}",
                )
            )
    return rules


def worth_rule(
    roster: dict[str, JsonObject],
    telemetry: list[JsonObject],
    complete: bool,
) -> JsonObject:
    samples: dict[str, list[float]] = defaultdict(list)
    for row in telemetry:
        if row["left"] == row["right"]:
            continue
        for side, unit in enumerate((row["left"], row["right"])):
            own = row["survivor_power"][side] / row["spent"][side]
            enemy = row["survivor_power"][1 - side] / row["spent"][1 - side]
            samples[unit].append((1 + own - enemy) / 2)
    worth = {
        unit: statistics.mean(samples[unit]) if samples[unit] else None
        for unit in roster
    }
    measured = [value for value in worth.values() if value is not None]
    ratio = max(measured) / min(measured) if measured and min(measured) > 0 else None
    passed = ratio is not None and (
        ratio <= 1.25 or math.isclose(ratio, 1.25, rel_tol=1e-12)
    )
    return rule(
        "roster_worth_ratio",
        passed,
        complete,
        worth=worth,
        ratio=ratio,
        maximum=1.25,
        formula="(1 + own surviving Power / own spent - enemy surviving Power / enemy spent) / 2",
        averaging="all nonmirror opponents, both ordered sides and seeds equally",
    )


def dominance_rule(roster: dict[str, JsonObject], complete: bool) -> JsonObject:
    efficiency = {
        unit: dict(
            hp_per_power=row["health"] / row["cost"],
            dps_per_power=row["damage"] / row["attack_interval"] / row["cost"],
        )
        for unit, row in roster.items()
    }
    dominance = [
        dict(leader=left, dominated=right)
        for left, own in efficiency.items()
        for right, other in efficiency.items()
        if left != right
        and own["hp_per_power"] > other["hp_per_power"]
        and own["dps_per_power"] > other["dps_per_power"]
    ]
    return rule(
        "no_hp_and_dps_per_power_dominance",
        not dominance and bool(roster),
        complete,
        efficiencies=efficiency,
        dominance=dominance,
        requirement="strictly greater on BOTH HP and DPS per Power is forbidden",
    )


def mirror_pvalue(wins: int, total: int) -> float:
    """Exact two-sided binomial p-value under a fair coin, including both tails."""
    tail = min(wins, total - wins)
    extreme_outcomes = 2 * sum(math.comb(total, count) for count in range(tail + 1))
    return min(1.0, extreme_outcomes / (1 << total))


def mirror_rules(
    roster: dict[str, JsonObject],
    by_pair: dict[tuple[str, str], list[JsonObject]],
    complete: bool,
) -> list[JsonObject]:
    rules = []
    for unit in roster:
        mirror = pair_summary(unit, unit, by_pair[unit, unit])
        p_values = [mirror_pvalue(wins, mirror["duels"]) for wins in mirror["wins"]]
        passed = mirror["duels"] >= MIRROR_MIN_DUELS and all(
            p_value >= MIRROR_SIGNIFICANCE for p_value in p_values
        )
        rules.append(
            rule(
                f"{unit}_mirror",
                passed,
                complete,
                requirement="at least 40 fights; neither side rejects 50% at 95% with a two-sided exact binomial test; draws remain in denominator",
                minimum_duels=MIRROR_MIN_DUELS,
                null_win_probability=0.5,
                significance=MIRROR_SIGNIFICANCE,
                p_values=p_values,
                **mirror,
            )
        )
    return rules


def evaluate_group(reports: list[JsonObject], complete: bool = True) -> JsonObject:
    admitted = [
        report
        for report in reports
        if report.get("status") == "complete"
        and report.get("outcome") == "matrix_complete"
        and "invalid_duel" not in report
        and not is_stalled(report)
    ]
    complete = complete and len(admitted) == len(reports)
    reports = admitted
    roster = definitions(reports[0]) if reports else {}
    by_pair: dict[tuple[str, str], list[JsonObject]] = defaultdict(list)
    telemetry: list[JsonObject] = []
    for report in reports:
        for row in report["duels"]:
            by_pair[row["left"], row["right"]].append(row)
            telemetry.append(dict(seed=report["seed"], **row))
    matrix = [
        pair_summary(left, right, by_pair[left, right])
        for left in roster
        for right in roster
    ]
    complete = complete and bool(reports)
    rules = counter_rules(by_pair, complete)
    rules.extend(mirror_rules(roster, by_pair, complete))
    rules.append(
        rule(
            "counter_table_coverage",
            set(roster) == set(COUNTERS),
            complete,
            runtime_ids=sorted(roster),
            configured_ids=sorted(COUNTERS),
            source=DESIGN_SOURCE,
        )
    )
    worth = worth_rule(roster, telemetry, complete)
    rules.extend((worth, dominance_rule(roster, complete)))
    return dict(
        unit_definitions=list(roster.values()),
        matrix=matrix,
        worth=worth["worth"],
        rules=rules,
        telemetry=telemetry,
        geometry=[
            dict(seed=report["seed"], **report["geometry"])
            for report in reports
            if isinstance(report.get("geometry"), dict)
        ],
        balance_status="pass"
        if all(row["status"] == "pass" for row in rules)
        else "fail",
    )
