"""Conservative C++ preprocessor boundaries for development-only symbols."""

from dataclasses import dataclass
import re

from x.lint.model import Finding, TestOnlySymbols

ALL = 0b1111
DEV_ONLY = 0b0010
LEXEMES = re.compile(
    r'R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
    re.DOTALL,
)
TOKENS = re.compile(r"[A-Za-z_]\w*|[01]|&&|\|\||[!()]")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


@dataclass(frozen=True)
class Truth:
    """Possible true/false worlds, indexed by the two macro values."""

    yes: int = ALL
    no: int = ALL

    def negate(self) -> "Truth":
        return Truth(self.no, self.yes)


@dataclass
class Expression:
    tokens: list[str]
    index: int = 0

    def take(self, token: str) -> bool:
        if self.index < len(self.tokens) and self.tokens[self.index] == token:
            self.index += 1
            return True
        return False

    def either(self) -> Truth:
        value = self.both()
        while self.take("||"):
            right = self.both()
            value = Truth(value.yes | right.yes, value.no & right.no)
        return value

    def both(self) -> Truth:
        value = self.atom()
        while self.take("&&"):
            right = self.atom()
            value = Truth(value.yes & right.yes, value.no | right.no)
        return value

    def atom(self) -> Truth:
        if self.take("!"):
            return self.atom().negate()
        if self.take("("):
            value = self.either()
            if not self.take(")"):
                raise ValueError("missing closing parenthesis")
            return value
        if self.take("defined"):
            parenthesized = self.take("(")
            self.name()
            if parenthesized and not self.take(")"):
                raise ValueError("missing defined closing parenthesis")
            return Truth()
        if self.take("0"):
            return Truth(0, ALL)
        if self.take("1"):
            return Truth(ALL, 0)
        name = self.name()
        yes = {"WITH_DEV_AUTOMATION_TESTS": 0b1010, "UE_BUILD_SHIPPING": 0b1100}
        return Truth(yes[name], ALL ^ yes[name]) if name in yes else Truth()

    def name(self) -> str:
        if self.index >= len(self.tokens):
            raise ValueError("missing identifier")
        name = self.tokens[self.index]
        if IDENTIFIER.fullmatch(name) is None:
            raise ValueError("invalid identifier")
        self.index += 1
        return name


def condition(text: str) -> Truth:
    """Unsupported syntax can be true or false; it never proves a guard."""
    matches = list(TOKENS.finditer(text))
    position = 0
    for match in matches:
        if text[position : match.start()].strip():
            return Truth()
        position = match.end()
    if text[position:].strip():
        return Truth()
    parser = Expression([match.group() for match in matches])
    try:
        result = parser.either()
        return result if parser.index == len(parser.tokens) else Truth()
    except (ValueError, RecursionError):
        return Truth()


@dataclass
class Branch:
    parent: int
    remaining: int
    saw_else: bool = False


def branch(kind: str, expression: str, active: int, stack: list[Branch]) -> int:
    if kind in {"if", "ifdef", "ifndef"}:
        value = condition(expression) if kind == "if" else Truth()
        stack.append(Branch(active, active & value.no))
        return active & value.yes
    if not stack:
        return ALL
    frame = stack[-1]
    if kind == "endif":
        stack.pop()
        return frame.parent
    if frame.saw_else:
        return frame.parent
    if kind == "else":
        frame.saw_else = True
        return frame.remaining
    value = condition(expression)
    active = frame.remaining & value.yes
    frame.remaining &= value.no
    return active


def splice(text: str) -> tuple[str, list[int]]:
    """Join C++ logical lines before recognizing comments or directives."""
    pieces: list[str] = []
    lines: list[int] = []
    position, line = 0, 1
    for match in re.finditer(r"\\\r?\n", text):
        chunk = text[position : match.start()]
        pieces.append(chunk)
        for char in chunk:
            lines.append(line)
            line += char == "\n"
        line += 1
        position = match.end()
    pieces.append(text[position:])
    for char in text[position:]:
        lines.append(line)
        line += char == "\n"
    return "".join(pieces), lines


def masks(text: str) -> tuple[str, str, list[re.Match[str]]]:
    code: list[str] = []
    directives: list[str] = []
    strings: list[re.Match[str]] = []
    position = 0
    for match in LEXEMES.finditer(text):
        chunk = text[position : match.start()]
        code.append(chunk)
        directives.append(chunk)
        literal = not match.group().startswith(("//", "/*"))
        code.append("".join("\n" if c == "\n" else " " for c in match.group()))
        marker = "@" if literal else " "
        directives.append("".join("\n" if c == "\n" else marker for c in match.group()))
        if literal and not match.group().startswith("'"):
            strings.append(match)
        position = match.end()
    code.append(text[position:])
    directives.append(text[position:])
    return "".join(code), "".join(directives), strings


def uses(
    code: str,
    strings: list[re.Match[str]],
    settings: TestOnlySymbols,
) -> list[tuple[int, str]]:
    identifiers = frozenset(settings.identifiers)
    result = [
        (match.start(), match.group())
        for match in IDENTIFIER.finditer(code)
        if match.group() in identifiers
    ]
    if settings.flags:
        names = "|".join(re.escape(flag) for flag in settings.flags)
        pattern = re.compile(rf"(?<![\w.-])(?:-)?(?:{names})(?![\w.-])")
        for literal in strings:
            delimiter = literal.group("delimiter")
            start = len(delimiter) + 3 if delimiter is not None else 1
            end = -len(delimiter) - 2 if delimiter is not None else -1
            result.extend(
                (literal.start() + start + match.start(), match.group())
                for match in pattern.finditer(literal.group()[start:end])
            )
    return sorted(result)


def scan(path: str, text: str, settings: TestOnlySymbols) -> list[Finding]:
    text, lines = splice(text)
    code, directives, strings = masks(text)
    occurrences = iter(uses(code, strings, settings))
    occurrence = next(occurrences, None)
    findings: list[Finding] = []
    stack: list[Branch] = []
    active, offset = ALL, 0
    for line in directives.splitlines(keepends=True):
        directive = DIRECTIVE.fullmatch(line.rstrip("\r\n"))
        line_active = active
        if directive and directive[1] == "elif" and stack:
            line_active = stack[-1].remaining
        if directive:
            active = branch(directive[1], directive[2], active, stack)
        end = offset + len(line)
        while occurrence is not None and occurrence[0] < end:
            position, name = occurrence
            if line_active & ~DEV_ONLY:
                findings.append(
                    Finding(
                        path,
                        lines[position],
                        "test-only-symbol",
                        f"{name} requires WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING",
                    )
                )
            occurrence = next(occurrences, None)
        offset = end
    return findings
