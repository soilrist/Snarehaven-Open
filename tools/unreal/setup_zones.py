"""M12-02 `L_Hunt` 존 채우기 — 하벤 만 지도(`islands.py`) 뭍 전체에 존별 자연물·채집 더미·분산 스포너를 놓는다(`.umap` 은 텍스트로 못 고친다).
땅·바다는 `setup_world_ground.py`·`setup_sea.py` 가 먼저 깐다. 자연물은 50m 구역마다 그 구역 뭍에 많은 존 순으로
구역×존 액터 하나(`ZoneProps_<존>_<i>_<j>`) — 존 경계·모래 해변 밖 자리는 버린다.

실행 (uproject 도 -script 도 **절대경로**, `create_data_tables.py` 먼저 — 칸 자리를 `DT_ZoneTable` 에서 읽는다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_zones.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

**칸 중심·반변은 표가 들고 있다**(DESIGN 1.3 맵 안 존 → `DT_ZoneTable`). 여기 적는 것은 칸마다 바닥 머티리얼·
스포너 라벨·자연물 개수와 색뿐이다(배치값, 🟡 — 게임 규칙 숫자가 아니다). 새 메시는 없다 — `Nature/` 팩 메시에
기존 머티리얼 인스턴스와 같은 부모로 만든 **색만 다른 MI** 를 입힌다(레퍼런스 가을섬 = 주황·빨강 단풍, 눈 존 = 연보라
침엽수 잎, `docs/reference/world_environment.md`). 평원 칸은 원래 바닥·스포너·`HuntNature_*` 를 그대로 쓴다.

자연물은 칸마다 액터 하나(`ZoneProps_<칸>`)에 **메시·색 조합마다 HISM 하나**로 모은다 — 모바일 드로우콜.
보이는 HISM 은 콜리전이 없고, 나무 줄기 자리에만 숨긴 원기둥 HISM 이 막는다(팩 나무 콜리전은 잎까지 감싼 볼록
껍질이라 사람이 잎 폭만큼 튕긴다). 나무는 분산 스포너 격자 칸(`DT_AnimalSpreadTable` 간격)에서 떨어뜨린다 —
스포너는 칸 위에서 바닥 트레이스로 동물을 세운다(`AAnimalSpreadActor::ProjectCellToGround`).
재실행 안전: 라벨로 찾아 자리·값만 다시 맞추고, 자연물 액터는 지우고 같은 시드로 다시 세운다.
**다시 돌리면 뒤이어 `thin_flower_clumps.py` 도 돌린다** — 꽃밭·꽃 군락을 3~6송이 작은 무리로 솎는다(M19-03).
"""
import math
import os
import random
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"
SPREAD_TABLE = "/Game/Game/Data/World/DT_AnimalSpreadTable"
NATURE_DIR = "/Game/Game/Environment/Nature"
GROUND_DIR = "/Game/Game/Environment/Ground"
CYLINDER = "/Engine/BasicShapes/Cylinder"   # 지름 100cm · 높이 100cm · 피벗 가운데

# 색만 다른 MI (🟡 팔레트). 부모는 `M_Nature_Matte`(반사 0·거칠기 1) — `DiffuseColor` 칸만 바꾼다.
# 팩 부모(FBX Lambert)는 반사가 남아 하늘 큐브맵이 잎 윗면을 회녹색(143,164,96)으로 바랬다(w15) — 레퍼런스 잎은 (3,111,75) 처럼 짙고 무광.
# 팩 원색(잎 0.16/0.79/0.67 청록)이 리니어 값 그대로 쓰여 채도가 높다 — 같은 결로 고른다.
MATERIALS = {
    GROUND_DIR + "/MI_Ground_Swamp": unreal.LinearColor(0.110, 0.120, 0.045, 1.0),       # 탁한 녹갈색 늪 바닥
    GROUND_DIR + "/MI_Ground_SwampWater": unreal.LinearColor(0.040, 0.085, 0.080, 1.0),  # 탁한 늪 물웅덩이
    GROUND_DIR + "/MI_Ground_Autumn": unreal.LinearColor(0.360, 0.140, 0.040, 1.0),      # 낙엽 깔린 적갈색 — 0.56/0.27/0.08 은 조명에 모래색으로 떴다(M12-03 첫 캡처)
    # 가을섬 — steam_01 금빛 소나무 양지 (237,164,61)·그늘 (214,132,46), 주홍 소나무 (228,105,46). 0.95/0.40/0.07 은 하늘빛 4.5 에 크림색(w39)
    NATURE_DIR + "/MI_Nature_Leafs_Gold": unreal.LinearColor(0.530, 0.230, 0.030, 1.0),
    NATURE_DIR + "/MI_Nature_Leafs_Orange": unreal.LinearColor(0.480, 0.090, 0.017, 1.0),
    NATURE_DIR + "/MI_Nature_Leafs_Red": unreal.LinearColor(0.820, 0.170, 0.080, 1.0),
    NATURE_DIR + "/MI_Nature_Leafs_Lilac": unreal.LinearColor(0.105, 0.162, 0.455, 1.0),   # 설원 소나무 — steam_18 푸른 라일락 양지 (123,149,198). 0.78/0.72/0.95 는 (235,235,240) 맨 흰색(w40), 0.37/0.42/0.79 도 (206,213,231)(w41), 0.13/0.20/0.56 은 (134,166,216) 9% 밝았다(w42)
    NATURE_DIR + "/MI_Nature_Leafs_Olive": unreal.LinearColor(0.250, 0.320, 0.100, 1.0),
    # 늪 자작나무 — steam_23 잎 (181~201,185~201,0~19) 연두 노랑·줄기 (191~215,170~190,165~183) 분홍빛 흰색·흙 둔덕 (129,95,67).
    # 햇빛 받은 무광 색 ≈ 리니어 값의 sRGB × 1.25(금빛 잎 w40 실측) 로 거꾸로 구했다.
    NATURE_DIR + "/MI_Nature_Leafs_Lime": unreal.LinearColor(0.340, 0.330, 0.018, 1.0),   # 0.010 은 (204,203,0) 파랑이 0(w45)
    NATURE_DIR + "/MI_Nature_Leafs_BirchBark": unreal.LinearColor(0.410, 0.310, 0.290, 1.0),
    GROUND_DIR + "/MI_Ground_SwampDirt": unreal.LinearColor(0.136, 0.072, 0.037, 1.0),
    # 레퍼런스 평원 — 팩 청록 잎은 박하색으로 떴다(M15-04 w4 캡처). steam_17 잎 sRGB 샘플의 리니어 값.
    NATURE_DIR + "/MI_Nature_Leafs_Pine": unreal.LinearColor(0.022, 0.150, 0.070, 1.0),    # 소나무 — steam_17 잎 (1~52,110~151,46~88) 청록. 0.110 은 30% 어둡고(w19) (0.018,0.21,0.05) 는 형광(0,154,42)(w23), 하늘빛 4.5·바닥 밝기에서 (0.028,0.175,0.095) 는 민트(13,151,96)(w30)
    NATURE_DIR + "/MI_Nature_Leafs_Broad": unreal.LinearColor(0.035, 0.175, 0.075, 1.0),   # 활엽수 — (60,137,52) 는 연두로 떴다(w6), 레퍼런스는 소나무와 같은 톤(w23 형광 → 청록 쪽으로)
    # 줄기 — 케니 WoodBarkDark 는 리니어 (0.8,0.46,0.37) 이라 크림색으로 떴다(w6). steam_17 적갈색 줄기. 이름은 Leafs_ 접두로 둬 임포트 정리에서 살아남게 한다.
    NATURE_DIR + "/MI_Nature_Leafs_Bark": unreal.LinearColor(0.200, 0.065, 0.028, 1.0),
    NATURE_DIR + "/MI_Nature_Leafs_Bush": unreal.LinearColor(0.025, 0.190, 0.045, 1.0),    # 덤불 (44,121,60)
    # 바위 — steam_17 바위는 매끈한 푸른 회색 (133,134,155)~(162,152,160). 팩 이끼 텍스처 바위는 녹갈색으로 떴다(w18)
    # 들꽃 주황·파랑 — 2026-09-25 레퍼런스 유튜브 영상 풀밭은 흰·주황·파랑 5잎 꽃이 빽빽한 무리로 핀다(밤에도 파랑이 빛나 보인다)
    NATURE_DIR + "/MI_Nature_Leafs_FlowerOrange": unreal.LinearColor(0.800, 0.250, 0.030, 1.0),
    NATURE_DIR + "/MI_Nature_Leafs_FlowerBlue": unreal.LinearColor(0.060, 0.100, 0.850, 1.0),
    NATURE_DIR + "/MI_Nature_Leafs_BerryDark": unreal.LinearColor(0.060, 0.012, 0.090, 1.0),   # 블랙베리 알 — 검보라(`make_tree_meshes.py` BerryBush_02)
    NATURE_DIR + "/MI_Nature_Leafs_BerryRed": unreal.LinearColor(0.520, 0.004, 0.012, 1.0),   # 링곤베리 알 — 진홍(BerryBush_03). 단풍 Leafs_Red(0.82,0.17,0.08)는 연어색으로 떴다(m1562 첫 캡처)
    NATURE_DIR + "/MI_Nature_Leafs_BerryPink": unreal.LinearColor(0.780, 0.030, 0.140, 1.0),   # 라즈베리 알 — 분홍빛 빨강(BerryBush_05), 링곤 진홍보다 밝고 푸르다
    NATURE_DIR + "/MI_Nature_Leafs_Mezereon": unreal.LinearColor(0.450, 0.020, 0.300, 1.0),   # 메제레움 — 자홍 꽃·열매(BerryBush_06)
    NATURE_DIR + "/MI_Nature_Leafs_Beet": unreal.LinearColor(0.250, 0.008, 0.060, 1.0),   # 사탕무 뿌리 윗머리 — 검붉은 자주(BeetPatch_01)
    NATURE_DIR + "/MI_Nature_Leafs_Wheat": unreal.LinearColor(0.750, 0.500, 0.120, 1.0),   # 밀 이삭 — 밝은 금빛, 줄기는 Leafs_Gold(Wheat_01)
    NATURE_DIR + "/MI_Nature_Leafs_Chanterelle": unreal.LinearColor(0.700, 0.420, 0.008, 1.0),   # 살구버섯 갓 — steam_01 (250,205,60) 진한 노랑. 0.60/0.40/0.025 는 금빛 바닥과 같은 옅은 레몬색으로 묻혔다(m1560 첫 캡처). 밑면은 수지 호박색(`make_tree_meshes.py` Mushroom_02)
    NATURE_DIR + "/MI_Nature_Leafs_Resin": unreal.LinearColor(0.950, 0.360, 0.020, 1.0),   # 수지 방울 — 호박색(`make_tree_meshes.py` ResinPine_01)
    NATURE_DIR + "/MI_Nature_Leafs_Rock": unreal.LinearColor(0.225, 0.205, 0.228, 1.0),   # 둥근 덩이(Boulder) 에 0.260·0.245·0.250 은 양지 (176,175,172)·그늘 (145,152,158) — steam_22 (159,152,159)·(122,123,143) 보다 밝아 sRGB 비의 2.2 제곱만큼 낮춤(w57). 하늘빛 4.5 에서 (0.24,0.24,0.285) 그늘이 (104,134,163) 파랑(w27), (0.22,0.23,0.32) 는 (52,75,109)(w25). steam_17 바위 (119~129,120~130,138~151) 그늘·(174,165,160) 양지. 0.30 회색은 햇빛에 (193,179,160) 베이지로 떴다(w19)
}
MATTE_PARENT = NATURE_DIR + "/M_Nature_Matte"
AMBIENT_FLOOR = 0.25   # 무광 부모 자체발광 = 색 × 이 값 — 그늘 속 잎·바위가 검정으로 안 떨어지는 바닥(w29)

