"""금고 열쇠패 아이콘 렌더 (M104-08b4 — `item.vault_pass`). 기본 도형만으로 만든 청록 패 + 금빛 열쇠.

  1) 렌더 (Blender 헤드리스 — 투명 배경 512x512):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_vault_pass_icon.py
       → Saved/Generated/hat_icon/VaultPass.png
  2) 외곽선·반입: `python3 tools/blender/render_hat_icon.py outline VaultPass vault_pass`
     → `tools/unreal/import_item_icons.py` → `create_data_tables.py`.
외부 에셋 없음. 모양은 우리 것 — 카드키가 아니라 구멍 뚫린 패에 열쇠를 박은 모양(Reference 6절).
"""
import math
import os

import bpy

ROOT = "~/UNREAL/Snarehaven/"
OUT = ROOT + "Saved/Generated/hat_icon/VaultPass.png"
SIZE = 512


def mat(name, rgb):
    m = bpy.data.materials.new(name)
    m.diffuse_color = rgb + (1,)
    return m


def put(obj, m):
    obj.data.materials.append(m)
    return obj


def main():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    teal, dark, gold = mat("Plate", (0.12, 0.45, 0.48)), mat("Rim", (0.07, 0.25, 0.3)), mat("Key", (0.95, 0.72, 0.2))

    # 패: 둥근 판 + 가장자리 테
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=1.5, depth=0.25, location=(0, 0, 0))
    put(bpy.context.object, dark)
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=1.3, depth=0.32, location=(0, 0, 0.03))
    put(bpy.context.object, teal)

    # 열쇠: 고리(원통 둘) + 막대 + 이빨 둘 — 판 위에 비스듬히
    parts = []
    bpy.ops.mesh.primitive_torus_add(major_radius=0.34, minor_radius=0.1, location=(-0.5, 0.45, 0.28))
    parts.append(bpy.context.object)
    bpy.ops.mesh.primitive_cube_add(location=(0.1, 0.0, 0.28))
    shaft = bpy.context.object
    shaft.scale = (0.85, 0.09, 0.09)
    shaft.rotation_euler = (0, 0, math.radians(-38))
    parts.append(shaft)
    for t, off in ((0.55, 0.0), (0.85, 0.0)):
        bpy.ops.mesh.primitive_cube_add(location=(-0.5 + t * 1.15 + 0.02, 0.45 - t * 0.9 - 0.2, 0.28))
        tooth = bpy.context.object
        tooth.scale = (0.08, 0.2, 0.09)
        tooth.rotation_euler = (0, 0, math.radians(-38))
        parts.append(tooth)
    for p in parts:
        put(p, gold)

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
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sc.render.filepath = OUT
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", OUT)


main()
