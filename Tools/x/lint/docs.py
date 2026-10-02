"""Documentation cites durable evidence and links to runner procedures."""

from pathlib import PurePosixPath
import re
import shlex

from x.lint.model import Finding

COMMAND = re.compile(
    r"^(?:UnrealEditor(?:-Cmd)?|Build\.sh|RunUAT\w*|UnrealBuildTool|"
    r"verify\.py|network\.py|hud_capture\.py|network_desktop\.py|SimulateMatches(?:\.py)?)$"
)


def invocation(text: str, *, block: bool) -> bool:
    try:
        tokens = shlex.split(text, comments=True)
    except ValueError:
        return False
    while tokens and (tokens[0] == "$" or re.match(r"^[A-Za-z_]\w*=", tokens[0])):
        tokens.pop(0)
    if not tokens:
        return False
    name = PurePosixPath(tokens[0]).name
    if name == "flock":
        return len(tokens) > 2
    if name in {"python", "python3"} and len(tokens) > 1:
        script = tokens[1]
        return script.startswith("Build/") or bool(
            COMMAND.fullmatch(PurePosixPath(script).name)
        )
    if name == "uv":
        return len(tokens) > 1 and tokens[1] == "run"
    if name == "blender":
        return "-b" in tokens[1:]
    return bool(COMMAND.fullmatch(name)) and (block or len(tokens) > 1)


def procedure_lines(text: str) -> list[int]:
    lines: list[int] = []
    fence: str | None = None
    for number, line in enumerate(text.splitlines(), 1):
        delimiter = re.match(r"^\s*(`{3,}|~{3,})", line)
        if delimiter:
            mark = delimiter.group(1)[0]
            if fence is None:
                fence = mark
            elif fence == mark:
                fence = None
            continue
        snippets = [line] if fence else re.findall(r"`+([^`]+)`+", line)
        if any(
            invocation(part.strip(), block=fence is not None)
            for snippet in snippets
            for part in re.split(r"&&|;|\|", snippet)
        ):
            lines.append(number)
    return lines


def scan(rule: str, path: str, text: str) -> list[Finding]:
    if not path.endswith(".md"):
        return []
    if rule == "saved-path":
        return [
            Finding(
                path,
                text.count("\n", 0, match.start()) + 1,
                rule,
                "evidence under Saved/ is not shared; cite durable evidence",
            )
            for match in re.finditer(r"(?<![\w])Saved/[^\s`\])>]*", text)
        ]
    if rule == "procedure-text":
        return [
            Finding(path, line, rule, "procedure bypasses ./x; link to ./x help")
            for line in procedure_lines(text)
        ]
    return []
