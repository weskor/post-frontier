"""Place Campus Zero environment-kit pieces (/Game/Art/Environment/SM_Env_*) from a map generator.

Imported by Build/ImportEnvironmentKit.py (contract table, bounds checks) and meant for
Build/GenerateCampusZero.py (placement); not run directly. Import the pieces first with
Build/ImportEnvironmentKit.py. Everything is in centimetres and degrees on the Unreal side, like the map
generator's `block(label, center, size, ...)`.

Piece contract (Build/GenerateEnvironmentKit.py docstring): origin at the footprint centre, base at z = 0,
front (doors, cab, lamps) = +X. The SPEC table below is that docstring's table in metres.

Handedness. The FBX comes from Blender (right-handed) and Unreal's importer flips Y (Blender +Y -> Unreal
-Y, +X and +Z unchanged; see Build/GenerateUnitMeshes.py). A mesh therefore looks like the Blender design
mirrored across the XZ plane, and a Blender yaw of t degrees is an Unreal yaw of -t. This only shows on
pieces that are lopsided in Y (the outer face of a hall wall module is Blender local -Y, Unreal local +Y).
`place_piece` takes plain Unreal placement values and needs no conversion. `assemble_hall` converts the
documented Blender recipe for you: its door sides are Unreal world directions (N = +Y, S = -Y, E = +X,
W = -X) and every wall's outer face points away from the hall centre.

Hall recipe (Blender docstring, "Hall assembly"): a hall of outer size SX x SY (whole metres, both >= 6 m)
is a ring of 3 m deep wall bands around a roof infill:
  corners  4 x DataHallCorner at (+-(SX-3)/2, +-(SY-3)/2), outer corner pointing away from the centre
  walls    each side has L = side - 6 m between the corners, filled with `d` DataHallDoor modules (4 m,
           unscaled) and n = floor((L - 4d)/2 + 0.5) DataHallBay modules (min 1 when L - 4d > 0) scaled
           along their length by (L - 4d)/(2n), split evenly between the doors: bay, door, bay, ...
  roof     (SX-6) x (SY-6); if both > 0, DataHallRoof tiles nx = floor(W/2 + 0.5) by ny = floor(D/2 + 0.5),
           scaled (W/(2 nx), D/(2 ny), 1)
Nothing is scaled in Z; only wall bays and roof tiles are scaled.
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art

require = art.require
assets = art.assets
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

MESH_FOLDER = "/Game/Art/Environment"
PREFIX = "SM_Env_"

# name: (footprint X m, footprint Y m, height m, material slots in mesh order). Footprint is the box the piece
# replaces in Build/GenerateCampusZero.py; the height includes rooftop kit, beacons and lamps.
SPEC = {
    "DataHallBay": (2.0, 3.0, 6.5, ("Shell", "Dark", "Glow", "Accent")),
    "DataHallDoor": (4.0, 3.0, 6.5, ("Shell", "Dark", "Glow", "Accent")),
    "DataHallCorner": (3.0, 3.0, 7.0, ("Shell", "Dark", "Glow", "Accent")),
    "DataHallRoof": (2.0, 2.0, 5.8, ("Shell", "Dark", "Glow")),
    "CoolingTower": (5.6, 5.6, 9.0, ("Shell", "Dark", "Glow", "Accent")),
    "Chiller": (2.6, 2.6, 3.0, ("Shell", "Dark", "Glow", "Accent")),
    "Transformer": (2.2, 1.7, 2.6, ("Shell", "Dark", "Glow", "Accent")),
    "Pylon": (7.0, 1.5, 13.0, ("Shell", "Dark", "Glow", "Accent")),
    "CommsMast": (0.8, 0.8, 15.0, ("Shell", "Dark", "Glow", "Accent")),
    "ClusterPylon": (0.7, 0.7, 4.2, ("Shell", "Dark", "Glow", "Accent")),
    "Container": (6.0, 2.45, 2.6, ("Shell", "Dark", "Accent")),
    "Wreck": (4.2, 1.9, 1.7, ("Shell", "Dark", "Accent")),
    "SandbagWall": (1.2, 3.0, 1.1, ("Shell", "Dark", "Accent")),
    "BurnBarrel": (0.7, 0.7, 1.0, ("Shell", "Dark", "Glow", "Accent")),
    "GeneratorShack": (3.0, 4.0, 2.5, ("Shell", "Dark", "Glow", "Accent")),
    "FenceSegment": (4.0, 0.3, 2.2, ("Shell", "Dark", "Glow", "Accent")),
    "CableSpool": (2.0, 2.0, 1.4, ("Shell", "Dark", "Accent")),
}

# Pieces whose simple collision is smaller than their mesh bounds: name -> (X, Y) footprint in metres at the origin,
# full height. The pylon's 7 m cross-arm is at 11 m, so only its 1.5 m body may block the ground (the old primitive
# map had four 30 cm leg posts and no collision on the arm). Everything else blocks its whole bounding box.
GROUND_FOOTPRINT = {"Pylon": (1.5, 1.5)}

_meshes = {}


def mesh_path(name):
    return "%s/%s%s" % (MESH_FOLDER, PREFIX, name)


def piece_size(name):
    """(footprint X, footprint Y, height) in cm, before any actor scale."""
    require(name in SPEC, "Unknown environment piece " + name)
    fx, fy, height, _slots = SPEC[name]
    return (fx * 100.0, fy * 100.0, height * 100.0)


def collision_size(name):
    """(X, Y) cm of the blocking footprint: the mesh footprint unless GROUND_FOOTPRINT narrows it."""
    fx, fy, _height = piece_size(name)
    if name in GROUND_FOOTPRINT:
        return (GROUND_FOOTPRINT[name][0] * 100.0, GROUND_FOOTPRINT[name][1] * 100.0)
    return (fx, fy)


def _load(name):
    if name not in _meshes:
        path = mesh_path(name)
        require(name in SPEC, "Unknown environment piece " + name)
        _meshes[name] = require(assets.load_asset(path),
                                "Missing %s: run Build/ImportEnvironmentKit.py first" % path)
    return _meshes[name]


def place_piece(name, center, yaw=0.0, base=0.0, scale=(1.0, 1.0, 1.0), label=None, collision=True,
                folder=None, materials=None):
    """Spawn one kit piece by footprint centre.

    center (x, y) in cm is the middle of the footprint, yaw in degrees (Unreal: positive turns +X toward +Y),
    base in cm is the z of the piece's underside (0 = ground). `scale` multiplies the actor's local X, Y, Z
    (footprint becomes footprint * scale). The mesh already carries its materials and simple collision from the
    import; `collision` picks the BlockAll profile (like the generator's block()) or NoCollision.
    `materials` maps a slot name (Shell, Dark, Glow, Accent) to a material that replaces the mesh's own for this
    actor only, so repeated pieces (containers, wrecks) can carry different paint jobs.
    """
    mesh = _load(name)
    actor = require(actors.spawn_actor_from_class(unreal.StaticMeshActor,
                                                  unreal.Vector(center[0], center[1], base),
                                                  unreal.Rotator(yaw=yaw)),
                    "Could not spawn " + (label or name))
    actor.set_actor_label(label or (PREFIX + name))
    if folder:
        actor.set_folder_path(folder)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    for index, slot in enumerate(SPEC[name][3]):
        if materials and slot in materials:
            component.set_material(index, materials[slot])
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


# --------------------------------------------------------------------------------------------------
# Hall assembly
# --------------------------------------------------------------------------------------------------
def _side_sequence(length, doors):
    """Fill `length` m with `doors` 4 m door modules and stretched 2 m bays -> [(piece, bay scale)]."""
    rest = length - 4.0 * doors
    if rest < -1e-9:
        raise ValueError("%g m of wall cannot hold %d doors" % (length, doors))
    bays = 0 if rest < 1e-9 else max(1, int(math.floor(rest / 2.0 + 0.5)))
    scale = rest / (2.0 * bays) if bays else 1.0
    groups = [bays // (doors + 1)] * (doors + 1)
    # leftover bays go to the groups nearest the middle first
    for k, index in enumerate(sorted(range(doors + 1), key=lambda i: abs(i - doors / 2.0))):
        if k < bays % (doors + 1):
            groups[index] += 1
    items = []
    for g in range(doors + 1):
        items += [("DataHallBay", scale)] * groups[g]
        if g < doors:
            items.append(("DataHallDoor", 1.0))
    return items


def hall_layout(size_x, size_y, doors=None):
    """Blender-frame recipe, metres/degrees: [(piece, x, y, yaw, scale_x, scale_y)].

    This is the exact positions/yaws/scales of hall_instances() in Build/GenerateEnvironmentKit.py (door sides
    S/E/N/W are Blender directions there). Pure Python; `assemble_hall` mirrors it into Unreal.
    """
    doors = doors or {}
    if size_x < 6 or size_y < 6:
        raise ValueError("Hall %g x %g m is below the 6 x 6 m minimum" % (size_x, size_y))
    cx, cy = (size_x - 3) / 2.0, (size_y - 3) / 2.0
    out = []
    for yaw, (x, y) in ((0, (-cx, -cy)), (90, (cx, -cy)), (180, (cx, cy)), (270, (-cx, cy))):
        out.append(("DataHallCorner", x, y, yaw, 1.0, 1.0))
    for side, yaw, length, half in (("S", 0, size_x - 6, cy), ("E", 90, size_y - 6, cx),
                                    ("N", 180, size_x - 6, cy), ("W", 270, size_y - 6, cx)):
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        ox, oy = s * half, -c * half  # rot(yaw) @ (0, -half)
        t = -length / 2.0
        for piece, scale in _side_sequence(length, doors.get(side, 0)):
            width = 4.0 if piece == "DataHallDoor" else 2.0 * scale
            along = t + width / 2.0
            out.append((piece, ox + c * along, oy + s * along, yaw, scale if piece == "DataHallBay" else 1.0, 1.0))
            t += width
    w, d = size_x - 6, size_y - 6
    if w > 0 and d > 0:
        nx, ny = max(int(math.floor(w / 2.0 + 0.5)), 1), max(int(math.floor(d / 2.0 + 0.5)), 1)
        for i in range(nx):
            for j in range(ny):
                out.append(("DataHallRoof", -w / 2.0 + (i + 0.5) * w / nx, -d / 2.0 + (j + 0.5) * d / ny, 0,
                            w / (2.0 * nx), d / (2.0 * ny)))
    return out


def hall_placements(size, doors=None, yaw=0.0):
    """Unreal-frame placements for a hall of outer `size` (x, y) cm: [(piece, x, y, yaw, scale_x, scale_y)]
    relative to the hall centre, cm/degrees. `doors` maps N/E/S/W (Unreal world directions, N = +Y) to a door
    module count; `yaw` turns the whole hall (its sides turn with it)."""
    size_x, size_y = size[0] / 100.0, size[1] / 100.0
    for value in (size_x, size_y):
        if abs(value - round(value)) > 1e-6:
            raise ValueError("Hall size %s cm is not a whole number of metres" % (tuple(size),))
    doors = doors or {}
    unknown = set(doors) - set("NESW")
    if unknown:
        raise ValueError("Unknown door sides %s (use N, E, S, W)" % sorted(unknown))
    # Unreal is the Blender layout mirrored across XZ: Unreal north (+Y) is the Blender south wall.
    blender_doors = {{"N": "S", "S": "N"}.get(side, side): count for side, count in doors.items()}
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    out = []
    for piece, x, y, piece_yaw, sx, sy in hall_layout(size_x, size_y, blender_doors):
        ux, uy = x * 100.0, -y * 100.0
        out.append((piece, ux * c - uy * s, ux * s + uy * c, (yaw - piece_yaw) % 360.0, sx, sy))
    return out


def assemble_hall(label, center, size, doors=None, yaw=0.0, base=0.0, collision=True, folder=None):
    """Spawn a data hall from the Bay/Corner/Door/Roof modules; returns the list of actors.

    center (x, y) cm is the middle of the hall, size (x, y) cm the outer footprint (whole metres, each >= 600),
    doors e.g. {"E": 2, "W": 2} counts loading-door modules per Unreal world side, yaw turns the whole hall,
    base is the ground z in cm. Modules cover the outer rectangle exactly; the wall band is 300 cm deep and the
    parapet top is 600 cm for every hall, so a map can keep using one `size` box for keep-out checks.
    """
    spawned = []
    counts = {}
    for piece, x, y, piece_yaw, sx, sy in hall_placements(size, doors, yaw):
        counts[piece] = counts.get(piece, 0) + 1
        spawned.append(place_piece(piece, (center[0] + x, center[1] + y), piece_yaw, base, (sx, sy, 1.0),
                                   "%s_%s_%d" % (label, piece.replace("DataHall", ""), counts[piece]),
                                   collision, folder))
    unreal.log("ENV_HALL %s size=%dx%d cm modules=%d" % (label, size[0], size[1], len(spawned)))
    return spawned
