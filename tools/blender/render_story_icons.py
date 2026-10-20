"""메인 스토리 장 끝 꾸미기 5종 아이콘 렌더 (`item.story_*` — docs/STORY.md 5절). 기본 도형만, 투명 배경 512x512.

  1) 렌더 (Blender 헤드리스):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_story_icons.py
       → Saved/Generated/hat_icon/Story<Pascal>.png 5장
  2) 외곽선·반입: `python3 tools/blender/render_hat_icon.py outline <Pascal> <id>` 5번
     → `tools/unreal/import_item_icons.py` → `create_data_tables.py`.
외부 에셋 없음. 모양은 우리 것(Reference 6절).
"""
import math
import os

import bpy

ROOT = "~/UNREAL/Snarehaven/"
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
SIZE = 512

COL = {
    "green": (0.16, 0.42, 0.22), "brown": (0.42, 0.26, 0.12), "wood": (0.68, 0.48, 0.26),
    "red": (0.78, 0.18, 0.16), "white": (0.93, 0.93, 0.9), "steel": (0.62, 0.66, 0.7),
    "gold": (0.98, 0.8, 0.25), "navy": (0.12, 0.2, 0.4),
}
_mats = {}


def put(obj, color):
    if color not in _mats:
        m = bpy.data.materials.new(color)
        m.diffuse_color = COL[color] + (1,)
        _mats[color] = m
    obj.data.materials.append(_mats[color])
    return obj


def cyl(loc, r, d, color, rot=(0, 0, 0), verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d, location=loc, rotation=rot)
    return put(bpy.context.object, color)


def cone(loc, r1, r2, d, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=r1, radius2=r2, depth=d, location=loc, rotation=rot)
    return put(bpy.context.object, color)


def box(loc, size, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(location=loc, rotation=rot)
    o = bpy.context.object
    o.scale = size
    return put(o, color)


def ball(loc, r, color, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc)
    o = bpy.context.object
    o.scale = scale
    return put(o, color)


def traveler_hat():
    cyl((0, 0, 0), 1.35, 0.14, "brown")
    cone((0, 0, 0.75), 0.85, 0.22, 1.4, "green")
    cyl((0, 0, 0.2), 0.9, 0.16, "red")
    ball((0.55, -0.55, 0.55), 0.2, "wood", (1, 1, 1.4))   # 솔방울


def ranch_signpost():
    box((0, 0, 0), (0.14, 0.14, 1.5), "brown")
    box((0.1, -0.1, 0.55), (0.95, 0.07, 0.4), "wood")
    box((-0.1, -0.1, -0.15), (0.8, 0.07, 0.32), "wood", (0, 0, math.radians(4)))
    for x in (-0.7, 0.8):
        ball((x, -0.18, 0.55), 0.06, "brown")


def boatman_scarf():
    bpy.ops.mesh.primitive_torus_add(major_radius=0.9, minor_radius=0.32, location=(0, 0, 0))
    o = bpy.context.object
    o.scale = (1, 1, 0.8)
    put(o, "red")
    box((0.55, -0.4, -0.85), (0.3, 0.1, 0.8), "red", (0, 0, math.radians(-10)))
    for i in range(3):
        box((0.55, -0.4, -0.5 + i * 0.01), (0.31, 0.11, 0.06), "white")
    box((0.55, -0.4, -1.2), (0.31, 0.11, 0.06), "navy")


def broken_harpoon():
    rot = (0, math.radians(90), math.radians(40))
    cyl((-0.55, 0, -0.45), 0.09, 1.3, "wood", (0, math.radians(90), math.radians(35)))
    cone((0.6, 0, 0.35), 0.28, 0.0, 0.8, "steel", (0, math.radians(90), math.radians(40)))
    cyl((0.15, 0, 0.05), 0.08, 0.5, "wood", (0, math.radians(90), math.radians(52)))
    box((0.78, 0.0, 0.5), (0.2, 0.05, 0.05), "steel", (0, 0, math.radians(40)))


def lighthouse_lamp():
    cyl((0, 0, -0.9), 0.7, 0.2, "navy")
    cyl((0, 0, -0.35), 0.4, 0.9, "white")
    cyl((0, 0, -0.55), 0.41, 0.2, "red")
    cyl((0, 0, 0.3), 0.55, 0.14, "navy")
    ball((0, 0, 0.7), 0.42, "gold")
    cone((0, 0, 1.2), 0.5, 0.0, 0.5, "red")


ICONS = (("StoryTravelerHat", traveler_hat), ("StoryRanchSignpost", ranch_signpost),
         ("StoryBoatmanScarf", boatman_scarf), ("StoryBrokenHarpoon", broken_harpoon),
         ("StoryLighthouseLamp", lighthouse_lamp))


def render(name, build):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    build()
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_cavity = True
    sh.show_shadows = False
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 3.6
    cam.data.clip_end = 100
    e = math.radians(62)
    cam.location = (0, -math.cos(e) * 20, math.sin(e) * 20)
    cam.rotation_euler = (math.radians(90) - e, 0, 0)
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


for n, b in ICONS:
    render(n, b)
