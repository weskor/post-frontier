"""Generate the Campus Zero environment kit for CoopRTS in Blender ("Neon Nightfall", StarCraft 2-style pass v2).

Run headless from the repo root (Blender 5.2.1, no assets, no network, deterministic):

    blender -b --factory-startup -P Build/GenerateEnvironmentKit.py

The kit replaces the primitive boxes and cylinders placed by `block(...)` in
Build/GenerateCampusZero.py. Every piece is one mesh object built from bmesh parts (bevelled boxes with
2-3 segment chamfers, lathes, lofts, extruded profiles, hazard-stripe panels), smooth shaded with weighted
normals. Units are metres, +Z up, origin at the centre of the footprint, base at z = 0, front (doors,
cab, lamps) = +X unless noted. Nothing is random, so re-running reproduces identical files.

Art direction (Saved/AgentBriefs/sc2-style.md): the Machine data halls are sleek megastructure segments
(pearl armour plates on dark cores, cyan light channels, hovering roof slabs with fins and a crystal, a red
lens per module) and its cooling tower, chiller, transformer, pylon, mast and fence are advanced energy
infrastructure with glowing cores. The human side is a fortified forward base: armoured crates with hazard
stripes, a wrecked armoured vehicle, modular barricade walls with floodlights, a prefab generator with a
reactor stack, a brazier post and a heavy cable reel.

Outputs (relative to the repo root)
    Art/Environment/SM_Env_<Name>.fbx   one mesh per file, exported at the origin (same FBX settings
                                        as Build/GenerateUnitMeshes.py; written in cm, 1 m = 100 UU)
    Art/Environment/Environment.blend   every piece spread along +X for editing (NOT at the origin;
                                        always export from this script, never from the .blend)
    Art/Environment/Preview.png         side lineup in three rows (tall Machine kit, halls and utilities,
                                        forward base), orthographic, camera looks along +Y
    Art/Environment/PreviewRTS.png      the same three rows from a 50 degree pitch RTS camera
                                        (perspective), every piece beside a 1.2 m human-scale capsule
    Art/Environment/PreviewHalls.png    the five Campus Zero halls assembled from the hall modules

Nominal footprint and height (metres; the script asserts the mesh bounds against this table, 1 cm
tolerance, centred on the origin). "Replaces" is the primitive in Build/GenerateCampusZero.py, so a
piece drops into the same `block()` footprint and the map's collision / keep-out layout is unchanged.
Height is the top of the whole mesh, including rooftop kit, beacons and lamps.

    Mesh             Footprint X x Y   Height  Slots                       Replaces
    DataHallBay      2.00 x 3.00       6.50    Shell Dark Glow Accent      HallA-D / DataHall0 wall
    DataHallDoor     4.00 x 3.00       6.50    Shell Dark Glow Accent      (loading-door wall bay)
    DataHallCorner   3.00 x 3.00       7.00    Shell Dark Glow Accent      (corner column)
    DataHallRoof     2.00 x 2.00       5.80    Shell Dark Glow             (roof infill tile)
    CoolingTower     5.60 dia          9.00    Shell Dark Glow Accent      CoolingTower 560 x 900
    Chiller          2.60 dia          3.00    Shell Dark Glow Accent      CoolingPlantChiller 260 x 300
    Transformer      2.20 x 1.70       2.60    Shell Dark Glow Accent      Substation7Transformer
    Pylon            7.00 x 1.50       13.00   Shell Dark Glow Accent      4 legs (150 x 150) + arm 700
    CommsMast        0.80 x 0.80       15.00   Shell Dark Glow Accent      FibreJunctionMast
    ClusterPylon     0.70 x 0.70       4.20    Shell Dark Glow Accent      ClusterPylon (+ eye)
    Container        6.00 x 2.45       2.60    Shell Dark Accent           Container
    Wreck            4.20 x 1.90       1.70    Shell Dark Accent           Wreck (body 1.3 + cab)
    SandbagWall      1.20 x 3.00       1.10    Shell Dark Accent           BunkerSandbags segment
    BurnBarrel       0.70 dia          1.00    Shell Dark Glow Accent      BurnBarrel
    GeneratorShack   3.00 x 4.00       2.50    Shell Dark Glow Accent      GeneratorShack
    FenceSegment     4.00 x 0.30       2.20    Shell Dark Glow Accent      CampusFence span
    CableSpool       2.00 dia          1.40    Shell Dark Accent           FibreJunctionSpool

Only the slots a mesh uses exist on it, always in the order Shell, Dark, Glow, Accent. The canonical
materials carry neutral values; Unreal (or the previews) override them per piece. Intended looks:
    Shell   Machine: polished pearl / white armour (cluster obelisk: glossy white). Human: painted
            gunmetal / steel-blue armour
    Dark    dark gunmetal cores, recesses, frames, vents, tracks, cable
    Glow    Machine: cyan light channels, energy cores, lamp tips. Human: amber floodlights, reactor and fire
    Accent  Machine: red lens / status lights. Human: hazard-stripe yellow paint (SandbagWall has no Glow
            slot, so its floodlight lenses are Accent)

The map's heights are collision boxes: halls were 6.0 m (DataHall0 5.2 m) tall, transformers 2.6 m,
the wreck body 1.3 m and so on. Only footprints are contractual; the hall wall (parapet top) is 6.0 m
for every hall, so DataHall0 gains 0.8 m. Triangle budget: below 25000 per piece.

Hall assembly (Data halls). A hall of outer size SX x SY (multiples of 1 m, both >= 6 m) is a ring of
3 m deep wall bands around a roof infill. Local +X of every wall module runs along the wall; its
outer face is local -Y. Yaw is about +Z through the module origin:

    corners  4 x DataHallCorner at (+-(SX-3)/2, +-(SY-3)/2), yaw 0 at (-,-), 90 at (+,-), 180 at (+,+),
             270 at (-,+) (outer corner always points away from the hall centre)
    walls    each side has L = side - 6 m of wall between the corners. South (yaw 0, y = -(SY-3)/2),
             East (yaw 90, x = +(SX-3)/2), North (yaw 180), West (yaw 270). Fill L with `d` door
             modules (4 m each, unscaled) and n = floor((L - 4d)/2 + 0.5) bays (min 1 when L-4d > 0),
             each bay scaled along its local X by (L - 4d)/(2n) (0.5 to 1.25), bays split evenly between
             the doors: bay, door, bay, door, bay ...
    roof     W x D = (SX - 6) x (SY - 6); if both > 0 tile DataHallRoof nx = floor(W/2 + 0.5) by
             ny = floor(D/2 + 0.5), scaled (W/(2 nx), D/(2 ny), 1), z offset 0. Skip when W or D is 0.
    (nothing is ever scaled in Z; scale only wall bays and roof tiles, never corners or doors)

Campus Zero halls (bays are scaled along their length by the factor shown; L = wall length between corners):
    DataHall0 12 x 20   N, S: L = 6, 3 bays x1.000 | E, W: L = 14, bay door bay door bay (2 doors,
                        3 bays x1.000) | roof 3 x 7 tiles x1.000
    HallA      9 x 13   N, S: L = 3, 2 bays x0.750 | W: L = 7, bay door bay (2 bays x0.750) |
                        E: L = 7, 4 bays x0.875 | roof 2 x 4 tiles (x0.750, x0.875)
    HallB     13 x  8   S: L = 7, 4 bays x0.875 | N: L = 7, bay door bay (2 bays x0.750) |
                        E, W: L = 2, 1 bay x1.000 | roof 4 x 1 tiles (x0.875, x1.000)
    HallC     17 x  7   S: L = 11, bay bay door bay bay (4 bays x0.875) | N: L = 11, 6 bays x0.917 |
                        E, W: L = 1, 1 bay x0.500 | roof 6 x 1 tiles (x0.917, x0.500)
    HallD     11 x  6   N, S: L = 5, 3 bays x0.833 | E, W: L = 0 | no roof tile (the bands meet)
The script prints the instance count per hall and asserts that the pieces tile each footprint. The
`hall_instances(size_x, size_y, doors)` function holds the exact positions, yaws and scales; the
door sides above are its `HALLS` table.

FBX export axis settings (bpy.ops.export_scene.fbx), identical to Build/GenerateUnitMeshes.py:
    axis_forward='Y', axis_up='Z', global_scale=1.0, apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, use_selection=True,
    use_mesh_modifiers=True, mesh_smooth_type='OFF' (normals only), use_triangles=True,
    add_leaf_bones=False, bake_anim=False, colors_type='LINEAR'.
Every mesh carries the colour attribute "SC2Mask" (R Edge, G Cavity, B Ground, linear floats), baked by
MasterMaterials.bake_masks(obj, "env") right before export; main() re-imports each FBX into a fresh scene
and asserts the values equal the bake within 1/255.
Blender's forward='Y', up='Z' is an identity transform (see the unit script docstring for why Unreal
needs exactly this). Unreal import: Import Uniform Scale 1, Convert Scene on, Force Front X Axis off.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector, kdtree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import MasterMaterials  # noqa: E402

try:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:
    ROOT = os.getcwd()
OUT = os.path.join(ROOT, "Art", "Environment")

SHELL, DARK, GLOW, ACCENT = range(4)
SLOT_NAMES = ("Shell", "Dark", "Glow", "Accent")
FBX_AXES = dict(axis_forward="Y", axis_up="Z")
PREFIX = "SM_Env_"

# name: (footprint X, footprint Y, height, slots). Footprint tolerance is 1 cm.
SPEC = {
    "DataHallBay": (2.0, 3.0, 6.5, "Shell Dark Glow Accent"),
    "DataHallDoor": (4.0, 3.0, 6.5, "Shell Dark Glow Accent"),
    "DataHallCorner": (3.0, 3.0, 7.0, "Shell Dark Glow Accent"),
    "DataHallRoof": (2.0, 2.0, 5.8, "Shell Dark Glow"),
    "CoolingTower": (5.6, 5.6, 9.0, "Shell Dark Glow Accent"),
    "Chiller": (2.6, 2.6, 3.0, "Shell Dark Glow Accent"),
    "Transformer": (2.2, 1.7, 2.6, "Shell Dark Glow Accent"),
    "Pylon": (7.0, 1.5, 13.0, "Shell Dark Glow Accent"),
    "CommsMast": (0.8, 0.8, 15.0, "Shell Dark Glow Accent"),
    "ClusterPylon": (0.7, 0.7, 4.2, "Shell Dark Glow Accent"),
    "Container": (6.0, 2.45, 2.6, "Shell Dark Accent"),
    "Wreck": (4.2, 1.9, 1.7, "Shell Dark Accent"),
    "SandbagWall": (1.2, 3.0, 1.1, "Shell Dark Accent"),
    "BurnBarrel": (0.7, 0.7, 1.0, "Shell Dark Glow Accent"),
    "GeneratorShack": (3.0, 4.0, 2.5, "Shell Dark Glow Accent"),
    "FenceSegment": (4.0, 0.3, 2.2, "Shell Dark Glow Accent"),
    "CableSpool": (2.0, 2.0, 1.4, "Shell Dark Accent"),
}
TRI_LIMIT = 25000


# --------------------------------------------------------------------------------------
# Materials
# --------------------------------------------------------------------------------------
def make_material(name, color, emission=0.0, metallic=0.0, roughness=0.5, coat=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if coat:
        bsdf.inputs["Coat Weight"].default_value = coat
        bsdf.inputs["Coat Roughness"].default_value = 0.1
    if emission:
        bsdf.inputs["Emission Color"].default_value = (*color, 1.0)
        bsdf.inputs["Emission Strength"].default_value = emission
    mat.diffuse_color = (*color, 1.0)
    return mat


def canonical_materials():
    return [
        make_material("Shell", (0.78, 0.81, 0.86), metallic=0.3, roughness=0.3),
        make_material("Dark", (0.03, 0.035, 0.045), metallic=0.6, roughness=0.45),
        make_material("Glow", (0.4, 0.9, 1.0), emission=3.0),
        make_material("Accent", (0.95, 0.62, 0.05), roughness=0.55),
    ]


# --------------------------------------------------------------------------------------
# Geometry builder. One bmesh per piece; every part is committed with a material slot index.
# --------------------------------------------------------------------------------------
def rot_matrix(rot):
    return Euler([math.radians(v) for v in rot], "XYZ").to_matrix()


def clip_poly(pts, axis, value, keep_greater):
    """Sutherland-Hodgman clip of a 2D polygon against one axis-aligned half plane."""
    out = []
    for i, p in enumerate(pts):
        q = pts[(i + 1) % len(pts)]
        p_in = p[axis] >= value if keep_greater else p[axis] <= value
        q_in = q[axis] >= value if keep_greater else q[axis] <= value
        if p_in:
            out.append(p)
        if p_in != q_in:
            t = (value - p[axis]) / (q[axis] - p[axis])
            out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
    return out


def poly_area(pts):
    return 0.5 * sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(pts, pts[1:] + pts[:1]))


class Model:
    def __init__(self, name):
        self.name = PREFIX + name
        self.bm = bmesh.new()

    # -- bookkeeping ----------------------------------------------------------------
    def _mark(self):
        """Fresh scratch bmesh for one part; `_commit` merges it into the piece."""
        return bmesh.new()

    def _commit(self, part, mat, xform=None, recalc=False):
        if xform is not None:
            bmesh.ops.transform(part, matrix=xform, verts=list(part.verts))
        if recalc:
            bmesh.ops.recalc_face_normals(part, faces=list(part.faces))
        mapping = {v: self.bm.verts.new(v.co) for v in part.verts}
        for face in part.faces:
            new = self.bm.faces.new([mapping[v] for v in face.verts])
            new.material_index = face.material_index if mat is None else mat
        part.free()

    # -- primitives -----------------------------------------------------------------
    def box(self, loc, dims, mat, rot=(0, 0, 0), bevel=0.0, seg=1):
        bm = self._mark()
        res = bmesh.ops.create_cube(bm, size=1.0)
        for v in res["verts"]:
            v.co = Vector((v.co.x * dims[0], v.co.y * dims[1], v.co.z * dims[2]))
        if bevel > 0.0:
            edges = list(dict.fromkeys(e for v in res["verts"] for e in v.link_edges))   # ordered: deterministic
            width = min(bevel, 0.45 * min(dims))
            bmesh.ops.bevel(bm, geom=edges, offset=width, offset_type="OFFSET",
                            profile_type="SUPERELLIPSE", segments=seg, profile=0.5, affect="EDGES")
        self._commit(bm, mat, Matrix.Translation(loc) @ rot_matrix(rot).to_4x4())

    def cyl(self, loc, height, dia, mat, axis="z", segs=24, taper=1.0, rot=(0, 0, 0), cap=True):
        """Cylinder centred on loc; taper scales the +axis end radius."""
        bm = self._mark()
        bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segs, radius1=dia / 2,
                              radius2=dia / 2 * taper, depth=height)
        pre = {"z": Matrix.Identity(3), "x": Matrix.Rotation(math.pi / 2, 3, "Y"),
               "y": Matrix.Rotation(-math.pi / 2, 3, "X")}[axis]
        self._commit(bm, mat, Matrix.Translation(loc) @ (rot_matrix(rot) @ pre).to_4x4())

    def rod(self, p0, p1, r0, mat, r1=None, segs=6):
        p0, p1 = Vector(p0), Vector(p1)
        d = p1 - p0
        bm = self._mark()
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segs, radius1=r0,
                              radius2=r0 if r1 is None else r1, depth=d.length)
        rmat = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix()
        self._commit(bm, mat, Matrix.Translation((p0 + p1) / 2) @ rmat.to_4x4())

    def sph(self, loc, dims, mat, segs=16, rot=(0, 0, 0)):
        """UV sphere as a lathe (bmesh's own primitive orders faces by allocator state, not deterministically)."""
        rings = max(6, segs // 2)
        profile = [(0.5 * math.sin(math.pi * k / rings), -0.5 * math.cos(math.pi * k / rings))
                   for k in range(rings + 1)]
        self.lathe(profile, segs, mat, loc=loc, rot=rot, scale=dims)

    def tor(self, loc, major, minor, mat, segs=28, minor_segs=8, rot=(0, 0, 0)):
        """Ring around local Z."""
        bm = self._mark()
        verts = []
        for i in range(segs):
            u = 2.0 * math.pi * i / segs
            for j in range(minor_segs):
                v = 2.0 * math.pi * j / minor_segs
                r = major + minor * math.cos(v)
                verts.append(bm.verts.new((r * math.cos(u), r * math.sin(u), minor * math.sin(v))))
        for i in range(segs):
            for j in range(minor_segs):
                a = verts[i * minor_segs + j]
                b = verts[((i + 1) % segs) * minor_segs + j]
                c = verts[((i + 1) % segs) * minor_segs + (j + 1) % minor_segs]
                d = verts[i * minor_segs + (j + 1) % minor_segs]
                bm.faces.new((a, b, c, d))
        self._commit(bm, mat, Matrix.Translation(loc) @ rot_matrix(rot).to_4x4(), recalc=True)

    def lathe(self, profile, segs, mat, loc=(0, 0, 0), closed=False, radial=None, rot=(0, 0, 0),
              scale=(1.0, 1.0, 1.0)):
        """Revolve (radius, z) points around Z. Points with radius 0 collapse to the axis.

        `closed` joins the last point to the first (ring profiles). `radial(i, r, z)` returns an inset.
        """
        bm = self._mark()
        rings = []
        for r, z in profile:
            if r < 1e-9:
                rings.append(bm.verts.new((0.0, 0.0, z)))
                continue
            ring = []
            for i in range(segs):
                a = 2.0 * math.pi * i / segs
                rr = r - (radial(i, r, z) if radial else 0.0)
                ring.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), z)))
            rings.append(ring)
        n = len(profile)
        for k in range(n if closed else n - 1):
            a, b = rings[k], rings[(k + 1) % n]
            a_pt, b_pt = not isinstance(a, list), not isinstance(b, list)
            if a_pt and b_pt:
                continue
            for i in range(segs):
                j = (i + 1) % segs
                if a_pt:
                    bm.faces.new((a, b[j], b[i]))
                elif b_pt:
                    bm.faces.new((a[i], a[j], b))
                else:
                    bm.faces.new((a[i], a[j], b[j], b[i]))
        self._commit(bm, mat, Matrix.Translation(loc) @ rot_matrix(rot).to_4x4() @ Matrix.Diagonal((*scale, 1.0)),
                     recalc=True)

    def loft(self, rings, mat, loc=(0, 0, 0)):
        """Chamfered-rectangle rings (z, half x, half y, chamfer), capped at both ends."""
        bm = self._mark()
        layers = []
        for z, hx, hy, c in rings:
            pts = [(hx - c, -hy), (hx, -hy + c), (hx, hy - c), (hx - c, hy),
                   (-hx + c, hy), (-hx, hy - c), (-hx, -hy + c), (-hx + c, -hy)]
            layers.append([bm.verts.new((x, y, z)) for x, y in pts])
        for a, b in zip(layers, layers[1:]):
            for i in range(8):
                j = (i + 1) % 8
                bm.faces.new((a[i], a[j], b[j], b[i]))
        bm.faces.new(layers[0])
        bm.faces.new(layers[-1])
        self._commit(bm, mat, Matrix.Translation(loc), recalc=True)

    def prism(self, points, depth, mat, xform):
        """Polygon in local XY extruded along local Z, then transformed by xform (4x4)."""
        bm = self._mark()
        bot = [bm.verts.new((x, y, 0.0)) for x, y in points]
        top = [bm.verts.new((x, y, depth)) for x, y in points]
        bm.faces.new(bot)
        bm.faces.new(top)
        for i in range(len(points)):
            j = (i + 1) % len(points)
            bm.faces.new((bot[i], bot[j], top[j], top[i]))
        self._commit(bm, mat, xform, recalc=True)

    def profile_x(self, pts, x0, x1, mat):
        """Polygon in the (y, z) plane extruded along +X from x0 to x1."""
        self.prism(pts, x1 - x0, mat, Matrix(((0, 0, 1, x0), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1))))

    def profile_y(self, pts, y0, y1, mat):
        """Polygon in the (x, z) plane extruded along +Y from y0 to y1."""
        self.prism(pts, y1 - y0, mat, Matrix(((1, 0, 0, 0), (0, 0, 1, y0), (0, 1, 0, 0), (0, 0, 0, 1))))

    def plane_prism(self, pts, depth, mat, origin, u, v, n):
        """Polygon in the (u, v) plane through `origin`, extruded `depth` along n."""
        rot = Matrix((Vector(u), Vector(v), Vector(n))).transposed().to_4x4()
        self.prism(pts, depth, mat, Matrix.Translation(origin) @ rot)

    def hazard(self, origin, u, v, n, ulen, vlen, depth=0.03, stripe=0.13, base=DARK, paint=ACCENT):
        """Hazard-stripe panel: `ulen` x `vlen` rectangle on the (u, v) plane, raised `depth` along n."""
        e = 0.004
        self.plane_prism([(e, e), (ulen - e, e), (ulen - e, vlen - e), (e, vlen - e)], depth, base, origin, u, v, n)
        a = -vlen
        while a < ulen:
            poly = [(a, 0.0), (a + stripe, 0.0), (a + stripe + vlen, vlen), (a + vlen, vlen)]
            poly = clip_poly(clip_poly(poly, 0, 0.0, True), 0, ulen, False)
            poly = [p for i, p in enumerate(poly) if i == 0 or (abs(p[0] - poly[i - 1][0]) + abs(p[1] - poly[i - 1][1])) > 1e-6]
            if len(poly) >= 3 and abs(poly_area(poly)) > 1e-4:
                self.plane_prism(poly, depth + 0.006, paint, origin, u, v, n)
            a += 2.0 * stripe

    def stamp(self, build, xform):
        """Build a sub-piece with `build(model)` and merge it transformed (a mirror is fine: normals are recomputed)."""
        sub = Model("stamp")
        build(sub)
        self._commit(sub.bm, None, xform, recalc=True)

    def diamond(self, x, y, z0, z1, r, mat, mid=0.5):
        """Square bipyramid (crystal) with its equator radius r at z0 + mid * (z1 - z0)."""
        self.lathe([(0.0, z0), (r, z0 + (z1 - z0) * mid), (0.0, z1)], 4, mat, loc=(x, y, 0.0))

    def ring_stripes(self, z0, h, r0, r1, n, mats=(ACCENT, DARK), loc=(0.0, 0.0)):
        """Annulus cut into n sectors alternating between two materials."""
        for k in range(n):
            a0, a1 = 2.0 * math.pi * k / n, 2.0 * math.pi * (k + 1) / n
            pts = [(r0 * math.cos(a0), r0 * math.sin(a0)), (r1 * math.cos(a0), r1 * math.sin(a0)),
                   (r1 * math.cos(a1), r1 * math.sin(a1)), (r0 * math.cos(a1), r0 * math.sin(a1))]
            self.prism(pts, h, mats[k % 2], Matrix.Translation((loc[0], loc[1], z0)))

    # -- finish ---------------------------------------------------------------------
    def finish(self, materials):
        bm = self.bm
        used = sorted({f.material_index for f in bm.faces})
        remap = {old: new for new, old in enumerate(used)}
        for face in bm.faces:
            face.material_index = remap[face.material_index]
        mesh = bpy.data.meshes.new(self.name)
        bm.to_mesh(mesh)
        bm.free()
        for index in used:
            mesh.materials.append(materials[index])
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(38.0))
        mod = obj.modifiers.new("WeightedNormal", "WEIGHTED_NORMAL")
        mod.keep_sharp = True
        mod.weight = 50
        with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        return obj



