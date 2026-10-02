"""Changed-file lint API used by check and land; every finding blocks."""

from collections.abc import Sequence
from pathlib import Path
import time

from x import gitinfo
from x.context import Context
from x.lint import docs, source, tools
from x.lint.model import CODE, RULES, Finding, Policy, apply_exceptions, load

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
    fix: bool,
    mypy_changed: bool = False,
) -> list[Finding]:
    paths = [p for p in selected if policy.enabled(rule, p)]
    if rule == "format":
        return tools.format_files(ctx, paths, fix=fix) if paths else []
    if rule in {"ruff", "mypy"}:
        if rule == "mypy" and (paths or mypy_changed):
            paths = [p for p in repository_files(ctx.repo) if policy.enabled(rule, p)]
        return tools.lint_python(ctx, rule, paths)
    findings: list[Finding] = []
    for path in paths:
        suffixes = {".md"} if rule in {"saved-path", "procedure-text"} else CODE
        if Path(path).suffix not in suffixes and path != "x":
            continue
        text = (ctx.repo / path).read_text()
        if rule == "rules-includes":
            headers = frozenset(
                p.name for p in (ctx.repo / "Source/CoopRTS/Rules").glob("*.h")
            )
            findings.extend(source.includes(path, text, headers=headers))
            continue
        scanner = docs.scan if rule in {"saved-path", "procedure-text"} else source.scan
        findings.extend(scanner(rule, path, text))
    return findings


def run(ctx: Context, paths: Sequence[Path], *, fix: bool) -> bool:
    policy = load(ctx.repo)
    selected = select(ctx.repo, paths, policy)
    mypy_changed = any(
        policy.enabled(
            "mypy",
            p.relative_to(ctx.repo).as_posix() if p.is_absolute() else p.as_posix(),
        )
        for p in paths
    )
    findings: list[Finding] = []
    durations: dict[str, float] = {}
    # Formatting happens before every source rule, so locations describe final bytes.
    for rule in ("format", *(rule for rule in RULES if rule != "format")):
        started = time.monotonic()
        try:
            findings.extend(
                check_rule(
                    ctx, rule, selected, policy, fix=fix, mypy_changed=mypy_changed
                )
            )
        except (OSError, ValueError) as error:
            findings.append(Finding("Tools/x/lint.toml", 1, rule, str(error)))
        durations[rule] = time.monotonic() - started
    findings = sorted(apply_exceptions(findings, policy))
    for finding in findings:
        print(finding)
    print(f"lint: {len(findings)} findings in {len(selected)} files")
    if ctx.run is not None:
        for rule in RULES:
            count = sum(item.rule == rule for item in findings)
            ctx.run.add_result(
                f"lint:{rule}", count == 0, f"{count} findings", durations[rule]
            )
    return not findings
