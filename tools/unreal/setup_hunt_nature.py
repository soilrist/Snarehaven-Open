"""M5-02b 사냥터 자연물 배치 — `SM_Nature_*` 14종을 `L_Hunt` 에 깔고 바닥 머티리얼을 바꾼다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_hunt_nature.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

에셋을 들여놓는 것은 M5-02a(`import_nature_assets.py`)가 이미 했다. 이 스크립트는 **레벨에 세우기**만 한다.
`setup_hunt_stage.py`(M0-03 최소 무대)는 건드리지 않는다 — `MinStagePlayerStart`·`MinStageFloor`·
`MinStageSun`·`MinStageSky`·`HuntStagePig` 는 그대로 두고, 바닥은 **머티리얼만** 갈아 끼운다.

**여기 숫자는 전부 레벨 배치값이다 — 게임 규칙 숫자가 아니다**(`setup_hunt_stage.py` 의 `ANIMAL_OFFSET_CM`
과 같은 성격, Autonomy 🟡). 배율을 직접 적지 않고 **목표 높이(cm)** 만 적는다 — 팩마다 모델 크기 기준이
달라서(Kenney 나무 123~171cm · Quaternius 바위 3m · 사람 캡슐 176cm) 배율을 손으로 적으면 팩을 바꿀 때
같이 틀어진다. 배율은 실행 중에 `목표 높이 ÷ 메시 실제 높이` 로 구한다.

재실행 안전: 액터 라벨(`HuntNature_*`)이 이미 있으면 새로 세우지 않고 **메시·배율만 다시 맞춘다**
(목표 높이를 고쳐 다시 돌릴 때 옛 배율이 남지 않게). 좌표는 고정 시드 난수라 같은 자리에 같은 것이 선다.
"""
import math
import random

import unreal

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"
NATURE_DIR = "/Game/Game/Environment/Nature"

FLOOR_LABEL = "MinStageFloor"
FLOOR_MATERIAL = NATURE_DIR + "/MI_Nature_Grass"   # 회색 자리표시 바닥 → 팩의 풀 머티리얼

LABEL_PREFIX = "HuntNature_"
PLACEMENT_SEED = 6902           # 고정 시드 — 다시 돌려도 같은 숲이 선다(좌표를 파일에 안 적어도 되게)
CLEAR_RADIUS_CM = 350.0         # 플레이어 시작(0,0)·돼지(120,0) 둘레 공터 — 여기엔 아무것도 안 세운다
FLOOR_HALF_CM = 2500.0          # `MinStageFloor` = Plane(100cm) × 50 이라 한 변 5000cm
PLACE_TRIES = 60                # 간격 조건을 못 맞추면 이만큼 다시 뽑고 포기(개수가 모자라면 로그로 남는다)

# (에셋 이름, 개수, 목표 높이 cm, 반경 최소 cm, 반경 최대 cm, 이웃과 최소 간격 cm)
# 목표 높이는 사람 캡슐 176cm 를 눈대중 기준으로 잡은 배치값이다(🟡 INBOX 확인). 나무를 6m 로 세워 봤더니
# 쿼터뷰 화면을 가려(`captures/69_hunt_forest.png`) 사람의 2~2.5배로 낮췄다.
PLACEMENTS = [
    ("SM_Nature_Tree_01", 8, 420.0, 700.0, 2200.0, 450.0),
    ("SM_Nature_Tree_02", 6, 340.0, 700.0, 2200.0, 450.0),
    ("SM_Nature_Pine_01", 6, 450.0, 800.0, 2200.0, 450.0),
    ("SM_Nature_Pine_02", 5, 400.0, 800.0, 2200.0, 450.0),
    ("SM_Nature_Bush_01", 8, 95.0, 450.0, 2000.0, 200.0),
    ("SM_Nature_Rock_01", 3, 180.0, 500.0, 2000.0, 300.0),
    ("SM_Nature_Rock_02", 3, 135.0, 500.0, 2000.0, 300.0),
    ("SM_Nature_Grass_01", 10, 55.0, 380.0, 1800.0, 120.0),
    ("SM_Nature_Grass_02", 10, 45.0, 380.0, 1800.0, 120.0),
    ("SM_Nature_Pebble_01", 6, 15.0, 380.0, 1600.0, 120.0),
    ("SM_Nature_Stump_01", 3, 75.0, 500.0, 1600.0, 200.0),
    ("SM_Nature_Log_01", 3, 60.0, 500.0, 1600.0, 200.0),
    ("SM_Nature_Mushroom_01", 4, 38.0, 400.0, 1500.0, 120.0),
    ("SM_Nature_Flower_01", 6, 38.0, 400.0, 1500.0, 120.0),
]


