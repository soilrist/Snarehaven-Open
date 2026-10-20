"""처음 들기·처음 다가가기 안내 표 DT_UsageHints 를 만든다 (M28-05a, docs/specs/onboarding-usage-hints.md).

실행 (인자 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_usage_hint_table.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행 안전: 테이블이 있으면 행만 다시 채운다.
행 이름 = 안내 키 (`tool.<ItemID>` · `place.<EPlaceableFunction>` · `ranch.slot` · `vendor.shop`).
ShowSeconds 6 은 명세 3절 「한 줄 떠 있는 시간」(가장 긴 줄 약 28자 ÷ 묵독 5자/초 → 5.6 올림) — 이 스크립트가 만든 숫자는 없다.
`Guide*` 칸 = M28-06 안내서 11쪽(ROADMAP M28-06 활동 목록 순서). 안내 키와 같은 활동은 그 줄에 붙이고,
다가가기·들기 계기가 없는 활동(채집 전반·집 짓기·배치·옷장)은 `guide.*` 줄 — `Line` 이 비어 한 줄 알약으론 안 뜬다.
쪽 글의 버튼 이름은 화면 글자 그대로, 작은따옴표로 감싸고 순서는 > (BalanceChevron 과 같은 글자) — UI 글꼴(Jua+대체)에 「」·→ 가 없어 네모로 나온다(루프#430,
`Snarehaven.Ui.Font.DataTableTextDrawable`). ('놓기' BuildHudWidget · '상점 열기' ShopScreenWidget·BackpackScreenWidget 등).
"""
import json

import unreal

TABLE_FOLDER = "/Game/Game/Data/Guide"
TABLE_NAME = "DT_UsageHints"
ROW_STRUCT = "/Script/Snarehaven.UsageHintRow"

SHOW_SECONDS = 6.0


def hint(name, line, also=None, guide=None):
    row = {"Name": name, "Line": line, "ShowSeconds": SHOW_SECONDS if line else 0.0, "AlsoItemIDs": also or []}
    if guide is not None:
        order, title, icon, steps = guide
        row.update({"GuideOrder": order, "GuideTitle": title, "GuideIconItemID": icon, "GuideSteps": steps})
    return row


def guide_only(name, guide):
    return hint(name, "", guide=guide)


