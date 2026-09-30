"""Render the HUD portrait icons from the Blender art sources (Art/UI, see Art/UI/IMPLEMENTATION.md).

Run headless from the repo root (Blender 5.2.1, no network, deterministic):

    blender -b --factory-startup -P Build/RenderUIIcons.py
    blender -b --factory-startup -P Build/RenderUIIcons.py -- --only Barracks --size 256 --samples 32

Options after `--`:  --only SUBSTRING (repeatable, render matching objects only), --size PX (default 256),
--samples N (default 96), --out DIR (default Art/UI/icons/portraits).

Read-only by construction: each source .blend (Art/Units/Units.blend, Art/Buildings/Buildings.blend,
Art/Environment/Environment.blend) is COPIED into a temporary directory and the copy is opened. Nothing is ever
saved, and no source file is modified, so it is safe while other scripts regenerate the sources.

Every top-level mesh named SM_* renders once, alone, as a square 256 px RGBA PNG with a transparent background:
    <out>/units/<name>.png          <out>/buildings/<name>.png          <out>/environment/<name>.png
plus <name>_team.png, a white mask of the Team-slot paint (alpha = coverage). The runtime can draw it over the
portrait tinted with the commander colour, so one portrait serves all five player colours.

Look: orthographic three-quarter view (camera in front of the +X face, 26 degrees to the right, 32 degrees
up), fitted to the object's bounding box with a 9 % margin, key sun warm from upper left, cool fill, rim light.
Units and buildings use fixed portrait materials chosen by faction from the object name, mapped onto the
contract slots by name (Team / Shell / Dark / Glow); Environment pieces map Shell / Dark / Glow / Accent.
Human = blue Team paint (commander 1), gunmetal shell, amber glow. Machine = pearl shell, cyan glow, red Team
lens. Construction scaffolds = amber Team, brushed steel.
"""
import math
import os
import shutil
import sys
import tempfile

import bpy
import numpy as np
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = (
    ("units", os.path.join(ROOT, "Art", "Units", "Units.blend")),
    ("buildings", os.path.join(ROOT, "Art", "Buildings", "Buildings.blend")),
    ("environment", os.path.join(ROOT, "Art", "Environment", "Environment.blend")),
)

AZIMUTH = -26.0     # degrees; camera swings toward -Y from the +X (front) axis
ELEVATION = 32.0
MARGIN = 1.09

# Portrait materials: (base colour, metallic, roughness, coat, emission strength). Colours are linear-ish sRGB
# values chosen to match the Blender previews of the art passes.
LOOKS = {
    "Human": {
        "Team": ((0.04, 0.30, 1.00), 0.30, 0.35, 0.4, 0.0),
        "Shell": ((0.30, 0.37, 0.47), 0.75, 0.34, 0.0, 0.0),
        "Dark": ((0.045, 0.05, 0.058), 0.60, 0.42, 0.0, 0.0),
        "Glow": ((1.00, 0.55, 0.15), 0.0, 0.5, 0.0, 3.0),
    },
    "Machine": {
        "Team": ((0.90, 0.02, 0.02), 0.0, 0.30, 0.5, 0.9),
        "Shell": ((0.80, 0.83, 0.87), 0.10, 0.20, 0.7, 0.0),
        "Dark": ((0.035, 0.04, 0.05), 0.40, 0.40, 0.0, 0.0),
        "Glow": ((0.45, 0.95, 1.00), 0.0, 0.5, 0.0, 2.6),
    },
    "Neutral": {
        "Team": ((1.00, 0.50, 0.02), 0.30, 0.40, 0.4, 0.0),
        "Shell": ((0.46, 0.48, 0.52), 0.70, 0.40, 0.0, 0.0),
        "Dark": ((0.045, 0.05, 0.058), 0.60, 0.42, 0.0, 0.0),
        "Glow": ((1.00, 0.55, 0.15), 0.0, 0.5, 0.0, 3.0),
    },
    "Environment": {
        "Shell": ((0.36, 0.40, 0.46), 0.60, 0.40, 0.0, 0.0),
        "Dark": ((0.05, 0.055, 0.065), 0.50, 0.50, 0.0, 0.0),
        "Glow": ((0.45, 0.95, 1.00), 0.0, 0.5, 0.0, 2.0),
        "Accent": ((0.95, 0.62, 0.15), 0.30, 0.45, 0.0, 0.0),
    },
}
SLOTS = ("Team", "Shell", "Dark", "Glow", "Accent")


