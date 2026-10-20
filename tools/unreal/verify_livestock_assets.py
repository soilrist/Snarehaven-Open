"""M5-04a 2층 검사 — 새 프로세스에서 소·양 에셋을 다시 읽어 크기·클립·스켈레톤 짝을 찍는다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_livestock_assets.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

임포트한 프로세스가 아니라 **새 프로세스에서 읽는 것**이 요점이다 — 같은 프로세스 로그는
저장됐다는 증거가 못 된다(루프#53 함정).

**삼각형 수는 못 잰다.** 스켈레탈 메시에는 스태틱 메시의 `get_num_triangles(lod)` 에 해당하는
Python API 가 없다(`EditorSkeletalMeshLibrary` 에는 `get_num_verts`·`get_num_sections` 뿐 —
루프#74 가 `dir()` 로 확인). 그래서 DESIGN 4.2 의 오브젝트당 3K **삼각형** 한도는 여기서
판정하지 않고 **정점 수만 적는다**. 이미 게임에 있는 돼지(M1-07)를 같은 잣대로 같이 찍어
견줄 수 있게 한다.
"""
import unreal

TARGETS = [
    ("/Game/Game/Characters/Animals/Cow", "SK_Animal_Cow"),
    ("/Game/Game/Characters/Animals/Sheep", "SK_Animal_Sheep"),
    ("/Game/Game/Characters/Animals/Pig", "SK_Animal_Pig"),   # 견주는 기준(M1-07)
]

fail = 0
for folder, mesh_name in TARGETS:
    mesh_path = "{}/{}".format(folder, mesh_name)
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        unreal.log_error("검사 실패 — 메시를 못 읽음: {}".format(mesh_path))
        fail += 1
        continue

    verts = unreal.EditorSkeletalMeshLibrary.get_num_verts(mesh, 0)
    sections = unreal.get_editor_subsystem(
        unreal.SkeletalMeshEditorSubsystem).get_num_sections(mesh, 0)
    slots = [str(m.material_slot_name) for m in mesh.materials]
    extent = mesh.get_bounds().box_extent
    unreal.log("검사 {}: 정점 {} · 섹션 {} · 크기 {:.0f}×{:.0f}×{:.0f}cm · 머티리얼 슬롯 {} {}".format(
        mesh_name, verts, sections, extent.x * 2, extent.y * 2, extent.z * 2, len(slots), slots))

    anims = sorted(p.rsplit("/", 1)[-1] for p in
                   unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False)
                   if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.AnimSequence))
    unreal.log("검사 {}: 클립 {}개 {}".format(mesh_name, len(anims), anims))

    for name in anims:
        seq = unreal.EditorAssetLibrary.load_asset("{}/{}".format(folder, name))
        # 껍데기(길이만 있고 포즈가 없는) 시퀀스를 통과라고 쓰지 않으려고 본 트랙까지 센다.
        tracks = unreal.AnimationLibrary.get_animation_track_names(seq)
        seq_skel = seq.get_editor_property("skeleton")
        unreal.log("검사 {}: {} 길이={:.2f}초 프레임={} 본트랙={} 스켈레톤={}".format(
            mesh_name, name,
            seq.get_editor_property("sequence_length"),
            seq.get_editor_property("number_of_sampled_frames"),
            len(tracks), seq_skel.get_name() if seq_skel else "없음"))
        if seq_skel != mesh.skeleton:
            # 클립이 남의 스켈레톤에 붙으면 화면에서 뒤틀린다.
            unreal.log_error("검사 실패 — 클립이 다른 스켈레톤: {}".format(name))
            fail += 1
        if len(tracks) == 0:
            unreal.log_error("검사 실패 — 본 트랙이 없는 껍데기 클립: {}".format(name))
            fail += 1

unreal.log("검사 결과: {}".format("전부 통과" if fail == 0 else "실패 {}건".format(fail)))
