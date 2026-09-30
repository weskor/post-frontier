"""SC2-style shared master material for CoopRTS, prototyped in Blender (no Unreal needed).

Run headless from the repo root (Blender 5.2.1, needs Art/Textures from the CC0 texture library):

    blender -b --factory-startup -P Build/MasterMaterials.py                # everything
    blender -b --factory-startup -P Build/MasterMaterials.py -- --save-only # only Art/Materials/MaterialPreview.blend
    blender -b --factory-startup -P Build/MasterMaterials.py -- --only units,field   # subset of renders

or import it (``sys.path.insert(0, "Build"); import MasterMaterials as mm``) and call ``mm.build_library()``,
``mm.bake_masks(obj, scope)`` and ``mm.apply(obj, faction, scope)``. The generators
(GenerateUnitMeshes.py, GenerateBuildingMeshes.py, GenerateEnvironmentKit.py) and their outputs are never
modified: this script appends COPIES of the meshes from Art/Units/Units.blend, Art/Buildings/Buildings.blend and
Art/Environment/Environment.blend, and writes only under Art/Materials/.

Outputs (Art/Materials/)
    MaterialPreview.blend      units, buildings and kit on an asphalt floor with the materials applied; saved
                               at the start of every run and again after each render. The collection
                               "Flat (before)" holds the untextured copies, hidden; unhide it to compare.
    Before-After-Units.png / Before-After-Buildings.png / Before-After-Kit.png   flat on top, textured below,
                               same 50 degree RTS camera
    Close-Human.png / Close-Machine.png                   one close-up per faction
    FieldView.png              RTS distance on the Campus Zero asphalt, about 40 px per unit
    FieldView-Flat.png         the same shot with the old flat materials
    UNREAL.md (hand written)   port plan

Slots and factions
    Team    player colour paint, parameter TeamColor (default Human blue (0.04, 0.50, 1.0), Machine red (1, 0.08, 0.08))
    Shell   painted metal body (Human gunmetal / steel blue, Machine pearl white, Cluster glossy white)
    Dark    dark mechanical parts (Human dark gunmetal, Machine near black)
    Glow    emissive (Human amber, Machine cyan)
    Accent  kit only: Human hazard paint (yellow with black stripes), Machine red lens light

One master graph serves every slot (Unreal: M_Shared, one material, instances per slot). The node groups below
are its functions; each lists the Unreal graph that replaces it.

NODE GROUPS
----------------------------------------------------------------------------------------------------------
SC2_Master (group; the material function body of M_Shared)
    inputs   BaseColor, TeamColor, UseTeam, Metallic, Roughness, ClearCoat, PearlTint, PearlAmount,
             BareMetalColor, EdgeWear, EdgeHighlight, GrimeColor, GrimeAmount, GroundGrime, PanelStrength,
             PanelLine, HazardStripes, HazardDark, EmissiveColor, EmissiveIntensity, SelfLit and the three
             sampled texture values PanelHeight, WearMask, GrimeMask (float, 0..1)
    outputs  BSDF
    Unreal   Base Color, Metallic, Roughness, Normal, Emissive Color from the MF_* pieces below, Clear Coat
             from ClearCoat. UseTeam and HazardStripes are Static Switch Parameters; all other inputs are
             Scalar / Vector Parameters. PanelHeight, WearMask and GrimeMask come from the material's three
             MF_Triplanar samples (Texture Object parameters TexPanel, TexWear, TexGrime; the sampled
             channel is a per-instance choice, see UNREAL.md).
    Steps    1 Colour   = Lerp(BaseColor, TeamColor, UseTeam), optional hazard stripes
             2 SC2_Wear (paint chips, edge highlight, grime, panel seams)
             3 Pearl    = Lerp(Colour, PearlTint, Fresnel(exp 3) * PearlAmount)
             4 Normal   = Bump node on (PanelHeight * PanelStrength), distance 2 cm
                          (Unreal: NormalFromHeightmap or a triplanar normal map, see UNREAL.md)
             5 Emissive = Lerp(EmissiveColor, TeamColor, UseTeam) * (EmissiveIntensity + UseTeam * SelfLit)
SC2_Wear (group; MF_SC2_Wear)
    inputs   Color, Metallic, Roughness, Edge, Cavity, Ground, WearMask, GrimeMask, PanelHeight and the
             EdgeWear / EdgeHighlight / BareMetalColor / GrimeColor / GrimeAmount / GroundGrime / PanelLine
             parameters
    outputs  Color, Metallic, Roughness
    Unreal   (per output the same maths, ten Multiply / Lerp / SmoothStep nodes):
        chipSignal = Edge * (0.55 + 0.9 * WearMask)
        chip       = SmoothStep(t, t + 0.12, chipSignal) * Saturate(EdgeWear * 10),  t = 0.9 - 0.6 * EdgeWear
        highlight  = SmoothStep(0.05, 0.6, Edge) * EdgeHighlight
        seam       = 1 - SmoothStep(0.05, 0.45, PanelHeight)
        grime      = Saturate(Cavity * GrimeAmount * (0.35 + 1.1 * GrimeMask) + Ground * GroundGrime
                              * GrimeMask * 1.3 + seam * GrimeAmount * 0.6)
        Color      = Lerp(Color, Screen(Color, Color), highlight * 0.9)          highlight: paint catches light
        Color      = Lerp(Color, BareMetalColor, chip)                             paint chipped off the edge
        Color      = Lerp(Color, Color * 0.15, seam * PanelLine)                   seam darkening
        Color      = Lerp(Color, Color * GrimeColor, grime)                        grime
        Metallic   = Lerp(Metallic, 1, chip) * (1 - 0.7 * grime)
        Roughness  = Lerp(Lerp(Roughness, 0.32, chip) - highlight * 0.15, 0.9, grime)
SC2_Masks (group; "MASK_MODE")
    outputs  Edge, Cavity, Ground   (each float 0..1)
    baked    Reads the colour attribute "SC2Mask" (R = Edge, G = Cavity, B = Ground). Written by
             bake_masks(): Edge = convex chamfer strips (vertices whose faces are all narrow and fan out over
             40+ degrees, so plates, rivets and cylinders stay clean), Cavity = concave creases plus hemisphere ray occlusion, Ground =
             0..1 falloff above the object's lowest point. This is the version the Unreal port uses:
             Vertex Color node, R / G / B, because Unreal has no bevel or AO node. Also works in EEVEE.
    live     Cycles only, no bake: Edge = SmoothStep on Geometry > Pointiness, Cavity = AO node (Inside off,
             Only Local off), Ground = Object position Z. Used for the "live" comparison shot only.
Triplanar sampling (Python helper _sample(), lives in the material, NOT in the group)
    Texture Coordinate (Object) -> Mapping (Scale = 1 / TileSize) -> Image Texture (Projection Box, Blend 0.25,
    Non-Color) -> Color to BW. Unreal: MF_Triplanar, a Material Function wrapping WorldAlignedTexture in
    LOCAL space (Transform Position World->Local, Transform Vector World->Local for the normal) so the texture
    rides with the actor; input Texture Object + TileSize, output R.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Vector, kdtree
from mathutils.bvhtree import BVHTree

try:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:
    ROOT = os.getcwd()
TEX = os.path.join(ROOT, "Art", "Textures")
OUT = os.path.join(ROOT, "Art", "Materials")
BLEND_PATH = os.path.join(OUT, "MaterialPreview.blend")
MASK_ATTR = "SC2Mask"
MASK_MODE = "baked"   # "baked" (vertex colours, Unreal-compatible) or "live" (Cycles Pointiness + AO)

SLOTS = ("Team", "Shell", "Dark", "Glow", "Accent")
FACTIONS = ("Human", "Machine", "Cluster")

# ---------------------------------------------------------------------------------------------------------
# Texture assets (Art/Textures, CC0). Each entry: folder/asset and the channel file used.
# ---------------------------------------------------------------------------------------------------------
TEXTURES = {
    # role: (folder, asset, map)
    "PlatesHuman": ("Panel", "MetalPlates002", "Height"),     # riveted plate seams
    "PlatesMachine": ("SciFiFloor", "Tiles108", "Height"),    # hairline panel grid
    "PlatesDark": ("Panel", "MetalPlates006", "Height"),      # diagonal armour tiles
    "ChipHuman": ("PaintedMetal", "PaintedMetal014", "Roughness"),   # bright blotches = paint scuffed through
    "ChipMachine": ("PaintedMetal", "PaintedMetal014", "Roughness"),
    "GrimeHuman": ("Metal", "Metal046A", "Height"),           # cloudy stains
    "GrimeMachine": ("Concrete", "Concrete016", "Height"),    # soft haze
    "GrimeDark": ("Metal", "Metal010", "Roughness"),          # brushed streaks
    "AsphaltColor": ("Asphalt", "Asphalt026A", "BaseColor"),
    "AsphaltRough": ("Asphalt", "Asphalt026A", "Roughness"),
}

# Scope: which asset class the material is for. tile = metres per texture tile (panel, wear, grime).
SCOPES = {
    "unit": dict(tile=(0.9, 0.7, 1.3), glow=2.0, ao=0.35, ground=0.45, band=0.016, grime=1.0),
    "bld": dict(tile=(2.4, 1.6, 3.0), glow=2.5, ao=0.6, ground=0.8, band=0.03, grime=0.8),
    "env": dict(tile=(3.6, 2.4, 4.5), glow=3.0, ao=1.0, ground=1.2, band=0.05, grime=0.55),
}

HUMAN_BLUE = (0.04, 0.50, 1.0)
MACHINE_RED = (1.0, 0.08, 0.08)
AMBER = (1.0, 0.55, 0.15)
CYAN = (0.35, 0.85, 1.0)


def _p(**kw):
    return kw


# Per (faction, slot) parameter set. Anything unspecified takes DEFAULTS. "tex" picks (panel, wear, grime) roles.
DEFAULTS = dict(
    BaseColor=(0.2, 0.2, 0.2), TeamColor=HUMAN_BLUE, UseTeam=0.0, Metallic=0.0, Roughness=0.5, ClearCoat=0.0,
    PearlTint=(0.8, 0.85, 1.0), PearlAmount=0.0, BareMetalColor=(0.60, 0.62, 0.66), EdgeWear=0.0,
    EdgeHighlight=0.0, GrimeColor=(0.36, 0.32, 0.28), GrimeAmount=0.0, GroundGrime=0.0, PanelStrength=0.0,
    PanelLine=0.0, HazardStripes=0.0, HazardDark=(0.02, 0.02, 0.02), EmissiveColor=(0.0, 0.0, 0.0),
    EmissiveIntensity=0.0, SelfLit=0.0,
)

PARAMS = {
    ("Human", "Shell"): _p(BaseColor=(0.30, 0.40, 0.56), Metallic=0.22, Roughness=0.42, EdgeWear=0.7,
                            EdgeHighlight=0.9, BareMetalColor=(0.66, 0.68, 0.72), GrimeAmount=0.45,
                            GroundGrime=0.55, PanelStrength=0.6, PanelLine=0.7,
                            tex=("PlatesHuman", "ChipHuman", "GrimeHuman")),
    ("Human", "Dark"): _p(BaseColor=(0.09, 0.10, 0.12), Metallic=0.5, Roughness=0.48, EdgeWear=0.55,
                           EdgeHighlight=0.9, BareMetalColor=(0.46, 0.48, 0.52), GrimeAmount=0.25,
                           GroundGrime=0.4, PanelStrength=0.4, PanelLine=0.4,
                           tex=("PlatesDark", "ChipHuman", "GrimeDark")),
    ("Human", "Team"): _p(BaseColor=HUMAN_BLUE, TeamColor=HUMAN_BLUE, UseTeam=1.0, Metallic=0.0, Roughness=0.40,
                           ClearCoat=0.2, EdgeWear=0.4, EdgeHighlight=0.3,
                           BareMetalColor=(0.62, 0.64, 0.68), GrimeAmount=0.2, GroundGrime=0.3,
                           PanelStrength=0.35, PanelLine=0.35, SelfLit=0.10,
                           tex=("PlatesHuman", "ChipHuman", "GrimeHuman")),
    ("Human", "Glow"): _p(BaseColor=(0.05, 0.03, 0.02), Roughness=0.5, EmissiveColor=AMBER,
                           EmissiveIntensity=1.0, EdgeWear=0.0, GrimeAmount=0.0,
                           tex=("PlatesHuman", "ChipHuman", "GrimeHuman")),
    ("Human", "Accent"): _p(BaseColor=(0.80, 0.55, 0.02), Metallic=0.1, Roughness=0.55, EdgeWear=0.9,
                             EdgeHighlight=0.5, BareMetalColor=(0.5, 0.5, 0.52), GrimeAmount=0.55,
                             GroundGrime=0.6, PanelStrength=0.2, PanelLine=0.2, HazardStripes=1.0,
                             tex=("PlatesHuman", "ChipHuman", "GrimeHuman")),
    ("Machine", "Shell"): _p(BaseColor=(0.88, 0.90, 0.94), Metallic=0.12, Roughness=0.20, ClearCoat=0.8,
                              PearlTint=(0.62, 0.80, 1.0), PearlAmount=0.30, EdgeWear=0.06, EdgeHighlight=0.65,
                              BareMetalColor=(0.85, 0.87, 0.90), GrimeColor=(0.62, 0.64, 0.70),
                              GrimeAmount=0.16, GroundGrime=0.06, PanelStrength=0.5, PanelLine=0.6,
                              tex=("PlatesMachine", "ChipMachine", "GrimeMachine")),
    ("Machine", "Dark"): _p(BaseColor=(0.022, 0.028, 0.040), Metallic=0.6, Roughness=0.30, ClearCoat=0.4,
                             EdgeWear=0.2, EdgeHighlight=0.8, BareMetalColor=(0.35, 0.40, 0.48),
                             GrimeAmount=0.15, PanelStrength=0.3, PanelLine=0.3,
                             tex=("PlatesMachine", "ChipMachine", "GrimeMachine")),
    ("Machine", "Team"): _p(BaseColor=MACHINE_RED, TeamColor=MACHINE_RED, UseTeam=1.0, Roughness=0.25,
                             ClearCoat=0.9, EdgeHighlight=0.5, PanelStrength=0.25, PanelLine=0.35,
                             GrimeAmount=0.12, SelfLit=0.35,
                             tex=("PlatesMachine", "ChipMachine", "GrimeMachine")),
    ("Machine", "Glow"): _p(BaseColor=(0.02, 0.05, 0.06), Roughness=0.3, EmissiveColor=CYAN,
                             EmissiveIntensity=1.0, tex=("PlatesMachine", "ChipMachine", "GrimeMachine")),
    ("Machine", "Accent"): _p(BaseColor=(0.05, 0.005, 0.005), Roughness=0.3, EmissiveColor=(1.0, 0.05, 0.03),
                               EmissiveIntensity=1.0, tex=("PlatesMachine", "ChipMachine", "GrimeMachine")),
}
PARAMS[("Cluster", "Shell")] = dict(PARAMS[("Machine", "Shell")], BaseColor=(0.90, 0.92, 0.95), Roughness=0.14,
                                    ClearCoat=1.0, EdgeHighlight=0.55, GrimeAmount=0.18)
for _slot in SLOTS:
    PARAMS.setdefault(("Cluster", _slot), PARAMS[("Machine", _slot)])
# glow intensity per scope is EmissiveIntensity * scope glow (unit = 2.0 in Unreal); the kit Machine Accent
# stays low (1.0) because 8.0 clips red to peach (Docs/World.md, Build/ImportEnvironmentKit.py)
ACCENT_MACHINE_GLOW = 1.0


# ---------------------------------------------------------------------------------------------------------
# Node building helpers
# ---------------------------------------------------------------------------------------------------------
class Graph:
    """Tiny DSL over a node tree: every helper returns an output socket; columns follow the data flow."""

    def __init__(self, tree):
        self.tree = tree
        self.cols = {}
        self.rows = {}

    def _place(self, node, col):
        row = self.rows.get(col, 0)
        self.rows[col] = row + 1
        node.location = (col * 240, -row * 200)
        self.cols[node.name] = col

    def _col_of(self, values):
        deepest = 0
        for v in values:
            if isinstance(v, bpy.types.NodeSocket):
                deepest = max(deepest, self.cols.get(v.node.name, 0))
        return deepest + 1

    def node(self, idname, inputs=(), **props):
        node = self.tree.nodes.new(idname)
        for key, value in props.items():
            setattr(node, key, value)
        values = [v for _, v in inputs]
        for key, value in inputs:
            self.set(node.inputs[key], value)
        self._place(node, self._col_of(values))
        return node

    def set(self, inp, value):
        if isinstance(value, bpy.types.NodeSocket):
            self.tree.links.new(value, inp)
        elif value is not None:
            inp.default_value = value if not isinstance(value, tuple) or len(value) == len(inp.default_value) \
                else (*value, 1.0)

    def m(self, op, a, b=None, c=None, clamp=False):
        node = self.node("ShaderNodeMath", [(0, a)] + ([(1, b)] if b is not None else [])
                         + ([(2, c)] if c is not None else []), operation=op, use_clamp=clamp)
        return node.outputs[0]

    def smooth(self, x, lo, hi):
        node = self.node("ShaderNodeMapRange", [("Value", x), ("From Min", lo), ("From Max", hi)],
                         interpolation_type="SMOOTHSTEP", clamp=True)
        return node.outputs["Result"]

    def fmix(self, fac, a, b):
        node = self.node("ShaderNodeMix", [(0, fac), (2, a), (3, b)], data_type="FLOAT")
        return node.outputs[0]

    def cmix(self, fac, a, b, blend="MIX"):
        node = self.node("ShaderNodeMix", [(0, fac), (6, a), (7, b)], data_type="RGBA", blend_type=blend)
        return node.outputs[2]

    def scale(self, color, factor):
        node = self.node("ShaderNodeVectorMath", [(0, color), ("Scale", factor)], operation="SCALE")
        return node.outputs[0]


def _interface(tree, inputs=(), outputs=()):
    for name, kind, default, lo, hi in inputs:
        sock = tree.interface.new_socket(name=name, in_out="INPUT", socket_type=kind)
        if default is not None:
            sock.default_value = default if kind != "NodeSocketColor" else (*default, 1.0)
        if lo is not None:
            sock.min_value = lo
            sock.max_value = hi
    for name, kind in outputs:
        tree.interface.new_socket(name=name, in_out="OUTPUT", socket_type=kind)


def _group_io(tree):
    gin = tree.nodes.new("NodeGroupInput")
    gout = tree.nodes.new("NodeGroupOutput")
    gin.location = (-300, 0)
    return gin, gout


F, C = "NodeSocketFloat", "NodeSocketColor"


def build_masks_group(mode):
    name = "SC2_Masks_" + mode
    if name in bpy.data.node_groups:
        return bpy.data.node_groups[name]
    tree = bpy.data.node_groups.new(name, "ShaderNodeTree")
    _interface(tree, outputs=(("Edge", F), ("Cavity", F), ("Ground", F)))
    _interface(tree, inputs=(("GroundHeight", F, 0.6, 0.01, 10.0), ("AODistance", F, 0.4, 0.01, 10.0)))
    g = Graph(tree)
    gin, gout = _group_io(tree)
    gout.location = (1200, 0)
    if mode == "baked":
        attr = g.node("ShaderNodeAttribute", attribute_type="GEOMETRY", attribute_name=MASK_ATTR)
        sep = g.node("ShaderNodeSeparateColor", [("Color", attr.outputs["Color"])])
        edge, cavity, ground = sep.outputs["Red"], sep.outputs["Green"], sep.outputs["Blue"]
    else:
        geo = g.node("ShaderNodeNewGeometry")
        edge = g.smooth(geo.outputs["Pointiness"], 0.53, 0.66)
        ao = g.node("ShaderNodeAmbientOcclusion", [("Distance", gin.outputs["AODistance"])], inside=False,
                    only_local=False, samples=8)
        cavity = g.m("SUBTRACT", 1.0, ao.outputs["AO"], clamp=True)
        pos = g.node("ShaderNodeTexCoord")
        sepz = g.node("ShaderNodeSeparateXYZ", [("Vector", pos.outputs["Object"])])
        # object origin is the footprint centre; ground is (Z + GroundHeight) falloff at the base, roughly
        ground = g.m("SUBTRACT", 1.0, g.m("DIVIDE", g.m("ADD", sepz.outputs["Z"], 0.7), gin.outputs["GroundHeight"]),
                     clamp=True)
    tree.links.new(edge, gout.inputs["Edge"])
    tree.links.new(cavity, gout.inputs["Cavity"])
    tree.links.new(ground, gout.inputs["Ground"])
    return tree


def build_wear_group():
    if "SC2_Wear" in bpy.data.node_groups:
        return bpy.data.node_groups["SC2_Wear"]
    tree = bpy.data.node_groups.new("SC2_Wear", "ShaderNodeTree")
    _interface(tree, inputs=(
        ("Color", C, (0.5, 0.5, 0.5), None, None), ("Metallic", F, 0.0, 0.0, 1.0), ("Roughness", F, 0.5, 0.0, 1.0),
        ("Edge", F, 0.0, 0.0, 1.0), ("Cavity", F, 0.0, 0.0, 1.0), ("Ground", F, 0.0, 0.0, 1.0),
        ("WearMask", F, 0.5, 0.0, 1.0), ("GrimeMask", F, 0.5, 0.0, 1.0), ("PanelHeight", F, 1.0, 0.0, 1.0),
        ("EdgeWear", F, 0.0, 0.0, 1.0), ("EdgeHighlight", F, 0.0, 0.0, 1.0),
        ("BareMetalColor", C, (0.6, 0.62, 0.66), None, None), ("GrimeColor", C, (0.22, 0.19, 0.16), None, None),
        ("GrimeAmount", F, 0.0, 0.0, 1.0), ("GroundGrime", F, 0.0, 0.0, 1.0), ("PanelLine", F, 0.0, 0.0, 1.0)),
        outputs=(("Color", C), ("Metallic", F), ("Roughness", F)))
    g = Graph(tree)
    gin, gout = _group_io(tree)
    gout.location = (2600, 0)
    i = gin.outputs

    # chip: paint lost on edges, broken up by the wear texture
    chip_signal = g.m("MULTIPLY", i["Edge"], g.m("MULTIPLY_ADD", i["WearMask"], 0.9, 0.55))
    threshold = g.m("SUBTRACT", 0.9, g.m("MULTIPLY", i["EdgeWear"], 0.6))
    chip = g.node("ShaderNodeMapRange", [("Value", chip_signal), ("From Min", threshold),
                                         ("From Max", g.m("ADD", threshold, 0.12))],
                  interpolation_type="SMOOTHSTEP", clamp=True).outputs["Result"]
    chip = g.m("MULTIPLY", chip, g.m("MULTIPLY", i["EdgeWear"], 10.0, clamp=True))
    highlight = g.m("MULTIPLY", g.smooth(i["Edge"], 0.05, 0.6), i["EdgeHighlight"])
    seam = g.m("SUBTRACT", 1.0, g.smooth(i["PanelHeight"], 0.05, 0.45))
    grime = g.m("ADD", g.m("MULTIPLY", g.m("MULTIPLY", i["Cavity"], i["GrimeAmount"]),
                           g.m("MULTIPLY_ADD", i["GrimeMask"], 1.1, 0.35)),
                g.m("ADD", g.m("MULTIPLY", g.m("MULTIPLY", i["Ground"], i["GroundGrime"]),
                               g.m("MULTIPLY", i["GrimeMask"], 1.3)),
                    g.m("MULTIPLY", g.m("MULTIPLY", seam, i["GrimeAmount"]), 0.6)), clamp=True)

    lifted = g.cmix(0.55, i["Color"], (1.0, 1.0, 1.0, 1.0))
    color = g.cmix(g.m("MULTIPLY", highlight, 0.95), i["Color"], lifted, "SCREEN")
    color = g.cmix(chip, color, i["BareMetalColor"])
    color = g.cmix(g.m("MULTIPLY", seam, i["PanelLine"]), color, g.scale(color, 0.15))
    color = g.cmix(grime, color, i["GrimeColor"], "MULTIPLY")

    metallic = g.m("MULTIPLY", g.fmix(chip, i["Metallic"], 1.0), g.m("SUBTRACT", 1.0, g.m("MULTIPLY", grime, 0.7)))
    rough = g.m("SUBTRACT", g.fmix(chip, i["Roughness"], 0.32), g.m("MULTIPLY", highlight, 0.15))
    rough = g.fmix(grime, rough, 0.9)
    tree.links.new(color, gout.inputs["Color"])
    tree.links.new(metallic, gout.inputs["Metallic"])
    tree.links.new(rough, gout.inputs["Roughness"])
    return tree


MASTER_INPUTS = (
    ("BaseColor", C, (0.2, 0.2, 0.2), None, None), ("TeamColor", C, HUMAN_BLUE, None, None),
    ("UseTeam", F, 0.0, 0.0, 1.0), ("Metallic", F, 0.0, 0.0, 1.0), ("Roughness", F, 0.5, 0.0, 1.0),
    ("ClearCoat", F, 0.0, 0.0, 1.0), ("PearlTint", C, (0.8, 0.85, 1.0), None, None),
    ("PearlAmount", F, 0.0, 0.0, 1.0), ("BareMetalColor", C, (0.6, 0.62, 0.66), None, None),
    ("EdgeWear", F, 0.0, 0.0, 1.0), ("EdgeHighlight", F, 0.0, 0.0, 1.0),
    ("GrimeColor", C, (0.22, 0.19, 0.16), None, None), ("GrimeAmount", F, 0.0, 0.0, 1.0),
    ("GroundGrime", F, 0.0, 0.0, 1.0), ("PanelStrength", F, 0.0, 0.0, 2.0), ("PanelLine", F, 0.0, 0.0, 1.0),
    ("HazardStripes", F, 0.0, 0.0, 1.0), ("HazardDark", C, (0.02, 0.02, 0.02), None, None),
    ("EmissiveColor", C, (0.0, 0.0, 0.0), None, None), ("EmissiveIntensity", F, 0.0, 0.0, 100.0),
    ("SelfLit", F, 0.0, 0.0, 10.0),
    ("PanelHeight", F, 1.0, 0.0, 1.0), ("WearMask", F, 0.5, 0.0, 1.0), ("GrimeMask", F, 0.5, 0.0, 1.0),
    ("GroundHeight", F, 0.6, 0.01, 10.0), ("AODistance", F, 0.4, 0.01, 10.0),
)


def build_master_group(mode):
    name = "SC2_Master_" + mode
    if name in bpy.data.node_groups:
        return bpy.data.node_groups[name]
    tree = bpy.data.node_groups.new(name, "ShaderNodeTree")
    _interface(tree, inputs=MASTER_INPUTS, outputs=(("BSDF", "NodeSocketShader"),))
    g = Graph(tree)
    gin, gout = _group_io(tree)
    gout.location = (2400, 0)
    i = gin.outputs

    masks = g.node("ShaderNodeGroup", [("GroundHeight", i["GroundHeight"]), ("AODistance", i["AODistance"])],
                   node_tree=build_masks_group(mode))
    base = g.cmix(i["UseTeam"], i["BaseColor"], i["TeamColor"])

    # hazard stripes: diagonal bands from the object-space position, box-projection free
    pos = g.node("ShaderNodeTexCoord")
    sep = g.node("ShaderNodeSeparateXYZ", [("Vector", pos.outputs["Object"])])
    diagonal = g.m("ADD", g.m("ADD", sep.outputs["X"], sep.outputs["Y"]), sep.outputs["Z"])
    stripe = g.m("GREATER_THAN", g.m("FRACT", g.m("MULTIPLY", diagonal, 11.0)), 0.5)
    base = g.cmix(g.m("MULTIPLY", stripe, i["HazardStripes"]), base, i["HazardDark"])

    wear = g.node("ShaderNodeGroup", [
        ("Color", base), ("Metallic", i["Metallic"]), ("Roughness", i["Roughness"]),
        ("Edge", masks.outputs["Edge"]), ("Cavity", masks.outputs["Cavity"]), ("Ground", masks.outputs["Ground"]),
        ("WearMask", i["WearMask"]), ("GrimeMask", i["GrimeMask"]), ("PanelHeight", i["PanelHeight"]),
        ("EdgeWear", i["EdgeWear"]), ("EdgeHighlight", i["EdgeHighlight"]), ("BareMetalColor", i["BareMetalColor"]),
        ("GrimeColor", i["GrimeColor"]), ("GrimeAmount", i["GrimeAmount"]), ("GroundGrime", i["GroundGrime"]),
        ("PanelLine", i["PanelLine"])], node_tree=build_wear_group())

    fres = g.node("ShaderNodeLayerWeight", [("Blend", 0.35)])
    color = g.cmix(g.m("MULTIPLY", fres.outputs["Fresnel"], i["PearlAmount"]), wear.outputs["Color"], i["PearlTint"])
    bump = g.node("ShaderNodeBump", [("Strength", i["PanelStrength"]), ("Distance", 0.02), ("Height", i["PanelHeight"])])

    emit_color = g.cmix(i["UseTeam"], i["EmissiveColor"], i["TeamColor"])
    emit_strength = g.m("ADD", i["EmissiveIntensity"], g.m("MULTIPLY", i["UseTeam"], i["SelfLit"]))
    bsdf = g.node("ShaderNodeBsdfPrincipled", [
        ("Base Color", color), ("Metallic", wear.outputs["Metallic"]), ("Roughness", wear.outputs["Roughness"]),
        ("Normal", bump.outputs["Normal"]), ("Coat Weight", i["ClearCoat"]), ("Coat Roughness", 0.08),
        ("Emission Color", emit_color), ("Emission Strength", emit_strength)])
    tree.links.new(bsdf.outputs["BSDF"], gout.inputs["BSDF"])
    return tree


# ---------------------------------------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------------------------------------
_images = {}


def _image(role):
    folder, asset, kind = TEXTURES[role]
    path = os.path.join(TEX, folder, asset, "%s_%s.jpg" % (asset, kind))
    if not os.path.isfile(path):
        raise FileNotFoundError("Missing texture " + path + " (Art/Textures library not fetched?)")
    if path not in _images:
        img = bpy.data.images.load(path)
        img.colorspace_settings.name = "sRGB" if kind == "BaseColor" else "Non-Color"
        _images[path] = img
    return _images[path]


def _sample(g, coord_socket, role, tile):
    """Box-projected texture sample in object space; returns a float socket (see the module docstring)."""
    mapping = g.node("ShaderNodeMapping", [("Vector", coord_socket), ("Scale", (1.0 / tile,) * 3)])
    tex = g.node("ShaderNodeTexImage", [("Vector", mapping.outputs["Vector"])], projection="BOX",
                 projection_blend=0.25, image=_image(role), interpolation="Linear")
    return g.node("ShaderNodeRGBToBW", [("Color", tex.outputs["Color"])]).outputs["Val"]


# Instance overrides on top of PARAMS (Unreal: another material instance of the same parent).
VARIANTS = {
    "Construction": {   # amber-painted scaffolds (CommandBuilding: TeamColor (0.8, 0.6, 0.18) until complete)
        "Team": dict(BaseColor=(1.0, 0.5, 0.02), TeamColor=(1.0, 0.5, 0.02)),
        "Shell": dict(BaseColor=(0.50, 0.52, 0.56), Metallic=0.3),
    },
}


def make_material(faction, slot, scope="unit", variant="", mode=None):
    """Build (or fetch) M_SC2_<Faction>_<Slot>_<Scope>[_<Variant>] with the parameters of PARAMS."""
    mode = mode or MASK_MODE
    name = "M_SC2_%s_%s_%s%s%s" % (faction, slot, scope, "_" + variant if variant else "",
                                   "" if mode == "baked" else "_" + mode)
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    params = dict(DEFAULTS, **PARAMS[(faction, slot)])
    params.update(VARIANTS.get(variant, {}).get(slot, {}))
    sc = SCOPES[scope]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    tree = mat.node_tree
    tree.nodes.clear()
    g = Graph(tree)
    coords = g.node("ShaderNodeTexCoord")
    panel_role, wear_role, grime_role = params["tex"]
    panel = _sample(g, coords.outputs["Object"], panel_role, sc["tile"][0])
    wear = _sample(g, coords.outputs["Object"], wear_role, sc["tile"][1])
    grime = _sample(g, coords.outputs["Object"], grime_role, sc["tile"][2])
    group = g.node("ShaderNodeGroup", [("PanelHeight", panel), ("WearMask", wear), ("GrimeMask", grime)],
                   node_tree=build_master_group(mode))
    for key, value in params.items():
        if key == "tex":
            continue
        if key == "EmissiveIntensity":
            value *= (ACCENT_MACHINE_GLOW if (slot == "Accent" and faction != "Human") else sc["glow"])
        if key in ("GrimeAmount", "GroundGrime"):
            value *= sc["grime"]
        group.inputs[key].default_value = (*value, 1.0) if isinstance(value, tuple) else value
    group.inputs["GroundHeight"].default_value = sc["ground"]
    group.inputs["AODistance"].default_value = sc["ao"]
    out = g.node("ShaderNodeOutputMaterial", [("Surface", group.outputs["BSDF"])])
    out.location = (group.location[0] + 300, 0)
    base = params["TeamColor"] if params["UseTeam"] else params["BaseColor"]
    mat.diffuse_color = (*base, 1.0)   # viewport colour of the solid mode
    mat["faction"], mat["slot"], mat["scope"], mat["variant"] = faction, slot, scope, variant
    return mat


def build_library(scope="unit"):
    return {(f, s): make_material(f, s, scope) for f in FACTIONS for s in SLOTS}


def apply(obj, faction, scope="unit", variant=""):
    """Replace the material slots of obj by name (Team / Shell / Dark / Glow / Accent)."""
    mesh = obj.data
    for index, old in enumerate(list(mesh.materials)):
        slot = old.name.split(".")[0].split("_")[-1] if old else "Shell"
        slot = slot if slot in SLOTS else SLOTS[min(index, 3)]
        mesh.materials[index] = make_material(faction, slot, scope, variant)
    for s in obj.material_slots:
        s.link = "DATA"


# ---------------------------------------------------------------------------------------------------------
# Flat "before" materials: the exact previews of the generators (Build/GenerateUnitMeshes.py preview_materials,
# Build/GenerateEnvironmentKit.py preview_palettes), which mirror the Unreal MI_Unit* / MI_Env* values.
# ---------------------------------------------------------------------------------------------------------
def flat_material(name, color, emission=0.0, metallic=0.0, roughness=0.5, coat=0.0):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
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


def flat_palette(look):
    fm = flat_material
    return {
        "unit_machine": {"Team": fm("F_M_Team", (1.0, 0.01, 0.01), emission=4.0),
                         "Shell": fm("F_M_Shell", (0.88, 0.90, 0.94), roughness=0.15, metallic=0.25, coat=0.7),
                         "Dark": fm("F_M_Dark", (0.05, 0.06, 0.08), roughness=0.3, metallic=0.7),
                         "Glow": fm("F_M_Glow", CYAN, emission=1.3)},
        "unit_human": {"Team": fm("F_H_Team", (0.05, 0.30, 1.0), roughness=0.4, metallic=0.2),
                       "Shell": fm("F_H_Shell", (0.22, 0.27, 0.36), roughness=0.40, metallic=0.50),
                       "Dark": fm("F_H_Dark", (0.045, 0.05, 0.06), roughness=0.5, metallic=0.7),
                       "Glow": fm("F_H_Glow", AMBER, emission=1.6)},
        "kit_machine": {"Shell": fm("F_C_Shell", (0.62, 0.67, 0.76), roughness=0.3, metallic=0.35),
                        "Dark": fm("F_C_Dark", (0.02, 0.028, 0.045), metallic=0.6, roughness=0.4),
                        "Glow": fm("F_C_Glow", (0.1, 0.78, 1.0), emission=5.0),
                        "Accent": fm("F_C_Accent", (1.0, 0.03, 0.03), emission=6.0)},
        "kit_cluster": {"Shell": fm("F_W_Shell", (0.85, 0.88, 0.92), roughness=0.18, coat=0.6),
                        "Dark": fm("F_W_Dark", (0.02, 0.028, 0.045), metallic=0.4, roughness=0.4),
                        "Glow": fm("F_W_Glow", (0.1, 0.78, 1.0), emission=5.0),
                        "Accent": fm("F_W_Accent", (1.0, 0.02, 0.02), emission=8.0)},
        "kit_human": {"Shell": fm("F_KH_Shell", (0.18, 0.27, 0.42), roughness=0.4, metallic=0.45),
                      "Dark": fm("F_KH_Dark", (0.045, 0.05, 0.06), roughness=0.45, metallic=0.6),
                      "Glow": fm("F_KH_Glow", (1.0, 0.5, 0.08), emission=5.0),
                      "Accent": fm("F_KH_Accent", (0.95, 0.62, 0.03), roughness=0.5)},
    }[look]


# ---------------------------------------------------------------------------------------------------------
# Mask baking: vertex colour attribute SC2Mask (R = Edge, G = Cavity, B = Ground)
# ---------------------------------------------------------------------------------------------------------
EDGE_ANGLE = (0.35, 0.8)   # rad: full 1-ring normal spread of a chamfer vertex that maps to edge 0 .. 1
AO_RAYS = 20
_hemi = []
for _k in range(AO_RAYS):   # deterministic cosine-weighted Fibonacci hemisphere
    _u = (_k + 0.5) / AO_RAYS
    _r, _phi = math.sqrt(_u), _k * 2.399963
    _hemi.append((_r * math.cos(_phi), _r * math.sin(_phi), math.sqrt(1.0 - _u)))


def compute_masks(mesh, scope):
    sc = SCOPES[scope]
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    bm.normal_update()

    signed = {e.index: (e.calc_face_angle_signed(0.0) if len(e.link_faces) == 2 else 0.0) for e in bm.edges}
    # Chamfer band: a vertex whose faces are ALL narrow strips (inradius under sc["band"]) is on a bevel; plate
    # faces are wide, so the vertices of a plate's flat side stay 0 and the highlight is the bevel strip itself.
    inradius = {f.index: 2.0 * f.calc_area() / max(1e-9, sum(e.calc_length() for e in f.edges)) for f in bm.faces}
    bvh = BVHTree.FromBMesh(bm)
    zmin = min(v.co.z for v in bm.verts)
    height = sc["ground"]
    out = []
    for v in bm.verts:
        n = v.normal.normalized() if v.normal.length > 0 else Vector((0, 0, 1))
        faces = list(v.link_faces)
        narrow = bool(faces) and all(inradius[f.index] < sc["band"] for f in faces)
        # Edge: chamfer vertex whose faces fan out (a bevel turns 40-90 degrees over its strips; a small
        # cylinder or a curved plate stays under 25), convex; concave creases feed the cavity instead.
        lowest = min((n.dot(f.normal) for f in faces), default=1.0)
        mean = sum(signed[e.index] for e in v.link_edges) / max(1, len(v.link_edges))
        spread = 2.0 * math.acos(max(-1.0, min(1.0, lowest)))
        fan = min(1.0, max(0.0, (spread - EDGE_ANGLE[0]) / (EDGE_ANGLE[1] - EDGE_ANGLE[0]))) if narrow else 0.0
        edge = fan * min(1.0, max(0.0, mean * 8.0))
        turn_concave = fan * min(1.0, max(0.0, -mean * 8.0))
        if faces:   # ray frame from the largest adjacent face: the smoothed vertex normal tilts into chamfers
            n = max(faces, key=lambda f: f.calc_area()).normal.copy()
        t = n.cross(Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((1, 0, 0))).normalized()
        b = n.cross(t)
        rot = (v.index * 0.6180339) % 1.0 * 6.2831853
        cr, sr = math.cos(rot), math.sin(rot)
        origin = v.co + n * 0.006
        hits = 0
        for hx, hy, hz in _hemi:
            x, y = hx * cr - hy * sr, hx * sr + hy * cr
            d = t * x + b * y + n * hz
            if bvh.ray_cast(origin, d, sc["ao"])[0] is not None:
                hits += 1
        occlusion = hits / AO_RAYS
        cavity = min(1.0, max(0.0, (occlusion - 0.2) / 0.6) + turn_concave * 0.6)
        ground = max(0.0, 1.0 - (v.co.z - zmin) / height) ** 2
        out.append((edge, cavity, ground))
    bm.free()
    return out


def bake_masks(obj, scope):
    mesh = obj.data
    masks = compute_masks(mesh, scope)
    attr = mesh.color_attributes.get(MASK_ATTR) or mesh.color_attributes.new(MASK_ATTR, "FLOAT_COLOR", "POINT")
    flat = []
    for edge, cavity, ground in masks:
        flat.extend((edge, cavity, ground, 1.0))
    attr.data.foreach_set("color", flat)
    mesh.update()
    return masks


# ---------------------------------------------------------------------------------------------------------
# Preview scene: copies of the generator meshes, flat "before" and textured "after", on an asphalt floor
# ---------------------------------------------------------------------------------------------------------
BLENDS = {"unit": "Art/Units/Units.blend", "bld": "Art/Buildings/Buildings.blend",
          "env": "Art/Environment/Environment.blend"}
HUMAN_KIT = ("Container", "Wreck", "SandbagWall", "BurnBarrel", "GeneratorShack", "CableSpool")
CLUSTER_KIT = ("ClusterPylon",)
ORIGIN = {"unit": Vector((0.0, 0.0, 0.0)), "bld": Vector((0.0, -18.0, 0.0)), "env": Vector((0.0, -52.0, 0.0))}
PREVIEW_COLUMNS = (0.0, 1.8, 3.6, 6.2)        # Build/GenerateUnitMeshes.py
KIT_ROWS = (("Pylon", "CommsMast", "ClusterPylon", "CoolingTower"),
            ("DataHallBay", "DataHallDoor", "DataHallCorner", "DataHallRoof", "Chiller", "Transformer",
             "FenceSegment"),
            ("Container", "Wreck", "SandbagWall", "BurnBarrel", "GeneratorShack", "CableSpool"))
KIT_ROW_Y = (12.0, 1.0, -9.5)                 # Build/GenerateEnvironmentKit.py RTS_ROW_Y
KIT_YAW = {"SandbagWall": 90, "GeneratorShack": -90}
CAMPUS_ASPHALT = (0.045, 0.048, 0.055)        # MI_Asphalt in Build/GenerateCampusZero.py
FIELD_DISTANCE = 130.0                        # Build/GenerateUnitMeshes.py: a unit is about 40 px tall
FIELD_SIZE = (2000, 1125)
FIELD_LAYOUT = {"SM_Human_Frontline": (-10.0, -1.5), "SM_Machine_Frontline": (-6.0, 2.0),
                "SM_Human_Siege": (-2.5, -2.0), "SM_Machine_Ranged": (3.0, 1.5), "SM_Human_Ranged": (7.0, -2.0),
                "SM_Machine_Siege": (11.0, 2.0)}


def scope_of(name, kind):
    return "bld" if kind == "unit" and name.endswith("_HQ") else kind


def faction_of(name, kind):
    if kind == "env":
        short = name[len("SM_Env_"):]
        return "Cluster" if short in CLUSTER_KIT else "Human" if short in HUMAN_KIT else "Machine"
    return "Machine" if "_Machine_" in name else "Human"


def flat_look(name, kind):
    if kind == "env":
        short = name[len("SM_Env_"):]
        return "kit_cluster" if short in CLUSTER_KIT else "kit_human" if short in HUMAN_KIT else "kit_machine"
    return None  # units and buildings keep the generators' object-level PV materials


def layout_units(names):
    out = {}
    for index, faction in enumerate(("Machine", "Human")):
        for column, role in enumerate(("Frontline", "Ranged", "Siege", "HQ")):
            name = "SM_%s_%s" % (faction, role)
            out[name] = (PREVIEW_COLUMNS[column], 1.9 if faction == "Machine" else -1.9,
                         1.0 if role == "HQ" else 0.6, 0.0)
    return out


def layout_kit(objs):
    out = {}
    for row, names in enumerate(KIT_ROWS):
        x = 0.0
        for short in names:
            name = "SM_Env_" + short
            yaw = KIT_YAW.get(short, 0)
            size = objs[name].dimensions
            width = size.y if abs(yaw) == 90 else size.x
            out[name] = (x + width / 2, KIT_ROW_Y[row], 0.0, float(yaw))
            x += width + 1.7
    return out


def make_collection(scene, name):
    coll = bpy.data.collections.new(name)
    scene.collection.children.link(coll)
    return coll


def load_kind(kind):
    """Append the generator's objects (its own mesh copies; the .blend on disk is untouched)."""
    with bpy.data.libraries.load(os.path.join(ROOT, BLENDS[kind]), link=False) as (src, dst):
        dst.objects = list(src.objects)
    return {o.name: o for o in dst.objects}


