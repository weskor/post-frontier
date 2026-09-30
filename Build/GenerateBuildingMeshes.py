"""Generate the construction-loop building meshes for CoopRTS in Blender.

Run headless from the repo root (Blender 5.2.1, no assets, no network, deterministic):

    blender -b --factory-startup -P Build/GenerateBuildingMeshes.py

Needs Art/Units/Units.blend (from Build/GenerateUnitMeshes.py) only for PreviewField.png, where the existing
unit meshes stand next to the buildings for scale. GenerateUnitMeshes.py is executed as a library (its final
`main()` call is cut off), so Model / Frame / material helpers, FBX axis settings and the preview colours are
shared with the units and that script's outputs are untouched.

Footprints come from ACommandBuilding::GetFootprintRadius (a square of half-extent in cm): Barracks 1.25 m,
Outpost 0.95 m, Workshop 1.45 m. The actor origin is the footprint centre and the footprint box is 1.3 m
tall, so mesh ground is z = -0.65. Metres, +Z up, forward = +X (door, ramp, crane jib and dish point +X).
Every building keeps inside +/-half-extent except roofs, antennas and ramps, which may overhang up to +10%.
All model code writes heights above the ground; BModel.add shifts them by GROUND.

Meshes (one object per FBX, exported at the origin, same FBX settings as the units)
    SM_Human_Barracks   SM_Human_Barracks_Frontline   SM_Human_Barracks_Ranged   SM_Human_Barracks_Siege
    SM_Human_Outpost    SM_Human_Workshop
    SM_Machine_Barracks SM_Machine_Barracks_Frontline SM_Machine_Barracks_Ranged SM_Machine_Barracks_Siege
    SM_Machine_Outpost  SM_Machine_Workshop
    SM_Construction_Barracks SM_Construction_Outpost SM_Construction_Workshop   (faction neutral)
A Barracks is configured once and permanently as Frontline, Ranged or Siege (no level 2). SM_*_Barracks is the
neutral, unconfigured building; each role mesh is the same base (same footprint, origin, slots) plus a role
add-on that echoes the unit it produces (barracks_role functions, verify() requires >500 extra triangles).

Art direction (Saved/AgentBriefs/sc2-style.md): StarCraft 2-style stylized sci-fi, not scrap. Human = rugged
blue-collar industrial future-military (Terran-like): armoured hulls on landing struts, blast doors, hazard
stripes, reactor stacks, floodlights, big Team-painted plates. Machine = elegant pearl AI (Protoss-like):
hovering shell segments split by Glow gaps, swept fins, crystals, floating arcs, one red lens. Every major
form is a chamfered plate (Bevel 2-3 segments) or a revolved / lofted curve; the finished mesh gets a Weighted
Normal pass. Team plates are big: 11-22 % of the footprint box as upward-facing Team area on the Barracks and
Workshops, more on the slim Outposts (dish, base ring) and on the amber-painted scaffolds (see the run table).

Material slots, in this order on every mesh (all four are used by every mesh):
    0 Team   ownership / build state tint, BIG painted plates visible from above (roof plates, dish, deck,
             status rings, crate lids). Amber while building, then team colour. The Machine lens is Team too.
    1 Shell  painted metal body (Human gunmetal / steel-blue, Machine pearl white)
    2 Dark   mechanical parts, underside, ground pad, door bays
    3 Glow   emissive lights and strips (Human amber, Machine cyan)
Budgets asserted in check(): at most 20000 triangles per building and 10000 per construction scaffold.
Scaffolds obey the same footprint rule (crane arms may overhang up to +10%).

Outputs (Art/Buildings/, relative to the repo root)
    SM_*.fbx            fifteen meshes
    Buildings.blend     the same meshes in a grid for editing (NOT at the origin; always export from this
                        script, never from the .blend), with faction preview materials linked per object.
                        Rewritten on EVERY run, including when a build step or check fails.
    Preview.png         side view (orthographic, looks along +Y, +X is right). Rows top to bottom: Machine,
                        Human, Construction. Columns: Barracks (neutral), Barracks_Frontline, Barracks_Ranged,
                        Barracks_Siege, Outpost, Workshop (scaffolds sit under Barracks, Outpost, Workshop).
    PreviewRTS.png      same lineup from a 50 degree RTS camera (25 degrees azimuth, fronts toward camera)
    PreviewField.png    RTS distance on the Campus Zero asphalt/scrapyard albedo under dusk light with
                        Frontline / Ranged / Siege units next to the buildings for scale
"""
import math
import os
import sys
import types

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

try:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:
    ROOT = os.getcwd()
OUT = os.path.join(ROOT, "Art", "Buildings")
UNITS_SCRIPT = os.path.join(ROOT, "Build", "GenerateUnitMeshes.py")
UNITS_BLEND = os.path.join(ROOT, "Art", "Units", "Units.blend")


def _load_unit_helpers():
    """Run GenerateUnitMeshes.py as a library: everything except its trailing main() call."""
    with open(UNITS_SCRIPT) as handle:
        source = handle.read().rstrip()
    assert source.endswith("\nmain()"), "GenerateUnitMeshes.py no longer ends with main()"
    namespace = {"__file__": UNITS_SCRIPT, "__name__": "unit_helpers"}
    exec(compile(source[:-len("main()")], UNITS_SCRIPT, "exec"), namespace)
    return types.SimpleNamespace(**namespace)


U = _load_unit_helpers()
TEAM, SHELL, DARK, GLOW = U.TEAM, U.SHELL, U.DARK, U.GLOW
Frame = U.Frame

GROUND = -0.65
HALF = {"Barracks": 1.25, "Outpost": 0.95, "Workshop": 1.45}
OVERHANG = 1.10
TRI_BUDGET_BUILDING = 20000
TRI_BUDGET_SCAFFOLD = 10000
MIN_TEAM_TOP_AREA = {"building": 0.6, "scaffold": 0.4}   # m^2 of Team faces looking up (normal.z > 0.5)
# min / max height above ground (m): the Outpost is the tall beacon, the Workshop the low wide one
HEIGHT_RANGE = {
    "Barracks": (1.7, 2.3), "Barracks_Frontline": (1.7, 2.6), "Barracks_Ranged": (1.7, 2.6),
    "Barracks_Siege": (1.7, 2.6), "Outpost": (3.4, 4.0), "Workshop": (1.6, 2.3),
}
SCAFFOLD_HEIGHT_RANGE = {"Barracks": (1.9, 2.5), "Outpost": (2.6, 3.4), "Workshop": (1.7, 2.4)}


