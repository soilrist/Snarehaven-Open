"""M5-03b 목장 건물·소품 배치 — 레퍼런스 거점 오두막·돌길·울타리·소품을 `L_Ranch` 에 세운다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_ranch_buildings.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

에셋을 들여놓는 것은 M5-03a(`import_ranch_buildings.py`)가 이미 했다. 이 스크립트는 **레벨에 세우기**만 한다.
`setup_ranch_stage.py`(M2-04b 목장 무대)가 세운 것은 **하나도 건드리지 않는다** — `RanchPlayerStart`·
`RanchFloor`·`RanchSun`·`RanchSky`·`RanchSlot0~9` 와 `BP_RanchSlot`·슬롯 상호작용(M2-04c)은 그대로다
(칸마다 깐 발판·난간만 그 칸 슬롯 액터에 붙인다).
슬롯 판정은 거리(`URanchInteractComponent`)라 소품을 곁에 놔도 가리지 않는다.

**여기 좌표·각도·크기는 전부 레벨 배치값이다 — 게임 규칙 숫자가 아니다**(`setup_hunt_nature.py` 와 같은
성격, Autonomy 🟡). 화면에 들어오는 범위를 먼저 재고 그 안에 담았다: 카메라가 720cm·60도(`SnarehavenCharacter.h`
의 `CameraDistance`/`CameraPitchDegrees`)라 시작 자리에서 **바닥이 보이는 구간은 x -350~850 · y ±800~1200**
이고, 그 밖에 놓은 것은 걸어가기 전에는 화면에 아예 안 나온다. 그래서 목장을 이 사다리꼴 안에 짰다.

크기는 팩마다 기준이 달라 나눠 다룬다(루프#68·#69 가 자연물에서 겪은 것과 같다):
  * **Quaternius 건물은 이미 사람 크기 기준**이다(헛간 773cm 폭 · 울타리 110cm 높이 · 사람 176cm) — 배율 1.
  * **Kenney 소품은 작다**(대문 100cm 폭 · 수레 134cm) — `목표 최대변 ÷ 메시 최대변` 으로 키운다.
배율을 파일에 직접 적지 않는 이유도 자연물 때와 같다 — 팩을 바꾸면 손으로 적은 배율이 같이 틀어진다.

재실행 안전: 액터 라벨(`RanchDeco_*`)이 이미 있으면 새로 세우지 않고 **메시·자리·배율만 다시 맞춘다**.
목록에서 빠진 `RanchDeco_*` 라벨 액터(예: M13-41 에서 뺀 `RanchDeco_Cart_01`)는 **지운다** — 표가 곧 맵이다.
"""
import json

import unreal

LEVEL_PATH = "/Game/Game/Maps/L_Ranch"
BUILDING_DIR = "/Game/Game/Environment/Buildings"
PROP_DIR = "/Game/Game/Environment/Props"

LABEL_PREFIX = "RanchDeco_"
ROOF_TAG = "SnareHouseRoof"   # `USnareHouseRoofSubsystem::RoofTag` — 로컬 플레이어가 그 밑에 들어가면 숨는다
PASS_THROUGH_PREFIX = "Poles_"   # 칸 뒤 장식 울타리 줄 — 폰 충돌을 끈다(`_place`)
# M35-05b 가챠 목장 데코(`docs/specs/gacha-deco.md` 4장) — 데코가 겉모습을 바꿀 액터에 종류 태그를 붙인다.
# 적용 코드(M35-05c)는 이름이 아니라 이 태그로 찾는다(PROMPT ③-12). (라벨 꼬리 접두사, 태그) — 무대 해·하늘빛은 `DECO_STAGE_TAGS`.
DECO_TAGS = (
    (("FenceFront_", "FenceLeft_", "FenceRight_", "Poles_"), "RanchDeco.Fence"),
    (("Path_Gate", "Planks_"), "RanchDeco.Path"),
    (("Coop_01",), "RanchDeco.Barn"),
)
DECO_STAGE_TAGS = {"RanchSun": "RanchDeco.Sky", "RanchSky": "RanchDeco.Sky"}   # `setup_ranch_stage.py` 가 세운 것 — 태그만 더한다
# 속이 빈 집 — 박스 충돌 하나로는 문으로 못 들어간다. 삼각형 그대로 충돌(`setup_harbor.py` 배와 같은 방법)
COMPLEX_COLLISION_MESHES = ("SM_Building_Cottage_01", "SM_Building_CottageRoof_01")
BOX_COLLISION_MESHES = ("SM_Building_Fence_02",)   # M99-04 밧줄 울타리 — 범위 상자 충돌

