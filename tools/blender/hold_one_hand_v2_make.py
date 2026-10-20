# 한 손 도구 든 자세 v2 (감독 PIE 2026-10-05 · docs/reference/character_motion.md 「걷기」 — 팔은 몸 옆에서 작게):
#   옛 A_Player_HoldOneHand(Mixamo Sword And Shield Idle)는 골반을 25~54° 옆으로 튼 싸움 자세다. 걸을 때는 상체만 든 자세를
#   받고 골반은 걷기 것(앞)이라, 옆으로 튼 몸 기준이던 오른팔이 몸 앞으로 수평으로 뻗어 보였다
#   (감독 PIE 손도끼 걷기 오른손 앞 22~48cm · 맨손 0~19cm, 몽둥이는 어깨 위로 들림).
#   v2 = 몸은 대기 v2 숨쉬기(hold_saw_make.py 와 같은 숨, 90장 3.0초 되풀이) — 골반이 앞을 봐 걷기와 섞어도 그대로다.
#   오른손(Socket_Weapon)만 IK 로 오른쪽 엉덩이 옆 조금 앞에 두고, 손 회전은 옛 자세 첫 장의 「골반 기준 손 회전」을 그대로 옮긴다
#   (도구 끝은 DROOP 만큼 아래로 숙임) — 도구 행 쥐는 각(GripOffset)은 옛 손 회전에 맞춰져 있어 손 회전을 새로 지으면 손도끼·마체테·곡괭이·몽둥이 날이 다 돌아간다.
#   왼손은 대기 그대로 늘어뜨린다(옛 자세는 방패 든 왼손이 가슴 앞이었다).
# 사용: [SNARE_HOLD_HEAVY=1] Blender -b -P hold_one_hand_v2_make.py -- <출력.glb> [연락표.png]
#   리그·원본은 `SNARE_PLAYER_RAW_DIR`(기본 = 메인 체크아웃 `_Source/raw/Snare/Characters/Player`)에서 읽기만 한다.
import sys, os, math
RAW = os.environ.get("SNARE_PLAYER_RAW_DIR", "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player")
sys.path.insert(0, RAW)
import bpy
import move_v2_lib as L
from move_v2_lib import V, Ry, Rx, TAU
from mathutils import Matrix
args = sys.argv[sys.argv.index("--") + 1:]
N = 90
K = dict(drop=.008, rise=.004, chest=1.2, clav=1.5, head_keep=.9)   # idle_v2_make.py 와 같은 숨
HAND = V((-0.20, -0.07, 0.37))      # 오른손목 = 오른쪽 엉덩이 옆 조금 앞, 허리(Spine z 0.378) 높이. 오른어깨 (−0.146, −0.045, 0.508)
ELBOW = V((-1, 0.3, -0.4))          # 팔꿈치는 바깥·조금 뒤
DROOP = 30.0                        # 도구 끝을 앞에서 아래로 숙이는 각 — 레퍼런스 기본 곡괭이 「쉴 때 자루가 몸 앞 오른쪽 아래로 늘어짐」(시험 HandToolsClearBodyAndGround ③)
# 무거운 곡괭이(고급·전기) 든 자세 = 같은 몸·같은 손목, 숙임만 HEAVY_DROOP(루프#1354, INBOX 결함 10-20 ④): 전기 곡괭이는 머리 폭이 87.5cm 로
#   가장 넓어 30° 면 아래 날이 발바닥보다 3.5cm 아래로 박혔다(시험 HandToolsClearBodyAndGround ②). 쓸 때: SNARE_HOLD_HEAVY=1 → A_Player_HoldHeavyPickaxe
HEAVY_DROOP = 15.0
HEAVY = os.environ.get("SNARE_HOLD_HEAVY") == "1"
if HEAVY:
    DROOP = HEAVY_DROOP
rig = L.Rig()
hip0 = rig.head0["Hips"]
feet = {s: rig.idleG4[s + "Foot"].copy() for s in ("Left", "Right")}
poles = {s: rig.head0[s + "Leg"] - (rig.head0[s + "UpLeg"] + rig.head0[s + "Foot"]) / 2 for s in ("Left", "Right")}

# 옛 자세 첫 장 — 골반 기준 오른손 회전
_before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=os.path.join(RAW, "weapons", "hold_one_hand.glb"), bone_heuristic='BLENDER')
_arm = [o for o in bpy.data.objects if o not in _before and o.type == 'ARMATURE'][0]
rig.sc.frame_set(0)
bpy.context.view_layer.update()
_pb = _arm.pose.bones
G = Matrix.Rotation(math.radians(DROOP), 3, 'X') @ rig.idleG["Hips"] @ _pb["Hips"].matrix.to_3x3().normalized().inverted() @ _pb["RightHand"].matrix.to_3x3().normalized()
_act = _arm.animation_data.action if _arm.animation_data else None
for o in [o for o in bpy.data.objects if o not in _before]:
    bpy.data.objects.remove(o, do_unlink=True)
if _act:
    bpy.data.actions.remove(_act)


def body(f):
    b = (1 - math.cos(TAU * f / N)) / 2
    lift = -K["drop"] + K["rise"] * b
    ch = -K["chest"] * b
    A = {"Spine1": Rx(ch * .4), "Spine2": Rx(ch * .6),
         "Neck": Rx(-ch * K["head_keep"] * .5), "Head": Rx(-ch * K["head_keep"] * .5)}
    for s, sg in (("Left", 1), ("Right", -1)):
        A[s + "Shoulder"] = Ry(-sg * K["clav"] * b)
    legs = {s: (feet[s], poles[s]) for s in ("Left", "Right")}
    return hip0 + V((0, 0, lift)), A, legs, {"Right": (HAND + V((0, 0, lift)), ELBOW)}


def pose(f):
    hips, A, legs, arms = body(f)
    _, G4, _ = rig.solve(hips, A, legs=legs, arms=arms)          # 1차: 아래팔 회전을 얻는다
    Cp = G4["RightForeArm"].to_3x3() @ rig.idleG["RightForeArm"].inverted()
    A["RightHand"] = Cp.inverted() @ G @ rig.idleG["RightHand"].inverted()
    out, G4, reach = rig.solve(hips, A, legs=legs, arms=arms)
    return out, reach


rig.bake("A_Player_HoldHeavyPickaxe" if HEAVY else "A_Player_HoldOneHand", N, pose)
rig.seam(N)
sc, PB = rig.sc, rig.PB
worst = 0.0
for f in range(N + 1):
    _, _, _, arms = body(f); sc.frame_set(f)
    worst = max(worst, (PB["RightHand"].head - arms["Right"][0]).length)
sc.frame_set(0)
print("HOLD1 오른손목 %s · 왼손목 %s · 손목-목표 최대 %.2fcm · 손 축 %s" % (
    tuple(round(v, 3) for v in PB["RightHand"].head), tuple(round(v, 3) for v in PB["LeftHand"].head), worst * 100,
    tuple(round(v, 2) for v in L.ax(PB["RightHand"].matrix))))
rig.heights(N)
rig.export(args[0])
if len(args) > 1: rig.sheet(N, args[1])