# --------------------------------------------------------------------------------------
# 2D profile helpers (plan-view outlines, centred on the origin, counter-clockwise)
# --------------------------------------------------------------------------------------
def rrect(w, d, r, n=2):
    """Rounded rectangle w (x) by d (y), corner radius r, n segments per corner (n = 1 is a plain chamfer)."""
    pts = []
    for (sx, sy), a0 in (((1, 1), 0.0), ((-1, 1), 90.0), ((-1, -1), 180.0), ((1, -1), 270.0)):
        cx, cy = sx * (w / 2 - r), sy * (d / 2 - r)
        for k in range(n + 1):
            a = math.radians(a0 + 90.0 * k / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def ellipse(a, b, n=28):
    return [(a * math.cos(2 * math.pi * i / n), b * math.sin(2 * math.pi * i / n)) for i in range(n)]


def hexagon(r):
    return [(r * math.cos(math.radians(60 * i)), r * math.sin(math.radians(60 * i))) for i in range(6)]


def clip_rect(pts, u0, u1, v0, v1):
    """Sutherland-Hodgman clip of a convex polygon to a rectangle."""
    def clip(poly, inside, cut):
        out = []
        for i, p in enumerate(poly):
            q = poly[(i + 1) % len(poly)]
            if inside(p):
                out.append(p)
                if not inside(q):
                    out.append(cut(p, q))
            elif inside(q):
                out.append(cut(p, q))
        return out

    def cut_u(val):
        return lambda p, q: (val, p[1] + (q[1] - p[1]) * (val - p[0]) / (q[0] - p[0]))

    def cut_v(val):
        return lambda p, q: (p[0] + (q[0] - p[0]) * (val - p[1]) / (q[1] - p[1]), val)

    for inside, cut in ((lambda p: p[0] >= u0, cut_u(u0)), (lambda p: p[0] <= u1, cut_u(u1)),
                        (lambda p: p[1] >= v0, cut_v(v0)), (lambda p: p[1] <= v1, cut_v(v1))):
        pts = clip(pts, inside, cut) if pts else pts
    return pts


# --------------------------------------------------------------------------------------
# Geometry builder: the unit Model with heights measured above the ground, plus hard-surface primitives
# --------------------------------------------------------------------------------------
class BModel(U.Model):
    def add(self, kind, loc, dims, mat, **kw):
        super().add(kind, Vector(loc) + Vector((0.0, 0.0, GROUND)), dims, mat, **kw)

    def bx(self, x0, x1, y0, y1, z0, z1, mat, **kw):
        """Box by extents (any order). Armour plates use bevel=0.03..0.06 with seg=3."""
        lo = (min(x0, x1), min(y0, y1), min(z0, z1))
        hi = (max(x0, x1), max(y0, y1), max(z0, z1))
        kw.setdefault("seg", 3 if kw.get("bevel", 0.0) >= 0.025 else 2)
        self.box(tuple((a + b) / 2.0 for a, b in zip(lo, hi)), tuple(b - a for a, b in zip(lo, hi)), mat, **kw)

    # -- raw bmesh parts ------------------------------------------------------------
    def _commit(self, bm, mat, bevel=0.0, seg=3):
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        for face in bm.faces:
            face.material_index = mat
        mesh = bpy.data.meshes.new("%s_part%d" % (self.name, len(self.parts)))
        bm.to_mesh(mesh)
        bm.free()
        for material in self.materials:
            mesh.materials.append(material)
        obj = bpy.data.objects.new(mesh.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        self.parts.append(obj)
        if bevel > 0.0:
            mod = obj.modifiers.new("Bevel", "BEVEL")
            mod.width = bevel
            mod.segments = seg
            mod.limit_method = "ANGLE"
            mod.angle_limit = math.radians(40.0)
            with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
                bpy.ops.object.modifier_apply(modifier=mod.name)

    def poly(self, pts, x0, x1, mat, loc=(0.0, 0.0), rotz=0.0, bevel=0.0, seg=3):
        """Prism: polygon `pts` (y, z above ground) extruded from local x0 to x1, turned rotz degrees about
        Z, then moved to `loc` (x, y). Convex profiles only."""
        bm = bmesh.new()
        r0 = [bm.verts.new((x0, y, z + GROUND)) for y, z in pts]
        r1 = [bm.verts.new((x1, y, z + GROUND)) for y, z in pts]
        n = len(pts)
        bm.faces.new(r0)
        bm.faces.new(list(reversed(r1)))
        for i in range(n):
            bm.faces.new((r0[i], r0[(i + 1) % n], r1[(i + 1) % n], r1[i]))
        bmesh.ops.transform(bm, matrix=Matrix.Translation((loc[0], loc[1], 0.0))
                            @ Matrix.Rotation(math.radians(rotz), 4, "Z"), verts=bm.verts)
        self._commit(bm, mat, bevel, seg)

    def blade(self, pts_xz, y0, y1, mat, bevel=0.0, seg=3):
        """Plate in the XZ plane (pts are (x, z above ground)) extruded from y0 to y1."""
        self.poly([(-x, z) for x, z in pts_xz], y0, y1, mat, rotz=90.0, bevel=bevel, seg=seg)

    def plan(self, pts, z0, z1, mat, loc=(0.0, 0.0), taper=1.0, rot=0.0, bevel=0.0, seg=3):
        """Plan-view prism: outline `pts` (x, y) from z0 to z1 above ground; the top outline is scaled by
        `taper` (float or (x, y)) about the outline origin, giving sloped armour. Turned by rot degrees."""
        tx, ty = taper if isinstance(taper, tuple) else (taper, taper)
        bm = bmesh.new()
        lo = [bm.verts.new((x, y, z0 + GROUND)) for x, y in pts]
        hi = [bm.verts.new((x * tx, y * ty, z1 + GROUND)) for x, y in pts]
        n = len(pts)
        bm.faces.new(lo)
        bm.faces.new(hi)
        for i in range(n):
            bm.faces.new((lo[i], lo[(i + 1) % n], hi[(i + 1) % n], hi[i]))
        bmesh.ops.transform(bm, matrix=Matrix.Translation((loc[0], loc[1], 0.0))
                            @ Matrix.Rotation(math.radians(rot), 4, "Z"), verts=bm.verts)
        self._commit(bm, mat, bevel, seg)

    def lathe(self, profile, loc, mat, verts=16, aim=None, closed=False, bevel=0.0, seg=2, scale=1.0):
        """Surface of revolution about local Z. profile = [(r, z), ...] bottom to top (r = 0 lands on the
        axis); closed=True joins the last point to the first (rings such as an annulus). `aim` turns local Z
        onto that direction. loc is the local origin (z above ground)."""
        bm = bmesh.new()
        rings = []
        for r, z in profile:
            if r <= 1e-9:
                rings.append([bm.verts.new((0.0, 0.0, z))])
            else:
                rings.append([bm.verts.new((r * math.cos(2 * math.pi * i / verts),
                                            r * math.sin(2 * math.pi * i / verts), z)) for i in range(verts)])
        count = len(rings) if closed else len(rings) - 1
        for i in range(count):
            a, b = rings[i], rings[(i + 1) % len(rings)]
            if len(a) == 1 and len(b) == 1:
                continue
            for j in range(verts):
                k = (j + 1) % verts
                if len(a) == 1:
                    bm.faces.new((a[0], b[j], b[k]))
                elif len(b) == 1:
                    bm.faces.new((a[j], b[0], a[k]))
                else:
                    bm.faces.new((a[j], a[k], b[k], b[j]))
        if not closed:
            for ring in (rings[0], rings[-1]):
                if len(ring) > 1:
                    bm.faces.new(ring)
        rot = Matrix.Identity(3)
        if aim is not None:
            a = Vector(aim).normalized()
            rot = a.to_track_quat("Z", "Y" if abs(a.y) < 0.99 else "X").to_matrix()
        bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(loc) + Vector((0, 0, GROUND)))
                            @ (rot @ Matrix.Scale(scale, 3)).to_4x4(), verts=bm.verts)
        self._commit(bm, mat, bevel, seg)

    def arc(self, major, minor, a0, a1, loc, mat, segs=12, minor_segs=8, aim=None):
        """Torus arc (a thick ring segment) from angle a0 to a1 degrees about local Z at loc."""
        bm = bmesh.new()
        rings = []
        full = abs(a1 - a0) >= 360.0
        for i in range(segs + (0 if full else 1)):
            t = math.radians(a0 + (a1 - a0) * i / segs)
            ring = []
            for j in range(minor_segs):
                v = 2 * math.pi * j / minor_segs
                r = major + minor * math.cos(v)
                ring.append(bm.verts.new((r * math.cos(t), r * math.sin(t), minor * math.sin(v))))
            rings.append(ring)
        for i in range(segs if full else segs):
            a, b = rings[i], rings[(i + 1) % len(rings)]
            for j in range(minor_segs):
                k = (j + 1) % minor_segs
                bm.faces.new((a[j], b[j], b[k], a[k]))
        if not full:
            bm.faces.new(rings[0])
            bm.faces.new(list(reversed(rings[-1])))
        rot = Matrix.Identity(3)
        if aim is not None:
            a = Vector(aim).normalized()
            rot = a.to_track_quat("Z", "Y" if abs(a.y) < 0.99 else "X").to_matrix()
        bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(loc) + Vector((0, 0, GROUND))) @ rot.to_4x4(),
                            verts=bm.verts)
        self._commit(bm, mat)

    def decal(self, pts_uv, origin, u_axis, v_axis, mat, outward, thick=0.012):
        """Thin painted plate: 2D polygon in (u, v) on the surface plane spanned by u_axis and v_axis at
        `origin`, raised `thick` toward `outward` (sunk 4 mm into the surface to avoid gaps)."""
        U_, V_ = Vector(u_axis).normalized(), Vector(v_axis).normalized()
        n = U_.cross(V_)
        if n.dot(Vector(outward)) < 0:
            n = -n
        o = Vector(origin) + Vector((0, 0, GROUND)) - n * 0.004
        bm = bmesh.new()
        lo = [bm.verts.new(o + U_ * u + V_ * v) for u, v in pts_uv]
        hi = [bm.verts.new(o + U_ * u + V_ * v + n * (thick + 0.004)) for u, v in pts_uv]
        c = len(pts_uv)
        bm.faces.new(lo)
        bm.faces.new(hi)
        for i in range(c):
            bm.faces.new((lo[i], lo[(i + 1) % c], hi[(i + 1) % c], hi[i]))
        self._commit(bm, mat)

    def hazard(self, origin, u_axis, v_axis, w, h, outward, mat=GLOW, base=DARK, period=0.14, thick=0.012):
        """Hazard-stripe panel of w x h on a surface: dark base plate with slanted stripes in `mat`."""
        self.decal([(0, 0), (w, 0), (w, h), (0, h)], origin, u_axis, v_axis, base, outward, thick * 0.5)
        s = -h
        while s < w:
            pts = clip_rect([(s, 0), (s + period / 2, 0), (s + period / 2 + h, h), (s + h, h)], 0, w, 0, h)
            if len(pts) >= 3:
                self.decal(pts, origin, u_axis, v_axis, mat, outward, thick)
            s += period

    # -- hard-surface parts -----------------------------------------------------------
    def strut(self, p0, p1, r0, mat=SHELL, r1=None, verts=8):
        self.rod(p0, p1, r0, mat, r1=r1, verts=verts)

    def piston(self, p0, p1, r=0.03):
        """Hydraulic piston: Shell sleeve over the first half, Dark rod out of it."""
        p0, p1 = Vector(p0), Vector(p1)
        mid = p0.lerp(p1, 0.55)
        self.rod(p0, mid, r * 1.6, SHELL, verts=8)
        self.rod(mid, p1, r, DARK, verts=8)

    def capsule(self, p0, p1, r, mat, verts=14):
        p0, p1 = Vector(p0), Vector(p1)
        self.rod(p0, p1, r, mat, verts=verts)
        self.sph(p0, (2 * r,) * 3, mat, verts=verts)
        self.sph(p1, (2 * r,) * 3, mat, verts=verts)

    def flood(self, x, y, z0, z1, tilt=18):
        """Floodlight on a post, lens facing +X: Dark housing, Glow lens."""
        self.rod((x, y, z0), (x, y, z1), 0.022, DARK, verts=6)
        self.box((x, y, z1 + 0.03), (0.10, 0.26, 0.16), DARK, rot=(0, tilt, 0), bevel=0.02, seg=2)
        self.box((x + 0.055, y, z1 + 0.03 - 0.02), (0.02, 0.21, 0.11), GLOW, rot=(0, tilt, 0))

    def stack(self, x, y, z0, z1, dia, glow_z=None):
        """Reactor / exhaust stack: chamfered Shell cylinder, Dark collars, Glow ring, lit Dark cap."""
        self.vcyl((x, y, (z0 + z1) / 2.0), z1 - z0, dia, SHELL, verts=16, bevel=0.025, seg=3)
        self.vcyl((x, y, z0 + 0.03), 0.06, dia * 1.14, DARK, verts=16, bevel=0.012)
        self.vcyl((x, y, z1 - 0.02), 0.05, dia * 1.10, DARK, verts=16, bevel=0.012)
        gz = glow_z if glow_z is not None else z0 + (z1 - z0) * 0.62
        self.tor((x, y, gz), dia / 2 + 0.006, 0.014, GLOW, verts=20)
        self.vcyl((x, y, z1 + 0.004), 0.012, dia * 0.7, GLOW, verts=16)

    def dish(self, base, aim, dia, mat=SHELL, rim=None):
        """Radar dish: thin bowl opening along `aim`, optional rim ring, feed arm and a Glow feed tip."""
        base = Vector(base)
        a = Vector(aim).normalized()
        s = dia / 1.04
        prof = [(0.0, -0.05), (0.15, -0.045), (0.35, 0.02), (0.52, 0.14), (0.52, 0.17), (0.48, 0.165), (0.32, 0.075),
                (0.12, 0.012), (0.0, 0.0)]
        self.lathe(prof, base, mat, verts=20, aim=a, scale=s)
        if rim is not None:
            self.arc(0.52 * s, 0.02 * s, 0, 360, base + a * (0.155 * s), rim, segs=20, aim=a)
        tip = base + a * (0.30 * s)
        self.rod(base, tip, 0.012, DARK, verts=6)
        self.sph(tip, (0.05, 0.05, 0.05), GLOW, verts=8)

    def crate(self, x, y, z0, s, top=SHELL, h=None):
        """Cargo container: chamfered Shell body, Dark corner posts, painted lid `top`."""
        h = s if h is None else h
        self.bx(x - s / 2, x + s / 2, y - s / 2, y + s / 2, z0, z0 + h - 0.03, SHELL, bevel=0.03)
        for sx in (-1, 1):
            for sy in (-1, 1):
                self.bx(x + sx * s / 2 - 0.03, x + sx * s / 2 + 0.03, y + sy * s / 2 - 0.03, y + sy * s / 2 + 0.03,
                        z0, z0 + h - 0.02, DARK, bevel=0.01)
        self.bx(x - s * 0.52, x + s * 0.52, y - s * 0.52, y + s * 0.52, z0 + h - 0.05, z0 + h, top, bevel=0.02)

    def finish(self):
        obj = super().finish()
        mod = obj.modifiers.new("WeightedNormal", "WEIGHTED_NORMAL")
        mod.keep_sharp = True
        mod.weight = 50
        with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        return obj


# --------------------------------------------------------------------------------------
# Human Barracks (Terran-like prefab): armoured hull on landing struts, blast door + ramp, big Team roof plates
# --------------------------------------------------------------------------------------
HB_CX = -0.10      # hull centre x
HB_BOT = 0.18      # hull underside above the ground


