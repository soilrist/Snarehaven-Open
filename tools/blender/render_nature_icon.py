"""늪 채집물 메시 → 아이템 아이콘 (M31-03 — `item.honey_mushroom` 등 6행 빈 칸).

  0) 게임 메시 원본(OBJ)을 만든다 — 게임 `SM_Nature_*` 와 같은 절차·같은 씨앗:
     python3 tools/unreal/make_tree_meshes.py   → Saved/Generated/SwampMushroom_01.obj 등
  1) 렌더 (Blender 헤드리스 — 투명 배경 512x512, 앞 위 3/4):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_nature_icon.py -- honey_mushroom
       → Saved/Generated/hat_icon/Nature_honey_mushroom.png
  2) 외곽선 — 모자 아이콘과 같은 후처리:
     python3 tools/blender/render_hat_icon.py outline Nature_honey_mushroom honey_mushroom
       → _Source/raw/Snare/Items/Icons/Icon_honey_mushroom.png  (반입은 `tools/unreal/import_item_icons.py`)

슬롯 색 = 게임 재질 색(`setup_zones.py`·`setup_ranch_buildings.py` 의 LinearColor 그대로).
게임은 버섯 넷이 한 메시·한 갓 색(`MI_Nature_Leafs_Resin`)이라 아이콘에선 갓 색만 종별로 바꾼다(🟡 실제 버섯 색 —
레퍼런스 자료 파일엔 Wild Growth 버섯이 없다). 수련은 물 웅덩이 판(`SwampWater`)을 빼고 잎·꽃만 찍는다.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = "~/UNREAL/Snarehaven/"
MESH_DIR = ROOT + "Saved/Generated/"
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
SIZE = 512
BARK = (0.200, 0.065, 0.028)    # MI_Nature_Leafs_Bark
BUSH = (0.025, 0.190, 0.045)    # MI_Nature_Leafs_Bush
SLOT_COLORS = {
    "Stem": BARK,
    "Leaves_Bush": BUSH,
    "Purple": (0.050, 0.020, 0.131),   # MI_Building_FlowerPurple
    "White": (0.700, 0.700, 0.662),    # MI_Building_FlowerWhite
    "Yellow": (0.733, 0.429, 0.012),   # MI_Building_FlowerYellow
}
# 버섯 갓 색 (선형) — 뽕나무버섯 = 게임 갓 호박색(MI_Nature_Leafs_Resin) 그대로(꿀색 갓), 턱받이버섯 = 포도주빛 적갈색(영명 Wine cap),
# 느타리 = 잿빛 베이지, 독사갈기버섯(전설) = 짙은 청록 — 넷이 52px 칸에서 색으로 갈리게(🟡 눈대중)
CAPS = {
    "honey_mushroom": (0.950, 0.360, 0.020),
    "king_stropharia": (0.300, 0.030, 0.030),
    "oyster_mushroom": (0.520, 0.480, 0.400),
    "vipers_mane": (0.020, 0.200, 0.160),
}
ITEMS = {**{k: "SwampMushroom_01" for k in CAPS}, "bog_flower": "BogFlower_01", "water_lily": "WaterLily_01"}


def main():
    item = sys.argv[sys.argv.index("--") + 1]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    bpy.ops.wm.obj_import(filepath=MESH_DIR + ITEMS[item] + ".obj", forward_axis="Y", up_axis="Z")
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in meshes:
        for mat in o.data.materials:
            slot = mat.name.split(".")[0]
            rgb = CAPS[item] if slot == "Resin" else SLOT_COLORS.get(slot, (1.0, 0.0, 1.0))
            mat.diffuse_color = rgb + (1.0,)
        if item == "water_lily":
            # 물 웅덩이 면 지우기 — 아이템은 잎·꽃만
            water = [i for i, m in enumerate(o.data.materials) if m.name.startswith("SwampWater")]
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.select_all(action="DESELECT")
            for i in water:
                o.active_material_index = i
                bpy.ops.object.material_slot_select()
            bpy.ops.mesh.delete(type="FACE")
            bpy.ops.object.mode_set(mode="OBJECT")
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
    # 버섯·늪꽃은 옆에서 조금 위(갓 밑 대·꽃대가 보이게), 수련은 물에 뜬 잎이라 더 위에서
    e = math.radians(55 if item == "water_lily" else 25)
    yaw = math.radians(-30)
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    cam.location = center + d * max(hi - lo) * 4
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    cam.data.clip_end = max(hi - lo) * 10
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + "Nature_" + item + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


main()
