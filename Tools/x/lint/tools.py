"""Pinned external tools; nonzero diagnostic exits are data, not failed execs."""

import json
from pathlib import Path
import re
import shutil
import subprocess

from x.context import Context
from x.lint.model import Finding


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


def binary(ctx: Context, name: str) -> str:
    local = ctx.repo / ".venv/bin" / name
    if local.is_file():
        return str(local)
    found = shutil.which(name)
    if found is None:
        raise ValueError(f"missing {name}; install the locked dev dependencies")
    return found


def format_files(ctx: Context, paths: list[str], *, fix: bool) -> list[Finding]:
    python = [p for p in paths if p.endswith(".py") or p == "x"]
    cpp = [p for p in paths if Path(p).suffix in {".h", ".cpp"}]
    findings: list[Finding] = []
    if python:
        before = {p: (ctx.repo / p).read_bytes() for p in python} if fix else {}
        if fix:
            applied = execute(
                ctx, [binary(ctx, "ruff"), "format", *python], "format-fix"
            )
            if applied.returncode:
                findings.append(
                    Finding("Tools/x/lint.toml", 1, "format", applied.stderr.strip())
                )
            for path, content in before.items():
                if (ctx.repo / path).read_bytes() != content:
                    print(f"reformatted {path}")
        checked = execute(
            ctx, [binary(ctx, "ruff"), "format", "--check", *python], "format-check"
        )
        if checked.returncode:
            findings.append(
                Finding(
                    python[0], 1, "format", (checked.stdout + checked.stderr).strip()
                )
            )
    if cpp:
        findings.extend(format_cpp(ctx, cpp, fix=fix))
    return findings


def format_cpp(ctx: Context, paths: list[str], *, fix: bool) -> list[Finding]:
    config = ctx.repo / ".clang-format"
    expected = re.search(
        r"clang-format (\d+\.\d+\.\d+)", config.read_text().splitlines()[0]
    )
    tool = binary(ctx, "clang-format")
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
        ]
    before = {p: (ctx.repo / p).read_bytes() for p in paths} if fix else {}
    if fix:
        applied = execute(ctx, [tool, "-i", *paths], "clang-fix")
        if applied.returncode:
            return [Finding(paths[0], 1, "format", applied.stderr.strip())]
        for path, content in before.items():
            if (ctx.repo / path).read_bytes() != content:
                print(f"reformatted {path}")
    checked = execute(ctx, [tool, "--dry-run", "--Werror", *paths], "clang-check")
    return diagnostics(checked, "format", paths[0])


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
    if not paths:
        return []
    if rule == "mypy":
        result = execute(
            ctx, [binary(ctx, "mypy"), "--strict", "--no-error-summary", *paths], "mypy"
        )
        return diagnostics(result, rule, paths[0])
    result = execute(
        ctx, [binary(ctx, "ruff"), "check", "--output-format=json", *paths], "ruff"
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