def human_barracks(mats, role=None):
    """Neutral Barracks (role None) or one of the three permanent configurations."""
    m = BModel("SM_Human_Barracks" + ("_" + role if role else ""), mats)
    cx, hb = HB_CX, HB_BOT
    # landing struts: chamfered pads, splayed struts with pistons
    for px in (-1.02, 0.82):
        for py in (-0.93, 0.93):
            m.bx(px - 0.19, px + 0.19, py - 0.19, py + 0.19, 0.0, 0.07, DARK, bevel=0.03)
            top = (px + (cx - px) * 0.28, py * 0.80, hb + 0.03)
            m.strut((px, py, 0.10), top, 0.085, SHELL, r1=0.065)
            m.piston((px + (cx - px) * 0.18, py * 1.02, 0.12), (px + (cx - px) * 0.30, py * 0.86, hb + 0.02), 0.028)
    # belly plate lit from below, lower hull, dark break, upper deck
    m.plan(rrect(1.70, 1.70, 0.20, 1), 0.10, hb, DARK, loc=(cx, 0), bevel=0.02)
    m.plan(rrect(1.78, 1.78, 0.22, 1), 0.135, 0.155, GLOW, loc=(cx, 0))
    m.plan(rrect(2.10, 2.10, 0.24, 2), hb, 0.84, SHELL, loc=(cx, 0), bevel=0.045)
    m.plan(rrect(2.03, 2.03, 0.22, 2), 0.83, 0.87, DARK, loc=(cx, 0))
    m.plan(rrect(1.96, 1.96, 0.20, 2), 0.86, 1.08, SHELL, loc=(cx, 0), bevel=0.04)
    # side walls: raised armour plates (the middle one is a big Team plate), hazard skirt, vent, light strip
    for s in (1, -1):
        for x0, x1, mat in ((-0.90, -0.45, SHELL), (-0.38, 0.18, TEAM), (0.25, 0.70, SHELL)):
            m.bx(x0, x1, s * 1.05, s * 1.088, 0.27, 0.80, mat, bevel=0.022)
        m.hazard((0.70, s * 1.05, 0.19) if s > 0 else (-0.90, s * 1.05, 0.19), (-s, 0, 0) if s > 0 else (1, 0, 0),
                 (0, 0, 1), 1.60, 0.07, (0, s, 0))
        m.bx(-0.85, 0.65, s * 0.975, s * 0.992, 1.03, 1.05, GLOW)
        Frame.surface(m, (-0.10, s * 0.98, 0.94), (0, s, 0), (1, 0, 0)).vent((0, 0, 0), (0.60, 0.12), slats=3)
    # front: blast door (frame, dark bay, lit interior, parted leaves with hazard stripes), Team canopy
    m.bx(0.93, 1.02, -0.64, 0.64, hb, 0.80, SHELL, bevel=0.03)
    m.bx(1.015, 1.05, -0.48, 0.48, 0.20, 0.74, DARK)
    m.bx(1.045, 1.055, -0.30, 0.30, 0.62, 0.66, GLOW)
    m.bx(1.045, 1.055, -0.42, 0.42, 0.205, 0.225, GLOW)
    for s in (1, -1):
        m.bx(1.03, 1.10, s * 0.26, s * 0.62, 0.20, 0.78, SHELL, bevel=0.025)
        m.hazard((1.10, s * 0.28, 0.24) if s > 0 else (1.10, s * 0.60, 0.24), (0, 1, 0), (0, 0, 1), 0.32, 0.50,
                 (1, 0, 0))
    m.bx(0.86, 1.16, -0.74, 0.74, 0.86, 0.92, SHELL, bevel=0.03)
    m.bx(0.90, 1.13, -0.70, 0.70, 0.92, 0.955, TEAM, bevel=0.02)
    m.bx(1.05, 1.14, -0.60, 0.60, 0.845, 0.86, GLOW)
    for s in (1, -1):
        m.flood(1.10, s * 0.68, 0.955, 1.16)
    # lowered ramp with Glow guide lights, rails and hydraulics
    ramp_l, ramp_t, ang = 0.39, 0.05, math.radians(29)
    rz = 0.5 * ramp_l * math.sin(ang) + 0.5 * ramp_t * math.cos(ang)
    m.box((1.17, 0.0, rz), (ramp_l, 1.10, ramp_t), SHELL, rot=(0, 29, 0), bevel=0.012, seg=2)
    for s in (1, -1):
        m.box((1.17, 0.58 * s, rz + 0.05), (ramp_l, 0.05, 0.09), DARK, rot=(0, 29, 0), bevel=0.012, seg=2)
        m.piston((1.02, 0.50 * s, 0.44), (1.22, 0.50 * s, 0.12), 0.022)
    for t in (-0.12, 0.0, 0.12):
        m.box((1.17 + t * math.cos(ang), 0.0, rz + 0.03 - t * math.sin(ang)), (0.03, 0.55, 0.012), GLOW,
              rot=(0, 29, 0))
    # shoulder pauldrons on the front corners: chamfered armour pods with Team caps
    for s in (1, -1):
        m.plan(rrect(0.50, 0.46, 0.10, 2), 0.64, 1.16, SHELL, loc=(0.76, s * 0.83), taper=0.9, bevel=0.04)
        m.plan(rrect(0.36, 0.32, 0.07, 2), 1.16, 1.19, TEAM, loc=(0.76, s * 0.83), taper=0.9, bevel=0.02)
        m.bx(0.55, 1.02, s * 0.83 - 0.02, s * 0.83 + 0.02, 0.60, 0.64, GLOW)
    # roof: two big Team plates, engine deck with vents, reactor stacks
    for s in (1, -1):
        m.bx(-0.75, 0.35, s * 0.52, s * 0.90, 1.08, 1.12, TEAM, bevel=0.03)
    m.plan(rrect(1.20, 0.80, 0.12, 2), 1.08, 1.24, SHELL, loc=(-0.15, 0), taper=(0.86, 0.8), bevel=0.03)
    for x in (0.10, 0.20, 0.30):
        m.bx(x - 0.02, x + 0.02, -0.26, 0.26, 1.235, 1.255, DARK)
    for s in (1, -1):
        m.stack(0.22, 0.34 * s, 1.20, 1.62, 0.24)
    # rear exhausts, antenna mast, small dish
    for s in (1, -1):
        m.cyl((-1.19, 0.50 * s, 0.50), (0.16, 0.30, 0.30), DARK, aim=(-1, 0, 0), verts=16)
        m.cyl((-1.28, 0.50 * s, 0.50), (0.03, 0.22, 0.22), GLOW, aim=(-1, 0, 0), verts=16)
    m.rod((-0.98, 0.80, 1.08), (-0.98, 0.80, 2.05), 0.022, DARK, verts=6)
    m.sph((-0.98, 0.80, 2.07), (0.06, 0.06, 0.06), GLOW, verts=8)
    m.rod((-0.95, -0.80, 1.08), (-0.95, -0.80, 1.35), 0.03, DARK, verts=8)
    m.dish((-0.95, -0.80, 1.45), (1, 0, 0.65), 0.55)
    if role:
        HUMAN_ROLES[role](m)
    return m.finish()


# --------------------------------------------------------------------------------------
# Machine Barracks: hovering pearl shell segments over a plinth, arched portal with hover steps, one red lens
# --------------------------------------------------------------------------------------
def arch_profile(half_w, z0, z_spring, rise, n=9):
    """Arched portal outline in (y, z): straight sides up to z_spring, half-ellipse of `rise` above."""
    pts = [(-half_w, z0), (half_w, z0), (half_w, z_spring)]
    for i in range(1, n):
        t = math.pi * i / n
        pts.append((half_w * math.cos(t), z_spring + rise * math.sin(t)))
    pts.append((-half_w, z_spring))
    return pts


def machine_barracks(mats, role=None):
    """Neutral Barracks (role None) or one of the three permanent configurations."""
    m = BModel("SM_Machine_Barracks" + ("_" + role if role else ""), mats)
    # plinth with a lit rim and four antigrav emitter posts at the corners
    m.plan(rrect(2.44, 2.44, 0.55, 4), 0.0, 0.07, DARK, bevel=0.02)
    m.plan(rrect(2.38, 2.38, 0.52, 4), 0.07, 0.08, GLOW)
    m.plan(rrect(2.32, 2.32, 0.50, 4), 0.08, 0.09, DARK)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.lathe([(0.13, 0.07), (0.10, 0.14), (0.06, 0.30), (0.0, 0.34)], (sx * 1.0, sy * 1.0, 0.0), SHELL, verts=14)
            m.sph((sx * 1.0, sy * 1.0, 0.33), (0.08, 0.08, 0.08), GLOW, verts=10)
    # lower shell hovering on a Glow underlight, cyan waist seam, Team crescents on its shoulder
    m.plan(rrect(1.50, 1.50, 0.50, 4), 0.19, 0.25, GLOW)
    m.plan(rrect(2.10, 2.10, 0.55, 4), 0.25, 0.80, SHELL, taper=0.94, bevel=0.06)
    m.plan(rrect(2.06, 2.06, 0.535, 4), 0.505, 0.525, GLOW)
    for s in (1, -1):
        m.bx(-0.55, 0.55, s * 0.72, s * 0.95, 0.80, 0.835, TEAM, bevel=0.025)
    # upper shell: a separate floating segment, tapered, with a Glow gap and a big Team top plate
    m.plan(rrect(1.44, 1.24, 0.42, 4), 0.80, 0.865, GLOW)
    m.plan(rrect(1.50, 1.30, 0.45, 4), 0.865, 1.30, SHELL, loc=(-0.05, 0), taper=0.80, bevel=0.06)
    m.plan(ellipse(0.40, 0.32, 28), 1.30, 1.335, TEAM, loc=(0.02, 0), bevel=0.02)
    m.lens((0.60, 0.0, 1.06), 0.19, aim=(1, 0, 0.2), clamps=4, collar=0.5)
    # arched portal with Glow outline and three hover steps
    m.poly(arch_profile(0.43, 0.24, 0.50, 0.25), 0.985, 1.005, GLOW)
    m.poly(arch_profile(0.38, 0.24, 0.50, 0.22), 0.99, 1.03, DARK)
    m.bx(1.025, 1.035, -0.24, 0.24, 0.44, 0.46, GLOW)
    for i, (x0, x1, z0) in enumerate(((1.03, 1.14, 0.18), (1.16, 1.27, 0.12), (1.29, 1.37, 0.06))):
        m.bx(x0, x1, -0.42, 0.42, z0, z0 + 0.04, SHELL, bevel=0.015)
        m.bx(x0 + 0.01, x1 - 0.01, -0.36, 0.36, z0 - 0.02, z0, GLOW)
    # swept crest fins with Glow inlays, mirrored
    for s in (1, -1):
        y0, y1 = (0.28, 0.35) if s > 0 else (-0.35, -0.28)
        m.blade([(-0.10, 1.06), (-0.90, 1.02), (-1.35, 1.85), (-0.85, 1.40)], y0, y1, SHELL, bevel=0.015, seg=2)
        yi = (0.35, 0.358) if s > 0 else (-0.358, -0.35)
        m.blade([(-0.30, 1.08), (-0.86, 1.05), (-1.18, 1.60), (-0.86, 1.34)], yi[0], yi[1], GLOW)
    # shoulder fins beside the lens, waist plates
    for s in (1, -1):
        m.blade([(0.20, 0.86), (0.66, 0.90), (0.50, 1.18), (0.15, 1.10)], s * 0.62 - 0.03, s * 0.62 + 0.03, SHELL,
                bevel=0.012, seg=2)
    if role:
        MACHINE_ROLES[role](m)
    return m.finish()


# --------------------------------------------------------------------------------------
# Barracks role add-ons (each Barracks is configured once as Frontline, Ranged or Siege). Everything goes on
# top of the neutral base and echoes the unit the building produces.
# --------------------------------------------------------------------------------------
def human_barracks_frontline(m):
    """Frontline (Luddite, riot shield): a heavy armoured gate with a half-raised portcullis and Team lintel
    around the door, and a rack of leaning riot shields (alternating Team / Shell) along both flanks."""
    for s in (1, -1):
        m.bx(1.10, 1.28, s * 0.62, s * 0.80, 0.0, 1.55, SHELL, bevel=0.03)
        m.bx(1.06, 1.32, s * 0.58, s * 0.84, 0.0, 0.12, DARK, bevel=0.02)
        m.hazard((1.285, 0.62 if s > 0 else -0.80, 0.20), (0, 1, 0), (0, 0, 1), 0.18, 1.00, (1, 0, 0), period=0.12)
    m.bx(1.08, 1.30, -0.86, 0.86, 1.40, 1.58, SHELL, bevel=0.035)
    m.bx(1.10, 1.28, -0.82, 0.82, 1.58, 1.62, TEAM, bevel=0.02)
    m.bx(1.12, 1.26, -0.80, 0.80, 1.385, 1.40, GLOW)
    for i in range(7):
        m.rod((1.19, -0.48 + 0.16 * i, 1.385), (1.19, -0.48 + 0.16 * i, 1.02), 0.02, DARK, verts=6)
    m.bx(1.17, 1.21, -0.52, 0.52, 1.00, 1.05, DARK)
    for s in (1, -1):
        m.bx(-0.70, 0.50, s * 1.10, s * 1.30, 0.0, 0.10, DARK, bevel=0.02)
        m.bx(-0.70, 0.50, s * 1.155, s * 1.195, 0.60, 0.65, DARK)
        for x in (-0.70, 0.50):
            m.bx(x - 0.03, x + 0.03, s * 1.16, s * 1.20, 0.0, 0.66, DARK)
        for i in range(7):
            x = -0.60 + 0.18 * i
            m.box((x, s * 1.22, 0.43), (0.16, 0.05, 0.62), TEAM if i % 2 == 0 else SHELL, rot=(12 * s, 0, 0),
                  bevel=0.02, seg=2)
            m.box((x, s * 1.185, 0.62), (0.09, 0.012, 0.03), GLOW, rot=(12 * s, 0, 0))


