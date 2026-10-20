"""공격 클립에서 **휘두르기 한 번만 남긴다** (INBOX 결함 ③, 루프#140).

**`landing_smash` 전용 — `club_swing` 이후로는 안 쓴다**(감독 지시 2026-09-23 14:05, 루프#161이 적음):
감독이 직접 만든 `club_swing.glb` 는 처음부터 휘두르기 한 바퀴 0.6초라 자를 것이 없다.
지우지 않고 남겨 두는 것은 Mixamo 콤보 클립을 또 받게 될 때 쓰는 길이어서다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/trim_player_attack_clip.py" \
      -unattended -nosplash -abslog=~/UNREAL/Snarehaven/logs/trim_player_attack_clip.log

임포트한 `A_Player_Attack` 은 **휘두르기 한 번이 아니라 4연타 콤보**다 —
`tools/unreal/check_attack_anim.py` 로 재면 145프레임·4.833초로 공격 간격 0.6초(DESIGN 3.1)의 **8.06배**이고,
꼬리 15프레임은 손이 아예 안 움직인다. 이걸 반복 재생하니 0.6초마다 타격이 나가는 동안 몸은 긴 콤보를
느릿느릿 돌아 "몽둥이 한 번 휘두르는 게 아니라 늘어지는 동작" 으로 보인다(사람 실플레이 2026-09-22).

그래서 **손이 멈춘 자리로 콤보를 토막 내고, 그중 하나만** 남긴다.
고르는 기준은 **이음매 ÷ 한 프레임 이동량** 이 가장 작은 토막이다 — 남긴 토막은 공격을 누르고 있는
동안 되풀이되므로, 끝 자세가 첫 자세에서 멀면 0.6초마다 팔이 순간이동한다. 최고 속도만 보고 고른
1차 시도가 이래서 틀렸다(토막 105~124 는 가장 빠르지만 이음매가 82.85cm = 한 프레임 이동량의 5.5배).
다만 잔떨림 토막이 이음매가 작다고 이기면 안 되므로, **최고 속도가 클립 전체 최고의 절반 이상**인
토막만 후보로 본다. 앞뒤로 **멈춘 프레임 한 장씩**을 붙여 자세가 가라앉은 데서 시작하고 끝나게 한다.
자르는 자리는 지어낸 숫자가 아니라 손 위치로 재서 고르므로 클립을 다시 임포트해도 같은 자리가 나온다.

다시 돌려도 안전하다: 이미 토막이 하나뿐인 클립(= 휘두르기 하나)은 건너뛴다.
"""
import unreal

ANIM_LIB = unreal.AnimationLibrary
MATH_LIB = unreal.MathLibrary
ASSET_LIB = unreal.EditorAssetLibrary

CLIP = "/Game/Game/Characters/Player/Snare/A_Player_Attack"
HAND_BONE = "RightHand"

# 손이 "멈췄다" 고 볼 한 프레임 이동량(cm). 30fps 에서 휘두르는 손은 한 프레임에 10~17cm 가므로
# 그보다 한 자릿수 작은 값이다 (걷기 클립의 `SAME_POSE_CM` 0.5 과 같은 결의 여유값).
REST_STEP_CM = 1.0

# 토막이 이보다 짧으면 휘두르기가 아니라 멈춤 사이의 잔떨림이다 — 0.6초 간격의 1/4 (30fps 로 5프레임).
MIN_SWING_FRAMES = 5


def component_position(anim, chain, frame):
    pos = ANIM_LIB.get_bone_pose_for_frame(anim, chain[0], frame, False).translation
    for bone in chain[1:]:
        pos = MATH_LIB.transform_location(ANIM_LIB.get_bone_pose_for_frame(anim, bone, frame, False), pos)
    return pos


def hand_track(anim, frames):
    chain = [str(b) for b in ANIM_LIB.find_bone_path_to_root(anim, HAND_BONE)]
    return [component_position(anim, chain, f) for f in range(frames + 1)]


