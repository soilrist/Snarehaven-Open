"""달리기·걷기 클립이 **매끄럽게 이어지는지** 클립 데이터로 확인한다 (결함 ②, 루프#139).

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/check_anim_loop.py" \
      -unattended -nosplash -abslog=~/UNREAL/Snarehaven/logs/check_anim_loop.log

보는 것 둘:
  ① **마지막 프레임이 첫 프레임과 같은가** — glTF/FBX 로 내보낸 반복 클립은 첫 포즈를 끝에 한 번 더
     넣는 일이 흔하다. 엔진은 [0, 길이] 를 돌며 되감으므로 그 포즈가 두 번 나와 한 프레임 멈춘다.
  ② **디딘 발이 프레임마다 뒤로 흐르는 속도** — 캐릭터는 일정 속도로 나아가는데 이 값이 프레임마다
     크게 흔들리면 발이 앞뒤로 미끄러져 "멈칫" 처럼 보인다 (속도는 중앙값으로 맞춰 뒀다).
"""
import unreal

ANIM_LIB = unreal.AnimationLibrary
MATH_LIB = unreal.MathLibrary

CLIPS = [
    ("걷기", "/Game/Game/Characters/Player/Snare/A_Player_Walk"),
    ("달리기", "/Game/Game/Characters/Player/Snare/A_Player_Run"),
]
FOOT_BONES = ["LeftToeBase", "RightToeBase"]


def component_position(anim, chain, frame):
    pos = ANIM_LIB.get_bone_pose_for_frame(anim, chain[0], frame, False).translation
    for bone in chain[1:]:
        pos = MATH_LIB.transform_location(ANIM_LIB.get_bone_pose_for_frame(anim, bone, frame, False), pos)
    return pos


def report(label, path):
    anim = unreal.EditorAssetLibrary.load_asset(path)
    frames = ANIM_LIB.get_num_frames(anim)
    length = ANIM_LIB.get_sequence_length(anim)
    spf = length / float(frames)
    tracks = {}
    for bone in FOOT_BONES:
        chain = [str(b) for b in ANIM_LIB.find_bone_path_to_root(anim, bone)]
        tracks[bone] = [component_position(anim, chain, f) for f in range(frames + 1)]

    # 첫 프레임과 마지막 프레임이 같으면 되감을 때 같은 포즈가 두 번 나온다.
    seam = max((tracks[b][frames] - tracks[b][0]).length() for b in FOOT_BONES)
    prev_gap = max((tracks[b][frames] - tracks[b][frames - 1]).length() for b in FOOT_BONES)

    axis = max("xy", key=lambda a: max(max(getattr(p, a) for p in ps) - min(getattr(p, a) for p in ps)
                                       for ps in tracks.values()))
    drifts = []
    for f in range(frames):
        drifts.append(max((getattr(ps[f], axis) - getattr(ps[f + 1], axis)) / spf for ps in tracks.values()))
    planted = sorted(d for d in drifts if d > 0.0)
    median = planted[len(planted) // 2] if planted else 0.0

    unreal.log("[{}] {} 프레임 {} · 길이 {:.3f}초 · 이음매 거리 {:.2f}cm (직전 프레임 간격 {:.2f}cm)".format(
        label, path.rsplit("/", 1)[-1], frames, length, seam, prev_gap))
    unreal.log("[{}] 디딘 발 흐름 중앙값 {:.1f} · 최소 {:.1f} · 최대 {:.1f} (클립 공간 cm/s)".format(
        label, median, planted[0] if planted else 0.0, planted[-1] if planted else 0.0))


for label, path in CLIPS:
    report(label, path)
unreal.log("클립 이음매 검사 끝")