# --------------------------------------------------------------------------------------
# Machine: data halls (megastructure wall modules, 3 m deep, parapet top 6.0 m, floating roof kit to 6.5 m)
# --------------------------------------------------------------------------------------
ROOF_Z = 5.5     # roof deck height
WALL_TOP = 6.0   # parapet top


def hall_core(m, x0, x1):
    """Dark structural core of a wall band: outer skin y = -1.36, inner face y = 1.5, deck at 5.5 m."""
    m.box(((x0 + x1) / 2, (-1.36 + 1.5) / 2, ROOF_Z / 2), (x1 - x0, 2.86, ROOF_Z), DARK)


def hall_panel(m, cx, width, z0, z1, inset=True):
    """Pearl armour plate standing proud of the core (face y = -1.44) with a raised centre plate."""
    zc, h = (z0 + z1) / 2, z1 - z0
    m.box((cx, -1.37, zc), (width, 0.14, h), SHELL, bevel=0.06, seg=2)
    if inset:
        m.box((cx, -1.445, zc), (width - 0.24, 0.04, h - 0.34), SHELL, bevel=0.02, seg=2)


def hall_cornice(m, x0, x1):
    """Two swept pearl cornice bands with a recessed cyan light channel between them; top at 6.0 m."""
    m.profile_x([(-1.36, 4.55), (-1.5, 4.75), (-1.5, 5.05), (-0.9, 5.05), (-0.9, 4.55)], x0, x1, SHELL)
    m.box(((x0 + x1) / 2, -1.42, 5.15), (x1 - x0, 0.1, 0.2), GLOW)
    m.profile_x([(-1.5, 5.25), (-1.5, 5.62), (-1.32, WALL_TOP), (-0.9, WALL_TOP), (-0.9, 5.25)], x0, x1, SHELL)


