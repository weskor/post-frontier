"""Blocking diagnostics and the single rollout/exception configuration."""

from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from dataclasses import dataclass
from fnmatch import fnmatchcase
from functools import cache
from pathlib import Path
import tomllib

RULES = (
    "suppression",
    "disabled-test",
    "marker",
    "direct-engine",
    "saved-path",
    "procedure-text",
    "rules-includes",
    "scope-map",
    "file-length",
    "function-length",
    "format",
    "ruff",
    "mypy",
)
CODE = {".h", ".cpp", ".cs", ".py", ".c"}


@cache
def matches_parts(path: tuple[str, ...], pattern: tuple[str, ...]) -> bool:
    if not pattern:
        return not path
    if pattern[0] == "**":
        return matches_parts(path, pattern[1:]) or (
            bool(path) and matches_parts(path[1:], pattern)
        )
    return (
        bool(path)
        and fnmatchcase(path[0], pattern[0])
        and matches_parts(path[1:], pattern[1:])
    )


def matches(path: str, pattern: str) -> bool:
    return matches_parts(tuple(path.split("/")), tuple(pattern.split("/")))


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str
    message: str

    def __str__(self) -> str:
        message = " ".join(self.message.splitlines())
        return f"{self.path}:{self.line}: {self.rule}: {message}"


@dataclass(frozen=True)
class ExceptionEntry:
    rule: str
    path: str
    reason: str


@dataclass(frozen=True)
class Policy:
    rules: dict[str, list[str]]
    exceptions: Sequence[ExceptionEntry]

    def enabled(self, rule: str, path: str) -> bool:
        return any(matches(path, pattern) for pattern in self.rules.get(rule, []))


class PolicyError(ValueError):
    def __init__(self, path: str, error: Exception) -> None:
        self.path = path
        super().__init__(str(error))


@contextmanager
def configuration(path: str) -> Iterator[None]:
    try:
        yield
    except (OSError, ValueError, TypeError, KeyError) as error:
        raise PolicyError(path, error) from error


def load_rules(repo: Path) -> dict[str, list[str]]:
    with (repo / "Tools/x/lint.toml").open("rb") as source:
        rules = tomllib.load(source)["rules"]
    if not isinstance(rules, dict):
        raise ValueError("rules must be a table")
    if set(rules) != set(RULES):
        raise ValueError("lint.toml must configure every known rule exactly once")
    if any(
        not isinstance(v, list) or any(not isinstance(p, str) for p in v)
        for v in rules.values()
    ):
        raise ValueError("rule scopes must be lists of path globs")
    return rules


def load_exceptions(repo: Path, rules: dict[str, list[str]]) -> list[ExceptionEntry]:
    with (repo / "Tools/x/lint-exceptions.toml").open("rb") as source:
        entries = tomllib.load(source).get("exceptions", [])
    if not isinstance(entries, list) or any(
        not isinstance(entry, dict)
        or set(entry) != {"rule", "path", "reason"}
        or any(not isinstance(value, str) for value in entry.values())
        for entry in entries
    ):
        raise ValueError("exceptions must be entries with string rule, path and reason")
    exceptions = [ExceptionEntry(**entry) for entry in entries]
    seen: set[tuple[str, str]] = set()
    for entry in exceptions:
        key = (entry.rule, entry.path)
        if (
            entry.rule not in RULES
            or not entry.reason.strip()
            or key in seen
            or not load_path(entry.path)
            or not any(matches(entry.path, p) for p in rules[entry.rule])
        ):
            raise ValueError(f"invalid or duplicate exception: {entry}")
        seen.add(key)
    return exceptions


def load(repo: Path) -> Policy:
    with configuration("Tools/x/lint.toml"):
        rules = load_rules(repo)
    with configuration("Tools/x/lint-exceptions.toml"):
        exceptions = load_exceptions(repo, rules)
    return Policy(rules, exceptions)


def load_path(path: str) -> bool:
    return bool(path) and not Path(path).is_absolute() and ".." not in Path(path).parts


def apply_exceptions(findings: list[Finding], policy: Policy) -> list[Finding]:
    keys = {(entry.rule, entry.path) for entry in policy.exceptions}
    used = {(item.rule, item.path) for item in findings} & keys
    result = [item for item in findings if (item.rule, item.path) not in keys]
    for entry in policy.exceptions:
        if (entry.rule, entry.path) not in used:
            result.append(
                Finding(
                    "Tools/x/lint-exceptions.toml",
                    1,
                    entry.rule,
                    f"unused exception for {entry.path}: {entry.reason}",
                )
            )
    return result
