"""One entry per executable content tool; imported libraries are not generators."""

from dataclasses import dataclass
from typing import Literal

Runtime = Literal["unreal", "blender", "python", "uv", "uv/reaper"]


@dataclass(frozen=True)
class Generator:
    name: str
    runtime: Runtime
    script: str
    outputs: str
    purpose: str
    arguments: tuple[str, ...] = ()
    geometry: bool = False
    offscreen: bool = False
    HELP: str = ""


MESH_PIPELINE = (
    "Order: fetch-textures → generate-unit-meshes → generate-building-meshes → "
    "generate-environment-kit → build-shared-material → import-unit-meshes → "
    "import-building-meshes → import-environment-kit → verify-masks → "
    "build-art-gallery → generate-campus-zero. Each step is ./x gen <name>."
)
TERRAIN_PIPELINE = (
    "Order: fetch-textures → generate-terrain-kit → build-shared-material → "
    "import-terrain-kit → verify-masks → draw-map-layout → generate-availability-zone."
)
AUDIO_PIPELINE = (
    "Order: fetch-audio-sources → generate-unit-audio (prepares layers, preserves "
    "existing REAPER sessions, renders and finishes WAVs) → import-audio. "
    "Pass --unit Human_Ambience to fetch/render just ambience; fetch --index "
    "rebuilds the licensed source index. --new-session discards manual session edits."
)

