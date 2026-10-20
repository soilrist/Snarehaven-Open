"""M6-03a 존 포탈 배치 — `L_Hunt` 에 거점존 포탈, `L_Ranch` 에 사냥터 포탈을 헤드리스로 놓는다.

실행 (uproject 도 -script 도 **절대경로** — 상대경로는 엔진 Binaries/Mac/ 기준으로 풀려 죽는다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_zone_portals.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

**존 ID 는 표(`DT_ZoneTable`)의 행 이름이지 새 숫자가 아니다** — 갈 맵은 포탈이 표에서 읽는다
(`AZonePortalActor::InitializeFromTable`). 여기서 정하는 것은 "어느 존에 서서 어디로 보내는지"
두 이름과 **자리·크기(배치값)** 뿐이다 (setup_ranch_stage.py 의 SLOT_SPACING_CM 과 같은 성격).
이미 있으면 새로 만들지 않고 자리만 다시 맞춘다(재실행해도 안전).

**자리 = 선착장(M13-39 · DESIGN 8.14)**: 플레이어 시작점에서 가장 가까운 해안(바다 액터의 섬 반변 기준,
축 방향)에 부두를 해안과 수직으로 놓는다. 포탈 액터 자리는 부두의 **배 타는 칸 가운데 판자 윗면**이다
(`AZonePortalActor` 생성자와 같은 약속 — 해안에서 바다 쪽 300cm, 바닥 +15cm, 앞 +X 가 바다).
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

# (레벨, 선 존, 갈 존, 라벨) — 존 ID 는 tools/unreal/create_data_tables.py 의 행 이름 그대로.
PORTALS = [
    ("/Game/Game/Maps/L_Hunt", "zone.hunt", "zone.ranch", "ZonePortal_ToRanch"),
    ("/Game/Game/Maps/L_Ranch", "zone.ranch", "zone.hunt", "ZonePortal_ToHunt"),
]

# DESIGN 8.14 섬 사이 배 — L_Hunt 안 섬끼리 부두 둘씩 짝(맵이 같아 존 표 대신 건너편 부두로 옮긴다).
# (라벨, 이쪽 거점, 건너편 거점, 버튼 이름, 짝 라벨). 거점 둘레 해안 중 건너편 쪽을 보고 부두 길이만큼 물이 깊은 가장 가까운 자리.
# 작은 세계 지도(2026-10-13): 배 6짝 — 부두가 건너편을 본다(`_ferry_spot` 조건). 갈대 나루·고사리 항구는 짝 없음(갈대 나루는 도토리 마을과 같은 뭍).
ISLAND_FERRY_LEVEL = "/Game/Game/Maps/L_Hunt"
# (이쪽 거점, 이쪽 이름, 건너편 거점, 건너편 이름, 라벨 꼬리)
_FERRY_PAIRS = [
    # 「작은 세계」 군도(2026-10-13) — 같은 뭍 안이 아니라 바다 건너 가장 가까운 섬끼리. 거점 자리는 `data/world_map.json`
    ("haven_outpost", "헤이븐 마을", "thimble_shop", "골무 장터", "HavenThimble"),
    ("snowbell_post", "눈방울 마을", "gull_harbor", "갈매기 항구", "SnowbellGull"),
    ("willow_outpost", "버들 마을", "moss_island", "이끼섬", "WillowMoss"),
    ("maple_outpost", "단풍 마을", "minnow_docks", "피라미 나루", "MapleMinnow"),
    ("otter_docks", "수달 나루터", "lakeside_outpost", "호숫가 마을", "OtterLakeside"),
    ("frost_camp", "서리골", "acorn_outpost", "도토리 마을", "FrostAcorn"),
]
ISLAND_FERRIES = []
for _a, _a_name, _b, _b_name, _tail in _FERRY_PAIRS:
    ISLAND_FERRIES += [
        ("IslandFerry_To" + _tail, _a, _b, _b_name, "IslandFerry_From" + _tail),
        ("IslandFerry_From" + _tail, _b, _a, _a_name, "IslandFerry_To" + _tail),
    ]
PIER_SPACING_CM = 1500.0   # 부두끼리(시작점 존 포탈 부두 포함) 이만큼 떨어뜨린다 — 부두 600 + 좌판 둘레

# DESIGN 8.14 부두 행: 바다 위 400cm 중 끝 200cm 가 배 타는 칸 → 그 가운데 = 해안에서 300cm. 판자 = 바닥 +15.
PIER_SEA_CM = 400.0
BOARDING_CELL_CM = 200.0
PLANK_ABOVE_FLOOR_CM = 15.0
# 가장 가까운 해안이 여럿이면(두 맵 다 그렇다) 이 순서로 고른다 — 🟡 감독 가정: 남(-Y)은 두 맵 다
# 해안 낚시 포인트가 모인 쪽이고 사냥터 강(x=-1800)을 가로지르지 않는다. (방향 x·y, 요 각)
COASTS = [((0.0, -1.0), -90.0), ((-1.0, 0.0), 180.0), ((0.0, 1.0), 90.0), ((1.0, 0.0), 0.0)]


def _find_first(actor_class):
    for actor in _actor_subsystem().get_all_level_actors():
        if isinstance(actor, actor_class):
            return actor
    return None


def _pier_spot(level_path: str):
    """시작점에서 가장 가까운 해안의 부두 자리 (위치, 요). 시작점·바다가 없으면 None."""
    start = _find_first(unreal.PlayerStart)
    sea = _find_first(unreal.SeaActor)
    if start is None or sea is None:
        unreal.log_error("{} 에 시작점({})이나 바다({})가 없다".format(level_path, start, sea))
        return None
    center = sea.get_actor_location()
    half = sea.get_editor_property("island_half_extent_cm")
    floor_z = sea.get_editor_property("island_floor_z")
    p = start.get_actor_location()
    lx, ly = p.x - center.x, p.y - center.y
    if sea.get_editor_property("coast_field_size").x > 0:
        # 사냥터 지도 — 네 방향으로 걸어 처음 바다가 되는 해안 중 가장 가까운 것 (`islands.py`)
        # 작은 세계 지도(2026-10-13): 시작점 바로 앞 해안이 비스듬하면 축 방향 해안은 몇 배 멀다(시험 PortalOnNearestCoast 는 축 방향 부두만 허용).
        # 그래서 시작점에서 옆으로 ±25m 비킨 줄에서도 해안을 찾아, 축 방향 거리가 가장 짧은 곳(옆 걸음은 조금만 벌점)에 부두를 놓는다.
        best = None
        for (dx_, dy_), yaw_ in COASTS:
            for lat in range(-2500, 2501, 100):
                ox, oy = p.x - dy_ * lat, p.y + dx_ * lat
                if not islands.on_land(ox, oy, 0.0):
                    continue
                cx, cy = islands.coast_crossing(ox, oy, dx_, dy_)
                sd = lambda s_: islands.signed_distance(cx + dx_ * s_, cy + dy_ * s_)  # noqa: E731
                if not (sd(300.0) >= 250.0 and sd(400.0) >= 300.0 and sd(-100.0) <= -60.0 and sd(-200.0) <= -120.0):
                    continue
                along = (cx - p.x) * dx_ + (cy - p.y) * dy_
                score = along + abs(lat) * 0.2
                if along > 0.0 and (best is None or score < best[0]):
                    best = (score, dx_, dy_, yaw_, cx, cy)
        if best is None:
            return None
        _, dx, dy, yaw, coast_x, coast_y = best
    else:
        # 해안까지 거리 = 반변 - (그 방향으로 이미 간 거리). min 은 같으면 목록 앞의 것을 준다.
        (dx, dy), yaw = min(COASTS, key=lambda c: half - (lx * c[0][0] + ly * c[0][1]))
        # 해안선 위의 점: 가는 축은 섬 가장자리, 다른 축은 시작점 그대로.
        coast_x = center.x + dx * half if dx else p.x
        coast_y = center.y + dy * half if dy else p.y
    out = PIER_SEA_CM - BOARDING_CELL_CM / 2.0
    location = unreal.Vector(coast_x + dx * out, coast_y + dy * out, floor_z + PLANK_ABOVE_FLOOR_CM)
    unreal.log_warning("[Pier] {} 시작점 ({:.0f},{:.0f}) → 해안 ({:.0f},{:.0f}) 방향 ({:.0f},{:.0f}) 포탈 {}".format(
        level_path, p.x, p.y, coast_x, coast_y, dx, dy, location))
    return location, yaw


def _actor_subsystem() -> unreal.EditorActorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert subsystem
    return subsystem


def _level_subsystem() -> unreal.LevelEditorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert subsystem
    return subsystem


def _find_actor_with_label(label: str):
    # 클래스 객체 비교는 커맨드릿에서 어긋나 중복 스폰이 난다(루프#26) — 라벨로 찾는다.
    for actor in _actor_subsystem().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def _place_portal(level_path: str, source_zone: str, target_zone: str, label: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
        unreal.log_error("level not found: {}".format(level_path))
        return False
    _level_subsystem().load_level(level_path)

    spot = _pier_spot(level_path)
    if spot is None:
        return False
    location, yaw = spot

    actor = _find_actor_with_label(label)
    if actor is None:
        actor = _actor_subsystem().spawn_actor_from_class(unreal.ZonePortalActor, location)
        if actor is None:
            unreal.log_error("failed to spawn: {}".format(label))
            return False
        actor.set_actor_label(label)
        unreal.log_warning("spawned: {} in {}".format(label, level_path))
    # 자리표시 큐브 시절 배율(0.5·2·3)을 지운다 — 부두 크기는 액터가 표 값으로 맞춘다.
    actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    actor.set_actor_location_and_rotation(location, unreal.Rotator(0.0, 0.0, yaw), False, False)

    # 두 이름만 준다 — 갈 맵·표시 이름은 포탈이 BeginPlay 에서 표를 읽어 채운다.
    actor.set_editor_property("source_zone_id", source_zone)
    actor.set_editor_property("target_zone_id", target_zone)
    unreal.log("zone: {} -> {} ({})".format(source_zone, target_zone, label))

    _level_subsystem().save_current_level()
    return True


def _coast_normal(x, y):
    """해안 점의 바다 쪽 단위 방향 (거리장 기울기)."""
    gx = islands.signed_distance(x + 50.0, y) - islands.signed_distance(x - 50.0, y)
    gy = islands.signed_distance(x, y + 50.0) - islands.signed_distance(x, y - 50.0)
    n = math.hypot(gx, gy) or 1.0
    return gx / n, gy / n


def _ferry_spot(here, there, avoid):
    """거점 here 둘레 해안 중 there 쪽을 보는 부두 자리 (해안 x·y, 바다 방향 dx·dy). 없으면 None.
    조건: 배 타는 칸(해안 +300)·부두 끝(+400)이 물 위, 내리는 자리(−100)·뭍 판자(−200)가 뭍, 다른 부두와 PIER_SPACING_CM."""
    outposts = {o["id"]: (o["x"], o["y"]) for o in islands.OUTPOSTS}
    ax, ay = islands.land_point(*outposts[here], margin=300.0)
    bx, by = islands.land_point(*outposts[there], margin=300.0)
    d = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / d, (by - ay) / d
    best = None
    for k in range(144):
        t = k * math.tau / 144
        cx, cy = islands.coast_crossing(ax, ay, math.cos(t), math.sin(t))
        dx, dy = _coast_normal(cx, cy)
        sd = lambda s: islands.signed_distance(cx + dx * s, cy + dy * s)  # noqa: E731
        if dx * ux + dy * uy < 0.3 or any(math.hypot(cx - px, cy - py) < PIER_SPACING_CM for px, py in avoid):
            continue
        if sd(300.0) >= 250.0 and sd(400.0) >= 300.0 and sd(-100.0) <= -60.0 and sd(-200.0) <= -120.0:
            r = math.hypot(cx - ax, cy - ay)
            if best is None or r < best[0]:
                best = (r, cx, cy, dx, dy)
    return best[1:] if best else None


def _place_island_ferries() -> bool:
    _level_subsystem().load_level(ISLAND_FERRY_LEVEL)
    sea = _find_first(unreal.SeaActor)
    if sea is None:
        unreal.log_error("{} 에 바다가 없다".format(ISLAND_FERRY_LEVEL))
        return False
    floor_z = sea.get_editor_property("island_floor_z")
    out = PIER_SEA_CM - BOARDING_CELL_CM / 2.0
    labels = {f[0] for f in ISLAND_FERRIES}
    avoid = []
    for actor in _actor_subsystem().get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith("IslandFerry_") and label not in labels:
            _actor_subsystem().destroy_actor(actor)   # 옛 작은 섬 짝(본섬↔가을섬·늪 섬)
        elif isinstance(actor, unreal.ZonePortalActor) and not label.startswith("IslandFerry_"):
            p = actor.get_actor_location()
            avoid.append((p.x, p.y))
    placed = {}
    for label, here, there, name, _ in ISLAND_FERRIES:
        spot = _ferry_spot(here, there, avoid)
        if spot is None:
            unreal.log_error("[IslandFerry] {} 자리 없음 ({} → {})".format(label, here, there))
            return False
        coast_x, coast_y, dx, dy = spot
        avoid.append((coast_x, coast_y))
        location = unreal.Vector(coast_x + dx * out, coast_y + dy * out, floor_z + PLANK_ABOVE_FLOOR_CM)
        yaw = math.degrees(math.atan2(dy, dx))
        actor = _find_actor_with_label(label)
        if actor is None:
            actor = _actor_subsystem().spawn_actor_from_class(unreal.ZonePortalActor, location)
            actor.set_actor_label(label)
        actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
        actor.set_actor_location_and_rotation(location, unreal.Rotator(0.0, 0.0, yaw), False, False)
        actor.set_editor_property("ferry_display_name", name)
        placed[label] = actor
        unreal.log_warning("[IslandFerry] {} 해안 ({:.0f},{:.0f}) 요 {:.0f} → 포탈 {} 버튼 {}".format(
            label, coast_x, coast_y, yaw, location, name))
    for label, *_rest, pair in ISLAND_FERRIES:
        placed[label].set_editor_property("ferry_to", placed[pair])
    _level_subsystem().save_current_level()
    return True


failed = 0
for level, source, target, actor_label in PORTALS:
    if not _place_portal(level, source, target, actor_label):
        failed += 1
if not _place_island_ferries():
    failed += 1

if failed:
    unreal.log_error("포탈 배치 실패 {}건".format(failed))
    raise SystemExit(1)
unreal.log("존 포탈 배치 저장 완료 — {}개".format(len(PORTALS)))