def _actor_subsystem() -> unreal.EditorActorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert subsystem
    return subsystem


def _level_subsystem() -> unreal.LevelEditorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert subsystem
    return subsystem


def _actors_by_label() -> dict:
    # 클래스 객체 비교는 커맨드릿에서 어긋나 중복 스폰을 낸다(루프#26) — 라벨로만 찾는다.
    found = {}
    for actor in _actor_subsystem().get_all_level_actors():
        found[actor.get_actor_label()] = actor
    return found


def _mesh_height_cm(mesh: unreal.StaticMesh) -> float:
    bounds = mesh.get_bounding_box()
    return float(bounds.max.z - bounds.min.z)


def _occupied_points(existing: dict) -> list:
    """이번에 세우는 것끼리만이 아니라 **지난 실행이 세워 둔 것**과도 겹치지 않게 시작 목록을 만든다."""
    points = []
    for label, actor in existing.items():
        if not label.startswith(LABEL_PREFIX):
            continue
        location = actor.get_actor_location()
        points.append((float(location.x), float(location.y)))
    return points


def _pick_spot(rng, radius_min: float, radius_max: float, spacing: float, points: list):
    for _ in range(PLACE_TRIES):
        angle = rng.uniform(0.0, math.tau)
        radius = math.sqrt(rng.uniform(radius_min ** 2, radius_max ** 2))   # 넓은 바깥쪽이 성기지 않게
        x = math.cos(angle) * radius
        y = math.sin(angle) * radius
        if math.hypot(x, y) < CLEAR_RADIUS_CM:
            continue
        if abs(x) > FLOOR_HALF_CM or abs(y) > FLOOR_HALF_CM:
            continue
        if all(math.hypot(x - px, y - py) >= spacing for px, py in points):
            return x, y
    return None


def _apply_mesh(actor: unreal.StaticMeshActor, mesh: unreal.StaticMesh, scale: float) -> None:
    component = actor.static_mesh_component
    # Static 인 채로 두면 라이트맵을 구울 때까지 화면에 빨간 "LIGHTING NEEDS TO BE REBUILT" 가
    # 뜨고 그게 3층 캡처에 그대로 찍힌다(루프#26). 메시를 붙이기 **전에** Movable 로 돌린다.
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(mesh)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))


def _place(asset_name: str, count: int, target_height: float, radius_min: float,
           radius_max: float, spacing: float, rng, existing: dict, points: list) -> tuple:
    mesh = unreal.load_asset(NATURE_DIR + "/" + asset_name)
    if mesh is None:
        unreal.log_error("메시 없음: {}".format(asset_name))
        return 0, 0, 0

    height = _mesh_height_cm(mesh)
    if height <= 0.0:
        unreal.log_error("높이 0: {}".format(asset_name))
        return 0, 0, 0
    scale = target_height / height
    slots = len(mesh.static_materials)
    triangles = mesh.get_num_triangles(0)

    placed = 0
    for index in range(count):
        label = "{}{}_{:02d}".format(LABEL_PREFIX, asset_name[len("SM_Nature_"):], index)
        if label in existing:
            # 자리는 그대로 두고 **메시·배율만 다시 맞춘다** — 목표 높이를 고쳐 다시 돌렸을 때
            # 이미 선 것이 옛 배율로 남으면 숲이 두 가지 크기로 섞인다(임포트 스크립트가 이름 규칙을
            # 매번 다시 맞추는 것과 같은 이유).
            actor = existing[label]
            _apply_mesh(actor, mesh, scale)
            location = actor.get_actor_location()
            points.append((float(location.x), float(location.y)))
            placed += 1
            continue
        spot = _pick_spot(rng, radius_min, radius_max, spacing, points)
        if spot is None:
            unreal.log_warning("자리 못 찾음(건너뜀): {}".format(label))
            continue
        x, y = spot
        actor = _actor_subsystem().spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(x, y, 0.0),
            unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
        if actor is None:
            unreal.log_error("스폰 실패: {}".format(label))
            continue
        actor.set_actor_label(label)
        _apply_mesh(actor, mesh, scale)
        points.append((x, y))
        placed += 1

    unreal.log("배치: {} {}개 높이 {:.0f}cm(원본 {:.0f}cm × {:.2f}) 삼각형 {} 슬롯 {}".format(
        asset_name, placed, target_height, height, scale, triangles, slots))
    return placed, placed * triangles, placed * slots


