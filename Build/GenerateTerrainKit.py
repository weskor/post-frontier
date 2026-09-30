"""Generate the SC2-style terrain kit (cliffs, ramps, destructible rocks, uplink tower) for CoopRTS in Blender.

Run headless from the repo root (Blender 5.2.1, no network, deterministic; the previews use the CC0 textures of
Art/Textures through Build/MasterMaterials.py):

    blender -b --factory-startup -P Build/GenerateTerrainKit.py
    blender -b --factory-startup -P Build/GenerateTerrainKit.py -- --skip-render    # meshes, FBX, checks, blend only

Nothing here starts Unreal. Units are metres, +Z up (1 m = 100 Unreal units); the FBX files are written with the
same settings and the same linear "SC2Mask" vertex colours as Build/GenerateEnvironmentKit.py (see the end of this
docstring). The script only writes Art/Terrain/.

GRID CONTRACT
=============
    module (cell)       400 cm x 400 cm (4.0 m). Cell centres sit on multiples of 400 cm: cell (i, j) is centred
                        at (400 i, 400 j) and covers [400 i - 200, 400 i + 200] in X and the same in Y. 800 cm
                        pieces are centred on a cell corner (odd multiples of 200 cm in both axes).
    cliff height step   300 cm (3.0 m). Level k has its top surface at z = 300 k. The map may use 1 or 2 steps.
    origin              the CENTRE of the module's footprint, at LOW-ground level (z = 0 is the low ground of that
                        piece). For a second step, place the same pieces with z = 300 on top of the first
                        step's Plateau_Fill / cliff tops (the origin is always the ground the piece stands on;
                        every piece is a solid block from its origin plane up to its top surface).
    +X                  points OUT of the cliff face, towards the low ground, and is the ramp's downhill end.
    yaw                 about +Z through the origin, multiples of 90 degrees only. A piece whose face looks along
                        direction (cos yaw, sin yaw) is placed with that yaw (0 = +X, 90 = +Y, 180 = -X, 270 = -Y).
    naming              SM_<Piece> is the Human (rock + steel) look, SM_<Piece>_Machine the Machine look, the
                        same geometry contract. SM_Rocks_* and SM_Watchtower have a single look.

Which cell holds which piece (plateau cells are the ones whose top is at +300 cm; "low" cells are ground):
    Cliff_Straight       plateau cell with ONE low orthogonal neighbour; faces +X. Solid cell, top at z = 300.
    Cliff_CornerOuter    plateau cell with TWO adjacent low neighbours (+X and +Y at yaw 0): a convex corner. The
                         plateau is a quarter disc of radius 400 cm centred on the cell corner (-200, -200);
                         yaw 90 faces (+Y, -X), 180 faces (-X, -Y), 270 faces (-Y, +X).
    Cliff_CornerInner    LOW-ground cell that has plateau on TWO adjacent sides (-X and -Y at yaw 0): a concave
                         corner. The piece is the quarter-round fillet that fills the corner: the cell minus a
                         disc of radius 400 cm centred on (+200, +200). It joins the Straight faces of the two
                         neighbouring plateau lines tangentially. The plateau cells that touch it on its -X and
                         -Y sides are then plain Plateau_Fill (their faces are replaced by the fillet's arc).
                         Yaw 90 puts the solid corner at (+X, -Y), 180 at (+X, +Y), 270 at (-X, +Y).
    Plateau_Fill         plateau cell with no low neighbour, or the cells beside a ramp's top edge.
    Ramp_Wide / Narrow   low-ground footprint 800 x 800 / 800 x 400 cm in front of a plateau edge: the ramp's
                         top edge (x = -400 in the piece) coincides with the plateau edge line, and the plateau
                         cells behind it are Plateau_Fill. Wide covers two cells of a Straight face run, Narrow
                         one; the cliff run continues on both sides of the ramp (Straight or CornerOuter).
    Rocks_Destructible   low-ground footprint 400 x 240 / 800 x 300 cm; the width runs along Y and seals a gap of
                         exactly 400 / 800 cm between two walls (yaw 90 for a gap that runs along X).
    Watchtower           400 x 400 cm round pad (diameter 400), origin at the pad centre, on whatever level it
                         stands on (z = 0 low ground, z = 300 on a plateau).

Every seam between neighbouring cliff modules is a flat 400 x 300 cm rectangle: the face relief (strata, terraces,
chamfer) is multiplied by a smooth envelope that reaches exactly zero at both ends of a module, so a Straight, a
CornerOuter and a CornerInner meet with an identical, perfectly flat cross-section. The script asserts this to
1 mm (`check_seams`) and re-checks a full assembly (`check_assembly`).

SLOTS AND LOOKS (Shell / Dark / Glow / Accent, always in that order, only the slots a mesh uses)
    Human cliff   Shell  steel retaining plates, I-beam, coping band and lip
                  Dark   the layered rock (Unreal: an MI child overriding BaseColor only, warm slate/earth grey)
                  Glow   amber flood-lamp lenses on the beam
                  Accent hazard-stripe band along the top lip
    Machine cliff Shell  terraced pearl panels; Dark plinth and lip undersides; Glow cyan light lines (flush bands)
    Human ramp    Shell  steel treads, parapet, caps; Dark grip deck and wedge; Glow amber lamps; Accent hazard
                  edge stripes on the deck and the parapet
    Machine ramp  Shell  pearl deck, wedge and parapet; Dark tread lines and caps; Glow cyan guide lines
    Plateau_Fill  Human: Dark ground + Shell frame ring. Machine: Shell tile with Dark seam, Glow ring
    Rocks         Dark rock, Shell weathered caps (both Unreal overrides of BaseColor), Glow fault cracks: the
                  tell-tale that they can be blown up
    Watchtower    Machine relay pylon: Shell pearl drums and fins, Dark core and pad, Glow halos and vision ring,
                  Accent the red lens (the Machine's HAL nod)

COLLISION INTENT (documented here, authored on the Unreal side: one simple hull per cell / piece, no per-triangle)
    Cliffs (all three, both looks)   BLOCK: one solid hull from z = 0 to the top. The top surface is a WALKABLE
                                     plateau plane at z = 300 cm; face relief is visual only.
    Plateau_Fill                     one box; the top is a walkable plane at z = 300 cm.
    Ramps                            WALKABLE deck (single plane, see below); parapets and the wedge below the deck
                                     BLOCK (a hull per parapet, one for the wedge). The deck strip is clear.
    Rocks_Destructible               BLOCK until destroyed (one box hull, full width, sealed at both ends). The
                                     destroyed actor replaces the mesh; rubble has no collision.
    Watchtower                       pad top (28 cm above ground, chamfered rim) is walkable; the column
                                     (radius 90 cm) BLOCKS; standing next to it is the vision spot (gameplay later).

RAMP GEOMETRY (exact, asserted)
    footprint       Wide 800 x 800 cm, Narrow 800 x 400 cm, centred on the origin; x = +400 is the foot (low end,
                    z = 0), x = -400 the top (z = 300). Run 800 cm, rise 300 cm.
    deck            ONE plane, z = 300 (400 - x) / 800. Slope 3/8 = 20.56 degrees (limit 30; the UE default
                    walkable angle is 44.8). It reaches z = 0 exactly along x = +400 with zero thickness (no lip,
                    no step) and z = 300 exactly along x = -400 (flush with the plateau tiles). Tread ribs, guide
                    lines and hazard edges are COPLANAR colour patches, not geometry.
    walkable width  Wide 700 cm, Narrow 300 cm (parapet walls are 50 cm thick inside the footprint, 55 cm high above
                    the deck plus a 10 cm cap). The unit capsule (radius 34, half-height 60) fits 10x / 4x abreast;
                    nothing sits above the deck strip (asserted), so the 120 cm capsule has full headroom.
    side walls      wedge courses below the deck plus a parapet above it, so the footprint is closed on both
                    sides (a retaining wall the same shape as the wedge). Height of the whole piece: 365 cm.

Piece table (nominal footprint X x Y and height in metres; the script asserts bounds to 1 mm; triangles < 25000)
    Cliff_Straight[_Machine]      4.0 x 4.0    3.00
    Cliff_CornerOuter[_Machine]   4.0 x 4.0    3.00   (quarter-disc plateau; its bounds are the full cell)
    Cliff_CornerInner[_Machine]   4.0 x 4.0    3.00   (fillet; its bounds are the full cell)
    Ramp_Wide[_Machine]           8.0 x 8.0    3.65
    Ramp_Narrow[_Machine]         8.0 x 4.0    3.65
    Plateau_Fill[_Machine]        4.0 x 4.0    3.00
    Rocks_Destructible_Small      2.4 x 4.0    2.60
    Rocks_Destructible_Large      3.0 x 8.0    3.40
    Watchtower                    4.0 x 4.0    9.00
(X x Y of the ramps and rocks are the piece's own axes: rocks are 240 x 400 cm at yaw 0.)

Outputs (relative to the repo root)
    Art/Terrain/SM_<Piece>.fbx    one mesh per file at the origin, FBX settings of GenerateEnvironmentKit.py
    Art/Terrain/Terrain.blend     the kit spread along +X (never export from the .blend) with the SC2 master
                                  materials applied, plus the "Assembly" collection of the preview scene
    Art/Terrain/Preview.png       side lineup, orthographic, camera looks along +Y (Human, Machine, other)
    Art/Terrain/PreviewRTS.png    the same rows from the 50 degree pitch RTS camera beside a 1.2 m capsule
    Art/Terrain/PreviewAssembly.png  an L-shaped plateau built ONLY from the kit (five outer corners, six
                                  straights, one inner corner, nine fills, a wide ramp, rocks, tower) with two units
                                  at the ramp for scale, RTS camera

FBX export axis settings (bpy.ops.export_scene.fbx), identical to GenerateEnvironmentKit.py:
    axis_forward='Y', axis_up='Z', global_scale=1.0, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
    object_types={'MESH'}, use_selection=True, use_mesh_modifiers=True, mesh_smooth_type='OFF', use_triangles=True,
    add_leaf_bones=False, bake_anim=False, colors_type='LINEAR'.
Every mesh carries the colour attribute "SC2Mask" (R Edge, G Cavity, B Ground, linear floats) baked by
MasterMaterials.bake_masks(obj, "env") right before export ("env" is the coarsest existing scope; there is no
terrain scope); main() re-imports every FBX and asserts the values equal the bake within 1/255.
Unreal import: Import Uniform Scale 1, Convert Scene on, Force Front X Axis off, vertex colour Replace.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector, kdtree
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import MasterMaterials  # noqa: E402

try:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:
    ROOT = os.getcwd()
OUT = os.path.join(ROOT, "Art", "Terrain")

SHELL, DARK, GLOW, ACCENT = range(4)
SLOT_NAMES = ("Shell", "Dark", "Glow", "Accent")
FBX_AXES = dict(axis_forward="Y", axis_up="Z")
PREFIX = "SM_"
CELL = 4.0
HALF = CELL / 2.0
STEP = 3.0
RAMP_RUN = 8.0
PARAPET_T, PARAPET_H, CAP_H = 0.5, 0.55, 0.10
TRI_LIMIT = 25000
CAPSULE_HEIGHT, CAPSULE_RADIUS = 1.2, 0.34
SKIP_RENDER = "--skip-render" in sys.argv

# name: (size X, size Y, height, slots, look). Footprint and height are asserted to 1 mm.
SPEC = {
    "Cliff_Straight": (4.0, 4.0, 3.0, "Shell Dark Glow Accent", "human"),
    "Cliff_CornerOuter": (4.0, 4.0, 3.0, "Shell Dark Glow Accent", "human"),
    "Cliff_CornerInner": (4.0, 4.0, 3.0, "Shell Dark Glow Accent", "human"),
    "Cliff_Straight_Machine": (4.0, 4.0, 3.0, "Shell Dark Glow", "machine"),
    "Cliff_CornerOuter_Machine": (4.0, 4.0, 3.0, "Shell Dark Glow", "machine"),
    "Cliff_CornerInner_Machine": (4.0, 4.0, 3.0, "Shell Dark Glow", "machine"),
    "Ramp_Wide": (8.0, 8.0, 3.65, "Shell Dark Glow Accent", "human"),
    "Ramp_Narrow": (8.0, 4.0, 3.65, "Shell Dark Glow Accent", "human"),
    "Ramp_Wide_Machine": (8.0, 8.0, 3.65, "Shell Dark Glow", "machine"),
    "Ramp_Narrow_Machine": (8.0, 4.0, 3.65, "Shell Dark Glow", "machine"),
    "Plateau_Fill": (4.0, 4.0, 3.0, "Shell Dark", "human"),
    "Plateau_Fill_Machine": (4.0, 4.0, 3.0, "Shell Dark Glow", "machine"),
    "Rocks_Destructible_Small": (2.4, 4.0, 2.6, "Shell Dark Glow", "rock"),
    "Rocks_Destructible_Large": (3.0, 8.0, 3.4, "Shell Dark Glow", "rock"),
    "Watchtower": (4.0, 4.0, 9.0, "Shell Dark Glow Accent", "machine"),
}


# --------------------------------------------------------------------------------------
# Materials (canonical kit slots; the SC2 master instances are applied to display copies only)
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


# Terrain overrides of the SC2 master instances (UNREAL.md section 7: children that override BaseColor only,
# plus the grime / highlight amounts that would otherwise make earth look like painted metal).
MasterMaterials.VARIANTS["Terrain"] = {
    "Dark": dict(BaseColor=(0.15, 0.115, 0.085), Metallic=0.0, Roughness=0.92, EdgeHighlight=0.25, EdgeWear=0.1,
                 BareMetalColor=(0.32, 0.27, 0.22), GrimeAmount=0.35, GroundGrime=0.5, PanelStrength=0.12,
                 PanelLine=0.0),
}
MasterMaterials.VARIANTS["Rock"] = {
    "Dark": dict(BaseColor=(0.13, 0.115, 0.10), Metallic=0.0, Roughness=0.9, EdgeHighlight=0.3, EdgeWear=0.1,
                 BareMetalColor=(0.3, 0.27, 0.24), GrimeAmount=0.35, GroundGrime=0.5, PanelStrength=0.10,
                 PanelLine=0.0),
    "Shell": dict(BaseColor=(0.34, 0.28, 0.21), Metallic=0.0, Roughness=0.85, ClearCoat=0.0, PearlAmount=0.0,
                  EdgeHighlight=0.3, EdgeWear=0.1, BareMetalColor=(0.5, 0.44, 0.36), GrimeAmount=0.3,
                  GroundGrime=0.3, PanelStrength=0.10, PanelLine=0.0),
}
DISPLAY = {"human": ("Human", "Terrain"), "machine": ("Machine", ""), "rock": ("Human", "Rock")}


# --------------------------------------------------------------------------------------
# Geometry helpers
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


def hn(a, b=0, c=0):
    """Deterministic integer hash noise in [0, 1)."""
    h = (a * 374761393 + b * 668265263 + c * 2147483647 + 1013904223) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h ^= h >> 16
    return (h & 0xFFFFFF) / float(0x1000000)


def smoothstep(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


def poly(bm, pts, mat, outward=None):
    """Add one planar polygon (duplicate / zero-area input is dropped); optionally flip it to face `outward`."""
    clean = []
    for p in pts:
        v = Vector(p)
        if not clean or (v - clean[-1]).length > 1e-7:
            clean.append(v)
    while len(clean) > 1 and (clean[0] - clean[-1]).length <= 1e-7:
        clean.pop()
    if len(clean) < 3:
        return None
    normal = Vector()
    for a, b in zip(clean[1:-1], clean[2:]):
        normal += (a - clean[0]).cross(b - clean[0])
    if normal.length < 2e-10:
        return None
    try:
        face = bm.faces.new([bm.verts.new(v) for v in clean])
    except ValueError:
        return None
    face.material_index = mat
    if outward is not None and normal.dot(Vector(outward)) < 0:
        face.normal_flip()
    return face


class Model:
    def __init__(self, name):
        self.name = PREFIX + name
        self.bm = bmesh.new()

    def _mark(self):
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

    def raw(self, fill, mat=None, recalc=True):
        """Fill a scratch bmesh with `fill(bm)` (poly() faces), weld it (1e-5 m) and merge it into the piece."""
        part = self._mark()
        fill(part)
        bmesh.ops.remove_doubles(part, verts=list(part.verts), dist=1e-5)
        self._commit(part, mat, None, recalc=recalc)

    def box(self, loc, dims, mat, rot=(0, 0, 0), bevel=0.0, seg=1):
        bm = self._mark()
        res = bmesh.ops.create_cube(bm, size=1.0)
        for v in res["verts"]:
            v.co = Vector((v.co.x * dims[0], v.co.y * dims[1], v.co.z * dims[2]))
        if bevel > 0.0:
            edges = list(dict.fromkeys(e for v in res["verts"] for e in v.link_edges))
            width = min(bevel, 0.45 * min(dims))
            bmesh.ops.bevel(bm, geom=edges, offset=width, offset_type="OFFSET",
                            profile_type="SUPERELLIPSE", segments=seg, profile=0.5, affect="EDGES")
        self._commit(bm, mat, Matrix.Translation(loc) @ rot_matrix(rot).to_4x4())

    def cyl(self, loc, height, dia, mat, axis="z", segs=24, taper=1.0, rot=(0, 0, 0), cap=True):
        bm = self._mark()
        bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segs, radius1=dia / 2,
                              radius2=dia / 2 * taper, depth=height)
        pre = {"z": Matrix.Identity(3), "x": Matrix.Rotation(math.pi / 2, 3, "Y"),
               "y": Matrix.Rotation(-math.pi / 2, 3, "X")}[axis]
        self._commit(bm, mat, Matrix.Translation(loc) @ (rot_matrix(rot) @ pre).to_4x4())

    def sph(self, loc, dims, mat, segs=16, rot=(0, 0, 0)):
        rings = max(6, segs // 2)
        profile = [(0.5 * math.sin(math.pi * k / rings), -0.5 * math.cos(math.pi * k / rings))
                   for k in range(rings + 1)]
        self.lathe(profile, segs, mat, loc=loc, rot=rot, scale=dims)

    def tor(self, loc, major, minor, mat, segs=28, minor_segs=8, rot=(0, 0, 0)):
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

    def lathe(self, profile, segs, mat, loc=(0, 0, 0), closed=False, rot=(0, 0, 0), scale=(1.0, 1.0, 1.0)):
        """Revolve (radius, z) points around Z. Points with radius 0 collapse to the axis."""
        bm = self._mark()
        rings = []
        for r, z in profile:
            if r < 1e-9:
                rings.append(bm.verts.new((0.0, 0.0, z)))
                continue
            rings.append([bm.verts.new((r * math.cos(2.0 * math.pi * i / segs),
                                        r * math.sin(2.0 * math.pi * i / segs), z)) for i in range(segs)])
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

    def prism(self, points, depth, mat, xform):
        bm = self._mark()
        bot = [bm.verts.new((x, y, 0.0)) for x, y in points]
        top = [bm.verts.new((x, y, depth)) for x, y in points]
        bm.faces.new(bot)
        bm.faces.new(top)
        for i in range(len(points)):
            j = (i + 1) % len(points)
            bm.faces.new((bot[i], bot[j], top[j], top[i]))
        self._commit(bm, mat, xform, recalc=True)

    def profile_y(self, pts, y0, y1, mat):
        """Polygon in the (x, z) plane extruded along +Y from y0 to y1."""
        self.prism(pts, y1 - y0, mat, Matrix(((1, 0, 0, 0), (0, 0, 1, y0), (0, 1, 0, 0), (0, 0, 0, 1))))

    def profile_x(self, pts, x0, x1, mat):
        """Polygon in the (y, z) plane extruded along +X from x0 to x1."""
        self.prism(pts, x1 - x0, mat, Matrix(((0, 0, 1, x0), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1))))

    def plane_prism(self, pts, depth, mat, origin, u, v, n):
        rot = Matrix((Vector(u), Vector(v), Vector(n))).transposed().to_4x4()
        self.prism(pts, depth, mat, Matrix.Translation(origin) @ rot)

    def diamond(self, x, y, z0, z1, r, mat, mid=0.5):
        self.lathe([(0.0, z0), (r, z0 + (z1 - z0) * mid), (0.0, z1)], 4, mat, loc=(x, y, 0.0))

    def sweep(self, frame, t0, t1, n, section, mat):
        """Extrude the closed (d, z) `section` along the cliff face from t0 to t1 (n steps)."""
        def fill(bm):
            rings = [[(*frame.xy(t0 + (t1 - t0) * j / n, d), z) for d, z in section] for j in range(n + 1)]
            m = len(section)
            for j in range(n):
                for k in range(m):
                    poly(bm, [rings[j][k], rings[j + 1][k], rings[j + 1][(k + 1) % m], rings[j][(k + 1) % m]], mat)
            poly(bm, rings[0][::-1], mat)
            poly(bm, rings[-1], mat)
        self.raw(fill, mat)

    def rock(self, cx, cy, z0, dims, seed, split=True, clamp_y=None, segs=9, rings=5):
        """Faceted boulder: a noise-displaced ellipsoid with a flat base at z0. `dims` = (X, Y, height) before noise.
        Faces looking up become Shell (weathered cap) when `split`, the rest Dark. `clamp_y` = half-width plane."""
        sx, sy, sz = dims

        def fill(bm):
            grid = []
            for k in range(rings + 1):
                phi = math.pi * k / rings
                zl = max(-math.cos(phi), -0.4)
                z = z0 + (zl + 0.4) / 1.4 * sz * (0.92 + 0.16 * hn(seed, 99, k))
                rr = math.sin(phi)
                if k in (0, rings):
                    grid.append((cx, cy, z))
                    continue
                ring = []
                for i in range(segs):
                    th = 2.0 * math.pi * (i + 0.5 * (k % 2)) / segs
                    f = 0.72 + 0.5 * hn(seed, i, k)
                    x = cx + 0.5 * sx * rr * math.cos(th) * f
                    y = cy + 0.5 * sy * rr * math.sin(th) * f
                    if clamp_y is not None:
                        y = max(-clamp_y, min(clamp_y, y))
                    ring.append((x, y, z))
                grid.append(ring)
            for k in range(rings):
                a, b = grid[k], grid[k + 1]
                for i in range(segs):
                    j = (i + 1) % segs
                    if k == 0:
                        poly(bm, [a, b[j], b[i]], DARK)
                    elif k == rings - 1:
                        poly(bm, [a[i], a[j], b], DARK)
                    else:
                        poly(bm, [a[i], a[j], b[j], b[i]], DARK)
        part = self._mark()
        fill(part)
        bmesh.ops.remove_doubles(part, verts=list(part.verts), dist=1e-5)
        bmesh.ops.recalc_face_normals(part, faces=list(part.faces))
        part.normal_update()
        for f in part.faces:
            f.material_index = SHELL if split and f.normal.z > 0.62 else DARK
        self._commit(part, None, None, recalc=False)

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
# Cliff faces. A frame maps (t, d) to plan-view XY: t in [0, 1] runs along the face, d >= 0 is the recess of the
# face into the plateau. The relief is multiplied by an envelope that is exactly 0 at t = 0 and t = 1, so every
# module ends in a flat 400 x 300 cm cross-section.
# --------------------------------------------------------------------------------------
class StraightFrame:
    name, n, margin, length = "straight", 40, 0.14, CELL
    plates = ((0.22, 0.47), (0.53, 0.78))
    lumps = (0.17, 0.83)      # parameter range the rock lumps may occupy
    back_loop = [(-HALF, HALF), (-HALF, -HALF)]      # closes the top / bottom polygon from t = 1 back to t = 0
    interior = ((-HALF, -HALF), (-HALF, HALF))       # inner end of the cap plane at t = 0, t = 1
    has_back = True

    def xy(self, t, d):
        return (HALF - d, -HALF + CELL * t)

    def limit(self, t):
        return 1e9

    def yaw(self, t):
        return 0.0

    def inside(self, x, y):
        return x <= HALF + 1e-3


class OuterFrame:
    """Convex corner: quarter disc of radius 4 m centred on the cell corner (-2, -2); t = 0 at (2, -2)."""
    name, n, margin, length = "outer", 64, 0.10, CELL * math.pi / 2
    plates = ((0.22, 0.47), (0.53, 0.78))
    lumps = (0.17, 0.83)
    back_loop = [(-HALF, -HALF)]
    interior = ((-HALF, -HALF), (-HALF, -HALF))
    has_back = False

    def xy(self, t, d):
        a = math.pi / 2 * t
        return (-HALF + (CELL - d) * math.cos(a), -HALF + (CELL - d) * math.sin(a))

    def limit(self, t):
        return 1e9

    def yaw(self, t):
        return math.degrees(math.pi / 2 * t)

    def inside(self, x, y):
        return math.hypot(x + HALF, y + HALF) <= CELL + 1e-3


class InnerFrame:
    """Concave corner: the cell minus the disc of radius 4 m centred on (2, 2); t = 0 at (-2, 2), t = 1 at (2, -2)."""
    name, n, margin, length = "inner", 64, 0.10, CELL * math.pi / 2
    plates = ((0.37, 0.48), (0.52, 0.63))
    lumps = (0.37, 0.63)
    back_loop = [(-HALF, -HALF)]
    interior = ((-HALF, -HALF), (-HALF, -HALF))
    has_back = False

    def xy(self, t, d):
        a = math.pi + math.pi / 2 * t
        return (HALF + (CELL + d) * math.cos(a), HALF + (CELL + d) * math.sin(a))

    def limit(self, t):
        """Largest recess that keeps the face inside the cell: the recess runs radially, and near the tangent
        points that direction leaves the cell (through x = -2 at t = 0, y = -2 at t = 1)."""
        delta = math.pi / 2 * min(t, 1.0 - t)
        return CELL * (1.0 / math.cos(delta) - 1.0)

    def yaw(self, t):
        return math.degrees(math.pi + math.pi / 2 * t) + 180.0

    def inside(self, x, y):
        return math.hypot(x - HALF, y - HALF) >= CELL - 1e-3 and x >= -HALF - 1e-3 and y >= -HALF - 1e-3


FRAMES = {"Straight": StraightFrame(), "CornerOuter": OuterFrame(), "CornerInner": InnerFrame()}
LIP_TOP = 2.945      # top of the lip band; the last 5.5 cm to z = 3.0 is a three-segment rounded edge
LIP_R = STEP - LIP_TOP
LIP_ANGLES = (150.0, 120.0, 90.0)


def envelope(t, margin):
    return smoothstep(min(t, 1.0 - t) / margin)


def relief(frame, t, amax):
    """Relief scale: 0 at both ends of the module, 1 in the middle, never pushing the face out of the cell."""
    return min(envelope(t, frame.margin), 0.92 * frame.limit(t) / amax)


def max_relief(bands):
    return max(b["rec"] * (1.0 + b["rough"]) for b in bands)


def band(z0, z1, rec, mat, rough=0.0, freq=0.0, phase=0.0, wob=0.0):
    return dict(z0=z0, z1=z1, rec=rec, mat=mat, rough=rough, freq=freq, phase=phase, wob=wob)


HUMAN_BANDS = [
    band(0.00, 0.42, 0.00, DARK),                                              # talus skirt, flush with the cell
    band(0.42, 1.00, 0.20, DARK, rough=0.30, freq=3.0, phase=0.4, wob=0.05),  # rough strata
    band(1.00, 1.60, 0.36, DARK),                                              # steel plate band
    band(1.60, 2.10, 0.22, DARK, rough=0.35, freq=2.0, phase=1.9, wob=0.06),
    band(2.10, 2.66, 0.40, DARK),                                              # steel plate band
    band(2.66, 2.72, 0.00, DARK),                                              # lip: overhang, shadow gap,
    band(2.72, 2.84, 0.00, ACCENT),                                            # hazard stripe,
    band(2.84, LIP_TOP, 0.00, SHELL),                                          # steel coping
]
MACHINE_BANDS = [
    band(0.00, 0.30, 0.00, DARK),
    band(0.30, 0.35, 0.00, GLOW),
    band(0.35, 0.98, 0.14, SHELL),
    band(0.98, 1.02, 0.14, GLOW),
    band(1.02, 1.62, 0.28, SHELL),
    band(1.62, 1.66, 0.28, GLOW),
    band(1.66, 2.26, 0.42, SHELL),
    band(2.26, 2.30, 0.42, GLOW),
    band(2.30, 2.66, 0.54, SHELL),
    band(2.66, 2.76, 0.00, SHELL),
    band(2.76, 2.82, 0.00, GLOW),
    band(2.82, LIP_TOP, 0.00, SHELL),
]
LOOK_BANDS = {"human": HUMAN_BANDS, "machine": MACHINE_BANDS}
LEDGE = {"human": (DARK, DARK), "machine": (SHELL, DARK)}    # (facing up, facing down)
TOP = {"human": DARK, "machine": SHELL}
TOP_RINGS = (1.0, 0.85, 0.6, 0.3)


def band_d(b, t, frame, amax):
    w = relief(frame, t, amax)
    return b["rec"] * w * (1.0 + b["rough"] * math.sin(2.0 * math.pi * b["freq"] * t + b["phase"]))


def skin_column(frame, bands, t):
    """(d, z) polyline of the face at parameter t: two points per band, then the top chamfer point."""
    amax = max_relief(bands)
    w = relief(frame, t, amax)
    bounds = [0.0]
    for b in bands[1:]:
        bounds.append(b["z0"] + w * b["wob"] * math.sin(2.0 * math.pi * 1.5 * t + b["phase"] + 1.0))
    bounds.append(bands[-1]["z1"])
    pts = []
    for k, b in enumerate(bands):
        d = band_d(b, t, frame, amax)
        pts += [(d, bounds[k]), (d, bounds[k + 1])]
    for phi in LIP_ANGLES:      # rounded top edge (scaled to nothing at the module ends, so the seam stays flat)
        pts.append((LIP_R * w * (1.0 + math.cos(math.radians(phi))), LIP_TOP + LIP_R * math.sin(math.radians(phi))))
    return pts


def skin_materials(bands, look):
    up, down = LEDGE[look]
    mats = []
    for k, b in enumerate(bands):
        if k:
            mats.append(up if b["rec"] >= bands[k - 1]["rec"] else down)
        mats.append(b["mat"])
    mats += [SHELL] * len(LIP_ANGLES)      # rounded top edge
    return mats


def cliff_body(bm, frame, look):
    bands = LOOK_BANDS[look]
    n = frame.n
    cols = [skin_column(frame, bands, i / n) for i in range(n + 1)]
    mats = skin_materials(bands, look)

    def P(i, k):
        d, z = cols[i][k]
        x, y = frame.xy(i / n, d)
        return (x, y, z)

    for i in range(n):
        for s in range(len(cols[0]) - 1):
            poly(bm, [P(i, s), P(i + 1, s), P(i + 1, s + 1), P(i, s + 1)], mats[s])
    top, bot = TOP[look], DARK
    for level, first, mat, scales in ((STEP, len(cols[0]) - 1, top, TOP_RINGS), (0.0, 0, bot, (1.0,))):
        front = [P(i, first) for i in range(n + 1)]
        ring = [(x, y) for x, y, _ in front] + list(frame.back_loop[:-1])
        ax, ay = frame.back_loop[-1]
        # concentric scaled copies of the outline about the apex keep the top's faces wide (no sliver fan, so no
        # false Edge mask) and let the baked masks fade over a band instead of streaking to the apex
        levels = [[(ax + s * (x - ax), ay + s * (y - ay), level) for x, y in ring] for s in scales]
        for a, b in zip(levels, levels[1:]):
            for i in range(len(a) - 1):
                poly(bm, [a[i], a[i + 1], b[i + 1], b[i]], mat)
        for i in range(len(ring) - 1):
            poly(bm, [(ax, ay, level), levels[-1][i], levels[-1][i + 1]], mat)
    for t_end, inner in ((0.0, frame.interior[0]), (1.0, frame.interior[1])):
        x, y = frame.xy(t_end, 0.0)
        poly(bm, [(x, y, 0.0), (x, y, STEP), (*inner, STEP), (*inner, 0.0)], DARK)
    if frame.has_back:
        poly(bm, [(-HALF, -HALF, 0.0), (-HALF, -HALF, STEP), (-HALF, HALF, STEP), (-HALF, HALF, 0.0)], DARK)


def surface_d(bands, k, t, frame):
    return band_d(bands[k], t, frame, max_relief(bands))


def add_lumps(m, frame, bands, specs):
    """Rock lumps nestled in rough strata bands; each is shrunk so it never passes the flush plane."""
    lo, hi = frame.lumps
    for k, u, z, r, seed in specs:
        t = lo + u * (hi - lo)
        d_s = surface_d(bands, k, t, frame)
        r_eff = min(r, (d_s - 0.02) / 0.72)
        if r_eff < 0.06:
            continue
        x, y = frame.xy(t, d_s + 0.5 * r_eff)
        m.rock(x, y, z - r_eff, (2.2 * r_eff, 2.2 * r_eff, 2.0 * r_eff), seed, split=False, segs=7, rings=4)


def human_details(m, frame):
    bands = HUMAN_BANDS
    length = frame.length
    # steel I-beam at mid-span from the plinth to the lip
    tb = 0.09 / length
    m.sweep(frame, 0.5 - tb, 0.5 + tb, 1, [(0.42, 0.42), (0.06, 0.42), (0.06, 2.66), (0.42, 2.66)], SHELL)
    # steel retaining plates on the two flat bands, two per band, with bolts
    for k, z0, z1 in ((2, 1.06, 1.52), (4, 2.16, 2.60)):
        a = bands[k]["rec"]
        section = [(a + 0.03, z0), (a - 0.04, z0), (a - 0.07, z0 + 0.03), (a - 0.07, z1 - 0.03), (a - 0.04, z1),
                   (a + 0.03, z1)]
        for t0, t1 in frame.plates:
            steps = max(1, round(length * (t1 - t0) / 0.4))
            m.sweep(frame, t0, t1, steps, section, SHELL)
            inset = min(0.30, 0.2 * length * (t1 - t0)) / length
            for t in (t0 + inset, t1 - inset):
                for z in (z0 + 0.13, z1 - 0.13):
                    x, y = frame.xy(t, a - 0.07 - 0.0125)
                    m.cyl((x, y, z), 0.025, 0.08, DARK, axis="x", segs=6, rot=(0, 0, frame.yaw(t)))
    # two flood lamps on the beam (housing + amber lens), staying behind the flush plane
    for z in (1.10, 2.32):
        x, y = frame.xy(0.5, 0.05)
        yaw = frame.yaw(0.5)
        m.box((x, y, z), (0.08, 0.30, 0.20), DARK, rot=(0, 0, yaw), bevel=0.02, seg=1)
        x, y = frame.xy(0.5, 0.012)
        m.box((x, y, z), (0.02, 0.20, 0.12), GLOW, rot=(0, 0, yaw))
    add_lumps(m, frame, bands, [(1, 0.05, 0.70, 0.30, 11), (1, 0.85, 0.66, 0.30, 12), (1, 0.45, 0.55, 0.20, 15),
                                (3, 0.30, 1.86, 0.26, 13), (3, 0.72, 1.90, 0.26, 14), (3, 0.98, 1.80, 0.20, 16)])


def cliff(kind, look):
    frame = FRAMES[kind]
    m = Model("Cliff_%s%s" % (kind, "_Machine" if look == "machine" else ""))
    m.raw(lambda bm: cliff_body(bm, frame, look), None)
    if look == "human":
        human_details(m, frame)
    return m


# --------------------------------------------------------------------------------------
# Plateau fill
# --------------------------------------------------------------------------------------
def rect_bands(bm, z, halves, mats):
    """Concentric coplanar squares: band i lies between halves[i] and halves[i + 1], the last entry fills the middle."""
    def corner(h):
        return [(-h, -h, z), (h, -h, z), (h, h, z), (-h, h, z)]
    for a, b, mat in zip(halves, halves[1:], mats):
        A, B = corner(a), corner(b)
        for k in range(4):
            poly(bm, [A[k], A[(k + 1) % 4], B[(k + 1) % 4], B[k]], mat, outward=(0, 0, 1))
    poly(bm, corner(halves[-1]), mats[-1], outward=(0, 0, 1))


def plateau_fill(look):
    m = Model("Plateau_Fill" + ("_Machine" if look == "machine" else ""))
    side = SHELL if look == "machine" else DARK
    halves, mats = (([2.0, 1.97, 1.15, 1.09], [DARK, SHELL, GLOW, SHELL]) if look == "machine"
                    else ([2.0, 0.95, 0.85], [DARK, SHELL, DARK]))

    def fill(bm):
        rect_bands(bm, STEP, halves, mats)
        h = HALF
        poly(bm, [(-h, -h, 0), (-h, h, 0), (h, h, 0), (h, -h, 0)], DARK, outward=(0, 0, -1))
        for nx, ny in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            u = (-ny, nx)
            poly(bm, [(nx * h + u[0] * h, ny * h + u[1] * h, 0), (nx * h - u[0] * h, ny * h - u[1] * h, 0),
                      (nx * h - u[0] * h, ny * h - u[1] * h, STEP), (nx * h + u[0] * h, ny * h + u[1] * h, STEP)],
                 side, outward=(nx, ny, 0))
    m.raw(fill, None)
    return m


# --------------------------------------------------------------------------------------
# Ramps
# --------------------------------------------------------------------------------------
def deck_z(x):
    return STEP * (RAMP_RUN / 2 - x) / RAMP_RUN


def ramp(width, look):
    machine = look == "machine"
    m = Model("Ramp_%s%s" % ("Wide" if width > 6 else "Narrow", "_Machine" if machine else ""))
    hw, yi = width / 2, width / 2 - PARAPET_T
    x_lo, x_hi = -RAMP_RUN / 2, RAMP_RUN / 2
    slope_deg = math.degrees(math.atan(STEP / RAMP_RUN))

    # -- deck: a single plane split into coplanar colour patches --------------------------------
    rib = 0.03 if machine else 0.08
    xs = [x_lo]
    for k in range(8):
        c = x_lo + 0.5 + k
        xs += [c - rib, c + rib]
    xs.append(x_hi)
    edge = 0.14 if machine else 0.30
    inner = yi - edge
    cuts = max(1, round(2 * inner / 1.1))
    ys = [-yi, -inner] + [-inner + 2 * inner * k / cuts for k in range(1, cuts)] + [inner, yi]
    deck_base, deck_rib, deck_edge = (SHELL, DARK, GLOW) if machine else (DARK, SHELL, ACCENT)
    normal = (STEP / RAMP_RUN, 0.0, 1.0)

    def core(bm):
        for i in range(len(xs) - 1):
            is_rib = i % 2 == 1
            for j in range(len(ys) - 1):
                mat = deck_edge if j in (0, len(ys) - 2) else deck_rib if is_rib else deck_base
                poly(bm, [(xs[i], ys[j], deck_z(xs[i])), (xs[i + 1], ys[j], deck_z(xs[i + 1])),
                          (xs[i + 1], ys[j + 1], deck_z(xs[i + 1])), (xs[i], ys[j + 1], deck_z(xs[i]))], mat,
                     outward=normal)
        poly(bm, [(x_lo, -yi, 0), (x_hi, -yi, 0), (x_hi, yi, 0), (x_lo, yi, 0)], DARK, outward=(0, 0, -1))
        poly(bm, [(x_lo, -yi, 0), (x_lo, yi, 0), (x_lo, yi, STEP), (x_lo, -yi, STEP)], DARK, outward=(-1, 0, 0))
    m.raw(core, None, recalc=False)

    # -- side walls: wedge courses below the deck, parapet and cap above it --------------------------
    wedge = [(x_hi, 0.0), (x_lo, 0.0), (x_lo, STEP)]
    parapet = [(x_hi, 0.0), (x_lo, STEP), (x_lo, STEP + PARAPET_H), (x_hi, PARAPET_H)]
    cap = [(x_hi, PARAPET_H), (x_lo, STEP + PARAPET_H), (x_lo, STEP + PARAPET_H + CAP_H), (x_hi, PARAPET_H + CAP_H)]
    band_z = [(0.30, 0.46), (1.05, 1.21), (1.80, 1.96), (2.55, 2.71)] if not machine else \
        [(0.38, 0.43), (1.08, 1.13), (1.78, 1.83), (2.48, 2.53)]
    strip = [(x_hi, 0.22), (x_lo, STEP + 0.22), (x_lo, STEP + 0.34), (x_hi, 0.34)]
    for s in (-1, 1):
        y_in, y_out = s * yi, s * (hw - 0.03)
        lo, hi = sorted((y_in, y_out))
        m.profile_y(wedge, lo, hi, SHELL if machine else DARK)
        m.profile_y(parapet, lo, hi, SHELL)
        m.profile_y(cap, min(y_in, s * hw), max(y_in, s * hw), DARK if machine else SHELL)
        for z0, z1 in band_z:
            piece = clip_poly(clip_poly(wedge, 1, z0, True), 1, z1, False)
            if len(piece) >= 3 and abs(poly_area(piece)) > 1e-3:
                if machine:
                    a, b = sorted((s * (hw - 0.04), s * (hw - 0.01)))
                    m.profile_y(piece, a, b, GLOW)
                else:
                    a, b = sorted((s * (hw - 0.06), s * hw))
                    m.profile_y(piece, a, b, SHELL)
        a, b = sorted((s * (yi - 0.012), s * (yi + 0.002)))
        m.profile_y(strip, a, b, GLOW if machine else ACCENT)
        # lamps / crystals on the cap
        for x in (-1.5, 2.0):
            z_top = deck_z(x) + PARAPET_H + CAP_H
            y = s * (hw - PARAPET_T / 2)
            if machine:
                m.diamond(x, y, z_top - 0.02, z_top + 0.48, 0.09, GLOW)
            else:
                nrm = Vector((math.sin(math.radians(slope_deg)), 0.0, math.cos(math.radians(slope_deg))))
                m.box(Vector((x, y, z_top)) + nrm * 0.06, (0.30, 0.18, 0.12), GLOW, rot=(0, slope_deg, 0),
                      bevel=0.02, seg=1)
    return m


# --------------------------------------------------------------------------------------
# Destructible rocks
# --------------------------------------------------------------------------------------
def fit_bounds(bm, depth, height):
    """Stretch the finished rock cluster to exactly `depth` (X, centred) and `height` (Z, from 0); Y is already clamped."""
    xs = [v.co.x for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    sx, sz = depth / (max(xs) - min(xs)), height / (max(zs) - min(zs))
    cx, z0 = (max(xs) + min(xs)) / 2, min(zs)
    for v in bm.verts:
        v.co.x = (v.co.x - cx) * sx
        v.co.z = (v.co.z - z0) * sz


def rocks(width, depth, height, seed):
    m = Model("Rocks_Destructible_" + ("Small" if width < 6 else "Large"))
    hw = width / 2
    n = round(width)
    step = width / n

    def h(*a):
        return hn(seed, *a)

    for i in range(n):        # bedrock row: wall to wall, the end boulders are clamped to the +-width/2 planes
        y = -hw + (i + 0.5) * step
        m.rock((h(i, 1) - 0.5) * 0.3 * depth, y, 0.0,
               (depth * (0.85 + 0.25 * h(i, 2)), 1.9 * step, 1.5 + 0.7 * h(i, 3)), seed * 100 + i, clamp_y=hw)
    for sgn in (-1, 1):       # tall end buttresses so the wall is sealed to capsule height right at the side walls
        m.rock(0.0, sgn * (hw - 0.3), 0.0, (depth * 0.9, 1.3, 2.3 + 0.4 * h(sgn + 2, 13)), seed * 100 + 90 + sgn,
               clamp_y=hw)
    for i in range(n - 1):    # second row over the joints
        y = -hw + (i + 1) * step
        m.rock((h(i, 4) - 0.5) * 0.2 * depth, y, 0.9 + 0.3 * h(i, 5),
               (depth * 0.75, 1.7 * step, 1.2 + 0.5 * h(i, 6)), seed * 100 + 30 + i, clamp_y=hw)
    for i in range(0, n - 1, 2):   # crown
        y = -hw + (i + 1.5) * step
        m.rock(-0.05 * depth, min(hw - 0.4, y), 1.7 + 0.35 * h(i, 7),
               (depth * 0.55, 1.4 * step, 1.0 + 0.4 * h(i, 8)), seed * 100 + 60 + i, clamp_y=hw)
    for i in range(n):        # front rubble
        y = -hw + (i + 0.3 + 0.5 * h(i, 9)) * step
        m.rock(depth * 0.42, y, 0.0, (0.9 + 0.3 * h(i, 10), 0.8 + 0.3 * h(i, 11), 0.6 + 0.4 * h(i, 12)),
               seed * 100 + 80 + i, clamp_y=hw)
    fit_bounds(m.bm, depth, height)
    # glowing fault cracks on the +X face (the mark of a destructible rock)
    m.bm.normal_update()
    bvh = BVHTree.FromBMesh(m.bm)
    for c in range(max(3, n * 3 // 4)):
        y0 = -hw + 0.7 + (width - 1.4) * h(c, 20)
        z0 = 0.35 + 0.7 * h(c, 21)
        pts = []
        for k in range(8):
            y = y0 + 0.16 * k * (1 if h(c, 22) > 0.5 else -1) + (0.12 if k % 2 else -0.12)
            z = z0 + 0.19 * k
            hit = bvh.ray_cast(Vector((depth, y, z)), Vector((-1, 0, 0)), depth * 2)
            if hit[0] is None:
                break
            pts.append((hit[0], hit[1]))
        if len(pts) < 4:
            continue

        def ribbon(bm, pts=pts):
            left, right = [], []
            for k, (p, nrm) in enumerate(pts):
                t = (pts[min(k + 1, len(pts) - 1)][0] - pts[max(k - 1, 0)][0]).normalized()
                side = nrm.cross(t).normalized()
                w = 0.035 * (0.35 + 0.65 * math.sin(math.pi * (k + 0.5) / len(pts)))
                left.append(p + nrm * 0.02 + side * w)
                right.append(p + nrm * 0.02 - side * w)
            for k in range(len(pts) - 1):
                poly(bm, [left[k], right[k], right[k + 1], left[k + 1]], GLOW, outward=pts[k][1])
        m.raw(ribbon, GLOW, recalc=False)
    return m


# --------------------------------------------------------------------------------------
# Watchtower ("uplink tower"): a Machine relay pylon on a round pad
# --------------------------------------------------------------------------------------
def watchtower():
    m = Model("Watchtower")
    m.lathe([(0.0, 0.0), (2.0, 0.0), (2.0, 0.10), (1.90, 0.20), (0.0, 0.20)], 48, DARK)      # pad, 4.0 m diameter
    m.lathe([(1.20, 0.20), (1.78, 0.20), (1.78, 0.215), (1.20, 0.215)], 48, SHELL, closed=True)
    m.lathe([(1.80, 0.20), (1.86, 0.20), (1.86, 0.215), (1.80, 0.215)], 48, GLOW, closed=True)   # vision ring
    m.lathe([(0.0, 0.20), (1.05, 0.20), (1.05, 0.35), (0.90, 0.50), (0.0, 0.50)], 32, DARK)     # column plinth
    m.cyl((0, 0, 3.8), 6.6, 0.36, DARK, segs=12)                                             # core rod

    def drum(z0, z1, r0, r1):
        m.lathe([(0.0, z0), (r0 * 0.88, z0), (r0, z0 + 0.09), (r1, z1 - 0.09), (r1 * 0.88, z1), (0.0, z1)], 32, SHELL)
    for z0, z1, r0, r1, halo in ((0.60, 2.50, 0.74, 0.62, 0.56), (2.70, 4.40, 0.56, 0.47, 0.42),
                                 (4.60, 5.90, 0.40, 0.33, 0.36)):
        drum(z0, z1, r0, r1)
        m.tor((0, 0, z1 + 0.10), halo, 0.035, GLOW, segs=32, minor_segs=6)
    for k in range(3):        # three buttress fins with a cyan seam
        a = math.radians(90 + 120 * k)
        u, tang = (math.cos(a), math.sin(a), 0.0), (-math.sin(a), math.cos(a), 0.0)
        fin = [(0.5, 0.5), (1.55, 0.5), (1.45, 0.68), (0.95, 1.6), (0.7, 3.4), (0.55, 3.6)]
        lens = [(1.38, 0.75), (1.30, 0.75), (0.62, 3.3), (0.68, 3.3)]
        for pts, depth, mat in ((fin, 0.16, SHELL), (lens, 0.20, GLOW)):
            origin = Vector((0, 0, 0)) - Vector(tang) * depth / 2
            m.plane_prism(pts, depth, mat, origin, u, (0, 0, 1), tang)
    m.lathe([(0.0, 6.30), (0.6, 6.30), (0.6, 6.35), (0.0, 6.35)], 24, DARK)                     # relay platform
    m.lathe([(0.0, 6.35), (0.85, 6.35), (0.95, 6.42), (0.85, 6.50), (0.0, 6.50)], 40, SHELL)
    m.tor((0, 0, 6.42), 0.93, 0.03, GLOW, segs=40, minor_segs=6)
    m.sph((0, 0, 7.15), (0.48, 0.48, 0.48), ACCENT, segs=20)                                    # red lens
    m.tor((0, 0, 7.15), 0.36, 0.025, GLOW, segs=28, minor_segs=6, rot=(70, 0, 20))
    for sx in (-1, 1):        # crystal cage: two crossing blade pairs
        blade = [(sx * 0.30, 6.50), (sx * 0.55, 6.90), (sx * 0.55, 7.90), (sx * 0.47, 7.75), (sx * 0.47, 6.95),
                 (sx * 0.25, 6.55)]
        m.profile_y([(y, z) for y, z in blade], -0.03, 0.03, SHELL)
        m.profile_x(blade, -0.03, 0.03, SHELL)
    m.diamond(0, 0, 7.75, 8.35, 0.13, GLOW)
    m.lathe([(0.0, 8.30), (0.07, 8.40), (0.03, 8.70), (0.0, 9.0)], 8, SHELL)
    return m


BUILDERS = [
    ("Cliff_Straight", lambda: cliff("Straight", "human")),
    ("Cliff_CornerOuter", lambda: cliff("CornerOuter", "human")),
    ("Cliff_CornerInner", lambda: cliff("CornerInner", "human")),
    ("Cliff_Straight_Machine", lambda: cliff("Straight", "machine")),
    ("Cliff_CornerOuter_Machine", lambda: cliff("CornerOuter", "machine")),
    ("Cliff_CornerInner_Machine", lambda: cliff("CornerInner", "machine")),
    ("Ramp_Wide", lambda: ramp(8.0, "human")),
    ("Ramp_Narrow", lambda: ramp(4.0, "human")),
    ("Ramp_Wide_Machine", lambda: ramp(8.0, "machine")),
    ("Ramp_Narrow_Machine", lambda: ramp(4.0, "machine")),
    ("Plateau_Fill", lambda: plateau_fill("human")),
    ("Plateau_Fill_Machine", lambda: plateau_fill("machine")),
    ("Rocks_Destructible_Small", lambda: rocks(4.0, 2.4, 2.6, 3)),
    ("Rocks_Destructible_Large", lambda: rocks(8.0, 3.0, 3.4, 5)),
    ("Watchtower", watchtower),
]


# --------------------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------------------
def bounds(obj):
    pts = [v.co for v in obj.data.vertices]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def signed_volume(obj):
    return sum(p.area * p.center.dot(p.normal) for p in obj.data.polygons) / 3.0


def check(name, obj):
    fx, fy, height, slots, _ = SPEC[name]
    lo, hi = bounds(obj)
    got = [s.material.name for s in obj.material_slots]
    assert got == slots.split(), "%s slots %s, expected %s" % (name, got, slots)
    used = {got[p.material_index] for p in obj.data.polygons}
    assert used == set(got), "%s has unused slots" % name
    assert triangles(obj) < TRI_LIMIT, "%s has %d triangles" % (name, triangles(obj))
    tol = 1e-3
    assert abs((hi.x - lo.x) - fx) < tol and abs((hi.y - lo.y) - fy) < tol, \
        "%s footprint %.4f x %.4f, expected %.3f x %.3f" % (name, hi.x - lo.x, hi.y - lo.y, fx, fy)
    assert abs(hi.x + lo.x) < tol and abs(hi.y + lo.y) < tol, "%s is not centred: %s %s" % (name, lo, hi)
    assert abs(lo.z) < tol, "%s does not sit on z = 0: %s" % (name, lo)
    assert abs(hi.z - height) < tol, "%s height %.4f, expected %.3f" % (name, hi.z, height)
    assert signed_volume(obj) > 0, "%s has inside-out faces (signed volume %.3f)" % (name, signed_volume(obj))
    if name.startswith("Cliff_"):
        frame = FRAMES[name.split("_")[1]]
        for v in obj.data.vertices:
            assert frame.inside(v.co.x, v.co.y), "%s leaves its cell envelope at %s" % (name, tuple(v.co))


def plane_set(obj, axis, value, keys, tol=2e-5):
    """Vertices on the plane co[axis] == value as a set of 1 mm-rounded (u, v) pairs."""
    return {tuple(round(obj.data.vertices[i].co[k] * 1000) for k in keys)
            for i in range(len(obj.data.vertices)) if abs(obj.data.vertices[i].co[axis] - value) < tol}


def check_seams(objs):
    """Cliff modules must meet with identical flat cross-sections (1 mm); fill and ramps must line up with them."""
    def cap_profile(obj, axis, value, keys):
        """Flat cap outline at a module end: the sorted heights (mm) of the vertices on the face plane u = +200 cm;
        the two back corners must exist too."""
        pts = plane_set(obj, axis, value, keys)
        assert {(-2000, 0), (-2000, 3000)} <= pts, "%s: cap at %s = %s is missing its back corners" % (obj.name, axis, value)
        return sorted(z for u, z in pts if u == 2000)

    for suffix in ("", "_Machine"):
        straight = objs["Cliff_Straight" + suffix]
        ref = cap_profile(straight, 1, -HALF, (0, 2))
        assert ref[0] == 0 and ref[-1] == round(STEP * 1000), "face plane must span 0..300 cm"
        assert ref == cap_profile(straight, 1, HALF, (0, 2)), "Straight ends differ (%s)" % suffix
        corner = objs["Cliff_CornerOuter" + suffix]
        assert ref == cap_profile(corner, 1, -HALF, (0, 2)), "CornerOuter t=0 end != Straight end"
        assert ref == cap_profile(corner, 0, -HALF, (1, 2)), "CornerOuter t=1 end != Straight end"
        inner = objs["Cliff_CornerInner" + suffix]
        assert ref == cap_profile(inner, 0, -HALF, (1, 2)), "CornerInner t=0 end != Straight end"
        assert ref == cap_profile(inner, 1, -HALF, (0, 2)), "CornerInner t=1 end != Straight end"
        back = plane_set(straight, 0, -HALF, (1, 2))
        fill = objs["Plateau_Fill" + suffix]
        corners = {(-2000, 0), (2000, 0), (-2000, 3000), (2000, 3000)}
        assert corners <= back and corners == plane_set(fill, 0, HALF, (1, 2)) == plane_set(fill, 0, -HALF, (1, 2)), \
            "Straight back face and Plateau_Fill sides do not match"
        assert corners == plane_set(fill, 1, HALF, (0, 2)) == plane_set(fill, 1, -HALF, (0, 2)), "fill sides"
        top_z = max(v.co.z for v in fill.data.vertices)
        assert abs(top_z - STEP) < 1e-3
        for name in ("Cliff_Straight", "Cliff_CornerOuter", "Cliff_CornerInner"):
            assert abs(max(v.co.z for v in objs[name + suffix].data.vertices) - STEP) < 1e-3, name
    # ramps: deck plane, foot and top edge, walkable strip
    slope = math.degrees(math.atan(STEP / RAMP_RUN))
    for name in ("Ramp_Wide", "Ramp_Narrow", "Ramp_Wide_Machine", "Ramp_Narrow_Machine"):
        obj = objs[name]
        hw = SPEC[name][1] / 2
        yi = hw - PARAPET_T
        deck = [v.co for v in obj.data.vertices if abs(v.co.y) <= yi + 1e-6 and abs(v.co.z - deck_z(v.co.x)) < 1e-4]
        assert deck, name
        assert abs(min(p.z for p in deck)) < 1e-4 and abs(max(p.z for p in deck) - STEP) < 1e-4, "%s deck ends" % name
        foot = [v.co for v in obj.data.vertices if v.co.x > RAMP_RUN / 2 - 1e-6 and abs(v.co.y) < yi - 0.02]
        assert foot and all(abs(p.z) < 1e-6 for p in foot), "%s: the foot must be flush with the ground" % name
        top = [v.co for v in obj.data.vertices if v.co.x < -RAMP_RUN / 2 + 1e-6 and abs(v.co.y) <= yi + 1e-6
               and abs(v.co.z - STEP) < 1e-6]   # the deck's top edge: at least its two corners
        assert {round(p.y * 1000) for p in top} >= {round(-yi * 1000), round(yi * 1000)}, "%s top edge" % name
        assert slope <= 30.0, "%s slope %.2f" % (name, slope)
        for v in obj.data.vertices:    # nothing above the walkable strip: a plane, no lips, full headroom
            if abs(v.co.y) < yi - 0.02:
                assert v.co.z <= deck_z(v.co.x) + 1e-4, "%s has geometry above the deck at %s" % (name, tuple(v.co))
        assert 2 * yi >= 4 * CAPSULE_RADIUS, "%s deck too narrow for the capsule" % name
        # the deck's face normals all equal the plane normal (exact single plane)
        n = Vector((STEP / RAMP_RUN, 0, 1)).normalized()
        for p in obj.data.polygons:
            if all(abs(obj.data.vertices[i].co.z - deck_z(obj.data.vertices[i].co.x)) < 1e-5
                   and abs(obj.data.vertices[i].co.y) <= yi + 1e-6 for i in p.vertices):
                assert p.normal.dot(n) > 0.99999, "%s deck face not on the plane" % name
    # rocks seal the gap: rays along X through the full width at capsule heights hit the rocks
    for name in ("Rocks_Destructible_Small", "Rocks_Destructible_Large"):
        obj = objs[name]
        bvh = BVHTree.FromPolygons([tuple(v.co) for v in obj.data.vertices], [tuple(p.vertices) for p in obj.data.polygons])
        w = SPEC[name][1]
        y = -w / 2 + 0.005
        while y < w / 2:
            for z in (0.15, 0.4, 0.7, 1.0, 1.3, 1.6):
                assert bvh.ray_cast(Vector((5.0, y, z)), Vector((-1, 0, 0)))[0] is not None, \
                    "%s leaves a gap at y=%.3f z=%.2f" % (name, y, z)
            y += 0.05


def place_bounds(name, obj, x, y, z, yaw):
    lo, hi = bounds(obj)
    c, s = round(math.cos(math.radians(yaw))), round(math.sin(math.radians(yaw)))
    pts = [(c * px - s * py + x, s * px + c * py + y) for px in (lo.x, hi.x) for py in (lo.y, hi.y)]
    return (min(p[0] for p in pts), min(p[1] for p in pts), z + lo.z), (max(p[0] for p in pts), max(p[1] for p in pts), z + hi.z)


def check_assembly(objs, layout):
    """Grid alignment and no overlaps for the preview assembly (3D boxes of the placed pieces)."""
    boxes = []
    for name, x, y, z, yaw in layout:
        assert yaw % 90 == 0, (name, yaw)
        lo, hi = place_bounds(name, objs[name], x, y, z, yaw)
        # every piece edge sits on the 200 cm sub-grid of the 400 cm module (rocks: depth is free in X or Y)
        if not name.startswith("Rocks"):
            for value in (lo[0], hi[0], lo[1], hi[1]):
                assert abs(value / HALF - round(value / HALF)) < 1e-3, "%s %s off the 200 cm grid" % (name, (x, y))
        else:
            width_axis = (lo[1], hi[1]) if yaw % 180 == 0 else (lo[0], hi[0])
            for value in width_axis:
                assert abs(value / HALF - round(value / HALF)) < 1e-3, "%s width edge off the grid" % name
        assert abs(z / STEP - round(z / STEP)) < 1e-9, "%s level %s not a multiple of the 300 cm step" % (name, z)
        boxes.append((name, lo, hi))
    for i, (na, la, ha) in enumerate(boxes):
        for nb, lb, hb in boxes[i + 1:]:
            overlap = [min(ha[k], hb[k]) - max(la[k], lb[k]) for k in range(3)]
            assert not all(o > 1e-3 for o in overlap), "%s overlaps %s" % (na, nb)


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
    """Re-import the FBX into a fresh scene; its SC2Mask colours must equal the baked masks (linear, 1/255)."""
    attr = MasterMaterials.MASK_ATTR
    scene = bpy.data.scenes.new("VerifyMasks")
    before = set(bpy.data.objects)
    with bpy.context.temp_override(scene=scene, view_layer=scene.view_layers[0]):
        bpy.ops.import_scene.fbx(filepath=path, colors_type="LINEAR")
    imported = [o for o in bpy.data.objects if o not in before]
    assert len(imported) == 1 and imported[0].type == "MESH", "%s: re-import gave %s" % (obj.name, imported)
    mesh = imported[0].data
    assert attr in mesh.color_attributes, "%s: no %s in FBX" % (obj.name, attr)
    layer = mesh.color_attributes[attr]
    assert len(mesh.color_attributes) == 1, "%s: extra colour attributes" % obj.name
    tree = kdtree.KDTree(len(obj.data.vertices))
    for v in obj.data.vertices:
        tree.insert(v.co, v.index)
    tree.balance()
    corner = layer.domain == "CORNER"
    assert corner or layer.domain == "POINT", "%s: colour domain %s" % (obj.name, layer.domain)
    worst, mid = 0.0, 0
    seen = set()
    for i, item in enumerate(layer.data):
        vertex = mesh.loops[i].vertex_index if corner else i
        near = [index for _, index, _ in tree.find_range(mesh.vertices[vertex].co, 1e-3)]
        assert near, "%s: re-imported vertex %d has no source vertex within 1 mm" % (obj.name, vertex)
        seen.update(near)
        c = item.color
        assert abs(c[3] - 1.0) < 1.0 / 255.0, "%s: alpha %.4f" % (obj.name, c[3])
        index = min(near, key=lambda k: max(abs(c[ch] - masks[k][ch]) for ch in range(3)))
        for channel in range(3):
            want = masks[index][channel]
            worst = max(worst, abs(c[channel] - want))
            mid += 1 if 0.05 < want < 0.95 else 0
    assert worst <= 1.0 / 255.0, "%s: FBX mask differs from the bake by %.4f (> 1/255)" % (obj.name, worst)
    assert len(seen) == len(obj.data.vertices), "%s: mask covers %d of %d vertices" % (
        obj.name, len(seen), len(obj.data.vertices))
    bpy.data.objects.remove(imported[0])
    bpy.data.meshes.remove(mesh)
    bpy.data.scenes.remove(scene)
    return worst, mid


# --------------------------------------------------------------------------------------
# Display copies (SC2 master materials), units for scale, the preview assembly and renders
# --------------------------------------------------------------------------------------
def display_copy(obj, name, look, variant_override=None):
    copy = obj.copy()
    copy.data = obj.data.copy()
    copy.name = name
    faction, variant = variant_override or DISPLAY[look]
    MasterMaterials.apply(copy, faction, "env", variant)
    return copy


def load_units():
    with bpy.data.libraries.load(os.path.join(ROOT, "Art", "Units", "Units.blend"), link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n in ("SM_Human_Frontline", "SM_Machine_Frontline", "SM_Human_Ranged")]
    units = {}
    for o in dst.objects:
        o.data = o.data.copy()
        MasterMaterials.bake_masks(o, "unit")
        MasterMaterials.apply(o, "Machine" if "_Machine_" in o.name else "Human", "unit")
        o.hide_render = True
        units[o.name] = o
    return units


def make_capsule():
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


def assembly_layout():
    """The preview plateau: 4 x 6 cells minus a 2 x 2 notch at the top-left, made only from kit pieces.
    Cell (i, j) is centred at (4 i, 4 j). Returns (piece, x, y, z, yaw)."""
    def c(i, j):
        return (CELL * i, CELL * j)
    out = []
    corners = {(0, 0): 180, (3, 0): 270, (0, 3): 90, (2, 5): 90, (3, 5): 0}
    straights = {(1, 0): 270, (2, 0): 270, (0, 1): 180, (0, 2): 180, (3, 3): 0, (3, 4): 0}
    fills = [(1, 1), (2, 1), (1, 2), (2, 2), (1, 3), (2, 3), (2, 4), (3, 1), (3, 2)]
    for (i, j), yaw in corners.items():
        out.append(("Cliff_CornerOuter", *c(i, j), 0.0, yaw))
    for (i, j), yaw in straights.items():
        out.append(("Cliff_Straight", *c(i, j), 0.0, yaw))
    out.append(("Cliff_CornerInner", *c(1, 4), 0.0, 90))
    for i, j in fills:
        out.append(("Plateau_Fill", *c(i, j), 0.0, 0))
    out.append(("Ramp_Wide", 3 * CELL + HALF + RAMP_RUN / 2, 6.0, 0.0, 0))
    out.append(("Watchtower", *c(2, 2), STEP, 0))
    out.append(("Rocks_Destructible_Large", 0.0, 18.0, 0.0, 0))
    out.append(("Rocks_Destructible_Small", 20.0, 14.0, 0.0, 0))
    return out


CAMPUS_LOW = (0.052, 0.058, 0.066)
SIDE_ROWS = (
    (("Cliff_Straight", 270), ("Cliff_CornerOuter", 270), ("Cliff_CornerInner", 270), ("Plateau_Fill", 0),
     ("Ramp_Wide", 0), ("Ramp_Narrow", 0)),
    (("Cliff_Straight_Machine", 270), ("Cliff_CornerOuter_Machine", 270), ("Cliff_CornerInner_Machine", 270),
     ("Plateau_Fill_Machine", 0), ("Ramp_Wide_Machine", 0), ("Ramp_Narrow_Machine", 0)),
    (("Rocks_Destructible_Small", 90), ("Rocks_Destructible_Large", 90), ("Watchtower", 0)),
)
SIDE_ROW_Z = (0.0, -6.0, -17.0)
RTS_ROW_Y = (20.0, 5.0, -10.0)


class PreviewRig:
    def __init__(self, display, units):
        self.display, self.units = display, units
        scene = self.scene = bpy.context.scene
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
        for name, direction, energy, color in (("PV_Key", (0.55, 0.65, -0.45), 2.8, (1.0, 0.93, 0.86)),
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
        self.floor_mat = make_material("PV_Ground", CAMPUS_LOW, roughness=0.9)

    def clear(self):
        for obj in self.props:
            bpy.data.objects.remove(obj, do_unlink=True)
        self.props = []

    def ground(self, center, size, z=0.0):
        obj = bpy.data.objects.new("PV_Ground", self.ground_mesh)
        self.scene.collection.objects.link(obj)
        obj.scale = (size[0], size[1], 0.1)
        obj.location = (center[0], center[1], z - 0.05)
        obj.data.materials.clear()
        obj.data.materials.append(self.floor_mat)
        self.props.append(obj)

    def piece(self, name, location, yaw=0.0):
        src = self.display[name]
        src.hide_render = True
        obj = src.copy()
        self.scene.collection.objects.link(obj)
        obj.hide_render = False
        obj.location = location
        obj.rotation_euler = (0.0, 0.0, math.radians(yaw))
        self.props.append(obj)
        return obj

    def unit(self, name, location, yaw):
        obj = self.units[name].copy()
        self.scene.collection.objects.link(obj)
        obj.hide_render = False
        obj.location = location
        obj.rotation_euler = (0.0, 0.0, math.radians(yaw))
        self.props.append(obj)

    def person(self, x, y, z=0.0):
        obj = self.capsule.copy()
        self.scene.collection.objects.link(obj)
        obj.hide_render = False
        obj.location = (x, y, z)
        self.props.append(obj)

    def lineup(self, view):
        gap = 1.7
        extent = 0.0
        for row, items in enumerate(SIDE_ROWS):
            x = 0.0
            for name, yaw in items:
                fx, fy = SPEC[name][0], SPEC[name][1]
                width = fy if yaw % 180 == 90 else fx
                depth = fx if yaw % 180 == 90 else fy
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
        self.cam.location = (extent / 2 - 0.85, -60.0, -6.5)
        self.cam.rotation_euler = (math.radians(90), 0, 0)
        self.scene.render.resolution_x, self.scene.render.resolution_y = 2600, 1500

    def camera_rts(self, target, distance, azimuth=20.0, lens=35, size=(3000, 1800)):
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

    def assembly(self, layout):
        for name, x, y, z, yaw in layout:
            self.piece(name, (x, y, z), yaw)
        # two units on the ramp and at its foot, one beside the tower on the plateau
        self.unit("SM_Human_Frontline", (18.0, 3.4, deck_z(0.0)), 180)
        self.unit("SM_Machine_Frontline", (23.6, 7.2, 0.0), 180)
        self.unit("SM_Human_Ranged", (23.6, 4.8, 0.0), 180)
        self.unit("SM_Human_Frontline", (11.0, 4.0, STEP), 180)


def render_previews(display, units, layout):
    rig = PreviewRig(display, units)
    extent = rig.lineup("side")
    cx = extent / 2 - 0.85
    for z in SIDE_ROW_Z:
        rig.ground((cx, 0.0), (extent + 6, 12.0), z)
    rig.camera_side(extent)
    rig.render(os.path.join(OUT, "Preview.png"))
    rig.clear()

    extent = rig.lineup("rts")
    rig.ground((cx, 0.0), (400, 400))
    rig.camera_rts((cx, 5.0, 2.0), 66.0, azimuth=15.0)
    rig.render(os.path.join(OUT, "PreviewRTS.png"))
    rig.clear()

    rig.assembly(layout)
    rig.ground((10.0, 10.0), (400, 400))
    rig.camera_rts((12.5, 7.5, 1.8), 42.0, azimuth=48.0, lens=32, size=(2800, 1600))
    rig.render(os.path.join(OUT, "PreviewAssembly.png"))
    rig.clear()
    return rig


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = canonical_materials()

    objects = {}
    for name, build in BUILDERS:
        obj = build().finish(mats)
        check(name, obj)
        objects[name] = obj
    check_seams(objects)
    layout = assembly_layout()
    check_assembly(objects, layout)

    masks = {}
    for name, obj in objects.items():
        masks[name] = MasterMaterials.bake_masks(obj, MasterMaterials.scope_of(obj.name, "env"))
        export_fbx(obj, os.path.join(OUT, obj.name + ".fbx"))
    print("MASK VERIFY | mesh | max |FBX - baked| | mid-range samples")
    total_mid = 0
    for name, obj in objects.items():
        worst, mid = verify_masks(obj, masks[name], os.path.join(OUT, obj.name + ".fbx"))
        total_mid += mid
        print("MASK VERIFY | %s | %.5f | %d" % (obj.name, worst, mid))
    assert total_mid > 0, "no mid-range mask values: the sRGB-versus-linear check proves nothing"

    # display copies with the SC2 master materials, spread along +X for the .blend
    display = {}
    kit = bpy.data.collections.new("Kit")
    bpy.context.scene.collection.children.link(kit)
    cursor = 0.0
    for name, obj in objects.items():
        look = SPEC[name][4]
        override = ("Human", "") if name.startswith("Ramp_") and look == "human" else None   # steel ramps: plain Human
        copy = display_copy(obj, "display_" + name, look, override)
        lo, hi = bounds(obj)
        copy.location = (cursor + (hi.x - lo.x) / 2, -40.0, 0.0)
        cursor += (hi.x - lo.x) + 2.0
        kit.objects.link(copy)
        display[name] = copy
    for obj in objects.values():
        bpy.data.objects.remove(obj, do_unlink=True)
    for name, obj in display.items():
        obj.name = PREFIX + name
        obj.data.name = PREFIX + name
    units = load_units()

    print("MESH | size X x Y (m) | height (m) | tris | slots")
    for name, obj in display.items():
        lo, hi = bounds(obj)
        print("%s | %.2f x %.2f | %.2f | %d | %s" % (
            obj.name, hi.x - lo.x, hi.y - lo.y, hi.z, triangles(obj), "/".join(s.material.name for s in obj.material_slots)))
    sys.stdout.flush()

    blend = os.path.join(OUT, "Terrain.blend")
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    if SKIP_RENDER:
        print("TERRAIN_KIT_DONE (renders skipped)")
        return

    for obj in display.values():
        obj.location = (0.0, 0.0, 0.0)
    rig = render_previews(display, units, layout)

    # leave the assembly in the .blend: spread kit row at y = -40, assembly at the origin, camera on the assembly
    for obj in display.values():
        obj.hide_render = False
    cursor = 0.0
    for name, obj in display.items():
        lo, hi = bounds(obj)
        obj.location = (cursor + (hi.x - lo.x) / 2, -40.0, 0.0)
        cursor += (hi.x - lo.x) + 2.0
    assembled = bpy.data.collections.new("Assembly")
    bpy.context.scene.collection.children.link(assembled)
    rig.assembly(layout)
    rig.ground((10.0, 10.0), (400, 400))
    for obj in rig.props:
        for coll in list(obj.users_collection):
            coll.objects.unlink(obj)
        assembled.objects.link(obj)
    rig.capsule.hide_render = True
    rig.camera_rts((12.5, 7.5, 1.8), 42.0, azimuth=48.0, lens=32, size=(2800, 1600))
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    print("TERRAIN_KIT_DONE")


main()
