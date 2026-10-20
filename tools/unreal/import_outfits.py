"""옷 메시 반입 — 감독이 몸 뼈대에 입힌 `<Name>_rig.glb` → `SK_Outfit_<Name>_01`.
M13-42 구명조끼 · M9-05 셔츠·바지. 옷이 늘면 `OUTFITS` 에 한 줄 더한다(스크립트를 복사하지 않는다).

실행 (uproject·script 둘 다 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <저장소>/Snarehaven.uproject \
      -run=pythonscript -script="<저장소>/tools/unreal/import_outfits.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=<저장소>/logs/outfit_import.log

`import_player_character.py` 와 같은 glTF Interchange 스택(일반 + glTF 전용)으로 **스켈레탈 메시**를 받는다.
다른 점 하나: 스켈레톤을 새로 만들지 않고 `SK_Player_Body` 의 스켈레톤을 지정한다 — 옷이 몸과 같은
스켈레톤이어야 `SetLeaderPoseComponent` 로 몸 뼈를 따라간다(결정 006 · DESIGN 13절 「구명조끼 옷 메시」·「셔츠·바지」).
원본은 감독이 Blender 에서 몸 `standing_idle_1.glb` 뼈 23개에 맞춰 입힌 것(`LifeJacket_fit.py`·`Outfit_fit.py`)이고
삼각형이 3000(DESIGN 표 상한) 안이라 감축하지 않는다. 원본은 본 저장소에만 있다(gitignore) — 절대경로로 읽는다.

색: glTF 전용 파이프라인이 만드는 머티리얼 인스턴스의 텍스처 파라미터로 역할(BaseColor·ORM·Normal)을
읽어 이름을 붙이고, 기본색 텍스처가 비어 있으면 오류로 남긴다(STATUS 「임포트한 에셋 색」 — 흰색 함정).

재실행 안전: 메시가 있고 스켈레톤이 몸 것이면 다시 받지 않고 이름 정리·보고만 한다.
원본을 다시 뽑았으면 환경변수 `SNARE_OUTFIT_REIMPORT=MeadowKnit,HarborSailor` 처럼 이름을 주면 그 옷 폴더 에셋을
지우고 처음처럼 받는다 — 같은 경로·이름으로 다시 생겨 표의 메시 참조는 그대로다(덮어 받기는 재질·텍스처가
원본 이름으로 한 벌 더 생겨 이름 충돌이 난다).
로그: 5.8 커맨드릿은 `unreal.log()` 를 삼킨다(STATUS 루프#157) — 전부 `log_warning`.
"""
import os

import unreal

SOURCE_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Outfits"
DEST_ROOT = "/Game/Game/Characters/Equipment/Outfits"
# 옷 이름(파스칼) — 원본 `<Name>/<Name>_rig.glb`, 목적지 `<Name>/SK_Outfit_<Name>_01`.
OUTFITS = ("LifeJacket", "FlannelShirt", "WorkPants")
# 스킨 조각 h 꾸미기 상의 6벌 — 원본은 `Items/Tops/<Name>/<Name>_rig.glb`(`tops_make.py`, 셔츠 틀과 같은 몸 복사 길).
TOPS_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Tops"
COSMETIC_TOPS = ("MeadowTee", "MeadowKnit", "MeadowPetalBlouse", "HarborStripeTee", "HarborPuffer", "HarborSailor",
                 # M83-04(루프#859) — `tops_make.py` 에 무늬만 다른 셋을 더함
                 "MeadowGingham", "HarborIslandShirt", "MeadowBeeSweater",
                 # M83-05(루프#861)
                 "MeadowCherryTee", "HarborAnchorSweat", "MeadowPatchwork",
                 # M84-04(루프#867)
                 "MeadowLeafTee", "HarborWaveSweat", "MeadowRainbow",
                 # M84-05(루프#868)
                 "HarborFishTee", "MeadowStrawberry", "HarborSunset",
                 # M88-04(루프#892)
                 "HarborZigzag", "MeadowToadstool", "HarborAurora",
                 # M88-05(루프#893)
                 "MeadowCactusTee", "HarborSnowSweat", "MeadowHoneycomb",
                 # M90-03(루프#900)
                 "HarborGullTee", "MeadowPineSweat", "HarborFlame",
                 # M90-04(루프#901)
                 "MeadowDaisyTee", "MeadowDiamondSweat", "HarborGalaxy",
                 # M91-03(루프#906)
                 "MeadowSunTee", "HarborKelpSweat", "MeadowFirefly",
                 # M91-04(루프#907)
                 "HarborShellTee", "HarborMoonSweat", "HarborLantern",
                 # M92-01(루프#913)
                 "MeadowAcornTee", "HarborRopeSweat", "HarborCoral",
                 # M92-02(루프#914)
                 "MeadowCloverTee", "HarborCompassSweat", "MeadowButterfly",
                 # M92-03(루프#915)
                 "MeadowPlumTee", "HarborMeteor")
