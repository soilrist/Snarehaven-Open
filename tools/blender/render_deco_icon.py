"""목장 꾸미기 메시 obj → 아이템 아이콘 렌더 (M77-03 가챠 꾸미기 — 바르코 아이콘이 없는 스킨용).

  1) 렌더 (Blender 헤드리스 — 투명 배경 512x512, 앞 위 3/4):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_deco_icon.py -- Fence <출력 이름> [재질=R,G,B ...]
       → Saved/Generated/hat_icon/<출력 이름>.png
  2) 외곽선·반입 길은 모자와 같다: `python3 tools/blender/render_hat_icon.py outline <출력 이름> <item_id 접미>`
     → `tools/unreal/import_item_icons.py` → `create_data_tables.py`.

원본 = `_Source/raw/Quaternius_FarmBuildings/unzipped/OBJ/<파일>.obj` (게임 메시 `import_ranch_buildings.py` 와 같은 원본).
첫 인자가 `.obj` 로 끝나면 저장소 기준 경로의 우리 메시 원본(예 `Saved/Generated/WallClock_01.obj` — M108-02 벽 장식)을 Z 위로 읽는다.
재질 인자 = 게임에서 스킨이 바꾸는 칸 색(예: 붉은 지붕 헛간 `RoofBlack=0.55,0.1,0.08`).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = "~/UNREAL/Snarehaven/"
SRC_DIR = ROOT + "_Source/raw/Quaternius_FarmBuildings/unzipped/OBJ/"
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
SIZE = 512


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    src, name = args[0], args[1]
    over = {}
    for a in args[2:]:
        k, v = a.split("=")
        over[k] = tuple(float(x) for x in v.split(",")) + (1,)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    if src.endswith(".obj"):   # 우리 메시 원본(`tools/unreal/make_*_meshes.py`) — Z 위·앞 -Y 로 그렸다
        bpy.ops.wm.obj_import(filepath=ROOT + src, up_axis="Z", forward_axis="Y")
    else:
        bpy.ops.wm.obj_import(filepath=SRC_DIR + src + ".obj")
    for m in bpy.data.materials:
        base = m.name.split('.')[0]
        if base in over:
            m.diffuse_color = over[base]
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
    e, yaw = math.radians(35), math.radians(float(os.environ.get("DECO_YAW", "30")))
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    # 거리 = 물건 크기의 2배(최소 40) — 집 가구(긴 변 190, M108-04)는 40 이면 카메라가 물건 안에 들어가 앞이 잘린다
    reach = max(40.0, max(hi - lo) * 2.0)
    cam.location = center + d * reach
    cam.data.clip_end = max(500.0, reach * 3.0)
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath, "extent", tuple(hi - lo), [(m.name, tuple(m.diffuse_color)) for m in bpy.data.materials])


main()