# 오두막·돌길 슬롯 색 — steam_22 실측 sRGB: 벽 (116,160,137) 박하 · 기와 (205,90,45) · 크림 테두리 (215,190,178) ·
# 문 (182,140,100) · 판석 (193,164,151). 첫 값은 햇빛 받은 무광 색 ≈ 리니어 sRGB × 1.25(`setup_zones.py` 늪 잎) 로 거꾸로 구했다.
MATTE_PARENT = "/Game/Game/Environment/Nature/M_Nature_Matte"   # `setup_zones.py` 가 만든 무광 부모
HOUSE_MATERIALS = {
    # 캡처 두 번(w49·w50)의 채널별 밝기 곡선으로 맞춘 값: 벽 (98,145,111)→(126,162,146), 기와 (172,68,28)→(208,110,62), 문 (169,124,71)→(181,145,119)
    "Wall": ("MI_Building_CottageWall", unreal.LinearColor(0.434, 0.195, 0.060, 1.0)),   # M98-a 꿀갈색 #B07A45
    "Roof": ("MI_Building_CottageRoof", unreal.LinearColor(0.694, 0.479, 0.127, 1.0)),   # M98-a 짚 #D9B864
    "Trim": ("MI_Building_CottageTrim", unreal.LinearColor(0.188, 0.080, 0.026, 1.0)),   # M98-a 진갈색 나무·밧줄 (120,80,45)
    "Door": ("MI_Building_CottageDoor", unreal.LinearColor(0.337, 0.199, 0.132, 1.0)),
    # 실내 마루 — 레퍼런스 집 안 나무 바닥(밝은 꿀색 판자 두 톤). 문 (182,140,100) 보다 밝게
    "Floor": ("MI_Building_CottageFloor", unreal.LinearColor(0.480, 0.290, 0.150, 1.0)),
    "FloorDark": ("MI_Building_CottageFloorDark", unreal.LinearColor(0.400, 0.230, 0.115, 1.0)),
    "Glass": ("MI_Building_CottageGlass", unreal.LinearColor(0.030, 0.040, 0.072, 1.0)),
    "Stone": ("MI_Building_PathStone", unreal.LinearColor(0.352, 0.156, 0.051, 1.0)),   # M98-a 나무 판자 길 (160,110,64)
    # 화단·화분·산울타리·널판 울타리 — steam_22 실측을 같은 ×1.25 규칙으로 거꾸로 구한 첫 값
    "Box": ("MI_Building_PlanterBox", unreal.LinearColor(0.235, 0.175, 0.155, 1.0)),       # 돌 틀 (140,122,108) — w53 0.214·0.158·0.121 은 (130,114,89)
    "Soil": ("MI_Building_PlanterSoil", unreal.LinearColor(0.050, 0.024, 0.017, 1.0)),     # 흙 (72,52,44)
    "Leaf": ("MI_Building_FlowerLeaf", unreal.LinearColor(0.012, 0.131, 0.020, 1.0)),      # 잎 (38,112,48)
    "Red": ("MI_Building_FlowerRed", unreal.LinearColor(0.550, 0.012, 0.008, 1.0)),        # (215,38,32)
    "Yellow": ("MI_Building_FlowerYellow", unreal.LinearColor(0.733, 0.429, 0.012, 1.0)),  # (245,192,38)
    "White": ("MI_Building_FlowerWhite", unreal.LinearColor(0.700, 0.700, 0.662, 1.0)),    # (240,240,234)
    "Purple": ("MI_Building_FlowerPurple", unreal.LinearColor(0.050, 0.020, 0.131, 1.0)),  # (72,48,112)
    "Center": ("MI_Building_FlowerCenter", unreal.LinearColor(0.700, 0.469, 0.033, 1.0)),  # (240,200,60)
    "Hedge": ("MI_Building_Hedge", unreal.LinearColor(0.046, 0.138, 0.038, 1.0)),          # (56,134,52) — w53 (0.028,0.194,0.024)→(4,163,32), w54 (0.075,0.120,0.045)→(86,124,63), w55 (0.065,0.138,0.038)→(76,135,55) 곡선 보간
    "Wood": ("MI_Building_FenceWood", unreal.LinearColor(0.305, 0.122, 0.039, 1.0)),       # M99-04 진갈색 기둥 (150,98,56) — 옛 주황 판자 (205,126,76)
    "Rope": ("MI_Building_FenceRope", unreal.LinearColor(0.768, 0.604, 0.301, 1.0)),       # M99-04 삼밧줄 (227,204,149)
    # M35-05b 가챠 목장 데코 재질(`docs/specs/gacha-deco.md` 4장) — 메시 슬롯에 걸지 않고 만들어만 둔다(적용은 M35-05c).
    # 같은 돌길 메시 위 색이라 돌길 판석(193,164,151)→Stone 값 비(≈0.61)를 그대로 썼다 🟡 — 3층 캡처로 다시 잰다.
    "PathDirt": ("MI_Building_PathDirt", unreal.LinearColor(0.215, 0.110, 0.049, 1.0)),    # 흙길 (160,118,80)
    # 오두막 기와(205,90,45)→Roof 값 비(≈0.87)로 붉은 헛간 지붕 (180,45,40)
    "BarnRoofRed": ("MI_Building_BarnRoofRed", unreal.LinearColor(0.397, 0.023, 0.018, 1.0)),
}
HOUSE_MESHES = ["SM_Building_Cottage_01", "SM_Building_CottageRoof_01", "SM_Building_Path_01", "SM_Building_Planter_01", "SM_Building_Pot_01",
                "SM_Building_Hedge_01", "SM_Building_Fence_02"]
