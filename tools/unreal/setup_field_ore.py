"""DESIGN 8.16 「들판 광석 바위」 — `L_Hunt` 들판 바위(HISM 묶음) 일부를 곡괭이로 캐는 광석 바위로 바꾼다 (INBOX 지시 10-13).

실행 (절대경로 — `fix_rock_collision.py` 머리말과 같은 명령줄에서 -script 만 이 파일로).

시작 거점(`haven_outpost`)에서 `NEAR_MIN`~`NEAR_MAX` cm 안의 둥근 바위(`SM_Nature_Boulder`)를 종류마다 `PER_KIND` 개씩 골라
묶음에서 빼고 그 자리에 `GatherableActor`(행 `Gatherable_OreRock<종류>`)를 세운다. 겉에는 광석 알갱이(`SM_Nature_Pebble_01`)를
종류 색으로 박아 붙인다 — 바위가 숨으면(무너지면) 알갱이도 같이 숨는다. 재실행 안전: 이미 `FieldOre_` 가 있으면 1단계는 안 한다
(뺀 바위를 되돌릴 수 없어서 — 처음부터 다시 하려면 맵을 되돌린 뒤 돌린다).

2단계(INBOX 지시 10-13 「광석 바위를 사냥터 전체로」, DESIGN 8.16 「지역별 분포」): 시작 거점에서 먼 곳일수록 귀한 광석 —
`FieldOre_<종류>_F<n>` 를 `ZONE_PLAN` 표대로 맵 전체 바위에서 골라 더한다. 이미 `_F` 가 있으면 안 한다. 겉모습은 `restyle_field_ore.py` 를 이어서 돌린다.
"""
import math
import os
import random
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands   # noqa: E402  — START (시작 거점 좌표)

MAP = "/Game/Game/Maps/L_Hunt"
NATURE_DIR = "/Game/Game/Environment/Nature"
MATTE_PARENT = NATURE_DIR + "/M_Nature_Matte"
PEBBLE = NATURE_DIR + "/SM_Nature_Pebble_01"
ROCK_MESH_PREFIX = "SM_Nature_Boulder"
ORE_BODY_MESH = NATURE_DIR + "/SM_Nature_Boulder_02"   # 행 `MeshRef` 와 같아야 한다
BODY_HEIGHT_CM = 110.0                                  # 행 `BodyHeightCm` 와 같아야 한다
LABEL = "FieldOre_"
SEED = 1086
PER_KIND = 2
NEAR_MIN, NEAR_MAX = 2500.0, 12000.0
NUGGETS = 6
NUGGET_CM = 44.0
# 2단계 지역별 분포 (DESIGN 8.16): 종류 → (개수, 시작 거점에서 거리 cm 범위, 설원 허용)
FAR_PLAN = {
    "Coal": (8, (2500.0, 25000.0), False), "Copper": (8, (2500.0, 25000.0), False), "Iron": (8, (2500.0, 25000.0), False),
    "Silver": (8, (25000.0, 45000.0), True), "Gold": (5, (25000.0, 45000.0), True), "Titanium": (5, (45000.0, 1.0e9), False),
}
# 종류 → (행 이름 끝, 알갱이 색 — 선형, 바닥 밝기 🟡)
KINDS = [
    ("Coal", unreal.LinearColor(0.012, 0.012, 0.014, 1.0)),      # 석탄 검정
    ("Copper", unreal.LinearColor(0.60, 0.22, 0.06, 1.0)),       # 구리 주황
    ("Iron", unreal.LinearColor(0.34, 0.26, 0.22, 1.0)),         # 철 회갈
    ("Titanium", unreal.LinearColor(0.55, 0.62, 0.70, 1.0)),     # 티타늄 푸른 은회
    ("Silver", unreal.LinearColor(0.80, 0.82, 0.86, 1.0)),       # 은 밝은 회백
    ("Gold", unreal.LinearColor(0.95, 0.62, 0.05, 1.0)),         # 금 노랑
]

subs = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
labels = [a.get_actor_label() for a in subs.get_all_level_actors()]
do_near = not any(l.startswith(LABEL) for l in labels)
do_far = not any(re.match(r"^FieldOre_[A-Za-z]+_F\d+$", l) for l in labels)
if not (do_near or do_far):
    unreal.log_warning("[FieldOre] 이미 있음 — 건드리지 않는다")
    raise SystemExit(0)

parent = unreal.EditorAssetLibrary.load_asset(MATTE_PARENT)
tools = unreal.AssetToolsHelpers.get_asset_tools()
mats = {}
for kind, color in KINDS:
    path = NATURE_DIR + "/MI_Nature_Ore_" + kind
    mat = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else \
        tools.create_asset("MI_Nature_Ore_" + kind, NATURE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mat, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, "DiffuseColor", color)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat, "AmbientFloor", 0.3)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    mats[kind] = mat