# 분산 스포너 — ZoneID → (라벨 앞머리, 행 이름, 개수). 늪은 서식 종이 없어(8.7) 스포너가 없다.
# 개수(🟡 배치값): 스포너 하나 = 16마리(DESIGN 3.1) · 반변 60m. 존마다 둘 = 동물 96마리 — 모바일·4~8인 복제 부담을 넘지 않게.
SPREADERS = {
    "zone.plains": ("AnimalSpread_HuntField", "Spread_HuntField", 2),
    "zone.snow": ("AnimalSpread_Snow", "Spread_Snow", 2),
    "zone.forest": ("AnimalSpread_Forest", "Spread_Forest", 2),
}
SPREAD_SEPARATION_CM = 25000.0   # 같은 존 스포너끼리 — 한 곳에 몰리지 않게
SPREAD_MIN_FILL = 0.5            # 스포너 격자 칸 중 제 존 뭍이 이 몫 이상인 자리만
# 새 지도 구역 (🟡 배치값) — ZONE_PROPS 개수는 50m 칸 하나 기준이라 구역도 50m, 개수에 구역 안 존 뭍 몫을 곱한다
SECTOR_CM = 5000.0
SECTOR_MIN_LAND = 0.1            # 존 뭍이 구역의 10% 아래면 그 존 자연물을 안 깐다
PILE_LAND_M2 = 1500.0            # 채집 더미 하나당 뭍 넓이 — GATHER_PILES 개수는 존 안 비율로만 쓴다

# 자연물 배치값 (🟡). 나무 높이 = 캐릭터 4~5배(world_environment.md 「침엽수 높이 캐릭터의 4~5배」 — 전 400~450 은 2.5배라 steam_17·22 보다 나무가 작았다).
# 높이는 `setup_hunt_nature.py` 와 같은 사람 캡슐 176cm 기준 목표 높이 — 배율은 실행 중에 구한다.
PROPS_SEED = 1202            # 고정 시드 — 다시 돌려도 같은 자리
EDGE_MARGIN_CM = 250.0       # 칸 가장자리에서 띄움 — 잎이 이웃 칸·바다로 넘어가지 않게
CELL_CLEAR_CM = 300.0        # 나무는 스포너 격자 칸 중심에서 이만큼 떨어진다(잎 반폭 + 동물 캡슐)
TREE_SPACING_CM = 420.0      # 나무끼리 간격 (M15-04 군락 330 → 나무를 1.75배로 키워 420 — 잎끼리 살짝 겹치는 정도)
SNOW_TREE_SPACING_CM = 300.0   # 설원 — steam_18 은 2~3그루가 잎이 닿게 선다. 420 이면 σ320 무리도 한 그루씩 떨어져 보였다(m1553)
RANCH_TREE_SPACING_CM = 320.0   # 목장 — 모래 띠·건물 묶음·카메라 쪽 띠를 빼면 나무 땅이 좁아 420 으론 48 중 17 그루(캡처 dense). 레퍼런스 거점 숲은 수관이 겹친다(steam_05)
PROP_SPACING_CM = 160.0      # 그 밖의 것끼리 간격
SMALL_SPACING_CM = 60.0      # 60cm 아래 풀·꽃·자갈 — 160 이면 군락 사이 빈 땅이 금방 차서 절반이 자리를 못 찾았다(M15-04)
SCALE_JITTER = 0.15          # 목표 높이 ±15% — 같은 메시가 도장 찍은 듯 안 보이게
TRUNK_DIAMETER_CM = 40.0     # 숨긴 줄기 콜리전 원기둥
TRUNK_HEIGHT_CM = 200.0
PLACE_TRIES = 80

