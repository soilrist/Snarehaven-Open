"""공격 클립이 **한 번의 휘두르기로 보이는지** 클립 데이터로 확인한다 (결함 ③, 루프#140).

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/check_attack_anim.py" \
      -unattended -nosplash -abslog=~/UNREAL/Snarehaven/logs/check_attack_anim.log

보는 것 둘:
  ① **클립 길이 vs 공격 간격 0.6초** (DESIGN 3.1) — 길이가 간격보다 길면 한 번 휘두르는 도중에 다음
     타격이 나가므로 모션이 끊기지 않고 늘어져 보인다. 재생 속도로 맞출 배율이 곧 길이÷간격이다.
  ② **휘두르는 손이 프레임마다 움직인 거리** — 앞뒤로 멈춰 있는 구간(정지 홀드)이 길면 재생 속도만
     올려도 "늘어진다" 가 남는다. 걷기·달리기에서 그랬다 (루프#139).
"""
import unreal

ANIM_LIB = unreal.AnimationLibrary
MATH_LIB = unreal.MathLibrary

CLIP = "/Game/Game/Characters/Player/Snare/A_Player_Attack"
HAND_BONE = "RightHand"
ATTACK_INTERVAL_SECONDS = 0.6  # DESIGN 3.1 「공격 간격」


def component_position(anim, chain, frame):
    pos = ANIM_LIB.get_bone_pose_for_frame(anim, chain[0], frame, False).translation
    for bone in chain[1:]:
        pos = MATH_LIB.transform_location(ANIM_LIB.get_bone_pose_for_frame(anim, bone, frame, False), pos)
    return pos


anim = unreal.EditorAssetLibrary.load_asset(CLIP)
frames = ANIM_LIB.get_num_frames(anim)
length = ANIM_LIB.get_sequence_length(anim)
chain = [str(b) for b in ANIM_LIB.find_bone_path_to_root(anim, HAND_BONE)]
track = [component_position(anim, chain, f) for f in range(frames + 1)]
steps = [(track[f + 1] - track[f]).length() for f in range(frames)]

unreal.log("[공격] {} 프레임 {} · 길이 {:.3f}초 · 간격 {:.2f}초 대비 {:.2f}배".format(
    CLIP.rsplit("/", 1)[-1], frames, length, ATTACK_INTERVAL_SECONDS, length / ATTACK_INTERVAL_SECONDS))
unreal.log("[공격] 손 이동 최소 {:.2f} · 중앙값 {:.2f} · 최대 {:.2f}cm/프레임 · 0.5cm 미만(정지) {}프레임".format(
    min(steps), sorted(steps)[len(steps) // 2], max(steps), sum(1 for s in steps if s < 0.5)))
unreal.log("[공격] 이음매 거리 {:.2f}cm (마지막↔첫 프레임)".format((track[frames] - track[0]).length()))

# 몇 번 휘두르는 클립인지는 손이 멈추는 자리로 갈린다 — 재생 속도만 올릴지, 한 번만 잘라 쓸지가 갈린다.
unreal.log("[공격] 프레임별 손 이동(cm) " + " ".join("{:.0f}".format(s) for s in steps))
rests = [f for f, s in enumerate(steps) if s < 1.0]
unreal.log("[공격] 손이 멈춘 프레임 {}".format(rests))

# 토막 하나만 남겨 되풀이할 때 **이음매가 튀지 않는지**가 고르는 기준이다 — 최고 속도만 보고 고르면
# 끝 자세가 첫 자세에서 멀어 0.6초마다 팔이 순간이동한다 (루프#140 1차 시도가 82.85cm 였다).
segs, start = [], None
for f, step in enumerate(steps):
    if step >= 1.0:
        start = f if start is None else start
    elif start is not None:
        if f - start >= 5:
            segs.append((max(start - 1, 0), min(f + 1, frames)))
        start = None
for a, b in segs:
    unreal.log("[공격] 토막 {}~{} · {}프레임 {:.3f}초 · 최고 {:.1f}cm/프레임 · 이음매 {:.2f}cm".format(
        a, b, b - a + 1, (b - a + 1) / float(frames) * length, max(steps[a:b]), (track[b] - track[a]).length()))
unreal.log("공격 클립 검사 끝")
