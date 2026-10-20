"""총 메시 obj → 아이템 아이콘 렌더 (M77-01 권총·소총 — 바르코 아이콘이 없는 총용).

  1) 렌더 (Blender 헤드리스 — 투명 배경 512x512, 옆 위 3/4):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_gun_icon.py -- PistolSemi_01
       → Saved/Generated/hat_icon/<Name>.png
  2) 외곽선·반입 길은 모자와 같다: `python3 tools/blender/render_hat_icon.py outline <Name> <item_id 접미>`
     → `tools/unreal/import_item_icons.py` → `create_data_tables.py`.

원본 = `Saved/Generated/<Name>.obj` (게임 메시 `SM_Weapon_<Name>` 내보내기).
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

ROOT = "~/UNREAL/Snarehaven/"
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
SIZE = 512


COLORS = {"Metal": (0.55, 0.58, 0.62, 1), "Dark": (0.2, 0.21, 0.24, 1), "Grip": (0.45, 0.28, 0.16, 1), "Hole": (0.05, 0.05, 0.05, 1)}


def main():
    name = sys.argv[sys.argv.index("--") + 1]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    bpy.ops.wm.obj_import(filepath=ROOT + "Saved/Generated/{}.obj".format(name))
    for m in bpy.data.materials:
        m.diffuse_color = COLORS.get(m.name.split('.')[0], (0.5, 0.5, 0.5, 1))
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
    sh.show_cavity = True
    sh.show_shadows = False
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(hi - lo) * 1.3
    ext = hi - lo
    # obj 는 위 = Y 로 들어와 얇은 축(폭)이 옆면 보는 방향이다 — 가장 얇은 축에서 보고 키 축으로 조금 든다
    order = sorted(range(3), key=lambda k: ext[k])
    view, tall = Vector(), Vector()
    view[order[0]] = 1
    tall[order[1]] = -1     # obj 의 키 축은 거꾸로 들어온다(손잡이가 위로 솟음)
    yaw, e = math.radians(15), math.radians(6)
    long_axis = Vector()
    long_axis[order[2]] = 1
    d = (view * math.cos(yaw) + long_axis * math.sin(yaw)) * math.cos(e) + tall * math.sin(e)
    d = d.normalized()
    cam.location = center + d * 4
    up = (tall - d * tall.dot(d)).normalized()
    cam.rotation_euler = Matrix((up.cross(d), up, d)).transposed().to_euler()    # 열 = 카메라 오른쪽·위·뒤
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath, "extent", tuple(ext))


main()
