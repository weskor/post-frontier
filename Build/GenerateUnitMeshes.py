"""Generate the unit and HQ meshes for CoopRTS in Blender.

Run headless from the repo root (Blender 5.2.1, no assets, no network, deterministic):

    blender -b --factory-startup -P Build/GenerateUnitMeshes.py

Every mesh is built from bmesh primitives plus Bevel modifiers (applied) and joined into one
object. Units are metres, +Z up, forward = +X. Ground units are centred on a capsule of radius
0.34 m and half-height 0.60 m (ground contact at z = -0.60). HQs are 3.0 x 3.0 x 2.0 m boxes
centred on the origin (base at z = -1.0). Budgets, asserted in check(): at most 12000 triangles per
unit and 30000 per HQ; unit footprints stay inside +/-0.47 m (Siege +/-0.67 m) in X and Y. Detail
comes from Frame / plate / seams / groove / vent / cable helpers on Model (panel breaks, bevelled
plates, rivets, Glow seam grooves, hoses), not from noise.

Outputs (relative to the repo root)
    Art/Units/SM_<Name>.fbx   one mesh object named SM_<Name> per file, exported at the origin
    Art/Units/Units.blend     the same eight meshes spread along +X for editing (NOT at the origin;
                              always export from this script, never from the .blend)
    Art/Units/Preview.png     side view lineup (orthographic, camera looks along +Y, +X is right)
    Art/Units/PreviewRTS.png  the same lineup from a 50 degree pitch RTS camera (perspective, 25
                              degrees azimuth so unit fronts (+X) turn toward the camera)
    Art/Units/PreviewField.png  RTS-distance check: all six units (no HQs) on the Campus Zero asphalt
                              with a scrapyard patch, low warm key + cool fill, about 40 px per unit
                              at 2000 px wide. Human and Machine should be equally easy to spot here.

Preview lineup order. Columns left to right: Frontline, Ranged, Siege, HQ. Machine row on top
(side view) or far (RTS view); Human row below (side view) or near (RTS view):
    Machine: SM_Machine_Frontline, SM_Machine_Ranged, SM_Machine_Siege, SM_Machine_HQ
    Human:   SM_Human_Frontline,   SM_Human_Ranged,   SM_Human_Siege,   SM_Human_HQ

Material slots, in this order on every mesh (all four are used by every mesh):
    0 Team   player colour accent (Machine: lens eye plus a top-mounted status lamp or strips; Human:
             painted stripe, shield band, shoulder plates, lid, flag, roof and parapet paint)
    1 Shell  faction body
    2 Dark   joints, weapons, underside
    3 Glow   small emissive accents
The materials carry neutral colours; the preview renders swap in per-faction colours per object
(Machine: red lens / white shell / cyan glow; Human: blue team paint / khaki shell / amber glow).
Human surface values are the HUMAN_* constants below and must equal FACTION_MATERIALS["Human"] in
Build/ImportUnitMeshes.py.

FBX export axis settings (bpy.ops.export_scene.fbx). Blender's native forward='Y', up='Z' is an
identity transform (vertices keep their Blender XYZ; +X forward stays +X) and writes the FBX
header UpAxis=+Z, FrontAxis=-Y, CoordAxis=+X, right handed. That is exactly the axis system
Unreal's importer converts to by default (FbxMainImport.cpp ConvertScene, bConvertScene on,
"Force Front X Axis" off), so Unreal applies no rotation, only its usual Y flip for handedness
(Blender +X -> Unreal +X, Blender +Y -> Unreal -Y, Z up). Do NOT use forward='-Y' or 'X':
those rotate the mesh relative to Unreal's expected axes.
    axis_forward='Y', axis_up='Z', global_scale=1.0, apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, use_selection=True,
    use_mesh_modifiers=True, mesh_smooth_type='OFF' (normals only), use_triangles=True,
    add_leaf_bones=False, bake_anim=False.
The file is written in centimetres (UnitScaleFactor 1, vertices x100), so 1 Blender m = 100 UU.
Unreal import: Import Uniform Scale 1, Convert Scene on (default), Force Front X Axis off.
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
OUT = os.path.join(ROOT, "Art", "Units")

TEAM, SHELL, DARK, GLOW = range(4)
SLOT_NAMES = ("Team", "Shell", "Dark", "Glow")
FBX_AXES = dict(axis_forward="Y", axis_up="Z")
TRI_BUDGET_UNIT = 12000   # triangles per unit mesh
TRI_BUDGET_HQ = 30000     # triangles per HQ mesh
FOOTPRINT_UNIT = 0.47     # max |x| or |y| in metres for units (the fixed footprint, +/-0.45 m plus bevels)
FOOTPRINT_SIEGE = 0.67    # same for the two Siege units


# --------------------------------------------------------------------------------------
# Material helpers
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
        make_material("Team", (0.85, 0.08, 0.06), emission=1.0),
        make_material("Shell", (0.75, 0.77, 0.80), roughness=0.4),
        make_material("Dark", (0.05, 0.055, 0.06), metallic=0.5, roughness=0.5),
        make_material("Glow", (0.4, 0.9, 1.0), emission=3.0),
    ]


# --------------------------------------------------------------------------------------
# Geometry builder
# --------------------------------------------------------------------------------------
def _torus(bm, major, minor, major_segments=28, minor_segments=8):
    verts = []
    for i in range(major_segments):
        u = 2.0 * math.pi * i / major_segments
        for j in range(minor_segments):
            v = 2.0 * math.pi * j / minor_segments
            r = major + minor * math.cos(v)
            verts.append(bm.verts.new((r * math.cos(u), r * math.sin(u), minor * math.sin(v))))
    for i in range(major_segments):
        for j in range(minor_segments):
            a = verts[i * minor_segments + j]
            b = verts[((i + 1) % major_segments) * minor_segments + j]
            c = verts[((i + 1) % major_segments) * minor_segments + (j + 1) % minor_segments]
            d = verts[i * minor_segments + (j + 1) % minor_segments]
            bm.faces.new((a, b, c, d))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)


class Model:
    """Accumulates primitive parts (each its own object so Bevel modifiers can be applied), then joins."""

    def __init__(self, name, materials):
        self.name = name
        self.materials = materials
        self.parts = []

    # -- core -----------------------------------------------------------------------
    def add(self, kind, loc, dims, mat, rot=(0, 0, 0), aim=None, bevel=0.0, seg=2,
            mirror=False, verts=16, taper=1.0):
        for flip in ((False, True) if mirror else (False,)):
            self._make(kind, loc, dims, mat, rot, aim, bevel, seg, flip, verts, taper)

    def _make(self, kind, loc, dims, mat, rot, aim, bevel, seg, flip, verts, taper):
        loc = Vector(loc)
        if aim is not None:
            a = Vector(aim).normalized()
            up = "Z" if abs(a.z) < 0.99 else "Y"
            rmat = a.to_track_quat("X", up).to_matrix()
        elif isinstance(rot, Matrix):
            rmat = rot.to_3x3()
        else:
            rmat = Euler([math.radians(v) for v in rot], "XYZ").to_matrix()
        if flip:  # mirror across the XZ plane (Y -> -Y), keeps the rotation proper
            s = Matrix.Diagonal((1.0, -1.0, 1.0))
            rmat = s @ rmat @ s
            loc = s @ loc

        bm = bmesh.new()
        pre = Matrix.Identity(3)
        scale = Matrix.Diagonal((dims[0], dims[1], dims[2]))
        if kind == "box":
            bmesh.ops.create_cube(bm, size=1.0)
        elif kind == "cyl":  # axis along local X, +X end has radius * taper
            bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=verts,
                                  radius1=0.5, radius2=0.5 * taper, depth=1.0)
            pre = Matrix.Rotation(math.pi / 2.0, 3, "Y")
        elif kind == "sph":
            bmesh.ops.create_uvsphere(bm, u_segments=verts, v_segments=max(6, verts // 2), radius=0.5)
        elif kind == "tor":  # dims = (major radius, minor radius, unused); ring around local Z
            _torus(bm, dims[0], dims[1], major_segments=max(16, verts), minor_segments=8)
            scale = Matrix.Identity(3)
            if aim is not None:  # ring axis (local Z) follows the aim direction
                pre = Matrix.Rotation(math.pi / 2.0, 3, "Y")
        else:
            raise ValueError(kind)
        xform = Matrix.Translation(loc) @ (rmat @ scale @ pre).to_4x4()
        bmesh.ops.transform(bm, matrix=xform, verts=bm.verts)
        for face in bm.faces:
            face.material_index = mat

        index = len(self.parts)
        mesh = bpy.data.meshes.new("%s_part%d" % (self.name, index))
        bm.to_mesh(mesh)
        bm.free()
        for material in self.materials:
            mesh.materials.append(material)
        obj = bpy.data.objects.new(mesh.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        self.parts.append(obj)

        if bevel > 0.0 and kind != "tor":
            width = min(bevel, 0.45 * min(dims))
            mod = obj.modifiers.new("Bevel", "BEVEL")
            mod.width = width
            mod.segments = seg
            mod.limit_method = "ANGLE"
            mod.angle_limit = math.radians(40.0)
            bpy.context.view_layer.objects.active = obj
            with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
                bpy.ops.object.modifier_apply(modifier=mod.name)

    # -- shape helpers --------------------------------------------------------------
    def box(self, loc, dims, mat, **kw):
        self.add("box", loc, dims, mat, **kw)

    def sph(self, loc, dims, mat, **kw):
        self.add("sph", loc, dims, mat, **kw)

    def cyl(self, loc, dims, mat, **kw):  # axis along local X (dims: length, dia, dia)
        self.add("cyl", loc, dims, mat, **kw)

    def vcyl(self, loc, height, dia, mat, taper=1.0, **kw):  # vertical, taper = top / bottom radius
        self.add("cyl", loc, (height, dia, dia), mat, rot=(0, -90, 0), taper=taper, **kw)

    def ycyl(self, loc, length, dia, mat, **kw):  # axis along Y
        self.add("cyl", loc, (length, dia, dia), mat, rot=(0, 0, 90), **kw)

    def tor(self, loc, major, minor, mat, **kw):
        self.add("tor", loc, (major, minor, 0.0), mat, **kw)

    def rod(self, p0, p1, r0, mat, r1=None, verts=10, **kw):
        p0, p1 = Vector(p0), Vector(p1)
        length = (p1 - p0).length
        self.add("cyl", (p0 + p1) / 2.0, (length, 2 * r0, 2 * r0), mat, aim=p1 - p0, verts=verts,
                 taper=(r1 / r0) if r1 else 1.0, **kw)

    def eye(self, c, r, aim=(1, 0, 0)):
        """Machine lens: dark housing, cyan-white ring, Team dome."""
        a = Vector(aim).normalized()
        c = Vector(c)
        h = min(0.14, max(0.05, 0.5 * r))
        self.cyl(c + a * (h / 2), (h, 2 * r, 2 * r), DARK, aim=a, verts=24)
        self.cyl(c + a * (h * 1.1), (0.4 * h, 1.7 * r, 1.7 * r), GLOW, aim=a, verts=24)
        self.sph(c + a * h, (0.9 * r, 1.4 * r, 1.4 * r), TEAM, aim=a, verts=20)

    # -- detail helpers -------------------------------------------------------------
    def groove(self, loc, length, normal, along, width=0.014, depth=0.012):
        """Inlaid seam on a surface: dark bed with a lit Glow core. loc lies on the surface."""
        f = Frame.surface(self, loc, normal, along)
        f.box((0, 0, 0), (length + 2 * width, width * 3.2, depth), DARK)
        f.box((0, 0, depth * 0.35), (length, width, depth), GLOW)

    def seams(self, loc, dims, mat, cuts, rot=(0, 0, 0), gap=0.02, depth=0.014, bevel=0.012,
              core=GLOW, **kw):
        """Shell block cut into bevelled plates by grooves. cuts = ((axis, fraction), ...) with the
        fraction measured from the -axis face; the groove floor is a `core` block sunk `depth`."""
        spans = []
        for ax in range(3):
            edges = [0.0] + sorted(fr for a, fr in cuts if a == ax) + [1.0]
            span = []
            for i in range(len(edges) - 1):
                lo = edges[i] * dims[ax] - dims[ax] / 2 + (gap / 2 if i > 0 else 0.0)
                hi = edges[i + 1] * dims[ax] - dims[ax] / 2 - (gap / 2 if i < len(edges) - 2 else 0.0)
                span.append((lo, hi))
            spans.append(span)
        f = Frame(self, loc, rot=rot)
        for x0, x1 in spans[0]:
            for y0, y1 in spans[1]:
                for z0, z1 in spans[2]:
                    f.box(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0),
                          mat, bevel=bevel, **kw)
        f.box((0, 0, 0), tuple(d - 2 * depth for d in dims), core, **kw)

    def cable(self, p0, p1, p2, r, mat=DARK, n=6, verts=6, **kw):
        """Hose or cable: quadratic Bezier p0 -> p2 pulled toward p1, drawn as short rods."""
        p0, p1, p2 = Vector(p0), Vector(p1), Vector(p2)
        pts = [p0.lerp(p1, i / n).lerp(p1.lerp(p2, i / n), i / n) for i in range(n + 1)]
        for a, b in zip(pts, pts[1:]):
            self.rod(a, b, r, mat, verts=verts, **kw)

    def plate(self, loc, normal, along, size, thick, mat, bevel=0.012, rivets=None, rivet_mat=DARK,
              inset=0.03, r=0.011, **kw):
        """Bolted scrap plate lying on a surface: size = (along, across), rivets = (nu, nv) border grid."""
        f = Frame.surface(self, loc, normal, along)
        f.box((0, 0, 0), (size[0], size[1], thick), mat, bevel=bevel, **kw)
        if rivets:
            f.rivets(size, z=thick / 2, nu=rivets[0], nv=rivets[1], inset=inset, r=r, mat=rivet_mat, **kw)
        return f

    def face(self, loc, dims, face, rot=(0, 0, 0)):
        """Surface frame on one face ('+x' ... '-z') of a box centred at loc; returns (frame, (u, v))."""
        R = Euler([math.radians(v) for v in rot], "XYZ").to_matrix()
        axis = "xyz".index(face[1])
        n = Vector((0.0, 0.0, 0.0))
        n[axis] = 1.0 if face[0] == "+" else -1.0
        if axis == 0:
            along, size = (0, 0, 1), (dims[2], dims[1])
        elif axis == 1:
            along, size = (1, 0, 0), (dims[0], dims[2])
        else:
            along, size = (1, 0, 0), (dims[0], dims[1])
        return Frame.surface(self, Vector(loc) + R @ (n * dims[axis] / 2), R @ n, R @ Vector(along)), size

    def ring_rivets(self, c, radius, n, mat=DARK, r=0.011, phase=0.0):
        """Rivet heads spaced around a vertical cylinder of the given radius centred on c."""
        for k in range(n):
            a = phase + 2.0 * math.pi * k / n
            radial = Vector((math.cos(a), math.sin(a), 0.0))
            Frame.surface(self, Vector(c) + radial * radius, radial, (0, 0, 1)).rivet((0, 0, 0), r=r, mat=mat)

    def lens(self, c, r, aim=(1, 0, 0), clamps=4, collar=0.5, ring_verts=28):
        """Machine lens housing: dark barrel, shell collar with clamp blocks, Glow ring, Team dome.
        `collar` is how far along the barrel (in barrel-length units) the collar and clamps sit."""
        self.eye(c, r, aim)
        a = Vector(aim).normalized()
        c = Vector(c)
        h = min(0.14, max(0.05, 0.5 * r))
        self.tor(c + a * (h * (collar + 0.1)), r * 1.02 + 0.012, 0.018, SHELL, aim=a, verts=ring_verts)
        u = a.cross(Vector((0, 0, 1)) if abs(a.z) < 0.99 else Vector((0, 1, 0))).normalized()
        v = a.cross(u)
        for k in range(clamps):
            ang = math.radians(360.0 * (k + 0.5) / clamps)
            radial = u * math.cos(ang) + v * math.sin(ang)
            f = Frame.surface(self, c + a * (h * collar) + radial * (r * 1.02 + 0.012), radial, a)
            f.box((0, 0, 0), (h * 1.15, r * 0.36, 0.045), SHELL, bevel=0.01)

    def wheel(self, loc, radius, width, lugs=16, lug=0.03, hub=SHELL):
        """Tyre with chevron tread lugs, axis along Y. The lowest lug corner sits at exactly loc.z - radius."""
        loc = Vector(loc)
        self.ycyl(loc, width, 2 * (radius - lug * 0.6), DARK, bevel=0.03, verts=24)
        tang = 2.0 * math.pi * radius / lugs * 0.5
        tmax = 0.5 * tang * math.cos(math.radians(22)) + 0.46 * width * math.sin(math.radians(22))
        angles = [2.0 * math.pi * k / lugs for k in range(lugs)]
        # tilted lug corners reach lower than the lug face centre: pull the outer faces in so no corner
        # dips below the ground plane and the lowest corner touches it exactly
        rf = min((radius - tmax * abs(math.sin(a))) / math.cos(a) for a in angles if math.cos(a) > 0.05)
        for k, th in enumerate(angles):
            radial = Vector((math.sin(th), 0.0, -math.cos(th)))
            f = Frame.surface(self, loc + radial * (rf - lug / 2), radial, (0, 1, 0))
            f.box((0, 0, 0), (width * 0.92, tang, lug), DARK, rot=(0, 0, 22 if k % 2 else -22))
        self.ycyl(loc, width + 0.03, radius * 0.95, hub, verts=14)
        self.ycyl(loc, width + 0.05, radius * 0.5, DARK, verts=12)
        for k in range(5):
            th = 2.0 * math.pi * k / 5
            self.ycyl(loc + Vector((math.sin(th), 0, math.cos(th))) * radius * 0.3, width + 0.07, 0.03,
                      hub, verts=6)

    # -- finish ---------------------------------------------------------------------
    def finish(self):
        bpy.ops.object.select_all(action="DESELECT")
        for part in self.parts:
            part.select_set(True)
        bpy.context.view_layer.objects.active = self.parts[0]
        bpy.ops.object.join()
        obj = bpy.context.view_layer.objects.active
        obj.name = self.name
        obj.data.name = self.name
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(38.0))
        return obj


def local_point(loc, rot, offset):
    return Vector(loc) + Euler([math.radians(v) for v in rot], "XYZ").to_matrix() @ Vector(offset)


class Frame:
    """Local axes for details attached to a tilted part: offsets and dims are in the part's own axes."""

    def __init__(self, model, loc, rot=(0, 0, 0), matrix=None):
        self.m = model
        self.loc = Vector(loc)
        self.R = (matrix.to_3x3() if matrix is not None
                  else Euler([math.radians(v) for v in rot], "XYZ").to_matrix())

    @classmethod
    def surface(cls, model, loc, normal, along=(1, 0, 0)):
        """Frame with +Z = surface normal and +X = `along` projected onto the surface."""
        z = Vector(normal).normalized()
        a = Vector(along)
        x = a - z * a.dot(z)
        if x.length < 1e-6:
            x = Vector((0, 1, 0)) - z * z.y
        x.normalize()
        return cls(model, loc, matrix=Matrix((x, z.cross(x), z)).transposed())

    def p(self, off):
        return self.loc + self.R @ Vector(off)

    def d(self, v):
        return self.R @ Vector(v)

    def box(self, off, dims, mat, rot=None, **kw):
        R = self.R if rot is None else self.R @ Euler([math.radians(v) for v in rot], "XYZ").to_matrix()
        self.m.add("box", self.p(off), dims, mat, rot=R, **kw)

    def cyl(self, off, dims, mat, axis=(1, 0, 0), **kw):  # dims: length, dia, dia along `axis`
        self.m.add("cyl", self.p(off), dims, mat, aim=self.d(axis), **kw)

    def sph(self, off, dims, mat, **kw):
        self.m.add("sph", self.p(off), dims, mat, rot=self.R, **kw)

    def rivet(self, off, r=0.011, mat=DARK, axis=(0, 0, 1), **kw):
        n = Vector(axis).normalized()
        self.m.add("cyl", self.p(off) + self.d(n * r * 0.3), (r, 2 * r, 2 * r), mat, aim=self.d(n),
                   verts=6, **kw)

    def rivets(self, size, z=0.0, nu=3, nv=2, inset=0.03, r=0.011, mat=DARK, axis=(0, 0, 1), **kw):
        """Border rivets on a size = (u, v) rectangle centred on the frame origin at height z."""
        u, v = size
        for i in range(nu):
            for j in range(nv):
                if 0 < i < nu - 1 and 0 < j < nv - 1:
                    continue
                x = -u / 2 + inset + (u - 2 * inset) * i / max(1, nu - 1)
                y = -v / 2 + inset + (v - 2 * inset) * j / max(1, nv - 1)
                self.rivet((x, y, z), r=r, mat=mat, axis=axis, **kw)

    def vent(self, off, size, slats=4, bg=DARK, fg=SHELL, **kw):
        u, v = size
        self.box(off, (u, v, 0.014), bg, **kw)
        for i in range(slats):
            y = -v / 2 + v * (i + 0.5) / slats
            self.box((off[0], off[1] + y, off[2] + 0.008), (u * 0.92, v / (slats * 2.4), 0.012), fg, **kw)