def _apply_floor_material() -> None:
    floor = _actors_by_label().get(FLOOR_LABEL)
    if floor is None:
        unreal.log_error("바닥 액터 없음: {}".format(FLOOR_LABEL))
        return
    material = unreal.load_asset(FLOOR_MATERIAL)
    if material is None:
        unreal.log_error("바닥 머티리얼 없음: {}".format(FLOOR_MATERIAL))
        return
    for component in floor.get_components_by_class(unreal.StaticMeshComponent):
        component.set_material(0, material)
    unreal.log("바닥 머티리얼: {} → {}".format(FLOOR_LABEL, FLOOR_MATERIAL))


def _report() -> None:
    """저장한 뒤 레벨을 **다시 훑어** 실제로 무엇이 섰는지 찍는다.

    스폰 직후의 변수를 믿지 않는다 — 저장이 안 된 컴포넌트는 같은 프로세스 안에서만 멀쩡해 보인다
    (루프#53 함정). 이 줄들을 **새 프로세스에서 다시 돌려** 읽는 것이 2층 검사다.
    """
    floor_material = "없음"
    floor = _actors_by_label().get(FLOOR_LABEL)
    if floor is not None:
        for component in floor.get_components_by_class(unreal.StaticMeshComponent):
            material = component.get_material(0)
            floor_material = material.get_name() if material else "없음"
    unreal.log("확인: 바닥 {} 머티리얼={}".format(FLOOR_LABEL, floor_material))

    counts = {}
    for label, actor in sorted(_actors_by_label().items()):
        if not label.startswith(LABEL_PREFIX):
            continue
        component = actor.static_mesh_component
        mesh = component.static_mesh
        mesh_name = mesh.get_name() if mesh else "없음"
        counts[mesh_name] = counts.get(mesh_name, 0) + 1
        location = actor.get_actor_location()
        scale = actor.get_actor_scale3d()
        height = _mesh_height_cm(mesh) * scale.z if mesh else 0.0
        unreal.log("확인: {} 메시={} 위치=({:.0f},{:.0f}) 배율={:.2f} 화면높이={:.0f}cm".format(
            label, mesh_name, location.x, location.y, scale.x, height))
    unreal.log("확인: 자연물 액터 {}종 {}개".format(len(counts), sum(counts.values())))


if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    unreal.log_error("레벨 없음: {}".format(LEVEL_PATH))
else:
    _level_subsystem().load_level(LEVEL_PATH)

    rng = random.Random(PLACEMENT_SEED)
    existing = _actors_by_label()
    points = _occupied_points(existing)

    total_actors = 0
    total_triangles = 0
    total_slots = 0
    for asset_name, count, target_height, radius_min, radius_max, spacing in PLACEMENTS:
        placed, triangles, slots = _place(
            asset_name, count, target_height, radius_min, radius_max, spacing, rng, existing, points)
        total_actors += placed
        total_triangles += triangles
        total_slots += slots

    _apply_floor_material()

    _level_subsystem().save_current_level()
    unreal.log("L_Hunt 자연물 합계: 액터 {}개 · 삼각형 {} · 머티리얼 슬롯 {}(인스턴싱 전 드로우콜 상한)".format(
        total_actors, total_triangles, total_slots))
    _report()