NATURE_DIR = "/Game/Game/Environment/Nature"
FURNITURE_DIR = "/Game/Game/Environment/Furniture"
# 자연물은 팩 원색(청록 잎)이라 사냥터처럼 슬롯 색을 덮는다 — `setup_zones.py` 의 `MI_Nature_Leafs_*` 를 그대로 쓴다
MATERIAL_OVERRIDES = {
    "SM_Nature_Bush_02": {"Leaves_Bush": NATURE_DIR + "/MI_Nature_Leafs_Bush"},
}

# 슬롯 줄의 배치값 — `setup_ranch_stage.py` 의 SLOT_COUNT/SLOT_FORWARD_CM/SLOT_SPACING_CM 과 같은 값을
# 여기서 다시 쓴다(슬롯 곁에 놓으려면 슬롯이 어디 있는지 알아야 한다). 저쪽을 고치면 여기도 같이 고친다.
SLOT_COUNT = 5
SLOT_FORWARD_CM = 250.0
SLOT_SPACING_CM = 200.0
SLOT_PLANK_SIZE_CM = 200.0      # 슬롯마다 깔 발판 — 간격과 같게 해서 다섯 칸이 한 줄 나무바닥이 된다
SLOT_RAIL_FORWARD_CM = 360.0    # 슬롯 뒤 난간 줄(기둥) — 헛간 앞면(x=414)에 닿지 않는 자리
SLOT_RAIL_SIZE_CM = 200.0

# 확장 칸(M12-05) 둘째 줄 배치값 — `setup_ranch_stage.py` 의 EXTRA_SLOT_FORWARD_CM/EXTRA_SLOT_SPACING_CM 과 같다.
# 발판·난간은 첫 줄과 같은 메시를 **칸 간격만큼** 키운다(첫 줄도 간격 = 발판 = 난간 크기). 난간은 둘째 줄 발판
# 뒤끝(첫 줄과 맞닿는 곳)에 선다 — 첫 줄처럼 +110 을 더 밀면 첫 줄 기둥(x=250) 한가운데에 박힌다.
EXTRA_SLOT_FORWARD_CM = 100.0
EXTRA_SLOT_SPACING_CM = 140.0
EXTRA_SLOT_RAIL_FORWARD_CM = EXTRA_SLOT_FORWARD_CM

# 칸 상한은 데이터가 정한다(DESIGN 3.2 `목장 슬롯 상한` · `DT_BaseRuleTable` Base 행 `RanchSlotMax`).
BASE_RULE_TABLE_PATH = "/Game/Game/Data/Balance/DT_BaseRuleTable"
BASE_RULE_ROW = "Base"