def build_assets(scene):
    """Returns (after, flat): name -> object, laid out under ORIGIN[kind] and hidden objects excluded."""
    after_coll = make_collection(scene, "SC2 (after)")
    flat_coll = make_collection(scene, "Flat (before)")
    after, flat = {}, {}
    for kind in ("unit", "bld", "env"):
        objs = load_kind(kind)
        layout = layout_units(objs) if kind == "unit" else layout_kit(objs) if kind == "env" else {
            n: (o.location.x, o.location.y, o.location.z, 0.0) for n, o in objs.items()}
        for name, obj in objs.items():
            x, y, z, yaw = layout[name]
            loc = Vector((x, y, z)) + ORIGIN[kind]
            # flat: the generator's own preview look (object-level PV materials; the kit gets its palette)
            look = flat_look(name, kind)
            if look:
                palette = flat_palette(look)
                for slot in obj.material_slots:
                    canonical = slot.material.name.split(".")[0]
                    slot.link = "OBJECT"
                    slot.material = palette[canonical]
            obj.name = "Flat_" + name
            obj.location = loc
            obj.rotation_euler = (0.0, 0.0, math.radians(yaw))
            flat_coll.objects.link(obj)
            obj["kind"] = kind
            flat[name] = obj
            # after: own mesh copy so the baked mask attribute never touches the flat one
            copy = obj.copy()
            copy.name = name
            copy.data = obj.data.copy()
            copy.data.name = name + "_SC2"
            for slot in copy.material_slots:
                slot.link = "DATA"
            scope = scope_of(name, kind)
            bake_masks(copy, scope)
            apply(copy, faction_of(name, kind), scope, "Construction" if "Construction" in name else "")
            after_coll.objects.link(copy)
            copy["kind"] = kind
            after[name] = copy
    flat_coll.hide_viewport = True
    return after, flat


