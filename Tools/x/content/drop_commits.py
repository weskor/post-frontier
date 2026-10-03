"""Git sequence editor: turn `pick` lines of the commits in X_DROP_SHAS into `drop`.

Standalone (no x imports): git runs it as `python drop_commits.py <todo>`. The todo
lists abbreviated hashes, so each is matched as a prefix of the full ones.
"""

import os
from pathlib import Path
import sys


def drop(todo: Path, shas: list[str]) -> None:
    lines = todo.read_text().splitlines(keepends=True)
    rewritten: list[str] = []
    for line in lines:
        fields = line.split()
        picked = (
            len(fields) > 1
            and fields[0] in ("pick", "p")
            and any(sha.startswith(fields[1]) for sha in shas)
        )
        rewritten.append(line.replace(fields[0], "drop", 1) if picked else line)
    todo.write_text("".join(rewritten))


if __name__ == "__main__":
    drop(Path(sys.argv[1]), os.environ["X_DROP_SHAS"].split())
