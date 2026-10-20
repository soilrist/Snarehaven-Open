"""DESIGN 8.16 「광산」 — `L_Hunt` 설원에 광산 입구를, 같은 맵 먼 곳에 광산 굴을 세운다. `setup_zones.py` 다음에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/setup_mine.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

원본 레퍼런스 광산은 "벙커처럼 안으로 들어가는 형태"(DC 레퍼런스 갤러리 뉴비 팁) — 입구를 밟으면 어두운 굴 안으로 들어가고,
굴에는 나무 버팀목과 등불, 광맥이 있다. 여기서는 굴을 바다 범위 밖(수영·추위 판정이 없는 곳)에 두고 `AMineDoorActor`
두 개가 서로의 앞자리로 폰을 옮긴다. 재실행 안전: `Mine_` 라벨을 전부 지우고 다시 세운다.
"""
import random
import re

import unreal

NATURE_DIR = "/Game/Game/Environment/Nature"
GROUND_DIR = "/Game/Game/Environment/Ground"
MATTE_PARENT = NATURE_DIR + "/M_Nature_Matte"   # `setup_zones.py` 가 만든 무광 부모 — 색·바닥 밝기 두 칸
GATHER_TABLE = "/Game/Game/Data/Gatherables/DT_GatherableTable"
CUBE = "/Engine/BasicShapes/Cube"               # 100cm · 피벗 가운데
FRAME = NATURE_DIR + "/SM_Nature_MineFrame_01"  # `make_tree_meshes.py` 버팀목 — 안쪽 폭 360, 높이 240, 굴 방향 X
BOULDERS = [NATURE_DIR + "/SM_Nature_Boulder_01", NATURE_DIR + "/SM_Nature_Boulder_02"]
LABEL = "Mine_"
SEED = 816

# 설원 입구 — 작은 세계 북서 대륙 설원, 설원 거점 frost_camp(서리골 30600, -38200) 북서쪽 45m, 해안에서 24m 안쪽(`islands.py`).
# 구멍은 카메라 쪽(-X)을 보고 그 앞 20m 도 설원 뭍이다.
# `setup_zones.py` 보다 먼저 돌린다 — 둔덕·틀이 장애물이 돼 나무가 입구를 막지 않는다
ENTRANCE = (28350.0, -34300.0)
# 굴 원점 — 바다 벽(맵 반폭 54400 + 바다 3000) 밖, 거리장·존 칸 밖이라 수영·추위가 안 걸린다
MINE = (70000.0, 0.0)
# 굴 방 (원점 기준 x0, x1, y0, y1) — 들머리 → 굴길 → 큰 방(기본 광맥) → 옆 굴길 → 깊은 방(고급 광맥)
ROOMS = [(-400.0, 600.0, -450.0, 450.0), (600.0, 1900.0, -200.0, 200.0), (1900.0, 3500.0, -1000.0, 1000.0),
         (2500.0, 2900.0, 1000.0, 2100.0), (1900.0, 3500.0, 2100.0, 3300.0)]
# 문 발판 반폭(x 깊이, y 너비, z) — 굴 폭 가득, 한 걸음 깊이
DOOR_EXTENT = (120.0, 170.0, 120.0)   # 깊이 60 은 폰으로 문 앞에 서도 발판에 안 닿을 때가 있었다(사용자 "광산도 안들어가져") — 구멍 앞 2.5m 부터
ARRIVE_GAP = 280.0    # 발판 가운데 → 도착 자리. 발판 반깊이 120 + 캡슐 반지름 34 보다 넉넉히 — 되돌아 튕기지 않게
# 버팀목 (원점 기준 x, y, 방향 yaw) — 굴길마다 400 간격, 굴 입구 두 곳
FRAMES = [(-400.0, 0.0, 0.0), (800.0, 0.0, 0.0), (1200.0, 0.0, 0.0), (1600.0, 0.0, 0.0),
          (2700.0, 1300.0, 90.0), (2700.0, 1700.0, 90.0)]
# 등불 (원점 기준 x, y) — 모바일 점광 9개. 굴 벽에 붙여 가운데 길을 밝힌다
LAMPS = [(-150.0, -380.0), (350.0, 380.0), (1000.0, -170.0), (1400.0, 170.0), (2100.0, -900.0),
         (3300.0, -900.0), (2700.0, 900.0), (2100.0, 3200.0), (3300.0, 2200.0)]
