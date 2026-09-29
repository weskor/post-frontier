"""Generate the placeholder unit and HQ meshes for CoopRTS in Blender.

Run headless from the repo root (Blender 5.2.1, no assets, no network, deterministic):

    blender -b --factory-startup -P Build/GenerateUnitMeshes.py

Every mesh is built from bmesh primitives plus Bevel modifiers (applied) and joined into one
object. Units are metres, +Z up, forward = +X. Ground units are centred on a capsule of radius
0.34 m and half-height 0.60 m (ground contact at z = -0.60). HQs are 3.0 x 3.0 x 2.0 m boxes
centred on the origin (base at z = -1.0).

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
    0 Team   player colour accent (Machine: lens eye; Human: painted stripe, shield band, flag)
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


# --------------------------------------------------------------------------------------
# Machine units: symmetric, glossy white shells, one lens eye, cyan-white seams
# --------------------------------------------------------------------------------------
def machine_frontline(mats):
    m = Model("SM_Machine_Frontline", mats)
    # short thick legs
    m.box((0.05, 0.19, -0.535), (0.44, 0.28, 0.13), SHELL, bevel=0.035, mirror=True)
    m.vcyl((0.0, 0.19, -0.44), 0.10, 0.17, DARK, mirror=True)
    m.box((0.0, 0.19, -0.30), (0.30, 0.26, 0.26), SHELL, bevel=0.05, mirror=True)
    m.sph((0.0, 0.19, -0.14), (0.24, 0.24, 0.24), DARK, mirror=True)
    m.box((0.135, 0.19, -0.30), (0.02, 0.10, 0.02), GLOW, mirror=True)
    m.box((0.0, 0.0, -0.10), (0.34, 0.50, 0.14), SHELL, bevel=0.04)
    # rounded-box torso, head module, big chest lens
    m.box((0.0, 0.0, 0.20), (0.50, 0.58, 0.50), SHELL, bevel=0.10, seg=3)
    m.box((0.0, 0.0, 0.525), (0.30, 0.36, 0.15), SHELL, bevel=0.05, seg=2)
    m.box((0.155, 0.0, 0.535), (0.02, 0.24, 0.03), GLOW)
    m.eye((0.20, 0.0, 0.30), 0.19, aim=(1, 0, 0.7))
    m.box((0.255, 0.0, -0.005), (0.02, 0.44, 0.02), GLOW)
    m.box((0.0, 0.292, 0.12), (0.30, 0.02, 0.02), GLOW, mirror=True)
    # heavy shoulder plates, arms, fists
    m.box((0.0, 0.325, 0.46), (0.36, 0.22, 0.14), SHELL, rot=(-14, 0, 0), bevel=0.05, mirror=True)
    m.box((0.0, 0.325, 0.535), (0.26, 0.03, 0.015), GLOW, rot=(-14, 0, 0), mirror=True)
    m.sph((0.0, 0.365, 0.32), (0.20, 0.20, 0.20), DARK, mirror=True)
    m.vcyl((0.02, 0.375, 0.15), 0.30, 0.13, DARK, mirror=True)
    m.box((0.08, 0.375, -0.04), (0.20, 0.16, 0.20), SHELL, bevel=0.04, mirror=True)
    # power pack
    m.box((-0.30, 0.0, 0.22), (0.14, 0.34, 0.30), SHELL, bevel=0.04)
    m.box((-0.375, 0.08, 0.22), (0.02, 0.03, 0.24), GLOW, mirror=True)
    return m.finish()


def machine_ranged(mats):
    m = Model("SM_Machine_Ranged", mats)
    m.sph((0.0, 0.0, 0.18), (0.62, 0.46, 0.46), SHELL, verts=24)
    m.tor((-0.03, 0.0, 0.18), 0.233, 0.014, GLOW, rot=(0, 90, 0), verts=28)
    # glowing underside
    m.vcyl((0.0, 0.0, -0.12), 0.16, 0.30, DARK, taper=0.6)
    m.vcyl((0.0, 0.0, -0.235), 0.05, 0.19, GLOW)
    m.sph((0.0, 0.0, -0.27), (0.2, 0.2, 0.11), GLOW)
    # halo ring held off the body by spokes
    m.tor((0.0, 0.0, 0.235), 0.40, 0.028, SHELL, verts=40)
    m.tor((0.0, 0.0, 0.125), 0.40, 0.028, SHELL, verts=40)
    m.tor((0.0, 0.0, 0.18), 0.395, 0.02, GLOW, verts=40)
    for ang in (55, 125, 235, 305):
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        m.rod((0.2 * c, 0.2 * s, 0.18), (0.40 * c, 0.40 * s, 0.18), 0.018, DARK, verts=8)
    # lens eye and twin emitter prongs
    m.eye((0.22, 0.0, 0.23), 0.15, aim=(1, 0, 0.6))
    m.cyl((0.29, 0.13, 0.06), (0.14, 0.10, 0.10), SHELL, verts=12, mirror=True)
    m.rod((0.22, 0.13, 0.06), (0.42, 0.13, 0.06), 0.034, DARK, verts=10, mirror=True)
    m.sph((0.425, 0.13, 0.06), (0.09, 0.09, 0.09), GLOW, verts=10, mirror=True)
    # top fin, rear exhaust
    m.box((-0.02, 0.0, 0.51), (0.26, 0.04, 0.22), SHELL, bevel=0.015)
    m.sph((0.07, 0.0, 0.605), (0.05, 0.05, 0.05), GLOW, verts=8)
    m.cyl((-0.325, 0.0, 0.16), (0.08, 0.16, 0.16), DARK)
    m.cyl((-0.362, 0.0, 0.16), (0.02, 0.12, 0.12), GLOW)
    return m.finish()


def machine_siege(mats):
    m = Model("SM_Machine_Siege", mats)
    m.box((0.0, 0.0, -0.20), (0.78, 0.60, 0.20), SHELL, bevel=0.05, seg=3)
    m.box((0.0, 0.0, -0.32), (0.60, 0.44, 0.06), DARK)
    m.box((-0.05, 0.0, 0.0), (0.50, 0.42, 0.24), SHELL, bevel=0.07, seg=3)
    m.box((0.0, 0.302, -0.20), (0.50, 0.012, 0.02), GLOW, mirror=True)
    m.box((0.392, 0.0, -0.20), (0.012, 0.36, 0.02), GLOW)
    m.box((-0.26, 0.0, 0.10), (0.04, 0.30, 0.12), SHELL, bevel=0.012)
    m.box((-0.05, 0.212, 0.0), (0.30, 0.012, 0.02), GLOW, mirror=True)
    # long forward barrel, dish and prism emitter
    m.cyl((0.22, 0.0, 0.0), (0.20, 0.22, 0.22), SHELL, verts=20)
    m.cyl((0.38, 0.0, 0.0), (0.34, 0.11, 0.11), SHELL, verts=16)
    for x in (0.30, 0.42):
        m.cyl((x, 0.0, 0.0), (0.03, 0.145, 0.145), DARK, verts=16)
    m.cyl((0.575, 0.0, 0.0), (0.10, 0.10, 0.10), SHELL, verts=24, taper=3.0)
    m.cyl((0.622, 0.0, 0.0), (0.012, 0.27, 0.27), GLOW, verts=24)
    m.cyl((0.625, 0.0, 0.0), (0.08, 0.07, 0.07), GLOW, verts=3)
    m.eye((-0.02, 0.0, 0.08), 0.15, aim=(0.35, 0, 1))
    # four splayed legs
    for sx in (1, -1):
        m.sph((0.26 * sx, 0.30, -0.20), (0.18, 0.18, 0.18), DARK, mirror=True)
        m.rod((0.26 * sx, 0.30, -0.20), (0.44 * sx, 0.52, -0.12), 0.06, SHELL, mirror=True)
        m.sph((0.44 * sx, 0.52, -0.12), (0.15, 0.15, 0.15), SHELL, mirror=True)
        m.rod((0.44 * sx, 0.52, -0.12), (0.48 * sx, 0.55, -0.55), 0.05, DARK, r1=0.038, mirror=True)
        m.vcyl((0.48 * sx, 0.55, -0.575), 0.05, 0.18, DARK, mirror=True)
    return m.finish()


# --------------------------------------------------------------------------------------
# Human units: scrappy, asymmetric, khaki / gunmetal plates, amber lamps
# --------------------------------------------------------------------------------------
def human_frontline(mats):
    m = Model("SM_Human_Frontline", mats)
    for s in (1, -1):
        m.box((0.04, 0.115 * s, -0.535), (0.27, 0.15, 0.13), DARK, bevel=0.03)
        m.box((0.0, 0.115 * s, -0.34), (0.19, 0.15, 0.30), SHELL, bevel=0.03)
    m.box((0.10, 0.115, -0.27), (0.06, 0.15, 0.11), DARK)
    m.box((0.10, -0.12, -0.30), (0.06, 0.14, 0.09), DARK, rot=(0, -6, 4))
    # belt, pouches, belt lamp
    m.box((0.0, 0.0, -0.12), (0.30, 0.36, 0.10), DARK, bevel=0.02)
    m.box((0.15, 0.14, -0.12), (0.09, 0.08, 0.10), SHELL, rot=(0, 0, 8), bevel=0.015)
    m.box((0.12, -0.05, -0.12), (0.05, 0.05, 0.05), DARK)
    m.sph((0.15, -0.05, -0.12), (0.07, 0.07, 0.07), GLOW, verts=8)
    # torso, scrap chest plate, tape band, shoulder plates
    m.box((0.0, 0.0, 0.11), (0.32, 0.42, 0.44), SHELL, bevel=0.05)
    m.box((0.175, -0.01, 0.15), (0.05, 0.32, 0.28), DARK, rot=(0, 0, 3), bevel=0.012)
    m.box((0.0, 0.0, 0.02), (0.34, 0.44, 0.05), SHELL, bevel=0.01)
    m.box((0.0, 0.27, 0.35), (0.24, 0.22, 0.06), SHELL, rot=(-22, 0, 4), bevel=0.02)
    m.box((-0.02, -0.26, 0.36), (0.30, 0.20, 0.05), DARK, rot=(18, -8, -6), bevel=0.02)
    m.box(local_point((0.0, 0.27, 0.35), (-22, 0, 4), (0, 0, 0.033)), (0.16, 0.14, 0.012), TEAM,
          rot=(-22, 0, 4))
    m.box(local_point((-0.02, -0.26, 0.36), (18, -8, -6), (0, 0, 0.03)), (0.20, 0.14, 0.012), TEAM,
          rot=(18, -8, -6))
    # helmet: dome, rim, visor, team stripe, side lamp
    m.vcyl((0.0, 0.0, 0.345), 0.06, 0.13, DARK)
    m.sph((0.0, 0.0, 0.47), (0.26, 0.26, 0.22), SHELL, verts=16)
    m.vcyl((0.0, 0.0, 0.395), 0.03, 0.29, DARK, verts=16)
    m.box((0.11, 0.0, 0.42), (0.06, 0.20, 0.06), DARK)
    m.box((0.0, 0.0, 0.575), (0.24, 0.09, 0.02), TEAM, bevel=0.006)
    m.box((0.08, -0.13, 0.50), (0.07, 0.06, 0.05), DARK)
    m.sph((0.115, -0.135, 0.50), (0.085, 0.085, 0.085), GLOW, verts=8)
    # riot shield (three panels), Team stripes, top band, scrap patch, rivets
    panels = ((0.31, 0.0, 0.02, 0.30, 0), (0.279, 0.245, 0.02, 0.20, 18), (0.279, -0.245, 0.02, 0.20, -18))
    for x, y, z, w, yaw in panels:
        rot = (0, -8, yaw)
        m.box((x, y, z), (0.06, w, 0.66), DARK, rot=rot, bevel=0.02)
        m.box(local_point((x, y, z), rot, (0, 0, 0.12)), (0.075, w, 0.10), TEAM, rot=rot)
        m.box(local_point((x, y, z), rot, (0, 0, 0.33)), (0.12, w, 0.06), TEAM, rot=rot, bevel=0.01)
        m.box(local_point((x, y, z), rot, (0, 0, 0.37)), (0.13, w, 0.02), SHELL, rot=rot)
    m.box(local_point((0.31, 0, 0.02), (0, -8, 0), (0.0, -0.05, -0.15)), (0.075, 0.14, 0.16), SHELL,
          rot=(0, -8, 12), bevel=0.01)
    for dz in (-0.25, 0.0):
        m.cyl(local_point((0.31, 0, 0.02), (0, -8, 0), (0.035, 0.11, dz)), (0.02, 0.03, 0.03), DARK, verts=8)
        m.cyl(local_point((0.31, 0, 0.02), (0, -8, 0), (0.035, -0.11, dz)), (0.02, 0.03, 0.03), DARK, verts=8)
    # shield arm, gauntlet
    m.rod((0.0, 0.25, 0.25), (0.26, 0.16, 0.02), 0.06, SHELL)
    m.sph((0.26, 0.16, 0.02), (0.11, 0.11, 0.11), DARK, verts=10)
    # sledgehammer over the right shoulder
    m.rod((0.0, -0.24, 0.24), (0.14, -0.29, 0.03), 0.06, SHELL)
    grip, tip = Vector((0.14, -0.29, 0.03)), Vector((-0.28, -0.20, 0.49))
    m.rod(grip, tip, 0.028, DARK, verts=8)
    for t0, t1 in ((0.12, 0.20), (0.50, 0.58)):
        m.rod(grip.lerp(tip, t0), grip.lerp(tip, t1), 0.036, SHELL, verts=8)
    m.box(tip + Vector((-0.01, -0.005, 0.03)), (0.15, 0.26, 0.15), DARK, rot=(0, -43, 0), bevel=0.02)
    m.sph((0.14, -0.29, 0.03), (0.10, 0.10, 0.10), DARK, verts=10)
    return m.finish()


def human_ranged(mats):
    m = Model("SM_Human_Ranged", mats)
    for s in (1, -1):
        m.box((0.03, 0.09 * s, -0.545), (0.22, 0.12, 0.11), DARK, bevel=0.025)
        m.box((0.0, 0.09 * s, -0.34), (0.12, 0.11, 0.32), SHELL, bevel=0.02)
    m.box((0.0, 0.0, -0.02), (0.20, 0.28, 0.30), SHELL, bevel=0.03)
    # poncho, hood, face shadow, goggle strip, team beret
    m.vcyl((0.0, 0.0, -0.04), 0.40, 0.46, SHELL, taper=0.5, verts=12)
    m.vcyl((0.0, 0.0, -0.235), 0.03, 0.47, DARK, verts=12)
    m.sph((0.0, 0.0, 0.29), (0.26, 0.26, 0.24), SHELL, verts=16)
    m.box((0.115, 0.0, 0.28), (0.08, 0.14, 0.10), DARK, bevel=0.015)
    m.box((0.155, 0.0, 0.30), (0.02, 0.12, 0.03), GLOW)
    m.sph((0.0, 0.0, 0.36), (0.27, 0.27, 0.15), TEAM, verts=16)
    # long rifle pointing +X, scope, tape
    m.box((0.0, -0.10, 0.14), (0.24, 0.06, 0.08), DARK, bevel=0.01)
    m.box((0.0, -0.10, 0.183), (0.24, 0.03, 0.012), SHELL)
    m.box((-0.14, -0.10, 0.12), (0.14, 0.05, 0.09), SHELL, rot=(0, -8, 0), bevel=0.01)
    m.box((0.16, -0.10, 0.14), (0.14, 0.05, 0.06), SHELL, bevel=0.01)
    m.cyl((0.28, -0.10, 0.14), (0.34, 0.035, 0.035), DARK, verts=8)
    m.cyl((0.02, -0.10, 0.205), (0.16, 0.04, 0.04), DARK, verts=8)
    m.sph((0.105, -0.10, 0.205), (0.05, 0.05, 0.05), GLOW, verts=8)
    for x in (0.22, 0.36):
        m.cyl((x, -0.10, 0.14), (0.03, 0.05, 0.05), SHELL, verts=8)
    # arms, team armband on the forward arm
    m.rod((0.0, -0.16, 0.10), (0.06, -0.11, 0.12), 0.04, SHELL, verts=8)
    a0, a1 = Vector((0.0, 0.16, 0.08)), Vector((0.18, -0.05, 0.13))
    m.rod(a0, a1, 0.04, SHELL, verts=8)
    m.rod(a0.lerp(a1, 0.32), a0.lerp(a1, 0.55), 0.056, TEAM, verts=10)
    # backpack, bedroll, lamp, bent antenna
    m.box((-0.17, 0.0, 0.06), (0.14, 0.24, 0.28), SHELL, bevel=0.03)
    m.ycyl((-0.17, 0.0, 0.25), 0.30, 0.12, DARK, verts=12)
    for y in (-0.09, 0.09):
        m.ycyl((-0.17, y, 0.25), 0.06, 0.135, TEAM, verts=12)
    m.box((-0.245, -0.06, 0.02), (0.03, 0.06, 0.05), DARK)
    m.sph((-0.265, -0.06, 0.02), (0.07, 0.07, 0.07), GLOW, verts=8)
    m.rod((-0.22, 0.08, 0.2), (-0.24, 0.10, 0.42), 0.012, DARK, verts=6)
    m.rod((-0.24, 0.10, 0.42), (-0.10, 0.16, 0.482), 0.012, DARK, verts=6)
    m.sph((-0.10, 0.16, 0.482), (0.05, 0.05, 0.05), GLOW, verts=8)
    return m.finish()


def human_siege(mats):
    m = Model("SM_Human_Siege", mats)
    # chunky wheels
    for sx in (1, -1):
        for sy in (1, -1):
            m.ycyl((0.36 * sx, 0.50 * sy, -0.40), 0.16, 0.40, DARK, bevel=0.03, verts=20)
            m.ycyl((0.36 * sx, 0.50 * sy, -0.40), 0.19, 0.20, SHELL, verts=12)
    # chassis, deck plates at odd angles, painted Team band on the deck
    m.box((0.0, 0.0, -0.22), (0.96, 0.64, 0.20), SHELL, bevel=0.03)
    m.box((-0.10, 0.0, -0.10), (0.5, 0.5, 0.05), DARK)
    m.box((0.05, 0.34, -0.16), (0.34, 0.04, 0.22), SHELL, rot=(-10, 0, 3))
    m.box((-0.05, -0.34, -0.18), (0.44, 0.04, 0.20), DARK, rot=(12, 0, -2))
    m.box((0.22, 0.0, -0.105), (0.22, 0.62, 0.03), TEAM)
    # cab with slit, roof plate, beacon, front work lamp
    m.box((-0.28, 0.06, 0.02), (0.30, 0.34, 0.24), SHELL, bevel=0.04)
    m.box((-0.128, 0.06, 0.06), (0.02, 0.24, 0.06), DARK)
    m.box((-0.28, 0.06, 0.155), (0.34, 0.38, 0.03), DARK)
    m.box((-0.22, 0.06, 0.176), (0.16, 0.28, 0.012), TEAM)
    for s in (1, -1):
        m.box((-0.28, 0.06 + 0.19 * s, 0.172), (0.34, 0.025, 0.02), SHELL)
    m.sph((-0.37, 0.06, 0.20), (0.10, 0.10, 0.10), GLOW, verts=8)
    m.box((0.44, 0.24, -0.06), (0.06, 0.08, 0.06), DARK)
    m.sph((0.475, 0.24, -0.06), (0.085, 0.085, 0.085), GLOW, verts=8)
    # exhaust pipe with rust band
    m.rod((-0.40, 0.27, -0.12), (-0.42, 0.27, 0.28), 0.035, DARK, verts=8)
    m.rod((-0.42, 0.27, 0.28), (-0.50, 0.27, 0.34), 0.035, DARK, verts=8)
    m.vcyl((-0.41, 0.27, 0.10), 0.05, 0.09, SHELL, verts=8)
    # Team flag on a rear pole, trailing behind and rolled toward the camera
    m.rod((-0.40, -0.22, -0.12), (-0.40, -0.22, 0.42), 0.02, DARK, verts=6)
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
    m.box((-0.25, 0.0, -0.35), (1.90, 2.0, 1.10), SHELL, bevel=0.05, seg=1)
    m.box((-0.25, 0.0, 0.29), (2.10, 2.15, 0.18), SHELL, bevel=0.04, seg=1)
    m.box((-0.25, 0.0, 0.395), (1.9, 1.95, 0.03), DARK)
    m.box((0.05, 0.0, 0.405), (0.36, 1.95, 0.035), TEAM)
    # door, lintel, Team paint band, bolted plates, amber lamps on posts
    m.box((0.68, 0.0, -0.45), (0.10, 0.70, 0.85), DARK)
    m.box((0.72, 0.0, -0.00), (0.08, 0.90, 0.05), SHELL, bevel=0.01)
    m.box((0.715, 0.0, 0.07), (0.05, 1.5, 0.14), TEAM)
    m.box((0.72, 0.78, -0.35), (0.04, 0.50, 0.45), DARK, rot=(0, 0, -5))
    m.box((0.72, -0.80, -0.30), (0.04, 0.42, 0.40), SHELL, rot=(4, 0, 3))
    for s in (1, -1):
        m.box((0.72, 0.98 * s, -0.35), (0.20, 0.20, 1.10), SHELL, bevel=0.02)
    for x in (0.2, -0.6):
        for s in (1, -1):
            m.box((x, 1.005 * s, -0.15), (0.50, 0.05, 0.07), DARK)
            m.box((x, 1.014 * s, -0.15), (0.42, 0.02, 0.02), GLOW)
    m.box((-0.25, 0.0, -0.55), (1.94, 2.04, 0.06), DARK)
    m.box((-0.55, -0.20, 0.50), (0.80, 0.50, 0.04), SHELL, rot=(6, -4, 12))
    for x, y in ((-0.85, -0.05), (-0.25, -0.35)):
        m.rod((x, y, 0.41), (x, y, 0.49), 0.02, DARK, verts=6)
    for s in (1, -1):
        m.rod((0.84, 0.58 * s, -0.90), (0.84, 0.58 * s, -0.15), 0.03, DARK, verts=6)
        m.box((0.78, 0.58 * s, -0.20), (0.12, 0.05, 0.05), DARK)
        m.sph((0.84, 0.58 * s, -0.16), (0.13, 0.13, 0.13), GLOW, verts=10)
        m.rod((0.70, 0.95 * s, 0.40), (0.70, 0.95 * s, 0.55), 0.025, DARK, verts=6)
        m.sph((0.70, 0.95 * s, 0.58), (0.11, 0.11, 0.11), GLOW, verts=10)
    # sandbags: front wings and side walls
    def bag(x, y, z, along):
        dims = (0.42, 0.30, 0.17) if along == "x" else (0.30, 0.42, 0.17)
        m.box((x + rng.uniform(-0.015, 0.015), y + rng.uniform(-0.015, 0.015), z), dims, SHELL,
              rot=(0, 0, rng.uniform(-7, 7)), bevel=0.055, seg=1)

    for s in (1, -1):
        for z, ys in ((-0.915, (0.62, 1.02, 1.26)), (-0.745, (0.82, 1.22)), (-0.575, (0.62, 1.02))):
            for y in ys:
                bag(1.28, y * s, z, "y")
        for z, xs in ((-0.915, (0.95, 0.53, 0.11, -0.31, -0.73, -1.15)),
                      (-0.745, (0.74, 0.32, -0.10, -0.52, -0.94))):
            for x in xs:
                bag(x, 1.27 * s, z, "x")
    # roof vents, antenna mast with guys, dish, beacon, banner pole
    m.box((-0.80, -0.55, 0.47), (0.30, 0.30, 0.14), DARK, rot=(0, 0, 8), bevel=0.02)
    m.rod((-0.80, -0.55, 0.50), (-0.80, -0.55, 0.72), 0.03, DARK, verts=6)
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
    # clutter: tyre stack and a rust barrel at the back corners
    m.tor((-1.15, -1.10, -0.90), 0.22, 0.10, DARK, verts=20)
    m.tor((-1.15, -1.10, -0.71), 0.22, 0.10, DARK, verts=20)
    m.vcyl((-1.30, 1.05, -0.78), 0.44, 0.34, SHELL, verts=14, bevel=0.015)
    m.vcyl((-1.30, 1.05, -0.78), 0.05, 0.36, DARK, verts=14)
    return m.finish()


def machine_hq(mats):
    m = Model("SM_Machine_HQ", mats)
    m.box((0.0, 0.0, -0.94), (2.94, 2.94, 0.12), DARK, bevel=0.02)
    slabs = ((2.70, -0.78, 0.20), (2.50, -0.54, 0.22), (2.30, -0.29, 0.22))
    for dim, z, h in slabs:
        m.box((0.0, 0.0, z), (dim, dim, h), SHELL, bevel=0.04, seg=2)
    m.box((0.0, 0.0, 0.375), (1.56, 1.56, 1.05), SHELL, bevel=0.05, seg=2)
    m.box((0.0, 0.0, 0.965), (1.36, 1.36, 0.07), SHELL, bevel=0.02)
    for dim, z in ((2.56, -0.665), (2.36, -0.415), (1.62, -0.165), (1.42, 0.915)):
        m.box((0.0, 0.0, z), (dim, dim, 0.03), GLOW)
    # server-rack vents and LEDs on the two big slabs, all four faces
    for k in range(4):
        rot = (0, 0, 90 * k)
        cs, sn = (round(math.cos(math.radians(90 * k))), round(math.sin(math.radians(90 * k))))
        for half, z in ((1.25, -0.54), (1.15, -0.29)):
            for y in (-0.75, -0.25, 0.25, 0.75):
                px, py = half + 0.005, y
                loc = (px * cs - py * sn, px * sn + py * cs, z)
                m.box(loc, (0.04, 0.30, 0.10), DARK, rot=rot)
                px = half + 0.02
                m.box((px * cs - (y + 0.11) * sn, px * sn + (y + 0.11) * cs, z), (0.02, 0.05, 0.03), GLOW, rot=rot)
    # core: corner seams, side slits, pylons, huge lens eye
    for sx in (1, -1):
        for sy in (1, -1):
            m.box((0.78 * sx, 0.78 * sy, 0.375), (0.05, 0.05, 0.98), GLOW)
            m.box((1.28 * sx, 1.28 * sy, -0.405), (0.24, 0.24, 0.55), SHELL, bevel=0.03)
            m.box((1.28 * sx, 1.28 * sy, -0.115), (0.18, 0.18, 0.03), GLOW)
    for sy in (1, -1):
        m.box((0.0, 0.78 * sy, 0.55), (1.0, 0.02, 0.03), GLOW)
        m.box((0.0, 0.78 * sy, 0.10), (1.0, 0.02, 0.03), GLOW)
    m.box((-0.78, 0.0, 0.55), (0.02, 1.0, 0.03), GLOW)
    m.box((-0.78, 0.0, 0.10), (0.02, 1.0, 0.03), GLOW)
    for x in (-0.6, -0.4, -0.2, 0.0, 0.2, 0.4, 0.6):
        for sy in (1, -1):
            m.box((x, 0.78 * sy, 0.325), (0.06, 0.03, 0.32), DARK)
    m.cyl((0.705, 0.0, 0.34), (0.16, 1.10, 1.10), DARK, verts=32)
    m.eye((0.62, 0.0, 0.34), 0.46, aim=(1, 0, 0.45))
    # spire with seam rings
    m.vcyl((0.0, 0.0, 1.39), 0.78, 0.18, SHELL, taper=0.2, verts=12)
    for z, r in ((1.18, 0.083), (1.42, 0.056)):
        m.tor((0.0, 0.0, z), r, 0.012, GLOW, verts=16)
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
    assert triangles(obj) < 6000, "%s has %d triangles" % (name, triangles(obj))
    lo, hi = bounds(obj)
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
