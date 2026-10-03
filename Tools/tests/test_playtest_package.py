"""Isolated artifact/publication/launch behavior; these tests never invoke Unreal."""

import argparse
from dataclasses import replace
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

from conftest import git
import pytest
from x import freshness, gitinfo, jsonio
from x.commands import package, play
from x.content import packages, playtest
from x.context import Context
from x.runs import Run
from x.settings import load

GAME = """import json
import os
from pathlib import Path
import signal
import sys
import time

if "--receipt" in sys.argv:
    path = Path(sys.argv[sys.argv.index("--receipt") + 1])
    path.write_text(json.dumps({"cwd": os.getcwd(), "appid": os.environ.get("SteamAppId"),
        "gameid": os.environ.get("SteamGameId"), "args": sys.argv[1:],
        "staged_id": Path("steam_appid.txt").read_text().strip()}))
    raise SystemExit(0)
if len(sys.argv) < 2:
    raise SystemExit(2)
map_name = sys.argv[1].rsplit("/", 1)[-1]
if not (Path(__file__).resolve().parents[2] / "Content/Maps" / (map_name + ".umap")).is_file():
    raise SystemExit(3)
log = next((Path(arg.partition("=")[2]) for arg in sys.argv if arg.startswith("-abslog=")), None)
def shutdown(*args):
    if log is not None:
        with log.open("a") as output:
            output.write("LogExit: Exiting.\\n")
    sys.exit(143)
signal.signal(signal.SIGTERM, shutdown)
if log is not None:
    log.write_text(f"LogLoad: Took 0.01 seconds to LoadMap({sys.argv[1]})\\n")
print("Game alive", flush=True)
time.sleep(90)
"""


@pytest.fixture
def playtest_repo(repo: Path) -> Path:
    source = Path(__file__).parents[1]
    shutil.copytree(
        source / "x",
        repo / "Tools/x",
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("__pycache__"),
    )
    (repo / "README.md").write_text(
        "### Controls in current source\n\nBindings live in source.\n\n"
        "| Input | Action |\n| --- | --- |\n"
        "| WASD | Pan |\n| G | Ping ([Ping rules](Docs/Design/ui.md)) |\n\n"
        "### Other section\nNot a control.\n"
    )
    ignored = repo / ".gitignore"
    ignored.write_text(ignored.read_text() + "Saved/\n__pycache__/\n")
    git(repo, "add", ".")
    git(repo, "commit", "-m", "authored playtest fixture inputs")
    return repo


def context(repo: Path, tmp_path: Path, *, mode: str = "success") -> Context:
    """A small cooker emits a real executable and only the selected map assets."""
    settings = replace(
        load(repo),
        engine_root=tmp_path / "engine",
        lock_dir=tmp_path / "locks",
        runs_root=tmp_path / "runs",
    )
    cooker = settings.engine_root / package.UAT_SCRIPT
    cooker.parent.mkdir(parents=True, exist_ok=True)
    cooker.write_text(
        f"#!{sys.executable}\n"
        "from pathlib import Path\nimport sys\n"
        "options = dict(arg[1:].split('=', 1) for arg in sys.argv[1:] if '=' in arg)\n"
        "root = Path(options['archivedirectory'])\n"
        "game = root / 'Linux/CoopRTS'\n"
        "game.mkdir(parents=True)\n"
        "(game / 'payload.pak').write_text('packaged content')\n"
        f"mode = {mode!r}\n"
        "if mode == 'failure': sys.exit(7)\n"
        "if mode == 'incomplete': sys.exit(0)\n"
        "name = 'CoopRTS-Linux-Shipping' if options['clientconfig'] == 'Shipping' else 'CoopRTS'\n"
        "binary = game / 'Binaries/Linux' / name\n"
        "binary.parent.mkdir(parents=True)\n"
        f"binary.write_text({('#!' + sys.executable + chr(10) + GAME)!r})\n"
        "binary.chmod(0o755)\n"
        "maps = game / 'Content/Maps'\n"
        "maps.mkdir(parents=True)\n"
        "for name in options['map'].split('+'):\n"
        "    (maps / (name.rsplit('/', 1)[-1] + '.umap')).write_text('cooked map')\n"
        "repo = Path(options['project']).parent\n"
        "if mode == 'source-drift': (repo / 'Source/rules.cpp').write_text('changed during cook')\n"
        "if mode == 'readme-drift':\n"
        "    readme = repo / 'README.md'\n"
        "    readme.write_text(readme.read_text().replace('| WASD | Pan |', '| WASD | New action |'))\n"
        "if mode == 'untracked-drift': (repo / 'untracked.txt').write_text('created during cook')\n"
    )
    cooker.chmod(0o755)
    record = Run(repo, settings.runs_root, "package", ["package", "--playtest"])
    return Context(repo, settings, record, "package")


