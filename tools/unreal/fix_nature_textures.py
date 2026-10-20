"""M5-02c Quaternius 자연 메시 색 고치기 — 원본 텍스처를 넣고 머티리얼에 물린다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/fix_nature_textures.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

루프#69 가 사냥터를 찍어 보고 찾은 결함 둘을 고친다 (`captures/69_hunt_forest.png`).

1) **덤불·바위 2·자갈이 흰색이다.** Quaternius 팩은 FBX 가 텍스처를 파일 이름으로만 참조하는데
   (`Rocks_Diffuse.png` 식, 원본 경로는 만든 사람 윈도우 바탕화면이다) 루프#68 이 zip 의 `FBX/` 만 풀고
   `Textures/` 를 안 풀어서 임포터가 못 찾았다. 그 결과 머티리얼 인스턴스(부모 `FBXLegacyPhongSurfaceMaterial`)에
   `DiffuseColorMap` 이 비었고 부모 기본값인 흰색이 나왔다. 여기서 텍스처를 넣고 그 칸을 채운다.
2) **풀 2종이 바닥과 같은 민트 단색이라 안 보인다.** Quaternius 풀 메시의 머티리얼 이름이 Kenney 의 것과
   똑같이 `grass` 라, 임포트할 때 머티리얼 재사용(같은 폴더·같은 이름)이 **Kenney 단색 머티리얼**을 물렸다.
   풀에는 Quaternius 텍스처를 쓰는 `MI_Nature_GrassBlade` 를 따로 만들어 붙인다.
   바닥(`MinStageFloor`)이 쓰는 `MI_Nature_Grass` 는 건드리지 않는다 — 그건 Kenney 팔레트 그대로고
   바꿀지는 사람 답을 기다리는 중이다(INBOX `확인` 루프#69).

Kenney 쪽 메시(나무·소나무·그루터기·통나무·버섯·꽃)는 손대지 않는다 — 그쪽 FBX 는 텍스처 없이
머티리얼에 색을 직접 담고 있어서 제 팔레트대로 들어왔다.

여러 번 돌려도 같은 결과다(재실행 안전): 텍스처가 이미 있으면 임포트를 건너뛰고, 파라미터·슬롯은 매번 다시 맞춘다.
"""
import os

import unreal

TEXTURE_DIR = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_NatureMegaKit/unzipped/Textures"
DEST_DIR = "/Game/Game/Environment/Nature"

DIFFUSE_PARAM = "DiffuseColorMap"          # 부모 머티리얼(Phong·Lambert 둘 다)의 베이스컬러 텍스처 칸
DIFFUSE_WEIGHT_PARAM = "DiffuseColorMapWeight"   # 0 이면 색만, 1 이면 텍스처가 그대로 나온다

# (원본 png, 만들 텍스처 에셋 이름) — 이름은 `docs/engines/unreal.md` 의 `T_<에셋몸통>_<이름>` 규칙.
TEXTURES = [
    ("Leaves_TwistedTree_C.png", "T_Nature_LeavesTwistedTree"),
    ("Rocks_Diffuse.png", "T_Nature_Rocks"),
    ("PathRocks_Diffuse.png", "T_Nature_PathRocks"),
    ("Grass.png", "T_Nature_GrassBlade"),
]

# 이미 있는 머티리얼 인스턴스 → 물려 줄 텍스처. (덤불·바위·자갈)
MATERIAL_TEXTURES = {
    "MI_Nature_Leaves_TwistedTree": "T_Nature_LeavesTwistedTree",
    "MI_Nature_Rocks": "T_Nature_Rocks",
    "MI_Nature_PathRocks": "T_Nature_PathRocks",
}

# 새로 만들 머티리얼 인스턴스 → (부모를 빌려 올 머티리얼 인스턴스, 물려 줄 텍스처). (풀)
NEW_MATERIALS = {
    "MI_Nature_GrassBlade": ("MI_Nature_Grass", "T_Nature_GrassBlade"),
}

# 슬롯을 갈아 끼울 메시 → {슬롯 이름: 새 머티리얼 인스턴스}.
# 슬롯 이름은 **대소문자를 무시하고** 맞춘다 — 언리얼 FName 은 대소문자를 안 가리지만 파이썬 문자열은 가려서,
# 같은 슬롯이 실행에 따라 `grass`·`Grass` 로 다르게 나온다(첫 실행에서 실제로 안 맞아 넘어갔다).
MESH_SLOT_OVERRIDES = {
    "SM_Nature_Grass_01": {"grass": "MI_Nature_GrassBlade"},
    "SM_Nature_Grass_02": {"grass": "MI_Nature_GrassBlade"},
}


def _path(name: str) -> str:
    return "{}/{}".format(DEST_DIR, name)


def _load(name: str):
    asset = unreal.EditorAssetLibrary.load_asset(_path(name))
    if asset is None:
        unreal.log_error("에셋을 못 읽었다: {}".format(_path(name)))
    return asset