L = NATURE_DIR + "/"
ROCK = {"": L + "MI_Nature_Leafs_Rock"}   # 모든 슬롯 — 매끈한 푸른 회색
WOOD = {"": L + "MI_Nature_Leafs_Bark"}   # 통나무·그루터기 — 팩 나무 껍질은 크림색 판자로 떴다(w18)
# 들꽃 포기(`make_house_meshes.py` wild_flower) — 목장 화분과 같은 꽃 머티리얼(`setup_ranch_buildings.py` SLOT 표).
# 팩 꽃(SM_Nature_Flower_01)은 옅은 결정처럼 보였다 — steam_18 흰·노랑 5잎 꽃 무리로 바꾼다.
_FB = "/Game/Game/Environment/Buildings/MI_Building_Flower"
BLOOM = {"Leaf": _FB + "Leaf", "White": _FB + "White", "Yellow": _FB + "Yellow", "Center": _FB + "Center"}
BLOOM_ORANGE = dict(BLOOM, White=L + "MI_Nature_Leafs_FlowerOrange", Yellow=L + "MI_Nature_Leafs_FlowerOrange")
BLOOM_BLUE = dict(BLOOM, White=L + "MI_Nature_Leafs_FlowerBlue", Yellow=L + "MI_Nature_Leafs_FlowerBlue")
FLOWER_CM = 50.0   # 40 은 카메라 7500cm 에서 점으로 묻혔다 — 영상 꽃은 캐릭터 키의 약 ⅓
# 칸 → [(메시, 개수, 목표 높이 cm, {슬롯 이름: MI}, 나무인가)]. 메시 "Puddle" 은 원기둥 물웅덩이(지름 cm 를 높이 칸에).
ZONE_PROPS = {
    # M15-04 레퍼런스 밀도 (world_environment.md 「배치 밀도: 화면당 나무 10~20 그루, 바위 무리 3~5, 빈 공간 절반 이상」).
    # 한 화면 ≈ 34m×19m(캐릭터 키 = 화면 9%) 라 칸(50m×50m) 하나 ≈ 4화면 → 나무 40~80, 군락 면적 합 ≈ 칸의 절반. 나무·바위는 군락(CLUSTERS)으로 모아 빈 땅을 남긴다.
    # 침엽수(Pine_05·06) 키는 사람(176cm)의 4.5배 = 792cm — 감독 보정 2026-10-09(M81-04: steam_17 침엽수는 캐릭터의 4~5배). 이전 560·500(≈3배)은 낮은 원뿔로 보였다. 예전 4.5배는 화면 위로 잘렸다(w57) — 다시 보면 감독 판단.
    # 나무는 `make_tree_meshes.py` 톱니 치마 소나무(Pine_05·06)·가지 소나무(Tree_04, 줄기에서 갈라진 술 치마 뭉치 — steam_17 왼쪽) — 쿼터니우스 Pine 은 위에서 보면 가시 별이라 레퍼런스(steam_17)와 딴판이었다.
    "zone.plains": [   # 레퍼런스 기본 섬 — 짙은 초록 침엽수·활엽수 군락, 회색 바위 무리, 작은 소품은 드문드문(steam_17)
        ("SM_Nature_Pine_05", 18, 792.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Pine_06", 16, 707.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Tree_04", 14, 580.0, {"Leaves_NormalTree": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Tree_04", 10, 480.0, {"Leaves_NormalTree": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Bush_02", 30, 130.0, {"Leaves_Bush": L + "MI_Nature_Leafs_Bush"}, False),
        ("SM_Nature_Boulder_01", 22, 120.0, ROCK, False),
        ("SM_Nature_Boulder_02", 22, 80.0, ROCK, False),
        ("SM_Nature_Stump_01", 6, 75.0, WOOD, False),
        ("SM_Nature_Log_01", 4, 60.0, WOOD, False),
        ("SM_Nature_WildFlower_01", 32, FLOWER_CM, BLOOM, False),
        ("SM_Nature_WildFlower_02", 28, FLOWER_CM, BLOOM, False),
        ("SM_Nature_WildFlower_01", 28, FLOWER_CM, BLOOM_ORANGE, False),
        ("SM_Nature_WildFlower_02", 14, FLOWER_CM, BLOOM_BLUE, False),
        # 평원 풀 포기는 초록 — 팩 금빛 풀잎은 야생 밀 다발과 같은 색이라 밀이 장식 풀로 읽혔다(m1566). 금빛은 밀만
        ("SM_Nature_Grass_02", 20, 45.0, {"": L + "MI_Nature_Leafs_Bush"}, False),
        ("SM_Nature_Pebble_01", 10, 15.0, {}, False),
    ],
    "zone.forest": [   # 레퍼런스 가을섬 — 주황·빨강·노랑 단풍 숲, 덤불·그루터기·통나무
        # 장식 버섯은 뺐다 — steam_01 버섯은 전부 캘 수 있는 살구버섯 무더기(채집 더미). 못 캐는 팩 분홍 버섯이 채집물처럼 보였다
        # steam_01 가을섬 나무는 전부 술 치마 소나무(금빛·주홍) — 공 뭉침 활엽수는 크림색 솜뭉치로 떴다(w39)
        ("SM_Nature_Pine_05", 20, 792.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Gold", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Pine_06", 14, 707.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Gold", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        # 주홍 나무는 steam_01 왼쪽·오른쪽 아래처럼 드러난 줄기 위 납작한 술 우산 뭉치 — 뾰족 소나무에 주홍만 입히면 금빛 나무와 모양이 같았다(m1557)
        ("SM_Nature_Tree_04", 10, 580.0, {"Leaves_NormalTree": L + "MI_Nature_Leafs_Orange", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Tree_04", 10, 480.0, {"Leaves_NormalTree": L + "MI_Nature_Leafs_Orange", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Bush_02", 30, 130.0, {"Leaves_Bush": L + "MI_Nature_Leafs_Red"}, False),
        ("SM_Nature_Boulder_01", 12, 120.0, ROCK, False),
        ("SM_Nature_Boulder_02", 14, 80.0, ROCK, False),
        ("SM_Nature_Stump_01", 10, 75.0, WOOD, False),
        ("SM_Nature_Log_01", 8, 60.0, WOOD, False),
        ("SM_Nature_WildFlower_01", 28, FLOWER_CM, BLOOM, False),
        ("SM_Nature_WildFlower_02", 52, FLOWER_CM, BLOOM, False),
        ("SM_Nature_WildFlower_01", 52, FLOWER_CM, BLOOM_ORANGE, False),
        ("SM_Nature_WildFlower_02", 26, FLOWER_CM, BLOOM_BLUE, False),
        ("SM_Nature_Grass_02", 40, 45.0, {}, False),
    ],
    "zone.snow": [     # 연보라 잎 침엽수 + 바위 (steam_18 눈+꽃) — 2~3그루 무리가 눈밭에 띄엄띄엄, 56그루는 화면을 덮었다(w41)
        ("SM_Nature_Pine_05", 18, 792.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Lilac", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        ("SM_Nature_Pine_06", 14, 707.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Lilac", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
        # steam_18 눈밭 바위는 절벽·나무 밑에 3~5개씩 작게 뭉친다 — 56개는 눈밭 전체에 회색 점으로 흩어졌다(캡처 m1550)
        ("SM_Nature_Boulder_01", 10, 120.0, ROCK, False),
        ("SM_Nature_Boulder_02", 14, 90.0, ROCK, False),
        ("SM_Nature_Stump_01", 6, 75.0, WOOD, False),
        # steam_18 꽃은 풀밭 쪽에만 핀다 — 눈 위엔 나무 밑 파랑 한두 포기. 96포기 꽃무리가 눈밭 한가운데 떴다(캡처 m1550)
        ("SM_Nature_WildFlower_02", 8, FLOWER_CM, BLOOM_BLUE, False),
        ("SM_Nature_Pebble_01", 30, 15.0, {}, False),
    ],
    "zone.swamp": [    # 레퍼런스 자작나무 들판(steam_23) — 흙 둔덕 위 흰 줄기·연두 잎 자작나무 무리, 짙은 초록 덤불, 회색 바위, 작은 물웅덩이
        # 올리브 공 뭉침 활엽수는 연두 솜뭉치로 떴다(w44). 흙 둔덕은 군락 중심마다 하나(DirtPatch = 원기둥 얼룩, 지름 cm).
        ("DirtPatch", 12, 900.0, {"": GROUND_DIR + "/MI_Ground_SwampDirt"}, False),
        ("Puddle", 5, 550.0, {"": GROUND_DIR + "/MI_Ground_SwampWater"}, False),
        ("SM_Nature_Birch_01", 44, 420.0, {"Leaves_NormalTree": L + "MI_Nature_Leafs_Lime", "Bark_Birch": L + "MI_Nature_Leafs_BirchBark"}, True),
        ("SM_Nature_Bush_02", 22, 150.0, {"Leaves_Bush": L + "MI_Nature_Leafs_Bush"}, False),
        ("SM_Nature_Boulder_01", 16, 130.0, ROCK, False),
        ("SM_Nature_Boulder_02", 16, 80.0, ROCK, False),
        ("SM_Nature_Grass_02", 30, 60.0, {}, False),
        ("SM_Nature_Log_01", 6, 60.0, WOOD, False),
    ],
}
# 군락 (🟡 배치값) — 칸마다 군락 중심 수와 퍼짐(가우스 σ cm). 나무·바위·덤불·들꽃은 무리 중심 둘레로, 풀·얼룩은 칸 전체에 흩는다.
# 나무를 1.75배로 키우고 간격을 420 으로 넓히자 σ450 군락이 차서 평원 나무 58 중 19 만 섰다 — 군락을 넓고 많게.
CLUSTERS = {"zone.ranch": (14, 600.0), "zone.plains": (10, 700.0), "zone.forest": (10, 700.0), "zone.snow": (13, 260.0),   # steam_18 설원 = 2~3그루 무리. (9, 680) 은 32그루가 한 그루씩 흩어졌다(m1551)
            "zone.swamp": (12, 150.0)}   # σ170·둔덕 760 은 나무 ¼ 이 둔덕 밖에 섰다(w45). steam_23 자작나무는 흙 둔덕 하나에 4~6그루로 촘촘하다
BIRCH_SPACING_CM = 170.0     # 자작나무끼리 — 잎 뭉치가 가늘어 소나무 간격(420)이면 둔덕에 한두 그루만 섰다
ROCK_CLUSTERS = 12           # 바위 무리 중심 수 — 숲 군락과 따로(「바위 = 3~5개 무리」)
ROCK_SIGMA_CM = 300.0
# 들꽃 무리 — steam_18 은 흰·노랑 꽃 포기 3~5개가 한 자리에 모여 핀다(풀밭 전체에 한 포기씩 흩지 않는다).
# 2026-09-25 유튜브 영상은 무리 하나가 꽃 수십 송이로 더 빽빽하고 넓다 — 색을 섞어 같은 중심에 두 배로 심는다.
FLOWER_PATCHES = 10
FLOWER_SIGMA_CM = 130.0   # 220 은 무리가 흩어져 포기 하나씩 보였다(2026-09-25 캡처 hunt_day)
# 꽃밭 — 2026-09-25 레퍼런스 유튜브 영상 풀밭은 지름 4~5m 둥근 꽃밭에 한 색 꽃이 촘촘히 깔린다(가장자리에서만 색이 섞인다).
# 군락(FLOWER_PATCHES)만으로는 포기가 드문드문이라 칸마다 꽃밭 몇 개를 따로 깐다. 전부 배치값(🟡).
MEADOWS = {"zone.plains": 6, "zone.forest": 5, "zone.ranch": 4}   # 설원 0 — steam_18 눈 위엔 꽃밭이 없다
MEADOW_RADIUS_CM = 250.0
MEADOW_STEP_CM = 55.0        # 포기 간격 — 꽃 한 포기 폭(≈45cm)보다 살짝 넓게
MEADOW_MIX = 0.2             # 가장자리 바깥 ⅓ 에서 옆 색이 섞이는 몫
# 낙엽 — steam_01 가을섬 바닥은 붉은·주황 잎이 나무 밑에 몰리고 풀밭엔 드문드문이다. 칸마다 (나무당 잎 수, 흩뿌림 잎 수). 전부 배치값(🟡).
FALLEN_LEAVES = {"zone.forest": (30, 1500)}   # (14, 260) 은 한 화면에 20장 남짓 — steam_01 은 150장 넘게 깔렸다(m1557)
LEAF_TREE_RING_CM = (120.0, 420.0)   # 나무 줄기에서 잎이 떨어지는 거리 — 치마 반지름(≈200·배율) 안팎
LEAF_COLORS = [("Red", NATURE_DIR + "/MI_Nature_Leafs_Red"), ("Orange", NATURE_DIR + "/MI_Nature_Leafs_Orange")]
LEAF_Z_CM = 0.5
# 모래 해변(setup_beach.py) 띠 — 원판 지름 최대 650cm·바다로 60cm 이상 내밀어 섬 가장자리 안쪽 ≈ 590cm 까지 모래다.
# 꽃이 모래 위에 피었다(2026-09-25 캡처 house_in) — 레퍼런스 해변엔 꽃이 없다. 섬 = (중심 x, 중심 y, 반변), 레벨마다 바꿔 끼운다.
BEACH_BAND_CM = 650.0
# 모래 위엔 바위·자갈만 — 나무·덤불·그루터기가 모래에 섰다(2026-09-25 캡처 beach2). 레퍼런스 해변(steam_17)은 비어 있다
BEACH_PROPS = ("SM_Nature_Boulder", "SM_Nature_Pebble")
# L_Hunt = None → 여러 섬 해안(`islands.py`). L_Ranch 는 (중심 x, 중심 y, 반변) 네모 섬 하나.
island = None
biome = None   # L_Hunt 에서 지금 까는 존 이름(islands.zone_at) — 다른 존 자리는 바다처럼 버린다
sector_scale = 1.0   # 지금 구역의 개수 배율(존 뭍 몫)
SEA_EDGE_CM = 150.0   # 모래 밖 소품(바위·자갈)·채집 더미도 해안에서 이만큼은 안쪽 — 바다에 서지 않게


def _coast_depth(wx, wy):
    """해안에서 뭍 안쪽으로 잰 거리(cm, 바다면 음수)."""
    if island is None:
        if biome and islands.zone_at(wx, wy) != biome:
            return -1.0
        return -islands.signed_distance(wx, wy)
    cx, cy, half = island
    return half - max(abs(wx - cx), abs(wy - cy))


def _on_beach(wx, wy):
    """모래 해변 또는 바다."""
    return _coast_depth(wx, wy) < BEACH_BAND_CM


MEADOW_COLORS = [("SM_Nature_WildFlower_01", "White", BLOOM), ("SM_Nature_WildFlower_01", "Orange", BLOOM_ORANGE),
                 ("SM_Nature_WildFlower_02", "Blue", BLOOM_BLUE)]   # 노랑 꽃밭은 영상에 없다 — 캡처 ranch_meadow 에서 노랑이 튀었다
CLUSTERED = ("SM_Nature_Pine", "SM_Nature_Tree", "SM_Nature_Birch", "SM_Nature_Boulder", "SM_Nature_Bush", "SM_Nature_WildFlower", "SM_Nature_Stump", "SM_Nature_Log")
OBSTACLE_CLEAR_CM = 250.0    # 칸 안의 다른 액터(상점·부두·포털·플레이어 시작) 둘레 공터
START_CLEAR_CM = 350.0       # 플레이어 시작 둘레 공터 — 캐릭터가 나무에 파묻히지 않을 만큼만(700 은 첫 화면이 빈 풀밭이었다 — 레퍼런스는 시작부터 숲이 보인다)
# 고원(`Plateau_`)은 피하지 않는다 — 회전된 경계 상자(+여유)가 평원 칸 절반을 막아 나무 58 중 17 만 섰다.
# 뒤에 도는 `setup_plateaus.py` 가 고원 자리 소품을 지우고 채집물을 밀어낸다.
PROP_SKIP_LABELS = ("ZoneProps_", "ZoneFloor_", "HuntNature_", "MinStageFloor", "Sea", "AnimalSpread_", "Plateau_")
# M13-02 채집 더미 (DESIGN 13.4) — 칸 → [(행 이름 꼬리, 곳 수)]. 곳 수는 13.4, 간격은 13.4 🟡 "더미 몸·자리".
# 몸 메시는 행의 `MeshRef` 가, 몸 높이는 행의 `BodyHeightCm`(DESIGN 3.3 더미 크기 표)이 BeginPlay 에 정한다 —
# 여기서도 같은 칸을 읽어 배율을 맞춘다(에디터 화면과 게임이 같게, 원천은 표 하나).
GATHER_PILES = {
    "zone.forest": [("MushroomPile", 6), ("ForestBush", 4), ("LingonBush", 2)],   # 수지는 벤 나무 덤(8.10) — 수지 나무 더미는 뺐다. 가을섬은 좁아 12곳
    # 나무위키 레퍼런스 평원 "꿩과 밀, 사탕무, 베리류, 꽃 종류가 스폰된다" — 블랙베리(VN24 건조한 곳)·라즈베리·사탕무·밀
    "zone.plains": [("FlowerPatch", 6), ("PlainsBush", 4), ("RaspberryBush", 3), ("BeetPatch", 3), ("WheatPatch", 3)],
    # 광맥(DESIGN 8.16)은 설원 광산 굴 안으로 옮겼다 — 원본 광맥은 광산 안에 있다. 배치는 `setup_mine.py`
    "zone.snow": [("SnowBush", 4)],
    # 메제레움 = 원본 Pix's Island(작은 섬) — 셋째 = 그 섬(`islands.py`) 뭍에만. 늪 버섯·늪꽃·수련 = 레퍼런스 Wild Growth Update 늪 채집물
    "zone.swamp": [("MezereonBush", 2, "moss_island"), ("SwampMushroom", 5), ("BogFlower", 3), ("WaterLily", 2)],
}
# 목장(L_Ranch) 둘레 숲 — 레퍼런스 거점은 나무 군락에 둘러싸인 풀밭이다(steam_00·steam_22). 울타리·헛간 쪽은 비운다.
RANCH_LEVEL = "/Game/Game/Maps/L_Ranch"
RANCH_HALF_CM = 2500.0
# 칸 중심 기준 사각형 — 울타리·헛간·풍차 묶음(x -300~1350, y -1100~1100) + 2m 여유, 대문에서 사냥터 포털(0,-2600)까지 길
# 셋째 = 남쪽 해안 항구 부두·잔교(`setup_harbor.py`, DESIGN 8.9 거점 항구)
RANCH_KEEP_OUT = [(525.0, 0.0, 1050.0, 1300.0), (-150.0, -1850.0, 350.0, 800.0), (1400.0, -2600.0, 1250.0, 350.0)]
# 나무만 피하는 카메라 쪽(화면 아래 = -X) 띠 — 7~8m 수관이 화면 위로 ≈660cm 투영돼 울타리·대문을 덮었다(w19).
# 레퍼런스 집 앞은 트여 있고 덤불·꽃·바위만 있다(steam_17). 울타리 남쪽 x=-300 에서 수관 투영+반지름(≈900cm) 만큼.
RANCH_TREE_KEEP_OUT = [(-800.0, 0.0, 600.0, 1400.0)]
# 레퍼런스 거점(steam_00·steam_22)은 집 둘레가 빽빽한 숲 무리다 — 나무 34·군락 8 은 26 그루만 서서 풀밭이 휑했다(캡처 beach2).
# 평원 칸(나무 54·덤불 30·바위 38) 수준으로 올린다(목장 칸은 건물 묶음·항구가 ⅓ 을 차지)
RANCH_PROPS = [
    ("SM_Nature_Pine_05", 20, 792.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
    ("SM_Nature_Pine_06", 16, 707.0, {"Leaves_Pine": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
    ("SM_Nature_Tree_04", 12, 580.0, {"Leaves_NormalTree": L + "MI_Nature_Leafs_Pine", "Bark_NormalTree": L + "MI_Nature_Leafs_Bark"}, True),
    ("SM_Nature_Bush_02", 30, 130.0, {"Leaves_Bush": L + "MI_Nature_Leafs_Bush"}, False),
    ("SM_Nature_Boulder_01", 16, 120.0, ROCK, False),
    ("SM_Nature_Boulder_02", 18, 80.0, ROCK, False),
    ("SM_Nature_Stump_01", 5, 75.0, WOOD, False),
    ("SM_Nature_Log_01", 3, 60.0, WOOD, False),
    ("SM_Nature_Grass_02", 20, 45.0, {}, False),
    ("SM_Nature_WildFlower_01", 20, FLOWER_CM, BLOOM, False),
    ("SM_Nature_WildFlower_02", 16, FLOWER_CM, BLOOM, False),
    ("SM_Nature_WildFlower_01", 16, FLOWER_CM, BLOOM_ORANGE, False),
    ("SM_Nature_WildFlower_02", 8, FLOWER_CM, BLOOM_BLUE, False),
]
GATHER_TABLE = "/Game/Game/Data/Gatherables/DT_GatherableTable"
GATHER_SEED = 1302
GATHER_SPACING_CM = 400.0    # 더미끼리 — 채집 사거리보다 넓게
GATHER_CLEAR_CM = 100.0      # 칸 자연물·액터 바깥으로 — 150 은 가을섬(칸보다 작은 섬)에 덤불 셋째 종을 넣자 수지 나무 자리가 안 남았다. 더미 반폭 약 90 이라 100 이면 잎 끝과 10cm 띈다
ISLET_REACH_CM = 2500.0      # 섬 제한 더미를 고르는 거점 둘레 반폭
GATHER_PLANE_EXTENT_CM = 2000.0   # 반폭이 이보다 큰 액터(바닥·바다 판·하늘)는 피하지 않는다

PUDDLE_Z = 1.0          # 바닥 바로 위 (`setup_sea.py` 연못과 같은 높이)
PUDDLE_THICK = 0.02


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _find(label):
    for actor in _actors().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def _table_rows(path, key, columns):
    """표를 열 문자열로 읽는다 — 행 구조체를 파이썬에서 직접 못 꺼낸다."""
    table = unreal.EditorAssetLibrary.load_asset(path)
    lib = unreal.DataTableFunctionLibrary
    keys = lib.get_data_table_column_as_string(table, key)
    cols = [lib.get_data_table_column_as_string(table, c) for c in columns]
    return {k: vals for k, *vals in zip(keys, *cols)}


def _spread_cells(zone_id, zone_half):
    """스포너 격자 칸 (칸 중심 기준 오프셋) — `AAnimalSpreadActor::MakeSpreadCells` 와 같은 식. 스포너 없는 칸은 빈 목록."""
    row = _table_rows(SPREAD_TABLE, "ZoneID", ["AreaHalfExtentCm", "MinSpacingCm"]).get(zone_id)
    if not row:
        return []
    half = min(float(row[0]), zone_half)   # FitToZone 이 칸 반변으로 자른다
    spacing = float(row[1])
    per_axis = int(math.floor(2.0 * half / spacing)) + 1
    first = -spacing * (per_axis - 1) * 0.5
    return [(first + i * spacing, first + j * spacing) for i in range(per_axis) for j in range(per_axis)]


def _matte_parent():
    """무광 부모 머티리얼 — 없을 때만 만든다(색 벡터 → 바탕색, 반사 0, 거칠기 1).
    존 소품은 HISM 이라 `used_with_instanced_static_meshes` 가 꺼져 있으면 게임에서 기본 회색 머티리얼로 떨어진다(w17)."""
    lib = unreal.MaterialEditingLibrary
    if unreal.EditorAssetLibrary.does_asset_exist(MATTE_PARENT):
        mat = unreal.EditorAssetLibrary.load_asset(MATTE_PARENT)
        if mat.get_editor_property("used_with_instanced_static_meshes") and lib.get_num_material_expressions(mat) == 5:
            return mat
        unreal.EditorAssetLibrary.delete_asset(MATTE_PARENT)   # 옛 판(바닥 밝기 없음·w33 땅 반사 실험판) — 다시 만든다, MI 는 아래에서 부모를 다시 건다
    folder, name = MATTE_PARENT.rsplit("/", 1)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "DiffuseColor")
    lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop, y in ((0.0, unreal.MaterialProperty.MP_SPECULAR, 200), (1.0, unreal.MaterialProperty.MP_ROUGHNESS, 300)):
        constant = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, y)
        constant.set_editor_property("r", value)
        lib.connect_material_property(constant, "", prop)
    # 바닥 밝기 — 자기 그림자 속 술 안쪽이 (0,8,4) 검정으로 떴다(w24~w29). 레퍼런스 그늘 잎은 짙은 초록이지 검정이 아니다.
    floor = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 400)
    floor.set_editor_property("parameter_name", "AmbientFloor")
    floor.set_editor_property("default_value", AMBIENT_FLOOR)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 400)
    lib.connect_material_expressions(color, "", glow, "A")
    lib.connect_material_expressions(floor, "", glow, "B")
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MATTE_PARENT, only_if_is_dirty=False)
    return mat


def _ensure_materials():
    parent = _matte_parent()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for path, color in MATERIALS.items():
        folder, name = path.rsplit("/", 1)
        mat = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else \
            tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mat, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, "DiffuseColor", color)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def _add_hism(actor, name):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = subsystem.k2_gather_subobject_data_for_instance(actor)[0]
    handle, fail = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
        parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent))
    if not fail.is_empty():
        raise RuntimeError("HISM 추가 실패 {}: {}".format(name, fail))
    subsystem.rename_subobject(handle, unreal.Text(name))
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    comp = lib.get_associated_object(lib.get_data(handle))
    # Static 이면 라이트맵을 굽기 전까지 "LIGHTING NEEDS TO BE REBUILT" 가 캡처에 찍힌다(`setup_hunt_stage.py`).
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    return comp


