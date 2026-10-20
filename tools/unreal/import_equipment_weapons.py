"""장비 임포트 — 바르코가 만든 손에 드는 것 → 정적 메시. M5-06a 몽둥이 1종으로 시작해 M9-04 도구 2종(손도끼·채집 바구니)을 더했다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_equipment_weapons.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=logs/import_equipment_weapons.log

원본은 감독이 바르코로 직접 만들어 `_Source/raw/Snare/`(gitignore)에 넣은 것이다 — 몽둥이 `Weapons/Club/`
(M5-06a0 · STATUS #0075), 도구 `Items/Tools/HandAxe/`·`Items/Tools/Basket/`(INBOX 2026-09-23 15:40),
모자 10종 `Items/Hats/<Name>/`(M13-35 · 받침점 바닥 · 가장 긴 변 1m). 원본은
본 저장소에만 있다(워크트리에는 없다) — 그래서 원본 경로는 본 저장소 절대경로다.

**삼각형을 줄인다.** 원본은 20000~22000 삼각형이라 DESIGN 4.2 의 **오브젝트당 3K** 를 7배 넘는다
(바르코 생성 파라미터가 `polygonCount=8000` 이었다 — 쿼드가 삼각형으로 갈리며 커졌다). 대안 메시가 없어
엔진의 LOD 감축으로 한도 안까지 줄인다. 한도 3000 은 설계서 값이고 이 스크립트가 만든 숫자가 아니다.

**크기를 맞춘다(도구만).** 바르코는 가장 긴 변을 1m 로 맞춰 내보낸다. 도구는 DESIGN 3.5 "도구 크기" 표의 값
(손도끼 45 · 바구니 35 · 전기톱 55 · 마체테 50 · 망원경 22 · 위성 통신기 20cm)에 **가장 긴 변**(x·y·z 최대)을
맞추는 한 배율(**목표 ÷ 잰 긴 변**)로 다시 받는다 — 13.5 도구 4종은 눕혀 내보내 긴 변이 z 가 아니다(M13-36).
몽둥이는 크기 값이 설계서에 없어 그대로 둔다(M5-06a 에서 이미 합격한 메시를 건드리지 않는다).

**glb 를 먼저, FBX 를 나중에 임포트한다.** FBX 에 텍스처가 내장돼 있는데도 Interchange 가 꺼내 주지
않아(루프#88 1차 실행: 나온 에셋이 메시 + 빈 `material_0` 둘뿐) 무채색으로 들어왔다. 같은 메시의
glb 에는 PNG 3장이 들어 있고 glTF 경로는 텍스처를 제대로 꺼낸다 — 그래서 **색은 glb 에서, 몸체는 FBX 에서**
받는다. 순서가 중요하다: 두 파일의 머티리얼 이름이 똑같이 `material_0` 이라, FBX 를 먼저 넣으면 glb 임포트가
**같은 폴더의 같은 이름 머티리얼을 재사용**해 빈 머티리얼이 그대로 남는다(루프#70 이 자연물에서 밟은 그 함정).
glb 것을 먼저 받아 규칙 이름으로 바꿔 두면 FBX 가 만드는 빈 `material_0` 과 섞이지 않는다.

재실행 안전: 끝까지 간 결과(메시·머티리얼·텍스처만 있고 한도 안·크기 맞음)면 아무것도 다시 하지 않는다.
하나라도 어긋나면 **그 장비의 폴더를** 비우고 처음부터 다시 받는다 — 그래서 장비마다 폴더가 따로다.

로그: UE 5.8 커맨드릿은 `unreal.log()` 를 로그 파일에서 삼킨다(STATUS 루프#157) — 그래서 전부
`log_warning` 으로 남긴다. `최종:` 줄이 크기(cm)·삼각형 검사 증거다.
"""
import os

import unreal

RAW = "~/UNREAL/Snarehaven/_Source/raw/Snare"
EQUIPMENT_DIR = "/Game/Game/Characters/Equipment"
TRIANGLE_LIMIT = 3000   # DESIGN 2·4.2 "오브젝트당 3K 이하" — 설계서 값
SIZE_TOLERANCE_CM = 0.5

