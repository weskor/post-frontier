"""Run-specific package publication, retention and content-hash launch checks."""

import shutil
from pathlib import Path

from x import freshness, gitinfo, jsonio

CONFIGS = ("development", "shipping")


def package_executable(directory: Path, target: str, config: str) -> Path:
    name = target if config == "development" else f"{target}-Linux-Shipping"
    candidates = sorted(directory.glob(f"**/{target}/Binaries/Linux/{name}"))
    if len(candidates) != 1 or not candidates[0].is_file():
        raise ValueError(
            f"package has no unique {name} executable; run ./x package {config}"
        )
    return candidates[0]


def latest_package(repo: Path, config: str, target: str) -> Path:
    latest = repo / "Saved/Packages" / config / "latest"
    if not latest.is_symlink() or not (latest / "package.json").is_file():
        raise ValueError(f"no {config} package; run ./x package {config}")
    metadata = jsonio.load(latest / "package.json")
    if metadata.get("config") != config or metadata.get(
        "package_hash"
    ) != freshness.current_hash(repo, "package"):
        raise ValueError(f"stale {config} package; run ./x package {config}")
    return package_executable(latest.resolve(), target, config)


def retain_packages(root: Path) -> None:
    packages = sorted(
        (
            path
            for path in root.iterdir()
            if not path.is_symlink()
            and path.is_dir()
            and (path / "package.json").is_file()
        ),
        key=lambda path: path.name,
        reverse=True,
    )
    for old in packages[2:]:
        shutil.rmtree(old)


def publish(
    repo: Path, directory: Path, config: str, run_id: str, package_hash: str
) -> None:
    jsonio.save(
        directory / "package.json",
        {
            "run_id": run_id,
            "config": config,
            "commit": gitinfo.commit(repo),
            "package_hash": package_hash,
        },
    )
    temporary = directory.parent / ".latest-new"
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(directory.name, target_is_directory=True)
    temporary.replace(directory.parent / "latest")
    retain_packages(directory.parent)
