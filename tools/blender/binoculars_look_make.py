# 망원경 보기 (감독 2026-10-05 · docs/reference/character_motion.md 「망원경」): 두 손을 눈 앞으로 올려 들여다본다.
#   45장 = 1.5초(재생 속도 1, 행 bUseAnimFixedLength): f0 = 대기(standing_idle_1 f2 = 리그 기준 자세)
#   → f9(0.3초)까지 두 손목을 턱 앞으로 올리고 고개를 살짝 든다 → f36 까지 버틴다 → f45 에 대기로 돌아온다.
#   팔이 짧고(어깨→손목 0.204) 머리가 커서(머리 메시 z 0.60~1.02) 눈 높이 앞은 닿지 않는다 —
#   손목을 목(Neck z 0.577) 위·몸 앞(y −0.17)에 두면 망원경(왼손 Socket_Tool)이 얼굴 앞에 온다.
#   방식은 slash_v2_make.py 와 같다 — `move_v2_lib` 리그에 손목 IK 를 찍는다.
# 사용: Blender -b -P binoculars_look_make.py -- <출력.glb> [연락표.png]
import sys, os
RAW = os.environ.get("SNARE_PLAYER_RAW_DIR", "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player")
sys.path.insert(0, RAW)
import bpy
import move_v2_lib as L
from move_v2_lib import V, Rx
args = sys.argv[sys.argv.index("--") + 1:]
N = 45
UP_END, DOWN_START = 9, 36
LOOK = {"Right": (V((-0.05, -0.17, 0.60)), V((-0.7, 0.0, -1.0))),
        "Left": (V((0.05, -0.17, 0.60)), V((0.7, 0.0, -1.0)))}
HEAD_UP = 6.0                             # 목·머리 각각 든 각(도)
rig = L.Rig()
hip0 = rig.head0["Hips"]
feet = {s: rig.idleG4[s + "Foot"].copy() for s in ("Left", "Right")}
poles = {s: rig.head0[s + "Leg"] - (rig.head0[s + "UpLeg"] + rig.head0[s + "Foot"]) / 2 for s in ("Left", "Right")}
IDLE_ARM = {s: (rig.head0[s + "Hand"], LOOK[s][1]) for s in ("Left", "Right")}


def raise_w(f):
    if f <= UP_END:
        return L.smooth(f / UP_END)
    if f <= DOWN_START:
        return 1.0
    return 1 - L.smooth((f - DOWN_START) / (N - DOWN_START))


def pose(f):
    w = raise_w(f)
    A = {"Neck": Rx(-HEAD_UP * w), "Head": Rx(-HEAD_UP * w)}
    legs = {s: (feet[s], poles[s]) for s in ("Left", "Right")}
    arms = {s: (IDLE_ARM[s][0].lerp(LOOK[s][0], w), LOOK[s][1]) for s in ("Left", "Right")}
    out, _, reach = rig.solve(hip0, A, legs=legs, arms=arms)
    return out, reach


rig.bake("A_Player_LookBinoculars", N, pose)
PB = rig.PB
rig.sc.frame_set(UP_END)
print("LOOK f%d 오른손목 %s · 왼손목 %s · 목 z %.3f" % (
    UP_END, tuple(round(v, 3) for v in PB["RightHand"].head), tuple(round(v, 3) for v in PB["LeftHand"].head), rig.head0["Neck"].z))
rig.seam(N)
rig.heights(N)
rig.a.select_set(True)
bpy.context.view_layer.objects.active = rig.a
rig.export(args[0])
if len(args) > 1:
    rig.sheet(N, args[1])
