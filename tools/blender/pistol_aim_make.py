# 권총 두 손 겨눔 들기·반동 (M39-06b · 명세 docs/specs/hunting-gun.md 4절 · DESIGN 8.6-c 「반동 클립 0.15초」):
#   레퍼런스 `레퍼런스 시트 01` — 총 든 사람은 총을 **가슴 높이에서 몸 앞(보는 쪽)으로 내민다**.
#   옛 상태(루프#577)는 든 자세가 없어 한 손 늘어뜨림이라 게임 카메라(뒤 위)에서 권총이 몸·모자에 가렸다.
#   hold  = 90장 3.0초 되풀이 — 대기 v2 숨 몸 + 오른팔을 어깨 높이로 오른쪽 앞에 뻗어 한 손으로 쥔다(왼손은 옆에 내림, 루프#580).
#   recoil = 12장 0.4초 한 번(쏘기 간격 0.4초 = 재생 속도 1) — f0 든 자세 → f2 총구가 위로 튀고 손목이 뒤·위로 → f4.5(0.15초) 든 자세 → 끝까지 버팀.
#   reload = 36장 1.2초 한 번(DESIGN 8.6-c 「권총 재장전 1.2초」, M39-06b2) — f6 오른손이 가슴 앞 가운데로 당기며 총구를 위로 35° →
#            왼손이 f9 왼쪽 허리(탄창 꺼냄) → f15 손잡이 밑(밀어 넣음, f16 위로 1.5cm) → f24 옆으로 내림 · f20 오른손 슬라이드 당김(뒤 2cm) → f30 든 자세 → 버팀.
#   손 회전: 권총 메시는 총구 +X · 손잡이→슬라이드 +Z(원점 = 쥐는 점, `make_weapon_meshes.py`). 소켓 +Z 가 손 뼈 +Y(= 도구 긴 축, hold_saw)라
#   손 뼈 +Y 를 위로, 손 뼈 +X 를 MUZZLE_SIGN 쪽 앞으로 둔다 — 부호는 2층 `Combat.Gun.PistolAimPose` 가 총구 방향으로 잰다.
# 현대 소총(M39-08c): rifle_recoil·rifle_reload — 든 자세는 권총 것(`A_Player_HoldPistol`)을 같이 쓴다.
#   두 손 겨눔은 이 리그에서 안 된다: 어깨 사이 0.29 · 팔 길이 0.203 · 몸 앞끝 y −0.229 라 왼손이 몸 앞에 나와서는
#   가운데(x ≈ 0)도 못 넘는다 — 오른손의 총(x −0.30)까지 0.45. 그래서 권총처럼 오른팔을 어깨 높이 옆 앞으로 뻗고,
#   95cm 총의 개머리(쥔 점 뒤 30cm ≈ 0.17)가 오른어깨 바깥 옆으로 온다(레퍼런스 `steam_01` 초록 모자 — 몸 옆으로 내민 긴 총).
#   rifle_recoil = 36장 1.2초 한 번(쏘기 간격 1.2초 = 재생 속도 1) — 총구 위 18° · 뒤 4cm, 7.5장(0.25초, DESIGN 8.6-c 소총 줄 「반동 0.25초」)에 든 자세.
#   rifle_reload = 60장 2.0초 한 번(행 ReloadSeconds) — 권총 재장전 순서를 60/36 로 늘리고, 오른손을 가슴 가운데까지 더 당겨
#   왼손이 소총 탄창(쥔 점 앞 11cm · 아래 8cm)에 닿게 한다.
# 바주카(#1211, INBOX 보정 「오른 어깨에 얹고 포구가 앞」): baz_hold·baz_recoil·baz_reload — 105cm 발사관(쥐는 점 뒤 40cm·앞 68cm, 관 중심이 쥔 점 위 7cm)을
#   오른 어깨 위에 얹는다. 1 단위 ≈ 176cm(소총 개머리 30cm ≈ 0.17)라 관 뒤끝 0.23 이 어깨 뒤로 나오고 앞 0.39 가 앞으로 나간다.
#   머리가 어깨보다 넓어(반폭 0.244 > 어깨 x 0.146) 관이 머리 옆에 닿지 않게 손목을 바깥(x −0.24)·어깨 위(z 0.58, 통 밑이 어깨에 닿는 높이)에 둔다.
#   왼손은 앞 손잡이가 팔 길이(0.203) 밖이라 못 쥐고, 가슴 앞 가운데로 뻗어 받치는 시늉을 한다(감독 10-16 2차).
#   baz_hold = 90장 3.0초 되풀이 · baz_recoil = 30장 1.0초(행 FireIntervalSeconds) — 포구 위 10°·뒤 5cm, 15장(0.5초)에 돌아옴 ·
#   baz_reload = 90장 3.0초(행 ReloadSeconds) — 포구를 앞아래 40°로 내리고 왼손이 가슴 앞으로 와 장전한 뒤 다시 얹는다.
# 사용: Blender -b -P pistol_aim_make.py -- hold|recoil|reload|rifle_recoil|rifle_reload|baz_hold|baz_recoil|baz_reload <출력.glb> [연락표.png]
import sys, os, math
RAW = os.environ.get("SNARE_PLAYER_RAW_DIR", "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player")
sys.path.insert(0, RAW)
import move_v2_lib as L
from move_v2_lib import V, Rx, Ry, TAU
from mathutils import Matrix
args = sys.argv[sys.argv.index("--") + 1:]
MODE = args[0]
RIFLE = MODE.startswith("rifle_")
BAZ = MODE.startswith("baz_")
if RIFLE:
    MODE = MODE[len("rifle_"):]
