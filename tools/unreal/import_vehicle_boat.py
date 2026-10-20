"""M6 구명보트 메시 임포트 — 바르코가 만든 구명보트 1종 → `SM_Vehicle_Boat_01` (`ABoatActor` 의 Hull).

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_vehicle_boat.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=logs/import_vehicle_boat.log

`import_equipment_weapons.py`(몽둥이)와 **같은 흐름**이다 — 이유는 그 파일 머리말에 있다:
glb 로 색(텍스처 3장·머티리얼)을 먼저 받고 FBX 로 몸체를 받은 뒤 슬롯을 갈아 끼우고,
LOD0 을 DESIGN 4.2 의 오브젝트당 3000 삼각형 안으로 줄인다. 다른 점은 둘이다:
  * 이름이 `docs/engines/unreal.md` 텍스처 접미어(`_D`·`_M`(ORM)·`_N`)를 따른다.
  * 원본은 바르코가 한 변 1m 로 정규화해 내보낸 것이라(길이 100cm) 게임 크기는 `ABoatActor::HullLengthCm`
    이 균등 배율로 맞춘다 — 여기서 크기를 바꾸지 않는다.

원본은 `_Source/raw/Snare/Items/Vehicles/Boat/`(gitignore) — 프롬프트·노드 기록은 같은 폴더 `Boat_concept.txt`.
"""
import os

import unreal

SOURCE_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Vehicles/Boat"
SOURCE_FBX = os.path.join(SOURCE_DIR, "Boat_v1.fbx")
SOURCE_GLB = os.path.join(SOURCE_DIR, "Boat_v1.glb")
DEST_DIR = "/Game/Game/Environment/Vehicles/Boat"
ASSET_NAME = "SM_Vehicle_Boat_01"
GLTF_TEMP_NAME = "SM_Vehicle_Boat_01_FromGltf"   # 텍스처·머티리얼만 받으려고 잠깐 만드는 메시. 끝에 지운다.
MATERIAL_NAME = "M_Vehicle_Boat"

# glTF 이미지 순서 (`Boat_v1.glb` 의 `materials[0]`: baseColor=0 · occlusion/metallicRoughness=1 · normal=2).
TEXTURE_NAMES_IN_GLTF_ORDER = ["T_Vehicle_Boat_D", "T_Vehicle_Boat_M", "T_Vehicle_Boat_N"]

TRIANGLE_LIMIT = 3000   # DESIGN 2·4.2 "오브젝트당 3K 이하" — 설계서 값

MESH_PATH = "{}/{}".format(DEST_DIR, ASSET_NAME)
MATERIAL_PATH = "{}/{}".format(DEST_DIR, MATERIAL_NAME)


def _pipeline(asset_name):
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)
    pipeline.set_editor_property("scene_name_sub_folder", False)
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", asset_name)
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_skeletal_meshes", False)
    mesh.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh.set_editor_property("build_nanite", False)   # 모바일 렌더러라 Nanite 를 끈 프로젝트다
    return pipeline


def _import(source_file, asset_name):
    override = unreal.InterchangePipelineStackOverride()
    override.add_pipeline(_pipeline(asset_name))
    task = unreal.AssetImportTask()
    task.filename = source_file
    task.destination_path = DEST_DIR
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


def _assets_of(kind):
    found = []
    for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
        if isinstance(unreal.EditorAssetLibrary.load_asset(path), kind):
            found.append(path)
    return sorted(found)


def _rename(path, new_path):
    if unreal.EditorAssetLibrary.rename_asset(path, new_path):
        unreal.log("이름 바꿈: {} -> {}".format(path, new_path))
    else:
        unreal.log_error("이름 바꾸기 실패: {} -> {}".format(path, new_path))


def _reduce_to_limit(mesh):
    before = mesh.get_num_triangles(0)
    if before <= TRIANGLE_LIMIT:
        unreal.log("감축 안 함 — 이미 한도 안: 삼각형 {}".format(before))
        return
    unreal.load_module("StaticMeshEditor")   # 커맨드릿은 스스로 안 올린다 — 안 올리면 서브시스템이 None
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is None:
        unreal.log_error("StaticMeshEditorSubsystem 이 없다 — 감축 못 함")
        return
    settings = unreal.MeshReductionSettings()
    # 목표 비율을 조금 낮게 잡는다 — 감축기가 목표를 몇 개 넘겨 끝내는 일이 있다.
    settings.set_editor_property("percent_triangles", float(TRIANGLE_LIMIT) * 0.97 / float(before))
    subsystem.set_lod_reduction_settings(mesh, 0, settings)
    unreal.log("LOD0 감축: 삼각형 {} -> {} (한도 {})".format(before, mesh.get_num_triangles(0), TRIANGLE_LIMIT))


