"""M5-01b2·M5-01e2 플레이어 클립 6개 임포트 — 바르코(ìì²´ ì ì) glb → `A_Player_*`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_player_animations.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=~/UNREAL/Snarehaven/logs/<이름>.log

원본은 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
출처는 사용자 본인 제작 몸 + Mixamo 리타겟 모션 — `docs/ASSET_LICENSES.md` 표(2026-09-21 행,
상업 이용 조건은 감독 확인 대기).

클립 매핑은 감독 지시(INBOX 2026-09-21 13:48) 그대로다.

`-abslog` 로 돌린다 — 이 맥의 stdout 은 Display 이상만 받아서 `unreal.log()`(Log 단계) 줄이
통째로 사라진다(루프#83). 로그 파일은 전 단계를 받으므로 거기서 읽으면 된다.

**옛 Quaternius 스크립트에 있던 glb 수술 코드는 지웠다** — 그건 한 파일(`UAL1_Standard.glb`)에
클립이 43개 들어 있어서 하나만 남긴 사본을 만들어야 했기 때문이다. 바르코 원본은 **파일 하나에
클립 하나**(이름 없음 · 채널 20~21 · 본 23)라 파일을 그대로 태우면 된다.

**glb 마다 메시·머티리얼·텍스처가 같이 들어 있다**(메시 1 · 머티리얼 1 · 이미지 3 — M5-01a2 가
그중 Idle 파일에서 몸을 받았다). `import_only_animations` 와 스켈레톤 지정, 메시·머티리얼 끄기가
없으면 임포트마다 몸이 또 하나씩 딸려 들어온다.
"""
import json
import math
import os
import struct
import tempfile

import unreal

# 작업트리에서 돌릴 땐 `SNARE_PLAYER_RAW_DIR` 로 그 트리의 raw 폴더를 가리킨다(없는 원본은 이미 있는 에셋이면 안 읽는다).
SOURCE_DIR = os.environ.get("SNARE_PLAYER_RAW_DIR", "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player")
DEST_DIR = "/Game/Game/Characters/Player/Snare"
SKELETON_PATH = DEST_DIR + "/SK_Player_Body_Skeleton"

