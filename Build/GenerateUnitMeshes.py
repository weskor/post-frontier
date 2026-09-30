"""Generate the unit and HQ meshes for CoopRTS in Blender (v3, StarCraft 2-style stylized sci-fi).

Run headless from the repo root (Blender 5.2.1, no assets, no network, deterministic):

    blender -b --factory-startup -P Build/GenerateUnitMeshes.py

Art direction is Saved/AgentBriefs/sc2-style.md and Docs/World.md ("The Offline" = blue-collar sci-fi,
"The Machine" = elegant pearl-white AI). Every mesh is built from bmesh primitives plus Bevel modifiers
(applied) and joined into one object, then given smooth shading with weighted normals. Chamfered armour
plates come from Model.armor (a box with a scaled / slid top face), panel breaks from Model.seams, hazard
tape and lit windows from Model.hazard / Model.lamp, lenses from Model.lens. Units are metres, +Z up,
forward = +X. Ground units are centred on a capsule of radius 0.34 m and half-height 0.60 m (ground
contact at z = -0.60; the Machine Ranged drone hovers with its lowest point at z >= -0.35). HQs are
3.0 x 3.0 m footprints centred on the origin (base at z = -1.0, top at most z = 1.8). Budgets, asserted in
check(): at most 15000 triangles per unit and 35000 per HQ; unit footprints stay inside +/-0.47 m
(Siege +/-0.67 m) in X and Y.

The eight meshes
    Machine (team 5): SOL 6000 (Frontline: pearl sentinel, floating shoulder shells, red lens), Autocomplete
        Drone (Ranged: hovering pod, floating halo, cyan core), Hallucinator (Siege: quadruped with an
        energy-prism lance), The Cluster (HQ: floating-segment monolith around one huge red lens).
    Human (The Offline): Luddite (Frontline: power armour, riot shield, hydraulic sledgehammer), Offline
        Ranger (Ranged: light armour, rail rifle, sensor backpack), Unplugger (Siege: mech walker with giant
        bolt-cutter jaws), The Bunker (HQ: prefab command post on landing struts, blast doors, reactor stack).

Outputs (relative to the repo root)
    Art/Units/SM_<Name>.fbx   one mesh object named SM_<Name> per file, exported at the origin
    Art/Units/Units.blend     the same eight meshes in a row along +X for editing, wearing the preview
                              palette as object-level material overrides (NOT at the origin; always export
                              from this script, never from the .blend). Saved on every run.
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
    0 Team   player colour: big painted plates (Human: pauldrons, pack lids, shield faces, roof plates,
             door canopy, barrier caps; Machine: the red lens dome plus inlays on shells and crowns)
    1 Shell  faction body (Human gunmetal / steel-blue, Machine pearl white)
    2 Dark   joints, weapons, underside, panel-line beds
    3 Glow   emissive accents (Human amber windows, exhausts and hazard-tape bars; Machine cyan seams and cores)
The materials carry neutral colours; the preview renders swap in per-faction colours per object
(Machine: red lens / pearl shell / cyan glow; Human: blue team paint / steel-blue shell / amber glow).
Human surface values are the HUMAN_* constants below and must equal FACTION_MATERIALS["Human"] in
Build/ImportUnitMeshes.py; the MACHINE_* constants likewise for FACTION_MATERIALS["Machine"].

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
OUT = os.path.join(ROOT, "Art", "Units")

TEAM, SHELL, DARK, GLOW = range(4)
SLOT_NAMES = ("Team", "Shell", "Dark", "Glow")
FBX_AXES = dict(axis_forward="Y", axis_up="Z")
TRI_BUDGET_UNIT = 15000   # triangles per unit mesh
TRI_BUDGET_HQ = 35000     # triangles per HQ mesh
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
            mirror=False, verts=16, taper=1.0, skew=(0.0, 0.0)):
        for flip in ((False, True) if mirror else (False,)):
            self._make(kind, loc, dims, mat, rot, aim, bevel, seg, flip, verts, taper, skew)

    def _make(self, kind, loc, dims, mat, rot, aim, bevel, seg, flip, verts, taper, skew):
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
        elif kind == "frust":  # box whose +Z face is scaled by taper (x, y) and slid by skew (cube fractions)
            bmesh.ops.create_cube(bm, size=1.0)
            tx, ty = taper if isinstance(taper, (tuple, list)) else (taper, taper)
            for v in bm.verts:
                if v.co.z > 0.0:
                    v.co.x = v.co.x * tx + skew[0]
                    v.co.y = v.co.y * ty + skew[1]
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
            mod.angle_limit = math.radians(30.0)
            bpy.context.view_layer.objects.active = obj
            with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
                bpy.ops.object.modifier_apply(modifier=mod.name)

    # -- shape helpers --------------------------------------------------------------
    def box(self, loc, dims, mat, **kw):
        self.add("box", loc, dims, mat, **kw)

    def armor(self, loc, dims, mat=SHELL, top=(0.8, 0.8), skew=(0.0, 0.0), bevel=0.03, seg=3, **kw):
        """Chamfered armour plate: a box whose top face is scaled by `top` (x, y) and slid by `skew`."""
        self.add("frust", loc, dims, mat, taper=top, skew=skew, bevel=bevel, seg=seg, **kw)

    def hazard(self, loc, normal, along, length, height, n=5, mat=GLOW, **kw):
        """Small hazard-tape patch on a surface: dark bed with slanted lit bars (amber Glow = hazard yellow)."""
        f = Frame.surface(self, loc, normal, along)
        f.box((0, 0, 0), (length, height, 0.012), DARK, **kw)
        lean = math.radians(32)
        margin = 0.5 * height * math.tan(lean)  # slanted bars must not overhang the bed
        span = max(0.0, length - 2 * margin)
        pitch = span / max(1, n - 1)
        for i in range(n):
            f.box((-span / 2 + pitch * i, 0, 0.006), (max(0.012, pitch * 0.4), height / math.cos(lean) * 0.98, 0.012),
                  mat, rot=(0, 0, math.degrees(lean)), **kw)

    def lamp(self, loc, normal, along, size, depth=0.05, mat=GLOW, **kw):
        """Lit window or floodlight lens: dark hood block with a Glow face; size = (along, across)."""
        f = Frame.surface(self, loc, normal, along)
        f.box((0, 0, depth / 2), (size[0] * 1.12, size[1] * 1.12, depth), DARK, bevel=min(size) * 0.12, **kw)
        f.box((0, 0, depth), (size[0] * 0.86, size[1] * 0.86, 0.012), mat, **kw)

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
        # weighted normals: big faces dominate the vertex normal, so chamfers stay crisp and panels stay flat
        mod = obj.modifiers.new("WeightedNormal", "WEIGHTED_NORMAL")
        mod.mode = "FACE_AREA"
        mod.weight = 60
        mod.keep_sharp = True
        with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        return obj


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

    def armor(self, off, dims, mat=SHELL, top=(0.8, 0.8), skew=(0.0, 0.0), bevel=0.03, seg=3, rot=None, **kw):
        R = self.R if rot is None else self.R @ Euler([math.radians(v) for v in rot], "XYZ").to_matrix()
        self.m.add("frust", self.p(off), dims, mat, rot=R, taper=top, skew=skew, bevel=bevel, seg=seg, **kw)

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
# Machine units: symmetric pearl-white shells, floating segments, cyan seams, one red lens
# --------------------------------------------------------------------------------------
def machine_frontline(mats):
    m = Model("SM_Machine_Frontline", mats)
    # slender legs: pointed foot, ankle ball, pearl shin spindle with floating greave blade, ringed knee, thigh shell
    for s in (1, -1):
        y = 0.13 * s
        m.armor((0.05, y, -0.555), (0.34, 0.15, 0.09), SHELL, top=(0.5, 0.8), skew=(-0.16, 0.0), bevel=0.035)
        m.sph((-0.04, y, -0.49), (0.11, 0.11, 0.11), DARK, verts=12)
        m.sph((-0.03, y, -0.39), (0.15, 0.13, 0.25), SHELL, verts=20)
        m.armor((0.085, y, -0.37), (0.05, 0.09, 0.20), SHELL, top=(0.35, 0.7), skew=(-0.2, 0.0), bevel=0.012, seg=2)
        m.sph((0.0, y, -0.255), (0.11, 0.11, 0.11), DARK, verts=12)
        m.tor((0.0, y, -0.255), 0.07, 0.009, GLOW, aim=(0, 1, 0), verts=20)
        m.sph((0.01, y, -0.125), (0.18, 0.15, 0.24), SHELL, verts=20)
        m.box((0.09, y, -0.125), (0.008, 0.02, 0.15), GLOW)
    # floating pelvis, waist ring, tapered chest with a cyan core, floating chest plate
    m.armor((0.0, 0.0, 0.03), (0.26, 0.32, 0.09), SHELL, top=(1.1, 1.0), bevel=0.035)
    m.vcyl((0.0, 0.0, 0.095), 0.05, 0.14, DARK)
    m.tor((0.0, 0.0, 0.095), 0.078, 0.009, GLOW, verts=24)
    m.armor((0.02, 0.0, 0.245), (0.30, 0.32, 0.26), SHELL, top=(1.05, 1.5), bevel=0.06)
    m.armor((0.20, 0.0, 0.25), (0.06, 0.30, 0.20), SHELL, top=(0.4, 0.8), bevel=0.02, seg=3)
    m.tor((0.185, 0.0, 0.24), 0.062, 0.012, DARK, aim=(1, 0, 0), verts=20)
    m.sph((0.19, 0.0, 0.24), (0.09, 0.09, 0.09), GLOW, verts=12)
    for s in (1, -1):
        m.box((0.10, 0.16 * s, 0.33), (0.10, 0.008, 0.012), GLOW)
    # head: elongated pearl helm with the single red lens, swept crest, halo ring
    m.vcyl((0.0, 0.0, 0.395), 0.05, 0.08, DARK)
    m.sph((0.02, 0.0, 0.48), (0.26, 0.19, 0.17), SHELL, verts=20)
    m.lens((0.115, 0.0, 0.48), 0.082, aim=(1, 0, 0), clamps=4)
    m.armor((-0.06, 0.0, 0.575), (0.30, 0.03, 0.06), SHELL, top=(0.35, 1.0), skew=(-0.25, 0.0), bevel=0.01, seg=2)
    m.tor((-0.14, 0.0, 0.48), 0.15, 0.010, GLOW, aim=(1, 0, 0), verts=32)
    # big floating shoulder shells: red inlays, glowing joints, swept crystal blades
    for s in (1, -1):
        m.sph((0.0, 0.27 * s, 0.27), (0.11, 0.11, 0.11), DARK, verts=12)
        m.tor((0.0, 0.285 * s, 0.31), 0.06, 0.008, GLOW, verts=20)
    f = Frame(m, (0.0, 0.29, 0.36), rot=(-22, 0, 0))
    f.sph((0, 0, 0), (0.40, 0.27, 0.15), SHELL, verts=24, mirror=True)
    f.sph((0, 0, -0.075), (0.28, 0.19, 0.06), SHELL, verts=16, mirror=True)
    f.armor((0.01, 0, 0.066), (0.24, 0.15, 0.03), TEAM, top=(0.7, 0.7), bevel=0.012, seg=2, mirror=True)
    m.armor((-0.10, 0.33, 0.47), (0.06, 0.06, 0.20), SHELL, top=(0.3, 0.5), rot=(-14, -12, 0), bevel=0.01, seg=2,
            mirror=True)
    # arms: upper sleeve, three floating forearm segments strung on a Glow core, hovering fist
    for s in (1, -1):
        a, e, h = Vector((0, 0.27 * s, 0.26)), Vector((0.02, 0.34 * s, 0.10)), Vector((0.16, 0.36 * s, -0.12))
        m.rod(a, e, 0.038, SHELL, verts=10)
        m.sph(e, (0.08, 0.08, 0.08), DARK, verts=10)
        m.rod(e, h, 0.016, GLOW, verts=8)
        for t, w in ((0.25, 0.09), (0.52, 0.085), (0.78, 0.075)):
            m.sph(e.lerp(h, t), (0.12, w, w), SHELL, aim=h - e, verts=14)
        m.armor(h + Vector((0.02, 0, -0.035)), (0.13, 0.11, 0.13), SHELL, top=(0.8, 0.8), bevel=0.03)
    # swept back fins and rear power crystal
    m.armor((-0.23, 0.11, 0.22), (0.04, 0.15, 0.44), SHELL, top=(0.5, 0.6), rot=(0, -22, 0), bevel=0.012, seg=2,
            mirror=True)
    m.box((-0.245, 0.11, 0.22), (0.012, 0.03, 0.30), GLOW, rot=(0, -22, 0), mirror=True)
    m.sph((-0.19, 0.0, 0.22), (0.10, 0.10, 0.10), GLOW, verts=10)
    return m.finish()


def machine_ranged(mats):
    m = Model("SM_Machine_Ranged", mats)
    # pearl pod hull with floating carapace plates, red status inlay and a dorsal fin
    m.sph((0.0, 0.0, 0.12), (0.60, 0.42, 0.32), SHELL, verts=28)
    m.armor((0.0, 0.0, 0.30), (0.36, 0.28, 0.04), SHELL, top=(0.85, 0.85), bevel=0.014, seg=3)
    m.armor((0.04, 0.0, 0.335), (0.22, 0.16, 0.02), TEAM, top=(0.85, 0.85), bevel=0.008, seg=2)
    m.armor((-0.06, 0.0, 0.41), (0.26, 0.03, 0.16), SHELL, top=(0.55, 1.0), skew=(-0.1, 0.0), bevel=0.012, seg=2)
    m.box((-0.06, 0.017, 0.42), (0.18, 0.006, 0.012), GLOW, mirror=True)
    m.rod((-0.12, 0.0, 0.48), (-0.15, 0.0, 0.63), 0.008, SHELL, verts=6)
    m.sph((-0.15, 0.0, 0.64), (0.035, 0.035, 0.035), GLOW, verts=8)
    # equatorial seam and the cyan belly core in a cradle with a visible gap
    m.tor((0.0, 0.0, 0.10), 0.262, 0.009, GLOW, verts=32)
    m.vcyl((0.0, 0.0, -0.065), 0.05, 0.24, DARK, taper=0.75, verts=20)
    m.sph((0.0, 0.0, -0.14), (0.22, 0.22, 0.22), GLOW, verts=16)
    # halo: eight floating pearl arcs around a lit inner ring, held off the hull
    m.tor((0.0, 0.0, 0.12), 0.335, 0.008, GLOW, verts=48)
    for k in range(8):
        a = math.radians(45 * k + 22.5)
        c, s = math.cos(a), math.sin(a)
        f = Frame.surface(m, Vector((0.375 * c, 0.375 * s, 0.12)), (c, s, 0), (-s, c, 0))
        f.armor((0, 0, 0), (0.27, 0.09, 0.05), SHELL, top=(0.95, 0.85), bevel=0.02, seg=3)
    # three underside thrusters set the hover height (lowest tip about -0.325)
    for k in range(3):
        a = math.radians(120 * k + 90)
        c, s = math.cos(a), math.sin(a)
        m.vcyl((0.17 * c, 0.17 * s, -0.15), 0.12, 0.10, DARK, taper=0.55, verts=14)
        m.vcyl((0.17 * c, 0.17 * s, -0.315), 0.02, 0.055, GLOW, verts=12)
    # red lens and a long crystal prism emitter between floating focus rings
    m.lens((0.25, 0.0, 0.16), 0.13, aim=(1, 0, 0.12), clamps=4)
    m.cyl((0.30, 0.0, 0.01), (0.32, 0.07, 0.07), GLOW, verts=6, taper=0.5)
    for x, r in ((0.19, 0.055), (0.29, 0.05), (0.38, 0.045)):
        m.tor((x, 0.0, 0.01), r + 0.012, 0.009, SHELL, aim=(1, 0, 0), verts=18)
    m.cyl((0.13, 0.0, 0.01), (0.06, 0.11, 0.11), DARK, verts=10)
    # rear exhaust and swept side blades
    m.cyl((-0.30, 0.0, 0.12), (0.06, 0.15, 0.15), DARK, verts=16)
    m.cyl((-0.335, 0.0, 0.12), (0.02, 0.11, 0.11), GLOW, verts=16)
    m.armor((-0.10, 0.27, 0.10), (0.26, 0.11, 0.025), SHELL, top=(0.8, 0.5), rot=(0, 0, -14), bevel=0.008, seg=2,
            mirror=True)
    return m.finish()


def machine_siege(mats):
    m = Model("SM_Machine_Siege", mats)
    # sleek pearl body, floating dorsal plates, glowing seam rings, red lens head
    m.sph((-0.02, 0.0, 0.10), (0.70, 0.34, 0.24), SHELL, verts=28)
    m.armor((-0.14, 0.0, 0.225), (0.36, 0.22, 0.045), SHELL, top=(0.85, 0.8), bevel=0.014, seg=3)
    m.armor((-0.18, 0.0, 0.258), (0.24, 0.16, 0.022), TEAM, top=(0.85, 0.85), bevel=0.008, seg=2)
    m.armor((0.10, 0.0, 0.245), (0.16, 0.17, 0.03), SHELL, top=(0.85, 0.85), bevel=0.012, seg=2)
    m.tor((0.0, 0.0, 0.10), 0.125, 0.009, GLOW, aim=(1, 0, 0), verts=24)
    m.tor((-0.17, 0.0, 0.10), 0.145, 0.009, GLOW, aim=(1, 0, 0), verts=24)
    m.lens((0.30, 0.0, 0.10), 0.115, aim=(1, 0, 0.1), clamps=4)
    # long energy-prism lance: two counter-rotated hex crystals in a floating pearl cradle
    m.cyl((0.27, 0.0, 0.34), (0.72, 0.11, 0.11), GLOW, verts=6, aim=(1, 0, 0.03), taper=0.45)
    m.cyl((0.24, 0.0, 0.34), (0.50, 0.15, 0.15), GLOW, verts=6, aim=(1, 0, 0.03), taper=0.6, rot=(30, 0, 0))
    m.cyl((0.605, 0.0, 0.35), (0.08, 0.05, 0.05), GLOW, verts=6, aim=(1, 0, 0.03), taper=0.1)
    m.armor((-0.06, 0.0, 0.34), (0.14, 0.20, 0.14), SHELL, top=(0.9, 0.85), bevel=0.03)
    for x in (0.08, 0.26, 0.44):
        m.tor((x, 0.0, 0.34 + (x - 0.26) * 0.03), 0.105, 0.013, SHELL, aim=(1, 0, 0.03), verts=18)
    for s in (1, -1):
        m.rod((-0.06, 0.10 * s, 0.32), (0.12, 0.10 * s, 0.24), 0.018, SHELL, verts=8)
    # four long pearl legs: ball hip, thigh spindle, ringed knee, tapered shin, hoof
    for sx in (1, -1):
        hip = Vector((0.22 * sx, 0.17, 0.03))
        knee = Vector((0.36 * sx, 0.34, -0.14))
        ankle = Vector((0.40 * sx, 0.43, -0.52))
        m.sph(hip, (0.14, 0.14, 0.14), DARK, verts=12, mirror=True)
        m.sph(hip.lerp(knee, 0.5), (0.27, 0.13, 0.13), SHELL, aim=(knee - hip), verts=16, mirror=True)
        m.sph(knee, (0.11, 0.11, 0.11), DARK, verts=10, mirror=True)
        m.tor(knee, 0.064, 0.009, GLOW, aim=(0, 1, 0), verts=16, mirror=True)
        m.sph(knee.lerp(ankle, 0.5), (0.44, 0.09, 0.09), SHELL, aim=(ankle - knee), verts=14, mirror=True)
        m.rod(knee.lerp(ankle, 0.2), knee.lerp(ankle, 0.8), 0.016, GLOW, verts=6, mirror=True)
        m.vcyl((0.40 * sx, 0.43, -0.575), 0.05, 0.13, DARK, verts=12, mirror=True)
        m.vcyl((0.40 * sx, 0.43, -0.535), 0.04, 0.10, SHELL, taper=0.7, verts=12, mirror=True)
    m.sph((-0.38, 0.0, 0.08), (0.11, 0.11, 0.11), GLOW, verts=10)
    return m.finish()


def machine_hq(mats):
    m = Model("SM_Machine_HQ", mats)
    # circular plinth: dark base, pearl step, lit floor rings
    m.vcyl((0.0, 0.0, -0.95), 0.10, 2.92, DARK, verts=48, bevel=0.015)
    m.vcyl((0.0, 0.0, -0.865), 0.07, 2.50, SHELL, verts=48, bevel=0.02)
    m.tor((0.0, 0.0, -0.895), 1.36, 0.014, GLOW, verts=48)
    m.tor((0.0, 0.0, -0.825), 1.22, 0.012, GLOW, verts=48)
    # floating monolith segments (slightly twisted) with lit gaps and a cyan core column
    m.box((0.0, 0.0, 0.40), (0.44, 0.44, 2.00), GLOW)
    segs = ((-0.52, 0.28, 1.20, 0), (-0.14, 0.34, 1.05, 8), (0.36, 0.52, 1.00, 0), (0.86, 0.34, 0.90, -8),
            (1.23, 0.26, 0.72, 6))
    for i, (z, h, w, yaw) in enumerate(segs):
        m.armor((0.0, 0.0, z), (w, w, h), SHELL, top=(0.94, 0.94), bevel=0.07, seg=3, rot=(0, 0, yaw))
        if i:
            zp, hp, wp, yp = segs[i - 1]
            gz = (zp + hp / 2 + z - h / 2) / 2
            m.box((0.0, 0.0, gz), (min(wp, w) * 0.86, min(wp, w) * 0.86, 0.10), GLOW, rot=(0, 0, yaw))
    m.box((0.0, 0.0, -0.70), (0.90, 0.90, 0.10), GLOW)
    m.armor((0.0, 0.0, 1.385), (0.44, 0.44, 0.03), TEAM, top=(0.9, 0.9), bevel=0.01, seg=2, rot=(0, 0, 6))
    # lit vertical seams on the side and rear faces of the tall segments
    for k in range(3):
        a = math.radians(90 * k + 90)
        n, t = Vector((math.cos(a), math.sin(a), 0.0)), Vector((-math.sin(a), math.cos(a), 0.0))
        for z, h in ((0.36, 0.40), (0.86, 0.24), (-0.14, 0.22)):
            for u in (-0.22, 0.22):
                Frame.surface(m, n * 0.505 + t * u + Vector((0, 0, z)), n, (0, 0, 1)).box(
                    (0, 0, 0), (h, 0.022, 0.014), GLOW)
    # the huge red lens: dark bezel plate, lit rim, clamped housing, red dome
    m.cyl((0.50, 0.0, 0.36), (0.09, 0.96, 0.96), DARK, verts=40)
    m.tor((0.545, 0.0, 0.36), 0.47, 0.014, GLOW, aim=(1, 0, 0), verts=48)
    m.lens((0.55, 0.0, 0.36), 0.40, aim=(1, 0, 0), clamps=8, collar=1.45, ring_verts=40)
    # two tilted orbit rings with lit cores
    m.tor((0.0, 0.0, -0.27), 1.15, 0.045, SHELL, rot=(8, 0, 0), verts=48)
    m.tor((0.0, 0.0, -0.27), 1.15, 0.022, GLOW, rot=(8, 0, 0), verts=48)
    m.tor((0.0, 0.0, 0.78), 0.95, 0.035, SHELL, rot=(-10, 6, 0), verts=48)
    m.tor((0.0, 0.0, 0.78), 0.95, 0.016, GLOW, rot=(-10, 6, 0), verts=48)
    # four floating crystal pylons on dark pads, with lit halos and tips
    for sx in (1, -1):
        for sy in (1, -1):
            x, y = 1.12 * sx, 1.12 * sy
            m.armor((x, y, -0.80), (0.36, 0.36, 0.06), DARK, top=(0.85, 0.85), bevel=0.02, seg=2)
            m.tor((x, y, -0.735), 0.15, 0.010, GLOW, verts=20)
            m.vcyl((x, y, -0.12), 1.05, 0.22, SHELL, taper=0.45, verts=6, bevel=0.01)
            m.vcyl((x, y, -0.05), 0.60, 0.10, GLOW, taper=0.5, verts=6)
            m.sph((x, y, 0.47), (0.07, 0.07, 0.07), GLOW, verts=8)
    # crown: pearl fins around a glowing crystal spire
    m.vcyl((0.0, 0.0, 1.61), 0.34, 0.16, GLOW, taper=0.25, verts=6)
    for k in range(4):
        yaw = 45 + 90 * k
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        m.armor((0.21 * c, 0.21 * s, 1.555), (0.15, 0.03, 0.36), SHELL, top=(0.3, 1.0), rot=(0, 8, yaw),
                bevel=0.008, seg=2)
    return m.finish()


# --------------------------------------------------------------------------------------
# Human units: powered armour, prefab steel-blue plating, amber lamps, big team-colour plates
# --------------------------------------------------------------------------------------
def human_frontline(mats):
    m = Model("SM_Human_Frontline", mats)
    # legs: lugged boot, toe wedge, flared greave, hydraulic knee, layered thigh plate
    for s in (1, -1):
        y = 0.165 * s
        m.box((0.03, y, -0.58), (0.40, 0.23, 0.04), DARK, bevel=0.015)
        m.armor((0.0, y, -0.50), (0.32, 0.22, 0.12), SHELL, top=(0.92, 0.94))
        m.armor((0.19, y, -0.51), (0.13, 0.20, 0.09), SHELL, top=(0.35, 0.9), skew=(-0.25, 0.0))
        m.armor((-0.01, y, -0.31), (0.24, 0.21, 0.28), SHELL, top=(1.12, 1.08), skew=(0.03, 0.0))
        m.box((-0.15, y, -0.31), (0.03, 0.15, 0.20), DARK)
        m.sph((0.0, y, -0.145), (0.20, 0.20, 0.20), DARK, verts=12)
        f = Frame.surface(m, (0.0, y, -0.145), (1, 0, 0), (0, 0, 1))
        f.armor((0, 0, 0.10), (0.15, 0.20, 0.08), SHELL, top=(0.7, 0.8))
        m.armor((0.0, y, -0.06), (0.24, 0.22, 0.14), SHELL, top=(1.1, 1.0))
        m.rod((-0.12, y * 1.16, -0.27), (-0.12, y * 1.16, -0.09), 0.018, DARK, verts=8)
        m.rod((-0.12, y * 1.16, -0.23), (-0.12, y * 1.16, -0.15), 0.028, SHELL, verts=8)
    # patched greave plate on the right shin
    m.armor((0.06, -0.272, -0.31), (0.14, 0.02, 0.15), DARK, top=(1, 1), bevel=0.008, seg=2)
    # pelvis skirt, hip guards, belt with hazard tape
    m.armor((0.0, 0.0, -0.03), (0.34, 0.42, 0.10), SHELL, top=(1.0, 1.0))
    m.armor((0.0, 0.24, -0.04), (0.22, 0.06, 0.12), SHELL, top=(0.9, 0.9), mirror=True)
    m.box((0.0, 0.0, 0.03), (0.32, 0.38, 0.06), DARK, bevel=0.015)
    m.hazard((0.163, 0.0, 0.03), (1, 0, 0), (0, 1, 0), 0.24, 0.04, n=7)
    # torso: broad tapered chest, front breastplate, lit sensor lamp, side vents
    m.armor((0.0, 0.0, 0.18), (0.34, 0.40, 0.32), SHELL, top=(1.12, 1.4), bevel=0.05)
    m.armor((0.20, 0.0, 0.19), (0.11, 0.36, 0.24), SHELL, top=(0.45, 0.85), bevel=0.035)
    m.lamp((0.232, -0.09, 0.20), (1, 0, 0.35), (0, 1, 0), (0.07, 0.05), depth=0.03)
    m.box((0.212, 0.09, 0.20), (0.05, 0.10, 0.02), DARK)
    for s in (1, -1):
        Frame.surface(m, (0.0, 0.265 * s, 0.18), (0, s, 0), (1, 0, 0)).vent((0, 0, 0), (0.18, 0.12), slats=3)
    # power pack: dark casing, back plate, glowing cooling slots, team lid, twin exhausts
    m.armor((-0.245, 0.0, 0.14), (0.17, 0.36, 0.32), DARK, top=(0.92, 0.92))
    m.armor((-0.335, 0.0, 0.14), (0.03, 0.30, 0.26), SHELL, top=(0.9, 0.9), bevel=0.012, seg=2)
    for dz in (-0.06, 0.0, 0.06):
        m.box((-0.353, 0.0, 0.14 + dz), (0.012, 0.22, 0.02), GLOW)
    m.armor((-0.245, 0.0, 0.318), (0.15, 0.30, 0.045), TEAM, top=(0.85, 0.85))
    m.vcyl((-0.29, 0.11, 0.42), 0.16, 0.07, DARK, taper=1.3, mirror=True)
    m.vcyl((-0.29, 0.11, 0.505), 0.02, 0.07, GLOW, mirror=True)
    # big team-colour pauldrons on chamfered shell rims
    f = Frame(m, (0.0, 0.325, 0.335), rot=(-16, 0, 0))
    f.armor((0, 0, 0.02), (0.32, 0.22, 0.09), TEAM, top=(0.74, 0.70), bevel=0.04, mirror=True)
    f.armor((0, 0, -0.045), (0.39, 0.27, 0.05), SHELL, top=(0.94, 0.94), bevel=0.02, mirror=True)
    f.box((0.10, 0.10, 0.075), (0.09, 0.012, 0.012), GLOW, mirror=True)
    # head: armoured helmet with lit visor, chin guard, crest and ear pods
    m.vcyl((0.0, 0.0, 0.35), 0.06, 0.10, DARK)
    m.armor((0.02, 0.0, 0.45), (0.23, 0.23, 0.16), SHELL, top=(0.8, 0.86), bevel=0.05)
    m.box((0.12, 0.0, 0.44), (0.05, 0.17, 0.07), DARK, bevel=0.015)
    m.box((0.148, 0.0, 0.44), (0.012, 0.14, 0.03), GLOW)
    m.armor((0.09, 0.0, 0.385), (0.10, 0.16, 0.05), SHELL, top=(0.8, 0.9), bevel=0.02, seg=2)
    m.armor((0.0, 0.0, 0.535), (0.12, 0.05, 0.03), TEAM, top=(0.9, 0.9), bevel=0.01, seg=2)
    m.ycyl((0.0, 0.125, 0.45), 0.04, 0.09, DARK, mirror=True)
    m.rod((-0.06, -0.10, 0.50), (-0.09, -0.10, 0.60), 0.008, DARK, verts=6)
    # left arm: sleeve, elbow and gauntlet behind the shield
    a, b = Vector((0.0, 0.30, 0.24)), Vector((0.26, 0.27, 0.04))
    m.rod(a, b, 0.055, SHELL, verts=12)
    m.sph(a.lerp(b, 0.45), (0.10, 0.10, 0.10), DARK, verts=10)
    m.sph(b, (0.11, 0.11, 0.11), DARK, verts=10)
    # riot shield: three curved panels, big team upper face, hazard-taped lower guard, lit viewport
    for x, y, yaw, w in ((0.37, 0.20, 0, 0.19), (0.335, 0.325, 26, 0.13), (0.335, 0.075, -26, 0.13)):
        f = Frame(m, (x, y, -0.01), (0, -6, yaw))
        f.armor((0, 0, 0), (0.055, w, 0.74), SHELL, top=(1, 1), bevel=0.02, seg=2)
        f.armor((0.03, 0, 0.13), (0.03, w * 0.92, 0.42), TEAM, top=(1, 1), bevel=0.012, seg=2)
        m.hazard(f.p((0.0315, 0, -0.25)), f.d((1, 0, 0)), f.d((0, 1, 0)), w * 0.84, 0.10, n=4)
        Frame.surface(m, f.p((0.0455, 0, 0.13)), f.d((1, 0, 0)), f.d((0, 0, 1))).rivets(
            (0.40, w * 0.84), nu=4, nv=2, inset=0.02, r=0.011, mat=SHELL)
        f.box((0.0, 0, 0.375), (0.075, w, 0.03), SHELL, bevel=0.01)
        for e in (1, -1):
            f.box((0.0, e * (w / 2 - 0.008), 0.0), (0.07, 0.016, 0.74), SHELL, bevel=0.006, seg=1)
    f = Frame(m, (0.37, 0.20, -0.01), (0, -6, 0))
    f.box((0.05, 0, 0.30), (0.02, 0.14, 0.035), GLOW)
    # right arm and hydraulic sledgehammer resting on the shoulder
    a, b = Vector((0.0, -0.30, 0.24)), Vector((0.17, -0.335, 0.0))
    m.rod(a, b, 0.055, SHELL, verts=12)
    m.sph(a.lerp(b, 0.45), (0.10, 0.10, 0.10), DARK, verts=10)
    m.sph(b, (0.12, 0.12, 0.12), DARK, verts=10)
    m.rod((0.22, -0.335, -0.12), (-0.12, -0.34, 0.46), 0.026, DARK, verts=8)
    m.ycyl((-0.12, -0.34, 0.48), 0.18, 0.15, SHELL, verts=16, bevel=0.02)
    for e in (1, -1):
        m.ycyl((-0.12, -0.34 + 0.10 * e, 0.48), 0.03, 0.125, DARK, verts=16)
    m.ycyl((-0.12, -0.34, 0.48), 0.035, 0.158, GLOW, verts=16)
    m.rod((0.02, -0.395, 0.12), (-0.09, -0.395, 0.40), 0.030, SHELL, verts=8)
    m.rod((0.02, -0.395, 0.12), (-0.05, -0.395, 0.30), 0.018, DARK, verts=8)
    return m.finish()


def human_ranged(mats):
    m = Model("SM_Human_Ranged", mats)
    # slim legs: boot, toe wedge, greave, hydraulic knee pad, thigh plate
    for s in (1, -1):
        y = 0.135 * s
        m.box((0.03, y, -0.58), (0.34, 0.19, 0.04), DARK, bevel=0.012)
        m.armor((0.0, y, -0.50), (0.28, 0.18, 0.12), SHELL, top=(0.92, 0.94))
        m.armor((0.165, y, -0.51), (0.11, 0.17, 0.09), SHELL, top=(0.35, 0.9), skew=(-0.25, 0.0))
        m.armor((-0.005, y, -0.32), (0.20, 0.17, 0.28), SHELL, top=(1.1, 1.05))
        m.sph((0.0, y, -0.155), (0.16, 0.16, 0.16), DARK, verts=12)
        f = Frame.surface(m, (0.0, y, -0.155), (1, 0, 0), (0, 0, 1))
        f.armor((0, 0, 0.08), (0.13, 0.16, 0.07), SHELL, top=(0.7, 0.8))
        m.armor((0.0, y, -0.07), (0.20, 0.18, 0.13), SHELL, top=(1.08, 1.0))
        m.rod((-0.10, y * 1.2, -0.28), (-0.10, y * 1.2, -0.10), 0.014, DARK, verts=8)
    # hips, belt with amber buckle lamp and pouches
    m.armor((0.0, 0.0, -0.03), (0.28, 0.34, 0.09), SHELL, top=(1.0, 1.0))
    m.box((0.0, 0.0, 0.02), (0.26, 0.30, 0.05), DARK, bevel=0.012)
    m.lamp((0.135, 0.0, 0.02), (1, 0, 0), (0, 1, 0), (0.05, 0.04), depth=0.025)
    m.armor((0.09, 0.175, -0.01), (0.09, 0.06, 0.10), DARK, top=(0.9, 0.9), bevel=0.015, seg=2, mirror=True)
    # torso with breastplate and side vents
    m.armor((0.0, 0.0, 0.17), (0.28, 0.32, 0.30), SHELL, top=(1.1, 1.35), bevel=0.045)
    m.armor((0.16, 0.0, 0.18), (0.09, 0.28, 0.22), SHELL, top=(0.45, 0.85), bevel=0.03)
    m.box((0.195, 0.0, 0.11), (0.03, 0.16, 0.014), GLOW)
    # sensor backpack: casing, team lid, mast, tilted dish with lit feed, whip antenna
    m.armor((-0.20, 0.0, 0.15), (0.15, 0.30, 0.30), DARK, top=(0.92, 0.92))
    m.armor((-0.20, 0.0, 0.315), (0.13, 0.26, 0.05), TEAM, top=(0.88, 0.88))
    m.armor((-0.275, 0.0, 0.15), (0.03, 0.24, 0.22), SHELL, top=(0.9, 0.9), bevel=0.012, seg=2)
    m.box((-0.292, 0.0, 0.15), (0.012, 0.18, 0.02), GLOW)
    m.rod((-0.24, 0.08, 0.34), (-0.24, 0.08, 0.50), 0.02, DARK, verts=8)
    m.cyl((-0.22, 0.08, 0.54), (0.07, 0.27, 0.27), SHELL, aim=(0.5, 0.5, 0.8), taper=0.35, verts=20)
    m.sph((-0.19, 0.11, 0.585), (0.06, 0.06, 0.06), GLOW, verts=8)
    m.rod((-0.27, -0.10, 0.34), (-0.31, -0.13, 0.61), 0.008, DARK, verts=6)
    # shoulder pads: big team paint on shell rims
    f = Frame(m, (0.0, 0.27, 0.32), rot=(-14, 0, 0))
    f.armor((0, 0, 0.02), (0.29, 0.20, 0.08), TEAM, top=(0.74, 0.70), bevel=0.035, mirror=True)
    f.armor((0, 0, -0.04), (0.31, 0.22, 0.045), SHELL, top=(0.94, 0.94), bevel=0.018, mirror=True)
    # head: helmet, lit visor band, side sensor monocle
    m.vcyl((0.0, 0.0, 0.335), 0.06, 0.09, DARK)
    m.armor((0.02, 0.0, 0.435), (0.20, 0.20, 0.15), SHELL, top=(0.8, 0.85), bevel=0.045)
    m.box((0.115, 0.0, 0.425), (0.045, 0.15, 0.06), DARK, bevel=0.012)
    m.box((0.14, 0.0, 0.425), (0.012, 0.12, 0.028), GLOW)
    m.ycyl((0.09, -0.115, 0.46), 0.05, 0.07, DARK)
    m.ycyl((0.09, -0.145, 0.46), 0.012, 0.05, GLOW)
    m.armor((0.0, 0.0, 0.51), (0.14, 0.05, 0.03), TEAM, top=(0.9, 0.9), bevel=0.01, seg=2)
    # long rail rifle: receiver, energy cell, shrouded barrel, twin rails with lit capacitor, scope, muzzle
    m.armor((-0.02, -0.20, 0.06), (0.32, 0.09, 0.13), SHELL, top=(0.95, 0.9), bevel=0.025, seg=3)
    m.box((-0.19, -0.20, 0.06), (0.06, 0.10, 0.11), DARK, bevel=0.012)
    m.box((0.02, -0.20, -0.04), (0.10, 0.07, 0.11), DARK, bevel=0.012)
    m.box((0.02, -0.20, -0.04), (0.07, 0.076, 0.05), GLOW)
    m.cyl((0.30, -0.20, 0.06), (0.30, 0.07, 0.07), DARK, verts=12)
    for e in (1, -1):
        m.rod((0.14, -0.20 + 0.05 * e, 0.06), (0.43, -0.20 + 0.05 * e, 0.06), 0.017, SHELL, verts=8)
    m.box((0.28, -0.20, 0.098), (0.22, 0.02, 0.014), GLOW)
    m.cyl((0.435, -0.20, 0.06), (0.03, 0.10, 0.10), SHELL, verts=12)
    m.cyl((0.452, -0.20, 0.06), (0.012, 0.075, 0.075), GLOW, verts=12)
    m.cyl((0.03, -0.20, 0.155), (0.22, 0.06, 0.06), DARK, verts=12)
    m.cyl((0.14, -0.20, 0.155), (0.012, 0.045, 0.045), GLOW, verts=12)
    m.box((0.03, -0.20, 0.125), (0.08, 0.03, 0.04), DARK)
    # arms reaching to the grips
    a, b = Vector((0.0, -0.265, 0.235)), Vector((0.03, -0.22, 0.05))
    m.rod(a, b, 0.045, SHELL, verts=12)
    m.sph(a.lerp(b, 0.5), (0.08, 0.08, 0.08), DARK, verts=10)
    m.sph(b, (0.09, 0.09, 0.09), DARK, verts=10)
    a, b = Vector((0.0, 0.265, 0.235)), Vector((0.28, -0.18, 0.05))
    m.rod(a, b, 0.042, SHELL, verts=12)
    m.sph(a.lerp(b, 0.4), (0.08, 0.08, 0.08), DARK, verts=10)
    m.sph(b, (0.09, 0.09, 0.09), DARK, verts=10)
    return m.finish()


def human_siege(mats):
    m = Model("SM_Human_Siege", mats)
    # two reverse-jointed legs on wide flat feet
    for s in (1, -1):
        y = 0.44 * s
        hip = Vector((-0.02, 0.31 * s, -0.04))
        knee = Vector((0.20, y, -0.25))
        ankle = Vector((-0.04, y, -0.47))
        m.box((0.03, y, -0.575), (0.40, 0.24, 0.05), DARK, bevel=0.02)
        m.armor((0.01, y, -0.51), (0.32, 0.20, 0.09), SHELL, top=(0.8, 0.85), bevel=0.035)
        m.armor((0.21, y, -0.535), (0.14, 0.19, 0.07), SHELL, top=(0.3, 0.9), skew=(-0.25, 0), bevel=0.025)
        m.sph(hip, (0.19, 0.19, 0.19), DARK, verts=12)
        m.rod(hip, knee, 0.075, DARK, verts=12)
        m.add("frust", hip.lerp(knee, 0.5) + Vector((0, 0.0, 0.05)), (0.30, 0.15, 0.09), SHELL,
              aim=(knee - hip), taper=(1.0, 0.75), bevel=0.03, seg=3)
        m.sph(knee, (0.17, 0.17, 0.17), DARK, verts=12)
        m.add("frust", knee, (0.13, 0.18, 0.10), SHELL, rot=(0, -20, 0), taper=(0.8, 0.8), bevel=0.03, seg=2)
        m.rod(knee, ankle, 0.065, DARK, verts=12)
        m.add("frust", knee.lerp(ankle, 0.45) + Vector((0.05, 0, 0.0)), (0.24, 0.12, 0.08), SHELL,
              aim=(ankle - knee), taper=(1.0, 0.8), bevel=0.025, seg=2)
        m.sph(ankle, (0.13, 0.13, 0.13), DARK, verts=10)
        m.rod(hip + Vector((0.0, 0.0, 0.08)), knee + Vector((0.0, 0.0, 0.10)), 0.016, SHELL, verts=8)
    # hull: chamfered lower body, skirts with hazard tape, team roof plates split by a vented spine
    m.armor((-0.04, 0.0, -0.03), (0.64, 0.56, 0.22), SHELL, top=(0.88, 0.85), bevel=0.05)
    m.armor((-0.06, 0.0, -0.16), (0.52, 0.44, 0.06), DARK, top=(0.9, 0.9), bevel=0.02, seg=2)
    m.armor((-0.06, 0.0, 0.135), (0.48, 0.52, 0.06), SHELL, top=(0.95, 0.95), bevel=0.03)
    for s in (1, -1):
        m.armor((-0.08, 0.17 * s, 0.185), (0.36, 0.19, 0.05), TEAM, top=(0.86, 0.82), bevel=0.028, seg=3)
        m.armor((-0.04, 0.30 * s, -0.03), (0.44, 0.05, 0.16), SHELL, top=(0.9, 0.7), bevel=0.02, seg=2)
        m.box((0.10, 0.286 * s, -0.03), (0.04, 0.014, 0.10), GLOW)
        m.hazard((-0.16, 0.281 * s, -0.03), (0, s, 0), (1, 0, 0), 0.18, 0.07, n=4)
    Frame.surface(m, (-0.12, 0.0, 0.165), (0, 0, 1), (1, 0, 0)).vent((0, 0, 0), (0.26, 0.10), slats=4)
    # cockpit cab: armoured block, team crown, lit canopy, side work lamps
    m.armor((0.12, 0.0, 0.22), (0.30, 0.36, 0.16), SHELL, top=(0.70, 0.85), skew=(-0.08, 0), bevel=0.04)
    m.armor((0.07, 0.0, 0.31), (0.16, 0.26, 0.03), TEAM, top=(0.8, 0.85), bevel=0.012, seg=2)
    m.lamp((0.225, 0.0, 0.225), (1, 0, 0.15), (0, 1, 0), (0.26, 0.07), depth=0.03)
    for s in (1, -1):
        m.lamp((0.07, 0.185 * s, 0.245), (0, s, 0.1), (1, 0, 0), (0.09, 0.045), depth=0.02)
    m.rod((0.0, 0.14, 0.30), (-0.05, 0.16, 0.50), 0.008, DARK, verts=6)
    # rear power block: glowing vents, two exhaust stacks
    m.armor((-0.37, 0.0, 0.05), (0.18, 0.40, 0.26), DARK, top=(0.9, 0.9), bevel=0.03)
    for dz in (-0.05, 0.0, 0.05):
        m.box((-0.463, 0.0, 0.05 + dz), (0.012, 0.30, 0.02), GLOW)
    m.vcyl((-0.32, 0.13, 0.25), 0.20, 0.08, DARK, taper=1.3, mirror=True)
    m.vcyl((-0.32, 0.13, 0.355), 0.02, 0.08, GLOW, mirror=True)
    # front boom and giant hydraulic bolt-cutter jaws, opening sideways so the V reads from above
    m.armor((0.22, 0.0, -0.03), (0.16, 0.34, 0.26), DARK, top=(0.85, 0.85), bevel=0.035)
    pivot = Vector((0.27, 0.0, -0.03))
    m.vcyl(pivot, 0.30, 0.20, DARK, verts=16)
    m.vcyl(pivot + Vector((0, 0, 0.16)), 0.02, 0.10, GLOW, verts=16)
    lean = math.radians(28)
    c, s_ = math.cos(lean), math.sin(lean)
    for sgn in (1, -1):
        d = Vector((c, sgn * s_, 0.0))
        inner = Vector((s_, -sgn * c, 0.0))
        f = Frame.surface(m, pivot + d * 0.215, d, (0, 0, 1))
        f.armor((0, 0, 0), (0.17, 0.16, 0.43), SHELL, top=(0.55, 0.45), bevel=0.035, seg=3)
        # cutting edge: lit strip on the inner face, hazard tape on the flat top
        Frame.surface(m, pivot + d * 0.25 + inner * 0.06, inner, d).box((0, 0, 0), (0.26, 0.05, 0.012), GLOW)
        m.hazard(pivot + d * 0.13 + Vector((0, 0, 0.086)), (0, 0, 1), d, 0.16, 0.08, n=4)
        root = pivot - d * 0.02 - inner * 0.10
        for z in (0.0,):
            m.rod(Vector((0.10, sgn * 0.15, -0.03)), root, 0.03, DARK, verts=8)
            m.rod(Vector((0.10, sgn * 0.15, -0.03)), Vector((0.10, sgn * 0.15, -0.03)).lerp(root, 0.45), 0.045,
                  SHELL, verts=8)
    return m.finish()


def human_hq(mats):
    m = Model("SM_Human_HQ", mats)
    # landing pad with hazard-taped edge and four heavy landing struts
    m.armor((0.0, 0.0, -0.96), (2.96, 2.96, 0.08), DARK, top=(1.0, 1.0), bevel=0.02, seg=2)
    for sx in (0.75, -0.95):
        for s in (1, -1):
            m.armor((sx, 0.90 * s, -0.905), (0.46, 0.46, 0.07), DARK, top=(0.8, 0.8), bevel=0.02, seg=2)
            m.vcyl((sx, 0.90 * s, -0.60), 0.62, 0.20, SHELL, taper=1.0, verts=14, bevel=0.012)
            m.vcyl((sx, 0.90 * s, -0.86), 0.10, 0.25, DARK, verts=14)
            m.vcyl((sx, 0.90 * s, -0.42), 0.05, 0.25, DARK, verts=14)
            m.rod((sx, 0.90 * s, -0.86), (sx, 1.04 * s, -0.30), 0.035, DARK, verts=8)
    # undercroft power core between the struts
    m.armor((-0.10, 0.0, -0.66), (1.5, 1.4, 0.42), DARK, top=(0.96, 0.96), bevel=0.03)
    for s in (1, -1):
        m.box((-0.10, 0.706 * s, -0.66), (1.2, 0.012, 0.05), GLOW)
        m.box((0.652, 0.0, -0.66), (0.012, 0.9, 0.05), GLOW)
    # prefab module: plated lower body cut by recessed panel lines, roof slab and parapet
    m.seams((-0.10, 0.0, 0.05), (2.0, 2.1, 0.70), SHELL, ((0, 0.34), (0, 0.66), (1, 0.5), (2, 0.5)), gap=0.03,
            depth=0.03, bevel=0.04, seg=2, core=DARK)
    m.armor((-0.10, 0.0, -0.33), (2.12, 2.22, 0.06), DARK, top=(0.98, 0.98), bevel=0.02, seg=2)
    m.armor((-0.10, 0.0, 0.43), (2.18, 2.28, 0.10), SHELL, top=(0.96, 0.96), bevel=0.05)
    for s in (1, -1):
        m.box((-0.10, 1.10 * s, 0.505), (2.10, 0.05, 0.07), SHELL, bevel=0.012)
        m.box((-1.16, 0.0, 0.505), (0.05, 2.20, 0.07), SHELL, bevel=0.012)
    # big team roof plates and a roof hatch
    for s in (1, -1):
        m.armor((0.18, 0.62 * s, 0.505), (0.98, 0.78, 0.05), TEAM, top=(0.92, 0.90), bevel=0.03, seg=3)
    m.armor((0.18, 0.0, 0.50), (0.60, 0.24, 0.05), DARK, top=(0.9, 0.9), bevel=0.015, seg=2)
    m.box((0.18, 0.0, 0.535), (0.40, 0.03, 0.012), GLOW)
    Frame.surface(m, (-0.55, 0.0, 0.482), (0, 0, 1), (1, 0, 0)).vent((0, 0, 0), (0.50, 0.30), slats=5)
    # blast door: posts, lintel, two heavy leaves with lit slits, hazard tape, team canopy
    m.armor((0.99, 0.0, -0.01), (0.12, 1.30, 0.70), DARK, top=(0.9, 0.95), bevel=0.02, seg=2)
    for s in (1, -1):
        m.armor((0.985, 0.60 * s, -0.0), (0.16, 0.16, 0.72), SHELL, top=(0.85, 0.85), bevel=0.03)
        Frame.surface(m, (1.062, 0.60 * s, -0.0), (1, 0, 0), (0, 0, 1)).rivets(
            (0.60, 0.10), nu=5, nv=2, inset=0.03, r=0.012, mat=DARK)
        m.box((1.0, 0.27 * s, -0.06), (0.06, 0.48, 0.52), DARK, bevel=0.012)
        Frame.surface(m, (1.03, 0.27 * s, -0.06), (1, 0, 0), (0, 0, 1)).rivets(
            (0.46, 0.44), nu=5, nv=3, inset=0.03, r=0.012, mat=SHELL)
        m.lamp((1.03, 0.27 * s, 0.13), (1, 0, 0), (0, 1, 0), (0.34, 0.06), depth=0.02)
        m.hazard((1.031, 0.27 * s, -0.16), (1, 0, 0), (0, 1, 0), 0.40, 0.10, n=6)
    m.armor((1.03, 0.0, 0.40), (0.34, 1.44, 0.08), TEAM, top=(0.75, 0.95), bevel=0.03, seg=3)
    # flanking front windows and side windows with hazard skirt tape
    for s in (1, -1):
        m.lamp((0.90, 0.82 * s, 0.14), (1, 0, 0), (0, 1, 0), (0.30, 0.18), depth=0.03)
        for x in (-0.65, -0.10, 0.45):
            m.lamp((x, 1.05 * s, 0.14), (0, s, 0), (1, 0, 0), (0.32, 0.16), depth=0.03)
            m.hazard((x, 1.05 * s, -0.22), (0, s, 0), (1, 0, 0), 0.34, 0.07, n=5)
    # entry ramp with rails
    m.armor((1.20, 0.0, -0.62), (0.58, 0.90, 0.60), SHELL, top=(0.05, 1.0), skew=(-0.475, 0.0), bevel=0.02, seg=2)
    for s in (1, -1):
        m.armor((1.20, 0.50 * s, -0.66), (0.58, 0.10, 0.52), DARK, top=(0.1, 1.0), skew=(-0.45, 0.0), bevel=0.015,
                seg=2)
    m.hazard((1.26, 0.0, -0.87), (0, 0, 1), (1, 0, 0), 0.36, 0.10, n=5)
    # armoured side barriers with team caps, front barrier blocks, floodlight masts
    for s in (1, -1):
        m.armor((-0.10, 1.32 * s, -0.72), (2.30, 0.26, 0.40), SHELL, top=(0.98, 0.6), bevel=0.035, seg=3)
        m.armor((-0.10, 1.30 * s, -0.505), (2.20, 0.16, 0.035), TEAM, top=(0.98, 0.9), bevel=0.012, seg=2)
        m.armor((1.30, 0.98 * s, -0.72), (0.40, 0.52, 0.40), SHELL, top=(0.6, 0.7), bevel=0.035, seg=3)
        m.armor((1.30, 0.98 * s, -0.505), (0.28, 0.40, 0.03), TEAM, top=(0.9, 0.9), bevel=0.012, seg=2)
        m.rod((0.84, 1.32 * s, -0.50), (0.84, 1.32 * s, 0.56), 0.04, DARK, verts=8)
        m.armor((0.84, 1.32 * s, 0.60), (0.20, 0.34, 0.12), DARK, top=(0.9, 0.9), bevel=0.02, seg=2)
        for k in (-1, 0, 1):
            m.lamp((0.945, 1.32 * s + 0.105 * k, 0.60), (1, 0, 0), (0, 1, 0), (0.09, 0.09), depth=0.03)
    # reactor stack: drum, ringed shaft with lit slits, cap and coolant pipes
    rx, ry = -0.80, 0.62
    m.vcyl((rx, ry, 0.62), 0.30, 0.66, SHELL, verts=20, bevel=0.02)
    m.vcyl((rx, ry, 0.79), 0.05, 0.70, DARK, verts=20)
    m.vcyl((rx, ry, 1.10), 0.60, 0.46, SHELL, taper=0.86, verts=20, bevel=0.012)
    for z in (0.93, 1.13, 1.33):
        m.vcyl((rx, ry, z), 0.035, 0.50 - (z - 0.93) * 0.10, DARK, verts=20)
    for k in range(6):
        a = math.radians(60 * k)
        c, s_ = math.cos(a), math.sin(a)
        Frame.surface(m, Vector((rx + 0.225 * c, ry + 0.225 * s_, 1.04)), (c, s_, 0), (0, 0, 1)).box(
            (0, 0, 0), (0.30, 0.05, 0.02), GLOW)
    m.vcyl((rx, ry, 1.44), 0.06, 0.38, DARK, verts=20)
    m.sph((rx, ry, 1.47), (0.30, 0.30, 0.26), SHELL, verts=20)
    m.vcyl((rx, ry, 1.60), 0.06, 0.09, GLOW, verts=12)
    m.cable((rx + 0.30, ry, 0.70), (rx + 0.60, ry - 0.20, 0.62), (rx + 0.50, ry - 0.55, 0.52), 0.04)
    # antenna mast with dish, guy wires and beacon
    ax, ay = -0.75, -0.80
    m.vcyl((ax, ay, 0.54), 0.06, 0.30, DARK, verts=12)
    m.rod((ax, ay, 0.54), (ax, ay, 1.72), 0.035, DARK, verts=8)
    m.box((ax, ay, 1.15), (0.03, 0.44, 0.03), DARK)
    m.cyl((ax + 0.09, ay + 0.06, 1.38), (0.07, 0.42, 0.42), SHELL, aim=(1, 0.3, 0.35), taper=0.4, verts=24)
    m.sph((ax + 0.19, ay + 0.09, 1.42), (0.07, 0.07, 0.07), GLOW, verts=8)
    for a in (35, 155, 275):
        c, s_ = math.cos(math.radians(a)), math.sin(math.radians(a))
        m.rod((ax, ay, 1.10), (ax + 0.42 * c, ay + 0.42 * s_, 0.50), 0.008, DARK, verts=4)
    m.sph((ax, ay, 1.75), (0.09, 0.09, 0.09), GLOW, verts=10)
    # rear generators with lit vents and stacks
    for s in (1, -1):
        m.armor((-1.30, 0.55 * s, -0.66), (0.28, 0.62, 0.40), DARK, top=(0.9, 0.9), bevel=0.025, seg=2)
        Frame.surface(m, (-1.445, 0.55 * s, -0.66), (-1, 0, 0), (0, 1, 0)).vent((0, 0, 0), (0.40, 0.20), slats=4)
        m.box((-1.30, 0.55 * s, -0.445), (0.22, 0.50, 0.03), TEAM)
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
        assert abs(lo.z + 1.0) < 1e-4 and hi.z <= 1.8 + 1e-4, (name, lo, hi)  # Unreal import allows top z <= 1.85
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
        colors_type="LINEAR",   # SC2Mask holds data, not colour: no sRGB encoding
        **FBX_AXES)


MASK_TOLERANCE = 1.0 / 255.0


def verify_masks(obj, path):
    """Re-import the exported FBX and compare its colour attribute with the SC2Mask baked on obj.

    Every imported corner is matched to the nearest source vertex by position (the import may reorder); each
    channel must agree within 1/255, which an sRGB-encoded export (0.5 -> 0.735) would not. Returns
    (max abs error, number of corners, number of channel values strictly between 0.05 and 0.95).
    """
    src = obj.data.color_attributes[MasterMaterials.MASK_ATTR]
    assert src.domain == "POINT" and src.data_type == "FLOAT_COLOR", (obj.name, src.domain, src.data_type)
    expected = [tuple(item.color) for item in src.data]
    tree = kdtree.KDTree(len(obj.data.vertices))
    for vertex in obj.data.vertices:
        tree.insert(vertex.co, vertex.index)
    tree.balance()

    before_objects, before_meshes = set(bpy.data.objects), set(bpy.data.meshes)
    before_materials = set(bpy.data.materials)
    bpy.ops.import_scene.fbx(filepath=path, colors_type="LINEAR", **FBX_AXES)   # raw values, no sRGB decode
    imported = [o for o in bpy.data.objects if o not in before_objects]
    try:
        assert len(imported) == 1 and imported[0].type == "MESH", (obj.name, [o.name for o in imported])
        mesh = imported[0].data
        attr = mesh.color_attributes.get(MasterMaterials.MASK_ATTR)
        assert attr is not None, "%s: FBX has colour attributes %s, no %s" % (
            obj.name, [a.name for a in mesh.color_attributes], MasterMaterials.MASK_ATTR)
        assert attr.data_type == "FLOAT_COLOR" and attr.domain == "CORNER", (obj.name, attr.data_type, attr.domain)
        # the importer may scale or rotate the object; bring vertices back to the source's frame
        to_source = imported[0].matrix_world
        worst, middling = 0.0, 0
        for loop in mesh.loops:
            co = to_source @ mesh.vertices[loop.vertex_index].co
            # parts touch, so several source vertices can coincide with different masks: best match wins
            candidates = tree.find_range(co, 1e-4)
            assert candidates, "%s: imported vertex %s has no source vertex" % (obj.name, tuple(co))
            got = tuple(attr.data[loop.index].color)
            worst = max(worst, min(max(abs(want - have) for want, have in zip(expected[index], got))
                                   for _, index, _ in candidates))
            middling += sum(0.05 < have < 0.95 for have in got)
        assert worst <= MASK_TOLERANCE, "%s: imported mask differs from baked by %.5f (> 1/255)" % (obj.name, worst)
        return worst, len(mesh.loops), middling
    finally:
        for o in imported:
            bpy.data.objects.remove(o)
        for mesh in [m for m in bpy.data.meshes if m not in before_meshes]:
            bpy.data.meshes.remove(mesh)
        for mat in [m for m in bpy.data.materials if m not in before_materials]:
            bpy.data.materials.remove(mat)


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
HUMAN_SHELL = ((0.22, 0.27, 0.36), 0.40, 0.50)
HUMAN_DARK = ((0.045, 0.05, 0.06), 0.5, 0.7)
HUMAN_GLOW_COLOR = (1.0, 0.55, 0.15)
MACHINE_SHELL = ((0.88, 0.90, 0.94), 0.15, 0.25)
MACHINE_DARK = ((0.05, 0.06, 0.08), 0.3, 0.7)
MACHINE_GLOW_COLOR = (0.35, 0.85, 1.0)


def preview_materials():
    return {
        "Machine": [
            make_material("PV_M_Team", (1.0, 0.01, 0.01), emission=4.0),
            make_material("PV_M_Shell", MACHINE_SHELL[0], roughness=MACHINE_SHELL[1], metallic=MACHINE_SHELL[2],
                          coat=0.7),
            make_material("PV_M_Dark", MACHINE_DARK[0], roughness=MACHINE_DARK[1], metallic=MACHINE_DARK[2]),
            make_material("PV_M_Glow", MACHINE_GLOW_COLOR, emission=1.3),
        ],
        "Human": [
            make_material("PV_H_Team", (0.05, 0.30, 1.0), roughness=0.4, metallic=0.2),
            make_material("PV_H_Shell", HUMAN_SHELL[0], roughness=HUMAN_SHELL[1], metallic=HUMAN_SHELL[2]),
            make_material("PV_H_Dark", HUMAN_DARK[0], roughness=HUMAN_DARK[1], metallic=HUMAN_DARK[2]),
            make_material("PV_H_Glow", HUMAN_GLOW_COLOR, emission=1.6),
        ],
    }


class PreviewRig:
    """Preview scene: lit lineup of copies with per-faction preview materials (originals hidden)."""

    def __init__(self, objects, pv):
        self.objects = objects
        scene = self.scene = bpy.context.scene

        scene.render.engine = "CYCLES"
        scene.cycles.device = "CPU"
        scene.cycles.samples = 48
        scene.cycles.use_denoising = True
        scene.render.resolution_x, scene.render.resolution_y = PREVIEW_SIZE
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = "PNG"
        scene.view_settings.view_transform = "Standard"

        world = bpy.data.worlds.new("PV_World")
        scene.world = world
        world.use_nodes = True
        bg = world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = (0.20, 0.23, 0.28, 1.0)
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


def render_previews(objects, pv):
    rig = PreviewRig(objects, pv)
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

    # SC2 material masks: Edge / Cavity / Ground in the SC2Mask colour attribute, written to the FBX linear.
    for obj in objects:
        MasterMaterials.bake_masks(obj, MasterMaterials.scope_of(obj.name, "unit"))
        export_fbx(obj, os.path.join(OUT, obj.name + ".fbx"))

    # Units.blend: the eight meshes in a row along +X, wearing the preview palette (Machine pearl, Human
    # gunmetal) as object-level material overrides so the palette shows when editing. The FBXs above were
    # written first, from the neutral Team / Shell / Dark / Glow materials.
    pv = preview_materials()
    for index, (obj, (faction, _)) in enumerate(zip(objects, MODELS)):
        obj.location = (index * 4.0, 0.0, 0.0)
        for slot, material in zip(obj.material_slots, pv[faction]):
            slot.link = "OBJECT"
            slot.material = material
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "Units.blend"))

    # Re-import every FBX and compare with the baked SC2Mask (after the save: the import is cleaned up).
    mid_total = 0
    for obj in objects:
        worst, corners, middling = verify_masks(obj, os.path.join(OUT, obj.name + ".fbx"))
        mid_total += middling
        print("MASK_VERIFY %s: %d corners, max |imported - baked| = %.6f, %d mid-range values" % (
            obj.name, corners, worst, middling))
    assert mid_total > 0, "no mid-range mask values: the linear-versus-sRGB check proved nothing"

    print("MESH | bounds min | bounds max | tris | slots")
    for obj in objects:
        lo, hi = bounds(obj)
        print("%s | (%.3f, %.3f, %.3f) | (%.3f, %.3f, %.3f) | %d | %s" % (
            obj.name, *lo, *hi, triangles(obj), "/".join(mat.name for mat in obj.data.materials)))
    sys.stdout.flush()

    for obj in objects:
        obj.location = (0.0, 0.0, 0.0)
    render_previews(objects, pv)
    print("UNIT_MESHES_DONE")


main()