GENERATORS = (
    Generator(
        "build-art-gallery",
        "unreal",
        "BuildArtGallery.py",
        "/Game/Maps/ArtGallery",
        "Rebuild the art inspection world.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "build-shared-material",
        "unreal",
        "BuildSharedMaterial.py",
        "/Game/Art/{Materials,Textures}",
        "Compile shared material and faction instances.",
        offscreen=True,
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "generate-command-map",
        "unreal",
        "GenerateCommandMap.py",
        "/Game/Maps/Boot",
        "Replace Boot match actors and navigation.",
    ),
    Generator(
        "generate-campus-zero",
        "unreal",
        "GenerateCampusZero.py",
        "/Game/Maps/CampusZero",
        "Rebuild the themed campus match map.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "generate-availability-zone",
        "unreal",
        "GenerateAvailabilityZone.py",
        "/Game/Maps/AvailabilityZone; /Game/Art/Materials/MI_AZ_*",
        "Rebuild the terrain-kit valley map.",
        HELP=TERRAIN_PIPELINE,
    ),
    Generator(
        "generate-availability-zone-v2",
        "unreal",
        "GenerateAvailabilityZoneV2.py",
        "/Game/Maps/AvailabilityZoneV2",
        "Rebuild the region-based valley map.",
        HELP="Order: draw-availability-zone-v2 → generate-availability-zone-v2.",
    ),
    Generator(
        "generate-match-content",
        "unreal",
        "GenerateMatchContent.py",
        "/Game/Units; /Game/Buildings; /Game/Content/DA_MatchContent",
        "Create catalogue while preserving tuned combat values.",
    ),
    Generator(
        "generate-menu-map",
        "unreal",
        "GenerateMenuMap.py",
        "/Game/Maps/Menu",
        "Create the empty frontend world.",
    ),
    Generator(
        "generate-overlay-materials",
        "unreal",
        "GenerateOverlayMaterials.py",
        "/Game/Materials/Overlay/M_WorldOverlay",
        "Compile depth-tested instanced RGBA overlay material.",
        offscreen=True,
    ),
    Generator(
        "import-unit-meshes",
        "unreal",
        "ImportUnitMeshes.py",
        "/Game/Art/Units; /Game/Maps/UnitGallery",
        "Import eight unit and HQ meshes.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "import-building-meshes",
        "unreal",
        "ImportBuildingMeshes.py",
        "/Game/Art/Buildings",
        "Import buildings and construction scaffolds.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "import-environment-kit",
        "unreal",
        "ImportEnvironmentKit.py",
        "/Game/Art/Environment",
        "Import environment meshes and collision.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "import-terrain-kit",
        "unreal",
        "ImportTerrainKit.py",
        "/Game/Art/Terrain",
        "Import terrain meshes and walkable collision.",
        HELP=TERRAIN_PIPELINE,
    ),
    Generator(
        "import-audio",
        "unreal",
        "ImportAudio.py",
        "/Game/Audio",
        "Import WAVs and rebuild native mix assets.",
        HELP=AUDIO_PIPELINE,
    ),
    Generator(
        "verify-masks",
        "unreal",
        "VerifyMasks.py",
        "log: baked vertex colour checks",
        "Check imported mesh masks against Blender colours.",
        geometry=True,
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "generate-unit-meshes",
        "blender",
        "GenerateUnitMeshes.py",
        "Art/Units/{*.fbx,Units.blend,Preview*.png}",
        "Build unit and HQ silhouettes and masks.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "generate-building-meshes",
        "blender",
        "GenerateBuildingMeshes.py",
        "Art/Buildings/{*.fbx,Buildings.blend,Preview*.png}",
        "Build faction buildings and scaffolds.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "generate-environment-kit",
        "blender",
        "GenerateEnvironmentKit.py",
        "Art/Environment/{*.fbx,Environment.blend,Preview*.png}",
        "Build campus environment kit.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "generate-terrain-kit",
        "blender",
        "GenerateTerrainKit.py",
        "Art/Terrain/{*.fbx,*.blend,Preview*.png}",
        "Build grid-aligned cliffs, ramps and rocks.",
        HELP=TERRAIN_PIPELINE,
    ),
    Generator(
        "master-materials",
        "blender",
        "MasterMaterials.py",
        "Art/Materials/{MaterialPreview.blend,*.png}",
        "Render the shared-material prototype.",
        HELP=MESH_PIPELINE,
    ),
    Generator(
        "render-ui-icons",
        "blender",
        "RenderUIIcons.py",
        "Art/UI/icons/portraits",
        "Render mesh portraits and team masks.",
        HELP="Order: generate-unit-meshes → generate-building-meshes → generate-environment-kit → render-ui-icons.",
    ),
    Generator(
        "render-texture-contact-sheet",
        "blender",
        "RenderTextureContactSheet.py",
        "Art/Textures/ContactSheet.png",
        "Render labelled CC0 material spheres.",
        HELP="Order: fetch-textures → render-texture-contact-sheet. Requires ImageMagick montage.",
    ),
    Generator(
        "export-ui-assets",
        "python",
        "ExportUIAssets.py",
        "Art/UI/{frames,icons/commands}",
        "Export SVG/PNG HUD frames and command glyphs.",
        HELP="Requires rsvg-convert; render-ui-icons supplies portraits separately.",
    ),
    Generator(
        "fetch-textures",
        "python",
        "FetchTextures.py",
        "Art/Textures",
        "Fetch CC0 DirectX texture maps.",
        HELP="Run again with --sources to rewrite the licence/source table.",
    ),
    Generator(
        "draw-map-layout",
        "python",
        "DrawMapLayout.py",
        "Art/Maps/AvailabilityZone-layout.png",
        "Validate and draw the v1 map layout.",
        arguments=("--report",),
        HELP=TERRAIN_PIPELINE
        + " Extra --svg PATH preserves SVG; --quiet prints errors only.",
    ),
    Generator(
        "draw-availability-zone-v2",
        "python",
        "DrawAvailabilityZoneV2.py",
        "Art/Maps/AvailabilityZoneV2-layout.png",
        "Validate v2 JSON and draw its layout.",
        HELP="Pass -- --derive to rebuild Build/Maps/AvailabilityZoneV2.json from Excalidraw before validating and drawing.",
    ),
    Generator(
        "availability-zone-layout",
        "python",
        "AvailabilityZoneLayout.py",
        "log: placement counts; optional SVG",
        "Validate the assembled terrain-kit layout.",
        HELP="Use --svg PATH to retain the assembly drawing.",
    ),
    Generator(
        "fetch-audio-sources",
        "uv",
        "FetchAudioSources.py",
        "Saved/AudioSources",
        "Fetch licensed recipe source layers.",
        HELP=AUDIO_PIPELINE,
    ),
    Generator(
        "generate-unit-audio",
        "uv/reaper",
        "GenerateUnitAudio.py",
        "Art/Audio; Saved/AudioLayers; Saved/AudioPreview",
        "Render recipe-based sound sets through REAPER.",
        HELP=AUDIO_PIPELINE,
    ),
    Generator(
        "generate-music-sample",
        "uv",
        "GenerateMusicSample.py",
        "Saved/AudioPreview/Dark_Horn_Sample_v2.{wav,mp3}; Saved/AudioMusic/DarkHorn_v2",
        "Render the original dark-horn listening sketch.",
        HELP="Run fetch-audio-sources first. Preserves existing REAPER session faders; not a game import.",
    ),
)
BY_NAME = {entry.name: entry for entry in GENERATORS}