def _pick(rng, half, spacing, points, blocked, anchors=None, sigma=0.0, obstacles=(), sand=None):
    """칸 안(중심 기준 오프셋) 한 자리. points = 이미 선 것 [(x, y, 간격)], blocked = 피할 칸 중심.
    anchors 가 있으면 군락 중심 하나를 골라 가우스로 퍼뜨린다. obstacles = 피할 사각형 [(x, y, 반폭 x, y)].
    sand(x, y) 가 참인 자리(모래 해변)는 버린다."""
    reach = half - EDGE_MARGIN_CM
    for _ in range(PLACE_TRIES):
        if anchors:
            ax, ay = rng.choice(anchors)
            x, y = rng.gauss(ax, sigma), rng.gauss(ay, sigma)
            if abs(x) > reach or abs(y) > reach:
                continue
        else:
            x, y = rng.uniform(-reach, reach), rng.uniform(-reach, reach)
        if any(abs(x - ox) < ex + OBSTACLE_CLEAR_CM and abs(y - oy) < ey + OBSTACLE_CLEAR_CM for ox, oy, ex, ey in obstacles):
            continue
        if any(math.hypot(x - bx, y - by) < CELL_CLEAR_CM for bx, by in blocked):
            continue
        if sand and sand(x, y):
            continue
        if all(math.hypot(x - px, y - py) >= (spacing + ps) * 0.5 for px, py, ps in points):
            return x, y
    return None


