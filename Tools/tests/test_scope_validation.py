"""Invalid scope configuration fails before any child can be launched."""

from pathlib import Path

import pytest
from x.scopes import load


@pytest.mark.parametrize(
    "spec,field",
    [
        ('kind = "automation"\nmap = "default"', "filter"),
        ('kind = "automation"\nfilter = " "\nmap = "default"', "filter"),
        ('kind = "automation"\nfilter = "CoopRTS.Rules"', "map"),
        ('kind = "automation"\nfilter = "CoopRTS.Rules"\nmap = ""', "map"),
        (
            'kind = "automation"\nfilter = "CoopRTS.Rules"\nmap = "/Game/Boot?listen"',
            "map",
        ),
        ('kind = "script"', "commands"),
        ('kind = "script"\ncommands = []', "commands"),
        ('kind = "script"\ncommands = [[]]', "commands"),
        ('kind = "script"\ncommands = [["python3"]]', "commands"),
        ('kind = "script"\ncommands = [["python3", ""]]', "commands"),
        ('kind = "script"\ncommands = [["python3", 1]]', "commands"),
        ('kind = "pytest"\npaths = []', "paths"),
    ],
)
def test_invalid_scope_fails_at_load(repo: Path, spec: str, field: str) -> None:
    (repo / "Tools/x/scopes.toml").write_text(f"[scopes.broken]\n{spec}\n[paths]\n")
    with pytest.raises(ValueError, match=f"scope broken: .*{field}"):
        load(repo)
