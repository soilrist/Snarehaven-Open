"""M5-02a 사냥터 자연 에셋 임포트 — Kenney Nature Kit + Quaternius Stylized Nature MegaKit → `SM_Nature_*`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_nature_assets.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본 zip 과 푼 폴더는 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
라이선스는 둘 다 CC0 1.0 — `docs/ASSET_LICENSES.md` 표(2026-09-20 행).

감독 지시(INBOX 2026-09-20 12:30)의 M5-02 몫: 자연 = Kenney Nature Kit(FBX) + Quaternius Nature MegaKit(FBX).
두 팩을 통째로 넣지 않고 **사냥터에 실제로 깔 것만 골라서** 넣는다(아래 `MODELS`, 14개) — 팩 전체는
Kenney 330 + Quaternius 68 이라 콘텐츠 브라우저가 쓰레기통이 된다. 역할은 **삼각형 수가 갈랐다**:
큰 나무·그루터기·통나무·버섯·꽃은 Kenney(나무 한 그루 1K 아래), 덤불·풀·바위·자갈은 Quaternius.
Quaternius 의 큰 나무(`CommonTree_1` 6265 · `Pine_1` 3947 · `Pine_3` 4964 삼각형)는
DESIGN 4.2 의 **오브젝트당 3K** 를 넘어서 뺐다 — 루프#68 이 실제로 임포트해 재 보고 바꾼 선택이다.
배치는 M5-02b 몫이고 이 조각은 에셋을 들여놓는 데까지다. 크기가 팩마다 다르다(아래 로그) — 배치 배율은 M5-02b.

임포트한 에셋 이름은 FBX 노드 이름 그대로 나오므로 여기서 프로젝트 접두어 규칙
(`docs/engines/unreal.md` 표: `SM_`/`M_`/`MI_`/`T_`)으로 바꿔 준다. 이미 있으면 다시 임포트하지 않는다(재실행 안전).
"""
import os

import unreal

KENNEY_DIR = "~/UNREAL/Snarehaven/_Source/raw/Kenney_NatureKit/unzipped/Models/FBX format"
QUATERNIUS_DIR = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_NatureMegaKit/unzipped/FBX"

DEST_DIR = "/Game/Game/Environment/Nature"
ASSET_BODY = "Nature"
TRIANGLE_LIMIT = 3000   # DESIGN 2·4.2 "오브젝트당 3K 이하" — 루프가 만든 숫자가 아니라 설계서 값

# (원본 파일, 만들 에셋 이름) — 고른 이유는 위 문서 주석. 이름은 `SM_Nature_<종류>_<번호>`.
MODELS = [
    (os.path.join(KENNEY_DIR, "tree_default.fbx"), "SM_Nature_Tree_01"),
    (os.path.join(KENNEY_DIR, "tree_oak.fbx"), "SM_Nature_Tree_02"),
    (os.path.join(KENNEY_DIR, "tree_pineDefaultA.fbx"), "SM_Nature_Pine_01"),
    (os.path.join(KENNEY_DIR, "tree_pineTallA.fbx"), "SM_Nature_Pine_02"),
    (os.path.join(KENNEY_DIR, "stump_round.fbx"), "SM_Nature_Stump_01"),
    (os.path.join(KENNEY_DIR, "log.fbx"), "SM_Nature_Log_01"),
    (os.path.join(KENNEY_DIR, "mushroom_redGroup.fbx"), "SM_Nature_Mushroom_01"),
    (os.path.join(KENNEY_DIR, "flower_yellowA.fbx"), "SM_Nature_Flower_01"),
    (os.path.join(QUATERNIUS_DIR, "Bush_Common.fbx"), "SM_Nature_Bush_01"),
    (os.path.join(QUATERNIUS_DIR, "Rock_Medium_1.fbx"), "SM_Nature_Rock_01"),
    (os.path.join(QUATERNIUS_DIR, "Rock_Medium_2.fbx"), "SM_Nature_Rock_02"),
    (os.path.join(QUATERNIUS_DIR, "Grass_Common_Tall.fbx"), "SM_Nature_Grass_01"),
    (os.path.join(QUATERNIUS_DIR, "Grass_Wispy_Short.fbx"), "SM_Nature_Grass_02"),
    (os.path.join(QUATERNIUS_DIR, "Pebble_Round_1.fbx"), "SM_Nature_Pebble_01"),
    # M15-04 — 케니 원뿔 소나무는 레퍼런스의 층층이 톱니 잎 소나무(steam_17)와 딴판이다. 쿼터니우스 층층 소나무·둥근 활엽수.
    (os.path.join(QUATERNIUS_DIR, "Pine_1.fbx"), "SM_Nature_Pine_03"),
    (os.path.join(QUATERNIUS_DIR, "Pine_3.fbx"), "SM_Nature_Pine_04"),
    (os.path.join(QUATERNIUS_DIR, "CommonTree_1.fbx"), "SM_Nature_Tree_03"),
]
# `setup_zones.py` 가 만드는 색 바꾼 잎 MI — 메시 슬롯에 없고 레벨 HISM 이 덮어쓴다. 안 쓰는 것으로 보고 지우면 안 된다.
KEEP_MATERIAL_PREFIX = "MI_Nature_Leafs_"



