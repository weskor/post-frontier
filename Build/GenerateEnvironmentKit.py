"""Generate the Campus Zero environment kit for CoopRTS in Blender ("Neon Nightfall").

Run headless from the repo root (Blender 5.2.1, no assets, no network, deterministic):

    blender -b --factory-startup -P Build/GenerateEnvironmentKit.py

The kit replaces the primitive boxes and cylinders placed by `block(...)` in
Build/GenerateCampusZero.py. Every piece is one mesh object built from bmesh parts (bevelled boxes,
lathes, lofts, lattice rods, corrugated slabs). Units are metres, +Z up, origin at the centre of the
footprint, base at z = 0, front (doors, cab, lamps) = +X unless noted. Nothing is random except
through fixed-seed generators, so re-running reproduces identical files.

Outputs (relative to the repo root)
    Art/Environment/SM_Env_<Name>.fbx   one mesh per file, exported at the origin (same FBX settings
                                        as Build/GenerateUnitMeshes.py; written in cm, 1 m = 100 UU)
    Art/Environment/Environment.blend   every piece spread along +X for editing (NOT at the origin;
                                        always export from this script, never from the .blend)
    Art/Environment/Preview.png         side lineup in three rows (tall Machine kit, halls and utilities,
                                        scrapyard), orthographic, camera looks along +Y
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
    Shell   Machine campus: pale blue-grey panel metal (cluster obelisk: glossy white). Human: khaki
            sheet metal, canvas, timber
    Dark    recesses, frames, vents, tyres, cable
    Glow    Machine: cyan-white seams and lamp tips. Human: amber work lamps and fire
    Accent  Machine: red status lens / lights. Human: paint stripe, rust, patched plates

The map's heights are collision boxes: halls were 6.0 m (DataHall0 5.2 m) tall, transformers 2.6 m,
the wreck body 1.3 m (the cab sat above it with collision off) and so on. Only footprints are
contractual; the hall wall (parapet top) is 6.0 m for every hall, so DataHall0 gains 0.8 m.

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
    add_leaf_bones=False, bake_anim=False.
Blender's forward='Y', up='Z' is an identity transform (see the unit script docstring for why Unreal
needs exactly this). Unreal import: Import Uniform Scale 1, Convert Scene on, Force Front X Axis off.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

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
TRI_LIMIT = 15000


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
        make_material("Shell", (0.60, 0.63, 0.68), roughness=0.45),
        make_material("Dark", (0.05, 0.055, 0.06), metallic=0.5, roughness=0.5),
        make_material("Glow", (0.4, 0.9, 1.0), emission=3.0),
        make_material("Accent", (0.75, 0.22, 0.06), roughness=0.7),
    ]


# --------------------------------------------------------------------------------------
# Geometry builder. One bmesh per piece; every part is committed with a material slot index.
# --------------------------------------------------------------------------------------
def rot_matrix(rot):
    return Euler([math.radians(v) for v in rot], "XYZ").to_matrix()


def trap(t):
    """Trapezoid corrugation wave in [0, 1] with four samples per period: 0, 1, 1, 0."""
    t %= 1.0
    if t < 0.25:
        return t / 0.25
    if t < 0.5:
        return 1.0
    if t < 0.75:
        return 1.0 - (t - 0.5) / 0.25
    return 0.0


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

    def slab(self, origin, u_dir, v_dir, n_dir, us, vs, front, mat_fn, edge_mat=DARK):
        """Closed panel: flat back on the n = 0 plane, front surface at n = front(u, v) > 0."""
        bm = self._mark()
        o, U, V, N = Vector(origin), Vector(u_dir), Vector(v_dir), Vector(n_dir)
        grid = [[bm.verts.new(o + U * u + V * v + N * front(u, v)) for v in vs] for u in us]
        nu, nv = len(us), len(vs)
        for i in range(nu - 1):
            for j in range(nv - 1):
                face = bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]))
                face.material_index = mat_fn((us[i] + us[i + 1]) / 2, (vs[j] + vs[j + 1]) / 2)
        loop = ([(i, 0) for i in range(nu)] + [(nu - 1, j) for j in range(1, nv)]
                + [(i, nv - 1) for i in range(nu - 2, -1, -1)] + [(0, j) for j in range(nv - 2, 0, -1)])
        fronts = [grid[i][j] for i, j in loop]
        backs = [bm.verts.new(o + U * us[i] + V * vs[j]) for i, j in loop]
        for k in range(len(loop)):
            m = (k + 1) % len(loop)
            bm.faces.new((fronts[k], fronts[m], backs[m], backs[k])).material_index = edge_mat
        bm.faces.new(backs).material_index = edge_mat
        self._commit(bm, None, None, recalc=True)

    # -- composites -----------------------------------------------------------------
    def lattice(self, z0, z1, h0, h1, panels, leg_r0, leg_r1, brace_r, leg_mat, brace_mat,
                leg_segs=8, brace_segs=6, rings=True):
        """Square lattice mast: four legs at (+-h, +-h) tapering from h0 to h1, X-braced faces."""
        corners = [(1, 1), (-1, 1), (-1, -1), (1, -1)]

        def h(z):
            return h0 + (h1 - h0) * (z - z0) / (z1 - z0)

        for sx, sy in corners:
            self.rod((sx * h0, sy * h0, z0), (sx * h1, sy * h1, z1), leg_r0, leg_mat, r1=leg_r1, segs=leg_segs)
        levels = [z0 + (z1 - z0) * k / panels for k in range(panels + 1)]
        for k in range(panels):
            za, zb = levels[k], levels[k + 1]
            for c in range(4):
                (ax, ay), (bx, by) = corners[c], corners[(c + 1) % 4]
                for (px, py), (qx, qy) in (((ax, ay), (bx, by)), ((bx, by), (ax, ay))):
                    self.rod((px * h(za), py * h(za), za), (qx * h(zb), qy * h(zb), zb), brace_r, brace_mat,
                             segs=brace_segs)
        if rings:
            for z in levels[1:-1]:
                for c in range(4):
                    (ax, ay), (bx, by) = corners[c], corners[(c + 1) % 4]
                    self.rod((ax * h(z), ay * h(z), z), (bx * h(z), by * h(z), z), brace_r, brace_mat,
                             segs=brace_segs)

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
        return obj



# --------------------------------------------------------------------------------------
# Machine: data halls (wall band modules, 3 m deep, parapet top at 6.0 m, rooftop kit to 6.5 m)
# --------------------------------------------------------------------------------------
ROOF_Z = 5.5     # roof deck height
WALL_TOP = 6.0   # parapet top


def hall_panel_rows(m, xs, rows, width=0.92):
    """Shell panels standing proud of the dark wall core (front face y = -1.48)."""
    for z0, z1 in rows:
        for x in xs:
            m.box((x, -1.42, (z0 + z1) / 2), (width, 0.12, z1 - z0), SHELL, bevel=0.03)


def hall_seams(m, x0, x1):
    """Horizontal cyan channel above the second panel row, recessed behind the panel faces."""
    m.box(((x0 + x1) / 2, -1.40, 4.125), (x1 - x0, 0.04, 0.12), GLOW)
    m.box(((x0 + x1) / 2, -1.38, 3.98), (x1 - x0, 0.03, 0.05), DARK)
    m.box(((x0 + x1) / 2, -1.38, 4.27), (x1 - x0, 0.03, 0.05), DARK)


def hall_parapet(m, x0, x1):
    m.box(((x0 + x1) / 2, -1.34, 5.75), (x1 - x0, 0.28, 0.5), SHELL, bevel=0.04)


def roof_chiller(m, cx, cy, w, d):
    """Rooftop chiller: boxy Shell housing, dark grille, cyan fan ring; hub tops out at 6.5 m."""
    m.box((cx, cy, ROOF_Z + 0.45), (w, d, 0.9), SHELL, bevel=0.05)
    dia = min(w, d) * 0.78
    m.cyl((cx, cy, 6.425), 0.05, dia, DARK, segs=24)
    m.tor((cx, cy, 6.45), dia * 0.42, 0.025, GLOW, segs=24, minor_segs=6)
    for angle in (0, 60, 120):
        m.box((cx, cy, 6.46), (dia * 0.92, 0.05, 0.03), DARK, rot=(0, 0, angle))
    m.cyl((cx, cy, 6.45), 0.1, 0.18, DARK, segs=12)
    for k in range(4):
        m.box((cx, cy + d / 2 + 0.005, ROOF_Z + 0.2 + k * 0.17), (w - 0.3, 0.03, 0.06), DARK)


def roof_vent(m, x, y):
    m.cyl((x, y, ROOF_Z + 0.175), 0.35, 0.3, DARK, segs=12)
    m.cyl((x, y, ROOF_Z + 0.385), 0.07, 0.46, SHELL, segs=12)


def data_hall_bay():
    m = Model("DataHallBay")
    length = 2.0
    m.box((0, 0.06, 2.75), (length, 2.88, ROOF_Z), DARK)
    m.box((0, -1.40, 0.25), (length, 0.2, 0.5), DARK, bevel=0.03)
    hall_panel_rows(m, (-0.5, 0.5), ((0.6, 2.25), (2.3, 3.95), (4.3, 5.45)))
    hall_seams(m, -1.0, 1.0)
    m.box((0, -1.41, 2.28), (0.03, 0.02, 3.35), GLOW)            # vertical seam between the columns
    for k in range(5):                                            # louvres on the lower left panel
        m.box((-0.5, -1.485, 0.9 + k * 0.25), (0.6, 0.03, 0.08), DARK)
    hall_parapet(m, -1.0, 1.0)
    m.box((0, -1.485, 5.75), (0.18, 0.03, 0.09), ACCENT)
    roof_chiller(m, 0.0, -0.35, 1.5, 1.2)
    for x in (-0.55, 0.0, 0.55):
        roof_vent(m, x, 1.0)
    return m


def data_hall_door():
    m = Model("DataHallDoor")
    m.box((0, 0.06, 2.75), (4.0, 2.88, ROOF_Z), DARK)
    for sx in (-1, 1):
        m.box((sx * 1.625, -1.40, 0.25), (0.75, 0.2, 0.5), DARK, bevel=0.03)
        for z0, z1 in ((0.6, 2.25), (2.3, 3.95)):
            m.box((sx * 1.60, -1.42, (z0 + z1) / 2), (0.7, 0.12, z1 - z0), SHELL, bevel=0.03)
    m.box((0, -1.42, 3.63), (2.3, 0.12, 0.64), SHELL, bevel=0.03)   # lintel panel above the door
    hall_seams(m, -2.0, 2.0)
    hall_panel_rows(m, (-1.5, -0.5, 0.5, 1.5), ((4.3, 5.45),))
    # Door frame, roll-up door part raised, cyan light spilling from the dock.
    for sx in (-1, 1):
        m.box((sx * 1.175, -1.42, 1.65), (0.15, 0.16, 3.3), SHELL, bevel=0.03)
    m.box((0, -1.42, 3.275), (2.5, 0.16, 0.15), SHELL, bevel=0.03)
    for k in range(6):
        m.box((0, -1.40, 3.1 - k * 0.36 + 0.0), (2.2, 0.08, 0.3), DARK, bevel=0.015)
    m.box((0, -1.37, 0.5), (2.1, 0.02, 0.85), GLOW)
    m.box((0, -1.25, 0.06), (2.2, 0.5, 0.12), DARK)               # dock plate
    m.sph((0, -1.39, 3.5), (0.22, 0.22, 0.22), ACCENT, segs=12)   # red loading lamp
    hall_parapet(m, -2.0, 2.0)
    for x in (-1.0, 1.0):
        m.box((x, -1.485, 5.75), (0.18, 0.03, 0.09), ACCENT)
    roof_chiller(m, -1.0, -0.35, 1.7, 1.2)
    roof_chiller(m, 1.0, -0.35, 1.7, 1.2)
    for x in (-1.2, 0.0, 1.2):
        roof_vent(m, x, 1.0)
    return m


def data_hall_corner():
    m = Model("DataHallCorner")
    m.box((0.06, 0.06, 2.75), (2.88, 2.88, ROOF_Z), DARK)
    m.box((0, -1.40, 0.25), (3.0, 0.2, 0.5), DARK, bevel=0.03)
    m.box((-1.40, 0.0, 0.25), (0.2, 3.0, 0.5), DARK, bevel=0.03)
    # Panels on the -Y face (x from the column to the next bay) and on the -X face (rotated copy).
    for z0, z1 in ((0.6, 2.25), (2.3, 3.95), (4.3, 5.45)):
        zc, h = (z0 + z1) / 2, z1 - z0
        m.box((-0.08, -1.42, zc), (1.08, 0.12, h), SHELL, bevel=0.03)
        m.box((1.0, -1.42, zc), (0.92, 0.12, h), SHELL, bevel=0.03)
        m.box((-1.42, -0.08, zc), (0.12, 1.08, h), SHELL, bevel=0.03)
        m.box((-1.42, 1.0, zc), (0.12, 0.92, h), SHELL, bevel=0.03)
    hall_seams(m, -1.0, 1.5)
    m.box((-1.40, 0.25, 4.125), (0.04, 2.5, 0.12), GLOW)
    m.box((-1.38, 0.25, 3.98), (0.03, 2.5, 0.05), DARK)
    m.box((-1.38, 0.25, 4.27), (0.03, 2.5, 0.05), DARK)
    # Corner column with vertical seams, parapets, beacon mast.
    m.box((-1.08, -1.08, 3.0), (0.8, 0.8, WALL_TOP), SHELL, bevel=0.08)
    m.box((-1.08, -1.5 + 0.015, 3.0), (0.06, 0.03, 4.6), GLOW)
    m.box((-1.5 + 0.015, -1.08, 3.0), (0.03, 0.06, 4.6), GLOW)
    m.box((-1.08, -1.08, 6.06), (0.8, 0.8, 0.12), DARK, bevel=0.03)
    m.cyl((-1.08, -1.08, 6.46), 0.68, 0.1, DARK, segs=8)
    m.sph((-1.08, -1.08, 6.9), (0.2, 0.2, 0.2), ACCENT, segs=12)
    m.box((0.0, -1.34, 5.75), (3.0, 0.28, 0.5), SHELL, bevel=0.04)
    m.box((-1.34, 0.0, 5.75), (0.28, 3.0, 0.5), SHELL, bevel=0.04)
    m.box((0.0, -1.485, 5.75), (0.18, 0.03, 0.09), ACCENT)
    m.box((-1.485, 0.0, 5.75), (0.03, 0.18, 0.09), ACCENT)
    # Rooftop access bulkhead.
    m.box((0.35, 0.35, ROOF_Z + 0.45), (1.3, 1.3, 0.9), SHELL, bevel=0.05)
    m.box((0.35, -0.305, ROOF_Z + 0.4), (0.6, 0.03, 0.7), DARK)
    m.box((0.35, -0.29, ROOF_Z + 0.82), (0.7, 0.02, 0.06), GLOW)
    roof_vent(m, 1.0, 1.05)
    return m


def data_hall_roof():
    m = Model("DataHallRoof")
    m.box((0, 0, 2.75), (2.0, 2.0, ROOF_Z), DARK)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * 0.5, sy * 0.5, ROOF_Z + 0.02), (0.9, 0.9, 0.04), SHELL, bevel=0.02)
    m.cyl((0, 0, ROOF_Z + 0.1), 0.2, 0.5, DARK, segs=16)
    m.tor((0, 0, ROOF_Z + 0.2), 0.27, 0.03, GLOW, segs=24, minor_segs=6)
    m.cyl((0, 0, ROOF_Z + 0.25), 0.1, 0.7, SHELL, segs=16)
    return m


# --------------------------------------------------------------------------------------
# Machine: cooling and power
# --------------------------------------------------------------------------------------
TOWER_K = (2.8 ** 2 - 1.85 ** 2) / 6.2 ** 2


def tower_r(z):
    return math.sqrt(1.85 ** 2 + TOWER_K * (z - 6.2) ** 2)


def cooling_tower():
    m = Model("CoolingTower")
    zs = [0.0, 0.6, 1.5, 2.5, 3.5, 4.5, 5.5, 6.2, 7.0, 7.8, 8.5, 8.85]
    inner_top = tower_r(8.85) - 0.32
    profile = [(0.0, 0.0)] + [(tower_r(z), z) for z in zs] + [(inner_top, 8.85), (tower_r(7.7) - 0.32, 7.7),
                                                             (0.0, 7.7)]
    m.lathe(profile, 128, SHELL, radial=lambda i, r, z: 0.06 if i % 4 >= 2 and z < 8.85 else 0.0)
    m.lathe([(2.55, 0.0), (2.8, 0.0), (2.72, 0.85), (2.55, 0.85)], 64, DARK, closed=True)
    for z, mat, band in ((1.6, DARK, 0.16), (5.0, DARK, 0.16), (3.3, GLOW, 0.09), (7.4, GLOW, 0.09)):
        r = tower_r(z) + 0.05
        m.lathe([(r - 0.12, z - band / 2), (r, z - band / 2), (r, z + band / 2), (r - 0.12, z + band / 2)],
                64, mat, closed=True)
    for k in range(12):                                          # base ventilation arches
        a = math.radians(k * 30.0)
        m.box((2.62 * math.cos(a), 2.62 * math.sin(a), 0.6), (0.2, 0.75, 1.1), DARK,
              rot=(0, 0, math.degrees(a)), bevel=0.03)
    rim_top = tower_r(9.0) + 0.06
    m.lathe([(inner_top - 0.03, 8.75), (rim_top, 8.75), (rim_top, 9.0), (inner_top - 0.03, 9.0)], 64, DARK,
            closed=True)
    # Fan grille inside the throat.
    m.cyl((0, 0, 7.75), 0.1, 3.1, DARK, segs=32)
    for r, mat in ((1.3, GLOW), (0.75, DARK)):
        m.tor((0, 0, 7.82), r, 0.045, mat, segs=48, minor_segs=6)
    for angle in range(0, 180, 30):
        m.box((0, 0, 7.83), (3.0, 0.07, 0.06), DARK, rot=(0, 0, angle))
    m.cyl((0, 0, 7.9), 0.25, 0.5, DARK, segs=16)
    m.sph((0, 0, 8.03), (0.36, 0.36, 0.2), GLOW, segs=16)
    for k in range(4):                                           # red warning lamps on the rim
        a = math.radians(45 + k * 90.0)
        r = (inner_top + rim_top) / 2
        m.sph((r * math.cos(a), r * math.sin(a), 8.93), (0.14, 0.14, 0.14), ACCENT, segs=8)
    return m


def chiller():
    m = Model("Chiller")
    m.cyl((0, 0, 0.15), 0.3, 2.6, DARK, segs=32)
    m.cyl((0, 0, 1.4), 2.2, 2.3, SHELL, segs=32)
    for z in (0.75, 1.4, 2.05):
        m.cyl((0, 0, z), 0.1, 2.38, DARK, segs=32)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        m.box((1.15 * math.cos(a), 1.15 * math.sin(a), 1.4), (0.05, 0.14, 1.3), GLOW, rot=(0, 0, math.degrees(a)))
        b = math.radians(90 * k)
        m.cyl((1.05 * math.cos(b), 1.05 * math.sin(b), 1.0), 0.5, 0.22, DARK, axis="x", segs=8,
              rot=(0, 0, math.degrees(b)))
    m.rod((1.22 * math.cos(0.5), 1.22 * math.sin(0.5), 0.3), (1.22 * math.cos(0.5), 1.22 * math.sin(0.5), 2.5), 0.08,
          DARK, segs=8)
    m.lathe([(0.98, 2.5), (1.2, 2.5), (1.2, 2.85), (0.98, 2.85)], 32, DARK, closed=True)
    m.cyl((0, 0, 2.7), 0.06, 1.96, DARK, segs=32)
    m.tor((0, 0, 2.74), 0.6, 0.04, GLOW, segs=32, minor_segs=6)
    for angle in range(0, 180, 45):
        m.box((0, 0, 2.75), (1.9, 0.06, 0.04), DARK, rot=(0, 0, angle))
    m.cyl((0, 0, 2.85), 0.3, 0.3, DARK, segs=12)
    for sx in (-1, 1):
        m.sph((sx * 1.2 * math.cos(1.0), 1.2 * math.sin(1.0) * sx, 0.75), (0.14, 0.14, 0.14), ACCENT, segs=8)
    return m


def transformer():
    m = Model("Transformer")
    m.box((0, 0, 0.125), (2.2, 1.7, 0.25), DARK, bevel=0.03)
    m.box((0, 0, 1.0), (1.5, 1.56, 1.5), SHELL, bevel=0.06)
    m.box((0, 0, 1.8), (1.62, 1.66, 0.1), DARK, bevel=0.02)
    for sx in (-1, 1):
        for k in range(12):
            m.box((sx * 0.925, -0.66 + k * 0.12, 1.0), (0.35, 0.04, 1.3), SHELL)
        m.box((sx * 0.925, 0, 0.32), (0.35, 1.5, 0.1), DARK)
        m.box((sx * 0.925, 0, 1.68), (0.35, 1.5, 0.1), DARK)
    m.cyl((0, 0.35, 2.05), 1.5, 0.42, DARK, axis="x", segs=16)
    for sx in (-0.5, 0.5):
        m.box((sx, 0.35, 1.95), (0.1, 0.3, 0.2), DARK)
    for x in (-0.5, 0.0, 0.5):                                    # bushing insulators with cyan tips
        m.cyl((x, -0.3, 1.93), 0.16, 0.24, DARK, segs=12)
        m.cyl((x, -0.3, 2.2), 0.7, 0.13, SHELL, segs=8)
        for k in range(5):
            m.cyl((x, -0.3, 2.06 + k * 0.1), 0.05, 0.32 - k * 0.02, SHELL, segs=12)
        m.cyl((x, -0.3, 2.5), 0.06, 0.16, DARK, segs=8)
        m.cyl((x, -0.3, 2.56), 0.08, 0.14, GLOW, segs=8)
    m.box((0, -0.79, 1.0), (1.2, 0.02, 0.05), GLOW)
    m.box((0, 0.79, 1.0), (1.2, 0.02, 0.05), GLOW)
    m.box((0.3, -0.795, 1.35), (0.42, 0.02, 0.26), ACCENT)
    m.box((-0.3, 0.795, 1.35), (0.42, 0.02, 0.26), ACCENT)
    return m


def pylon():
    m = Model("Pylon")
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * 0.57, sy * 0.57, 0.2), (0.36, 0.36, 0.4), DARK, bevel=0.03)
    m.lattice(0.2, 10.4, 0.62, 0.27, 9, 0.13, 0.085, 0.05, SHELL, SHELL)
    m.lattice(10.4, 11.9, 0.27, 0.2, 2, 0.085, 0.07, 0.05, SHELL, SHELL)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.rod((sx * 0.2, sy * 0.2, 11.9), (0, 0, 12.85), 0.07, SHELL, r1=0.05, segs=8)
    m.cyl((0, 0, 12.7), 0.3, 0.16, DARK, segs=8)
    m.sph((0, 0, 12.9), (0.2, 0.2, 0.2), ACCENT, segs=10)
    # Cross-arm: two planar trusses joined by ties, tapering toward the ends.
    def zb(x):
        return 10.9 + 0.35 * abs(x) / 3.5

    def zt(x):
        return 11.75 - 0.4 * abs(x) / 3.5

    xs = [-3.42 + 0.855 * k for k in range(9)]
    for py in (-0.18, 0.18):
        for a, b in zip(xs, xs[1:]):
            m.rod((a, py, zb(a)), (b, py, zb(b)), 0.06, SHELL)
            m.rod((a, py, zt(a)), (b, py, zt(b)), 0.06, SHELL)
        for k, x in enumerate(xs):
            m.rod((x, py, zb(x)), (x, py, zt(x)), 0.05, SHELL)
        for k in range(8):
            a, b = xs[k], xs[k + 1]
            if k % 2 == 0:
                m.rod((a, py, zb(a)), (b, py, zt(b)), 0.045, SHELL)
            else:
                m.rod((a, py, zt(a)), (b, py, zb(b)), 0.045, SHELL)
    for x in xs:
        m.rod((x, -0.18, zb(x)), (x, 0.18, zb(x)), 0.045, SHELL)
    for sx in (-1, 1):
        m.box((sx * 3.45, 0, 11.3), (0.1, 0.5, 0.36), DARK)
        for x in (sx * 3.0, sx * 1.5):
            m.box((x, 0, zb(x) - 0.05), (0.16, 0.5, 0.1), DARK)
            m.rod((x, 0, zb(x) - 0.1), (x, 0, zb(x) - 0.95), 0.03, DARK, segs=6)
            for k in range(7):
                m.cyl((x, 0, zb(x) - 0.2 - k * 0.11), 0.05, 0.32, SHELL, segs=10)
            m.sph((x, 0, zb(x) - 1.02), (0.22, 0.22, 0.22), GLOW, segs=10)
    return m


def comms_mast():
    m = Model("CommsMast")
    m.box((0, 0, 0.1), (0.8, 0.8, 0.2), DARK, bevel=0.02)
    m.lattice(0.15, 13.5, 0.34, 0.15, 9, 0.06, 0.045, 0.028, SHELL, SHELL)
    for z in (5.0, 10.0):
        m.box((0, 0, z), (0.8, 0.8, 0.07), DARK)
    for z, spread in ((9.7, 0.32), (12.0, 0.2)):                    # sector antennas on the four faces
        for k in range(4):
            a = math.radians(90 * k)
            m.box((spread * math.cos(a), spread * math.sin(a), z), (0.1, 0.24, 1.0), DARK, rot=(0, 0, 90 * k))
            m.box((spread * math.cos(a), spread * math.sin(a), z + 0.55), (0.08, 0.18, 0.06), ACCENT,
                  rot=(0, 0, 90 * k))
    m.cyl((0, 0.3, 7.2), 0.1, 0.72, SHELL, axis="y", segs=16, taper=1.0)  # dish disc
    m.cyl((0, 0.3, 7.2), 0.14, 0.2, DARK, axis="y", segs=10)
    m.box((0, 0, 13.55), (0.5, 0.5, 0.1), DARK, bevel=0.02)
    m.cyl((0, 0, 13.85), 0.5, 0.3, DARK, segs=12)
    m.sph((0, 0, 14.2), (0.42, 0.42, 0.42), GLOW, segs=14)
    m.rod((0, 0, 14.4), (0, 0, 14.85), 0.02, DARK, segs=6)
    m.sph((0, 0, 14.85), (0.3, 0.3, 0.3), ACCENT, segs=10)
    return m


def cluster_pylon():
    m = Model("ClusterPylon")
    m.box((0, 0, 0.1), (0.7, 0.7, 0.2), DARK, bevel=0.03)
    m.loft([(0.2, 0.3, 0.3, 0.07), (3.5, 0.2, 0.2, 0.05)], SHELL)
    # Four cyan seams tilted to follow the taper (half size 0.30 at z 0.2, 0.20 at z 3.5).
    slope = math.degrees(math.atan(0.1 / 3.3))
    for k in range(4):
        offset = Vector((0.256, 0.0, 1.85))
        offset.rotate(Euler((0, 0, math.radians(90 * k))))
        m.box(offset, (0.03, 0.05, 2.7), GLOW, rot=(0, -slope, 90 * k))
    m.box((0, 0, 3.05), (0.5, 0.5, 0.1), DARK, bevel=0.02)
    m.cyl((0, 0, 3.6), 0.22, 0.42, DARK, segs=16)
    m.tor((0, 0, 3.72), 0.26, 0.03, GLOW, segs=24, minor_segs=6)
    m.sph((0, 0, 3.95), (0.5, 0.5, 0.5), ACCENT, segs=20)
    return m


# --------------------------------------------------------------------------------------
# Human scrapyard: containers, wreck, sandbags, barrel, generator shack
# --------------------------------------------------------------------------------------
def container():
    m = Model("Container")
    rng = random.Random(11)
    period, periods = 0.28, 21
    us = [k * period / 4 for k in range(periods * 4 + 1)]           # 5.88 m of corrugation
    zlines = [0.24, 0.5, 0.78, 1.05, 1.3, 1.55, 1.85, 2.15, 2.44]    # stripe rows 1.05 .. 1.55
    for side in (1, -1):
        dents = [(rng.uniform(0.6, 5.3), rng.uniform(0.6, 2.0), rng.uniform(0.25, 0.4), rng.uniform(0.035, 0.05))
                 for _ in range(3)]

        def front(u, v, dents=dents):
            dent = sum(d * math.exp(-((u - u0) ** 2 + (v - v0) ** 2) / (2 * s * s)) for u0, v0, s, d in dents)
            return max(0.02, 0.06 + 0.05 * trap(u / period) - dent)

        m.slab((-2.94, side * 1.08, 0), (1, 0, 0), (0, 0, 1), (0, side, 0), us, zlines, front,
               lambda u, v: ACCENT if 1.05 < v < 1.55 else SHELL)
    end_us = [k * 0.275 / 4 for k in range(8 * 4 + 1)]
    m.slab((-2.86, -1.1, 0), (0, 1, 0), (0, 0, 1), (-1, 0, 0), end_us, [0.24, 1.34, 2.44],
           lambda u, v: 0.05 + 0.04 * trap(u / 0.275), lambda u, v: SHELL)
    m.box((0, 0, 1.3), (5.88, 2.18, 2.4), DARK)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.box((sx * 2.92, sy * 1.145, 1.3), (0.16, 0.16, 2.6), DARK, bevel=0.02)
        m.box((0, sx * 1.175, 2.53), (5.68, 0.1, 0.14), DARK)
        m.box((0, sx * 1.175, 0.1), (5.68, 0.1, 0.2), DARK)
        m.box((sx * 2.95, 0, 2.53), (0.1, 2.3, 0.14), DARK)
        m.box((sx * 2.95, 0, 0.1), (0.1, 2.3, 0.2), DARK)
    m.box((0, 0, 2.54), (5.88, 2.3, 0.06), SHELL)
    for k in range(14):
        m.box((-2.7 + 0.4154 * k, 0, 2.585), (0.1, 2.3, 0.03), SHELL)
    # Doors at +X: two leaves, ribs, locking bars, painted band.
    for sy in (-1, 1):
        m.box((2.92, sy * 0.56, 1.34), (0.08, 1.1, 2.2), SHELL, bevel=0.01)
        m.box((2.96, sy * 0.56, 1.3), (0.02, 1.1, 0.5), ACCENT)
        for k in range(3):
            m.box((2.97, sy * (0.13 + k * 0.34), 1.34), (0.02, 0.05, 2.1), SHELL)
        for y in (sy * 0.2, sy * 0.92):
            m.rod((2.98, y, 0.22), (2.98, y, 2.5), 0.018, DARK, segs=6)
            m.box((2.985, y, 0.5), (0.03, 0.12, 0.06), DARK)
            m.box((2.985, y, 2.2), (0.03, 0.12, 0.06), DARK)
        m.box((2.985, sy * 0.2 + sy * 0.12, 1.2), (0.03, 0.04, 0.3), DARK)
    return m


def wreck():
    m = Model("Wreck")
    m.box((0, 0, 0.42), (4.0, 1.5, 0.3), DARK, bevel=0.05)
    m.box((-0.78, 0, 0.79), (2.55, 1.7, 0.72), SHELL, bevel=0.12, seg=2)      # rear tub
    m.box((1.28, 0, 0.68), (1.5, 1.7, 0.5), SHELL, bevel=0.1, seg=2)          # front tub
    m.box((1.3, 0, 1.0), (0.9, 0.9, 0.25), DARK)                              # exposed engine
    m.box((1.35, 0, 1.22), (1.45, 1.62, 0.1), SHELL, rot=(0, -9, 0), bevel=0.03)   # popped hood
    m.box((-0.35, 0, 1.405), (1.9, 1.6, 0.57), SHELL, bevel=0.06)             # cab
    for sy in (-1, 1):
        m.box((-0.35, sy * 0.805, 1.45), (1.4, 0.02, 0.3), DARK)
    m.box((0.61, 0, 1.45), (0.02, 1.2, 0.3), DARK)
    m.box((-1.31, 0, 1.45), (0.02, 1.2, 0.3), DARK)
    m.box((-0.35, 0, 1.696), (1.7, 1.4, 0.008), DARK)                         # burnt roof
    m.box((1.36, 0, 1.33), (1.25, 1.5, 0.02), DARK, rot=(0, -9, 0))           # burnt hood
    for sy in (-1, 1):                                                        # soot up the flanks
        m.box((-0.5, sy * 0.855, 1.0), (2.2, 0.02, 0.34), DARK)
    m.box((0.0, 0.85, 0.85), (1.1, 0.02, 0.3), ACCENT)                        # rust patches
    m.box((-1.4, -0.85, 0.9), (0.9, 0.02, 0.25), ACCENT)
    m.box((-1.2, 0.3, 1.16), (0.8, 0.5, 0.02), ACCENT)
    m.box((2.04, 0, 0.55), (0.12, 1.6, 0.16), DARK, bevel=0.02)
    m.box((-2.04, 0, 0.55), (0.12, 1.6, 0.16), DARK, bevel=0.02)
    for x in (-1.35, 1.35):
        for y in (-0.825, 0.825):
            if x > 0 and y < 0:                                                # this wheel is gone
                m.cyl((x, y, 0.3), 0.2, 0.3, ACCENT, axis="y", segs=12)
                continue
            m.cyl((x, y, 0.34), 0.22, 0.68, DARK, axis="y", segs=16)
            m.cyl((x, y, 0.34), 0.25, 0.36, ACCENT, axis="y", segs=12)
    return m


def sandbag_wall():
    m = Model("SandbagWall")
    rng = random.Random(5)
    h = 0.22
    layers = [[(-0.3, 0.6), (0.3, 0.6)]] * 3 + [[(-0.225, 0.45), (0.225, 0.45)], [(0.0, 0.7)]]
    for course, columns in enumerate(layers):
        if course % 2 == 0:
            spans = [(-1.5 + 0.75 * k, -1.5 + 0.75 * (k + 1)) for k in range(4)]
        else:
            spans = [(-1.5, -1.125)] + [(-1.125 + 0.75 * k, -1.125 + 0.75 * (k + 1)) for k in range(3)] + [(1.125, 1.5)]
        for x, width in columns:
            for y0, y1 in spans:
                roll = rng.random()
                mat = DARK if roll < 0.14 else ACCENT if roll < 0.2 else SHELL
                m.box((x, (y0 + y1) / 2, course * h + h / 2), (width, y1 - y0, h), mat, bevel=0.095, seg=3)
    return m


def burn_barrel():
    m = Model("BurnBarrel")
    m.lathe([(0.0, 0.0), (0.3, 0.0), (0.32, 0.05), (0.32, 0.8), (0.3, 0.82), (0.26, 0.82), (0.26, 0.7),
             (0.0, 0.7)], 24, SHELL)
    for z in (0.22, 0.6):
        m.lathe([(0.3, z - 0.035), (0.35, z - 0.035), (0.35, z + 0.035), (0.3, z + 0.035)], 24, DARK, closed=True)
    m.lathe([(0.29, 0.0), (0.335, 0.0), (0.335, 0.1), (0.29, 0.1)], 24, ACCENT, closed=True)
    m.lathe([(0.25, 0.79), (0.335, 0.79), (0.335, 0.84), (0.25, 0.84)], 24, ACCENT, closed=True)
    m.cyl((0, 0, 0.72), 0.04, 0.5, GLOW, segs=16)
    for base, r, top, tilt in (((0, 0, 0.72), 0.14, 1.0, (0, 0)), ((0.12, 0.02, 0.72), 0.08, 0.93, (0.02, 0)),
                               ((-0.09, 0.1, 0.72), 0.08, 0.95, (-0.02, 0.03)),
                               ((-0.05, -0.12, 0.72), 0.07, 0.9, (0, -0.03)),
                               ((0.06, -0.1, 0.72), 0.06, 0.88, (0.03, -0.02))):
        m.rod(base, (base[0] + tilt[0], base[1] + tilt[1], top), r, GLOW, r1=0.025, segs=8)
    return m


def corr_wall(m, origin, u_dir, n_dir, ulen, z0, z1, mat, crest=0.06, amp=0.06):
    periods = max(1, round(ulen / 0.28))
    us = [ulen * k / (4 * periods) for k in range(4 * periods + 1)]
    m.slab(origin, u_dir, (0, 0, 1), n_dir, us, [z0, (z0 + z1) / 2, z1],
           lambda u, v: crest + amp * trap(u * periods / ulen), lambda u, v: mat)


def wall_plate(m, axis, sign, along, z, w, h, mat, face):
    """Bolted plate on a wall whose corrugation crest is at `face` (plate 4 cm thick, bolt heads to face + 0.06)."""
    t = 0.04
    p = sign * (face + t / 2)
    b = sign * (face + t + 0.01)
    if axis == "x":
        m.box((p, along, z), (t, w, h), mat)
    else:
        m.box((along, p, z), (w, t, h), mat)
    for du in (-1, 1):
        for dv in (-1, 1):
            u, v = along + du * (w / 2 - 0.08), z + dv * (h / 2 - 0.08)
            if axis == "x":
                m.cyl((b, u, v), 0.02, 0.07, DARK, axis="x", segs=6)
            else:
                m.cyl((u, b, v), 0.02, 0.07, DARK, axis="y", segs=6)


def generator_shack():
    m = Model("GeneratorShack")
    slope = math.degrees(math.atan(0.1))
    m.box((0, 0, 0.075), (3.0, 4.0, 0.15), DARK, bevel=0.02)
    m.box((0, 0, 1.25), (2.62, 3.62, 2.2), DARK)
    # Corrugated walls: back planes at |x| = 1.32 and |y| = 1.82, crests at 1.44 and 1.94.
    corr_wall(m, (1.32, -1.82, 0), (0, 1, 0), (1, 0, 0), 1.1, 0.15, 2.36, SHELL)
    corr_wall(m, (1.32, 0.72, 0), (0, 1, 0), (1, 0, 0), 1.1, 0.15, 2.36, SHELL)
    corr_wall(m, (1.32, -0.72, 0), (0, 1, 0), (1, 0, 0), 1.44, 2.15, 2.36, SHELL)
    corr_wall(m, (-1.32, -1.82, 0), (0, 1, 0), (-1, 0, 0), 3.64, 0.15, 2.1, SHELL)
    for sy in (-1, 1):
        corr_wall(m, (-1.32, sy * 1.82, 0), (1, 0, 0), (0, sy, 0), 2.64, 0.15, 2.1, SHELL, amp=0.06)
        y0 = 1.82 if sy > 0 else -1.94
        m.prism([(-1.32, 2.1), (1.32, 2.1), (1.32, 2.362)], 0.12, SHELL,
                Matrix(((1, 0, 0, 0), (0, 0, 1, y0), (0, 1, 0, 0), (0, 0, 0, 1))))
    # Door in the front wall, work lamp above it.
    for sy in (-1, 1):
        m.box((1.46, sy * 0.75, 1.15), (0.08, 0.1, 2.0), SHELL, bevel=0.01)
    m.box((1.46, 0, 1.15), (0.06, 1.4, 2.0), DARK)
    m.box((1.495, 0.25, 1.1), (0.01, 0.7, 0.55), ACCENT)
    m.box((1.49, -0.5, 1.1), (0.02, 0.06, 0.3), DARK)
    for z in (0.5, 1.8):
        m.box((1.485, 0.6, z), (0.03, 0.08, 0.14), DARK)
    m.box((1.49, 0, 2.26), (0.02, 1.2, 0.12), GLOW)
    for k in range(7):
        m.box((1.49, -0.6 + k * 0.2, 2.26), (0.02, 0.03, 0.18), DARK)
    m.box((1.49, 0, 2.355), (0.02, 1.26, 0.03), DARK)
    m.box((1.49, 0, 2.165), (0.02, 1.26, 0.03), DARK)
    for sy in (-1, 1):                                            # side lamps
        m.box((1.05, sy * 1.965, 2.0), (0.06, 0.06, 0.16), DARK)
        m.sph((1.05, sy * 1.915, 1.95), (0.16, 0.16, 0.16), GLOW, segs=10)
    # Bolted patch plates.
    for sy in (-1, 1):
        wall_plate(m, "y", sy, -0.5, 1.4, 0.9, 0.7, ACCENT, 1.94)
        wall_plate(m, "y", sy, 0.7, 0.9, 0.8, 0.6, DARK, 1.94)
        wall_plate(m, "x", sy * -1, sy * 0.8, 1.2, 0.8, 0.7, DARK if sy > 0 else ACCENT, 1.44)
    wall_plate(m, "x", 1, -1.2, 1.3, 0.8, 0.7, ACCENT, 1.44)
    wall_plate(m, "x", 1, 1.25, 0.9, 0.7, 0.6, DARK, 1.44)
    # Monopitch roof: top of the ribs runs from z = 2.5 at the front to 2.2 at the back.
    rm = rot_matrix((0, -slope, 0))
    n = rm @ Vector((0, 0, 1))
    length = 3.0 / math.cos(math.radians(slope)) - 0.02
    surface = Vector((0, 0, 2.31))
    m.box(surface - n * 0.04, (length, 4.0, 0.08), SHELL, rot=(0, -slope, 0))
    c = surface + n * 0.02
    for k in range(10):
        m.box((c.x, -1.8 + 0.4 * k, c.z), (length, 0.09, 0.04), DARK, rot=(0, -slope, 0))
    m.cyl((-0.7, 1.0, 2.34), 0.26, 0.22, DARK, segs=10)
    m.cyl((-0.7, 1.0, 2.485), 0.03, 0.3, ACCENT, segs=10)
    return m


# --------------------------------------------------------------------------------------
# Machine perimeter fence and Fibre Junction spool
# --------------------------------------------------------------------------------------
def fence_segment():
    m = Model("FenceSegment")
    m.box((0, 0, 0.08), (3.64, 0.3, 0.16), SHELL, bevel=0.02)
    for sx in (-1, 1):
        m.box((sx * 1.91, 0, 1.0), (0.18, 0.2, 2.0), DARK, bevel=0.02)
        m.box((sx * 1.91, 0, 2.04), (0.18, 0.22, 0.08), DARK, bevel=0.015)
        m.cyl((sx * 1.91, 0, 2.14), 0.12, 0.12, GLOW, segs=8)
        for dy in (-1, 1):
            m.rod((sx * 1.91, 0, 1.9), (sx * 1.91, dy * 0.12, 2.12), 0.025, DARK, segs=6)
    for z, dia in ((1.95, 0.08), (1.0, 0.05), (0.22, 0.06)):
        m.cyl((0, 0, z), 3.64, dia, DARK, axis="x", segs=8)
    for dy in (-1, 1):
        m.rod((-1.82, dy * 0.12, 2.12), (1.82, dy * 0.12, 2.12), 0.018, DARK, segs=5)
    xa, xb, za, zb = -1.82, 1.82, 0.22, 1.95
    pitch = 0.44
    for family in (1, -1):
        y = 0.012 * family
        cs = [xa - zb - 0.2 + pitch * k for k in range(16)] if family > 0 else [xa + za - 0.2 + pitch * k for k in range(16)]
        for c in cs:
            if family > 0:                         # z = x - c
                lo, hi = max(xa, za + c), min(xb, zb + c)
                p, q = (lo, y, lo - c), (hi, y, hi - c)
            else:                                  # z = c - x
                lo, hi = max(xa, c - zb), min(xb, c - za)
                p, q = (lo, y, c - lo), (hi, y, c - hi)
            if hi - lo > 0.05:
                m.rod(p, q, 0.028, SHELL, segs=5)
    m.box((0, 0, 1.3), (0.34, 0.03, 0.22), ACCENT)
    return m


def cable_spool():
    m = Model("CableSpool")
    m.cyl((0, 0, 0.05), 0.1, 2.0, SHELL, segs=32)
    m.cyl((0, 0, 1.33), 0.1, 1.96, SHELL, segs=32)
    m.lathe([(0.9, 1.28), (1.0, 1.28), (1.0, 1.39), (0.9, 1.39)], 32, ACCENT, closed=True)
    m.cyl((0, 0, 0.69), 1.18, 0.7, DARK, segs=16)
    coil = [(0.0, 0.1)] + [(0.86 if k % 2 == 0 else 0.81, 0.1 + 0.084 * k) for k in range(15)] + [(0.0, 1.28)]
    m.lathe(coil, 32, DARK)
    for z in (0.4, 0.95):
        m.lathe([(0.8, z - 0.05), (0.875, z - 0.05), (0.875, z + 0.05), (0.8, z + 0.05)], 32, ACCENT, closed=True)
    for k in range(8):
        a = math.radians(45 * k)
        m.cyl((0.62 * math.cos(a), 0.62 * math.sin(a), 1.39), 0.02, 0.1, DARK, segs=6)
    m.cyl((0, 0, 1.39), 0.02, 0.34, DARK, segs=12)
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
        **FBX_AXES)


# --------------------------------------------------------------------------------------
# Preview renders
# --------------------------------------------------------------------------------------
CAMPUS_ASPHALT = (0.045, 0.048, 0.055)   # MI_Asphalt in Build/GenerateCampusZero.py
CAMPUS_SCRAP = (0.075, 0.06, 0.045)      # MI_ScrapGround in Build/GenerateCampusZero.py
SIDE_SIZE = (2400, 2300)
RTS_SIZE = (3000, 1800)
HALLS_SIZE = (2400, 1350)
CAPSULE_HEIGHT, CAPSULE_RADIUS = 1.2, 0.34

# Which look each piece is previewed with (see the docstring; Unreal assigns the real materials).
LOOKS = {"ClusterPylon": "cluster", "Container": "scrap", "Wreck": "scrap", "SandbagWall": "scrap",
         "BurnBarrel": "scrap", "GeneratorShack": "scrap", "CableSpool": "scrap"}
# Three rows (tall Machine kit, Machine halls and utilities, scrapyard). Side view stacks the rows in Z,
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
            "Shell": make_material("PV_C_Shell", (0.34, 0.38, 0.45), roughness=0.45, metallic=0.25),
            "Dark": make_material("PV_C_Dark", (0.018, 0.02, 0.026), metallic=0.6, roughness=0.45),
            "Glow": make_material("PV_C_Glow", (0.35, 0.9, 1.0), emission=9.0),
            "Accent": make_material("PV_C_Accent", (1.0, 0.03, 0.03), emission=6.0),
        }

    return {
        "campus": campus(),
        "cluster": {
            "Shell": make_material("PV_W_Shell", (0.86, 0.88, 0.9), roughness=0.22, coat=0.6),
            "Dark": make_material("PV_W_Dark", (0.03, 0.035, 0.045), metallic=0.4, roughness=0.4),
            "Glow": make_material("PV_W_Glow", (0.45, 0.95, 1.0), emission=9.0),
            "Accent": make_material("PV_W_Accent", (1.0, 0.02, 0.02), emission=8.0),
        },
        "scrap": {
            "Shell": make_material("PV_S_Shell", (0.46, 0.40, 0.26), roughness=0.85, metallic=0.05),
            "Dark": make_material("PV_S_Dark", (0.14, 0.145, 0.155), roughness=0.6, metallic=0.3),
            "Glow": make_material("PV_S_Glow", (1.0, 0.55, 0.15), emission=9.0),
            "Accent": make_material("PV_S_Accent", (0.6, 0.22, 0.07), roughness=0.75),
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

    for obj in objects:
        export_fbx(obj, os.path.join(OUT, obj.name + ".fbx"))

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