def run_package(
    ctx: Context, *, is_playtest: bool = True, config: str = "development"
) -> Path:
    args = argparse.Namespace(config=config, playtest=is_playtest)
    assert ctx.run is not None
    assert package.run(args, ctx) == 0
    variant = "playtest" if is_playtest else config
    return ctx.repo / "Saved/Packages" / variant / ctx.run.id


def ordinary_shipping(repo: Path, run_id: str = "0000-normal") -> Path:
    directory = repo / "Saved/Packages/shipping" / run_id
    executable = directory / "Linux/CoopRTS/Binaries/Linux/CoopRTS-Linux-Shipping"
    executable.parent.mkdir(parents=True)
    executable.write_text("normal shipping")
    packages.publish(
        repo, directory, "shipping", run_id, freshness.current_hash(repo, "package")
    )
    return directory


def ordinary_development(repo: Path, run_id: str = "0000-development") -> Path:
    directory = repo / "Saved/Packages/development" / run_id
    executable = directory / "Linux/CoopRTS/Binaries/Linux/CoopRTS"
    executable.parent.mkdir(parents=True)
    executable.write_text("normal development")
    packages.publish(
        repo, directory, "development", run_id, freshness.current_hash(repo, "package")
    )
    return directory


def archive_bytes(bundle: tarfile.TarFile, name: str) -> bytes:
    stream = bundle.extractfile(name)
    assert stream is not None
    return stream.read()


@pytest.mark.parametrize("requested_config", ["development", "shipping"])
def test_distributable_contains_full_development_payload_controls_identity_and_metadata(
    playtest_repo: Path, tmp_path: Path, requested_config: str
) -> None:
    repo = playtest_repo
    previous = ordinary_shipping(repo)
    development = ordinary_development(repo)
    directory = run_package(context(repo, tmp_path), config=requested_config)
    metadata = jsonio.load(directory / "package.json")
    commit = gitinfo.commit(repo)
    assert metadata["config"] == "development"
    assert metadata["variant"] == "playtest"
    assert metadata["commit"] == commit
    assert metadata["steam_app_id"] == 480
    assert metadata["steam_identity"] == "staged-appid-and-launcher-env"
    assert metadata["maps"] == list(playtest.MAPS)
    assert metadata["default_map"] == playtest.DEFAULT_MAP
    assert metadata["package_hash"] == playtest.input_hash(repo)
    assert (repo / "Saved/Packages/shipping/latest").resolve() == previous
    assert (repo / "Saved/Packages/development/latest").resolve() == development
    assert packages.latest_package_directory(repo, "shipping") == previous
    assert packages.latest_package_directory(repo, "development") == directory
    assert not gitinfo.is_dirty(repo)
    assert (directory.parent / "latest").resolve() == directory
    assert not (repo / "Builds").exists()
    archives = list(directory.glob("*.tar.gz"))
    assert [path.name for path in archives] == [f"CoopRTS-playtest-{commit}.tar.gz"]
    with tarfile.open(archives[0]) as bundle:
        prefix = f"CoopRTS-playtest-{commit}"
        names = set(bundle.getnames())
        assert f"{prefix}/Linux/CoopRTS/payload.pak" in names
        assert f"{prefix}/Linux/CoopRTS/Binaries/Linux/CoopRTS" in names
        assert (
            archive_bytes(
                bundle, f"{prefix}/Linux/CoopRTS/Binaries/Linux/steam_appid.txt"
            )
            == b"480\n"
        )
        instructions = archive_bytes(bundle, f"{prefix}/PLAYTEST.txt").decode()
        assert "| WASD | Pan |" in instructions
        assert "Ping (Ping rules)" in instructions
        assert "Docs/Design/ui.md" not in instructions
        assert json.loads(archive_bytes(bundle, f"{prefix}/package.json")) == metadata
        assert bundle.getmember(f"{prefix}/PLAYTEST.sh").mode & 0o111
        cooked = {Path(name).stem for name in names if name.endswith(".umap")}
        assert cooked == {"Menu", "AvailabilityZoneV2", "AvailabilityZone"}
        assert not any(name.endswith(".tar.gz") for name in names)