# --------------------------------------------------------------------------------------
# Machine units: symmetric, glossy white shells, one lens eye, cyan-white seams
# --------------------------------------------------------------------------------------
def machine_frontline(mats):
    m = Model("SM_Machine_Frontline", mats)
    y = 0.19
    # feet: dark tread sole, shell boot, toe cap, heel block, Glow edge seam
    m.box((0.05, y, -0.5875), (0.44, 0.28, 0.025), DARK, mirror=True)
    m.box((0.05, y, -0.525), (0.44, 0.28, 0.10), SHELL, bevel=0.035, mirror=True)
    m.box((0.20, y, -0.475), (0.15, 0.24, 0.05), SHELL, rot=(0, -12, 0), bevel=0.02, mirror=True)
    m.box((-0.11, y, -0.47), (0.13, 0.24, 0.06), SHELL, bevel=0.02, mirror=True)
    m.box((0.05, y + 0.142, -0.525), (0.36, 0.008, 0.018), GLOW, mirror=True)
    # ankle, shin plates cut by a Glow seam, shin guard, rear vents, hydraulic piston
    m.vcyl((0.0, y, -0.44), 0.10, 0.17, DARK, mirror=True)
    m.seams((0.0, y, -0.30), (0.30, 0.26, 0.26), SHELL, ((2, 0.4),), bevel=0.025, mirror=True)
    m.box((0.155, y, -0.29), (0.04, 0.19, 0.20), SHELL, rot=(0, -7, 0), bevel=0.015, mirror=True)
    for dz in (-0.05, 0.0, 0.05):
        m.box((-0.155, y, -0.30 + dz), (0.014, 0.17, 0.02), DARK, mirror=True)
    m.rod((-0.17, y, -0.46), (-0.19, y, -0.14), 0.02, DARK, mirror=True)
    m.rod((-0.17, y, -0.46), (-0.18, y, -0.32), 0.032, SHELL, mirror=True)
    # knee: dark joint, shell cap with Glow slit
    m.sph((0.0, y, -0.14), (0.24, 0.24, 0.24), DARK, mirror=True)
    m.box((0.10, y, -0.14), (0.07, 0.17, 0.13), SHELL, bevel=0.03, mirror=True)
    m.box((0.138, y, -0.14), (0.01, 0.10, 0.02), GLOW, mirror=True)
    # hips: three plates, front skirt, side plates, dark waist ring
    m.seams((0.0, 0.0, -0.10), (0.34, 0.50, 0.14), SHELL, ((1, 0.33), (1, 0.67)), bevel=0.03)
    m.box((0.185, 0.0, -0.14), (0.05, 0.32, 0.12), SHELL, rot=(0, 12, 0), bevel=0.02)
    m.box((0.0, 0.265, -0.09), (0.22, 0.05, 0.12), SHELL, bevel=0.02, mirror=True)
    m.box((0.0, 0.0, -0.02), (0.42, 0.46, 0.03), DARK)
    # rounded-box torso, belt band with lit ring, flank plates, side vents, chest lens
    m.box((0.0, 0.0, 0.20), (0.50, 0.58, 0.50), SHELL, bevel=0.10, seg=3)
    m.box((0.0, 0.0, 0.06), (0.53, 0.61, 0.045), SHELL, bevel=0.015)
    m.box((0.0, 0.0, 0.094), (0.52, 0.60, 0.014), GLOW)
    for s in (1, -1):
        m.box((0.265, 0.27 * s, 0.24), (0.06, 0.12, 0.32), SHELL, rot=(0, -6, 0), bevel=0.025, seg=2)
        m.groove((0.298, 0.27 * s, 0.24), 0.24, (1, 0, 0), (0, 0, 1))
    Frame.surface(m, (0.0, 0.295, 0.20), (0, 1, 0), (1, 0, 0)).vent((0, 0, 0), (0.24, 0.20), slats=5, mirror=True)
    m.lens((0.20, 0.0, 0.30), 0.19, aim=(1, 0, 0.7))
    # head module: neck, crown plate, Team status bar, Glow visor slit, ear pods
    m.vcyl((0.0, 0.0, 0.445), 0.06, 0.16, DARK)
    m.box((0.0, 0.0, 0.52), (0.30, 0.36, 0.14), SHELL, bevel=0.05)
    m.box((0.155, 0.0, 0.53), (0.02, 0.24, 0.03), GLOW)
    m.box((-0.005, 0.0, 0.595), (0.24, 0.30, 0.025), SHELL, bevel=0.01)
    m.box((0.03, 0.0, 0.6125), (0.12, 0.20, 0.012), TEAM)
    m.ycyl((0.0, 0.205, 0.52), 0.05, 0.12, DARK, mirror=True)
    m.ycyl((0.0, 0.23, 0.52), 0.02, 0.06, GLOW, mirror=True)
    # heavy layered shoulder plates, guard wing, dark ball joints
    m.box((0.0, 0.325, 0.46), (0.36, 0.22, 0.14), SHELL, rot=(-14, 0, 0), bevel=0.05, mirror=True)
    m.box((0.0, 0.325, 0.535), (0.26, 0.03, 0.015), GLOW, rot=(-14, 0, 0), mirror=True)
    m.box((0.0, 0.425, 0.40), (0.30, 0.04, 0.16), SHELL, rot=(-14, 0, 0), bevel=0.02, mirror=True)
    m.sph((0.0, 0.365, 0.32), (0.20, 0.20, 0.20), DARK, mirror=True)
    # arms: dark core, armour sleeve, Glow ring, piston, fist with finger grooves and knuckle guard
    m.vcyl((0.02, 0.375, 0.15), 0.30, 0.13, DARK, mirror=True)
    m.vcyl((0.02, 0.375, 0.20), 0.15, 0.165, SHELL, verts=14, mirror=True)
    m.tor((0.02, 0.375, 0.115), 0.075, 0.009, GLOW, verts=16, mirror=True)
    m.rod((0.105, 0.375, 0.02), (0.105, 0.375, 0.30), 0.016, DARK, mirror=True)
    m.rod((0.105, 0.375, 0.16), (0.105, 0.375, 0.30), 0.024, SHELL, mirror=True)
    m.box((0.08, 0.375, -0.04), (0.20, 0.16, 0.20), SHELL, bevel=0.04, mirror=True)
    for dy in (-0.045, 0.0, 0.045):
        m.box((0.182, 0.375 + dy, -0.06), (0.008, 0.006, 0.15), DARK, mirror=True)
    m.box((0.18, 0.375, 0.03), (0.03, 0.14, 0.05), SHELL, bevel=0.01, mirror=True)
    # power pack: two plates, cooling fins, exhaust nozzles, cables to the shoulders
    m.seams((-0.30, 0.0, 0.22), (0.14, 0.34, 0.30), SHELL, ((2, 0.5),), bevel=0.03)
    for i in range(4):
        m.box((-0.385, 0.0, 0.11 + 0.055 * i), (0.03, 0.30, 0.02), SHELL, bevel=0.006)
    m.cyl((-0.33, 0.10, 0.41), (0.10, 0.07, 0.07), DARK, aim=(-0.3, 0, 1), mirror=True)
    m.cyl((-0.345, 0.10, 0.455), (0.02, 0.055, 0.055), GLOW, aim=(-0.3, 0, 1), mirror=True)
    m.cable((-0.30, 0.10, 0.37), (-0.15, 0.31, 0.33), (-0.03, 0.36, 0.34), 0.012, mirror=True)
    return m.finish()


