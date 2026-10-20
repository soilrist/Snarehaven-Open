"""M1-07 Quaternius Farm Animals 돼지 임포트 — `Pig.fbx` → `SK_Animal_Pig` + 애니메이션.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_animal_pig.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본은 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
라이선스는 CC0 1.0 — `docs/ASSET_LICENSES.md` 표.

임포트한 에셋 이름은 FBX 노드 이름 그대로 나오므로 여기서 프로젝트 접두어 규칙
(`docs/engines/unreal.md` 표: `SK_`/`A_`/`M_`/`T_`)으로 바꿔 준다. 갓 만든 에셋이라
리다이렉터가 남아도 참조하는 곳이 없다. 이미 있으면 다시 임포트하지 않는다(재실행 안전).
"""
import unreal

FBX_PATH = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_FarmAnimals/FBX/Pig.fbx"
DEST_DIR = "/Game/Game/Characters/Animals/Pig"
MESH_NAME = "SK_Animal_Pig"
ASSET_BODY = "Animal_Pig"

# M1-09 — 애니 전용 임포트. `Pig.fbx` 에는 Idle·Jump 둘뿐이라, 감독이 같은 팩의 `Pig.blend`(CC0)에서
# 액션마다 FBX 하나로 다시 뽑았다. **메시는 안 받고 클립만** 위 스켈레톤에 붙인다.
# M41-02 — Walk/Run 도 건다. 빈 칸이면 걷거나 도망갈 때 대기 클립이 돌아 다리가 선 채 미끄러졌다(M41-01 조사).
ANIM_DIR = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_FarmAnimals/FBX"
ANIM_IMPORTS = [
    ("Pig_Anim_Death.fbx", "A_Animal_Pig_Death"),
    ("Pig_Anim_Walk.fbx", "A_Animal_Pig_Walk"),
    ("Pig_Anim_Run.fbx", "A_Animal_Pig_Run"),
]

# 루프#47 사고 복구표. 아래 `_target_name` 이 **접두어가 이미 붙은 이름에 또 붙였다** —
# 이 스크립트를 두 번째로 돌린 순간 `A_Animal_Pig_Idle` 이 `A_Animal_Pig_A_Animal_Idle` 이 됐다.
# 이제 `_target_name` 이 그걸 막지만(규칙대로면 그대로 둔다), 이미 망가진 이름은 이 표가 되돌린다.
REPAIR_NAMES = {
    "A_Animal_Pig_A_Animal_Idle": "A_Animal_Pig_Idle",
    "A_Animal_Pig_A_Animal_Jump": "A_Animal_Pig_Jump",
    "MI_Animal_Pig_MI_Animal_Material": "MI_Animal_Pig_Material",
    "MI_Animal_Pig_MI_Animal_Material_003": "MI_Animal_Pig_Material_003",
}


def _import() -> list:
    task = unreal.AssetImportTask()
    task.filename = FBX_PATH
    task.destination_path = DEST_DIR
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


def _import_anim(file_name: str, asset_name: str) -> None:
    """`file_name` 의 클립 하나를 `asset_name` 으로 스켈레톤에 붙인다. 이미 있으면 건너뛴다."""
    asset_path = "{}/{}".format(DEST_DIR, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log("이미 있음: {}".format(asset_path))
        return

    skeleton_path = "{}/{}_Skeleton".format(DEST_DIR, MESH_NAME)
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
    # 스켈레톤을 지정해야 새 스켈레톤을 만들지 않고 돼지 몸에 그대로 붙는다.
    options.set_editor_property("skeleton", skeleton)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    options.anim_sequence_import_data.set_editor_property("import_bone_tracks", True)
    options.anim_sequence_import_data.set_editor_property(
        "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    options.anim_sequence_import_data.set_editor_property("convert_scene", True)

    task = unreal.AssetImportTask()
    task.filename = "{}/{}".format(ANIM_DIR, file_name)
    task.destination_path = DEST_DIR
    # 이름을 여기서 못 박는다 — blend 에서 뽑은 FBX 의 테이크 이름은 파일마다 `Scene` 하나다.
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        unreal.log("애니 임포트됨: {}".format(path))


def _short(name: str) -> str:
    """FBX 노드 이름에서 꼬리표만 남긴다: `Armature|Pig_Idle` → `Idle`."""
    tail = name.split("|")[-1]
    for junk in ("Pig_", "_Pig", "Pig", "Armature_", "Armature"):
        tail = tail.replace(junk, "")
    return tail.strip("_") or "Base"


def _prefixed(prefix: str, name: str) -> str:
    """이미 규칙대로면 그대로 둔다 — 여러 번 돌려도 이름이 자라지 않게 (루프#47 사고)."""
    if name in REPAIR_NAMES:
        return REPAIR_NAMES[name]
    if name.startswith(prefix):
        return name
    return prefix + _short(name)


def _target_name(asset) -> str:
    name = asset.get_name()
    if isinstance(asset, unreal.SkeletalMesh):
        return MESH_NAME
    if isinstance(asset, unreal.Skeleton):
        return MESH_NAME + "_Skeleton"
    if isinstance(asset, unreal.PhysicsAsset):
        return MESH_NAME + "_PhysicsAsset"
    if isinstance(asset, unreal.AnimSequence):
        return _prefixed("A_{}_".format(ASSET_BODY), name)
    if isinstance(asset, unreal.MaterialInstance):
        return _prefixed("MI_{}_".format(ASSET_BODY), name)
    if isinstance(asset, unreal.MaterialInterface):
        return _prefixed("M_{}_".format(ASSET_BODY), name)
    if isinstance(asset, unreal.Texture):
        return _prefixed("T_{}_".format(ASSET_BODY), name)
    return name


if not unreal.EditorAssetLibrary.does_asset_exist("{}/{}".format(DEST_DIR, MESH_NAME)):
    for path in _import():
        unreal.log("임포트됨: {}".format(path))

for anim_file, anim_name in ANIM_IMPORTS:
    _import_anim(anim_file, anim_name)

# 임포트 직후에도, 재실행 때도 같은 규칙으로 정리한다.
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        continue
    want = _target_name(asset)
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

unreal.EditorAssetLibrary.save_directory(DEST_DIR, only_if_is_dirty=False, recursive=True)
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    unreal.log("최종: {}  [{}]".format(path, asset.get_class().get_name() if asset else "?"))
