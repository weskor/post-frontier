"""Canonical settings, with cook maps read directly from Unreal configuration."""

from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
import re
import tomllib
from types import MappingProxyType


@dataclass(frozen=True)
class Settings:
    engine_root: Path
    project: str
    editor_target: str
    game_target: str
    lock_dir: Path
    headless_pool_size: int
    runs_root: Path
    default_map: str
    playtest_map: str
    stall_seconds: float
    freshness: Mapping[str, tuple[str, ...]]
    repo: Path

    def maps(self) -> list[str]:
        text = (self.repo / "Config/DefaultGame.ini").read_text()
        return re.findall(
            r'^\s*\+MapsToCook\s*=\s*\(FilePath="([^"]+)"\)\s*$', text, re.MULTILINE
        )


def load(repo: Path) -> Settings:
    with (repo / "Tools/x/settings.toml").open("rb") as source:
        data = tomllib.load(source)
    if data["headless_pool_size"] < 1 or data["stall_seconds"] <= 0:
        raise ValueError("pool size and stall duration must be positive")
    return Settings(
        engine_root=Path(data["engine_root"]).expanduser(),
        project=data["project"],
        editor_target=data["editor_target"],
        game_target=data["game_target"],
        lock_dir=Path(data["lock_dir"]).expanduser(),
        headless_pool_size=data["headless_pool_size"],
        runs_root=Path(data["runs_root"]).expanduser(),
        default_map=data["default_map"],
        playtest_map=data["playtest_map"],
        stall_seconds=float(data["stall_seconds"]),
        freshness=MappingProxyType(
            {key: tuple(value) for key, value in data["freshness"].items()}
        ),
        repo=repo,
    )
