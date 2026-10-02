"""Pinned external tools; nonzero diagnostic exits are data, not failed execs."""

import json
from pathlib import Path
import re
import shutil
import subprocess
import tomllib

from x.context import Context
from x.lint.model import Finding, matches


def execute(
    ctx: Context, argv: list[str], label: str
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv, cwd=ctx.repo, text=True, capture_output=True, check=False
    )
    if ctx.run is not None:
        log = ctx.run.dir / f"lint-{label}.log"
        log.write_text(f"$ {' '.join(argv)}\n{result.stdout}{result.stderr}")
        ctx.run.add_artifact(log, f"lint {label}")
    return result


def binary(name: str) -> str:
    found = shutil.which(name)
    if found is None:
        raise ValueError(f"missing {name} on PATH")
    return found


def python_groups(ctx: Context, paths: list[str]) -> list[tuple[str | None, list[str]]]:
    with (ctx.repo / "Tools/x/lint.toml").open("rb") as source:
        spec: object = tomllib.load(source).get("python")
    if spec is None:
        return [(None, paths)] if paths else []
    if not isinstance(spec, dict) or set(spec) != {"version", "paths"}:
        raise ValueError("python target requires version and paths")
    version: object = spec["version"]
    patterns: object = spec["paths"]
    if (
        not isinstance(version, str)
        or re.fullmatch(r"3\.\d+", version) is None
        or not isinstance(patterns, list)
        or any(not isinstance(pattern, str) for pattern in patterns)
    ):
        raise ValueError("python target requires a version and path globs")
    embedded = [p for p in paths if any(matches(p, glob) for glob in patterns)]
    embedded_set = set(embedded)
    normal = [p for p in paths if p not in embedded_set]
    return [
        (target, files)
        for target, files in ((None, normal), (version, embedded))
        if files
    ]


def format_files(
    ctx: Context, paths: list[str], *, fix: bool
) -> tuple[list[Finding], list[Path]]:
    """Return findings and changed paths from the formatters' existing snapshots."""
    python = [p for p in paths if p.endswith(".py") or p == "x"]
    cpp = [p for p in paths if Path(p).suffix in {".h", ".cpp"}]
    findings: list[Finding] = []
    reformatted: list[Path] = []
    for target, files in python_groups(ctx, python):
        python_findings, python_reformatted = format_python(
            ctx, files, fix=fix, target=target
        )
        findings.extend(python_findings)
        reformatted.extend(python_reformatted)
    if cpp:
        cpp_findings, cpp_reformatted = format_cpp(ctx, cpp, fix=fix)
        findings.extend(cpp_findings)
        reformatted.extend(cpp_reformatted)
    return findings, reformatted


def format_python(
    ctx: Context, paths: list[str], *, fix: bool, target: str | None
) -> tuple[list[Finding], list[Path]]:
    flags = (
        [] if target is None else ["--target-version", "py" + target.replace(".", "")]
    )
    label = "" if target is None else "-py" + target.replace(".", "")
    findings: list[Finding] = []
    reformatted: list[Path] = []
    if fix:
        before = {p: (ctx.repo / p).read_bytes() for p in paths}
        applied = execute(
            ctx,
            ["uv", "run", "--locked", "ruff", "format", *flags, *paths],
            "format-fix" + label,
        )
        if applied.returncode:
            findings.append(
                Finding("Tools/x/lint.toml", 1, "format", applied.stderr.strip())
            )
        for path, content in before.items():
            if (ctx.repo / path).read_bytes() != content:
                print(f"reformatted {path}")
                reformatted.append(Path(path))
    checked = execute(
        ctx,
        ["uv", "run", "--locked", "ruff", "format", "--check", *flags, *paths],
        "format-check" + label,
    )
    if checked.returncode:
        output = checked.stdout + checked.stderr
        changed = re.findall(r"^Would reformat: (.+)$", output, re.MULTILINE)
        findings.extend(
            Finding(path, 1, "format", "file differs from Ruff formatting")
            for path in changed
        )
        if not changed:
            findings.extend(diagnostics(checked, "format", paths[0]))
    return findings, reformatted


def format_cpp(
    ctx: Context, paths: list[str], *, fix: bool
) -> tuple[list[Finding], list[Path]]:
    config = ctx.repo / ".clang-format"
    expected = re.search(
        r"clang-format (\d+\.\d+\.\d+)", config.read_text().splitlines()[0]
    )
    tool = binary("clang-format")
    version = execute(ctx, [tool, "--version"], "clang-version")
    actual = re.search(r"version (\d+\.\d+\.\d+)", version.stdout)
    if (
        expected is None
        or actual is None
        or actual[1] != expected[1]
        or version.returncode
    ):
        return [
            Finding(
                ".clang-format",
                1,
                "format",
                f"exact formatter version required: {expected[1] if expected else 'missing'}; {version.stdout.strip()}",
            )
        ], []
    before = {p: (ctx.repo / p).read_bytes() for p in paths} if fix else {}
    findings: list[Finding] = []
    reformatted: list[Path] = []
    if fix:
        applied = execute(ctx, [tool, "-i", *paths], "clang-fix")
        if applied.returncode:
            findings.append(Finding(paths[0], 1, "format", applied.stderr.strip()))
        for path, content in before.items():
            if (ctx.repo / path).read_bytes() != content:
                print(f"reformatted {path}")
                reformatted.append(Path(path))
    checked = execute(ctx, [tool, "--dry-run", "--Werror", *paths], "clang-check")
    findings.extend(diagnostics(checked, "format", paths[0]))
    return findings, reformatted


def diagnostics(
    result: subprocess.CompletedProcess[str], rule: str, fallback: str
) -> list[Finding]:
    findings: list[Finding] = []
    for line in (result.stdout + result.stderr).splitlines():
        match = re.match(r"(.+?):(\d+)(?::\d+)?: (?:error: )?(.*)", line)
        if match and ": note:" not in line:
            findings.append(Finding(match[1], int(match[2]), rule, match[3]))
    if result.returncode and not findings:
        findings.append(
            Finding(fallback, 1, rule, (result.stdout + result.stderr).strip())
        )
    return findings


def lint_python(ctx: Context, rule: str, paths: list[str]) -> list[Finding]:
    return [
        finding
        for target, files in python_groups(ctx, paths)
        for finding in lint_python_group(ctx, rule, files, target)
    ]


def lint_python_group(
    ctx: Context, rule: str, paths: list[str], target: str | None
) -> list[Finding]:
    label = rule if target is None else rule + "-py" + target.replace(".", "")
    if rule == "mypy":
        flags = [] if target is None else ["--python-version", target]
        result = execute(
            ctx,
            ["uv", "run", "--locked", "mypy", "--strict", "--no-error-summary", *flags, *paths],
            label,
        )
        return diagnostics(result, rule, paths[0])
    flags = (
        [] if target is None else ["--target-version", "py" + target.replace(".", "")]
    )
    result = execute(
        ctx,
        ["uv", "run", "--locked", "ruff", "check", "--output-format=json", *flags, *paths],
        label,
    )
    if result.returncode not in {0, 1}:
        return [Finding(paths[0], 1, rule, result.stderr.strip())]
    values = json.loads(result.stdout)
    return [
        Finding(
            Path(item["filename"]).relative_to(ctx.repo).as_posix(),
            item["location"]["row"],
            rule,
            f"{item['code']}: {item['message']}",
        )
        for item in values
    ]