# (원본 이름, 폴더, 메시 이름, 머티리얼·텍스처 이름 몸통, 목표 긴 변 cm 또는 None)
# 목표는 DESIGN 3.5 "도구 크기" 표 — 메시의 가장 긴 변(x·y·z 최대)을 맞춘다(손도끼·바구니는 z 가 가장 길다).
EQUIPMENT = [
    ("{}/Weapons/Club/Club_v1".format(RAW), EQUIPMENT_DIR + "/Weapons", "SM_Weapon_Club_01", "Weapon_Club", None),
    ("{}/Items/Tools/HandAxe/HandAxe_v1".format(RAW), EQUIPMENT_DIR + "/Tools/HandAxe",
     "SM_Tool_HandAxe_01", "Tool_HandAxe", 80.0),
    ("{}/Items/Tools/Basket/Basket_v1".format(RAW), EQUIPMENT_DIR + "/Tools/Basket",
     "SM_Tool_Basket_01", "Tool_Basket", 60.0),
]
# M13-36 13.5 도구 4종 — 목표 긴 변은 DESIGN 3.5 「도구 크기」 표.
# 2026-10-05 손 도구는 월드 cm(몸 키 176) — 옛 값은 몸 리그 100cm 단위였는데 월드 배율 1 로 붙어 키의 1/4 로 작았다.
EQUIPMENT += [("{0}/Items/Tools/{1}/{1}_v1".format(RAW, name), "{}/Tools/{}".format(EQUIPMENT_DIR, name),
               "SM_Tool_{}_01".format(name), "Tool_" + name, cm)
              for name, cm in (("Chainsaw", 97.0), ("Machete", 88.0), ("Binoculars", 30.0), ("SatelliteCommunicator", 20.0),
                               # M14-04 곡괭이 3종 (DESIGN 3.5 「도구 크기」·8.16)
                               ("PickaxeBasic", 88.0), ("PickaxeAdvanced", 88.0), ("PickaxeElectric", 88.0))]
# M13-43 낚싯대 4종 110 · 하이킹 배낭 35 (DESIGN 3.5 「도구 크기」 — 몸 리그 기준, 붙을 때 몸 배율을 따른다).
# M13-44 낚싯대는 `_v2` — 감독이 주축을 +Z 로 세운 원본이라 상자 긴 변 = 주축 참길이 110 이다(v1 은 대각선으로 누워 127~142).
# M13-45 반입할 때 손 뼈(`Socket_Tool` = 왼손) 좌표계에서 계산한 회전 하나로 돌린다(M13-44 의 X 축 180° 뒤집기는
# 끝이 몸 뒤 86cm 로 넘어갔다 — #0240). 원본 +Z(손잡이→끝) 를 `A_Player_FishIdle` 첫 프레임에서 **폰 앞 수평 위 54°
# · 왼쪽 22°** 로 보내는 손 공간 방향 (0.730, 0.240, 0.640) 이 되게 하는 회전이다 — 값·근거는 DESIGN 3.5 「도구 크기」
# 낚싯대 행. 피벗(손잡이 15%)·참길이 110 은 그대로다(회전해도 원점을 지나는 축이라 피벗이 안 움직인다).
ROD_ROTATION = unreal.Rotator(roll=0.0, pitch=-50.19, yaw=18.19)
ROTATION_TAG = "SnarehavenImportRotation"   # 메시에 적어 두는 반입 회전 — 값이 바뀌면 already_done 이 다시 받게 한다
EQUIPMENT += [("{0}/Items/Tools/{1}/{1}_v2".format(RAW, name), "{}/Tools/{}".format(EQUIPMENT_DIR, name),
               "SM_Tool_{}_01".format(name), "Tool_" + name, 110.0, ROD_ROTATION)
              for name in ("FishingRod", "TelescopicRod", "BaitRod", "LuckyRod")]
EQUIPMENT += [("{}/Items/Backpacks/HikingBackpack/HikingBackpack_v1".format(RAW), EQUIPMENT_DIR + "/Backpacks/HikingBackpack",
               "SM_Backpack_HikingBackpack_01", "Backpack_HikingBackpack", 45.0)]
