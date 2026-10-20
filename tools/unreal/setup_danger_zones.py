"""M6-04c 위험구역 배치 — `L_Hunt` 에 `ADangerZoneActor` 를 놓는다(`.umap` 은 텍스트로 못 고친다).

실행 (uproject 도 -script 도 **절대경로** — 상대경로는 엔진 Binaries/Mac/ 기준으로 풀려 죽는다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_danger_zones.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

**여기서 정하는 것은 구역 중심 좌표(배치값) 하나뿐이다** — 반지름 500cm·5마리·300초는
`DT_DangerZoneTable` 의 `Danger_HuntField` 행이 들고 있고, 구역은 BeginPlay 에서 그 행을 읽는다
(`ADangerZoneActor::InitializeFromTable`). 설계 숫자를 여기 적으면 사본이 둘이 된다.
이미 있으면 새로 스폰하지 않고 자리·행만 다시 맞춘다(재실행 안전 — 좌표를 고쳐 다시 찍는 일이 잦다, 루프#73).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"
ZONE_LABEL = "DangerZone_HuntField"
ZONE_ROW_NAME = "Danger_HuntField"

# 배치값(설계 숫자 아님): 작은 세계 지도(2026-10-13) 북서 대륙 설원 — 거점 frost_camp(서리골 30600, -38200) 서쪽 60m 설원에서 해안 25m 안쪽 첫 뭍
# (광산 입구는 서쪽이라 겹치지 않는다).
# 행 `ZoneID` 가 설원이라 칸 밖에 놓이면 BeginPlay 가 설원 안 무작위 점으로 옮긴다(`ADangerZoneActor::InitializeFromTable`) —
# 매판 자리가 바뀌지 않게 처음부터 설원 안에 놓는다. 평원 시작점(헤이븐 거점)에서 멀다(#0204 시작점 늑대).
ZONE_X, ZONE_Y = islands.land_point(24600.0, -38200.0, zone="snow", margin=2500.0)
ZONE_CENTER = unreal.Vector(ZONE_X, ZONE_Y, 100.0)


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

zone = _find_actor_with_label(ZONE_LABEL)
if zone is None:
    zone = _actor_subsystem().spawn_actor_from_class(unreal.DangerZoneActor, ZONE_CENTER)
    if zone is None:
        unreal.log_error("failed to spawn: {}".format(ZONE_LABEL))
        raise SystemExit(1)
    zone.set_actor_label(ZONE_LABEL)
    unreal.log("spawned: {} in {}".format(ZONE_LABEL, LEVEL_PATH))
else:
    unreal.log("reuse (already exists): {}".format(ZONE_LABEL))

zone.set_actor_location(ZONE_CENTER, False, False)
# 행 이름 하나만 준다 — 표는 생성자가 이미 물려 뒀고(CDO), 수치는 BeginPlay 에서 읽는다.
zone.set_editor_property("danger_zone_row_name", ZONE_ROW_NAME)
unreal.log("zone: {} row={} at ({}, {}, {})".format(
    ZONE_LABEL, ZONE_ROW_NAME, ZONE_CENTER.x, ZONE_CENTER.y, ZONE_CENTER.z))

_level_subsystem().save_current_level()
unreal.log("위험구역 배치 저장 완료 - {}".format(LEVEL_PATH))
