"""
ROADMAP M1-01/M1-05/M1-06/M2-02/M7-01/M7-07/M6-01/M6-04a/M6-05a/M8-01/M10-01/M11-01/M11-05/M12-04 — DT_AnimalTable / DT_ItemTable /
DT_GatherableTable / DT_RanchProductTable / DT_CropProductTable / DT_FishingTable / DT_GachaRates /
DT_GachaConfig / DT_GachaPool / DT_ZoneTable / DT_DangerZoneTable / DT_AnimalSpreadTable / DT_AnalyticsEventTable /
DT_PlayerVitalsTable / DT_PlayerCombatTable 을 만들고 행을 채운다.

실행 (인자 둘 다 절대경로여야 한다 — 상대경로면 Binaries/Mac 기준으로 풀려 실패, STATUS #0005):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_data_tables.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

.uasset 은 바이너리라 텍스트로 만들지 않는다 (engines/unreal.md "언리얼 파일 다루기").
재실행 안전: 테이블이 이미 있으면 다시 만들지 않고 행만 다시 채운다.
숫자 근거는 DESIGN 3.1·3.2·10.2 "확정 수치" 표뿐이다 — 이 스크립트가 만든 숫자는 없다 (DataDriven.md 3).
"""
import json
import os
import re

import unreal

ANIMAL_TABLE_FOLDER = "/Game/Game/Data/Animals"
ANIMAL_TABLE_NAME = "DT_AnimalTable"
ANIMAL_ROW_STRUCT = "/Script/Snarehaven.AnimalRow"

ITEM_TABLE_FOLDER = "/Game/Game/Data/Items"
ITEM_TABLE_NAME = "DT_ItemTable"
ITEM_ROW_STRUCT = "/Script/Snarehaven.ItemRow"

# 아이템 아이콘이 사는 곳 (M5-08 · `tools/unreal/import_item_icons.py` 가 들이는 폴더).
ICON_FOLDER = "/Game/Game/UI/Icons/Items"

GATHERABLE_TABLE_FOLDER = "/Game/Game/Data/Gatherables"
GATHERABLE_TABLE_NAME = "DT_GatherableTable"
GATHERABLE_ROW_STRUCT = "/Script/Snarehaven.GatherableRow"

RANCH_TABLE_FOLDER = "/Game/Game/Data/Ranch"
RANCH_TABLE_NAME = "DT_RanchProductTable"
RANCH_ROW_STRUCT = "/Script/Snarehaven.RanchProductRow"

# 작물 표는 목장 표와 **같은 폴더**다 — 둘 다 거점 슬롯 하나(`URanchComponent`)가 읽는 표이고
# 슬롯 타입만 갈린다 (결정 005-2 · DESIGN 8-2). 새 시스템이 아니라 같은 시스템의 두 번째 표다.
CROP_TABLE_FOLDER = "/Game/Game/Data/Ranch"
CROP_TABLE_NAME = "DT_CropProductTable"
CROP_ROW_STRUCT = "/Script/Snarehaven.CropProductRow"

# 낚시 표는 **제 폴더**다 — 목장·작물은 같은 거점(목장)의 두 슬롯 타입이라 폴더를 나눴지만
# (결정 005-2), 낚시터는 다른 거점이다 (DESIGN 8.4). 짜임만 빌리고 자리는 따로 둔다.
FISHING_TABLE_FOLDER = "/Game/Game/Data/Fishing"
FISHING_TABLE_NAME = "DT_FishingTable"
FISHING_ROW_STRUCT = "/Script/Snarehaven.FishingRow"

ZONE_TABLE_FOLDER = "/Game/Game/Data/World"
ZONE_TABLE_NAME = "DT_ZoneTable"
ZONE_ROW_STRUCT = "/Script/Snarehaven.ZoneRow"

DANGER_TABLE_FOLDER = "/Game/Game/Data/World"
DANGER_TABLE_NAME = "DT_DangerZoneTable"
DANGER_ROW_STRUCT = "/Script/Snarehaven.DangerZoneRow"

# 분산 스폰 표도 World 폴더다 — 위험구역과 **같은 존의 반대쪽 규칙**이라(모으기 ↔ 흩기)
# 나란히 둔다 (DESIGN 3.1 "동물 맵 전역 분산 스폰" 표).
SPREAD_TABLE_FOLDER = "/Game/Game/Data/World"
SPREAD_TABLE_NAME = "DT_AnimalSpreadTable"
SPREAD_ROW_STRUCT = "/Script/Snarehaven.AnimalSpreadRow"

ANALYTICS_TABLE_FOLDER = "/Game/Game/Data/Analytics"

VITALS_TABLE_FOLDER = "/Game/Game/Data/Balance"
VITALS_TABLE_NAME = "DT_PlayerVitalsTable"
VITALS_ROW_STRUCT = "/Script/Snarehaven.PlayerVitalsRow"

# 공격 네 숫자도 같은 Balance 폴더다 — 원본이 DESIGN 3.1 의 다른 표라 체력·스태미나와 표를 갈랐다 (M11-05).
COMBAT_TABLE_FOLDER = "/Game/Game/Data/Balance"
COMBAT_TABLE_NAME = "DT_PlayerCombatTable"
COMBAT_ROW_STRUCT = "/Script/Snarehaven.PlayerCombatRow"
ANALYTICS_TABLE_NAME = "DT_AnalyticsEventTable"
ANALYTICS_ROW_STRUCT = "/Script/Snarehaven.AnalyticsEventRow"

GACHA_TABLE_FOLDER = "/Game/Game/Data/Gacha"
GACHA_RATE_TABLE_NAME = "DT_GachaRates"
GACHA_RATE_ROW_STRUCT = "/Script/Snarehaven.GachaRateRow"
GACHA_CONFIG_TABLE_NAME = "DT_GachaConfig"
GACHA_CONFIG_ROW_STRUCT = "/Script/Snarehaven.GachaCostRow"
GACHA_POOL_TABLE_NAME = "DT_GachaPool"
GACHA_POOL_ROW_STRUCT = "/Script/Snarehaven.GachaPoolRow"

# 자리표시 세 종(Quaternius 돼지·소·양)만 **게임에서 굽는다** — 바르코 12종처럼 DESIGN 8.7 "크기"
# 표가 없고 원본 단위도 제각각이라, 안 구우면 화면을 덮는다. 값 90cm 은 새 숫자가 아니라
# DESIGN 3.1 「동물 몸 세로(캡슐 높이)」 그대로다 — 자리표시가 캡슐에 맞춰 서던 옛 동작을 유지한다.
# **12종은 0 이다**(= 안 굽는다). 굽는 것을 캡슐 높이로 하던 코드가 전 종을 90cm 로 만들어 버려
# 감독이 2026-09-23 18:55 결함으로 올렸다 (`FAnimalRow::MeshFitHeightCm` 주석).
PLACEHOLDER_FIT_HEIGHT_CM = 90.0

# **서식 존** (M5-07e · `FAnimalRow::HabitatZoneID`). 분산 스포너는 자기 존과 같은 행만 세운다.
# M12-00 이 `L_Hunt` 안에 평원·설원·숲 존을 세웠으므로(아래 `ZONE_ROWS`) 바르코 12종은 `ANIMAL_SPECIES` 의
# `Zone` 칸대로 나뉜다(DESIGN 8.7 "서식 존 배정" 표). 자리표시 돼지·소·양은 시작 존인 평원에 둔다 —
# `-SnareStageAnimals` 검사에서 예전처럼 시작점 둘레에 서게.
PLACEHOLDER_HABITAT_ZONE = "zone.plains"

# **돼지·소·양 세 행은 자리표시다**(`bPlaceholderSpecies` = True, 사용자 지시 2026-09-23 16:55 ②) —
# 행은 남기되 **기본 플레이에는 한 마리도 서지 않는다**(분산 스포너가 안 고르고, 맵에 놓인 것은
# 게임모드가 치운다). 검사 스위치 `-SnareStageAnimals` 를 주면 예전처럼 선다. 행을 아주 빼는 것은
# 가리키는 검사·도감이 0 이 되는 날, M5-07 마지막 조각에서 (DESIGN 8.7 머리말).
# 돼지 1행 (ROADMAP M1-01 합격 기준). MaxHP 100 · 포획 임계 25% · 드롭 1~3개 는 전부 DESIGN 3.1 표.
# MeshRef·IdleAnimRef 는 M1-07 에서 임포트한 Quaternius 돼지(CC0), DeathAnimRef 는 M1-09 에서
# 같은 팩 `Pig.blend` 에서 다시 뽑은 죽음 클립. Walk·Run 은 M41-02 에서 같은 폴더 FBX 로 들였다(걷기·도주 칸).
# `MoveSpeed` 45 는 DESIGN 3.1 「동물 도주 속도」(감독 결정 2026-09-23, 루프#153 답 — 250 -> 140 -> 45) —
# 전 종 공통이라 세 행이 같다(M1-11b). 140 의 근거였던 걷기 약 156 은 몸 메시 교체 전 값이고
# 지금 빌드 실측은 걷기 56.7cm/s 라(STATUS #0148), 그 **0.8배**여야 스태미나 0(걷기 강제)에서도 따라붙는다.
# 전 종 공통 두 값 (M22) — DESIGN 3.1 「최상급 드롭 확률」·「시체 수명」 줄의 사본.
PRISTINE_DROP_CHANCE = 0.05   # 최상급(pristine_) 1개가 나올 확률 — 나머지 95% 는 일반 드롭에서 균등
CORPSE_LIFE_SECONDS = 8.0     # 죽거나 포획된 몸이 사라지기까지(초), 호스트가 SetLifeSpan
VARIANT_CHANCE = 0.03         # M21-07 색 변종 — 동물이 설 때마다 3%, 종 상관없이 같은 값 (DESIGN 10.6 「변종 확률」)

ANIMAL_ROWS = [
    {
        "Name": "Animal_Pig",
        "AnimalID": "animal.pig",
        "DisplayName": "돼지",
        "MaxHP": 80.0,   # DESIGN 3.1 「동물 체력」 순한 종 80 (M22)
        "CaptureThresholdPct": 25.0,
        "MoveSpeed": 45.0,
        "DeathDropItems": ["item.pig_meat", "item.pig_hide"],
        "DeathDropCountMin": 1,
        "DeathDropCountMax": 3,
        "PristineDropChance": PRISTINE_DROP_CHANCE,
        "CorpseLifeSeconds": CORPSE_LIFE_SECONDS,
        "PetItemID": "item.pet_pig",
        "bPlaceholderSpecies": True,
        "HabitatZoneID": PLACEHOLDER_HABITAT_ZONE,
        "MeshFitHeightCm": PLACEHOLDER_FIT_HEIGHT_CM,
        "MeshRef": "/Game/Game/Characters/Animals/Pig/SK_Animal_Pig.SK_Animal_Pig",
        "IdleAnimRef": "/Game/Game/Characters/Animals/Pig/A_Animal_Pig_Idle.A_Animal_Pig_Idle",
        "DeathAnimRef": "/Game/Game/Characters/Animals/Pig/A_Animal_Pig_Death.A_Animal_Pig_Death",
        "WalkAnimRef": "/Game/Game/Characters/Animals/Pig/A_Animal_Pig_Walk.A_Animal_Pig_Walk",
        "FleeAnimRef": "/Game/Game/Characters/Animals/Pig/A_Animal_Pig_Run.A_Animal_Pig_Run",
    },
    # 소·양 2행 (ROADMAP M5-04b). **숫자는 돼지와 같다** — DESIGN 3.1 확정 수치표가 체력 100 을
    # "전 종 공통 (MVP 기준, 종별 차등은 콘텐츠 확장 단계)" 로, 포획 임계 25%·드롭 1~3 개도 종 구분 없이
    # 적어 두었다. 이 스크립트가 만든 숫자는 여기서도 0개다 (DataDriven.md 3).
    # 메시·클립은 M5-04a 에서 임포트한 Quaternius Farm Animals(CC0).
    {
        "Name": "Animal_Cow",
        "AnimalID": "animal.cow",
        "DisplayName": "소",
        "MaxHP": 80.0,   # DESIGN 3.1 「동물 체력」 순한 종 80 (M22)
        "CaptureThresholdPct": 25.0,
        "MoveSpeed": 45.0,
        "DeathDropItems": ["item.cow_meat", "item.cow_hide"],
        "DeathDropCountMin": 1,
        "DeathDropCountMax": 3,
        "PristineDropChance": PRISTINE_DROP_CHANCE,
        "CorpseLifeSeconds": CORPSE_LIFE_SECONDS,
        "PetItemID": "item.pet_cow",
        "bPlaceholderSpecies": True,
        "HabitatZoneID": PLACEHOLDER_HABITAT_ZONE,
        "MeshFitHeightCm": PLACEHOLDER_FIT_HEIGHT_CM,
        "MeshRef": "/Game/Game/Characters/Animals/Cow/SK_Animal_Cow.SK_Animal_Cow",
        "IdleAnimRef": "/Game/Game/Characters/Animals/Cow/A_Animal_Cow_Idle.A_Animal_Cow_Idle",
        "DeathAnimRef": "/Game/Game/Characters/Animals/Cow/A_Animal_Cow_Death.A_Animal_Cow_Death",
    },
    {
        "Name": "Animal_Sheep",
        "AnimalID": "animal.sheep",
        "DisplayName": "양",
        "MaxHP": 80.0,   # DESIGN 3.1 「동물 체력」 순한 종 80 (M22)
        "CaptureThresholdPct": 25.0,
        "MoveSpeed": 45.0,
        "DeathDropItems": ["item.sheep_meat", "item.sheep_hide"],
        "DeathDropCountMin": 1,
        "DeathDropCountMax": 3,
        "PristineDropChance": PRISTINE_DROP_CHANCE,
        "CorpseLifeSeconds": CORPSE_LIFE_SECONDS,
        "PetItemID": "item.pet_sheep",
        "bPlaceholderSpecies": True,
        "HabitatZoneID": PLACEHOLDER_HABITAT_ZONE,
        "MeshFitHeightCm": PLACEHOLDER_FIT_HEIGHT_CM,
        "MeshRef": "/Game/Game/Characters/Animals/Sheep/SK_Animal_Sheep.SK_Animal_Sheep",
        "IdleAnimRef": "/Game/Game/Characters/Animals/Sheep/A_Animal_Sheep_Idle.A_Animal_Sheep_Idle",
        # 원본 `Sheep.fbx` 에는 Idle·Jump 뿐이었고(M5-04a), 감독이 `Blends/Sheep.blend`(CC0)에서
        # Death 액션 26프레임을 따로 뽑아 줘 루프#131 이 임포트했다 — 막힘#74 는 여기서 닫힌다.
        "DeathAnimRef": "/Game/Game/Characters/Animals/Sheep/A_Animal_Sheep_Death.A_Animal_Sheep_Death",
        # M41-03 — 같은 blend 의 Walk(64프레임)·Run(20프레임) 을 `tools/blender/export_sheep_anims.py` 가 뽑았다.
        "WalkAnimRef": "/Game/Game/Characters/Animals/Sheep/A_Animal_Sheep_Walk.A_Animal_Sheep_Walk",
        "FleeAnimRef": "/Game/Game/Characters/Animals/Sheep/A_Animal_Sheep_Run.A_Animal_Sheep_Run",
    },
]

# 바르코 12종 (ROADMAP M5-07c 확장판 — INBOX 지시 2026-09-23 16:55 "여우 하나가 아니라 12종 전부").
# **이 표가 종 하나를 설명하는 유일한 자리다** — 아래 `extend_with_animal_species()` 가 여기서
# 동물 행 12개 · 드롭/펫/목장 재료 아이템 행 · 목장 생산 행 12개를 한꺼번에 만든다. 종이 늘어도
# 손으로 세 군데를 고치지 않는다 (DataDriven.md 1 — 루프#75 가 소·양 목장 행을 빠뜨린 사고의 예방).
#
# 칸의 출처 (이 스크립트가 고른 숫자·이름은 0개):
#   - 종 이름·드롭 종류: DESIGN 8.7 "종 | 레퍼런스 자료의 드롭" 표. 고유명은 Reference.md 5절대로 새로 지었다
#     좋은 품질 접두어는 원본 `Pristine` 하나 = `pristine_` (감독 2026-09-24 12:40 — 아이콘 이름 = 원본에 맞춤,
#     한국어 이름도 「최상급」 하나로 새로 지었다 — 법무 점검 2026-10-02, 원본 번역어를 쓰지 않는다). 레퍼런스 자료의 "탈것" 칸은
#     우리 게임에서 **펫**이다(포획 → 목장, DESIGN 3.2) — 그래서 `Pet` 칸이 그 자리에 선다.
#   - 새 4종의 깃털은 **종마다 따로**다(원본 `Hazel Hen`·`Kiiruna`·`Peacock`·`Capercaillie` 깃털 + `Pristine`).
#     새고기만 네 종이 같은 행을 가리킨다. 드롭 칸이 셋인 것은 그대로라 확률(셋 중 하나)도 옛 공용 깃털과 같다.
#   - 메시·클립: M5-07b2 가 들인 `Content/Game/Characters/Animals/<폴더>/` 에셋 이름 규칙 그대로.
#   - 체력 100 · 포획 임계 25% · 도주 속도 45 · 드롭 1~3개: **전 종 공통**(DESIGN 3.1)이라 아래 코드가
#     돼지 행에서 그대로 읽어 온다 — 같은 숫자를 열세 번 적으면 한쪽만 고치는 사고가 난다.
#
# `Produce` = 목장에 놓았을 때 나오는 **목장 전용 재료**(DESIGN 3.2 원칙 — 사냥 드롭과 겹치면 방치형이
# 사냥과 구분되지 않는다). 감독 결정 2026-09-23(루프#163 이 DESIGN 3.2 표에 근거와 함께 적었다):
# 살아 있는 짐승에게서 얻는 것으로 골랐다 — 털(여우·스라소니·늑대·울버린) · 젖(염소 → 기존 `item.milk`
# 재사용) · 낙각(순록·엘크 공용, 뿔은 해마다 저절로 떨어진다) · 꿀(곰이 벌집을 뒤진다) · 알(새 4종 공용).
ANIMAL_SPECIES = [
    {
        "Folder": "Fox", "ID": "fox", "Name": "여우", "Zone": "zone.plains",
        "Drops": [("fox_meat", "여우고기"), ("fox_tail", "여우 꼬리"),
                  ("pristine_fox_tail", "최상급 여우 꼬리")],
        "MaxHP": 100.0, "Damage": 6.0, "Behavior": "CorneredCounter",
        "Produce": ("fox_fur", "여우털"),
    },
    {
        "Folder": "Goat", "ID": "goat", "Name": "염소", "Zone": "zone.plains",
        "Drops": [("goat_meat", "염소고기"), ("goat_horn", "염소 뿔"),
                  ("pristine_goat_horn", "최상급 염소 뿔")],
        "MaxHP": 100.0, "Damage": 8.0, "Behavior": "RetaliateWhenHit",
        "Produce": ("milk", "우유"),
    },
    {
        "Folder": "Reindeer", "ID": "reindeer", "Name": "순록", "Zone": "zone.snow",
        "Drops": [("reindeer_meat", "순록고기"), ("reindeer_antler", "순록 뿔"),
                  ("pristine_reindeer_antler", "최상급 순록 뿔")],
        "MaxHP": 100.0, "Damage": 8.0, "Behavior": "CorneredCounter",
        "Produce": ("shed_antler", "낙각"),
    },
    {
        "Folder": "Elk", "ID": "elk", "Name": "엘크", "Zone": "zone.plains",   # 원본 무스는 봄(평원) 바이옴 대부분에 난다 (DESIGN 8.7)
        "Drops": [("elk_meat", "엘크고기"), ("elk_antler", "엘크 뿔"),
                  ("pristine_elk_antler", "최상급 엘크 뿔")],
        "MaxHP": 140.0, "Damage": 12.0, "Behavior": "RetaliateWhenHit",
        "Produce": ("shed_antler", "낙각"),
    },
    {
        "Folder": "Lynx", "ID": "lynx", "Name": "스라소니", "Zone": "zone.forest",
        "Drops": [("lynx_meat", "스라소니고기"), ("lynx_tooth", "스라소니 이빨"),
                  ("pristine_lynx_tooth", "최상급 스라소니 이빨")],
        "MaxHP": 140.0, "Damage": 10.0, "Behavior": "RetaliateWhenHit",
        "Produce": ("lynx_fur", "스라소니 털"),
    },
    {
        # 8.7 표의 늑대 줄에는 "깨끗한" 트로피가 없다 — 드롭이 둘뿐인 것은 빠뜨린 것이 아니다.
        "Folder": "Wolf", "ID": "wolf", "Name": "늑대", "Zone": "zone.snow",
        "Drops": [("wolf_meat", "늑대고기"), ("wolf_tooth", "늑대 이빨")],
        "MaxHP": 160.0, "Damage": 12.0, "Behavior": "Aggressive",
        "Produce": ("wolf_fur", "늑대 털"),
    },
    {
        "Folder": "Wolverine", "ID": "wolverine", "Name": "울버린", "Zone": "zone.snow",
        "Drops": [("wolverine_meat", "울버린고기"), ("wolverine_skull", "울버린 머리뼈"),
                  ("pristine_wolverine_skull", "최상급 울버린 머리뼈")],
        "MaxHP": 160.0, "Damage": 12.0, "Behavior": "Aggressive",
        "Produce": ("wolverine_fur", "울버린 털"),
    },
    {
        "Folder": "Bear", "ID": "bear", "Name": "곰", "Zone": "zone.forest",
        "Drops": [("bear_meat", "곰고기"), ("bear_paw", "곰 발바닥"),
                  ("pristine_bear_paw", "최상급 곰 발바닥")],
        "MaxHP": 200.0, "Damage": 20.0, "Behavior": "Aggressive",
        "Produce": ("honey", "꿀"),
    },
    {
        "Folder": "Pheasant", "ID": "pheasant", "Name": "꿩", "Zone": "zone.plains",
        "Drops": [("bird_meat", "새고기"), ("pheasant_feather", "꿩 깃털"),
                  ("pristine_pheasant_feather", "최상급 꿩 깃털")],
        "MaxHP": 80.0, "Damage": 0.0, "Behavior": "Flee",
        "Produce": ("egg", "알"),
    },
    {
        "Folder": "Peacock", "ID": "peacock", "Name": "공작", "Zone": "zone.plains",
        "Drops": [("bird_meat", "새고기"), ("peacock_feather", "공작새 깃털"),
                  ("pristine_peacock_feather", "최상급 공작새 깃털")],
        "MaxHP": 80.0, "Damage": 0.0, "Behavior": "Flee",
        "Produce": ("egg", "알"),
    },
    {
        "Folder": "Ptarmigan", "ID": "ptarmigan", "Name": "뇌조", "Zone": "zone.snow",
        "Drops": [("bird_meat", "새고기"), ("ptarmigan_feather", "뇌조 깃털"),
                  ("pristine_ptarmigan_feather", "최상급 뇌조 깃털")],
        "MaxHP": 80.0, "Damage": 0.0, "Behavior": "Flee",
        "Produce": ("egg", "알"),
    },
    {
        "Folder": "Capercaillie", "ID": "capercaillie", "Name": "큰뇌조", "Zone": "zone.forest",
        "Drops": [("bird_meat", "새고기"), ("capercaillie_feather", "큰뇌조 깃털"),
                  ("pristine_capercaillie_feather", "최상급 큰뇌조 깃털")],
        "MaxHP": 80.0, "Damage": 0.0, "Behavior": "Flee",
        "Produce": ("egg", "알"),
    },
    {
        # 이야기 종 (docs/specs/story-fogbuck.md 조각 2b) — 순록 몸체를 그대로 쓰고(`BodyFolder`), 체력은 순록의 2배(명세 3절 🟡).
        # 무작위 스포너·도감·변종에는 안 든다(`bStorySpecies`). 고기·뿔·낙각은 순록 아이템 행을 재사용한다(새 아이콘 0).
        "Folder": "Fogbuck", "BodyFolder": "Reindeer", "ID": "fogbuck", "Name": "안개뿔", "Zone": "zone.snow",
        "Drops": [("reindeer_meat", "순록고기"), ("reindeer_antler", "순록 뿔")],
        "MaxHP": 200.0, "Damage": 0.0, "Behavior": "Flee",
        "Produce": ("shed_antler", "낙각"),
        "Story": True,
    },
]

# 동물 공격 중 **전 종 공통** 값 셋 (DESIGN 3.1 "동물 공격" 표, 감독 결정 2026-09-23).
# 종마다 다른 것은 위 표의 `Damage`·`Behavior` 둘뿐이라 공통 값은 여기 한 곳에 둔다 —
# 열세 번 적으면 한쪽만 고치는 사고가 난다(루프#75). 2층 `Snarehaven.Animal.Attack.MatchesDesign`
# 이 DESIGN 3.1 표를 읽어 여기서 나온 행과 맞대므로, 한쪽만 고치면 검사가 운다.
ATTACK_INTERVAL_SEC = 1.5     # DESIGN 3.1 「동물 공격 간격」 = 1.5초 (플레이어 간격 0.6 의 2.5배)
ATTACK_RANGE_CM = 150.0       # DESIGN 3.1 「동물 공격 사거리」 = 150cm (플레이어 근접 사거리를 빌린 값)
AGGRO_RADIUS_CM = 500.0       # DESIGN 3.1 「선공 감지 반경」 = 500cm (도주 이탈 한계를 빌린 값)

# 선공 종만 위 반경을 쓴다 — 나머지는 0 이고, **0 은 미정이 아니라 "먼저 달려들지 않는다"** 는 값이다.
AGGRO_BEHAVIOR = "Aggressive"


def _clip_ref(asset_dir, body, clip):
    """`A_<body>_<clip>` 이 **실제로 있을 때만** 참조 문자열을 돌려준다 (없으면 빈 칸).

    종마다 가진 클립이 다르다 — 포유류 8종은 6개, 새 4종은 걷기·공격이 없다(DESIGN 8.7
    "새는 날갯짓 도주 하나로 걷기·달리기를 겸한다"). 자리표시 돼지·양도 Quaternius 원본에
    Walk/Run 이 없다. 목록을 손으로 또 적지 않고 **반입된 에셋에 직접 물어본다** — 임포터
    (`import_animal_species.py`)의 클립 목록과 여기가 갈릴 일이 없다 (DataDriven.md 1).
    빈 칸은 코드가 대기 클립으로 떨어뜨린다 (`AAnimalCharacter::ChooseLocomotionAnim`).
    """
    asset_name = "A_{}_{}".format(body, clip)
    asset_path = "{}/{}".format(asset_dir, asset_name)
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return ""
    return "{0}.{1}".format(asset_path, asset_name)