def machine_ranged(mats):
    m = Model("SM_Machine_Ranged", mats)
    m.sph((0.0, 0.0, 0.18), (0.62, 0.46, 0.46), SHELL, verts=24)
    for x, r in ((-0.03, 0.233), (0.12, 0.216), (-0.17, 0.197)):  # Glow seam rings around the hull
        m.tor((x, 0.0, 0.18), r, 0.012, GLOW, rot=(0, 90, 0), verts=28)

    def on_hull(x, y):  # point and outward normal on the ellipsoid hull
        z = 0.23 * math.sqrt(max(0.0, 1.0 - (x / 0.31) ** 2 - (y / 0.23) ** 2))
        return Vector((x, y, 0.18 + z)), Vector((x / 0.31 ** 2, y / 0.23 ** 2, z / 0.23 ** 2))

    # layered shell pads on the upper hull, carapace disc with Team lamp
    for ang in (50, 130, 230, 310):
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        p, n = on_hull(0.19 * c, 0.15 * s)
        Frame.surface(m, p, n, (-s, c, 0)).sph((0, 0, -0.012), (0.20, 0.14, 0.07), SHELL, verts=12)
    m.vcyl((0.02, 0.0, 0.405), 0.03, 0.26, SHELL, taper=0.9, verts=24, bevel=0.01)
    m.vcyl((0.02, 0.0, 0.424), 0.012, 0.21, TEAM, verts=24)
    # glowing underside with stabiliser fins
    m.vcyl((0.0, 0.0, -0.12), 0.16, 0.30, DARK, taper=0.6)
    m.vcyl((0.0, 0.0, -0.235), 0.05, 0.19, GLOW)
    m.sph((0.0, 0.0, -0.27), (0.2, 0.2, 0.11), GLOW)
    for ang in (90, 210, 330):
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        f = Frame.surface(m, (0.17 * c, 0.17 * s, -0.12), (-s, c, 0), (c, s, 0))
        f.box((0, 0, 0), (0.17, 0.11, 0.024), SHELL, bevel=0.008)
    # halo ring held off the body by spokes, with clamp blocks
    m.tor((0.0, 0.0, 0.235), 0.40, 0.028, SHELL, verts=40)
    m.tor((0.0, 0.0, 0.125), 0.40, 0.028, SHELL, verts=40)
    m.tor((0.0, 0.0, 0.18), 0.395, 0.02, GLOW, verts=40)
    for ang in (55, 125, 235, 305):
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        m.rod((0.2 * c, 0.2 * s, 0.18), (0.40 * c, 0.40 * s, 0.18), 0.018, DARK, verts=8)
    for k in range(8):
        ang = math.radians(45.0 * k + 22.5)
        c, s = math.cos(ang), math.sin(ang)
        Frame.surface(m, (0.40 * c, 0.40 * s, 0.18), (c, s, 0), (0, 0, 1)).box(
            (0, 0, 0), (0.135, 0.07, 0.05), DARK, bevel=0.01)
    # lens eye and twin emitter prongs with mounts and coils
    m.lens((0.22, 0.0, 0.23), 0.15, aim=(1, 0, 0.6))
    m.box((0.2, 0.13, 0.07), (0.12, 0.08, 0.10), SHELL, bevel=0.02, mirror=True)
    m.cyl((0.29, 0.13, 0.06), (0.14, 0.10, 0.10), SHELL, verts=12, mirror=True)
    m.rod((0.22, 0.13, 0.06), (0.42, 0.13, 0.06), 0.034, DARK, verts=10, mirror=True)
    for x in (0.37, 0.395):
        m.cyl((x, 0.13, 0.06), (0.014, 0.085, 0.085), DARK, verts=12, mirror=True)
    m.sph((0.425, 0.13, 0.06), (0.09, 0.09, 0.09), GLOW, verts=10, mirror=True)
    # top fin with mast, rear exhaust and stabiliser blades
    m.box((-0.03, 0.0, 0.47), (0.30, 0.045, 0.10), SHELL, bevel=0.015)
    m.box((-0.06, 0.0, 0.545), (0.20, 0.04, 0.06), SHELL, bevel=0.012)
    m.box((-0.09, 0.0, 0.60), (0.10, 0.035, 0.05), SHELL, bevel=0.01)
    m.box((-0.04, 0.026, 0.50), (0.20, 0.006, 0.012), GLOW, mirror=True)
    m.box((0.06, 0.0, 0.47), (0.07, 0.06, 0.03), DARK)
    m.rod((0.07, 0.0, 0.52), (0.07, 0.0, 0.60), 0.008, DARK, verts=6)
    m.sph((0.07, 0.0, 0.605), (0.05, 0.05, 0.05), GLOW, verts=8)
    m.cyl((-0.325, 0.0, 0.16), (0.08, 0.16, 0.16), DARK)
    m.cyl((-0.362, 0.0, 0.16), (0.02, 0.12, 0.12), GLOW)
    m.box((-0.29, 0.13, 0.17), (0.15, 0.02, 0.15), SHELL, rot=(0, 0, 14), bevel=0.008, mirror=True)
    return m.finish()


def machine_siege(mats):
    m = Model("SM_Machine_Siege", mats)
    # hull: lower slab cut into plates by Glow seams, upper hull with a centre seam, vents
    m.seams((0.0, 0.0, -0.20), (0.78, 0.60, 0.20), SHELL, ((0, 0.36), (0, 0.64)), bevel=0.04, seg=2)
    m.box((0.0, 0.0, -0.32), (0.60, 0.44, 0.06), DARK)
    m.seams((-0.05, 0.0, 0.0), (0.50, 0.42, 0.24), SHELL, ((1, 0.5),), bevel=0.06, seg=2)
    m.groove((0.392, 0.0, -0.20), 0.30, (1, 0, 0), (0, 1, 0))
    Frame.surface(m, (-0.30, 0.0, 0.0), (-1, 0, 0), (0, 1, 0)).vent((0, 0, 0), (0.28, 0.10), slats=4)
    Frame.surface(m, (-0.05, 0.21, 0.0), (0, 1, 0), (1, 0, 0)).vent((0, 0, 0), (0.26, 0.12), slats=4, mirror=True)
    Frame.surface(m, (-0.05, 0.302, -0.20), (0, 1, 0), (1, 0, 0)).vent((0, 0, 0), (0.30, 0.08), slats=3, mirror=True)
    for s in (1, -1):
        m.box((-0.06, 0.185 * s, 0.122), (0.34, 0.04, 0.012), TEAM)
    # long forward barrel: breech shroud, recoil pistons, coil rings, dish and prism emitter
    m.cyl((0.22, 0.0, 0.0), (0.20, 0.22, 0.22), SHELL, verts=20)
    m.cyl((0.38, 0.0, 0.0), (0.34, 0.11, 0.11), SHELL, verts=16)
    for x in (0.30, 0.42):
        m.cyl((x, 0.0, 0.0), (0.03, 0.145, 0.145), DARK, verts=16)
    for x in (0.48, 0.51):
        m.cyl((x, 0.0, 0.0), (0.012, 0.128, 0.128), GLOW, verts=16)
    for i in range(3):
        m.box((0.16 + 0.05 * i, 0.0, 0.108), (0.03, 0.05, 0.012), DARK)
    m.rod((0.20, 0.135, 0.0), (0.45, 0.135, 0.0), 0.018, DARK, verts=8, mirror=True)
    m.rod((0.20, 0.135, 0.0), (0.32, 0.135, 0.0), 0.028, SHELL, verts=8, mirror=True)
    m.cyl((0.575, 0.0, 0.0), (0.10, 0.10, 0.10), SHELL, verts=24, taper=3.0)
    m.cyl((0.622, 0.0, 0.0), (0.012, 0.27, 0.27), GLOW, verts=24)
    m.cyl((0.625, 0.0, 0.0), (0.08, 0.07, 0.07), GLOW, verts=3)
    m.lens((-0.02, 0.0, 0.08), 0.15, aim=(0.35, 0, 1))
    # four splayed legs: ball hip, armoured upper leg, hydraulic piston, Glow bands, knee, foot
    for sx in (1, -1):
        a = Vector((0.26 * sx, 0.30, -0.20))
        b = Vector((0.44 * sx, 0.52, -0.12))
        c = Vector((0.48 * sx, 0.55, -0.55))
        m.sph(a, (0.18, 0.18, 0.18), DARK, mirror=True)
        m.rod(a, b, 0.06, SHELL, mirror=True)
        m.rod(a.lerp(b, 0.25), a.lerp(b, 0.62), 0.078, SHELL, verts=12, bevel=0.01, mirror=True)
        m.rod(a + Vector((0, 0, 0.10)), b + Vector((0, 0, 0.09)), 0.018, DARK, verts=8, mirror=True)
        m.rod(a + Vector((0, 0, 0.10)), a.lerp(b, 0.5) + Vector((0, 0, 0.095)), 0.03, SHELL, verts=8,
              mirror=True)
        m.sph(b, (0.15, 0.15, 0.15), SHELL, mirror=True)
        m.rod(b, c, 0.05, DARK, r1=0.038, mirror=True)
        for t, r in ((0.3, 0.055), (0.7, 0.049)):
            m.rod(b.lerp(c, t), b.lerp(c, t + 0.035), r, GLOW, verts=12, mirror=True)
        m.vcyl((0.48 * sx, 0.55, -0.575), 0.05, 0.18, DARK, mirror=True)
        m.vcyl((0.48 * sx, 0.55, -0.545), 0.03, 0.14, SHELL, taper=0.85, verts=14, mirror=True)
        m.tor((0.48 * sx, 0.55, -0.56), 0.087, 0.006, GLOW, verts=16, mirror=True)
    return m.finish()