def hall_front(m, x0, x1):
    """Outer skin of a wall run from x0 to x1: plinth, two armour rows, cyan channels, cornice, red lens."""
    mid, w = (x0 + x1) / 2, x1 - x0
    m.box((mid, -1.35, 0.25), (w, 0.3, 0.5), DARK, bevel=0.04, seg=2)
    m.box((mid, -1.485, 0.525), (w - 0.06, 0.03, 0.05), GLOW)
    cols = max(1, int(round(w)))
    pitch = w / cols
    for k in range(cols):
        cx = x0 + pitch * (k + 0.5)
        hall_panel(m, cx, pitch - 0.1, 0.62, 2.95)
        hall_panel(m, cx, pitch - 0.1, 3.1, 4.4)
        if k:
            m.box((x0 + pitch * k, -1.375, 2.51), (0.05, 0.05, 3.78), GLOW)
    m.box((mid, -1.385, 3.025), (w, 0.05, 0.07), GLOW)
    for k in range(5):                                              # louvres on the first lower plate
        m.box((x0 + pitch * 0.5, -1.47, 0.98 + k * 0.2), (max(0.2, pitch - 0.5), 0.03, 0.06), DARK)
    hall_cornice(m, x0, x1)
    m.sph((mid, -1.45, 5.43), (0.2, 0.1, 0.2), ACCENT, segs=12)


def hall_deck(m, x0, x1, y0=-0.85, y1=1.45):
    """Pearl roof deck plate with a dark seam all round."""
    m.box(((x0 + x1) / 2, (y0 + y1) / 2, ROOF_Z + 0.03), (x1 - x0 - 0.1, y1 - y0, 0.06), SHELL, bevel=0.025, seg=2)


def roof_hover(m, cx, cy, w, d, tip):
    """Anti-grav roof element: dark emitter pad, cyan underlight, floating pearl slab with fins up to `tip`."""
    base = ROOF_Z + 0.06
    m.box((cx, cy, base + 0.05), (w - 0.3, d - 0.3, 0.1), DARK, bevel=0.03, seg=2)
    m.box((cx, cy, base + 0.16), (w - 0.5, d - 0.5, 0.03), GLOW)
    m.box((cx, cy, base + 0.42), (w, d, 0.16), SHELL, bevel=0.06, seg=2)
    for sy in (-1, 1):
        m.box((cx, cy + sy * (d / 2 - 0.16), base + 0.505), (w - 0.5, 0.05, 0.02), GLOW)   # cyan slab edge lights
    for sx in (-1, 1):
        x = cx + sx * w * 0.3
        m.profile_x([(cy - 0.5, base + 0.5), (cy + 0.5, base + 0.5), (cy + 0.14, tip), (cy - 0.14, tip)],
                    x - 0.04, x + 0.04, SHELL)
    m.diamond(cx, cy, base + 0.54, tip - 0.1, 0.13, GLOW)


def data_hall_bay():
    m = Model("DataHallBay")
    hall_core(m, -1.0, 1.0)
    hall_front(m, -1.0, 1.0)
    hall_deck(m, -1.0, 1.0)
    roof_hover(m, 0.0, 0.3, 1.86, 1.9, 6.5)
    return m


def data_hall_door():
    m = Model("DataHallDoor")
    hall_core(m, -2.0, 2.0)
    m.box((0, -1.3, 0.06), (2.4, 0.4, 0.12), DARK)                    # dock plate
    m.box((0, -1.42, 0.145), (2.0, 0.03, 0.05), GLOW)
    for sx in (-1, 1):
        m.box((sx * 1.625, -1.35, 0.25), (0.75, 0.3, 0.5), DARK, bevel=0.04, seg=2)
        m.box((sx * 1.27, -1.35, 1.78), (0.2, 0.3, 3.32), SHELL, bevel=0.05, seg=2)   # jambs
        m.profile_y([(sx * 1.17, 3.44), (sx * 1.17, 2.9), (sx * 0.62, 3.44)], -1.5, -1.2, SHELL)   # arch gussets
        hall_panel(m, sx * 1.69, 0.5, 0.62, 2.95, inset=False)
        hall_panel(m, sx * 1.69, 0.5, 3.1, 4.4, inset=False)
        m.box((sx * 1.69, -1.385, 3.025), (0.55, 0.05, 0.07), GLOW)
        m.box((sx * 1.2, -1.36, 4.19), (0.46, 0.14, 0.42), SHELL, bevel=0.05, seg=2)
        m.box((sx * 0.5, -1.36, 4.19), (0.9, 0.14, 0.42), SHELL, bevel=0.05, seg=2)
    m.box((0, -1.35, 3.665), (2.94, 0.3, 0.45), SHELL, bevel=0.06, seg=2)   # lintel
    # Energy gate: cyan curtain behind dark louvre bars.
    m.box((0, -1.3, 1.72), (2.3, 0.03, 3.2), GLOW)
    for k in range(-3, 4):
        m.box((k * 0.33, -1.335, 1.75), (0.12, 0.05, 3.3), DARK, bevel=0.015)
    m.box((0, -1.335, 1.9), (2.3, 0.06, 0.1), DARK)
    m.sph((0, -1.45, 3.665), (0.22, 0.1, 0.22), ACCENT, segs=12)      # red gate lens
    m.tor((0, -1.47, 3.665), 0.17, 0.02, GLOW, segs=24, minor_segs=6, rot=(90, 0, 0))
    hall_cornice(m, -2.0, 2.0)
    hall_deck(m, -2.0, 2.0)
    for sx in (-1, 1):
        roof_hover(m, sx * 1.0, 0.3, 1.86, 1.9, 6.5)
    return m