HINT_ROWS = [
    # 도구 — 처음 손에 들 때 (낚싯대 넷·곡괭이 셋은 대표 줄 하나)
    hint("tool.item.rod_basic", "물가 낚시 포인트 앞에서 '낚시' > 찌가 잠기면 연타",
         ["item.rod_bait", "item.rod_lucky", "item.rod_telescopic"],
         (1, "낚시", "item.rod_basic", ["상점에서 낚싯대를 사서 가방에 넣어요",
                                        "물가 낚시 포인트 앞에서 '낚시'",
                                        "찌가 잠기면 '낚시'를 빠르게 연타"])),
    hint("tool.item.hatchet", "나무 앞에서 '베기' — 한 번에 원목 3~5개"),
    hint("tool.item.pickaxe_basic", "광맥 앞에서 '캐기' — 돌·광석이 나와요",
         ["item.pickaxe_advanced", "item.pickaxe_electric"]),
    hint("tool.item.chainsaw", "나무 앞에서 '베기' 한 번에 1.5초 동안 썰어요"),
    # 마체테 — 덤불·채집 더미는 필요 도구가 없어 버튼이 '채집'(GetInteractVerb), 들고 캐면 하나 더(DESIGN 13.5 +1)
    hint("tool.item.machete", "덤불·버섯 더미 앞에서 '채집' — 하나씩 더 나와요"),
    hint("tool.item.gather_basket", "덤불·꽃 앞에서 '채집' — 열매와 꽃을 따요"),
    hint("tool.item.binoculars", "들고 있으면 화면을 더 멀리까지 볼 수 있어요"),
    hint("tool.item.satellite_communicator", "가지고 있으면 왼쪽 위 지도 줄에 내 좌표가 떠요"),
    # 총 — 탄약은 가방 물건이라 '재장전'(HuntHud 공격 왼쪽 버튼)이 가방 탄약을 탄창에 넣고, 쏘기는 '공격' 버튼(M39-03). 소총은 같은 순서.
    hint("tool.item.pistol_semi", "탄약을 사서 '재장전' > 동물 쪽으로 '공격'", ["item.rifle_modern", "item.shotgun", "item.crossbow", "item.pistol_auto", "item.smg", "item.rifle_burst", "item.rifle_semi", "item.shotgun_auto", "item.shotgun_short", "item.mg", "item.bazooka"]),
    hint("tool.item.club", "동물 앞에서 '공격' > 체력이 낮아지면 '포획'", None,
         (3, "사냥·포획", "item.club", ["몽둥이·총을 들고 동물 앞에서 '공격'",
                                       "체력이 낮아지면 버튼이 '포획'으로 바뀌어요",
                                       "잡은 동물은 가방에 — 목장 칸에 넣어요"])),
    # 놓인 것 — 처음 다가갈 때 (불과 화로는 글이 같아도 두 줄 — 기능 이름이 키다)
    hint("place.Workbench", "'열기' > 재료를 골라 만들기", None,
         (5, "제작", "item.plank", ["작업대 앞 '열기' 또는 '배낭' > '제작' 탭",
                                   "만들 것 줄에서 재료 수를 확인해요",
                                   "그 줄을 누르면 재료가 빠지고 물건이 가방에"])),
    hint("place.Campfire", "'열기' > 날고기·물고기를 넣으면 시간이 지나 요리", None,
         (8, "요리", "item.campfire", ["모닥불 앞에서 '열기' > '요리' 탭",
                                      "날고기·물고기 요리 줄을 누르면 굽기 시작",
                                      "남은 초가 끝나면 요리가 가방에"])),
    hint("place.Grill", "'열기' > 날고기·물고기를 넣으면 시간이 지나 요리"),
    hint("place.Smelter", "'열기' > 광석을 넣으면 주괴로 녹아요", None,
         (9, "용광로", "item.iron_ingot", ["제련소 앞에서 '열기' > '제련' 탭",
                                          "광석 줄을 누르면 녹이기 시작",
                                          "남은 초가 끝나면 주괴가 가방에"])),
    hint("place.Storage", "'열기' > 가방 물건을 넣어 두는 칸"),
    # 텐트 — 둘레가 내 땅(DESIGN 8.11), 「열기」는 작업대 판 + '집' 탭(CraftingScreenWidget OpenFor · CraftTabHouse)
    hint("place.Tent", "둘레가 내 땅 — '열기' > 제작·'집' 탭"),
    # 배 — 땅에 안 놓이고 물가에서 띄운다(DESIGN 8.9). 다가가기 = 물가 판 '배 띄우기' 가 뜨는 자리(WaterActionWidget 버튼 글자)
    hint("place.Boat", "물가에서 '배 띄우기' > '타기' > '내리기'"),
    # 건축가 꾸미기(DESIGN 8.11, M108) — 발전기만 「열기」가 연료 판(CraftingScreenWidget OpenFor), 나머지는 배낭 제작 + '가방에 넣기'.
    # 그래서 꾸미기 줄은 그 물건만의 쓰임(오르기·밤 불빛·위에 놓기)을 알리고, 쓰임이 「보기」뿐인 둘은 치우는 길을 알린다.
    hint("place.Generator", "'열기' > 나무·석탄을 넣으면 그동안 전기가 나와요"),
    hint("place.Stairs", "계단을 걸어 오르면 집 2층이에요"),
    hint("place.FlameLight", "밤이 되면 전기 없이 저절로 불이 켜져요"),
    hint("place.Rug", "러그 위에도 가구를 놓을 수 있어요"),
    hint("place.WallDecor", "벽 장식 — 치울 땐 '열기' > '가방에 넣기'"),
    hint("place.TopDecor", "탁자 위 소품 — 치울 땐 '열기' > '가방에 넣기'"),
    # 목장 칸·상인
    hint("ranch.slot", "빈 칸 '넣기' > 동물·씨앗 / 찬 칸 '수확'", None,
         (4, "목장 수확", "item.seed_carrot", ["목장 빈 칸 앞에서 '넣기'",
                                              "'동물 / 씨앗' 탭에서 넣을 것을 골라요",
                                              "시간이 지나 칸이 차면 '수확'"])),
    hint("vendor.shop", "'상점 열기' > 코인으로 도구·텐트를 사요", None,
         (10, "상점", "item.hatchet", ["상인 앞에서 '상점 열기'",
                                      "'사기' 탭 — 코인으로 도구·텐트를 사요",
                                      "'팔기' 탭 — 재료를 코인으로 바꿔요"])),
    # 안내서 쪽만 — 들기·다가가기 계기가 없는 활동 (Line 비움 > 한 줄 알약으론 안 뜸)
    guide_only("guide.gather",
               (2, "채집", "item.wood", ["맨손으로 마른 가지·꽃 덤불 앞에서 '채집'",
                                        "손도끼는 나무 앞 '베기', 곡괭이는 광맥 앞 '캐기'",
                                        "도구가 가방에만 있어도 버튼이 알아서 들어요"])),
    guide_only("guide.build",
               (6, "집 짓기", "item.tent", ["상점에서 산 텐트를 '배낭'에서 고르고 '놓기'",
                                           "'돌리기'로 방향을 잡고 '놓기'",
                                           "텐트 '열기' > '집' 탭 > '집 올리기'"])),
    guide_only("guide.place",
               (7, "배치", "item.storage_box", ["'배낭'에서 놓을 물건을 고르고 '놓기'",
                                               "'놓을 수 있음'이 뜨는 자리에서 '놓기'",
                                               "다시 거둘 땐 '열기' > '가방에 넣기'"])),
    guide_only("guide.wardrobe",
               (11, "옷장", "item.traveler_hat", ["'배낭' > '장비' 탭 > '상점 열기'",
                                                 "이번 주 옷을 골라 사요",
                                                 "산 모자를 고르고 '장착'"])),
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
unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(HINT_ROWS, ensure_ascii=False))
unreal.EditorAssetLibrary.save_loaded_asset(table)
unreal.log("[UsageHints] {} 행: {}".format(
    TABLE_NAME, [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]))
