"""M6-03a 2층 검사 — **새 프로세스**에서 두 맵을 다시 열어 포탈이 실제로 저장됐는지, 표가 그 포탈을
받아 주는지 찍는다. 놓은 프로세스의 로그는 저장됐다는 증거가 못 된다(루프#53 함정).

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_zone_portals.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

보는 것 넷: (1) 맵마다 포탈이 **하나씩만** 있다 (2) 선 존·갈 존이 setup 이 적은 그대로다
(3) 갈 존이 표에 있고 선 존의 `ConnectedZoneIDs` 에 들어 있다 (4) 갈 존의 `LevelName` 이 실제 맵이다.
(3)·(4) 는 `AZonePortalActor::ResolveTargetZone` 이 BeginPlay 에서 하는 판정과 같은 질문이다 —
여기서 거절되면 포탈이 꺼진 채로 선다(= 버튼이 안 뜬다).
"""
import json

import unreal

ZONE_TABLE_PATH = "/Game/Game/Data/World/DT_ZoneTable"
MAP_FOLDER = "/Game/Game/Maps"

EXPECTED = [
    ("/Game/Game/Maps/L_Hunt", "ZonePortal_ToRanch", "zone.hunt", "zone.ranch"),
    ("/Game/Game/Maps/L_Ranch", "ZonePortal_ToHunt", "zone.ranch", "zone.hunt"),
]

fail = 0

table = unreal.EditorAssetLibrary.load_asset(ZONE_TABLE_PATH)
if table is None:
    unreal.log_error("검사 실패 — 존 표를 못 읽음: {}".format(ZONE_TABLE_PATH))
    raise SystemExit(1)

# 행 구조체를 파이썬에서 읽는 API 는 없다 — JSON 으로 내보내 읽는다(verify_livestock_data.py 와 같은 길).
# 언리얼은 행 이름을 `Name` 칸에 실어 배열로 내보낸다. 키는 **`ZoneID` 칸**으로 잡는다 —
# `AZonePortalActor::ReadZoneRows` 가 그렇게 잡는다(비었을 때만 행 이름).
rows = {}
for row in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)):
    rows[str(row.get("ZoneID") or row["Name"])] = row

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

for level_path, label, source_zone, target_zone in EXPECTED:
    level_subsystem.load_level(level_path)
    portals = [a for a in actor_subsystem.get_all_level_actors()
               if isinstance(a, unreal.ZonePortalActor) and a.get_editor_property("ferry_to") is None]   # 섬 배는 존 포탈이 아니다
    if len(portals) != 1:
        unreal.log_error("검사 실패 — {} 의 포탈이 {}개(1개여야 함)".format(level_path, len(portals)))
        fail += 1
        continue

    portal = portals[0]
    got_label = portal.get_actor_label()
    got_source = str(portal.get_editor_property("source_zone_id"))
    got_target = str(portal.get_editor_property("target_zone_id"))
    if (got_label, got_source, got_target) != (label, source_zone, target_zone):
        unreal.log_error("검사 실패 — {} 의 포탈이 다름: {} {} -> {}".format(
            level_path, got_label, got_source, got_target))
        fail += 1
        continue

    # 표가 이 포탈을 받아 주는가 (ResolveTargetZone 과 같은 질문).
    source_row = rows.get(got_source)
    target_row = rows.get(got_target)
    if source_row is None or target_row is None:
        unreal.log_error("검사 실패 — 표에 없는 존: {} 또는 {}".format(got_source, got_target))
        fail += 1
        continue

    connected = [str(z) for z in source_row.get("ConnectedZoneIDs", [])]
    target_level = str(target_row.get("LevelName", ""))
    if got_target not in connected:
        unreal.log_error("검사 실패 — {} 는 {} 와 이어져 있지 않다(표: {})".format(
            got_target, got_source, connected))
        fail += 1
        continue
    if not target_level or not unreal.EditorAssetLibrary.does_asset_exist(
            "{}/{}".format(MAP_FOLDER, target_level)):
        unreal.log_error("검사 실패 — 갈 맵이 없다: {}".format(target_level))
        fail += 1
        continue

    unreal.log("통과 — {}: {} ({} -> {}) · 갈 맵 {} · 자리 {}".format(
        level_path, got_label, got_source, got_target, target_level,
        portal.get_actor_location()))

if fail:
    unreal.log_error("포탈 검사 실패 {}건".format(fail))
    raise SystemExit(1)
unreal.log("포탈 검사 통과 — 맵 {}개".format(len(EXPECTED)))