if BAZ:
    MODE = MODE[len("baz_"):]
N = {"hold": 90, "recoil": 30 if BAZ else 36 if RIFLE else 12, "reload": 90 if BAZ else 60 if RIFLE else 36}[MODE]
T = 60 / 36 if RIFLE else 1.0       # 재장전 장 늘림(2.0초 ÷ 1.2초)
K = dict(drop=.008, rise=.004, chest=1.2, clav=1.5, head_keep=.9)   # idle_v2_make.py 와 같은 숨
# 루프#580(감독 #579 3층 「옆쪽 불합격 — 머리 옆·뒤에 가린다」): 머리 반폭 0.244·앞끝 y −0.262(z 0.6~1.0, Blender 측정)가
#   팔 길이 0.203 보다 커서 몸 가운데 앞 두 손 겨눔은 옆·뒤 카메라에서 늘 머리에 덮인다. 레퍼런스 steam_01 초록 모자처럼
#   오른팔을 어깨 높이로 오른쪽 앞에 뻗어 총몸을 머리 옆 윤곽 밖(x −0.30 = 머리 가운데에서 0.298)에 둔다. 두 손은 포기(🟡 INBOX 확인).
HAND_R = V((-0.24, -0.08, 0.58)) if BAZ else V((-0.30, -0.17, 0.505))  # 오른손목 = 어깨 (−0.146, −0.045, 0.508) 높이 · 바깥 0.154 · 앞 0.125 → 0.198(거의 폄)
HAND_L = V((0.17, -0.09, 0.335))    # 왼손목 = 옆에 내림(대기 손 높이 0.323 근처, 어깨에서 0.19)
if BAZ:                              # 감독 10-16 「왼손 통 앞쪽 받침」 — 통 앞쪽은 왼어깨에서 0.36 이상(팔 0.203 밖)이라 못 닿는다.
    HAND_L = V((0.03, -0.22, 0.50))     # 그래서 왼팔을 가슴 앞 가운데로 끝까지 뻗어(어깨에서 0.21) 통 쪽으로 받치는 시늉 — 옆에서 손이 통 아래 앞에 보인다
ELBOW = {"Right": V((-1, 0.2, -0.7)), "Left": V((1, 0.2, -0.7))}   # 팔꿈치는 바깥·아래
MUZZLE_SIGN = 1.0                   # 손 뼈 +X = MUZZLE_SIGN × 몸 앞 (−1 이면 총구가 뒤 180° — 2층 첫 실행 루프#578)
KICK_DEG, KICK_UP, KICK_BACK = 14.0, 0.02, 0.025   # 반동 봉우리: 총구 위로 14° · 손목 위 2cm · 뒤 2.5cm
KICK_PEAK, KICK_END = 2.0, 4.5      # 장 — 4.5장 = 0.15초
if RIFLE:                           # 소총: 더 세게(위 18° · 뒤 4cm) · 0.25초 = 7.5장에 돌아옴 — 2층 `Combat.Gun.RifleAimPose` 가 DESIGN 줄로 잰다
    KICK_DEG, KICK_UP, KICK_BACK = 18.0, 0.025, 0.04
    KICK_PEAK, KICK_END = 2.5, 7.5
