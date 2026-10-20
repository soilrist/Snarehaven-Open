"""M5-03a 목장 건물·소품 임포트 — Quaternius Farm Buildings + Kenney Fantasy Town Kit → `SM_Building_*`·`SM_Prop_*`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_ranch_buildings.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본 zip 과 푼 폴더는 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
라이선스는 둘 다 CC0 1.0 — `docs/ASSET_LICENSES.md` 표(2026-09-20 행).

감독 지시(INBOX 2026-09-20 12:30)의 M5-03 몫: 건물 = Kenney Fantasy Town Kit(FBX) + Quaternius Farm Buildings(FBX).
두 팩을 통째로 넣지 않고 **목장에 실제로 세울 것만** 고른다(아래 `GROUPS`, 12개) — 팩 전체는
Kenney 170 + Quaternius 13 이고, Kenney 쪽은 벽·지붕 조각을 쌓아 집을 짓는 모듈 킷이라 통째로 넣으면
콘텐츠 브라우저만 채운다. 역할을 이렇게 갈랐다:
  * **건물은 Quaternius** — 헛간·사일로·우물·풍차가 **한 메시에 통째로** 들어 있어 쌓아 올릴 필요가 없다.
  * **소품은 Kenney** — 수레·등불·기둥·널빤지·대문처럼 목장 슬롯 곁에 놓을 작은 것이 많다.
`TowerWindmill`(3901 삼각형)은 DESIGN 4.2 의 **오브젝트당 3K** 를 넘어서 뺐고 대신 `Windmill`(716)을 쓴다 —
루프#68 이 자연물에서 한 것과 같은 판단이다.

**폴더를 둘로 가른 이유**(루프#70 사고): 임포터의 머티리얼 재사용은 **같은 폴더의 같은 이름**을 찾는다.
Quaternius 건물의 머티리얼은 `Brown`·`White`·`Grey` 같은 흔한 색 이름이고 Kenney 소품은 전부 `colormap`
하나를 쓴다 — 한 폴더에 넣으면 나중에 다른 팩을 더할 때 남의 머티리얼을 물 수 있다.

배치는 M5-03b 몫이고 이 조각은 에셋을 들여놓는 데까지다. 크기가 팩마다 다르다(아래 로그) — 배치 배율은 M5-03b.

임포트한 에셋 이름은 FBX 노드 이름 그대로 나오므로 여기서 프로젝트 접두어 규칙
(`docs/engines/unreal.md` 표: `SM_`/`M_`/`MI_`/`T_`)으로 바꿔 준다. 이미 있으면 다시 임포트하지 않는다(재실행 안전).
"""
import os

import unreal

FARM_DIR = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_FarmBuildings/unzipped/FBX"
TOWN_DIR = "~/UNREAL/Snarehaven/_Source/raw/Kenney_FantasyTownKit/unzipped/Models/FBX format"

TRIANGLE_LIMIT = 3000   # DESIGN 2·4.2 "오브젝트당 3K 이하" — 루프가 만든 숫자가 아니라 설계서 값

# (넣을 폴더, 에셋 몸통, [(원본 파일, 만들 에셋 이름)]) — 고른 이유는 위 문서 주석.
GROUPS = [
    ("/Game/Game/Environment/Buildings", "Building", [
        (os.path.join(FARM_DIR, "Barn.fbx"), "SM_Building_Barn_01"),
        (os.path.join(FARM_DIR, "SmallBarn.fbx"), "SM_Building_Barn_02"),
        (os.path.join(FARM_DIR, "ChickenCoop.fbx"), "SM_Building_Coop_01"),
        (os.path.join(FARM_DIR, "Silo.fbx"), "SM_Building_Silo_01"),
        (os.path.join(FARM_DIR, "Well.fbx"), "SM_Building_Well_01"),
        (os.path.join(FARM_DIR, "Windmill.fbx"), "SM_Building_Windmill_01"),
        (os.path.join(FARM_DIR, "Fence.fbx"), "SM_Building_Fence_01"),
    ]),
    ("/Game/Game/Environment/Props", "Prop", [
        (os.path.join(TOWN_DIR, "fence-gate.fbx"), "SM_Prop_Gate_01"),
        (os.path.join(TOWN_DIR, "cart.fbx"), "SM_Prop_Cart_01"),
        (os.path.join(TOWN_DIR, "lantern.fbx"), "SM_Prop_Lantern_01"),
        (os.path.join(TOWN_DIR, "poles.fbx"), "SM_Prop_Poles_01"),
        (os.path.join(TOWN_DIR, "planks.fbx"), "SM_Prop_Planks_01"),
    ]),
]


def _pipelines(asset_name: str) -> list:
    """FBX 한 파일 = 정적 메시 한 개. 기본값대로 두면 `<파일이름>/StaticMeshes/` 하위 폴더로 들어간다."""
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)   # `StaticMeshes/` 하위 폴더 없이
    pipeline.set_editor_property("scene_name_sub_folder", False)    # `<파일이름>/` 없이
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", asset_name)          # 합친 메시 이름을 여기서 못 박는다

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_skeletal_meshes", False)       # 건물·소품은 전부 정적 메시다
    mesh.set_editor_property(
        "combine_static_meshes_behavior",
        unreal.InterchangeCombineStaticMeshesBehavior.ALL)          # 지붕·벽을 한 덩어리로
    # Nanite 는 이 프로젝트가 끈 기능이다(DESIGN 4.2 모바일 렌더러) — 데이터만 늘어난다.
    mesh.set_editor_property("build_nanite", False)
    return [pipeline]


