"""M6-04c 2층 검사 — **새 프로세스**에서 `L_Hunt` 를 다시 열어 위험구역이 실제로 저장됐는지,
표가 그 구역을 받아 주는지 찍는다. 놓은 프로세스의 로그는 저장됐다는 증거가 못 된다(루프#53 함정).

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_danger_zones.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

보는 것 다섯: (1) 맵에 구역이 **하나씩만** 있다 (2) 행 이름·표가 setup 이 적은 그대로다
(3) 그 행이 표에 있고 숫자가 전부 정해져 있다 (4) 행의 `ZoneID` 가 **이 맵인 존**을 가리킨다
(5) 구역 원(중심 ± 반지름)이 바닥 밖으로 안 나간다.
(3) 은 `ADangerZoneActor::SetDangerZoneRow` 가 BeginPlay 에서 하는 판정과 같은 질문이다 —
여기서 거절되면 구역이 꺼진 채로 서서 **동물이 한 마리도 안 선다**. (5) 는 그 코드가 못 보는 것이다
(스포너는 맵을 모른다) — 중심을 잘못 옮기면 동물이 바닥 밖에 서서 떨어진다.
"""
import json

import unreal

DANGER_TABLE_PATH = "/Game/Game/Data/World/DT_DangerZoneTable"
ZONE_TABLE_PATH = "/Game/Game/Data/World/DT_ZoneTable"
FLOOR_LABEL = "MinStageFloor"

# (레벨, 구역 라벨, 볼 행 이름) — setup_danger_zones.py 가 적은 그대로.
EXPECTED = [
    ("/Game/Game/Maps/L_Hunt", "DangerZone_HuntField", "Danger_HuntField"),
]

fail = 0


def _rows_by_name(table_path: str):
    """행 이름 -> 행 dict. 행 구조체를 파이썬에서 읽는 API 는 없다 — JSON 으로 내보내 읽는다."""
    table = unreal.EditorAssetLibrary.load_asset(table_path)
    if table is None:
        return None
    exported = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    return {str(row["Name"]): row for row in exported}


danger_rows = _rows_by_name(DANGER_TABLE_PATH)
zone_rows = _rows_by_name(ZONE_TABLE_PATH)
if danger_rows is None or zone_rows is None:
    unreal.log_error("검사 실패 - 표를 못 읽음: {} 또는 {}".format(DANGER_TABLE_PATH, ZONE_TABLE_PATH))
    raise SystemExit(1)
if not danger_rows:
    # 빈 검사 금지(Verification 5) — 행이 0개면 아래 루프가 조용히 다 통과한다.
    unreal.log_error("검사 실패 - 위험구역 표에 행이 0개다")
    raise SystemExit(1)

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

for level_path, label, row_name in EXPECTED:
    level_subsystem.load_level(level_path)
    actors = actor_subsystem.get_all_level_actors()
    zones = [a for a in actors if isinstance(a, unreal.DangerZoneActor)]
    if len(zones) != 1:
        unreal.log_error("검사 실패 - {} 의 위험구역이 {}개(1개여야 함)".format(level_path, len(zones)))
        fail += 1
        continue

    zone = zones[0]
    got_label = zone.get_actor_label()
    got_row = str(zone.get_editor_property("danger_zone_row_name"))
    if (got_label, got_row) != (label, row_name):
        unreal.log_error("검사 실패 - {} 의 구역이 다름: {} row={}".format(level_path, got_label, got_row))
        fail += 1
        continue
    if zone.get_editor_property("danger_zone_table") is None:
        # 표가 비면 구역이 꺼진 채로 선다 — 생성자 경로 참조가 끊긴 경우다.
        unreal.log_error("검사 실패 - {} 의 구역에 표가 안 물려 있다".format(got_label))
        fail += 1
        continue

    row = danger_rows.get(got_row)
    if row is None:
        unreal.log_error("검사 실패 - 표에 없는 행: {}".format(got_row))
        fail += 1
        continue

    radius = float(row.get("RadiusCm", -1.0))
    count = int(row.get("AnimalCount", -1))
    respawn = int(row.get("RespawnSeconds", -1))
    if radius <= 0.0 or count <= 0 or respawn <= 0 or not str(row.get("AnimalRowName", "")):
        unreal.log_error("검사 실패 - {} 행의 수치가 미정이다(반지름 {} 마리수 {} 재생성 {})".format(
            got_row, radius, count, respawn))
        fail += 1
        continue

    # 행이 가리키는 존이 **이 맵**인가. 다른 맵의 구역을 여기 놓으면 표와 화면이 갈라진다.
    zone_id = str(row.get("ZoneID", ""))
    zone_row = zone_rows.get(zone_id) or next(
        (r for r in zone_rows.values() if str(r.get("ZoneID", "")) == zone_id), None)
    map_name = level_path.rsplit("/", 1)[-1]
    if zone_row is None or str(zone_row.get("LevelName", "")) != map_name:
        unreal.log_error("검사 실패 - {} 행의 존 {} 은 이 맵({})이 아니다".format(got_row, zone_id, map_name))
        fail += 1
        continue

    # 구역 원이 바닥 안에 있는가 (스포너는 맵을 몰라서 이걸 못 본다).
    floor = next((a for a in actors if a.get_actor_label() == FLOOR_LABEL), None)
    if floor is None:
        unreal.log_error("검사 실패 - 바닥({})을 못 찾음".format(FLOOR_LABEL))
        fail += 1
        continue
    floor_origin, floor_extent = floor.get_actor_bounds(False)
    center = zone.get_actor_location()
    outside = (abs(center.x - floor_origin.x) + radius > floor_extent.x
               or abs(center.y - floor_origin.y) + radius > floor_extent.y)
    if outside:
        unreal.log_error("검사 실패 - 구역이 바닥 밖으로 나간다: 중심 ({}, {}) 반지름 {} / 바닥 반폭 ({}, {})".format(
            center.x, center.y, radius, floor_extent.x, floor_extent.y))
        fail += 1
        continue

    unreal.log("통과 - {}: {} row={} · 존 {} · 반지름 {} 마리수 {} 재생성 {}초 · 중심 {}".format(
        level_path, got_label, got_row, zone_id, radius, count, respawn, center))

if fail:
    unreal.log_error("위험구역 검사 실패 {}건".format(fail))
    raise SystemExit(1)
unreal.log("위험구역 검사 통과 - 맵 {}개".format(len(EXPECTED)))