# 돼지가 떨어뜨리는 재료 2종 + 포획했을 때 되는 펫 1종(M1-05).
# 재료 2종은 감독 확정(DESIGN 3.1, 2026-09-19). 재화(스네어 코인)는 드롭에 넣지 않는다 (DESIGN 3.1·4.8).
ITEM_ROWS = [
    {
        "Name": "Item_PigMeat",
        "ItemID": "item.pig_meat",
        "DisplayName": "돼지고기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_PigHide",
        "ItemID": "item.pig_hide",
        "DisplayName": "돼지 가죽",
        "ItemType": "Material",
    },
    # 채집 재료 2종 (M1-06). 무엇을 주는지는 DESIGN 3.3 감독 확정 —
    # 베리 덤불 → 산딸기, 마른 나무 → 나뭇가지. 둘 다 재료라 EItemType::Material.
    {
        "Name": "Item_Berry",
        "ItemID": "item.berry",
        "DisplayName": "산딸기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Wood",
        "ItemID": "item.wood",
        "DisplayName": "나뭇가지",
        "ItemType": "Material",
    },
    # 목장 생산 재료 1종 (M2-02, 감독 결정 2026-09-19 — DESIGN 3.2 "사이클당 생산량").
    # 사냥 드롭(pig_meat/pig_hide)과 겹치지 않는 **목장 전용** 재료다.
    # 이 행이 있다고 지급되는 건 아니다 — 수확·지급은 백엔드 판정 (CLAUDE.md 8, DESIGN 4.1).
    {
        "Name": "Item_Truffle",
        "ItemID": "item.truffle",
        "DisplayName": "송로버섯",
        "ItemType": "Material",
    },
    # 포획한 돼지. 재화가 아니라 목장에 배치하는 생산 수단이다 (DESIGN 3.2·4.1) —
    # 보유·목장 배치는 백엔드가 기록하고, 클라이언트는 등록 요청만 쌓는다 (CLAUDE.md 8).
    {
        "Name": "Item_PetPig",
        "ItemID": "item.pet_pig",
        "DisplayName": "돼지 (펫)",
        "ItemType": "Pet",
    },
    # 소·양의 사냥 드롭 재료 4종 + 펫 2종 (M5-04b). DESIGN 3.1 은 드롭을 "재료 아이템 1~3개 (종별 테이블)
    # … 고기/가죽류" 로만 적고 돼지 외의 종은 적지 않았다 — 그래서 **돼지와 같은 짜임(고기·가죽)** 으로 갔다
    # (🟡 INBOX `확인`). 우유·양털 같은 **목장 전용** 재료 자리는 비워 둔다 — 돼지의 송로버섯처럼
    # 목장 생산물은 사냥 드롭과 겹치지 않아야 하고, 그 값은 아직 사람이 안 정했다.
    {
        "Name": "Item_CowMeat",
        "ItemID": "item.cow_meat",
        "DisplayName": "소고기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_CowHide",
        "ItemID": "item.cow_hide",
        "DisplayName": "소가죽",
        "ItemType": "Material",
    },
    {
        "Name": "Item_SheepMeat",
        "ItemID": "item.sheep_meat",
        "DisplayName": "양고기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_SheepHide",
        "ItemID": "item.sheep_hide",
        "DisplayName": "양가죽",
        "ItemType": "Material",
    },
    # 포획한 소·양. 목장 생산 행(`DT_RanchProductTable`)은 아래 RANCH_ROWS 에 있다 —
    # 무엇을 얼마 만에 생산하는지는 감독이 정했다(2026-09-21, DESIGN 3.2 · DataDriven.md 3).
    {
        "Name": "Item_PetCow",
        "ItemID": "item.pet_cow",
        "DisplayName": "소 (펫)",
        "ItemType": "Pet",
    },
    {
        "Name": "Item_PetSheep",
        "ItemID": "item.pet_sheep",
        "DisplayName": "양 (펫)",
        "ItemType": "Pet",
    },
    # 소·양의 **목장 전용** 생산 재료 2종 (감독 결정 2026-09-21, DESIGN 3.2 "소·양 사이클당 생산량").
    # 루프#75 가 비워 뒀던 자리다 — 돼지의 송로버섯처럼 사냥 드롭(cow_meat/cow_hide …)과 겹치지 않아야
    # 방치형이 사냥과 구분된다. 아이템 ID·이름 모두 감독이 정한 값이고 이 스크립트가 고른 것이 아니다.
    {
        "Name": "Item_Milk",
        "ItemID": "item.milk",
        "DisplayName": "우유",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Wool",
        "ItemID": "item.wool",
        "DisplayName": "양털",
        "ItemType": "Material",
    },
    # 거점 **작물** 3종의 수확물 (감독 결정 2026-09-22, DESIGN 8.2 확정 표 — M6-05 질문 답).
    # 이름·ID 는 전부 그 표에서 왔고 이 스크립트가 고른 것이 없다 (DataDriven.md 3).
    # 재배 시간·상한은 아이템이 아니라 작물 표(CROP_ROWS)가 들고, 사이클당 코인(5·10·20)은
    # 어느 표에도 없다 — 지급액은 백엔드가 판정한다 (CLAUDE.md 8 · 목장 코인 10 과 같은 자리).
    {
        "Name": "Item_Carrot",
        "ItemID": "item.carrot",
        "DisplayName": "당근",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Potato",
        "ItemID": "item.potato",
        "DisplayName": "감자",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Wheat",
        "ItemID": "item.wheat",
        "DisplayName": "밀",
        "ItemType": "Material",
    },
    # 낚시터 **어종** 21종의 수확물 (DESIGN 8.4 확정 표 — 사용자 지시 2026-09-23 으로 3종 → 21종).
    # 이름·ID 는 전부 그 표에서 왔고 이 스크립트가 고른 것이 없다 (DataDriven.md 3). 줄 순서도 표 순서 그대로다
    # — 표와 나란히 놓고 눈으로 대조할 수 있어야 한쪽만 고치는 사고를 사람이 먼저 본다.
    # 낚시 시간·상한은 아이템이 아니라 어종 표(FISHING_ROWS)가 들고, 사이클당 코인(5~60)은
    # 어느 표에도 없다 — 지급액은 백엔드가 판정한다 (CLAUDE.md 8 · 작물·목장 코인과 같은 자리).
    {
        "Name": "Item_CrucianCarp",
        "ItemID": "item.crucian_carp",
        "DisplayName": "붕어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Salmon",
        "ItemID": "item.salmon",
        "DisplayName": "연어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_CaveCrab",
        "ItemID": "item.cave_crab",
        "DisplayName": "동굴 게",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Carp",
        "ItemID": "item.carp",
        "DisplayName": "잉어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Perch",
        "ItemID": "item.perch",
        "DisplayName": "농어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Pike",
        "ItemID": "item.pike",
        "DisplayName": "강꼬치고기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Whitefish",
        "ItemID": "item.whitefish",
        "DisplayName": "백어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Haddock",
        "ItemID": "item.haddock",
        "DisplayName": "해덕대구",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Cod",
        "ItemID": "item.cod",
        "DisplayName": "대구",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Catfish",
        "ItemID": "item.catfish",
        "DisplayName": "메기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Trout",
        "ItemID": "item.trout",
        "DisplayName": "송어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Tuna",
        "ItemID": "item.tuna",
        "DisplayName": "참치",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Stingray",
        "ItemID": "item.stingray",
        "DisplayName": "가오리",
        "ItemType": "Material",
    },
    {
        "Name": "Item_RainbowTrout",
        "ItemID": "item.rainbow_trout",
        "DisplayName": "무지개송어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Redfish",
        "ItemID": "item.redfish",
        "DisplayName": "점성어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_BlackGrouper",
        "ItemID": "item.black_grouper",
        "DisplayName": "검은 참바리",
        "ItemType": "Material",
    },
    {
        "Name": "Item_ArcticChar",
        "ItemID": "item.arctic_char",
        "DisplayName": "북극 곤들매기",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Swordfish",
        "ItemID": "item.swordfish",
        "DisplayName": "황새치",
        "ItemType": "Material",
    },
    {
        "Name": "Item_MistyGrouper",
        "ItemID": "item.misty_grouper",
        "DisplayName": "안개바리",
        "ItemType": "Material",
    },
    {
        "Name": "Item_SalmonShark",
        "ItemID": "item.salmon_shark",
        "DisplayName": "악상어",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Axolotl",
        "ItemID": "item.axolotl",
        "DisplayName": "우파루파",
        "ItemType": "Material",
    },
    {
        "Name": "Item_Eel",
        "ItemID": "item.eel",
        "DisplayName": "장어",
        "ItemType": "Material",
    },
    # 장비 1종 — 몽둥이 (M5-06a · DESIGN 3.5 로스터). **데미지 칸은 여전히 없다**: DESIGN 3.1 근접 공격이
    # "무기 없음(맨손), 종별 차등 없음" 이라 무기는 1타 데미지를 바꾸지 않는다.
    # 메시는 M5-06a 가 임포트한 바르코 몽둥이, 소켓 이름은 DESIGN 3.5 가 정한 `Socket_Weapon` 이다 —
    # **소켓 자체는 아직 스켈레톤에 없다**(M5-06b 몫). 이름만 적어 두는 것이라 지금은 아무것도 안 붙는다.
    # 사거리·감쇠 세 칸은 M11-04 가 더했다 — DESIGN 8.6-b **근접 행 그대로**(150 / 1.0 / 1.0)이고
    # 1.0 / 1.0 은 "감쇠 없음" 이다. 맨손과 같은 값을 굳이 적어 두는 이유는, 총기가 들어올 때
    # (8절 항목7) 값만 갈아 끼우면 되도록 **무기가 사거리의 원본이라는 길을 지금 세워 두는 것**이다.
    {
        "Name": "Item_Club",
        "ItemID": "item.club",
        "DisplayName": "몽둥이",
        "ItemType": "Equipment",
        "EquipSlot": "Weapon",
        "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_Club_01.SM_Weapon_Club_01",
        "EquipSocketName": "Socket_Weapon",
        "GripOffset": "(Location=(X=0,Y=0,Z=10),Rotation=(Pitch=0,Yaw=0,Roll=0))",   # M18-11 DESIGN 3.1 「쥐는 자리」
        "UseSoundCue": "Club",   # M16-07 — 맞을 때 퍽 (`SFX_Club`). 맨손은 이 칸이 비어 기본 `Hit`
        # M18-11 — 한 손 무기 자세 + 옆으로 후려치기 (DESIGN 3.1 「무기별 몸 동작」)
        "HoldPoseAnim": "/Game/Game/Characters/Player/Snare/A_Player_HoldOneHand.A_Player_HoldOneHand",
        "UseAnim": "/Game/Game/Characters/Player/Snare/A_Player_SwingHorizontal.A_Player_SwingHorizontal",
        "AttackRangeCm": 150,
        "FalloffStartRatio": 1.0,
        "MinDamageRatio": 1.0,
    },
]


# 열매 덤불은 거둬도 서 있고 열매만 없어진다 — 손이 가장 뻗은 때(줍기 0.6초 클립 절반)에 (DESIGN 13.4 · INBOX 결함 10-06 02:20).
BERRY_BUSH_MESH_PREFIX = "SM_Nature_BerryBush_"
FRUIT_STRIP_DELAY_SECONDS = 0.3

# 채집 오브젝트 2종 (ROADMAP M1-06 · DESIGN 3.3 감독 확정 2026-09-19).
# 터치 1회당 1개 · 진행바 없음 · 리스폰 없음 — 개수 1 도 설계서 확정값이고 이 스크립트가 만든 숫자가 아니다.
# MeshRef 는 진짜 아트가 들어올 때까지 비워 둔다 (액터의 자리표시 큐브가 대신한다).
GATHERABLE_ROWS = [
    {
        "Name": "Gatherable_BerryBush",
        "GatherableID": "gatherable.berry_bush",
        "DisplayName": "베리 덤불",
        "GrantItemID": "item.berry",
        "GrantCount": 1,
        "BonusToolItemID": "item.machete",         # 13.5 마체테 채집 +1 (🟡 베리 덤불도 채집)
        "MeshRef": "/Game/Game/Environment/Nature/SM_Nature_BerryBush_05.SM_Nature_BerryBush_05",   # M15-21 — 더미 덤불과 같은 베리 메시
        "BodyHeightCm": 110.0,
        "bKeepBodyWhenGathered": True,
        "GatheredVisualDelaySeconds": FRUIT_STRIP_DELAY_SECONDS,
    },
    {
        "Name": "Gatherable_DeadTree",
        "GatherableID": "gatherable.dead_tree",
        "DisplayName": "마른 나무",
        "GrantItemID": "item.wood",
        "GrantCount": 1,
        "MeshRef": "/Game/Game/Environment/Nature/SM_Nature_Stump_01.SM_Nature_Stump_01",   # M15-21 — 마른 나무는 그루터기 메시 (🟡)
        "BodyHeightCm": 120.0,
    },
    # DESIGN 8.10 나무 베기 — 손도끼(도구 칸)로 나무 3번·향나무 4번 쳐야 쓰러지고 한 그루 1~4개(호스트 굴림, 원본 통나무 1~4),
    # 벤 나무는 300초 뒤 다시 자란다 (10-06 감독 확정 — 옛 3~5 는 한 번에 쓰러지던 걸 메우던 값).
    # 몸은 맵에 이미 깔린 장식 나무다 (`DecorMeshPrefix` — 스포너가 그 자리에 세운다, 맵은 안 고친다).
    {
        "Name": "Gatherable_Tree",
        "GatherableID": "gatherable.tree",
        "DisplayName": "나무",
        "GrantItemID": "item.wood",
        "GrantCount": 1,
        "GrantCountMax": 4,
        "HitsToFell": 3,
        "ExtraItemID": "item.resin",               # 레퍼런스처럼 벤 나무에서 수지 0~2 덤 (8.10)
        "ExtraCountMax": 2,
        "RespawnSeconds": 300.0,
        "RequiredToolItemID": "item.hatchet",
        "BonusToolItemID": "item.chainsaw",        # 13.5 전기톱 — 손도끼 대신 들고 베면 그루마다 +1
        "DecorMeshPrefix": "SM_Nature_Tree_",
    },
    # 향나무는 사냥 존만 (8.10). 소나무 장식이 L_Hunt 에만 있어 그 메시를 향나무로 본다 (🟡 메시 대응).
    {
        "Name": "Gatherable_CedarTree",
        "GatherableID": "gatherable.cedar_tree",
        "DisplayName": "향나무",
        "GrantItemID": "item.cedar",
        "GrantCount": 1,
        "GrantCountMax": 4,
        "HitsToFell": 4,                           # 향나무는 한 번 더 (8.10)
        "ExtraItemID": "item.resin",               # 레퍼런스처럼 벤 나무에서 수지 0~2 덤 (8.10)
        "ExtraCountMax": 2,
        "RespawnSeconds": 300.0,
        "RequiredToolItemID": "item.hatchet",
        "BonusToolItemID": "item.chainsaw",        # 13.5 전기톱 — 손도끼 대신 들고 베면 그루마다 +1
        "DecorMeshPrefix": "SM_Nature_Pine_",
    },
]

# M13-02 존 칸 채집 더미 5종 (DESIGN 13.4). 맨손 1개 · 300초 뒤 다시 남 · 무엇을 줄지는 `GrantOptions` 가중치를
# 호스트가 굴린다. 가중치·ID 는 13.4 표 그대로이고 2층 `Snarehaven.Gather.Piles.MatchDesign` 이 표와 맞댄다.
# 몸 메시는 기존 `Nature/` 팩 자리표시다(🟡 13.4 「더미 몸」). 배치는 `setup_zones.py` 가 한다.
GATHER_PILES = [
    # (행 이름 끝, 표시 이름, 메시, 몸 높이 cm, [(아이템, 가중치)]) — 몸 높이는 DESIGN 3.3 더미 크기 표(M13-20).
    ("MushroomPile", "버섯 더미", "SM_Nature_Mushroom_02", 50.0,
     [("chanterelle", 3), ("penny_bun", 3), ("yellowleg_bonnet", 3), ("fly_amanita", 1)]),
    # 섬마다 덤불이 다르다(DESIGN 13.4 · 레퍼런스 가을섬 = 블루베리·링곤베리, 본토 평원 = 블랙베리, 설원 = 클라우드베리)
    ("ForestBush", "블루베리 덤불", "SM_Nature_BerryBush_04", 110.0, [("blueberry", 1)]),
    ("LingonBush", "링곤베리 덤불", "SM_Nature_BerryBush_03", 110.0, [("lingonberry", 1)]),
    ("PlainsBush", "블랙베리 덤불", "SM_Nature_BerryBush_02", 110.0, [("blackberry", 1)]),
    # 평원 = 나무위키 "밀, 사탕무, 베리류, 꽃" · 메제레움 = 원본 Pix's Island(작은 섬, 공식 위키 Berries)
    ("RaspberryBush", "라즈베리 덤불", "SM_Nature_BerryBush_05", 110.0, [("berry", 1)]),
    ("BeetPatch", "야생 사탕무", "SM_Nature_BeetPatch_01", 45.0, [("sugar_beet", 1)]),
    ("WheatPatch", "야생 밀", "SM_Nature_Wheat_01", 80.0, [("wheat", 1)]),
    ("MezereonBush", "서향 덤불", "SM_Nature_BerryBush_06", 110.0, [("mezereon", 1)]),
    ("FlowerPatch", "꽃 더미", "SM_Nature_WildFlower_02", 50.0,
     [("kingcup", 1), ("windflower", 1), ("hepatica", 1), ("poppy", 1), ("fireweed", 1)]),
    ("SnowBush", "설원 덤불", "SM_Nature_BerryBush_01", 110.0, [("cloudberry", 4), ("black_lotus", 1)]),
    # 늪 = 레퍼런스 Wild Growth Update(2026-04) — 늪 버섯 넷(바이퍼스 메인은 전설), 희귀 늪꽃·수련
    ("SwampMushroom", "늪 버섯 더미", "SM_Nature_SwampMushroom_01", 70.0,
     [("honey_mushroom", 3), ("king_stropharia", 3), ("oyster_mushroom", 3), ("vipers_mane", 1)]),
    ("BogFlower", "늪꽃", "SM_Nature_BogFlower_01", 120.0, [("bog_flower", 1)]),
    ("WaterLily", "수련", "SM_Nature_WaterLily_01", 20.0, [("water_lily", 1)]),
]
# 채집 전용 새 아이템 (13.4). 꽃·베리는 13.3 작물(M13-01)이 만든다 — 아직 없으면 아래에서 빈자리만 채운다.
GATHER_ITEMS = [
    ("chanterelle", "살구버섯", "Material", 0),
    ("penny_bun", "그물버섯", "Material", 0),
    ("yellowleg_bonnet", "끈적버섯", "Material", 0),
    ("fly_amanita", "광대버섯", "Food", -20),     # 13.4 광대버섯 먹기 −20
    ("resin", "수지", "Material", 0),
    ("honey_mushroom", "뽕나무버섯", "Material", 0),
    ("king_stropharia", "턱받이버섯", "Material", 0),
    ("oyster_mushroom", "느타리버섯", "Material", 0),
    ("vipers_mane", "독사갈기버섯", "Material", 0),
    ("bog_flower", "늪꽃", "Material", 0),
    ("water_lily", "수련", "Material", 0),
]
GATHER_CROP_NAMES = {"blueberry": "블루베리", "lingonberry": "링곤베리", "blackberry": "블랙베리", "mezereon": "서향 열매", "kingcup": "메리골드", "windflower": "아네모네",
                     "hepatica": "노루귀", "poppy": "양귀비", "fireweed": "분홍바늘꽃", "cloudberry": "클라우드베리",
                     "black_lotus": "검은 연꽃"}
for _suffix, _name, _mesh, _height, _options in GATHER_PILES:
    GATHERABLE_ROWS.append({
        "Name": "Gatherable_" + _suffix,
        "GatherableID": "gatherable." + "".join("_" + c.lower() if c.isupper() else c for c in _suffix).lstrip("_"),
        "DisplayName": _name,
        "GrantOptions": [{"ItemID": "item." + item, "Weight": weight} for item, weight in _options],
        "GrantCount": 1,
        "RespawnSeconds": 300.0,
        "BonusToolItemID": "item.machete",         # 13.5 마체테 채집 +1
        "MeshRef": "/Game/Game/Environment/Nature/{0}.{0}".format(_mesh),
        "BodyHeightCm": _height,
        "bKeepBodyWhenGathered": _mesh.startswith(BERRY_BUSH_MESH_PREFIX),
        "GatheredVisualDelaySeconds": FRUIT_STRIP_DELAY_SECONDS if _mesh.startswith(BERRY_BUSH_MESH_PREFIX) else 0.0,
    })

# DESIGN 8.16 광맥 2종 (M14-02). 곡괭이 필요 · 한 번에 1~3(호스트 굴림) · 600초 뒤 다시 남. 기본 광맥은 곡괭이 셋 다,
# 고급 광맥은 고급·전기만 — 전기 곡괭이는 `BonusToolItemID`(+1, 필요 도구 대신 써도 된다). 몸 = 바르코 광맥 메시(M14-04).
ORE_VEINS = [
    ("OreBasic", "광맥", [("coal_chunk", 4), ("copper_chunk", 3), ("iron_chunk", 3)], "pickaxe_basic", ["pickaxe_advanced"]),
    ("OreAdvanced", "고급 광맥", [("titanium_chunk", 5), ("silver_chunk", 3), ("gold_chunk", 2)], "pickaxe_advanced", []),
]
for _suffix, _name, _options, _tool, _also in ORE_VEINS:
    GATHERABLE_ROWS.append({
        "Name": "Gatherable_" + _suffix,
        "GatherableID": "gatherable." + "".join("_" + c.lower() if c.isupper() else c for c in _suffix).lstrip("_"),
        "DisplayName": _name,
        "GrantOptions": [{"ItemID": "item." + item, "Weight": weight} for item, weight in _options],
        "GrantCount": 1,
        "GrantCountMax": 3,
        "RespawnSeconds": 600.0,
        "RequiredToolItemID": "item." + _tool,
        "AlsoToolItemIDs": ["item." + t for t in _also],
        "BonusToolItemID": "item.pickaxe_electric",
        "MeshRef": "/Game/Game/Environment/Props/{0}/SM_Prop_{0}_01.SM_Prop_{0}_01".format(_suffix),   # M14-04 바르코
        "BodyHeightCm": 90.0,
    })

# INBOX 지시 10-13 「들판 바위를 곡괭이로 캐 광석」 (DESIGN 8.16 「들판 광석 바위」). 광맥과 같은 지급 길(ClaimLoot) · 광석 종류마다 한 행 —
# 겉에 박힌 광석 알갱이 색이 종류를 말해 주므로 굴리는 칸이 아니라 행이 종류를 정한다. 세 번 쳐야 무너지고 600초 뒤 다시 선다.
FIELD_ORE_ROCKS = [
    ("OreRockCoal", "석탄 바위", "coal_chunk", "pickaxe_basic", ["pickaxe_advanced"]),
    ("OreRockCopper", "구리 바위", "copper_chunk", "pickaxe_basic", ["pickaxe_advanced"]),
    ("OreRockIron", "철 바위", "iron_chunk", "pickaxe_basic", ["pickaxe_advanced"]),
    ("OreRockTitanium", "티타늄 바위", "titanium_chunk", "pickaxe_advanced", []),
    ("OreRockSilver", "은 바위", "silver_chunk", "pickaxe_advanced", []),
    ("OreRockGold", "금 바위", "gold_chunk", "pickaxe_advanced", []),
]
for _suffix, _name, _item, _tool, _also in FIELD_ORE_ROCKS:
    GATHERABLE_ROWS.append({
        "Name": "Gatherable_" + _suffix,
        "GatherableID": "gatherable." + "".join("_" + c.lower() if c.isupper() else c for c in _suffix).lstrip("_"),
        "DisplayName": _name,
        "GrantItemID": "item." + _item,
        "GrantCount": 1,
        "GrantCountMax": 3,
        "HitsToFell": 3,
        "RespawnSeconds": 600.0,
        "RequiredToolItemID": "item." + _tool,
        "AlsoToolItemIDs": ["item." + t for t in _also],
        "BonusToolItemID": "item.pickaxe_electric",
        "MeshRef": "/Game/Game/Environment/Nature/SM_Nature_Boulder_02.SM_Nature_Boulder_02",
        "BodyHeightCm": 110.0,
        "TrunkCollisionCm": "(X=110.0,Y=100.0)",
    })


def add_gather_pile_items():
    """채집 더미가 주는 아이템 행 — 이미 있으면(작물 M13-01) 건드리지 않는다. 판매가 0 은 13.4 🟡."""
    for item_id, name, item_type, heal in GATHER_ITEMS:
        if _add_item_row(item_id, name, item_type) and ITEM_ROWS[-1]["ItemID"] == "item." + item_id:
            ITEM_ROWS[-1].update({"HealAmount": heal, "SellPrice": 0, "Grade": "Common"})
            if item_id in ("chanterelle", "penny_bun", "yellowleg_bonnet", "honey_mushroom", "king_stropharia", "oyster_mushroom",
                           "vipers_mane"):
                ITEM_ROWS[-1]["CraftTag"] = "Mushroom"    # 13.6 식용 버섯 7 (광대버섯 제외 — 독사갈기는 M33-02 에 넣음)
    for item_id, name in GATHER_CROP_NAMES.items():
        _add_item_row(item_id, name, "Material")


# DESIGN 8.3·13.8 탈것 목줄 3 · 안장 3 (M13-03, 결정 009). 목줄 = 도감 카테고리 100% 보상(판매가 0),
# 안장 = 고급 작업대 레시피(8.10) — 판매가는 가공품 규칙(재료 값 + 1, 서버 `sellPriceOf` 와 같은 식).
MOUNT_COLLARS = [("collar_hunting", "사냥 고삐"), ("collar_farming", "농사 고삐"), ("collar_fishing", "낚시 고삐")]
MOUNT_SADDLES = [("saddle_hunting", "사냥 길마"), ("saddle_farming", "농사 길마"), ("saddle_fishing", "낚시 길마")]


def add_mount_items():
    """목줄·안장 행과 안장 레시피 3개. RECIPE_ROWS 보다 먼저 불러야 레시피 표에 선다."""
    prices = {row["ItemID"]: row["SellPrice"] for row in ITEM_ROWS}
    hide = min(row["SellPrice"] for row in ITEM_ROWS if row.get("CraftTag") == "Hide")
    for item_id, name in MOUNT_COLLARS + MOUNT_SADDLES:
        _add_item_row(item_id, name, "Material")
        if ITEM_ROWS[-1]["ItemID"] != "item." + item_id:
            continue
        ingredients = [_ing(4, "plank"), _ing(4, tag="Hide")] if item_id.startswith("saddle_") else None
        price = prices["item.plank"] * 4 + hide * 4 + 1 if ingredients else 0
        ITEM_ROWS[-1].update({"SellPrice": price, "Grade": "Common", "CraftTag": "None"})
        if ingredients:
            RECIPES.append((item_id, ingredients, "Workbench", 1))


# DESIGN 13.8 보석 5 · 보물 지도 · 판매용 2 (M13-10) — 위험구역 상자 전리품(10.6). 판매가 = 13.8 「값」 칸
# (서버 `LOOT_SELL_PRICE` 와 같은 값). 보물 지도는 값이 없어 0(🟡 못 판다). 종류는 재료(🟡 쓰임이 판매뿐).
TREASURE_ITEMS = [("ruby", "루비", 15), ("sapphire", "사파이어", 15), ("amethyst", "자수정", 15),
                  ("aquamarine", "아쿠아마린", 15), ("emerald", "에메랄드", 15), ("treasure_map", "보물 지도", 0),
                  ("mystery_bag", "비밀 보따리", 10), ("cash_bundle", "지폐 한 묶음", 20),
                  ("vault_pass", "금고 열쇠패", 0)]   # 8.1c 폐허 금고 열쇠 — 판매 0·건네기 제외 (M104-08)


def add_treasure_items():
    """보석·보물 지도·판매용 행. extend_with_crafting 뒤에 불러야 판매가가 덮이지 않는다."""
    for item_id, name, price in TREASURE_ITEMS:
        _add_item_row(item_id, name, "Material")
        if ITEM_ROWS[-1]["ItemID"] == "item." + item_id:
            ITEM_ROWS[-1].update({"SellPrice": price, "Grade": "Common", "CraftTag": "None"})


# DESIGN 8.17 고구마 시세 — 고구마 장수에게 사서 잡화상에 판다(`BuySweetPotato`·`SellSweetPotato`). 판매가는 시세가 정하므로
# 표의 SellPrice 는 0(서버 `sellPriceOf` 도 0 — 일반 `SellItem` 으로는 안 팔린다). 썩은 고구마는 버리기만.
SWEET_POTATO_ITEMS = [("sweet_potato", "고구마"), ("sweet_potato_rotten", "썩은 고구마")]


def add_sweet_potato_items():
    for item_id, name in SWEET_POTATO_ITEMS:
        _add_item_row(item_id, name, "Material")
        if ITEM_ROWS[-1]["ItemID"] == "item." + item_id:
            ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": "Common", "CraftTag": "None"})


# DESIGN 8.18 과일나무 — 고유 과일 1종(서버 `nativeFruitOf`)·친구 거점에서 얻은 남의 과일. 먹기 체력 +10(사과 VN402 와 같다).
# 표의 SellPrice 는 고유 과일 값 8(서버 `sellPriceOf` 와 전수 대조) — 남의 과일 40 은 서버 `SellItem` 이 계정마다 정한다.
# 재료 태그 `Fruit`(사과 파이)는 사과 파이 판매가 연쇄가 있어 이 조각에서 안 붙였다(INBOX 확인).
FRUIT_ITEMS = [("pear", "배"), ("peach", "복숭아"), ("cherry", "체리"), ("orange", "오렌지"), ("plum", "자두")]


def add_fruit_items():
    for item_id, name in FRUIT_ITEMS:
        _add_item_row(item_id, name, "Food")
        if ITEM_ROWS[-1]["ItemID"] == "item." + item_id:
            ITEM_ROWS[-1].update({"HealAmount": 10, "SellPrice": 8, "Grade": "Common", "CraftTag": "None"})


# DESIGN 13.8 페인트 7 (M13-11) — 잡화점 각 2(판매 0, 상점 물건), 작업대 칠하기 탭에서 가구 1개 = 1통.
# 이름·칠한 색은 13.8 「이름」·「칠한 색」 칸 그대로(🟡). 종류는 재료(🟡 EItemType 에 소모품이 없다).
PAINT_ITEMS = [("paint_white", "하양 물감", "F2F0EA"), ("paint_charcoal", "숯검정 물감", "3A3A3C"),
               ("paint_blue", "파랑 물감", "3F6FB5"), ("paint_red", "빨강 물감", "C0413A"),
               ("paint_yellow", "노랑 물감", "E8C547"), ("paint_mint", "박하 물감", "8FD9C0"),
               ("paint_blush", "볼연지 물감", "E8A6A6")]


# DESIGN 10.8 꾸미기 상자·열쇠 (상점 개편 B). **숫자는 10.8 표의 사본이다** — 서버 `CRATE_POOLS`·`SHOP_ITEMS`
# 와 2층 node 검사(「꾸미기 상자 10.8」)·C++ `Snarehaven.Crate.*` 가 맞댄다. 계절이 바뀌면 상자 행과 CRATE_ROWS 를 새로 더한다.
CRATE_ITEMS = [("crate_autumn26", "가을 꾸미기 상자"), ("crate_key", "상자 열쇠")]
CRATE_KEY_GEM_PRICE = 10
# 가을 상자 풀 = 꾸미기 42벌(COSMETIC_*) + 목장 꾸미기 3 — 옷장 상점(10.7)과 같은 45종. 효과 없는 꾸미기만(10.3).
CRATE_SEASON = ("crate_autumn26", "autumn26")
CRATE_GRADE_PCT = {"Common": 75.0, "Rare": 20.0, "Epic": 5.0}


def crate_pool():
    """(상자, 계절, 나오는 것, 등급, %) — 등급 % 를 등급 안 품목 수로 고르게 나눈다. 순서 = 서버 `cratePoolOf`(일반 → 희귀 → 에픽)."""
    entries = [(item_id, grade) for table in (COSMETIC_HATS, COSMETIC_FACES, COSMETIC_TOPS, COSMETIC_BOTTOMS,
                                              COSMETIC_SHOES, COSMETIC_GLOVES, COSMETIC_BACKPACKS)
               for item_id, _name, grade, *_rest in table]
    entries += [(item_id, grade) for item_id, _name, grade, *_rest in RANCH_COSMETIC_ITEMS]
    pool = []
    for grade, pct in CRATE_GRADE_PCT.items():
        in_grade = [item_id for item_id, g in entries if g == grade]
        pool += [(CRATE_SEASON[0], CRATE_SEASON[1], item_id, grade, pct / len(in_grade)) for item_id in in_grade]
    return pool


def add_crate_items():
    """상자·열쇠 행. 못 판다(판매가 0) — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name in CRATE_ITEMS:
        _add_item_row(item_id, name, "Material")
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": "Common", "CraftTag": "None"})


# DESIGN 10.9 목장 동물 꾸미기 (상점 개편 C) — 효과 없는 겉모습, 목장 칸 하나에 하나. 리본·목걸이는 스킨 조각 n
# 전용 메시(`Equipment/Pet/`), 모자도 동물용 둥근 모자 전용 메시(M40-05 루프#597 — 전엔 농부 모자를 빌림). 붙는 곳은 동물 뼈(`head`·`neck`), 폭은 펫 키 × 비율
# (동물마다 키가 달라 월드 cm 하나로는 못 맞춘다). 등급은 상자 풀(10.8) 칸과 같다 — 서버 `RANCH_COSMETICS`·`CRATE_POOLS` 가 맞댄다.
PET_DIR = "/Game/Game/Characters/Equipment/Pet"
RANCH_COSMETIC_ITEMS = [
    ("ranch_ribbon", "동물 리본", "Common", "{0}/RanchRibbon/SM_Pet_RanchRibbon_01", "head", 0.35),
    ("ranch_necklace", "동물 목걸이", "Common", "{0}/RanchNecklace/SM_Pet_RanchNecklace_01", "neck", 0.45),
    ("ranch_hat", "동물 모자", "Rare", "{0}/RanchHat/SM_Pet_RanchHat_01", "head", 0.5),
]


def add_ranch_cosmetic_items():
    """목장 꾸미기 행. 젬 물건이라 못 판다(판매가 0) — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, mesh, bone, ratio in RANCH_COSMETIC_ITEMS:
        _add_item_row(item_id, name, "PetCosmetic")
        path = mesh.format(PET_DIR, HAT_DIR)
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None",
                              "EquipMeshRef": "{0}.{1}".format(path, path.rsplit("/", 1)[1]),
                              "EquipSocketName": bone, "PetCosmeticWidthRatio": ratio})


