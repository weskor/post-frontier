"""Render Art/Textures/ContactSheet.png: one lit, labelled sphere per material.

    blender -b --factory-startup -P Build/RenderTextureContactSheet.py

Needs Blender plus ImageMagick (`montage`) for labels and tiling. Normal maps are
DirectX, so the green channel is inverted for Blender (which expects OpenGL).
"""
import subprocess
import sys
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parent.parent
TEX = ROOT / "Art" / "Textures"
TMP = Path("/tmp/texsheet")
TMP.mkdir(exist_ok=True)
SIZE = 384
UV_TILES = 3.0

sets = sorted(d for d in TEX.glob("*/*") if d.is_dir())
assert sets, "no textures found"

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 48
scene.cycles.use_denoising = False
scene.render.resolution_x = scene.render.resolution_y = SIZE
scene.render.image_settings.file_format = "PNG"
scene.view_settings.view_transform = "Standard"

world = bpy.data.worlds.new("W")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs[0].default_value = (0.32, 0.36, 0.42, 1)
bg.inputs[1].default_value = 1.0

bpy.ops.mesh.primitive_uv_sphere_add(segments=96, ring_count=48, radius=1)
sphere = bpy.context.object
bpy.ops.object.shade_smooth()

cam_data = bpy.data.cameras.new("C")
cam_data.lens = 70
cam = bpy.data.objects.new("C", cam_data)
scene.collection.objects.link(cam)
cam.location = (0, -6.2, 0)
cam.rotation_euler = (1.5708, 0, 0)
scene.camera = cam

for name, loc, energy, size in (("Key", (3, -4, 4), 500, 3), ("Rim", (-4, -1, 3), 250, 3)):
    ld = bpy.data.lights.new(name, "AREA")
    ld.energy = energy
    ld.size = size
    lo = bpy.data.objects.new(name, ld)
    lo.location = loc
    scene.collection.objects.link(lo)
    track = lo.constraints.new("TRACK_TO")
    track.target = sphere
    track.track_axis = "TRACK_NEGATIVE_Z"
    track.up_axis = "UP_Y"


def image(path, colorspace):
    img = bpy.data.images.load(str(path))
    img.colorspace_settings.name = colorspace
    return img


def build_material(d: Path):
    aid = d.name
    mat = bpy.data.materials.new(aid)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (UV_TILES * 2, UV_TILES, 1)
    nt.links.new(tc.outputs["UV"], mp.inputs["Vector"])

    def tex(suffix, cs):
        p = d / f"{aid}_{suffix}.jpg"
        if not p.exists():
            return None
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = image(p, cs)
        n.extension = "REPEAT"
        nt.links.new(mp.outputs[0], n.inputs[0])
        return n

    base = tex("BaseColor", "sRGB")
    nt.links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    ao = tex("AO", "Non-Color")
    if ao:
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs[0].default_value = 1.0
        nt.links.new(base.outputs["Color"], mix.inputs[6])
        nt.links.new(ao.outputs["Color"], mix.inputs[7])
        nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
    rough = tex("Roughness", "Non-Color")
    nt.links.new(rough.outputs["Color"], bsdf.inputs["Roughness"])
    metal = tex("Metallic", "Non-Color")
    if metal:
        nt.links.new(metal.outputs["Color"], bsdf.inputs["Metallic"])
    norm = tex("Normal", "Non-Color")
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    inv = nt.nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    com = nt.nodes.new("ShaderNodeCombineColor")
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(norm.outputs["Color"], sep.inputs[0])
    nt.links.new(sep.outputs[1], inv.inputs[1])  # DX -> GL: flip green
    nt.links.new(sep.outputs[0], com.inputs[0])
    nt.links.new(inv.outputs[0], com.inputs[1])
    nt.links.new(sep.outputs[2], com.inputs[2])
    nt.links.new(com.outputs[0], nmap.inputs["Color"])
    nt.links.new(nmap.outputs[0], bsdf.inputs["Normal"])
    return mat


labels = []
for d in sets:
    sphere.data.materials.clear()
    sphere.data.materials.append(build_material(d))
    f = TMP / f"{d.name}.png"
    scene.render.filepath = str(f)
    bpy.ops.render.render(write_still=True)
    labels.append((d.parent.name, d.name, f))
    print("rendered", d.name)

args = ["montage"]
for cat, aid, f in labels:
    args += ["-label", f"{cat}/{aid}", str(f)]
args += [
    "-tile", "4x", "-geometry", f"{SIZE}x{SIZE}+6+6", "-background", "#1b1d22",
    "-fill", "white", "-pointsize", "20", str(TEX / "ContactSheet.png"),
]
subprocess.run(args, check=True)
print("wrote", TEX / "ContactSheet.png")