# (원본 파일, 우리 에셋 이름). Attack 은 **감독이 직접 만든 `club_swing2.glb`** 다
# (DESIGN 8.8 "공격만 감독이 Blender 직접 제작", 감독 지시 2026-09-23 14:05) —
# 몽둥이 한 바퀴 0.6초(30fps 18프레임)라 공격 간격과 길이가 같다. 이전 `landing_smash.glb` 는
# Mixamo 4연타 콤보 145프레임이라 `trim_player_attack_clip.py` 로 잘라 쓰던 것이다.
CLIPS = (
    ("idle_v3.glb", "A_Player_Idle"),  # 루프#402 v3(INBOX 12:25 팔): 바탕 팔 앞 들림 29° → 4.6°·팔꿈치 21 → 7.3°, 몸통 옆에 축 늘어짐 (`idle_v2_make.py` arm_back·elbow). 옛 루프#354 v2(레퍼런스 관찰표 「거의 가만히, 숨쉬기만」): 한 숨 3.0초, 오르내림 0.4cm·가슴 1.2°·어깨 1.5°, 머리 수평·발 고정 (`idle_v2_make.py`). 옛: standing_idle_1.glb 8.9초 — 머리 두리번 8.8°·손 1.4cm (바탕 자세로는 계속 쓴다)
    # 루프#404(INBOX 결함 12:40 괄호 「대기 변주 2개」, 레퍼런스 `idleVariation1·2`): 첫 장·끝 장 = 대기 v3 첫 장, 한 번만 튼다(`idle_var_make.py`).
    ("idle_look.glb", "A_Player_IdleLook"),        # 105장 3.5초 — 고개 왼쪽 35° → 오른쪽 35° → 가운데, 머리 젖힘 유지
    ("idle_stretch.glb", "A_Player_IdleStretch"),  # 90장 3.0초 — 두 팔 머리 옆으로 기지개, 가슴 8°·머리 10° 뒤로
    ("walk_cycle_v5.glb", "A_Player_Walk"),  # 루프#1308 v5(INBOX 보정 10-19): 클립 속도 32.3 → 약 57(걷기 최고 100cm/s), 보폭 22 → 39cm, 주기·팔·발 그대로. 옛 v4: 루프#402 v4(INBOX 12:25 팔): 팔꿈치 최대 35 → 11°(위팔 선 기준 elbow_aim)·팔 앞 기울기 최대 13.6°·흔들기 폭 24° 그대로 주기·보폭·발. 옛 루프#352 v3(레퍼런스 관찰표 `docs/reference/character_motion.md`): 종종걸음 주기 0.667초·팔 ±12°·앞숙임 8°·튐 1.8cm, 디딘 발 미끄러짐 ≤0.01cm. 옛 감독 09-25 v2: 발 굴림·골반 튐/기울임·팔 ±36°, 디딘 발 미끄러짐 ≤0.09cm. 옛: 감독 2026-09-23: 뼈 23개 이음매 0° 한 걸음 주기 (`loop_cycle_make.py`)
    ("run_cycle_v4.glb", "A_Player_Run"),  # 루프#402 v4(INBOX 12:25 팔 「달리기 45° 이하」): 팔꿈치 최대 81 → 36.5°, 주기·보폭·속도 114.6 그대로. 옛 루프#353 v3(걷기 v3 와 같은 결): 주기 0.533 → 0.4초·팔 ±38 → ±24°·보폭 61 → 47cm, 게임식 속도 114.6 그대로. 옛 v2: 공중 구간 30%·팔꿈치 65~80°, 앞이 대기와 같아 요 보정 없음. 옛: 같은 날 — 원본 run.glb 는 끝→처음 67° 튐 = "5걸음마다 멈칫"
    ("club_swing3.glb", "A_Player_Attack"),  # 감독 09-25 v3: 머리 위로 치켜들어 앞 바닥으로 내리찍기, 발 IK 고정·타격 f6=0.2초 그대로 (`club_swing3_make.py`). 옛 v2 는 어깨 높이 수평 찌르기로 보였다
    ("chop.glb", "A_Player_Chop"),  # 감독 09-25: 손도끼 나무 베기 — 허리 높이 수평 휘두르기 18프레임 0.6초, 타격 f6 (`chop_make.py`). 곡괭이는 공격(내리찍기) 클립
    # 아래 둘은 감독이 Mixamo 에서 받아 대기와 **같은 23뼈**로 옮긴 것(`mixamo_retarget.py`, 지시 2026-09-23 16:10).
    # 앞 방향이 대기와 같아 `CLIP_YAW_FIX_DEGREES` 에 넣지 않는다(달리기·옛 공격 원본과 다른 점).
    ("hit.glb", "A_Player_Hit"),      # 30fps 14프레임 0.47초 — 첫 장 = 끝 장 ≈ 대기 자세
    ("death.glb", "A_Player_Death"),  # 30fps 92프레임 3.07초 — 뒤로 누워 끝(되풀이 없이 한 번만 튼다)
    # 감독 2026-09-24: 물·배·낚시 6개도 같은 Mixamo → 23뼈 리타겟, 앞 방향이 대기와 같다. 되풀이는 코드가 건다.
    ("swim_idle_v2.glb", "A_Player_SwimIdle"),  # v2: 2초 61장, 가슴 앞 팔 젓기·다리 자전거, 어깨가 수면(SinkCm 50 그대로). 옛: Treading Water 91프레임 3.0초 — 제자리 헤엄(머리·어깨가 물 위)
    ("swim_move_v2.glb", "A_Player_SwimMove"),  # v2: 1.33초 개헤엄 — 머리 들고 팔이 번갈아 물 위로 뻗음. 옛: Swimming 137프레임 4.5초 — 제자리, 몸이 수평
    ("boat_sit.glb", "A_Player_BoatSit"),     # Sitting Idle(바닥) 327프레임 10.9초 — 무릎 세우고 바닥에 앉음
    ("weapons/fish_wait_r.glb", "A_Player_FishIdle"),   # 2026-10-05 오른손(Socket_Weapon)으로 다시 만듦(레퍼런스 UlFyyrxMNt8 1:50). 루프#388(레퍼런스 관찰표 21행 · 감독 15:30 ③): 90장 3.0초 되풀이 — 대기 v2 숨 + 오른손 허리 높이 몸 앞, 대 끝이 물(앞) 쪽 수평 위 약 11° (`weapons/fish_pose_make.py wait`). 옛: Mixamo Fishing Idle 107프레임 — 대 수평 위 57.5° 라 위에서 짧아 보였다
    ("weapons/fish_cast_r.glb", "A_Player_FishCast"),  # 2026-10-05 오른손으로 다시 만듦. 루프#389(레퍼런스 관찰표 20행 · 감독 15:30 ③·⑧): 14장 0.467초 한 번 — 바탕 Quaternius UAL2 `OverhandThrow`(CC0)를 거울 없이 오른손 그대로 리타깃(`ual2_retarget.py`, rt_overhand_throw_r.glb), 뒤로 젖힘 빼고 손 듦 0.1초 → 0.2초에 대 앞 아래 → 끝 장 = FishIdle 첫 장 (`weapons/fish_cast_make.py`). 옛: Mixamo Fishing Cast And Reel In 0~78 프레임 2.6초
    ("weapons/fish_pull_r.glb", "A_Player_FishReel"),   # 2026-10-05 오른손으로 다시 만듦. 루프#388(관찰표 22행): 20장 0.667초 되풀이 — 상체를 오른쪽 뒤로 돌려 대를 오른어깨 너머로 세워(수평 위 64~70°, 끝이 뒤) 작게 당겼다 놓기 (`weapons/fish_pose_make.py pull`). 옛: Mixamo 감기 195~247 프레임 1.73초
    ("ride.glb", "A_Player_Ride"),            # M6-06 탈것 탄 자세 30프레임 1초(`ride_make.py`) — 등을 끼고 앉음, 원점 = 엉덩이 밑면
    ("eat.glb", "A_Player_Eat"),              # 감독 09-25 아이템 사용: 48프레임 1.6초 한 번 — 두 손 입 앞·세 번 베어 묾(`eat_make.py`)
    ("gather_v2.glb", "A_Player_Gather"),     # 루프#405(레퍼런스 관찰표 25행 · 감독 15:30 ④·⑧): 18장 0.6초 한 번 — 서서(골반 높이 변화 0) 오른팔만 몸 앞 오른쪽 아래로 뻗었다 돌아옴, 바탕 Quaternius UAL1 `PickUp_Table`(CC0)를 오른손으로 거울 리타깃(`ual2_retarget.py`)한 앞으로 뻗기 박자 (`gather_v2_make.py`). 옛: 30프레임 1.0초 쪼그려 오른손 바닥→가슴(`gather_make.py`)
    ("wave.glb", "A_Player_Wave"),            # 감독 09-25 이모트: 45프레임 1.5초 한 번 — 오른손 머리 옆 세 번 흔들기(`emote_make.py wave`)
    ("cheer.glb", "A_Player_Cheer"),          # 36프레임 1.2초 한 번 — 웅크렸다 두 팔 V 로 튀어 오름
    ("dance.glb", "A_Player_Dance"),          # 40프레임 되풀이 — 골반 좌우·두 손 번갈아(첫 장 ≠ 대기, 되풀이 전용)
    # M18-11 무기·도구 전용 몸 동작 — `mixamo/weapons/*.fbx` 를 `mixamo_retarget.py` 로 옮긴 뒤 `weapons/make_all.sh` 가 만든다.
    # 든 자세(Hold*)는 원본 한 바퀴가 이미 이음매 0~0.1° 라 `loop_cycle_make.py` 로 전 구간을 그대로 되풀이한다.
    # 쓰기 클립은 `weapons/trim_inplace.py` 로 앞뒤를 잘라 **타격 장이 길이의 1/3** 에 오게 했다 —
    # 휘두르기는 공격 간격(0.6초)에 길이를 맞춰 트므로 타격이 늘 0.2초(AttackHitDelaySeconds)에 맞는다(DESIGN 3.1).
    ("weapons/hold_fists.glb", "A_Player_HoldFists"),        # Fighting Idle 100장 3.3초 되풀이 — 맨손 주먹 자세
    ("weapons/jab.glb", "A_Player_Jab"),                     # Lead Jab f5~35 31장 1.0초, 왼손 가장 뻗은 원본 f15 = 새 f10
    ("weapons/cross.glb", "A_Player_Cross"),                 # Cross Punch f22~55 34장 1.1초, 오른손 가장 뻗은 원본 f33 = 새 f11
    ("weapons/hold_one_hand_v2.glb", "A_Player_HoldOneHand"),   # 한 손 도구(손도끼·마체테·기본 곡괭이·몽둥이): 90장 3.0초 되풀이 — 대기 v2 숨 몸(골반 앞) + 오른손 오른쪽 엉덩이 옆, 손 회전은 옛 Sword And Shield Idle 첫 장의 골반 기준 손 회전(`tools/blender/hold_one_hand_v2_make.py`, 감독 PIE 10-05: 옛 자세는 골반을 25~54° 튼 싸움 자세라 걸을 때 오른팔이 몸 앞으로 수평으로 뻗었다)
    ("weapons/swing_horizontal.glb", "A_Player_SwingHorizontal"),  # Standing Melee Attack Horizontal f10~64 55장, 손 가장 앞 f28 = 새 f18
    ("weapons/swing_downward.glb", "A_Player_SwingDownward"),      # Standing Melee Attack Downward f8~62 55장, 내려찍는 f26 = 새 f18
    ("weapons/mine_two_hand.glb", "A_Player_MineTwoHand"),   # 위 내려치기를 두 팔 IK 로 한 자루 쥐게 구움(`weapons/two_hand_make.py`)
    ("weapons/slash_v3.glb", "A_Player_Slash"),              # 마체테만: 18장 0.6초 — 든 자세(HoldOneHand f0) → f2 몸 오른쪽 뒤 백스윙 → f6 허리 높이 수평으로 몸 앞 베기 → f9 앞 왼쪽 빠짐 → f13~18 든 자세(`tools/blender/slash_v2_make.py`, 감독 10-05 · 레퍼런스 마체테는 덤불을 옆으로 벤다). 옛 Mixamo Slash 37장은 날이 땅 아래 −133cm 까지 박혀 버림. 타격 f6 = 1/3
    ("weapons/hold_heavy_pickaxe.glb", "A_Player_HoldHeavyPickaxe"),   # 고급·전기 곡괭이만(루프#1354): 90장 3.0초 되풀이 — HoldOneHand 와 같은 몸·손목, 도구 숙임만 15°(`tools/blender/hold_one_hand_v2_make.py` SNARE_HOLD_HEAVY=1). 머리 폭 87.5cm 전기 곡괭이가 30° 면 땅에 박힘
    ("weapons/hold_two_hand.glb", "A_Player_HoldTwoHand"),   # 2hand Idle 144장 4.8초 되풀이 — 옛 두 손 도구 자세(무릎·허리 굽힘). 루프#1354 부터 쓰는 행 없음, 전기톱 시험 역검증만 읽는다
    ("weapons/hold_rod_r.glb", "A_Player_HoldRod"),            # 낚싯대 4종: 90장 3.0초 되풀이 — 대기 v2 숨 + 오른손(Socket_Weapon) 오른쪽 엉덩이 옆(2026-10-05 레퍼런스 UlFyyrxMNt8 1:50), 대를 거의 세워 뒤쪽 위로 수평 위 약 65°(`weapons/fish_pose_make.py hold`, 레퍼런스 관찰표 19행 · 루프#390)
    ("weapons/hold_saw.glb", "A_Player_HoldSaw"),            # 전기톱만: 90장 3.0초 되풀이 — 대기 v2 숨쉬기 몸 + 두 손 가슴 앞, 톱 긴 축 위에서 앞으로 20°(`weapons/hold_saw_make.py`, 레퍼런스 관찰표)
    ("weapons/saw_thrust_v2.glb", "A_Player_SawThrust"),     # 전기톱만: 45장 1.5초 한 번(행 bUseAnimFixedLength, 재생 속도 1) — 세로(HoldSaw f0) → f6(0.2초)에 수평·허리 앞으로 내밀기 → f36(1.2초)까지 떨며 버팀 → f45 세로로 돌아옴(`weapons/saw_thrust_make.py`, 레퍼런스 관찰표 · 감독 15:30 ⓪). 타격 f6
    ("weapons/mine_one_hand_v3.glb", "A_Player_MineOneHand"),   # 기본 곡괭이만: 18장 0.6초 — 든 자세 → f2 몸 옆 백스윙 → f6 몸 앞 오른쪽 아래 내려치기(한 손 휘두르기 0.3초) → f13~18 든 자세(`weapons/mine_one_hand_make.py`, 레퍼런스 관찰표 · 감독 15:30 ①). 타격 f6 = 1/3
    ("weapons/mine_grind_v3.glb", "A_Player_MineGrind"),    # 고급·전기 곡괭이만: 45장 1.5초 한 번(행 bUseAnimFixedLength, 재생 속도 1) — v3(루프#1354 `tools/blender/mine_grind_v3_make.py`)는 섞는 든 자세만 HoldHeavyPickaxe f0 으로 바꿈 · 든 자세(HoldHeavyPickaxe f0) → f6(0.2초)에 두 손으로 자루를 몸 앞에 눕혀 머리 아래 날을 허리 앞 아래 광맥에 대기 → f36(1.2초)까지 떨며 버팀 → f45 든 자세(`weapons/mine_grind_make.py`, 레퍼런스 관찰표 · 감독 15:30 ②·⓪). 타격 f6
    ("weapons/binoculars_look.glb", "A_Player_LookBinoculars"),  # 망원경만: 45장 1.5초 한 번(행 bUseAnimFixedLength) — 대기 → f9(0.3초)까지 두 손목을 턱 앞(목 위·몸 앞)으로 올리고 고개를 듦 → f36 버팀 → f45 대기(`tools/blender/binoculars_look_make.py`, 감독 10-05)
    ("weapons/hold_pistol.glb", "A_Player_HoldPistol"),      # 권총만(M39-06b): 90장 3.0초 되풀이 — 대기 v2 숨 몸 + 오른손목 가슴 앞 가운데로 내밀고 왼손이 아래에서 받침, 총구 = 몸 앞(`tools/blender/pistol_aim_make.py hold`, 레퍼런스 steam_01 「총을 가슴 높이에서 몸 앞으로」)
    ("weapons/pistol_recoil.glb", "A_Player_PistolRecoil"),  # 권총만: 12장 0.4초 = 쏘기 간격(재생 속도 1) — f2 총구 위 14°·손목 뒤 2.5cm 위 2cm → f4.5(0.15초) 든 자세 → 버팀(`pistol_aim_make.py recoil`, DESIGN 8.6-c 「반동 클립 0.15초」)
    ("weapons/pistol_reload.glb", "A_Player_PistolReload"),  # 권총만(M39-06b2): 36장 1.2초 한 번 = 행 ReloadSeconds(재생 속도 1) — f6 오른손 가슴 앞 가운데·총구 위 35° → 왼손 f9 허리 탄창 → f15 손잡이 밑 밀어 넣기 → f20 슬라이드 당김 → f30 든 자세(`pistol_aim_make.py reload`)
    ("weapons/rifle_recoil.glb", "A_Player_RifleRecoil"),    # 현대 소총만(M39-08c): 36장 1.2초 = 쏘기 간격(재생 속도 1) — f2.5 총구 위 18°·뒤 4cm → f7.5(0.25초) 든 자세 → 버팀. 든 자세는 `A_Player_HoldPistol` 같이 씀(`pistol_aim_make.py rifle_recoil`, DESIGN 8.6-c 소총 줄 「반동 0.25초」)
    ("weapons/rifle_reload.glb", "A_Player_RifleReload"),    # 현대 소총만: 60장 2.0초 한 번 = 행 ReloadSeconds — 권총 재장전을 60/36 로 늘리고 f25 왼손이 소총 탄창(가슴 가운데)에 닿음(`pistol_aim_make.py rifle_reload`)
    ("weapons/bazooka_hold.glb", "A_Player_HoldBazooka"),    # 바주카만(#1211): 90장 3.0초 되풀이 — 대기 v2 숨 몸 + 오른 어깨 위에 발사관을 얹고 포구가 앞·4° 위(`pistol_aim_make.py baz_hold`, INBOX 보정 「어깨에 얹고 포구가 앞」)
    ("weapons/bazooka_recoil.glb", "A_Player_BazookaRecoil"),  # 바주카만: 30장 1.0초 = 쏘기 간격(재생 속도 1) — f3 포구 위 10°·뒤 3cm(0.03) → f15(0.5초) 든 자세(`pistol_aim_make.py baz_recoil`)
    ("weapons/bazooka_reload.glb", "A_Player_BazookaReload"),  # 바주카만: 90장 3.0초 한 번 = 행 ReloadSeconds — 포구 앞아래 40°로 내림 → 왼손 가슴 앞 장전 → 다시 어깨에 얹음(`pistol_aim_make.py baz_reload`)
)