# M35-04 가챠 「펫 의상」 경품 6종(`docs/GACHA_DISCLOSURE.md`) — 위 목장 꾸미기와 같은 1칸에 씌운다(서버 `GACHA_PET_COSMETICS`).
# 행 ID 는 가챠 ID 그대로(`gacha.` — 서버가 주는 아이템 ID). 리본은 위 리본 메시를 다시 쓰고, 왕관·축제 깃발·
# 황금 갈기(루프#538)·방울 목걸이(M40-02 루프#592)·밀짚모자(M40-03 루프#595)는 전용 메시(`ranch_cosmetic_make.py` `Gacha*`). 마지막 칸 = 목 굵기 배수(0 = 목걸이 기본 1.4, 방울 1.25·깃발 1.7·갈기 1.8 —
# DESIGN 10.9). 상자·옷장 풀에는 넣지 않는다(뽑기 전용).
GACHA_PET_COSMETIC_ITEMS = [
    ("gacha.pet_ribbon", "펫 리본 목줄", "Common", "{0}/GachaRibbon/SM_Pet_GachaRibbon_01", "neck", 0.45, 1.3),
    ("gacha.pet_strawhat", "밀짚모자 펫장식", "Common", "{0}/GachaStrawhat/SM_Pet_GachaStrawhat_01", "head", 0.5, 0.0),
    ("gacha.pet_bell", "방울 목걸이 펫장식", "Rare", "{0}/GachaBell/SM_Pet_GachaBell_01", "neck", 0.45, 1.25),
    ("gacha.pet_crown", "왕관 펫장식", "Epic", "{0}/GachaCrown/SM_Pet_GachaCrown_01", "head", 0.5, 0.0),
    ("gacha.pet_festival_flags", "축제 깃발 세트", "Epic", "{0}/GachaFestivalFlags/SM_Pet_GachaFestivalFlags_01", "neck", 0.45, 1.5),
    ("gacha.pet_gold_mane", "황금 갈기 펫스킨", "Legendary", "{0}/GachaGoldMane/SM_Pet_GachaGoldMane_01", "neck", 0.45, 1.8),
]


def add_gacha_pet_cosmetic_items():
    """가챠 펫 의상 행. `_add_item_row` 는 `item.` 을 붙이므로 행을 직접 세운다. 뽑기 경품이라 못 판다(판매가 0)."""
    for item_id, name, grade, mesh, bone, ratio, per_neck in GACHA_PET_COSMETIC_ITEMS:
        path = mesh.format(PET_DIR, HAT_DIR)
        ITEM_ROWS.append({"Name": _item_row_name(item_id.split(".", 1)[1].replace("pet_", "gacha_pet_", 1)),
                          "ItemID": item_id, "DisplayName": name, "ItemType": "PetCosmetic",
                          "SellPrice": 0, "Grade": grade, "CraftTag": "None",
                          "EquipMeshRef": "{0}.{1}".format(path, path.rsplit("/", 1)[1]),
                          "EquipSocketName": bone, "PetCosmeticWidthRatio": ratio, "PetCollarWidthPerNeck": per_neck})


# M35-05b 가챠 「목장 데코」 경품 5종(`docs/GACHA_DISCLOSURE.md`) — 목장 겉모습 스킨(`docs/specs/gacha-deco.md` 4장 · DESIGN 10.9b).
# 종류(`kind`)는 서버 `GACHA_RANCH_DECOS` 와 같다. 메시·재질은 이미 반입된 것만(새 메시 0) — 돌담은 널판 울타리에 돌길 재질
# 자리표시 🟡, 흙길·붉은 지붕 MI 는 `setup_ranch_buildings.py` `HOUSE_MATERIALS` 가 만든다. 별빛 = 해 ×0.35 · 하늘빛 ×0.6 · 푸른빛.
# (ID, 이름, 등급, 종류, 메시, 재질, 재질 슬롯, 해 배수, 하늘 배수, 해 색)
RANCH_DECO_BUILDING_DIR = "/Game/Game/Environment/Buildings"
GACHA_RANCH_DECO_ITEMS = [
    ("gacha.deco_wood_fence", "나무 울타리 스킨", "Common", "fence", "SM_Building_Fence_01", None, None, 0.0, 0.0, None),
    ("gacha.deco_dirt_path", "흙길 타일", "Common", "path", None, "MI_Building_PathDirt", None, 0.0, 0.0, None),
    ("gacha.deco_stone_fence", "돌담 울타리 스킨", "Rare", "fence", "SM_Building_Fence_02", "MI_Building_PathStone", None, 0.0, 0.0, None),
    ("gacha.deco_red_barn", "붉은 지붕 헛간 스킨", "Epic", "barn", "SM_Building_Barn_02", "MI_Building_BarnRoofRed", "RoofBlack",
     0.0, 0.0, None),
    ("gacha.deco_starlight_bg", "별빛 목장 배경 스킨", "Legendary", "sky", None, None, None, 0.35, 0.6, (0.55, 0.62, 1.0)),
]


def add_gacha_ranch_deco_items():
    """가챠 목장 데코 행. `_add_item_row` 는 `item.` 을 붙이므로 행을 직접 세운다. 뽑기 경품이라 못 판다(판매가 0)."""
    def ref(name):
        return "" if name is None else "{0}/{1}.{1}".format(RANCH_DECO_BUILDING_DIR, name)
    for item_id, name, grade, kind, mesh, material, slot, sun, sky, color in GACHA_RANCH_DECO_ITEMS:
        row = {"Name": _item_row_name(item_id.split(".", 1)[1].replace("deco_", "gacha_deco_", 1)),
               "ItemID": item_id, "DisplayName": name, "ItemType": "RanchDeco",
               "SellPrice": 0, "Grade": grade, "CraftTag": "None",
               "RanchDecoKind": kind, "RanchDecoMeshRef": ref(mesh), "RanchDecoMaterialRef": ref(material),
               "RanchDecoMaterialSlot": slot or "None", "RanchDecoSunScale": sun, "RanchDecoSkyScale": sky}
        if color is not None:
            row["RanchDecoSunColor"] = "(R={},G={},B={},A=1.0)".format(*color)
        ITEM_ROWS.append(row)


# 스킨 조각 f (`docs/specs/skin-parts.md` 4장) — 사람이 쓰는 꾸미기 모자 6벌, 두 세트(들꽃·항구). 효과 없는 겉모습,
# 머리 칸에 `UEquipmentComponent::SetCosmetic` 으로 입는다. 메시·붙는 곳·폭은 장비 모자와 같은 규칙(폭 = v2 메시 폭 × 1.76).
# 등급은 명세 「칸마다 일반 3·희귀 2·에픽 1」. 젬 물건이라 못 판다(판매가 0).
COSMETIC_HATS = [
    ("meadow_bucket", "들꽃 벙거지", "Common", "MeadowBucket", 155.4),
    ("meadow_cap", "들꽃 챙모자", "Common", "MeadowCap", 136.1),
    ("meadow_frog", "청개구리 모자", "Rare", "MeadowFrog", 114.9),
    ("harbor_beanie", "줄무늬 방울 비니", "Common", "HarborBeanie", 123.2),
    ("harbor_captain", "항구 선장모", "Rare", "HarborCaptain", 110.7),
    ("harbor_whale", "고래 모자", "Epic", "HarborWhale", 151.8),
    # M83-02 (루프#856) — `tools/blender/hats_more_make.py`, 폭 = 메시 폭 × 1.76 (루프#857 머리 틀을 실제 머리 크기로 키워 폭 갱신)
    ("meadow_acorn", "도토리 모자", "Common", "MeadowAcorn", 131.8),
    ("harbor_souwester", "노란 비옷 모자", "Common", "HarborSouwester", 129.6),
    ("meadow_crown", "황금 왕관", "Rare", "MeadowCrown", 112.8),
    ("harbor_bandana", "해적 두건", "Rare", "HarborBandana", 109.3),
    ("meadow_wizard", "별빛 마법사 모자", "Epic", "MeadowWizard", 144.2),
    # M83-03 (루프#858) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_sunflower", "해바라기 모자", "Common", "MeadowSunflower", 164.7),
    ("harbor_dixie", "수병 모자", "Common", "HarborDixie", 131.6),
    ("harbor_horned", "뿔 투구", "Rare", "HarborHorned", 157.5),
    ("meadow_mushroom", "버섯 모자", "Rare", "MeadowMushroom", 148.7),
    ("meadow_tophat", "꽃 실크햇", "Epic", "MeadowTopHat", 128.6),
    # M84-02 (루프#865) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_cowboy", "카우보이 모자", "Common", "MeadowCowboy", 162.6),
    ("meadow_bear", "곰 모자", "Common", "MeadowBear", 112.8),
    ("meadow_catears", "고양이 귀 모자", "Rare", "MeadowCatEars", 110.7),
    ("harbor_tricorn", "삼각 모자", "Rare", "HarborTricorn", 155.8),
    ("meadow_propeller", "프로펠러 모자", "Epic", "MeadowPropeller", 115.3),
    # M84-03 (루프#866) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_beret", "베레모", "Common", "MeadowBeret", 136.1),
    ("harbor_fez", "페즈", "Common", "HarborFez", 111.4),
    ("meadow_bunny", "토끼 모자", "Rare", "MeadowBunny", 111.4),
    ("meadow_pumpkin", "호박 모자", "Rare", "MeadowPumpkin", 158.0),
    ("harbor_shark", "상어 모자", "Epic", "HarborShark", 151.4),
    # M88-02 (루프#890) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_ladybug", "무당벌레 모자", "Common", "MeadowLadybug", 126.7),
    ("harbor_lighthouse", "등대 모자", "Rare", "HarborLighthouse", 111.4),
    ("meadow_flowerpot", "화분 모자", "Common", "MeadowFlowerpot", 136.4),
    ("meadow_carrot", "당근 모자", "Rare", "MeadowCarrot", 110.7),
    ("harbor_jellyfish", "해파리 모자", "Epic", "HarborJellyfish", 130.9),
    # M88-03 (루프#891) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("harbor_sailor_cap", "세일러 모자", "Common", "HarborSailorCap", 124.1),
    ("meadow_teapot", "찻주전자 모자", "Common", "MeadowTeapot", 170.9),
    ("meadow_owl", "부엉이 모자", "Rare", "MeadowOwl", 130.2),
    ("harbor_crab", "게 모자", "Rare", "HarborCrab", 145.1),
    ("meadow_cake", "케이크 모자", "Epic", "MeadowCake", 136.5),
    # M90-01 (루프#898) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_snail", "달팽이 모자", "Common", "MeadowSnail", 111.4),
    ("harbor_buoy", "부표 모자", "Common", "HarborBuoy", 111.4),
    ("meadow_pineapple", "파인애플 모자", "Rare", "MeadowPineapple", 124.1),
    ("harbor_starfish", "불가사리 모자", "Rare", "HarborStarfish", 111.4),
    ("meadow_rocket", "로켓 모자", "Epic", "MeadowRocket", 114.3),
    # M90-02 (루프#899) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_duck", "오리 모자", "Common", "MeadowDuck", 159.1),
    ("meadow_beehive", "벌집 모자", "Common", "MeadowBeehive", 145.2),
    ("harbor_conch", "소라 모자", "Rare", "HarborConch", 111.4),
    ("meadow_cactus", "선인장 모자", "Rare", "MeadowCactus", 111.4),
    ("meadow_ufo", "UFO 모자", "Epic", "MeadowUfo", 167.8),
    # M91-01 (루프#904) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_penguin", "펭귄 모자", "Common", "MeadowPenguin", 133.4),
    ("meadow_donut", "도넛 모자", "Common", "MeadowDonut", 114.4),
    ("meadow_mouse", "쥐 모자", "Rare", "MeadowMouse", 144.9),
    ("harbor_igloo", "이글루 모자", "Rare", "HarborIgloo", 163.1),
    ("harbor_rainbow", "무지개 모자", "Rare", "HarborRainbow", 153.5),
    ("harbor_treasure", "보물 상자 모자", "Epic", "HarborTreasure", 111.4),
    # M91-02 (루프#905) — 같은 스크립트, 폭 = 메시 폭 × 1.76
    ("meadow_fox", "여우 모자", "Common", "MeadowFox", 119.7),
    ("meadow_pig", "돼지 모자", "Common", "MeadowPig", 147.8),
    ("harbor_sailboat", "돛단배 모자", "Rare", "HarborSailboat", 126.7),
    ("meadow_cheese", "치즈 모자", "Rare", "MeadowCheese", 111.4),
    ("meadow_dragon", "용 모자", "Epic", "MeadowDragon", 123.7),
]


# 머리를 덮지 않는 장식 — 써도 머리카락을 남긴다(DESIGN 3.5 「모자 쓸 때 머리카락」, 행 `bHatKeepsHair`).
# 고르는 법: `hats_more_make.py` 에서 윗면 돔(`dome`·`_plain_dome`·`inner`) 없이 띠만 두른 메시 = 왕관 하나.
HAT_KEEPS_HAIR = {"meadow_crown"}


def add_cosmetic_hats():
    """꾸미기 모자 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder, width_cm in COSMETIC_HATS:
        _add_item_row(item_id, name, "Cosmetic")
        mesh = "SM_Hat_{}_01".format(folder)
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "Head",
                              "EquipMeshRef": "{0}/{1}/{2}.{2}".format(HAT_DIR, folder, mesh),
                              "EquipSocketName": "Socket_Head", "HatWidthCm": width_cm,
                              "bHatKeepsHair": item_id in HAT_KEEPS_HAIR})


# 스킨 조각 g (`docs/specs/skin-parts.md` 4장) — 꾸미기 얼굴 장식 6벌, 모자와 같은 두 세트(들꽃·항구). 효과 없는 겉모습,
# 얼굴 장식 칸에 `UEquipmentComponent::SetCosmetic` 으로 입는다. 메시는 몸 공간 치수 그대로·받침점 = `Socket_Face` 라
# 몸 배율을 따른다(`bEquipFollowsBodyScale`). 등급은 명세 「칸마다 일반 3·희귀 2·에픽 1」. 젬 물건이라 못 판다(판매가 0).
FACE_DIR = "/Game/Game/Characters/Equipment/Faces"
COSMETIC_FACES = [
    ("meadow_ladybug_nose", "무당벌레 코", "Common", "MeadowLadybugNose"),
    ("meadow_daisy_glasses", "데이지 동그란 안경", "Common", "MeadowDaisyGlasses"),
    ("meadow_petal_goggles", "꽃잎 고글", "Rare", "MeadowPetalGoggles"),
    ("harbor_eyepatch", "줄무늬 안대", "Common", "HarborEyepatch"),
    ("harbor_sunglasses", "항구 선글라스", "Rare", "HarborSunglasses"),
    ("harbor_diving_mask", "잠수 마스크", "Epic", "HarborDivingMask"),
    # M87-01 +4 (루프#883) — 일반 2·희귀 1·에픽 1
    ("meadow_mustache", "갈색 콧수염", "Common", "MeadowMustache"),
    ("harbor_bandage", "코 반창고", "Common", "HarborBandage"),
    ("harbor_monocle", "금테 외눈 안경", "Rare", "HarborMonocle"),
    ("meadow_star_shades", "별빛 안경", "Epic", "MeadowStarShades"),
    # M87-02 +4 (루프#884) — 일반 2·희귀 1·에픽 1
    ("harbor_kerchief", "남색 얼굴 두건", "Common", "HarborKerchief"),
    ("meadow_cheek_stars", "뺨 별 스티커", "Common", "MeadowCheekStars"),
    ("meadow_heart_shades", "하트 안경", "Rare", "MeadowHeartShades"),
    ("harbor_white_beard", "흰 수염", "Epic", "HarborWhiteBeard"),
]


def add_cosmetic_faces():
    """꾸미기 얼굴 장식 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder in COSMETIC_FACES:
        _add_item_row(item_id, name, "Cosmetic")
        mesh = "SM_Face_{}_01".format(folder)
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "FaceAccessory",
                              "EquipMeshRef": "{0}/{1}/{2}.{2}".format(FACE_DIR, folder, mesh),
                              "EquipSocketName": "Socket_Face", "bEquipFollowsBodyScale": True})


# 스킨 조각 h (`docs/specs/skin-parts.md` 4장) — 꾸미기 상의 6벌, 모자·얼굴 장식과 같은 두 세트(들꽃·항구). 효과 없는 겉모습,
# 상의 칸에 `UEquipmentComponent::SetCosmetic` 으로 입는다 — 플란넬 셔츠처럼 몸 스켈레톤 스켈레탈 레이어(`tops_make.py`).
# 등급은 명세 「칸마다 일반 3·희귀 2·에픽 1」. 젬 물건이라 못 판다(판매가 0). 메시 경로는 `_outfit_mesh` 와 같은 규칙.
COSMETIC_TOPS = [
    ("meadow_tee", "들꽃 반팔 티", "Common", "MeadowTee"),
    ("meadow_knit", "들꽃 니트", "Common", "MeadowKnit"),
    ("meadow_petal_blouse", "꽃잎 주름 블라우스", "Rare", "MeadowPetalBlouse"),
    ("harbor_stripe_tee", "줄무늬 마린 티", "Common", "HarborStripeTee"),
    ("harbor_puffer", "항구 패딩", "Rare", "HarborPuffer"),
    ("harbor_sailor", "세일러 깃 셔츠", "Epic", "HarborSailor"),
    # M83-04 (루프#859) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_gingham", "들꽃 체크 셔츠", "Common", "MeadowGingham"),
    ("harbor_island_shirt", "섬 꽃무늬 셔츠", "Rare", "HarborIslandShirt"),
    ("meadow_bee_sweater", "꿀벌 줄무늬 스웨터", "Epic", "MeadowBeeSweater"),
    # M83-05 (루프#861) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_cherry_tee", "버찌 무늬 티", "Common", "MeadowCherryTee"),
    ("harbor_anchor_sweat", "닻무늬 맨투맨", "Rare", "HarborAnchorSweat"),
    ("meadow_patchwork", "조각보 셔츠", "Epic", "MeadowPatchwork"),
    # M84-04 (루프#867) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_leaf_tee", "잎새 무늬 티", "Common", "MeadowLeafTee"),
    ("harbor_wave_sweat", "물결 줄무늬 맨투맨", "Rare", "HarborWaveSweat"),
    ("meadow_rainbow", "무지개 줄무늬 셔츠", "Epic", "MeadowRainbow"),
    # M84-05 (루프#868) — `tops_make.py` 에 무늬만 다른 셋
    ("harbor_fish_tee", "물고기 무늬 티", "Common", "HarborFishTee"),
    ("meadow_strawberry", "딸기 무늬 맨투맨", "Rare", "MeadowStrawberry"),
    ("harbor_sunset", "노을빛 셔츠", "Epic", "HarborSunset"),
    # M88-04 (루프#892) — `tops_make.py` 에 무늬만 다른 셋
    ("harbor_zigzag", "지그재그 티", "Common", "HarborZigzag"),
    ("meadow_toadstool", "독버섯 맨투맨", "Rare", "MeadowToadstool"),
    ("harbor_aurora", "오로라 셔츠", "Epic", "HarborAurora"),
    # M88-05 (루프#893) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_cactus_tee", "선인장 티", "Common", "MeadowCactusTee"),
    ("harbor_snow_sweat", "눈송이 맨투맨", "Rare", "HarborSnowSweat"),
    ("meadow_honeycomb", "벌집 셔츠", "Epic", "MeadowHoneycomb"),
    # M90-03 (루프#900) — `tops_make.py` 에 무늬만 다른 셋
    ("harbor_gull_tee", "갈매기 티", "Common", "HarborGullTee"),
    ("meadow_pine_sweat", "소나무 맨투맨", "Rare", "MeadowPineSweat"),
    ("harbor_flame", "불꽃 셔츠", "Epic", "HarborFlame"),
    # M90-04 (루프#901) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_daisy_tee", "데이지 티", "Common", "MeadowDaisyTee"),
    ("meadow_diamond_sweat", "마름모 맨투맨", "Rare", "MeadowDiamondSweat"),
    ("harbor_galaxy", "은하 셔츠", "Epic", "HarborGalaxy"),
    # M91-03 (루프#906) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_sun_tee", "해님 티", "Common", "MeadowSunTee"),
    ("harbor_kelp_sweat", "다시마 맨투맨", "Rare", "HarborKelpSweat"),
    ("meadow_firefly", "반딧불 셔츠", "Epic", "MeadowFirefly"),
    # M91-04 (루프#907) — `tops_make.py` 에 무늬만 다른 셋
    ("harbor_shell_tee", "조개 티", "Common", "HarborShellTee"),
    ("harbor_moon_sweat", "초승달 맨투맨", "Rare", "HarborMoonSweat"),
    ("harbor_lantern", "등불 셔츠", "Epic", "HarborLantern"),
    # M92-01 (루프#913) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_acorn_tee", "도토리 티", "Common", "MeadowAcornTee"),
    ("harbor_rope_sweat", "밧줄 맨투맨", "Rare", "HarborRopeSweat"),
    ("harbor_coral", "산호 셔츠", "Epic", "HarborCoral"),
    # M92-02 (루프#914) — `tops_make.py` 에 무늬만 다른 셋
    ("meadow_clover_tee", "클로버 티", "Common", "MeadowCloverTee"),
    ("harbor_compass_sweat", "나침반 맨투맨", "Rare", "HarborCompassSweat"),
    ("meadow_butterfly", "나비 셔츠", "Epic", "MeadowButterfly"),
    # M92-03 (루프#915) — `tops_make.py` 에 무늬만 다른 둘
    ("meadow_plum_tee", "자두 티", "Common", "MeadowPlumTee"),
    ("harbor_meteor", "별똥별 셔츠", "Epic", "HarborMeteor"),
]


def add_cosmetic_tops():
    """꾸미기 상의 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder in COSMETIC_TOPS:
        _add_item_row(item_id, name, "Cosmetic")
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "OutfitTop",
                              **_outfit_mesh(folder)})


# 스킨 조각 i (`docs/specs/skin-parts.md` 4장) — 꾸미기 하의 6벌, 상의와 같은 두 세트(들꽃·항구). 효과 없는 겉모습,
# 하의 칸에 `UEquipmentComponent::SetCosmetic` 으로 입는다 — 작업 바지처럼 몸 스켈레톤 스켈레탈 레이어(`bottoms_make.py`).
COSMETIC_BOTTOMS = [
    ("meadow_shorts", "들꽃 반바지", "Common", "MeadowShorts"),
    ("meadow_corduroy", "들꽃 골덴 바지", "Common", "MeadowCorduroy"),
    ("meadow_petal_skirt", "꽃잎 치마", "Rare", "MeadowPetalSkirt"),
    ("harbor_cargo", "항구 카고 바지", "Common", "HarborCargo"),
    ("harbor_rain_pants", "항구 비옷 바지", "Rare", "HarborRainPants"),
    ("harbor_bell_bottoms", "세일러 나팔바지", "Epic", "HarborBellBottoms"),
    # M83-04 (루프#859) — `bottoms_make.py` 에 둘
    ("harbor_capri", "항구 줄무늬 7부 바지", "Common", "HarborCapri"),
    ("meadow_star_pants", "별무늬 바지", "Rare", "MeadowStarPants"),
    # M83-05 (루프#861) — `bottoms_make.py` 에 둘
    ("harbor_sail_shorts", "돛 줄무늬 반바지", "Common", "HarborSailShorts"),
    ("meadow_patch_pants", "조각보 바지", "Rare", "MeadowPatchPants"),
    # M84-04 (루프#867) — `bottoms_make.py` 에 둘
    ("harbor_check_shorts", "청록 체크 반바지", "Common", "HarborCheckShorts"),
    ("meadow_denim", "청 데님 바지", "Rare", "MeadowDenim"),
    # M84-05 (루프#868) — `bottoms_make.py` 에 둘
    ("meadow_plaid_shorts", "주황 격자 반바지", "Common", "MeadowPlaidShorts"),
    ("harbor_polka", "노란 물방울 바지", "Rare", "HarborPolka"),
    # M88-04 (루프#892) — `bottoms_make.py` 에 둘
    ("meadow_cloud_shorts", "구름 반바지", "Common", "MeadowCloudShorts"),
    ("harbor_camo", "얼룩 위장 바지", "Rare", "HarborCamo"),
    # M88-05 (루프#893) — `bottoms_make.py` 에 둘
    ("meadow_moon_shorts", "초승달 반바지", "Common", "MeadowMoonShorts"),
    ("harbor_argyle", "아가일 바지", "Rare", "HarborArgyle"),
    # M90-03 (루프#900) — `bottoms_make.py` 에 둘
    ("meadow_ladybug_shorts", "무당벌레 반바지", "Common", "MeadowLadybugShorts"),
    ("harbor_tartan", "타탄 바지", "Rare", "HarborTartan"),
    # M90-04 (루프#901) — `bottoms_make.py` 에 둘
    ("meadow_cherry_shorts", "체리 반바지", "Common", "MeadowCherryShorts"),
    ("harbor_pinstripe", "핀스트라이프 바지", "Rare", "HarborPinstripe"),
    # M91-03 (루프#906) — `bottoms_make.py` 에 둘
    ("meadow_leaf_shorts", "나뭇잎 반바지", "Common", "MeadowLeafShorts"),
    ("harbor_wave_pants", "물결 바지", "Rare", "HarborWavePants"),
    # M91-04 (루프#907) — `bottoms_make.py` 에 둘
    ("meadow_paw_shorts", "발자국 반바지", "Common", "MeadowPawShorts"),
    ("harbor_bolt_pants", "번개 바지", "Rare", "HarborBoltPants"),
    # M92-01 (루프#913) — `bottoms_make.py` 에 둘
    ("meadow_shroom_shorts", "버섯 반바지", "Common", "MeadowShroomShorts"),
    ("harbor_chevron", "갈래 줄 바지", "Rare", "HarborChevron"),
    # M92-02 (루프#914) — `bottoms_make.py` 에 둘
    ("meadow_raindrop_shorts", "빗방울 반바지", "Common", "MeadowRaindropShorts"),
    ("harbor_weave", "엮은 줄 바지", "Rare", "HarborWeave"),
    # M92-03 (루프#915) — `bottoms_make.py` 에 둘
    ("meadow_sprout_shorts", "새싹 반바지", "Common", "MeadowSproutShorts"),
    ("harbor_dive_flag", "잠수 깃발 바지", "Rare", "HarborDiveFlag"),
]


def add_cosmetic_bottoms():
    """꾸미기 하의 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder in COSMETIC_BOTTOMS:
        _add_item_row(item_id, name, "Cosmetic")
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "OutfitBottom",
                              **_outfit_mesh(folder)})


# 스킨 조각 j (`docs/specs/skin-parts.md` 4장) — 꾸미기 신발 6벌, 같은 두 세트(들꽃·항구). 효과 없는 겉모습,
# 신발 칸에 `UEquipmentComponent::SetCosmetic` 으로 신는다 — 몸 스켈레톤 스켈레탈 레이어(`shoes_make.py`).
COSMETIC_SHOES = [
    ("meadow_canvas", "들꽃 캔버스화", "Common", "MeadowCanvas"),
    ("meadow_clogs", "데이지 나막신", "Common", "MeadowClogs"),
    ("meadow_petal_boots", "꽃잎 부츠", "Rare", "MeadowPetalBoots"),
    ("harbor_deck_shoes", "항구 갑판화", "Common", "HarborDeckShoes"),
    ("harbor_rain_boots", "항구 장화", "Rare", "HarborRainBoots"),
    ("harbor_flippers", "항구 오리발", "Epic", "HarborFlippers"),
    # M87-03 (루프#885) — `shoes_make.py` 에 무늬·목 높이만 다른 셋 + 발목 날개 하나
    ("harbor_hightops", "항구 하이탑", "Common", "HarborHighTops"),
    ("meadow_star_kicks", "별무늬 운동화", "Common", "MeadowStarKicks"),
    ("meadow_berry_boots", "산딸기 부츠", "Rare", "MeadowBerryBoots"),
    ("meadow_wing_kicks", "날개 운동화", "Epic", "MeadowWingKicks"),
]


def add_cosmetic_shoes():
    """꾸미기 신발 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder in COSMETIC_SHOES:
        _add_item_row(item_id, name, "Cosmetic")
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "Shoes",
                              **_outfit_mesh(folder)})


# 스킨 조각 k (`docs/specs/skin-parts.md` 4장) — 꾸미기 장갑 6벌, 같은 두 세트(들꽃·항구). 효과 없는 겉모습,
# 장갑 칸에 `UEquipmentComponent::SetCosmetic` 으로 낀다 — 몸 스켈레톤 스켈레탈 레이어(`gloves_make.py`).
COSMETIC_GLOVES = [
    ("meadow_garden_gloves", "들꽃 정원 장갑", "Common", "MeadowGardenGloves"),
    ("meadow_mittens", "데이지 벙어리장갑", "Common", "MeadowMittens"),
    ("meadow_petal_gloves", "꽃잎 소매 장갑", "Rare", "MeadowPetalGloves"),
    ("harbor_fingerless", "항구 반장갑", "Common", "HarborFingerless"),
    ("harbor_rubber_gloves", "항구 고무장갑", "Rare", "HarborRubberGloves"),
    ("harbor_crab_gloves", "꽃게 집게 장갑", "Epic", "HarborCrabGloves"),
    # M87-04 (루프#886) — `gloves_make.py` 에 무늬·소매 길이만 다른 셋
    ("harbor_leather_gloves", "항구 가죽 장갑", "Common", "HarborLeatherGloves"),
    ("meadow_wool_gloves", "털실 장갑", "Common", "MeadowWoolGloves"),
    ("harbor_arm_sleeves", "항구 팔 토시", "Rare", "HarborArmSleeves"),
]


def add_cosmetic_gloves():
    """꾸미기 장갑 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder in COSMETIC_GLOVES:
        _add_item_row(item_id, name, "Cosmetic")
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "Gloves",
                              **_outfit_mesh(folder)})