def human_barracks_ranged(m):
    """Ranged (Offline Ranger, long rifle): tripod sniper posts with long rifles aimed forward on the roof, rifle
    racks with scopes along both flanks, Team target discs downrange of the door, and a radar bar on the mast."""
    for s in (1, -1):
        x0, y = -0.25, s * 0.72
        for dx, dy in ((0.16, 0.0), (-0.10, 0.14), (-0.10, -0.14)):
            m.rod((x0, y, 1.46), (x0 + dx, y + dy, 1.12), 0.015, DARK, verts=6)
        m.rod((x0 - 0.30, y, 1.48), (x0 + 0.85, y, 1.51), 0.022, DARK, verts=8)
        m.bx(x0 - 0.36, x0 - 0.12, y - 0.04, y + 0.04, 1.43, 1.53, SHELL, bevel=0.02)
        m.cyl((x0 + 0.10, y, 1.55), (0.20, 0.06, 0.06), SHELL, verts=10)
        m.sph((x0 + 0.20, y, 1.55), (0.05, 0.05, 0.05), GLOW, verts=8)
        m.sph((x0 + 0.85, y, 1.51), (0.04, 0.04, 0.04), GLOW, verts=8)
        m.bx(-0.70, 0.50, s * 1.10, s * 1.24, 0.0, 0.10, DARK, bevel=0.02)
        m.bx(-0.70, 0.50, s * 1.18, s * 1.22, 0.10, 0.85, DARK)
        m.bx(-0.72, 0.52, s * 1.14, s * 1.30, 0.85, 0.90, TEAM, bevel=0.02)
        for i in range(8):
            x = -0.62 + 0.15 * i
            m.rod((x, s * 1.155, 0.12), (x + 0.02, s * 1.155, 0.82), 0.02, DARK, verts=6)
            m.bx(x - 0.02, x + 0.02, s * 1.135, s * 1.175, 0.54, 0.62, GLOW)
        m.rod((1.30, s * 0.95, 0.0), (1.30, s * 0.95, 0.72), 0.022, DARK, verts=8)
        m.cyl((1.30, s * 0.95, 0.90), (0.04, 0.42, 0.42), TEAM, aim=(1, 0, 0), verts=24)
        m.cyl((1.315, s * 0.95, 0.90), (0.04, 0.26, 0.26), SHELL, aim=(1, 0, 0), verts=20)
        m.cyl((1.33, s * 0.95, 0.90), (0.04, 0.10, 0.10), GLOW, aim=(1, 0, 0), verts=12)
    m.box((-0.98, 0.80, 2.00), (0.55, 0.05, 0.10), SHELL, rot=(0, 0, 20), bevel=0.015, seg=2)
    m.sph((-0.98 + 0.26, 0.80 + 0.095, 2.00), (0.06, 0.06, 0.06), TEAM, verts=8)
    m.sph((-0.98 - 0.26, 0.80 - 0.095, 2.00), (0.06, 0.06, 0.06), TEAM, verts=8)
    for z in (1.60, 1.78):
        m.box((-0.98, 0.80, z), (0.32, 0.02, 0.02), DARK, rot=(0, 0, -25))


def human_barracks_siege(m):
    """Siege (Unplugger walker): a vehicle bay door with a hazard apron on the -Y flank and a gantry crane over
    the roof lifting a heavy Team-banded cannon barrel."""
    for px in (-1.10, 0.85):
        for s in (1, -1):
            m.bx(px - 0.07, px + 0.07, s * 1.24 - 0.07, s * 1.24 + 0.07, 0.06, 1.70, SHELL, bevel=0.03)
            m.bx(px - 0.12, px + 0.12, s * 1.24 - 0.12, s * 1.24 + 0.12, 0.0, 0.06, DARK, bevel=0.02)
    for s in (1, -1):
        m.bx(-1.17, 0.92, s * 1.24 - 0.06, s * 1.24 + 0.06, 1.70, 1.80, SHELL, bevel=0.03)
        m.bx(-1.10, 0.90, s * 1.24 - 0.02, s * 1.24 + 0.02, 1.685, 1.70, GLOW)
    m.bx(-0.45, -0.27, -1.30, 1.30, 1.80, 1.92, SHELL, bevel=0.03)
    m.bx(-0.42, -0.30, -1.28, 1.28, 1.785, 1.80, GLOW)
    m.bx(-0.52, -0.20, -0.18, 0.18, 1.66, 1.80, DARK, bevel=0.02)
    m.sph((-0.36, 0.0, 1.62), (0.10, 0.10, 0.10), GLOW, verts=8)
    bar = (-0.35, 0.0, 1.40)
    m.cyl(bar, (1.10, 0.28, 0.28), DARK, aim=(1, 0, 0), verts=16)
    for x, mat in ((-0.75, TEAM), (-0.30, TEAM), (0.02, GLOW)):
        m.cyl((x, 0.0, 1.40), (0.14, 0.30, 0.30), mat, aim=(1, 0, 0), verts=16)
    m.cyl((0.22, 0.0, 1.40), (0.12, 0.36, 0.36), SHELL, aim=(1, 0, 0), verts=12, bevel=0.01)
    for x in (-0.75, -0.30):
        for sy in (1, -1):
            m.rod((-0.36, 0.08 * sy, 1.66), (x, 0.08 * sy, 1.54), 0.01, DARK, verts=4)
    # vehicle bay on the -Y flank: frame, dark door with a hazard header and lit slits, hazard apron
    m.bx(-0.85, 0.60, -1.14, -1.09, 0.20, 0.82, SHELL, bevel=0.025)
    m.bx(-0.77, 0.52, -1.17, -1.13, 0.24, 0.74, DARK)
    m.hazard((-0.77, -1.17, 0.64), (1, 0, 0), (0, 0, 1), 1.29, 0.10, (0, -1, 0))
    for z in (0.34, 0.50):
        m.bx(-0.65, 0.40, -1.175, -1.165, z, z + 0.03, GLOW)
    m.bx(-0.80, 0.55, -1.36, -1.13, 0.0, 0.05, DARK, bevel=0.015)
    m.hazard((-0.80, -1.36, 0.05), (1, 0, 0), (0, 1, 0), 1.35, 0.23, (0, 0, 1), period=0.16)


HUMAN_ROLES = {"Frontline": human_barracks_frontline, "Ranged": human_barracks_ranged,
               "Siege": human_barracks_siege}


def machine_barracks_frontline(m):
    """Frontline (SOL 6000, big front plate): two sentinel cradles flanking the hover steps, each a pearl
    sentinel standing in a pair of Glow-edged rings behind a big Team front plate."""
    for s in (1, -1):
        cx, cy = 1.10, s * 0.74
        m.plan(ellipse(0.19, 0.19, 24), 0.09, 0.13, DARK, loc=(cx, cy), bevel=0.01)
        m.tor((cx, cy, 0.135), 0.17, 0.012, GLOW, verts=24)
        for dy in (-0.10, 0.10):
            m.tor((cx, cy + dy, 0.50), 0.24, 0.02, SHELL, aim=(0, 1, 0), verts=28)
        m.tor((cx, cy, 0.50), 0.235, 0.008, GLOW, aim=(0, 1, 0), verts=28)
        m.lathe([(0.0, 0.14), (0.10, 0.16), (0.12, 0.38), (0.09, 0.66), (0.0, 0.70)], (cx, cy, 0.0), SHELL, verts=16)
        m.sph((cx, cy, 0.80), (0.17, 0.17, 0.17), SHELL, verts=14)
        m.bx(cx + 0.06, cx + 0.09, cy - 0.05, cy + 0.05, 0.79, 0.83, GLOW)
        m.bx(cx + 0.14, cx + 0.19, cy - 0.20, cy + 0.20, 0.22, 1.05, TEAM, bevel=0.045)
        m.bx(cx + 0.19, cx + 0.205, cy - 0.13, cy + 0.13, 0.36, 0.41, GLOW)
        m.bx(cx - 0.10, cx + 0.10, cy - 0.30, cy + 0.30, 0.13, 0.16, DARK, bevel=0.01)


def machine_barracks_ranged(m):
    """Ranged (Autocomplete Drone): a drone launch ring floating over the upper shell on four crystal pylons,
    alternating Shell / Team segments with a Glow inner ring, six docked drones and two launching."""
    cx, cz, R = -0.05, 1.95, 0.62
    for k in range(6):
        a0 = 60 * k + 1.5
        m.arc(R, 0.05, a0, a0 + 57, (cx, 0, cz), TEAM if k % 2 else SHELL, segs=8)
    m.tor((cx, 0.0, cz), R - 0.045, 0.012, GLOW, verts=44)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        m.rod((cx + 0.46 * math.cos(a), 0.46 * math.sin(a), 1.28), (cx + R * math.cos(a), R * math.sin(a), cz - 0.03),
              0.022, SHELL, verts=8)
    for k in range(6):
        a = math.radians(60 * k + 30)
        p = Vector((cx + R * math.cos(a), R * math.sin(a), cz + 0.08))
        m.lathe([(0.0, -0.05), (0.10, 0.0), (0.0, 0.055)], p, SHELL, verts=10)
        m.sph((p.x, p.y, p.z - 0.03), (0.06, 0.06, 0.05), GLOW, verts=8)
    for s in (1, -1):
        p = Vector((cx, s * 0.26, 2.42))
        m.lathe([(0.0, -0.05), (0.11, 0.0), (0.0, 0.06)], p, SHELL, verts=10)
        m.sph((p.x, p.y, p.z - 0.035), (0.07, 0.07, 0.05), GLOW, verts=8)
        m.rod((cx, s * 0.26, 2.36), (cx, s * 0.26, cz + 0.10), 0.008, GLOW, verts=4)


def machine_barracks_siege(m):
    """Siege (Hallucinator artillery): a prism lance forge. A hexagonal pearl lance in segments around a Glow
    core, threaded through three forge rings (the middle one Team) and pointing forward between the fins; a
    Glow forge pool sits behind it."""
    z = 1.55
    for x0, x1 in ((-0.80, -0.10), (0.02, 0.56), (0.68, 1.06)):
        m.lathe([(0.0, x0), (0.12, x0 + 0.05), (0.12, x1 - 0.05), (0.0, x1)], (0.0, 0.0, z), SHELL, verts=6,
                aim=(1, 0, 0))
    m.lathe([(0.0, -0.78), (0.05, -0.74), (0.05, 1.10), (0.0, 1.14)], (0.0, 0.0, z), GLOW, verts=6, aim=(1, 0, 0))
    m.lathe([(0.0, 1.06), (0.10, 1.12), (0.0, 1.35)], (0.0, 0.0, z), GLOW, verts=6, aim=(1, 0, 0))
    for x, mat in ((0.0, SHELL), (0.55, TEAM), (1.00, SHELL)):
        m.tor((x, 0.0, z), 0.25, 0.03, mat, aim=(1, 0, 0), verts=32)
        m.tor((x, 0.0, z), 0.215, 0.010, GLOW, aim=(1, 0, 0), verts=32)
    for x in (0.0, 0.55):
        m.bx(x - 0.05, x + 0.05, -0.08, 0.08, 1.27, 1.34, SHELL, bevel=0.015)
    m.lathe([(0.16, 1.30), (0.22, 1.30), (0.22, 1.38), (0.16, 1.38)], (-0.55, 0.0, 0.0), DARK, verts=20, closed=True)
    m.plan(ellipse(0.16, 0.16, 20), 1.30, 1.325, GLOW, loc=(-0.55, 0))


MACHINE_ROLES = {"Frontline": machine_barracks_frontline, "Ranged": machine_barracks_ranged,
                 "Siege": machine_barracks_siege}