# (폴더, 에셋 이름, 라벨 꼬리, x, y, yaw, 목표 최대변 cm 또는 None=팩 원본 크기)
PLACEMENTS = [
    # 뒤 — 레퍼런스 거점(steam_00·steam_22)은 빨간 헛간·사일로·풍차가 아니라 **박하색 벽·주황 기와 오두막**이다(M15-04).
    # 헛간 둘·사일로·풍차를 빼고 `make_house_meshes.py` 오두막 둘을 세운다. 앞면(박공·문)이 -X(카메라)를 본다.
    # x=820 이면 앞면 620 · 문 디딤돌 550 — 슬롯 난간(x=360) 뒤로 한 걸음, 박공 꼭대기까지 화면에 든다.
    (BUILDING_DIR, "SM_Building_Cottage_01", "Cottage_A", 820.0, -330.0, 0.0, None),
    (BUILDING_DIR, "SM_Building_Cottage_01", "Cottage_B", 880.0, 470.0, 0.0, None),
    # 덮개(앞벽·지붕) — 몸통과 같은 자리. 안에 들어가면 숨는다(`ROOF_TAG`, 레퍼런스 실내 보기)
    (BUILDING_DIR, "SM_Building_CottageRoof_01", "Cottage_A_Roof", 820.0, -330.0, 0.0, None),
    (BUILDING_DIR, "SM_Building_CottageRoof_01", "Cottage_B_Roof", 880.0, 470.0, 0.0, None),
    # 실내 가구 — 집 안(벽 안쪽 x 중심±185 · y 중심±255). 레퍼런스 집 안: 탁자·의자·보관함·작업대·벤치
    # 탁자·의자는 x 800 — x 850 이던 때는 탁자 뒤(보관함·상자 더미 사이 x 925~950) 6칸이 갇혔다(HouseFurniture.WalkInsideAndOut).
    (FURNITURE_DIR + "/TableWood", "SM_Furn_TableWood_01", "InA_Table", 800.0, -330.0, 90.0, 145.0),
    (FURNITURE_DIR + "/ChairWood", "SM_Furn_ChairWood_01", "InA_Chair0", 800.0, -440.0, 90.0, 55.0),
    (FURNITURE_DIR + "/ChairWood", "SM_Furn_ChairWood_01", "InA_Chair1", 800.0, -220.0, -90.0, 55.0),
    (FURNITURE_DIR + "/StorageBox", "SM_Furn_StorageBox_01", "InA_Box", 960.0, -530.0, 180.0, 80.0),
    # M18-03 원본 집 안처럼 더 채운다 — 화덕(뒤 오른쪽 구석)·상자 더미(뒷벽, 선반 대신)·등불(앞 왼쪽 구석)·화분(앞 오른쪽 구석).
    # 문(y -330)→탁자 길은 비운다. 침대·러그·선반 메시는 반입된 게 없다(감독 에셋).
    (FURNITURE_DIR + "/GardenBrazier", "SM_Furn_GardenBrazier_01", "InA_Brazier", 950.0, -130.0, 0.0, 90.0),
    (PROP_DIR + "/Harbor", "SM_Prop_Crates_01", "InA_Crates", 965.0, -300.0, 90.0, 110.0),
    (PROP_DIR, "SM_Prop_Lantern_01", "InA_Lantern", 665.0, -555.0, 0.0, 130.0),
    (BUILDING_DIR, "SM_Building_Pot_01", "InA_Pot", 670.0, -115.0, 0.0, 60.0),
    (FURNITURE_DIR + "/WorkbenchAdv", "SM_Furn_WorkbenchAdv_01", "InB_Workbench", 990.0, 470.0, 180.0, 150.0),
    # 긴 의자는 앞벽 쪽으로 붙인다 — x 820 이던 때는 작업대와 함께 뒤 구석(x 900~1025 · y 575~675) 30칸을 막아 못 갔다.
    (FURNITURE_DIR + "/Bench", "SM_Furn_Bench_01", "InB_Bench", 760.0, 650.0, 90.0, 170.0),
    (FURNITURE_DIR + "/StorageBox", "SM_Furn_StorageBox_01", "InB_Box", 990.0, 290.0, 180.0, 80.0),
    # 앞마당 돌 판석 길 — 대문(x=-300)에서 슬롯 발판 앞(x=150)까지(steam_22 문 앞 판석)
    (BUILDING_DIR, "SM_Building_Path_01", "Path_Gate", -380.0, 0.0, 0.0, None),
    # 집 앞 덤불·꽃 — steam_22 는 집 벽을 따라 둥근 술 뭉치 덤불(사냥터 장식 덤불과 같은 `Bush_02`)과 꽃 화분이 선다. 마당 모닥불(steam_00 거점마다 하나)
    (NATURE_DIR, "SM_Nature_Bush_02", "Bush_A0", 575.0, -570.0, 0.0, 120.0),
    (NATURE_DIR, "SM_Nature_Bush_02", "Bush_A1", 575.0, -90.0, 40.0, 110.0),
    (NATURE_DIR, "SM_Nature_Bush_02", "Bush_B0", 635.0, 230.0, 80.0, 115.0),
    (NATURE_DIR, "SM_Nature_Bush_02", "Bush_B1", 635.0, 710.0, 20.0, 120.0),
    # 문 양옆 화분(steam_22) — 팩 꽃(SM_Nature_Flower_01)은 60cm 로 키워도 옅은 결정처럼 보여 빼고 큰 꽃 화분으로
    (BUILDING_DIR, "SM_Building_Pot_01", "Pot_A0", 565.0, -440.0, 0.0, None),
    (BUILDING_DIR, "SM_Building_Pot_01", "Pot_A1", 565.0, -220.0, 90.0, None),
    (BUILDING_DIR, "SM_Building_Pot_01", "Pot_B0", 625.0, 360.0, 180.0, None),
    (BUILDING_DIR, "SM_Building_Pot_01", "Pot_B1", 625.0, 580.0, 270.0, None),
    # 대문 안 돌길 양옆 화단(steam_22 문 앞 판석 길 양옆 돌 틀 화단). 대문 쪽(x -280~-100)에만 — x=-40·길이 270 은
    # 시작점(0,0)→잡화점 손님 자리(-114,600) 캡슐 길을 막았다(ShopPierLevel.CustomerReachesCounter).
    (BUILDING_DIR, "SM_Building_Planter_01", "Planter_L", -190.0, -200.0, 0.0, 180.0),
    (BUILDING_DIR, "SM_Building_Planter_01", "Planter_R", -190.0, 200.0, 180.0, 180.0),
    (FURNITURE_DIR + "/Campfire", "SM_Furn_Campfire_01", "Campfire_01", -150.0, -600.0, 0.0, 130.0),
    (BUILDING_DIR, "SM_Building_Coop_01", "Coop_01", 450.0, -640.0, -20.0, None),
    # 앞 — 플레이어가 서는 자리 좌우. x 를 음수로 두면 **화면 아래 가장자리에 걸려 반이 잘리고**,
    # y 를 ±640 까지 벌리면 이번엔 좌우 끝에서 잘렸다(루프#73 이 캡처로 두 번 재서 잡았다).
    # 계산한 화면 폭(±830)보다 실제로 더 좁게 들어오는 이유는 **메시 피벗이 바운딩박스 가운데가
    # 아니어서**다 — 잰 크기로 미리 맞출 수 없어 캡처를 보고 ±480 으로 좁혔다.
    (BUILDING_DIR, "SM_Building_Well_01", "Well_01", 80.0, -480.0, 15.0, None),
    # 수레(`Cart_01`)는 뺐다 — DESIGN 8.14 「목장 수레」(M13-41): 잡화점 좌판과 겹쳐 카운터로 가는 길을 막았다.
    # 이제 좌판이 우물과 짝을 이루는 오른쪽 앞 표지다. 맵에 남은 옛 수레는 아래 `_remove_stale` 가 지운다.
    # 앞 울타리 — 가운데(돌길)를 비우고 양옆에 널판 울타리. 대문 소품(SM_Prop_Gate_01)은 한쪽 기둥만 떠 보여 뺐다(steam_22 는 틈만 있다).
    # x=-340 은 화면 근거리 끝(-350)에 딱 붙어 등불 기둥까지 잘렸다 — 한 줄 통째로 -300 으로 당겼다.
    (BUILDING_DIR, "SM_Building_Fence_02", "FenceFront_00", -300.0, -520.0, 90.0, None),
    (BUILDING_DIR, "SM_Building_Fence_02", "FenceFront_01", -300.0, 520.0, 90.0, None),
    (PROP_DIR, "SM_Prop_Lantern_01", "Lantern_00", -300.0, -190.0, 0.0, 220.0),
    (PROP_DIR, "SM_Prop_Lantern_01", "Lantern_01", -300.0, 190.0, 0.0, 220.0),
    # 옆 울타리 — 마당 좌우를 막는다(뒤쪽은 헛간·사일로가 막는다). y=±560 은 널빤지 발판
    # 가장자리(±500)에서 60cm 밖이라 붙어 보였다 — ±800 으로 벌려 마당을 냈다.
    # M17-11: 옆도 산울타리(녹색 덩어리) 대신 앞과 같은 나무 널판 울타리(한 토막 597cm, 메시 가운데 기준)로 —
    # x=-50·550 두 토막이 -347~847 을 이어 막는다(옛 산울타리 5토막 -350~650 을 덮는다).
    # 메시 긴 변이 +X 라 yaw 0 이어야 마당 옆(X 방향)을 따라 선다. yaw 90 이던 때는 토막이 마당을 Y 로 가로질러
    # x=550 토막이 집 B 문 앞(y 503~1097)을 막아 집 B 안이 통째로 갇혔다(HouseFurniture.WalkInsideAndOut).
] + [(BUILDING_DIR, "SM_Building_Fence_02", "Fence{}_{:02d}".format(side_name, i), -50.0 + i * 600.0, side_y, 0.0, None)
     for side_name, side_y in (("Left", -800.0), ("Right", 800.0)) for i in range(2)]