# 스킨 조각 l (`docs/specs/skin-parts.md` 4장) — 꾸미기 배낭 6벌, 같은 두 세트(들꽃·항구). 효과 없는 겉모습,
# 배낭 칸에 `UEquipmentComponent::SetCosmetic` 으로 멘다 — 칸 효과(하이킹 배낭 +8칸)는 그대로, 등 메시만 바뀐다.
# 스태틱 메시·하이킹 배낭과 같은 `Socket_Back`·같은 피벗(`backpack_make.py`)이라 몸 배율을 따른다(`bEquipFollowsBodyScale`).
BACKPACK_DIR = "/Game/Game/Characters/Equipment/Backpacks"
COSMETIC_BACKPACKS = [
    ("meadow_daypack", "들꽃 데이팩", "Common", "MeadowDaypack"),
    ("meadow_basket_pack", "버들 바구니 배낭", "Common", "MeadowBasketPack"),
    ("meadow_frog_pack", "청개구리 배낭", "Rare", "MeadowFrogPack"),
    ("harbor_duffel", "줄무늬 더플 배낭", "Common", "HarborDuffel"),
    ("harbor_lifebuoy_pack", "구명환 배낭", "Rare", "HarborLifebuoyPack"),
    ("harbor_octopus_pack", "문어 배낭", "Epic", "HarborOctopusPack"),
    # M83-01 (루프#855) — `tools/blender/backpack_more_make.py`
    ("meadow_mushroom_pack", "버섯 배낭", "Common", "MeadowMushroomPack"),
    ("harbor_crate_pack", "선적 상자 배낭", "Common", "HarborCratePack"),
    ("meadow_beehive_pack", "꿀벌통 배낭", "Rare", "MeadowBeehivePack"),
    # M84-01 (루프#864)
    ("meadow_watermelon_pack", "수박 배낭", "Common", "MeadowWatermelonPack"),
    ("harbor_barrel_pack", "술통 배낭", "Common", "HarborBarrelPack"),
    ("harbor_whale_pack", "고래 배낭", "Rare", "HarborWhalePack"),
    # M88-01 (루프#889)
    ("meadow_strawberry_pack", "딸기 배낭", "Common", "MeadowStrawberryPack"),
    ("meadow_cat_pack", "고양이 배낭", "Common", "MeadowCatPack"),
    ("harbor_submarine_pack", "잠수함 배낭", "Rare", "HarborSubmarinePack"),
]