# M13-35 모자 10종(+ 8.16 채굴 모자 · M21-05c 새싹 모자 · M21-07e 우두머리 모자) — 크기는 붙일 때 `UEquipmentComponent` 가 한 배율로 맞춘다(DESIGN 3.5 「모자 크기」) — 그래서 None.
# `_v2`(09-25) — 몸 머리 타원체에 맞춰 지은 모자. 받침점 = 정수리(`Socket_Head`)라 소켓에 붙이면 바로 머리에 씌워진다
# (v1 은 받침점이 모자 바닥이라 정수리 위에 떴다). 제작: `_Source/raw/Snare/Items/Hats/hats_v2_make.py`.
HATS = ("Fisher", "Farmer", "Hunter", "Lumberjack", "Strawberry", "Cook", "Looter", "Traveler", "Merchant", "Runner", "Mining", "Sprout", "Chief",
        # 스킨 조각 f 꾸미기 모자 6(`docs/specs/skin-parts.md` 6장) — 같은 v2 틀
        "MeadowBucket", "MeadowCap", "MeadowFrog", "HarborBeanie", "HarborCaptain", "HarborWhale",
        # M83-02 +5(루프#856) — `tools/blender/hats_more_make.py`
        "MeadowAcorn", "HarborSouwester", "MeadowCrown", "HarborBandana", "MeadowWizard",
        # M83-03 +5(루프#858) — 같은 스크립트
        "MeadowSunflower", "HarborDixie", "HarborHorned", "MeadowMushroom", "MeadowTopHat",
        # M84-02 +5(루프#865) — 같은 스크립트
        "MeadowCowboy", "MeadowBear", "MeadowCatEars", "HarborTricorn", "MeadowPropeller",
        # M84-03 +5(루프#866) — 같은 스크립트
        "MeadowBeret", "HarborFez", "MeadowBunny", "MeadowPumpkin", "HarborShark",
        # M88-02 +5(루프#890) — 같은 스크립트
        "MeadowLadybug", "HarborLighthouse", "MeadowFlowerpot", "MeadowCarrot", "HarborJellyfish",
        # M88-03 +5(루프#891) — 같은 스크립트
        "HarborSailorCap", "MeadowTeapot", "MeadowOwl", "HarborCrab", "MeadowCake",
        # M90-01 +5(루프#898) — 같은 스크립트
        "MeadowSnail", "HarborBuoy", "MeadowPineapple", "HarborStarfish", "MeadowRocket",
        # M90-02 +5(루프#899) — 같은 스크립트
        "MeadowDuck", "MeadowBeehive", "HarborConch", "MeadowCactus", "MeadowUfo",
        # M91-01 +6(루프#904) — 같은 스크립트
        "MeadowPenguin", "MeadowDonut", "MeadowMouse", "HarborIgloo", "HarborRainbow", "HarborTreasure",
        # M91-02 +5(루프#905) — 같은 스크립트
        "MeadowFox", "MeadowPig", "HarborSailboat", "MeadowCheese", "MeadowDragon")
EQUIPMENT += [("{0}/Items/Hats/{1}/Hat{1}_v2".format(RAW, name), "{}/Hats/{}".format(EQUIPMENT_DIR, name),
               "SM_Hat_{}_01".format(name), "Hat_" + name, None) for name in HATS]
# 스킨 조각 g 꾸미기 얼굴 장식 6(`docs/specs/skin-parts.md` 6장) — 받침점 = `Socket_Face` 자리, 몸 공간 치수 그대로라
# 크기는 None(행의 `bEquipFollowsBodyScale` 이 몸 배율을 준다). 제작: `_Source/raw/Snare/Items/FaceAcc/face_make.py`.
FACE_ACCESSORIES = ("MeadowLadybugNose", "MeadowDaisyGlasses", "MeadowPetalGoggles", "HarborEyepatch", "HarborSunglasses", "HarborDivingMask",
                    "MeadowMustache", "HarborBandage", "HarborMonocle", "MeadowStarShades",  # M87-01 +4 (`tools/blender/faces_more_make.py`)
                    "HarborKerchief", "MeadowCheekStars", "MeadowHeartShades", "HarborWhiteBeard")  # M87-02 +4 (`tools/blender/faces_more2_make.py`)
# 루프#1358 — 끈·안경다리가 머리를 도는 9벌은 둘레를 지금 머리(찹쌀떡 2.5)·머리카락 껍질에 맞춘 v2(`face_make.py` `band_point`).
FACE_STRAP_V2 = ("MeadowDaisyGlasses", "MeadowPetalGoggles", "HarborEyepatch", "HarborSunglasses", "HarborDivingMask",
                 "HarborMonocle", "MeadowStarShades", "HarborKerchief", "MeadowHeartShades")
EQUIPMENT += [("{0}/Items/FaceAcc/{1}/Face{1}_v{2}".format(RAW, name, 2 if name in FACE_STRAP_V2 else 1), "{}/Faces/{}".format(EQUIPMENT_DIR, name),
               "SM_Face_{}_01".format(name), "Face_" + name, None) for name in FACE_ACCESSORIES]
# 스킨 조각 l 꾸미기 배낭 6(`docs/specs/skin-parts.md` 6장) — 하이킹 배낭과 같은 피벗(경계 가운데)·같은 몸 쪽 면(메시 −Y)이라
# `Socket_Back` 에 그대로 붙는다. 몸 공간 치수 그대로라 크기는 None(행의 `bEquipFollowsBodyScale` 이 몸 배율을 준다).
# 제작: `_Source/raw/Snare/Items/Backpacks/backpack_make.py`.
COSMETIC_BACKPACKS = ("MeadowDaypack", "MeadowBasketPack", "MeadowFrogPack", "HarborDuffel", "HarborLifebuoyPack", "HarborOctopusPack",
                      # M83-01 +3(루프#855) — `tools/blender/backpack_more_make.py`
                      "MeadowMushroomPack", "HarborCratePack", "MeadowBeehivePack",
                      # M84-01 +3(루프#864)
                      "MeadowWatermelonPack", "HarborBarrelPack", "HarborWhalePack",
                      # M88-01 +3(루프#889)
                      "MeadowStrawberryPack", "MeadowCatPack", "HarborSubmarinePack")