def look_name(obj_name):
    if obj_name.startswith("SM_Env_"):
        return "Environment"
    if "_Machine_" in obj_name:
        return "Machine"
    if "_Human_" in obj_name:
        return "Human"
    return "Neutral"


def slot_kind(material_name):
    for kind in SLOTS:
        if kind.lower() in (material_name or "").lower():
            return kind
    return "Shell"


_cache = {}


def portrait_material(look, kind):
    key = (look, kind)
    if key not in _cache:
        rgb, metallic, roughness, coat, emission = LOOKS[look].get(kind, LOOKS[look]["Shell"])
        mat = bpy.data.materials.new("UI_%s_%s" % (look, kind))
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Metallic"].default_value = metallic
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Coat Weight"].default_value = coat
        if emission > 0.0:
            bsdf.inputs["Emission Color"].default_value = (*rgb, 1.0)
            bsdf.inputs["Emission Strength"].default_value = emission
        _cache[key] = mat
    return _cache[key]


def emission_material(name, value):
    if name not in bpy.data.materials:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        tree = mat.node_tree
        tree.nodes.clear()
        out = tree.nodes.new("ShaderNodeOutputMaterial")
        emit = tree.nodes.new("ShaderNodeEmission")
        emit.inputs["Color"].default_value = (value, value, value, 1.0)
        emit.inputs["Strength"].default_value = 1.0
        tree.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    return bpy.data.materials[name]


def sun(scene, name, direction, energy, color, angle=6.0):
    data = bpy.data.lights.new(name, "SUN")
    data.energy = energy
    data.color = color
    data.angle = math.radians(angle)
    obj = bpy.data.objects.new(name, data)
    obj.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(obj)
    return obj