# **`run.glb` 은 몸이 90도 돌아간 채로 만들어져 있다** (루프#85 가 발끝 좌표로 확인 — 옛 공격 원본
# `landing_smash.glb` 도 같았다. 새 `club_swing2.glb` 는 앞이 대기와 같아 보정이 없다, 감독 14:05).
# 서 있기·걷기는 좌우가 x 축이고 앞뒤가 y 축인데(`LeftToeBase` x≈+9 고정 · y 가 -16~15 로 흔들림),
# 달리기·공격만 좌우가 y 축이다(달리기 왼발-오른발 평균 (x +1.5, y +18.4) · 공격 (x -9.5, y +22.6)).
# glb 의 **레스트 포즈는 네 파일이 모두 같으므로**(`Root`·`Hips` 회전 전부 단위) 원본 애니메이션
# 자체가 돌아간 것이고, 실제로 `Hips` 회전 키의 요(yaw)가 이 둘에서만 90도 근처다.
# 안 고치면 달리기·공격으로 갈리는 순간 캐릭터가 옆을 보고 달리고 옆을 때린다.
#
# **엔진 쪽으로는 못 고친다** (루프#85 가 둘 다 해 보고 실패):
#   · 임포트 뒤 `AnimDataController.set_bone_track_keys` — UE 5.8 의 `AnimationSequencerDataModel`
#     에서는 True 만 돌려주고 아무것도 안 바꾸는 껍데기다(`get_bone_track_by_name` 도 빈 구조체).
#   · 임포트 중 `InterchangeGenericAssetsPipeline.import_offset_rotation` — 애니메이션 전용
#     임포트(`import_only_animations`)에는 안 걸린다. 다시 임포트해도 요가 그대로 +90.3 이었다.
# 그래서 **원본 glb 를 고친 사본**을 만들어 그걸 태운다. `_Source/raw/` 원본은 건드리지 않는다.
# 90 은 게임 규칙 숫자가 아니라 **축 보정**이다 — `ASnarehavenCharacter` 가 메시에 거는 -90 과 같은 종류.
CLIP_YAW_FIX_DEGREES = {}   # run_cycle_v2 는 앞이 대기와 같다(09-25). 옛 run.glb 는 90

