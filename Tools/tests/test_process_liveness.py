"""A process reaped after opening procfs stat is no longer alive."""

import errno
from pathlib import Path
import subprocess
import sys

import pytest
from test_process import alive


def test_reaping_between_stat_open_and_read_is_not_alive(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
    observed: list[int | None] = []

    def read_after_reaping(
        path: Path, encoding: str | None = None, errors: str | None = None
    ) -> str:
        assert path == Path(f"/proc/{child.pid}/stat")
        with path.open(encoding=encoding, errors=errors) as stream:
            child.terminate()
            child.wait()
            try:
                return stream.read()
            except ProcessLookupError as error:
                observed.append(error.errno)
                raise

    monkeypatch.setattr(Path, "read_text", read_after_reaping)
    try:
        assert not alive(child.pid)
        assert observed == [errno.ESRCH]
    finally:
        if child.poll() is None:
            child.terminate()
        child.wait()
