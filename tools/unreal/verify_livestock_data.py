"""M5-04b 2층 검사 — 새 프로세스에서 소·양 **데이터 행과 레벨 배치**를 다시 읽는다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_livestock_data.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

채운 프로세스가 아니라 **새 프로세스에서 읽는 것**이 요점이다 — 같은 프로세스 로그는 저장됐다는
증거가 못 된다(루프#53 함정). M5-04a 의 `verify_livestock_assets.py` 가 에셋을 봤다면 이쪽은
그 에셋을 가리키는 행과, `L_Hunt` 에 실제로 선 액터를 본다.

**숫자를 만들지 않는다** — 기대값은 DESIGN 3.1 확정 수치표(체력 100 "전 종 공통"·포획 임계 25%·
드롭 1~3개)를 그대로 옮긴 것이고, 돼지 행과도 같은지 함께 본다.
"""
import json

import unreal

ANIMAL_TABLE = "/Game/Game/Data/Animals/DT_AnimalTable"
ITEM_TABLE = "/Game/Game/Data/Items/DT_ItemTable"
LEVEL_PATH = "/Game/Game/Maps/L_Hunt"

# (행 이름, 동물 ID, 펫 아이템 ID, 드롭 2종, 죽는 클립이 있어야 하는가)
EXPECTED_ANIMALS = [
    ("Animal_Pig", "animal.pig", "item.pet_pig", ["item.pig_meat", "item.pig_hide"], True),
    ("Animal_Cow", "animal.cow", "item.pet_cow", ["item.cow_meat", "item.cow_hide"], True),
    # 양의 Death 는 blend 재추출본이다(루프#131 임포트 — 막힘#74 해제). 이제 세 종 다 죽는 클립이 있다.
    ("Animal_Sheep", "animal.sheep", "item.pet_sheep", ["item.sheep_meat", "item.sheep_hide"], True),
]

EXPECTED_ITEMS = [
    "Item_CowMeat", "Item_CowHide", "Item_SheepMeat", "Item_SheepHide", "Item_PetCow", "Item_PetSheep",
]

# DESIGN 3.1 확정 수치표. 이 스크립트가 고른 값이 아니다.
EXPECTED_MAX_HP = 100.0
EXPECTED_CAPTURE_PCT = 25.0
EXPECTED_DROP_MIN = 1
EXPECTED_DROP_MAX = 3

EXPECTED_ACTORS = [
    ("HuntStagePig", "Animal_Pig"),
    ("HuntStageCow", "Animal_Cow"),
    ("HuntStageSheep", "Animal_Sheep"),
]

fail = 0


def ref_of(row, field):
    """행의 에셋 참조를 읽는다. **빈 참조는 `"None"` 이라는 글자로 나온다** — 언리얼이 JSON 으로
    내보낼 때 빈 `TSoftObjectPtr` 를 그렇게 적는다. 그대로 두면 "비었는데 채워진 것처럼" 보여
    이 검사가 빈 칸을 통과시킨다(루프#75 에서 실제로 양의 Death 를 "생겼다" 고 잘못 읽었다).
    """
    value = str(row.get(field, "") or "")
    return "" if value == "None" else value


def load_rows(table_path):
    table = unreal.EditorAssetLibrary.load_asset(table_path)
    if table is None:
        return None, None
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    # 언리얼은 행 이름을 `Name` 칸에 실어 배열로 내보낸다.
    return table, {row["Name"]: row for row in rows}


animal_table, animal_rows = load_rows(ANIMAL_TABLE)
if animal_rows is None:
    unreal.log_error("검사 실패 — 동물 테이블을 못 읽음: {}".format(ANIMAL_TABLE))
    fail += 1