# 스킨 조각 i 꾸미기 하의 6벌 — 원본은 `Items/Bottoms/<Name>/<Name>_rig.glb`(`bottoms_make.py`, 바지 틀과 같은 몸 복사 길).
BOTTOMS_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Bottoms"
COSMETIC_BOTTOMS = ("MeadowShorts", "MeadowCorduroy", "MeadowPetalSkirt", "HarborCargo", "HarborRainPants",
                    "HarborBellBottoms",
                    # M83-04(루프#859) — `bottoms_make.py` 에 둘을 더함
                    "HarborCapri", "MeadowStarPants",
                    # M83-05(루프#861)
                    "HarborSailShorts", "MeadowPatchPants",
                    # M84-04(루프#867)
                    "HarborCheckShorts", "MeadowDenim",
                    # M84-05(루프#868)
                    "MeadowPlaidShorts", "HarborPolka",
                    # M88-04(루프#892)
                    "MeadowCloudShorts", "HarborCamo",
                    # M88-05(루프#893)
                    "MeadowMoonShorts", "HarborArgyle",
                    # M90-03(루프#900)
                    "MeadowLadybugShorts", "HarborTartan",
                    # M90-04(루프#901)
                    "MeadowCherryShorts", "HarborPinstripe",
                    # M91-03(루프#906)
                    "MeadowLeafShorts", "HarborWavePants",
                    # M91-04(루프#907)
                    "MeadowPawShorts", "HarborBoltPants",
                    # M92-01(루프#913)
                    "MeadowShroomShorts", "HarborChevron",
                    # M92-02(루프#914)
                    "MeadowRaindropShorts", "HarborWeave",
                    # M92-03(루프#915)
                    "MeadowSproutShorts", "HarborDiveFlag")
# 스킨 조각 j 꾸미기 신발 6벌 — 원본은 `Items/Shoes/<Name>/<Name>_rig.glb`(`shoes_make.py`, 하의와 같은 몸 복사 길).
SHOES_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Shoes"
COSMETIC_SHOES = ("MeadowCanvas", "MeadowClogs", "MeadowPetalBoots", "HarborDeckShoes", "HarborRainBoots",
                  "HarborFlippers",
                  # M87-03(루프#885)
                  "HarborHighTops", "MeadowStarKicks", "MeadowBerryBoots", "MeadowWingKicks")
# 스킨 조각 k 꾸미기 장갑 6벌 — 원본은 `Items/Gloves/<Name>/<Name>_rig.glb`(`gloves_make.py`, 신발과 같은 몸 복사 길).
GLOVES_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Gloves"
COSMETIC_GLOVES = ("MeadowGardenGloves", "MeadowMittens", "MeadowPetalGloves", "HarborFingerless",
                   "HarborRubberGloves", "HarborCrabGloves",
                   # M87-04(루프#886)
                   "HarborLeatherGloves", "MeadowWoolGloves", "HarborArmSleeves")
BODY_SKELETON = "/Game/Game/Characters/Player/Snare/SK_Player_Body_Skeleton.SK_Player_Body_Skeleton"
GLTF_PIPELINE = "/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"
TRIANGLE_LIMIT = 3000   # DESIGN 13절 「구명조끼 옷 메시」·「셔츠·바지」 삼각형 — 설계서 값
# glTF 머티리얼 인스턴스의 텍스처 파라미터 → 이름 꼬리. ORM 은 한 장이 두 파라미터에 같이 물린다.
TEXTURE_ROLES = (("BaseColorTexture", "BaseColor"), ("NormalTexture", "Normal"),
                 ("MetallicRoughnessTexture", "ORM"), ("OcclusionTexture", "ORM"))

# 다시 받을 옷 이름(파스칼) — 위 설명 「재실행 안전」.
REIMPORT = {n.strip() for n in os.environ.get("SNARE_OUTFIT_REIMPORT", "").split(",") if n.strip()}

log = unreal.log_warning
lib = unreal.EditorAssetLibrary


def _source(name: str) -> str:
    folder = (TOPS_DIR if name in COSMETIC_TOPS else BOTTOMS_DIR if name in COSMETIC_BOTTOMS
              else SHOES_DIR if name in COSMETIC_SHOES else GLOVES_DIR if name in COSMETIC_GLOVES else SOURCE_DIR)
    return "{0}/{1}/{1}_rig.glb".format(folder, name)