_obstacle_cache = None   # 레벨 하나의 월드 사각형 목록 — 구역 수백 개가 레벨을 매번 다시 훑지 않게. 레벨을 바꾸면 None


def _zone_obstacles(x0, y0, half):
    """칸 안 다른 액터(상점·부두·포털·플레이어 시작) 사각형 — 칸 중심 기준 오프셋. 바닥·바다 판처럼 큰 것은 뺀다."""
    global _obstacle_cache
    if _obstacle_cache is None:
        _obstacle_cache = _world_obstacles()
    return [(ox - x0, oy - y0, ex, ey) for ox, oy, ex, ey in _obstacle_cache
            if abs(ox - x0) < half + ex + 500 and abs(oy - y0) < half + ey + 500]


def _world_obstacles():
    boxes = []
    for actor in _actors().get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith(PROP_SKIP_LABELS) or actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            continue
        origin, extent = actor.get_actor_bounds(False)
        # 두 변이 다 클 때만 판(바닥·바다·하늘) — 한 변만 긴 강 띠(`River_1` 500×10000)는 피한다. max 로 재면 강 속에 나무·덤불이 섰다(09-25)
        if min(extent.x, extent.y) > GATHER_PLANE_EXTENT_CM:
            continue
        clear = START_CLEAR_CM if isinstance(actor, unreal.PlayerStart) else 0.0
        boxes.append((origin.x, origin.y, max(extent.x, clear), max(extent.y, clear)))
        if max(extent.x, extent.y) > 500.0:
            unreal.log("[ZoneProps] 큰 장애물 {} ({:.0f}, {:.0f}) 반폭 {:.0f}×{:.0f}".format(label, origin.x, origin.y, extent.x, extent.y))
    unreal.log("[ZoneProps] 피할 액터 {}개".format(len(boxes)))
    return boxes


def _anchors(rng, reach, count, obstacles, sand=None):
    """군락 중심 — 장애물 사각형·모래 밖에서만 고른다(목장 가운데에 중심이 잡히면 나무가 전부 튕겨 나간다)."""
    anchors = []
    for _ in range(count * PLACE_TRIES):   # 구역 가장자리 존 조각은 자리가 드물다 — 못 채워도 끝낸다
        if len(anchors) >= count:
            break
        x, y = rng.uniform(-reach, reach), rng.uniform(-reach, reach)
        if sand and sand(x, y):
            continue
        if not any(abs(x - ox) < ex and abs(y - oy) < ey for ox, oy, ex, ey in obstacles):
            anchors.append((x, y))
    return anchors