HIPS_NODE_NAME = "Hips"   # 몸 전체가 매달린 뼈 — 여기 회전이 곧 몸이 보는 방향이다


def _yawed_glb(source: str, degrees: float) -> str:
    """`Hips` 회전 키를 위 축으로 돌린 glb 사본을 임시 파일로 써서 그 경로를 준다.

    glTF 는 Y 가 위다. `Hips` 의 부모(`Root`)가 회전 없이 고정이고 `Hips` 에는 이동 키가 없어서
    회전 키만 돌리면 몸 전체가 그대로 돈다. 바이트 길이가 안 변하게 제자리에 덮어써서
    (float32 쿼터니언 xyzw 를 같은 자리에) glb 헤더·오프셋을 다시 계산할 일이 없다.
    """
    blob = bytearray(open(source, "rb").read())
    chunks, off = [], 12
    while off < len(blob):
        length, kind = struct.unpack_from("<I4s", blob, off)
        chunks.append((kind, off + 8, length))
        off += 8 + length
    gltf = json.loads(bytes(blob[chunks[0][1]:chunks[0][1] + chunks[0][2]]).decode("utf-8").rstrip("\x00 "))
    bin_start = next(start for kind, start, _ in chunks if kind == b"BIN\x00")

    hips = [i for i, node in enumerate(gltf["nodes"]) if node.get("name") == HIPS_NODE_NAME]
    half = math.radians(degrees) / 2.0
    qy = (0.0, math.sin(half), 0.0, math.cos(half))   # 위(Y) 축 회전, xyzw

    turned = 0
    for animation in gltf.get("animations", []):
        for channel in animation["channels"]:
            if channel["target"]["path"] != "rotation" or channel["target"]["node"] not in hips:
                continue
            accessor = gltf["accessors"][animation["samplers"][channel["sampler"]]["output"]]
            view = gltf["bufferViews"][accessor["bufferView"]]
            base = bin_start + view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            for key in range(accessor["count"]):
                at = base + key * 16
                x, y, z, w = struct.unpack_from("<4f", blob, at)
                # qy * q (부모 공간에서 먼저 도는 쪽)
                struct.pack_into(
                    "<4f", blob, at,
                    qy[3] * x + qy[0] * w + qy[1] * z - qy[2] * y,
                    qy[3] * y - qy[0] * z + qy[1] * w + qy[2] * x,
                    qy[3] * z + qy[0] * y - qy[1] * x + qy[2] * w,
                    qy[3] * w - qy[0] * x - qy[1] * y - qy[2] * z)
                turned += 1

    handle, path = tempfile.mkstemp(suffix="_" + os.path.basename(source))
    with os.fdopen(handle, "wb") as out:
        out.write(blob)
    unreal.log("앞 방향 보정 사본: {} `{}` 회전 키 {}개를 {:+.0f}도 → {}".format(
        os.path.basename(source), HIPS_NODE_NAME, turned, degrees, path))
    return path