# --------------------------------------------------------------------------------------
# Human Outpost: armoured hex plinth on landing struts, reactor drum and a big Team sensor dish
# --------------------------------------------------------------------------------------
def human_outpost(mats):
    m = BModel("SM_Human_Outpost", mats)
    hb = 0.16
    for k in range(6):   # landing pads and struts at the hex corners
        a = math.radians(60 * k)
        px, py = 0.80 * math.cos(a), 0.80 * math.sin(a)
        m.bx(px - 0.11, px + 0.11, py - 0.11, py + 0.11, 0.0, 0.06, DARK, bevel=0.02)
        m.strut((px, py, 0.09), (0.64 * math.cos(a), 0.64 * math.sin(a), hb + 0.03), 0.06, SHELL, r1=0.05)
    m.plan(hexagon(0.74), 0.10, hb, DARK, bevel=0.02)
    m.plan(hexagon(0.80), 0.125, 0.145, GLOW)
    m.plan(hexagon(0.90), hb, 0.52, SHELL, bevel=0.045)
    m.plan(hexagon(0.86), 0.50, 0.54, DARK)
    m.plan(hexagon(0.84), 0.54, 0.62, SHELL, bevel=0.035)
    m.plan(hexagon(0.76), 0.62, 0.655, TEAM, bevel=0.02)
    # hex faces: hazard skirt and a Glow light strip
    for k in range(6):
        th = math.radians(60 * k + 30)
        n = Vector((math.cos(th), math.sin(th), 0.0))
        t = Vector((-math.sin(th), math.cos(th), 0.0))
        ap = 0.90 * math.cos(math.radians(30))
        m.hazard(n * ap - t * 0.28 + Vector((0, 0, 0.19)), t, (0, 0, 1), 0.56, 0.08, n)
        m.decal([(0, 0), (0.50, 0), (0.50, 0.03), (0, 0.03)], n * ap - t * 0.25 + Vector((0, 0, 0.40)), t,
                (0, 0, 1), GLOW, n)
    # reactor drum with Glow ring, ribbed neck with Glow strips, Team collar, mast
    m.vcyl((0.0, 0.0, 0.87), 0.50, 0.90, SHELL, verts=24, bevel=0.03)
    m.tor((0.0, 0.0, 0.90), 0.455, 0.014, GLOW, verts=28)
    m.vcyl((0.0, 0.0, 1.56), 0.88, 0.62, SHELL, verts=24, bevel=0.02)
    for z in (1.30, 1.82):
        m.vcyl((0.0, 0.0, z), 0.05, 0.70, DARK, verts=24, bevel=0.01)
    for k in range(6):
        a = math.radians(60 * k + 15)
        n = Vector((math.cos(a), math.sin(a), 0.0))
        f = Frame.surface(m, n * 0.31 + Vector((0, 0, 1.56)), n, (0, 0, 1))
        f.box((0, 0, 0), (0.56, 0.06, 0.012), DARK)
        f.box((0, 0, 0.006), (0.52, 0.024, 0.014), GLOW)
    m.vcyl((0.0, 0.0, 2.05), 0.10, 0.92, DARK, verts=24, bevel=0.015)
    m.vcyl((0.0, 0.0, 2.12), 0.05, 0.88, TEAM, verts=24)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        m.flood(0.40 * math.cos(a), 0.40 * math.sin(a), 1.12, 1.30)
    m.vcyl((0.0, 0.0, 2.50), 0.70, 0.36, SHELL, taper=0.6, verts=16, bevel=0.015)
    m.tor((0.0, 0.0, 2.35), 0.17, 0.012, GLOW, verts=20)
    # sensor dish (Team painted) on a yoke, feed horn, tall beacon mast behind it
    for s in (1, -1):
        m.rod((0.0, 0.0, 2.80), (-0.02, 0.28 * s, 3.00), 0.035, DARK, verts=8)
    m.dish((0.0, 0.0, 3.02), (1, 0, 0.55), 1.15, mat=TEAM, rim=DARK)
    m.rod((-0.10, 0.0, 2.85), (-0.10, 0.0, 3.78), 0.025, DARK, verts=8)
    m.box((-0.10, 0.0, 3.56), (0.03, 0.42, 0.03), DARK)
    for s in (1, -1):
        m.sph((-0.10, 0.21 * s, 3.56), (0.09, 0.09, 0.09), GLOW, verts=10)
    m.sph((-0.10, 0.0, 3.80), (0.12, 0.12, 0.12), GLOW, verts=10)
    return m.finish()


# --------------------------------------------------------------------------------------
# Machine Outpost: pearl pylon with a Team base ring, orbiting arc segments, a floating cyan crystal, red lens
# --------------------------------------------------------------------------------------
PYLON = [(0.0, 0.11), (0.50, 0.11), (0.50, 0.20), (0.36, 0.55), (0.24, 1.10), (0.20, 1.70), (0.24, 2.05),
         (0.20, 2.30), (0.0, 2.38)]


def pylon_r(z):
    for (r0, z0), (r1, z1) in zip(PYLON, PYLON[1:]):
        if z0 <= z <= z1:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    raise ValueError(z)


def machine_outpost(mats):
    m = BModel("SM_Machine_Outpost", mats)
    m.plan(ellipse(0.92, 0.92, 36), 0.0, 0.07, DARK, bevel=0.02)
    m.lathe([(0.50, 0.07), (0.80, 0.07), (0.80, 0.115), (0.50, 0.115)], (0, 0, 0), TEAM, verts=36, closed=True)
    m.tor((0.0, 0.0, 0.10), 0.86, 0.014, GLOW, verts=36)
    m.lathe(PYLON, (0, 0, 0), SHELL, verts=24)
    for z in (0.42, 0.95, 1.55, 2.08):
        m.tor((0.0, 0.0, z), pylon_r(z) + 0.004, 0.014, GLOW, verts=28)
    # three swept base fins with Glow edges
    for k in range(3):
        m.poly([(0.40, 0.12), (0.90, 0.12), (0.88, 0.17), (0.66, 0.66), (0.44, 1.12)], -0.06, 0.06, SHELL,
               rotz=120 * k - 90, bevel=0.02, seg=3)
        m.poly([(0.62, 0.12), (0.90, 0.12), (0.90, 0.145), (0.62, 0.145)], -0.065, 0.065, GLOW, rotz=120 * k - 90)
    # red lens on the front of the pylon head
    m.lens((0.23, 0.0, 1.95), 0.13, aim=(1, 0, 0.15), clamps=4, collar=0.5)
    # orbiting arc segments (separated by gaps): Shell layer, Team layer, Glow inlays
    for k in range(3):
        m.arc(0.66, 0.06, 120 * k + 15, 120 * k + 105, (0, 0, 1.48), SHELL, segs=12)
        m.arc(0.62, 0.016, 120 * k + 20, 120 * k + 100, (0, 0, 1.48), GLOW, segs=12)
        m.arc(0.52, 0.06, 120 * k + 75, 120 * k + 165, (0, 0, 2.62), TEAM, segs=12)
        m.arc(0.48, 0.016, 120 * k + 80, 120 * k + 160, (0, 0, 2.62), GLOW, segs=12)
    # floating crystal, cage ring and orbiting shards
    m.lathe([(0.0, 2.72), (0.17, 3.05), (0.0, 3.62)], (0, 0, 0), GLOW, verts=6)
    m.tor((0.0, 0.0, 3.05), 0.24, 0.016, SHELL, verts=28)
    for k in range(3):
        a = math.radians(90 + 120 * k)
        base = Vector((0.36 * math.cos(a), 0.36 * math.sin(a), 2.95))
        m.lathe([(0.0, -0.16), (0.055, 0.0), (0.0, 0.16)], base, SHELL, verts=6,
                aim=(0.25 * math.cos(a), 0.25 * math.sin(a), 1.0))
    return m.finish()