# --------------------------------------------------------------------------------------
# Human units: scrappy, asymmetric, khaki / gunmetal plates, amber lamps
# --------------------------------------------------------------------------------------
def human_frontline(mats):
    m = Model("SM_Human_Frontline", mats)
    # boots (lugged soles, toe caps, straps), legs, shin guards and knee pads bolted on at odd angles
    for s in (1, -1):
        y = 0.115 * s
        m.box((0.04, y, -0.5275), (0.27, 0.15, 0.105), DARK, bevel=0.03)
        m.box((0.04, y, -0.585), (0.29, 0.16, 0.01), SHELL)
        for i in range(5):
            m.box((-0.06 + 0.06 * i, y, -0.595), (0.03, 0.155, 0.01), DARK)
        m.box((0.155, y, -0.52), (0.09, 0.15, 0.08), SHELL, bevel=0.025)
        m.box((0.0, y, -0.34), (0.19, 0.15, 0.30), SHELL, bevel=0.03)
        m.box((0.0, y, -0.45), (0.21, 0.17, 0.03), DARK, bevel=0.008)
    m.plate((0.105, 0.115, -0.36), (1, 0, 0), (0, 0, 1), (0.17, 0.11), 0.03, DARK, rivets=(2, 2), rivet_mat=SHELL)
    m.plate((0.105, -0.115, -0.385), (1, 0.03, 0.06), (0, 0, 1), (0.13, 0.11), 0.03, DARK, rivets=(2, 2),
            rivet_mat=SHELL)
    m.plate((0.11, 0.115, -0.235), (1, 0, 0), (0, 0, 1), (0.10, 0.13), 0.04, DARK, bevel=0.015,
            rivets=(2, 2), rivet_mat=SHELL)
    m.plate((0.105, -0.12, -0.25), (1, -0.1, -0.12), (0, 0, 1), (0.11, 0.12), 0.04, DARK, bevel=0.015,
            rivets=(2, 2), rivet_mat=SHELL)
    # belt with buckle, flapped pouches, belt lamp, tool roll strapped across the back
    m.box((0.0, 0.0, -0.12), (0.30, 0.36, 0.10), DARK, bevel=0.02)
    m.plate((0.155, 0.0, -0.12), (1, 0, 0), (0, 0, 1), (0.075, 0.085), 0.02, SHELL, bevel=0.008)
    m.box((0.14, 0.15, -0.135), (0.09, 0.08, 0.09), SHELL, rot=(0, 0, 8), bevel=0.015)
    m.box((0.145, 0.15, -0.085), (0.10, 0.085, 0.03), DARK, rot=(0, 0, 8), bevel=0.01)
    m.box((0.06, -0.19, -0.135), (0.10, 0.06, 0.09), SHELL, rot=(0, 0, -10), bevel=0.015)
    m.box((0.065, -0.19, -0.085), (0.11, 0.065, 0.03), DARK, rot=(0, 0, -10), bevel=0.01)
    m.box((0.12, -0.05, -0.12), (0.05, 0.05, 0.05), DARK)
    m.sph((0.15, -0.05, -0.12), (0.07, 0.07, 0.07), GLOW, verts=8)
    m.ycyl((-0.19, 0.0, -0.09), 0.30, 0.10, SHELL, verts=12)
    for y in (-0.10, 0.10):
        m.ycyl((-0.19, y, -0.09), 0.03, 0.112, DARK, verts=12)
    for y in (-0.15, 0.15):
        m.ycyl((-0.19, y, -0.09), 0.012, 0.105, DARK, verts=12)
    # torso, riveted scrap chest plate, tape band
    m.box((0.0, 0.0, 0.11), (0.32, 0.42, 0.44), SHELL, bevel=0.05)
    m.box((0.175, -0.01, 0.15), (0.05, 0.32, 0.28), DARK, rot=(0, 0, 3), bevel=0.012)
    Frame.surface(m, (0.20, -0.01, 0.15), (1, 0, 0), (0, 0, 1)).rivets((0.28, 0.30), nu=3, nv=3, inset=0.035,
                                                                        mat=SHELL)
    m.box((0.0, 0.0, 0.02), (0.34, 0.44, 0.05), SHELL, bevel=0.01)
    # backpack with rear plate, Team lid stripe and an exhaust stack with rust bands
    m.box((-0.215, 0.0, 0.12), (0.13, 0.30, 0.34), DARK, bevel=0.03)
    m.plate((-0.283, 0.0, 0.12), (-1, 0, 0), (0, 0, 1), (0.26, 0.24), 0.02, SHELL, bevel=0.008, rivets=(3, 3))
    m.box((-0.215, 0.0, 0.305), (0.13, 0.28, 0.03), SHELL, bevel=0.01)
    m.box((-0.215, 0.0, 0.324), (0.10, 0.20, 0.012), TEAM)
    m.vcyl((-0.25, 0.09, 0.40), 0.30, 0.055, DARK, verts=10)
    for z in (0.32, 0.44):
        m.vcyl((-0.25, 0.09, z), 0.03, 0.08, SHELL, verts=10)
    m.vcyl((-0.25, 0.09, 0.565), 0.05, 0.05, DARK, taper=1.9, verts=10)
    # layered shoulder plates with Team paint, riveted, and lower pads
    m.box((0.0, 0.27, 0.35), (0.24, 0.22, 0.06), SHELL, rot=(-22, 0, 4), bevel=0.02)
    m.box((-0.02, -0.26, 0.36), (0.30, 0.20, 0.05), DARK, rot=(18, -8, -6), bevel=0.02)
    m.box(local_point((0.0, 0.27, 0.35), (-22, 0, 4), (0, 0, 0.033)), (0.16, 0.14, 0.012), TEAM,
          rot=(-22, 0, 4))
    m.box(local_point((-0.02, -0.26, 0.36), (18, -8, -6), (0, 0, 0.03)), (0.20, 0.14, 0.012), TEAM,
          rot=(18, -8, -6))
    Frame(m, (0.0, 0.27, 0.35), (-22, 0, 4)).rivets((0.24, 0.22), z=0.03, nu=2, nv=2, inset=0.025, r=0.010)
    Frame(m, (-0.02, -0.26, 0.36), (18, -8, -6)).rivets((0.30, 0.20), z=0.025, nu=2, nv=2, inset=0.025,
                                                         r=0.010, mat=SHELL)
    m.box((0.0, 0.31, 0.30), (0.20, 0.18, 0.05), DARK, rot=(-34, 0, 4), bevel=0.015)
    m.box((-0.02, -0.30, 0.30), (0.22, 0.17, 0.05), SHELL, rot=(28, -8, -6), bevel=0.015)
    # helmet: dome, riveted rim, visor, neck guard, ear plates, Team stripe, lamp with hood and cable
    m.vcyl((0.0, 0.0, 0.345), 0.06, 0.13, DARK)
    m.sph((0.0, 0.0, 0.47), (0.26, 0.26, 0.22), SHELL, verts=16)
    m.vcyl((0.0, 0.0, 0.395), 0.03, 0.29, DARK, verts=16)
    m.ring_rivets((0.0, 0.0, 0.395), 0.145, 8, mat=SHELL, r=0.009)
    m.box((0.11, 0.0, 0.42), (0.06, 0.20, 0.06), DARK)
    m.box((-0.12, 0.0, 0.42), (0.05, 0.22, 0.10), DARK, rot=(0, 14, 0), bevel=0.01)
    m.ycyl((0.0, 0.135, 0.45), 0.03, 0.10, DARK, mirror=True)
    m.box((0.0, 0.0, 0.575), (0.24, 0.09, 0.02), TEAM, bevel=0.006)
    m.box((0.08, -0.13, 0.50), (0.07, 0.06, 0.05), DARK)
    m.sph((0.115, -0.135, 0.50), (0.085, 0.085, 0.085), GLOW, verts=8)
    m.box((0.09, -0.13, 0.535), (0.09, 0.08, 0.015), DARK)
    m.cable((0.02, -0.13, 0.44), (-0.12, -0.20, 0.36), (-0.21, -0.10, 0.30), 0.010)
    # riot shield (three panels): Team stripes, edge trim, riveted faces, patch plate, lit viewport slit
    panels = ((0.31, 0.0, 0.02, 0.30, 0), (0.279, 0.245, 0.02, 0.20, 18), (0.279, -0.245, 0.02, 0.20, -18))
    for x, y, z, w, yaw in panels:
        rot = (0, -8, yaw)
        f = Frame(m, (x, y, z), rot)
        m.box((x, y, z), (0.06, w, 0.66), DARK, rot=rot, bevel=0.02)
        f.box((0, 0, 0.12), (0.075, w, 0.10), TEAM)
        f.box((0, 0, 0.33), (0.12, w, 0.06), TEAM, bevel=0.01)
        f.box((0, 0, 0.37), (0.13, w, 0.02), SHELL)
        for e in (1, -1):
            f.box((0.004, e * (w / 2 - 0.012), -0.03), (0.07, 0.024, 0.58), SHELL, bevel=0.006)
        f.box((0.004, 0, -0.325), (0.07, w, 0.02), SHELL, bevel=0.006)
        face = Frame.surface(m, f.p((0.03, 0, 0)), f.d((1, 0, 0)), f.d((0, 0, 1)))
        face.rivets((0.58, w - 0.02), nu=5, nv=2, inset=0.03, r=0.011, mat=SHELL)
        if yaw:
            face.box((-0.06, 0, 0.007), (0.30, 0.024, 0.014), SHELL, rot=(0, 0, 40 if yaw > 0 else -40))
        else:
            face.box((0.21, 0, 0.007), (0.035, 0.20, 0.014), GLOW)
            pf = Frame(m, face.p((-0.15, 0.06, 0.012)), matrix=face.R @ Euler((0, 0, math.radians(10))).to_matrix())
            pf.box((0, 0, 0), (0.17, 0.15, 0.024), SHELL, bevel=0.008)
            pf.rivets((0.17, 0.15), z=0.012, nu=2, nv=2, inset=0.02, r=0.009)
    # shield arm with elbow pad and cuff, gauntlet
    a, b = Vector((0.0, 0.25, 0.25)), Vector((0.26, 0.16, 0.02))
    m.rod(a, b, 0.06, SHELL)
    m.sph(a.lerp(b, 0.42), (0.09, 0.09, 0.09), DARK, verts=10)
    m.rod(a.lerp(b, 0.78), a.lerp(b, 0.9), 0.072, DARK)
    m.sph(b, (0.11, 0.11, 0.11), DARK, verts=10)
    # sledgehammer over the right shoulder: taped handle, banded steel head
    a2, b2 = Vector((0.0, -0.24, 0.24)), Vector((0.14, -0.29, 0.03))
    m.rod(a2, b2, 0.06, SHELL)
    m.sph(a2.lerp(b2, 0.4), (0.09, 0.09, 0.09), DARK, verts=10)
    grip, tip = b2, Vector((-0.28, -0.20, 0.49))
    m.rod(grip, tip, 0.028, DARK, verts=8)
    for t0, t1 in ((0.12, 0.20), (0.50, 0.58)):
        m.rod(grip.lerp(tip, t0), grip.lerp(tip, t1), 0.036, SHELL, verts=8)
    m.rod(grip.lerp(tip, 0.28), grip.lerp(tip, 0.38), 0.04, TEAM, verts=8)
    head = tip + Vector((-0.01, -0.005, 0.03))
    m.box(head, (0.15, 0.26, 0.15), DARK, rot=(0, -43, 0), bevel=0.02)
    hf = Frame(m, head, (0, -43, 0))
    for e in (1, -1):
        hf.box((0, 0.14 * e, 0), (0.165, 0.03, 0.165), DARK, bevel=0.01)
        hf.box((0, 0.06 * e, 0), (0.172, 0.03, 0.172), SHELL, bevel=0.006)
    m.sph(b2, (0.10, 0.10, 0.10), DARK, verts=10)
    return m.finish()