def _import_everything():
    for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
        if unreal.EditorAssetLibrary.delete_asset(path):
            unreal.log("비움: {}".format(path))

    # 1) 색 먼저 — glb 의 머티리얼·텍스처 (순서가 바뀌면 FBX 의 빈 `material_0` 을 재사용한다).
    for path in _import(SOURCE_GLB, GLTF_TEMP_NAME):
        unreal.log("임포트됨(glb): {}".format(path))
    textures = _assets_of(unreal.Texture2D)
    if len(textures) != len(TEXTURE_NAMES_IN_GLTF_ORDER):
        unreal.log_error("텍스처가 {}장이 아니라 {}장이다: {}".format(len(TEXTURE_NAMES_IN_GLTF_ORDER), len(textures), textures))
    else:
        for path, want in zip(textures, TEXTURE_NAMES_IN_GLTF_ORDER):
            _rename(path, "{}/{}".format(DEST_DIR, want))
    materials = _assets_of(unreal.MaterialInterface)
    if len(materials) != 1:
        unreal.log_error("glb 머티리얼이 1개가 아니라 {}개다: {}".format(len(materials), materials))
    else:
        _rename(materials[0], MATERIAL_PATH)

    # 2) 몸체는 FBX 에서 (pivotToBottom — 바닥 z=0).
    for path in _import(SOURCE_FBX, ASSET_NAME):
        unreal.log("임포트됨(FBX): {}".format(path))

    # 3) 슬롯을 **새 구조체로** 통째로 바꾼다 (static_materials 는 복사본을 돌려준다).
    boat = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL_PATH)
    if isinstance(boat, unreal.StaticMesh) and isinstance(material, unreal.MaterialInterface):
        slots = [unreal.StaticMaterial(material_interface=material, material_slot_name=s.material_slot_name)
                 for s in boat.static_materials]
        boat.set_editor_property("static_materials", slots)
        unreal.log("머티리얼 물림: {} 슬롯 {}개 <- {}".format(ASSET_NAME, len(slots), material.get_name()))
    else:
        unreal.log_error("메시나 머티리얼을 못 읽었다 — 보트가 무채색으로 남는다")

    keep = (ASSET_NAME, MATERIAL_NAME) + tuple(TEXTURE_NAMES_IN_GLTF_ORDER)
    for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
        if path.split("/")[-1].split(".")[0] in keep:
            continue
        if unreal.EditorAssetLibrary.delete_asset(path):
            unreal.log("지움(임시): {}".format(path))
        else:
            unreal.log_error("지우기 실패: {}".format(path))

    if isinstance(boat, unreal.StaticMesh):
        _reduce_to_limit(boat)
    unreal.EditorAssetLibrary.save_directory(DEST_DIR, only_if_is_dirty=False, recursive=True)


def _already_done():
    if not (unreal.EditorAssetLibrary.does_asset_exist(MESH_PATH)
            and unreal.EditorAssetLibrary.does_asset_exist(MATERIAL_PATH)):
        return False
    keep = [MESH_PATH, MATERIAL_PATH] + ["{}/{}".format(DEST_DIR, n) for n in TEXTURE_NAMES_IN_GLTF_ORDER]
    for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
        if path.split(".")[0] not in keep:
            return False
    mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
    return isinstance(mesh, unreal.StaticMesh) and 0 < mesh.get_num_triangles(0) <= TRIANGLE_LIMIT


if not os.path.exists(SOURCE_FBX) or not os.path.exists(SOURCE_GLB):
    unreal.log_error("원본이 없다: {} / {}".format(SOURCE_FBX, SOURCE_GLB))
elif _already_done():
    unreal.log("이미 있음 — 다시 임포트하지 않는다: {}".format(MESH_PATH))
else:
    _import_everything()

# 4) 새로 읽어 확인 — 크기·피벗·삼각형·슬롯을 값으로 남긴다.
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([DEST_DIR], force_rescan=True)
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if isinstance(asset, unreal.StaticMesh):
        box = asset.get_bounding_box()
        extent = box.max - box.min
        triangles = asset.get_num_triangles(0)
        slots = [s.material_interface.get_name() if s.material_interface is not None else "(빔)"
                 for s in asset.static_materials]
        unreal.log("최종: {} [StaticMesh] 크기(cm)={:.1f}x{:.1f}x{:.1f} 최소=({:.1f},{:.1f},{:.1f}) 삼각형={} 머티리얼={} {}".format(
            path, extent.x, extent.y, extent.z, box.min.x, box.min.y, box.min.z, triangles,
            "+".join(slots) if slots else "(없음)", "한도밖(3K)" if triangles > TRIANGLE_LIMIT else "한도안"))
        if triangles > TRIANGLE_LIMIT or triangles == 0 or "(빔)" in slots:
            unreal.log_error("메시 검사 실패: {}".format(path))
    elif isinstance(asset, unreal.Texture2D):
        users = [p.split("/")[-1] for p in unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, False)]
        unreal.log("최종: {} [Texture2D] {}x{} 압축={} 쓰는곳={}".format(
            path, asset.blueprint_get_size_x(), asset.blueprint_get_size_y(),
            asset.get_editor_property("compression_settings"), users if users else "(없음)"))
        if not users:
            unreal.log_error("아무 머티리얼도 안 쓰는 텍스처다: {}".format(path))
    elif asset is not None:
        unreal.log("최종: {} [{}]".format(path, asset.get_class().get_name()))

unreal.log("구명보트 임포트 끝")