def _pipelines(asset_name: str) -> list:
    """FBX 한 파일 = 정적 메시 한 개. 기본값대로 두면 `<파일이름>/StaticMeshes/` 하위 폴더로 들어간다."""
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)   # `StaticMeshes/` 하위 폴더 없이
    pipeline.set_editor_property("scene_name_sub_folder", False)    # `<파일이름>/` 없이
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", asset_name)          # 합친 메시 이름을 여기서 못 박는다

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_skeletal_meshes", False)       # 자연물은 전부 정적 메시다
    mesh.set_editor_property(
        "combine_static_meshes_behavior",
        unreal.InterchangeCombineStaticMeshesBehavior.ALL)          # 나뭇잎·줄기를 한 덩어리로
    # Nanite 는 이 프로젝트가 끈 기능이다(DESIGN 4.2 모바일 렌더러) — 데이터만 늘어난다.
    mesh.set_editor_property("build_nanite", False)
    return [pipeline]


def _import(file_path: str, asset_name: str) -> list:
    override = unreal.InterchangePipelineStackOverride()
    for pipeline in _pipelines(asset_name):
        override.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.filename = file_path
    task.destination_path = DEST_DIR
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


def _short(name: str) -> str:
    """원본 이름에서 꼬리표만 남기고 첫 글자를 대문자로: `colorRed` → `ColorRed` (폴더·에셋은 PascalCase)."""
    tail = name
    for prefix in ("MI_", "M_", "T_", "SM_"):
        if tail.startswith(prefix):
            tail = tail[len(prefix):]
            break
    tail = tail.strip("_")
    if not tail:
        return "Base"
    return tail[0].upper() + tail[1:]


def _prefixed(prefix: str, name: str) -> str:
    """이미 규칙대로면 그대로 둔다 — 여러 번 돌려도 이름이 자라지 않게 (루프#47 사고)."""
    if name.startswith(prefix):
        return name
    return prefix + _short(name)


def _target_name(asset) -> str:
    name = asset.get_name()
    if isinstance(asset, unreal.StaticMesh):
        return name                                     # 임포트할 때 이미 이름을 못 박았다
    if isinstance(asset, unreal.MaterialInstance):
        return _prefixed("MI_{}_".format(ASSET_BODY), name)
    if isinstance(asset, unreal.MaterialInterface):
        return _prefixed("M_{}_".format(ASSET_BODY), name)
    if isinstance(asset, unreal.Texture):
        return _prefixed("T_{}_".format(ASSET_BODY), name)
    return name


def _triangle_count(mesh) -> int:
    """LOD 0 삼각형 수 — DESIGN 4.2 의 오브젝트당 3K 한도를 로그로 확인하려고 센다."""
    return mesh.get_num_triangles(0)


