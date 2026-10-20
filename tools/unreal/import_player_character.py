"""M5-01a 플레이어 몸 임포트 — 바르코(ìì²´ ì ì) 리깅 몸 → `SK_Player_Body`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_player_character.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본은 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
출처는 사용자 본인 제작(바르코 + Mixamo 모션 리타겟) — `docs/ASSET_LICENSES.md` 표(2026-09-21 행).

**감독 지시(INBOX 2026-09-21 13:48)는 `tpose_mesh.fbx` 를 먼저 쓰라고 했는데 그 파일에는 뼈가 없다.**
`tpose_mesh.fbx`·`tpose_mesh.glb` 둘 다 스킨·본이 0 이라(노드 1개 `texture_mapping`) 애니메이션을
붙일 수가 없다. 리깅된 몸은 **애니메이션 glb 안에** 들어 있다 — `standing_idle_1.glb` 는
메시(정점 2007 · 머티리얼 1)와 본 23개(`Root`→`Hips`→… Mixamo 이름에서 `mixamorig:` 접두어만 빠진
짜임)를 같이 담고 있다. 그래서 몸은 여기서 가져온다 (🟡 INBOX `확인`).

클립은 이 바퀴에서 넣지 않는다 — 같은 glb 들에 한 파일당 한 클립씩 들어 있고 M5-01b 몫이다.
그래서 애니메이션 임포트를 끄고 메시·스켈레톤·머티리얼만 받는다.

**옛 Quaternius 몸(`Player/` 바로 아래)은 건드리지 않는다.** 지금 게임이 그걸 쓰고 있고
(`ASnarehavenCharacter` 의 경로·2층 검사), 클립(b)까지 들어와야 갈아탈 수 있다. 새 것은
`Player/Snare/` 에 나란히 넣고, 갈아 끼우는 바퀴(c)가 경로를 옮기고 옛것을 지운다.
폴더를 가르는 또 다른 이유는 루프#70 의 머티리얼 이름 재사용 사고다(같은 폴더·같은 이름이면
임포트가 남의 팩 머티리얼을 물린다).

임포트한 에셋 이름은 원본 노드 이름 그대로 나오므로 여기서 프로젝트 접두어 규칙
(`docs/engines/unreal.md` 표: `SK_`/`MI_`/`T_`)으로 바꿔 준다. 이미 있으면 다시 임포트하지 않는다(재실행 안전).
"""
import unreal

SOURCE_PATH = (
    "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player/standing_idle_1.glb"
)
# INBOX 결함 2026-10-04 12:40 ② — 머리 공을 앞면 평평한 둥근 상자로 민 원본(`make_player_box_head.py` 가 위 glb 에서 만든다).
# 뼈·스킨·UV·그림은 위 glb 와 바이트까지 같고 머리 정점 자리·법선만 다르다.
BOX_HEAD_SOURCE_PATH = (
    "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player/standing_idle_1_boxhead.glb"
)
DEST_DIR = "/Game/Game/Characters/Player/Snare"
MESH_NAME = "SK_Player_Body"
ASSET_BODY = "Player"

# 엔진 기본 glTF 파이프라인 스택은 둘이다 (`BaseEngine.ini` 의 `InterchangeGLTFTranslator` 줄):
# `DefaultGLTFAssetsPipeline`(에셋 일반 규칙) + `DefaultGLTFPipeline`(glTF 전용 머티리얼 처리).
# 앞의 것만 우리 값을 넣은 새 인스턴스로 갈아 끼우고, 뒤의 것은 엔진 것 그대로 태운다.
# 플러그인 콘텐츠라 `EditorAssetLibrary.load_asset` 은 못 찾는다(에셋 레지스트리에 없다 — 루프#63 헛발 둘).
# `unreal.load_asset` 은 패키지를 직접 열어서 된다.
GLTF_PIPELINE = "/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"


def _pipelines() -> list:
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)   # `SkeletalMeshes/` 하위 폴더 없이
    pipeline.set_editor_property("scene_name_sub_folder", False)    # `standing_idle_1/` 없이
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", MESH_NAME)           # 메시 이름을 여기서 못 박는다

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_static_meshes", False)         # 스킨 메시 하나뿐이다
    mesh.set_editor_property(
        "combine_skeletal_meshes_behavior",
        unreal.InterchangeCombineSkeletalMeshesBehavior.BY_SKELETON)

    # 클립은 M5-01b 몫 — 여기서 들어오면 `SK_Player_Body_Anim` 같은 이름이 하나 더 생긴다.
    animation = pipeline.get_editor_property("animation_pipeline")
    animation.set_editor_property("import_animations", False)

    result = [pipeline]
    try:
        result.append(unreal.load_asset(GLTF_PIPELINE))
    except Exception as err:
        unreal.log_error("glTF 전용 파이프라인을 못 찾았다: {} ({})".format(GLTF_PIPELINE, err))
    return result


def _import() -> list:
    override = unreal.InterchangePipelineStackOverride()
    for pipeline in _pipelines():
        override.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.filename = SOURCE_PATH
    task.destination_path = DEST_DIR
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


# glb 안 그림 셋은 이름이 비어 있어 엔진이 `..._texture_0/1/2` 로 번호만 붙여 준다.
# 어느 번호가 무엇인지는 원본 머티리얼이 말해 준다 (`standing_idle_1.glb` 의 `material_0`):
# baseColorTexture=0 · occlusionTexture=1·metallicRoughnessTexture=1 · normalTexture=2.
TEXTURE_TAILS = {"texture_0": "BaseColor", "texture_1": "ORM", "texture_2": "Normal"}