LAMP_COLOR = unreal.Color(r=255, g=170, b=90, a=255)   # 백열등 주황 — 밤 불빛(`setup_night_lights.py`)보다 붉게
LAMP_INTENSITY = 120.0   # cd — 노출 −1.5 에서 등불 둘레 바닥이 주황으로 뜨는 정도 🟡
LAMP_RADIUS = 700.0
EXPOSURE_BIAS = -1.5     # 굴 안은 바깥보다 약 ⅓ 밝기 — 원본 광산은 어둡고 등불만 밝다(채굴 모자 "어둠을 밝힌다") 🟡
# 광맥 (DESIGN 8.16) — 큰 방 6 · 깊은 방 4. 원점 기준
ORES = {"Gatherable_OreBasic": [(2150.0, -600.0), (2300.0, 650.0), (2750.0, -150.0), (3150.0, -650.0), (3250.0, 500.0), (2600.0, 750.0)],
        "Gatherable_OreAdvanced": [(2150.0, 2450.0), (2350.0, 3000.0), (3100.0, 2400.0), (3250.0, 3000.0)]}
# 색만 다른 MI (🟡 팔레트) — (색, 바닥 밝기). 바닥 밝기가 크면 스스로 빛난다(등불·바깥 빛)
MATERIALS = {
    GROUND_DIR + "/MI_Ground_Mine": (unreal.LinearColor(0.060, 0.050, 0.042, 1.0), 0.25),     # 흙 섞인 굴 바닥
    NATURE_DIR + "/MI_Nature_Leafs_MineRock": (unreal.LinearColor(0.085, 0.078, 0.075, 1.0), 0.25),   # 굴 벽 바위
    NATURE_DIR + "/MI_Nature_Leafs_MineDark": (unreal.LinearColor(0.0, 0.0, 0.0, 1.0), 0.0),   # 입구 구멍·굴 밖 어둠
    NATURE_DIR + "/MI_Nature_Leafs_MineLamp": (unreal.LinearColor(1.0, 0.55, 0.18, 1.0), 6.0),
    NATURE_DIR + "/MI_Nature_Leafs_MineDaylight": (unreal.LinearColor(0.85, 0.90, 1.0, 1.0), 2.0),   # 굴 안에서 본 바깥 빛
}

subs = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary


def _materials():
    parent = assets.load_asset(MATTE_PARENT)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    out = {}
    for path, (color, floor) in MATERIALS.items():
        folder, name = path.rsplit("/", 1)
        mat = assets.load_asset(path) if assets.does_asset_exist(path) else \
            tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mat, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, "DiffuseColor", color)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat, "AmbientFloor", floor)
        assets.save_asset(path, only_if_is_dirty=False)
        out[name] = mat
    return out