def _ensure_house_materials() -> None:
    """슬롯 색 MI 를 만들고(있으면 색만 다시) 집·돌길 메시 슬롯에 건다 — 반입기는 OBJ 머티리얼을 지운다."""
    parent = unreal.EditorAssetLibrary.load_asset(MATTE_PARENT)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    by_slot = {}
    for slot, (name, color) in HOUSE_MATERIALS.items():
        path = BUILDING_DIR + "/" + name
        mat = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else \
            tools.create_asset(name, BUILDING_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mat, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, "DiffuseColor", color)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        by_slot[slot] = mat
    for mesh_name in HOUSE_MESHES:
        path = BUILDING_DIR + "/" + mesh_name
        mesh = unreal.EditorAssetLibrary.load_asset(path)
        if mesh is None:
            unreal.log_error("메시 없음: {} — make_house_meshes.py → import_tree_meshes.py 먼저".format(path))
            continue
        for index, slot in enumerate(mesh.static_materials):
            if str(slot.material_slot_name) in by_slot:
                mesh.set_material(index, by_slot[str(slot.material_slot_name)])
        if mesh_name in COMPLEX_COLLISION_MESHES:
            mesh.get_editor_property("body_setup").set_editor_property(
                "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        if mesh_name in BOX_COLLISION_MESHES:
            # 기둥·밧줄 사이가 비어 몸을 막는 면이 없다 — 메시 범위 상자 하나를 단순 충돌로 단다(마당 울타리가 막아야 한다)
            unreal.load_module("StaticMeshEditor")
            meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            if meshes.get_simple_collision_count(mesh) == 0:
                meshes.add_simple_collisions(mesh, unreal.ScriptingCollisionShapeType.BOX)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def _actor_subsystem() -> unreal.EditorActorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert subsystem
    return subsystem


def _level_subsystem() -> unreal.LevelEditorSubsystem:
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert subsystem
    return subsystem


def _actors_by_label() -> dict:
    # 클래스 객체 비교는 커맨드릿에서 어긋나 중복 스폰을 낸다(루프#26) — 라벨로만 찾는다.
    found = {}
    for actor in _actor_subsystem().get_all_level_actors():
        found[actor.get_actor_label()] = actor
    return found


def _mesh_size_cm(mesh: unreal.StaticMesh) -> tuple:
    bounds = mesh.get_bounding_box()
    return (float(bounds.max.x - bounds.min.x),
            float(bounds.max.y - bounds.min.y),
            float(bounds.max.z - bounds.min.z))


def _slot_max() -> int:
    table = unreal.load_asset(BASE_RULE_TABLE_PATH)
    if table is None:
        unreal.log_error("규칙 표 없음: {}".format(BASE_RULE_TABLE_PATH))
        return SLOT_COUNT
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    for row in rows:
        if row.get("Name") == BASE_RULE_ROW and int(row.get("RanchSlotMax", -1)) > 0:
            return int(row["RanchSlotMax"])
    unreal.log_error("RanchSlotMax 없음: {} {}".format(BASE_RULE_TABLE_PATH, BASE_RULE_ROW))
    return SLOT_COUNT


def _slot_props() -> list:
    """슬롯 칸마다 발판(널빤지) 하나와 뒤 난간(기둥) 하나 — 첫 줄 SLOT_COUNT 칸 + 확장 둘째 줄.

    팩에 올가미 메시가 없어서(루프#71 `확인`) 널빤지로 짠다 — 칸마다 나무 발판을 깔고 그 뒤로
    나무 널판 울타리 토막을 한 줄 세운다(M15-04 — 기둥 줄은 집 앞에 떠 있는 말뚝처럼 보였다). 좌표는 슬롯 좌표에서 끌어온다.
    마지막 칸은 붙일 슬롯 라벨(`RanchSlotN`) — 슬롯을 숨기면 붙은 발판·난간도 같이 숨는다
    (`URanchInteractComponent::ApplySlotVisibility`).
    """
    rows = []
    for index in range(SLOT_COUNT):
        offset = (index - (SLOT_COUNT - 1) / 2.0) * SLOT_SPACING_CM
        slot_label = "RanchSlot{}".format(index)
        rows.append((PROP_DIR, "SM_Prop_Planks_01", "Planks_{:02d}".format(index),
                     SLOT_FORWARD_CM, offset, 0.0, SLOT_PLANK_SIZE_CM, slot_label))
        rows.append((BUILDING_DIR, "SM_Building_Fence_02", "Poles_{:02d}".format(index),
                     SLOT_RAIL_FORWARD_CM, offset, 90.0, SLOT_RAIL_SIZE_CM, slot_label))

    extra_count = _slot_max() - SLOT_COUNT
    for extra in range(extra_count):
        index = SLOT_COUNT + extra
        offset = (extra - (extra_count - 1) / 2.0) * EXTRA_SLOT_SPACING_CM
        slot_label = "RanchSlot{}".format(index)
        rows.append((PROP_DIR, "SM_Prop_Planks_01", "Planks_{:02d}".format(index),
                     EXTRA_SLOT_FORWARD_CM, offset, 0.0, EXTRA_SLOT_SPACING_CM, slot_label))
        rows.append((BUILDING_DIR, "SM_Building_Fence_02", "Poles_{:02d}".format(index),
                     EXTRA_SLOT_RAIL_FORWARD_CM, offset, 90.0, EXTRA_SLOT_SPACING_CM, slot_label))
    return rows


def _apply(actor: unreal.StaticMeshActor, mesh: unreal.StaticMesh, x: float, y: float,
           yaw: float, scale: float) -> None:
    component = actor.static_mesh_component
    # Static 인 채로 두면 라이트맵을 구울 때까지 화면에 빨간 "LIGHTING NEEDS TO BE REBUILT" 가
    # 뜨고 그게 3층 캡처에 그대로 찍힌다(루프#26). 메시를 붙이기 **전에** Movable 로 돌린다.
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_static_mesh(mesh)
    actor.set_actor_location(unreal.Vector(x, y, 0.0), False, False)
    actor.set_actor_rotation(unreal.Rotator(0.0, 0.0, yaw), False)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))