@pytest.mark.parametrize("change", ["tracked", "staged", "untracked"])
def test_dirty_playtest_refuses_before_cook_and_preserves_all_publications(
    playtest_repo: Path, tmp_path: Path, change: str
) -> None:
    repo = playtest_repo
    successful = run_package(context(repo, tmp_path))
    development = ordinary_development(repo)
    shipping = ordinary_shipping(repo)
    archive = next(successful.glob("*.tar.gz"))
    previous_archive = archive.read_bytes()
    pointers = {
        path.parent / "latest": (path.parent / "latest").readlink()
        for path in (successful, development, shipping)
    }
    temporary = successful.parent / ".latest-new"
    temporary.symlink_to(successful.name, target_is_directory=True)
    if change == "untracked":
        # Deliberately outside the package hash: dirty attribution still refuses.
        (repo / "untracked.txt").write_text("uncommitted authored input")
    else:
        (repo / "Source/rules.cpp").write_text("uncommitted source")
        if change == "staged":
            git(repo, "add", "Source/rules.cpp")
    assert gitinfo.is_dirty(repo)
    ctx = context(repo, tmp_path)
    assert ctx.run is not None
    directory = successful.parent / ctx.run.id
    with pytest.raises(ValueError, match="requires a clean Git tree"):
        package.run(argparse.Namespace(config="development", playtest=True), ctx)
    assert not (ctx.run.dir / "package.log").exists()
    assert ctx.run.record["execs"] == []
    assert not directory.exists()
    assert archive.read_bytes() == previous_archive
    assert temporary.readlink() == Path(successful.name)
    assert all(path.readlink() == target for path, target in pointers.items())


def test_shipping_does_not_resolve_a_development_playtest(
    playtest_repo: Path, tmp_path: Path
) -> None:
    directory = run_package(context(playtest_repo, tmp_path))
    with pytest.raises(
        ValueError, match=r"no shipping package; run \./x package shipping"
    ):
        packages.latest_package_directory(playtest_repo, "shipping")
    assert packages.latest_package_directory(playtest_repo, "development") == directory


def test_launcher_runs_from_other_directory_with_test_identity_and_intact_arguments(
    playtest_repo: Path, tmp_path: Path
) -> None:
    directory = run_package(context(playtest_repo, tmp_path))
    moved = tmp_path / "extracted build with spaces"
    moved.mkdir()
    archive = next(directory.glob("*.tar.gz"))
    with tarfile.open(archive) as bundle:
        bundle.extractall(moved, filter="data")
    extracted = moved / archive.name.removesuffix(".tar.gz")
    receipt = tmp_path / "launch receipt.json"
    environment = {
        **os.environ,
        "STEAM_ROOT": str(tmp_path / "absent steam installation"),
    }
    environment.pop("LD_PRELOAD", None)
    subprocess.run(
        [extracted / "PLAYTEST.sh", "--receipt", receipt, "argument with spaces"],
        cwd=tmp_path,
        env=environment,
        check=True,
    )
    result = jsonio.load(receipt)
    assert result["appid"] == result["gameid"] == result["staged_id"] == "480"
    assert Path(result["cwd"]) == extracted / "Linux/CoopRTS/Binaries/Linux"
    assert result["args"] == ["--receipt", str(receipt), "argument with spaces"]