def _place_zone_props(zone_id, x0, y0, half, label=None, cells=None):
    label = label or "ZoneProps_" + zone_id.split(".")[1].capitalize()
    old = _find(label)
    if old:
        _actors().destroy_actor(old)
    obstacles = _zone_obstacles(x0, y0, half) + (RANCH_KEEP_OUT if zone_id == "zone.ranch" else [])
    tree_obstacles = obstacles + (RANCH_TREE_KEEP_OUT if zone_id == "zone.ranch" else [])
    # 루트 없는 빈 액터 — 첫 HISM 이 루트가 된다. 인스턴스 좌표는 칸 중심 오프셋이다.
    actor = _actors().spawn_actor_from_class(unreal.Actor, unreal.Vector(x0, y0, 0.0))
    actor.set_actor_label(label)

    rng = random.Random("{}:{}".format(PROPS_SEED, zone_id))   # 문자열 시드는 실행마다 같다(hash() 는 아니다)
    cells = _spread_cells(zone_id, half) if cells is None else cells
    reach = half - EDGE_MARGIN_CM
    cluster_count, sigma = CLUSTERS.get(zone_id, (0, 0.0))
    sand = lambda x, y: _on_beach(x0 + x, y0 + y)   # 칸 오프셋 → 월드
    sea = lambda x, y: _coast_depth(x0 + x, y0 + y) < SEA_EDGE_CM
    anchors = _anchors(rng, reach, _scaled(cluster_count), obstacles, sand)
    rock_anchors = _anchors(rng, reach, _scaled(ROCK_CLUSTERS), obstacles, sea)
    flower_anchors = _anchors(rng, reach, _scaled(FLOWER_PATCHES), obstacles, sand)
    points = []
    trunks = []
    counts = {}
    first = True
    for index, (mesh_name, count, height, overrides, is_tree) in enumerate(ZONE_PROPS[zone_id]):
        count = _scaled(count)
        if count == 0 or (mesh_name == "DirtPatch" and not anchors):
            continue
        dirt = mesh_name == "DirtPatch"
        puddle = mesh_name == "Puddle" or dirt
        mesh = unreal.load_asset(CYLINDER if puddle else NATURE_DIR + "/" + mesh_name)
        comp = _add_hism(actor, "{:02d}_{}".format(index, mesh_name))
        if first:
            # 첫 HISM 이 루트 — 액터 자리를 칸 중심으로 다시 맞춘다.
            actor.set_actor_location(unreal.Vector(x0, y0, 0.0), False, False)
            first = False
        comp.set_static_mesh(mesh)
        if "Boulder" in mesh_name:
            comp.set_collision_profile_name("BlockAll")   # 바위는 몸을 막는다(루프#1084, DESIGN 바위 충돌)
        else:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        for slot_index, slot in enumerate(mesh.static_materials):
            mi = overrides.get(str(slot.material_slot_name), overrides.get(""))
            if mi:
                comp.set_material(slot_index, unreal.load_asset(mi))
        if puddle or height < 60.0:
            # 바닥 얼룩·풀·꽃 그림자는 안 보이고 모바일 그림자 비용만 든다(M15-04)
            comp.set_editor_property("cast_shadow", False)
        bounds = mesh.get_bounding_box()
        base = height / float(bounds.max.z - bounds.min.z)

        tree_spacing = {"zone.ranch": RANCH_TREE_SPACING_CM, "zone.snow": SNOW_TREE_SPACING_CM}.get(zone_id, TREE_SPACING_CM)
        spacing = height * 0.6 if puddle else (BIRCH_SPACING_CM if "Birch" in mesh_name else tree_spacing if is_tree else
                                               PROP_SPACING_CM if height >= 60.0 else SMALL_SPACING_CM)
        clustered = mesh_name.startswith(CLUSTERED)
        placed = 0
        if dirt:
            # 흙 둔덕은 군락 중심 그 자리 — 자리 목록(points)에 안 넣어 나무가 그 위에 선다
            for ax, ay in anchors[:count]:
                comp.add_instance(unreal.Transform(unreal.Vector(ax, ay, PUDDLE_Z * 0.5),
                                                   unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)),
                                                   unreal.Vector(height / 100.0, height / 100.0 * rng.uniform(0.65, 0.9), PUDDLE_THICK)))
            counts[mesh_name] = min(count, len(anchors))
            continue
        for _ in range(count):
            spot = _pick(rng, half, spacing, points, cells if (is_tree or clustered) else [],
                         (rock_anchors if "Boulder" in mesh_name else flower_anchors if "WildFlower" in mesh_name else anchors) if clustered else None,
                         ROCK_SIGMA_CM if "Boulder" in mesh_name else FLOWER_SIGMA_CM if "WildFlower" in mesh_name else sigma * (1.0 if is_tree else 1.4),
                         tree_obstacles if is_tree else obstacles, sea if mesh_name.startswith(BEACH_PROPS) else sand)
            if spot is None:
                unreal.log_warning("[ZoneProps] 자리 못 찾음(건너뜀): {} {}".format(zone_id, mesh_name))
                continue
            x, y = spot
            points.append((x, y, spacing))
            if puddle:
                scale = unreal.Vector(height / 100.0, height / 100.0 * rng.uniform(0.6, 1.0), PUDDLE_THICK)
                location = unreal.Vector(x, y, PUDDLE_Z)
            else:
                s = base * rng.uniform(1.0 - SCALE_JITTER, 1.0 + SCALE_JITTER)
                scale = unreal.Vector(s, s, s)
                location = unreal.Vector(x, y, 0.0)
            comp.add_instance(unreal.Transform(location, unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)), scale))
            if is_tree:
                trunks.append((x, y))
            placed += 1
        counts[mesh_name] = counts.get(mesh_name, 0) + placed

    counts["Meadow"] = _place_meadows(actor, rng, zone_id, reach, obstacles, points, x0, y0)
    counts["Leaf"] = _place_fallen_leaves(actor, zone_id, reach, obstacles, trunks, x0, y0)

    if trunks:
        trunk = _add_hism(actor, "{:02d}_TrunkCollision".format(len(ZONE_PROPS[zone_id])))
        trunk.set_static_mesh(unreal.load_asset(CYLINDER))
        trunk.set_editor_property("hidden_in_game", True)
        trunk.set_visibility(False)
        trunk.set_editor_property("cast_shadow", False)
        for x, y in trunks:
            trunk.add_instance(unreal.Transform(
                unreal.Vector(x, y, TRUNK_HEIGHT_CM * 0.5), unreal.Rotator(0.0, 0.0, 0.0),
                unreal.Vector(TRUNK_DIAMETER_CM / 100.0, TRUNK_DIAMETER_CM / 100.0, TRUNK_HEIGHT_CM / 100.0)))
    unreal.log("[ZoneProps] {} 합계 {}개 (나무 {} · 피한 스포너 칸 {}) {}".format(
        label, sum(counts.values()), len(trunks), len(cells), ", ".join("{} {}".format(k, v) for k, v in counts.items())))


def _scaled(count):
    """구역 개수 배율 — 존 뭍이 구역 일부뿐이면 그만큼만."""
    return int(round(count * sector_scale))


def _place_meadows(actor, rng, zone_id, reach, obstacles, points, x0, y0):
    """둥근 꽃밭 — 한 색이 촘촘히, 가장자리만 옆 색이 섞인다. 나무·바위 자리(points)는 비킨다. 심은 포기 수를 돌려준다."""
    if not MEADOWS.get(zone_id):
        return 0   # 늪은 꽃밭 없음 — 빈 HISM 을 달지 않는다
    comps = []
    for mesh_name, color, overrides in MEADOW_COLORS:
        mesh = unreal.load_asset(NATURE_DIR + "/" + mesh_name)
        comp = _add_hism(actor, "Meadow_" + color)
        comp.set_static_mesh(mesh)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("cast_shadow", False)
        for slot_index, slot in enumerate(mesh.static_materials):
            mi = overrides.get(str(slot.material_slot_name), overrides.get(""))
            if mi:
                comp.set_material(slot_index, unreal.load_asset(mi))
        bounds = mesh.get_bounding_box()
        comps.append((comp, FLOWER_CM / float(bounds.max.z - bounds.min.z)))
    planted = 0
    steps = int(MEADOW_RADIUS_CM // MEADOW_STEP_CM)
    for cx, cy in _anchors(rng, reach - MEADOW_RADIUS_CM, _scaled(MEADOWS.get(zone_id, 0)), obstacles,
                           lambda ax, ay: _on_beach(x0 + ax, y0 + ay)):
        main = rng.randrange(len(comps))
        for i in range(-steps, steps + 1):
            for j in range(-steps, steps + 1):
                x = cx + (i + rng.uniform(-0.35, 0.35)) * MEADOW_STEP_CM
                y = cy + (j + rng.uniform(-0.35, 0.35)) * MEADOW_STEP_CM
                r = math.hypot(x - cx, y - cy)
                if r > MEADOW_RADIUS_CM * rng.uniform(0.8, 1.0) or _on_beach(x0 + x, y0 + y):
                    continue
                if any(abs(x - ox) < ex + OBSTACLE_CLEAR_CM and abs(y - oy) < ey + OBSTACLE_CLEAR_CM for ox, oy, ex, ey in obstacles):
                    continue
                if any(math.hypot(x - px, y - py) < ps * 0.5 for px, py, ps in points if ps > SMALL_SPACING_CM):
                    continue
                pick = rng.randrange(len(comps)) if r > MEADOW_RADIUS_CM * 0.66 and rng.random() < MEADOW_MIX else main
                comp, base = comps[pick]
                s = base * rng.uniform(1.0 - SCALE_JITTER, 1.0 + SCALE_JITTER)
                comp.add_instance(unreal.Transform(unreal.Vector(x, y, 0.0), unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)),
                                                   unreal.Vector(s, s, s)))
                planted += 1
    return planted