EQUIPMENT += [("{0}/Items/Backpacks/{1}/Backpack{1}_v1".format(RAW, name), "{}/Backpacks/{}".format(EQUIPMENT_DIR, name),
               "SM_Backpack_{}_01".format(name), "Backpack_" + name, None) for name in COSMETIC_BACKPACKS]
# 스킨 조각 n 목장 동물 리본·목걸이(`docs/specs/skin-parts.md` 6장) — 크기는 `ARanchPetActor::SetCosmetic` 이 펫 키 ×
# 행 `PetCosmeticWidthRatio` 로 맞춘다 — 그래서 None. 원점: 리본 = 매듭 아래 가운데, 목걸이 = 고리 가운데.
# 제작: `_Source/raw/Snare/Items/RanchCosmetics/ranch_cosmetic_make.py`. 가챠 펫 의상 셋(루프#538): 왕관 = 밑면 받침(머리),
# 축제 깃발·황금 갈기 = 고리 가운데(목). M40-02 방울 목걸이(루프#592) = 고리 가운데(목). M40-03 밀짚모자(루프#595) = 밑면 받침(머리).
# M40-04 리본 목줄(루프#596) = 고리 가운데(목).
RANCH_COSMETICS = ("RanchRibbon", "RanchNecklace", "GachaCrown", "GachaFestivalFlags", "GachaGoldMane", "GachaBell", "GachaStrawhat",
                   "GachaRibbon", "RanchHat")
EQUIPMENT += [("{0}/Items/RanchCosmetics/{1}/{1}_v1".format(RAW, name), "{}/Pet/{}".format(EQUIPMENT_DIR, name),
               "SM_Pet_{}_01".format(name), "Pet_" + name, None) for name in RANCH_COSMETICS]
# M13-37 가구 10종·상자·가방 — 받침점 바닥·가장 긴 변 1m. 크기는 놓을 때 액터가 한 배율로 맞춘다
# (DESIGN 8.11 「가구·상자 진짜 메시 크기」) — 그래서 None. 장비 목록과 따로 둔다(도구 줄은 M13-36 몫).
FURNITURE = [("{0}/Items/Furniture/{1}/{1}_v1".format(RAW, name), "/Game/Game/Environment/Furniture/" + name,
              "SM_Furn_{}_01".format(name), "Furn_" + name, None)
             for name in ("Bench", "ChairWood", "TableWood", "FenceWood", "StorageBox", "Campfire",
                          "GardenBrazier", "Grill", "WorkbenchAdv", "Tent")]
FURNITURE += [("{0}/Items/Furniture/{1}/{1}_v1".format(RAW, name), "/Game/Game/Environment/Props/" + name,
               "SM_Prop_{}_01".format(name), "Prop_" + name, None) for name in ("LootChest", "DropBag")]
# M13-39 잡화점 좌판·선착장 부두 — 받침점 바닥·가장 긴 변 1m. 크기는 액터가 한 배율로 맞춘다
# (DESIGN 8.14 「잡화점·선착장 진짜 모습」) — 그래서 None. 칠하기 칸은 없다(칠하는 가구가 아니다).
PROPS = [("{0}/Items/Props/{1}/{1}_v1".format(RAW, name), "/Game/Game/Environment/Props/" + name,
          "SM_Prop_{}_01".format(name), "Prop_" + name, None) for name in ("ShopStall", "FerryPier", "Smelter")]   # 제련소 M14-04
# M13-40 목장 칸 말뚝·물고기 그림자 — 칠하지 않는 소품이라 가구 목록(칠하기 칸 끼움)과 따로 둔다.
# 크기는 액터가 한 배율(말뚝 높이 120)·눌림(그림자 길이 50·높이 2)으로 맞춘다(DESIGN 3.2·8.13) — 그래서 None.
SOFT_PROPS = [("{0}/Items/Props/{1}/{1}_v1".format(RAW, name), "/Game/Game/Environment/Props/" + name,
          "SM_Prop_{}_01".format(name), "Prop_" + name, None) for name in ("TetherPost", "FishShadow", "OreBasic", "OreAdvanced")]   # 광맥 M14-04

log = unreal.log_warning