# --------------------------------------------------------------------------------------
# Human Workshop: armoured hull with a tool wall, front bay under a gantry crane lifting a Team armour plate
# --------------------------------------------------------------------------------------
def human_workshop(mats):
    m = BModel("SM_Human_Workshop", mats)
    hb = 0.18
    cx = -0.575
    for px in (-1.18, -0.06):
        for py in (-1.10, 1.10):
            m.bx(px - 0.19, px + 0.19, py - 0.19, py + 0.19, 0.0, 0.07, DARK, bevel=0.03)
            top = (px + (cx - px) * 0.28, py * 0.82, hb + 0.03)
            m.strut((px, py, 0.10), top, 0.085, SHELL, r1=0.065)
            m.piston((px + (cx - px) * 0.18, py * 1.02, 0.12), (px + (cx - px) * 0.30, py * 0.88, hb + 0.02), 0.028)
    m.plan(rrect(1.45, 2.40, 0.20, 1), 0.10, hb, DARK, loc=(cx, 0), bevel=0.02)
    m.plan(rrect(1.53, 2.48, 0.22, 1), 0.135, 0.155, GLOW, loc=(cx, 0))
    m.plan(rrect(1.55, 2.60, 0.26, 2), hb, 0.86, SHELL, loc=(cx, 0), bevel=0.045)
    m.plan(rrect(1.48, 2.53, 0.24, 2), 0.85, 0.89, DARK, loc=(cx, 0))
    m.plan(rrect(1.40, 2.44, 0.22, 2), 0.88, 1.12, SHELL, loc=(-0.60, 0), bevel=0.04)
    # side walls: armour plates (one big Team plate), hazard skirt, vents and light strips
    for s in (1, -1):
        for x0, x1, mat in ((-1.05, -0.62, SHELL), (-0.55, -0.08, TEAM)):
            m.bx(x0, x1, s * 1.30, s * 1.338, 0.27, 0.82, mat, bevel=0.022)
        m.hazard((-0.06, s * 1.30, 0.19) if s > 0 else (-1.05, s * 1.30, 0.19), (-s, 0, 0) if s > 0 else (1, 0, 0),
                 (0, 0, 1), 1.00, 0.07, (0, s, 0))
        m.bx(-1.15, -0.10, s * 1.222, s * 1.238, 1.05, 1.07, GLOW)
        Frame.surface(m, (-0.60, s * 1.23, 0.96), (0, s, 0), (1, 0, 0)).vent((0, 0, 0), (0.70, 0.12), slats=3)
    # roof: two big Team plates, reactor stacks, vent grille, rear dish on a pedestal, floodlights
    for s in (1, -1):
        m.bx(-1.20, -0.30, s * 0.62, s * 1.10, 1.12, 1.16, TEAM, bevel=0.03)
        m.stack(-0.42, 0.30 * s, 1.12, 1.56, 0.28)
    for x in (-0.72, -0.62, -0.52):
        m.bx(x - 0.02, x + 0.02, -0.22, 0.22, 1.12, 1.13, DARK)
    m.rod((-1.05, 0.0, 1.12), (-1.05, 0.0, 1.40), 0.04, DARK, verts=8)
    m.dish((-1.05, 0.0, 1.55), (1, 0, 0.65), 0.72)
    # front wall: tool rack (Dark panel, shelf, hanging tools, Glow strip)
    m.bx(0.185, 0.235, -1.00, 1.00, 0.30, 0.80, DARK, bevel=0.02)
    m.bx(0.20, 0.36, -1.00, 1.00, 0.26, 0.31, SHELL, bevel=0.015)
    m.bx(0.235, 0.255, -0.95, 0.95, 0.78, 0.80, GLOW)
    for i in range(9):
        y = -0.88 + 0.22 * i
        k = i % 3
        if k == 0:    # wrench
            m.rod((0.25, y, 0.40), (0.25, y, 0.70), 0.013, SHELL, verts=6)
            m.bx(0.24, 0.27, y - 0.045, y + 0.045, 0.70, 0.75, SHELL)
        elif k == 1:  # hammer
            m.rod((0.25, y, 0.36), (0.25, y, 0.68), 0.013, DARK, verts=6)
            m.bx(0.24, 0.29, y - 0.07, y + 0.07, 0.66, 0.73, SHELL)
        else:         # pliers
            m.rod((0.25, y - 0.02, 0.40), (0.25, y + 0.01, 0.72), 0.011, SHELL, verts=6)
            m.rod((0.25, y + 0.02, 0.40), (0.25, y - 0.01, 0.72), 0.011, SHELL, verts=6)
    # front bay: deck with hazard border, Glow guide lights, workpiece and gantry crane with a lifted Team plate
    m.plan(rrect(1.20, 2.10, 0.15, 1), 0.0, 0.08, DARK, loc=(0.80, 0), bevel=0.02)
    m.hazard((1.25, -0.96, 0.08), (0, 1, 0), (1, 0, 0), 1.92, 0.12, (0, 0, 1))
    for s in (1, -1):
        m.hazard((0.32, s * 1.00 if s < 0 else s * 0.90, 0.08), (1, 0, 0), (0, 1, 0), 0.85, 0.10, (0, 0, 1))
    for y in (-0.55, -0.20, 0.55, 0.20):
        m.bx(0.42, 0.62, y - 0.02, y + 0.02, 0.08, 0.092, GLOW)
    m.bx(0.70, 1.20, -0.55, -0.05, 0.08, 0.40, SHELL, bevel=0.04)
    m.bx(0.72, 1.18, -0.53, -0.07, 0.40, 0.43, DARK)
    m.bx(0.95, 1.03, -0.57, -0.03, 0.30, 0.32, GLOW)
    for s in (1, -1):
        m.bx(0.88, 1.02, s * 0.88, s * 1.02, 0.08, 1.62, SHELL, bevel=0.03)
        m.bx(0.84, 1.06, s * 0.84, s * 1.06, 0.08, 0.16, DARK, bevel=0.02)
        m.flood(0.98, s * 0.95, 1.72, 1.90)
    m.bx(0.86, 1.04, -1.03, 1.03, 1.55, 1.72, SHELL, bevel=0.035)
    m.bx(0.90, 1.00, -1.00, 1.00, 1.535, 1.555, GLOW)
    m.bx(0.83, 1.07, 0.02, 0.32, 1.40, 1.55, DARK, bevel=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.rod((0.95, 0.17, 1.40), (0.95 + sx * 0.26, 0.17 + sy * 0.26, 1.04), 0.011, DARK, verts=4)
    m.bx(0.64, 1.26, -0.14, 0.48, 0.98, 1.04, TEAM, bevel=0.02)
    return m.finish()


# --------------------------------------------------------------------------------------
# Machine Workshop: hovering pearl hull, a floating fabrication cradle and two mirrored fabrication arms
# --------------------------------------------------------------------------------------
def machine_workshop(mats):
    m = BModel("SM_Machine_Workshop", mats)
    cx = -0.575
    m.plan(rrect(2.82, 2.82, 0.6, 4), 0.0, 0.06, DARK, bevel=0.02)
    m.plan(rrect(2.74, 2.74, 0.57, 4), 0.06, 0.075, GLOW)
    m.plan(rrect(2.68, 2.68, 0.55, 4), 0.075, 0.085, DARK)
    # lower shell on a Glow underlight, waist seam, Team crescents
    m.plan(rrect(1.10, 2.00, 0.45, 4), 0.19, 0.25, GLOW, loc=(cx, 0))
    m.plan(rrect(1.55, 2.50, 0.55, 4), 0.25, 0.80, SHELL, loc=(cx, 0), taper=0.94, bevel=0.06)
    m.plan(rrect(1.52, 2.46, 0.53, 4), 0.505, 0.525, GLOW, loc=(cx, 0))
    for s in (1, -1):
        m.bx(-1.05, -0.15, s * 0.98, s * 1.13, 0.80, 0.835, TEAM, bevel=0.025)
    # upper shell: separate floating segment with a Glow gap and a big Team top plate, red lens on the front
    m.plan(rrect(1.10, 1.85, 0.40, 4), 0.80, 0.865, GLOW, loc=(-0.60, 0))
    m.plan(rrect(1.15, 1.90, 0.42, 4), 0.865, 1.25, SHELL, loc=(-0.60, 0), taper=0.82, bevel=0.06)
    m.plan(ellipse(0.34, 0.62, 30), 1.25, 1.285, TEAM, loc=(-0.62, 0), bevel=0.02)
    m.lens((-0.07, 0.0, 0.99), 0.17, aim=(1, 0, 0.15), clamps=4, collar=0.5)
    # mirrored crest blades at the rear
    for s in (1, -1):
        y0, y1 = (0.40, 0.47) if s > 0 else (-0.47, -0.40)
        m.blade([(-0.35, 1.20), (-1.10, 1.17), (-1.40, 1.90), (-0.90, 1.45)], y0, y1, SHELL, bevel=0.015, seg=2)
        yi = (0.47, 0.478) if s > 0 else (-0.478, -0.47)
        m.blade([(-0.55, 1.22), (-1.05, 1.20), (-1.25, 1.66), (-0.92, 1.42)], yi[0], yi[1], GLOW)
    # fabrication cradle: dark disc, Glow ring, pearl arc supports, floating crystal workpiece
    m.plan(ellipse(0.62, 0.62, 36), 0.06, 0.14, DARK, loc=(0.75, 0), bevel=0.02)
    m.tor((0.75, 0.0, 0.145), 0.58, 0.014, GLOW, verts=36)
    for k in range(3):
        m.arc(0.50, 0.05, 120 * k + 20, 120 * k + 100, (0.75, 0.0, 0.26), SHELL, segs=10)
        m.arc(0.46, 0.015, 120 * k + 25, 120 * k + 95, (0.75, 0.0, 0.26), GLOW, segs=10)
    m.lathe([(0.0, -0.26), (0.15, 0.0), (0.0, 0.26)], (0.75, 0.0, 0.66), GLOW, verts=6)
    m.tor((0.75, 0.0, 0.66), 0.24, 0.016, SHELL, rot=(70, 0, 0), verts=28)
    # mirrored fabrication arms: pedestal, shoulder, upper arm, elbow, forearm, tool with a Glow beam
    for s in (1, -1):
        m.lathe([(0.20, 0.06), (0.17, 0.18), (0.13, 0.42), (0.0, 0.44)], (0.42, s * 1.0, 0.0), SHELL, verts=16)
        m.tor((0.42, s * 1.0, 0.20), 0.18, 0.014, GLOW, verts=20)
        S, E, W, T = ((0.42, s * 1.0, 0.50), (0.62, s * 0.58, 1.36), (0.80, s * 0.28, 1.06), (0.76, s * 0.14, 0.78))
        m.capsule(S, E, 0.075, SHELL)
        m.capsule(E, W, 0.058, SHELL)
        m.rod(W, T, 0.028, DARK, verts=8)
        m.sph(T, (0.06, 0.06, 0.06), GLOW, verts=8)
        m.rod(T, (0.75, 0.0, 0.62), 0.008, GLOW, verts=4)
        for j in (S, E, W):
            m.sph(j, (0.10, 0.10, 0.10), GLOW, verts=10)
        m.piston((0.42, s * 0.88, 0.36), (0.55, s * 0.70, 1.00), 0.02)
    return m.finish()


# --------------------------------------------------------------------------------------
# Construction scaffolds (faction neutral): foundation slab with Team hazard border, skeleton girders, crane
# lifting a component, cargo crates, work lights. Amber Team while building is the game tint.
# --------------------------------------------------------------------------------------
def beam(m, p0, p1, w, h, mat, bevel=0.015):
    """Flat girder between two points at the same height (or tilted in Z): box turned to the segment."""
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    yaw = math.degrees(math.atan2(d.y, d.x))
    length = math.hypot(d.x, d.y)
    m.box((p0 + p1) / 2, (length, w, h), mat, rot=(0, 0, yaw), bevel=bevel, seg=2)


def column(m, x, y, z0, z1, s=0.16, mat=SHELL):
    """H-section steel column with a Dark base plate and a Glow work light."""
    m.bx(x - s / 2, x + s / 2, y - s * 0.12, y + s * 0.12, z0, z1, mat, bevel=0.01)
    m.bx(x - s * 0.12, x + s * 0.12, y - s / 2, y + s / 2, z0, z1, mat, bevel=0.01)
    m.bx(x - s * 0.8, x + s * 0.8, y - s * 0.8, y + s * 0.8, z0, z0 + 0.05, DARK, bevel=0.015)


def crane(m, mx, my, mast_top, tip, load_z, load=None):
    """Tower crane: Shell mast with lights, Team jib beam, counter-jib and weight, cab, hook and a load."""
    m.bx(mx - 0.09, mx + 0.09, my - 0.09, my + 0.09, 0.05, mast_top, SHELL, bevel=0.025)
    for z in (0.55, 1.05, 1.55):
        m.bx(mx - 0.11, mx + 0.11, my - 0.11, my + 0.11, z, z + 0.05, DARK, bevel=0.012)
    m.bx(mx - 0.20, mx + 0.20, my - 0.20, my + 0.20, 0.0, 0.10, DARK, bevel=0.03)
    d = Vector((tip[0] - mx, tip[1] - my, 0.0))
    u = d.normalized()
    beam(m, (mx, my, mast_top), (tip[0], tip[1], mast_top), 0.13, 0.11, TEAM)
    beam(m, (mx, my, mast_top + 0.06), (tip[0], tip[1], mast_top + 0.06), 0.06, 0.03, DARK)
    back = (mx - u.x * 0.25, my - u.y * 0.25, mast_top)
    beam(m, (mx, my, mast_top), back, 0.13, 0.11, TEAM)
    m.bx(back[0] - 0.10, back[0] + 0.10, back[1] - 0.10, back[1] + 0.10, mast_top - 0.16, mast_top + 0.05, DARK,
         bevel=0.02)
    m.rod((mx, my, mast_top), (mx, my, mast_top + 0.32), 0.03, DARK, verts=8)
    m.rod((mx, my, mast_top + 0.32), (mx + d.x * 0.55, my + d.y * 0.55, mast_top + 0.06), 0.011, DARK, verts=4)
    m.rod((mx, my, mast_top + 0.32), (tip[0], tip[1], mast_top + 0.06), 0.011, DARK, verts=4)
    m.rod((mx, my, mast_top + 0.32), (back[0], back[1], mast_top + 0.05), 0.011, DARK, verts=4)
    m.bx(mx - 0.13, mx + 0.13, my - 0.13, my + 0.13, mast_top - 0.42, mast_top - 0.14, SHELL, bevel=0.03)
    m.bx(mx - 0.14, mx + 0.14, my - 0.10, my + 0.10, mast_top - 0.34, mast_top - 0.24, GLOW)
    m.sph((tip[0], tip[1], mast_top - 0.09), (0.10, 0.10, 0.10), GLOW, verts=8)
    m.rod((tip[0], tip[1], mast_top - 0.05), (tip[0], tip[1], load_z + 0.12), 0.012, DARK, verts=4)
    if load == "plate":
        m.bx(tip[0] - 0.22, tip[0] + 0.22, tip[1] - 0.22, tip[1] + 0.22, load_z, load_z + 0.06, TEAM, bevel=0.02)
        for sx in (-1, 1):
            for sy in (-1, 1):
                m.rod((tip[0], tip[1], load_z + 0.12), (tip[0] + sx * 0.20, tip[1] + sy * 0.20, load_z + 0.06), 0.008,
                      DARK, verts=4)
    elif load == "dish":
        m.dish((tip[0], tip[1], load_z + 0.10), (0.4, 0, 1), 0.85, mat=SHELL, rim=TEAM)
    elif load == "beam":
        beam(m, (tip[0] - 0.55, tip[1], load_z), (tip[0] + 0.55, tip[1], load_z), 0.16, 0.14, TEAM)
        for x in (-0.35, 0.35):
            m.rod((tip[0], tip[1], load_z + 0.12), (tip[0] + x, tip[1], load_z + 0.07), 0.008, DARK, verts=4)


def foundation(m, h):
    """Chamfered slab, inset Shell deck and a ring of Team hazard plates; rebar stubs at the corners."""
    m.bx(-h, h, -h, h, 0.0, 0.08, DARK, bevel=0.03)
    m.bx(-h + 0.22, h - 0.22, -h + 0.22, h - 0.22, 0.08, 0.12, SHELL, bevel=0.025)
    w = 0.15
    for s in (1, -1):
        m.bx(-h + 0.04, h - 0.04, s * (h - 0.04), s * (h - 0.04 - w), 0.08, 0.108, TEAM, bevel=0.02)
        m.bx(s * (h - 0.04), s * (h - 0.04 - w), -h + 0.04 + w + 0.03, h - 0.04 - w - 0.03, 0.08, 0.108, TEAM,
             bevel=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            for dx, dy in ((0, 0), (0.06, 0.02), (0.02, 0.07)):
                x, y = sx * (h - 0.30) - sx * dx, sy * (h - 0.30) - sy * dy
                m.rod((x, y, 0.08), (x, y, 0.30 + 0.05 * (dx + dy) * 10), 0.009, DARK, verts=4)


def construction(mats, kind):
    m = BModel("SM_Construction_" + kind, mats)
    h = HALF[kind] - 0.02
    foundation(m, h)
    if kind == "Barracks":
        # skeleton: corner columns, two rings of beams, braces, first armour plates and a deck section
        c = 0.80
        for sx in (-1, 1):
            for sy in (-1, 1):
                column(m, sx * c, sy * c, 0.12, 1.50)
        for z in (0.85, 1.50):
            for s in (1, -1):
                m.bx(-c, c, s * c - 0.04, s * c + 0.04, z, z + 0.08, SHELL, bevel=0.015)
                m.bx(s * c - 0.04, s * c + 0.04, -c, c, z, z + 0.08, SHELL, bevel=0.015)
        for s in (1, -1):
            m.rod((-c, s * c, 0.95), (c, s * c, 1.50), 0.016, DARK, verts=6)
            m.rod((c, s * c, 0.95), (-c, s * c, 1.50), 0.016, DARK, verts=6)
        m.bx(-0.76, 0.76, -c - 0.02, -c + 0.05, 0.14, 0.82, SHELL, bevel=0.03)
        m.bx(-0.76, 0.05, c - 0.05, c + 0.02, 0.14, 0.82, SHELL, bevel=0.03)
        m.hazard((-0.76, -c - 0.02, 0.86), (1, 0, 0), (0, 0, 1), 1.52, 0.06, (0, -1, 0))
        m.bx(-c, 0.05, -c, c, 0.84, 0.90, SHELL, bevel=0.025)
        m.bx(-c + 0.10, -0.08, -c + 0.10, c - 0.10, 0.90, 0.93, TEAM, bevel=0.02)
        for z in (0.30, 0.55):
            m.crate(0.98, 0.95, 0.12 if z < 0.4 else 0.42, 0.30, top=TEAM if z > 0.4 else SHELL)
        m.crate(1.00, 0.55, 0.12, 0.30, top=SHELL)
        crane(m, -1.00, -1.00, 2.05, (0.45, 0.40), 1.05, load="plate")
        m.flood(-0.95, 0.95, 0.12, 0.60)
        m.flood(0.95, -0.95, 0.12, 0.60)
    elif kind == "Outpost":
        # half-built hex plinth and reactor drum inside a column cage; the crane is lifting the sensor dish
        m.plan(hexagon(0.74), 0.12, 0.34, SHELL, loc=(0, 0), bevel=0.04)
        m.plan(hexagon(0.70), 0.30, 0.34, DARK)
        m.vcyl((0.0, 0.0, 0.62), 0.56, 0.86, SHELL, verts=24, bevel=0.03)
        m.tor((0.0, 0.0, 0.66), 0.435, 0.014, GLOW, verts=28)
        m.vcyl((0.0, 0.0, 1.12), 0.44, 0.36, DARK, verts=16, bevel=0.02)
        for k in range(6):
            a = math.radians(60 * k)
            column(m, 0.60 * math.cos(a), 0.60 * math.sin(a), 0.12, 1.75, s=0.13)
        for z in (0.95, 1.75):
            for k in range(6):
                a0, a1 = math.radians(60 * k), math.radians(60 * k + 60)
                m.rod((0.60 * math.cos(a0), 0.60 * math.sin(a0), z), (0.60 * math.cos(a1), 0.60 * math.sin(a1), z),
                      0.03, SHELL, verts=8)
        for k in range(0, 6, 2):
            a0, a1 = math.radians(60 * k), math.radians(60 * k + 60)
            m.rod((0.60 * math.cos(a0), 0.60 * math.sin(a0), 0.95), (0.60 * math.cos(a1), 0.60 * math.sin(a1), 1.75),
                  0.014, DARK, verts=6)
        m.plan(hexagon(0.64), 0.34, 0.37, TEAM, bevel=0.015)
        m.crate(0.78, -0.55, 0.12, 0.26, top=TEAM)
        m.crate(0.78, -0.25, 0.12, 0.26, top=SHELL)
        crane(m, -0.62, 0.62, 2.75, (0.0, -0.05), 1.95, load="dish")
        m.flood(0.78, 0.60, 0.12, 0.60)
    else:  # Workshop
        c1, c0 = 0.25, -1.20
        for x in (c0, c1):
            for y in (-1.05, 1.05):
                column(m, x, y, 0.12, 1.30)
        for z in (0.80, 1.30):
            for y in (-1.05, 1.05):
                m.bx(c0, c1, y - 0.04, y + 0.04, z, z + 0.08, SHELL, bevel=0.015)
            for x in (c0, c1):
                m.bx(x - 0.04, x + 0.04, -1.05, 1.05, z, z + 0.08, SHELL, bevel=0.015)
        for y in (-1.05, 1.05):
            m.rod((c0, y, 0.90), (c1, y, 1.30), 0.016, DARK, verts=6)
        m.bx(c0 + 0.05, -0.20, -1.03, 1.03, 0.78, 0.84, SHELL, bevel=0.025)
        m.bx(c0 + 0.15, -0.30, -0.93, 0.93, 0.84, 0.87, TEAM, bevel=0.02)
        m.bx(c0 + 0.02, c0 + 0.09, -1.02, 1.02, 0.14, 0.76, SHELL, bevel=0.03)
        m.hazard((c0 + 0.09, -1.02, 0.86 - 0.66), (0, 1, 0), (0, 0, 1), 2.04, 0.07, (1, 0, 0))
        for y in (-0.95, 0.95):
            column(m, 0.95, y, 0.12, 1.65, s=0.14)
        m.crate(0.60, -1.15, 0.12, 0.30, top=TEAM)
        m.crate(0.95, -1.15, 0.12, 0.30, top=SHELL)
        m.crate(0.60, 1.15, 0.12, 0.30, top=SHELL)
        m.crate(0.60, 1.15, 0.42, 0.30, top=TEAM)
        crane(m, 1.12, 1.08, 2.05, (0.55, -0.05), 1.72, load="beam")
        m.flood(-0.15, 1.15, 0.12, 0.70)
    return m.finish()


# --------------------------------------------------------------------------------------
# Registry, checks, export, previews
# --------------------------------------------------------------------------------------
MODELS = (
    ("Machine", "Barracks", machine_barracks),
    ("Machine", "Barracks_Frontline", lambda mats: machine_barracks(mats, "Frontline")),
    ("Machine", "Barracks_Ranged", lambda mats: machine_barracks(mats, "Ranged")),
    ("Machine", "Barracks_Siege", lambda mats: machine_barracks(mats, "Siege")),
    ("Machine", "Outpost", machine_outpost),
    ("Machine", "Workshop", machine_workshop),
    ("Human", "Barracks", human_barracks),
    ("Human", "Barracks_Frontline", lambda mats: human_barracks(mats, "Frontline")),
    ("Human", "Barracks_Ranged", lambda mats: human_barracks(mats, "Ranged")),
    ("Human", "Barracks_Siege", lambda mats: human_barracks(mats, "Siege")),
    ("Human", "Outpost", human_outpost),
    ("Human", "Workshop", human_workshop),
    ("Construction", "Barracks", lambda mats: construction(mats, "Barracks")),
    ("Construction", "Outpost", lambda mats: construction(mats, "Outpost")),
    ("Construction", "Workshop", lambda mats: construction(mats, "Workshop")),
)


def bounds(obj):
    return U.bounds(obj)


def triangles(obj):
    return U.triangles(obj)


def kind_of(name):
    for prefix in ("SM_Human_", "SM_Machine_", "SM_Construction_"):
        if name.startswith(prefix):
            return name[len(prefix):]
    raise ValueError(name)


def team_top_area(obj):
    mesh = obj.data
    return sum(p.area for p in mesh.polygons if mesh.materials[p.material_index].name == "Team" and p.normal.z > 0.5)


def check(obj):
    name = obj.name
    kind = kind_of(name)
    base = kind.split("_")[0]
    scaffold = name.startswith("SM_Construction_")
    used = {p.material_index for p in obj.data.polygons}
    assert used == {0, 1, 2, 3}, "%s uses slots %s" % (name, sorted(used))
    assert [s.material.name for s in obj.material_slots] == list(U.SLOT_NAMES), name
    budget = TRI_BUDGET_SCAFFOLD if scaffold else TRI_BUDGET_BUILDING
    assert triangles(obj) <= budget, "%s has %d triangles (budget %d)" % (name, triangles(obj), budget)
    lo, hi = bounds(obj)
    half = HALF[base]
    assert abs(lo.z - GROUND) < 1e-4, "%s does not touch the ground: %s" % (name, lo)
    limit = half * OVERHANG
    assert max(-lo.x, -lo.y, hi.x, hi.y) <= limit + 1e-4, "%s footprint %s %s exceeds %.3f" % (name, lo, hi, limit)
    assert min(-lo.x, -lo.y, hi.x, hi.y) >= 0.80 * half, "%s does not fill its footprint: %s %s" % (name, lo, hi)
    height = hi.z - GROUND
    hmin, hmax = (SCAFFOLD_HEIGHT_RANGE if scaffold else HEIGHT_RANGE)[base if scaffold else kind]
    assert hmin <= height <= hmax, "%s height %.2f outside %.1f..%.1f" % (name, height, hmin, hmax)
    area = team_top_area(obj)
    need = MIN_TEAM_TOP_AREA["scaffold" if scaffold else "building"]
    assert area >= need, "%s has only %.2f m2 of upward Team faces (need %.2f)" % (name, area, need)


# --------------------------------------------------------------------------------------
# Preview renders
# --------------------------------------------------------------------------------------
PREVIEW_SIZE = (2400, 1500)
RTS_SIZE = (2400, 1500)
COLUMN = {"Barracks": 0, "Barracks_Frontline": 1, "Barracks_Ranged": 2, "Barracks_Siege": 3, "Outpost": 4,
          "Workshop": 5}
ROW = {"Machine": 0, "Human": 1, "Neutral": 2}   # Machine top / far, Human middle, Construction bottom / near
COLUMN_DX = 4.2
ROW_DZ = 4.2      # side view row pitch
ROW_DY = 4.6      # RTS view row pitch
LOOK_PREFIX = (("SM_Human_", "Human"), ("SM_Machine_", "Machine"), ("SM_Construction_", "Neutral"))
# Field layout (x, y) in metres. The scrapyard patch covers x < 0 (Human side), asphalt elsewhere.
FIELD_BUILDINGS = {
    "SM_Human_Barracks": (-20.0, 4.0), "SM_Human_Barracks_Frontline": (-15.0, 4.0),
    "SM_Human_Barracks_Ranged": (-10.0, 4.0), "SM_Human_Barracks_Siege": (-5.0, 4.0),
    "SM_Human_Outpost": (-19.0, -4.0), "SM_Human_Workshop": (-13.0, -4.0),
    "SM_Construction_Barracks": (-1.5, 4.5), "SM_Construction_Outpost": (-1.5, 0.0),
    "SM_Construction_Workshop": (-1.5, -4.5),
    "SM_Machine_Barracks": (3.0, 4.0), "SM_Machine_Barracks_Frontline": (8.0, 4.0),
    "SM_Machine_Barracks_Ranged": (13.0, 4.0), "SM_Machine_Barracks_Siege": (18.0, 4.0),
    "SM_Machine_Outpost": (5.0, -4.0), "SM_Machine_Workshop": (11.0, -4.0),
}
# (unit mesh, x, y): each configured Barracks gets the unit it produces standing at its door, for scale
FIELD_UNITS = (
    ("SM_Human_Frontline", -12.6, 3.2), ("SM_Human_Frontline", -12.6, 4.8),
    ("SM_Human_Ranged", -7.6, 3.2), ("SM_Human_Ranged", -7.6, 4.8), ("SM_Human_Siege", -2.6, 4.0),
    ("SM_Machine_Frontline", 10.6, 3.2), ("SM_Machine_Frontline", 10.6, 4.8),
    ("SM_Machine_Ranged", 15.6, 3.2), ("SM_Machine_Ranged", 15.6, 4.8), ("SM_Machine_Siege", 20.6, 4.0),
)


def look_of(name):
    for prefix, look in LOOK_PREFIX:
        if name.startswith(prefix):
            return look
    raise ValueError(name)


def preview_materials():
    """Preview surfaces for the SC2-style pass. Machine: pearl-white coated shell, cyan glow, red lens (the unit
    preview set). Human: painted gunmetal / steel-blue shell, near-black mechanical parts, amber lights, blue
    team paint. Neutral (scaffolds): brushed steel with amber Team, the under-construction tint."""
    pv = U.preview_materials()
    pv["Machine"] = [
        U.make_material("PV_M_Team", (0.85, 0.02, 0.02), roughness=0.3, emission=0.7, coat=0.5),
        U.make_material("PV_M_Shell", (0.80, 0.83, 0.87), roughness=0.2, metallic=0.1, coat=0.7),
        U.make_material("PV_M_Dark", (0.035, 0.04, 0.05), metallic=0.4, roughness=0.4),
        U.make_material("PV_M_Glow", (0.45, 0.95, 1.0), emission=2.6),
    ]
    pv["Human"] = [
        U.make_material("PV_H_Team", (0.06, 0.25, 0.95), roughness=0.35, metallic=0.3, coat=0.4),
        U.make_material("PV_H_Shell", (0.30, 0.37, 0.47), roughness=0.34, metallic=0.75),
        U.make_material("PV_H_Dark", (0.045, 0.05, 0.058), roughness=0.42, metallic=0.6),
        U.make_material("PV_H_Glow", U.HUMAN_GLOW_COLOR, emission=3.0),
    ]
    pv["Neutral"] = [
        U.make_material("PV_N_Team", (1.0, 0.50, 0.02), roughness=0.4, metallic=0.3, coat=0.4),
        U.make_material("PV_N_Shell", (0.46, 0.48, 0.52), roughness=0.4, metallic=0.7),
        U.make_material("PV_N_Dark", (0.045, 0.05, 0.058), roughness=0.42, metallic=0.6),
        U.make_material("PV_N_Glow", U.HUMAN_GLOW_COLOR, emission=3.0),
    ]
    return pv


class Rig:
    """Lit lineup of copies with per-look preview materials (originals hidden)."""

    def __init__(self, objects):
        self.objects = objects
        self.pv = preview_materials()
        scene = self.scene = bpy.context.scene
        scene.render.engine = "CYCLES"
        scene.cycles.device = "CPU"
        scene.cycles.samples = 64
        scene.cycles.use_denoising = True
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = "PNG"
        scene.view_settings.view_transform = "Standard"
        world = bpy.data.worlds.new("PV_World")
        scene.world = world
        world.use_nodes = True
        bg = world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = (0.10, 0.12, 0.16, 1.0)   # ambient sky so shadowed sides still read
        bg.inputs["Strength"].default_value = 1.2

        self.lights = {}
        self.sun((0.5, 0.6, -0.8), 2.6, (1.0, 0.97, 0.92), "PV_Key")
        self.sun((-0.6, 0.3, -0.5), 0.9, (0.6, 0.75, 1.0), "PV_Fill")
        self.sun((0.1, -0.7, -0.5), 0.6, (0.8, 0.85, 1.0), "PV_Front")

        cube = bpy.data.meshes.new("PV_Ground")
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bm.to_mesh(cube)
        bm.free()
        cube.materials.append(U.make_material("PV_Ground", (0.05, 0.055, 0.065), roughness=0.9))
        self.grounds = []
        for name in ("PV_Ground_A", "PV_Ground_B", "PV_Ground_C"):
            ground = bpy.data.objects.new(name, cube)
            scene.collection.objects.link(ground)
            self.grounds.append(ground)

        self.props = {}
        for src in objects:
            self.props[src.name] = self.copy_of(src, look_of(src.name))

        self.cam_data = bpy.data.cameras.new("PV_Cam")
        self.cam = bpy.data.objects.new("PV_Cam", self.cam_data)
        scene.collection.objects.link(self.cam)
        scene.camera = self.cam

    def sun(self, direction, energy, color, name):
        data = bpy.data.lights.new(name, "SUN")
        data.energy = energy
        data.color = color
        data.angle = math.radians(6)
        obj = bpy.data.objects.new(name, data)
        obj.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
        self.scene.collection.objects.link(obj)
        self.lights[name] = obj

    def copy_of(self, src, look):
        copy = src.copy()
        self.scene.collection.objects.link(copy)
        src.hide_render = True
        for slot, material in zip(copy.material_slots, self.pv[look]):
            slot.link = "OBJECT"
            slot.material = material
        return copy

    def place(self, view):
        """Side view: rows stacked in Z. RTS view: rows separated in Y (Machine far, Construction near)."""
        for src in self.objects:
            prop = self.props[src.name]
            col, row = COLUMN[kind_of(src.name)], ROW[look_of(src.name)]
            prop.hide_render = False
            if view == "side":
                prop.location = (col * COLUMN_DX, 0.0, (2 - row) * ROW_DZ - GROUND)
            else:
                prop.location = (col * COLUMN_DX, (1 - row) * ROW_DY, -GROUND)
        cx = 2.5 * COLUMN_DX
        for ground in self.grounds:
            ground.hide_render = True
        if view == "side":
            for row, ground in enumerate(self.grounds):
                ground.hide_render = False
                ground.scale = (29.0, 5.2, 0.04)
                ground.location = (cx, 0.0, (2 - row) * ROW_DZ - 0.02)
        else:
            ground = self.grounds[0]
            ground.hide_render = False
            ground.scale = (30.0, 22.0, 0.04)
            ground.location = (cx, 0.0, -0.02)

    def camera_side(self):
        self.scene.render.resolution_x, self.scene.render.resolution_y = PREVIEW_SIZE
        self.cam_data.type = "ORTHO"
        self.cam_data.ortho_scale = 27.0
        self.cam.location = (2.5 * COLUMN_DX, -60.0, ROW_DZ + 2.0)
        self.cam.rotation_euler = (math.radians(90), 0, 0)

    def camera_rts(self, target=(2.5 * COLUMN_DX, -0.3, 0.8), distance=46.0, azimuth=25.0, lens=55, pitch=50.0):
        """50 degree pitch; azimuth rotates the camera around the target (0 = looking along +Y)."""
        self.cam_data.type = "PERSP"
        self.cam_data.lens = lens
        offset = Vector((0.0, -math.cos(math.radians(pitch)), math.sin(math.radians(pitch)))) * distance
        offset.rotate(Euler((0, 0, math.radians(azimuth))))
        self.cam.location = Vector(target) + offset
        self.cam.rotation_euler = (math.radians(90 - pitch), 0, math.radians(azimuth))

    def render(self, path):
        bpy.context.view_layer.update()
        self.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

    def render_field(self, path):
        """RTS-distance field render on Campus Zero ground under dusk light, with the existing units for scale.

        Ground albedos, camera (50 degree pitch, 85 mm, 130 m) and dusk light are the unit PreviewField.png
        settings (U.FIELD_* / U.CAMPUS_*), so a Frontline here is about 40 px tall at 2000 px wide."""
        assert os.path.exists(UNITS_BLEND), "run Build/GenerateUnitMeshes.py first (needs %s)" % UNITS_BLEND
        scene = self.scene
        wanted = sorted({n for n, _, _ in FIELD_UNITS})
        with bpy.data.libraries.load(UNITS_BLEND, link=False) as (src, dst):
            dst.objects = [n for n in src.objects if n in wanted]
        units = {o.name: o for o in dst.objects}
        assert sorted(units) == wanted, "Units.blend lacks %s" % sorted(set(wanted) - set(units))
        scene.render.resolution_x, scene.render.resolution_y = U.FIELD_SIZE
        for light in self.lights.values():
            light.hide_render = True
        self.sun(U.FIELD_KEY_DIR, U.FIELD_KEY_ENERGY, U.FIELD_KEY_COLOR, "PV_DuskKey")
        self.sun(U.FIELD_FILL_DIR, U.FIELD_FILL_ENERGY, U.FIELD_FILL_COLOR, "PV_DuskFill")
        bg = scene.world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = (*U.FIELD_SKY_COLOR, 1.0)
        bg.inputs["Strength"].default_value = U.FIELD_SKY_STRENGTH

        for src in self.objects:
            prop = self.props[src.name]
            prop.hide_render = src.name not in FIELD_BUILDINGS
            if not prop.hide_render:
                prop.location = (*FIELD_BUILDINGS[src.name], -GROUND)
        for name, x, y in FIELD_UNITS:
            unit = units[name].copy()   # copies share the mesh; the loaded originals stay unlinked
            scene.collection.objects.link(unit)
            for slot, material in zip(unit.material_slots, self.pv[look_of(name)]):
                slot.link = "OBJECT"
                slot.material = material
            unit.location = (x, y, 0.6)
        asphalt, scrap = self.grounds[0], self.grounds[1]
        for ground, name, color, roughness in ((asphalt, "PV_Asphalt", U.CAMPUS_ASPHALT, 0.9),
                                               (scrap, "PV_ScrapGround", U.CAMPUS_SCRAP, 0.95)):
            ground.material_slots[0].link = "OBJECT"
            ground.material_slots[0].material = U.make_material(name, color, roughness=roughness)
            ground.hide_render = False
        asphalt.scale = (600.0, 600.0, 0.04)
        asphalt.location = (0.0, 0.0, -0.02)
        scrap.scale = (30.0, 40.0, 0.04)
        scrap.location = (-15.0, 0.0, -0.018)
        self.cam_data.clip_end = 2000.0
        self.camera_rts(target=(0.0, 0.0, 0.5), distance=U.FIELD_DISTANCE, azimuth=25.0, lens=85)
        self.render(path)


def render_previews(objects):
    rig = Rig(objects)
    rig.place("side")
    rig.camera_side()
    rig.render(os.path.join(OUT, "Preview.png"))
    rig.place("rts")
    rig.scene.render.resolution_x, rig.scene.render.resolution_y = RTS_SIZE
    rig.camera_rts()
    rig.render(os.path.join(OUT, "PreviewRTS.png"))
    rig.render_field(os.path.join(OUT, "PreviewField.png"))



BLEND_DX = 4.2   # Buildings.blend grid: columns by kind, rows by faction (same order as the previews)
BLEND_DY = 4.8


def save_layout(objects):
    """Write Art/Buildings/Buildings.blend: the meshes in a grid (columns Barracks / Barracks_Frontline / Ranged /
    Siege / Outpost /
    Workshop along +X, rows Machine / Human / Construction along -Y), ground on z = 0, with faction preview
    materials linked per object so the file reads at a glance. Mesh data keeps the canonical
    Team/Shell/Dark/Glow slots. Called on every run, also when a build step or a check fails, so an open
    Blender window always shows the current state. NOT for export: run checks and FBX export first."""
    pv = preview_materials()
    for obj in objects:
        look = look_of(obj.name)
        for slot, material in zip(obj.material_slots, pv[look]):
            slot.link = "OBJECT"
            slot.material = material
        obj.location = (COLUMN[kind_of(obj.name)] * BLEND_DX, (1 - ROW[look]) * BLEND_DY, -GROUND)
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "Buildings.blend"))


