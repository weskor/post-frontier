"""Run-specific package publication, retention and content-hash launch checks."""

from datetime import UTC, datetime
from pathlib import Path
import shutil
from typing import Any

from x import freshness, gitinfo, jsonio
from x.content import playtest

CONFIGS = ("development", "shipping")


def publication_order(metadata: dict[str, Any]) -> str:
    # Legacy run IDs have second precision; their random suffix is not a clock.
    return str(metadata.get("published_at", metadata.get("run_id", "")[:15]))


def package_executable(directory: Path, target: str, config: str) -> Path:
    name = target if config == "development" else f"{target}-Linux-Shipping"
    candidates = sorted(directory.glob(f"**/{target}/Binaries/Linux/{name}"))
    if len(candidates) != 1 or not candidates[0].is_file():
        raise ValueError(
            f"package has no unique {name} executable; run ./x package {config}"
        )
    return candidates[0]


def latest_package_directory(repo: Path, config: str) -> Path:
    variants = (config, "playtest") if config == "shipping" else (config,)
    candidates = []
    for variant in variants:
        latest = repo / "Saved/Packages" / variant / "latest"
        if latest.is_symlink() and (latest / "package.json").is_file():
            candidates.append((latest.resolve(), jsonio.load(latest / "package.json")))
    if not candidates:
        raise ValueError(f"no {config} package; run ./x package {config}")
    for directory, metadata in sorted(
        candidates, key=lambda item: publication_order(item[1]), reverse=True
    ):
        expected = freshness.current_hash(repo, "package")
        if metadata.get("variant") == "playtest":
            expected = playtest.input_hash(repo, expected)
        if (
            metadata.get("config") == config
            and metadata.get("package_hash") == expected
        ):
            return directory
    raise ValueError(f"stale {config} package; run ./x package {config}")


def latest_package(repo: Path, config: str, target: str) -> Path:
    return package_executable(latest_package_directory(repo, config), target, config)


def retain_packages(root: Path) -> None:
    packages = sorted(
        (
            path
            for path in root.iterdir()
            if not path.is_symlink()
            and path.is_dir()
            and (path / "package.json").is_file()
        ),
        key=lambda path: (
            publication_order(jsonio.load(path / "package.json"))
            if root.name == "playtest"
            else path.name
        ),
        reverse=True,
    )
    for old in packages[2:]:
        shutil.rmtree(old)


def publish(
    repo: Path,
    directory: Path,
    config: str,
    run_id: str,
    package_hash: str,
    *,
    details: dict[str, Any] | None = None,
    archive: Path | None = None,
) -> None:
    jsonio.save(
        directory / "package.json",
        {
            "run_id": run_id,
            "config": config,
            "commit": gitinfo.commit(repo),
            "package_hash": package_hash,
            "published_at": datetime.now(UTC).strftime("%Y%m%d-%H%M%S-%f"),
            **(details or {}),
        },
    )
    if archive is not None:
        playtest.compress(directory, archive)
        current = playtest.input_hash(repo)
        if current != package_hash or gitinfo.commit(repo) != (details or {}).get(
            "commit"
        ):
            raise ValueError(
                "package inputs changed during archiving; run ./x package again"
            )
    temporary = directory.parent / ".latest-new"
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(directory.name, target_is_directory=True)
    temporary.replace(directory.parent / "latest")
    retain_packages(directory.parent)