def _place_fallen_leaves(actor, zone_id, reach, obstacles, trunks, x0, y0):
    """낙엽 — 나무마다 둘레 고리에 몇 장, 칸 전체에 드문드문. 칸 난수와 따로 굴려 다른 배치 자리를 안 흔든다. 깐 수를 돌려준다."""
    if zone_id not in FALLEN_LEAVES:
        return 0
    per_tree, scatter = FALLEN_LEAVES[zone_id]
    rng = random.Random(zone_id + ".leaves")
    mesh = unreal.load_asset(NATURE_DIR + "/SM_Nature_Leaf_01")
    comps = []
    for color, mi in LEAF_COLORS:
        comp = _add_hism(actor, "Leaf_" + color)
        comp.set_static_mesh(mesh)
        comp.set_material(0, unreal.load_asset(mi))
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("cast_shadow", False)
        comps.append(comp)
    spots = []
    for tx, ty in trunks:
        for _ in range(per_tree):
            a, d = rng.uniform(0.0, math.tau), rng.uniform(*LEAF_TREE_RING_CM)
            spots.append((tx + d * math.cos(a), ty + d * math.sin(a)))
    spots += [(rng.uniform(-reach, reach), rng.uniform(-reach, reach)) for _ in range(_scaled(scatter))]
    laid = 0
    for x, y in spots:
        if max(abs(x), abs(y)) > reach or _on_beach(x0 + x, y0 + y):
            continue
        if any(abs(x - ox) < ex + OBSTACLE_CLEAR_CM and abs(y - oy) < ey + OBSTACLE_CLEAR_CM for ox, oy, ex, ey in obstacles):
            continue
        s = rng.uniform(0.8, 1.2)
        rng.choice(comps).add_instance(unreal.Transform(unreal.Vector(x, y, LEAF_Z_CM),
                                                        unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)), unreal.Vector(s, s, 1.0)))
        laid += 1
    return laid


def _gather_prefix(zone_id):
    return "Gather_" + zone_id.split(".")[1].capitalize() + "_"


def _gather_obstacles(x0, y0, half):
    """칸 안에서 피할 사각형 [(중심 x, y, 반폭 x, y)] — 자연물 HISM 인스턴스 하나하나와 그 밖의 액터 경계."""
    boxes = []
    for actor in _actors().get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith("Gather_") or label == "Beach":
            continue   # 모래 원판(`setup_beach.py`)은 밟는 바닥 — 뭍 더미는 `_on_beach` 가 이미 피하고, 섬 제한 더미는 모래 위에 선다
        comps = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
        if comps:
            for comp in comps:
                mesh = comp.get_editor_property("static_mesh")
                if not mesh:
                    continue
                box = mesh.get_bounding_box()
                for i in range(comp.get_instance_count()):
                    t = comp.get_instance_transform(i, True)
                    ex = max(box.max.x - box.min.x, box.max.y - box.min.y) * 0.5 * abs(t.scale3d.x)
                    if ex <= GATHER_PLANE_EXTENT_CM:   # 바다 판 인스턴스는 건너뛴다
                        boxes.append((t.translation.x, t.translation.y, ex, ex))
            continue
        origin, extent = actor.get_actor_bounds(False)
        if min(extent.x, extent.y) > GATHER_PLANE_EXTENT_CM:
            continue   # 바닥·바다 판·하늘 (강 띠는 한 변이 좁아 피한다) — 칸만 한 것은 자리를 막는 물건이 아니다
        boxes.append((origin.x, origin.y, extent.x, extent.y))
        if max(extent.x, extent.y) > 500.0:
            unreal.log("[GatherPiles] 큰 장애물 {} ({:.0f}, {:.0f}) 반폭 {:.0f}×{:.0f}".format(
                label, origin.x, origin.y, extent.x, extent.y))
    reach = half + GATHER_CLEAR_CM + GATHER_PLANE_EXTENT_CM
    return [b for b in boxes if abs(b[0] - x0) < reach and abs(b[1] - y0) < reach]


