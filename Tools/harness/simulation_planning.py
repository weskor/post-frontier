"""Validate simulation options and enumerate unchanged map/variant/seed jobs."""

from __future__ import annotations

import argparse
import math
import re

from harness.verify import JsonObject

MAP_V2 = "/Game/Maps/AvailabilityZoneV2"
MAP_V1 = "/Game/Maps/AvailabilityZone"
DEFAULT_ECONOMY = dict(
    human_baseline=2,
    jev_baseline=2,
    normal_rate=4,
    rich_rate=6,
    normal_amount=1200,
    rich_amount=1500,
)
RATE_KEYS = ("human_baseline", "jev_baseline", "normal_rate", "rich_rate")


def parse_variant(text: str) -> tuple[str, dict[str, int]]:
    name, separator, overrides = text.partition(":")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        raise argparse.ArgumentTypeError(
            "Variant name must contain only letters, digits, _ or -"
        )
    values = dict(DEFAULT_ECONOMY)
    if not separator:
        if name not in ("baseline2", "baseline3", "baseline4"):
            raise argparse.ArgumentTypeError(
                "Use baseline2/3/4 or NAME:human_baseline=1,jev_baseline=2,..."
            )
        values["human_baseline"] = int(name[-1])
    else:
        if not overrides:
            raise argparse.ArgumentTypeError("Economy overrides cannot be empty")
        seen = set()
        for part in overrides.split(","):
            key, equals, value = part.partition("=")
            maximum = 10000 if key in RATE_KEYS else 100000000
            if (
                key not in values
                or key in seen
                or not equals
                or not value.isdecimal()
                or int(value) > maximum
            ):
                raise argparse.ArgumentTypeError(
                    f"Invalid or duplicate economy override: {part}"
                )
            seen.add(key)
            values[key] = int(value)
    return name, values


def resolved_time_cap(args: argparse.Namespace) -> float:
    if args.time_cap is not None:
        return float(args.time_cap)
    return 300 if args.duel else 2400


def validate_options(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    time_cap = resolved_time_cap(args)
    if args.duel and (args.variant or args.matrix or args.compare_dilation is not None):
        parser.error(
            "--duel cannot use economy --variant, --matrix or --compare-dilation"
        )
    if args.matches <= 0 or not 0 <= args.seed <= 2147483647 - args.matches + 1:
        parser.error("matches must be positive and seeds within signed int32")
    if any(
        not math.isfinite(value)
        for value in (
            time_cap,
            args.sample_seconds,
            args.dilation,
            args.stall_seconds,
            args.comparison_tolerance,
        )
    ):
        parser.error("Numeric parameters must be finite")
    if (
        not 1 <= time_cap <= 86400
        or not 1 <= args.sample_seconds <= 86400
        or not 1 <= args.dilation <= 32
    ):
        parser.error("time caps must be 1..86400s and dilation 1..32")
    if args.compare_dilation is not None and (
        not math.isfinite(args.compare_dilation) or not 1 < args.compare_dilation <= 32
    ):
        parser.error("comparison dilation must be >1 and <=32")
    if args.stall_seconds < 60 or not 0 <= args.comparison_tolerance <= 1:
        parser.error("stall interval must be >=60s; comparison tolerance must be 0..1")
    if args.matrix and (args.maps or args.variant):
        parser.error("--matrix already defines maps and variants")


def plan_jobs(
    parser: argparse.ArgumentParser, args: argparse.Namespace
) -> list[JsonObject]:
    time_cap = resolved_time_cap(args)
    maps = args.maps or ([MAP_V2] if args.duel else [MAP_V2, MAP_V1])
    if len(set(maps)) != len(maps) or any(
        not re.fullmatch(r"/Game/[A-Za-z0-9_/]+", name) for name in maps
    ):
        parser.error(
            "Maps must be unique /Game/... package paths without URL options or extensions"
        )
    if args.duel:
        return [
            dict(
                mode="duel",
                map=map_name,
                variant="duel",
                seed=seed,
                dilation=args.dilation,
                time_cap=time_cap,
            )
            for map_name in maps
            for seed in range(args.seed, args.seed + args.matches)
        ]
    variants = args.variant or [parse_variant("baseline2")]
    if len({name for name, _ in variants}) != len(variants):
        parser.error("Variant names must be unique")
    combinations = (
        [
            (MAP_V2, parse_variant(name))
            for name in ("baseline2", "baseline3", "baseline4")
        ]
        + [(MAP_V1, parse_variant("baseline2"))]
        if args.matrix
        else [(map_name, variant) for map_name in maps for variant in variants]
    )
    dilation_values = (
        [1.0, args.compare_dilation] if args.compare_dilation else [args.dilation]
    )
    cap = args.sample_seconds if args.compare_dilation else time_cap
    return [
        dict(
            map=map_name,
            variant=name,
            economy=economy,
            seed=seed,
            dilation=dilation,
            time_cap=cap,
        )
        for map_name, (name, economy) in combinations
        for seed in range(args.seed, args.seed + args.matches)
        for dilation in dilation_values
    ]
