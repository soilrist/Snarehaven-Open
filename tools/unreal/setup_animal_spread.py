"""INBOX 지시 ★4(감독 2026-09-23) 배치 — `L_Hunt` 에 `AAnimalSpreadActor` 를 놓는다(`.umap` 은 텍스트로 못 고친다).

실행 (uproject 도 -script 도 **절대경로** — 상대경로는 엔진 Binaries/Mac/ 기준으로 풀려 죽는다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_animal_spread.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

**여기서 정하는 것은 흩는 영역의 중심 좌표(배치값) 하나뿐이다** — 16마리·간격 1200cm·시작점 금지
1500cm·90초는 `DT_AnimalSpreadTable` 의 `Spread_HuntField` 행이 들고 있고, 액터는 BeginPlay 에서
그 행을 읽는다(`AAnimalSpreadActor::InitializeFromTable`). 설계 숫자를 여기 적으면 사본이 둘이 된다.
이미 있으면 새로 스폰하지 않고 자리·행만 다시 맞춘다(재실행 안전 — `setup_danger_zones.py` 와 같다).
"""
import unreal

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"
SPREAD_LABEL = "AnimalSpread_HuntField"
SPREAD_ROW_NAME = "Spread_HuntField"

# 배치값(설계 숫자 아님): 맵 한가운데. 흩는 격자는 이 액터를 중심으로 ±2500cm 로 깔리고,
# `L_Hunt` 바닥(`SM_Floor_Hunt`)도 원점을 중심으로 놓여 있어 중심을 옮기면 격자가 바닥 밖으로 나간다.
# Z 는 0 이면 된다 — 자리는 아래로 선 트레이스를 쏴 바닥에 붙인다(`ProjectCellToGround`).
SPREAD_CENTER = unreal.Vector(0.0, 0.0, 0.0)


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


if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    unreal.log_error("level not found: {}".format(LEVEL_PATH))
    raise SystemExit(1)

_level_subsystem().load_level(LEVEL_PATH)

spread = _find_actor_with_label(SPREAD_LABEL)
if spread is None:
    spread = _actor_subsystem().spawn_actor_from_class(unreal.AnimalSpreadActor, SPREAD_CENTER)
    if spread is None:
        unreal.log_error("failed to spawn: {}".format(SPREAD_LABEL))
        raise SystemExit(1)
    spread.set_actor_label(SPREAD_LABEL)
    unreal.log("spawned: {} in {}".format(SPREAD_LABEL, LEVEL_PATH))
else:
    unreal.log("reuse (already exists): {}".format(SPREAD_LABEL))

spread.set_actor_location(SPREAD_CENTER, False, False)
# 행 이름 하나만 준다 — 표는 생성자가 이미 물려 뒀고(CDO), 수치는 BeginPlay 에서 읽는다.
spread.set_editor_property("spread_row_name", SPREAD_ROW_NAME)
unreal.log("spread: {} row={} at ({}, {}, {})".format(
    SPREAD_LABEL, SPREAD_ROW_NAME, SPREAD_CENTER.x, SPREAD_CENTER.y, SPREAD_CENTER.z))

_level_subsystem().save_current_level()
unreal.log("동물 분산 배치 저장 완료 - {}".format(LEVEL_PATH))