# 1) 임포트 — 이름 바꾸기는 나중에 한 번에. 머티리얼 재사용(`bReuseExistingMaterials`)이
#    같은 폴더의 **같은 이름**을 찾는 방식이라, 중간에 이름을 바꾸면 팩마다 사본이 생긴다.
for file_path, asset_name in MODELS:
    asset_path = "{}/{}".format(DEST_DIR, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log("이미 있음: {}".format(asset_path))
        continue
    if not os.path.exists(file_path):
        unreal.log_error("원본이 없다(zip 을 안 풀었나): {}".format(file_path))
        continue
    for path in _import(file_path, asset_name):
        unreal.log("임포트됨: {}".format(path))

# 2) 임포트 직후에도, 재실행 때도 같은 규칙으로 이름을 정리한다.
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        continue
    want = _target_name(asset)
    if asset.get_name() == want:
        continue
    new_path = "{}/{}".format(path.rsplit("/", 1)[0], want)
    if unreal.EditorAssetLibrary.does_asset_exist(new_path):
        unreal.log_error("이름 충돌로 못 바꿈: {} → {}".format(path, new_path))
        continue
    if unreal.EditorAssetLibrary.rename_asset(path, new_path):
        unreal.log("이름 바꿈: {} → {} ({})".format(path, new_path, asset.get_class().get_name()))
    else:
        unreal.log_error("이름 바꾸기 실패: {} → {}".format(path, new_path))

# 3) 아무 메시도 안 쓰는 머티리얼은 지운다. FBX 한 파일이 팩 전체의 머티리얼 목록을 들고 있어서,
#    고르지 않은 모델(TwistedTree·PathRocks …)의 것까지 딸려 들어온다.
#    **역참조(find_package_referencers_for_asset)로 고르면 안 된다** — 갓 임포트한 메시는 아직
#    에셋 레지스트리에 안 잡혀 "쓰는 곳 없음" 으로 나오고, 루프#68 첫 실행이 실제로 13개를 다 지웠다.
#    메시의 머티리얼 슬롯을 직접 읽어 쓰는 것만 남긴다.
# 3K 한도(DESIGN 4.2)를 넘는 원본은 LOD0 을 줄인다 — 쿼터니우스 나무는 삼각형 3947~6265 (M15-04 첫 임포트 로그).
unreal.load_module("StaticMeshEditor")   # 커맨드릿은 스스로 안 올린다 — 안 올리면 서브시스템이 None
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=False, include_folder=False):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh) or mesh.get_num_triangles(0) <= TRIANGLE_LIMIT:
        continue
    before = mesh.get_num_triangles(0)
    settings = unreal.MeshReductionSettings()
    # 목표를 조금 낮게 — 감축기가 목표를 몇 개 넘겨 끝내는 일이 있다(`import_vehicle_boat.py` 와 같은 수).
    settings.set_editor_property("percent_triangles", float(TRIANGLE_LIMIT) * 0.97 / float(before))
    meshes.set_lod_reduction_settings(mesh, 0, settings)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    unreal.log("LOD0 감축: {} 삼각형 {} -> {}".format(mesh.get_name(), before, mesh.get_num_triangles(0)))

# 쿼터니우스 FBX 는 색을 안 실어 와 흰 잎·흰 줄기로 뜬다 — 오버라이드 없이 쓰는 곳(수지 나무 채집물)도 레퍼런스 색이 나게 기본값을 칠한다.
# 값은 `setup_zones.py` MATERIALS 의 MI_Nature_Leafs_Pine·Broad·Bark 와 같은 값.
QUATERNIUS_COLORS = {
    "MI_Nature_Leaves_Pine": unreal.LinearColor(0.012, 0.100, 0.040, 1.0),
    "MI_Nature_Leaves_NormalTree": unreal.LinearColor(0.018, 0.115, 0.035, 1.0),
    "MI_Nature_Bark_NormalTree": unreal.LinearColor(0.200, 0.065, 0.028, 1.0),
}
for asset_name, color in QUATERNIUS_COLORS.items():
    mi = unreal.EditorAssetLibrary.load_asset("{}/{}".format(DEST_DIR, asset_name))
    if mi is not None:
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "DiffuseColor", color)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)

used_materials = set()
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, unreal.StaticMesh):
        continue
    for slot in asset.static_materials:
        material = slot.material_interface
        if material is not None:
            used_materials.add(material.get_path_name().split(".")[0])

for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, unreal.MaterialInterface):
        continue
    if path.split(".")[0] in used_materials or asset.get_name().startswith(KEEP_MATERIAL_PREFIX):
        continue
    if unreal.EditorAssetLibrary.delete_asset(path):
        unreal.log("쓰는 메시가 없어 지움: {}".format(path))
    else:
        unreal.log_error("지우기 실패: {}".format(path))

unreal.EditorAssetLibrary.save_directory(DEST_DIR, only_if_is_dirty=False, recursive=True)

# 4) 새로 읽어 확인 — 껍데기 메시(삼각형 0)를 통과라고 적지 않으려고 크기·삼각형 수를 같이 센다.
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        unreal.log_error("못 읽음: {}".format(path))
        continue
    if isinstance(asset, unreal.StaticMesh):
        extent = asset.get_bounding_box().max - asset.get_bounding_box().min
        triangles = _triangle_count(asset)
        # 슬롯에 붙은 머티리얼 이름까지 적는다 — 빈 슬롯(머티리얼을 지워 버린 메시)을 통과로 세지 않으려고.
        slots = [
            slot.material_interface.get_name() if slot.material_interface is not None else "(빔)"
            for slot in asset.static_materials
        ]
        unreal.log(
            "최종: {} [StaticMesh] 크기(cm)={:.0f}x{:.0f}x{:.0f} 삼각형={} 머티리얼={} {}".format(
                path, extent.x, extent.y, extent.z, triangles,
                "+".join(slots) if slots else "(없음)",
                "한도밖(3K)" if triangles > TRIANGLE_LIMIT else "한도안"))
        if triangles > TRIANGLE_LIMIT:
            unreal.log_error("DESIGN 4.2 의 오브젝트당 {} 삼각형을 넘었다: {}".format(TRIANGLE_LIMIT, path))
    else:
        unreal.log("최종: {} [{}]".format(path, asset.get_class().get_name()))
