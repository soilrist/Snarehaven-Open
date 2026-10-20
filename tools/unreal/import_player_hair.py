"""M19-01 — 기본 머리카락 덩어리 반입: `make_player_hair_mesh.py`(OBJ 원본) → `SM_Player_Hair_01` + 단색 `MI_Player_Hair`.

`make_player_hair_mesh.py` 다음에 돌린다:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/import_player_hair.py" -unattended -nosplash -log

메시 반입은 `setup_ranch_ground.py` 흐름(OBJ → 임시 폴더 → 이름 바꿔 옮김), 색은 `import_mount_saddle.py` 흐름
(`MI_Nature_Grass` 의 부모 + `DiffuseColor`, 텍스처 가중치 0). 겉모습일 뿐이라 충돌은 끈다. 재실행 안전.
"""
import os

import unreal

DEST_DIR = "/Game/Game/Characters/Player/Hair"
IMPORT_TEMP = DEST_DIR + "/_HairImport"
SOURCE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "PlayerHair.obj"))
MESH_PATH = DEST_DIR + "/SM_Player_Hair_01"
MI_NAME = "MI_Player_Hair"
PARENT_FROM = "/Game/Game/Environment/Nature/MI_Nature_Grass"   # 이 인스턴스의 부모(FBX Legacy)를 빌린다
# 팔레트이지 게임 규칙 숫자가 아니다 — 짙은 밤색(sRGB ≈ #5C3A22). 시트 "옷 한 색 + 포인트 한 색" 에서
# 초록 티·갈색 바지와 겹치지 않고 살구 피부와 대비되는 머리 색. 안장 가죽(0.200, 0.085, 0.035)보다 한 단 어둡다.
HAIR_COLOR = unreal.LinearColor(0.105, 0.042, 0.016, 1.0)

assets = unreal.EditorAssetLibrary


def _import():
    if not os.path.exists(SOURCE):
        unreal.log_error("[PlayerHair] 원본 없음 — make_player_hair_mesh.py 먼저: {}".format(SOURCE))
        raise SystemExit(1)
    task = unreal.AssetImportTask()
    task.filename = SOURCE
    task.destination_path = IMPORT_TEMP
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    made = None
    for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
        asset = assets.load_asset(path)
        if isinstance(asset, unreal.StaticMesh) and asset.get_name() == "PlayerHair":
            if assets.does_asset_exist(MESH_PATH):
                assets.delete_asset(MESH_PATH)
            assets.rename_asset(path.split(".")[0], MESH_PATH)
            made = MESH_PATH
    assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
    return made


def _material():
    path = "{}/{}".format(DEST_DIR, MI_NAME)
    if assets.does_asset_exist(path):
        mi = assets.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MI_NAME, DEST_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    parent = assets.load_asset(PARENT_FROM).get_editor_property("parent")
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "DiffuseColor", HAIR_COLOR)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "DiffuseColorMapWeight", 0.0)
    assets.save_asset(path, only_if_is_dirty=False)
    return mi


if _import() is None:
    unreal.log_error("[PlayerHair] 반입 실패")
    raise SystemExit(1)
mesh = assets.load_asset(MESH_PATH)
mesh.set_material(0, _material())
# 반입기가 Nanite 를 켜 둔다 — 모바일 렌더러는 대체 메시(자동 단순화)를 그려 1740 → 297 삼각형으로 뭉개졌다(첫 반입).
# 이 프로젝트는 Nanite 를 끈다(`import_tree_meshes.py` 와 같다)
nanite = mesh.get_editor_property("nanite_settings")
nanite.enabled = False
mesh.set_editor_property("nanite_settings", nanite)
mesh.get_editor_property("body_setup").set_editor_property(
    "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
box = mesh.get_bounding_box()
unreal.log_warning("[PlayerHair] {} 크기 {:.1f}x{:.1f}x{:.1f} 삼각형 {} 머티리얼 {}".format(
    MESH_PATH, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, mesh.get_num_triangles(0),
    mesh.get_material(0).get_path_name()))
assets.save_asset(MESH_PATH, only_if_is_dirty=False)