# 후보: 맵 전체 둥근 바위 인스턴스 (시작 거점에서 거리 포함)
sx, sy = islands.START
all_rocks = []
for actor in subs.get_all_level_actors():
    for comp in actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
        if comp.static_mesh is None or not comp.static_mesh.get_name().startswith(ROCK_MESH_PREFIX):
            continue
        for i in range(comp.get_instance_count()):
            t = comp.get_instance_transform(i, True)
            t = t[1] if isinstance(t, tuple) else t
            all_rocks.append((comp, i, t, math.hypot(t.translation.x - sx, t.translation.y - sy)))
rng = random.Random(SEED)
picked = []   # (comp, i, t, 종류, 라벨 접미)
if do_near:
    candidates = [r for r in all_rocks if NEAR_MIN <= r[3] <= NEAR_MAX]
    rng.shuffle(candidates)
    near = candidates[:PER_KIND * len(KINDS)]
    if len(near) < PER_KIND * len(KINDS):
        raise RuntimeError("후보 바위 부족: {}".format(len(candidates)))
    picked += [(c, i, t, KINDS[n % len(KINDS)][0], "") for n, (c, i, t, d) in enumerate(near)]
if do_far:
    rng_far = random.Random(SEED + 1)
    pool = [r for r in all_rocks if islands.on_land(r[2].translation.x, r[2].translation.y, margin=1500.0)]
    rng_far.shuffle(pool)
    used = set()
    for kind, (count, (dmin, dmax), snow_ok) in FAR_PLAN.items():
        got = 0
        for c, i, t, d in pool:
            if got >= count:
                break
            if (id(c), i) in used:
                continue
            snow = islands.zone_at(t.translation.x, t.translation.y) == "snow"
            # 설원 바위는 은·금 전용(거리 무관), 그 밖의 땅은 거리 띠로
            if (snow_ok and snow) or (not snow and dmin <= d < dmax):
                used.add((id(c), i))
                got += 1
                picked.append((c, i, t, kind, "F"))
        if got < count:
            raise RuntimeError("후보 바위 부족: {} {}/{}".format(kind, got, count))

body_mesh = unreal.EditorAssetLibrary.load_asset(ORE_BODY_MESH)
box = body_mesh.get_bounding_box()
scale = BODY_HEIGHT_CM / float(box.max.z - box.min.z)
radius = 0.5 * max(float(box.max.x - box.min.x), float(box.max.y - box.min.y)) * scale
pebble = unreal.EditorAssetLibrary.load_asset(PEBBLE)
pbox = pebble.get_bounding_box()
pscale = NUGGET_CM / max(float(pbox.max.x - pbox.min.x), float(pbox.max.y - pbox.min.y), float(pbox.max.z - pbox.min.z))

# 묶음에서 뺄 때 번호가 밀리니 컴포넌트마다 큰 번호부터
by_comp = {}
for comp, i, t, kind, tag in picked:
    by_comp.setdefault(comp, []).append((i, t, kind, tag))
placed = 0
counts = {}
for comp, items in by_comp.items():
    for i, t, kind, tag in sorted(items, key=lambda it: -it[0]):
        comp.remove_instance(i)
        counts[kind] = counts.get(kind, 0) + 1
        name = "{}{}".format(tag, counts[kind]) if tag else "{}".format(counts[kind])
        x, y = t.translation.x, t.translation.y
        ground = float(t.translation.z)
        yaw = rng.uniform(0.0, 360.0)
        ore = subs.spawn_actor_from_class(unreal.GatherableActor, unreal.Vector(x, y, ground), unreal.Rotator(0.0, 0.0, yaw))
        ore.set_actor_label("{}{}_{}".format(LABEL, kind, name))
        ore.set_editor_property("gatherable_row_name", "Gatherable_OreRock" + kind)
        ore.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        for k in range(NUGGETS):
            ang = math.radians(yaw + k * 360.0 / NUGGETS + rng.uniform(-15.0, 15.0))
            h = BODY_HEIGHT_CM * rng.uniform(0.30, 0.70)
            r = radius * 0.82   # 몸통 가운데 높이에서 바위 겉에 반쯤 박히게
            nug = subs.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x + r * math.cos(ang), y + r * math.sin(ang), ground + h),
                                              unreal.Rotator(rng.uniform(-30.0, 30.0), rng.uniform(0.0, 360.0), 0.0))
            nug.set_actor_label("{}{}_{}_Nugget{}".format(LABEL, kind, name, k))
            nug.set_actor_scale3d(unreal.Vector(pscale, pscale, pscale))
            nc = nug.static_mesh_component
            nc.set_static_mesh(pebble)
            nc.set_material(0, mats[kind])
            nc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            nc.set_mobility(unreal.ComponentMobility.MOVABLE)   # 움직이는 바위(Movable)에는 Static 이 안 붙는다
            nug.attach_to_actor(ore, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        placed += 1

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log_warning("[FieldOre] 광석 바위 {} 개 (종류별 {}) ".format(placed, counts))
for a in subs.get_all_level_actors():
    if a.get_actor_label().endswith("_1") and a.get_actor_label().startswith(LABEL):
        unreal.log_warning("[FieldOre] 위치 {} = {}".format(a.get_actor_label(), a.get_actor_location()))