def _import_texture(file_name: str, asset_name: str) -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(_path(asset_name)):
        unreal.log("이미 있음: {}".format(_path(asset_name)))
        return
    file_path = os.path.join(TEXTURE_DIR, file_name)
    if not os.path.exists(file_path):
        unreal.log_error("원본이 없다(zip 의 Textures/ 를 안 풀었나): {}".format(file_path))
        return

    task = unreal.AssetImportTask()
    task.filename = file_path
    task.destination_path = DEST_DIR
    task.destination_name = asset_name     # 파일 이름 대신 규칙에 맞는 이름으로 바로 들어가게
    task.automated = True                  # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        unreal.log("임포트됨: {}".format(path))


def _apply_texture(material, texture) -> None:
    """베이스컬러 칸에 텍스처를 넣고 그 칸의 가중치를 켠다. 가중치를 안 켜면 넣어도 색만 나온다."""
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        material, DIFFUSE_PARAM, texture)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        material, DIFFUSE_WEIGHT_PARAM, 1.0)


# 1) 텍스처 넣기.
for file_name, asset_name in TEXTURES:
    _import_texture(file_name, asset_name)

# 2) 이미 있는 머티리얼 인스턴스에 물리기 (덤불·바위·자갈).
for material_name, texture_name in MATERIAL_TEXTURES.items():
    material = _load(material_name)
    texture = _load(texture_name)
    if material is None or texture is None:
        continue
    _apply_texture(material, texture)
    unreal.EditorAssetLibrary.save_asset(_path(material_name), only_if_is_dirty=False)
    unreal.log("텍스처 물림: {} ← {}".format(material_name, texture_name))

# 3) 풀 전용 머티리얼 인스턴스 만들기 — 부모는 곁에 있는 Kenney 풀 머티리얼에서 빌려 온다.
for new_name, (parent_source_name, texture_name) in NEW_MATERIALS.items():
    texture = _load(texture_name)
    parent_source = _load(parent_source_name)
    if texture is None or parent_source is None:
        continue
    parent = parent_source.get_editor_property("parent")
    if parent is None:
        unreal.log_error("부모를 못 찾았다: {}".format(parent_source_name))
        continue

    if unreal.EditorAssetLibrary.does_asset_exist(_path(new_name)):
        material = _load(new_name)
        unreal.log("이미 있음: {}".format(_path(new_name)))
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            new_name, DEST_DIR, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        if material is None:
            unreal.log_error("머티리얼 인스턴스를 못 만들었다: {}".format(new_name))
            continue
        unreal.log("만듦: {}".format(_path(new_name)))
    unreal.MaterialEditingLibrary.set_material_instance_parent(material, parent)
    _apply_texture(material, texture)
    unreal.EditorAssetLibrary.save_asset(_path(new_name), only_if_is_dirty=False)

# 4) 풀 메시의 슬롯을 새 머티리얼로 갈아 끼우기 — 슬롯 이름은 그대로 둔다(레벨 액터의 덮어쓰기와 짝이 맞게).
for mesh_name, overrides in MESH_SLOT_OVERRIDES.items():
    mesh = _load(mesh_name)
    if mesh is None:
        continue
    slots = list(mesh.static_materials)
    changed = False
    for index, slot in enumerate(slots):
        want_name = overrides.get(str(slot.material_slot_name).lower())
        if want_name is None:
            continue
        want = _load(want_name)
        if want is None:
            continue
        if slot.material_interface is not None and slot.material_interface.get_name() == want_name:
            continue
        slot.set_editor_property("material_interface", want)
        slots[index] = slot
        changed = True
    if changed:
        mesh.set_editor_property("static_materials", slots)
        unreal.EditorAssetLibrary.save_asset(_path(mesh_name), only_if_is_dirty=False)
        unreal.log("슬롯 바꿈: {} ← {}".format(mesh_name, overrides))

unreal.EditorAssetLibrary.save_directory(DEST_DIR, only_if_is_dirty=False, recursive=True)

# 5) 새로 읽어 확인 — 빈 칸을 통과라고 적지 않으려고 파라미터 값을 다시 꺼내 본다.
for material_name in list(MATERIAL_TEXTURES) + list(NEW_MATERIALS):
    material = _load(material_name)
    if material is None:
        continue
    texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
        material, DIFFUSE_PARAM)
    weight = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
        material, DIFFUSE_WEIGHT_PARAM)
    if texture is None:
        unreal.log_error("확인 실패({} 가 비었다): {}".format(DIFFUSE_PARAM, material_name))
        continue
    unreal.log("확인: {} {}={} 가중치={:.1f}".format(
        material_name, DIFFUSE_PARAM, texture.get_name(), weight))

for mesh_name in MESH_SLOT_OVERRIDES:
    mesh = _load(mesh_name)
    if mesh is None:
        continue
    slots = ["{}={}".format(
        slot.material_slot_name,
        slot.material_interface.get_name() if slot.material_interface is not None else "(빔)")
        for slot in mesh.static_materials]
    unreal.log("확인: {} 슬롯 {}".format(mesh_name, " ".join(slots)))
