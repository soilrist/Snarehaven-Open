"""M5-01a2·b2 2층 검사 — 새 프로세스에서 바르코 플레이어 몸과 클립 4개를 다시 읽는다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_player_body_assets.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

임포트한 프로세스가 아니라 **새 프로세스에서 읽는 것**이 요점이다 — 같은 프로세스 로그는
저장됐다는 증거가 못 된다(루프#53 함정).

**삼각형 수는 못 잰다** — 스켈레탈 메시에는 `get_num_triangles` Python API 가 없다(루프#74).
그래서 DESIGN 4.2 의 3K 한도는 여기서 판정하지 않고 정점 수만 적는다. 견주던 Quaternius 몸은
루프#85(M5-01c2)에서 지웠다.
"""
import unreal

ROOT_BONE = "Root"   # 바르코 리그(Mixamo 짜임)의 뿌리

TARGETS = [
    ("/Game/Game/Characters/Player/Snare/SK_Player_Body", "바르코"),
]

def bone_names_under(mesh, bone: str) -> list:
    """뿌리에서 내려가며 뼈 이름을 모은다 — `get_bone_children` 은 한 단계만 준다(재귀 인자 없음)."""
    found = []
    for child in mesh.get_bone_children(bone):
        found.append(str(child))
        found += bone_names_under(mesh, child)
    return found


fail = 0
for mesh_path, label in TARGETS:
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        unreal.log_error("검사 실패 — 메시를 못 읽음: {}".format(mesh_path))
        fail += 1
        continue

    skeleton = mesh.skeleton
    bones = skeleton.get_editor_property("bone_tree") if skeleton else []
    editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    verts = editor.get_num_verts(mesh, 0)
    sections = editor.get_num_sections(mesh, 0)
    extent = mesh.get_bounds().box_extent
    slots = [str(m.material_slot_name) for m in mesh.materials]
    mats = [m.material_interface.get_name() if m.material_interface else "(비었다)"
            for m in mesh.materials]

    unreal.log("검사 {} {}: 뼈 {} · 정점 {} · 섹션 {} · 크기 {:.0f}×{:.0f}×{:.0f}cm".format(
        label, mesh.get_name(), len(bones), verts, sections,
        extent.x * 2, extent.y * 2, extent.z * 2))
    unreal.log("    스켈레톤 {} · 머티리얼 슬롯 {} {} → {}".format(
        skeleton.get_name() if skeleton else "(없다)", len(slots), slots, mats))

    # 리타겟한 클립(M5-01b)이 붙으려면 뼈 이름·계층이 맞아야 한다 — 뿌리에서 세어 찍어 둔다.
    children = bone_names_under(mesh, ROOT_BONE)
    unreal.log("    뿌리 `{}` 아래 뼈 {}개 · 첫 다섯 {}".format(
        ROOT_BONE, len(children), [str(b) for b in children[:5]]))
    if not children and mesh_path == TARGETS[0][0]:
        unreal.log_error("검사 실패 — 뿌리 뼈 `{}` 가 없다: {}".format(ROOT_BONE, mesh_path))
        fail += 1

    # 켜져 있으면 엔진이 glb 의 부드러운 법선을 버리고 다시 계산해 팔·신발이 각져 보인다(루프#846, INBOX 보정 2026-10-09).
    recompute = editor.get_lod_build_settings(mesh, 0).get_editor_property("recompute_normals")
    unreal.log_warning("[PlayerBody] 법선 다시 계산(recompute_normals) {}".format(recompute))
    if recompute:
        unreal.log_error("검사 실패 — 법선을 다시 계산하는 설정이 켜져 있다(각진 면): {}".format(mesh_path))
        fail += 1

    for m in mesh.materials:
        if m.material_interface is None:
            unreal.log_error("검사 실패 — 머티리얼이 빈 슬롯: {} {}".format(
                mesh_path, m.material_slot_name))
            fail += 1

# 클립(M5-01b2)은 **몸과 같은 스켈레톤을 가리키고 본 트랙이 있어야** 쓸 수 있다.
# 길이만 있고 포즈가 없는 껍데기 시퀀스를 통과라고 쓰지 않으려고 트랙 수까지 센다.
CLIPS = ("A_Player_Idle", "A_Player_Walk", "A_Player_Run", "A_Player_Attack")
WANT_SKELETON = "/Game/Game/Characters/Player/Snare/SK_Player_Body_Skeleton"

for name in CLIPS:
    path = "/Game/Game/Characters/Player/Snare/{}".format(name)
    anim = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(anim, unreal.AnimSequence):
        unreal.log_error("검사 실패 — 클립을 못 읽음: {}".format(path))
        fail += 1
        continue
    skeleton = anim.get_editor_property("skeleton")
    tracks = unreal.AnimationLibrary.get_animation_track_names(anim)
    frames = anim.get_editor_property("number_of_sampled_frames")
    unreal.log("검사 클립 {}: 길이 {:.2f}초 · 프레임 {} · 본트랙 {} · 스켈레톤 {}".format(
        name, anim.get_editor_property("sequence_length"), frames, len(tracks),
        skeleton.get_path_name() if skeleton else "(없다)"))
    if skeleton is None or skeleton.get_path_name().split(".")[0] != WANT_SKELETON:
        unreal.log_error("검사 실패 — 스켈레톤이 다르다: {} (바라는 것 {})".format(path, WANT_SKELETON))
        fail += 1
    if not tracks or frames < 2:
        unreal.log_error("검사 실패 — 포즈가 없는 껍데기 클립: {}".format(path))
        fail += 1

# 텍스처가 머티리얼에 실제로 물렸는지 — 루프#70 의 흰색 사고를 여기서 잡는다.
for path in unreal.EditorAssetLibrary.list_assets(
        "/Game/Game/Characters/Player/Snare", recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    unreal.log("에셋: {}  [{}]".format(path, asset.get_class().get_name() if asset else "?"))
    if isinstance(asset, unreal.MaterialInstanceConstant):
        used = [p.parameter_value.get_name()
                for p in asset.get_editor_property("texture_parameter_values")
                if p.parameter_value]
        unreal.log("    물린 텍스처 {}개: {}".format(len(used), used))
        if not used:
            unreal.log_error("검사 실패 — 머티리얼에 텍스처가 하나도 안 물렸다: {}".format(path))
            fail += 1

unreal.log_warning("검사 끝 — 실패 {}건".format(fail))
