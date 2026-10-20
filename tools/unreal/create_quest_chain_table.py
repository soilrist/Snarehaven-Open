"""의뢰 줄기 표 DT_QuestChain 을 만든다 (DESIGN 8.14 「의뢰 줄기」, ROADMAP M105-02).

실행 (인자 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_quest_chain_table.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행 안전: 테이블이 있으면 행만 다시 채운다.
숫자 원본은 DESIGN 8.14 「의뢰 줄기」 표다 — 여기를 고치면 DESIGN 줄도 같이 고친다
(2층 `Snarehaven.Quest.Chain.MatchesDesign` 이 맞대어 본다).
"""
import json

import unreal

TABLE_FOLDER = "/Game/Game/Data/Quest"
TABLE_NAME = "DT_QuestChain"
ROW_STRUCT = "/Script/Snarehaven.QuestChainRow"

CHAIN_ROWS = [
    {"Name": "chain.1", "Title": "땔감 부탁",
     "Needs": [{"ItemId": "item.wood", "Count": 30}, {"ItemId": "item.berry", "Count": 10}],
     "RewardCoins": 10, "RewardItemId": "None", "RewardItemCount": 0},
    {"Name": "chain.2", "Title": "물가 부탁",
     "Needs": [{"ItemId": "item.crucian_carp", "Count": 3}, {"ItemId": "item.carp", "Count": 1}],
     "RewardCoins": 12, "RewardItemId": "None", "RewardItemCount": 0},
    {"Name": "chain.3", "Title": "밭 부탁",
     "Needs": [{"ItemId": "item.carrot", "Count": 3}, {"ItemId": "item.potato", "Count": 2},
               {"ItemId": "item.wheat", "Count": 2}],
     "RewardCoins": 16, "RewardItemId": "None", "RewardItemCount": 0},
    {"Name": "chain.4", "Title": "사냥 부탁",
     "Needs": [{"ItemId": "item.fox_tail", "Count": 2}, {"ItemId": "item.goat_horn", "Count": 2},
               {"ItemId": "item.reindeer_meat", "Count": 5}],
     "RewardCoins": 20, "RewardItemId": "None", "RewardItemCount": 0},
    {"Name": "chain.5", "Title": "큰 부탁",
     "Needs": [{"ItemId": "item.egg", "Count": 2}, {"ItemId": "item.crucian_carp", "Count": 5},
               {"ItemId": "item.wood", "Count": 50}],
     "RewardCoins": 22, "RewardItemId": "item.vault_pass", "RewardItemCount": 1},
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
    unreal.log("생성: {}/{}".format(folder, asset_name))
    return table


table = ensure_table(TABLE_FOLDER, TABLE_NAME, ROW_STRUCT)
unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(CHAIN_ROWS, ensure_ascii=False))
unreal.EditorAssetLibrary.save_loaded_asset(table)
unreal.log("[QuestChain] {} 행: {}".format(
    TABLE_NAME, [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]))