# 엔진 기본 glTF 파이프라인 스택은 둘이다(루프#63 이 몸을 넣을 때 알아낸 것 — `BaseEngine.ini`).
# 애니메이션만 받을 때도 뒤의 glTF 전용 파이프라인을 같이 태운다.
GLTF_PIPELINE = "/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"


def _pipelines(skeleton, asset_name: str) -> list:
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.set_editor_property("asset_type_sub_folders", False)   # `Animations/` 하위 폴더 없이
    pipeline.set_editor_property("scene_name_sub_folder", False)    # `walk/` 없이
    pipeline.set_editor_property("use_source_name_for_asset", True)
    pipeline.set_editor_property("asset_name", asset_name)          # 원본 클립은 이름이 비어 있다

    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    # 스켈레톤을 지정하고 `import_only_animations` 를 켜야 glb 안의 몸이 안 따라온다.
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("import_only_animations", True)

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_static_meshes", False)
    mesh.set_editor_property("import_skeletal_meshes", False)
    pipeline.get_editor_property("material_pipeline").set_editor_property("import_materials", False)

    result = [pipeline]
    try:
        result.append(unreal.load_asset(GLTF_PIPELINE))             # 플러그인 콘텐츠는 `unreal.load_asset` 으로만 열린다
    except Exception as err:
        unreal.log_error("glTF 전용 파이프라인을 못 찾았다: {} ({})".format(GLTF_PIPELINE, err))
    return result