def _mesh(label, mesh_path, x, y, z, scale, yaw=0.0, material=None, collide=True, hidden=False):
    actor = subs.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw))
    actor.set_actor_label(LABEL + label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    comp = actor.static_mesh_component
    comp.set_static_mesh(assets.load_asset(mesh_path))
    if material:
        comp.set_material(0, material)
    if not collide:
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_hidden_in_game(hidden)
    return actor


def _box(label, x0, x1, y0, y1, z0, z1, material=None, collide=True, hidden=False):
    return _mesh(label, CUBE, (x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5,
                 ((x1 - x0) / 100.0, (y1 - y0) / 100.0, (z1 - z0) / 100.0), material=material, collide=collide, hidden=hidden)


def _door(label, x, y, exit_xy, exit_yaw):
    door = subs.spawn_actor_from_class(unreal.MineDoorActor, unreal.Vector(x, y, DOOR_EXTENT[2]))
    door.set_actor_label(LABEL + label)
    door.set_actor_scale3d(unreal.Vector(*(e / 50.0 for e in DOOR_EXTENT)))   # 발판 기본 반폭 50
    door.set_editor_property("exit_location", unreal.Vector(exit_xy[0], exit_xy[1], 0.0))
    door.set_editor_property("exit_yaw_degrees", exit_yaw)
    return door


def _inside(x, y, pad=0.0):
    return any(x0 - pad <= x <= x1 + pad and y0 - pad <= y <= y1 + pad for x0, x1, y0, y1 in ROOMS)


def _walls(mats, rng):
    """방 둘레 중 다른 방과 이어지지 않은 곳에 벽 — 보이는 건 바위 덩이, 막는 건 숨긴 상자."""
    ox, oy = MINE
    count = 0
    for index, (x0, x1, y0, y1) in enumerate(ROOMS):
        for side, (ax, ay, bx, by, nx, ny) in enumerate(((x0, y0, x1, y0, 0, -1), (x0, y1, x1, y1, 0, 1),
                                                          (x0, y0, x0, y1, -1, 0), (x1, y0, x1, y1, 1, 0))):
            length = abs(bx - ax) + abs(by - ay)
            steps = int(length // 50.0)
            run = None
            for k in range(steps + 1):
                t = k / steps
                px, py = ax + (bx - ax) * t, ay + (by - ay) * t
                closed = not _inside(px + nx * 40.0, py + ny * 40.0)   # 바로 바깥이 다른 방이면 트인 곳
                if closed and run is None:
                    run = (px, py)
                if (not closed or k == steps) and run is not None:
                    ex, ey = (px, py) if closed else (px - (bx - ax) / steps, py - (by - ay) / steps)
                    lo_x, hi_x = sorted((run[0], ex))
                    lo_y, hi_y = sorted((run[1], ey))
                    # 막는 상자 — 두께 100, 방 밖으로
                    _box("Wall_{}_{}_{}".format(index, side, count), ox + lo_x + min(nx, 0) * 100.0, ox + hi_x + max(nx, 0) * 100.0,
                         oy + lo_y + min(ny, 0) * 100.0, oy + hi_y + max(ny, 0) * 100.0, 0.0, 300.0, hidden=True)
                    # 보이는 바위 — 170 간격, 벽 밖으로 반쯤 묻는다
                    span = max(hi_x - lo_x, hi_y - lo_y)
                    for j in range(int(span // 170.0) + 1):
                        f = j / max(1, int(span // 170.0))
                        bx_, by_ = lo_x + (hi_x - lo_x) * f, lo_y + (hi_y - lo_y) * f
                        size = rng.uniform(1.4, 2.0)
                        _mesh("Rock_{}_{}_{}_{}".format(index, side, count, j), BOULDERS[j % 2],
                              ox + bx_ + nx * 70.0, oy + by_ + ny * 70.0, 0.0, (size, size, size * rng.uniform(1.3, 1.7)),
                              yaw=rng.uniform(0.0, 360.0), material=mats["MI_Nature_Leafs_MineRock"], collide=False)
                    count += 1
                    run = None
    return count


def _ores():
    table = assets.load_asset(GATHER_TABLE)
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    meshes = dict(zip(names, unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "MeshRef")))
    heights = dict(zip(names, unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "BodyHeightCm")))
    rng = random.Random(SEED)
    placed = 0
    for row, spots in ORES.items():
        mesh = unreal.load_asset(re.search(r"(/Game/[^.'\"]+)", meshes[row]).group(1))
        box = mesh.get_bounding_box()
        scale = float(heights[row]) / float(box.max.z - box.min.z)   # `setup_zones.py` 채집 더미와 같은 배율
        for n, (x, y) in enumerate(spots):
            actor = subs.spawn_actor_from_class(unreal.GatherableActor, unreal.Vector(MINE[0] + x, MINE[1] + y, 0.0),
                                                unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
            actor.set_actor_label("Gather_Mine_{}_{}".format(row.split("_", 1)[1], n + 1))
            actor.set_editor_property("gatherable_row_name", row)
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            placed += 1
    return placed


def _post_process():
    volume = subs.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(MINE[0] + 1500.0, MINE[1] + 1100.0, 4000.0))
    volume.set_actor_label(LABEL + "Dark")
    # 기본 붓은 200cm 상자 — 굴과 카메라(거리 7500 × 줌 1.3, 55° → 뒤 5600·위 8000)를 다 품게 늘린다
    volume.set_actor_scale3d(unreal.Vector(90.0, 70.0, 60.0))
    volume.set_editor_property("unbound", False)
    volume.set_editor_property("priority", 1.0)
    s = volume.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    volume.set_editor_property("settings", s)


unreal.EditorLoadingAndSavingUtils.load_map("/Game/Game/Maps/L_Hunt")
for old in subs.get_all_level_actors():
    label = old.get_actor_label()
    # 옛 설원·늪 광맥(`setup_zones.py` GATHER_PILES 에서 뺐다)도 같이 걷는다
    if label.startswith((LABEL, "Gather_Mine_", "Gather_Snow_OreBasic_", "Gather_Swamp_OreAdvanced_")):
        subs.destroy_actor(old)
mats = _materials()
rng = random.Random(SEED)

# 설원 입구 — 바위 둔덕 셋 뒤, 버팀목 틀 안에 검은 구멍
ex, ey = ENTRANCE
# 큰 둔덕이 구멍 판을 뚫고 나오지 않게 뒤로 뺐다(m1568: 바위 면이 구멍 안에 비쳤다)
_mesh("Mound", BOULDERS[0], ex + 420.0, ey, 0.0, (3.4, 3.4, 3.2), yaw=20.0)
_mesh("MoundL", BOULDERS[1], ex + 150.0, ey - 330.0, 0.0, (2.2, 2.2, 2.4), yaw=70.0)
_mesh("MoundR", BOULDERS[1], ex + 150.0, ey + 330.0, 0.0, (2.2, 2.2, 2.4), yaw=200.0)
# 구멍·틀은 보기만 — 충돌이 있으면 발판 끝에서 멈춰 문이 안 열렸다. 옆 둔덕이 벽 노릇을 한다
_mesh("Hole", CUBE, ex + 20.0, ey, 120.0, (0.2, 3.6, 2.4), material=mats["MI_Nature_Leafs_MineDark"], collide=False)
_mesh("Frame", FRAME, ex, ey, 0.0, (1.0, 1.0, 1.0), collide=False)

# 굴 — 굴 밖 어둠 판, 방 바닥, 벽, 안쪽 문(바깥 빛), 버팀목, 등불, 광맥
ox, oy = MINE
_box("Void", ox - 6000.0, ox + 9000.0, oy - 6000.0, oy + 8000.0, -80.0, -60.0, mats["MI_Nature_Leafs_MineDark"], collide=False)
for i, (x0, x1, y0, y1) in enumerate(ROOMS):
    _box("Floor_{}".format(i), ox + x0, ox + x1, oy + y0, oy + y1, -20.0, 0.0, mats["MI_Ground_Mine"])
walls = _walls(mats, rng)
_mesh("ExitLight", CUBE, ox - 420.0, oy, 120.0, (0.2, 3.6, 2.4), material=mats["MI_Nature_Leafs_MineDaylight"], collide=False)
for i, (x, y, yaw) in enumerate(FRAMES):
    _mesh("Beam_{}".format(i), FRAME, ox + x, oy + y, 0.0, (1.0, 1.0, 1.0), yaw=yaw)
for i, (x, y) in enumerate(LAMPS):
    # 몸은 큐브가 아니라 등불 메시 — 크기·높이는 `setup_mine_lamps.py` 가 맞춘다(M17-03). 이 스크립트로 굴을 다시 지으면 그 스크립트를 이어서 돌린다
    _mesh("Lamp_{}".format(i), "/Game/Game/Environment/Props/SM_Prop_Lantern_01", ox + x, oy + y, 150.0, (0.353, 0.353, 0.353), collide=False)
    light = subs.spawn_actor_from_class(unreal.PointLight, unreal.Vector(ox + x, oy + y, 190.0))
    light.set_actor_label(LABEL + "Light_{}".format(i))
    comp = light.get_component_by_class(unreal.PointLightComponent)
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    comp.set_editor_property("intensity", LAMP_INTENSITY)
    comp.set_editor_property("light_color", LAMP_COLOR)
    comp.set_editor_property("attenuation_radius", LAMP_RADIUS)
    comp.set_editor_property("cast_shadows", False)   # 모바일 점광 그림자는 비싸다
ores = _ores()
_post_process()

# 문 둘 — 입구 발판은 구멍 앞, 안쪽 발판은 바깥 빛 앞. 서로의 앞자리(발판 밖 ARRIVE_GAP)를 가리킨다
in_x = ox - 400.0 + DOOR_EXTENT[0] + 10.0
out_x = ex - DOOR_EXTENT[0] - 10.0
_door("DoorIn", out_x, ey, (in_x + ARRIVE_GAP, oy), 0.0)
_door("DoorOut", in_x, oy, (out_x - ARRIVE_GAP, ey), 180.0)

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
unreal.log_warning("[Mine] 입구 {} · 굴 {} · 방 {} · 벽 {} · 버팀목 {} · 등불 {} · 광맥 {}".format(
    ENTRANCE, MINE, len(ROOMS), walls, len(FRAMES), len(LAMPS), ores))