def test_controls_are_generated_again_and_only_playtest_freshness_tracks_readme(
    playtest_repo: Path, tmp_path: Path
) -> None:
    repo = playtest_repo
    previous = ordinary_development(repo)
    first = run_package(context(repo, tmp_path))
    assert packages.latest_package_directory(repo, "development") == first
    readme = repo / "README.md"
    original = readme.read_text()
    readme.write_text(original.replace("| WASD | Pan |", "| WASD | Updated pan |"))
    assert packages.latest_package_directory(repo, "development") == previous
    readme.write_text(original)
    assert packages.latest_package_directory(repo, "development") == first
    readme.write_text(original.replace("| WASD | Pan |", "| WASD | Updated pan |"))
    git(repo, "add", "README.md")
    git(repo, "commit", "-m", "updated generated controls")
    second = run_package(context(repo, tmp_path))
    assert "| WASD | Updated pan |" in (second / "PLAYTEST.txt").read_text()
    assert packages.latest_package_directory(repo, "development") == second
    (repo / "Source/rules.cpp").write_text("new source")
    with pytest.raises(ValueError, match="stale development package"):
        packages.latest_package_directory(repo, "development")


def test_newer_normal_development_takes_precedence_without_mutating_playtest(
    playtest_repo: Path, tmp_path: Path
) -> None:
    directory = run_package(context(playtest_repo, tmp_path))
    normal = ordinary_development(playtest_repo)
    assert packages.latest_package_directory(playtest_repo, "development") == normal
    assert (directory.parent / "latest").resolve() == directory
    helper = playtest_repo / "Tools/x/content/playtest.py"
    original = helper.read_text()
    helper.write_text(original + "\n# changed packaging implementation\n")
    assert packages.latest_package_directory(playtest_repo, "development") == normal
    assert jsonio.load(directory / "package.json")[
        "package_hash"
    ] != playtest.input_hash(playtest_repo)


@pytest.mark.parametrize("config", ["development", "shipping"])
def test_normal_packages_keep_configured_maps_and_have_no_test_steam_files(
    playtest_repo: Path, tmp_path: Path, config: str
) -> None:
    (playtest_repo / "Source/rules.cpp").write_text("local normal-package edit")
    assert gitinfo.is_dirty(playtest_repo)
    directory = run_package(
        context(playtest_repo, tmp_path), is_playtest=False, config=config
    )
    metadata = jsonio.load(directory / "package.json")
    assert metadata["config"] == config
    assert "variant" not in metadata and "steam_app_id" not in metadata
    assert not list(directory.glob("*.tar.gz"))
    assert not list(directory.glob("**/steam_appid.txt"))
    assert not (directory / "PLAYTEST.txt").exists()
    assert {path.stem for path in directory.glob("**/*.umap")} == {"Boot"}
    assert packages.latest_package_directory(playtest_repo, config) == directory
    assert gitinfo.is_dirty(playtest_repo)


@pytest.mark.parametrize(
    "mode", ["failure", "incomplete", "source-drift", "readme-drift", "untracked-drift"]
)
def test_failed_or_changed_cook_cleans_run_and_preserves_latest(
    playtest_repo: Path, tmp_path: Path, mode: str
) -> None:
    repo = playtest_repo
    successful = run_package(context(repo, tmp_path))
    ctx = context(repo, tmp_path, mode=mode)
    assert ctx.run is not None
    args = argparse.Namespace(config="development", playtest=True)
    if mode == "failure":
        assert package.run(args, ctx) == 7
    else:
        with pytest.raises(ValueError, match=r"no unique|inputs changed"):
            package.run(args, ctx)
    assert not (successful.parent / ctx.run.id).exists()
    assert (successful.parent / "latest").resolve() == successful
    assert len(list(successful.glob("*.tar.gz"))) == 1