def setup_scene(size, samples):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.film_exposure = 1.0
    scene.render.film_transparent = True
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    world = bpy.data.worlds.new("UI_World")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.13, 0.16, 0.22, 1.0)
    bg.inputs["Strength"].default_value = 1.15
    az = math.radians(AZIMUTH)
    # Directions are where the light travels. Key from upper left of the view, fill from the right, rim from behind.
    right = Vector((math.sin(az), math.cos(az), 0.0)).normalized()
    forward = Vector((math.cos(az), -math.sin(az), 0.0))     # from object toward the camera (horizontal)
    lights = [
        sun(scene, "UI_Key", (-forward * 0.6 - right * 0.7) + Vector((0, 0, -1.05)), 3.4, (1.0, 0.96, 0.90)),
        sun(scene, "UI_Fill", (-forward * 0.3 + right * 0.9) + Vector((0, 0, -0.55)), 1.1, (0.62, 0.76, 1.0), 20.0),
        sun(scene, "UI_Rim", (forward * 0.9) + Vector((0, 0, -0.35)), 1.6, (0.75, 0.85, 1.0)),
    ]
    cam_data = bpy.data.cameras.new("UI_Cam")
    cam_data.type = "ORTHO"
    cam_data.clip_start = 0.1
    cam_data.clip_end = 2000.0
    cam = bpy.data.objects.new("UI_Cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return scene, cam


def frame_camera(cam, obj):
    """Aim the orthographic camera at obj's bounding box and fit it to the square frame with a margin."""
    az, el = math.radians(AZIMUTH), math.radians(ELEVATION)
    direction = Vector((math.cos(az) * math.cos(el), -math.sin(az) * math.cos(el), math.sin(el)))
    rot = direction.to_track_quat("Z", "Y")          # camera looks along -Z, its Y axis is up on screen
    cam.rotation_euler = rot.to_euler()
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    inv = rot.inverted()
    pts = [inv @ c for c in corners]
    xs, ys = [p.x for p in pts], [p.y for p in pts]
    cx, cy = (min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5
    span = max(max(xs) - min(xs), max(ys) - min(ys)) * MARGIN
    depth = sum(p.z for p in pts) / len(pts)
    center_world = rot @ Vector((cx, cy, depth))
    cam.location = center_world + direction * 100.0
    cam.data.ortho_scale = max(span, 0.5)


def assign(obj, look, mask=False):
    """Give every slot a portrait material by slot kind; mask=True gives Team white emission, others black."""
    for slot in obj.material_slots:
        kind = slot_kind(slot.material.name if slot.material else "")
        slot.link = "OBJECT"
        if mask:
            slot.material = emission_material("UI_MaskWhite", 1.0) if kind == "Team" and look != "Environment" \
                else emission_material("UI_MaskBlack", 0.0)
        else:
            slot.material = portrait_material(look, kind)


def render_png(scene, path):
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def pixels(path):
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "sRGB"
    arr = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(arr)
    size = tuple(img.size)
    bpy.data.images.remove(img)
    return arr.reshape(size[1], size[0], 4)


def write_mask(mask_render, path):
    """Team mask: white RGB, alpha = Team-slot coverage (luminance of the emission pass times silhouette alpha)."""
    arr = pixels(mask_render)
    alpha = np.clip(arr[..., 3] * arr[..., :3].max(axis=-1), 0.0, 1.0)
    out = np.ones_like(arr)
    out[..., 3] = alpha
    img = bpy.data.images.new("UI_mask_out", arr.shape[1], arr.shape[0], alpha=True)
    img.pixels.foreach_set(out.reshape(-1))
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def render_source(group, blend_copy, out_root, only, size, samples):
    bpy.ops.wm.open_mainfile(filepath=blend_copy)
    _cache.clear()          # datablocks of the previous file are gone
    objects = [o for o in bpy.data.objects if o.parent is None and o.type == "MESH" and o.name.startswith("SM_")]
    objects = [o for o in objects if not only or any(token in o.name for token in only)]
    if not objects:
        return 0
    scene, cam = setup_scene(size, samples)
    out_dir = os.path.join(out_root, group)
    os.makedirs(out_dir, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="ui_mask_")
    for obj in sorted(objects, key=lambda o: o.name):
        look = look_name(obj.name)
        for other in bpy.data.objects:
            if other.type == "MESH":
                other.hide_render = other is not obj
        frame_camera(cam, obj)
        bpy.context.view_layer.update()
        assign(obj, look)
        scene.cycles.samples = samples
        render_png(scene, os.path.join(out_dir, obj.name + ".png"))
        assign(obj, look, mask=True)
        scene.cycles.samples = 12
        scene.cycles.use_denoising = False
        mask_tmp = os.path.join(tmp, obj.name + ".png")
        render_png(scene, mask_tmp)
        scene.cycles.use_denoising = True
        write_mask(mask_tmp, os.path.join(out_dir, obj.name + "_team.png"))
        print("PORTRAIT", group, obj.name, look)
    shutil.rmtree(tmp, ignore_errors=True)
    return len(objects)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only, size, samples = [], 256, 96
    out_root = os.path.join(ROOT, "Art", "UI", "icons", "portraits")
    i = 0
    while i < len(argv):
        if argv[i] == "--only":
            only.append(argv[i + 1])
        elif argv[i] == "--size":
            size = int(argv[i + 1])
        elif argv[i] == "--samples":
            samples = int(argv[i + 1])
        elif argv[i] == "--out":
            out_root = os.path.abspath(argv[i + 1])
        else:
            raise SystemExit("unknown option " + argv[i])
        i += 2
    total = 0
    with tempfile.TemporaryDirectory(prefix="ui_blend_copy_") as scratch:
        for group, path in SOURCES:
            if not os.path.exists(path):
                print("SKIP missing", path)
                continue
            copy = os.path.join(scratch, os.path.basename(path))
            shutil.copy2(path, copy)              # only the copy is ever opened
            total += render_source(group, copy, out_root, only, size, samples)
    print("UI_PORTRAITS_RENDERED", total)


main()