def asphalt_material(textured):
    name = "M_SC2_Asphalt" if textured else "F_Asphalt"
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    if not textured:
        return flat_material(name, CAMPUS_ASPHALT, roughness=0.9)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    tree = mat.node_tree
    tree.nodes.clear()
    g = Graph(tree)
    coords = g.node("ShaderNodeTexCoord")

    def sampler(role, tile):
        mapping = g.node("ShaderNodeMapping", [("Vector", coords.outputs["Object"]), ("Scale", (1.0 / tile,) * 3)])
        return g.node("ShaderNodeTexImage", [("Vector", mapping.outputs["Vector"])], projection="BOX",
                      projection_blend=0.2, image=_image(role))

    color, rough = sampler("AsphaltColor", 3.0), sampler("AsphaltRough", 3.0)
    # keep the mean albedo equal to MI_Asphalt so contrast against units is unchanged
    img = _image("AsphaltColor")
    px = list(img.pixels)[0::4][::97]
    gain = 1.4 * CAMPUS_ASPHALT[2] / max(1e-4, sum(px) / len(px))
    tinted = g.cmix(1.0, color.outputs["Color"], (gain, gain, gain, 1.0), "MULTIPLY")
    bsdf = g.node("ShaderNodeBsdfPrincipled", [("Base Color", tinted), ("Roughness", g.m("MULTIPLY_ADD", g.node(
        "ShaderNodeRGBToBW", [("Color", rough.outputs["Color"])]).outputs["Val"], 0.25, 0.7))])
    g.node("ShaderNodeOutputMaterial", [("Surface", bsdf.outputs["BSDF"])])
    return mat


def make_floor(scene, coll):
    mesh = bpy.data.meshes.new("Floor")
    half = 300.0
    mesh.from_pydata([(-half, -half, 0), (half, -half, 0), (half, half, 0), (-half, half, 0)], [], [(0, 1, 2, 3)])
    mesh.materials.append(asphalt_material(True))
    obj = bpy.data.objects.new("Floor", mesh)
    obj.location = (30.0, -30.0, -0.005)
    coll.objects.link(obj)
    return obj


def sun(scene, coll, name, direction, energy, color, angle=6.0):
    data = bpy.data.lights.new(name, "SUN")
    data.energy, data.color, data.angle = energy, color, math.radians(angle)
    obj = bpy.data.objects.new(name, data)
    obj.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
    coll.objects.link(obj)
    return obj


def set_sky(scene, horizon, zenith, ground, strength):
    world = scene.world or bpy.data.worlds.new("SC2_World")
    scene.world = world
    world.use_nodes = True
    tree = world.node_tree
    tree.nodes.clear()
    g = Graph(tree)
    coords = g.node("ShaderNodeTexCoord")
    z = g.node("ShaderNodeSeparateXYZ", [("Vector", coords.outputs["Generated"])]).outputs["Z"]
    ramp = g.node("ShaderNodeValToRGB", [("Fac", z)])
    elements = ramp.color_ramp.elements
    elements[0].position, elements[0].color = 0.46, (*ground, 1.0)
    elements[1].position, elements[1].color = 0.62, (*zenith, 1.0)
    mid = elements.new(0.50)
    mid.color = (*horizon, 1.0)
    bg = g.node("ShaderNodeBackground", [("Color", ramp.outputs["Color"]), ("Strength", strength)])
    g.node("ShaderNodeOutputWorld", [("Surface", bg.outputs["Background"])])


def aim(cam, target, distance, azimuth=25.0, pitch=50.0, lens=55.0):
    cam.data.type = "PERSP"
    cam.data.lens = lens
    cam.data.clip_end = 2000.0
    offset = Vector((0.0, -math.cos(math.radians(pitch)), math.sin(math.radians(pitch)))) * distance
    offset.rotate(Euler((0, 0, math.radians(azimuth))))
    cam.location = Vector(target) + offset
    cam.rotation_euler = (math.radians(90 - pitch), 0, math.radians(azimuth))


# name: (origin kind, local target, distance, azimuth, pitch, lens, size)
CAMERAS = {
    "Cam_Units": ("unit", (3.9, -0.2, 0.4), 24.0, 25.0, 50.0, 55.0, (2000, 1200)),
    "Cam_Buildings": ("bld", (10.6, -0.6, 0.6), 45.0, 8.0, 50.0, 55.0, (2400, 1300)),
    "Cam_Kit": ("env", (14.0, 1.0, 3.5), 76.0, 12.0, 50.0, 40.0, (2600, 1500)),
}


def make_cameras(scene, coll):
    cams = {}
    for name, (kind, target, distance, azimuth, pitch, lens, size) in CAMERAS.items():
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        coll.objects.link(cam)
        aim(cam, Vector(target) + ORIGIN[kind], distance, azimuth, pitch, lens)
        cam["size"] = size
        cams[name] = cam
    return cams


def setup_bloom(scene):
    """Compositor bloom so emissive Glow / Team strips read as light (Unreal: the project's Bloom post process)."""
    tree = bpy.data.node_groups.new("SC2_Comp", "CompositorNodeTree")
    scene.compositing_node_group = tree
    tree.interface.new_socket(name="Image", in_out="OUTPUT", socket_type="NodeSocketColor")
    layers = tree.nodes.new("CompositorNodeRLayers")
    glare = tree.nodes.new("CompositorNodeGlare")
    out = tree.nodes.new("NodeGroupOutput")
    glare.inputs["Type"].default_value = "Bloom"
    glare.inputs["Threshold"].default_value = 0.7
    glare.inputs["Strength"].default_value = 0.5
    glare.inputs["Size"].default_value = 0.5
    tree.links.new(layers.outputs["Image"], glare.inputs["Image"])
    tree.links.new(glare.outputs["Image"], out.inputs["Image"])


def setup_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "Standard"
    setup_bloom(scene)
    return scene


def build_scene():
    scene = setup_scene()
    build_library("unit")
    after, flat = build_assets(scene)
    stage = make_collection(scene, "Stage")
    rig = make_collection(scene, "Rig")
    make_floor(scene, rig)
    lights = {
        "PV_Key": sun(scene, rig, "PV_Key", (0.5, 0.6, -0.8), 2.6, (1.0, 0.97, 0.92)),
        "PV_Fill": sun(scene, rig, "PV_Fill", (-0.6, 0.3, -0.5), 0.9, (0.6, 0.75, 1.0)),
        "PV_Front": sun(scene, rig, "PV_Front", (0.1, -0.7, -0.5), 0.6, (0.8, 0.85, 1.0)),
    }
    set_sky(scene, (0.36, 0.40, 0.47), (0.16, 0.21, 0.32), (0.05, 0.05, 0.06), 1.0)
    cams = make_cameras(scene, rig)
    scene.camera = cams["Cam_Units"]
    scene.render.resolution_x, scene.render.resolution_y = CAMERAS["Cam_Units"][-1]
    return scene, after, flat, stage, rig, lights, cams


SAVE_BLEND = True   # False when --out redirects renders (experiments must not touch the watched .blend)


def save():
    if not SAVE_BLEND:
        return
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH, compress=False)
    print("SAVED", BLEND_PATH)
    sys.stdout.flush()


RENDER_SCALE = 1.0   # --scale 0.5 halves resolution and sample count for quick tuning


def render(scene, cam, path, size=None, samples=64):
    scene.camera = cam
    w, h = size or tuple(cam["size"])
    scene.render.resolution_x, scene.render.resolution_y = int(w * RENDER_SCALE), int(h * RENDER_SCALE)
    scene.cycles.samples = max(8, int(samples * RENDER_SCALE))
    scene.render.filepath = path
    bpy.context.view_layer.update()
    bpy.ops.render.render(write_still=True)


def show(scene, after=None, flat=None, stage=None):
    """Toggle which sets render; the Flat collection stays hidden in the viewport regardless."""
    for coll, flag in (("SC2 (after)", after), ("Flat (before)", flat), ("Stage", stage)):
        if flag is not None:
            bpy.data.collections[coll].hide_render = not flag


def show_kind(kind):
    """Only objects of this kind (unit / bld / env) render; None shows every kind."""
    for coll in (bpy.data.collections["SC2 (after)"], bpy.data.collections["Flat (before)"]):
        for obj in coll.objects:
            obj.hide_render = kind is not None and obj.get("kind") != kind


def caption(src, label, dst):
    import subprocess
    subprocess.run(["magick", src, "-background", "#101216", "-gravity", "North", "-splice", "0x64", "-fill", "white",
                    "-pointsize", "38", "-annotate", "+0+10", label, dst], check=True)


def before_after(scene, cam, out, kind, samples=64):
    import subprocess
    tmp_flat, tmp_after = out + ".flat.png", out + ".after.png"
    show_kind(kind)
    show(scene, after=False, flat=True, stage=False)
    render(scene, cam, tmp_flat, samples=samples)
    show(scene, after=True, flat=False, stage=False)
    render(scene, cam, tmp_after, samples=samples)
    show_kind(None)
    caption(tmp_flat, "BEFORE: flat colours", tmp_flat)
    caption(tmp_after, "AFTER: M_Shared master material (baked masks, CC0 textures)", tmp_after)
    subprocess.run(["magick", tmp_flat, tmp_after, "-append", out], check=True)
    os.remove(tmp_flat)
    os.remove(tmp_after)


def stage_objects(scene, sources, items):
    """items: name -> (x, y, z, yaw). Linked-data copies in the Stage collection (positions are world)."""
    coll = bpy.data.collections["Stage"]
    for name, (x, y, z, yaw) in items.items():
        obj = sources[name].copy()
        obj.location = (x, y, z)
        obj.rotation_euler = (0.0, 0.0, math.radians(yaw))
        coll.objects.link(obj)


def clear_stage():
    coll = bpy.data.collections["Stage"]
    for obj in list(coll.objects):
        bpy.data.objects.remove(obj)


def dusk(scene, lights, on):
    """Campus Zero lighting of Build/GenerateUnitMeshes.py: low warm key, cool fill and a dark sky."""
    if on:
        for light in lights.values():
            light.hide_render = True
        rig = bpy.data.collections["Rig"]
        lights["dusk_key"] = sun(scene, rig, "PV_DuskKey", (-0.7, -0.4, -0.3), 4.0, (1.0, 0.66, 0.42))
        lights["dusk_fill"] = sun(scene, rig, "PV_DuskFill", (0.6, 0.3, -0.6), 1.8, (0.5, 0.6, 1.0))
        world = scene.world
        world["_sky_backup"] = 1
        set_sky(scene, (0.05, 0.06, 0.10), (0.05, 0.06, 0.10), (0.05, 0.06, 0.10), 3.5)
    else:
        for key in ("dusk_key", "dusk_fill"):
            if key in lights:
                bpy.data.objects.remove(lights.pop(key))
        for light in lights.values():
            light.hide_render = False
        set_sky(scene, (0.36, 0.40, 0.47), (0.16, 0.21, 0.32), (0.05, 0.05, 0.06), 1.0)


def field_stats(with_path, floor_path):
    """Object pixels = where the image differs from the floor-only render. Reports how much of the most vivid
    (saturation x value) 3 % of object pixels are Team-hued (blue 0.55-0.72 or red <0.04 / >0.96)."""
    import numpy as np

    def load(path):
        img = bpy.data.images.load(path)
        img.colorspace_settings.name = "Non-Color"
        arr = np.array(img.pixels[:], dtype=np.float32).reshape(-1, 4)[:, :3]
        bpy.data.images.remove(img)
        return arr

    a, b = load(with_path), load(floor_path)
    mask = np.abs(a - b).max(axis=1) > 0.03
    obj = a[mask]
    mx, mn = obj.max(axis=1), obj.min(axis=1)
    sat = (mx - mn) / np.maximum(mx, 1e-4)
    vivid = sat * mx
    r, g, bl = obj[:, 0], obj[:, 1], obj[:, 2]
    delta = np.maximum(mx - mn, 1e-4)
    hue = np.where(mx == r, ((g - bl) / delta) % 6, np.where(mx == g, (bl - r) / delta + 2, (r - g) / delta + 4)) / 6
    top = vivid >= np.quantile(vivid, 0.97)
    team = ((hue > 0.55) & (hue < 0.72)) | (hue < 0.04) | (hue > 0.96)
    amber = (hue >= 0.04) & (hue < 0.16)
    cyan = (hue >= 0.44) & (hue <= 0.55)
    return dict(object_px=int(mask.sum()), team_share_of_vivid=float((team & top).sum() / top.sum()),
                amber_share=float((amber & top).sum() / top.sum()), cyan_share=float((cyan & top).sum() / top.sum()),
                mean_vivid_team=float(vivid[team & (sat > 0.35)].mean()) if (team & (sat > 0.35)).any() else 0.0,
                mean_vivid_other=float(vivid[~team & (sat > 0.35)].mean()) if (~team & (sat > 0.35)).any() else 0.0)


FIELD_EXTRAS = {"SM_Human_Barracks_Ranged": (-15.0, 9.0, 0.65, 0.0), "SM_Machine_Barracks_Frontline": (15.0, 9.5, 0.65, 0.0),
                "SM_Human_Workshop": (-19.0, -9.0, 0.65, 0.0), "SM_Machine_Workshop": (19.0, -9.0, 0.65, 0.0),
                "SM_Env_Container": (-3.0, -11.0, 0.0, 90.0), "SM_Env_Transformer": (4.0, 11.0, 0.0, 0.0)}


def render_field(scene, after, flat, lights, floor, cam, out_dir):
    items = {name: (x, y, 0.6, 0.0) for name, (x, y) in FIELD_LAYOUT.items()}
    items.update(FIELD_EXTRAS)
    dusk(scene, lights, True)
    aim(cam, (0.0, 0.0, 0.3), FIELD_DISTANCE, 25.0, 50.0, 85.0)
    stats = {}
    for textured, name in ((True, "FieldView.png"), (False, "FieldView-Flat.png")):
        floor.data.materials[0] = asphalt_material(textured)
        stage_objects(scene, after if textured else flat, items)
        show(scene, after=False, flat=False, stage=True)
        render(scene, cam, os.path.join(out_dir, name), FIELD_SIZE, samples=96)
        show(scene, stage=False)
        render(scene, cam, os.path.join(out_dir, "_floor.png"), FIELD_SIZE, samples=24)
        stats["after" if textured else "flat"] = field_stats(os.path.join(out_dir, name),
                                                             os.path.join(out_dir, "_floor.png"))
        os.remove(os.path.join(out_dir, "_floor.png"))
        show(scene, stage=True)
        clear_stage()
    floor.data.materials[0] = asphalt_material(True)
    dusk(scene, lights, False)
    show(scene, after=True, flat=False, stage=False)
    return stats


CLOSE = {"Human": ("SM_Human_Frontline", "SM_Human_Ranged", "SM_Human_Siege", "SM_Human_Barracks_Frontline"),
         "Machine": ("SM_Machine_Frontline", "SM_Machine_Ranged", "SM_Machine_Siege",
                     "SM_Machine_Barracks_Frontline")}


def render_close(scene, after, cam, out_dir):
    for faction, (a, b, c, building) in CLOSE.items():
        items = {a: (-1.7, -1.4, 0.6, 0.0), b: (0.0, -1.8, 0.6, 0.0), c: (1.8, -1.3, 0.6, 0.0),
                 building: (0.3, 3.0, 0.65, 0.0)}
        stage_objects(scene, after, items)
        show(scene, after=False, flat=False, stage=True)
        aim(cam, (0.2, 0.1, 0.8), 11.5, 18.0, 32.0, 50.0)
        render(scene, cam, os.path.join(out_dir, "Close-%s.png" % faction), (2000, 1200), samples=128)
        clear_stage()
    show(scene, after=True, flat=False, stage=False)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    global RENDER_SCALE, MASK_MODE, SAVE_BLEND
    RENDER_SCALE = float(argv[argv.index("--scale") + 1]) if "--scale" in argv else 1.0
    MASK_MODE = argv[argv.index("--masks") + 1] if "--masks" in argv else MASK_MODE
    out_dir = argv[argv.index("--out") + 1] if "--out" in argv else OUT
    SAVE_BLEND = out_dir == OUT
    os.makedirs(out_dir, exist_ok=True)
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else None

    def want(name):
        return only is None or name in only

    scene, after, flat, stage, rig, lights, cams = build_scene()
    save()
    if "--save-only" in argv:
        return
    aux = bpy.data.objects.new("Cam_Aux", bpy.data.cameras.new("Cam_Aux"))
    rig.objects.link(aux)
    floor = bpy.data.objects["Floor"]
    if want("units"):
        before_after(scene, cams["Cam_Units"], os.path.join(out_dir, "Before-After-Units.png"), "unit")
        save()
    if want("buildings"):
        before_after(scene, cams["Cam_Buildings"], os.path.join(out_dir, "Before-After-Buildings.png"), "bld")
        save()
    if want("kit"):
        before_after(scene, cams["Cam_Kit"], os.path.join(out_dir, "Before-After-Kit.png"), "env", samples=48)
        save()
    if want("close"):
        render_close(scene, after, aux, out_dir)
        save()
    if want("field"):
        stats = render_field(scene, after, flat, lights, floor, aux, out_dir)
        print("FIELD_STATS", stats)
        save()
    bpy.data.objects.remove(aux)
    scene.camera = cams["Cam_Units"]
    save()
    print("MASTER_MATERIALS_DONE")


if __name__ == "__main__":
    main()
