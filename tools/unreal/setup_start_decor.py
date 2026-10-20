"""M17-03 시작 자리 꾸미기 — `L_Hunt`·`L_Ranch` 의 PlayerStart 화면 안에 나무·바위·덤불·풀을 깐다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_start_decor.py" \
      -unattended -nosplash \
      "-ini:Engine:[ConsoleVariables]:Editor.AsyncStaticMeshCompilation=0" \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

`Editor.AsyncStaticMeshCompilation=0` 이 없으면 메시가 비동기로 컴파일돼 `RanchFloor` 에 충돌이 아직 없어서 선 추적이 다 빗나가고
`L_Ranch` 에 하나도 안 선다(10-02 실측 "세운 것 0개 · 건너뛴 후보 318").

왜: 밀도 자(`-SnareMeasureDensity`, M15-22)가 두 맵 시작 자리 화면 안 나무·바위를 0 으로 쟀다(시트는 나무 10~20 ·
바위 무리 3~5 · 빈 공간 절반 이상). 기존 `Nature` 메시만 쓰고 새 메시는 만들지 않는다.

여기 숫자는 전부 레벨 배치값이다(게임 규칙 숫자 아님 — 🟡). 자리는 고정 시드라 다시 돌려도 같은 곳에 선다.
재실행 안전: 옛 `StartDecor_` 액터는 먼저 지우고 같은 시드로 다시 세운다(메시 목록이 바뀌어도 옛 것이 안 남는다).
**다시 돌리면 뒤이어 `thin_flower_clumps.py` 도 돌린다** — 꽃(`Flower_01`)도 무리 크기 상한에 같이 솎인다(M19-03).
M17-12·13: 풀 존에 청록 나무·빨간 덤불이 섞였다 — Kenney `Tree_01/02`·`Pine_01/02`(설원 팔레트)와 Quaternius `Bush_01`
(검붉은 가시 뭉치) 대신 초록 술 치마 나무(`Pine_05/06`·`Tree_04`·`Birch_01`)와 초록 술 덤불 `Bush_02` 를 쓴다.
바닥 위에만 세운다 — 후보 자리마다 위에서 아래로 쏘아 `bWorldStatic` 바닥이 걸리고 높이가 낮을 때만 쓴다
(바다·절벽 위에 서지 않게, 판단은 이름이 아니라 실제 충돌 결과).
"""
import math
import random

import unreal

NATURE_DIR = "/Game/Game/Environment/Nature"
LABEL_PREFIX = "StartDecor_"
PLACEMENT_SEED = 24503
CLEAR_RADIUS_CM = 380.0         # 플레이어 둘레 공터 — 여기엔 아무것도 안 세운다
# 화면(1920x1080 쿼터뷰)이 덮는 바닥: 플레이어 기준 X -700~+1010 · Y ±1200 (밀도 자 실측). 안쪽으로 조금 줄여 잡는다.
X_RANGE = (-620.0, 940.0)
Y_RANGE = (-1080.0, 1080.0)
GROUND_Z_MAX_CM = 60.0          # 바닥이 이보다 높으면 언덕·구조물이라 건너뜀
GROUND_Z_MIN_CM = -30.0         # 이보다 낮으면 물

# (맵, 시작 라벨, 세우지 않을 사각형 [(x0, x1, y0, y1) 월드 cm]) — L_Ranch 는 집 두 채·마당(울타리 안 x 200~1150 · y ±900).
# 이 구역이 없을 때 Pine_05@(513,331)·Tree_04@(756,64) 가 마당 길을 막아 집 A·B 를 못 갔다(HouseFurniture.WalkInsideAndOut).
MAPS = [
    ("/Game/Game/Maps/L_Hunt", "MinStagePlayerStart", []),
    ("/Game/Game/Maps/L_Ranch", "RanchPlayerStart", [(200.0, 1150.0, -900.0, 900.0)]),
]

# (에셋, 개수, 목표 높이 cm, 이웃과 최소 간격 cm, 무리 크기)
TREES = [
    ("SM_Nature_Tree_04", 4, 420.0, 250.0),
    ("SM_Nature_Birch_01", 3, 340.0, 250.0),
    ("SM_Nature_Pine_05", 4, 636.0, 250.0),
    ("SM_Nature_Pine_06", 3, 566.0, 250.0),
]
ROCK_CLUSTERS = 4                # 바위 무리 수 — 무리 사이 400cm 이상(밀도 자 기준), 한 무리에 두 개(120cm 안)
ROCK_CLUSTER_GAP_CM = 380.0
ROCKS = [("SM_Nature_Rock_01", 180.0), ("SM_Nature_Rock_02", 135.0)]
FILLERS = [
    ("SM_Nature_Bush_02", 6, 95.0, 200.0),
    ("SM_Nature_Grass_01", 6, 55.0, 120.0),
    ("SM_Nature_Grass_02", 6, 45.0, 120.0),
    ("SM_Nature_Flower_01", 5, 38.0, 120.0),
    ("SM_Nature_Pebble_01", 4, 15.0, 120.0),
]


