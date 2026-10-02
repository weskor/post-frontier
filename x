#!/usr/bin/env python3
"""Repository-local entry point; runtime dependencies are standard-library only."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "Tools"))

from x.cli import main

if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
