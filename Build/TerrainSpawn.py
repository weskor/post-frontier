"""Spawn the Habitable Zone v2 terrain computed by Build/TerrainPlan.py: plateau and wall cells, ramps, cover props,
hazard plates, the paved apron and the null-nav caps on rock-wall tops.

Imported by Build/GenerateAvailabilityZoneV2.py inside the editor; `spawn` and `block` are that script's helpers.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import EnvKit
import TerrainKit
import TerrainPlan

HAZARD_PLATE = 360.0
VENT_EVERY = 3


def suffixed(piece, look):
    """Kit mesh name of `piece` in the Human or Machine look."""
    return piece + ("_Machine" if look == "machine" else "")


def check_prop_table():
    """TerrainPlan.PROP_SIZE is the pure-Python copy of EnvKit.SPEC: they must agree."""
    for name, (size_x, size_y) in TerrainPlan.PROP_SIZE.items():
        actual = EnvKit.piece_size(name)[:2]
        if abs(actual[0] - size_x * 100) > 0.5 or abs(actual[1] - size_y * 100) > 0.5:
            raise RuntimeError("TerrainPlan.PROP_SIZE[%s] differs from EnvKit.SPEC: %s" % (name, actual))


def null_nav_volume(spawn, label, centre, half_extent):
    """A NavModifierVolume of NavArea_Null, resized from the brush factory's default like the nav bounds volume."""
    volume = spawn(unreal.NavModifierVolume, label, centre)
    volume.set_folder_path("AZV2/Terrain/NavCaps")
    _, original = volume.get_actor_bounds(False)
    volume.set_actor_scale3d(unreal.Vector(half_extent[0] / original.x, half_extent[1] / original.y,
                                           half_extent[2] / original.z))
    volume.set_editor_property("area_class", unreal.NavArea_Null)
    return volume


def place_terrain(terrain, spawn, block, cylinder, materials):
    """Spawn everything; returns counts for the generator's log line."""
    check_prop_table()
    counts = {"plateau": 0, "wall": 0, "ramp": 0, "prop": 0, "hazard": 0, "apron": 0}
    for cell in terrain.solid_pieces():
        name = suffixed(cell["piece"], cell["look"])
        kind = "Wall" if cell["wall"] else "Plateau"
        TerrainKit.place_piece(name, cell["centre"], yaw=cell["yaw"], base=0.0,
                               label="%s_%d_%d" % (kind, cell["cell"][0], cell["cell"][1]),
                               folder="AZV2/Terrain/" + kind)
        counts["wall" if cell["wall"] else "plateau"] += 1
        if cell["wall"]:
            half = TerrainPlan.CELL / 2
            null_nav_volume(spawn, "WallNavCap_%d_%d" % cell["cell"], (cell["centre"][0], cell["centre"][1], terrain.height),
                            (half, half, 100.0))
    for ramp in terrain.ramp_pieces():
        TerrainKit.place_piece(suffixed(ramp["piece"], ramp["look"]), ramp["centre"], yaw=ramp["yaw"], base=0.0,
                               label="Ramp_%d_%d" % (ramp["centre"][0], ramp["centre"][1]), folder="AZV2/Terrain/Ramps")
        counts["ramp"] += 1
    for number, prop in enumerate(terrain.props):
        EnvKit.place_piece(prop["kit"], prop["pos"], yaw=prop["yaw"], base=0.0,
                           label="Cover_%02d_%s" % (number, prop["kit"]), folder="AZV2/Terrain/Cover")
        counts["prop"] += 1
    for number, (x, y) in enumerate(terrain.hazard_plates()):
        block("HazardPlate_%03d" % number, (x, y), (HAZARD_PLATE, HAZARD_PLATE, 3), materials["MI_AZ_Hazard"],
              base=0.6, folder="AZV2/Terrain/Hazard")
        if number % VENT_EVERY == 0:
            block("HazardVent_%03d" % number, (x, y), (90, 90, 6), materials["MI_AZ_GlowHuman"], mesh=cylinder,
                  base=3.6, folder="AZV2/Terrain/Hazard")
        counts["hazard"] += 1
    for number, (i, j0, j1, _region) in enumerate(terrain.apron_runs()):
        block("Apron_%03d" % number, (i * TerrainPlan.CELL, (j0 + j1) / 2 * TerrainPlan.CELL),
              (TerrainPlan.CELL, (j1 - j0 + 1) * TerrainPlan.CELL, 2), materials["MI_AZ_DeckPlate"],
              base=0.2, folder="AZV2/Terrain/Apron")
        counts["apron"] += 1
    return counts
