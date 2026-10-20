"""바르코 동물 반입 — `_Source/raw/Snare/Characters/Animals/<종>/` → `/Game/Game/Characters/Animals/<종>/`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_animal_species.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본은 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
출처는 바르코 생성 + 감독 리깅/애니 — `docs/ASSET_LICENSES.md` 표.

**표 한 줄이 한 종이다** (감독 지시 INBOX 2026-09-23 12:40). 여우로 틀을 세우고 나머지 11종은
줄만 늘린다. 종마다 파일 이름이 같은 틀이라 경로는 줄에서 조립한다:
  `SK_Animal_<종>.fbx`(메시+뼈, 텍스처 내장) · `A_Animal_<종>_<클립>.fbx`(뼈만, 클립 1개, 30fps)

`import_animal_livestock.py`(소·양)와 같은 절차이고 다른 점 둘:
  - **클립이 전부 애니 전용 임포트다.** 메시 FBX 에는 클립이 없고 클립 FBX 에는 메시가 없다
    (감독이 Blender 에서 따로 뽑는다) — 그래서 메시 → 스켈레톤 → 클립 6개 순서로 간다.
  - **텍스처를 따로 안 받는다.** `SK_Animal_<종>.fbx` 가 `T_Animal_<종>_D.png`·`_N.png`(원화 색
    보정본)를 `.fbm` 으로 품고 있어 임포트가 그 이름 그대로 텍스처 둘을 만든다. 따로 받으면
    같은 그림이 두 벌 생긴다.

**게임 크기는 가져올 때 곱하는 균일 배율 하나로 맞춘다** (DESIGN 8.7 "크기" 표, 감독 결정
2026-09-23). 바르코는 가장 긴 변을 1m 로 정규화해 내보내서 반입 직후엔 곰과 뇌조가 같은 크기다.
메시와 클립에 **같은 배율**을 준다 — 클립만 원래 크기로 남으면 뼈 오프셋이 몸과 어긋나 발이
미끄러진다. 배율은 표의 네 번째 칸이고, 게임 크기 = 정규화 크기 × 배율이다.

동물마다 폴더를 가르는 이유는 루프#70 의 머티리얼 이름 재사용 사고다(같은 폴더·같은 이름이면
임포트가 남의 팩 머티리얼을 물린다).

임포트한 에셋 이름은 FBX 노드 이름 그대로 나오므로 여기서 프로젝트 접두어 규칙
(`docs/engines/unreal.md` 표: `SK_`/`A_`/`M_`/`MI_`/`T_`)으로 바꿔 준다.
이미 있으면 다시 임포트하지 않는다(재실행 안전).
"""
import unreal


# **이 엔진(5.8) 커맨드릿 로그는 `Log` 등급을 파일에 안 적는다** — `unreal.log()`·`print()` 로 찍은
# 줄은 `-log` 출력에서 통째로 사라지고 Warning·Error 만 남는다(루프#157 실측: TESTMARK 실험).
# 그래서 결과는 **고정 경로 보고 파일**에 적고, STATUS 인용도 그 파일에서 딴다.
def _report(path: str, lines: list) -> None:
    import datetime
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("# {}\n".format(stamp))
        handle.write("\n".join(lines) + "\n")
    unreal.log_warning("보고 파일: {} ({}줄)".format(path, len(lines)))

SAID = []


def say(msg: str) -> None:
    SAID.append(msg)
    unreal.log(msg)


RAW_ROOT = "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Animals"
DEST_ROOT = "/Game/Game/Characters/Animals"

MAMMAL_CLIPS = ["Idle", "Walk", "Run", "Attack", "Hit", "Death"]
# 새는 공격이 없고 **걷기 자리에도 Run 을 쓴다**(날갯짓 도주 — DESIGN 8.7, HANDOFF_11.md).
BIRD_CLIPS = ["Idle", "Run", "Hit", "Death"]

# (폴더=종, 에셋 몸통, 클립 목록, 균일 배율) — 몸통이 `Animal_Fox` 면 `SK_Animal_Fox`·`A_Animal_Fox_Idle`.
# 12종 전부 파일 이름 틀이 같아 **표 한 줄이 한 종**이고 아래 코드는 손대지 않는다.
# 배율은 DESIGN 8.7 "크기" 표 그대로다 — 여기서 숫자를 새로 정하지 않는다.
ANIMALS = [
    ("Fox", "Animal_Fox", MAMMAL_CLIPS, 0.9),
    ("Goat", "Animal_Goat", MAMMAL_CLIPS, 1.0),
    ("Reindeer", "Animal_Reindeer", MAMMAL_CLIPS, 1.5),
    ("Elk", "Animal_Elk", MAMMAL_CLIPS, 1.7),
    ("Lynx", "Animal_Lynx", MAMMAL_CLIPS, 1.0),
    ("Wolf", "Animal_Wolf", MAMMAL_CLIPS, 1.3),
    ("Wolverine", "Animal_Wolverine", MAMMAL_CLIPS, 0.9),
    ("Bear", "Animal_Bear", MAMMAL_CLIPS, 1.8),
    ("Pheasant", "Animal_Pheasant", BIRD_CLIPS, 0.7),
    ("Peacock", "Animal_Peacock", BIRD_CLIPS, 1.1),
    ("Ptarmigan", "Animal_Ptarmigan", BIRD_CLIPS, 0.5),
    ("Capercaillie", "Animal_Capercaillie", BIRD_CLIPS, 0.8),
]

