#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["remotezip==0.12.6", "numpy==2.5.3", "scipy==1.18.1", "soundfile==0.14.0", "pedalboard==0.9.25", "pyloudnorm==0.2.0"]
# ///
"""Fetch the recorded source layers used by the unit sound recipes (Build/unit_audio/*.py SOURCES).

    uv run Build/FetchAudioSources.py           # download every recipe's missing sources
    uv run Build/FetchAudioSources.py --index   # (re)build Saved/AudioSources/sonniss-index.tsv

Sources are individual files from the Sonniss #GameAudioGDC bundles (https://sonniss.com/gameaudiogdc),
pulled out of the multi-GB bundle ZIPs with HTTP range requests so only the listed files download.
They unpack into the Git-ignored cache Saved/AudioSources/Sonniss/<Library>/<File> and are never
committed: the bundle licence forbids passing the sounds on as sound effects, even modified. Only the
finished game sounds in Art/Audio are kept. Rerunnable: files already present are skipped.

--index reads only each bundle ZIP's central directory and writes one row per file:
`<zip file name>\t<size in bytes>\t<member path>`. Search it to pick recipe SOURCES; the first column
and the member path are exactly what a SOURCES entry needs.
"""

import argparse
import concurrent.futures
from importlib import import_module
import os
import re
from typing import Protocol, cast
import urllib.request
from zipfile import ZipFile

from unit_audio import recipes
from unit_audio.core import ROOT, source_path


class RemoteZipFactory(Protocol):
    def __call__(self, url: str, *, headers: dict[str, str]) -> ZipFile: ...


# RemoteZip subclasses ZipFile; only its HTTP constructor lacks published types.
RemoteZip = cast(RemoteZipFactory, import_module("remotezip").RemoteZip)

BASE = "https://downloads.sonniss.com/"
ARCHIVE_PAGES = ["https://sonniss.com/gameaudiogdc", "https://gdc.sonniss.com/"]
INDEX = ROOT / "Saved" / "AudioSources" / "sonniss-index.tsv"
# The download host rejects requests without a browser user agent and the bundle page as referrer.
HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/140 Safari/537.36",
    "Referer": "https://gdc.sonniss.com/",
}


def fetch(units: list[str] | None) -> None:
    wanted = sorted({entry for module in recipes(units) for entry in module.SOURCES})
    by_zip: dict[str, list[str]] = {}
    for bundle, member in wanted:
        if not source_path(member).exists():
            by_zip.setdefault(bundle, []).append(member)
    for bundle, members in by_zip.items():
        with RemoteZip(BASE + bundle, headers=HEADERS) as archive:
            for member in members:
                out = source_path(member)
                out.parent.mkdir(parents=True, exist_ok=True)
                # Write then rename, so a concurrent fetch or render never reads a half-written file.
                partial = out.with_name(f".{out.name}.{os.getpid()}.part")
                partial.write_bytes(archive.read(member))
                partial.replace(out)
                print(f"fetched {out.relative_to(ROOT)}")
    print(f"AUDIO_SOURCES_READY {len(wanted)}")


def build_index() -> None:
    zips: set[str] = set()
    for page in ARCHIVE_PAGES:
        with urllib.request.urlopen(
            urllib.request.Request(page, headers=HEADERS), timeout=60
        ) as response:
            html = response.read().decode("utf-8", "replace")
        zips |= set(
            re.findall(r"https://downloads\.sonniss\.com/([^\"'<> ]+\.zip)", html)
        )

    def members(bundle: str) -> list[str]:
        with RemoteZip(BASE + bundle, headers=HEADERS) as archive:
            return [
                f"{bundle}\t{info.file_size}\t{info.filename}"
                for info in archive.infolist()
                if not info.is_dir() and "__MACOSX" not in info.filename
            ]

    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        rows = [row for listing in pool.map(members, sorted(zips)) for row in listing]
    INDEX.parent.mkdir(parents=True, exist_ok=True)
    INDEX.write_text("\n".join(rows) + "\n")
    print(
        f"SONNISS_INDEX_WRITTEN {len(zips)} bundles, {len(rows)} files -> {INDEX.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--index",
        action="store_true",
        help="rebuild the bundle file index instead of fetching",
    )
    parser.add_argument(
        "--unit",
        action="append",
        metavar="FACTION_ROLE",
        help="only this unit's sources, e.g. Human_Ranged (repeatable); default: every recipe",
    )
    args = parser.parse_args()
    if args.index:
        build_index()
    else:
        fetch(args.unit)
