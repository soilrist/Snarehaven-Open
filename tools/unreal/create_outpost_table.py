"""거점(마을) 이름 표 DT_OutpostTable 을 만든다 (큰 지도 마을 이름 글자·들어설 때 이름 띠의 원본).

실행 (인자 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_outpost_table.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행 안전: 테이블이 있으면 행만 다시 채운다. 행 이름 = 거점 id, 자리는 `data/world_map.json`(`islands.OUTPOSTS`)에서 읽는다.
이름은 DESIGN 지도 절 「마을 이름」 표와 같다 (감독 🟡 2026-10-13 — 레퍼런스 게임 이름은 쓰지 않는다).
"""
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

TABLE_FOLDER = "/Game/Game/Data/World"
TABLE_NAME = "DT_OutpostTable"
ROW_STRUCT = "/Script/Snarehaven.OutpostRow"

ENTER_RADIUS_CM = 3000.0   # DESIGN 마을 이름 표: 거점 틀(setup_outposts 가장 먼 점 ≈1450) 두 배

NAMES = {
    "haven_outpost": "헤이븐 마을",
    "snowbell_post": "눈방울 마을",
    "willow_outpost": "버들 마을",
    "maple_outpost": "단풍 마을",
    "acorn_outpost": "도토리 마을",
    "lakeside_outpost": "호숫가 마을",
    "frost_camp": "서리골",
    "moss_island": "이끼섬",
    "gull_harbor": "갈매기 항구",
    "fern_harbor": "고사리 항구",
    "reed_harbor": "갈대 나루",
    "otter_docks": "수달 나루터",
    "minnow_docks": "피라미 나루",
    "thimble_shop": "골무 장터",
}


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


ids = [o["id"] for o in islands.OUTPOSTS]
if sorted(ids) != sorted(NAMES):
    raise RuntimeError("거점 id 와 이름 표가 다르다: 이름 없음 {} / 거점 없음 {}".format(
        sorted(set(ids) - set(NAMES)), sorted(set(NAMES) - set(ids))))

OUTPOST_ROWS = [{
    "Name": o["id"],
    "DisplayName": NAMES[o["id"]],
    "XCm": float(o["x"]),
    "YCm": float(o["y"]),
    "EnterRadiusCm": ENTER_RADIUS_CM,
} for o in islands.OUTPOSTS]

table = ensure_table(TABLE_FOLDER, TABLE_NAME, ROW_STRUCT)
unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(OUTPOST_ROWS, ensure_ascii=False))
unreal.EditorAssetLibrary.save_loaded_asset(table)
unreal.log("[Outpost] {} 행 {}개".format(TABLE_NAME, len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))))
