"""DESIGN 8.9·8.13 — DT_WaterActivityTable(수영·배·두드리기 낚시) 한 행을 만든다.

실행 (인자 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_water_tables.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행 안전: 테이블이 있으면 행만 다시 채운다.
숫자는 전부 DESIGN 8.9 "착수 숫자" 표와 8.13 표에서 온다 — 이 스크립트가 만든 숫자는 없다 (DataDriven.md 3).
"""
import json

import unreal

TABLE_FOLDER = "/Game/Game/Data/Balance"
TABLE_NAME = "DT_WaterActivityTable"
ROW_STRUCT = "/Script/Snarehaven.WaterActivityRow"

WATER_ROWS = [
    {
        "Name": "Default",
        # 8.9
        "SeaWidthCm": 3000,
        "SwimSpeedMul": 2.0,
        "SwimStaminaPerSecond": 12,
        "DrownHealthPerSecond": 5,
        "BoatSpeedMul": 7.5,
        "BoatMotorMul": 1.3,
        "BoatElectricMotorMul": 1.6,
        "BoatSeats": 3,
        # 8.3 탈것 (M6-06)
        "MountSpeedMul": 8.0,
        "MountLengthPerRiderWidth": 3.0,
        "MountSeatBackOfLength": 0.2,
        "MountWalkClipSpeedCmPerSecond": 53.2,
        "MountRunClipSpeedCmPerSecond": 173.6,
        # 8.13
        "CastRangeCm": 400,
        "BiteMinSeconds": 6,
        "BiteMaxSeconds": 12,
        "WormBiteMul": 0.9,
        "TapGoal": 100,
        "TapBasePresses": 10,
        "TapMinutesDivisor": 10,
        "TapDecayPerSecond": 15,
        "TapFailSeconds": 3,
        "SpotCatchesBeforeGone": 3,
        "SpotRespawnSeconds": 300,
        "FarSeaMinCycleMinutes": 120,
    },
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
unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(WATER_ROWS))
unreal.EditorAssetLibrary.save_loaded_asset(table)
unreal.log("[Water] {} 행: {}".format(TABLE_NAME, [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]))