def _place_gather_piles(zone_id, x0, y0, rx0, ry0, rows):
    """rows = [(행 이름 꼬리, 곳 수[, 거점 id])] — 상자 (x0, y0, 반폭 rx0, ry0) 안 존 뭍에 흩는다.
    거점 id 가 붙은 행은 그 거점 둘레 작은 섬 뭍에만."""
    prefix = _gather_prefix(zone_id)
    for actor in _actors().get_all_level_actors():
        if actor.get_actor_label().startswith(prefix):
            _actors().destroy_actor(actor)
    obstacles = _gather_obstacles(x0, y0, max(rx0, ry0))
    table = unreal.EditorAssetLibrary.load_asset(GATHER_TABLE)
    meshes = dict(zip(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "GatherableID"),
                      unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "MeshRef")))
    row_ids = dict(zip([str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)],
                    unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "GatherableID")))
    heights = dict(zip(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "GatherableID"),
                       unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "BodyHeightCm")))
    rng = random.Random("{}:{}".format(GATHER_SEED, zone_id))
    points = []
    for row_tail, count, *only_island in rows:
        row_name = "Gatherable_" + row_tail
        height = float(heights.get(row_ids.get(row_name, ""), "0") or 0)
        mesh_path = meshes.get(row_ids.get(row_name, ""), "")
        found = re.search(r"(/Game/[^.'\"]+)", mesh_path)
        mesh = unreal.load_asset(found.group(1)) if found else None
        if not mesh or height <= 0:
            unreal.log_error("[GatherPiles] 행·메시·몸 높이 없음: {} ({}, {})".format(row_name, mesh_path, height))
            raise SystemExit(1)
        box = mesh.get_bounding_box()
        scale = height / float(box.max.z - box.min.z)
        placed = 0
        # 섬 제한 행은 그 거점(작은 섬) 둘레 상자에서 고른다 — 원본 Pix's Island 메제레움
        cx, cy, rx, ry = (x0, y0, rx0, ry0) if not only_island else \
            next((o["x"], o["y"], ISLET_REACH_CM, ISLET_REACH_CM) for o in islands.OUTPOSTS if o["id"] == only_island[0])
        for n in range(count):
            def free(x, y):
                if any(math.hypot(x - px, y - py) < GATHER_SPACING_CM for px, py in points):
                    return False
                if only_island:
                    # 작은 섬은 좁아 통째로 모래띠(650) 안일 수 있다 — 모래 위라도 물가에서 120cm 안쪽이면 선다
                    if islands.signed_distance(x, y) > -120.0:
                        return False
                elif _on_beach(x, y):
                    return False
                return not any(abs(x - ox) < ex + GATHER_CLEAR_CM and abs(y - oy) < ey + GATHER_CLEAR_CM
                               for ox, oy, ex, ey in obstacles)
            spot = None
            for _ in range(PLACE_TRIES * 4):
                x, y = cx + rng.uniform(-rx, rx), cy + rng.uniform(-ry, ry)
                if free(x, y):
                    spot = (x, y)
                    break
            if spot is None:
                # 섬(가을섬)은 칸보다 작고 둔덕·나무가 빽빽해 빈자리가 좁다 — 무작위가 못 맞히면 100cm 격자를 훑어 빈자리 중에서 고른다
                step = max(100.0, max(rx, ry) / 150.0)   # 큰 존 상자도 격자 9만 칸 안쪽
                grid = [(cx - rx + i * step, cy - ry + j * step)
                        for i in range(int(rx * 2 / step) + 1) for j in range(int(ry * 2 / step) + 1)]
                spare = [p for p in grid if free(*p)]
                spot = rng.choice(spare) if spare else None
            if spot is None:
                unreal.log_error("[GatherPiles] 자리 못 찾음: {} {}번째 — 장애물 {}개, 큰 것 {}, 고른 곳 둘레 {}".format(
                    row_name, n + 1, len(obstacles), sorted(obstacles, key=lambda o: -max(o[2], o[3]))[:5],
                    [o for o in obstacles if abs(o[0] - cx) < rx + o[2] and abs(o[1] - cy) < ry + o[3]][:8]))
                raise SystemExit(1)
            points.append(spot)
            actor = _actors().spawn_actor_from_class(unreal.GatherableActor, unreal.Vector(spot[0], spot[1], 0.0),
                                                     unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
            actor.set_actor_label("{}{}_{}".format(prefix, row_tail, n + 1))
            actor.set_editor_property("gatherable_row_name", row_name)
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            placed += 1
        unreal.log("[GatherPiles] {} {} {}곳 (배율 {:.2f}, 피한 것 {}개)".format(
            zone_id, row_name, placed, scale, len(obstacles)))


def _report_gather_piles():
    """저장 뒤 다시 훑어 라벨·행으로 센다(스폰 직후 변수를 믿지 않는다 — 루프#53)."""
    counts = {}
    for actor in _actors().get_all_level_actors():
        if actor.get_actor_label().startswith("Gather_"):
            key = "{} {}".format(actor.get_actor_label().split("_")[1], actor.get_editor_property("gatherable_row_name"))
            counts[key] = counts.get(key, 0) + 1
    unreal.log("[GatherPiles] 확인: " + ", ".join("{} {}곳".format(k, v) for k, v in sorted(counts.items())))


def _report():
    """저장한 뒤 레벨을 다시 훑어 실제로 선 인스턴스 수를 찍는다(스폰 직후 변수를 믿지 않는다 — 루프#53)."""
    counts = {}
    for actor in _actors().get_all_level_actors():
        label = actor.get_actor_label()
        if not label.startswith("ZoneProps_"):
            continue
        zone = label.split("_")[1]
        comps = actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
        visible = sum(c.get_instance_count() for c in comps if not c.get_editor_property("hidden_in_game"))
        n, h, v = counts.get(zone, (0, 0, 0))
        counts[zone] = (n + 1, h + len(comps), v + visible)
    for zone, (n, h, v) in sorted(counts.items()):
        unreal.log("[ZoneProps] 확인: {} 구역 {}개 · HISM {}개 · 보이는 인스턴스 {}개".format(zone, n, h, v))
    if not counts:
        unreal.log_error("[ZoneProps] 확인: 자연물 액터 없음")


def _land_samples(x0, y0, half, step=500.0):
    """구역 안 표본 점 중 모래 띠 안쪽 뭍 점의 존 이름 목록."""
    n = int(2 * half / step)
    names = []
    for a in range(n):
        for b in range(n):
            px, py = x0 - half + (a + 0.5) * step, y0 - half + (b + 0.5) * step
            if islands.signed_distance(px, py) < -BEACH_BAND_CM:
                names.append(islands.zone_at(px, py))
    return names, n * n


def _biome_box(name):
    """존 칸이 덮는 월드 상자 (가운데 x, y, 반폭 x, y)."""
    xs, ys = [], []
    for i in range(islands.ZI):
        for j in range(islands.ZJ):
            if islands.ZONE[i * islands.ZJ + j] & 0x0F == islands.ZONE_CODES[name]:
                xs.append(i)
                ys.append(j)
    x0 = islands.X0 + min(xs) * islands.CELL_CM
    x1 = islands.X0 + (max(xs) + 1) * islands.CELL_CM
    y0 = islands.Y0 + min(ys) * islands.CELL_CM
    y1 = islands.Y0 + (max(ys) + 1) * islands.CELL_CM
    return (x0 + x1) * 0.5, (y0 + y1) * 0.5, (x1 - x0) * 0.5, (y1 - y0) * 0.5


def _spread_offsets(row_name):
    half, spacing = (float(v) for v in _table_rows(SPREAD_TABLE, "ZoneID", ["AreaHalfExtentCm", "MinSpacingCm"])
                     .get(next(z for z, (_, r, _) in SPREADERS.items() if r == row_name)))
    per_axis = int(math.floor(2.0 * half / spacing)) + 1
    first = -spacing * (per_axis - 1) * 0.5
    return [(first + i * spacing, first + j * spacing) for i in range(per_axis) for j in range(per_axis)]


def _spread_spots(name, offsets, count):
    """스포너 자리 — 격자 칸 중 제 존 뭍 몫이 큰 자리부터, 서로 SPREAD_SEPARATION_CM 떨어지게."""
    cx, cy, rx, ry = _biome_box(name)
    scored = []
    step = 2000.0
    for a in range(int(2 * rx / step) + 1):
        for b in range(int(2 * ry / step) + 1):
            x, y = cx - rx + a * step, cy - ry + b * step
            if islands.zone_at(x, y) != name or not islands.on_land(x, y, 1000.0):
                continue
            fill = sum(1 for ox, oy in offsets if islands.zone_at(x + ox, y + oy) == name and
                       islands.on_land(x + ox, y + oy, 300.0)) / float(len(offsets))
            if fill >= SPREAD_MIN_FILL:
                scored.append((-fill, x, y))
    spots = []
    for _, x, y in sorted(scored):
        if all(math.hypot(x - sx, y - sy) >= SPREAD_SEPARATION_CM for sx, sy in spots):
            spots.append((x, y))
            if len(spots) == count:
                break
    return spots


_ensure_materials()
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.load_level(LEVEL_PATH)

# 옛 배치(자연물·채집 더미·스포너·낱개 자연물 `HuntNature_*`)를 다 걷고 다시 세운다
for actor in _actors().get_all_level_actors():
    if actor.get_actor_label().startswith(("ZoneProps_", "Gather_", "AnimalSpread_", "HuntNature_")):
        _actors().destroy_actor(actor)

spread_cells = []   # 월드 좌표 — 나무가 비킨다
for zone_id, (prefix, row_name, count) in SPREADERS.items():
    offsets = _spread_offsets(row_name)
    spots = _spread_spots(zone_id.split(".")[1], offsets, count)
    if len(spots) < count:
        unreal.log_error("[Zones] 스포너 자리 모자람: {} {}/{}".format(zone_id, len(spots), count))
        raise SystemExit(1)
    for k, (x, y) in enumerate(spots):
        spread = _actors().spawn_actor_from_class(unreal.AnimalSpreadActor, unreal.Vector(x, y, 0.0))
        spread.set_actor_label(prefix if k == 0 else "{}_{}".format(prefix, k + 1))
        spread.set_editor_property("spread_row_name", row_name)
        spread_cells += [(x + ox, y + oy) for ox, oy in offsets]
        unreal.log("[Zones] 스포너 {} row={} ({:.0f}, {:.0f})".format(spread.get_actor_label(), row_name, x, y))

# 구역 표본 — 존마다 모래 띠 안쪽 뭍 몫
x_min, x_max, y_min, y_max = islands.BOUNDS
half = SECTOR_CM * 0.5
sectors = []   # (존 이름, i, j, 가운데 x, y, 몫)
land_m2 = {}
for i in range(int(math.ceil((x_max - x_min) / SECTOR_CM))):
    for j in range(int(math.ceil((y_max - y_min) / SECTOR_CM))):
        x0, y0 = x_min + (i + 0.5) * SECTOR_CM, y_min + (j + 0.5) * SECTOR_CM
        names, total = _land_samples(x0, y0, half)
        for name in set(n for n in names if n):
            share = names.count(name) / float(total)
            land_m2[name] = land_m2.get(name, 0.0) + share * (SECTOR_CM / 100.0) ** 2
            if share >= SECTOR_MIN_LAND:
                sectors.append((name, i, j, x0, y0, share))

# 채집 더미가 먼저 자리를 잡고 자연물이 그 둘레를 피한다 — 군락이 빽빽해 거꾸로 하면 더미 자리가 안 남는다(M15-04).
for zone_id, rows in GATHER_PILES.items():
    biome = zone_id.split(".")[1]
    fixed = [r for r in rows if len(r) > 2]
    spread_rows = [r for r in rows if len(r) == 2]
    total = int(round(land_m2.get(biome, 0.0) / PILE_LAND_M2))
    weight = float(sum(c for _, c in spread_rows))
    scaled = [(tail, max(1, int(round(total * c / weight)))) for tail, c in spread_rows]
    _place_gather_piles(zone_id, *_biome_box(biome), scaled + fixed)

EDGE_MARGIN_CM = 0.0   # 구역끼리 맞붙는다 — 가장자리를 띄우면 50m 마다 빈 줄이 난다
for name, i, j, x0, y0, share in sectors:
    zone_id = "zone." + name
    if zone_id not in ZONE_PROPS:
        continue
    biome, sector_scale = name, share
    cells = [(cx - x0, cy - y0) for cx, cy in spread_cells if abs(cx - x0) < half + CELL_CLEAR_CM and abs(cy - y0) < half + CELL_CLEAR_CM]
    _place_zone_props(zone_id, x0, y0, half, "ZoneProps_{}_{}_{}".format(name.capitalize(), i, j), cells)
biome, sector_scale = None, 1.0

level_subsystem.save_current_level()
unreal.log("[Zones] 저장: {} 구역 {}개 · 존 뭍 {}".format(
    LEVEL_PATH, len(sectors), ", ".join("{} {:.0f}m²".format(k, v) for k, v in sorted(land_m2.items()))))
_report()
_report_gather_piles()

ZONE_PROPS["zone.ranch"] = RANCH_PROPS
island = (0.0, 0.0, RANCH_HALF_CM)
EDGE_MARGIN_CM = 250.0
_obstacle_cache = None
level_subsystem.load_level(RANCH_LEVEL)
_place_zone_props("zone.ranch", 0.0, 0.0, RANCH_HALF_CM)
level_subsystem.save_current_level()
unreal.log("[Zones] 저장: {} 둘레 숲".format(RANCH_LEVEL))