def _dest(name: str) -> str:
    return "{}/{}".format(DEST_ROOT, name)


def _mesh_name(name: str) -> str:
    return "SK_Outfit_{}_01".format(name)


def _pipelines(name: str, skeleton) -> list:
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)
    pipeline.set_editor_property("scene_name_sub_folder", False)
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", _mesh_name(name))

    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)     # 새 스켈레톤을 만들지 않는다

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_static_meshes", False)
    mesh.set_editor_property("create_physics_asset", False)   # 옷은 부딪히지 않는다(컴포넌트 충돌 끔)
    mesh.set_editor_property("update_skeleton_reference_pose", False)   # 몸 스켈레톤을 건드리지 않는다
    mesh.set_editor_property(
        "combine_skeletal_meshes_behavior", unreal.InterchangeCombineSkeletalMeshesBehavior.BY_SKELETON)

    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    return [pipeline, unreal.load_asset(GLTF_PIPELINE)]


def _import(name: str, skeleton) -> None:
    override = unreal.InterchangePipelineStackOverride()
    for pipeline in _pipelines(name, skeleton):
        override.add_pipeline(pipeline)
    task = unreal.AssetImportTask()
    task.filename = _source(name)
    task.destination_path = _dest(name)
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        log("임포트됨: {}".format(path))


def _assets(name: str) -> list:
    return [(p, lib.load_asset(p)) for p in lib.list_assets(_dest(name), recursive=True, include_folder=False)]


def _rename(path: str, want: str) -> None:
    new_path = "{}/{}".format(path.rsplit("/", 1)[0], want)
    if path.split(".")[0] == new_path:
        return
    if lib.does_asset_exist(new_path):
        unreal.log_error("이름 충돌로 못 바꿈: {} -> {}".format(path, new_path))
    elif lib.rename_asset(path, new_path):
        log("이름 바꿈: {} -> {}".format(path, new_path))
    else:
        unreal.log_error("이름 바꾸기 실패: {} -> {}".format(path, new_path))


def _tidy(name: str) -> None:
    """메시·머티리얼은 종류로, 텍스처는 인스턴스가 물린 파라미터로 이름을 정한다. 몸 스켈레톤이 아닌
    스켈레톤·물리 에셋이 생겼으면 지운다(메시는 몸 스켈레톤을 가리키므로 참조가 끊기지 않는다)."""
    body = "Outfit_" + name
    for path, asset in _assets(name):
        if isinstance(asset, unreal.SkeletalMesh):
            _rename(path, _mesh_name(name))
        elif isinstance(asset, unreal.MaterialInstance):
            for param, tail in TEXTURE_ROLES:
                texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(asset, param)
                if texture is not None and texture.get_path_name().startswith(_dest(name)):
                    _rename(texture.get_path_name(), "T_{}_{}".format(body, tail))
            _rename(path, "MI_" + body)
        elif isinstance(asset, unreal.MaterialInterface):
            _rename(path, "M_" + body)
        elif isinstance(asset, (unreal.Skeleton, unreal.PhysicsAsset)):
            if lib.delete_asset(path):
                log("지움(새로 생긴 {}): {}".format(asset.get_class().get_name(), path))


def _relink(name: str, imported: bool) -> None:
    """메시 재질 칸을 `MI_Outfit_<Name>` 에 다시 건다. `_tidy` 가 인스턴스 이름을 바꿔도 메시는 더럽혀지지 않아
    디스크의 메시가 지워진 옛 이름(`M_<원본>`)을 가리킨 채 남았다 — 게임에서 기본 격자 재질로 보였다(루프#460 3층).
    방금 반입했거나 칸이 바뀌었으면 메시를 강제로 저장한다(안 바뀐 조끼는 그대로 둔다)."""
    mesh_path = "{}/{}".format(_dest(name), _mesh_name(name))
    mi_path = "{}/MI_Outfit_{}".format(_dest(name), name)
    mesh = lib.load_asset(mesh_path) if lib.does_asset_exist(mesh_path) else None
    mi = lib.load_asset(mi_path) if lib.does_asset_exist(mi_path) else None
    if mesh is None or mi is None:
        unreal.log_error("재질을 다시 걸 메시·인스턴스가 없다: {} · {}".format(mesh_path, mi_path))
        return
    # 배열 원소는 사본이라 고친 원소로 새 배열을 만들어 통째로 다시 넣는다.
    materials = []
    changed = False
    for slot in mesh.get_editor_property("materials"):
        if slot.material_interface is None or slot.material_interface.get_path_name() != mi.get_path_name():
            slot.set_editor_property("material_interface", mi)
            changed = True
        materials.append(slot)
    if changed:
        mesh.set_editor_property("materials", materials)
    # 파랑이 센 무늬(물고기 티, 루프#868)는 반입기가 노말맵(TC_NORMALMAP·sRGB 끔)으로 오인해 옷이 연두로 보였다 — 색 그림으로 고정.
    tex_path = "{}/T_Outfit_{}_BaseColor".format(_dest(name), name)
    tex = lib.load_asset(tex_path) if lib.does_asset_exist(tex_path) else None
    if tex is not None and (tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_DEFAULT
                            or not tex.get_editor_property("srgb")):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", True)
        lib.save_asset(tex_path, only_if_is_dirty=False)
        log("무늬 그림을 색 그림으로 고정: {}".format(tex_path))
    if changed or imported:
        lib.save_asset(mesh_path, only_if_is_dirty=False)
        log("재질 다시 걸고 저장: {} -> {} (칸 바뀜 {} · 방금 반입 {})".format(mesh_path, mi.get_name(), changed, imported))


