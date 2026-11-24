# SPDX-License-Identifier: GPL-2.0-or-later
"""Render a real preview picture for every Create preset with Blender itself.

    build-blender/bin/blender -b --factory-startup --python tools/render_presets.py -- \
        tools/presets.json blender/intern/harness/resources/presets [only_id ...]

One lit set (a figure on a plinth in a colonnade, or a product bottle, or a clay model for
3D presets) is dressed for each preset: the camera starts where the move starts (close for
zoom-ins, wide for pull-backs, low for tilt-ups, top-down for overhead, rolled for a dutch
angle, fisheye for the fisheye lens...) and effects are built for real (an emissive burst,
flames, chrome, ice, shards, particles...). The shell plays each preset's move over its
picture, so the tiles show both the look and the motion.
"""

import json
import math
import os
import random
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
presets = json.load(open(argv[0]))
out_dir = argv[1]
only = set(argv[2:])
os.makedirs(out_dir, exist_ok=True)

W, H = 480, 308          # tile ratio 0.64, rendered at 1.5x the largest tile
ORANGE = (0.984, 0.58, 0.043)
BLUE = (0.294, 0.576, 1.0)
SKY = (0.62, 0.796, 1.0)
MIST = (0.835, 0.851, 0.886)


def srgb(c):
    return tuple(((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in c)


def mat(name, color, rough=0.5, metal=0.0, emit=None, strength=0.0, transmission=0.0, alpha=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*srgb(color), 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if transmission:
        b.inputs["Transmission Weight"].default_value = transmission
    if emit:
        b.inputs["Emission Color"].default_value = (*srgb(emit), 1)
        b.inputs["Emission Strength"].default_value = strength
    if alpha < 1:
        b.inputs["Alpha"].default_value = alpha
    return m


def clear():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras, bpy.data.curves):
        for d in list(coll):
            coll.remove(d)


def add(kind, name, loc=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0), m=None, smooth=True, **kw):
    getattr(bpy.ops.mesh, "primitive_%s_add" % kind)(location=loc, rotation=rot, **kw)
    ob = bpy.context.object
    ob.name = name
    ob.scale = scale
    if m:
        ob.data.materials.append(m)
    if smooth and hasattr(ob.data, "polygons"):
        for p in ob.data.polygons:
            p.use_smooth = True
    return ob


def build_set(p):
    """The stage, the subject and the lights for preset p. Returns the subject's centre."""
    clear()
    scene = bpy.context.scene
    world = bpy.data.worlds.new("W")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    a, cat = p["anim"], p["cat"]
    sky = a.get("sky")
    bg.inputs["Color"].default_value = (*srgb((0.9, 0.45, 0.25) if sky else (0.06, 0.07, 0.09)), 1)
    bg.inputs["Strength"].default_value = 0.7 if sky else 0.5

    floor_m = mat("Floor", (0.10, 0.105, 0.115), rough=0.35)
    add("plane", "Floor", scale=(40, 40, 1), m=floor_m)
    if a.get("road"):
        road = mat("Road", (0.04, 0.04, 0.045), rough=0.8)
        add("plane", "Road", loc=(0, 0, 0.002), scale=(1.6, 40, 1), m=road)
        line = mat("Line", MIST, rough=0.5, emit=MIST, strength=0.6)
        for i in range(-10, 30, 3):
            add("cube", "Stripe", loc=(0, i, 0.006), scale=(0.06, 0.6, 0.004), m=line, smooth=False)

    # Colonnade behind (not for product / 3D studio sets).
    studio = cat in {"product", "productimg", "3d", "apps"}
    if not studio:
        col_m = mat("Stone", (0.55, 0.56, 0.6), rough=0.6)
        for i in range(-5, 6):
            add("cylinder", "Pillar", loc=(i * 1.6, 5 + (i % 2) * 1.2, 1.6), scale=(0.22, 0.22, 1.6), m=col_m, vertices=24)
    else:
        sweep = mat("Sweep", (0.13, 0.135, 0.15), rough=0.55)
        add("plane", "Backdrop", loc=(0, 4, 3), rot=(math.radians(90), 0, 0), scale=(12, 6, 1), m=sweep)

    # Subject.
    fx = a.get("fx", "")
    body_color = MIST
    rough, metal, trans = 0.45, 0.0, 0.0
    if fx == "metal":
        body_color, rough, metal = (0.95, 0.95, 0.97), 0.08, 1.0
    elif fx == "frost":
        body_color, rough, trans = (0.75, 0.88, 1.0), 0.15, 0.6
    elif fx == "swap" and p["id"] == "outfit_swap":
        body_color = (0.25, 0.32, 0.55)
    elif fx == "squash":
        body_color, rough = (0.95, 0.72, 0.55), 0.95
    body_m = mat("Body", body_color, rough=rough, metal=metal, transmission=trans)
    accent_m = mat("Accent", ORANGE, rough=0.35)
    lift = 0.6 if fx == "float" else 0.0
    centre = Vector((0, 0, 1.15 + lift))

    if cat in {"product", "productimg"} or p["id"] in {"product_model", "packshot"}:
        glass = mat("Glass", (0.85, 0.55, 0.25), rough=0.05, transmission=0.9)
        add("cylinder", "Plinth", loc=(0, 0, 0.25), scale=(0.9, 0.9, 0.25), m=mat("Plinth", (0.2, 0.2, 0.22), rough=0.3), vertices=64)
        add("cylinder", "Bottle", loc=(0, 0, 1.15), scale=(0.38, 0.38, 0.65), m=glass, vertices=64)
        add("cylinder", "Cap", loc=(0, 0, 1.95), scale=(0.2, 0.2, 0.16), m=mat("Gold", (0.95, 0.7, 0.35), rough=0.2, metal=1), vertices=48)
        add("cube", "Label", loc=(0, -0.385, 1.1), scale=(0.24, 0.01, 0.2), m=accent_m, smooth=False)
        centre = Vector((0, 0, 1.2))
    elif cat == "3d":
        clay = mat("Clay", (0.72, 0.7, 0.68), rough=0.85)
        add("cylinder", "Turntable", loc=(0, 0, 0.06), scale=(1.1, 1.1, 0.06), m=mat("Disc", (0.18, 0.18, 0.2), rough=0.4), vertices=64)
        if p["id"] == "game_prop":
            add("cube", "Crate", loc=(0, 0, 0.62), scale=(0.55, 0.55, 0.55), m=mat("Wood", (0.55, 0.34, 0.18), rough=0.7), smooth=False)
            for z in (0.12, 1.12):
                add("cube", "Band", loc=(0, 0, z), scale=(0.57, 0.57, 0.06), m=mat("Iron", (0.3, 0.3, 0.32), rough=0.4, metal=1), smooth=False)
        elif p["id"] == "low_poly":
            ob = add("ico_sphere", "Rock", loc=(0, 0, 0.7), scale=(0.7, 0.6, 0.55), m=mat("Rock", (0.45, 0.5, 0.58), rough=0.8), smooth=False, subdivisions=1)
        elif p["id"] == "environment":
            for i in range(3):
                add("cube", "Arch", loc=(i * 0.9 - 0.9, 0, 0.8), scale=(0.12, 0.12, 0.8), m=clay, smooth=False)
            add("cube", "Beam", loc=(0, 0, 1.62), scale=(1.1, 0.14, 0.08), m=clay, smooth=False)
        else:
            add("uv_sphere", "Head", loc=(0, 0, 1.65), scale=(0.24, 0.24, 0.28), m=clay)
            add("cylinder", "Torso", loc=(0, 0, 1.05), scale=(0.3, 0.2, 0.38), m=clay, vertices=32)
            for s in (-1, 1):
                add("cylinder", "Leg", loc=(s * 0.13, 0, 0.42), scale=(0.09, 0.09, 0.36), m=clay, vertices=24)
                add("cylinder", "Arm", loc=(s * 0.48, 0, 1.12), rot=(0, math.radians(s * 60), 0), scale=(0.07, 0.07, 0.3), m=clay, vertices=24)
        centre = Vector((0, 0, 0.85))
    else:
        if fx == "plinth" or p["id"] in {"figure_3d", "render_3d"}:
            add("cylinder", "Plinth", loc=(0, 0, 0.15), scale=(0.7, 0.7, 0.15), m=mat("Plinth", (0.16, 0.16, 0.18), rough=0.3), vertices=64)
        sx, sz = (1.35, 0.55) if fx == "melt" else (1.0, 1.0)
        add("uv_sphere", "Head", loc=(0, 0, (1.95 + lift) * (0.75 if fx == "melt" else 1)), scale=(0.27 * sx, 0.27 * sx, 0.3 * sz), m=body_m)
        add("cylinder", "Torso", loc=(0, 0, (1.2 + lift) * (0.7 if fx == "melt" else 1)), scale=(0.38 * sx, 0.26 * sx, 0.55 * sz), m=body_m, vertices=48)
        add("cube", "Scarf", loc=(0, -0.2, (1.62 + lift) * (0.72 if fx == "melt" else 1)), scale=(0.3, 0.08, 0.06), m=accent_m)
        for s in (-1, 1):
            add("cylinder", "Leg", loc=(s * 0.16, 0, (0.38 + lift) * (0.6 if fx == "melt" else 1)), scale=(0.11 * sx, 0.11 * sx, 0.38 * sz), m=body_m, vertices=24)
        if fx == "peel":
            add("cylinder", "StickerEdge", loc=(0, 0.25, 1.15), rot=(math.radians(90), 0, 0), scale=(0.95, 1.25, 0.01), m=mat("White", (1, 1, 1), rough=0.6), vertices=64)
        if fx == "glitch":
            for s, c in ((-0.12, BLUE), (0.12, ORANGE)):
                add("cylinder", "Ghost", loc=(s, 0.05, 1.2), scale=(0.38, 0.26, 0.55), m=mat("G", c, emit=c, strength=2.5, alpha=0.6), vertices=48)

    # Effects.
    rnd = random.Random(p["id"])
    if fx == "burst":
        add("uv_sphere", "Fireball", loc=(0, 2.2, 1.6), scale=(1.4, 1.4, 1.2), m=mat("Fire", ORANGE, emit=(1, 0.55, 0.15), strength=18))
        add("torus", "Shock", loc=(0, 2.0, 1.4), rot=(math.radians(90), 0, 0), m=mat("Ring", (1, 0.8, 0.5), emit=(1, 0.75, 0.4), strength=10), major_radius=2.4, minor_radius=0.05)
    if fx == "fire":
        fm = mat("Flame", ORANGE, emit=(1, 0.5, 0.1), strength=22)
        for i in range(9):
            ang = i / 9 * math.tau
            add("cone", "Flame", loc=(math.cos(ang) * 0.42, math.sin(ang) * 0.32, 0.55), scale=(0.12, 0.12, 0.45 + rnd.random() * 0.5), m=fm, vertices=12)
    if fx in {"dust", "shatter", "sparkle"}:
        pm = mat("Bits", MIST if fx != "sparkle" else (1, 1, 1), emit=(1, 1, 1) if fx == "sparkle" else None, strength=8 if fx == "sparkle" else 0, rough=0.4)
        for i in range(60 if fx == "dust" else 26):
            sz = 0.03 if fx != "shatter" else 0.09
            add("cube", "Bit", loc=(rnd.uniform(-0.2, 1.6), rnd.uniform(-0.5, 0.5), rnd.uniform(0.5, 2.4)),
                rot=(rnd.random() * 3, rnd.random() * 3, rnd.random() * 3), scale=(sz, sz, sz * (0.4 if fx == "shatter" else 1)), m=pm, smooth=False)
    if fx == "bolt":
        bm = mat("Bolt", SKY, emit=SKY, strength=40)
        x, z = -0.9, 3.4
        for i in range(7):
            nx, nz = x + rnd.uniform(-0.3, 0.3), z - 0.45
            mid = Vector(((x + nx) / 2, 0.4, (z + nz) / 2))
            add("cylinder", "Arc", loc=mid, rot=(0, math.atan2(nx - x, nz - z), 0), scale=(0.025, 0.025, 0.25), m=bm, vertices=8)
            x, z = nx, nz
    if fx == "sweep":
        add("plane", "Streak", loc=(0.2, -0.6, 1.3), rot=(0, math.radians(25), 0), scale=(0.05, 1, 1.4), m=mat("Streak", (1, 1, 1), emit=(1, 1, 1), strength=6))
    if a.get("through"):
        add("torus", "Portal", loc=(0, -3.2, 1.3), rot=(math.radians(90), 0, 0), m=mat("Frame", (0.1, 0.1, 0.12), rough=0.4), major_radius=1.6, minor_radius=0.18)
    if a.get("focus"):
        add("uv_sphere", "Foreground", loc=(-0.9, -3.2, 1.2), scale=(0.35, 0.35, 0.35), m=mat("Leaf", (0.3, 0.55, 0.35), rough=0.6))

    # Lights: warm key, cool rim, soft fill.
    def light(name, kind, loc, energy, color, size=2.0):
        d = bpy.data.lights.new(name, kind)
        d.energy = energy
        d.color = color
        if kind == 'AREA':
            d.size = size
        ob = bpy.data.objects.new(name, d)
        scene.collection.objects.link(ob)
        ob.location = loc
        direction = centre - Vector(loc)
        ob.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
        return ob
    light("Key", 'AREA', (-3.5, -3.0, 4.0), 900, (1.0, 0.86, 0.72), 3)
    light("Rim", 'AREA', (3.0, 3.5, 3.0), 700, SKY, 2)
    light("Fill", 'AREA', (3.5, -4.0, 1.5), 180, (1, 1, 1), 4)
    if cat == "3d" or studio:
        light("Top", 'AREA', (0, 0, 6), 400, (1, 1, 1), 5)
    return centre


def place_camera(p, centre):
    a = p["anim"]
    scene = bpy.context.scene
    data = bpy.data.cameras.new("Cam")
    cam = bpy.data.objects.new("Cam", data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    zoom = float(a.get("zoom", 1) or 1)
    dist = 6.5
    height = centre.z + 0.2
    yaw = math.radians(-18)
    lens = 40
    if zoom > 1.3:
        dist = 4.4 if zoom < 2 else 3.0          # starts close for push-ins
    if zoom < 0.9:
        dist = 9.5                               # wide for pull-backs
    if a.get("dollyZoom"):
        lens, dist = (24, 4.2) if a["dollyZoom"] > 0 else (85, 12)
    if a.get("fisheye"):
        lens, dist = 12, 3.2
    dy = float(a.get("dy", 0) or 0)
    if dy > 0:
        height = 0.35                            # low, looking up
    elif dy < 0:
        height = centre.z + 2.8                  # high, looking down
    if a.get("orbit") or a.get("spin"):
        yaw = math.radians(-40)
    target = centre.copy()
    if a.get("overhead"):
        cam.location = (0, -0.6, 5.2)
        target = Vector((0, 0, 0))
    else:
        cam.location = (math.sin(yaw) * -dist, -math.cos(yaw) * dist, height)
    if p["id"] == "eyes_in":
        target = centre + Vector((0, 0, 0.8))
        cam.location = Vector(cam.location) + Vector((0, 2.0, 0.6))
    direction = target - Vector(cam.location)
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    roll = float(a.get("roll", 0) or 0)
    if roll:
        cam.rotation_euler.rotate_axis('Z', math.radians(-roll * 1.2))
    data.lens = lens
    if a.get("focus"):
        data.dof.use_dof = True
        data.dof.focus_object = None
        data.dof.focus_distance = (target - Vector(cam.location)).length
        data.dof.aperture_fstop = 1.4
    if a.get("blur"):
        scene.render.use_motion_blur = True


def render(p):
    scene = bpy.context.scene
    centre = build_set(p)
    place_camera(p, centre)
    r = scene.render
    r.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 40
    scene.cycles.use_denoising = True
    try:
        scene.cycles.denoiser = 'OPENIMAGEDENOISE'
    except Exception:
        pass
    r.resolution_x, r.resolution_y, r.resolution_percentage = W, H, 100
    r.image_settings.file_format = 'JPEG'
    r.image_settings.quality = 86
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.look = 'AgX - Medium High Contrast'
    r.filepath = os.path.join(out_dir, p["id"] + ".jpg")
    bpy.ops.render.render(write_still=True)


for p in presets:
    if only and p["id"] not in only:
        continue
    try:
        render(p)
        print("PRESET", p["id"], flush=True)
    except Exception as ex:
        print("PRESET-FAIL", p["id"], ex, flush=True)