def _recorded_source(asset) -> str:
    """에셋에 적힌 임포트 원본 파일 이름. 못 읽으면 빈 문자열."""
    try:
        import_data = asset.get_editor_property("asset_import_data")
        return os.path.basename(import_data.get_first_filename()) if import_data else ""
    except Exception as err:      # 엔진 판이 바뀌어 칸 이름이 달라질 때를 대비해서만
        unreal.log_warning("원본 이름을 못 읽었다: {} ({})".format(asset.get_name(), err))
        return ""


def _import_clip(source_name: str, asset_name: str, skeleton) -> None:
    asset_path = "{}/{}".format(DEST_DIR, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        # **이름이 아니라 에셋에 적힌 원본으로 판정한다**(PROMPT 3-12, 루프#160 의 재반입 교훈).
        # 감독이 클립을 갈아 끼우면(표의 원본 이름이 바뀌면) 저절로 다시 받고,
        # 같은 원본이면 손대지 않는다 — 걷기·달리기는 임포트 뒤 한 바퀴로 다듬은 에셋이라
        # (`trim_player_anim_loops.py`, 루프#139) 까닭 없이 다시 받으면 그 다듬기가 날아간다.
        recorded = _recorded_source(unreal.EditorAssetLibrary.load_asset(asset_path))
        if recorded and not recorded.endswith(os.path.basename(source_name)):   # 에셋엔 파일 이름만 적힌다(`weapons/` 접두 없음)
            unreal.log("원본이 바뀌었다: {} -> {} — 지우고 다시 받는다".format(recorded, source_name))
            unreal.EditorAssetLibrary.delete_asset(asset_path)
        else:
            unreal.log("이미 있음({} 그대로): {}".format(recorded or "원본 이름 못 읽음", asset_path))
            return

    source = os.path.join(SOURCE_DIR, source_name)
    if not os.path.isfile(source):
        unreal.log_error("원본이 없다: {}".format(source))
        return

    yaw = CLIP_YAW_FIX_DEGREES.get(asset_name)
    scratch = _yawed_glb(source, yaw) if yaw else None

    override = unreal.InterchangePipelineStackOverride()
    for pipeline in _pipelines(skeleton, asset_name):
        override.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.filename = scratch or source
    task.destination_path = DEST_DIR
    task.destination_name = asset_name
    task.automated = True          # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    for path in task.get_editor_property("imported_object_paths"):
        unreal.log("임포트됨: {} ← {}".format(path, source_name))
    if scratch:
        os.remove(scratch)


skeleton_asset = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
if skeleton_asset is None:
    unreal.log_error("스켈레톤이 없다 — 몸부터 임포트해야 한다(M5-01a2): {}".format(SKELETON_PATH))
else:
    for source_name, name in CLIPS:
        _import_clip(source_name, name, skeleton_asset)

    # `only_if_is_dirty=False` 로 폴더를 통째로 다시 저장하면 이번에 손대지도 않은 에셋
    # (M5-01a2 의 메시·머티리얼·텍스처 7개)까지 바이트가 달라져 커밋에 딸려 온다 — 더러워진 것만 저장한다.
    unreal.EditorAssetLibrary.save_directory(DEST_DIR, only_if_is_dirty=True, recursive=True)
    for _source, name in CLIPS:
        anim = unreal.EditorAssetLibrary.load_asset("{}/{}".format(DEST_DIR, name))
        if anim is None:
            unreal.log_error("만들어지지 않았다: {}/{}".format(DEST_DIR, name))
            continue
        # 본 트랙 수까지 센다 — 껍데기(길이만 있고 포즈가 없는) 시퀀스를 통과라고 쓰지 않으려고.
        tracks = unreal.AnimationLibrary.get_animation_track_names(anim)
        unreal.log("최종: {}  [{}]  길이={:.2f}초  프레임={}  본트랙={}  스켈레톤={}".format(
            name, anim.get_class().get_name(),
            anim.get_editor_property("sequence_length"),
            anim.get_editor_property("number_of_sampled_frames"),
            len(tracks),
            anim.get_editor_property("skeleton").get_name()))