def add_cosmetic_backpacks():
    """꾸미기 배낭 행 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, grade, folder in COSMETIC_BACKPACKS:
        _add_item_row(item_id, name, "Cosmetic")
        mesh = "SM_Backpack_{}_01".format(folder)
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": grade, "CraftTag": "None", "EquipSlot": "Backpack",
                              "EquipMeshRef": "{0}/{1}/{2}.{2}".format(BACKPACK_DIR, folder, mesh),
                              "EquipSocketName": "Socket_Back", "bEquipFollowsBodyScale": True})


def add_paint_items():
    """페인트 행. 상점 물건이라 판매가 0 — extend_with_crafting 뒤에 불러 덮이지 않게 한다."""
    for item_id, name, color_hex in PAINT_ITEMS:
        _add_item_row(item_id, name, "Material")
        ITEM_ROWS[-1].update({"SellPrice": 0, "Grade": "Common", "CraftTag": "None", "PaintColorHex": color_hex})


def ensure_table(folder, asset_name, row_struct_path):
    """테이블을 가져온다. 없으면 행 구조체를 붙여 새로 만든다."""
    asset_path = "{}/{}".format(folder, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log("이미 있음: {}".format(asset_path))
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    row_struct = unreal.load_object(None, row_struct_path)
    if row_struct is None:
        raise RuntimeError("행 구조체를 못 찾았다: {} (모듈 빌드부터)".format(row_struct_path))

    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, folder, unreal.DataTable, factory)
    unreal.log("생성: {}".format(asset_path))
    return table


def fill_table(table, rows):
    """행을 JSON 으로 채운다 (배열 칸이 있어 CSV 보다 JSON 이 안전하다)."""
    unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
    row_names = [str(name) for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    unreal.log("행 {}개: {}".format(len(row_names), ", ".join(row_names)))
    unreal.EditorAssetLibrary.save_loaded_asset(table)


# 목장 생산 1행 (ROADMAP M2-02). 주기 30분·미수확 누적 상한 5 는 DESIGN 3.2 "확정 수치" 표 그대로다.
# ProducedItemID = item.truffle 은 **감독 결정**(2026-09-19, 루프#27 질문 답 — DESIGN 3.2 표에 적혀 있다).
# 이 스크립트가 고른 아이템이 아니다 (DataDriven.md 3).
# 소·양 2행은 감독 결정(2026-09-21, 루프#75 질문 답 — DESIGN 3.2 "소·양 사이클당 생산량").
# 주기 30분·상한 5개는 돼지 행과 **같은 값**이다 — 위 확정 수치 표에서 온 것이지 새로 만든 숫자가 아니다.
RANCH_ROWS = [
    {
        "Name": "Ranch_Pig",
        "AnimalID": "animal.pig",
        "ProducedItemID": "item.truffle",
        "CycleMinutes": 30,
        "MaxStack": 5,
    },
    {
        "Name": "Ranch_Cow",
        "AnimalID": "animal.cow",
        "ProducedItemID": "item.milk",
        "CycleMinutes": 30,
        "MaxStack": 5,
    },
    {
        "Name": "Ranch_Sheep",
        "AnimalID": "animal.sheep",
        "ProducedItemID": "item.wool",
        "CycleMinutes": 30,
        "MaxStack": 5,
    },
]


def _item_row_name(item_id):
    """`fox_meat` → `Item_FoxMeat`. AssetNaming 의 행 이름 규칙(파스칼)을 손으로 안 적으려고 만든다."""
    return "Item_" + "".join(part.capitalize() for part in item_id.split("_"))


def _add_item_row(item_id, display_name, item_type):
    """같은 아이템을 여러 종이 가리켜도(새 4종의 새고기·순록과 엘크의 낙각) 행은 하나만 선다."""
    full_id = "item.{}".format(item_id)
    for row in ITEM_ROWS:
        if row["ItemID"] == full_id:
            return full_id
    ITEM_ROWS.append({
        "Name": _item_row_name(item_id),
        "ItemID": full_id,
        "DisplayName": display_name,
        "ItemType": item_type,
    })
    return full_id


# 메인 스토리 장 끝 꾸미기 5종 (docs/STORY.md 5절 · 서버 STORY_CHAPTER_ITEMS). 팔 수 없는 보상.
# 🟡 모델·입는 칸은 화면 조각(스토리 ②~⑤)에서 붙인다 — 지금은 행만 서고 ItemType 은 자리표시(Material)다.
STORY_REWARD_ITEMS = [
    ("story_traveler_hat", "솔방울의 여행자 모자"), ("story_ranch_signpost", "목장 나무 팻말"),
    ("story_boatman_scarf", "뱃사공 목도리"), ("story_broken_harpoon", "부서진 작살 장식"),
    ("story_lighthouse_lamp", "등대 모양 등불"),
]


def add_story_reward_items():
    for item_id, name in STORY_REWARD_ITEMS:
        _add_item_row(item_id, name, "Material")


def extend_with_animal_species():
    """`ANIMAL_SPECIES` 한 표에서 동물 행 12개 · 아이템 행 · 목장 생산 행 12개를 만든다 (M5-07c).

    **숫자는 여기서 하나도 만들지 않는다** — 체력은 위 표의 종별 칸(DESIGN 3.1 「동물 체력」), 포획 임계·도주 속도·드롭 개수는 돼지 행에서,
    목장 주기·누적 상한은 `Ranch_Pig` 행에서 읽는다. DESIGN 3.1 이 셋을 "전 종 공통 (MVP)" 로,
    3.2 가 주기 30분·상한 5개를 전 종 공통으로 적어 뒀기 때문이다 (DataDriven.md 3).
    """
    pig_row = next(row for row in ANIMAL_ROWS if row["Name"] == "Animal_Pig")
    pig_ranch_row = next(row for row in RANCH_ROWS if row["Name"] == "Ranch_Pig")

    for species in ANIMAL_SPECIES:
        folder = species["Folder"]
        body = species.get("BodyFolder", folder)   # 이야기 종은 다른 종의 몸체를 쓴다
        story = species.get("Story", False)
        animal_id = "animal.{}".format(species["ID"])
        asset_dir = "/Game/Game/Characters/Animals/{}".format(body)

        drop_ids = [_add_item_row(item_id, name, "Material") for item_id, name in species["Drops"]]
        pet_id = _add_item_row("pet_{}".format(species["ID"]),
                               "{} (펫)".format(species["Name"]), "Pet")
        # M21-07 색 변종 펫 — 12종 × 1 (DESIGN 10.6 「변종 펫 아이템」 `item.pet_<종>_variant`).
        variant_pet_id = "" if story else _add_item_row("pet_{}_variant".format(species["ID"]),
                                                        "{} 변종 (펫)".format(species["Name"]), "Pet")
        produce_id = _add_item_row(species["Produce"][0], species["Produce"][1], "Material")

        ANIMAL_ROWS.append({
            "Name": "Animal_{}".format(folder),
            "AnimalID": animal_id,
            "DisplayName": species["Name"],
            "MaxHP": species["MaxHP"],   # DESIGN 3.1 「동물 체력」 종별 (M22)
            "CaptureThresholdPct": pig_row["CaptureThresholdPct"],
            "MoveSpeed": pig_row["MoveSpeed"],
            "DeathDropItems": drop_ids,
            "DeathDropCountMin": pig_row["DeathDropCountMin"],
            "DeathDropCountMax": pig_row["DeathDropCountMax"],
            "PristineDropChance": PRISTINE_DROP_CHANCE,
            "CorpseLifeSeconds": CORPSE_LIFE_SECONDS,
            "PetItemID": pet_id,
            "VariantPetItemID": variant_pet_id,
            "VariantChance": 0.0 if story else VARIANT_CHANCE,
            "bStorySpecies": story,
            # 12종이 곧 기본 플레이의 동물이다 — 자리표시 칸은 끈다 (위 돼지·소·양과 갈리는 자리).
            "bPlaceholderSpecies": False,
            "HabitatZoneID": species["Zone"],
            # 메시는 **가져올 때** DESIGN 8.7 "크기" 표 배율로 이미 구워져 있다 — 0 = 게임에서 또 굽지 않는다.
            "MeshFitHeightCm": 0.0,
            "MeshRef": "{0}/SK_Animal_{1}.SK_Animal_{1}".format(asset_dir, body),
            "IdleAnimRef": "{0}/A_Animal_{1}_Idle.A_Animal_{1}_Idle".format(asset_dir, body),
            "DeathAnimRef": "{0}/A_Animal_{1}_Death.A_Animal_{1}_Death".format(asset_dir, body),
            # M5-07d 클립 칸 넷. 도주 = 달리기 클립(`_Run`)이다 — 동물이 움직이는 길은 도주뿐이고
            # 새 4종은 그 한 클립이 걷기까지 겸한다(DESIGN 8.7). 없는 클립은 빈 칸으로 남는다.
            "WalkAnimRef": _clip_ref(asset_dir, "Animal_{}".format(body), "Walk"),
            "FleeAnimRef": _clip_ref(asset_dir, "Animal_{}".format(body), "Run"),
            "AttackAnimRef": _clip_ref(asset_dir, "Animal_{}".format(body), "Attack"),
            "HitAnimRef": _clip_ref(asset_dir, "Animal_{}".format(body), "Hit"),
            # M5-07d 공격 숫자 칸. 종마다 다른 것은 데미지·행동 둘뿐이고 나머지는 전 종 공통이다.
            "BehaviorKind": species["Behavior"],
            "AttackDamage": species["Damage"],
            "AttackIntervalSec": ATTACK_INTERVAL_SEC,
            "AttackRangeCm": ATTACK_RANGE_CM,
            "AggroRadiusCm": AGGRO_RADIUS_CM if species["Behavior"] == AGGRO_BEHAVIOR else 0.0,
        })

        RANCH_ROWS.append({
            "Name": "Ranch_{}".format(folder),
            "AnimalID": animal_id,
            "ProducedItemID": produce_id,
            "CycleMinutes": pig_ranch_row["CycleMinutes"],
            "MaxStack": pig_ranch_row["MaxStack"],
        })


def fill_placeholder_attack_columns():
    """자리표시 3종(돼지·소·양)의 M5-07d 칸을 채운다.

    그 셋은 DESIGN 3.1 "동물 공격" 표에도 8.7 행동 열에도 줄이 없다 — 교체 대상이라 설계서가
    다루지 않는다. 그래서 **가장 순한 쪽**으로 채운다: 행동 `Flee` · 1타 데미지 0(한 번도 안 친다)
    · 선공 반경 0. 간격·사거리는 전 종 공통 값 그대로다. 칸을 비워 두지 않는 이유는 `-1`(미정)이
    행에 남으면 무결성 검사가 "안 채운 칸" 으로 세기 때문이다 (Verification.md 7).
    클립 칸은 반입된 에셋에 물어 채운다 — 소만 Walk/Run 이 있고 돼지·양에는 없다(Quaternius 원본).
    """
    for row in ANIMAL_ROWS:
        if not row.get("bPlaceholderSpecies"):
            continue
        body = row["Name"]
        asset_dir = "/Game/Game/Characters/Animals/{}".format(body.split("_", 1)[1])
        row.update({
            "WalkAnimRef": _clip_ref(asset_dir, body, "Walk"),
            "FleeAnimRef": _clip_ref(asset_dir, body, "Run"),
            "AttackAnimRef": _clip_ref(asset_dir, body, "Attack"),
            "HitAnimRef": _clip_ref(asset_dir, body, "Hit"),
            "BehaviorKind": "Flee",
            "AttackDamage": 0.0,
            "AttackIntervalSec": ATTACK_INTERVAL_SEC,
            "AttackRangeCm": ATTACK_RANGE_CM,
            "AggroRadiusCm": 0.0,
            # M21-07 변종은 바르코 12종만(DESIGN 10.6 「변종 대상」) — 자리표시는 0 = 변종 없음, 펫 칸은 빈 채.
            "VariantChance": 0.0,
        })


# 행 ID 와 아이콘 이름이 갈린 것 (M13-23) — 손도끼 행은 서버 카탈로그 ID `item.hatchet` 을 지키고(#0207),
# 아이콘은 원본 파일 이름 그대로 `T_Icon_HandAxe` 다. 행 ID 는 안 바꾼다.
ICON_NAME_EXCEPTIONS = {"hatchet": "HandAxe"}


def fill_item_icons():
    """아이템 행의 `IconRef` 를 **아이콘 에셋이 실제로 있을 때만** 채운다 (M5-08).

    위 `_clip_ref` 와 같은 짜임이다 — 아이콘 목록을 여기 손으로 또 적지 않고 반입된 에셋에
    직접 물어본다(`import_item_icons.py` 와 갈릴 일이 없다, DataDriven.md 1). 파일이 없는 행은
    **비워 둔다** — 자리표시 돼지·소·양 계열과 펫 행이 그렇고, 그건 교체 대상이라 비어 있는 것이
    맞다(감독 지시 2026-09-23 15:10). 빈 칸은 데이터 무결성 검사가 경고로만 센다.

    이름 규칙은 임포터와 같다: `item.fox_meat` → `T_Icon_FoxMeat`. 예외는 `ICON_NAME_EXCEPTIONS` 한 곳
    (C++ 검사 `SnarehavenItemIconTest.cpp` 에 같은 표).
    """
    for row in ITEM_ROWS:
        item_id = row["ItemID"].split(".", 1)[1]
        asset_name = "T_Icon_" + ICON_NAME_EXCEPTIONS.get(
            item_id, "".join(part.capitalize() for part in item_id.split("_")))
        asset_path = "{}/{}".format(ICON_FOLDER, asset_name)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            row["IconRef"] = "{0}.{1}".format(asset_path, asset_name)


def fill_pet_neck_widths():
    """동물 행 `PetNeckWidthCm`·`PetNeckCenterCm` — 목장 펫 목걸이 고리를 벌릴 목 굵기와 둘 자리(스킨 조각 n·M40-02, DESIGN 10.9).

    **숫자를 짓지 않고 메시에서 잰다** — `ARanchPetActor::MeasureNeckWidthCm`(기준 자세 목 뼈 자리 단면).
    정점은 에디터에만 있어 게임이 못 재니 여기서 재 행에 적는다. 목 뼈가 없는 몸은 0(목걸이도 키 비율로).
    2층 `Snarehaven.Ranch.Cosmetic.Pet.RibbonNecklaceFit` 이 행 값 = 지금 잰 값을 맞댄다.
    """
    for row in ANIMAL_ROWS:
        mesh = unreal.load_asset(row["MeshRef"].split(".")[0])
        row["PetNeckWidthCm"] = round(unreal.RanchPetActor.measure_neck_width_cm(mesh), 1) if mesh else 0.0
        # 메시가 없으면(에디터 밖 `dump_item_rows` 스텁 포함) 가운데도 0 — 스텁 값에 round 를 부르면 죽는다.
        center = unreal.RanchPetActor.measure_neck_center_cm(mesh) if mesh else None
        row["PetNeckCenterCm"] = ({"X": round(center.x, 1), "Y": round(center.y, 1), "Z": round(center.z, 1)}
                                  if center is not None else {"X": 0.0, "Y": 0.0, "Z": 0.0})
        unreal.log_warning("목 굵기: {} {}cm 가운데 {}".format(row["Name"], row["PetNeckWidthCm"], row["PetNeckCenterCm"]))


# M43-02 동물 소리 묶음 — DESIGN 8.7 「동물 소리」 표가 원본이고 여기는 사본이다.
# 종 이름 `if` 대신 **이 표 한 줄**이 종 → 묶음을 정한다 (DataDriven.md 1). 소리 파일 이름은 `tools/audio/make_sounds.py` `ANIMAL_SOUNDS`.
ANIMAL_SOUND_GROUPS = {
    "Bird": ["animal.pheasant", "animal.peacock", "animal.ptarmigan", "animal.capercaillie"],
    "Deer": ["animal.reindeer", "animal.elk", "animal.goat", "animal.fogbuck"],
    "Predator": ["animal.fox", "animal.lynx", "animal.wolf", "animal.wolverine", "animal.bear"],
    "Livestock": ["animal.pig", "animal.cow", "animal.sheep"],
}
# 덤빔 소리가 있는 묶음 — 새·가축은 전부 `Flee` 라 만들지 않았다(DESIGN 8.7 「때」 줄).
ANIMAL_SOUND_AGGRO_GROUPS = {"Deer", "Predator"}
# 높낮이 — 곰만 0.75(늑대 86cm ÷ 곰 115cm, DESIGN 8.7 「높낮이」 줄), 나머지 1.0.
ANIMAL_SOUND_PITCH = {"animal.bear": 0.75}


def fill_animal_sound_columns():
    """동물 행 소리 칸 넷(`HitSoundCue`·`DeathSoundCue`·`AggroSoundCue`·`SoundPitch`)을 묶음 표로 채운다 (M43-02).

    덤빔 칸은 행동이 `Flee` 가 아닌 종만 — 덤비지 않는 동물이 으르렁대면 행동과 소리가 어긋난다.
    표에 없는 종이면 멈춘다(빈 칸으로 조용히 무음이 되는 일을 막는다). 2층 `Snarehaven.Animal.Sound.TableColumns`.
    """
    group_of = {animal_id: group for group, ids in ANIMAL_SOUND_GROUPS.items() for animal_id in ids}
    for row in ANIMAL_ROWS:
        group = group_of.get(row["AnimalID"])
        if group is None:
            raise SystemExit("동물 소리 묶음 표에 없는 종: {}".format(row["AnimalID"]))
        aggro = group in ANIMAL_SOUND_AGGRO_GROUPS and row.get("BehaviorKind", "Flee") != "Flee"
        row.update({
            "HitSoundCue": "Animal{}Hit".format(group),
            "DeathSoundCue": "Animal{}Death".format(group),
            "AggroSoundCue": "Animal{}Aggro".format(group) if aggro else "",
            "SoundPitch": ANIMAL_SOUND_PITCH.get(row["AnimalID"], 1.0),
        })


extend_with_animal_species()
fill_placeholder_attack_columns()
fill_animal_sound_columns()
fill_item_icons()
fill_pet_neck_widths()


# 거점 작물 27행 (ROADMAP M6-05a 3행 + M13-01 24행). **이 스크립트가 만든 숫자는 0개다** — 앞 셋은
# DESIGN 8.2 "거점 작물 확정 수치" 표, 나머지 24는 13.3 작물 표 그대로이고, 2층
# `Snarehaven.Crop.Table.MatchesDesign` 이 두 표를 읽어 여기와 맞댄다 — 한쪽만 고치면 운다.
# MaxStack 칸은 없다 — ⑩ 부터 작물은 한 번 거두면 칸이 빈다(원본 레퍼런스, DESIGN 8.2 심기·수확 표).
#
# 행 이름은 `Crop_<씨앗 이름>`, 정체성은 `ProducedItemID` 다 — `crop.*` 라는 새 ID 무리를 만들지 않는다
# (`CropProductRow.h` 주석 · 도감이 농사 칸을 `item.carrot` 으로 센다, DESIGN 8.5). 씨앗 ID 는 표 칸
# `SeedItemID` 가 든다 — 차·커피·참나무는 수확 아이템과 이름이 달라 지어낼 수 없다(13.3).
#
# (수확 아이템, 씨앗, 재배 분, 수확 코인, 작물 이름, 수확 아이템 이름). 코인 = 재배 분 × 0.33 (백엔드가 지급,
# 여기서는 판매가 계산에만 쓴다). 수확 아이템 이름은 🟡 — 13.3 에는 작물 이름만 있다(찻잎·커피콩·사과만 다르게).
CROPS = [
    ("carrot", "seed_carrot", 15, 5, "당근", "당근"),
    ("potato", "seed_potato", 30, 10, "감자", "감자"),
    ("wheat", "seed_wheat", 60, 20, "밀", "밀"),
    ("berry", "seed_berry", 15, 5, "산딸기", "산딸기"),
    ("blueberry", "seed_blueberry", 15, 5, "블루베리", "블루베리"),
    ("lingonberry", "seed_lingonberry", 15, 5, "링곤베리", "링곤베리"),
    ("blackberry", "seed_blackberry", 15, 5, "블랙베리", "블랙베리"),
    ("mezereon", "seed_mezereon", 30, 10, "서향", "서향 열매"),
    ("sugar_beet", "seed_sugar_beet", 30, 10, "사탕무", "사탕무"),
    ("onion", "seed_onion", 30, 10, "양파", "양파"),
    ("cucumber", "seed_cucumber", 30, 10, "오이", "오이"),
    ("chili", "seed_chili", 30, 10, "고추", "고추"),
    ("cabbage", "seed_cabbage", 30, 10, "양배추", "양배추"),
    ("kingcup", "seed_kingcup", 30, 10, "메리골드", "메리골드"),
    ("windflower", "seed_windflower", 30, 10, "아네모네", "아네모네"),
    ("hepatica", "seed_hepatica", 30, 10, "노루귀", "노루귀"),
    ("poppy", "seed_poppy", 30, 10, "양귀비", "양귀비"),
    ("fireweed", "seed_fireweed", 30, 10, "분홍바늘꽃", "분홍바늘꽃"),
    ("pumpkin", "seed_pumpkin", 60, 20, "호박", "호박"),
    ("cotton", "seed_cotton", 60, 20, "목화", "목화"),
    ("canola", "seed_canola", 60, 20, "카놀라", "카놀라"),
    ("tea_leaves", "seed_tea", 60, 20, "차", "찻잎"),
    ("coffee_beans", "seed_coffee", 60, 20, "커피", "커피콩"),
    ("wood", "seed_oak", 60, 20, "참나무", "나뭇가지"),
    ("cloudberry", "seed_cloudberry", 120, 40, "클라우드베리", "클라우드베리"),
    ("black_lotus", "seed_black_lotus", 120, 40, "검은 연꽃", "검은 연꽃"),
    ("apple", "seed_apple", 120, 40, "사과나무", "사과"),
]
CROP_ROWS = [{
    "Name": "Crop_" + _item_row_name(seed[len("seed_"):])[len("Item_"):],
    "ProducedItemID": "item." + item,
    "SeedItemID": "item." + seed,
    "CycleMinutes": minutes,
} for item, seed, minutes, _, _, _ in CROPS]


# 낚시터 어종 21행 (ROADMAP M10-01 · 사용자 지시 2026-09-23 으로 3행 → 21행). **이 스크립트가 만든 숫자는
# 0개다** — 15·21·27 … 180분은 전부 DESIGN 8.4 "낚시(어업) MVP 확정 수치" 표이고, 2층
# `Snarehaven.Fishing.Table.MatchesDesign` 이 그 표를 읽어 여기와 맞댄다 — 한쪽만 고치면 운다.
# MaxStack 5 도 새 숫자가 아니다: DESIGN 8.4 마지막 줄이 "목장·작물과 동일(3.2)" 이라 위 RANCH_ROWS
# 의 값과 같고, 검사도 목장 표에서 읽어 비교한다. 21행이 같은 상한을 쓰는 것도 그 한 줄에서 온다.
#
# 행 이름은 `Fishing_<어종>`, 정체성은 `ProducedItemID` 다 — `fish.*` 라는 새 ID 무리를 만들지 않는다
# (`FishingRow.h` 주석 · 도감이 낚시 칸을 아이템 ID 로 센다, DESIGN 8.5).
FISHING_ROWS = [
    {
        "Name": "Fishing_CrucianCarp",
        "ProducedItemID": "item.crucian_carp",
        "CycleMinutes": 15,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Salmon",
        "ProducedItemID": "item.salmon",
        "CycleMinutes": 21,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_CaveCrab",
        "ProducedItemID": "item.cave_crab",
        "CycleMinutes": 27,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Carp",
        "ProducedItemID": "item.carp",
        "CycleMinutes": 30,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Perch",
        "ProducedItemID": "item.perch",
        "CycleMinutes": 30,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Pike",
        "ProducedItemID": "item.pike",
        "CycleMinutes": 51,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Whitefish",
        "ProducedItemID": "item.whitefish",
        "CycleMinutes": 60,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Haddock",
        "ProducedItemID": "item.haddock",
        "CycleMinutes": 60,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Cod",
        "ProducedItemID": "item.cod",
        "CycleMinutes": 60,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Catfish",
        "ProducedItemID": "item.catfish",
        "CycleMinutes": 60,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Trout",
        "ProducedItemID": "item.trout",
        "CycleMinutes": 69,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Tuna",
        "ProducedItemID": "item.tuna",
        "CycleMinutes": 69,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Stingray",
        "ProducedItemID": "item.stingray",
        "CycleMinutes": 99,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_RainbowTrout",
        "ProducedItemID": "item.rainbow_trout",
        "CycleMinutes": 120,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Redfish",
        "ProducedItemID": "item.redfish",
        "CycleMinutes": 120,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_BlackGrouper",
        "ProducedItemID": "item.black_grouper",
        "CycleMinutes": 120,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_ArcticChar",
        "ProducedItemID": "item.arctic_char",
        "CycleMinutes": 141,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Swordfish",
        "ProducedItemID": "item.swordfish",
        "CycleMinutes": 141,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_MistyGrouper",
        "ProducedItemID": "item.misty_grouper",
        "CycleMinutes": 159,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_SalmonShark",
        "ProducedItemID": "item.salmon_shark",
        "CycleMinutes": 180,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Axolotl",
        "ProducedItemID": "item.axolotl",
        "CycleMinutes": 180,
        "MaxStack": 5,
    },
    {
        "Name": "Fishing_Eel",
        "ProducedItemID": "item.eel",
        "CycleMinutes": 120,
        "MaxStack": 5,
    },
]


# ── 제작·요리·건축·배낭·직접 낚시·상점 (DESIGN 8.9~8.14, 레퍼런스 원본 맞춤 2026-09-24) ──
# 아이템 ID·숫자는 전부 그 절의 표다. 표에 ID·값이 없어 가까운 규칙으로 채운 것은 `# 🟡` 를 붙였다.
# 2층 `Snarehaven.Crafting.*` · `Snarehaven.Shop.*` 가 DESIGN 을 읽어 여기와 맞댄다.

# DESIGN 8.13 "서식지" — 민물 12, 나머지 9 는 바다. 먼바다 = 바다 중 낚시 시간 120분 이상.
FRESHWATER_FISH = {
    "crucian_carp", "carp", "salmon", "perch", "pike", "whitefish", "catfish", "trout",
    "rainbow_trout", "arctic_char", "cave_crab", "axolotl",
}
DEEP_SEA_MIN_MINUTES = 120
SWAMP_FISH = {"eel"}    # 8.13 "늪 1" — 늪 물웅덩이에서만

# DESIGN 8.4 "레퍼런스 조리 회복량" 열 = 생선구이 회복량(8.10). 붕어·잉어는 열이 `—` 라 8.10 이 10 으로 정했다.
# 우파루파 구이는 8.4 열이 아니라 13.7 요리 표에서 온다 — 아래 `COOKING_DISHES`.
GRILLED_FISH_HEAL = {
    "crucian_carp": 10, "carp": 10, "salmon": 10, "cave_crab": 13, "perch": 15, "pike": 25,
    "whitefish": 30, "haddock": 30, "cod": 30, "catfish": 30, "trout": 35, "tuna": 35,
    "stingray": 50, "rainbow_trout": 60, "redfish": 60, "black_grouper": 60,
    "arctic_char": 70, "swordfish": 70, "misty_grouper": 80, "salmon_shark": 90,
    "eel": 60,
}

# DESIGN 8.2·13.3 작물 수확 코인. 코인은 어느 표에도 없어(백엔드 몫) 판매가 계산에만 쓴다.
CROP_HARVEST_COINS = {item: coins for item, _, _, coins, _, _ in CROPS}

# M9-04·M13-36 도구 메시 (DESIGN 3.5 "도구 크기" — 바르코, `import_equipment_weapons.py` 가 가장 긴 변을 표 값으로 들인다). 왼손 `Socket_Tool`.
# 감독 09-25: 휘둘러 베는 도구(손도끼·곡괭이·마체테·전기톱)는 베기·내리찍기 클립이 오른손으로 휘두르니 무기와 같은 오른손
# `Socket_Weapon` 에 든다 — 그동안 무기는 숨는다(`UEquipmentComponent::UpdateHandVisibility`). 바구니 등은 왼손 그대로.
# 2026-10-05 낚싯대도 오른손 `Socket_Weapon`(아래 ROD_MESH — 레퍼런스 UlFyyrxMNt8 1:50).
TOOL_DIR = "/Game/Game/Characters/Equipment/Tools"


SWING_TOOLS = ("HandAxe", "Chainsaw", "Machete", "PickaxeBasic", "PickaxeAdvanced", "PickaxeElectric")


PLAYER_ANIM_DIR = "/Game/Game/Characters/Player/Snare"


def _tool_mesh(name):
    socket = "Socket_Weapon" if name in SWING_TOOLS else "Socket_Tool"
    return {"EquipMeshRef": "{0}/{1}/SM_Tool_{1}_01.SM_Tool_{1}_01".format(TOOL_DIR, name), "EquipSocketName": socket}


# 옷 = 몸 스켈레톤 스켈레탈 메시(`import_outfits.py`, 결정 006). 행 안 글자라 아이콘 채우기 순서와 무관.
OUTFIT_DIR = "/Game/Game/Characters/Equipment/Outfits"


def _outfit_mesh(name):
    return {"EquipOutfitMeshRef": "{0}/{1}/SK_Outfit_{1}_01.SK_Outfit_{1}_01".format(OUTFIT_DIR, name)}


HAND_AXE_MESH = _tool_mesh("HandAxe")
# M13-43 낚싯대 110·배낭 35 는 몸 리그(약 100cm) 기준 크기라 몸 배율을 따른다(DESIGN 3.5 「도구 크기」).
# 2026-10-05 감독 지시 — 레퍼런스는 낚싯대를 오른손으로 쥔다(영상 UlFyyrxMNt8 1:50, 관찰표 19행). 오른손 `Socket_Weapon` + 쥐는 각:
# 메시 반입 회전(`import_equipment_weapons.py` ROD_ROTATION)은 왼손 Socket_Tool 기준이라, 옛 왼손 쥔 모양의 거울 자리를 오른손 소켓에서
# 만드는 각 Q = R오른⁻¹·S·R왼·H(참조 자세 소켓 회전·폰 좌우 거울·메시 대칭면 거울)를 2층 RealMeshes 가 찍은 값으로 넣는다.
# 낚시 클립 4개(`fish_pose_make.py`·`fish_cast_make.py`)도 같은 거울 자리로 오른손 IK 다시 만듦. 위치 0 = 메시 피벗(손잡이)이 손목.
ROD_GRIP = {"GripOffset": "(Location=(X=0,Y=0,Z=0),Rotation=(Pitch=22.09,Yaw=118.94,Roll=33.97))"}
ROD_MESH = {"bEquipFollowsBodyScale": True, "EquipSocketName": "Socket_Weapon", **ROD_GRIP}
# 낚싯대 4종 든 자세 — 레퍼런스 「한 손으로 손잡이 끝을 허리 옆에 쥐고 대를 세워 뒤쪽 위로」(관찰표 19행, 루프#390).
# 쓰기 클립(UseAnim)은 비운다 — 던지기·기다리기·감기는 낚시 상태가 고르고, 몽둥이를 같이 들면 휘두르기는 몽둥이 것.
ROD_HOLD = {"HoldPoseAnim": PLAYER_ANIM_DIR + "/A_Player_HoldRod.A_Player_HoldRod"}
HIKING_BACKPACK_MESH = {"EquipMeshRef": "/Game/Game/Characters/Equipment/Backpacks/HikingBackpack/"
                        "SM_Backpack_HikingBackpack_01.SM_Backpack_HikingBackpack_01",
                        "EquipSocketName": "Socket_Back", "bEquipFollowsBodyScale": True}
BASKET_MESH = _tool_mesh("Basket")


# M18-11 도구마다 든 자세·쓰기 클립 (DESIGN 3.1 「무기별 몸 동작」). 낚싯대는 든 자세만(ROD_HOLD).
# 감독 10-05: 바구니·망원경은 든 자세 = 대기(A_Player_Idle — 한 손 옆에 늘어뜨림), 쓰기 = 줍기(A_Player_Gather)·보기(A_Player_LookBinoculars).
# 무기를 함께 들면 휘두르기·든 자세는 무기 것이다(왼손 도구라 손이 다르다 — `ASnarehavenCharacter::ResolveHeldBodyClips`).
def _grip(y=0.0, z=0.0, roll=0.0, yaw=0.0):
    """M18-11 쥐는 자리 (DESIGN 3.1 「쥐는 자리」) — 손목에 올 메시 로컬 점(cm)과 긴 축을 손가락 쪽에 두는 각.
    yaw = 긴 축(Z) 둘레로 날 방향 돌리기. 손도끼 메시는 날이 -X 라 180 이어야 내려찍는 앞쪽으로 선다(사람 10-15 「반대로 들고있냐」)."""
    return {"GripOffset": "(Location=(X=0,Y={},Z={}),Rotation=(Pitch=0,Yaw={},Roll={}))".format(y, z, yaw, roll)}


# 「정해진 길이」 칸(bUseAnimFixedLength) — 전기톱·고급/전기 곡괭이는 한 번 누르면 쓰기 클립 길이(1.5초)만큼 내밀어 떨다 돌아온다
# (INBOX 15:30 ⓪ 사용자 2026-10-04, DESIGN 3.1 「고정 길이 도구 쓰기」).
def _body_anims(hold, use):
    return {"HoldPoseAnim": "{0}/A_Player_{1}.A_Player_{1}".format(PLAYER_ANIM_DIR, hold),
            "UseAnim": "{0}/A_Player_{1}.A_Player_{1}".format(PLAYER_ANIM_DIR, use)}

POWER_DIR = "/Game/Game/Environment/Props/Power"   # 전력 가구 메시(`import_power_meshes.py`)
WALL_DECOR_DIR = "/Game/Game/Environment/Props/WallDecor"   # 벽 장식 메시(`import_wall_decor_meshes.py`, M108-02)
TABLE_DECOR_DIR = "/Game/Game/Environment/Props/TableDecor"   # 위에 올리는 소품 메시(`import_top_decor_meshes.py`, M108-03)
HOME_FURN_DIR = "/Game/Game/Environment/Furniture/Home"       # 집 가구 메시(`import_home_furniture_meshes.py`, M108-04)

# 새 아이템 행. (id, 이름, 종류, 그 밖의 칸). 판매가·태그·등급은 아래 fill_economy_columns 가 규칙으로 채운다.
CRAFTING_ITEMS = [
    # 8.10 재료·가공품
    ("cedar", "향나무", "Material", {"CraftTag": "Cedar"}),
    ("plank", "나무판", "Material", {}),
    ("cedar_plank", "향나무판", "Material", {}),
    ("berry_bait", "산딸기 미끼 팩", "Material", {}),
    ("worm_bait", "지렁이 팩", "Material", {}),                                     # 🟡 ID
    # 8.10·8.11 배치형 — 칸 수는 설계서에 없다(땅 16×16 칸만 있다) → 🟡 가구 크기로 감독 가정
    ("bench", "벤치", "Placeable", {"FootprintX": 2, "FootprintY": 1}),
    ("chair_wood", "나무 의자", "Placeable", {"FootprintX": 1, "FootprintY": 1}),
    ("table_wood", "나무 탁자", "Placeable", {"FootprintX": 2, "FootprintY": 1, "TopSlots": 3}),   # 8.11 위에 올리기 슬롯 3
    ("fence_wood", "나무 울타리", "Placeable", {"FootprintX": 1, "FootprintY": 1}),
    ("storage_box", "보관함", "Placeable",
     {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Storage", "BonusSlots": 20, "TopSlots": 2}),   # 8.12 보관함 20칸 · 8.11 위 슬롯 2
    ("campfire", "모닥불", "Placeable",
     {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Campfire", "CookSeconds": 20}),  # 8.10 요리 시간
    ("garden_brazier", "정원 화로", "Placeable",                                     # 🟡 ID · 기능 Campfire(불 옆 회복이 같다)
     {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Campfire", "CookSeconds": 12}),
    ("grill", "그릴", "Placeable",
     {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Grill", "CookSeconds": 6}),
    ("workbench_adv", "고급 작업대", "Placeable",
     {"FootprintX": 2, "FootprintY": 1, "PlaceableFunction": "Workbench"}),
    ("tent", "텐트", "Placeable", {"FootprintX": 3, "FootprintY": 3, "PlaceableFunction": "Tent"}),  # 🟡 ID·칸
    # M108-02 벽 장식 — 집 벽 칸 변에 걸린다(서버 `PlaceStructure` 의 `wall`, 기능 `WallDecor` 가 클라 놓기를 벽으로 돌린다). 메시 = `make_wall_decor_meshes.py` →
    # `import_wall_decor_meshes.py`, 폭은 DESIGN 8.11 크기 표(그린 치수의 1.5배 — 감독 보정 10-20 08:50)
    ("picture_frame", "액자", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_PictureFrame_01.SM_Prop_PictureFrame_01", "PlacedMeshWidthCm": 60.0}),
    ("wall_clock", "벽시계", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_WallClock_01.SM_Prop_WallClock_01", "PlacedMeshWidthCm": 51.0}),
    ("wall_shelf", "벽 선반", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_WallShelf_01.SM_Prop_WallShelf_01", "PlacedMeshWidthCm": 60.0}),
    # M108-05c 벽 장식 넷 — 같은 벽 걸기 길·같은 1.5배(폭 = 그린 가로 × 1.5, DESIGN 8.11 크기 표)
    ("wall_mirror", "아치 거울", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_WallMirror_01.SM_Prop_WallMirror_01", "PlacedMeshWidthCm": 45.0}),
    ("wall_map", "섬 지도", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_WallMap_01.SM_Prop_WallMap_01", "PlacedMeshWidthCm": 60.0}),
    ("wall_anchor", "벽걸이 닻", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_WallAnchor_01.SM_Prop_WallAnchor_01", "PlacedMeshWidthCm": 48.0}),
    ("wall_wreath", "들꽃 화환", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor",
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_WallWreath_01.SM_Prop_WallWreath_01", "PlacedMeshWidthCm": 54.0}),
    # M108-03 위에 올리는 소품 — 탁자·보관함 위 슬롯에만 놓인다(서버 `PlaceStructure` 의 `top`). 가로 40 이하. 메시 = `make_top_decor_meshes.py` →
    # `import_top_decor_meshes.py`, 폭은 DESIGN 8.11 크기 표(그린 치수의 1.5배 — 감독 보정 10-20 09:40). 기능 `TopDecor` 가 클라 놓기를 받침 슬롯으로 돌린다
    ("mug", "머그잔", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "TopDecor",
     "PlacedMeshRef": TABLE_DECOR_DIR + "/SM_Prop_Mug_01.SM_Prop_Mug_01", "PlacedMeshWidthCm": 30.0}),
    ("flower_pot", "작은 화분", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "TopDecor",
     "PlacedMeshRef": TABLE_DECOR_DIR + "/SM_Prop_FlowerPot_01.SM_Prop_FlowerPot_01", "PlacedMeshWidthCm": 33.0}),
    ("candle", "촛대", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "TopDecor",
     "PlacedMeshRef": TABLE_DECOR_DIR + "/SM_Prop_Candle_01.SM_Prop_Candle_01", "PlacedMeshWidthCm": 34.0}),
    # M108-04 집 가구 — 꾸미기만(기능 없음). 메시 = `make_home_furniture_meshes.py` → `import_home_furniture_meshes.py`, 폭·칸은 DESIGN 8.11
    # 「가구·상자 진짜 메시 크기」 표(그린 치수 그대로), 레시피는 8.10 표. `TopSlots` = 8.11 「받침 슬롯 늘림」(폭 ÷ 40 내림, 상한 3 — 서버 `TOP_HOSTS`)
    ("armchair", "안락의자", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_ArmChair_01.SM_Furn_ArmChair_01", "PlacedMeshWidthCm": 80.0}),
    ("sofa", "소파", "Placeable", {"FootprintX": 2, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Sofa_01.SM_Furn_Sofa_01", "PlacedMeshWidthCm": 170.0}),
    ("table_round", "둥근 탁자", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_TableRound_01.SM_Furn_TableRound_01", "PlacedMeshWidthCm": 90.0,
     "TopSlots": 2}),
    ("table_long", "긴 식탁", "Placeable", {"FootprintX": 2, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_TableLong_01.SM_Furn_TableLong_01", "PlacedMeshWidthCm": 190.0,
     "TopSlots": 3}),
    ("bed_single", "1인 침대", "Placeable", {"FootprintX": 2, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_BedSingle_01.SM_Furn_BedSingle_01", "PlacedMeshWidthCm": 195.0}),
    ("bed_double", "2인 침대", "Placeable", {"FootprintX": 2, "FootprintY": 2,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_BedDouble_01.SM_Furn_BedDouble_01", "PlacedMeshWidthCm": 195.0}),
    ("bookshelf", "책장", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Bookshelf_01.SM_Furn_Bookshelf_01", "PlacedMeshWidthCm": 92.0}),
    ("dresser", "서랍장", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Dresser_01.SM_Furn_Dresser_01", "PlacedMeshWidthCm": 93.0,
     "TopSlots": 2}),
    ("kitchen_counter", "조리대", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_KitchenCounter_01.SM_Furn_KitchenCounter_01", "PlacedMeshWidthCm": 97.0,
     "TopSlots": 2}),
    ("kitchen_sink", "개수대", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_KitchenSink_01.SM_Furn_KitchenSink_01", "PlacedMeshWidthCm": 97.0}),
    # M108-05a 식물 넷 — 꾸미기만. 같은 메시 길, 폭은 8.11 크기 표
    ("potted_tree", "화분 나무", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_PottedTree_01.SM_Furn_PottedTree_01", "PlacedMeshWidthCm": 60.0}),
    ("potted_berry", "열매 덤불 화분", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_PottedBerry_01.SM_Furn_PottedBerry_01", "PlacedMeshWidthCm": 68.0}),
    ("flower_box", "꽃 상자", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_FlowerBox_01.SM_Furn_FlowerBox_01", "PlacedMeshWidthCm": 96.0}),
    ("potted_sprout", "새싹 큰 화분", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_PottedSprout_01.SM_Furn_PottedSprout_01", "PlacedMeshWidthCm": 74.0}),
    # M108-05b 러그 넷 — 바닥 가구와 같은 칸에 깔린다(`Rug`, 8.11 「러그 깔기」). 긴 변 = FootprintX
    ("rug_round", "둥근 러그", "Placeable", {"FootprintX": 2, "FootprintY": 2, "PlaceableFunction": "Rug",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_RugRound_01.SM_Furn_RugRound_01", "PlacedMeshWidthCm": 180.0}),
    ("rug_runner", "긴 복도 러그", "Placeable", {"FootprintX": 3, "FootprintY": 1, "PlaceableFunction": "Rug",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_RugRunner_01.SM_Furn_RugRunner_01", "PlacedMeshWidthCm": 270.0}),
    ("rug_square", "네모 무늬 러그", "Placeable", {"FootprintX": 3, "FootprintY": 2, "PlaceableFunction": "Rug",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_RugSquare_01.SM_Furn_RugSquare_01", "PlacedMeshWidthCm": 280.0}),
    ("doormat", "문 앞 깔개", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Rug",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Doormat_01.SM_Furn_Doormat_01", "PlacedMeshWidthCm": 90.0}),
    # M108-05d 바깥 넷 — 마당 꾸미기. 디딤 길은 러그 층(아치·가구 밑에 깔림), 나머지는 바닥 가구. 폭은 8.11 크기 표
    ("fence_picket", "말뚝 울타리", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_FencePicket_01.SM_Furn_FencePicket_01", "PlacedMeshWidthCm": 100.0}),
    ("path_log", "통나무 디딤 길", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Rug",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_PathLog_01.SM_Furn_PathLog_01", "PlacedMeshWidthCm": 96.0}),
    ("garden_arch", "꽃 아치", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_GardenArch_01.SM_Furn_GardenArch_01", "PlacedMeshWidthCm": 100.0}),
    ("mailbox", "우편함", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Mailbox_01.SM_Furn_Mailbox_01", "PlacedMeshWidthCm": 48.0}),
    # M108-05e 조명 넷 — 비전기 불빛(`FlameLight`): 밤이면 전력 없이 켜짐, 땅 하나 점광 상한 안에서 (8.11 「비전기 조명」)
    ("candle_stand", "세움 촛대", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "FlameLight",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_CandleStand_01.SM_Furn_CandleStand_01", "PlacedMeshWidthCm": 41.0}),
    ("oil_lamp", "등잔대", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "FlameLight",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_OilLamp_01.SM_Furn_OilLamp_01", "PlacedMeshWidthCm": 30.0}),
    ("lantern_post", "기둥 등불", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "FlameLight",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_LanternPost_01.SM_Furn_LanternPost_01", "PlacedMeshWidthCm": 41.0}),
    ("garden_torch", "정원 횃불", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "FlameLight",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_GardenTorch_01.SM_Furn_GardenTorch_01", "PlacedMeshWidthCm": 22.0}),
    # M108-06b 나무 계단 — 2층 집 안 한 줄(3×1), 2층 바닥 275 까지 디딤 12단 (8.11 「계단·위층」, 판정은 서버 `stairPlaceFail`)
    ("stairs_wood", "나무 계단", "Placeable", {"FootprintX": 3, "FootprintY": 1, "PlaceableFunction": "Stairs",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_StairsWood_01.SM_Furn_StairsWood_01", "PlacedMeshWidthCm": 300.0}),
    # M108-07b 연료 발전기 — 바닥 가구 1×1, 연료는 서버 `FeedGenerator` (8.11 「연료 발전기 숫자」)
    ("generator", "연료 발전기", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Generator",
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Generator_01.SM_Furn_Generator_01", "PlacedMeshWidthCm": 64.0}),
    # M108-08b 전기 가구 넷 (8.11 「🟡 전기 가구」) — 냉장고·선풍기 바닥 1×1, 라디오 위에 올리기(1.5배), 네온 간판 벽 걸기(1.5배).
    # 소모 `PowerUse` = 8.11 「🟡 전기 가구」 줄 · 켜짐 메시 짝 `PlacedOnMeshRef`(불 칸만 밝은 팔레트 칸, M108-08b-2), 켜짐 셈은 08c
    ("fridge", "냉장고", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PowerUse": 40,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_Fridge_01.SM_Furn_Fridge_01", "PlacedMeshWidthCm": 64.0,
     "PlacedOnMeshRef": HOME_FURN_DIR + "/SM_Furn_Fridge_On_01.SM_Furn_Fridge_On_01"}),
    ("fan_stand", "선풍기", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PowerUse": 20,
     "PlacedMeshRef": HOME_FURN_DIR + "/SM_Furn_FanStand_01.SM_Furn_FanStand_01", "PlacedMeshWidthCm": 51.0,
     "PlacedOnMeshRef": HOME_FURN_DIR + "/SM_Furn_FanStand_On_01.SM_Furn_FanStand_On_01",
     # M108-08d-1 날개 따로 — 머리 가운데(메시 원본 `FAN_HUB_Z` 86)·젖힘 `FAN_TILT` 20 · 초당 2바퀴(8.11 「🟡 전기 가구」)
     "PlacedSpinMeshRef": HOME_FURN_DIR + "/SM_Furn_FanBlade_01.SM_Furn_FanBlade_01",
     "PlacedSpinPivotCm": {"X": 0.0, "Y": 0.0, "Z": 86.0}, "PlacedSpinTiltDeg": 20.0, "PlacedSpinTurnsPerSecond": 2.0}),
    ("radio", "라디오", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "TopDecor", "PowerUse": 15,
     "PlacedMeshRef": TABLE_DECOR_DIR + "/SM_Prop_Radio_01.SM_Prop_Radio_01", "PlacedMeshWidthCm": 36.0,
     "PlacedOnMeshRef": TABLE_DECOR_DIR + "/SM_Prop_Radio_On_01.SM_Prop_Radio_On_01",
     # M108-08d-2 켜진 동안 가락 고리 — 감쇠 200 / 900cm · 음량 0.35(8.11 「🟡 전기 가구」)
     "PlacedOnLoopCue": "Radio_Loop", "PlacedOnLoopVolume": 0.35, "PlacedOnLoopInnerCm": 200.0, "PlacedOnLoopOuterCm": 900.0}),
    ("neon_sign", "네온 간판", "Placeable", {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "WallDecor", "PowerUse": 25,
     "PlacedMeshRef": WALL_DECOR_DIR + "/SM_Prop_NeonSign_01.SM_Prop_NeonSign_01", "PlacedMeshWidthCm": 59.0,
     "PlacedOnMeshRef": WALL_DECOR_DIR + "/SM_Prop_NeonSign_On_01.SM_Prop_NeonSign_On_01"}),
    # DESIGN 8.16 채광·제련 — 덩어리 6 · 주괴 6 · 금속 부품 · 철판 · 제련소(제련 30초 = 요리 타이머 칸)
    ("coal_chunk", "석탄 덩어리", "Material", {}),
    ("copper_chunk", "구리 덩어리", "Material", {}),
    ("iron_chunk", "철 덩어리", "Material", {}),
    ("titanium_chunk", "티타늄 덩어리", "Material", {}),
    ("silver_chunk", "은 덩어리", "Material", {}),
    ("gold_chunk", "금 덩어리", "Material", {}),
    ("coal_ingot", "코크스", "Material", {}),
    ("copper_ingot", "구리 주괴", "Material", {}),
    ("iron_ingot", "철 주괴", "Material", {}),
    ("titanium_ingot", "티타늄 주괴", "Material", {}),
    ("silver_ingot", "은 주괴", "Material", {}),
    ("gold_ingot", "금 주괴", "Material", {}),
    ("metal_parts", "쇠붙이 부속", "Material", {}),
    ("sheet_metal", "철판", "Material", {}),
    ("smelter", "제련소", "Placeable",
     {"FootprintX": 1, "FootprintY": 1, "PlaceableFunction": "Smelter", "CookSeconds": 30,
      "PlacedMeshRef": "/Game/Game/Environment/Props/Smelter/SM_Prop_Smelter_01.SM_Prop_Smelter_01",
      "PlacedMeshWidthCm": 90.0}),                                       # DESIGN 8.11 표 (M14-04)
    # DESIGN 8.11 「집 2층·전력」 전자 재료 6 (M20-01 표 → M20-02). 키트·배터리·전등은 놓는 가구 1칸 (M20-04a).
    # 키트·배터리·전등 메시 = `make_power_meshes.py` → `import_power_meshes.py` (INBOX 보정 10-06 02:45 — 팔레트·드럼 대용 끝, 전등 등불 빌림 끝 M40-06)
    ("solar_kit", "햇빛 집열판", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": POWER_DIR + "/SM_Prop_SolarPanel_01.SM_Prop_SolarPanel_01", "PlacedMeshWidthCm": 90.0}),
    ("light_bulb", "전구", "Material", {}),                                         # 🟡 ID (표에 ID 없음)
    ("battery", "배터리", "Placeable", {"FootprintX": 1, "FootprintY": 1,
     "PlacedMeshRef": POWER_DIR + "/SM_Prop_Battery_01.SM_Prop_Battery_01", "PlacedMeshWidthCm": 50.0}),
    ("lamp_electric", "전등", "Placeable", {"FootprintX": 1, "FootprintY": 1,             # 🟡 ID
     "PlacedMeshRef": POWER_DIR + "/SM_Prop_LampElectric_01.SM_Prop_LampElectric_01", "PlacedMeshWidthCm": 19.0}),
    ("polymer", "굳힘 수지", "Material", {}),
    ("circuit_board", "회로판", "Material", {}),
    ("electric_motor", "소형 전동기", "Material", {}),
    ("stair_kit", "층계 짜맞춤", "Material", {}),                                 # 8.11 집 2층 재료 (M20-03)
    # 13.8 비금속 부품 (M13-09). 구명조끼 = 상의 칸(🟡 몸 장비) · 수영 스태미나 소모 ×0.5 (13.8 「쓰는 곳」)
    ("resin_adhesive", "끈끈이 풀", "Material", {}),
    ("cotton_fabric", "무명천", "Material", {}),
    ("tarp", "기름 먹인 천", "Material", {}),
    ("life_jacket", "구명조끼", "Equipment", {"EquipSlot": "OutfitTop", "SwimStaminaMul": 0.5, **_outfit_mesh("LifeJacket")}),
    # 8.9 배 — 물 위에 띄우므로 격자 칸 0. 부품 셋은 가격·레시피가 설계서에 없어 행만 선다(🟡).
    ("lifeboat", "구명보트", "Placeable", {"PlaceableFunction": "Boat"}),            # 🟡 ID
    ("boat_motor", "배 추진기", "Material", {}),                                     # 🟡 ID·종류
    ("boat_motor_electric", "전동 추진기", "Material", {}),                       # 🟡 ID·종류
    ("boat_container", "배 짐칸", "Material", {"BonusSlots": 8}),             # 🟡 ID·종류, 8.9 보관 8칸
    # 8.12·8.13 장비 — 부위·낚시 배율, M13-43 낚싯대 메시(2026-10-05 오른손 Socket_Weapon).
    ("rod_basic", "기본 낚싯대", "Equipment", {"EquipSlot": "Tool", "UseSoundCue": "Cast", "BiteSoundCue": "Bite", "ReelSoundCue": "Reel", "FishingSpeedMul": 1.0, **_tool_mesh("FishingRod"), **ROD_MESH, **ROD_HOLD}),       # 🟡 ID
    ("rod_telescopic", "접이식 낚싯대", "Equipment", {"EquipSlot": "Tool", "UseSoundCue": "Swing", "BiteSoundCue": "Bite", "ReelSoundCue": "Reel", "FishingSpeedMul": 1.2, **_tool_mesh("TelescopicRod"), **ROD_MESH, **ROD_HOLD}),  # 🟡 ID
    ("rod_bait", "미끼통 낚싯대", "Equipment", {"EquipSlot": "Tool", "UseSoundCue": "Splash", "BiteSoundCue": "Bite", "ReelSoundCue": "Reel", "FishingSpeedMul": 1.5, **_tool_mesh("BaitRod"), **ROD_MESH, **ROD_HOLD}),
    ("rod_lucky", "황금 찌 낚싯대", "Equipment", {"EquipSlot": "Tool", "UseSoundCue": "Cast", "BiteSoundCue": "Pickup", "ReelSoundCue": "Reel", "FishingSpeedMul": 0.85, **_tool_mesh("LuckyRod"), **ROD_MESH, **ROD_HOLD}),
    ("fisher_hat", "물가 챙갓", "Equipment", {"EquipSlot": "Head", "FishingSpeedMul": 1.35}),       # 🟡 ID
    # DESIGN 13.5 모자 9 — 머리 칸. 서버 효과(수확·판매·뱃삯·나무)는 CloudScript HAT_EFFECTS, 몸 효과만 여기 값.
    ("farmer_hat", "밭두렁 갓", "Equipment", {"EquipSlot": "Head"}),
    ("hunter_hat", "길잡이 두건", "Equipment", {"EquipSlot": "Head", "PristineChanceMul": 1.3}),   # 13.5 최상급 확률 ×1.3
    ("lumberjack_hat", "도끼꾼 털모자", "Equipment", {"EquipSlot": "Head"}),
    ("strawberry_hat", "딸기 꼭지 베레", "Equipment", {"EquipSlot": "Head", "HealthRegenPerSecond": 0.1}),  # +1 / 10초
    ("cook_hat", "주방 두건", "Equipment", {"EquipSlot": "Head"}),
    ("looter_hat", "보물꾼 모자", "Equipment", {"EquipSlot": "Head"}),
    ("traveler_hat", "여행자 모자", "Equipment", {"EquipSlot": "Head"}),
    ("merchant_hat", "장사치 중절", "Equipment", {"EquipSlot": "Head"}),
    ("runner_hat", "달리기 모자", "Equipment", {"EquipSlot": "Head", "RunSpeedMul": 1.12}),
    # DESIGN 8.16 채굴 모자 — 쓰면 머리 앞 점광(광산 등불 120cd·700 보다 밝고 넓게)
    ("mining_hat", "갱도 투구", "Equipment", {"EquipSlot": "Head", "HeadLightIntensityCd": 200.0, "HeadLightRadiusCm": 900.0}),
    # DESIGN 11 「7일 출석」 7일째 · 13.5 새싹 모자 (M21-05c) — 효과 없음(겉모습만), 출석으로만 얻는다
    ("sprout_hat", "새싹 모자", "Equipment", {"EquipSlot": "Head"}),
    # DESIGN 10.6 「우두머리 모자」 · 13.5 (M21-07e) — 주간 우두머리 보상, 효과 없음(겉모습만)
    ("chief_hat", "우두머리 모자", "Equipment", {"EquipSlot": "Head"}),
    ("hatchet", "손도끼", "Equipment", {"EquipSlot": "Tool", "UseAction": "Smash", "UseSoundCue": "Chop", **HAND_AXE_MESH, **_grip(z=-32, yaw=180), **_body_anims("HoldOneHand", "SwingDownward")}),                     # 🟡 ID (M9-04 지시는 hand_axe — 서버 카탈로그·나무가 hatchet)
    ("gather_basket", "채집 바구니", "Equipment", {"EquipSlot": "Tool", "UseAction": "Gather", "UseSoundCue": "Pickup", **BASKET_MESH, **_body_anims("Idle", "Gather")}),           # M9-04 · 5절 로스터
    # DESIGN 13.5 도구 4 — 도구 칸. 나무·채집 +1 은 CloudScript ClaimLoot(가방에 있을 때), 여기는 몸 효과만.
    ("chainsaw", "전동 톱", "Equipment", {"EquipSlot": "Tool", "UseAction": "Chop", "UseSoundCue": "Chainsaw_Rev", "HoldLoopSoundCue": "Chainsaw_Idle", **_tool_mesh("Chainsaw"), **_grip(y=-39, roll=-90), **_body_anims("HoldSaw", "SawThrust"), "bUseAnimFixedLength": True,
                                       "UseHoldStartSeconds": 0.2, "UseHoldEndSeconds": 1.2}),  # 누르는 동안 f6~f36 버팀 (DESIGN 3.1 「전기톱 누르는 동안 버팀」)
    ("machete", "마체테", "Equipment", {"EquipSlot": "Tool", "UseAction": "Chop", "UseSoundCue": "Machete", **_tool_mesh("Machete"), **_grip(y=-35, roll=-90), **_body_anims("HoldOneHand", "Slash")}),
    ("binoculars", "망원경", "Equipment", {"EquipSlot": "Tool", "CameraDistanceMul": 1.3, **_tool_mesh("Binoculars"),
                                       **_body_anims("Idle", "LookBinoculars"), "bUseAnimFixedLength": True}),
    # 위성 통신기는 손에 들지 않는다 — 레퍼런스는 좌표 HUD 만, 몸은 빈손(character_motion.md 「위성 통신기」). 메시 칸을 비워 빈손으로 든다.
    ("satellite_communicator", "별길 수신기", "Equipment", {"EquipSlot": "Tool", "bShowsMapCoordinates": True, "bPassiveWhileCarried": True}),
    # DESIGN 8.16 곡괭이 3 — 도구 칸. 메시는 손도끼 자리표시(M14-04 바르코 전).
    # 든 자세는 곧게 선 대기 v2 숨 몸 — 기본 `HoldOneHand`, 고급·전기 `HoldHeavyPickaxe`(같은 몸·손목, 머리가 넓어 덜 숙임 15°). 옛 `HoldTwoHand`(2hand Idle)는
    # 무릎·허리를 굽혀 들면 몸이 앞으로 숙여졌다(INBOX 결함 10-20 ④, 루프#1354 · DESIGN 3.5 「무거운 곡괭이 든 자세」).
    ("pickaxe_basic", "나무자루 곡괭이", "Equipment", {"EquipSlot": "Tool", "UseAction": "Mine", "UseSoundCue": "Mine", **_tool_mesh("PickaxeBasic"), **_grip(z=-35), **_body_anims("HoldOneHand", "MineOneHand")}),
    ("pickaxe_advanced", "강철 곡괭이", "Equipment", {"EquipSlot": "Tool", "UseAction": "Mine", "UseSoundCue": "Mine", **_tool_mesh("PickaxeAdvanced"), **_grip(z=-35), **_body_anims("HoldHeavyPickaxe", "MineGrind"), "bUseAnimFixedLength": True}),
    ("pickaxe_electric", "전동 곡괭이", "Equipment", {"EquipSlot": "Tool", "UseAction": "Mine", "UseSoundCue": "Mine", "HoldLoopSoundCue": "ElectricHum", **_tool_mesh("PickaxeElectric"), **_grip(z=-35), **_body_anims("HoldHeavyPickaxe", "MineGrind"), "bUseAnimFixedLength": True}),
    # DESIGN 8.6-c 사냥 총 (M39-01) — 무기 칸. 숫자는 전부 8.6-c 표, 감쇠 0.5/0.5 는 8.6-b 원거리 기본값.
    # 든 자세 = 두 손 겨눔, 쓰기 = 반동(M39-06b `tools/blender/pistol_aim_make.py`) — 반동 클립 0.4초 = 쏘기 간격이라 재생 속도 1.
    # 메시(M39-06a)는 `make_weapon_meshes.py` → `import_weapon_meshes.py` — 원점이 쥐는 점이라 `GripOffset` 이 없다.
    ("pistol_semi", "단발 권총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_PistolSemi_01.SM_Weapon_PistolSemi_01",
     "AttackDamage": 20.0, "AttackRangeCm": 1000.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 7, "ReloadSeconds": 1.2, "FireIntervalSeconds": 0.4, "ProjectileSpeedCmPerSecond": 4000.0,
     "AmmoItemID": "item.ammo", "ShotHapticKind": "Pistol", **_body_anims("HoldPistol", "PistolRecoil"),
     "ReloadAnim": "{0}/A_Player_PistolReload.A_Player_PistolReload".format(PLAYER_ANIM_DIR)}),
    ("ammo", "권총탄", "Material", {}),                                             # 8.6-c 잡화점 25발 묶음 (ID 는 그대로, 표시 이름만 — 탄 4종 중 권총·기관단총용)
    # 탄 4종 중 소총탄 (사람 지시 2026-10-11 23:45) — 소총·기관총용. 산탄(`ammo_shotgun`)·화살(`arrow`)은 쓰는 총 행과 같은 바퀴에 넣는다(M쓰는 곳 없는 아이템 0 단정).
    ("ammo_rifle", "소총탄", "Material", {}),
    ("ammo_shotgun", "산탄", "Material", {}),                                       # 8.6-c 산탄총 3종용 16발 묶음
    ("arrow", "화살", "Material", {}),                                              # 8.6-c 석궁용 15발 묶음
    # 8.6-c 연발 권총·기관단총 (INBOX 총 11종 3조각) — 탄약은 권총탄 `item.ammo`. 든 자세·반동·재장전은 권총 것.
    ("pistol_auto", "연발 권총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_PistolAuto_01.SM_Weapon_PistolAuto_01",
     "AttackDamage": 12.0, "AttackRangeCm": 900.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 15, "ReloadSeconds": 1.4, "FireIntervalSeconds": 0.2, "ProjectileSpeedCmPerSecond": 4000.0,
     "AmmoItemID": "item.ammo", "ShotHapticKind": "Pistol", **_body_anims("HoldPistol", "PistolRecoil"),
     "ReloadAnim": "{0}/A_Player_PistolReload.A_Player_PistolReload".format(PLAYER_ANIM_DIR)}),
    ("smg", "기관단총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_Smg_01.SM_Weapon_Smg_01",
     "AttackDamage": 10.0, "AttackRangeCm": 800.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 30, "ReloadSeconds": 1.8, "FireIntervalSeconds": 0.15, "ProjectileSpeedCmPerSecond": 4500.0,
     "AmmoItemID": "item.ammo", "ShotHapticKind": "Pistol", **_body_anims("HoldPistol", "PistolRecoil"),
     "ReloadAnim": "{0}/A_Player_PistolReload.A_Player_PistolReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 점사 소총·반자동 소총 (INBOX 총 11종 4조각) — 탄약은 소총탄 `item.ammo_rifle`. 든 자세·반동·재장전은 소총 것. 점사 3발 묶음 동작은 아직 없어 한 발씩 쏘는 간격으로만 구분(DESIGN 🟡).
    ("rifle_burst", "점사 소총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_RifleBurst_01.SM_Weapon_RifleBurst_01",
     "AttackDamage": 18.0, "AttackRangeCm": 1800.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 12, "ReloadSeconds": 2.0, "FireIntervalSeconds": 0.5, "ProjectileSpeedCmPerSecond": 5500.0,
     "AmmoItemID": "item.ammo_rifle", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    ("rifle_semi", "반자동 소총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_RifleSemi_01.SM_Weapon_RifleSemi_01",
     "AttackDamage": 28.0, "AttackRangeCm": 2200.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 10, "ReloadSeconds": 2.2, "FireIntervalSeconds": 0.7, "ProjectileSpeedCmPerSecond": 5800.0,
     "AmmoItemID": "item.ammo_rifle", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 자동 산탄총·짧은 산탄총 (INBOX 총 11종 5조각) — 탄약은 산탄 `item.ammo_shotgun`. 든 자세·반동·재장전은 소총 것.
    ("shotgun_auto", "연사 산탄총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_ShotgunAuto_01.SM_Weapon_ShotgunAuto_01",
     "PelletCount": 6, "PelletSpreadHalfDegrees": 10.0, "AttackDamage": 22.0, "AttackRangeCm": 700.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 8, "ReloadSeconds": 2.6, "FireIntervalSeconds": 0.6, "ProjectileSpeedCmPerSecond": 4500.0,
     "AmmoItemID": "item.ammo_shotgun", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    ("shotgun_short", "짧은 산탄총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_ShotgunShort_01.SM_Weapon_ShotgunShort_01",
     "PelletCount": 10, "PelletSpreadHalfDegrees": 18.0, "AttackDamage": 35.0, "AttackRangeCm": 500.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 2, "ReloadSeconds": 1.6, "FireIntervalSeconds": 1.3, "ProjectileSpeedCmPerSecond": 4200.0,
     "AmmoItemID": "item.ammo_shotgun", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 기관총 (INBOX 총 11종 6조각) — 탄약은 소총탄 `item.ammo_rifle`. 든 자세·반동·재장전은 소총 것.
    ("mg", "기관총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_MachineGun_01.SM_Weapon_MachineGun_01",
     "AttackDamage": 16.0, "AttackRangeCm": 1500.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 40, "ReloadSeconds": 4.0, "FireIntervalSeconds": 0.22, "ProjectileSpeedCmPerSecond": 5500.0,
     "AmmoItemID": "item.ammo_rifle", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 석궁 (INBOX 총 11종 2조각) — 상점에서 판다. 한 발씩 장전하는 느린 총. 든 자세·반동·재장전 동작은 소총 것을 빌린다.
    ("crossbow", "석궁", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_Crossbow_01.SM_Weapon_Crossbow_01",
     "AttackDamage": 35.0, "AttackRangeCm": 1800.0, "FalloffStartRatio": 0.6, "MinDamageRatio": 0.7,
     "MagazineSize": 1, "ReloadSeconds": 1.8, "FireIntervalSeconds": 1.6, "ProjectileSpeedCmPerSecond": 3000.0,
     "AmmoItemID": "item.arrow", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 산탄총 (INBOX 총 11종 1조각) — 상점에서 판다(권총 같은 길). 든 자세·반동·재장전은 소총 것을 빌린다(동작은 메시와 달리 겹쳐도 됨).
    ("shotgun", "산탄총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_Shotgun_01.SM_Weapon_Shotgun_01",
     "PelletCount": 8, "PelletSpreadHalfDegrees": 12.0, "AttackDamage": 30.0, "AttackRangeCm": 800.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 6, "ReloadSeconds": 2.2, "FireIntervalSeconds": 1.0, "ProjectileSpeedCmPerSecond": 4500.0,
     "AmmoItemID": "item.ammo_shotgun", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 현대 소총 (M39-08a) — 고급 작업대 제작(8.10). 메시(M39-08b)는 권총과 같은 `make_weapon_meshes.py` 길.
    # 든 자세는 권총 것(두 손은 팔 길이로 안 됨 — `pistol_aim_make.py` 머리말), 반동 0.25초·재장전 2.0초는 소총 클립(M39-08c). 탄약은 소총탄 `item.ammo_rifle`.
    ("rifle_modern", "신식 소총", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_RifleModern_01.SM_Weapon_RifleModern_01",
     "AttackDamage": 40.0, "AttackRangeCm": 2500.0, "FalloffStartRatio": 0.5, "MinDamageRatio": 0.5,
     "MagazineSize": 5, "ReloadSeconds": 2.0, "FireIntervalSeconds": 1.2, "ProjectileSpeedCmPerSecond": 6000.0,
     "AmmoItemID": "item.ammo_rifle", "ShotHapticKind": "Rifle", **_body_anims("HoldPistol", "RifleRecoil"),
     "ReloadAnim": "{0}/A_Player_RifleReload.A_Player_RifleReload".format(PLAYER_ANIM_DIR)}),
    # 8.6-c 바주카 (사람 지시 2026-10-15 19:15, 숫자는 감독 확정) — 직격 300 + 반경 300cm 폭발(가장자리 30%). 탄창 1·재장전 3초라 한 발씩 신중히. 탄약은 로켓 `item.rocket`.
    # 든 자세·반동·재장전은 바주카 전용 어깨 자세(#1211 `pistol_aim_make.py baz_*`). 사거리는 탄 속도 2500 × 포물선 약하게 — 3000cm, 거리 감쇠 없음(직격은 늘 300).
    ("bazooka", "바주카", "Equipment", {"EquipSlot": "Weapon", "EquipSocketName": "Socket_Weapon",
     "EquipMeshRef": "/Game/Game/Characters/Equipment/Weapons/SM_Weapon_Bazooka_01.SM_Weapon_Bazooka_01",
     "AttackDamage": 300.0, "AttackRangeCm": 3000.0, "FalloffStartRatio": 1.0, "MinDamageRatio": 1.0,
     "SplashRadiusCm": 300.0, "SplashEdgeRatio": 0.3,
     "MagazineSize": 1, "ReloadSeconds": 3.0, "FireIntervalSeconds": 1.0, "ProjectileSpeedCmPerSecond": 2500.0,
     "AmmoItemID": "item.rocket", "ShotHapticKind": "Rifle", **_body_anims("HoldBazooka", "BazookaRecoil"),
     "ReloadAnim": "{0}/A_Player_BazookaReload.A_Player_BazookaReload".format(PLAYER_ANIM_DIR)}),
    ("rocket", "로켓", "Material", {}),                                             # 8.6-c 바주카 탄 — 잡화점 4발 묶음(발당 5 코인)
    ("hiking_backpack", "등산 짐가방", "Equipment", {"EquipSlot": "Backpack", "BonusSlots": 8, **HIKING_BACKPACK_MESH}),     # 🟡 ID · M13-43 등 Socket_Back
    # DESIGN 13절 「셔츠·바지」(M9-05) — 효과 없는 꾸미기 옷. 셔츠는 조끼와 같은 상의 칸이라 서로 바꿔 입는다.
    ("flannel_shirt", "플란넬 셔츠", "Equipment", {"EquipSlot": "OutfitTop", **_outfit_mesh("FlannelShirt")}),
    ("work_pants", "작업 바지", "Equipment", {"EquipSlot": "OutfitBottom", **_outfit_mesh("WorkPants")}),
    # 8.10 음식
    ("roast_meat", "구운 고기", "Food", {"HealAmount": 30}),
    ("berry_jam", "산딸기잼", "Food", {"HealAmount": 10, "CraftTag": "Jam"}),                 # 13.6 원본 +10
    # 13.6 요리 재료 9 (M13-06). HealAmount = 「날로 먹기」 열, 태그 = 「재료 태그 추가」
    ("flour", "밀가루", "Material", {}),
    ("sugar", "설탕", "Material", {"HealAmount": 8}),
    ("vegetable_oil", "식물 기름", "Material", {"HealAmount": 5, "CraftTag": "Oil"}),
    ("tea_bag", "찻잎 주머니", "Material", {}),
    ("salt", "소금", "Material", {"HealAmount": 5, "CraftTag": "Spice"}),
    ("black_pepper", "흑후추", "Material", {"HealAmount": 5, "CraftTag": "Spice"}),
    ("dill", "딜", "Material", {"CraftTag": "Spice"}),
    ("empty_bottle", "빈 병", "Material", {"FilledItemID": "item.water_bottle"}),          # 🟡 채우기 칸
    ("water_bottle", "샘물 병", "Food", {"HealAmount": 15}),
    # 13.6 버프 소모품 (M13-08). HealAmount = 「체력」 열, 버프는 아래 ITEM_BUFFS. 탄산은 13.7 재료로 먼저 섰다.
    ("hot_soda", "따뜻한 탄산", "Food", {"HealAmount": 25}),
    ("energy_drink_mild", "기운 탄산 연한", "Food", {}),
    ("energy_drink", "기운 탄산 보통", "Food", {"HealAmount": 5}),
    ("energy_drink_strong", "기운 탄산 진한", "Food", {"HealAmount": 5}),
    ("coffee_bottle", "커피 보온병", "Food", {"HealAmount": 15}),
    ("tea_bottle", "차 병", "Food", {"HealAmount": 15}),
    ("heat_pack", "손난로", "Food", {}),
    ("snackbar", "곡식 막대", "Food", {"HealAmount": 25}),
    ("water_canister", "수통", "Food", {"HealAmount": 20}),
    ("canned_fish", "절임 생선통", "Food", {"HealAmount": 25}),
]

# M13-35 모자 10종 — 행마다 자기 메시(`import_equipment_weapons.py` 가 반입) + 머리 소켓 + 월드 폭.
# 붙일 때 `UEquipmentComponent` 가 메시 바닥 긴 변을 `HatWidthCm` 에 맞추는 한 배율로 키운다(높이는 메시 비율).
# 폭은 DESIGN 3.5 「모자별 폭」 표 그대로(v2 메시 폭 × 몸 배율 1.76) — 모자마다 따로라 기본 폭은 안 쓴다.
HAT_DIR = "/Game/Game/Characters/Equipment/Hats"
HAT_WIDTH_CM = {"hunter": 119.2, "fisher": 142.7, "farmer": 174.4, "lumberjack": 119.2, "strawberry": 117.0,
                "cook": 118.6, "looter": 149.8, "traveler": 111.4, "merchant": 147.0, "runner": 116.2,
                "mining": 124.2, "sprout": 114.2, "chief": 119.2}
HAT_DEFAULT_WIDTH_CM = 66.0


def _hat_columns(item_id):
    name = item_id[:-len("_hat")].capitalize()   # fisher_hat → Fisher (원본 폴더 이름)
    mesh = "SM_Hat_{}_01".format(name)
    return {"EquipMeshRef": "{0}/{1}/{2}.{2}".format(HAT_DIR, name, mesh), "EquipSocketName": "Socket_Head",
            "HatWidthCm": HAT_WIDTH_CM.get(name.lower(), HAT_DEFAULT_WIDTH_CM)}

# M13-37 설치물 10종 — 행마다 자기 메시(`import_equipment_weapons.py` FURNITURE 가 반입) + 월드 폭.
# 놓일 때 `ASnarePlacedStructureActor` 가 메시 수평 긴 변을 `PlacedMeshWidthCm` 에 맞추는 한 배율로 키운다.
# 폭은 DESIGN 8.11 「가구·상자 진짜 메시 크기」 표 그대로.
FURNITURE_DIR = "/Game/Game/Environment/Furniture"
PLACED_MESH_WIDTH_CM = {"bench": 170.0, "chair_wood": 55.0, "table_wood": 145.0, "fence_wood": 100.0,
                        "storage_box": 80.0, "campfire": 90.0, "garden_brazier": 75.0, "grill": 75.0,
                        "workbench_adv": 145.0, "tent": 260.0}


def _placed_mesh_columns(item_id):
    name = "".join(part.capitalize() for part in item_id.split("_"))   # chair_wood → ChairWood (원본 폴더 이름)
    mesh = "SM_Furn_{}_01".format(name)
    return {"PlacedMeshRef": "{0}/{1}/{2}.{2}".format(FURNITURE_DIR, name, mesh),
            "PlacedMeshWidthCm": PLACED_MESH_WIDTH_CM[item_id]}

# 13.6 「버프」 열 — (BuffKind, BuffValue). 지속은 전부 13.6 「지속」 180초. 잼 둘은 요리 행(COOKING_DISHES)에 붙는다.
# 링곤베리잼 0.4 = 원본 "체력 재생 40%" → 자연 회복(3.1) × 1.4 (🟡 원본 우선, 13.6 행을 고쳤다).
BUFF_SECONDS = 180.0
ITEM_BUFFS = {
    "energy_drink_mild": ("Speed", 0.05), "energy_drink": ("Speed", 0.12), "energy_drink_strong": ("Speed", 0.30),
    "coffee_bottle": ("ColdResist", 0.8), "tea_bottle": ("ColdResist", 0.6), "heat_pack": ("ColdResist", 0.9),
    "hot_soda": ("ColdResist", 0.4), "blueberry_jam": ("SwimSave", 0.8), "lingonberry_jam": ("Regen", 0.4),
}

# 13.3 「날로 먹기」 열 + 13.6 감자 +5 · 13.6 태그 — 작물 행(CROPS 루프가 만든다)에 붙인다. 메제레움은 독(−40, 태그 없음).
CROP_EXTRAS = {
    "carrot": {"CraftTag": "Vegetable"}, "potato": {"HealAmount": 5, "CraftTag": "Vegetable"},
    "blueberry": {"HealAmount": 10, "CraftTag": "Berry"}, "lingonberry": {"HealAmount": 12, "CraftTag": "Berry"},
    "blackberry": {"CraftTag": "Berry"}, "cloudberry": {"CraftTag": "Berry"}, "mezereon": {"HealAmount": -40},
    "sugar_beet": {"HealAmount": 8, "CraftTag": "Vegetable"}, "onion": {"HealAmount": 8, "CraftTag": "Vegetable"},
    "cucumber": {"HealAmount": 5, "CraftTag": "Vegetable"}, "cabbage": {"HealAmount": 10, "CraftTag": "Vegetable"},
    "pumpkin": {"CraftTag": "Vegetable"}, "chili": {"CraftTag": "Spice"}, "apple": {"HealAmount": 10, "CraftTag": "Fruit"},
}

# 8.10 ★ 설계도 4장 — 설계도 아이템 ID 는 설계서에 없다 → 🟡 `item.blueprint_<결과>`.
BLUEPRINT_RESULTS = [("rod_bait", "미끼통 낚싯대"), ("rod_lucky", "황금 찌 낚싯대"),
                     ("grill", "그릴"), ("workbench_adv", "고급 작업대")]


def _ing(count, item=None, tag=None):
    """재료 한 칸. ID 와 태그 중 하나만 채운다 (`FRecipeIngredient`)."""
    return {"ItemID": "item.{}".format(item) if item else "None", "Tag": tag or "None", "Count": count}


# DESIGN 13.7 요리 44 — 전부 불 1개. (id, 이름, 체력, 재료). 결과를 재료로 쓰는 요리는 재료 뒤에 둔다(판매가 한 바퀴).
# 잼 5종은 8.10 산딸기잼과 같은 `Jam` 태그. 2층 `Snarehaven.Crafting.Cooking.Table.MatchesDesign` 이 13.7 표와 맞댄다.
COOKING_DISHES = [
    ("bread", "빵", 30, [_ing(2, "flour")]),
    ("ciabatta", "치아바타", 40, [_ing(2, "flour"), _ing(1, tag="Oil")]),
    ("ciabatta_topped", "채소 얹은 치아바타", 40, [_ing(1, "ciabatta"), _ing(1, tag="Vegetable")]),
    ("carrot_bread", "당근 화덕빵", 30, [_ing(1, "flour"), _ing(2, "carrot")]),
    ("croissant", "크루아상", 15, [_ing(1, "flour"), _ing(1, "milk")]),
    ("croissant_jam", "잼 크루아상", 15, [_ing(1, "croissant"), _ing(1, tag="Jam")]),
    ("cake", "케이크", 75, [_ing(2, "flour"), _ing(2, "sugar"), _ing(1, "egg"), _ing(1, "milk")]),
    ("sandwich", "샌드위치", 15, [_ing(1, "bread"), _ing(1, tag="Vegetable")]),
    ("fish_sandwich", "생선 샌드위치", 15, [_ing(1, "bread"), _ing(1, tag="Fish")]),
    ("cloudberry_jam", "클라우드베리잼", 40, [_ing(3, "cloudberry")]),
    ("blackberry_jam", "블랙베리잼", 15, [_ing(3, "blackberry")]),
    ("mezereon_jam", "서향잼", 15, [_ing(3, "mezereon")]),
    ("blueberry_jam", "블루베리잼", 15, [_ing(3, "blueberry")]),
    ("lingonberry_jam", "링곤베리잼", 15, [_ing(3, "lingonberry")]),
    ("pumpkin_pie", "호박 속 구이", 25, [_ing(1, "flour"), _ing(1, "pumpkin"), _ing(1, "sugar")]),
    ("berry_pie", "열매 타르트", 20, [_ing(1, "flour"), _ing(2, tag="Berry")]),
    ("apple_pie", "사과 파이", 25, [_ing(1, "flour"), _ing(2, tag="Fruit")]),
    ("meat_pie", "고기 파이", 25, [_ing(1, "flour"), _ing(1, tag="Meat")]),
    ("chanterelle_soup", "살구버섯 죽", 70, [_ing(3, "chanterelle"), _ing(1, "water_bottle")]),
    ("fish_soup", "물고기 탕", 20, [_ing(1, tag="Fish"), _ing(1, "water_bottle")]),
    ("meat_soup", "고깃국", 25, [_ing(1, tag="Meat"), _ing(1, "water_bottle")]),
    ("hot_pot", "고기 전골", 55, [_ing(2, tag="Meat"), _ing(2, tag="Vegetable"), _ing(1, "water_bottle")]),
    ("berry_salad", "열매 무침", 20, [_ing(3, tag="Berry")]),
    ("mushroom_salad", "버섯 무침", 18, [_ing(2, tag="Mushroom"), _ing(1, tag="Vegetable")]),
    ("roasted_mushrooms", "숯불 버섯", 20, [_ing(2, tag="Mushroom")]),
    ("roasted_vegetables", "구운 채소", 15, [_ing(2, tag="Vegetable")]),
    ("stuffed_mushroom", "게살 버섯", 20, [_ing(1, "cave_crab"), _ing(1, tag="Mushroom")]),
    ("cooked_bird", "구운 새고기", 30, [_ing(1, "bird_meat")]),
    ("cooked_reindeer", "구운 순록 고기", 35, [_ing(1, "reindeer_meat")]),
    ("cooked_wolf", "구운 늑대 고기", 30, [_ing(1, "wolf_meat")]),
    ("seasoned_meat", "양념 고기", 15, [_ing(1, tag="Meat"), _ing(1, tag="Spice")]),
    ("seasoned_fish", "양념 생선", 18, [_ing(1, tag="Fish"), _ing(1, "salt")]),
    ("sauteed_meat", "볶은 고기", 20, [_ing(1, tag="Meat"), _ing(1, tag="Oil")]),
    ("fried_meat", "지짐 고기", 25, [_ing(1, tag="Meat"), _ing(1, "flour"), _ing(1, tag="Oil")]),
    ("soda_battered_meat", "탄산 반죽 고기 튀김", 20, [_ing(1, tag="Meat"), _ing(1, "flour"), _ing(1, "hot_soda")]),
    ("meat_and_vegetables", "고기와 채소", 20, [_ing(1, tag="Meat"), _ing(1, tag="Vegetable")]),
    ("fish_and_vegetables", "물고기 채소찜", 20, [_ing(1, tag="Fish"), _ing(1, tag="Vegetable")]),
    ("fish_and_chips", "피시 앤 칩스", 15, [_ing(1, tag="Fish"), _ing(1, "potato"), _ing(1, tag="Oil")]),
    ("fish_bowl", "생선 덮밥", 10, [_ing(1, tag="Fish"), _ing(1, "cucumber")]),
    ("cabbage_roll", "양배추쌈", 15, [_ing(1, "cabbage"), _ing(1, tag="Meat")]),
    ("onion_rings", "양파 튀김 고리", 10, [_ing(2, "onion"), _ing(1, "flour"), _ing(1, tag="Oil")]),
    ("caramelized_carrot", "캐러멜 당근", 15, [_ing(2, "carrot"), _ing(1, "sugar")]),
    ("bear_sausage", "곰고기 훈제 순대", 65, [_ing(2, "bear_meat"), _ing(1, tag="Spice")]),
    ("grilled_axolotl", "우파루파 구이", 20, [_ing(1, "axolotl")]),
]

# DESIGN 8.11 「판매가」 줄 — 상점 물건이지만 산 값의 ⅓(전구·배터리·키트), 폴리머는 수지(0)로 만들지만 1.
# 상점·레시피 규칙보다 먼저 이긴다(서버 `FIXED_SELL_PRICE` 와 같다).
FIXED_SELL_PRICES = {"light_bulb": 2, "battery": 5, "solar_kit": 10, "polymer": 1, "lamp_electric": 5}  # 전등 = 재료 합

ORE_CHUNK_PRICES = {"coal_chunk": 1, "copper_chunk": 1, "iron_chunk": 2, "titanium_chunk": 4, "silver_chunk": 5, "gold_chunk": 8}

# DESIGN 13.4 「판매가」 늪 채집물 (M33-01) — 서버 GATHER_SELL_PRICE 와 짝. 시간당 상한 = 곳 수 × 12번(300초) × 값:
# 늪꽃 3곳 × 4 = 수련 2곳 × 6 = 평원 꽃 6곳 × 2 (144/시간). 독사갈기 = 검은 연꽃 8(설원 덤불 1/5, 원본 「전설」).
GATHER_SELL_PRICES = {"vipers_mane": 8, "bog_flower": 4, "water_lily": 6}

# DESIGN 8.10 레시피 표 그대로. (결과 id, 재료, 자리, 결과 개수)
RECIPES = [
    ("plank", [_ing(4, "wood")], "Workbench", 1),
    ("cedar_plank", [_ing(4, "cedar")], "Workbench", 1),
    ("bench", [_ing(2, "plank"), _ing(1, "cedar_plank")], "Workbench", 1),
    ("chair_wood", [_ing(2, "plank")], "Workbench", 1),
    ("table_wood", [_ing(3, "plank"), _ing(1, "cedar_plank")], "Workbench", 1),
    ("fence_wood", [_ing(2, "plank")], "Workbench", 1),
    ("storage_box", [_ing(6, "plank"), _ing(2, "cedar_plank")], "Workbench", 1),
    ("armchair", [_ing(2, "plank"), _ing(2, "cotton_fabric")], "Workbench", 1),
    ("sofa", [_ing(3, "plank"), _ing(4, "cotton_fabric")], "Workbench", 1),
    ("table_round", [_ing(2, "plank"), _ing(1, "cedar_plank")], "Workbench", 1),
    ("table_long", [_ing(5, "plank"), _ing(2, "cedar_plank")], "Workbench", 1),
    ("bed_single", [_ing(4, "plank"), _ing(3, "cotton_fabric")], "Workbench", 1),
    ("bed_double", [_ing(6, "plank"), _ing(5, "cotton_fabric")], "Workbench", 1),
    ("bookshelf", [_ing(5, "plank"), _ing(1, "cedar_plank")], "Workbench", 1),
    ("dresser", [_ing(4, "plank"), _ing(2, "cedar_plank")], "Workbench", 1),
    ("kitchen_counter", [_ing(4, "plank"), _ing(1, "cedar_plank"), _ing(1, "metal_parts")], "Workbench", 1),
    ("kitchen_sink", [_ing(4, "plank"), _ing(2, "metal_parts")], "Workbench", 1),
    ("potted_tree", [_ing(2, "plank"), _ing(1, "seed_oak")], "Workbench", 1),
    ("potted_berry", [_ing(1, "plank"), _ing(1, "seed_berry")], "Workbench", 1),
    ("flower_box", [_ing(2, "plank"), _ing(2, "poppy"), _ing(2, "kingcup")], "Workbench", 1),
    ("potted_sprout", [_ing(1, "plank"), _ing(1, "seed_tea")], "Workbench", 1),
    ("rug_round", [_ing(3, "cotton_fabric")], "Workbench", 1),
    ("rug_runner", [_ing(2, "cotton_fabric")], "Workbench", 1),
    ("rug_square", [_ing(5, "cotton_fabric"), _ing(2, "poppy")], "Workbench", 1),
    ("doormat", [_ing(4, "wheat")], "Workbench", 1),
    ("fence_picket", [_ing(3, "plank")], "Workbench", 1),
    ("path_log", [_ing(2, "wood")], "Workbench", 1),
    ("garden_arch", [_ing(4, "plank"), _ing(1, "poppy"), _ing(1, "kingcup")], "Workbench", 1),
    ("mailbox", [_ing(1, "plank"), _ing(1, "sheet_metal")], "Workbench", 1),
    ("candle_stand", [_ing(2, "plank"), _ing(1, "honey")], "Workbench", 1),                     # 8.10 M108-05e
    ("oil_lamp", [_ing(1, "plank"), _ing(1, "vegetable_oil")], "Workbench", 1),
    ("lantern_post", [_ing(2, "plank"), _ing(1, "sheet_metal"), _ing(1, "honey")], "Workbench", 1),
    ("garden_torch", [_ing(1, "wood"), _ing(1, "resin")], "Workbench", 1),
    ("stairs_wood", [_ing(8, "plank"), _ing(1, "stair_kit")], "Workbench", 1),                # 8.10 M108-06
    ("picture_frame", [_ing(1, "plank"), _ing(1, "cotton_fabric")], "Workbench", 1),          # 8.10 M108-02 (루프#1427)
    ("wall_clock", [_ing(1, "plank"), _ing(1, "metal_parts")], "Workbench", 1),
    ("wall_shelf", [_ing(2, "plank")], "Workbench", 1),
    ("mug", [_ing(1, "plank")], "Workbench", 1),                                              # 8.10 M108-03 (루프#1427)
    ("flower_pot", [_ing(1, "plank"), _ing(1, "kingcup")], "Workbench", 1),
    ("candle", [_ing(1, "sheet_metal"), _ing(1, "honey")], "Workbench", 1),
    ("wall_mirror", [_ing(1, "plank"), _ing(1, "metal_parts")], "Workbench", 1),
    ("wall_map", [_ing(1, "cotton_fabric"), _ing(1, "plank")], "Workbench", 1),
    ("wall_anchor", [_ing(2, "iron_ingot"), _ing(1, "cotton_fabric")], "Workbench", 1),
    ("wall_wreath", [_ing(1, "wood"), _ing(2, "poppy"), _ing(2, "kingcup")], "Workbench", 1),
    ("campfire", [_ing(5, "wood")], "Anywhere", 1),
    ("berry_bait", [_ing(3, "berry")], "Anywhere", 3),
    ("rod_bait", [_ing(4, "plank"), _ing(2, tag="Feather")], "Workbench", 1),
    ("rod_lucky", [_ing(4, "plank"), _ing(2, tag="Hide")], "Workbench", 1),
    ("grill", [_ing(6, "plank"), _ing(2, tag="Hide")], "Workbench", 1),
    ("workbench_adv", [_ing(10, "plank"), _ing(4, "cedar_plank")], "Workbench", 1),
    # 13.8 부품 4 + 텐트 제작(새 길 — 상점 35 코인과 나란히). 접착제·원단은 어디서나(M22 ⑥)
    ("resin_adhesive", [_ing(2, "resin")], "Anywhere", 1),
    ("cotton_fabric", [_ing(3, "cotton")], "Anywhere", 1),
    ("tarp", [_ing(2, "cotton_fabric"), _ing(1, "resin_adhesive")], "Workbench", 1),
    ("life_jacket", [_ing(2, "cotton_fabric"), _ing(1, "tarp")], "Workbench", 1),
    ("tent", [_ing(4, "tarp"), _ing(6, "plank")], "Workbench", 1),
    ("roast_meat", [_ing(1, tag="Meat")], "Fire", 1),
    ("berry_jam", [_ing(3, "berry")], "Fire", 1),
    # 13.6 요리 재료 — 모닥불 요리(원본 「요리」, 8.10 「작업대 없이 되는 것」 10-06 보정). 티백은 제작 없음(상자 전리품)
    ("flour", [_ing(2, "wheat")], "Fire", 1),
    ("sugar", [_ing(2, "sugar_beet")], "Fire", 1),
    ("vegetable_oil", [_ing(3, "canola")], "Fire", 1),
    # 13.6 버프 소모품 — 음료 5 는 불, 생선 통조림은 작업대 `Fish` 2
    ("energy_drink_mild", [_ing(1, "water_bottle"), _ing(1, "sugar")], "Fire", 1),
    ("energy_drink", [_ing(1, "water_bottle"), _ing(1, "sugar"), _ing(1, "coffee_beans")], "Fire", 1),
    ("energy_drink_strong", [_ing(1, "water_bottle"), _ing(2, "sugar"), _ing(2, "coffee_beans")], "Fire", 1),
    ("coffee_bottle", [_ing(1, "water_bottle"), _ing(2, "coffee_beans")], "Fire", 1),
    ("tea_bottle", [_ing(1, "water_bottle"), _ing(1, "tea_bag")], "Fire", 1),
    ("canned_fish", [_ing(2, tag="Fish")], "Workbench", 1),
    # 8.10 늪 꽃 물감 (M33-02) — 늪꽃 = 보라 이삭 → 파란색, 수련 = 분홍 → 블러셔. 상점 물건이라 판매가 0 그대로
    ("paint_blue", [_ing(1, "bog_flower")], "Workbench", 1),
    ("paint_blush", [_ing(1, "water_lily")], "Workbench", 1),
    # 8.16 제련소·제련·금속 가공 (서버 RECIPES 와 같다). 고급 곡괭이는 고급 작업대 — 서버 advWorkbench 가 막는다.
    ("smelter", [_ing(4, "plank"), _ing(6, "iron_chunk")], "Workbench", 1),
    ("coal_ingot", [_ing(2, "coal_chunk")], "Smelter", 1),
] + [(metal + "_ingot", [_ing(2, metal + "_chunk"), _ing(1, "coal_chunk")], "Smelter", 1)
     for metal in ("copper", "iron", "titanium", "silver", "gold")] + [
    ("metal_parts", [_ing(1, "iron_ingot")], "Workbench", 2),
    ("sheet_metal", [_ing(2, "iron_ingot")], "Workbench", 1),
    ("pickaxe_advanced", [_ing(3, "iron_ingot"), _ing(2, "plank")], "Workbench", 1),
    # 8.11 「집 2층·전력」 — 폴리머만 작업대, 나머지는 고급 작업대(서버 advWorkbench). 재료가 결과보다 앞(판매가 한 바퀴).
    ("polymer", [_ing(3, "resin")], "Workbench", 1),
    ("circuit_board", [_ing(2, "copper_ingot"), _ing(1, "polymer")], "Workbench", 1),
    ("electric_motor", [_ing(2, "copper_ingot"), _ing(1, "iron_ingot"), _ing(1, "circuit_board")], "Workbench", 1),
    ("solar_kit", [_ing(1, "circuit_board"), _ing(1, "sheet_metal"), _ing(1, "polymer")], "Workbench", 1),
    ("battery", [_ing(1, "copper_ingot"), _ing(1, "coal_ingot"), _ing(1, "polymer")], "Workbench", 1),
    ("stair_kit", [_ing(6, "plank"), _ing(2, "metal_parts")], "Workbench", 1),    # 8.11 집 2층 재료 · 고급 작업대 (M20-03)
    ("lamp_electric", [_ing(1, "light_bulb"), _ing(1, "metal_parts"), _ing(1, "plank")], "Workbench", 1),  # 8.11 전등 (M20-04a)
    # 8.10 전기 보트 모터 (M33-03) — 원본 VN463 고급 작업대 부착. 상자 2% 의 둘째 길 · 고급 작업대(서버 advWorkbench)
    ("boat_motor_electric", [_ing(1, "boat_motor"), _ing(1, "electric_motor")], "Workbench", 1),
    # 8.11 연료 발전기 (M108-07b) — 고급 작업대(서버 advWorkbench). 소형 전동기 둘째 쓰는 곳
    ("generator", [_ing(1, "electric_motor"), _ing(2, "sheet_metal"), _ing(2, "metal_parts")], "Workbench", 1),
    # 8.11 「🟡 전기 가구」 (M108-08) — 고급 작업대(서버 advWorkbench). 소모 큰 것이 재료 비쌈
    ("fridge", [_ing(1, "electric_motor"), _ing(3, "sheet_metal"), _ing(2, "polymer")], "Workbench", 1),
    ("fan_stand", [_ing(1, "electric_motor"), _ing(2, "metal_parts"), _ing(1, "polymer")], "Workbench", 1),
    ("radio", [_ing(1, "circuit_board"), _ing(2, "plank"), _ing(1, "metal_parts")], "Workbench", 1),
    ("neon_sign", [_ing(2, "light_bulb"), _ing(1, "circuit_board"), _ing(2, "polymer")], "Workbench", 1),
    # 8.6-c·8.10 현대 소총 (M39-08a) — 고급 작업대(서버 advWorkbench). 금속 부품이 앞에 있어야 판매가가 한 바퀴에 선다
    ("rifle_modern", [_ing(4, "iron_ingot"), _ing(2, "metal_parts"), _ing(2, "plank")], "Workbench", 1),
] + [(seed, [_ing(4, item)], "Workbench", 1) for item, seed, _, _, _, _ in CROPS] + [
    (dish, ingredients, "Fire", 1) for dish, _, _, ingredients in COOKING_DISHES]

# DESIGN 8.14 "사는 목록". (id, 가격, 개수) — 지렁이 팩은 8.13 "×3".
SHOP_ITEMS = [
    ("rod_basic", 3, 1), ("rod_telescopic", 10, 1), ("worm_bait", 1, 3), ("fisher_hat", 15, 1),
    ("hatchet", 5, 1), ("tent", 35, 1), ("hiking_backpack", 40, 1), ("garden_brazier", 15, 1),
    ("lifeboat", 50, 1), ("club", 5, 1), ("gather_basket", 5, 1),                      # M32-03 🟡 채집 바구니 = 손도끼 값
    ("salt", 1, 1), ("black_pepper", 1, 1), ("dill", 1, 1), ("empty_bottle", 1, 1),     # 13.6 잡화점 각 1
    ("farmer_hat", 15, 1), ("hunter_hat", 15, 1), ("lumberjack_hat", 15, 1), ("cook_hat", 5, 1),   # 13.5 모자 7
    ("traveler_hat", 15, 1), ("merchant_hat", 15, 1), ("runner_hat", 15, 1), ("mining_hat", 15, 1),
    ("chainsaw", 25, 1), ("machete", 10, 1), ("binoculars", 10, 1), ("satellite_communicator", 10, 1),  # 13.5 도구 4
    ("heat_pack", 5, 1), ("hot_soda", 3, 1), ("snackbar", 3, 1),                          # 13.6 버프 소모품 3
    ("flannel_shirt", 5, 1), ("work_pants", 5, 1),
    ("pickaxe_basic", 10, 1), ("pickaxe_electric", 150, 1),                               # 8.16 곡괭이                                        # 13절 「셔츠·바지」 각 5
    ("solar_kit", 30, 1), ("battery", 15, 1), ("light_bulb", 5, 1),                        # 8.11 「집 2층·전력」 잡화점
    ("crate_key", 50, 1),                                                                  # 10.8 열쇠 코인 값
    ("pistol_semi", 30, 1), ("ammo", 5, 25),                                               # 8.6-c 사냥 총 · 탄약 25발 묶음
    ("ammo_rifle", 10, 20), ("shotgun", 60, 1), ("ammo_shotgun", 8, 16),   # 8.6-c 소총탄 20발·산탄총·산탄 16발 묶음
    ("crossbow", 45, 1), ("arrow", 6, 15),         # 8.6-c 석궁·화살 15발 묶음
    ("pistol_auto", 50, 1), ("smg", 90, 1),        # 8.6-c 연발 권총·기관단총
    ("rifle_burst", 110, 1), ("rifle_semi", 75, 1),  # 8.6-c 점사 소총·반자동 소총
    ("shotgun_auto", 100, 1), ("shotgun_short", 55, 1),
    ("bazooka", 300, 1), ("rocket", 20, 4),        # 8.6-c 바주카·로켓 4발 묶음
    ("mg", 150, 1)  # 기관총 · 8.6-c 자동 산탄총·짧은 산탄총
# DESIGN 13.8 페인트 7 — 잡화점 각 2.
] + [(item_id, 2, 1) for item_id, _, _ in PAINT_ITEMS] + [
# DESIGN 13.3 「씨앗 파는 곳」 — 작물 씨앗 27(새 24 + 당근·감자·밀 루프#1238), 값 = 작물 판매가(수확 코인 ÷ 5), 한 번에 1개(🟡).
    (seed, max(1, coins // 5), 1) for _, seed, _, coins, _, _ in CROPS]

# DESIGN 8.14 판매가 — 사냥 트로피 중 "좋은 품질" 접두어 (8.7 표, 1:7 은 원본 뿔 320:2250).
FINE_PREFIXES = ("pristine_",)
TROPHY_SUFFIXES = ("_horn", "_antler", "_tooth", "_skull", "_paw", "_tail", "feather")


def _fish_sell_price(minutes):
    """8.14 물고기 = 낚시 시간(분) ÷ 15. 나머지 처리는 설계서에 없다 → 🟡 반올림(농어 2·악상어 12 는 그대로)."""
    return (minutes + 7) // 15


def extend_with_crafting():
    """8.9~8.14 새 아이템·레시피·상점 행을 만들고, 모든 아이템 행의 태그·등급·판매가를 채운다."""
    fish_minutes = {row["ProducedItemID"].split(".", 1)[1]: row["CycleMinutes"] for row in FISHING_ROWS}

    for fish_id, minutes in fish_minutes.items():
        next(row for row in FISHING_ROWS if row["ProducedItemID"] == "item." + fish_id)["Habitat"] = (
            "Freshwater" if fish_id in FRESHWATER_FISH
            else "Swamp" if fish_id in SWAMP_FISH
            else "DeepSea" if minutes >= DEEP_SEA_MIN_MINUTES else "Sea")

    for item_id, name, item_type, extra in CRAFTING_ITEMS:
        _add_item_row(item_id, name, item_type)
        ITEM_ROWS[-1].update(extra)
        if extra.get("EquipSlot") == "Head":
            ITEM_ROWS[-1].update(_hat_columns(item_id))
        if item_id in PLACED_MESH_WIDTH_CM:
            ITEM_ROWS[-1].update(_placed_mesh_columns(item_id))

    for item, seed, _, _, crop_name, item_name in CROPS:
        _add_item_row(item, item_name, "Material")    # 산딸기·나무·당근·감자·밀은 이미 있어 건너뛴다
        _add_item_row(seed, "{} 씨앗".format(crop_name), "Material")

    items = {row["ItemID"].split(".", 1)[1]: row for row in ITEM_ROWS}
    for item_id, extra in CROP_EXTRAS.items():
        items[item_id].update(extra)
    for fish_id, heal in GRILLED_FISH_HEAL.items():
        _add_item_row("grilled_" + fish_id, "{} 구이".format(items[fish_id]["DisplayName"]), "Food")
        ITEM_ROWS[-1]["HealAmount"] = heal
        RECIPES.append(("grilled_" + fish_id, [_ing(1, fish_id)], "Fire", 1))

    for result_id, result_name in BLUEPRINT_RESULTS:
        _add_item_row("blueprint_" + result_id, "{} 설계도".format(result_name), "Material")

    for dish, name, heal, _ in COOKING_DISHES:
        _add_item_row(dish, name, "Food")
        ITEM_ROWS[-1].update({"HealAmount": heal, "CraftTag": "Jam"} if name.endswith("잼") else {"HealAmount": heal})  # 잼 크루아상 제외
    # 버섯 행(13.4)도 판매가 계산 전에 — 요리 재료(살구버섯·`Mushroom`)가 값을 찾는다.
    add_gather_pile_items()

    items = {row["ItemID"].split(".", 1)[1]: row for row in ITEM_ROWS}
    for item_id, (kind, value) in ITEM_BUFFS.items():
        items[item_id].update({"BuffKind": kind, "BuffValue": value, "BuffSeconds": BUFF_SECONDS})

    # 태그·등급·판매가 (8.10·8.12·8.14). 가공품은 레시피 재료 값에서 계산하므로 원재료부터 채운다.
    for item_id, row in items.items():
        tag, price, grade = "None", 0, "Common"
        if item_id in fish_minutes:
            tag, price = "Fish", _fish_sell_price(fish_minutes[item_id])
            # 🟡 등급 규칙: 설계서는 4단만 정했다 → 낚시 시간 60·120·180분 경계(8.4 티어)로 가른다.
            minutes = fish_minutes[item_id]
            grade = ("Legendary" if minutes >= 180 else "Epic" if minutes >= 120
                     else "Rare" if minutes >= 60 else "Common")
        elif item_id in ("wood", "cedar", "berry"):
            tag = {"wood": "Wood", "cedar": "Cedar", "berry": "Berry"}[item_id]    # 8.14 나무·산딸기 0 (향나무 🟡)
        elif item_id.endswith("_meat") and row["ItemType"] == "Material":
            tag, price = "Meat", 1
        elif item_id.endswith(("_hide", "_fur")) or item_id == "wool":
            tag, price = "Hide", 2                                                  # 가죽·털 2 (양털 🟡 털)
        elif item_id.endswith(TROPHY_SUFFIXES) or item_id == "shed_antler":
            fine = item_id.startswith(FINE_PREFIXES)
            tag = "Feather" if item_id.endswith("feather") else "None"
            price, grade = (20, "Rare") if fine else (3, "Common")                # 🟡 좋은 품질 = 레어
        elif item_id in CROP_HARVEST_COINS:
            price = max(1, CROP_HARVEST_COINS[item_id] // 5)
        elif item_id in FIXED_SELL_PRICES:
            price = FIXED_SELL_PRICES[item_id]                                      # 8.11 전자 재료 (서버 FIXED_SELL_PRICE)
        elif item_id in ORE_CHUNK_PRICES:
            price = ORE_CHUNK_PRICES[item_id]                                       # 8.16 덩어리 (서버 ORE_CHUNK_PRICE)
        elif item_id in GATHER_SELL_PRICES:
            price = GATHER_SELL_PRICES[item_id]                                     # 13.4 늪 채집물 (서버 GATHER_SELL_PRICE)
        elif item_id in ("truffle", "milk", "honey", "egg"):
            price = 2                                                               # 🟡 목장 생산물 = 목장 코인 10 ÷ 5 (작물 규칙)
        elif row["ItemType"] == "Pet":
            price = 10                                                              # 🟡 펫 = 목장 한 주기 코인 10 (8.14 「펫 판매가」, 서버 SELL_PRICE.pet)
        row["CraftTag"] = row.get("CraftTag", tag)
        row["SellPrice"] = price
        row["Grade"] = grade

    def ingredient_value(ing):
        if ing["Tag"] != "None":
            # 🟡 태그 재료 값 = 그 태그 아이템 중 가장 싼 판매가 (아무 것이나 넣을 수 있어서)
            return min(r["SellPrice"] for r in items.values() if r["CraftTag"] == ing["Tag"])
        return items[ing["ItemID"].split(".", 1)[1]]["SellPrice"]

    # 가공품 = 재료 값 합 + 1, 합이 0 이면 0(못 판다) (8.14 — 공짜 채집물로 코인이 새지 않게).
    # 재료가 결과보다 뒤에 있는 줄이 생겨도 맞게 값이 안 바뀔 때까지 되풀이한다 — 서버 `sellPriceOf` 는 재귀.
    # 상점 물건은 0 그대로 — 사서 바로 팔기를 막는다(서버 `sellPriceOf`).
    shop_ids = {item_id for item_id, _, _ in SHOP_ITEMS}
    for _ in range(len(RECIPES)):
        changed = False
        for result_id, ingredients, _, count in RECIPES:
            if result_id in shop_ids or result_id in FIXED_SELL_PRICES:
                continue
            total = sum(ingredient_value(i) * i["Count"] for i in ingredients)
            # 여럿 나오면 한 개 값 = (합 + 1) ÷ 개수 내림 (8.16 금속 부품 3 — 서버 `sellPriceOf` 와 같은 식)
            price = max(1, (total + 1) // count) if total > 0 else 0
            changed = changed or items[result_id]["SellPrice"] != price
            items[result_id]["SellPrice"] = price
        if not changed:
            break

    fill_item_icons()


extend_with_crafting()
# 판매가·태그를 채운 뒤 — 버섯 행(13.4)은 extend_with_crafting 안에서 먼저 선다.
add_mount_items()
add_treasure_items()
add_sweet_potato_items()
add_fruit_items()
add_paint_items()
add_crate_items()
add_ranch_cosmetic_items()
add_gacha_pet_cosmetic_items()
add_gacha_ranch_deco_items()
add_cosmetic_hats()
add_cosmetic_faces()
add_cosmetic_tops()
add_cosmetic_bottoms()
add_cosmetic_shoes()
add_cosmetic_gloves()
add_cosmetic_backpacks()
add_story_reward_items()
# 위 셋이 뒤에 붙인 탈것·보물·페인트 행도 아이콘을 받게 한 번 더 (M13-30 — 빠지면 21행이 빈 칸이었다).
fill_item_icons()

RECIPE_TABLE_FOLDER = "/Game/Game/Data/Crafting"
RECIPE_TABLE_NAME = "DT_RecipeTable"
RECIPE_ROW_STRUCT = "/Script/Snarehaven.RecipeRow"
RECIPE_ROWS = [{
    "Name": "Recipe_" + _item_row_name(result_id)[len("Item_"):],
    "RecipeID": "recipe." + result_id,
    "Ingredients": ingredients,
    "Station": station,
    "RequiredBlueprintID": ("item.blueprint_" + result_id
                            if result_id in dict(BLUEPRINT_RESULTS) else "None"),
    "ResultItemID": "item." + result_id,
    "ResultCount": count,
} for result_id, ingredients, station, count in RECIPES]

SHOP_TABLE_FOLDER = "/Game/Game/Data/Shop"
SHOP_TABLE_NAME = "DT_ShopTable"
SHOP_ROW_STRUCT = "/Script/Snarehaven.ShopRow"
SHOP_ROWS = [{
    "Name": "Shop_" + _item_row_name(item_id)[len("Item_"):],
    "ItemID": "item." + item_id,
    "Price": price,
    "Count": count,
} for item_id, price, count in SHOP_ITEMS]


# 올가미 기계 등급 4행 + 비용·천장 1행 (ROADMAP M7-01).
# **이 스크립트가 만든 숫자는 0개다** — 확률 70/22/6.5/1.5 · 1회 젬 100 · 1회 코인 500 · 10연차 젬 900 ·
# 천장 50 은 전부 DESIGN 10.2 "뽑기 규칙 (확정 수치)" 이고, `docs/GACHA_DISCLOSURE.md` 가 그 표를
# 옮겨 적은 공개 문서다. **수치를 바꿀 때는 그 문서를 먼저 고치고 여기를 맞춘다** — 둘이 어긋난 채
# 화면에 뜨면 확률 오표시라 게임산업법상 2년 이하 징역 또는 2000만원 이하 벌금이다 (DESIGN 10.4).
# 등급 이름(커먼·레어·에픽·레전더리)도 DESIGN 10.2 표기 그대로다.
#
# 아이템별 개별 확률은 **여기 적지 않는다** — 뽑기 풀(`DT_GachaPool`, M7-07)은 아래
# `read_gacha_pool_rows` 가 공개 문서 "아이템별 확률" 표(감독 확정 2026-09-22)를 그대로 읽어 채운다.
GACHA_RATE_ROWS = [
    {"Name": "Gacha_Common", "Grade": "Common", "DisplayName": "커먼", "ProbabilityPct": 70.0},
    {"Name": "Gacha_Rare", "Grade": "Rare", "DisplayName": "레어", "ProbabilityPct": 22.0},
    {"Name": "Gacha_Epic", "Grade": "Epic", "DisplayName": "에픽", "ProbabilityPct": 6.5},
    {"Name": "Gacha_Legendary", "Grade": "Legendary", "DisplayName": "레전더리", "ProbabilityPct": 1.5},
]

# 비용·천장은 등급마다 다른 값이 아니라 기계 전체에 하나씩이라 표를 갈랐다 — 등급 행에 끼우면
# 같은 숫자가 네 번 적히고 넷 중 하나만 고쳐도 아무도 모른다. 그래서 행이 하나뿐인 표다.
GACHA_CONFIG_ROWS = [
    {
        "Name": "Gacha_Cost",
        "SinglePullGems": 100,
        "SinglePullCoins": 500,
        "TenPullGems": 900,
        "PityPullCount": 50,
    },
]

# 뽑기 풀 19행 (ROADMAP M7-07). **원본은 `docs/GACHA_DISCLOSURE.md` "아이템별 확률" 표 하나다** —
# 여기에 아이템·확률을 다시 적으면 문서와 표가 두 곳에 살고, 어긋난 채 화면에 뜨면 오표시다 (DESIGN 10.4).
# 서버 풀(`cloudscript_main.js` `GACHA_POOL`)과의 맞대기는 2층 `Snarehaven.Gacha.Pool.MatchesDisclosureAndServer`
# 와 `test_cloudscript.js` "뽑기 풀이 GACHA_DISCLOSURE ..." 가 본다.
GACHA_GRADE_BY_LABEL = {"커먼": "Common", "레어": "Rare", "에픽": "Epic", "레전더리": "Legendary"}


def read_gacha_pool_rows():
    doc_path = os.path.join(unreal.Paths.project_dir(), "docs", "GACHA_DISCLOSURE.md")
    rows = []
    with open(doc_path, encoding="utf-8") as f:
        for line in f:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) != 5 or not cells[0].startswith("gacha."):
                continue
            item_id, name, grade, category, pct = cells
            match = re.fullmatch(r"([0-9.]+)%", pct)
            if grade not in GACHA_GRADE_BY_LABEL or match is None:
                raise RuntimeError("공개 문서 줄을 못 읽었다: {}".format(line.strip()))
            rows.append({
                "Name": "Pool_" + item_id.split(".", 1)[1],
                "ItemID": item_id,
                "DisplayName": name,
                "Grade": GACHA_GRADE_BY_LABEL[grade],
                "Category": category,
                "ProbabilityPct": float(match.group(1)),
                "SellPrice": 0,
            })
    if not rows:
        raise RuntimeError("공개 문서에서 아이템 줄을 하나도 못 찾았다: {}".format(doc_path))
    # 희귀 소재 개당 판매가 = DESIGN 10.2 「희귀 소재 판매가」 줄(문서 이름 → 공개 문서 ID). 서버 `FIXED_SELL_PRICE` 와의
    # 맞대기는 `test_cloudscript.js` "판매가 — DT_GachaPool …" 가 본다. 잡화점 팔기 탭이 이 값을 보여 준다(루프#539).
    prices = read_gacha_material_sell_prices()
    for row in rows:
        if row["Category"] == "희귀 소재":
            base_name = re.sub(r" x\d+$", "", row["DisplayName"])
            if base_name not in prices:
                raise RuntimeError("DESIGN 10.2 희귀 소재 판매가 줄에 없다: {}".format(base_name))
            row["SellPrice"] = prices[base_name]
    return rows


def read_gacha_material_sell_prices():
    """DESIGN 10.2 「희귀 소재 판매가」 줄의 `이름 **값**` 쌍 → {이름: 개당 코인}."""
    design_path = os.path.join(unreal.Paths.project_dir(), "docs", "DESIGN.md")
    with open(design_path, encoding="utf-8") as f:
        lines = [line for line in f if line.startswith("| 희귀 소재 판매가 |")]
    if len(lines) != 1:
        raise RuntimeError("DESIGN 10.2 희귀 소재 판매가 줄이 하나가 아니다: {}개".format(len(lines)))
    value_cell = lines[0].split("|")[2].split("(")[0]
    prices = {}
    for part in value_cell.split("·"):
        match = re.search(r"([^—*]+?)\s*\*\*([\d,]+)\*\*", part)
        if match:
            prices[match.group(1).strip()] = int(match.group(2).replace(",", ""))
    return prices


# 존 2행 (ROADMAP M6-01). **이 스크립트가 만든 숫자는 여기서도 0개다 — 이름뿐이다.**
# 존 종류와 순서는 DESIGN 1.3("사냥존(기존 L_Hunt) -> 거점존(기존 L_Ranch)")이고, 표기 `사냥존`·`거점존`도
# 그 절 그대로다. 맵 이름은 `Content/Game/Maps/` 에 실제로 있는 둘이다 (`docs/WORLD_ZONES.md` 표 A).
#
# **로비 `L_Title`·부팅 `L_Boot` 은 행이 아니다** — 존이 아니라 흐름 화면이고, 포탈로 걸어 들어가는 곳이
# 아니라 버튼·자동 진입으로 오간다 (표 A·B). 존 표에 넣으면 포탈이 로비로 열릴 수 있는 자리가 생긴다.
#
# 이어지는 존은 **사냥존 <-> 거점존 양방향**이다 — M6-03 이 두 맵에 포탈을 하나씩 놓아 왕복을 만든다.
# 지금 `L_Ranch` 로 들어가는 길은 게임 안에 0줄이라(표 A), 이 표가 그 길의 첫 근거가 된다.
ZONE_ROWS = [
    {
        "Name": "Zone_Hunt",
        "ZoneID": "zone.hunt",
        "LevelName": "L_Hunt",
        "DisplayName": "사냥존",
        "ConnectedZoneIDs": ["zone.ranch"],
    },
    {
        "Name": "Zone_Ranch",
        "ZoneID": "zone.ranch",
        "LevelName": "L_Ranch",
        "DisplayName": "거점존",
        "ConnectedZoneIDs": ["zone.hunt"],
    },
] + [
    # M12-00 — `L_Hunt` **안의 칸** 넷. 포탈로 가는 맵이 아니라(ConnectedZoneIDs 없음) 한 맵을 사분면으로 나눈 자리다.
    # **숫자는 DESIGN 1.3 "맵 안 존" 표가 원본이고 여기는 사본이다** — 한쪽만 고치면
    # 2층 `Snarehaven.Zone.Region.MatchesDesign` 이 운다. 이름은 8.7 종 표 "서식 존" 열(평원·설원·숲)과 12절(늪)뿐.
    {
        "Name": row_name,
        "ZoneID": zone_id,
        "LevelName": "L_Hunt",
        "DisplayName": display,
        "ConnectedZoneIDs": [],
        "RegionCenterCm": {"X": center[0], "Y": center[1]},
        "RegionHalfExtentCm": half,
        # 새 지도(2026-09-25) — 안/밖은 이 코드로 가른다(`islands.py` ZONE_CODES). 정사각형은 존 칸을 감싸는 틀일 뿐
        "FieldCode": code,
        "HealthDrainPerSecond": drain,
        "bBuildBlocked": build_blocked,
        "bChargesEntryFee": entry_fee,
        # M12-03 미니맵 칸 색(sRGB). DESIGN 1.3 "미니맵" 표가 원본 — 한쪽만 고치면
        # 2층 `Snarehaven.Ui.Minimap.DrawsZoneCells` 가 운다.
        "MinimapColor": {"R": int(color[0:2], 16), "G": int(color[2:4], 16), "B": int(color[4:6], 16), "A": 255},
    }
    for row_name, zone_id, display, center, half, code, drain, build_blocked, entry_fee, color in [
        ("Zone_Plains", "zone.plains", "평원", (-9200.0, -5600.0), 38800.0, 1, 0.0, False, False, "2B763E"),
        ("Zone_Snow", "zone.snow", "설원", (22400.0, -7400.0), 44200.0, 2, 2.0, True, False, "CBD5E2"),
        ("Zone_Forest", "zone.forest", "숲(가을섬 성격)", (-9200.0, 31400.0), 23600.0, 3, 0.0, False, False, "ECCA65"),
        ("Zone_Swamp", "zone.swamp", "늪", (-22600.0, -25600.0), 17200.0, 4, 0.0, True, False, "788D1D"),
    ]
]


# 위험구역 1행 (ROADMAP M6-04a). **숫자는 DESIGN 8.1 표가 원본이고 여기는 사본이다** —
# 반지름 500cm · 동물 5마리 · 재생성 300초 (감독 결정 2026-09-22, INBOX 지시 07:25).
# 한쪽만 고치면 2층 `Snarehaven.DangerZone.Table.MatchesDesign` 이 운다.
#
# 구역은 **사냥존 안 한 곳**이다 — 새 맵을 만들지 않는다(결정 005-3 · DESIGN 1.3). `ZoneID` 는
# `DT_ZoneTable` 의 `zone.hunt`, 세울 종은 `DT_AnimalTable` 의 `Animal_Wolf` 다.
# **종 선택은 숫자가 아니라 배치다**(🟡) — DESIGN 8.1 이 수·크기·주기만 정했고 종은 안 정했다.
# 돼지였으나 자리표시 3종이 기본 플레이에서 빠지면서(사용자 지시 2026-09-23 16:55 ②) 바르코 12종 중
# 하나로 옮겼다 — **늑대**는 DESIGN 8.7 행동 열의 선공 3종 중 하나라 "위험구역" 이라는 이름과 맞는다.
DANGER_ZONE_ROWS = [
    {
        "Name": "Danger_HuntField",
        "ZoneID": "zone.snow",   # #0204 선공 늑대는 서식 칸(설원)에만 — 평원 시작점 코앞에 서던 결함 (DESIGN 8.7)
        "AnimalRowName": "Animal_Wolf",
        "RadiusCm": 500.0,
        "AnimalCount": 5,
        "RespawnSeconds": 300,
        "LifetimeSeconds": 0,
        "AppearIntervalSeconds": 0,
        # M21-07f 주간 우두머리 — DESIGN 10.6 「우두머리 HP」 ×5 · 「우두머리 자리·종」 몸 1.5배 (상시 구역에만)
        "BossHPMultiplier": 5.0,
        "BossBodyScale": 1.5,
        "BossHatChance": 0.1,   # M21-07g 고지용 사본 — 굴림은 서버 BOSS_HAT_CHANCE (DESIGN 10.6 「우두머리 모자」)
    },
    # M12-07 랜덤 월드 이벤트(헬기 추락형 한정 루팅) — **같은 위험구역 행에 수명·등장 주기만 더한다.**
    # 숫자는 DESIGN 8.1b 표가 원본이고 전부 위 8.1 행과의 비율이다(근거는 그 표). 2층
    # `Snarehaven.WorldEvent.Table.MatchesDesign` 이 대조한다. 자리는 `L_Hunt` 성격 존 칸 중 무작위라 좌표가 없다.
    {
        "Name": "Event_CrashSite",
        "ZoneID": "zone.hunt",
        "AnimalRowName": "Animal_Wolf",
        "RadiusCm": 500.0,
        "AnimalCount": 10,
        "RespawnSeconds": 300,
        "LifetimeSeconds": 600,
        "AppearIntervalSeconds": 1800,
        "BossHPMultiplier": 0.0,   # 이벤트 구역엔 우두머리가 없다 (DESIGN 10.6 「상시 위험구역」)
        "BossBodyScale": 0.0,
        "BossHatChance": 0.0,
    },
]


# 사냥존 분산 스폰 1행 (INBOX 지시 ★4, 감독 2026-09-23). **숫자는 DESIGN 3.1
# "동물 맵 전역 분산 스폰" 표가 원본이고 여기는 사본이다** — 16마리 · 간격 1200cm ·
# 시작점 금지 1500cm · 재등장 90초 · 영역 반변 2500cm(`L_Hunt` 바닥 실측).
# 한쪽만 고치면 2층 `Snarehaven.AnimalSpread.Table.MatchesDesign` 이 운다.
#
# 종은 이 행에 안 적는다 — 스포너가 `DT_AnimalTable` 의 행을 차례로 돌려 쓴다(DESIGN 3.1 "종 배분").
# 위험구역 행이 `AnimalRowName` 으로 한 종을 고르는 것과 갈리는 자리다.
#
# M12-00: 존마다 한 행 — 스포너가 존 표의 칸 중심·반변에 맞춰 선다(`AAnimalSpreadActor::FitToZone`).
# `Spread_HuntField` 는 맵에 놓인 스포너가 이 이름을 적고 있어 이름은 두고 존만 평원으로 옮긴다.
# 늪은 서식 종이 없어(8.7) 행이 없다. 세 행의 숫자는 같은 3.1 값이다 — 존 넓이가 옛 사냥터와 같아서다(1.3).
ANIMAL_SPREAD_ROWS = [
    {
        "Name": row_name,
        "ZoneID": zone_id,
        "AreaHalfExtentCm": 6000.0,
        "AnimalCount": 16,
        "MinSpacingCm": 1200.0,
        "PlayerStartClearCm": 1500.0,
        "RespawnSeconds": 90,
        # M21-01 시작점 순한 동물 — 시작점(헤이븐 거점)이 있는 평원만 2, 거리 800cm (DESIGN 3.1)
        "StarterAnimalCount": starters,
        "StarterRingCm": 800.0,
    }
    for row_name, zone_id, starters in [
        ("Spread_HuntField", "zone.plains", 2),
        ("Spread_Snow", "zone.snow", 0),
        ("Spread_Forest", "zone.forest", 0),
    ]
]


# 분석 이벤트 9행 (ROADMAP M8-01). **이 스크립트가 만든 숫자는 여기서도 0개다 — 이름뿐이다.**
# 원본은 `docs/ANALYTICS.md` 표 A 이고 이 목록은 그 사본이다 — 한쪽만 고치면 2층
# `Snarehaven.Analytics.EventTable.MatchesDocument` 가 운다 (M7-02 확률 공개 검사와 같은 방식).
#
# `Sender` 는 **누가 보내나** 다 (`EAnalyticsEventSender`). 같은 사건을 호스트와 게스트가 같이 보내면
# 한 번 일어난 일이 두 번 세어진다 — 그래서 사냥·포획·존 이동은 `HostAuthority` 다 (DESIGN 4.1 권위 2단).
# `Requester` 는 "눌렀다" 까지만 보낸다는 뜻이고, 몇 개를 받았는지·코인이 얼마인지는 백엔드가 쓴다
# (`docs/ANALYTICS.md` 표 C — 그 값들을 클라이언트가 칸에 적으면 반려다, CLAUDE.md 8).
#
# **지금 이 이름들을 보내는 코드는 0줄이다** — 통로는 M8-02, 부르는 자리는 M8-03~06 이다.
ANALYTICS_ROWS = [
    {
        "Name": "Event_AnimalKilled",
        "EventID": "animal_killed",
        "Sender": "HostAuthority",
        "PayloadKeys": ["pet_item_id", "drop_count", "zone_id"],
    },
    {
        "Name": "Event_AnimalCaptureSucceeded",
        "EventID": "animal_capture_succeeded",
        "Sender": "HostAuthority",
        "PayloadKeys": ["pet_item_id", "zone_id"],
    },
    {
        "Name": "Event_AnimalCaptureFailed",
        "EventID": "animal_capture_failed",
        "Sender": "HostAuthority",
        "PayloadKeys": ["pet_item_id", "zone_id", "reason"],
    },
    {
        "Name": "Event_RanchPetRegisterRequested",
        "EventID": "ranch_pet_register_requested",
        "Sender": "Requester",
        # 목장에 실리는 값은 종 ID(`animal.pig` — `DT_AnimalTable`)라 칸 이름도 `animal_id` 다.
        # 코어 루프 세 줄의 `pet_item_id`(`item.pet_pig`)와는 값 종류가 다르다 (루프#112).
        "PayloadKeys": ["slot_index", "animal_id"],
    },
    {
        "Name": "Event_RanchHarvestRequested",
        "EventID": "ranch_harvest_requested",
        "Sender": "Requester",
        "PayloadKeys": ["slot_index"],
    },
    {
        "Name": "Event_GachaPullRequested",
        "EventID": "gacha_pull_requested",
        "Sender": "Requester",
        "PayloadKeys": ["pull_count", "currency"],
    },
    {
        "Name": "Event_CurrencyBalanceChanged",
        "EventID": "currency_balance_changed",
        "Sender": "Observer",
        "PayloadKeys": ["currency", "new_balance", "balance_source"],
    },
    {
        "Name": "Event_SessionStarted",
        "EventID": "session_started",
        "Sender": "Client",
        "PayloadKeys": ["is_new_account"],
    },
    {
        "Name": "Event_ZoneTravelled",
        "EventID": "zone_travelled",
        "Sender": "HostAuthority",
        "PayloadKeys": ["from_zone_id", "to_zone_id"],
    },
    # M11-03. **`HostAuthority` 가 아니라 `Client` 다** - 생존 값(`UPlayerVitalsComponent`)은 복제되지
    # 않아 각자 자기 폰에서 죽고, 죽은 본인의 클라이언트 하나만 이 줄을 보낸다. 한 사건이 두 번
    # 세어질 자리가 없다. 싣는 값은 죽은 자리의 존과 **버린 지급 요청 줄 수**뿐이고,
    # 실제 대기열 폐기 원장은 백엔드가 쓴다 (docs/ANALYTICS.md 표 C).
    {
        "Name": "Event_PlayerDied",
        "EventID": "player_died",
        "Sender": "Client",
        "PayloadKeys": ["zone_id", "lost_pending_count"],
    },
]


# 플레이어 체력·스태미나 1행 (ROADMAP M11-01·M5-01e2). **이 스크립트가 만든 숫자는 0개다** —
# 열두 칸 전부 DESIGN 3.1 "플레이어 체력·스태미나" 표(루프#142 확정)와 「사망 시 잃는 것」·
# 「쓰러짐 → 부활」 줄의 사본이고,
# 2층 `Snarehaven.PlayerVitals.Table.MatchesDesign` 이 그 표를 읽어 여기와 맞댄다 — 한쪽만 고치면 운다.
# 행이 하나뿐인 것은 플레이어가 한 종류뿐이어서다 (종족·직업이 생기면 행이 는다, 코드는 그대로).
PLAYER_VITALS_ROWS = [
    {
        "Name": "Player",
        "MaxHealth": 100,
        "MaxStamina": 100,
        # 0 은 미정이 아니라 **확정값**이다 — 스태미나 = 수영 전용(DESIGN 3.1 "스태미나 = 수영 전용",
        # 사용자 방향 2026-09-23). 달리기·공격은 스태미나를 쓰지 않는다.
        "RunStaminaPerSecond": 0,
        "AttackStaminaCost": 0,
        "StaminaRegenPerSecond": 20,
        "StaminaRegenDelaySeconds": 0.6,
        "ExhaustionHealthPerSecond": 2,
        "HealthRegenPerSecond": 1,
        "HealthRegenStaminaThreshold": 50,
        # DESIGN 3.1 「사망 시 잃는 것」 표의 "부활 시 체력·스태미나 | 50 / 50" (M11-03).
        "RespawnHealth": 50,
        "RespawnStamina": 50,
        # DESIGN 3.1 「쓰러짐 → 부활」 줄의 "누운 채 1.0 초" (M5-01e2). 클립 길이는 여기 없다 —
        # 부활까지의 시간은 이 값 + `A_Player_Death` 의 길이이고, 길이는 클립에서 읽는다.
        "DeathLingerSeconds": 1.0,
    },
]


# 플레이어 근접 공격 1행 (ROADMAP M11-05). **이 스크립트가 만든 숫자는 0개다** — 네 칸 전부
# DESIGN 3.1 "확정 수치" 표의 사본이고, 2층 `Snarehaven.Combat.Melee.TableMatchesDesign` 이
# 그 표를 읽어 여기와 맞댄다 — 한쪽만 고치면 운다.
# 무기별 차등은 이 표가 아니라 `DT_ItemTable` 의 무기 칸이다 (DESIGN 8.6-b) — 여기는 **맨손 기준** 한 줄.
PLAYER_COMBAT_ROWS = [
    {
        "Name": "Player",
        "AttackDamage": 20,
        "AttackRangeCm": 150,
        "AttackIntervalSeconds": 0.6,
        # 클립을 갈아 끼우면 이 값도 같이 고친다 (재생 속도가 `클립 길이 ÷ 공격 간격` 이라
        # 타격 프레임의 실시각이 움직인다 — DESIGN 3.1 「공격 타격 지연」).
        "AttackHitDelaySeconds": 0.2,
        # M22 상인·의뢰인 대화 거리 — DESIGN 3.1 「상인 대화 거리」 (노점에 막혀 184cm 에서 멈춘다).
        "NpcTalkRangeCm": 250,
        # M21-02 때리는 손맛 — DESIGN 3.1 같은 이름 줄의 사본 (히트스톱·넉백·카메라 흔들림·흰 깜빡임·파편).
        "HitStopSeconds": 0.06,
        "HitStopTimeDilation": 0.05,
        "KnockbackCm": 120,
        "KnockbackSeconds": 0.15,
        "CameraShakeAmplitudeCm": 3,
        "CameraShakeSeconds": 0.12,
        "HitFlashSeconds": 0.08,
        "HitDebrisMin": 6,
        "HitDebrisMax": 10,
        # M21-06 같이 잡기 — DESIGN 3.1 「같이 잡기 드롭 창」 (막타 전 이 시간 안 명중자도 드롭을 각자 받는다).
        "ShareDropWindowSeconds": 10,
        # M21-09 올가미 조이기 링 — DESIGN 3.1 「올가미 링」 줄들 (링 길이 0 이면 즉시 포획으로 되돌아간다).
        "CaptureRingSeconds": 1.5,
        "CaptureWindowSeconds": 0.6,
        "CaptureWindowHardSeconds": 0.4,
        "CaptureHardMinHP": 140,
        "CaptureEscapeSeconds": 1.5,
        "CaptureRingToleranceSeconds": 0.25,
        # M18-11 맨손 — 잽·크로스 번갈아 (DESIGN 3.1 「무기별 몸 동작」). 든 자세는 비운다(루프#399, DESIGN 3.3 「맨손 든 자세」):
        # 주먹 가드(`A_Player_HoldFists`)가 상체를 늘 덮어 복싱 자세·고개 숙임(얼굴 -3~-18°)이 됐다 — 레퍼런스는 팔이 몸통 옆에 늘어진다.
        "HoldPoseAnim": "",
        "UseAnim": "/Game/Game/Characters/Player/Snare/A_Player_Jab.A_Player_Jab",
        "UseAnimAlt": "/Game/Game/Characters/Player/Snare/A_Player_Cross.A_Player_Cross",
    },
]


animal_table = ensure_table(ANIMAL_TABLE_FOLDER, ANIMAL_TABLE_NAME, ANIMAL_ROW_STRUCT)
fill_table(animal_table, ANIMAL_ROWS)

item_table = ensure_table(ITEM_TABLE_FOLDER, ITEM_TABLE_NAME, ITEM_ROW_STRUCT)
fill_table(item_table, ITEM_ROWS)

gatherable_table = ensure_table(GATHERABLE_TABLE_FOLDER, GATHERABLE_TABLE_NAME, GATHERABLE_ROW_STRUCT)
fill_table(gatherable_table, GATHERABLE_ROWS)

ranch_table = ensure_table(RANCH_TABLE_FOLDER, RANCH_TABLE_NAME, RANCH_ROW_STRUCT)
fill_table(ranch_table, RANCH_ROWS)

crop_table = ensure_table(CROP_TABLE_FOLDER, CROP_TABLE_NAME, CROP_ROW_STRUCT)
fill_table(crop_table, CROP_ROWS)

fishing_table = ensure_table(FISHING_TABLE_FOLDER, FISHING_TABLE_NAME, FISHING_ROW_STRUCT)
fill_table(fishing_table, FISHING_ROWS)

gacha_rate_table = ensure_table(GACHA_TABLE_FOLDER, GACHA_RATE_TABLE_NAME, GACHA_RATE_ROW_STRUCT)
fill_table(gacha_rate_table, GACHA_RATE_ROWS)

gacha_config_table = ensure_table(GACHA_TABLE_FOLDER, GACHA_CONFIG_TABLE_NAME, GACHA_CONFIG_ROW_STRUCT)
fill_table(gacha_config_table, GACHA_CONFIG_ROWS)

gacha_pool_rows = read_gacha_pool_rows()
gacha_pool_table = ensure_table(GACHA_TABLE_FOLDER, GACHA_POOL_TABLE_NAME, GACHA_POOL_ROW_STRUCT)
fill_table(gacha_pool_table, gacha_pool_rows)
unreal.log("[GachaPool] 행 {}개, 확률 합 {:.1f}%".format(
    len(gacha_pool_rows), sum(r["ProbabilityPct"] for r in gacha_pool_rows)))

zone_table = ensure_table(ZONE_TABLE_FOLDER, ZONE_TABLE_NAME, ZONE_ROW_STRUCT)
fill_table(zone_table, ZONE_ROWS)

danger_table = ensure_table(DANGER_TABLE_FOLDER, DANGER_TABLE_NAME, DANGER_ROW_STRUCT)
fill_table(danger_table, DANGER_ZONE_ROWS)

spread_table = ensure_table(SPREAD_TABLE_FOLDER, SPREAD_TABLE_NAME, SPREAD_ROW_STRUCT)
fill_table(spread_table, ANIMAL_SPREAD_ROWS)

analytics_table = ensure_table(ANALYTICS_TABLE_FOLDER, ANALYTICS_TABLE_NAME, ANALYTICS_ROW_STRUCT)
fill_table(analytics_table, ANALYTICS_ROWS)

vitals_table = ensure_table(VITALS_TABLE_FOLDER, VITALS_TABLE_NAME, VITALS_ROW_STRUCT)
fill_table(vitals_table, PLAYER_VITALS_ROWS)

combat_table = ensure_table(COMBAT_TABLE_FOLDER, COMBAT_TABLE_NAME, COMBAT_ROW_STRUCT)
fill_table(combat_table, PLAYER_COMBAT_ROWS)

recipe_table = ensure_table(RECIPE_TABLE_FOLDER, RECIPE_TABLE_NAME, RECIPE_ROW_STRUCT)
fill_table(recipe_table, RECIPE_ROWS)

shop_table = ensure_table(SHOP_TABLE_FOLDER, SHOP_TABLE_NAME, SHOP_ROW_STRUCT)
fill_table(shop_table, SHOP_ROWS)

CRATE_ROWS = [{
    "Name": "Crate_{}_{}".format(season.capitalize(), _item_row_name(item_id)[len("Item_"):]),
    "CrateItemID": "item." + crate_id,
    "Season": season,
    "ItemID": "item." + item_id,
    "Grade": grade,
    "ChancePct": pct,
    "KeyGemPrice": CRATE_KEY_GEM_PRICE,
} for crate_id, season, item_id, grade, pct in crate_pool()]
crate_table = ensure_table(SHOP_TABLE_FOLDER, "DT_CrateTable", "/Script/Snarehaven.CrateRow")
fill_table(crate_table, CRATE_ROWS)

# ── 거점 규칙 (DESIGN 8.10 불 옆 회복 · 8.11 텐트·땅·집·유지비·보관함) ──
# 판정은 CloudScript 몫이고, 클라이언트는 이 한 줄로 미리보기 색·버튼·불 옆 회복만 한다.
# 2층 `Snarehaven.Base.Rules.MatchesDesign` 가 DESIGN 8.10·8.11 을 읽어 여기와 맞댄다.
BASE_RULE_TABLE_FOLDER = "/Game/Game/Data/Balance"
BASE_RULE_TABLE_NAME = "DT_BaseRuleTable"
BASE_RULE_ROW_STRUCT = "/Script/Snarehaven.BaseRuleRow"
BASE_RULE_ROWS = [
    {
        "Name": "Base",
        "FireHealRadiusCm": 300,     # 8.10 모닥불·화로 300cm 안
        "FireHealPerSecond": 1,      # 8.10 체력 +1/초
        "LandCells": 16,             # 8.11 텐트 중심 16 × 16 칸
        "CellSizeCm": 100,           # 8.11 1칸 100cm
        "TentMax": 2,                # 8.11 한 사람 최대 2
        "FurnitureMax": 40,          # 8.11 땅 하나에 40
        "FurnitureMaxFloor2": 60,    # 8.11 「집 2층·전력」 2층 효과 40 → 60 (서버 FURNITURE_MAX_FLOOR2)
        "Floor2Cost": {              # 8.11 「집 2층·전력」 2층 재료 (서버 FLOOR2_COST)
            "item.stair_kit": 1,
            "item.plank": 40,
            "item.cedar_plank": 20,
            "item.sheet_metal": 4,
        },
        "SolarKitItemID": "item.solar_kit",     # 8.11 「집 2층·전력」 키트 공급 10
        "SolarKitPower": 10,
        "BatteryItemID": "item.battery",        # 8.11 배터리 +10 (키트 1개 이상일 때만)
        "BatteryPower": 10,
        "LampItemID": "item.lamp_electric",     # 8.11 전등 소모 5
        "LampPowerUse": 5,
        "LampLightCandela": 80,                 # 3.3 「밤 불빛」 전구 80cd (setup_night_lights.py 와 같은 값)
        "LampLightRadiusCm": 528,               # 3.3 반경 528cm = 캐릭터 176cm × 3
        "FlameLightCandela": 60,                # 8.11 「비전기 조명」 모닥불 120cd 의 절반
        "FlameLightRadiusCm": 352,              # 8.11 캐릭터 176cm × 2
        "LandLightMax": 8,                      # 8.11 땅 하나 점광 상한(전등 + 불꽃)
        "GeneratorItemID": "item.generator",    # 8.11 「연료 발전기 숫자」 (M108-07b, 서버 GENERATOR_*)
        "GeneratorPower": 100,                  # 연료가 남을 때 공급 100
        "GeneratorFuelMinutes": {"item.wood": 10, "item.coal_chunk": 30},
        "GeneratorFuelMaxHours": 12,
        "UpgradeDeadlineHours": 24,  # 8.11 24시간 안에
        "UpgradeWoodCount": 150,     # 8.11 나무 150
        "UpgradeCedarCount": 50,     # 8.11 향나무 50
        "HouseColorCount": 5,        # 8.11 5가지 중 고름
        "UpkeepHoursPerWood": 2,     # 8.11 1개 = 2시간
        "UpkeepMaxHours": 200,       # 8.11 최대 200시간
        "PinLength": 5,              # 8.11 PIN 5자리
        "UpkeepItemID": "item.wood", # 3.2·8.11 유지비 = 나무 (서버 UPKEEP_ITEM)
        "RanchExpandFameStep": 50,   # 3.2 k번째 확장 명성 50·k
        "RanchExpandCoinStep": 500,  # 3.2 k번째 확장 코인 500·k
        "RanchSlotMax": 10,          # 3.2 목장 슬롯 상한 10칸
    },
]

base_rule_table = ensure_table(BASE_RULE_TABLE_FOLDER, BASE_RULE_TABLE_NAME, BASE_RULE_ROW_STRUCT)
fill_table(base_rule_table, BASE_RULE_ROWS)

# ── 목장 서버 실패 문구 (M12-05) ──
# 행 이름 = 서버 `code`. 같은 code 가 함수마다 뜻이 다르면 `<함수>_<code>` 행 (FRanchFailureRow).
# 표에 없는 code 는 서버 사유 문장을 그대로 쓴다. 문구는 게임 규칙 숫자가 아니다.
RANCH_FAILURE_TABLE_FOLDER = "/Game/Game/Data/Ranch"
RANCH_FAILURE_TABLE_NAME = "DT_RanchFailureTable"
RANCH_FAILURE_ROW_STRUCT = "/Script/Snarehaven.RanchFailureRow"
RANCH_FAILURE_ROWS = [
    {"Name": name, "Message": message}
    for name, message in [
        ("locked", "목장이 잠겼다 - 나무를 넣어라"),
        ("not_enough_fame", "명성이 모자란다"),
        ("not_enough_coins", "코인이 모자란다"),
        ("not_enough_items", "나무가 모자란다"),
        ("FeedRanchUpkeep_full", "목장 유지가 이미 가득이다"),
        ("ExpandRanchSlot_full", "목장 칸이 이미 최대다"),
        # ⑩ 작물 심기·거두기 — `not_enough_items` 기본 행("나무가 모자란다")은 씨앗에 맞지 않아 함수 행을 둔다.
        ("PlantCrop_not_enough_items", "씨앗이 없다"),
        ("PlantCrop_slot_full", "이미 차 있는 칸이다"),
        ("PlantCrop_bad_slot", "잘못된 칸이다"),
        ("PlantCrop_bad_args", "심을 수 없는 씨앗이다"),
        ("HarvestCrop_empty_slot", "거둘 작물이 없다"),
        # #0190 펫 배치는 펫 아이템 1개를 쓴다 — 기본 `not_enough_items`("나무가 모자란다")는 펫에 맞지 않는다.
        ("RegisterPet_not_enough_items", "포획한 펫이 없다"),
        # #0198 펫 빼기 — 쌓인 생산물을 먼저 거두므로 가방 자리가 없으면 거절된다.
        ("RemovePet_empty_slot", "뺄 펫이 없는 칸이다"),
        ("RemovePet_bad_slot", "잘못된 칸이다"),
        # 10.8 꾸미기 상자 — 기본 `not_enough_items`·`inventory_full` 문구가 목장 말이라 함수 행을 둔다.
        ("OpenCrate_not_enough_items", "상자가 없다"),
        ("OpenCrate_no_key", "열쇠가 없다 - 잡화 상인에게 산다"),
        ("OpenCrate_inventory_full", "가방이 가득 찼다 - 한 칸 비워라"),
        # 10.9 목장 동물 꾸미기 — 기본 문구가 나무·생산물 말이라 함수 행을 둔다.
        ("SetPetCosmetic_not_enough_items", "꾸미기가 배낭에 없다"),
        ("SetPetCosmetic_empty_slot", "동물이 없는 칸이다"),
        ("SetPetCosmetic_bad_slot", "잘못된 칸이다"),
        ("SetPetCosmetic_bad_args", "목장 꾸미기가 아니다"),
        ("SetPetCosmetic_already", "이미 그 꾸미기를 씌웠다"),
        ("SetPetCosmetic_inventory_full", "가방이 가득 찼다 - 벗긴 꾸미기 자리를 비워라"),
        ("inventory_full", "가방이 가득 찼다 - 생산물 자리를 비워라"),
        ("network", "서버에 닿지 못했다 - 잠시 뒤 다시"),
        ("bad_args", "잘못된 요청이다"),
    ]
]

ranch_failure_table = ensure_table(RANCH_FAILURE_TABLE_FOLDER, RANCH_FAILURE_TABLE_NAME, RANCH_FAILURE_ROW_STRUCT)
fill_table(ranch_failure_table, RANCH_FAILURE_ROWS)

# ── 업적 (DESIGN 8.15, M12-04) ──
# ID·카운터·목표·코인은 서버 `ACHIEVEMENTS` 와 같다. 코인은 알림 카드 글에만 쓰는 표시 전용(M48-03b) — 지급은 서버만(CLAUDE.md 8).
# 2층 `Snarehaven.Achievement.Table.*` 이 DESIGN 8.15 표·서버 표와 맞댄다.
ACHIEVEMENT_TABLE_FOLDER = "/Game/Game/Data/Achievements"
ACHIEVEMENT_TABLE_NAME = "DT_AchievementTable"
ACHIEVEMENT_ROW_STRUCT = "/Script/Snarehaven.AchievementRow"
ACHIEVEMENT_ROWS = [
    {"Name": name, "AchievementID": aid, "DisplayName": label, "Counter": counter, "Target": target,
     "RewardCoins": coins}
    for name, aid, label, counter, target, coins in [
        ("Ach_Capture1", "ach.capture_1", "첫 올가미", "capture", 1, 10),
        ("Ach_Capture10", "ach.capture_10", "올가미 솜씨", "capture", 10, 30),
        ("Ach_Capture50", "ach.capture_50", "올가미 장인", "capture", 50, 80),
        ("Ach_Hunt10", "ach.hunt_10", "사냥 첫걸음", "hunt", 10, 10),
        ("Ach_Hunt100", "ach.hunt_100", "노련한 사냥꾼", "hunt", 100, 40),
        ("Ach_Fish1", "ach.fish_1", "첫 입질", "fish", 1, 5),
        ("Ach_Fish50", "ach.fish_50", "어부의 하루", "fish", 50, 40),
        ("Ach_Harvest10", "ach.harvest_10", "목장 일꾼", "harvest", 10, 10),
        ("Ach_Harvest100", "ach.harvest_100", "목장 주인", "harvest", 100, 50),
        ("Ach_Craft10", "ach.craft_10", "손재주", "craft", 10, 15),
        ("Ach_House1", "ach.house_1", "내 집 마련", "house", 1, 35),
        ("Ach_Codex50", "ach.codex_50", "도감 절반", "codexPercent", 50, 50),
        ("Ach_Codex100", "ach.codex_100", "도감 완성", "codexPercent", 100, 100),
    ]
]

achievement_table = ensure_table(ACHIEVEMENT_TABLE_FOLDER, ACHIEVEMENT_TABLE_NAME, ACHIEVEMENT_ROW_STRUCT)
fill_table(achievement_table, ACHIEVEMENT_ROWS)

unreal.log("M1-01/M1-05/M1-06/M2-02/M7-01/M6-01/M6-04a/M6-05a/M8-01/M10-01/M11-01/M11-05 데이터 테이블 준비 완료")
