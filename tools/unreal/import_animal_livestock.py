"""M5-04a Quaternius Farm Animals 소·양 임포트 — `Cow.fbx`·`Sheep.fbx` → `SK_Animal_Cow`·`SK_Animal_Sheep`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_animal_livestock.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본은 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
라이선스는 CC0 1.0 — `docs/ASSET_LICENSES.md` 표.

돼지(`import_animal_pig.py`, M1-07)와 같은 절차이고 동물만 표로 늘렸다. 다른 점 둘:
  - **동물마다 폴더를 가른다** (`Animals/Cow`·`Animals/Sheep`) — 루프#70 의 머티리얼 이름
    재사용 사고(같은 폴더·같은 이름이면 남의 팩 머티리얼을 물린다)를 애초에 막는다.
  - 소는 Death 까지 원본 FBX 에 들어 있지만 **양은 Idle·Jump 둘뿐**이었다(루프#74 `막힘`).
    감독이 같은 팩 `Blends/Sheep.blend`(CC0)에서 Death 액션만 FBX 로 다시 뽑아 줘서,
    돼지(`import_animal_pig.py` M1-09)와 같은 **애니 전용 임포트**로 `ANIM_IMPORTS` 가 붙인다.

임포트한 에셋 이름은 FBX 노드 이름 그대로 나오므로 여기서 프로젝트 접두어 규칙
(`docs/engines/unreal.md` 표: `SK_`/`A_`/`M_`/`T_`)으로 바꿔 준다. 이미 있으면 다시
임포트하지 않는다(재실행 안전).
"""
import unreal

RAW_DIR = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_FarmAnimals/FBX"

# (FBX 파일, 폴더 이름, 에셋 몸통) — 몸통이 `Animal_Cow` 면 `SK_Animal_Cow`·`A_Animal_Cow_Idle` 이 된다.
ANIMALS = [
    ("Cow.fbx", "Cow", "Animal_Cow"),
    ("Sheep.fbx", "Sheep", "Animal_Sheep"),
]

DEST_ROOT = "/Game/Game/Characters/Animals"

# 폴더 이름 -> [(애니 전용 FBX, 붙일 에셋 이름)] — 원본 FBX 에 없어서 blend 에서 따로 뽑은 클립만.
# 메시는 안 받고 위에서 만든 스켈레톤에 클립만 건다.
ANIM_IMPORTS = {
    "Sheep": [
        ("Sheep_Anim_Death.fbx", "A_Animal_Sheep_Death"),
        # M41-03 — 걷기·도주 칸이 비어 대기 클립으로 미끄러지던 것. `tools/blender/export_sheep_anims.py` 가 뽑는다.
        ("Sheep_Anim_Walk.fbx", "A_Animal_Sheep_Walk"),
        ("Sheep_Anim_Run.fbx", "A_Animal_Sheep_Run"),
    ],
}


def _short(name: str, species: str) -> str:
    """FBX 노드 이름에서 꼬리표만 남긴다: `Armature|Cow_Idle` → `Idle`."""
    tail = name.split("|")[-1]
    for junk in (species + "_", "_" + species, species, "Armature_", "Armature"):
        tail = tail.replace(junk, "")
    return tail.strip("_") or "Base"


def _prefixed(prefix: str, name: str, species: str) -> str:
    """이미 규칙대로면 그대로 둔다 — 여러 번 돌려도 이름이 자라지 않게 (루프#47 사고)."""
    if name.startswith(prefix):
        return name
    return prefix + _short(name, species)


def _target_name(asset, mesh_name: str, body: str, species: str) -> str:
    name = asset.get_name()
    if isinstance(asset, unreal.SkeletalMesh):
        return mesh_name
    if isinstance(asset, unreal.Skeleton):
        return mesh_name + "_Skeleton"
    if isinstance(asset, unreal.PhysicsAsset):
        return mesh_name + "_PhysicsAsset"
    if isinstance(asset, unreal.AnimSequence):
        return _prefixed("A_{}_".format(body), name, species)
    if isinstance(asset, unreal.MaterialInstance):
        return _prefixed("MI_{}_".format(body), name, species)
    if isinstance(asset, unreal.MaterialInterface):
        return _prefixed("M_{}_".format(body), name, species)
    if isinstance(asset, unreal.Texture):
        return _prefixed("T_{}_".format(body), name, species)
    return name


def _import(fbx_path: str, dest_dir: str) -> list:
    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = dest_dir
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


def _import_anim(file_name: str, asset_name: str, dest_dir: str, mesh_name: str) -> None:
    """`file_name` 의 클립 하나를 `asset_name` 으로 그 동물 스켈레톤에 붙인다. 이미 있으면 건너뛴다."""
    asset_path = "{}/{}".format(dest_dir, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log("이미 있음: {}".format(asset_path))
        return

    skeleton_path = "{}/{}_Skeleton".format(dest_dir, mesh_name)
    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    if skeleton is None:
        unreal.log_error("스켈레톤이 없다 — 메시부터 임포트해야 한다: {}".format(skeleton_path))
        return

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("import_as_skeletal", True)
    # 스켈레톤을 지정해야 새 스켈레톤을 만들지 않고 이미 있는 몸에 그대로 붙는다.
    options.set_editor_property("skeleton", skeleton)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    options.anim_sequence_import_data.set_editor_property("import_bone_tracks", True)
    options.anim_sequence_import_data.set_editor_property(
        "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    options.anim_sequence_import_data.set_editor_property("convert_scene", True)

    task = unreal.AssetImportTask()
    task.filename = "{}/{}".format(RAW_DIR, file_name)
    task.destination_path = dest_dir
    # 이름을 여기서 못 박는다 — blend 에서 뽑은 FBX 의 테이크 이름은 파일마다 `Scene` 하나다.
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        unreal.log("애니 임포트됨: {}".format(path))


for fbx_name, folder, body in ANIMALS:
    species = folder
    dest_dir = "{}/{}".format(DEST_ROOT, folder)
    mesh_name = "SK_{}".format(body)

    if unreal.EditorAssetLibrary.does_asset_exist("{}/{}".format(dest_dir, mesh_name)):
        unreal.log("이미 있음, 임포트 건너뜀: {}/{}".format(dest_dir, mesh_name))
    else:
        for path in _import("{}/{}".format(RAW_DIR, fbx_name), dest_dir):
            unreal.log("임포트됨: {}".format(path))

    for anim_file, anim_name in ANIM_IMPORTS.get(folder, []):
        _import_anim(anim_file, anim_name, dest_dir, mesh_name)

    # 임포트 직후에도, 재실행 때도 같은 규칙으로 정리한다.
    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is None:
            continue
        want = _target_name(asset, mesh_name, body, species)
        if asset.get_name() == want:
            unreal.log("이름 그대로: {} ({})".format(path, asset.get_class().get_name()))
            continue
        new_path = "{}/{}".format(path.rsplit("/", 1)[0], want)
        if unreal.EditorAssetLibrary.does_asset_exist(new_path):
            unreal.log_error("이름 충돌로 못 바꿈: {} → {}".format(path, new_path))
            continue
        if unreal.EditorAssetLibrary.rename_asset(path, new_path):
            unreal.log("이름 바꿈: {} → {} ({})".format(path, new_path, asset.get_class().get_name()))
        else:
            unreal.log_error("이름 바꾸기 실패: {} → {}".format(path, new_path))

    unreal.EditorAssetLibrary.save_directory(dest_dir, only_if_is_dirty=False, recursive=True)
    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        unreal.log("최종: {}  [{}]".format(path, asset.get_class().get_name() if asset else "?"))