SWAP_XY = Matrix(((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))   # mirror across the diagonal


def data_hall_corner():
    m = Model("DataHallCorner")
    m.box((0.07, 0.07, ROOF_Z / 2), (2.86, 2.86, ROOF_Z), DARK)
    hall_front(m, -0.6, 1.5)                                          # -Y face
    m.stamp(lambda s: hall_front(s, -0.6, 1.5), SWAP_XY)              # -X face
    hall_deck(m, -0.75, 1.5, y0=-0.85, y1=1.5)
    # Corner tower: pearl column, cyan channels, swept fin, floating red lens and crystal.
    m.box((-1.05, -1.05, 0.25), (0.9, 0.9, 0.5), DARK, bevel=0.05, seg=2)
    m.box((-1.02, -1.02, 3.3), (0.86, 0.86, 5.6), SHELL, bevel=0.12, seg=3)
    m.box((-1.02, -1.475, 3.3), (0.1, 0.05, 4.6), GLOW)
    m.box((-1.475, -1.02, 3.3), (0.05, 0.1, 4.6), GLOW)
    for z in (1.2, 3.3, 5.3):
        m.box((-1.02, -1.02, z), (0.92, 0.92, 0.12), DARK, bevel=0.03, seg=2)
    d = Vector((-0.7071, -0.7071, 0.0))
    n = Vector((-0.7071, 0.7071, 0.0))
    m.plane_prism([(0.3, 2.0), (0.62, 2.6), (0.62, 5.4), (0.5, 6.0), (0.3, 6.0)], 0.07, SHELL,
                  Vector((-1.02, -1.02, 0.0)) + d * 0.0 - n * 0.035, d, (0, 0, 1), n)
    m.box((-1.02, -1.02, 6.16), (0.92, 0.92, 0.12), DARK, bevel=0.04, seg=2)
    m.sph((-1.02, -1.02, 6.32), (0.2, 0.2, 0.2), ACCENT, segs=12)
    m.tor((-1.02, -1.02, 6.32), 0.24, 0.025, GLOW, segs=24, minor_segs=6)
    m.diamond(-1.02, -1.02, 6.5, 7.0, 0.13, GLOW)
    roof_hover(m, 0.5, 0.5, 1.5, 1.5, 6.5)
    return m


def data_hall_roof():
    m = Model("DataHallRoof")
    m.box((0, 0, ROOF_Z / 2), (2.0, 2.0, ROOF_Z), DARK)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * 0.5, sy * 0.5, ROOF_Z + 0.03), (0.92, 0.92, 0.06), SHELL, bevel=0.025, seg=2)
    m.box((0, 0, ROOF_Z + 0.012), (1.96, 0.05, 0.024), GLOW)
    m.box((0, 0, ROOF_Z + 0.012), (0.05, 1.96, 0.024), GLOW)
    m.cyl((0, 0, ROOF_Z + 0.11), 0.1, 0.8, DARK, segs=24)
    m.tor((0, 0, ROOF_Z + 0.18), 0.34, 0.03, GLOW, segs=32, minor_segs=6)
    m.cyl((0, 0, ROOF_Z + 0.255), 0.09, 0.8, SHELL, segs=24, taper=0.8)
    return m


# --------------------------------------------------------------------------------------
# Machine: energy infrastructure (cooling and power)
# --------------------------------------------------------------------------------------
TOWER_K = (2.8 ** 2 - 1.85 ** 2) / 6.2 ** 2


def tower_r(z):
    return math.sqrt(1.85 ** 2 + TOWER_K * (z - 6.2) ** 2)


def cooling_tower():
    m = Model("CoolingTower")
    m.lathe([(0.0, 0.0), (2.8, 0.0), (2.8, 0.2), (2.6, 0.42), (0.0, 0.42)], 64, DARK)
    m.lathe([(2.1, 0.42), (2.45, 0.42), (2.45, 0.46), (2.1, 0.46)], 64, GLOW, closed=True)
    for k in range(4):                                                # red status lenses on the plinth
        a = math.radians(45 + 90 * k)
        m.sph((2.7 * math.cos(a), 2.7 * math.sin(a), 0.2), (0.16, 0.16, 0.16), ACCENT, segs=10)
    m.cyl((0, 0, 4.4), 7.9, 2.3, GLOW, segs=32)                       # energy core, seen through the gaps
    m.lathe([(1.1, 8.3), (1.62, 8.3), (1.62, 8.38), (1.1, 8.38)], 64, DARK, closed=True)   # throat floor ring
    tiers = ((0.5, 2.4), (2.6, 4.5), (4.7, 6.5), (6.7, 8.4))
    for z0, z1 in tiers:                                              # floating fluted pearl rings
        zs = [z0 + (z1 - z0) * k / 5 for k in range(6)]
        ring = [(tower_r(z), z) for z in zs] + [(tower_r(z) - 0.4, z) for z in reversed(zs)]
        m.lathe(ring, 64, SHELL, closed=True, radial=lambda i, r, z: 0.05 if i % 4 >= 2 else 0.0)
        r = tower_r(z1)
        m.lathe([(r - 0.06, z1 - 0.1), (r + 0.02, z1 - 0.1), (r + 0.02, z1 - 0.04), (r - 0.06, z1 - 0.04)], 64,
                GLOW, closed=True)
        rb = tower_r(z0)
        m.lathe([(rb - 0.4, z0), (rb + 0.03, z0), (rb + 0.03, z0 + 0.12), (rb - 0.4, z0 + 0.12)], 64, DARK, closed=True)
    for (_a, z0), (z1, _b) in zip(tiers[:-1], tiers[1:]):             # dark struts hold the rings apart
        zc = (z0 + z1) / 2
        for k in range(8):
            a = math.radians(45 * k + 22.5)
            r = tower_r(zc) - 0.2
            m.box((r * math.cos(a), r * math.sin(a), zc), (0.3, 0.16, z1 - z0 + 0.06), DARK,
                  rot=(0, 0, math.degrees(a)))
    ro = tower_r(8.4)
    m.lathe([(ro - 0.4, 8.4), (ro + 0.04, 8.4), (ro + 0.04, 8.5), (ro - 0.4, 8.5)], 64, DARK, closed=True)
    m.cyl((0, 0, 8.55), 0.4, 0.5, GLOW, segs=16)
    for k in range(4):                                                # arms carrying the floating halos
        a = math.radians(90 * k)
        m.rod((1.75 * math.cos(a), 1.75 * math.sin(a), 8.5), (1.35 * math.cos(a), 1.35 * math.sin(a), 8.72), 0.035,
              DARK, segs=6)
    m.tor((0, 0, 8.72), 1.35, 0.06, GLOW, segs=48, minor_segs=8)
    m.tor((0, 0, 8.94), 0.9, 0.06, GLOW, segs=48, minor_segs=8)
    return m


def chiller():
    m = Model("Chiller")
    m.lathe([(0.0, 0.0), (1.3, 0.0), (1.3, 0.16), (1.15, 0.34), (0.0, 0.34)], 32, DARK)
    m.lathe([(0.98, 0.34), (1.12, 0.34), (1.12, 0.38), (0.98, 0.38)], 32, GLOW, closed=True)
    m.cyl((0, 0, 1.45), 2.1, 1.7, GLOW, segs=24)                      # core, glowing through the plate seams
    for k in range(8):                                                # armour plates around the core
        a = math.radians(45 * k)
        m.box((0.95 * math.cos(a), 0.95 * math.sin(a), 1.45), (0.5, 0.78, 1.9), SHELL, rot=(0, 0, math.degrees(a)),
              bevel=0.09, seg=2)
    for z in (0.55, 2.35):
        m.lathe([(0.8, z - 0.05), (1.25, z - 0.05), (1.25, z + 0.05), (0.8, z + 0.05)], 32, DARK, closed=True)
    m.lathe([(0.8, 1.4), (1.25, 1.4), (1.25, 1.5), (0.8, 1.5)], 32, DARK, closed=True)
    m.lathe([(0.0, 2.4), (1.15, 2.4), (1.0, 2.6), (0.0, 2.6)], 32, DARK)
    m.tor((0, 0, 2.75), 0.85, 0.05, GLOW, segs=40, minor_segs=8)     # floating halo
    m.sph((0, 0, 2.8), (0.4, 0.4, 0.4), GLOW, segs=16)
    for k in range(4):
        a = math.radians(90 * k + 45)
        m.sph((1.22 * math.cos(a), 1.22 * math.sin(a), 0.22), (0.16, 0.16, 0.16), ACCENT, segs=8)
        m.rod((0.9 * math.cos(a), 0.9 * math.sin(a), 2.6), (0.55 * math.cos(a), 0.55 * math.sin(a), 2.72), 0.03, DARK,
              segs=6)
    return m


def transformer():
    m = Model("Transformer")
    m.box((0, 0, 0.1), (2.2, 1.7, 0.2), DARK, bevel=0.04, seg=2)
    m.box((0, 0, 0.87), (1.9, 1.3, 1.34), SHELL, bevel=0.1, seg=3)
    for sy in (-1, 1):
        m.box((0, sy * 0.7, 0.87), (1.7, 0.08, 0.85), GLOW)          # glow behind the radiator fins
        for k in range(10):
            m.box((-0.8 + 0.1778 * k, sy * 0.74, 0.87), (0.05, 0.22, 1.0), SHELL, bevel=0.015)
    m.box((0.955, 0, 1.3), (0.03, 1.0, 0.07), GLOW)                   # front light channel
    m.box((0.955, 0, 0.55), (0.03, 0.5, 0.2), ACCENT)
    m.box((0, 0, 1.58), (1.6, 1.1, 0.08), DARK, bevel=0.02)
    for x in (-0.55, 0.0, 0.55):                                      # insulator spires with cyan tips
        m.cyl((x, -0.15, 1.66), 0.08, 0.34, DARK, segs=12)
        m.cyl((x, -0.15, 2.09), 0.78, 0.3, SHELL, segs=12, taper=0.4)
        for z in (1.85, 2.05, 2.25):
            m.cyl((x, -0.15, z), 0.05, 0.4 - (z - 1.85) * 0.5, SHELL, segs=12)
        m.tor((x, -0.15, 2.2), 0.24, 0.02, GLOW, segs=20, minor_segs=6)
        m.sph((x, -0.15, 2.53), (0.14, 0.14, 0.14), GLOW, segs=10)
    m.cyl((0, 0.4, 1.8), 1.3, 0.2, DARK, axis="x", segs=12)          # bus bar
    for x in (-0.65, 0.65):
        m.box((x, 0.4, 1.68), (0.1, 0.2, 0.2), DARK)
    return m


def pylon():
    m = Model("Pylon")
    m.box((0, 0, 0.25), (1.5, 1.5, 0.5), DARK, bevel=0.1, seg=2)

    def hx(z):
        return 0.58 - 0.30 * (z - 0.5) / 10.1

    m.loft([(0.5, 0.58, 0.58, 0.16), (10.6, 0.28, 0.28, 0.09)], SHELL)
    slope = math.degrees(math.atan(0.30 / 10.1))
    for k in range(4):                                                # cyan channels tilted with the taper
        off = Vector((hx(5.7) + 0.005, 0.0, 5.7))
        off.rotate(Euler((0, 0, math.radians(90 * k))))
        m.box(off, (0.03, 0.08, 8.4), GLOW, rot=(0, -slope, 90 * k))
    for z in (2.8, 5.6, 8.4):
        m.box((0, 0, z), (2 * hx(z) + 0.1, 2 * hx(z) + 0.1, 0.2), DARK, bevel=0.03, seg=2)
    for sx in (-1, 1):                                                # base buttress fins
        m.profile_y([(sx * 0.45, 0.8), (sx * 0.75, 0.5), (sx * 0.75, 0.9), (sx * 0.4, 3.8)], -0.09, 0.09, SHELL)
        m.profile_x([(sx * 0.45, 0.8), (sx * 0.75, 0.5), (sx * 0.75, 0.9), (sx * 0.4, 3.8)], -0.09, 0.09, SHELL)
    m.box((0, 0, 10.68), (0.62, 0.62, 0.16), DARK, bevel=0.04, seg=2)
    m.cyl((0, 0, 11.015), 0.51, 0.16, GLOW, segs=12)                  # energy column carrying the floating arm
    m.box((0, 0, 11.45), (5.4, 0.5, 0.36), SHELL, bevel=0.1, seg=2)
    for sx in (-1, 1):
        m.profile_y([(sx * 2.5, 11.27), (sx * 3.46, 11.4), (sx * 3.46, 11.62), (sx * 2.5, 11.63)], -0.2, 0.2, SHELL)
        m.box((sx * 3.47, 0, 11.51), (0.06, 0.3, 0.14), GLOW)
        m.sph((sx * 3.2, 0, 11.7), (0.14, 0.14, 0.14), ACCENT, segs=8)
        for x in (sx * 1.2, sx * 2.4):                                # hanging cyan insulators
            m.rod((x, 0, 11.27), (x, 0, 10.75), 0.03, DARK, segs=6)
            m.diamond(x, 0, 10.4, 10.85, 0.12, GLOW)
    m.box((0, -0.255, 11.45), (5.0, 0.03, 0.06), GLOW)
    m.box((0, 0.255, 11.45), (5.0, 0.03, 0.06), GLOW)
    m.cyl((0, 0, 11.965), 0.67, 0.14, DARK, segs=8)
    m.tor((0, 0, 12.0), 0.3, 0.03, GLOW, segs=28, minor_segs=6)
    m.diamond(0, 0, 12.35, 13.0, 0.2, GLOW, mid=0.55)
    return m