def _pipeline(asset_name: str, scale: float, rotation=None) -> unreal.InterchangeGenericAssetsPipeline:
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("import_offset_uniform_scale", scale)
    if rotation is not None:
        pipeline.set_editor_property("import_offset_rotation", rotation)
    pipeline.set_editor_property("asset_type_sub_folders", False)
    pipeline.set_editor_property("scene_name_sub_folder", False)
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", asset_name)

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_skeletal_meshes", False)   # 손에 드는 프롭이라 정적 메시다
    mesh.set_editor_property(
        "combine_static_meshes_behavior",
        unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh.set_editor_property("build_nanite", False)             # 모바일 렌더러라 Nanite 를 끈 프로젝트다
    return pipeline


def _import(source_file: str, dest_dir: str, asset_name: str, scale: float = 1.0, rotation=None) -> list:
    override = unreal.InterchangePipelineStackOverride()
    override.add_pipeline(_pipeline(asset_name, scale, rotation))

    task = unreal.AssetImportTask()
    task.filename = source_file
    task.destination_path = dest_dir
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


def _subsystem():
    # 커맨드릿은 이 모듈을 스스로 안 올린다 — 안 올리면 서브시스템이 None 이다(루프#88 에서 실제로 밟았다).
    unreal.load_module("StaticMeshEditor")
    return unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)


def _longest(mesh: unreal.StaticMesh) -> float:
    box = mesh.get_bounding_box()
    return max(box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z)


def _rotation_text(rotation: unreal.Rotator) -> str:
    return "pitch={:.2f} yaw={:.2f} roll={:.2f}".format(rotation.pitch, rotation.yaw, rotation.roll)


def _axis_reach(mesh: unreal.StaticMesh, axis: unreal.Vector) -> tuple:
    """꼭짓점을 축에 투영한 (최소, 최대). 돌려 받은 메시는 상자 긴 변이 참길이가 아니다(대각선) — 그래서 꼭짓점으로 잰다."""
    description = mesh.get_static_mesh_description(0)
    reach = [description.get_vertex_position(unreal.VertexID(id_value=i)).dot(axis)
             for i in range(description.get_vertex_count())]
    return min(reach), max(reach)


def _reduce_to_limit(mesh: unreal.StaticMesh) -> None:
    """LOD0 을 한도 안까지 감축한다. 이미 한도 안이면 건드리지 않는다."""
    before = mesh.get_num_triangles(0)
    if before <= TRIANGLE_LIMIT:
        log("감축 안 함 — 이미 한도 안: 삼각형 {}".format(before))
        return
    subsystem = _subsystem()
    settings = unreal.MeshReductionSettings()
    settings.set_editor_property("percent_triangles", float(TRIANGLE_LIMIT) / float(before))
    subsystem.set_lod_reduction_settings(mesh, 0, settings)
    log("LOD0 감축: 삼각형 {} -> {} (목표 {})".format(before, mesh.get_num_triangles(0), TRIANGLE_LIMIT))


def _rename(path: str, new_path: str) -> None:
    if unreal.EditorAssetLibrary.rename_asset(path, new_path):
        log("이름 바꿈: {} -> {}".format(path, new_path))
    else:
        unreal.log_error("이름 바꾸기 실패: {} -> {}".format(path, new_path))


def _recorded_source(asset) -> str:
    """에셋에 적힌 임포트 원본 파일 이름(`import_player_animations.py` 와 같은 방법). 못 읽으면 빈 문자열."""
    try:
        import_data = asset.get_editor_property("asset_import_data")
        return os.path.basename(import_data.get_first_filename()) if import_data else ""
    except Exception as err:      # 엔진 판이 바뀌어 칸 이름이 달라질 때를 대비해서만
        log("원본 이름을 못 읽었다: {} ({})".format(asset.get_name(), err))
        return ""


def _assets_of(dest_dir: str, kind) -> list:
    return sorted(path for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False)
                  if isinstance(unreal.EditorAssetLibrary.load_asset(path), kind))