def _texture_tail(name: str) -> str:
    low = name.lower()
    for key, tail in TEXTURE_TAILS.items():
        if low.endswith(key):
            return tail
    if "normal" in low:
        return "Normal"
    if "occlusion" in low or "roughness" in low or "metallic" in low:
        return "ORM"           # 한 장에 AO·러프니스·메탈릭이 같이 들어 있다(glTF 관례)
    if "basecolor" in low or "base_color" in low or "color" in low or "diffuse" in low:
        return "BaseColor"
    return name


def _target_name(asset) -> str:
    name = asset.get_name()
    if isinstance(asset, unreal.SkeletalMesh):
        return MESH_NAME
    if isinstance(asset, unreal.Skeleton):
        return MESH_NAME + "_Skeleton"
    if isinstance(asset, unreal.PhysicsAsset):
        return MESH_NAME + "_PhysicsAsset"
    if isinstance(asset, unreal.MaterialInstance):
        return "MI_{}_Body".format(ASSET_BODY)      # 원본 머티리얼은 `material_0` 하나뿐이다
    if isinstance(asset, unreal.MaterialInterface):
        return "M_{}_Body".format(ASSET_BODY)
    if isinstance(asset, unreal.Texture):
        prefix = "T_{}_Body_".format(ASSET_BODY)
        if name.startswith(prefix):
            return name    # 이미 정리됨 — `_ORM` 은 꼬리 표에 안 걸려 재실행 때 접두어가 겹쳤다(루프#396)
        return prefix + _texture_tail(name)
    return name


def _source_file(mesh) -> str:
    data = mesh.get_editor_property("asset_import_data")
    return data.get_first_filename() if data else ""


def _reimport_box_head(mesh) -> None:
    """이미 있는 몸에 상자 머리 원본을 **기하만** 다시 받는다 — 스켈레톤·머티리얼·소켓·물리는 그대로.

    Interchange 다시 받기 기본 전략 `ApplyNoProperties`(에셋 설정을 덮지 않음) + 머티리얼·텍스처·클립 끔
    + 스켈레톤은 지금 몸 것을 지정하고 참조 자세를 안 건드린다(`import_outfits.py` 와 같은 설정).
    """
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_name", MESH_NAME)
    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", mesh.get_editor_property("skeleton"))
    mesh_pipe = pipeline.get_editor_property("mesh_pipeline")
    mesh_pipe.set_editor_property("import_static_meshes", False)
    mesh_pipe.set_editor_property("update_skeleton_reference_pose", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    material = pipeline.get_editor_property("material_pipeline")
    material.set_editor_property("import_materials", False)
    material.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)

    params = unreal.ImportAssetParameters()
    params.set_editor_property("reimport_asset", mesh)
    params.set_editor_property("is_automated", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    source = manager.create_source_data(BOX_HEAD_SOURCE_PATH)
    result = manager.import_asset(DEST_DIR, source, params)
    ok = result[0] if isinstance(result, tuple) else result   # 파이썬에선 out 인자가 튜플로 같이 온다
    unreal.log_warning("[PlayerBody] 상자 머리 다시 받기 {} ← {}".format("성공" if ok else "실패", BOX_HEAD_SOURCE_PATH))


def _keep_imported_normals(mesh) -> None:
    """LOD0 빌드 설정 `recompute_normals` 를 끈다 — 켜져 있으면 엔진이 glb 의 부드러운 법선을 버리고 면마다 다시 계산해
    팔·주먹·신발이 다각형 면으로 각져 보인다(INBOX 보정 2026-10-09 18:59, 루프#846). 접선은 그대로 다시 계산한다."""
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    settings = subsystem.get_lod_build_settings(mesh, 0)
    if not settings.get_editor_property("recompute_normals"):
        unreal.log("법선 가져온 값 유지 — 이미 꺼져 있음")
        return
    settings.set_editor_property("recompute_normals", False)
    subsystem.set_lod_build_settings(mesh, 0, settings)
    unreal.log_warning("[PlayerBody] 법선 다시 계산 끔 — glb 법선 그대로 쓴다")


existing = unreal.EditorAssetLibrary.load_asset("{}/{}".format(DEST_DIR, MESH_NAME))
if existing is None:
    for path in _import():
        unreal.log("임포트됨: {}".format(path))
elif _source_file(existing) != BOX_HEAD_SOURCE_PATH:
    unreal.log_warning("[PlayerBody] 지금 원본 {} → 상자 머리로 바꾼다".format(_source_file(existing)))
    _reimport_box_head(existing)
else:
    unreal.log("이미 있음: {}/{}".format(DEST_DIR, MESH_NAME))

_mesh = unreal.EditorAssetLibrary.load_asset("{}/{}".format(DEST_DIR, MESH_NAME))
if _mesh is not None:
    _keep_imported_normals(_mesh)

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

unreal.EditorAssetLibrary.save_directory(DEST_DIR, only_if_is_dirty=True, recursive=True)   # 클립 33개를 괜히 다시 저장하지 않는다
for path in unreal.EditorAssetLibrary.list_assets(DEST_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    unreal.log("최종: {}  [{}]".format(path, asset.get_class().get_name() if asset else "?"))
