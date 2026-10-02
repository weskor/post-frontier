"""Place terrain-kit pieces (/Game/Art/Terrain/SM_*) from a map generator.

Imported by Build/ImportTerrainKit.py (contract table, materials, bounds checks) and Build/GenerateAvailabilityZone.py
(placement); not run directly. Import the pieces first with Build/ImportTerrainKit.py. It is the terrain twin of
Build/EnvKit.py: everything is centimetres and degrees on the Unreal side, origin at the footprint centre, base on
z = 0 (the ground the piece stands on), `place_piece` takes plain Unreal placement values.

The contract table is the docstring of Build/GenerateTerrainKit.py ("Piece table", metres) and its `SPEC`; the two
must agree, ImportTerrainKit fails if an imported mesh does not match it.

Orientation (Unreal side, not Blender side). Unreal's FBX importer flips Blender +Y to Unreal -Y (see
Build/EnvKit.py). Straight pieces face local +X and are unaffected. The docstring's asymmetric pieces are drawn for
Blender: a CornerOuter at yaw 0 has its two low neighbours at Blender +X and +Y, so in Unreal they are +X and -Y;
the ramp's downhill end is +X in both. Build/AvailabilityZoneLayout.py holds the resulting yaw rules
(CORNER_OUTER_LOW_AT_YAW0) and Build/ImportTerrainKit.py measures the imported vertices and stops if the assumption
is wrong.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art

require = art.require
assets = art.assets
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

MESH_FOLDER = "/Game/Art/Terrain"
PREFIX = "SM_"

# name: (footprint X m, footprint Y m, height m, material slots in mesh order, look). Look picks the material set
# (LOOKS below): "human" rock + steel, "machine" pearl + cyan, "rock" the destructible boulders.
SPEC = {
    "Cliff_Straight": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow", "Accent"), "human"),
    "Cliff_CornerOuter": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow", "Accent"), "human"),
    "Cliff_CornerInner": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow", "Accent"), "human"),
    "Cliff_Straight_Machine": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow"), "machine"),
    "Cliff_CornerOuter_Machine": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow"), "machine"),
    "Cliff_CornerInner_Machine": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow"), "machine"),
    "Ramp_Wide": (8.0, 8.0, 3.65, ("Shell", "Dark", "Glow", "Accent"), "human"),
    "Ramp_Narrow": (8.0, 4.0, 3.65, ("Shell", "Dark", "Glow", "Accent"), "human"),
    "Ramp_Wide_Machine": (8.0, 8.0, 3.65, ("Shell", "Dark", "Glow"), "machine"),
    "Ramp_Narrow_Machine": (8.0, 4.0, 3.65, ("Shell", "Dark", "Glow"), "machine"),
    "Plateau_Fill": (4.0, 4.0, 3.0, ("Shell", "Dark"), "human"),
    "Plateau_Fill_Machine": (4.0, 4.0, 3.0, ("Shell", "Dark", "Glow"), "machine"),
    "Rocks_Destructible_Small": (2.4, 4.0, 2.6, ("Shell", "Dark", "Glow"), "rock"),
    "Rocks_Destructible_Large": (3.0, 8.0, 3.4, ("Shell", "Dark", "Glow"), "rock"),
    "Watchtower": (4.0, 4.0, 9.0, ("Shell", "Dark", "Glow", "Accent"), "machine"),
}

# Terrain variants of the Env-scope master instances (Art/Materials/UNREAL.md section 7: children that override
# BaseColor and the earth-ish amounts). The values are the Terrain / Rock variants of Build/GenerateTerrainKit.py
# (MasterMaterials.VARIANTS), with GrimeAmount / GroundGrime multiplied by the Env scope's 0.55 as
# Build/BuildSharedMaterial.py does for every Env instance. name: (parent, vectors, scalars).
ENV_GRIME = 0.55
VARIANTS = {
    "MI_SC2_Human_Dark_Terrain": (
        "MI_SC2_Human_Dark_Env",
        {"BaseColor": (0.15, 0.115, 0.085), "BareMetalColor": (0.32, 0.27, 0.22)},
        {"Metallic": 0.0, "Roughness": 0.92, "EdgeHighlight": 0.25, "EdgeWear": 0.1, "GrimeAmount": 0.35 * ENV_GRIME,
         "GroundGrime": 0.5 * ENV_GRIME, "PanelStrength": 0.12, "PanelLine": 0.0}),
    "MI_SC2_Human_Dark_Rock": (
        "MI_SC2_Human_Dark_Env",
        {"BaseColor": (0.13, 0.115, 0.10), "BareMetalColor": (0.3, 0.27, 0.24)},
        {"Metallic": 0.0, "Roughness": 0.9, "EdgeHighlight": 0.3, "EdgeWear": 0.1, "GrimeAmount": 0.35 * ENV_GRIME,
         "GroundGrime": 0.5 * ENV_GRIME, "PanelStrength": 0.10, "PanelLine": 0.0}),
    "MI_SC2_Human_Shell_Rock": (
        "MI_SC2_Human_Shell_Env",
        {"BaseColor": (0.34, 0.28, 0.21), "BareMetalColor": (0.5, 0.44, 0.36)},
        {"Metallic": 0.0, "Roughness": 0.85, "ClearCoat": 0.0, "PearlAmount": 0.0, "EdgeHighlight": 0.3,
         "EdgeWear": 0.1, "GrimeAmount": 0.3 * ENV_GRIME, "GroundGrime": 0.3 * ENV_GRIME, "PanelStrength": 0.10,
         "PanelLine": 0.0}),
}


def look_material(name, slot):
    """MI_SC2_* instance name for `slot` of terrain piece `name` (a VARIANTS name where the kit needs earth)."""
    look = SPEC[name][4]
    if look == "machine":
        return "MI_SC2_Machine_%s_Env" % slot
    if look == "rock":
        return {"Shell": "MI_SC2_Human_Shell_Rock", "Dark": "MI_SC2_Human_Dark_Rock"}.get(slot, "MI_SC2_Human_%s_Env" % slot)
    return "MI_SC2_Human_Dark_Terrain" if slot == "Dark" else "MI_SC2_Human_%s_Env" % slot


_meshes = {}


def mesh_path(name):
    return "%s/%s%s" % (MESH_FOLDER, PREFIX, name)


def piece_size(name):
    """(footprint X, footprint Y, height) in cm, before any actor scale."""
    require(name in SPEC, "Unknown terrain piece " + name)
    fx, fy, height = SPEC[name][:3]
    return (fx * 100.0, fy * 100.0, height * 100.0)


def _load(name):
    if name not in _meshes:
        require(name in SPEC, "Unknown terrain piece " + name)
        _meshes[name] = require(assets.load_asset(mesh_path(name)),
                                "Missing %s: run Build/ImportTerrainKit.py first" % mesh_path(name))
    return _meshes[name]


def place_piece(name, center, yaw=0.0, base=0.0, scale=(1.0, 1.0, 1.0), label=None, collision=True, folder=None):
    """Spawn one terrain piece by footprint centre (cm), yaw in degrees (Unreal: positive turns +X toward +Y) and
    base z (cm) of the piece's underside. The mesh carries its materials and simple collision from the import;
    `collision` picks BlockAll (the kit's own hulls) or NoCollision."""
    mesh = _load(name)
    actor = require(actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(center[0], center[1], base),
                                                  unreal.Rotator(yaw=yaw)),
                    "Could not spawn " + (label or name))
    actor.set_actor_label(label or (PREFIX + name))
    if folder:
        actor.set_folder_path(folder)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor
