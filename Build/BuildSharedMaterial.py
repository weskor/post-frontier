"""Build M_Shared, the SC2-style master material, its triplanar/wear material functions and every MI_SC2_* instance.

Implements Art/Materials/UNREAL.md (prototype: Build/MasterMaterials.py). Unreal editor Python, rerunnable, run from
the project root with the editor closed and no other Unreal process from this repo running:

  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/BuildSharedMaterial.py" \
    -unattended -RenderOffscreen -nosplash

Offscreen rendering (not -nullrhi) is required: only a real RHI compiles the shaders, so a graph or HLSL error shows as
"Failed to compile Material" in the log instead of at the first render. Require M_SHARED_COMPILED,
SC2_TEXTURES_IMPORTED and SC2_INSTANCES_BUILT in the log (each is only printed after its
own read-back checks pass), and no RuntimeError.

What it makes (all under /Game/Art, replaced or updated in place on every run)
    Textures/T_<Asset>_<Map>        the seven CC0 mask textures of UNREAL.md section 5: linear, Masks compression,
                                    at most 1024 px (the meshes have no UVs, so they are only ever sampled triplanar)
    Materials/MF_Triplanar_Local    Texture Object + TileSize (m) + Sharpness -> one channel, object-space triplanar
    Materials/MF_SC2_Wear           edge chips, bevel highlight, grime and panel seams (UNREAL.md section 2.2)
    Materials/M_Shared              the master; graph is rebuilt from this script on every run
    Materials/MI_SC2_<Faction>_<Slot>_<Scope>   one MaterialInstanceConstant per faction, slot and scope
                                    (Unit, Bld, Env) with the section 8 values; Construction_<Slot>_Bld for scaffolds

Parameters, static switches and the graph follow UNREAL.md section 2. Two implementation notes:
* The triplanar sample, the wear maths, the hazard stripes and the panel bump are `Custom` (HLSL) nodes wrapped in the
  functions above (or in M_Shared for the last two). The maths is line for line the section 2.2 pseudo code.
* The panel bump is a screen-space surface-gradient perturbation (ddx/ddy of the triplanar height) of the *vertex*
  normal, written to the Normal pin in WORLD space (`tangent_space_normal` off): the meshes have no UVs and so no
  tangents, and PixelNormalWS may not feed the Normal pin.

USE_BAKED_MASKS: the meshes carry the baked SC2Mask vertex colours (R Edge, G Cavity, B Ground) only after
ImportUnitMeshes / ImportBuildingMeshes / ImportEnvironmentKit imported them with Vertex Color Import Option Replace,
and a mesh without vertex colours reads white (everything "worn and in a crevice"). It is True because Build/VerifyMasks.py
read the masks back from all 40 imported meshes (MASKS_VERIFIED 40, Ground within 0.002 of the formula, so the import
is linear). Set it False to rebuild the instances for meshes imported without masks; it sets the `UseBakedMasks`
static switch on every instance.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ArtMaterials as art

require = art.require
assets = art.assets
library = art.library
tools = art.tools

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURE_DIR = os.path.join(ROOT, "Art", "Textures")
TEXTURE_FOLDER = "/Game/Art/Textures"
USE_BAKED_MASKS = True

# ---------------------------------------------------------------- textures (UNREAL.md section 5)
# role: (folder, asset, map). Same roles as TEXTURES in Build/MasterMaterials.py.
TEXTURES = {
    "PlatesHuman": ("Panel", "MetalPlates002", "Height"),
    "PlatesMachine": ("SciFiFloor", "Tiles108", "Height"),
    "PlatesDark": ("Panel", "MetalPlates006", "Height"),
    "Chip": ("PaintedMetal", "PaintedMetal014", "Roughness"),
    "GrimeHuman": ("Metal", "Metal046A", "Height"),
    "GrimeMachine": ("Concrete", "Concrete016", "Height"),
    "GrimeDark": ("Metal", "Metal010", "Roughness"),
}


def texture_name(role):
    _folder, asset, kind = TEXTURES[role]
    return "T_%s_%s" % (asset, kind)


def import_textures():
    tasks = []
    for role in TEXTURES:
        folder, asset, kind = TEXTURES[role]
        source = os.path.join(TEXTURE_DIR, folder, asset, "%s_%s.jpg" % (asset, kind))
        require(os.path.isfile(source), "Missing " + source + " (python3 Build/FetchTextures.py)")
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source)
        task.set_editor_property("destination_path", TEXTURE_FOLDER)
        task.set_editor_property("destination_name", texture_name(role))
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append((role, task))
    tools.import_asset_tasks([task for _role, task in tasks])
    loaded = {}
    for role, _task in tasks:
        path = "%s/%s" % (TEXTURE_FOLDER, texture_name(role))
        texture = require(assets.load_asset(path), "Texture import failed for " + path)
        require(isinstance(texture, unreal.Texture2D), path + " is not a Texture2D")
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("max_texture_size", 1024)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        require(assets.save_loaded_asset(texture), "Could not save " + path)
        require(not texture.get_editor_property("srgb")
                and texture.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_MASKS
                and texture.get_editor_property("max_texture_size") == 1024,
                "Texture settings did not stick on " + path)
        loaded[role] = texture
    unreal.log("SC2_TEXTURES_IMPORTED %d" % len(loaded))
    return loaded


# ---------------------------------------------------------------- graph helpers
class Graph:
    """Small builder over MaterialEditingLibrary for a Material or a MaterialFunction."""

    def __init__(self, owner, function=False, preview_texture=None):
        self.owner = owner
        self.function = function
        self.preview_texture = preview_texture

    def function_input(self, name, kind, priority, x, y, default=0.0):
        """FunctionInput with a wired preview (the function's own preview compile needs one), used as the default."""
        node = self.node("FunctionInput", x, y, input_name=name, input_type=getattr(unreal.FunctionInputType, kind),
                         sort_priority=priority, use_preview_value_as_default=True)
        if kind == "FUNCTION_INPUT_TEXTURE2D":
            preview = self.node("TextureObject", x - 250, y, texture=self.preview_texture,
                                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        elif kind == "FUNCTION_INPUT_VECTOR3":
            preview = self.node("Constant3Vector", x - 250, y, constant=unreal.LinearColor(default, default, default, 1.0))
        else:
            preview = self.node("Constant", x - 250, y, r=default)
        self.link(preview, node, "Preview")
        return node

    def node(self, kind, x, y, **props):
        cls = getattr(unreal, "MaterialExpression" + kind)
        if self.function:
            expr = library.create_material_expression_in_function(self.owner, cls, x, y)
        else:
            expr = library.create_material_expression(self.owner, cls, x, y)
        require(expr, "Could not create " + kind)
        for key, value in props.items():
            expr.set_editor_property(key, value)
        return expr

    def link(self, source, target, pin, out=""):
        """Connect `source` (an expression or an (expression, output name) pair) to input `pin` of `target`."""
        if isinstance(source, tuple):
            source, out = source
        require(library.connect_material_expressions(source, out, target, pin),
                "Could not connect %s.%s -> %s.%s" % (source.get_name(), out or "out", target.get_name(), pin))

    def const(self, value, x, y):
        return self.node("Constant", x, y, r=value)

    def scalar(self, name, default, x, y, group=""):
        return self.node("ScalarParameter", x, y, parameter_name=name, default_value=default, group=group)

    def vector(self, name, default, x, y, group=""):
        return self.node("VectorParameter", x, y, parameter_name=name,
                         default_value=unreal.LinearColor(*default, 1.0), group=group)

    def switch(self, name, default, on, off, x, y, group=""):
        node = self.node("StaticSwitchParameter", x, y, parameter_name=name, default_value=default, group=group)
        self.link(on, node, "True")
        self.link(off, node, "False")
        return node

    def custom(self, description, code, inputs, output_type, x, y, extra_outputs=()):
        """Custom HLSL node; `inputs` maps input name -> source expression or (expression, output name)."""
        node = self.node("Custom", x, y, description=description, code=code, output_type=output_type)
        entries = []
        for name in inputs:
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", name)
            entries.append(entry)
        node.set_editor_property("inputs", entries)
        outputs = []
        for name, kind in extra_outputs:
            entry = unreal.CustomOutput()
            entry.set_editor_property("output_name", name)
            entry.set_editor_property("output_type", kind)
            outputs.append(entry)
        node.set_editor_property("additional_outputs", outputs)
        for name, source in inputs.items():
            self.link(source, node, name)
        return node


def _out(expr, wanted):
    """Real output pin name of `expr` matching `wanted` (case-insensitive), so 'R' works on any version."""
    if not wanted:
        return ""
    names = [str(n) for n in library.get_material_expression_output_names(expr)]
    for name in names:
        if name.lower() == wanted.lower():
            return name
    raise RuntimeError("%s has no output %r (has %s)" % (expr.get_name(), wanted, names))


F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3



# ---------------------------------------------------------------- MF_Triplanar_Local
TRIPLANAR_HLSL = """
float3 w = pow(abs(normalize(N)), max(Sharp, 1.0));
w /= max(w.x + w.y + w.z, 1e-4);
float v = Texture2DSample(Tex, TexSampler, P.yz).r * w.x;
v += Texture2DSample(Tex, TexSampler, P.xz).r * w.y;
v += Texture2DSample(Tex, TexSampler, P.xy).r * w.z;
return v;
"""


def build_triplanar(function, preview_texture):
    g = Graph(function, True, preview_texture)
    tex = g.function_input("Tex", "FUNCTION_INPUT_TEXTURE2D", 0, -900, -200)
    tile = g.function_input("TileSize", "FUNCTION_INPUT_SCALAR", 1, -900, 0, 1.0)
    sharp = g.function_input("Sharpness", "FUNCTION_INPUT_SCALAR", 2, -900, 200, 4.0)
    # Object-space position in metres -> tiles; object-space vertex normal for the blend weights.
    position = g.node("WorldPosition", -900, 400)   # default: absolute world position, all shader offsets included
    local = g.node("TransformPosition", -650, 400,
                   transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                   transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    g.link(position, local, "")
    tile_cm = g.node("Multiply", -650, 0)
    g.link(tile, tile_cm, "A")
    tile_cm.set_editor_property("const_b", 100.0)
    scaled = g.node("Divide", -400, 300)
    g.link(local, scaled, "A")
    g.link(tile_cm, scaled, "B")
    normal_ws = g.node("VertexNormalWS", -900, 600)
    normal_local = g.node("Transform", -650, 600,
                          transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                          transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    g.link(normal_ws, normal_local, "")
    sample = g.custom("Triplanar", TRIPLANAR_HLSL, {"Tex": tex, "P": scaled, "N": normal_local, "Sharp": sharp}, F1, -150, 200)
    result = g.node("FunctionOutput", 150, 200, output_name="Result")
    g.link(sample, result, "")
    library.update_material_function(function)


# ---------------------------------------------------------------- MF_SC2_Wear
WEAR_HLSL = """
float Edge = Masks.x, Cavity = Masks.y, Ground = Masks.z;
float chipSignal = Edge * (0.55 + 0.9 * WearMask);
float t = 0.9 - 0.6 * EdgeWear;
float chip = smoothstep(t, t + 0.12, chipSignal) * saturate(EdgeWear * 10.0);
float highlight = smoothstep(0.05, 0.6, Edge) * EdgeHighlight;
float seam = 1.0 - smoothstep(0.05, 0.45, PanelHeight);
float grime = saturate(Cavity * GrimeAmount * (0.35 + 1.1 * GrimeMask)
                     + Ground * GroundGrime * GrimeMask * 1.3
                     + seam * GrimeAmount * 0.6);
float3 lifted = lerp(Color, float3(1, 1, 1), 0.55);
float3 c = lerp(Color, 1.0 - (1.0 - Color) * (1.0 - lifted), highlight * 0.95);
c = lerp(c, BareMetal, chip);
c = lerp(c, c * 0.15, seam * PanelLine);
c = lerp(c, c * GrimeColor, grime);
OutMetallic = lerp(Metallic, 1.0, chip) * (1.0 - 0.7 * grime);
OutRoughness = lerp(lerp(Roughness, 0.32, chip) - highlight * 0.15, 0.9, grime);
return c;
"""
WEAR_INPUTS = (("Color", "FUNCTION_INPUT_VECTOR3"), ("Metallic", "FUNCTION_INPUT_SCALAR"),
               ("Roughness", "FUNCTION_INPUT_SCALAR"), ("Masks", "FUNCTION_INPUT_VECTOR3"),
               ("WearMask", "FUNCTION_INPUT_SCALAR"), ("GrimeMask", "FUNCTION_INPUT_SCALAR"),
               ("PanelHeight", "FUNCTION_INPUT_SCALAR"), ("EdgeWear", "FUNCTION_INPUT_SCALAR"),
               ("EdgeHighlight", "FUNCTION_INPUT_SCALAR"), ("BareMetal", "FUNCTION_INPUT_VECTOR3"),
               ("GrimeColor", "FUNCTION_INPUT_VECTOR3"), ("GrimeAmount", "FUNCTION_INPUT_SCALAR"),
               ("GroundGrime", "FUNCTION_INPUT_SCALAR"), ("PanelLine", "FUNCTION_INPUT_SCALAR"))


def build_wear(function):
    g = Graph(function, True)
    sources = {}
    for index, (name, kind) in enumerate(WEAR_INPUTS):
        sources[name] = g.function_input(name, kind, index, -600, index * 140, 0.5)
    wear = g.custom("SC2 wear", WEAR_HLSL, sources, F3, 0, 0,
                    extra_outputs=(("OutMetallic", F1), ("OutRoughness", F1)))
    for pin, out in (("Color", ""), ("Metallic", "OutMetallic"), ("Roughness", "OutRoughness")):
        node = g.node("FunctionOutput", 400, {"Color": 0, "Metallic": 200, "Roughness": 400}[pin], output_name=pin)
        g.link((wear, _out(wear, out)), node, "")
    library.update_material_function(function)


# ---------------------------------------------------------------- M_Shared
HAZARD_HLSL = """
float s = step(0.5, frac((P.x + P.y + P.z) * 0.01 * 11.0));   // object-space cm -> the prototype's metres
return lerp(Base, Dark, s);
"""
BUMP_HLSL = """
// Surface-gradient bump (Mikkelsen): height in cm (H * Depth), vertex normal in, world-space normal out.
float3 dpdx = ddx(P), dpdy = ddy(P);
float3 nrm = normalize(N);
float3 r1 = cross(dpdy, nrm), r2 = cross(nrm, dpdx);
float det = dot(dpdx, r1);
float h = H * Depth;
float3 grad = sign(det) * (ddx(h) * r1 + ddy(h) * r2);
float3 bumped = normalize(abs(det) * nrm - grad);
return normalize(lerp(nrm, bumped, saturate(Strength)));
"""

DEFAULT_TEXTURES = ("PlatesHuman", "Chip", "GrimeHuman")


def build_shared(material, textures, triplanar, wear):
    g = Graph(material)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("tangent_space_normal", False)

    # Parameters (UNREAL.md section 2.1). Groups only sort the instance editor.
    team = g.vector("TeamColor", (0.04, 0.5, 1.0), -2400, -1200, "Paint")
    base = g.vector("BaseColor", (0.2, 0.2, 0.2), -2400, -1000, "Paint")
    metallic = g.scalar("Metallic", 0.0, -2400, -800, "Paint")
    roughness = g.scalar("Roughness", 0.5, -2400, -700, "Paint")
    clear_coat = g.scalar("ClearCoat", 0.0, -2400, -600, "Paint")
    pearl_tint = g.vector("PearlTint", (0.8, 0.85, 1.0), -2400, -500, "Paint")
    pearl_amount = g.scalar("PearlAmount", 0.0, -2400, -350, "Paint")
    bare = g.vector("BareMetalColor", (0.6, 0.62, 0.66), -2400, -250, "Edge")
    edge_wear = g.scalar("EdgeWear", 0.0, -2400, -100, "Edge")
    edge_highlight = g.scalar("EdgeHighlight", 0.0, -2400, 0, "Edge")
    grime_color = g.vector("GrimeColor", (0.36, 0.32, 0.28), -2400, 100, "Grime")
    grime_amount = g.scalar("GrimeAmount", 0.0, -2400, 250, "Grime")
    ground_grime = g.scalar("GroundGrime", 0.0, -2400, 350, "Grime")
    panel_line = g.scalar("PanelLine", 0.0, -2400, 450, "Panels")
    panel_strength = g.scalar("PanelStrength", 0.0, -2400, 550, "Panels")
    hazard_dark = g.vector("HazardDark", (0.02, 0.02, 0.02), -2400, 700, "Extras")
    emissive_color = g.vector("EmissiveColor", (0.0, 0.0, 0.0), -2400, 850, "Extras")
    emissive_intensity = g.scalar("EmissiveIntensity", 0.0, -2400, 1000, "Extras")
    self_lit = g.scalar("SelfLit", 0.0, -2400, 1100, "Extras")
    tile = g.vector("TileSize", (0.9, 0.7, 1.3), -2400, 1300, "Textures")
    tex = {}
    for index, (name, role) in enumerate(zip(("TexPanel", "TexWear", "TexGrime"), DEFAULT_TEXTURES)):
        tex[name] = g.node("TextureObjectParameter", -2400, 1500 + index * 150, parameter_name=name,
                           texture=textures[role], sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                           group="Textures")

    # Triplanar samples: panel height, wear mask, grime mask.
    samples = []
    for index, (name, channel) in enumerate((("TexPanel", "R"), ("TexWear", "G"), ("TexGrime", "B"))):
        call = g.node("MaterialFunctionCall", -1700, 1300 + index * 260)
        call.set_material_function(triplanar)
        g.link(tex[name], call, "Tex")
        g.link(tile, call, "TileSize", _out(tile, channel))
        samples.append(call)
    call_sharp = g.const(4.0, -1900, 1300)
    for call in samples:
        g.link(call_sharp, call, "Sharpness")
    panel_height, wear_mask, grime_mask = (_result(call) for call in samples)

    # Masks: vertex colour R Edge, G Cavity, B Ground, gated by UseBakedMasks (unimported meshes read white).
    vertex_color = g.node("VertexColor", -2000, 200)
    zero = g.node("Constant3Vector", -2000, 350, constant=unreal.LinearColor(0, 0, 0, 1))
    masks = g.switch("UseBakedMasks", False, vertex_color, zero, -1700, 250, "Edge")

    # Paint colour: Lerp(BaseColor, TeamColor, UseTeam), then hazard stripes in object space.
    colour = g.switch("UseTeam", False, team, base, -1700, -1100, "Paint")
    position = g.node("WorldPosition", -2000, 550)
    local = g.node("TransformPosition", -1800, 550,
                   transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                   transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    g.link(position, local, "")
    striped = g.custom("Hazard stripes", HAZARD_HLSL, {"Base": colour, "Dark": hazard_dark, "P": local}, F3, -1400, -1000)
    colour = g.switch("HazardStripes", False, striped, colour, -1200, -1000, "Extras")

    # MF_SC2_Wear
    wear_call = g.node("MaterialFunctionCall", -900, -600)
    wear_call.set_material_function(wear)
    for pin, source in (("Color", colour), ("Metallic", metallic), ("Roughness", roughness), ("Masks", masks),
                        ("WearMask", wear_mask), ("GrimeMask", grime_mask), ("PanelHeight", panel_height),
                        ("EdgeWear", edge_wear), ("EdgeHighlight", edge_highlight), ("BareMetal", bare),
                        ("GrimeColor", grime_color), ("GrimeAmount", grime_amount), ("GroundGrime", ground_grime),
                        ("PanelLine", panel_line)):
        g.link(source, wear_call, pin)
    worn_color = (wear_call, _out(wear_call, "Color"))
    worn_metallic = (wear_call, _out(wear_call, "Metallic"))
    worn_roughness = (wear_call, _out(wear_call, "Roughness"))

    # Pearl grazing tint: Lerp(colour, PearlTint, Fresnel(3) * PearlAmount).
    fresnel3 = g.node("Fresnel", -500, -300, exponent=3.0)
    pearl_mask = g.node("Multiply", -300, -300)
    g.link(fresnel3, pearl_mask, "A")
    g.link(pearl_amount, pearl_mask, "B")
    pearl = g.node("LinearInterpolate", -100, -500)
    g.link(worn_color, pearl, "A")
    g.link(pearl_tint, pearl, "B")
    g.link(pearl_mask, pearl, "Alpha")

    # Clear coat, option A: Roughness = Lerp(Roughness, 0.08, ClearCoat * Fresnel(5)).
    fresnel5 = g.node("Fresnel", -500, 0, exponent=5.0)
    coat_mask = g.node("Multiply", -300, 0)
    g.link(clear_coat, coat_mask, "A")
    g.link(fresnel5, coat_mask, "B")
    gloss = g.node("LinearInterpolate", -100, 100)
    g.link(worn_roughness, gloss, "A")
    gloss.set_editor_property("const_b", 0.08)
    g.link(coat_mask, gloss, "Alpha")

    # Panel bump on the Normal pin, world space.
    normal_vs = g.node("VertexNormalWS", -900, 900)
    depth = g.const(2.0, -900, 1050)
    height = g.node("Multiply", -700, 900, const_b=1.0)
    g.link(panel_height, height, "A")
    bump = g.custom("Panel bump", BUMP_HLSL, {"H": height, "P": position, "N": normal_vs, "Depth": depth,
                                              "Strength": panel_strength}, F3, -500, 900)

    # Emissive: Team slots glow with TeamColor * SelfLit, the others with EmissiveColor * EmissiveIntensity.
    team_glow_scale = g.node("Add", -900, 1200)
    g.link(emissive_intensity, team_glow_scale, "A")
    g.link(self_lit, team_glow_scale, "B")
    team_glow = g.node("Multiply", -700, 1200)
    g.link(team, team_glow, "A")
    g.link(team_glow_scale, team_glow, "B")
    flat_glow = g.node("Multiply", -700, 1350)
    g.link(emissive_color, flat_glow, "A")
    g.link(emissive_intensity, flat_glow, "B")
    emissive = g.switch("UseTeam", False, team_glow, flat_glow, -450, 1250, "Paint")

    for (source, out), prop in (((pearl, ""), unreal.MaterialProperty.MP_BASE_COLOR),
                                (worn_metallic, unreal.MaterialProperty.MP_METALLIC),
                                ((gloss, ""), unreal.MaterialProperty.MP_ROUGHNESS),
                                ((emissive, ""), unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                                ((bump, ""), unreal.MaterialProperty.MP_NORMAL)):
        require(library.connect_material_property(source, out, prop), "Could not connect " + str(prop))


def _result(call):
    return (call, _out(call, "Result"))


# ---------------------------------------------------------------- instances (UNREAL.md section 8)
HUMAN_BLUE = (0.04, 0.50, 1.0)
MACHINE_RED = (1.0, 0.08, 0.08)
AMBER = (1.0, 0.55, 0.15)
CYAN = (0.35, 0.85, 1.0)
CLEAN = dict(
    BaseColor=(0.2, 0.2, 0.2), TeamColor=HUMAN_BLUE, UseTeam=0.0, Metallic=0.0, Roughness=0.5, ClearCoat=0.0,
    PearlTint=(0.8, 0.85, 1.0), PearlAmount=0.0, BareMetalColor=(0.60, 0.62, 0.66), EdgeWear=0.0,
    EdgeHighlight=0.0, GrimeColor=(0.36, 0.32, 0.28), GrimeAmount=0.0, GroundGrime=0.0, PanelStrength=0.0,
    PanelLine=0.0, HazardStripes=0.0, HazardDark=(0.02, 0.02, 0.02), EmissiveColor=(0.0, 0.0, 0.0),
    EmissiveIntensity=0.0, SelfLit=0.0)
HUMAN_TEX = ("PlatesHuman", "Chip", "GrimeHuman")
DARK_TEX = ("PlatesDark", "Chip", "GrimeDark")
MACHINE_TEX = ("PlatesMachine", "Chip", "GrimeMachine")


def _p(**kw):
    return kw


# (faction, slot) -> parameter overrides of CLEAN; "tex" = (panel, wear, grime) roles. Mirrors PARAMS in
# Build/MasterMaterials.py, i.e. the table of UNREAL.md section 8.
PARAMS = {
    ("Human", "Shell"): _p(BaseColor=(0.46, 0.53, 0.64), Metallic=0.10, Roughness=0.42, EdgeWear=0.7,
                           EdgeHighlight=0.9, BareMetalColor=(0.66, 0.68, 0.72), GrimeAmount=0.32,
                           GroundGrime=0.35, PanelStrength=0.6, PanelLine=0.7, tex=HUMAN_TEX),
    ("Human", "Dark"): _p(BaseColor=(0.15, 0.165, 0.19), Metallic=0.35, Roughness=0.48, EdgeWear=0.55,
                          EdgeHighlight=0.9, BareMetalColor=(0.46, 0.48, 0.52), GrimeAmount=0.2,
                          GroundGrime=0.25, PanelStrength=0.4, PanelLine=0.4, tex=DARK_TEX),
    ("Human", "Team"): _p(BaseColor=HUMAN_BLUE, TeamColor=HUMAN_BLUE, UseTeam=1.0, Metallic=0.0, Roughness=0.40,
                          ClearCoat=0.2, EdgeWear=0.4, EdgeHighlight=0.3, BareMetalColor=(0.62, 0.64, 0.68),
                          GrimeAmount=0.2, GroundGrime=0.3, PanelStrength=0.35, PanelLine=0.35, SelfLit=0.10,
                          tex=HUMAN_TEX),
    ("Human", "Glow"): _p(BaseColor=(0.05, 0.03, 0.02), Roughness=0.5, EmissiveColor=AMBER, EmissiveIntensity=1.0,
                          tex=HUMAN_TEX),
    ("Human", "Accent"): _p(BaseColor=(0.80, 0.55, 0.02), Metallic=0.1, Roughness=0.55, EdgeWear=0.9,
                            EdgeHighlight=0.5, BareMetalColor=(0.5, 0.5, 0.52), GrimeAmount=0.55,
                            GroundGrime=0.6, PanelStrength=0.2, PanelLine=0.2, HazardStripes=1.0, tex=HUMAN_TEX),
    ("Machine", "Shell"): _p(BaseColor=(0.88, 0.90, 0.94), Metallic=0.12, Roughness=0.20, ClearCoat=0.8,
                             PearlTint=(0.62, 0.80, 1.0), PearlAmount=0.30, EdgeWear=0.06, EdgeHighlight=0.65,
                             BareMetalColor=(0.85, 0.87, 0.90), GrimeColor=(0.62, 0.64, 0.70), GrimeAmount=0.16,
                             GroundGrime=0.06, PanelStrength=0.5, PanelLine=0.6, tex=MACHINE_TEX),
    ("Machine", "Dark"): _p(BaseColor=(0.04, 0.05, 0.07), Metallic=0.6, Roughness=0.30, ClearCoat=0.4,
                            EdgeWear=0.2, EdgeHighlight=0.8, BareMetalColor=(0.35, 0.40, 0.48), GrimeAmount=0.15,
                            PanelStrength=0.3, PanelLine=0.3, tex=MACHINE_TEX),
    ("Machine", "Team"): _p(BaseColor=MACHINE_RED, TeamColor=MACHINE_RED, UseTeam=1.0, Roughness=0.25,
                            ClearCoat=0.9, EdgeHighlight=0.5, PanelStrength=0.25, PanelLine=0.35, GrimeAmount=0.12,
                            SelfLit=0.35, tex=MACHINE_TEX),
    ("Machine", "Glow"): _p(BaseColor=(0.02, 0.05, 0.06), Roughness=0.3, EmissiveColor=CYAN, EmissiveIntensity=1.0,
                            tex=MACHINE_TEX),
    ("Machine", "Accent"): _p(BaseColor=(0.05, 0.005, 0.005), Roughness=0.3, EmissiveColor=(1.0, 0.05, 0.03),
                              EmissiveIntensity=1.0, tex=MACHINE_TEX),
}
PARAMS[("Cluster", "Shell")] = dict(PARAMS[("Machine", "Shell")], BaseColor=(0.90, 0.92, 0.95), Roughness=0.14,
                                    ClearCoat=1.0, EdgeHighlight=0.55, GrimeAmount=0.18)
for _slot in ("Team", "Dark", "Glow", "Accent"):
    PARAMS[("Cluster", _slot)] = PARAMS[("Machine", _slot)]

# Scope: TileSize (panel, wear, grime) in metres, glow multiplier of EmissiveIntensity, grime multiplier.
SCOPES = {"Unit": dict(tile=(0.9, 0.7, 1.3), glow=2.0, grime=1.0),
          "Bld": dict(tile=(2.4, 1.6, 3.0), glow=2.5, grime=0.8),
          "Env": dict(tile=(3.6, 2.4, 4.5), glow=3.0, grime=0.55)}
# Unreal dusk (manual exposure bias 10) is much darker than the Blender preview, so emissive above ~1 per channel tone-maps
# to cream / white. Per-faction gain on the scope glow keeps amber amber and cyan cyan (gallery captures, sc2-art-unreal).
GLOW_GAIN = {"Human": 0.5, "Machine": 0.75, "Cluster": 0.75}
ACCENT_MACHINE_GLOW = 1.0   # kit Machine/Cluster Accent lenses stay unscaled: 8.0 clips red to peach

# Construction scaffolds (UNREAL.md section 7): Human Team/Dark/Glow, amber Team default, bare-steel Shell.
CONSTRUCTION = {"Team": dict(BaseColor=(0.8, 0.6, 0.18), TeamColor=(0.8, 0.6, 0.18)),
                "Shell": dict(BaseColor=(0.50, 0.52, 0.56), Metallic=0.3)}
# Deviations from the UNREAL.md section 8 table that the ArtGallery / CampusZero captures asked for (Blender's studio
# light is far brighter than the Campus Zero dusk): the campus halls stay cool grey-blue (v9 judged them so, kit Shell
# 0.50 / 0.55 / 0.64), not white. The Machine units and buildings keep the table's 0.88 pearl.
SCOPE_OVERRIDES = {("Machine", "Shell", "Env"): dict(BaseColor=(0.60, 0.64, 0.72))}

SLOTS_OF_SCOPE = {"Unit": ("Team", "Shell", "Dark", "Glow"), "Bld": ("Team", "Shell", "Dark", "Glow"),
                  "Env": ("Shell", "Dark", "Glow", "Accent")}
FACTIONS_OF_SCOPE = {"Unit": ("Human", "Machine"), "Bld": ("Human", "Machine"), "Env": ("Human", "Machine", "Cluster")}
VECTORS = ("BaseColor", "TeamColor", "PearlTint", "BareMetalColor", "GrimeColor", "HazardDark", "EmissiveColor")
SCALARS = ("Metallic", "Roughness", "ClearCoat", "PearlAmount", "EdgeWear", "EdgeHighlight", "GrimeAmount",
           "GroundGrime", "PanelLine", "PanelStrength", "EmissiveIntensity", "SelfLit")


def instance_name(faction, slot, scope):
    return "MI_SC2_%s_%s_%s" % (faction, slot, scope)


def build_instance(name, faction, slot, scope, master, textures, overrides=None):
    params = dict(CLEAN, **PARAMS[(faction, slot)])
    params.update(overrides or {})
    scale = SCOPES[scope]
    role = params["tex"]
    glow = ACCENT_MACHINE_GLOW if (slot == "Accent" and faction != "Human") else scale["glow"] * GLOW_GAIN[faction]
    vectors = {key: params[key] for key in VECTORS}
    vectors["TileSize"] = scale["tile"]
    scalars = {key: params[key] for key in SCALARS}
    scalars["EmissiveIntensity"] = params["EmissiveIntensity"] * glow
    scalars["GrimeAmount"] = params["GrimeAmount"] * scale["grime"]
    scalars["GroundGrime"] = params["GroundGrime"] * scale["grime"]
    switches = {"UseTeam": params["UseTeam"] > 0.5, "HazardStripes": params["HazardStripes"] > 0.5,
                "UseBakedMasks": USE_BAKED_MASKS}
    maps = dict(zip(("TexPanel", "TexWear", "TexGrime"), (textures[r] for r in role)))
    return art._instance(name, master, vectors, scalars, switches, maps)


def build_instances(master, textures):
    count = 0
    for scope, slots in SLOTS_OF_SCOPE.items():
        for faction in FACTIONS_OF_SCOPE[scope]:
            for slot in slots:
                build_instance(instance_name(faction, slot, scope), faction, slot, scope, master, textures,
                               SCOPE_OVERRIDES.get((faction, slot, scope)))
                count += 1
    for slot in SLOTS_OF_SCOPE["Bld"]:
        build_instance(instance_name("Construction", slot, "Bld"), "Human", slot, "Bld", master, textures,
                       CONSTRUCTION.get(slot))
        count += 1
    unreal.log("SC2_INSTANCES_BUILT %d baked_masks=%s" % (count, USE_BAKED_MASKS))
    return count


# ---------------------------------------------------------------- run
def discard(*names):
    """Delete the named assets, dependents first. Clearing an existing graph instead is not safe: the engine's
    delete-all-expressions asserts on rooted expressions in a reloaded function. The instances only reference the
    parent by path and are re-parented below."""
    for name in names:
        path = art.FOLDER + "/" + name
        if assets.does_asset_exist(path):
            require(assets.delete_asset(path), "Could not delete " + path)


def create(name, asset_class, factory):
    return require(tools.create_asset(name, art.FOLDER, asset_class, factory), "Could not create " + name)


def main():
    textures = import_textures()
    discard("M_Shared", "MF_SC2_Wear", "MF_Triplanar_Local")
    triplanar = create("MF_Triplanar_Local", unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    build_triplanar(triplanar, textures["PlatesHuman"])
    require(assets.save_loaded_asset(triplanar), "Could not save MF_Triplanar_Local")
    wear = create("MF_SC2_Wear", unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    build_wear(wear)
    require(assets.save_loaded_asset(wear), "Could not save MF_SC2_Wear")
    material = create("M_Shared", unreal.Material, unreal.MaterialFactoryNew())
    build_shared(material, textures, triplanar, wear)
    library.recompile_material(material)
    require(assets.save_loaded_asset(material), "Could not save M_Shared")
    stats = library.get_statistics(material)
    require(stats.num_pixel_shader_instructions > 0,
            "M_Shared has no compiled shader (run with -RenderOffscreen, not -nullrhi, so shaders really compile)")
    expressions = library.get_num_material_expressions(material)
    switches = sorted(str(n) for n in library.get_static_switch_parameter_names(material))
    require(switches == ["HazardStripes", "UseBakedMasks", "UseTeam"], "Unexpected static switches %s" % switches)
    scalars = sorted(str(n) for n in library.get_scalar_parameter_names(material))
    vectors = sorted(str(n) for n in library.get_vector_parameter_names(material))
    require(len(scalars) == 12 and len(vectors) == 8, "Unexpected parameters %s %s" % (scalars, vectors))
    require(library.get_material_default_static_switch_parameter_value(material, "UseBakedMasks") is False,
            "UseBakedMasks must default off")
    unreal.log("M_SHARED_COMPILED expressions=%d ps_instructions=%d vs_instructions=%d samplers=%d texture_samples=%d "
               "scalars=%d vectors=%d switches=%s" % (
                   expressions, stats.num_pixel_shader_instructions, stats.num_vertex_shader_instructions,
                   stats.num_samplers, stats.num_pixel_texture_samples, len(scalars), len(vectors), switches))
    build_instances(material, textures)


main()
