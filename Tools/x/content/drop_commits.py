"""Git sequence editor: remove `pick` lines whose subject is argv[2] from todo file argv[1].

Standalone (no x imports): git runs it as `python drop_commits.py <todo>` with the
subject in X_DROP_SUBJECT.
"""

import os
from pathlib import Path
import sys


def drop(todo: Path, subject: str) -> None:
    lines = todo.read_text().splitlines(keepends=True)
    todo.write_text(
        "".join(
            line
            for line in lines
            if not (line.startswith("pick ") and line.rstrip().endswith(" " + subject))
        )
    )


if __name__ == "__main__":
    drop(Path(sys.argv[1]), os.environ["X_DROP_SUBJECT"])