def _actor_subsystem() -> unreal.EditorActorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert subsystem
    return subsystem


def _actors_by_label() -> dict:
    found = {}
    for actor in _actor_subsystem().get_all_level_actors():
        found[actor.get_actor_label()] = actor
    return found


def _mesh_height_cm(mesh: unreal.StaticMesh) -> float:
    bounds = mesh.get_bounding_box()
    return float(bounds.max.z - bounds.min.z)


def _trace_z(world, x: float, y: float):
    """위에서 아래로 쏜 첫 충돌의 높이. 안 맞으면 None."""
    result = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(x, y, 3000.0), unreal.Vector(x, y, -3000.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
        unreal.DrawDebugTrace.NONE, True)
    blocked, hit = result if isinstance(result, tuple) else (True, result)
    if not blocked or hit is None:
        return None
    return float(hit.to_tuple()[4].z)


class TileHeight:
    """`L_Hunt` 바닥 타일(`WorldGround_*`)의 삼각형으로 높이를 읽는다.

    커맨드릿에서는 이 타일에 선 트레이스가 안 맞는다(같은 자리 `L_Ranch` 평면은 맞는다). 그래서 메시 정점에서
    직접 구한다 — 시작 자리 둘레 사각형에 걸치는 삼각형만 모아 두고 점이 든 삼각형의 평면 높이를 쓴다.
    """

    def __init__(self, origin, half_cm):
        self.triangles = []
        lo_x, hi_x = origin[0] - half_cm, origin[0] + half_cm
        lo_y, hi_y = origin[1] - half_cm, origin[1] + half_cm
        for actor in _actor_subsystem().get_all_level_actors():
            if not actor.get_actor_label().startswith("WorldGround_"):
                continue
            center, extent = actor.get_actor_bounds(False)
            if center.x + extent.x < lo_x or center.x - extent.x > hi_x:
                continue
            if center.y + extent.y < lo_y or center.y - extent.y > hi_y:
                continue
            mesh = actor.static_mesh_component.static_mesh
            verts, indices = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)[:2]
            for i in range(0, len(indices), 3):
                tri = [verts[indices[i]], verts[indices[i + 1]], verts[indices[i + 2]]]
                if max(v.x for v in tri) < lo_x or min(v.x for v in tri) > hi_x:
                    continue
                if max(v.y for v in tri) < lo_y or min(v.y for v in tri) > hi_y:
                    continue
                self.triangles.append([(float(v.x), float(v.y), float(v.z)) for v in tri])

    def z(self, x: float, y: float):
        for (ax, ay, az), (bx, by, bz), (cx, cy, cz) in self.triangles:
            det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(det) < 1e-9:
                continue
            u = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / det
            v = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / det
            if u >= -1e-6 and v >= -1e-6 and u + v <= 1.0 + 1e-6:
                return u * az + v * bz + (1.0 - u - v) * cz
        return None


def _flat_land_z(world, tiles, x: float, y: float):
    """바닥 높이 — 둘레 네 점(80cm)도 같은 높이여야 한다(해안 경사·물가는 None)."""
    centre = _trace_z(world, x, y)
    if centre is None:
        centre = tiles.z(x, y)
        if centre is None:
            return None
        for dx, dy in ((80, 0), (-80, 0), (0, 80), (0, -80)):
            other = tiles.z(x + dx, y + dy)
            if other is None or abs(other - centre) > 12.0:
                return None
    return centre


