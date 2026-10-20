"""들판 광석 바위 겉모습 다시 입히기 (INBOX 보정 10-13 「보통 바위와 구분 안 됨」) — `setup_field_ore.py` 로 세운 `FieldOre_*` 에 적용.

실행: setup_field_ore.py 와 같은 명령줄에서 -script 만 이 파일로.

바위 몸통을 보통 바위(밝은 회색)와 다른 어두운 슬레이트 갈회색 머티리얼(`MI_Nature_OreBody`)로 덮고, 옛 알갱이(`_Nugget`)를 지운 뒤
바위 위쪽 절반에 큰 결정 덩이 `CRYSTALS` 개를 종류 색으로 박는다. 덩이는 몸통에 붙여 바위가 숨으면 같이 숨는다. 재실행 안전(옛 덩이를 다 지우고 다시 세움).
"""
import math
import os
import random
import sys

import unreal

MAP = "/Game/Game/Maps/L_Hunt"
NATURE_DIR = "/Game/Game/Environment/Nature"
PEBBLE = NATURE_DIR + "/SM_Nature_Pebble_01"
LABEL = "FieldOre_"
SEED = 1087
CRYSTALS = 4
CRYSTAL_CM = 62.0      # 바위 지름(약 130cm)의 1/4 이상, 카메라에서 한눈에 (🟡)
BODY_COLOR = unreal.LinearColor(0.05, 0.055, 0.07, 1.0)   # 어두운 슬레이트 갈회색
KINDS = {   # 종류 → 결정 색 (선형)
    "Coal": unreal.LinearColor(0.0015, 0.0015, 0.002, 1.0),     # 석탄 검정
    "Copper": unreal.LinearColor(0.85, 0.20, 0.03, 1.0),         # 구리 주황
    "Iron": unreal.LinearColor(0.28, 0.07, 0.04, 1.0),          # 철 붉은 갈색
    "Titanium": unreal.LinearColor(0.0, 0.60, 0.55, 1.0),       # 티타늄 청록
    "Silver": unreal.LinearColor(0.85, 0.92, 1.0, 1.0),         # 은 흰 푸른
    "Gold": unreal.LinearColor(1.0, 0.72, 0.0, 1.0),            # 금 노랑
}

subs = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def make_mi(name, parent_path, color, floor):
    path = NATURE_DIR + "/" + name
    mat = lib.load_asset(path) if lib.does_asset_exist(path) else \
        tools.create_asset(name, NATURE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mat, lib.load_asset(parent_path))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, "DiffuseColor", color)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat, "AmbientFloor", floor)
    lib.save_asset(path, only_if_is_dirty=False)
    return mat


body_mat = make_mi("MI_Nature_OreBody", NATURE_DIR + "/M_Nature_MatteFade", BODY_COLOR, 0.12)
mats = {k: make_mi("MI_Nature_Ore_" + k, NATURE_DIR + "/M_Nature_Matte", c, 0.55) for k, c in KINDS.items()}

for a in list(subs.get_all_level_actors()):
    if a.get_actor_label().startswith(LABEL) and "_Nugget" in a.get_actor_label():
        subs.destroy_actor(a)

pebble = lib.load_asset(PEBBLE)
pbox = pebble.get_bounding_box()
pscale = CRYSTAL_CM / max(float(pbox.max.x - pbox.min.x), float(pbox.max.y - pbox.min.y), float(pbox.max.z - pbox.min.z))
rng = random.Random(SEED)
rocks = 0
for ore in subs.get_all_level_actors():
    label = ore.get_actor_label()
    if not label.startswith(LABEL) or "_Nugget" in label:
        continue
    kind = label.split("_")[1]
    body = ore.get_components_by_class(unreal.StaticMeshComponent)[0]
    body.set_material(0, body_mat)
    loc = ore.get_actor_location()
    sc = float(ore.get_actor_scale3d().x)
    box = body.static_mesh.get_bounding_box()
    height = float(box.max.z - box.min.z) * sc
    radius = 0.5 * max(float(box.max.x - box.min.x), float(box.max.y - box.min.y)) * sc
    yaw = rng.uniform(0.0, 360.0)
    for k in range(CRYSTALS):
        ang = math.radians(yaw + k * 360.0 / CRYSTALS + rng.uniform(-20.0, 20.0))
        h = height * rng.uniform(0.62, 0.90)                 # 위쪽 절반
        r = radius * (0.80 - 0.55 * (h / height - 0.5))        # 높을수록 안쪽으로 (둥근 윗면)
        nug = subs.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(loc.x + r * math.cos(ang), loc.y + r * math.sin(ang), loc.z + h),
                                          unreal.Rotator(rng.uniform(-25.0, 25.0), rng.uniform(0.0, 360.0), 0.0))
        nug.set_actor_label("{}_Nugget{}".format(label, k))
        nug.set_actor_scale3d(unreal.Vector(pscale, pscale, pscale * 1.35))   # 위로 길쭉한 결정 모양
        nc = nug.static_mesh_component
        nc.set_static_mesh(pebble)
        nc.set_material(0, mats[kind])
        nc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        nc.set_mobility(unreal.ComponentMobility.MOVABLE)
        nug.attach_to_actor(ore, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    rocks += 1

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log_warning("[FieldOre] 다시 입힘: 바위 {} 개 · 덩이 {} 개씩".format(rocks, CRYSTALS))