if BAZ:                            # 바주카: 포구 위 10° · 뒤 5cm(0.03) · 0.5초 = 15장에 돌아옴
    KICK_DEG, KICK_UP, KICK_BACK = 10.0, 0.01, 0.03
    KICK_PEAK, KICK_END = 3.0, 15.0
    HOLD_PITCH = 4.0                # 든 자세에서 포구를 수평보다 4° 위로(어깨에 얹힌 느낌, Rx(−) = 포구 위)
else:
    HOLD_PITCH = 0.0
# 재장전 — 팔 길이 0.203 안(오른어깨 (−0.146, −0.045, 0.508) · 왼어깨 (0.146, …))에서 두 손이 가슴 앞 가운데에 모인다.
R_IN = V((-0.04, -0.16, 0.42))      # 오른손목 당김 (어깨에서 0.18)
L_POUCH = V((0.15, -0.05, 0.31))    # 왼손 허리 탄창 (어깨에서 0.198)
L_MAG = V((0.04, -0.13, 0.37))      # 왼손 손잡이 밑 (어깨에서 0.194)
RELOAD_PITCH, MAG_PUSH, RACK_BACK = 35.0, 0.015, 0.02
if BAZ:                             # 포구를 앞아래 40°로 내리고(어깨에서 내림) 왼손이 가슴 앞 가운데로 와 뒤끝에 로켓을 밀어 넣는다
    RELOAD_PITCH, RACK_BACK = -40.0, 0.0
    R_IN = V((-0.17, -0.20, 0.40))      # 오른손목: 어깨에서 0.19 앞아래
    L_POUCH = V((0.15, -0.05, 0.31))
    L_MAG = V((0.0, -0.15, 0.45))       # 왼손 가슴 앞 가운데(왼어깨에서 0.19)
if RIFLE:
    # 소총 탄창은 쥔 점 앞 11cm·아래 8cm(`make_weapon_meshes.py` rifle) — 총구 위 35° 일 때 쥔 점에서 앞 ≈ 0.077 이라
    # 오른손을 가슴 가운데 왼쪽까지 당겨야 왼손(어깨 (0.146, −0.045, 0.508))이 닿는다. 오른어깨에서 0.195 · 왼어깨에서 ≈ 0.205(IK 가 1cm 안 모자람).
    R_IN = V((0.02, -0.12, 0.45))
    L_MAG = V((0.02, -0.197, 0.45))
# (장, 값) 꺾은선 — 장 사이는 L.smooth 로 잇는다.
if BAZ:                             # 90장 = 36장 × 2.5: 0.4초(12장)에 내리고 → 왼손 앞(30장) → 밀어 넣기(50장) → 0.4초 안에 다시 얹음(80장)
    T = 2.5
R_KEYS = [(0 * T, 0.0), (6 * T, 1.0), (22 * T, 1.0), (30 * T, 0.0)]                 # 0 = 든 자세 HAND_R · 1 = R_IN(총구 위 35°)
L_KEYS = [(0 * T, HAND_L), (4 * T, HAND_L), (9 * T, L_POUCH), (15 * T, L_MAG), (17 * T, L_MAG + V((0, 0, MAG_PUSH))), (24 * T, HAND_L)]
RACK_KEYS = [(0 * T, 0.0), (19 * T, 0.0), (20.5 * T, 1.0), (22 * T, 0.0)]
rig = L.Rig()
hip0 = rig.head0["Hips"]
feet = {s: rig.idleG4[s + "Foot"].copy() for s in ("Left", "Right")}
poles = {s: rig.head0[s + "Leg"] - (rig.head0[s + "UpLeg"] + rig.head0[s + "Foot"]) / 2 for s in ("Left", "Right")}
fwd, up = V((0, -1, 0)), V((0, 0, 1))
xh = fwd * MUZZLE_SIGN
GRIP0 = Matrix((xh, up, xh.cross(up))).transposed()      # 손 뼈 월드 회전 (열 = x, y(위), z)