# 바르코가 **가장 긴 변을 1m 로 정규화**해 내보낸다(루프#158 재열람 로그 12줄이 전부 100.0~100.6cm).
# 그래서 "이 종이 이미 제 배율로 들어와 있나" 를 **이름이 아니라 실제 바운드**로 판정한다:
# 최장 변이 `100 × 배율` 이면 맞게 들어온 것이고, 아니면 통째로 지우고 다시 받는다.
# 정규화 오차가 최대 0.6cm 라 허용치는 그보다 넉넉한 1.5cm.
RAW_LONGEST_CM = 100.0
SCALE_TOLERANCE_CM = 1.5


def _mesh_import(fbx_path: str, dest_dir: str, mesh_name: str, scale: float) -> list:
    """메시 FBX 하나를 받는다. 이름은 여기서 못 박아 `SK_Animal_Fox` 로 바로 나오게 한다."""
    # 5.8 은 FBX 를 Interchange 로 받지만(임포트 로그가 `LogInterchangeEngine`) 옛 `FbxImportUI`
    # 를 주면 변환해서 쓴다. **에셋 크기를 실제로 굽히는 칸은 이것 하나뿐이었다**(루프#160 실측):
    #   - `InterchangeGenericAssetsPipeline.import_offset_uniform_scale` → 씬 오프셋이라 100.0cm 그대로
    #   - `mesh_pipeline.build_scale3d` → 스켈레탈 메시에는 빌드 배율이 없어 역시 100.0cm 그대로
    # 대신 이 길에는 대가가 하나 있다 — **`create_physics_asset` 칸이 변환에서 안 넘어가** 종마다
    # `SK_..._PhysicsAsset` 이 빠진다(🟡 확인, STATUS #0155). 지금 코드에 물리 에셋을 쓰는 줄은
    # 0개다(동물 충돌은 `ACharacter` 캡슐) — 크기를 먼저 맞추고 물리 에셋은 따로 푼다.
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", True)
    # 텍스처 둘(`T_..._D`·`_N`)이 FBX 안에 박혀 있어 이 둘을 켜 둬야 같이 나온다(머리말).
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", True)
    options.set_editor_property("import_animations", False)   # 클립은 아래 애니 전용 임포트 몫
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    # **게임 크기는 여기서 정해진다** — 정규화 1m 짜리를 DESIGN 8.7 배율로 굽힌다.
    options.skeletal_mesh_import_data.set_editor_property("import_uniform_scale", scale)

    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = dest_dir
    task.destination_name = mesh_name
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths"))


