"""걷기·달리기 클립이 **실제로 밟는 속도**를 클립에서 잰다 (DESIGN 3.3 · 감독 결정 2026-09-21).

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/measure_player_anim_speed.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=~/UNREAL/Snarehaven/logs/measure_player_anim_speed.log

DESIGN 3.3 은 "루트 이동 거리 ÷ 재생 시간" 이라고 적었지만 **이 클립들에는 루트 모션이 없다**
(`enable_root_motion=False` — 제자리 클립이다). 재는 자리를 발로 옮긴다:

  **바닥에 붙어 있는 발끝(`LeftToeBase`/`RightToeBase`)이 몸 기준으로 뒤로 흐르는 속도 = 그 클립의 이동 속도.**

발이 안 미끄러지려면 캐릭터가 딱 그 속도로 나아가야 한다. 다만 **이 값은 클립 공간 속도**라,
몸을 캡슐에 맞춰 키운 배율을 곱해야 화면 속도가 된다 — 곱하는 것은 C++ 쪽
(`ASnarehavenCharacter::ApplyPlayerBody` 가 잰 `BodyMeshScale`)이다.
숫자를 지어낸 것이 아니라 **이미 있는 클립에서 읽은 값**이라는 점은 원래 결정과 같다.

축: **재서 고른다** — 양 발끝이 가장 크게 움직인 축(x·y)이 진행 방향이다. 리그를 갈아타면 앞 축도
바뀌므로 박아 두지 않는다(루프#85 — 바르코는 Mixamo 짜임).

디딘 발 판정도 **높이가 아니라 움직임으로 한다.** 발 높이에 여유값을 두고 자르면 리그마다 여유를
다시 맞춰야 하고, 바르코에서는 디딘 프레임이 49개 중 2~11개밖에 안 잡혀 두 발 값이 267 대 90 으로
갈렸다(루프#85). 대신 **매 프레임 두 발 중 진행축 뒤로 더 빠르게 흐르는 쪽이 디딘 발**이다 —
디딘 발은 몸 기준으로 뒤로 흐르고, 뜬 발은 앞으로 넘어간다. 여유값이 없어 리그를 갈아타도 그대로 쓴다.

**프레임별 속도의 중앙값을 쓴다.** 디딘 구간 끝에는 앞꿈치로 구르며 느려지는 프레임이, 달리기에는
두 발이 다 뜨는 프레임이 섞인다. 중앙값은 그 꼬리를 무시하고 클립이 일정하게 밟는 속도를 집는다
(루프#77: 양 끝 차이로 재면 97.9 대신 90.7 이 나왔다).
"""
import unreal

ANIM_LIB = unreal.AnimationLibrary
MATH_LIB = unreal.MathLibrary

PLAYER_FEET = ["LeftToeBase", "RightToeBase"]
# 늑대(탈것 몸 자리표시, DESIGN 8.3)는 발이 넷 — 같은 규칙("뒤로 가장 빨리 흐르는 발 = 디딘 발")이 그대로 든다.
WOLF_FEET = ["foot_front_l", "foot_front_r", "foot_back_l", "foot_back_r"]
CLIPS = [
    ("걷기", "/Game/Game/Characters/Player/Snare/A_Player_Walk", PLAYER_FEET),
    ("달리기", "/Game/Game/Characters/Player/Snare/A_Player_Run", PLAYER_FEET),
    ("늑대 걷기", "/Game/Game/Characters/Animals/Wolf/A_Animal_Wolf_Walk", WOLF_FEET),
    ("늑대 달리기", "/Game/Game/Characters/Animals/Wolf/A_Animal_Wolf_Run", WOLF_FEET),
]


def component_position(anim, chain, frame):
    """로컬 포즈를 뿌리까지 합성해 컴포넌트 공간 위치를 만든다 (본 로컬 이동은 프레임마다 안 바뀐다)."""
    pos = ANIM_LIB.get_bone_pose_for_frame(anim, chain[0], frame, False).translation
    for bone in chain[1:]:
        pos = MATH_LIB.transform_location(ANIM_LIB.get_bone_pose_for_frame(anim, bone, frame, False), pos)
    return pos


def median(values):
    """가운데 값. 짝수면 가운데 둘의 평균 — 느려지는 꼬리 프레임에 끌려가지 않게 평균 대신 쓴다."""
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2 == 1:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def measure(anim, foot_bones):
    frame_count = ANIM_LIB.get_num_frames(anim)
    seconds_per_frame = ANIM_LIB.get_sequence_length(anim) / float(frame_count)
    tracks = {}
    for bone in foot_bones:
        chain = [str(b) for b in ANIM_LIB.find_bone_path_to_root(anim, bone)]
        tracks[bone] = [component_position(anim, chain, f) for f in range(frame_count)]

    # 진행축: 양 발이 더 크게 흔들린 쪽. 바르코는 +Y 를 보고 서 있어서 y 가 나온다.
    axis = max("xy", key=lambda a: max(max(getattr(p, a) for p in ps) - min(getattr(p, a) for p in ps)
                                       for ps in tracks.values()))

    # 매 프레임, 뒤로 가장 빨리 흐르는 발이 디딘 발이다. 마지막 프레임은 루프라 첫 프레임으로 잇는다.
    speeds = []
    for f in range(frame_count):
        drift = max((getattr(ps[f], axis) - getattr(ps[(f + 1) % frame_count], axis)) / seconds_per_frame
                    for ps in tracks.values())
        if drift > 0.0:          # 뒤로 흐르는 발이 하나도 없는 프레임 = 두 발 다 떠 있다
            speeds.append(drift)
    if len(speeds) < 2:
        unreal.log_warning("  디딘 프레임이 없다 (%d개)" % len(speeds))
        return None
    speed = median(speeds)
    unreal.log_warning("  진행축 %s · 디딘 프레임 %d/%d · 속도 %.1f~%.1f cm/s → 중앙값 %.1f cm/s"
               % (axis, len(speeds), frame_count, min(speeds), max(speeds), speed))
    return speed


def main():
    unreal.log("클립 속도 측정 시작 (발끝이 바닥에 붙어 뒤로 흐르는 속도)")
    results = []
    for label, path, foot_bones in CLIPS:
        anim = unreal.EditorAssetLibrary.load_asset(path)
        if anim is None:
            unreal.log_error("클립 없음: %s" % path)
            continue
        unreal.log_warning("%s %s — 길이 %.4f초 · %d프레임 · 루트모션 %s"
                   % (label, path, ANIM_LIB.get_sequence_length(anim),
                      ANIM_LIB.get_num_frames(anim), anim.get_editor_property("enable_root_motion")))
        speed = measure(anim, foot_bones)
        if speed is not None:
            results.append((label, speed))
    for label, speed in results:
        # 5.8 커맨드릿은 `Log` 등급을 파일에 안 적는다(`import_animal_species.py` 머리말) — 결과 줄은 Warning 으로.
        unreal.log_warning("측정 결과: %s 기준 속도 = %.1f cm/s" % (label, speed))


main()