def human_ranged(mats):
    m = Model("SM_Human_Ranged", mats)
    # boots with lugged soles and toe caps, wrapped legs
    for s in (1, -1):
        y = 0.09 * s
        m.box((0.03, y, -0.535), (0.22, 0.12, 0.09), DARK, bevel=0.025)
        m.box((0.03, y, -0.59), (0.24, 0.13, 0.02), SHELL)
        m.box((0.115, y, -0.51), (0.07, 0.115, 0.06), SHELL, bevel=0.02)
        m.box((0.0, y, -0.34), (0.12, 0.11, 0.32), SHELL, bevel=0.02)
        for i in range(3):
            m.box((0.0, y, -0.45 + 0.07 * i), (0.135, 0.125, 0.022), DARK, bevel=0.005)
    m.box((0.0, 0.0, -0.02), (0.20, 0.28, 0.30), SHELL, bevel=0.03)
    # poncho: two layers, hem band, rope belt with buckle, flapped pouches, bandolier
    m.vcyl((0.0, 0.0, -0.04), 0.40, 0.46, SHELL, taper=0.5, verts=12)
    m.vcyl((0.0, 0.0, -0.235), 0.03, 0.47, DARK, verts=12)
    m.vcyl((0.0, 0.0, 0.115), 0.14, 0.32, SHELL, taper=0.70, verts=12)
    m.vcyl((0.0, 0.0, 0.0), 0.03, 0.34, DARK, verts=12)
    m.box((0.17, 0.0, 0.0), (0.03, 0.05, 0.05), SHELL, bevel=0.01)
    for ang in (60, 200):
        a = math.radians(ang)
        radial = Vector((math.cos(a), math.sin(a), 0.0))
        f = Frame.surface(m, radial * 0.185 + Vector((0, 0, -0.045)), radial, (0, 0, 1))
        f.box((0, 0, 0), (0.09, 0.09, 0.05), DARK, bevel=0.012)
        f.box((0.03, 0, 0.008), (0.035, 0.095, 0.05), SHELL, bevel=0.008)
        f.rivet((0.03, 0, 0.034), r=0.009, mat=DARK)
    p0, p1 = Vector((0.06, 0.10, 0.19)), Vector((0.17, -0.06, -0.02))
    m.rod(p0, p1, 0.02, DARK, verts=8)
    for t in (0.2, 0.4, 0.6, 0.8):
        m.box(p0.lerp(p1, t), (0.04, 0.035, 0.055), SHELL, bevel=0.006)
    # hood, collar, face shadow, goggles with Glow lenses and strap, Team beret
    m.vcyl((0.0, 0.0, 0.19), 0.03, 0.22, DARK, verts=12)
    m.sph((0.0, 0.0, 0.29), (0.26, 0.26, 0.24), SHELL, verts=16)
    m.box((0.115, 0.0, 0.28), (0.08, 0.14, 0.10), DARK, bevel=0.015)
    m.vcyl((0.0, 0.0, 0.305), 0.04, 0.27, DARK, verts=16)
    for sy in (1, -1):
        m.cyl((0.145, 0.04 * sy, 0.30), (0.03, 0.06, 0.06), DARK, verts=12)
        m.cyl((0.16, 0.04 * sy, 0.30), (0.012, 0.048, 0.048), GLOW, verts=12)
    m.sph((0.0, 0.0, 0.36), (0.27, 0.27, 0.15), TEAM, verts=16)
    m.sph((0.0, 0.0, 0.435), (0.04, 0.04, 0.03), TEAM, verts=8)
    # long rifle pointing +X: stock, butt plate, magazine, vented handguard, scope, muzzle brake, Team tape
    y0 = -0.10
    m.box((0.0, y0, 0.14), (0.24, 0.06, 0.08), DARK, bevel=0.01)
    m.box((0.0, y0, 0.183), (0.24, 0.03, 0.012), SHELL)
    m.box((-0.14, y0, 0.12), (0.14, 0.05, 0.09), SHELL, rot=(0, -8, 0), bevel=0.01)
    m.box((-0.205, y0, 0.115), (0.02, 0.055, 0.10), DARK, rot=(0, -8, 0))
    m.box((0.16, y0, 0.14), (0.14, 0.05, 0.06), SHELL, bevel=0.01)
    for x in (0.12, 0.16, 0.20):
        m.box((x, y0, 0.172), (0.02, 0.03, 0.008), DARK)
    m.box((0.02, y0, 0.085), (0.05, 0.045, 0.10), DARK, rot=(0, 8, 0), bevel=0.01)
    m.cyl((0.28, y0, 0.14), (0.34, 0.035, 0.035), DARK, verts=8)
    m.cyl((0.42, y0, 0.14), (0.06, 0.05, 0.05), DARK, verts=8)
    m.cyl((0.02, y0, 0.205), (0.16, 0.04, 0.04), DARK, verts=8)
    m.sph((0.105, y0, 0.205), (0.05, 0.05, 0.05), GLOW, verts=8)
    for x in (-0.03, 0.06):
        m.box((x, y0, 0.19), (0.02, 0.03, 0.03), DARK)
    for x in (0.22, 0.36):
        m.cyl((x, y0, 0.14), (0.03, 0.05, 0.05), SHELL, verts=8)
    m.box((0.175, y0, 0.14), (0.025, 0.056, 0.066), TEAM, bevel=0.004)
    # arms with elbow pads and gloves, team armband on the forward arm
    m.rod((0.0, -0.16, 0.10), (0.06, -0.11, 0.12), 0.04, SHELL, verts=8)
    a0, a1 = Vector((0.0, 0.16, 0.08)), Vector((0.18, -0.05, 0.13))
    m.rod(a0, a1, 0.04, SHELL, verts=8)
    m.rod(a0.lerp(a1, 0.32), a0.lerp(a1, 0.55), 0.056, TEAM, verts=10)
    m.sph(a0, (0.09, 0.09, 0.09), DARK, verts=8)
    m.sph((0.0, -0.16, 0.10), (0.09, 0.09, 0.09), DARK, verts=8)
    m.sph(a1, (0.075, 0.075, 0.075), DARK, verts=8)
    m.sph((0.06, -0.11, 0.12), (0.07, 0.07, 0.07), DARK, verts=8)
    # backpack: strap, side pouches, lashed bedroll with Team bands, back lamp, bent antenna with pennant
    m.box((-0.17, 0.0, 0.06), (0.14, 0.24, 0.28), SHELL, bevel=0.03)
    m.box((-0.245, 0.0, 0.14), (0.02, 0.25, 0.03), DARK)
    m.box((-0.252, 0.0, 0.14), (0.012, 0.04, 0.04), SHELL)
    m.box((-0.17, 0.14, 0.0), (0.10, 0.06, 0.16), DARK, bevel=0.015, mirror=True)
    m.box((-0.17, 0.14, 0.07), (0.11, 0.065, 0.03), SHELL, bevel=0.008, mirror=True)
    m.ycyl((-0.17, 0.0, 0.25), 0.30, 0.12, SHELL, verts=12)
    for y in (-0.09, 0.09):
        m.ycyl((-0.17, y, 0.25), 0.06, 0.135, TEAM, verts=12)
    for y in (-0.14, 0.14):
        m.ycyl((-0.17, y, 0.25), 0.02, 0.13, DARK, verts=12)
    m.box((-0.245, -0.06, 0.02), (0.03, 0.06, 0.05), DARK)
    m.sph((-0.265, -0.06, 0.02), (0.07, 0.07, 0.07), GLOW, verts=8)
    m.box((-0.22, 0.08, 0.20), (0.04, 0.04, 0.03), DARK)
    m.rod((-0.22, 0.08, 0.2), (-0.24, 0.10, 0.42), 0.012, DARK, verts=6)
    m.rod((-0.24, 0.10, 0.42), (-0.10, 0.16, 0.482), 0.012, DARK, verts=6)
    m.sph((-0.10, 0.16, 0.482), (0.05, 0.05, 0.05), GLOW, verts=8)
    m.box((-0.17, 0.13, 0.44), (0.09, 0.012, 0.045), TEAM, rot=(0, 0, 8))
    return m.finish()