class Equipment:
    def __init__(self, source_stem, dest_dir, mesh_name, body, target_cm, rotation=None):
        self.fbx = source_stem + ".fbx"
        self.glb = source_stem + ".glb"
        self.dest_dir = dest_dir
        self.mesh_name = mesh_name
        self.gltf_temp_name = mesh_name + "_FromGltf"   # 텍스처·머티리얼만 받으려고 잠깐 만드는 메시. 끝에 지운다.
        self.material_name = "M_" + body   # glTF 경로가 만드는 것은 인스턴스가 아니라 머티리얼이라 `M_`
        # glTF 이미지 순서 그대로 (`materials[0]`: baseColor=0 · occlusion/metallicRoughness=1 · normal=2).
        # 이름이 없는 이미지라 임포터가 `texture_0`... 로 짓는다 — 순서로 이름을 붙이고, 압축 설정으로 확인한다
        # (법선 텍스처만 `TC_NORMALMAP` 이라 매핑이 어긋나면 로그에서 바로 보인다).
        self.texture_names = ["T_{}_{}".format(body, s) for s in ("BaseColor", "ORM", "Normal")]
        self.target_cm = target_cm
        self.rotation = rotation   # 돌려 받기(낚싯대 — 위 EQUIPMENT 주석)
        # 돌린 뒤 원본 +Z(손잡이→끝) 가 가리키는 방향. 크기는 이 축으로 잰 참길이다.
        self.axis = None if rotation is None else \
            unreal.MathLibrary.greater_greater_vector_rotator(unreal.Vector(0.0, 0.0, 1.0), rotation)
        self.mesh_path = "{}/{}".format(dest_dir, mesh_name)
        self.material_path = "{}/{}".format(dest_dir, self.material_name)

    def length(self, mesh) -> float:
        """목표와 비교할 길이 — 안 돌린 메시는 상자 긴 변, 돌린 메시는 축 참길이."""
        if self.axis is None:
            return _longest(mesh)
        low, high = _axis_reach(mesh, self.axis)
        return high - low

    def import_everything(self) -> None:
        for path in unreal.EditorAssetLibrary.list_assets(self.dest_dir, recursive=True, include_folder=False):
            if unreal.EditorAssetLibrary.delete_asset(path):
                log("비움: {}".format(path))

        # 1) 색 먼저 — glb 의 머티리얼·텍스처.
        for path in _import(self.glb, self.dest_dir, self.gltf_temp_name):
            log("임포트됨(glb): {}".format(path))

        textures = _assets_of(self.dest_dir, unreal.Texture2D)
        if len(textures) != len(self.texture_names):
            unreal.log_error("텍스처가 {}장이 아니라 {}장이다 — 이름을 못 붙인다: {}".format(
                len(self.texture_names), len(textures), textures))
        else:
            for path, want in zip(textures, self.texture_names):
                _rename(path, "{}/{}".format(self.dest_dir, want))

        materials = _assets_of(self.dest_dir, unreal.MaterialInterface)
        if len(materials) != 1:
            unreal.log_error("glb 머티리얼이 1개가 아니라 {}개다: {}".format(len(materials), materials))
        else:
            _rename(materials[0], self.material_path)

        # 2) 몸체는 FBX 에서. 목표 높이가 있으면 한 번 재고 그 배율로 다시 받는다 — 배율을 지오메트리에 굽는다.
        #    (LOD 빌드 배율 `build_scale3d` 는 Interchange 메시에서 바운드가 안 바뀌었다 — #0207 1차 실행 100.1cm 그대로.)
        #    첫 반입은 **안 돌리고** 잰다 — 돌린 상자 긴 변은 참길이가 아니다. 회전은 다시 받을 때 넣는다.
        for path in _import(self.fbx, self.dest_dir, self.mesh_name):
            log("임포트됨(FBX): {}".format(path))
        if self.target_cm is not None:
            before = _longest(unreal.EditorAssetLibrary.load_asset(self.mesh_path))
            factor = self.target_cm / before
            _import(self.fbx, self.dest_dir, self.mesh_name, factor, self.rotation)
            log("크기 맞춤: {} 긴 변 {:.1f} -> {:.1f}cm (배율 {:.4f} · 회전 {})".format(
                self.mesh_name, before, self.length(unreal.EditorAssetLibrary.load_asset(self.mesh_path)), factor,
                self.rotation))

        # 3) FBX 가 만든 빈 머티리얼 대신 glb 머티리얼을 물리고, 임시로 받은 메시·빈 머티리얼을 지운다.
        mesh = unreal.EditorAssetLibrary.load_asset(self.mesh_path)
        material = unreal.EditorAssetLibrary.load_asset(self.material_path)
        if isinstance(mesh, unreal.StaticMesh) and isinstance(material, unreal.MaterialInterface):
            # 슬롯 구조체를 **새로 만들어** 통째로 넣는다 — `static_materials` 가 돌려주는 것은 복사본이라
            # 그 안의 `material_interface` 를 고쳐 봐야 메시에 안 닿는다(루프#88 3차 실행에서 조용히 비었다).
            slots = [unreal.StaticMaterial(material_interface=material, material_slot_name=slot.material_slot_name)
                     for slot in mesh.static_materials]
            mesh.set_editor_property("static_materials", slots)
            log("머티리얼 물림: {} 슬롯 {}개 <- {}".format(self.mesh_name, len(slots), material.get_name()))
        else:
            unreal.log_error("메시나 머티리얼을 못 읽었다 — {} 가 무채색으로 남는다".format(self.mesh_name))
            return

        keep = {self.mesh_name, self.material_name, *self.texture_names}
        for path in unreal.EditorAssetLibrary.list_assets(self.dest_dir, recursive=True, include_folder=False):
            if path.split("/")[-1].split(".")[0] in keep:
                continue
            if unreal.EditorAssetLibrary.delete_asset(path):
                log("지움(임시): {}".format(path))
            else:
                unreal.log_error("지우기 실패: {}".format(path))

        _reduce_to_limit(mesh)
        if self.rotation is not None:
            unreal.EditorAssetLibrary.set_metadata_tag(mesh, ROTATION_TAG, _rotation_text(self.rotation))
        unreal.EditorAssetLibrary.save_directory(self.dest_dir, only_if_is_dirty=False, recursive=True)

    def already_done(self) -> bool:
        """끝까지 간 결과일 때만 건너뛴다 — 중간에 죽은 반쪽 상태를 ‘있음’으로 읽으면
        삼각형도 안 줄고 찌꺼기도 남은 채 끝난다(루프#88 5차 실행)."""
        if not (unreal.EditorAssetLibrary.does_asset_exist(self.mesh_path)
                and unreal.EditorAssetLibrary.does_asset_exist(self.material_path)):
            return False
        keep = [self.mesh_path, self.material_path] + ["{}/{}".format(self.dest_dir, n) for n in self.texture_names]
        for path in unreal.EditorAssetLibrary.list_assets(self.dest_dir, recursive=True, include_folder=False):
            if path.split(".")[0] not in keep:
                return False   # 찌꺼기(material_0 · 임시 glTF 메시)가 남았다
        mesh = unreal.EditorAssetLibrary.load_asset(self.mesh_path)
        if not (isinstance(mesh, unreal.StaticMesh) and 0 < mesh.get_num_triangles(0) <= TRIANGLE_LIMIT):
            return False
        # 원본 줄기가 바뀌면(M13-44 낚싯대 v1 → v2) 크기가 같아도 옛 메시다 — 다시 받는다.
        recorded = _recorded_source(mesh)
        if recorded and recorded != os.path.basename(self.fbx):
            log("원본이 바뀌었다: {} -> {} — 지우고 다시 받는다".format(recorded, os.path.basename(self.fbx)))
            return False
        if self.rotation is not None:
            # 회전 값이 바뀌면(M13-44 X 축 180° → M13-45) 크기가 같아도 옛 메시다 — 적어 둔 회전과 비교한다.
            recorded_rotation = unreal.EditorAssetLibrary.get_metadata_tag(mesh, ROTATION_TAG)
            if recorded_rotation != _rotation_text(self.rotation):
                log("회전이 바뀌었다: '{}' -> '{}' — 다시 받는다: {}".format(
                    recorded_rotation, _rotation_text(self.rotation), self.mesh_path))
                return False
        return self.target_cm is None or abs(self.length(mesh) - self.target_cm) <= SIZE_TOLERANCE_CM

    def run(self) -> None:
        if not os.path.exists(self.fbx) or not os.path.exists(self.glb):
            unreal.log_error("원본이 없다: {} / {}".format(self.fbx, self.glb))
        elif self.already_done():
            log("이미 있음 — 다시 임포트하지 않는다: {} + {}".format(self.mesh_path, self.material_path))
        else:
            self.import_everything()
        self.report()

    def report(self) -> None:
        """새로 읽어 확인. **레지스트리를 먼저 다시 훑는다** — 갓 만든 에셋은 레지스트리에 없어서
        "쓰는 곳 없음" 으로 잘못 나온다(루프#68 이 그걸 믿고 머티리얼을 다 지웠다).
        껍데기 메시(삼각형 0)·한도밖·빈 머티리얼·크기 어긋남을 통과라고 적지 않으려고 값을 전부 남긴다."""
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([self.dest_dir], force_rescan=True)
        for path in unreal.EditorAssetLibrary.list_assets(self.dest_dir, recursive=True, include_folder=False):
            asset = unreal.EditorAssetLibrary.load_asset(path)
            if asset is None:
                unreal.log_error("못 읽음: {}".format(path))
                continue
            if isinstance(asset, unreal.StaticMesh):
                box = asset.get_bounding_box()
                extent = box.max - box.min
                triangles = asset.get_num_triangles(0)
                slots = [slot.material_interface.get_name() if slot.material_interface is not None else "(빔)"
                         for slot in asset.static_materials]
                size_note = ""
                if self.target_cm is not None:
                    longest = self.length(asset)
                    size_ok = abs(longest - self.target_cm) <= SIZE_TOLERANCE_CM
                    size_note = " {}={:.1f} 목표긴변={:.1f} {}".format(
                        "긴변" if self.axis is None else "축참길이", longest, self.target_cm,
                        "크기맞음" if size_ok else "크기어긋남")
                    if self.axis is not None:
                        low, high = _axis_reach(asset, self.axis)
                        size_note += " 축={} 끝={:.1f} 손잡이={:.1f} 회전태그='{}'".format(
                            self.axis, high, low, unreal.EditorAssetLibrary.get_metadata_tag(asset, ROTATION_TAG))
                    if not size_ok:
                        unreal.log_error("DESIGN 3.5 도구 크기 {:.1f}cm 와 다르다({:.1f}): {}".format(
                            self.target_cm, longest, path))
                log("최종: {} [StaticMesh] 크기(cm)={:.1f}x{:.1f}x{:.1f} 피벗밑z={:.1f} 삼각형={} 머티리얼={} {}{}".format(
                    path, extent.x, extent.y, extent.z, box.min.z, triangles,
                    "+".join(slots) if slots else "(없음)",
                    "한도밖(3K)" if triangles > TRIANGLE_LIMIT else "한도안", size_note))
                if triangles > TRIANGLE_LIMIT:
                    unreal.log_error("DESIGN 4.2 의 오브젝트당 {} 삼각형을 넘었다: {}".format(TRIANGLE_LIMIT, path))
                if triangles == 0:
                    unreal.log_error("삼각형이 0 이다(껍데기 메시): {}".format(path))
                if "(빔)" in slots:
                    unreal.log_error("머티리얼 슬롯이 비었다(무채색으로 나온다): {}".format(path))
            elif isinstance(asset, unreal.Texture2D):
                # 압축 설정과 **쓰는 머티리얼**을 같이 적는다 — 법선만 NORMALMAP 이라 glTF 순서 매핑이 맞는지 보이고,
                # 참조가 비면 임포트는 됐는데 아무 데도 안 물린 텍스처라는 뜻이다(루프#70 이 밟은 상태).
                users = [p.split("/")[-1] for p in
                         unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, False)]
                log("최종: {} [Texture2D] {}x{} 압축={} 쓰는곳={}".format(
                    path, asset.blueprint_get_size_x(), asset.blueprint_get_size_y(),
                    asset.get_editor_property("compression_settings"), users if users else "(없음)"))
                if not users:
                    unreal.log_error("아무 머티리얼도 안 쓰는 텍스처다: {}".format(path))
            else:
                log("최종: {} [{}]".format(path, asset.get_class().get_name()))