def _report(name: str, body_skeleton) -> None:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([_dest(name)], force_rescan=True)
    for path, asset in _assets(name):
        if asset is None:
            unreal.log_error("못 읽음: {}".format(path))
        elif isinstance(asset, unreal.SkeletalMesh):
            box = asset.get_bounds().box_extent * 2.0
            origin = asset.get_bounds().origin
            skeleton = asset.get_editor_property("skeleton")
            # 파이썬에 스켈레탈 삼각형 수를 주는 함수가 없으면 -1 — 삼각형 한도는 2층 C++ 시험이 잰다.
            tris = asset.get_num_triangles(0) if hasattr(asset, "get_num_triangles") else -1
            materials = [m.material_interface.get_name() if m.material_interface else "(빔)"
                         for m in asset.get_editor_property("materials")]
            log("최종: {} [SkeletalMesh] 크기(cm)={:.1f}x{:.1f}x{:.1f} 가운데z={:.1f} 삼각형={} 스켈레톤={} 머티리얼={}".format(
                path, box.x, box.y, box.z, origin.z, tris, skeleton.get_name() if skeleton else "(없음)",
                "+".join(materials)))
            if skeleton != body_skeleton:
                unreal.log_error("스켈레톤이 몸 것이 아니다: {}".format(skeleton))
            if tris > TRIANGLE_LIMIT:
                unreal.log_error("삼각형 {} > {}".format(tris, TRIANGLE_LIMIT))
        elif isinstance(asset, unreal.MaterialInstance):
            base = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(asset, "BaseColorTexture")
            log("최종: {} [{}] 기본색텍스처={}".format(path, asset.get_class().get_name(),
                                                  base.get_name() if base else "(없음 — 흰색)"))
            if base is None:
                unreal.log_error("기본색 텍스처가 비었다 — 흰색으로 나온다: {}".format(path))
        elif isinstance(asset, unreal.Texture2D):
            log("최종: {} [Texture2D] {}x{} 압축={}".format(path, asset.blueprint_get_size_x(),
                                                        asset.blueprint_get_size_y(),
                                                        asset.get_editor_property("compression_settings")))
        else:
            log("최종: {} [{}]".format(path, asset.get_class().get_name()))


def _run(name: str, body_skeleton) -> None:
    mesh_path = "{}/{}".format(_dest(name), _mesh_name(name))
    if name in REIMPORT and os.path.exists(_source(name)):
        for path, _ in _assets(name):
            if lib.delete_asset(path):
                log("다시 받으려고 지움: {}".format(path))
    existing = lib.load_asset(mesh_path) if lib.does_asset_exist(mesh_path) else None
    imported = False
    if not os.path.exists(_source(name)):
        unreal.log_error("원본이 없다: {}".format(_source(name)))
    elif existing is not None and existing.get_editor_property("skeleton") == body_skeleton:
        log("이미 있음 — 다시 임포트하지 않는다: {}".format(mesh_path))
    else:
        _import(name, body_skeleton)
        imported = True
    _tidy(name)
    # 바뀐 것만 저장 — 이미 있는 옷(조끼)은 다시 쓰지 않아 .uasset 이 그대로 남는다.
    lib.save_directory(_dest(name), only_if_is_dirty=True, recursive=True)
    _relink(name, imported)
    _report(name, body_skeleton)


body_skeleton = unreal.load_asset(BODY_SKELETON)
if body_skeleton is None:
    unreal.log_error("몸 스켈레톤이 없다: {}".format(BODY_SKELETON))
else:
    for outfit in OUTFITS + COSMETIC_TOPS + COSMETIC_BOTTOMS + COSMETIC_SHOES + COSMETIC_GLOVES:
        _run(outfit, body_skeleton)