def human_siege(mats):
    m = Model("SM_Human_Siege", mats)
    # tyres with chevron tread, hubs and lug nuts on bare axles
    for sx in (1, -1):
        for sy in (1, -1):
            m.wheel((0.36 * sx, 0.50 * sy, -0.39), 0.21, 0.16, lugs=14, lug=0.03)
        m.rod((0.36 * sx, 0.50, -0.39), (0.36 * sx, -0.50, -0.39), 0.03, DARK, verts=8)
    # chassis cut into bolted panels, deck plates at odd angles, painted Team band on the deck
    m.seams((0.0, 0.0, -0.22), (0.96, 0.64, 0.20), SHELL, ((0, 0.30), (0, 0.66)), gap=0.016, depth=0.015,
            bevel=0.02, core=DARK)
    m.box((-0.10, 0.0, -0.10), (0.5, 0.5, 0.05), DARK)
    m.box((0.05, 0.34, -0.16), (0.34, 0.04, 0.22), SHELL, rot=(-10, 0, 3))
    f, sz = m.face((0.05, 0.34, -0.16), (0.34, 0.04, 0.22), "+y", rot=(-10, 0, 3))
    f.rivets(sz, nu=3, nv=2, inset=0.03)
    m.box((-0.05, -0.34, -0.18), (0.44, 0.04, 0.20), DARK, rot=(12, 0, -2))
    f, sz = m.face((-0.05, -0.34, -0.18), (0.44, 0.04, 0.20), "-y", rot=(12, 0, -2))
    f.rivets(sz, nu=4, nv=2, inset=0.03, mat=SHELL)
    m.box((0.22, 0.0, -0.105), (0.22, 0.62, 0.03), TEAM)
    # bull bar, headlamps
    m.box((0.51, 0.0, -0.235), (0.06, 0.58, 0.08), DARK, bevel=0.015)
    for y in (-0.2, 0.0, 0.2):
        m.rod((0.52, y, -0.235), (0.52, y, -0.08), 0.02, DARK, verts=8)
    m.rod((0.52, -0.25, -0.08), (0.52, 0.25, -0.08), 0.02, DARK, verts=8)
    m.box((0.44, 0.24, -0.06), (0.06, 0.08, 0.06), DARK)
    m.sph((0.475, 0.24, -0.06), (0.085, 0.085, 0.085), GLOW, verts=8)
    m.box((0.44, -0.24, -0.06), (0.06, 0.08, 0.06), DARK)
    # cab: riveted side plates, windscreen slit, side windows, roof plate, beacon on a housing
    m.box((-0.28, 0.06, 0.02), (0.30, 0.34, 0.24), SHELL, bevel=0.04)
    for face_name in ("+y", "-y"):
        f, sz = m.face((-0.28, 0.06, 0.02), (0.30, 0.34, 0.24), face_name)
        f.rivets(sz, nu=3, nv=2, inset=0.035)
    m.box((-0.128, 0.06, 0.06), (0.02, 0.24, 0.06), DARK)
    for s in (1, -1):
        m.box((-0.28, 0.06 + 0.172 * s, 0.06), (0.14, 0.012, 0.06), DARK)
    m.box((-0.28, 0.06, 0.155), (0.34, 0.38, 0.03), DARK)
    m.box((-0.22, 0.06, 0.176), (0.16, 0.28, 0.012), TEAM)
    for s in (1, -1):
        m.box((-0.28, 0.06 + 0.19 * s, 0.172), (0.34, 0.025, 0.02), SHELL)
    m.vcyl((-0.37, 0.06, 0.185), 0.03, 0.09, DARK, verts=8)
    m.sph((-0.37, 0.06, 0.215), (0.10, 0.10, 0.10), GLOW, verts=8)
    # exhaust stack with heat-wrap bands, clamp and flared rain cap
    m.rod((-0.40, 0.27, -0.12), (-0.42, 0.27, 0.28), 0.04, DARK, verts=8)
    m.rod((-0.42, 0.27, 0.28), (-0.50, 0.27, 0.34), 0.04, DARK, verts=8)
    m.cyl((-0.515, 0.27, 0.35), (0.07, 0.07, 0.07), DARK, aim=(-0.8, 0, 0.6), taper=1.7, verts=10)
    for z in (0.0, 0.09, 0.18):
        m.vcyl((-0.41, 0.27, z), 0.04, 0.10, SHELL, verts=8)
    m.rod((-0.41, 0.27, 0.05), (-0.40, 0.20, 0.05), 0.012, DARK, verts=6)
    # spare tyre, tool roll and jerrycan strapped to the deck
    m.tor((-0.535, 0.0, -0.12), 0.13, 0.045, DARK, rot=(0, 90, 0), verts=20)
    m.cyl((0.10, 0.26, -0.055), (0.32, 0.09, 0.09), SHELL, verts=10)
    for x in (-0.02, 0.10, 0.22):
        m.cyl((x, 0.26, -0.055), (0.025, 0.10, 0.10), DARK, verts=10)
    m.box((-0.11, -0.25, -0.03), (0.14, 0.10, 0.17), SHELL, bevel=0.02, rot=(0, 0, -8))
    m.vcyl((-0.075, -0.25, 0.065), 0.03, 0.04, DARK, verts=8)
    # Team flag on a rear pole, trailing behind and rolled toward the camera
    m.rod((-0.40, -0.22, -0.12), (-0.40, -0.22, 0.42), 0.02, DARK, verts=6)
    m.sph((-0.40, -0.22, 0.435), (0.045, 0.045, 0.045), SHELL, verts=8)
    m.box((-0.53, -0.22, 0.33), (0.26, 0.02, 0.18), TEAM, rot=(40, 0, 0))
    # giant bolt-cutter jaws pointing +X: a long open V of blades, the opening plane rolled 50 degrees
    # off vertical so the V reads in the side view (vertical spread) and from the RTS camera
    # (lateral spread). Heavy pivot bolt, crossed handles running back to the cab.
    roll, half_open, handle_open = math.radians(50.0), math.radians(34.0), math.radians(8.0)
    spread = Vector((0.0, math.sin(roll), math.cos(roll)))     # jaw opening axis
    normal = Vector((1.0, 0.0, 0.0)).cross(spread)             # jaw-plane normal (bolt axis)
    pivot = Vector((-0.04, 0.0, 0.24))

    def frame_rot(d):
        z = d.cross(normal)
        basis = Matrix(((d.x, normal.x, z.x), (d.y, normal.y, z.y), (d.z, normal.z, z.z)))
        return tuple(math.degrees(a) for a in basis.to_euler("XYZ"))

    # support gantry from the chassis up to the pivot
    for s in (1, -1):
        m.rod((0.0, 0.10 * s, -0.12), (-0.04, 0.07 * s, 0.20), 0.035, DARK, verts=8)
    m.box((0.02, 0.0, -0.10), (0.26, 0.30, 0.05), DARK)
    for s in (1, -1):
        d = (Vector((1.0, 0.0, 0.0)) * math.cos(half_open) + spread * (s * math.sin(half_open)))
        rot = frame_rot(d)
        # blade: broad slab heel and body, chisel tip, inner cutting edge in Dark
        m.box(pivot + d * 0.10, (0.24, 0.28, 0.16), SHELL, rot=rot, bevel=0.03)
        for face_name in ("+y", "-y"):
            f, sz = m.face(pivot + d * 0.10, (0.24, 0.28, 0.16), face_name, rot=rot)
            f.rivets(sz, nu=2, nv=2, inset=0.03, r=0.012)
        m.box(pivot + d * 0.32, (0.50, 0.25, 0.10), SHELL, rot=rot, bevel=0.015)
        m.cyl(pivot + d * 0.67, (0.26, 0.32, 0.14), SHELL, rot=rot, taper=0.12, verts=4)
        m.box(pivot + d * 0.38 - spread * (s * 0.06), (0.66, 0.06, 0.04), DARK, rot=rot)
        m.box(pivot + d * 0.64 - spread * (s * 0.06), (0.16, 0.10, 0.03), DARK, rot=rot)
        # crossed handle: back and to the opposite side of the pivot, with a grip sleeve
        h = Vector((-math.cos(handle_open), 0.0, 0.0)) - spread * (s * math.sin(handle_open))
        end = pivot + h * 0.52
        m.rod(pivot, end, 0.032, SHELL, verts=8)
        m.rod(pivot + h * 0.30, end, 0.05, DARK, verts=8)
    m.rod((-0.36, 0.06, 0.16), pivot + Vector((-0.32, 0.0, 0.0)), 0.025, DARK, verts=6)
    # pivot bolt: fat shaft along the jaw normal, hex head and nut, glowing wear ring
    m.cyl(pivot, (0.34, 0.11, 0.11), DARK, aim=normal, verts=16)
    m.cyl(pivot + normal * 0.17, (0.06, 0.20, 0.20), SHELL, aim=normal, verts=6)
    m.cyl(pivot - normal * 0.17, (0.06, 0.20, 0.20), SHELL, aim=-normal, verts=6)
    m.cyl(pivot + normal * 0.205, (0.02, 0.07, 0.07), GLOW, aim=normal, verts=10)
    return m.finish()


# --------------------------------------------------------------------------------------
# HQs (3 x 3 x 2 m, base at z = -1.0)
# --------------------------------------------------------------------------------------
def human_hq(mats):
    m = Model("SM_Human_HQ", mats)
    rng = random.Random(6000)
    m.box((0.0, 0.0, -0.95), (2.96, 2.96, 0.10), DARK, bevel=0.02)
    # poured concrete panels with dark expansion joints, roof slab with parapet, corrugated sheet, Team paint
    m.seams((-0.25, 0.0, -0.35), (1.90, 2.0, 1.10), SHELL, ((0, 0.36), (0, 0.68), (2, 0.5)), gap=0.03,
            depth=0.03, bevel=0.025, core=DARK)
    m.box((-0.25, 0.0, 0.29), (2.10, 2.15, 0.18), SHELL, bevel=0.04, seg=1)
    m.box((-0.25, 0.0, 0.395), (1.9, 1.95, 0.03), DARK)
    m.box((0.05, 0.0, 0.405), (0.36, 1.95, 0.035), TEAM)
    for i in range(15):
        m.box((-0.72, -0.84 + 0.12 * i, 0.417), (0.90, 0.05, 0.02), SHELL)
    m.box((-1.285, 0.0, 0.44), (0.05, 2.15, 0.08), SHELL, bevel=0.01)
    m.box((-1.285, 0.0, 0.483), (0.03, 2.05, 0.008), TEAM)
    for s in (1, -1):
        m.box((-0.25, 1.05 * s, 0.44), (2.05, 0.05, 0.08), SHELL, bevel=0.01)
        m.box((-0.25, 1.05 * s, 0.483), (2.0, 0.03, 0.008), TEAM)
    # door with frame, cross bars, rivets and a lit viewport slot; lintel, Team paint band above it
    m.box((0.68, 0.0, -0.45), (0.10, 0.70, 0.85), DARK)
    for s in (1, -1):
        m.box((0.725, 0.39 * s, -0.45), (0.07, 0.06, 0.90), SHELL, bevel=0.01)
    m.box((0.72, 0.0, -0.00), (0.08, 0.90, 0.05), SHELL, bevel=0.01)
    m.box((0.715, 0.0, 0.07), (0.05, 1.5, 0.14), TEAM)
    for z in (-0.28, -0.62):
        m.box((0.745, 0.0, z), (0.03, 0.62, 0.05), SHELL, bevel=0.008)
    Frame.surface(m, (0.73, 0.0, -0.45), (1, 0, 0), (0, 0, 1)).rivets((0.8, 0.62), nu=4, nv=2, inset=0.04,
                                                                        mat=SHELL, r=0.012)
    m.box((0.735, 0.0, -0.10), (0.02, 0.30, 0.05), GLOW)
    m.box((0.86, 0.0, -0.875), (0.28, 0.9, 0.05), SHELL, bevel=0.01)
    # bolted patch plates, door posts with rivets, shuttered side windows
    m.plate((0.72, 0.78, -0.35), (1, -0.087, 0), (0, 0, 1), (0.45, 0.50), 0.04, DARK, bevel=0.01,
            rivets=(3, 3), rivet_mat=SHELL)
    m.plate((0.72, -0.80, -0.30), (1, 0.05, -0.07), (0, 0, 1), (0.40, 0.42), 0.04, SHELL, bevel=0.01,
            rivets=(3, 3))
    for s in (1, -1):
        m.box((0.72, 0.98 * s, -0.35), (0.20, 0.20, 1.10), SHELL, bevel=0.02)
        f, sz = m.face((0.72, 0.98 * s, -0.35), (0.20, 0.20, 1.10), "+x")
        f.rivets(sz, nu=5, nv=2, inset=0.035)
    for x in (0.2, -0.6):
        for s in (1, -1):
            m.box((x, 1.005 * s, -0.15), (0.50, 0.05, 0.07), DARK)
            m.box((x, 1.014 * s, -0.15), (0.42, 0.02, 0.02), GLOW)
            m.box((x, 1.03 * s, -0.085), (0.56, 0.04, 0.035), SHELL, bevel=0.008)
    m.box((-0.25, 0.0, -0.55), (1.94, 2.04, 0.06), DARK)
    # conduits and downpipes along the walls
    for s in (1, -1):
        m.rod((-1.10, 1.03 * s, -0.42), (0.55, 1.03 * s, -0.42), 0.03, DARK, verts=8)
        for x in (-0.9, -0.3, 0.3):
            m.box((x, 1.02 * s, -0.42), (0.05, 0.04, 0.09), SHELL, bevel=0.006)
        m.rod((-1.13, 1.04 * s, -0.90), (-1.13, 1.04 * s, 0.30), 0.035, DARK, verts=8)
    # back wall: ladder, generator with vented cover and a tall exhaust stack with rust bands
    for y in (0.10, 0.28):
        m.rod((-1.235, y, -0.90), (-1.235, y, 0.15), 0.02, DARK, verts=6)
    for i in range(9):
        m.rod((-1.235, 0.10, -0.85 + 0.11 * i), (-1.235, 0.28, -0.85 + 0.11 * i), 0.012, DARK, verts=6)
    m.box((-1.34, -0.25, -0.74), (0.24, 0.62, 0.34), DARK, bevel=0.02)
    Frame.surface(m, (-1.46, -0.25, -0.74), (-1, 0, 0), (0, 1, 0)).vent((0, 0, 0), (0.46, 0.24), slats=4)
    m.box((-1.34, -0.25, -0.545), (0.20, 0.50, 0.05), SHELL, bevel=0.01)
    m.box((-1.34, 0.065, -0.72), (0.05, 0.012, 0.05), GLOW)
    m.rod((-1.37, -0.42, -0.52), (-1.37, -0.42, 0.58), 0.04, DARK, verts=8)
    for z in (-0.30, 0.0, 0.30):
        m.vcyl((-1.37, -0.42, z), 0.04, 0.10, SHELL, verts=8)
    for z in (-0.10, 0.30):
        m.rod((-1.37, -0.42, z), (-1.2, -0.42, z), 0.012, DARK, verts=6)
    m.cyl((-1.37, -0.42, 0.62), (0.07, 0.06, 0.06), DARK, aim=(0, 0, 1), taper=1.7, verts=10)
    # roof: scrap panel on posts, vent stack, water tank, AC unit, hatch
    m.box((-0.55, -0.20, 0.50), (0.80, 0.50, 0.04), SHELL, rot=(6, -4, 12))
    for x, y in ((-0.85, -0.05), (-0.25, -0.35)):
        m.rod((x, y, 0.41), (x, y, 0.49), 0.02, DARK, verts=6)
    m.box((-0.80, -0.55, 0.47), (0.30, 0.30, 0.14), DARK, rot=(0, 0, 8), bevel=0.02)
    m.rod((-0.80, -0.55, 0.50), (-0.80, -0.55, 0.72), 0.03, DARK, verts=6)
    m.vcyl((-1.0, 0.55, 0.62), 0.40, 0.42, SHELL, verts=14, bevel=0.01)
    for z in (0.50, 0.74):
        m.vcyl((-1.0, 0.55, z), 0.03, 0.435, DARK, verts=14)
    m.vcyl((-1.0, 0.55, 0.83), 0.03, 0.16, DARK, verts=10)
    m.box((-0.30, -0.62, 0.50), (0.42, 0.34, 0.18), DARK, bevel=0.02)
    m.vcyl((-0.30, -0.62, 0.603), 0.02, 0.26, SHELL, verts=16)
    m.box((-0.30, -0.62, 0.618), (0.24, 0.03, 0.008), DARK)
    m.box((-0.30, -0.62, 0.618), (0.03, 0.24, 0.008), DARK)
    m.box((-0.42, 0.0, 0.44), (0.34, 0.34, 0.05), DARK, bevel=0.01)
    m.box((-0.42, 0.0, 0.47), (0.16, 0.03, 0.02), SHELL)
    # amber lamps on posts with hoods
    for s in (1, -1):
        m.rod((0.84, 0.58 * s, -0.90), (0.84, 0.58 * s, -0.15), 0.03, DARK, verts=6)
        m.box((0.78, 0.58 * s, -0.20), (0.12, 0.05, 0.05), DARK)
        m.sph((0.84, 0.58 * s, -0.16), (0.13, 0.13, 0.13), GLOW, verts=10)
        m.vcyl((0.84, 0.58 * s, -0.105), 0.03, 0.16, DARK, verts=10)
        m.rod((0.70, 0.95 * s, 0.40), (0.70, 0.95 * s, 0.55), 0.025, DARK, verts=6)
        m.sph((0.70, 0.95 * s, 0.58), (0.11, 0.11, 0.11), GLOW, verts=10)
        m.vcyl((0.70, 0.95 * s, 0.63), 0.03, 0.14, DARK, verts=10)
    # sandbags: front wings and side walls
    def bag(x, y, z, along):
        dims = (0.42, 0.30, 0.17) if along == "x" else (0.30, 0.42, 0.17)
        m.box((x + rng.uniform(-0.015, 0.015), y + rng.uniform(-0.015, 0.015), z), dims, SHELL,
              rot=(0, 0, rng.uniform(-7, 7)), bevel=0.055, seg=2)

    for s in (1, -1):
        for z, ys in ((-0.915, (0.62, 1.02, 1.26)), (-0.745, (0.82, 1.22)), (-0.575, (0.62, 1.02))):
            for y in ys:
                bag(1.28, y * s, z, "y")
        for z, xs in ((-0.915, (0.95, 0.53, 0.11, -0.31, -0.73, -1.15)),
                      (-0.745, (0.74, 0.32, -0.10, -0.52, -0.94))):
            for x in xs:
                bag(x, 1.27 * s, z, "x")
    # antenna mast with guys, dish, beacon, banner pole
    m.box((-0.60, 0.55, 0.42), (0.20, 0.20, 0.03), DARK)
    m.rod((-0.60, 0.55, 0.41), (-0.60, 0.55, 0.90), 0.045, DARK, verts=8)
    m.rod((-0.60, 0.55, 0.90), (-0.48, 0.60, 0.965), 0.025, DARK, verts=6)
    m.sph((-0.48, 0.60, 0.965), (0.07, 0.07, 0.07), GLOW, verts=8)
    m.box((-0.60, 0.55, 0.80), (0.03, 0.55, 0.03), DARK, rot=(0, 0, 9))
    m.rod((-0.60, 0.55, 0.78), (-1.05, 0.95, 0.41), 0.01, DARK, verts=4)
    m.rod((-0.60, 0.55, 0.78), (-0.15, 0.20, 0.41), 0.01, DARK, verts=4)
    m.cyl((-0.52, 0.72, 0.68), (0.06, 0.08, 0.08), SHELL, aim=(1, 0.5, 0.3), taper=4.0, verts=12)
    m.rod((0.55, -0.85, 0.41), (0.55, -0.85, 0.97), 0.025, DARK, verts=6)
    m.rod((0.55, -0.85, 0.955), (0.55, -0.20, 0.955), 0.015, DARK, verts=6)
    m.box((0.55, -0.52, 0.72), (0.03, 0.58, 0.44), TEAM, rot=(0, -15, 0), bevel=0.006)
    # clutter: treaded tyre stack and two rust barrels at the back corners
    for z in (-0.90, -0.71):
        m.tor((-1.15, -1.10, z), 0.22, 0.10, DARK, verts=20)
        for k in range(14):
            a = 2.0 * math.pi * k / 14
            radial = Vector((math.cos(a), math.sin(a), 0.0))
            Frame.surface(m, Vector((-1.15, -1.10, z)) + radial * 0.322, radial, (0, 0, 1)).box(
                (0, 0, 0), (0.12, 0.08, 0.035), DARK, rot=(0, 0, 18 if k % 2 else -18))
    for by in (1.05, 0.68):
        m.vcyl((-1.30, by, -0.78), 0.44, 0.34, SHELL, verts=14, bevel=0.015)
        for z in (-0.94, -0.78, -0.62):
            m.vcyl((-1.30, by, z), 0.03, 0.36, DARK, verts=14)
        m.vcyl((-1.30, by, -0.555), 0.02, 0.30, DARK, verts=14)
    return m.finish()


