"""M0-03 L_Hunt 최소 무대 — PlayerStart·바닥·빛·하늘(+M1-08 자리표시 동물, M5-04b 소·양)을 헤드리스로 넣는다.

실행 (uproject 는 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="tools/unreal/setup_hunt_stage.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

크기·색은 임시 자리표시(설계 숫자 아님, INBOX 지시 2026-09-18 17:05). 이미 해당 클래스 액터가
있으면 건드리지 않는다(재실행해도 안전).
"""
import unreal

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"

# 자리표시 동물을 플레이어 시작점 앞 어디에 둘지. 게임 규칙 숫자가 아니라 **레벨 배치**다
# (공격 사거리 150cm 안이라 하이라이트가 잡히고, 그래야 M1-08 HUD 캡처에 HP 바가 보인다).
ANIMAL_OFFSET_CM = 120.0

# M5-04b: 사냥터에 **3종**이 서야 한다(ROADMAP 합격 기준). 자리는 게임 규칙 숫자가 아니라 레벨 배치다 —
# 쿼터뷰 화면에 세 마리가 같이 들어오게 잡되, **공격 사거리(150cm) 안에는 돼지만** 둬서
# 하이라이트 대상이 하나로 유지되게 한다(HUD 는 사거리 안 가장 가까운 1마리만 잡는다, DESIGN 3.1).
# 종은 `DT_AnimalTable` 의 행 이름으로 고른다 — 액터는 하나고 종은 데이터다 (DataDriven.md 1).
LIVESTOCK = [
    ("Animal_Cow", unreal.Vector(300.0, -260.0, 100.0), "HuntStageCow"),
    ("Animal_Sheep", unreal.Vector(300.0, 260.0, 100.0), "HuntStageSheep"),
]


def _actor_subsystem() -> unreal.EditorActorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert subsystem
    return subsystem


def _level_subsystem() -> unreal.LevelEditorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert subsystem
    return subsystem


def _find_actor_by_label(label: str):
    # 클래스 객체끼리 비교하면 커맨드릿에서 같은 클래스인데도 어긋나 **중복 스폰**이 난다
    # (루프#26 에서 실제로 무대가 두 벌이 됐다). 라벨은 우리가 붙인 이름이라 흔들리지 않는다.
    for actor in _actor_subsystem().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def _has_actor_with_label(label: str) -> bool:
    return _find_actor_by_label(label) is not None


def _spawn(actor_class, location, label: str) -> None:
    if _has_actor_with_label(label):
        unreal.log("skip (already exists): {}".format(label))
        return
    actor = _actor_subsystem().spawn_actor_from_class(actor_class, location)
    if actor is None:
        unreal.log_error("failed to spawn: {}".format(label))
        return
    actor.set_actor_label(label)
    unreal.log("spawned: {}".format(label))
    if actor_class == unreal.StaticMeshActor:
        plane = unreal.load_asset("/Engine/BasicShapes/Plane")
        actor.static_mesh_component.set_static_mesh(plane)
        actor.set_actor_scale3d(unreal.Vector(50.0, 50.0, 1.0))
    elif actor_class == unreal.DirectionalLight:
        actor.set_actor_rotation(unreal.Rotator(pitch=-45.0, yaw=30.0, roll=0.0), False)


def _place_animal(row_name: str, location: unreal.Vector, label: str) -> None:
    """동물 한 마리를 세우고 어떤 종인지(볼 행 이름)를 지정한다.

    이미 있으면 새로 스폰하지 않고 자리·행만 다시 맞춘다 — 좌표를 고쳐 다시 돌리는 일이 잦다(루프#73).
    """
    actor = _find_actor_by_label(label)
    if actor is None:
        actor = _actor_subsystem().spawn_actor_from_class(unreal.AnimalCharacter, location)
        if actor is None:
            unreal.log_error("failed to spawn: {}".format(label))
            return
        actor.set_actor_label(label)
        unreal.log("spawned: {}".format(label))
    actor.set_actor_location(location, False, False)
    actor.set_editor_property("animal_row_name", row_name)
    unreal.log("placed: {} row={} at ({}, {}, {})".format(
        label, row_name, location.x, location.y, location.z))


def _make_movable(label: str) -> None:
    """정적 메시를 Movable 로 돌린다.

    Static 인 채로 두면 라이트맵을 구울 때까지 화면에 빨간 "LIGHTING NEEDS TO BE REBUILT" 가 뜨고,
    그게 3층 캡처에 그대로 찍힌다(루프#26). 자리표시 무대는 라이트맵을 굽지 않으므로 Movable 이 맞다.
    """
    for actor in _actor_subsystem().get_all_level_actors():
        if actor.get_actor_label() != label:
            continue
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
        unreal.log("movable: {}".format(label))


if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    unreal.log_error("level not found: {}".format(LEVEL_PATH))
else:
    _level_subsystem().load_level(LEVEL_PATH)

    _spawn(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0), "MinStagePlayerStart")
    _spawn(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), "MinStageFloor")
    _spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 300.0), "MinStageSun")
    _spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 300.0), "MinStageSky")
    # M1-08: HUD 는 하이라이트된 동물이 있어야 HP 바를 보여준다. 진짜 돼지 메시는 M1-07 몫이라
    # 지금은 DT_AnimalTable 의 돼지 행을 읽는 자리표시 액터 한 마리만 둔다.
    _spawn(unreal.AnimalCharacter, unreal.Vector(ANIMAL_OFFSET_CM, 0.0, 100.0), "HuntStagePig")
    # M5-04b: 소·양을 더 세운다. 라벨로 찾아 **자리와 행을 다시 맞추므로 재실행이 안전**하다 —
    # 화면 가장자리에서 잘리는지는 찍어 봐야 알기 때문에(루프#73) 좌표를 고쳐 다시 돌릴 일이 생긴다.
    for row_name, location, label in LIVESTOCK:
        _place_animal(row_name, location, label)

    _make_movable("MinStageFloor")

    _level_subsystem().save_current_level()
    unreal.log("L_Hunt 최소 무대 저장 완료")