def _place(dest_dir: str, asset_name: str, suffix: str, x: float, y: float, yaw: float,
           target_size: float, existing: dict, slot_label: str = None) -> tuple:
    mesh = unreal.load_asset(dest_dir + "/" + asset_name)
    if mesh is None:
        unreal.log_error("메시 없음: {}/{}".format(dest_dir, asset_name))
        return 0, 0, 0

    size = _mesh_size_cm(mesh)
    longest = max(size)
    if longest <= 0.0:
        unreal.log_error("크기 0: {}".format(asset_name))
        return 0, 0, 0
    # 목표를 안 적으면 팩 원본 크기 그대로다 — Quaternius 건물은 이미 사람(176cm) 기준으로 만들어졌다.
    scale = 1.0 if target_size is None else target_size / longest

    label = LABEL_PREFIX + suffix
    actor = existing.get(label)
    if actor is None:
        actor = _actor_subsystem().spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(x, y, 0.0), unreal.Rotator(0.0, 0.0, yaw))
        if actor is None:
            unreal.log_error("스폰 실패: {}".format(label))
            return 0, 0, 0
        actor.set_actor_label(label)
    # 이미 서 있던 것도 자리·배율을 매번 다시 맞춘다 — 표를 고쳐 다시 돌렸을 때 옛 값이 남지 않게
    # (`setup_hunt_nature.py` 가 배율을 다시 맞추는 것과 같은 이유).
    _apply(actor, mesh, x, y, yaw, scale)
    if suffix.endswith("_Roof"):
        actor.tags = [ROOF_TAG]
    for prefixes, deco_tag in DECO_TAGS:
        if suffix.startswith(prefixes):
            actor.tags = [deco_tag]
    if suffix.startswith(PASS_THROUGH_PREFIX):
        # 칸 뒤 장식 울타리 줄은 몸을 막지 않는다 — 높이 36cm·가로대가 한쪽 면에만 있는 판자라 서→동은 디딤 높이로 넘고
        # 동→서는 x=397 에서 멈추는 한쪽 길이었다(INBOX 결함 2026-10-04 12:30, 감독 PIE 실측). 레퍼런스 칸 장식처럼 지나간다.
        # 응답만 바꾸면 프로필 이름(BlockAll)이 남아 불러올 때 프로필 값으로 되돌아간다(루프#400 실측) — 프로필을 Custom 으로 먼저 돌린다.
        component = actor.static_mesh_component
        component.set_editor_property("use_default_collision", False)
        component.set_collision_profile_name("Custom")
        component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_IGNORE)
    for slot_name, mi_path in MATERIAL_OVERRIDES.get(asset_name, {}).items():
        actor.static_mesh_component.set_material_by_name(slot_name, unreal.load_asset(mi_path))
    if slot_label is not None:
        slot = existing.get(slot_label)
        if slot is None:
            unreal.log_error("붙일 슬롯 없음: {} → {}".format(label, slot_label))
        else:
            actor.attach_to_actor(slot, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                  unreal.AttachmentRule.KEEP_WORLD, False)

    triangles = mesh.get_num_triangles(0)
    slots = len(mesh.static_materials)
    unreal.log("배치: {} 자리=({:.0f},{:.0f}) yaw={:.0f} 크기={:.0f}x{:.0f}x{:.0f}cm 배율={:.2f} 삼각형={} 슬롯={}".format(
        label, x, y, yaw, size[0] * scale, size[1] * scale, size[2] * scale, scale, triangles, slots))
    return 1, triangles, slots