TINT_PARAMETER = "Color"   # USnarePlacedStructureActor::TintParameter 와 같은 이름 — 칠하기(M13-11)가 이 칸에 색을 곱한다


def _add_tint_parameter(material_path: str) -> None:
    """glTF 머티리얼은 파라미터가 하나도 없다 — 기본색 입력 앞에 `Color`(기본 흰색) 곱하기를 끼운다.

    흰색이면 텍스처 그대로라 칠하지 않은 가구는 원본 색이고, 칠하면 텍스처에 그 색이 곱해진다(M13-37).
    이미 있으면 아무것도 안 한다(재실행 안전).
    """
    lib = unreal.MaterialEditingLibrary
    material = unreal.load_asset(material_path)
    if TINT_PARAMETER in [str(n) for n in lib.get_vector_parameter_names(material)]:
        log("칠하기 칸 이미 있음: {}".format(material_path))
        return
    prop = unreal.MaterialProperty.MP_BASE_COLOR
    source = lib.get_material_property_input_node(material, prop)
    source_output = lib.get_material_property_input_node_output_name(material, prop)
    if source is None:
        unreal.log_error("기본색 입력이 비었다 — 칠하기 칸을 못 끼운다: {}".format(material_path))
        return
    tint = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
    tint.set_editor_property("parameter_name", TINT_PARAMETER)
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    multiply = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 0)
    lib.connect_material_expressions(source, source_output, multiply, "A")
    lib.connect_material_expressions(tint, "RGB", multiply, "B")
    lib.connect_material_property(multiply, "", prop)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(material_path, only_if_is_dirty=False)
    log("칠하기 칸 끼움: {} (기본색 입력 {}:{} × {})".format(
        material_path, source.get_class().get_name(), source_output or "(기본)", TINT_PARAMETER))