def machine_hq(mats):
    m = Model("SM_Machine_HQ", mats)
    m.box((0.0, 0.0, -0.94), (2.94, 2.94, 0.12), DARK, bevel=0.02)
    for s in (1, -1):  # lit floor track around the plinth
        m.box((1.42 * s, 0.0, -0.878), (0.02, 2.84, 0.012), GLOW)
        m.box((0.0, 1.42 * s, -0.878), (2.84, 0.02, 0.012), GLOW)
    # three tiers, each a grid of tiles with Glow joints
    slabs = ((2.70, -0.78, 0.20), (2.50, -0.54, 0.22), (2.30, -0.29, 0.22))
    for dim, z, h in slabs:
        m.seams((0.0, 0.0, z), (dim, dim, h), SHELL, ((0, 0.30), (0, 0.70), (1, 0.30), (1, 0.70)), gap=0.02,
                depth=0.02, bevel=0.03, seg=2)
    # core with two Glow seam rings, roof plate with heat-sink fins and corner beacons
    m.seams((0.0, 0.0, 0.375), (1.56, 1.56, 1.05), SHELL, ((2, 0.12), (2, 0.65)), gap=0.024, depth=0.02,
            bevel=0.035, seg=2)
    m.box((0.0, 0.0, 0.965), (1.36, 1.36, 0.07), SHELL, bevel=0.02)
    for dim, z in ((2.56, -0.665), (2.36, -0.415), (1.62, -0.165), (1.42, 0.915)):
        m.box((0.0, 0.0, z), (dim, dim, 0.03), GLOW)
    for i in range(7):
        m.box((-0.50 + 0.1667 * i, 0.0, 1.03), (0.05, 1.10, 0.06), SHELL, bevel=0.008)
    for sx in (1, -1):
        for sy in (1, -1):
            m.sph((0.62 * sx, 0.62 * sy, 1.03), (0.06, 0.06, 0.06), GLOW, verts=8)
    # server-rack vents with slats and LEDs on the two big tiers, all four faces
    for k in range(4):
        cs, sn = (round(math.cos(math.radians(90 * k))), round(math.sin(math.radians(90 * k))))
        n, t = Vector((cs, sn, 0.0)), Vector((-sn, cs, 0.0))
        for half, z in ((1.25, -0.54), (1.15, -0.29)):
            for y in (-0.75, -0.25, 0.25, 0.75):
                f = Frame.surface(m, n * half + t * y + Vector((0, 0, z)), n, t)
                f.vent((0, 0, 0), (0.30, 0.10), slats=3)
                f.box((0.115, 0, 0.012), (0.05, 0.03, 0.014), GLOW)
    # Team status strips on the top tier, facing the lens
    for s in (1, -1):
        m.box((1.0, 0.55 * s, -0.173), (0.10, 0.42, 0.014), TEAM)
    # core: corner seams, side slits; corner pylons with glow slits, spikes and cables to the core
    for sx in (1, -1):
        for sy in (1, -1):
            m.box((0.78 * sx, 0.78 * sy, 0.375), (0.05, 0.05, 0.98), GLOW)
            m.box((1.28 * sx, 1.28 * sy, -0.405), (0.24, 0.24, 0.55), SHELL, bevel=0.03)
            m.box((1.28 * sx, 1.28 * sy, -0.66), (0.28, 0.28, 0.05), DARK, bevel=0.01)
            m.box((1.28 * sx, 1.28 * sy, -0.115), (0.18, 0.18, 0.03), GLOW)
            m.box((1.16 * sx, 1.28 * sy, -0.40), (0.012, 0.10, 0.34), GLOW)
            m.box((1.28 * sx, 1.16 * sy, -0.40), (0.10, 0.012, 0.34), GLOW)
            m.rod((1.28 * sx, 1.28 * sy, -0.10), (1.28 * sx, 1.28 * sy, 0.14), 0.035, SHELL, r1=0.012, verts=8)
            m.sph((1.28 * sx, 1.28 * sy, 0.15), (0.06, 0.06, 0.06), GLOW, verts=8)
            m.cable((1.20 * sx, 1.20 * sy, -0.10), (0.95 * sx, 0.95 * sy, 0.30), (0.80 * sx, 0.80 * sy, 0.05),
                    0.02)
    for x in (-0.6, -0.4, -0.2, 0.0, 0.2, 0.4, 0.6):
        for sy in (1, -1):
            m.box((x, 0.78 * sy, 0.325), (0.06, 0.03, 0.32), DARK)
    # huge lens: dark bezel, Glow rim ring, collar with clamp blocks, lens eye
    m.cyl((0.705, 0.0, 0.34), (0.16, 1.10, 1.10), DARK, verts=32)
    m.tor((0.777, 0.0, 0.34), 0.52, 0.012, GLOW, aim=(1, 0, 0), verts=40)
    m.lens((0.62, 0.0, 0.34), 0.46, aim=(1, 0, 0.45), clamps=8, collar=1.45, ring_verts=40)
    # spire with seam rings, sensor disc and antenna whips
    m.vcyl((0.0, 0.0, 1.39), 0.78, 0.18, SHELL, taper=0.2, verts=12)
    for z, r in ((1.18, 0.083), (1.42, 0.056)):
        m.tor((0.0, 0.0, z), r, 0.012, GLOW, verts=16)
    m.vcyl((0.0, 0.0, 1.10), 0.025, 0.34, SHELL, verts=16)
    m.tor((0.0, 0.0, 1.10), 0.17, 0.008, GLOW, verts=24)
    for k in range(3):
        c, s = math.cos(math.radians(120 * k + 30)), math.sin(math.radians(120 * k + 30))
        m.rod((0.10 * c, 0.10 * s, 1.03), (0.16 * c, 0.16 * s, 1.32), 0.012, DARK, verts=6)
        m.sph((0.16 * c, 0.16 * s, 1.33), (0.035, 0.035, 0.035), GLOW, verts=6)
    m.sph((0.0, 0.0, 1.765), (0.07, 0.07, 0.07), GLOW, verts=10)
    return m.finish()


MODELS = (
    ("Machine", machine_frontline), ("Machine", machine_ranged), ("Machine", machine_siege),
    ("Machine", machine_hq),
    ("Human", human_frontline), ("Human", human_ranged), ("Human", human_siege), ("Human", human_hq),
)


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