@pytest.mark.parametrize(
    "phase", ["compress", "publish", "archive-drift", "untracked-archive-drift"]
)
def test_late_failure_cleans_metadata_archive_and_restores_published_pointer(
    playtest_repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, phase: str
) -> None:
    repo = playtest_repo
    successful = run_package(context(repo, tmp_path))
    ctx = context(repo, tmp_path)
    assert ctx.run is not None
    if phase == "compress":

        def fail_compression(directory: Path, archive: Path) -> None:
            archive.write_bytes(b"partial gzip")
            raise OSError("compression failed")

        monkeypatch.setattr(playtest, "compress", fail_compression)
    elif phase == "publish":

        def fail_retention(root: Path) -> None:
            raise OSError("retention failed")

        monkeypatch.setattr(packages, "retain_packages", fail_retention)
    else:
        compress = playtest.compress

        def change(directory: Path, archive: Path) -> None:
            compress(directory, archive)
            path = "Source/rules.cpp" if phase == "archive-drift" else "untracked.txt"
            (repo / path).write_text("changed during compression")

        monkeypatch.setattr(playtest, "compress", change)
    with pytest.raises((OSError, ValueError), match=r"failed|inputs changed"):
        package.run(argparse.Namespace(config="development", playtest=True), ctx)
    assert not (successful.parent / ctx.run.id).exists()
    assert not (successful.parent / ".latest-new").exists()
    assert (successful.parent / "latest").resolve() == successful


def test_missing_controls_fail_before_cooker_and_do_not_publish(
    playtest_repo: Path, tmp_path: Path
) -> None:
    ctx = context(playtest_repo, tmp_path)
    assert ctx.run is not None
    (playtest_repo / "README.md").write_text("no controls")
    git(playtest_repo, "add", "README.md")
    git(playtest_repo, "commit", "-m", "missing controls fixture")
    with pytest.raises(ValueError, match="Controls in current source"):
        package.run(argparse.Namespace(config="development", playtest=True), ctx)
    assert not (playtest_repo / "Saved/Packages/playtest" / ctx.run.id).exists()
    assert not (playtest_repo / "Saved/Packages/playtest/latest").exists()
    assert not (ctx.run.dir / "package.log").exists()


def test_playtest_retention_does_not_prune_ordinary_shipping(
    playtest_repo: Path, tmp_path: Path
) -> None:
    previous = ordinary_shipping(playtest_repo)
    first = run_package(context(playtest_repo, tmp_path))
    second = run_package(context(playtest_repo, tmp_path))
    third = run_package(context(playtest_repo, tmp_path))
    assert not first.exists()
    assert second.is_dir() and third.is_dir() and previous.is_dir()
    assert (previous.parent / "latest").resolve() == previous
    assert packages.latest_package_directory(playtest_repo, "shipping") == previous
    assert packages.latest_package_directory(playtest_repo, "development") == third


def test_development_smoke_reaches_cooked_playtest_map_without_requiring_boot(
    playtest_repo: Path, tmp_path: Path
) -> None:
    ctx = context(playtest_repo, tmp_path)
    directory = run_package(ctx)
    record = Run(
        playtest_repo,
        ctx.settings.runs_root,
        "play",
        ["play", "--smoke"],
    )
    launch = Context(playtest_repo, ctx.settings, record, "play")
    args = argparse.Namespace(
        shipping=False, smoke=True, map=None, steam=False, extra=[]
    )
    # Real owned child: the stub exits immediately unless the selected map was cooked.
    assert play.run(args, launch) == 0
    evidence = jsonio.load(record.dir / "smoke.json")
    assert evidence["requested_map"] == playtest.DEFAULT_MAP
    assert evidence["tier"] == "development-map"
    assert evidence["ready"] and evidence["shutdown"] and evidence["term_sent"]
    assert evidence["engine_exit_code"] == 143
    assert not evidence["failure"]
    assert not list(directory.glob("**/Boot.umap"))
