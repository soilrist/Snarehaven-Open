"""바르코 동물 2층 검사 — 새 프로세스에서 되읽어 메시·스켈레톤·클립·텍스처 짝을 찍는다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_animal_species.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

임포트한 프로세스가 아니라 **새 프로세스에서 읽는 것**이 요점이다 — 같은 프로세스 로그는
저장됐다는 증거가 못 된다(루프#53 함정). `verify_livestock_assets.py`(소·양)와 같은 잣대에
바르코 동물에만 있는 둘을 더 본다:
  - **클립이 종 스켈레톤에 붙었나** — 클립 FBX 는 뼈만 담고 있어, 스켈레톤 지정이 빠지면
    엔진이 `root_Skeleton` 같은 새 스켈레톤을 조용히 만들고 화면에서 뒤틀린다.
  - **머티리얼이 원화 색 보정본을 물고 있나** — `T_..._D`·`T_..._N` 이 아니라 엔진 기본
    회색을 물면 3층 캡처에서야 드러난다.
  - **게임 크기가 DESIGN 8.7 표대로인가** — 바르코 원본은 가장 긴 변이 1m 로 정규화돼 나와
    곰과 뇌조가 같은 크기다. 임포터가 배율을 빠뜨리면 이 검사가 잡는다(±1cm).

경고 줄은 판정하지 않는다(로그는 이 스크립트 밖에 있다) — 검사는 **에셋의 실제 값**으로만 한다.
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


MAMMAL_CLIPS = ["Idle", "Walk", "Run", "Attack", "Hit", "Death"]
BIRD_CLIPS = ["Idle", "Run", "Hit", "Death"]      # 새는 공격 없음, 걷기 자리에도 Run

# (폴더=종, 에셋 몸통, 클립 목록, 기대 뼈 수, 기대 게임 크기 폭×길이×높이 cm)
# 뼈 수는 `_Source/raw/.../HANDOFF*.md` 표 — 종마다 다른 것이 이 검사의 핵심이다: 리깅에서
# 뼈가 빠지면 본트랙 수가 모자라게 나온다.
# **게임 크기는 DESIGN 8.7 "크기" 표를 그대로 옮긴 것**이다(감독 결정 2026-09-23). 여기서
# 숫자를 정하지 않는다 — 임포터의 배율 칸과 따로 적어 둬야 배율을 빠뜨린 반입을 잡아낸다.
ANIMALS = [
    ("Fox", "Animal_Fox", MAMMAL_CLIPS, 22, (32, 90, 61)),
    ("Goat", "Animal_Goat", MAMMAL_CLIPS, 19, (39, 100, 93)),
    ("Reindeer", "Animal_Reindeer", MAMMAL_CLIPS, 19, (56, 127, 150)),
    ("Elk", "Animal_Elk", MAMMAL_CLIPS, 19, (80, 143, 171)),
    ("Lynx", "Animal_Lynx", MAMMAL_CLIPS, 20, (42, 100, 90)),
    ("Wolf", "Animal_Wolf", MAMMAL_CLIPS, 22, (46, 130, 86)),
    ("Wolverine", "Animal_Wolverine", MAMMAL_CLIPS, 21, (26, 90, 48)),
    ("Bear", "Animal_Bear", MAMMAL_CLIPS, 19, (73, 180, 115)),
    ("Pheasant", "Animal_Pheasant", BIRD_CLIPS, 17, (28, 70, 48)),
    ("Peacock", "Animal_Peacock", BIRD_CLIPS, 18, (36, 110, 84)),
    ("Ptarmigan", "Animal_Ptarmigan", BIRD_CLIPS, 17, (36, 50, 45)),
    ("Capercaillie", "Animal_Capercaillie", BIRD_CLIPS, 17, (43, 80, 72)),
]

# DESIGN 표 값이 cm 단위 반올림이라(예: 곰 40.4×1.8 = 72.72 → 73) 반올림 오차 0.5 보다 넉넉하게 둔다.
SIZE_TOLERANCE_CM = 1.0

DEST_ROOT = "/Game/Game/Characters/Animals"

fail = 0

for folder, body, clips, want_bones, want_size in ANIMALS:
    dest_dir = "{}/{}".format(DEST_ROOT, folder)
    mesh_path = "{}/SK_{}".format(dest_dir, body)
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        unreal.log_error("검사 실패 - 메시를 못 읽음: {}".format(mesh_path))
        fail += 1
        continue

    skeleton = mesh.skeleton
    verts = unreal.get_editor_subsystem(
        unreal.SkeletalMeshEditorSubsystem).get_num_verts(mesh, 0)
    extent = mesh.get_bounds().box_extent
    size = (extent.x * 2, extent.y * 2, extent.z * 2)
    say("검사 {}: 정점 {} · 크기 {:.1f}x{:.1f}x{:.1f}cm (기대 {}x{}x{}cm) · 스켈레톤 {}".format(
        body, verts, size[0], size[1], size[2],
        want_size[0], want_size[1], want_size[2],
        skeleton.get_name() if skeleton else "없음"))
    # 크기는 게임에서 사람이 바로 느끼는 값이라 세 축을 다 본다(높이만 맞고 길이가 틀릴 수 있다).
    for axis, got, want in zip("폭길높", size, want_size):
        if abs(got - want) > SIZE_TOLERANCE_CM:
            unreal.log_error("검사 실패 - {} {} {:.1f}cm, 기대 {}cm (±{}cm)".format(
                body, axis, got, want, SIZE_TOLERANCE_CM))
            fail += 1

    for slot in mesh.materials:
        mat = slot.material_interface
        # 스켈레탈 메시에는 "이 머티리얼이 쓰는 텍스처" 를 주는 파이썬 API 가 없다 —
        # 임포트가 만든 머티리얼 인스턴스의 텍스처 파라미터 값을 그대로 읽는다.
        values = (mat.get_editor_property("texture_parameter_values")
                  if isinstance(mat, unreal.MaterialInstance) else [])
        used = sorted(v.parameter_value.get_name() for v in values if v.parameter_value)
        say("검사 {}: 머티리얼 {} · 텍스처 {}".format(
            body, mat.get_name() if mat else "없음", used))
        # 원화 색 보정본을 물지 않으면 3층 캡처가 엉뚱한 색으로 나온다.
        for want in ("T_{}_D".format(body), "T_{}_N".format(body)):
            if want not in used:
                unreal.log_error("검사 실패 - 머티리얼이 {} 를 안 문다".format(want))
                fail += 1

    for clip in clips:
        path = "{}/A_{}_{}".format(dest_dir, body, clip)
        seq = unreal.EditorAssetLibrary.load_asset(path)
        if seq is None:
            unreal.log_error("검사 실패 - 클립을 못 읽음: {}".format(path))
            fail += 1
            continue
        # 껍데기(길이만 있고 포즈가 없는) 시퀀스를 통과라고 쓰지 않으려고 본 트랙까지 센다.
        tracks = unreal.AnimationLibrary.get_animation_track_names(seq)
        seq_skel = seq.get_editor_property("skeleton")
        say("검사 {}: {} 길이={:.2f}초 프레임={} 본트랙={} 스켈레톤={}".format(
            body, seq.get_name(),
            seq.get_editor_property("sequence_length"),
            seq.get_editor_property("number_of_sampled_frames"),
            len(tracks), seq_skel.get_name() if seq_skel else "없음"))
        if seq_skel != skeleton:
            unreal.log_error("검사 실패 - 클립이 다른 스켈레톤: {}".format(seq.get_name()))
            fail += 1
        # 본 트랙 수 = 뼈 수. 0 이면 껍데기 클립이고, 모자라면 리깅에서 빠진 뼈가 있다는 뜻이다.
        if len(tracks) != want_bones:
            unreal.log_error("검사 실패 - 본 트랙 {}개, 기대 {}개: {}".format(
                len(tracks), want_bones, seq.get_name()))
            fail += 1

say("검사 결과: {}".format("전부 통과" if fail == 0 else "실패 {}건".format(fail)))

_report("~/UNREAL/Snarehaven/logs/verify_animal_species.txt", SAID)