for spec in FURNITURE:
    Equipment(*spec).run()
    _add_tint_parameter(Equipment(*spec).material_path)

for spec in PROPS:
    prop = Equipment(*spec)
    prop.run()
    # 좌판은 막고 부두는 밟고 선다 — 자동 상자 충돌은 부두 판자 위 빈 공간까지 막는다.
    # 그래서 모양 그대로(복잡 충돌을 단순 충돌로) 쓴다. 이미 그 값이면 저장하지 않는다(재실행 안전).
    mesh = unreal.EditorAssetLibrary.load_asset(prop.mesh_path)
    body_setup = mesh.get_editor_property("body_setup")
    if body_setup.get_editor_property("collision_trace_flag") != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
        body_setup.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        unreal.EditorAssetLibrary.save_asset(prop.mesh_path, only_if_is_dirty=False)
    log("충돌: {} {}".format(prop.mesh_path, body_setup.get_editor_property("collision_trace_flag")))

for spec in EQUIPMENT + SOFT_PROPS:
    Equipment(*spec).run()

log("장비 임포트 끝 (M5-06a 몽둥이 · M9-04 도구 2종 · M13-35 모자 10종 · M13-36 도구 4종 · M13-39 좌판·부두 · M13-40 말뚝·물고기 그림자)")
