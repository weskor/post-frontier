"""Distributable Linux test-Steam package contents, not release configuration."""

import hashlib
from pathlib import Path
import re
import shlex
import tarfile

from x import freshness

# Both choices are reachable in CoopSessionSubsystem::GetSelectedMap().
MAPS = (
    "/Game/Maps/Menu",
    "/Game/Maps/AvailabilityZoneV2",
    "/Game/Maps/AvailabilityZone",
)
DEFAULT_MAP = MAPS[1]
INPUTS = (
    "README.md",
    "Tools/x/commands/package.py",
    "Tools/x/content/packages.py",
    "Tools/x/content/playtest.py",
)


def input_hash(repo: Path, package_hash: str | None = None) -> str:
    digest = hashlib.sha256(
        (package_hash or freshness.current_hash(repo, "package")).encode()
    )
    for relative in INPUTS:
        content = (repo / relative).read_bytes()
        digest.update(relative.encode())
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return digest.hexdigest()


def controls(repo: Path) -> str:
    """Read the current controls table; fail rather than distribute stale controls."""
    readme = (repo / "README.md").read_text()
    section = re.search(
        r"^### Controls in current source\s*\n(.*?)(?=^#{1,3} |\Z)",
        readme,
        re.MULTILINE | re.DOTALL,
    )
    if section is None:
        raise ValueError("README.md has no Controls in current source section")
    table = re.search(
        r"^\| Input \| Action \|\n\|[^\n]+\|\n(?:\|[^\n]+\|\n)+",
        section.group(1),
        re.MULTILINE,
    )
    if table is None:
        raise ValueError("README.md controls table is missing or empty")
    # Keep the authored cells, but make links usable outside the source checkout.
    return re.sub(r"\[([^\]]+)\]\([^\)]+\)", r"\1", table.group(0)).rstrip()


def prepare(
    repo: Path, directory: Path, executable: Path, target: str, commit: str
) -> Path:
    """Stage test identity/instructions and return the intended archive path."""
    table = controls(repo)
    relative = executable.relative_to(directory)
    (executable.parent / "steam_appid.txt").write_text("480\n")
    launcher = directory / "PLAYTEST.sh"
    launcher.write_text(
        "#!/bin/sh\nset -eu\n"
        'root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        f'cd -- "$root"/{shlex.quote(str(relative.parent))}\n'
        "export SteamAppId=480 SteamGameId=480\n"
        'steam_root=${STEAM_ROOT:-"$HOME/.local/share/Steam"}\n'
        'overlay="$steam_root/ubuntu12_64/gameoverlayrenderer.so"\n'
        'if [ -f "$overlay" ]; then\n'
        '  export LD_PRELOAD="$overlay${LD_PRELOAD:+:$LD_PRELOAD}"\n'
        "fi\n"
        f'exec ./{shlex.quote(executable.name)} "$@"\n'
    )
    launcher.chmod(0o755)
    (directory / "PLAYTEST.txt").write_text(
        f"Post-Frontier playtest — Linux x86_64 — commit {commit}\n\n"
        "START\n"
        "Extract the whole archive, not just the executable. Log in to the Steam\n"
        "desktop client, enable its in-game overlay, then run ./PLAYTEST.sh from\n"
        "the extracted folder. A Vulkan-capable Linux GPU/driver is required.\n"
        "If Steam lives elsewhere, set STEAM_ROOT to its directory containing\n"
        "ubuntu12_64/gameoverlayrenderer.so. You may also add PLAYTEST.sh as a\n"
        "non-Steam shortcut. Do not launch Spacewar instead of this helper.\n"
        "For offline play use ./PLAYTEST.sh -nosteam, then Play vs JEV.\n\n"
        "HOST AND JOIN\n"
        "Everyone needs this identical complete build and a different logged-in\n"
        "Steam friends account. Leave Availability Zone v2 selected, choose\n"
        "Host co-op, then Invite friends in the match menu. Accept the Steam\n"
        "invite or use the host's Steam friends-list Join Game. Up to five\n"
        "players including the host; the host must remain running. Classic\n"
        "Availability Zone is also included because the menu can select it.\n"
        "Play Again keeps the lobby; Leave to Menu leaves it. If Steam is\n"
        "unavailable, check login/overlay and the visible status before hosting.\n\n"
        "CONTROLS (generated from README.md)\n"
        f"{table}\n\n"
        "SEND FEEDBACK\n"
        "Tell the organizer what happened, which commander you were, and this\n"
        "commit. Send screenshots, the game's Saved/Logs/CoopRTS.log, crash\n"
        "reports in Saved/Crashes/, and the host's match JSON files in\n"
        "Saved/Telemetry/ (host-only; no automatic upload).\n"
        f"These paths are under {relative.parents[2]}/ in the extracted build.\n"
        f"If they are not there, check $HOME/.config/Epic/{target}/Saved/:\n"
        "Logs/CoopRTS.log, Crashes/ and Telemetry/*.json. Start with PLAYTEST.sh;\n"
        "Development writes game logs without an extra -log argument.\n"
    )
    return directory / f"CoopRTS-playtest-{commit}.tar.gz"


def compress(directory: Path, archive: Path) -> None:
    """Include metadata and all staged contents before publishing latest."""
    root = archive.name.removesuffix(".tar.gz")
    with tarfile.open(archive, "w:gz") as bundle:
        for path in sorted(directory.iterdir()):
            if path != archive:
                bundle.add(path, arcname=f"{root}/{path.name}")
