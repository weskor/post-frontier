"""Lexical workaround, include-boundary and source-size checks."""

import ast
import io
from pathlib import Path
import re
import tokenize

from x.lint.model import CODE, Finding

PATTERNS = {
    "suppression": r"\b(?:NOLINT\w*|noqa|type\s*:\s*ignore|pyright\s*:\s*ignore)\b|#\s*if\s+0\b|clang-format\s+off",
    "disabled-test": r"EAutomationTestFlags::Disabled|pytest\.mark\.(?:skip\w*|xfail)\b|\bskipif\b|unittest\.skip\w*\b",
    "marker": r"\b(?:TODO|FIXME|HACK|XXX)\b|raise\s+NotImplementedError\b|\bunimplemented\s*\(",
    "direct-engine": r"\b(?:UnrealEditor(?:-Cmd)?|Build\.sh|RunUAT\w*|UnrealBuildTool|GenerateProjectFiles\w*)\b",
}
LEXEMES = re.compile(
    r'R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
    re.DOTALL,
)


def blank(match: re.Match[str]) -> str:
    return "".join("\n" if char == "\n" else " " for char in match.group())


def python_visible(text: str) -> str:
    lines = text.splitlines(keepends=True)
    try:
        tokens = tokenize.generate_tokens(io.StringIO(text).readline)
        for token in tokens:
            if token.type != tokenize.STRING:
                continue
            (start, col), (end, end_col) = token.start, token.end
            for line in range(start - 1, end):
                left = col if line == start - 1 else 0
                right = end_col if line == end - 1 else len(lines[line].rstrip("\n"))
                lines[line] = (
                    lines[line][:left] + " " * (right - left) + lines[line][right:]
                )
    except (tokenize.TokenError, IndentationError):
        pass
    return "".join(lines)


def cpp_functions(text: str) -> list[tuple[int, int]]:
    """Count full definitions, not nested control blocks or lambda bodies."""
    clean = LEXEMES.sub(blank, text)
    clean = re.sub(r"^\s*#.*$", blank, clean, flags=re.MULTILINE)
    tokens = list(re.finditer(r"[A-Za-z_]\w*|::|[^\s]", clean))
    stack: list[tuple[int, bool]] = []
    spans: list[tuple[int, int]] = []
    boundary = 0
    for token in tokens:
        symbol = token.group()
        if symbol == "{":
            prefix = clean[boundary : token.start()]
            function = bool(
                re.search(
                    r"\b(?!if\b|for\b|while\b|switch\b|catch\b)([\w:~]+)\s*"
                    r"\([^;{}]*\)\s*(?:const\s*|override\s*|final\s*|noexcept(?:\([^)]*\))?\s*|->\s*[\w:<>&* ]+)*$",
                    prefix,
                )
            )
            # A parent function owns every nested block, including lambdas.
            function = function and not any(item[1] for item in stack)
            declaration = re.sub(
                r"^\s*(?:(?:public|protected|private)\s*:\s*|"
                r"U(?:FUNCTION|CONSTRUCTOR)\([^\n]*\)\s*)*",
                "",
                prefix,
            )
            signature = token.start() - len(declaration.lstrip())
            stack.append((signature, function))
            boundary = token.end()
        elif symbol == "}":
            if stack:
                start, function = stack.pop()
                if function:
                    spans.append(
                        (
                            clean.count("\n", 0, start) + 1,
                            clean.count("\n", 0, token.end()) + 1,
                        )
                    )
            boundary = token.end()
        elif symbol == ";":
            boundary = token.end()
    return spans


def functions(path: str, text: str) -> list[tuple[int, int]]:
    if Path(path).suffix == ".py" or path == "x":
        try:
            tree = ast.parse(text)
        except SyntaxError:
            return []  # The Python tools report invalid syntax.
        return [
            (node.lineno, node.end_lineno or node.lineno)
            for node in ast.walk(tree)
            if isinstance(node, ast.FunctionDef | ast.AsyncFunctionDef)
        ]
    return cpp_functions(text)


def scan(rule: str, path: str, text: str) -> list[Finding]:
    suffix = Path(path).suffix
    if suffix not in CODE and path != "x":
        return []
    if rule == "direct-engine" and path.startswith(("Tools/x/", "Tools/harness/")):
        return []
    if rule in PATTERNS:
        visible = python_visible(text) if suffix == ".py" or path == "x" else text
        if suffix != ".py" and path != "x" and rule != "direct-engine":
            visible = LEXEMES.sub(
                lambda m: m.group() if m.group().startswith(("//", "/*")) else blank(m),
                text,
            )
        if rule == "direct-engine":
            visible = text
        return [
            Finding(
                path,
                visible.count("\n", 0, match.start()) + 1,
                rule,
                f"forbidden {match.group()}",
            )
            for match in re.finditer(PATTERNS[rule], visible)
            if not (
                rule == "direct-engine"
                and re.search(
                    r"\busing\s+$",
                    visible[visible.rfind("\n", 0, match.start()) + 1 : match.start()],
                )
            )
        ]
    if rule == "rules-includes":
        return includes(path, text)
    if suffix not in {".h", ".cpp", ".py"} and path != "x":
        return []
    if rule == "file-length" and len(text.splitlines()) > 500:
        return [
            Finding(path, 501, rule, f"{len(text.splitlines())} lines; maximum 500")
        ]
    if rule == "function-length":
        return [
            Finding(
                path, start, rule, f"function has {end - start + 1} lines; maximum 60"
            )
            for start, end in functions(path, text)
            if end - start + 1 > 60
        ]
    return []


def includes(
    path: str, text: str, *, headers: frozenset[str] = frozenset()
) -> list[Finding]:
    result: list[Finding] = []
    literals = [(m.start(), m.end()) for m in LEXEMES.finditer(text)]
    for match in re.finditer(
        r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', text, re.MULTILINE
    ):
        if any(start <= match.start() < end for start, end in literals):
            continue
        header = match.group(1)
        allowed = (
            header == "CoreMinimal.h"
            or header in headers
            or header == f"{Path(path).stem}.h"
            or (
                header.startswith("Rules/")
                and header.endswith(".h")
                and not header.endswith(".generated.h")
                and ".." not in Path(header).parts
            )
            or header == f"{Path(path).stem}.generated.h"
        )
        if not allowed:
            result.append(
                Finding(
                    path,
                    text.count("\n", 0, match.start()) + 1,
                    "rules-includes",
                    f"forbidden include {header}",
                )
            )
    return result