def _tag_stage_for_deco() -> None:
    """무대 해·하늘빛에 `RanchDeco.Sky` 태그만 더한다 — 다른 값은 `setup_ranch_stage.py` 몫이라 건드리지 않는다."""
    found = _actors_by_label()
    for label, deco_tag in DECO_STAGE_TAGS.items():
        actor = found.get(label)
        if actor is None:
            unreal.log_error("데코 태그 붙일 무대 액터 없음: {}".format(label))
            continue
        tags = [str(tag) for tag in actor.tags]
        if deco_tag not in tags:
            actor.tags = list(actor.tags) + [deco_tag]


def _remove_stale(wanted: set) -> None:
    """목록에 없는 `RanchDeco_*` 라벨 액터를 지운다 — 표에서 뺀 소품이 맵에 남지 않게."""
    for label, actor in _actors_by_label().items():
        if label.startswith(LABEL_PREFIX) and label not in wanted:
            _actor_subsystem().destroy_actor(actor)
            unreal.log("삭제: {} (배치 목록에 없음)".format(label))


def _report() -> None:
    """저장한 뒤 레벨을 **다시 훑어** 실제로 무엇이 섰는지 찍는다.

    스폰 직후의 변수를 믿지 않는다 — 저장이 안 된 컴포넌트는 같은 프로세스 안에서만 멀쩡해 보인다
    (루프#53 함정). 이 줄들을 **새 프로세스에서 다시 돌려** 읽는 것이 2층 검사다.
    """
    found = _actors_by_label()
    kept = [label for label in sorted(found) if label.startswith(("RanchSlot", "RanchPlayerStart",
                                                                 "RanchFloor", "RanchSun", "RanchSky"))]
    unreal.log("확인: 무대 액터 그대로 {}개 — {}".format(len(kept), ", ".join(kept)))

    total = 0
    for label in sorted(found):
        if not label.startswith(LABEL_PREFIX):
            continue
        actor = found[label]
        component = actor.static_mesh_component
        mesh = component.static_mesh
        mesh_name = mesh.get_name() if mesh else "없음"
        material = component.get_material(0)
        material_name = material.get_name() if material else "없음"
        location = actor.get_actor_location()
        scale = actor.get_actor_scale3d()
        size = _mesh_size_cm(mesh) if mesh else (0.0, 0.0, 0.0)
        parent = actor.get_attach_parent_actor()
        pawn = "{}/{}".format(component.get_collision_profile_name(),
                              component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN).name)
        unreal.log("확인: {} 메시={} 머티리얼0={} 자리=({:.0f},{:.0f}) 배율={:.2f} 화면크기={:.0f}x{:.0f}x{:.0f}cm 부모={} 폰충돌={}".format(
            label, mesh_name, material_name, location.x, location.y, scale.x,
            size[0] * scale.x, size[1] * scale.y, size[2] * scale.z,
            parent.get_actor_label() if parent else "없음", pawn))
        total += 1
    unreal.log("확인: 목장 배치 액터 {}개".format(total))
    deco_counts = {}
    for actor in found.values():
        for tag in actor.tags:
            if str(tag).startswith("RanchDeco."):
                deco_counts[str(tag)] = deco_counts.get(str(tag), 0) + 1
    unreal.log("확인: 데코 태그 액터 {}".format(", ".join("{} {}".format(k, deco_counts[k]) for k in sorted(deco_counts)) or "없음"))


if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    unreal.log_error("레벨 없음: {}".format(LEVEL_PATH))
else:
    _ensure_house_materials()
    _level_subsystem().load_level(LEVEL_PATH)

    existing = _actors_by_label()
    total_actors = 0
    total_triangles = 0
    total_slots = 0
    for row in PLACEMENTS + _slot_props():
        placed, triangles, slots = _place(*row[:7], existing, *row[7:])
        total_actors += placed
        total_triangles += triangles
        total_slots += slots

    _remove_stale({LABEL_PREFIX + row[2] for row in PLACEMENTS + _slot_props()})
    _tag_stage_for_deco()

    _level_subsystem().save_current_level()
    unreal.log("L_Ranch 목장 배치 합계: 액터 {}개 · 삼각형 {} · 머티리얼 슬롯 {}(인스턴싱 전 드로우콜 상한)".format(
        total_actors, total_triangles, total_slots))
    _report()