def check(obj):
    name = obj.name
    used = {p.material_index for p in obj.data.polygons}
    assert used == {0, 1, 2, 3}, "%s uses slots %s" % (name, sorted(used))
    assert [s.material.name for s in obj.material_slots] == list(SLOT_NAMES), name
    budget = TRI_BUDGET_HQ if name.endswith("_HQ") else TRI_BUDGET_UNIT
    assert triangles(obj) <= budget, "%s has %d triangles (budget %d)" % (name, triangles(obj), budget)
    lo, hi = bounds(obj)
    if not name.endswith("_HQ"):
        limit = FOOTPRINT_SIEGE if "_Siege" in name else FOOTPRINT_UNIT
        assert max(-lo.x, -lo.y, hi.x, hi.y) <= limit, "%s footprint %s %s exceeds %.2f" % (name, lo, hi, limit)
    if name.endswith("_HQ"):
        assert abs(lo.z + 1.0) < 1e-4 and hi.z <= 1.0 + 1e-4 + (0.8 if "Machine" in name else 0.0), (name, lo, hi)
        assert max(abs(lo.x), abs(lo.y), hi.x, hi.y) <= 1.5 + 1e-4, (name, lo, hi)
    elif name.endswith("_Ranged") and "Machine" in name:
        assert lo.z >= -0.35, (name, lo)
    else:
        assert abs(lo.z + 0.60) < 1e-4, "%s does not touch the ground: %s" % (name, lo)


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
PREVIEW_COLUMNS = (0.0, 1.8, 3.6, 6.2)  # x of each column; HQ column is wider
PREVIEW_SIZE = (2000, 1500)

# PreviewField.png: RTS-distance render on the Campus Zero floor (2000 px wide).
FIELD_SIZE = (2000, 1125)
CAMPUS_ASPHALT = (0.045, 0.048, 0.055)   # MI_Asphalt in Build/GenerateCampusZero.py
CAMPUS_SCRAP = (0.075, 0.06, 0.045)      # MI_ScrapGround in Build/GenerateCampusZero.py
FIELD_DISTANCE = 130.0                   # metres; a unit is about 40 px tall at 85 mm on a 36 mm sensor
FIELD_KEY_DIR = (-0.7, -0.4, -0.3)       # low warm key (about 20 degrees), light travels toward -X
FIELD_KEY_ENERGY = 4.0
FIELD_KEY_COLOR = (1.0, 0.66, 0.42)
FIELD_FILL_DIR = (0.6, 0.3, -0.6)
FIELD_FILL_ENERGY = 1.8
FIELD_FILL_COLOR = (0.5, 0.6, 1.0)
FIELD_SKY_COLOR = (0.05, 0.06, 0.10)
FIELD_SKY_STRENGTH = 3.5
# Unit positions (x, y) in metres. The scrapyard patch covers x < 0; each faction stands on both floors.
FIELD_LAYOUT = {
    "SM_Human_Frontline": (-10.0, -1.5), "SM_Machine_Frontline": (-6.0, 2.0), "SM_Human_Siege": (-2.5, -2.0),
    "SM_Machine_Ranged": (3.0, 1.5), "SM_Human_Ranged": (7.0, -2.0), "SM_Machine_Siege": (11.0, 2.0),
}


# Human surfaces mirror FACTION_MATERIALS["Human"] in Build/ImportUnitMeshes.py (colour, roughness,
# metallic) and HUMAN_GLOW in Build/ArtMaterials.py, so the previews predict the Unreal result.
# Change them together. Team is a stand-in for the per-team tint applied in game.
HUMAN_SHELL = ((0.46, 0.40, 0.26), 0.85, 0.05)
HUMAN_DARK = ((0.30, 0.31, 0.33), 0.55, 0.3)
HUMAN_GLOW_COLOR = (1.0, 0.55, 0.15)


def preview_materials():
    return {
        "Machine": [
            make_material("PV_M_Team", (1.0, 0.01, 0.01), emission=4.0),
            make_material("PV_M_Shell", (0.82, 0.85, 0.88), roughness=0.22, coat=0.6),
            make_material("PV_M_Dark", (0.035, 0.04, 0.05), metallic=0.4, roughness=0.4),
            make_material("PV_M_Glow", (0.45, 0.95, 1.0), emission=6.0),
        ],
        "Human": [
            make_material("PV_H_Team", (0.06, 0.25, 0.95), roughness=0.55),
            make_material("PV_H_Shell", HUMAN_SHELL[0], roughness=HUMAN_SHELL[1], metallic=HUMAN_SHELL[2]),
            make_material("PV_H_Dark", HUMAN_DARK[0], roughness=HUMAN_DARK[1], metallic=HUMAN_DARK[2]),
            make_material("PV_H_Glow", HUMAN_GLOW_COLOR, emission=7.0),
        ],
    }


class PreviewRig:
    """Preview scene: lit lineup of copies with per-faction preview materials (originals hidden)."""

    def __init__(self, objects):
        self.objects = objects
        scene = self.scene = bpy.context.scene
        pv = preview_materials()

        scene.render.engine = "CYCLES"
        scene.cycles.device = "CPU"
        scene.cycles.samples = 64
        scene.cycles.use_denoising = True
        scene.render.resolution_x, scene.render.resolution_y = PREVIEW_SIZE
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = "PNG"
        scene.view_settings.view_transform = "Standard"

        world = bpy.data.worlds.new("PV_World")
        scene.world = world
        world.use_nodes = True
        bg = world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = (0.012, 0.015, 0.02, 1.0)
        bg.inputs["Strength"].default_value = 1.0

        self.lights = {}

        def sun(direction, energy, color, name):
            data = bpy.data.lights.new(name, "SUN")
            data.energy = energy
            data.color = color
            data.angle = math.radians(6)
            obj = bpy.data.objects.new(name, data)
            obj.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
            scene.collection.objects.link(obj)
            self.lights[name] = obj

        self.sun = sun
        sun((0.5, 0.6, -0.8), 2.6, (1.0, 0.97, 0.92), "PV_Key")
        sun((-0.6, 0.3, -0.5), 0.9, (0.6, 0.75, 1.0), "PV_Fill")
        sun((0.1, -0.7, -0.5), 0.6, (0.8, 0.85, 1.0), "PV_Front")

        ground_mesh = bpy.data.meshes.new("PV_Ground")
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bm.to_mesh(ground_mesh)
        bm.free()
        ground_mesh.materials.append(make_material("PV_Ground", (0.05, 0.055, 0.065), roughness=0.9))
        self.grounds = []
        for name in ("PV_Ground_A", "PV_Ground_B"):
            ground = bpy.data.objects.new(name, ground_mesh)
            scene.collection.objects.link(ground)
            self.grounds.append(ground)

        self.props = {}
        for (faction, _), src in zip(MODELS, objects):
            copy = src.copy()
            scene.collection.objects.link(copy)
            src.hide_render = True
            copy.hide_render = False
            for slot, material in zip(copy.material_slots, pv[faction]):
                slot.link = "OBJECT"
                slot.material = material
            self.props[src.name] = copy

        self.cam_data = bpy.data.cameras.new("PV_Cam")
        self.cam = bpy.data.objects.new("PV_Cam", self.cam_data)
        scene.collection.objects.link(self.cam)
        scene.camera = self.cam

    def place(self, view):
        """Side view: rows stacked in Z. RTS view: rows separated in Y (Machine far, Human near)."""
        for index, (faction, _) in enumerate(MODELS):
            src = self.objects[index]
            row = 0 if faction == "Machine" else 1
            drop = 1.0 if src.name.endswith("_HQ") else 0.6
            x = PREVIEW_COLUMNS[index % 4]
            if view == "side":
                self.props[src.name].location = (x, 0.0, (3.7 if row == 0 else 0.0) + drop)
            else:
                self.props[src.name].location = (x, 1.9 if row == 0 else -1.9, drop)
        a, b = self.grounds
        if view == "side":
            a.scale = b.scale = (9.4, 4.0, 0.04)
            a.location = (3.3, 0.0, -0.02)
            b.location = (3.3, 0.0, 3.68)
            b.hide_render = False
        else:
            a.scale = (16.0, 12.0, 0.04)
            a.location = (3.5, 0.0, -0.02)
            b.hide_render = True

    def camera_side(self):
        self.cam_data.type = "ORTHO"
        self.cam_data.ortho_scale = 9.6
        self.cam.location = (3.3, -30.0, 3.35)
        self.cam.rotation_euler = (math.radians(90), 0, 0)

    def camera_rts(self, target=(3.5, -0.3, 0.2), distance=19.0, azimuth=25.0, lens=55):
        """50 degree pitch; azimuth rotates the camera around the target (0 = looking along +Y)."""
        self.cam_data.type = "PERSP"
        self.cam_data.lens = lens
        pitch = math.radians(50)
        offset = Vector((0.0, -math.cos(pitch), math.sin(pitch))) * distance
        offset.rotate(Euler((0, 0, math.radians(azimuth))))
        self.cam.location = Vector(target) + offset
        self.cam.rotation_euler = (math.radians(90 - 50), 0, math.radians(azimuth))

    def render(self, path):
        bpy.context.view_layer.update()
        self.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

    def render_field(self, path):
        """RTS-distance field render: every unit on Campus Zero ground under dusk light.

        Ground albedos are the values at the top of Build/GenerateCampusZero.py (MI_Asphalt and
        MI_ScrapGround). Camera: 50 degree pitch, 85 mm lens, far enough that a unit is about 40 px
        tall at 2000 px wide. Light: low warm key from the east (unit fronts, +X), cool fill and sky.
        """
        scene = self.scene
        scene.render.resolution_x, scene.render.resolution_y = FIELD_SIZE
        for light in self.lights.values():
            light.hide_render = True
        self.sun(FIELD_KEY_DIR, FIELD_KEY_ENERGY, FIELD_KEY_COLOR, "PV_DuskKey")
        self.sun(FIELD_FILL_DIR, FIELD_FILL_ENERGY, FIELD_FILL_COLOR, "PV_DuskFill")
        bg = scene.world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = (*FIELD_SKY_COLOR, 1.0)
        bg.inputs["Strength"].default_value = FIELD_SKY_STRENGTH

        for index, (faction, _) in enumerate(MODELS):
            src = self.objects[index]
            prop = self.props[src.name]
            if src.name in FIELD_LAYOUT:
                prop.hide_render = False
                prop.location = (*FIELD_LAYOUT[src.name], 0.6)
            else:
                prop.hide_render = True
        asphalt, scrap = self.grounds
        for ground, name, color, roughness in ((asphalt, "PV_Asphalt", CAMPUS_ASPHALT, 0.9),
                                               (scrap, "PV_ScrapGround", CAMPUS_SCRAP, 0.95)):
            ground.material_slots[0].link = "OBJECT"
            ground.material_slots[0].material = make_material(name, color, roughness=roughness)
            ground.hide_render = False
        asphalt.scale = (600.0, 600.0, 0.04)
        asphalt.location = (0.0, 0.0, -0.02)
        scrap.scale = (30.0, 40.0, 0.04)
        scrap.location = (-15.0, 0.0, -0.018)

        self.cam_data.clip_end = 2000.0
        self.camera_rts(target=(0.0, 0.0, 0.3), distance=FIELD_DISTANCE, azimuth=25.0, lens=85)
        self.render(path)


def render_previews(objects):
    rig = PreviewRig(objects)
    rig.place("side")
    rig.camera_side()
    rig.render(os.path.join(OUT, "Preview.png"))
    rig.place("rts")
    rig.camera_rts()
    rig.render(os.path.join(OUT, "PreviewRTS.png"))
    rig.render_field(os.path.join(OUT, "PreviewField.png"))


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = canonical_materials()

    objects = []
    for _, build in MODELS:
        obj = build(mats)
        check(obj)
        objects.append(obj)

    for obj in objects:
        export_fbx(obj, os.path.join(OUT, obj.name + ".fbx"))

    for index, obj in enumerate(objects):
        obj.location = (index * 4.0, 0.0, 0.0)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "Units.blend"))

    print("MESH | bounds min | bounds max | tris | slots")
    for obj in objects:
        lo, hi = bounds(obj)
        print("%s | (%.3f, %.3f, %.3f) | (%.3f, %.3f, %.3f) | %d | %s" % (
            obj.name, *lo, *hi, triangles(obj), "/".join(s.material.name for s in obj.material_slots)))
    sys.stdout.flush()

    for obj in objects:
        obj.location = (0.0, 0.0, 0.0)
    render_previews(objects)
    print("UNIT_MESHES_DONE")


main()