def swings(steps, frames):
    """손이 멈춘 자리로 끊은 토막들 — 앞뒤로 멈춘 프레임 한 장씩 붙인 (시작, 끝, 최고 속도)."""
    found = []
    start = None
    for f, step in enumerate(steps):
        if step >= REST_STEP_CM:
            start = f if start is None else start
        elif start is not None:
            if f - start >= MIN_SWING_FRAMES:
                found.append((max(start - 1, 0), min(f + 1, frames), max(steps[start:f])))
            start = None
    if start is not None and len(steps) - start >= MIN_SWING_FRAMES:
        found.append((max(start - 1, 0), frames, max(steps[start:])))
    return found


anim = ASSET_LIB.load_asset(CLIP)
frames = ANIM_LIB.get_num_frames(anim)
track = hand_track(anim, frames)
steps = [(track[f + 1] - track[f]).length() for f in range(frames)]
found = swings(steps, frames)
unreal.log("[공격] 토막 {}개 {}".format(len(found), [(a, b) for a, b, _ in found]))

if len(found) <= 1:
    unreal.log("[공격] 건너뜀 — 이미 휘두르기 하나다 ({}프레임 · {:.3f}초)".format(
        frames, ANIM_LIB.get_sequence_length(anim)))
else:
    # 잔떨림 토막이 이음매만 작다고 이기지 않게, 진짜 휘두르기만 후보로 본다.
    fastest = max(s[2] for s in found)
    candidates = [s for s in found if s[2] >= fastest * 0.5]
    best = min(candidates, key=lambda s: (track[s[1]] - track[s[0]]).length() / s[2])
    start, end = best[0], best[1]
    seam = (track[end] - track[start]).length()
    unreal.log("[공격] 고른 토막 {}~{} · 최고 {:.1f}cm/프레임 · 이음매 {:.2f}cm (한 프레임 이동량의 {:.1f}배)".format(
        start, end, best[2], seam, seam / best[2]))
    kept = list(range(start, end + 1))

    controller = anim.get_editor_property("controller")
    model = anim.get_editor_property("data_model_interface")

    # 트랙은 **이름으로** 받는다 — `get_bone_animation_tracks()` 는 5.8 에서 빈 배열이다 (루프#139).
    track_names = list(model.get_bone_track_names())
    if not track_names:
        unreal.log_error("[공격] 본 트랙을 하나도 못 읽었다 — 저장하지 않는다")
    else:
        # 키 배열은 프레임별 로컬 포즈로 만든다 — pos·rot·scale 길이가 저절로 같아진다
        # (`set_bone_track_keys` 는 셋 길이가 다르면 조용히 `false`, 루프#139).
        sliced = []
        for bone in track_names:
            poses = [ANIM_LIB.get_bone_pose_for_frame(anim, bone, f, True) for f in kept]
            sliced.append((bone, [p.translation for p in poses], [p.rotation for p in poses], [p.scale3d for p in poses]))

        controller.open_bracket("루프#140 휘두르기 한 번만 남기기", False)
        controller.set_number_of_frames(unreal.FrameNumber(len(kept) - 1), False)
        failed = [str(b) for b, p, r, s in sliced if not controller.set_bone_track_keys(b, p, r, s, False)]
        controller.close_bracket(False)

        if failed:
            unreal.log_error("[공격] 본 {}개 키 교체 실패 — 저장하지 않는다: {}".format(len(failed), ", ".join(failed[:5])))
        else:
            ASSET_LIB.save_asset(CLIP, only_if_is_dirty=False)
            unreal.log("[공격] 프레임 {} -> {} · 길이 {:.3f}초 (남긴 토막 {}~{} · 최고 속도 {:.1f}cm/프레임)".format(
                frames, ANIM_LIB.get_num_frames(anim), ANIM_LIB.get_sequence_length(anim), start, end, best[2]))

unreal.log("공격 클립 다듬기 끝")
