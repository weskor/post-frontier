#!/usr/bin/env python3
"""Fetch the curated CC0 texture library into Art/Textures/<Category>/<AssetId>/.

Standard library only. Deterministic: fixed asset list, fixed resolution (2K JPG).
Rerunnable: an asset whose folder already holds a complete `.fetched` marker is skipped.

    python3 Build/FetchTextures.py            # download/unpack whatever is missing
    python3 Build/FetchTextures.py --sources  # (re)write Art/Textures/SOURCES.md from the list below

Source: ambientCG (https://ambientcg.com), all assets CC0 1.0. Before downloading an
asset the script confirms via the public API that the asset exists as a Material and
that its asset page carries the CC0 licence statement; otherwise it refuses to keep it.

Kept maps (normalised file names, JPG):
    <Id>_BaseColor.jpg  sRGB albedo
    <Id>_Normal.jpg     DirectX convention (green channel down): Unreal's native format,
                        so do NOT tick "Flip Green Channel" on import. ambientCG's NormalGL is dropped.
    <Id>_Roughness.jpg  linear
    <Id>_Metallic.jpg   linear, only where the set ships one
    <Id>_AO.jpg         linear, only where the set ships one
    <Id>_Height.jpg     linear, only when the 2K file is <= HEIGHT_MAX_BYTES
Everything else in the ZIP (GL normal, .blend/.usdc/.mtlx/.tres, preview sphere PNG) is dropped.
"""

from http.client import HTTPResponse
import io
import json
from pathlib import Path
import shutil
import sys
from typing import cast
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Art" / "Textures"
RES = "2K-JPG"
HEIGHT_MAX_BYTES = 3 * 1024 * 1024
UA = {"User-Agent": "Mozilla/5.0 (FetchTextures.py; CoopRTS asset pipeline)"}

# (category, ambientCG id, intended use)
ASSETS = [
    (
        "PaintedMetal",
        "Metal032",
        "Clean smooth grey-blue painted steel: human shell base, flat armour plates; tint toward pearl for the Machine",
    ),
    (
        "PaintedMetal",
        "PaintedMetal014",
        "Worn grey-blue painted plate: human shell, repaired/scuffed panels",
    ),
    (
        "Metal",
        "Metal010",
        "Brushed blue-grey steel: trim, pipes, pistons, barrel bands",
    ),
    (
        "Metal",
        "Metal046A",
        "Dark gunmetal: Dark slot mechanical parts, joints, engine housings",
    ),
    (
        "Panel",
        "MetalPlates002",
        "Bolted plate grid with seams and rivets: building walls, roofs, vehicle hulls (normal detail)",
    ),
    (
        "Panel",
        "MetalPlates006",
        "Dark studded sci-fi plating: Machine-side and heavy-armour surfaces (normal detail)",
    ),
    ("Tread", "DiamondPlate001", "Diamond tread plate: catwalks, vehicle decks, ramps"),
    (
        "Tread",
        "MetalWalkway006",
        "Open steel grating: walkways, vents, radiators (use with opacity or as a visual-only albedo)",
    ),
    ("Rubber", "Rubber004", "Dark smooth rubber: tyres, tracks, hoses, seals"),
    (
        "Hazard",
        "PaintedMetal016",
        "Rusted black/yellow hazard stripes: worn trim only; crisp hazard stripes will be procedural (keep small, see colour language)",
    ),
    (
        "Concrete",
        "Concrete024",
        "Clean smooth concrete: Campus Zero pads, plazas, building bases",
    ),
    (
        "Concrete",
        "Concrete016",
        "Weathered stained blue-grey concrete: walls, barriers, old slabs",
    ),
    ("Asphalt", "Asphalt026A", "Dark cracked asphalt: Campus Zero roads and parking"),
    (
        "Ground",
        "Gravel004",
        "Grey chunky gravel: Campus Zero verges, rooftops, rubble ground",
    ),
    ("Ground", "Gravel006", "Dark compacted dirt/gravel: bare ground and worn paths"),
    (
        "SciFiFloor",
        "Tiles108",
        "Black gridded tech tiles: data-hall and lab floors (Machine megastructure floors)",
    ),
]

MAP_SUFFIXES = {  # zip suffix -> output name
    "Color": "BaseColor",
    "NormalDX": "Normal",
    "Roughness": "Roughness",
    "Metalness": "Metallic",
    "AmbientOcclusion": "AO",
    "Displacement": "Height",
}


