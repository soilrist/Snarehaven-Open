"""걷기·달리기 클립을 **한 바퀴만 남기고** 다듬는다 (INBOX 결함 ②, 루프#139).

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/trim_player_anim_loops.py" \
      -unattended -nosplash -abslog=~/UNREAL/Snarehaven/logs/trim_player_anim_loops.log

임포트한 Quaternius 클립에는 두 가지 군더더기가 있다 (`tools/unreal/check_anim_loop.py` 로 잰 값):
  ① **앞머리 정지 프레임** — 달리기는 0·1·2 프레임의 발 위치가 **완전히 같다**(0.00cm).
     30fps 라 되감을 때마다 0.07초 동안 그 자리에 선다. 이것이 사람이 말한 "틱 걸리듯 멈칫" 이다.
  ② **한 바퀴를 넘긴 꼬리** — 마지막 프레임이 첫 프레임에서 22.3cm(걷기 29.7cm) 떨어져 있어,
     되감는 순간 발이 한 프레임 만에 그만큼 튄다(보통 한 프레임 이동량은 5cm 안쪽).

그래서 **앞머리 정지 구간을 통째로 버리고, 그 다음 자세로 되돌아오는 프레임 직전까지만** 남긴다.
자르는 자리는 지어낸 숫자가 아니라 발 위치로 재서 고른다 — 클립을 다시 임포트해도 같은 자리가 나온다.

다시 돌려도 안전하다: 이미 다듬어진 클립(앞머리 정지 없음 + 이음매가 한 프레임 이동량 안)은 건너뛴다.
"""
import unreal

ANIM_LIB = unreal.AnimationLibrary
MATH_LIB = unreal.MathLibrary
ASSET_LIB = unreal.EditorAssetLibrary

CLIPS = [
    ("걷기", "/Game/Game/Characters/Player/Snare/A_Player_Walk"),
    ("달리기", "/Game/Game/Characters/Player/Snare/A_Player_Run"),
]
FOOT_BONES = ["LeftToeBase", "RightToeBase"]

# 같은 자세로 볼 거리(cm). 30fps 에서 발은 한 프레임에 5~8cm 움직이므로 그보다 훨씬 작은 값이다.
SAME_POSE_CM = 0.5

# 한 걸음 주기는 아무리 빨라도 0.25초는 된다 — 30fps 로 8프레임. 그보다 가까운 프레임은
# 걸음 한 바퀴가 아니라 그냥 옆 프레임이라 후보에서 뺀다.
# 발이 반대로 놓인 반 바퀴 지점은 따로 거를 필요가 없다: `pose_distance` 가 양발 거리의
# 최댓값이라 왼발이 맞아도 오른발이 어긋나면 후보가 되지 못한다.
MIN_CYCLE_FRAMES = 8


def component_position(anim, chain, frame):
    pos = ANIM_LIB.get_bone_pose_for_frame(anim, chain[0], frame, False).translation
    for bone in chain[1:]:
        pos = MATH_LIB.transform_location(ANIM_LIB.get_bone_pose_for_frame(anim, bone, frame, False), pos)
    return pos


def foot_tracks(anim, frames):
    tracks = {}
    for bone in FOOT_BONES:
        chain = [str(b) for b in ANIM_LIB.find_bone_path_to_root(anim, bone)]
        tracks[bone] = [component_position(anim, chain, f) for f in range(frames + 1)]
    return tracks


def pose_distance(tracks, a, b):
    return max((tracks[bone][a] - tracks[bone][b]).length() for bone in tracks)


def pick_cycle(tracks, frames):
    """(시작 프레임, 끝 프레임) — 끝 프레임은 시작 자세로 돌아오는 프레임이라 **남기지 않는다**."""
    start = 0
    while start + 1 <= frames and pose_distance(tracks, start + 1, start) < SAME_POSE_CM:
        start += 1

    # 앞머리 정지 구간은 **한 장도 남기지 않는다**. 그 자세는 걸음 한 바퀴에 속하지 않아서,
    # 정지 자세에 맞춰 끝 프레임을 고르면 이음매가 어긋난 채로 남는다 (1차 시도가 이래서 실패했다:
    # 달리기 이음매 22.28 -> 21.60cm, 거의 그대로였다).
    if start > 0:
        start += 1

    end = min(range(start + MIN_CYCLE_FRAMES, frames + 1), key=lambda f: pose_distance(tracks, f, start))
    return start, end


def trim(label, path):
    anim = ASSET_LIB.load_asset(path)
    frames = ANIM_LIB.get_num_frames(anim)
    tracks = foot_tracks(anim, frames)
    start, end = pick_cycle(tracks, frames)
    seam = pose_distance(tracks, frames, 0)
    step = pose_distance(tracks, frames, frames - 1)

    if start == 0 and seam <= max(step, SAME_POSE_CM):
        unreal.log("[{}] 건너뜀 — 이미 한 바퀴다 (앞머리 정지 없음 · 이음매 {:.2f}cm)".format(label, seam))
        return

    controller = anim.get_editor_property("controller")
    model = anim.get_editor_property("data_model_interface")

    # 트랙은 **이름으로** 받아온다 — `get_bone_animation_tracks()` 는 5.8 에서 빈 배열을 돌려주고,
    # 그걸 돌면 루프가 한 번도 안 돌아 프레임 수만 줄어든 반쪽 클립이 조용히 저장된다 (루프#139).
    track_names = list(model.get_bone_track_names())
    if not track_names:
        unreal.log_error("[{}] 본 트랙을 하나도 못 읽었다 — 저장하지 않는다".format(label))
        return

    # 남길 구간은 [start, end) — end 는 start 와 같은 자세라 넣으면 그 한 장이 두 번 나와 한 프레임 멈춘다.
    # 키 배열은 구조체(`RawAnimSequenceTrack`)를 뜯지 않고 프레임별 로컬 포즈로 만든다. 그러면
    # pos·rot·scale 길이가 저절로 같아진다 — `SetBoneTrackKeys` 는 셋 길이가 다르면 조용히 `false` 다.
    kept = list(range(start, end))
    sliced = []
    for bone in track_names:
        poses = [ANIM_LIB.get_bone_pose_for_frame(anim, bone, f, True) for f in kept]
        sliced.append((bone, [p.translation for p in poses], [p.rotation for p in poses], [p.scale3d for p in poses]))

    controller.open_bracket("루프#139 한 바퀴만 남기기", False)
    controller.set_number_of_frames(unreal.FrameNumber(len(kept) - 1), False)
    failed = [str(bone) for bone, pos, rot, scale in sliced if not controller.set_bone_track_keys(bone, pos, rot, scale, False)]
    controller.close_bracket(False)

    # 한 본이라도 거부당하면 클립이 반쪽이 된다 — 저장하지 않고 실패를 크게 알린다.
    if failed:
        unreal.log_error("[{}] 본 {}개 키 교체 실패 — 저장하지 않는다: {}".format(label, len(failed), ", ".join(failed[:5])))
        return

    ASSET_LIB.save_asset(path, only_if_is_dirty=False)
    unreal.log("[{}] {} 프레임 {} -> {} (남긴 구간 {}~{} · 버린 앞머리 {}장 · 꼬리 {}장)".format(
        label, path.rsplit("/", 1)[-1], frames, ANIM_LIB.get_num_frames(anim),
        start, end - 1, start, frames - end + 1))


for label, path in CLIPS:
    trim(label, path)
unreal.log("클립 한 바퀴 다듬기 끝")
