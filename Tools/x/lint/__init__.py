"""Changed-file lint API used by check and land; every finding blocks."""

from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
import time

from x import gitinfo
from x.context import Context
from x.lint import docs, source, tools
from x.lint.model import (
    CODE,
    RULES,
    Finding,
    Policy,
    PolicyError,
    apply_exceptions,
    load,
)
from x.scopes import load as load_scopes


@dataclass(frozen=True)
class LintResult:
    ok: bool
    reformatted: tuple[Path, ...] = ()


CONFIG = {
    "Tools/x/lint.toml",
    "Tools/x/lint-exceptions.toml",
    "pyproject.toml",
    "uv.lock",
    ".clang-format",
}


def repository_files(repo: Path, *, tracked: bool = False) -> list[str]:
    args = (
        ("ls-files", "-z")
        if tracked
        else ("ls-files", "-co", "--exclude-standard", "-z")
    )
    return sorted(
        {
            p
            for p in gitinfo.query(repo, *args).split("\0")
            if p and (repo / p).is_file()
        }
    )


def select(repo: Path, paths: Sequence[Path], policy: Policy) -> list[str]:
    relative = {
        p.relative_to(repo).as_posix() if p.is_absolute() else p.as_posix()
        for p in paths
    }
    if relative & CONFIG:
        relative.update(repository_files(repo))
    relative.update(entry.path for entry in policy.exceptions)
    return sorted(p for p in relative if (repo / p).is_file())


def check_rule(
    ctx: Context,
    rule: str,
    selected: list[str],
    policy: Policy,
    *,
    mypy_changed: bool = False,
) -> list[Finding]:
    if rule == "scope-map":
        tracked = [
            Path(path)
            for path in gitinfo.query(ctx.repo, "ls-files", "-z").split("\0")
            if path and policy.enabled(rule, path)
        ]
        if not tracked:
            return []
        return [
            Finding(str(path), 1, rule, "tracked path has no scope-map entry")
            for path in load_scopes(ctx.repo).unmapped(tracked)
        ]
    paths = [p for p in selected if policy.enabled(rule, p)]
    if rule in {"ruff", "mypy"}:
        if rule == "mypy" and (paths or mypy_changed):
            paths = [p for p in repository_files(ctx.repo) if policy.enabled(rule, p)]
        return tools.lint_python(ctx, rule, paths, policy)
    findings: list[Finding] = []
    for path in paths:
        if rule != "land-bypass":
            suffixes = {".md"} if rule in {"saved-path", "procedure-text"} else CODE
            if Path(path).suffix not in suffixes and path != "x":
                continue
        try:
            text = (ctx.repo / path).read_text()
        except UnicodeDecodeError:
            if rule == "land-bypass":
                continue
            raise
        if rule == "rules-includes":
            headers = frozenset(
                p.name for p in (ctx.repo / "Source/CoopRTS/Rules").glob("*.h")
            )
            findings.extend(source.includes(path, text, headers=headers))
            continue
        scanner = docs.scan if rule in {"saved-path", "procedure-text"} else source.scan
        findings.extend(scanner(rule, path, text))
    return findings


def run(ctx: Context, paths: Sequence[Path], *, fix: bool) -> LintResult:
    """Return blocking-lint success and paths actually changed by formatting."""
    try:
        policy = load(ctx.repo)
    except PolicyError as error:
        return report(ctx, [Finding(error.path, 1, "configuration", str(error))], 0, {})
    selected = select(ctx.repo, paths, policy)
    mypy_changed = any(
        policy.enabled(
            "mypy",
            p.relative_to(ctx.repo).as_posix() if p.is_absolute() else p.as_posix(),
        )
        for p in paths
    )
    findings: list[Finding] = []
    reformatted: list[Path] = []
    durations: dict[str, float] = {}
    # Formatting happens before every source rule, so locations describe final bytes.
    for rule in ("format", *(rule for rule in RULES if rule != "format")):
        started = time.monotonic()
        try:
            if rule == "format":
                format_paths = [p for p in selected if policy.enabled(rule, p)]
                format_findings, reformatted = tools.format_files(
                    ctx, format_paths, policy, fix=fix
                )
                findings.extend(format_findings)
            else:
                findings.extend(
                    check_rule(ctx, rule, selected, policy, mypy_changed=mypy_changed)
                )
        except (OSError, ValueError) as error:
            findings.append(Finding("Tools/x/lint.toml", 1, rule, str(error)))
        durations[rule] = time.monotonic() - started
    return report(
        ctx,
        sorted(apply_exceptions(findings, policy)),
        len(selected),
        durations,
        reformatted,
    )


def report(
    ctx: Context,
    findings: list[Finding],
    files: int,
    durations: dict[str, float],
    reformatted: Sequence[Path] = (),
) -> LintResult:
    for finding in findings:
        print(finding)
    print(f"lint: {len(findings)} findings in {files} files")
    if ctx.run is not None:
        invalid_config = any(item.rule == "configuration" for item in findings)
        if invalid_config:
            ctx.run.add_result("lint:configuration", False, str(findings[0]))
        for rule in RULES:
            count = sum(item.rule == rule for item in findings)
            details = (
                "blocked by invalid lint configuration"
                if invalid_config
                else f"{count} findings"
            )
            ctx.run.add_result(
                f"lint:{rule}",
                count == 0 and not invalid_config,
                details,
                durations.get(rule, 0.0),
            )
    return LintResult(not findings, tuple(reformatted))
