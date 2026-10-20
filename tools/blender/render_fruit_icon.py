"""과일·고구마 아이템 아이콘 (아이콘 빈 칸 7개 — `item.sweet_potato` `item.sweet_potato_rotten` `item.pear` `item.peach` `item.cherry` `item.orange` `item.plum`).

  1) 렌더 (Blender 헤드리스 — 기본 도형으로 짠 투명 배경 512x512, 앞 위 3/4):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_fruit_icon.py -- pear
       → Saved/Generated/hat_icon/Fruit_pear.png
  2) 외곽선 — 모자 아이콘과 같은 후처리:
     python3 tools/blender/render_hat_icon.py outline Fruit_pear pear
       → _Source/raw/Snare/Items/Icons/Icon_pear.png  (반입은 `tools/unreal/import_item_icons.py`)

색은 선형 값이 아니라 눈대로 고른 표시 색이다(🟡 레퍼런스 자료 파일에 없음). 도형은 우리 것: 배 = 두 공을 겹친 호리병, 체리 = 줄기가 한 점에서 갈라진 쌍.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

OUT_DIR = "~/UNREAL/Snarehaven/Saved/Generated/hat_icon/"
SIZE = 512
STEM = (0.20, 0.10, 0.04)
LEAF = (0.08, 0.35, 0.08)


def blob(loc, scale, rgb, rot=(0, 0, 0), seg=48):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=seg // 2, location=loc, rotation=rot)
    o = bpy.context.object
    o.scale = scale
    bpy.ops.object.shade_smooth()
    mat = bpy.data.materials.new("m")
    mat.diffuse_color = rgb + (1.0,)
    o.data.materials.append(mat)
    return o


def stick(a, b, rgb, r=0.035):
    a, b = Vector(a), Vector(b)
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=(b - a).length, location=(a + b) / 2)
    o = bpy.context.object
    o.rotation_euler = (b - a).to_track_quat("Z", "Y").to_euler()
    mat = bpy.data.materials.new("m")
    mat.diffuse_color = rgb + (1.0,)
    o.data.materials.append(mat)
    return o


def leaf(loc, rot, size=0.32):
    return blob(loc, (size, size * 0.45, size * 0.08), LEAF, rot=rot, seg=24)


def build(item):
    if item == "sweet_potato":
        blob((0, 0, 0), (1.25, 0.5, 0.5), (0.52, 0.12, 0.22), rot=(0, 0, math.radians(15)))
        blob((-1.0, -0.1, 0), (0.45, 0.28, 0.28), (0.52, 0.12, 0.22), rot=(0, 0, math.radians(15)))
        blob((0.1, -0.2, 0.28), (0.5, 0.12, 0.1), (0.78, 0.30, 0.34), rot=(0, 0, math.radians(15)))
    elif item == "sweet_potato_rotten":
        c = (0.28, 0.18, 0.13)
        blob((0, 0, 0), (1.2, 0.5, 0.42), c, rot=(0, 0, math.radians(15)))
        blob((-0.95, -0.1, -0.05), (0.42, 0.26, 0.24), c, rot=(0, 0, math.radians(15)))
        for x, y in ((0.3, -0.2), (-0.3, -0.15), (0.7, 0.05)):
            blob((x, y, 0.3), (0.2, 0.15, 0.08), (0.30, 0.38, 0.14), seg=16)   # 곰팡이 반점
        blob((0.0, -0.25, 0.3), (0.16, 0.12, 0.07), (0.07, 0.05, 0.04), seg=16)
    elif item == "pear":
        c = (0.62, 0.72, 0.16)
        blob((0, 0, -0.25), (0.8, 0.8, 0.75), c)
        blob((0, 0, 0.55), (0.5, 0.5, 0.6), c)
        stick((0, 0, 1.0), (0.12, 0, 1.45), STEM)
        leaf((0.3, 0, 1.25), (0, math.radians(-25), 0))
    elif item == "peach":
        blob((0, 0, 0), (0.9, 0.9, 0.85), (0.92, 0.22, 0.20))
        stick((0, 0, 0.8), (0.05, 0, 1.15), STEM)
        leaf((0.3, 0, 1.0), (0, math.radians(-25), 0))
    elif item == "cherry":
        c = (0.70, 0.04, 0.10)
        blob((-0.55, 0, -0.5), (0.5, 0.5, 0.48), c)
        blob((0.55, 0, -0.6), (0.5, 0.5, 0.48), c)
        stick((-0.55, 0, -0.05), (0.0, 0, 1.0), STEM, 0.03)
        stick((0.55, 0, -0.15), (0.0, 0, 1.0), STEM, 0.03)
        leaf((0.25, 0, 0.95), (0, math.radians(-20), 0), 0.3)
    elif item == "orange":
        blob((0, 0, 0), (0.9, 0.9, 0.86), (0.98, 0.50, 0.04))
        blob((0, 0, 0.84), (0.16, 0.16, 0.06), (0.35, 0.40, 0.08), seg=16)
        leaf((0.3, 0, 0.9), (0, math.radians(-25), 0))
    elif item == "plum":
        blob((0, 0, 0), (0.85, 0.85, 0.8), (0.34, 0.08, 0.40))
        blob((-0.3, -0.5, 0.2), (0.3, 0.2, 0.3), (0.55, 0.30, 0.60))   # 희뿌연 분
        stick((0, 0, 0.75), (0.05, 0, 1.1), STEM)
        leaf((0.28, 0, 0.95), (0, math.radians(-25), 0))
    else:
        raise SystemExit("모르는 이름: " + item)


def main():
    item = sys.argv[sys.argv.index("--") + 1]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    build(item)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    center = (lo + hi) / 2
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_cavity = False
    sh.show_shadows = False
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(hi - lo) * 1.3
    e = math.radians(25)
    yaw = math.radians(-20)
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    cam.location = center + d * max(hi - lo) * 4
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    cam.data.clip_end = max(hi - lo) * 10
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + "Fruit_" + item + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


main()