def _import(file_path: str, asset_name: str, dest_dir: str) -> list:
    override = unreal.InterchangePipelineStackOverride()
    for pipeline in _pipelines(asset_name):
        override.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.filename = file_path
    task.destination_path = dest_dir
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


# 임포터가 지은 이름이 규칙과 어긋나는 것만 손으로 못 박는다 (원본 이름 → 최종 이름).
# Kenney FBX 는 텍스처와 머티리얼 이름이 똑같이 `colormap` 이라 머티리얼이 `colormap1` 로 들어온다 —
# 꼬리 숫자 `1` 은 AssetNaming 의 두 자리 번호(`_01`) 규칙과 어긋나고 뜻도 없다.
NAME_FIXUPS = {
    "colormap1": "MI_Prop_Colormap",
    "MI_Prop_Colormap1": "MI_Prop_Colormap",
}


def _short(name: str) -> str:
    """원본 이름에서 꼬리표만 남기고 첫 글자를 대문자로: `colormap` → `Colormap` (폴더·에셋은 PascalCase)."""
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


def _target_name(asset, body: str) -> str:
    name = asset.get_name()
    if name in NAME_FIXUPS:
        return NAME_FIXUPS[name]
    if isinstance(asset, unreal.StaticMesh):
        return name                                     # 임포트할 때 이미 이름을 못 박았다
    if isinstance(asset, unreal.MaterialInstance):
        return _prefixed("MI_{}_".format(body), name)
    if isinstance(asset, unreal.MaterialInterface):
        return _prefixed("M_{}_".format(body), name)
    if isinstance(asset, unreal.Texture):
        return _prefixed("T_{}_".format(body), name)
    return name


for dest_dir, body, models in GROUPS:
    # 1) 임포트 — 이름 바꾸기는 나중에 한 번에. 머티리얼 재사용(`bReuseExistingMaterials`)이
    #    같은 폴더의 **같은 이름**을 찾는 방식이라, 중간에 이름을 바꾸면 팩마다 사본이 생긴다.
    for file_path, asset_name in models:
        asset_path = "{}/{}".format(dest_dir, asset_name)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            unreal.log("이미 있음: {}".format(asset_path))
            continue
        if not os.path.exists(file_path):
            unreal.log_error("원본이 없다(zip 을 안 풀었나): {}".format(file_path))
            continue
        for path in _import(file_path, asset_name, dest_dir):
            unreal.log("임포트됨: {}".format(path))

    # 2) 임포트 직후에도, 재실행 때도 같은 규칙으로 이름을 정리한다.
    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is None:
            continue
        want = _target_name(asset, body)
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

    # 3) 아무 메시도 안 쓰는 머티리얼은 지운다. FBX 한 파일이 팩 전체의 머티리얼 목록을 들고 오기도 한다.
    #    **역참조(find_package_referencers_for_asset)로 고르면 안 된다** — 갓 임포트한 메시는 아직
    #    에셋 레지스트리에 안 잡혀 "쓰는 곳 없음" 으로 나온다(루프#68 이 실제로 13개를 다 지웠다).
    #    메시의 머티리얼 슬롯을 직접 읽어 쓰는 것만 남긴다.
    used_materials = set()
    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(asset, unreal.StaticMesh):
            continue
        for slot in asset.static_materials:
            material = slot.material_interface
            if material is not None:
                used_materials.add(material.get_path_name().split(".")[0])

    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(asset, unreal.MaterialInterface):
            continue
        if path.split(".")[0] in used_materials:
            continue
        if unreal.EditorAssetLibrary.delete_asset(path):
            unreal.log("쓰는 메시가 없어 지움: {}".format(path))
        else:
            unreal.log_error("지우기 실패: {}".format(path))

    unreal.EditorAssetLibrary.save_directory(dest_dir, only_if_is_dirty=False, recursive=True)

# 4) 새로 읽어 확인 — 껍데기 메시(삼각형 0)를 통과라고 적지 않으려고 크기·삼각형 수를 같이 센다.
#    색은 슬롯 이름으로 확인이 안 된다(루프#69·#70) — 텍스처를 쓰는 부모면 그 칸이 찼는지도 적는다.
for dest_dir, body, _models in GROUPS:
    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is None:
            unreal.log_error("못 읽음: {}".format(path))
            continue
        if isinstance(asset, unreal.StaticMesh):
            extent = asset.get_bounding_box().max - asset.get_bounding_box().min
            triangles = asset.get_num_triangles(0)
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
        elif isinstance(asset, unreal.MaterialInstance):
            texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                asset, "DiffuseColorMap")
            weight = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
                asset, "DiffuseColorMapWeight")
            # 텍스처가 없는 팩(Quaternius 건물)은 색이 여기 들어 있다 — 흰색(1,1,1)으로 들어왔는지 본다.
            color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
                asset, "DiffuseColor")
            parent = asset.get_editor_property("parent")
            unreal.log("최종: {} [{}] 부모={} DiffuseColorMap={} 가중치={:.1f} DiffuseColor=({:.2f},{:.2f},{:.2f})".format(
                path, asset.get_class().get_name(),
                parent.get_name() if parent is not None else "(없음)",
                texture.get_name() if texture is not None else "(빔)", weight,
                color.r, color.g, color.b))
        else:
            unreal.log("최종: {} [{}]".format(path, asset.get_class().get_name()))