def comms_mast():
    m = Model("CommsMast")
    m.box((0, 0, 0.15), (0.8, 0.8, 0.3), DARK, bevel=0.06, seg=2)
    m.loft([(0.3, 0.26, 0.26, 0.08), (6.0, 0.17, 0.17, 0.06), (13.2, 0.09, 0.09, 0.03)], SHELL)
    for sx in (-1, 1):
        for prof in ([(sx * 0.2, 0.3), (sx * 0.4, 0.3), (sx * 0.4, 0.6), (sx * 0.2, 3.4)],
                     [(sx * 0.14, 8.6), (sx * 0.4, 8.9), (sx * 0.4, 9.5), (sx * 0.12, 10.4)]):
            m.profile_y(prof, -0.03, 0.03, SHELL)
            m.profile_x(prof, -0.03, 0.03, SHELL)
    for z in (4.2, 6.6, 11.0):                                        # floating cyan halos
        m.tor((0, 0, z), 0.32, 0.025, GLOW, segs=28, minor_segs=6)
        for k in range(2):
            a = math.radians(180 * k)
            m.rod((0.12 * math.cos(a), 0.12 * math.sin(a), z), (0.3 * math.cos(a), 0.3 * math.sin(a), z), 0.018, DARK,
                  segs=6)
    for z in (5.0, 10.0):
        m.box((0, 0, z), (0.5, 0.5, 0.08), DARK, bevel=0.02, seg=2)
    m.box((0, 0, 13.3), (0.36, 0.36, 0.2), DARK, bevel=0.04, seg=2)
    m.sph((0, 0, 13.7), (0.44, 0.44, 0.44), GLOW, segs=16)
    m.lathe([(0.0, 13.95), (0.07, 14.0), (0.0, 15.0)], 6, SHELL)
    m.sph((0, 0, 14.35), (0.24, 0.24, 0.24), ACCENT, segs=12)
    return m


def cluster_pylon():
    m = Model("ClusterPylon")
    m.box((0, 0, 0.12), (0.7, 0.7, 0.24), DARK, bevel=0.06, seg=2)
    m.loft([(0.24, 0.29, 0.29, 0.09), (2.2, 0.23, 0.23, 0.08), (3.1, 0.19, 0.19, 0.07)], SHELL)
    slope = math.degrees(math.atan(0.06 / 1.96))
    for k in range(4):                                                # cyan seams on the four faces
        off = Vector((0.262, 0.0, 1.35))
        off.rotate(Euler((0, 0, math.radians(90 * k))))
        m.box(off, (0.03, 0.05, 2.2), GLOW, rot=(0, -slope, 90 * k))
    for z in (0.75, 1.9):
        m.box((0, 0, z), (0.58 - (z - 0.75) * 0.05, 0.58 - (z - 0.75) * 0.05, 0.1), DARK, bevel=0.025, seg=2)
    m.box((0, 0, 3.15), (0.5, 0.5, 0.1), DARK, bevel=0.03, seg=2)
    for sx in (-1, 1):                                                # crescent prongs cradling the lens
        prong = [(sx * 0.16, 3.12), (sx * 0.34, 3.45), (sx * 0.34, 3.95), (sx * 0.29, 3.85), (sx * 0.29, 3.5),
                 (sx * 0.14, 3.2)]
        m.profile_y(prong, -0.03, 0.03, SHELL)
        m.profile_x(prong, -0.03, 0.03, SHELL)
    m.sph((0, 0, 3.66), (0.5, 0.5, 0.5), ACCENT, segs=24)            # red lens
    m.tor((0, 0, 3.66), 0.32, 0.025, GLOW, segs=28, minor_segs=6, rot=(70, 0, 20))
    m.diamond(0, 0, 4.0, 4.2, 0.09, GLOW)
    return m


# --------------------------------------------------------------------------------------
# Human forward base: armoured crates, wrecked vehicle, barricade, brazier post, prefab generator
# --------------------------------------------------------------------------------------
def container():
    m = Model("Container")
    m.box((0, 0, 0.1), (5.9, 2.3, 0.2), DARK, bevel=0.04, seg=2)      # skid
    m.box((0, 0, 1.25), (5.7, 2.1, 2.3), DARK)                        # core
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * 2.85, sy * 1.075, 1.3), (0.3, 0.3, 2.6), DARK, bevel=0.05, seg=2)   # corner castings
    for side in (-1, 1):
        for k in range(6):                                            # armour plates
            x = -2.25 + 0.9 * k
            m.box((x, side * 1.14, 1.425), (0.84, 0.1, 1.75), SHELL, bevel=0.05, seg=2)
            m.box((x, side * 1.185, 1.425), (0.56, 0.05, 1.3), SHELL, bevel=0.02, seg=2)
        m.hazard((-2.7, side * 1.05, 0.24), (1, 0, 0), (0, 0, 1), (0, side, 0), 5.4, 0.23, depth=0.04)
    m.box((0, 0, 2.45), (5.5, 2.0, 0.1), SHELL, bevel=0.04, seg=2)
    for k in range(4):
        m.box((-2.0 + 1.333 * k, 0, 2.525), (0.12, 2.0, 0.05), DARK, bevel=0.015)
    m.box((-0.7, 0, 2.55), (0.9, 0.7, 0.1), SHELL, bevel=0.03, seg=2)
    m.hazard((2.05, -0.9, 2.5), (0, 1, 0), (1, 0, 0), (0, 0, 1), 1.8, 0.65, depth=0.03)
    # Blast doors at +X.
    m.box((2.86, 0, 1.3), (0.06, 1.9, 2.1), DARK)
    for sy in (-1, 1):
        m.box((2.92, sy * 0.46, 1.3), (0.1, 0.88, 1.95), SHELL, bevel=0.03, seg=2)
        for y in (sy * 0.12, sy * 0.8):
            m.rod((2.975, y, 0.4), (2.975, y, 2.2), 0.02, DARK, segs=6)
    m.hazard((2.97, -0.9, 0.55), (0, 1, 0), (0, 0, 1), (1, 0, 0), 1.8, 0.32, depth=0.024)
    m.tor((2.955, 0, 1.45), 0.14, 0.03, DARK, segs=16, minor_segs=6, rot=(0, 90, 0))
    m.hazard((-2.7, -0.9, 0.55), (0, 1, 0), (0, 0, 1), (-1, 0, 0), 1.8, 0.4, depth=0.03)
    return m


def wreck():
    m = Model("Wreck")
    m.profile_y([(-2.07, 0.45), (-2.07, 0.95), (-1.7, 1.15), (0.6, 1.15), (1.7, 0.85), (2.1, 0.62), (2.1, 0.45)],
                -0.72, 0.72, SHELL)
    m.box((0, 0, 0.42), (4.0, 1.3, 0.3), DARK, bevel=0.05, seg=2)
    # Tracks: the -Y side has lost its front half.
    m.box((0, 0.83, 0.31), (3.9, 0.24, 0.62), DARK, bevel=0.1, seg=2)
    m.box((-0.8, -0.83, 0.31), (2.3, 0.24, 0.62), DARK, bevel=0.1, seg=2)
    m.box((0, 0.83, 0.72), (3.9, 0.24, 0.1), SHELL, bevel=0.04, seg=2)
    m.box((-0.8, -0.83, 0.72), (2.3, 0.24, 0.1), SHELL, bevel=0.04, seg=2)
    for x in (-1.5, -0.75, 0.0, 0.75, 1.5):
        m.cyl((x, 0.92, 0.31), 0.05, 0.46, SHELL, axis="y", segs=16)
        if x <= 0.0:
            m.cyl((x, -0.92, 0.31), 0.05, 0.46, SHELL, axis="y", segs=16)
    for x in (0.75, 1.5):                                             # bare axles where the track is gone
        m.rod((x, -0.9, 0.31), (x, -0.5, 0.31), 0.05, DARK, segs=8)
    # Turret knocked askew, drooping gun, bent antenna.
    m.cyl((-0.3, 0, 1.2), 0.1, 1.0, DARK, segs=20)
    m.box((-0.5, 0.05, 1.35), (1.3, 1.1, 0.4), SHELL, rot=(9, 0, -26), bevel=0.12, seg=2)
    m.box((0.2, 0.3, 1.34), (0.25, 0.4, 0.3), DARK, rot=(0, 0, -26), bevel=0.04, seg=2)
    m.rod((0.3, 0.32, 1.32), (1.75, 0.5, 0.96), 0.07, DARK, r1=0.06, segs=10)
    m.rod((1.6, 0.49, 1.0), (1.85, 0.52, 0.92), 0.1, DARK, segs=10)
    m.rod((-1.25, 0.55, 1.15), (-1.4, 0.6, 1.694), 0.022, DARK, segs=6)
    # Battle damage: blast holes, scorched engine deck, peeled plate, fallen track link, hazard-striped rear.
    m.box((0.6, 0.735, 0.97), (1.0, 0.03, 0.25), DARK)
    m.box((-1.2, 0, 1.17), (0.8, 0.7, 0.04), DARK)
    m.box((1.1, -0.2, 1.02), (0.55, 0.45, 0.05), DARK, rot=(0, 15, 0))
    m.box((1.25, 0.4, 1.1), (0.7, 0.05, 0.5), SHELL, rot=(35, -10, 20), bevel=0.015)
    m.box((1.2, -0.8, 0.2), (1.1, 0.26, 0.18), DARK, rot=(0, -10, 0), bevel=0.05, seg=2)
    m.hazard((-1.65, -0.6, 1.15), (0, 1, 0), (1, 0, 0), (0, 0, 1), 1.2, 0.4, depth=0.03)
    m.hazard((-2.07, -0.7, 0.5), (0, 1, 0), (0, 0, 1), (-1, 0, 0), 1.4, 0.35, depth=0.024)
    return m


