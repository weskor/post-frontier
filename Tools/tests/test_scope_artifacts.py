"""A validator scope must not claim images left by preceding scopes."""

import sys
from dataclasses import replace
from pathlib import Path

from x.context import Context
from x.runs import Run
from x.settings import load
from x.testing import run_scopes


def test_script_records_only_created_or_rewritten_images(repo: Path) -> None:
    settings = replace(load(repo), runs_root=repo.parent / "runs")
    record = Run(repo, settings.runs_root, "test", ["test", "maps"])
    ctx = Context(repo, settings, record, "test")
    unrelated = record.dir / "preceding.png"
    unrelated.write_bytes(b"preceding scope image")
    rewritten = record.dir / "rewritten.svg"
    rewritten.write_text("old validator output")
    script = repo / "validator.py"
    script.write_text(
        "import os\nfrom pathlib import Path\n"
        "run = Path(os.environ['X_RUN_DIR'])\n"
        "(run / 'rewritten.svg').write_text('<svg>updated</svg>')\n"
        "(run / 'nested').mkdir()\n"
        "(run / 'nested/new.svg').write_text('<svg>new</svg>')\n"
    )
    (repo / "Tools/x/scopes.toml").write_text(
        '[scopes.maps]\nkind = "script"\n'
        f'commands = [["{sys.executable}", "validator.py"]]\n[paths]\n'
    )
    assert run_scopes(ctx, ["maps"])
    assert {artifact["path"] for artifact in record.record["artifacts"]} == {
        str(rewritten),
        str(record.dir / "nested/new.svg"),
    }
    assert unrelated.read_bytes() == b"preceding scope image"