def _anim_import(fbx_path: str, asset_name: str, dest_dir: str, mesh_name: str, scale: float) -> bool:
    """클립 하나를 `asset_name` 으로 그 종 스켈레톤에 붙인다. 이미 있으면 건너뛴다."""
    asset_path = "{}/{}".format(dest_dir, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        say("이미 있음: {}".format(asset_path))
        return True

    skeleton_path = "{}/{}_Skeleton".format(dest_dir, mesh_name)
    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    if skeleton is None:
        unreal.log_error("스켈레톤이 없다 - 메시부터 임포트해야 한다: {}".format(skeleton_path))
        return False

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
    # **메시와 같은 배율**이어야 한다 — 클립만 1.0 으로 남으면 뼈 오프셋·뿌리 이동이 몸 크기와
    # 어긋나 발이 미끄러진다(INBOX 지시 2026-09-23 17:05).
    options.anim_sequence_import_data.set_editor_property("import_uniform_scale", scale)

    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = dest_dir
    # 이름을 여기서 못 박는다 - Blender 가 뽑은 FBX 의 테이크 이름은 파일마다 같을 수 있다.
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        say("클립 임포트됨: {}".format(path))
    return unreal.EditorAssetLibrary.does_asset_exist(asset_path)


def _target_name(asset, mesh_name: str, body: str) -> str:
    name = asset.get_name()
    if isinstance(asset, unreal.SkeletalMesh):
        return mesh_name
    if isinstance(asset, unreal.Skeleton):
        return mesh_name + "_Skeleton"
    if isinstance(asset, unreal.PhysicsAsset):
        return mesh_name + "_PhysicsAsset"
    if isinstance(asset, unreal.AnimSequence):
        return name                                  # 위에서 이미 못 박아 넣었다
    if isinstance(asset, unreal.MaterialInstance):
        return "MI_{}".format(body)                  # 원본 머티리얼은 `material_0` 하나뿐이다
    if isinstance(asset, unreal.MaterialInterface):
        return "M_{}".format(body)
    if isinstance(asset, unreal.Texture):
        return name if name.startswith("T_") else "T_{}_{}".format(body, name)
    return name


def _rename_pass(dest_dir: str, mesh_name: str, body: str) -> None:
    """임포트 직후에도, 재실행 때도 같은 규칙으로 정리한다."""
    for path in unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is None:
            continue
        want = _target_name(asset, mesh_name, body)
        if asset.get_name() == want:
            say("이름 그대로: {} ({})".format(path, asset.get_class().get_name()))
            continue
        new_path = "{}/{}".format(path.rsplit("/", 1)[0], want)
        if unreal.EditorAssetLibrary.does_asset_exist(new_path):
            unreal.log_error("이름 충돌로 못 바꿈: {} -> {}".format(path, new_path))
            continue
        if unreal.EditorAssetLibrary.rename_asset(path, new_path):
            say("이름 바꿈: {} -> {} ({})".format(path, new_path, asset.get_class().get_name()))
        else:
            unreal.log_error("이름 바꾸기 실패: {} -> {}".format(path, new_path))


def _longest_side_cm(mesh_path: str):
    """이미 있는 메시의 **가장 긴 변**을 cm 로 준다. 못 읽으면 None."""
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        return None
    extent = mesh.get_bounds().box_extent
    return max(extent.x, extent.y, extent.z) * 2.0


failed = []

for folder, body, clips, scale in ANIMALS:
    dest_dir = "{}/{}".format(DEST_ROOT, folder)
    mesh_name = "SK_{}".format(body)
    src_dir = "{}/{}".format(RAW_ROOT, folder)
    mesh_path = "{}/{}".format(dest_dir, mesh_name)

    # **이름이 아니라 실제 크기·에셋 수로 건너뛸지 정한다** — 배율이 바뀌면 같은 이름 그대로 다시
    # 받아야 하고, 개수가 모자라면(예: 물리 에셋이 빠진 반입) 그것도 덜 들어온 것이다.
    longest = _longest_side_cm(mesh_path) if unreal.EditorAssetLibrary.does_asset_exist(mesh_path) else None
    want_longest = RAW_LONGEST_CM * scale
    # 물리 에셋은 이 길로는 안 나온다(위 `_mesh_import` 주석) — 기대 개수에서 뺀다.
    want_count = 5 + len(clips)
    have_count = len(unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False))
    if (longest is not None
            and abs(longest - want_longest) <= SCALE_TOLERANCE_CM
            and have_count == want_count):
        say("이미 제 배율(x{:.1f})·제 개수({}개), 건너뜀: {} (최장 {:.1f}cm, 기대 {:.1f}cm)".format(
            scale, have_count, mesh_path, longest, want_longest))
    else:
        if longest is not None:
            # 배율이 다르면 **폴더를 통째로 비우고 새로 받는다**. 덮어쓰기만 하면 옛 스켈레톤이
            # 남아 클립이 옛 크기의 뼈에 붙는다. 이 12종은 아직 `DT_AnimalTable` 이 안 쓴다(M5-07c).
            say("다시 받는다(최장 {:.1f}cm 기대 {:.1f}cm · 에셋 {}개 기대 {}개) - 폴더를 비운다: {}".format(
                longest, want_longest, have_count, want_count, dest_dir))
            if not unreal.EditorAssetLibrary.delete_directory(dest_dir):
                unreal.log_error("폴더를 못 지웠다: {}".format(dest_dir))
                failed.append("{} 폴더 삭제".format(folder))
                continue
        for path in _mesh_import("{}/{}.fbx".format(src_dir, mesh_name), dest_dir, mesh_name, scale):
            say("임포트됨: {}".format(path))

    # 스켈레톤이 있어야 클립이 붙는다 - 메시가 실패하면 클립은 시도하지 않는다.
    if not unreal.EditorAssetLibrary.does_asset_exist("{}/{}_Skeleton".format(dest_dir, mesh_name)):
        unreal.log_error("메시 임포트 실패: {}".format(mesh_name))
        failed.append("{} 메시".format(folder))
        continue

    for clip in clips:
        asset_name = "A_{}_{}".format(body, clip)
        if not _anim_import("{}/{}.fbx".format(src_dir, asset_name), asset_name, dest_dir, mesh_name, scale):
            failed.append("{} {}".format(folder, clip))

    _rename_pass(dest_dir, mesh_name, body)

    unreal.EditorAssetLibrary.save_directory(dest_dir, only_if_is_dirty=False, recursive=True)

    made = unreal.EditorAssetLibrary.list_assets(dest_dir, recursive=True, include_folder=False)
    for path in made:
        asset = unreal.EditorAssetLibrary.load_asset(path)
        say("최종: {}  [{}]".format(path, asset.get_class().get_name() if asset else "?"))
    say("종 {} 에셋 {}개 (기대 {}개: SK 1 + Skeleton 1 + MI 1 + T 2 + A {}) · 배율 x{:.1f} · 최장 {:.1f}cm".format(
        folder, len(made), 5 + len(clips), len(clips), scale, _longest_side_cm(mesh_path) or -0.0))

if failed:
    unreal.log_error("반입 실패 {}건: {}".format(len(failed), ", ".join(failed)))
else:
    say("반입 완료 - 실패 0건")

_report("~/UNREAL/Snarehaven/logs/import_animal_species.txt", SAID)
