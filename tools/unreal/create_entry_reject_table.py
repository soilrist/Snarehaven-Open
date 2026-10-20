"""입장 거절 문구 표 DT_EntryRejectTable 을 만든다 (나룻배·섬 입장료 PayFerry + 멀티 방 참가).

실행 (인자 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_entry_reject_table.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행 안전: 테이블이 있으면 행만 다시 채운다.
행 이름 = 거절 코드. 서버 코드는 cloudscript_main.js `PayFerry` 의 fail() 셋, 멀티 코드는
USnareOnlineSubsystem::ClassifyJoinError·ClassifyNetworkFailure 가 만든다.
목장 실패 문구는 여기 두지 않는다 (목장 HUD 가 자기 표를 든다).
"""
import json

import unreal

TABLE_FOLDER = "/Game/Game/Data/World"
TABLE_NAME = "DT_EntryRejectTable"
ROW_STRUCT = "/Script/Snarehaven.EntryRejectRow"

REJECT_ROWS = [
    # 서버 PayFerry
    {"Name": "not_enough_coins", "Message": "코인이 모자란다 ({need} 필요, {have} 있음)"},
    # 50% 는 DESIGN 8.3 표의 값(서버 CODEX_UNLOCK_PERCENT)을 문구로 옮긴 것 — 판정은 서버다.
    {"Name": "locked", "Message": "위험구역 선착장은 도감 사냥+식물 50% 가 먼저다"},
    {"Name": "bad_args", "Message": "그곳으로는 배가 가지 않는다"},
    # 서버에 닿지 못함
    {"Name": "network", "Message": "서버에 닿지 못했다 - 잠시 뒤 다시"},
    # 멀티 입장
    {"Name": "room_full", "Message": "방이 가득 찼다 - 다른 방을 고르자"},
    {"Name": "version_mismatch", "Message": "게임 버전이 달라 들어갈 수 없다 - 업데이트 뒤 다시"},
    {"Name": "room_join_failed", "Message": "방에 들어가지 못했다 - 다시 고르자"},
    # 표에 없는 코드
    {"Name": "unknown", "Message": "들어갈 수 없다 ({code})"},
]


def ensure_table(folder, asset_name, row_struct_path):
    asset_path = "{}/{}".format(folder, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)
    row_struct = unreal.load_object(None, row_struct_path)
    if row_struct is None:
        raise RuntimeError("행 구조체를 못 찾았다: {} (모듈 빌드부터)".format(row_struct_path))
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, folder, unreal.DataTable, factory)
    unreal.log("생성: {}".format(asset_path))
    return table


table = ensure_table(TABLE_FOLDER, TABLE_NAME, ROW_STRUCT)
unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(REJECT_ROWS, ensure_ascii=False))
unreal.EditorAssetLibrary.save_loaded_asset(table)
unreal.log("[EntryReject] {} 행: {}".format(
    TABLE_NAME, [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]))