def kick(f):
    if MODE != "recoil" or f >= KICK_END:
        return 0.0
    if f <= KICK_PEAK:
        return L.smooth(f / KICK_PEAK)
    return 1 - L.smooth((f - KICK_PEAK) / (KICK_END - KICK_PEAK))


def keyed(keys, f):
    if f <= keys[0][0]:
        return keys[0][1]
    for (fa, va), (fb, vb) in zip(keys, keys[1:]):
        if f <= fb:
            t = L.smooth((f - fa) / (fb - fa))
            return va + (vb - va) * t
    return keys[-1][1]


def body(f):
    b = (1 - math.cos(TAU * f / N)) / 2 if MODE == "hold" else 0.0   # 반동은 든 자세 f0 몸 그대로
    lift = -K["drop"] + K["rise"] * b
    ch = -K["chest"] * b
    A = {"Spine1": Rx(ch * .4), "Spine2": Rx(ch * .6),
         "Neck": Rx(-ch * K["head_keep"] * .5), "Head": Rx(-ch * K["head_keep"] * .5)}
    for s, sg in (("Left", 1), ("Right", -1)):
        A[s + "Shoulder"] = Ry(-sg * K["clav"] * b)
    k = kick(f)
    shift = V((0, KICK_BACK * k, lift + KICK_UP * k))
    legs = {s: (feet[s], poles[s]) for s in ("Left", "Right")}
    hand_r, hand_l, pitch = HAND_R, HAND_L, KICK_DEG * k
    if MODE == "reload":
        r = keyed(R_KEYS, f)
        hand_r = HAND_R.lerp(R_IN, r) + V((0, RACK_BACK * keyed(RACK_KEYS, f), 0))
        hand_l, pitch = keyed(L_KEYS, f), RELOAD_PITCH * r
    arms = {"Right": (hand_r + shift, ELBOW["Right"]), "Left": (hand_l, ELBOW["Left"])}
    grip = Rx(-pitch - HOLD_PITCH * (1 - (r if MODE == "reload" else 0.0))) @ GRIP0                            # Rx(+) = 앞으로 숙임 → 음수 = 총구 위로
    return hip0 + V((0, 0, lift)), A, legs, arms, grip


def pose(f):
    hips, A, legs, arms, grip = body(f)
    _, G4, _ = rig.solve(hips, A, legs=legs, arms=arms)          # 1차: 아래팔 회전을 얻는다
    for s in ("Right",):                                          # 왼손은 쥐지 않으니 대기 손 회전 그대로
        fa = s + "ForeArm"
        Cp = G4[fa].to_3x3() @ rig.idleG[fa].inverted()
        A[s + "Hand"] = Cp.inverted() @ grip @ rig.idleG[s + "Hand"].inverted()
    out, G4, reach = rig.solve(hips, A, legs=legs, arms=arms)
    return out, reach


name = {"hold": "A_Player_HoldPistol", "recoil": "A_Player_PistolRecoil", "reload": "A_Player_PistolReload"}[MODE]
if RIFLE:
    name = {"recoil": "A_Player_RifleRecoil", "reload": "A_Player_RifleReload"}[MODE]
rig.bake(name, N, pose)
rig.seam(N)
sc, PB = rig.sc, rig.PB
worst = 0.0
for f in range(N + 1):
    _, _, _, arms, _ = body(f); sc.frame_set(f)
    for s in ("Left", "Right"):
        worst = max(worst, (PB[s + "Hand"].head - arms[s][0]).length)
for f in ((0, 2, 5, 8) if MODE != "reload" else tuple(round(k * T) for k in (0, 6, 9, 15, 20, 30))):
    sc.frame_set(f)
    print("PISTOL f%d 오른손목 %s · 왼손목 %s · 손 x축 %s" % (f, tuple(round(v, 3) for v in PB["RightHand"].head),
          tuple(round(v, 3) for v in PB["LeftHand"].head), tuple(round(v, 2) for v in PB["RightHand"].matrix.col[0][:3])))
print("PISTOL 손목-목표 최대 %.2fcm" % (worst * 100))
sc.frame_set(0)
rig.heights(N)
rig.export(args[1])
if len(args) > 2: rig.sheet(N, args[2])
