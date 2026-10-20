"""M2-04b 목장 무대 — L_Ranch 레벨 + BP_RanchSlot 슬롯 5개(+M12-05 확장 칸 5개)를 헤드리스로 만든다.

실행 (uproject 도 -script 도 **절대경로** — 상대경로는 엔진 Binaries/Mac/ 기준으로 풀려
`Could not load Python file` 로 죽는다, 루프#30 에서 실제로 겪음):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_ranch_stage.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

칸 수 5 는 DESIGN 3.2 확정 수치(명세 docs/specs/ranch-slot.md 3장). 좌표·간격은 **레벨 배치값**이고
게임 규칙 숫자가 아니다 (setup_hunt_stage.py 의 ANIMAL_OFFSET_CM 과 같은 성격). 이미 있으면
건드리지 않는다(재실행해도 안전).
"""
import unreal

LEVEL_PATH = "/Game/Game/Maps/L_Ranch"
SLOT_BP_PATH = "/Game/Game/Ranch/BP_RanchSlot"

# DESIGN 3.2 확정 수치 — 초기 목장 슬롯 5칸.
SLOT_COUNT = 5

# 배치값: 플레이어 시작점 앞(+X)에 한 줄로, 탑다운 카메라(720cm·60도) 화면에 다섯 칸이 다 들어오는 간격.
SLOT_FORWARD_CM = 250.0
SLOT_SPACING_CM = 200.0

# DESIGN 3.2 확정 수치 — 칸 확장(ExpandRanchSlot) 상한 10칸 (M12-05). 6~10번째 칸은 미리 세워 두고,
# 실행 중에는 `URanchInteractComponent::ApplySlotVisibility` 가 서버 slotCap 만큼만 보이게 한다.
SLOT_MAX = 10
# 배치값: 첫 줄 앞(플레이어 쪽) 둘째 줄. 우물(80,-480)·수레(60,480)에 닿지 않게 간격을 좁힌다.
EXTRA_SLOT_FORWARD_CM = 100.0
EXTRA_SLOT_SPACING_CM = 140.0


def _actor_subsystem() -> unreal.EditorActorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert subsystem
    return subsystem


def _level_subsystem() -> unreal.LevelEditorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert subsystem
    return subsystem


def _has_actor_with_label(label: str) -> bool:
    # 클래스 객체끼리 비교하면 커맨드릿에서 같은 클래스인데도 어긋나 **중복 스폰**이 난다
    # (루프#26 에서 실제로 무대가 두 벌이 됐다). 라벨은 우리가 붙인 이름이라 흔들리지 않는다.
    for actor in _actor_subsystem().get_all_level_actors():
        if actor.get_actor_label() == label:
            return True
    return False


def _spawn(actor_class, location, label: str):
    if _has_actor_with_label(label):
        unreal.log("skip (already exists): {}".format(label))
        return None
    actor = _actor_subsystem().spawn_actor_from_class(actor_class, location)
    if actor is None:
        unreal.log_error("failed to spawn: {}".format(label))
        return None
    actor.set_actor_label(label)
    unreal.log("spawned: {}".format(label))
    return actor


def _make_slot_blueprint():
    """ARanchSlotActor 를 부모로 하는 BP_RanchSlot 을 만든다 (명세 4장의 에셋 이름 그대로).

    몸(기둥+고리)은 C++ 쪽에 있다 — 블루프린트는 맵에 깔 때 칸 번호를 주기 위한 껍데기다.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(SLOT_BP_PATH):
        unreal.log("skip (already exists): {}".format(SLOT_BP_PATH))
        return

    package_path, asset_name = SLOT_BP_PATH.rsplit("/", 1)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.RanchSlotActor)
    new_asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, unreal.Blueprint, factory)
    if new_asset is None:
        unreal.log_error("failed to create: {}".format(SLOT_BP_PATH))
        return
    unreal.EditorAssetLibrary.save_loaded_asset(new_asset)
    unreal.log("created: {}".format(SLOT_BP_PATH))


def _set_ranch_game_mode() -> None:
    """L_Ranch 의 월드 세팅에 ARanchGameMode 를 지정한다 (M2-04c).

    사냥 HUD 를 목장에서 감추는 판단은 게임모드가 한다 — 맵 이름 문자열로 가르지 않는다
    (감독 지시 2026-09-19). 월드 세팅은 `.umap` 안이라 텍스트로 못 고치고 여기서만 바꾼다.
    """
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings_list = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
    if not settings_list:
        unreal.log_error("world settings not found: {}".format(LEVEL_PATH))
        return
    settings_list[0].set_editor_property("default_game_mode", unreal.RanchGameMode)
    unreal.log("game mode override: RanchGameMode")


def _spawn_slots() -> None:
    slot_class = unreal.EditorAssetLibrary.load_blueprint_class(SLOT_BP_PATH)
    if slot_class is None:
        unreal.log_error("blueprint class not found: {}".format(SLOT_BP_PATH))
        return

    # 가운데를 0으로 두고 좌우 대칭으로 깐다 (5칸이면 -2..+2).
    for index in range(SLOT_COUNT):
        offset = (index - (SLOT_COUNT - 1) / 2.0) * SLOT_SPACING_CM
        actor = _spawn(slot_class, unreal.Vector(SLOT_FORWARD_CM, offset, 0.0),
                       "RanchSlot{}".format(index))
        if actor is not None:
            # URanchComponent 의 몇 번째 칸인지. 안 주면 INDEX_NONE 으로 남는다 (RanchSlotActor.h).
            actor.set_editor_property("slot_index", index)

    extra_count = SLOT_MAX - SLOT_COUNT
    for extra in range(extra_count):
        index = SLOT_COUNT + extra
        offset = (extra - (extra_count - 1) / 2.0) * EXTRA_SLOT_SPACING_CM
        actor = _spawn(slot_class, unreal.Vector(EXTRA_SLOT_FORWARD_CM, offset, 0.0),
                       "RanchSlot{}".format(index))
        if actor is not None:
            actor.set_editor_property("slot_index", index)


if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    _level_subsystem().load_level(LEVEL_PATH)
else:
    # new_level 은 빈 레벨을 만들고 바로 연다 — WorldFactory 로 만든 뒤 따로 여는 것보다 한 단계 짧다.
    if not _level_subsystem().new_level(LEVEL_PATH):
        unreal.log_error("failed to create level: {}".format(LEVEL_PATH))
        raise SystemExit(1)
    unreal.log("created: {}".format(LEVEL_PATH))

_make_slot_blueprint()

_spawn(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0), "RanchPlayerStart")
floor = _spawn(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), "RanchFloor")
if floor is not None:
    # ±2500 평판(M18-04 `make_ranch_ground_meshes.py` → `setup_ranch_ground.py`) — 옛 엔진 Plane 자리
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Game/Game/Environment/Ground/SM_Ground_RanchFloor_01"))
    floor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))   # 판이 이미 5000cm — 옛 Plane(100cm) 은 ×50 이었다
    # Static 인 채로 두면 라이트맵을 구울 때까지 캡처에 빨간 "LIGHTING NEEDS TO BE REBUILT" 가 찍힌다(루프#26).
    floor.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)

sun = _spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 300.0), "RanchSun")
if sun is not None:
    sun.set_actor_rotation(unreal.Rotator(pitch=-45.0, yaw=30.0, roll=0.0), False)
_spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 300.0), "RanchSky")

_spawn_slots()
_set_ranch_game_mode()

_level_subsystem().save_current_level()
unreal.log("L_Ranch 목장 무대 저장 완료")