def sandbag_wall():
    m = Model("SandbagWall")
    m.box((0, 0, 0.3), (1.0, 3.0, 0.6), DARK)
    length = (3.0 - 2 * 0.05) / 3
    body = [(-0.55, 0.0), (0.55, 0.0), (0.55, 0.5), (0.4, 0.85), (-0.4, 0.85), (-0.55, 0.5)]
    for k in range(3):
        y0 = -1.5 + k * (length + 0.05)
        y1 = y0 + length
        yc = (y0 + y1) / 2
        m.profile_y(body, y0, y1, SHELL)
        m.hazard((-0.4, y0 + 0.06, 0.85), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.8, length - 0.12, depth=0.04, stripe=0.12)
        for sx in (-1, 1):
            m.hazard((sx * 0.55, y0 + 0.08, 0.1), (0, 1, 0), (0, 0, 1), (sx, 0, 0), length - 0.16, 0.3, depth=0.044,
                     stripe=0.1)
        if k != 1:                                                    # floodlights on the end blocks
            m.cyl((0.0, yc, 0.925), 0.07, 0.08, DARK, segs=8)
            m.box((0.04, yc, 1.02), (0.28, 0.5, 0.16), DARK, bevel=0.035, seg=2)
            m.box((0.19, yc, 1.02), (0.02, 0.44, 0.11), ACCENT)   # no Glow slot on this piece: hazard-yellow lens
    return m


def burn_barrel():
    m = Model("BurnBarrel")
    m.lathe([(0.0, 0.0), (0.35, 0.0), (0.35, 0.06), (0.3, 0.3), (0.0, 0.3)], 24, DARK)
    m.ring_stripes(0.06, 0.16, 0.3, 0.35, 16)
    m.cyl((0, 0, 0.46), 0.32, 0.34, SHELL, segs=16, taper=0.85)
    for k in range(4):                                                # brackets to the bowl
        a = math.radians(90 * k + 45)
        m.box((0.2 * math.cos(a), 0.2 * math.sin(a), 0.6), (0.14, 0.05, 0.18), DARK, rot=(0, 0, math.degrees(a)))
    m.lathe([(0.0, 0.6), (0.18, 0.6), (0.33, 0.78), (0.33, 0.83), (0.29, 0.83), (0.15, 0.7), (0.0, 0.7)], 24, SHELL)
    m.cyl((0, 0, 0.715), 0.02, 0.3, GLOW, segs=16)
    m.lathe([(0.0, 0.72), (0.18, 0.78), (0.12, 0.9), (0.0, 1.0)], 8, GLOW)
    for base, r, top in (((0.1, 0.04, 0.72), 0.05, 0.9), ((-0.08, 0.08, 0.72), 0.05, 0.86), ((-0.03, -0.11, 0.72), 0.045, 0.83)):
        m.rod(base, (base[0], base[1], top), r, GLOW, r1=0.012, segs=6)
    return m


def generator_shack():
    m = Model("GeneratorShack")
    m.box((0, 0, 0.11), (3.0, 4.0, 0.22), DARK, bevel=0.05, seg=2)   # skid
    m.box((0, 0, 0.92), (2.7, 3.6, 1.4), SHELL, bevel=0.12, seg=3)   # prefab body
    m.box((0, 0, 1.67), (2.5, 3.4, 0.1), SHELL, bevel=0.05, seg=2)   # roof plate
    for sy in (-1, 1):
        for x in (-0.85, 0.0, 0.85):
            m.box((x, sy * 1.83, 0.92), (0.78, 0.06, 1.0), SHELL, bevel=0.03, seg=2)
        m.box((0, sy * 1.85, 0.3), (2.4, 0.06, 0.14), DARK)
    for y0 in (-1.7, 0.75):                                           # hazard panels flanking the door
        m.hazard((1.35, y0, 0.3), (0, 1, 0), (0, 0, 1), (1, 0, 0), 0.95, 0.6, depth=0.05, stripe=0.1)
    # Front door and lamps.
    m.box((1.37, 0, 0.82), (0.08, 1.3, 1.25), DARK, bevel=0.02)
    m.box((1.42, 0, 0.8), (0.05, 1.0, 1.1), SHELL, bevel=0.02, seg=2)
    m.box((1.4, 0, 1.55), (0.04, 0.9, 0.09), GLOW)
    m.hazard((1.35, -0.5, 1.4), (0, 1, 0), (0, 0, 1), (1, 0, 0), 1.0, 0.12, depth=0.04, stripe=0.06)
    m.hazard((0.4, -1.6, 1.72), (0, 1, 0), (1, 0, 0), (0, 0, 1), 3.2, 0.7, depth=0.03)
    # Reactor stack and cooling stack.
    m.cyl((-0.55, 0.6, 1.79), 0.12, 1.3, DARK, segs=24)
    m.cyl((-0.55, 0.6, 2.06), 0.44, 1.1, SHELL, segs=24)
    m.ring_stripes(1.9, 0.16, 0.55, 0.585, 16, loc=(-0.55, 0.6))
    m.lathe([(0.53, 2.14), (0.575, 2.14), (0.575, 2.2), (0.53, 2.2)], 24, GLOW, closed=True, loc=(-0.55, 0.6, 0.0))
    m.cyl((-0.55, 0.6, 2.32), 0.1, 1.2, DARK, segs=24)
    m.sph((-0.55, 0.6, 2.3), (0.7, 0.7, 0.4), GLOW, segs=16)
    m.cyl((-0.55, -1.0, 1.95), 0.46, 0.7, SHELL, segs=16)
    m.cyl((-0.55, -1.0, 2.2), 0.06, 0.8, DARK, segs=16)
    m.cyl((-0.55, -1.0, 1.98), 0.06, 0.74, GLOW, segs=16)
    m.rod((-0.55, 0.6, 1.95), (-0.55, -1.0, 1.95), 0.05, DARK, segs=8)
    for sy in (-1, 1):                                                # amber floodlight masts
        m.rod((1.15, sy * 1.6, 1.72), (1.15, sy * 1.6, 2.2), 0.04, DARK, segs=8)
        m.box((1.2, sy * 1.6, 2.28), (0.24, 0.3, 0.16), DARK, bevel=0.03, seg=2)
        m.box((1.33, sy * 1.6, 2.28), (0.02, 0.24, 0.11), GLOW)
    return m


# --------------------------------------------------------------------------------------
# Perimeter fence and cable reel
# --------------------------------------------------------------------------------------
def fence_segment():
    m = Model("FenceSegment")
    for sx in (-1, 1):
        x = sx * 1.85
        m.box((x, 0, 0.1), (0.3, 0.3, 0.2), DARK, bevel=0.03, seg=2)
        m.box((x, 0, 1.05), (0.26, 0.26, 1.7), SHELL, bevel=0.05, seg=2)
        m.box((x, -0.135, 1.1), (0.08, 0.03, 1.2), GLOW)
        m.box((x, 0.135, 1.1), (0.08, 0.03, 1.2), GLOW)
        m.box((x, 0, 1.95), (0.3, 0.3, 0.1), DARK, bevel=0.03, seg=2)
        m.cyl((x, 0, 2.03), 0.06, 0.2, DARK, segs=12)
        m.cyl((x, 0, 2.14), 0.12, 0.18, GLOW, segs=12)
    m.box((0, 0, 0.36), (3.4, 0.16, 0.56), SHELL, bevel=0.05, seg=2)   # armoured base wall
    m.box((0, 0, 0.06), (3.4, 0.2, 0.12), DARK)
    m.sph((0, -0.09, 0.4), (0.16, 0.06, 0.16), ACCENT, segs=10)
    for z in (0.7, 1.85):
        m.box((0, 0, z), (3.4, 0.09, 0.09), DARK, bevel=0.02)
    for z in (0.95, 1.2, 1.45, 1.7):                                  # energy bars
        m.box((0, 0, z), (3.4, 0.03, 0.045), GLOW)
    for k in range(9):
        m.box((-1.7 + 0.425 * k, 0, 1.28), (0.05, 0.08, 1.15), DARK)
    m.box((0, 0, 2.0), (3.4, 0.09, 0.07), SHELL, bevel=0.02)
    return m


def cable_spool():
    m = Model("CableSpool")
    m.lathe([(0.0, 0.0), (0.94, 0.0), (1.0, 0.05), (1.0, 0.09), (0.94, 0.14), (0.0, 0.14)], 32, SHELL)
    m.lathe([(0.0, 1.22), (0.94, 1.22), (1.0, 1.26), (1.0, 1.31), (0.94, 1.36), (0.0, 1.36)], 32, SHELL)
    m.ring_stripes(0.03, 0.08, 0.9, 1.0, 24)
    m.ring_stripes(1.36, 0.04, 0.72, 0.94, 24)
    m.cyl((0, 0, 0.68), 1.1, 0.5, DARK, segs=16)
    coil = [(0.0, 0.14)] + [(0.86 if k % 2 == 0 else 0.81, 0.14 + 0.078 * k) for k in range(15)] + [(0.0, 1.22)]
    m.lathe(coil, 32, DARK)
    for z in (0.4, 0.95):
        m.lathe([(0.8, z - 0.05), (0.875, z - 0.05), (0.875, z + 0.05), (0.8, z + 0.05)], 32, ACCENT, closed=True)
    for k in range(6):                                                # bracing plates between the flanges
        a = math.radians(60 * k + 30)
        m.box((0.95 * math.cos(a), 0.95 * math.sin(a), 0.68), (0.08, 0.22, 1.1), DARK, rot=(0, 0, math.degrees(a)),
              bevel=0.02)
    m.cyl((0, 0, 1.38), 0.04, 0.4, DARK, segs=16)
    for k in range(8):
        a = math.radians(45 * k)
        m.cyl((0.3 * math.cos(a), 0.3 * math.sin(a), 1.38), 0.04, 0.07, DARK, segs=6)
    return m

BUILDERS = (
    ("DataHallBay", data_hall_bay), ("DataHallDoor", data_hall_door), ("DataHallCorner", data_hall_corner),
    ("DataHallRoof", data_hall_roof), ("CoolingTower", cooling_tower), ("Chiller", chiller),
    ("Transformer", transformer), ("Pylon", pylon), ("CommsMast", comms_mast),
    ("ClusterPylon", cluster_pylon), ("Container", container), ("Wreck", wreck),
    ("SandbagWall", sandbag_wall), ("BurnBarrel", burn_barrel), ("GeneratorShack", generator_shack),
    ("FenceSegment", fence_segment), ("CableSpool", cable_spool),
)
assert [n for n, _ in BUILDERS] == list(SPEC), "SPEC and BUILDERS disagree"


