"""M6-06 탈것 안장 임포트 — `saddle_make.py`(Blender 헤드리스) FBX → `SM_Mount_Saddle` + 단색 머티리얼 2개.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_mount_saddle.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=~/UNREAL/Snarehaven/logs/<이름>.log

메시는 `import_vehicle_boat.py` 흐름(Interchange 정적 메시, Nanite 끔), 색은 `setup_floor_colors.py` 흐름
(`MI_Nature_Grass` 의 부모 + `DiffuseColor`, 텍스처 가중치 0)을 그대로 쓴다. 텍스처 없는 단색 안장이다.
원본은 `_Source/raw/Snare/Characters/Equipment/Saddle/`(gitignore) — 작업트리에서는 `SNARE_SADDLE_RAW_DIR` 로 가리킨다.
재실행 안전: 메시는 지우고 다시 받고, 머티리얼은 있으면 색만 다시 맞춘다.
"""
import os

import unreal

SOURCE_DIR = os.environ.get("SNARE_SADDLE_RAW_DIR",
                            "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Equipment/Saddle")
SOURCE_FBX = os.path.join(SOURCE_DIR, "SM_Mount_Saddle.fbx")
DEST_DIR = "/Game/Game/Characters/Equipment/Saddle"
MESH_PATH = DEST_DIR + "/SM_Mount_Saddle"
PARENT_FROM = "/Game/Game/Environment/Nature/MI_Nature_Grass"   # 이 인스턴스의 부모(FBX Legacy)를 빌린다
DIFFUSE_COLOR_PARAM = "DiffuseColor"
DIFFUSE_WEIGHT_PARAM = "DiffuseColorMapWeight"

# FBX 슬롯 이름 → (머티리얼 이름, 리니어 색). 팔레트이지 게임 규칙 숫자가 아니다 — 가죽 갈색 · 짙은 끈.
SLOTS = {
    "Leather": ("MI_Mount_Saddle_Leather", unreal.LinearColor(0.200, 0.085, 0.035, 1.0)),
    "Strap": ("MI_Mount_Saddle_Strap", unreal.LinearColor(0.050, 0.025, 0.012, 1.0)),
}


def _pipeline():
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)
    pipeline.set_editor_property("scene_name_sub_folder", False)
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", "SM_Mount_Saddle")
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_skeletal_meshes", False)
    mesh.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh.set_editor_property("build_nanite", False)
    pipeline.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
    return pipeline


def _material(name, color, parent):
    path = "{}/{}".format(DEST_DIR, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.EditorAssetLibrary.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DEST_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, DIFFUSE_COLOR_PARAM, color)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, DIFFUSE_WEIGHT_PARAM, 0.0)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mi


if not os.path.isfile(SOURCE_FBX):
    unreal.log_error("원본이 없다: {}".format(SOURCE_FBX))
else:
    if unreal.EditorAssetLibrary.does_asset_exist(MESH_PATH):
        unreal.EditorAssetLibrary.delete_asset(MESH_PATH)
    override = unreal.InterchangePipelineStackOverride()
    override.add_pipeline(_pipeline())
    task = unreal.AssetImportTask()
    task.filename = SOURCE_FBX
    task.destination_path = DEST_DIR
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        unreal.log("임포트됨: {}".format(path))

    parent = unreal.EditorAssetLibrary.load_asset(PARENT_FROM).get_editor_property("parent")
    saddle = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
    slots = []
    for s in saddle.static_materials:
        want = SLOTS.get(str(s.material_slot_name))
        if want is None:
            unreal.log_error("모르는 슬롯: {}".format(s.material_slot_name))
            slots.append(s)
            continue
        slots.append(unreal.StaticMaterial(material_interface=_material(want[0], want[1], parent),
                                           material_slot_name=s.material_slot_name))
    saddle.set_editor_property("static_materials", slots)
    unreal.EditorAssetLibrary.save_asset(MESH_PATH, only_if_is_dirty=False)

    box = saddle.get_bounding_box()
    unreal.log("최종: {} 크기(cm)={:.1f}x{:.1f}x{:.1f} 최소=({:.1f},{:.1f},{:.1f}) 최대=({:.1f},{:.1f},{:.1f}) 삼각형={} 슬롯={}".format(
        MESH_PATH, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z,
        box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z, saddle.get_num_triangles(0),
        ["{}={}".format(s.material_slot_name, s.material_interface.get_name() if s.material_interface else "(빔)")
         for s in saddle.static_materials]))
    for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
        unreal.log("폴더: {}".format(path))