def http_get(url: str) -> bytes:
    req = urllib.request.Request(url, headers=UA)
    with cast(HTTPResponse, urllib.request.urlopen(req, timeout=120)) as r:
        return r.read()


def verify_cc0(asset_id: str) -> None:
    api = json.loads(
        http_get(f"https://ambientcg.com/api/v2/full_json?type=Material&id={asset_id}")
    )
    found = [a for a in api.get("foundAssets", []) if a["assetId"] == asset_id]
    if not found:
        sys.exit(f"{asset_id}: not found as an ambientCG Material")
    page = http_get(f"https://ambientcg.com/a/{asset_id}").decode("utf-8", "replace")
    if "CC0 license" not in page:
        sys.exit(
            f"{asset_id}: asset page does not state the CC0 licence; refusing to keep it"
        )


def fetch(category: str, asset_id: str) -> None:
    dest = OUT / category / asset_id
    marker = dest / ".fetched"
    if marker.exists():
        print(f"skip   {category}/{asset_id}")
        return
    verify_cc0(asset_id)
    url = f"https://ambientcg.com/get?file={asset_id}_{RES}.zip"
    print(f"fetch  {category}/{asset_id}  {url}")
    zf = zipfile.ZipFile(io.BytesIO(http_get(url)))
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    kept = []
    for suffix, name in MAP_SUFFIXES.items():
        member = f"{asset_id}_{RES}_{suffix}.jpg"
        if member not in zf.namelist():
            continue
        info = zf.getinfo(member)
        if name == "Height" and info.file_size > HEIGHT_MAX_BYTES:
            continue
        (dest / f"{asset_id}_{name}.jpg").write_bytes(zf.read(member))
        kept.append(name)
    if "BaseColor" not in kept or "Normal" not in kept or "Roughness" not in kept:
        shutil.rmtree(dest)
        sys.exit(f"{asset_id}: zip lacks BaseColor/Normal/Roughness ({kept})")
    marker.write_text(json.dumps({"maps": kept, "resolution": RES}) + "\n")


def write_sources() -> None:
    rows = []
    for category, asset_id, use in ASSETS:
        marker = OUT / category / asset_id / ".fetched"
        if not marker.exists():
            sys.exit(f"{category}/{asset_id} not fetched; run without --sources first")
        maps = ", ".join(json.loads(marker.read_text())["maps"])
        rows.append(
            f"| {category}/{asset_id} | https://ambientcg.com/a/{asset_id} | CC0 | {maps} | 2K JPG | DirectX | {use} |"
        )
    text = "\n".join(
        [
            "# Texture sources",
            "",
            "All assets are CC0 1.0 from ambientCG (https://ambientcg.com); no attribution required.",
            "Regenerate with `python3 Build/FetchTextures.py` then `python3 Build/FetchTextures.py --sources`.",
            "",
            "- Files live at `Art/Textures/<Category>/<AssetId>/<AssetId>_<Map>.jpg`.",
            "- **Normals are DirectX (green down), Unreal's native convention.** Import with the default NormalMap compression and no green flip.",
            "- Import BaseColor as sRGB; Normal, Roughness, Metallic, AO and Height as linear (non-sRGB).",
            "- Metallic is only present where the source set ships one; otherwise treat the material as dielectric (paint) or drive metalness from the master material.",
            "- Height is kept only where its 2K file is 3 MB or smaller.",
            "- No clean CC0 hazard-stripe material exists on ambientCG or checked alternatives; `PaintedMetal016` is black/yellow stripes heavily rusted through, so use it for derelict/worn trim only and build crisp hazard stripes procedurally in the master material.",
            "- `ContactSheet.png` shows every material as a labelled lit sphere.",
            "",
            "| Asset | Source URL | License | Maps kept | Resolution | Normal convention | Intended use |",
            "| --- | --- | --- | --- | --- | --- | --- |",
            *rows,
            "",
        ]
    )
    (OUT / "SOURCES.md").write_text(text)
    print(f"wrote {OUT / 'SOURCES.md'}")


def main() -> None:
    if "--sources" in sys.argv[1:]:
        write_sources()
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for category, asset_id, _ in ASSETS:
        fetch(category, asset_id)
    total = sum(f.stat().st_size for f in OUT.rglob("*") if f.is_file())
    print(f"done: {len(ASSETS)} assets, {total / 1e6:.1f} MB")


if __name__ == "__main__":
    try:
        main()
    except urllib.error.URLError as e:
        sys.exit(f"network error: {e}")