# --------------------------------------------------------------------------------------
# Hall assembly (documented in the docstring; also drives PreviewHalls.png)
# --------------------------------------------------------------------------------------
# name: (size X, size Y, door modules per side S/E/N/W)
HALLS = {
    "DataHall0": (12, 20, {"E": 2, "W": 2}),
    "HallA": (9, 13, {"W": 1}),
    "HallB": (13, 8, {"N": 1}),
    "HallC": (17, 7, {"S": 1}),
    "HallD": (11, 6, {}),
}


def side_sequence(length, doors):
    """Fill `length` metres with `doors` 4 m door modules and stretched 2 m bays. -> (items, bay scale)."""
    rest = length - 4.0 * doors
    assert rest >= -1e-9, (length, doors)
    bays = 0 if rest < 1e-9 else max(1, int(math.floor(rest / 2.0 + 0.5)))
    scale = rest / (2.0 * bays) if bays else 1.0
    groups = [bays // (doors + 1)] * (doors + 1)
    for k, index in enumerate(sorted(range(doors + 1), key=lambda i: abs(i - doors / 2.0))):
        if k < bays % (doors + 1):
            groups[index] += 1
    items = []
    for g in range(doors + 1):
        items += [("DataHallBay", scale)] * groups[g]
        if g < doors:
            items.append(("DataHallDoor", 1.0))
    return items, scale


def hall_instances(size_x, size_y, doors):
    """[(piece, x, y, yaw, scale_x, scale_y)] in metres/degrees for a hall of outer size size_x x size_y."""
    assert size_x >= 6 and size_y >= 6, (size_x, size_y)
    cx, cy = (size_x - 3) / 2.0, (size_y - 3) / 2.0
    out = []
    for yaw, (x, y) in ((0, (-cx, -cy)), (90, (cx, -cy)), (180, (cx, cy)), (270, (-cx, cy))):
        out.append(("DataHallCorner", x, y, yaw, 1.0, 1.0))
    for side, yaw, length, half in (("S", 0, size_x - 6, cy), ("E", 90, size_y - 6, cx),
                                    ("N", 180, size_x - 6, cy), ("W", 270, size_y - 6, cx)):
        items, _ = side_sequence(length, doors.get(side, 0))
        rot = Matrix.Rotation(math.radians(yaw), 2)
        origin = rot @ Vector((0.0, -half))
        t = -length / 2.0
        for piece, scale in items:
            width = (4.0 if piece == "DataHallDoor" else 2.0 * scale)
            centre = origin + rot @ Vector((t + width / 2.0, 0.0))
            out.append((piece, centre.x, centre.y, yaw, scale if piece == "DataHallBay" else 1.0, 1.0))
            t += width
        assert abs(t - length / 2.0) < 1e-6, (side, t, length)
    w, d = size_x - 6, size_y - 6
    if w > 0 and d > 0:
        nx, ny = int(math.floor(w / 2.0 + 0.5)), int(math.floor(d / 2.0 + 0.5))
        nx, ny = max(nx, 1), max(ny, 1)
        for i in range(nx):
            for j in range(ny):
                out.append(("DataHallRoof", -w / 2.0 + (i + 0.5) * w / nx, -d / 2.0 + (j + 0.5) * d / ny, 0,
                            w / (2.0 * nx), d / (2.0 * ny)))
    return out


def hall_summary(name):
    size_x, size_y, doors = HALLS[name]
    counts = {}
    for piece, _x, _y, _yaw, sx, sy in hall_instances(size_x, size_y, doors):
        key = (piece, round(sx, 3), round(sy, 3))
        counts[key] = counts.get(key, 0) + 1
    return "%s %d x %d: %s" % (name, size_x, size_y, ", ".join(
        "%d x %s%s" % (n, piece, "" if (sx, sy) == (1.0, 1.0) else " (scale %.3f, %.3f)" % (sx, sy))
        for (piece, sx, sy), n in sorted(counts.items())))


def check_hall_footprints(objects):
    """Every hall's modules must exactly tile the hall's outer rectangle (sum of footprint areas)."""
    for name, (size_x, size_y, doors) in HALLS.items():
        area = 0.0
        for piece, _x, _y, _yaw, sx, sy in hall_instances(size_x, size_y, doors):
            fx, fy = SPEC[piece][0], SPEC[piece][1]
            area += fx * sx * fy * sy
        assert abs(area - size_x * size_y) < 1e-6, (name, area, size_x * size_y)


# --------------------------------------------------------------------------------------
# Checks and export
# --------------------------------------------------------------------------------------
def bounds(obj):
    pts = [v.co for v in obj.data.vertices]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def check(name, obj):
    fx, fy, height, slots = SPEC[name]
    lo, hi = bounds(obj)
    got = [s.material.name for s in obj.material_slots]
    assert got == slots.split(), "%s slots %s, expected %s" % (name, got, slots)
    used = {got[p.material_index] for p in obj.data.polygons}
    assert used == set(got), "%s has unused slots" % name
    assert triangles(obj) < TRI_LIMIT, "%s has %d triangles" % (name, triangles(obj))
    tol = 0.01
    assert abs((hi.x - lo.x) - fx) < tol and abs((hi.y - lo.y) - fy) < tol, \
        "%s footprint %.3f x %.3f, expected %.3f x %.3f" % (name, hi.x - lo.x, hi.y - lo.y, fx, fy)
    assert abs(hi.x + lo.x) < tol and abs(hi.y + lo.y) < tol, "%s is not centred: %s %s" % (name, lo, hi)
    assert abs(lo.z) < tol, "%s does not sit on z = 0: %s" % (name, lo)
    assert abs(hi.z - height) < tol, "%s height %.3f, expected %.3f" % (name, hi.z, height)


def export_fbx(obj, path):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"MESH"}, global_scale=1.0,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", use_mesh_modifiers=True,
        mesh_smooth_type="OFF", use_triangles=True, add_leaf_bones=False, bake_anim=False,
        colors_type="LINEAR", **FBX_AXES)


def verify_masks(obj, masks, path):
    """Re-import the FBX into a fresh scene; its SC2Mask colours must equal the baked masks (linear, 1/255).
    The importer reads the raw FBX values (colors_type LINEAR, float attribute): the default SRGB import would
    decode them and hide whether the export wrote the linear mask unchanged."""
    attr = MasterMaterials.MASK_ATTR
    scene = bpy.data.scenes.new("VerifyMasks")
    before = set(bpy.data.objects)
    with bpy.context.temp_override(scene=scene, view_layer=scene.view_layers[0]):
        bpy.ops.import_scene.fbx(filepath=path, colors_type="LINEAR")
    imported = [o for o in bpy.data.objects if o not in before]
    assert len(imported) == 1 and imported[0].type == "MESH", "%s: re-import gave %s" % (obj.name, imported)
    mesh = imported[0].data
    assert attr in mesh.color_attributes, "%s: no %s in FBX, has %s" % (
        obj.name, attr, [a.name for a in mesh.color_attributes])
    layer = mesh.color_attributes[attr]
    assert len(mesh.color_attributes) == 1, "%s: extra colour attributes %s" % (
        obj.name, [a.name for a in mesh.color_attributes])
    tree = kdtree.KDTree(len(obj.data.vertices))
    for v in obj.data.vertices:
        tree.insert(v.co, v.index)
    tree.balance()
    corner = layer.domain == "CORNER"
    assert corner or layer.domain == "POINT", "%s: colour domain %s" % (obj.name, layer.domain)
    worst, mid, sample = 0.0, 0, "none in 0.05..0.95"
    seen = set()
    for i, item in enumerate(layer.data):
        vertex = mesh.loops[i].vertex_index if corner else i
        # Several source vertices can share a position (separate parts of one mesh) with different masks, so a
        # re-imported corner must equal the mask of one of the source vertices at its position.
        near = [index for _, index, _ in tree.find_range(mesh.vertices[vertex].co, 1e-3)]
        assert near, "%s: re-imported vertex %d has no source vertex within 1 mm" % (obj.name, vertex)
        seen.update(near)
        c = item.color
        assert abs(c[3] - 1.0) < 1.0 / 255.0, "%s: alpha %.4f, expected 1" % (obj.name, c[3])
        index = min(near, key=lambda k: max(abs(c[ch] - masks[k][ch]) for ch in range(3)))
        for channel in range(3):
            want = masks[index][channel]
            worst = max(worst, abs(c[channel] - want))
            if 0.05 < want < 0.95:
                mid += 1
                if mid == 1:
                    sample = "v%d (%.4f, %.4f, %.4f) -> (%.4f, %.4f, %.4f)" % (index, *masks[index], *c[:3])
    assert worst <= 1.0 / 255.0, "%s: FBX mask differs from the bake by %.4f (> 1/255)" % (obj.name, worst)
    assert len(seen) == len(obj.data.vertices), "%s: mask covers %d of %d vertices" % (
        obj.name, len(seen), len(obj.data.vertices))
    bpy.data.objects.remove(imported[0])
    bpy.data.meshes.remove(mesh)
    bpy.data.scenes.remove(scene)
    return worst, mid, sample


# --------------------------------------------------------------------------------------
# Preview renders
# --------------------------------------------------------------------------------------
CAMPUS_ASPHALT = (0.045, 0.048, 0.055)   # MI_Asphalt in Build/GenerateCampusZero.py
CAMPUS_SCRAP = (0.075, 0.06, 0.045)      # MI_ScrapGround in Build/GenerateCampusZero.py
SIDE_SIZE = (2400, 2300)
RTS_SIZE = (3000, 1800)
HALLS_SIZE = (2400, 1350)
CAPSULE_HEIGHT, CAPSULE_RADIUS = 1.2, 0.34

LOOKS = {"ClusterPylon": "cluster", "Container": "human", "Wreck": "human", "SandbagWall": "human",
         "BurnBarrel": "human", "GeneratorShack": "human", "CableSpool": "human"}
# Three rows (tall Machine kit, Machine halls and utilities, forward base). Side view stacks the rows in Z,
# RTS view spreads them in Y (first row farthest from the camera).
ROWS = (("Pylon", "CommsMast", "ClusterPylon", "CoolingTower"),
        ("DataHallBay", "DataHallDoor", "DataHallCorner", "DataHallRoof", "Chiller", "Transformer",
         "FenceSegment"),
        ("Container", "Wreck", "SandbagWall", "BurnBarrel", "GeneratorShack", "CableSpool"))
SIDE_ROW_Z = (0.0, -9.0, -14.0)
RTS_ROW_Y = (12.0, 1.0, -9.5)
# yaw so the front (+X: doors, cab, lamps) or long axis faces the camera looking along +Y
PREVIEW_YAW = {"SandbagWall": 90, "GeneratorShack": -90}