class Placer:
    def __init__(self, origin, avoid, world, tiles, rng, existing, keep_out):
        self.origin = origin
        self.keep_out = keep_out
        self.avoid = avoid
        self.world = world
        self.tiles = tiles
        self.rng = rng
        self.existing = existing
        self.points = []
        self.placed = 0
        self.skipped_ground = 0

    def spot(self, spacing: float):
        for _ in range(80):
            dx = self.rng.uniform(*X_RANGE)
            dy = self.rng.uniform(*Y_RANGE)
            if math.hypot(dx, dy) < CLEAR_RADIUS_CM:
                continue
            x, y = self.origin[0] + dx, self.origin[1] + dy
            if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, x1, y0, y1 in self.keep_out):
                continue
            if any(math.hypot(x - ax, y - ay) < radius for ax, ay, radius in self.avoid):
                continue
            if any(math.hypot(x - px, y - py) < spacing for px, py in self.points):
                continue
            z = _flat_land_z(self.world, self.tiles, x, y)
            if z is None or z > GROUND_Z_MAX_CM or z < GROUND_Z_MIN_CM:
                self.skipped_ground += 1
                continue
            return x, y, z
        return None

    def put(self, label: str, mesh, target_height: float, at):
        scale = target_height / _mesh_height_cm(mesh)
        actor = self.existing.get(label)
        if actor is None:
            actor = _actor_subsystem().spawn_actor_from_class(
                unreal.StaticMeshActor, unreal.Vector(at[0], at[1], at[2]),
                unreal.Rotator(0.0, 0.0, self.rng.uniform(0.0, 360.0)))
            if actor is None:
                unreal.log_error("스폰 실패: " + label)
                return
            actor.set_actor_label(label)
        component = actor.static_mesh_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_static_mesh(mesh)
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        where = actor.get_actor_location()
        self.points.append((float(where.x), float(where.y)))
        self.placed += 1


def _decorate(level_path: str, start_label: str, keep_out) -> None:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level_path)
    world = unreal.EditorLevelLibrary.get_editor_world()
    for label, actor in _actors_by_label().items():
        if label.startswith(LABEL_PREFIX):
            actor.destroy_actor()
    existing = _actors_by_label()
    start = existing.get(start_label)
    if start is None:
        unreal.log_error("시작 자리 없음: " + start_label)
        return
    location = start.get_actor_location()
    origin = (float(location.x), float(location.y))
    avoid = []
    for label, actor in existing.items():
        if label.startswith(LABEL_PREFIX) or label.startswith("WorldGround_") or label.startswith("ZoneProps_"):
            continue
        if actor.get_class().get_name() in ("SeaActor", "PlayerStart", "Sky", "DirectionalLight", "SkyLight"):
            continue
        center, extent = actor.get_actor_bounds(False)
        if abs(center.x - origin[0]) > 2500 or abs(center.y - origin[1]) > 2500:
            continue
        if max(extent.x, extent.y) > 800.0 or extent.z < 5.0:
            continue                    # 바닥 판·바다 같은 넓고 납작한 것은 피할 대상이 아니다
        avoid.append((float(center.x), float(center.y), max(float(extent.x), float(extent.y)) + 60.0))

    tiles = TileHeight(origin, 1500.0)
    unreal.log_warning('DECOR 바닥 삼각형 {}개 · 피할 구조물 {}개'.format(len(tiles.triangles), len(avoid)))
    rng = random.Random(PLACEMENT_SEED)
    placer = Placer(origin, avoid, world, tiles, rng, existing, keep_out)

    for cluster in range(ROCK_CLUSTERS):
        head = placer.spot(ROCK_CLUSTER_GAP_CM)
        if head is None:
            unreal.log_warning("바위 무리 자리 못 찾음: {}".format(cluster))
            continue
        for index, (name, height) in enumerate(ROCKS):
            mesh = unreal.load_asset(NATURE_DIR + "/" + name)
            angle = rng.uniform(0.0, math.tau)
            offset = 120.0 * index
            at = (head[0] + math.cos(angle) * offset, head[1] + math.sin(angle) * offset, head[2])
            placer.put("{}Rock_{:02d}_{}".format(LABEL_PREFIX, cluster, index), mesh, height, at)

    for name, count, height, spacing in TREES:
        mesh = unreal.load_asset(NATURE_DIR + "/" + name)
        for index in range(count):
            at = placer.spot(spacing)
            label = "{}{}_{:02d}".format(LABEL_PREFIX, name[len("SM_Nature_"):], index)
            if at is None:
                if label not in existing:
                    unreal.log_warning("자리 못 찾음: " + label)
                    continue
                at = (existing[label].get_actor_location().x, existing[label].get_actor_location().y,
                      existing[label].get_actor_location().z)
            placer.put(label, mesh, height, at)

    for name, count, height, spacing in FILLERS:
        mesh = unreal.load_asset(NATURE_DIR + "/" + name)
        for index in range(count):
            at = placer.spot(spacing)
            if at is None:
                continue
            placer.put("{}{}_{:02d}".format(LABEL_PREFIX, name[len("SM_Nature_"):], index), mesh, height, at)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log_warning("DECOR {} 시작=({:.0f},{:.0f}) 세운 것 {}개 · 바닥 아님으로 건너뛴 후보 {}".format(
        level_path, origin[0], origin[1], placer.placed, placer.skipped_ground))


for path, label, keep_out in MAPS:
    _decorate(path, label, keep_out)