def build_all(mats):
    """Build every mesh. If one builder raises, save what exists so far (dropping its half-built parts),
    then re-raise."""
    objects = []
    try:
        for look, kind, build in MODELS:
            obj = build(mats)
            assert obj.name == "SM_%s_%s" % (look, kind), obj.name
            objects.append(obj)
    except Exception:
        for stray in [o for o in bpy.data.objects if o not in objects]:
            bpy.data.objects.remove(stray)
        save_layout(objects)
        raise
    return objects


def verify(objects):
    """Run check() on every mesh and require each configured Barracks to add real geometry over the neutral
    base of its faction; on the first failure save the blend (to inspect it), then re-raise."""
    try:
        for obj in objects:
            check(obj)
        tris = {o.name: triangles(o) for o in objects}
        for name, count in tris.items():
            if kind_of(name).startswith("Barracks_"):
                base = name.rsplit("_", 1)[0]
                assert count > tris[base] + 500, "%s adds only %d triangles to %s" % (name, count - tris[base], base)
    except AssertionError:
        save_layout(objects)
        raise


def main():
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objects = build_all(U.canonical_materials())
    verify(objects)
    for obj in objects:
        U.export_fbx(obj, os.path.join(OUT, obj.name + ".fbx"))
    print("MESH | bounds min | bounds max | size (x y z) | tris | upward Team m2 (share of footprint box)")
    for obj in objects:
        lo, hi = bounds(obj)
        area = team_top_area(obj)
        foot = (2 * HALF[kind_of(obj.name).split("_")[0]]) ** 2
        print("%s | (%.3f, %.3f, %.3f) | (%.3f, %.3f, %.3f) | %.2f x %.2f x %.2f | %d | %.2f (%d%%)" % (
            obj.name, *lo, *hi, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, triangles(obj), area, round(100 * area / foot)))
    sys.stdout.flush()
    save_layout(objects)
    render_previews(objects)
    print("BUILDING_MESHES_DONE")


if __name__ == "__main__":
    main()