def preview_palettes():
    def campus():
        return {
            "Shell": make_material("PV_C_Shell", (0.62, 0.67, 0.76), roughness=0.3, metallic=0.35),
            "Dark": make_material("PV_C_Dark", (0.02, 0.028, 0.045), metallic=0.6, roughness=0.4),
            "Glow": make_material("PV_C_Glow", (0.1, 0.78, 1.0), emission=5.0),
            "Accent": make_material("PV_C_Accent", (1.0, 0.03, 0.03), emission=6.0),
        }

    return {
        "campus": campus(),
        "cluster": {
            "Shell": make_material("PV_W_Shell", (0.85, 0.88, 0.92), roughness=0.18, coat=0.6),
            "Dark": make_material("PV_W_Dark", (0.02, 0.028, 0.045), metallic=0.4, roughness=0.4),
            "Glow": make_material("PV_W_Glow", (0.1, 0.78, 1.0), emission=5.0),
            "Accent": make_material("PV_W_Accent", (1.0, 0.02, 0.02), emission=8.0),
        },
        "human": {
            "Shell": make_material("PV_H_Shell", (0.18, 0.27, 0.42), roughness=0.4, metallic=0.45),
            "Dark": make_material("PV_H_Dark", (0.045, 0.05, 0.06), roughness=0.45, metallic=0.6),
            "Glow": make_material("PV_H_Glow", (1.0, 0.5, 0.08), emission=5.0),
            "Accent": make_material("PV_H_Accent", (0.95, 0.62, 0.03), roughness=0.5),
        },
    }


def make_capsule():
    """1.2 m human-scale capsule (radius 0.34), base at z = 0."""
    m = Model("PV_Capsule")
    r = CAPSULE_RADIUS
    profile = [(0.0, 0.0)]
    for k in range(1, 6):
        a = math.radians(90 - 90 * k / 6)
        profile.append((r * math.cos(a), r - r * math.sin(a)))
    top = CAPSULE_HEIGHT - r
    for k in range(6):
        a = math.radians(90 * k / 6)
        profile.append((r * math.cos(a), top + r * math.sin(a)))
    profile.append((0.0, CAPSULE_HEIGHT))
    m.lathe(profile, 24, SHELL)
    return m.finish([make_material("PV_Capsule", (0.95, 0.8, 0.12), roughness=0.5)])


class PreviewRig:
    def __init__(self, objects):
        self.objects = {o.name[len(PREFIX):]: o for o in objects}
        scene = self.scene = bpy.context.scene
        self.palettes = preview_palettes()
        self.capsule = make_capsule()
        self.capsule.hide_render = True
        scene.render.engine = "CYCLES"
        scene.cycles.device = "CPU"
        scene.cycles.samples = 48
        scene.cycles.use_denoising = True
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = "PNG"
        scene.view_settings.view_transform = "Standard"
        world = bpy.data.worlds.new("PV_World")
        scene.world = world
        world.use_nodes = True
        bg = world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = (0.05, 0.065, 0.12, 1.0)
        bg.inputs["Strength"].default_value = 1.6
        for name, direction, energy, color in (("PV_Key", (0.55, 0.65, -0.45), 2.6, (1.0, 0.93, 0.86)),
                                               ("PV_Fill", (-0.6, 0.4, -0.7), 2.0, (0.5, 0.62, 1.0)),
                                               ("PV_Front", (0.1, 0.8, -0.35), 0.9, (0.65, 0.78, 1.0))):
            data = bpy.data.lights.new(name, "SUN")
            data.energy = energy
            data.color = color
            data.angle = math.radians(6)
            obj = bpy.data.objects.new(name, data)
            obj.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
            scene.collection.objects.link(obj)
        ground_mesh = bpy.data.meshes.new("PV_Ground")
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bm.to_mesh(ground_mesh)
        bm.free()
        self.ground_mesh = ground_mesh
        self.cam_data = bpy.data.cameras.new("PV_Cam")
        self.cam = bpy.data.objects.new("PV_Cam", self.cam_data)
        scene.collection.objects.link(self.cam)
        scene.camera = self.cam
        self.props = []

    def clear(self):
        for obj in self.props:
            bpy.data.objects.remove(obj, do_unlink=True)
        self.props = []

    def ground(self, center, size, color, roughness=0.9):
        obj = bpy.data.objects.new("PV_Ground", self.ground_mesh)
        obj.data = self.ground_mesh
        self.scene.collection.objects.link(obj)
        obj.scale = (size[0], size[1], 0.1)
        obj.location = (center[0], center[1], center[2] - 0.05)
        mat = make_material("PV_Ground", color, roughness=roughness)
        obj.data.materials.clear()
        obj.data.materials.append(mat)
        self.props.append(obj)

    def piece(self, name, location, yaw=0.0, scale=(1.0, 1.0, 1.0)):
        src = self.objects[name]
        src.hide_render = True
        obj = src.copy()
        self.scene.collection.objects.link(obj)
        obj.hide_render = False
        obj.location = location
        obj.rotation_euler = (0.0, 0.0, math.radians(yaw))
        obj.scale = scale
        palette = self.palettes[LOOKS.get(name, "campus")]
        for slot, canonical in zip(obj.material_slots, [s.material.name for s in obj.material_slots]):
            slot.link = "OBJECT"
            slot.material = palette[canonical]
        self.props.append(obj)
        return obj

    def person(self, x, y, z=0.0):
        obj = self.capsule.copy()
        self.scene.collection.objects.link(obj)
        obj.hide_render = False
        obj.location = (x, y, z)
        self.props.append(obj)

    def lineup(self, view):
        """Lay the rows out left to right, each piece followed by a capsule. Returns the widest row."""
        gap = 1.7
        extent = 0.0
        for row, names in enumerate(ROWS):
            x = 0.0
            for name in names:
                fx, fy = SPEC[name][0], SPEC[name][1]
                yaw = PREVIEW_YAW.get(name, 0)
                width = fy if abs(yaw) == 90 else fx
                depth = fx if abs(yaw) == 90 else fy
                y0 = 0.0 if view == "side" else RTS_ROW_Y[row]
                z0 = SIDE_ROW_Z[row] if view == "side" else 0.0
                self.piece(name, (x + width / 2, y0, z0), yaw)
                self.person(x + width + 0.75, y0 - min(depth / 2, 1.0) + 0.3, z0)
                x += width + gap
            extent = max(extent, x)
        return extent

    def camera_side(self, extent):
        self.cam_data.type = "ORTHO"
        self.cam_data.ortho_scale = extent + 0.5
        self.cam_data.clip_end = 500.0
        self.cam.location = (extent / 2 - 0.85, -60.0, 0.45)
        self.cam.rotation_euler = (math.radians(90), 0, 0)
        self.scene.render.resolution_x, self.scene.render.resolution_y = SIDE_SIZE

    def camera_rts(self, target, distance, azimuth=20.0, lens=35, size=RTS_SIZE):
        self.cam_data.type = "PERSP"
        self.cam_data.lens = lens
        self.cam_data.clip_end = 1000.0
        pitch = math.radians(50)
        offset = Vector((0.0, -math.cos(pitch), math.sin(pitch))) * distance
        offset.rotate(Euler((0, 0, math.radians(azimuth))))
        self.cam.location = Vector(target) + offset
        self.cam.rotation_euler = (math.radians(90 - 50), 0, math.radians(azimuth))
        self.scene.render.resolution_x, self.scene.render.resolution_y = size

    def render(self, path):
        bpy.context.view_layer.update()
        self.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

    def halls(self):
        layout = {"DataHall0": (-2.0, 10.0), "HallA": (14.5, 8.0), "HallB": (33.0, 6.0),
                  "HallC": (10.5, -12.0), "HallD": (33.0, -12.0)}
        for name, (ox, oy) in layout.items():
            size_x, size_y, doors = HALLS[name]
            for piece, x, y, yaw, sx, sy in hall_instances(size_x, size_y, doors):
                self.piece(piece, (ox + x, oy + y, 0.0), yaw, (sx, sy, 1.0))
            self.person(ox - size_x / 2 - 1.0, oy - size_y / 2 + 0.6)


def render_previews(objects):
    rig = PreviewRig(objects)
    extent = rig.lineup("side")
    cx = extent / 2 - 0.85
    for z, color in zip(SIDE_ROW_Z, (CAMPUS_ASPHALT, CAMPUS_ASPHALT, CAMPUS_SCRAP)):
        rig.ground((cx, 0.0, z), (extent + 6, 12.0), color)
    rig.camera_side(extent)
    rig.render(os.path.join(OUT, "Preview.png"))
    rig.clear()

    extent = rig.lineup("rts")
    rig.ground((cx, 0.0, 0.0), (400, 400), CAMPUS_ASPHALT)
    rig.ground((cx, RTS_ROW_Y[2] + 0.3, 0.012), (extent + 4, 8.0), CAMPUS_SCRAP)
    rig.camera_rts((cx, 1.0, 3.5), 58.0)
    rig.render(os.path.join(OUT, "PreviewRTS.png"))
    rig.clear()

    rig.halls()
    rig.ground((20.0, 0.0, 0.0), (400, 400), CAMPUS_ASPHALT)
    rig.camera_rts((19.0, 1.0, 1.5), 72.0, azimuth=15.0, lens=32, size=HALLS_SIZE)
    rig.render(os.path.join(OUT, "PreviewHalls.png"))


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = canonical_materials()

    objects = []
    for name, build in BUILDERS:
        obj = build().finish(mats)
        check(name, obj)
        objects.append(obj)
    check_hall_footprints(objects)

    masks = {}
    for obj in objects:
        masks[obj.name] = MasterMaterials.bake_masks(obj, MasterMaterials.scope_of(obj.name, "env"))
        export_fbx(obj, os.path.join(OUT, obj.name + ".fbx"))

    print("MASK VERIFY | mesh | max |FBX - baked| | mid-range samples | sample vertex: baked (R,G,B) -> FBX (R,G,B)")
    total_mid = 0
    for obj in objects:
        worst, mid, sample = verify_masks(obj, masks[obj.name], os.path.join(OUT, obj.name + ".fbx"))
        total_mid += mid
        print("MASK VERIFY | %s | %.5f | %d | %s" % (obj.name, worst, mid, sample))
    assert total_mid > 0, "no mid-range mask values: the sRGB-versus-linear check proves nothing"

    cursor = 0.0
    for obj in objects:
        lo, hi = bounds(obj)
        obj.location = (cursor + (hi.x - lo.x) / 2, 0.0, 0.0)
        cursor += (hi.x - lo.x) + 2.0
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "Environment.blend"))

    print("MESH | footprint X x Y (m) | height (m) | tris | slots")
    for obj in objects:
        lo, hi = bounds(obj)
        print("%s | %.2f x %.2f | %.2f | %d | %s" % (
            obj.name, hi.x - lo.x, hi.y - lo.y, hi.z, triangles(obj), "/".join(s.material.name for s in obj.material_slots)))
    print("HALL ASSEMBLY")
    for name in HALLS:
        print(hall_summary(name))
    sys.stdout.flush()

    for obj in objects:
        obj.location = (0.0, 0.0, 0.0)
    render_previews(objects)
    print("ENVIRONMENT_KIT_DONE")


main()
