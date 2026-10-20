"""총 아이콘 v2 — 권총·소총을 아이콘 전용으로 다시 지어 렌더 (INBOX 보정 2026-10-09 15:50).

게임 메시(`make_weapon_meshes.py`)는 상자 몇 개라 칸에서 자리표시처럼 보인다. 치수는 그 파일과 같게 두고,
아이콘에만 모서리 둥글림·슬라이드 홈·총열 끝·나무결·가늠쇠·방아쇠울·광택을 더한다. 소총은 대각선으로 눕혀 칸을 채운다.

  1) 렌더: ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_gun_icon_v2.py -- PistolSemi_01      (또는 RifleModern_01)
       → Saved/Generated/hat_icon/<Name>.png
  2) 외곽선·반입은 `render_gun_icon.py` 와 같다 (`render_hat_icon.py outline` → `import_item_icons.py` → `create_data_tables.py`).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = "~/UNREAL/Snarehaven/"
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
SIZE = 512

MAT = {
    "Metal": ((0.72, 0.75, 0.80, 1), 0.15, 0.35),
    "Dark": ((0.24, 0.25, 0.30, 1), 0.2, 0.45),
    "Blue": ((0.40, 0.44, 0.52, 1), 0.2, 0.40),
    "Wood": ((0.58, 0.34, 0.16, 1), 0.0, 0.60),
    "Grain": ((0.36, 0.19, 0.08, 1), 0.0, 0.70),
    "Brass": ((0.86, 0.66, 0.22, 1), 0.9, 0.30),
    "Lens": ((0.35, 0.65, 0.85, 1), 0.2, 0.15),
    "Green": ((0.20, 0.45, 0.20, 1), 0.1, 0.50),
    "Red": ((0.78, 0.22, 0.16, 1), 0.1, 0.45),
}
_mats = {}


def mat(name):
    if name not in _mats:
        m = bpy.data.materials.new(name)
        m.diffuse_color, m.metallic, m.roughness = MAT[name][0], MAT[name][1], MAT[name][2]
        _mats[name] = m
    return _mats[name]


def box(name, x0, x1, y0, y1, z0, z1, bevel=0.35, rot_y=0.0):
    bpy.ops.mesh.primitive_cube_add()
    o = bpy.context.active_object
    o.location = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    o.scale = ((x1 - x0) / 2, (y1 - y0) / 2, (z1 - z0) / 2)
    bpy.ops.object.transform_apply(scale=True)
    if rot_y:
        o.rotation_euler = (0, math.radians(rot_y), 0)
    if bevel:
        b = o.modifiers.new("b", "BEVEL")
        b.width = min(bevel, (x1 - x0) / 2.2, (z1 - z0) / 2.2, (y1 - y0) / 2.2)
        b.segments = 2
    o.data.materials.append(mat(name))
    return o


def tube(name, x0, x1, z, r, y=0.0, sides=16):
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides, radius=r, depth=x1 - x0)
    o = bpy.context.active_object
    o.rotation_euler = (0, math.radians(90), 0)
    o.location = ((x0 + x1) / 2, y, z)
    o.data.materials.append(mat(name))
    bpy.ops.object.shade_smooth()
    return o


def tilted(center, deg, objs):
    """objs 를 center(x, z) 둘레로 deg 만큼 돌린다 — 손잡이와 결 줄을 한 덩이로."""
    pv = bpy.data.objects.new("grp", None)
    bpy.context.scene.collection.objects.link(pv)
    pv.location = (center[0], 0, center[1])
    bpy.context.view_layer.update()
    for o in objs:
        mw = o.matrix_world.copy()
        o.parent = pv
        o.matrix_parent_inverse = pv.matrix_world.inverted()
    pv.rotation_euler = (0, math.radians(deg), 0)
    bpy.context.view_layer.update()
    return pv


def pistol():
    # 슬라이드(위쪽 쇠) + 앞뒤 홈 + 총구
    box("Metal", -5.0, 24.0, -3.2, 3.2, 5.0, 12.0, bevel=0.9)
    box("Dark", 16.0, 24.4, -3.35, 3.35, 5.3, 6.2, bevel=0.2)               # 슬라이드 앞 밑단 띠
    for i in range(6):                                                      # 뒤 홈(슬라이드 뒤 톱니)
        x = -4.2 + i * 1.4
        box("Dark", x, x + 0.6, -3.4, 3.4, 5.6, 11.6, bevel=0.0)
    box("Dark", 8.0, 15.0, -3.35, -3.0, 9.0, 10.6, bevel=0.1)               # 탄 빼는 창
    tube("Dark", 24.0, 26.0, 8.5, 1.9)
    box("Dark", 21.0, 23.0, -0.8, 0.8, 12.0, 14.0, bevel=0.3)               # 가늠쇠
    box("Dark", -4.5, -2.0, -1.8, 1.8, 12.0, 13.6, bevel=0.3)               # 가늠자
    # 틀 + 방아쇠울
    box("Blue", -3.5, 20.0, -2.9, 2.9, 2.0, 5.0, bevel=0.5)
    box("Blue", 3.0, 13.0, -1.0, 1.0, -2.0, 0.4, bevel=0.3)
    box("Blue", 11.2, 13.0, -1.0, 1.0, -2.0, 2.4, bevel=0.3)
    box("Dark", 5.5, 7.0, -0.8, 0.8, -0.2, 2.6, bevel=0.2)                  # 방아쇠
    # 나무 손잡이 + 줄무늬 (뒤로 17도 기운다)
    parts = [box("Wood", -3.5, 3.5, -3.4, 3.4, -8.0, 2.0, bevel=0.8)]
    for i in range(5):
        z = -6.8 + i * 1.8
        parts.append(box("Grain", -3.6, 3.6, -3.55, 3.55, z, z + 0.45, bevel=0.0))
    parts.append(box("Dark", -3.8, 3.8, -3.5, 3.5, -8.7, -7.9, bevel=0.3))    # 손잡이 밑판
    tilted((0, 2.0), 17, parts)
    for o in parts:
        o.location.x += 0.0


def rifle():
    # 나무 개머리 + 뒤판 + 결
    st = box("Wood", -30.0, -6.0, -4.5, 4.5, -9.0, 5.0, bevel=1.6)
    for i in range(7):
        z = -6.5 + i * 1.6
        box("Grain", -27.0 + i * 0.0, -7.0, -4.7, 4.7, z, z + 0.35, bevel=0.0)
    box("Dark", -30.8, -29.6, -4.7, 4.7, -9.2, 4.8, bevel=0.3)              # 개머리 뒤판
    # 손잡이(권총 쥠)
    g = box("Wood", -2.8, 2.8, -4.2, 4.2, -9.0, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    # 몸통 + 먼지 덮개 홈 + 장전 손잡이
    box("Metal", -6.0, 22.0, -4.8, 4.8, 0.0, 7.5, bevel=0.9)
    box("Dark", 4.0, 19.0, -4.95, -4.5, 2.5, 5.0, bevel=0.2)                # 배출구
    box("Dark", 10.0, 12.0, 4.5, 5.9, 3.5, 5.0, bevel=0.3)                  # 장전 손잡이
    # 덮개(나무) + 총열 + 총구 덮개 + 가늠쇠
    box("Wood", 22.0, 44.0, -4.5, 4.5, 0.5, 6.0, bevel=1.0)
    for i in range(4):
        box("Grain", 24.0 + i * 5.0, 24.7 + i * 5.0, -4.7, 4.7, 0.7, 5.8, bevel=0.0)
    tube("Dark", 22.0, 62.0, 4.5, 1.9)
    tube("Metal", 62.0, 66.0, 4.5, 2.5)
    box("Dark", 59.0, 61.0, -1.0, 1.0, 5.5, 8.5, bevel=0.2)
    # 탄창(앞으로 기운) + 방아쇠울
    t = box("Dark", 8.0, 14.0, -2.9, 2.9, -10.0, 0.5, bevel=0.5)
    tilted((11.0, 0.0), -10, [t])
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Blue", 6.8, 8.0, -1.4, 1.4, -2.3, 0.0, bevel=0.3)
    box("Dark", 4.0, 5.2, -1.1, 1.1, -2.5, 0.0, bevel=0.2)
    # 조준경: 받침 둘 + 통 + 앞뒤 굵은 입구 + 파란 렌즈
    box("Dark", 2.0, 4.0, -1.9, 1.9, 7.5, 9.0, bevel=0.2)
    box("Dark", 15.0, 17.0, -1.9, 1.9, 7.5, 9.0, bevel=0.2)
    tube("Dark", -1.0, 21.0, 11.0, 2.8)
    tube("Metal", 17.5, 22.5, 11.0, 3.5)
    tube("Metal", -3.5, 0.5, 11.0, 3.3)
    tube("Lens", 22.5, 22.9, 11.0, 3.0)
    tube("Lens", -3.9, -3.5, 11.0, 2.8)


def shotgun():
    # 나무 개머리(직선) + 결 + 뒤판
    box("Wood", -26.0, -5.0, -4.0, 4.0, -8.0, 4.0, bevel=1.6)
    for i in range(6):
        z = -5.5 + i * 1.6
        box("Grain", -24.0, -6.0, -4.2, 4.2, z, z + 0.35, bevel=0.0)
    box("Dark", -26.8, -25.6, -4.2, 4.2, -8.2, 3.8, bevel=0.3)
    g = box("Wood", -2.8, 2.8, -4.2, 4.2, -9.0, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    # 몸통 + 배출구
    box("Metal", -5.0, 16.0, -4.8, 4.8, 0.0, 7.5, bevel=0.9)
    box("Dark", 3.0, 13.0, -4.95, -4.5, 2.5, 5.0, bevel=0.2)
    # 굵은 총열 + 아래 탄창 통 + 나무 펌프 마디
    tube("Dark", 16.0, 58.0, 5.0, 3.2)
    tube("Metal", 58.0, 60.0, 5.0, 3.6)
    tube("Metal", 16.0, 45.0, 0.0, 2.6)
    box("Wood", 24.0, 40.0, -4.6, 4.6, -3.2, 3.2, bevel=1.0)
    for i in range(4):
        box("Grain", 26.0 + i * 3.5, 26.6 + i * 3.5, -4.8, 4.8, -2.8, 2.8, bevel=0.0)
    # 방아쇠울 + 가늠쇠
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Blue", 6.8, 8.0, -1.4, 1.4, -2.3, 0.0, bevel=0.3)
    box("Dark", 4.0, 5.2, -1.1, 1.1, -2.5, 0.0, bevel=0.2)
    box("Dark", 57.0, 59.0, -0.8, 0.8, 8.2, 10.0, bevel=0.2)


def crossbow():
    # 나무 개머리 + 손잡이 + 앞으로 뻗은 몸
    box("Wood", -22.0, -4.0, -3.2, 3.2, -5.0, 3.0, bevel=1.2)
    for i in range(5):
        z = -4.0 + i * 1.5
        box("Grain", -20.0, -5.0, -3.4, 3.4, z, z + 0.3, bevel=0.0)
    g = box("Wood", -4.0, 1.0, -3.6, 3.6, -9.0, -0.5, bevel=0.8)
    tilted((-1.5, -4.0), 12, [g])
    box("Wood", -4.0, 38.0, -3.0, 3.0, -1.0, 4.0, bevel=0.9)
    box("Dark", -2.0, 36.0, -0.8, 0.8, 4.0, 4.8, bevel=0.2)
    box("Metal", -4.0, 6.0, -3.5, 3.5, 4.0, 6.5, bevel=0.8)
    # 활 날개(쇠, 끝이 앞으로 휨) + 줄 + 화살
    for s in (-1.0, 1.0):
        box("Metal", 34.0, 38.0, min(3.0 * s, 14.0 * s), max(3.0 * s, 14.0 * s), 0.5, 3.5, bevel=0.6)
        box("Metal", 36.0, 40.0, min(14.0 * s, 24.0 * s), max(14.0 * s, 24.0 * s), 0.5, 3.0, bevel=0.6)
    box("Dark", 21.0, 21.8, -24.0, 24.0, 1.6, 2.4, bevel=0.0)
    box("Metal", 38.0, 44.0, -1.2, 1.2, -1.0, 2.0, bevel=0.4)
    tube("Wood", 6.0, 46.0, 5.4, 0.5)
    box("Blue", 2.5, 6.0, -1.2, 1.2, -3.5, -2.5, bevel=0.3)


def pistol_auto():
    pistol()
    # 아이콘 권총은 게임 메시(×1.406)의 그린 치수라 확장 탄창·보정기도 같은 판으로 — 손잡이와 같이 17도 기운다
    mag = [box("Dark", -3.6, 3.6, -2.0, 2.0, -15.0, -7.9, bevel=0.5)]
    for i in range(3):
        mag.append(box("Metal", -3.7, 3.7, -2.1, 2.1, -13.6 + i * 1.8, -13.3 + i * 1.8, bevel=0.0))
    tilted((0, 2.0), 17, mag)
    box("Metal", 23.0, 30.0, -2.1, 2.1, 8.5, 12.1, bevel=0.5)                 # 앞 보정기
    box("Dark", 25.5, 27.0, -2.2, 2.2, 9.2, 11.4, bevel=0.2)


def smg():
    box("Metal", -22.0, -6.0, -0.9, 0.9, 1.0, 2.8, bevel=0.3)                 # 접은 어깨대
    box("Metal", -23.0, -21.0, -3.5, 3.5, -4.0, 4.0, bevel=0.4)
    g = box("Wood", -3.0, 2.5, -3.6, 3.6, -10.0, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    box("Metal", -6.0, 24.0, -4.4, 4.4, 0.0, 7.0, bevel=0.9)
    box("Dark", 4.0, 19.0, -4.55, -4.2, 2.5, 5.0, bevel=0.2)                  # 배출구
    box("Dark", 24.0, 34.0, -3.2, 3.2, 1.5, 6.0, bevel=0.6)
    for i in range(4):
        box("Metal", 25.0 + i * 2.4, 25.8 + i * 2.4, -3.35, 3.35, 2.0, 5.5, bevel=0.0)
    tube("Dark", 34.0, 49.0, 4.0, 1.8)
    box("Dark", 12.0, 17.0, -2.6, 2.6, -16.0, 0.0, bevel=0.5)                 # 곧은 긴 탄창
    box("Wood", 27.0, 32.0, -2.4, 2.4, -8.0, 1.5, bevel=0.6)
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Dark", 46.0, 48.0, -0.8, 0.8, 5.5, 7.5, bevel=0.2)


def rifle_burst():
    box("Metal", -20.0, -8.0, -3.4, 3.4, -5.0, 3.0, bevel=0.6)                # 쇠 어깨받침
    box("Dark", -20.8, -19.6, -3.6, 3.6, -5.2, 3.2, bevel=0.3)
    g = box("Wood", -2.8, 2.8, -3.8, 3.8, -9.5, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    box("Metal", -9.0, 34.0, -4.8, 4.8, 0.0, 8.0, bevel=1.0)
    box("Dark", 4.0, 28.0, -4.95, -4.5, 2.5, 5.5, bevel=0.2)                  # 배출구
    box("Dark", -17.0, -9.5, -3.2, 3.2, -14.0, 0.0, bevel=0.6)                # 손잡이 뒤 탄창
    for i in range(3):
        box("Metal", -17.1, -9.4, -3.3, 3.3, -12.0 + i * 3.5, -11.6 + i * 3.5, bevel=0.0)
    tube("Dark", 34.0, 55.0, 4.5, 1.9)
    box("Dark", 34.0, 46.0, -3.4, 3.4, 1.0, 6.5, bevel=0.6)
    box("Dark", 2.0, 4.0, -1.5, 1.5, 8.0, 12.0, bevel=0.2)                    # 운반 손잡이 아치
    box("Dark", 22.0, 24.0, -1.5, 1.5, 8.0, 12.0, bevel=0.2)
    box("Dark", 2.0, 24.0, -1.8, 1.8, 12.0, 14.0, bevel=0.5)
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Dark", 52.0, 54.0, -0.8, 0.8, 6.0, 8.5, bevel=0.2)


def rifle_semi():
    st = box("Wood", -34.0, -6.0, -3.8, 3.8, -10.0, 6.0, bevel=1.6)
    for i in range(7):
        z = -7.0 + i * 1.7
        box("Grain", -30.0, -7.0, -4.0, 4.0, z, z + 0.35, bevel=0.0)
    box("Dark", -34.8, -33.6, -4.0, 4.0, -10.2, 5.8, bevel=0.3)               # 개머리 뒤판
    g = box("Wood", -2.8, 2.8, -3.6, 3.6, -9.0, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    box("Metal", -6.0, 20.0, -3.8, 3.8, 0.0, 7.0, bevel=0.9)
    box("Dark", 4.0, 16.0, -3.95, -3.5, 2.5, 5.0, bevel=0.2)                  # 배출구
    box("Wood", 20.0, 52.0, -3.2, 3.2, 1.0, 5.5, bevel=1.0)
    for i in range(5):
        box("Grain", 22.0 + i * 6.0, 22.7 + i * 6.0, -3.4, 3.4, 1.2, 5.3, bevel=0.0)
    tube("Dark", 20.0, 71.0, 4.5, 1.6)
    tube("Metal", 69.0, 72.0, 4.5, 2.1)
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Dark", 66.0, 68.0, -0.8, 0.8, 5.5, 8.5, bevel=0.2)
    box("Dark", 14.0, 17.0, -1.6, 1.6, 7.0, 8.6, bevel=0.2)


def shotgun_auto():
    box("Wood", -22.0, -5.0, -3.8, 3.8, -6.0, 4.0, bevel=1.6)
    for i in range(5):
        z = -4.0 + i * 1.6
        box("Grain", -20.0, -6.0, -4.0, 4.0, z, z + 0.35, bevel=0.0)
    box("Dark", -22.8, -21.6, -4.0, 4.0, -6.2, 3.8, bevel=0.3)
    g = box("Wood", -2.8, 2.8, -3.8, 3.8, -9.0, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    box("Metal", -5.0, 24.0, -4.8, 4.8, 0.0, 7.5, bevel=0.9)
    box("Dark", 3.0, 18.0, -4.95, -4.5, 2.5, 5.0, bevel=0.2)
    tube("Dark", 10.0, 22.0, -5.0, 5.5)                                       # 아래 큰 탄창통
    tube("Metal", 21.0, 22.5, -5.0, 5.9)
    tube("Dark", 24.0, 60.0, 4.5, 2.8)
    tube("Metal", 60.0, 62.0, 4.5, 3.2)
    box("Metal", 24.0, 50.0, -4.4, 4.4, 1.5, 8.5, bevel=1.0)                  # 방열 덮개
    for i in range(5):
        box("Dark", 27.0 + i * 5.0, 28.0 + i * 5.0, -4.6, 4.6, 7.0, 8.7, bevel=0.0)
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Dark", 58.0, 60.0, -0.8, 0.8, 8.0, 10.0, bevel=0.2)


def shotgun_short():
    g = box("Wood", -10.0, -2.0, -3.6, 3.6, -9.0, 0.5, bevel=1.0)
    tilted((-1.0, 0.5), 14, [g])
    box("Wood", -12.0, -6.0, -3.6, 3.6, -9.5, -4.0, bevel=1.2)                # 손잡이 뒤꼭지
    box("Metal", -8.0, 12.0, -5.0, 5.0, 0.0, 7.0, bevel=0.9)
    box("Dark", 0.0, 8.0, -5.15, -4.7, 2.0, 5.0, bevel=0.2)
    tube("Dark", 12.0, 41.0, 3.5, 2.7, y=-2.8)                                # 통 둘 나란히
    tube("Dark", 12.0, 41.0, 3.5, 2.7, y=2.8)
    tube("Metal", 41.0, 42.5, 3.5, 3.0, y=-2.8)
    tube("Metal", 41.0, 42.5, 3.5, 3.0, y=2.8)
    box("Metal", 24.0, 27.0, -5.6, 5.6, 0.2, 6.8, bevel=0.5)                  # 묶음띠
    box("Blue", 0.5, 6.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)


def machinegun():
    box("Metal", -24.0, -8.0, -3.6, 3.6, -5.0, 4.0, bevel=0.8)
    g = box("Wood", -2.8, 2.8, -3.8, 3.8, -9.5, 0.5, bevel=0.8)
    tilted((-1.0, 0.5), 14, [g])
    box("Metal", -8.0, 36.0, -5.0, 5.0, 0.0, 8.5, bevel=1.0)
    box("Dark", 4.0, 30.0, -5.15, -4.7, 2.5, 6.0, bevel=0.2)                  # 배출구
    box("Dark", 6.0, 24.0, -12.0, -5.0, -6.0, 6.0, bevel=0.8)                 # 옆 탄띠통(앞에서 보이는 쪽)
    for i in range(4):
        box("Brass", 7.0 + i * 4.2, 9.5 + i * 4.2, -12.1, -11.8, -4.5, 4.5, bevel=0.0)
    box("Dark", 36.0, 62.0, -3.6, 3.6, 1.0, 7.5, bevel=0.8)
    for i in range(5):
        box("Metal", 39.0 + i * 5.0, 40.0 + i * 5.0, -3.8, 3.8, 6.8, 7.7, bevel=0.0)
    tube("Dark", 62.0, 86.0, 4.5, 2.0)
    tube("Metal", 84.0, 87.0, 4.5, 2.5)
    box("Metal", 58.0, 60.0, -4.5, -3.0, -10.0, 1.0, bevel=0.2)               # 두발
    box("Metal", 58.0, 60.0, 3.0, 4.5, -10.0, 1.0, bevel=0.2)
    box("Dark", 4.0, 6.0, -1.5, 1.5, 8.5, 12.5, bevel=0.2)                    # 운반 손잡이 아치
    box("Dark", 26.0, 28.0, -1.5, 1.5, 8.5, 12.5, bevel=0.2)
    box("Dark", 4.0, 28.0, -1.8, 1.8, 12.5, 14.5, bevel=0.5)
    box("Blue", 2.5, 8.0, -1.4, 1.4, -3.5, -2.3, bevel=0.3)
    box("Dark", 82.0, 84.0, -0.8, 0.8, 6.0, 8.5, bevel=0.2)


def bazooka():
    """바주카 — 게임 메시(`make_weapon_meshes.py bazooka`)와 같은 치수: 초록 발사관 + 앞뒤 어두운 입 + 띠 둘 + 손잡이 둘 + 가늠쇠."""
    tube("Green", -34.0, 62.0, 7.0, 5.5)
    tube("Dark", -40.0, -34.0, 7.0, 6.8)
    tube("Dark", 62.0, 68.0, 7.0, 6.8)
    tube("Metal", 8.0, 11.0, 7.0, 6.1)
    tube("Metal", 40.0, 43.0, 7.0, 6.1)
    box("Metal", -8.0, 38.0, -2.5, 2.5, 0.0, 2.0, bevel=0.5)
    parts = [box("Wood", -3.0, 2.0, -1.8, 1.8, -9.5, 1.5, bevel=0.6)]
    tilted((-1.0, 0.0), 12, parts)
    box("Dark", 2.0, 8.0, -1.4, 1.4, -1.0, 0.0, bevel=0.2)
    box("Dark", 30.0, 35.0, -1.8, 1.8, -8.0, 1.5, bevel=0.5)
    box("Dark", 50.0, 52.0, -0.8, 0.8, 12.5, 16.0, bevel=0.2)
    box("Dark", -22.0, -20.0, -0.8, 0.8, 12.5, 15.0, bevel=0.2)


def rocket():
    """로켓 탄 — 빨간 뾰족 머리 + 흰(쇠) 몸통 + 뒤 꼬리날개 넷."""
    tube("Metal", -14.0, 8.0, 0.0, 3.2)
    bpy.ops.mesh.primitive_cone_add(vertices=16, radius1=3.2, radius2=0.0, depth=9.0)
    o = bpy.context.active_object
    o.rotation_euler = (0, math.radians(90), 0)
    o.location = (12.5, 0.0, 0.0)
    o.data.materials.append(mat("Red"))
    bpy.ops.object.shade_smooth()
    tube("Dark", -16.0, -14.0, 0.0, 2.4)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        f = box("Red", -15.0, -8.0, -0.4, 0.4, 2.6, 6.8, bevel=0.0)
        f.rotation_euler = (a - math.radians(90), 0, 0)
        f.location = (0, 0, 0)
        # 축 둘레로 돌리려고 원점을 몸통 중심에 둔다
        bpy.context.view_layer.objects.active = f
        bpy.ops.object.origin_set(type="ORIGIN_CURSOR")


def main():
    name = sys.argv[sys.argv.index("--") + 1]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    {"Pistol": pistol, "Shotgu": shotgun, "Crossb": crossbow, "PistolAuto_01": pistol_auto, "Smg_01": smg,
     "RifleBurst_01": rifle_burst, "RifleSemi_01": rifle_semi, "ShotgunAuto_01": shotgun_auto, "ShotgunShort_01": shotgun_short,
     "MachineGun_01": machinegun, "Bazooka_01": bazooka, "Rocket_01": rocket}.get(
        name if name in ("PistolAuto_01", "Smg_01", "RifleBurst_01", "RifleSemi_01", "ShotgunAuto_01", "ShotgunShort_01", "MachineGun_01", "Bazooka_01", "Rocket_01") else name[:6], rifle)()
    pivot = bpy.data.objects.new("pivot", None)
    bpy.context.scene.collection.objects.link(pivot)
    for o in list(bpy.data.objects):
        if o is not pivot and o.parent is None:
            o.parent = pivot
    # 소총은 앞이 오른쪽 위로 가는 대각선(칸 채우기), 권총은 살짝만 든다
    tilt = 24 if name.startswith(("Rifle", "Shotgun", "MachineGun", "Bazooka", "Rocket")) else 14 if name.startswith("Smg") else 4
    pivot.rotation_euler = (0, math.radians(-tilt), 0)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in bpy.data.objects:
        if o.type == "MESH":
            ev = o.evaluated_get(dg)
            pts += [o.matrix_world @ v.co for v in ev.to_mesh().vertices]
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    center = (lo + hi) / 2
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_specular_highlight = True
    sh.show_cavity = True
    sh.cavity_type = "BOTH"
    sh.show_shadows = False
    sc.render.film_transparent = True
    sc.view_settings.view_transform = "Standard"
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(hi.x - lo.x, hi.z - lo.z, hi.y - lo.y if name.startswith("Crossbow") else 0) * 1.25
    yaw, e = math.radians(-12), math.radians(52 if name.startswith("Crossbow") else 14)   # 옆면을 앞에서 조금 비스듬히 위에서
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    cam.location = center + d * 200
    cam.data.clip_end = 1000
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


main()