else:
    unreal.log("검사 DT_AnimalTable: 행 {}개 {}".format(len(animal_rows), sorted(animal_rows)))
    for row_name, animal_id, pet_id, drops, wants_death in EXPECTED_ANIMALS:
        row = animal_rows.get(row_name)
        if row is None:
            unreal.log_error("검사 실패 — 행이 없다: {}".format(row_name))
            fail += 1
            continue
        death_ref = ref_of(row, "DeathAnimRef")
        unreal.log(
            "검사 {}: ID={} · 이름={} · HP={} · 포획임계={}% · 드롭={} {}~{} · 펫={} · 메시={} · Idle={} · Death={}".format(
                row_name, row.get("AnimalID"), row.get("DisplayName"), row.get("MaxHP"),
                row.get("CaptureThresholdPct"), row.get("DeathDropItems"),
                row.get("DeathDropCountMin"), row.get("DeathDropCountMax"), row.get("PetItemID"),
                str(row.get("MeshRef", "")).rsplit("/", 1)[-1],
                str(row.get("IdleAnimRef", "")).rsplit("/", 1)[-1],
                death_ref.rsplit("/", 1)[-1] if death_ref else "(없음)"))

        checks = [
            ("AnimalID", row.get("AnimalID"), animal_id),
            ("PetItemID", row.get("PetItemID"), pet_id),
            ("DeathDropItems", row.get("DeathDropItems"), drops),
            ("MaxHP", row.get("MaxHP"), EXPECTED_MAX_HP),
            ("CaptureThresholdPct", row.get("CaptureThresholdPct"), EXPECTED_CAPTURE_PCT),
            ("DeathDropCountMin", row.get("DeathDropCountMin"), EXPECTED_DROP_MIN),
            ("DeathDropCountMax", row.get("DeathDropCountMax"), EXPECTED_DROP_MAX),
        ]
        for field, got, want in checks:
            if got != want:
                unreal.log_error("검사 실패 — {}.{} 가 {} (기대 {})".format(row_name, field, got, want))
                fail += 1

        # 메시·Idle 은 반드시 있어야 한다 — 비면 자리표시 실린더가 서서 3층 캡처가 거짓이 된다.
        for field in ("MeshRef", "IdleAnimRef"):
            ref = ref_of(row, field)
            if not ref:
                unreal.log_error("검사 실패 — {}.{} 가 비었다".format(row_name, field))
                fail += 1
            elif not unreal.EditorAssetLibrary.does_asset_exist(ref.split(".")[0]):
                unreal.log_error("검사 실패 — {}.{} 가 없는 에셋을 가리킨다: {}".format(row_name, field, ref))
                fail += 1

        if wants_death and not death_ref:
            unreal.log_error("검사 실패 — {}.DeathAnimRef 가 비었다".format(row_name))
            fail += 1
        elif wants_death and not unreal.EditorAssetLibrary.does_asset_exist(death_ref.split(".")[0]):
            # 경로만 적히고 에셋이 없으면 죽어도 Idle 을 돈다 — 채워진 것처럼 보이는 빈 칸(루프#75)의 다른 얼굴.
            unreal.log_error("검사 실패 — {}.DeathAnimRef 가 없는 에셋을 가리킨다: {}".format(row_name, death_ref))
            fail += 1
        if not wants_death and death_ref:
            # 양에 Death 가 생겼다면 이 검사표(와 INBOX `막힘`)를 같이 고쳐야 한다.
            unreal.log("검사 알림 — {} 에 Death 가 생겼다: {} (검사표·INBOX 막힘 갱신 필요)".format(row_name, death_ref))

item_table, item_rows = load_rows(ITEM_TABLE)
if item_rows is None:
    unreal.log_error("검사 실패 — 아이템 테이블을 못 읽음: {}".format(ITEM_TABLE))
    fail += 1
else:
    unreal.log("검사 DT_ItemTable: 행 {}개 {}".format(len(item_rows), sorted(item_rows)))
    for row_name in EXPECTED_ITEMS:
        row = item_rows.get(row_name)
        if row is None:
            unreal.log_error("검사 실패 — 아이템 행이 없다: {}".format(row_name))
            fail += 1
            continue
        unreal.log("검사 {}: ID={} · 이름={} · 종류={}".format(
            row_name, row.get("ItemID"), row.get("DisplayName"), row.get("ItemType")))

    # 동물 행이 가리키는 아이템이 표에 실제로 있는지 — 없으면 포획·드롭이 조용히 빈손이 된다.
    if animal_rows:
        known_ids = {str(row.get("ItemID")) for row in item_rows.values()}
        for row_name, _animal_id, pet_id, drops, _wants_death in EXPECTED_ANIMALS:
            for item_id in list(drops) + [pet_id]:
                if item_id not in known_ids:
                    unreal.log_error("검사 실패 — {} 이 가리키는 아이템이 표에 없다: {}".format(row_name, item_id))
                    fail += 1

# 레벨 배치 — 액터가 서 있고 제 행을 보고 있는지.
if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    unreal.log_error("검사 실패 — 레벨이 없다: {}".format(LEVEL_PATH))
    fail += 1
else:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL_PATH)
    actors = {a.get_actor_label(): a for a in
              unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    animals = [a for a in actors.values() if isinstance(a, unreal.AnimalCharacter)]
    unreal.log("검사 L_Hunt: 동물 액터 {}마리 {}".format(
        len(animals), sorted(a.get_actor_label() for a in animals)))
    for label, row_name in EXPECTED_ACTORS:
        actor = actors.get(label)
        if actor is None:
            unreal.log_error("검사 실패 — 액터가 없다: {}".format(label))
            fail += 1
            continue
        got_row = str(actor.get_editor_property("animal_row_name"))
        location = actor.get_actor_location()
        unreal.log("검사 {}: 행={} · 자리=({:.0f}, {:.0f}, {:.0f})".format(
            label, got_row, location.x, location.y, location.z))
        if got_row != row_name:
            unreal.log_error("검사 실패 — {} 의 행이 {} (기대 {})".format(label, got_row, row_name))
            fail += 1
    if len(animals) != len(EXPECTED_ACTORS):
        unreal.log_error("검사 실패 — 동물 액터가 {}마리 (기대 {})".format(len(animals), len(EXPECTED_ACTORS)))
        fail += 1

unreal.log("검사 결과: {}".format("전부 통과" if fail == 0 else "실패 {}건".format(fail)))
