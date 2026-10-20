# 마체테 쓰기 v2 (감독 2026-10-05 · docs/reference/character_motion.md 「마체테」 — 레퍼런스 마체테는 덤불을 옆으로 벤다):
#   옛 A_Player_Slash(Mixamo Sword And Shield Slash 37장)는 6/12장에서 손목이 무릎 높이로 내려가 날이 수직 아래를 봐
#   88cm 날이 발바닥 아래 −133cm 까지 박혔다(2층 `Snarehaven.Equipment.Pickaxe.HandToolsClearBodyAndGround` 경고).
#   v2 = 오른손 한 손으로 허리 높이 수평 옆 베기: f0 = 든 자세(HoldOneHand f0) → f2 몸 오른쪽 뒤로 날을 젖힘(백스윙)
#   → f6 몸 앞(타격, 0.2초 = DESIGN 3.1 「공격 타격 지연」) → f9 몸 앞 왼쪽으로 빠짐 → f13 까지 든 자세로 돌아와 f18 까지 머문다.
#   길이 18장 = 공격 간격 0.6초(재생 속도 1). 손목은 내내 허리(Spine z 0.378) 위, 날 긴 축은 수평 ±10° 안.
#   방식은 `_Source/raw/.../weapons/mine_one_hand_make.py`(기본 곡괭이) 그대로 — `move_v2_lib` 리그에 손목 IK·손 축을 찍는다.
#   시트는 마체테 메시(Machete_v1.glb, 게임 88cm ÷ 몸 배율 1.76, 쥐는 점 행 GripOffset Y −35 = 원본 +0.398)를 오른손에 붙여 그린다.
# 사용: Blender -b -P slash_v2_make.py -- <출력.glb> [연락표.png]
#   리그·원본 폴더는 `SNARE_PLAYER_RAW_DIR`(기본 = 메인 체크아웃 `_Source/raw/Snare/Characters/Player`)에서 읽기만 한다.
import sys, os, math
RAW = os.environ.get("SNARE_PLAYER_RAW_DIR", "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player")
sys.path.insert(0, RAW)
import bpy
import move_v2_lib as L
from move_v2_lib import V, Rx, Rz
from mathutils import Matrix, Quaternion
args = sys.argv[sys.argv.index("--") + 1:]
N = 18                    # 0.6초 = 공격 간격
BACK, HIT, FOLLOW, SETTLE = 2, 6, 9, 13
# 손목 자리(몸 앞 = −Y, 캐릭터 왼쪽 = +X, 위 = +Z, 발바닥 = 0). 오른어깨 (−0.146, −0.045, 0.508), 팔 길이 0.204.
HAND = [V((-0.27, -0.02, 0.44)), V((-0.10, -0.19, 0.41)), V((0.00, -0.15, 0.44))]
# 날 긴 축(손잡이 → 날 끝): 오른쪽 뒤 → 앞 → 앞 왼쪽, 조금 아래(덤불 높이)
AXIS = [V((-0.60, 0.78, -0.05)).normalized(), V((-0.25, -1.0, -0.12)).normalized(), V((0.80, -0.60, -0.12)).normalized()]
ELBOW = [V((-1.0, 0.4, -0.5)), V((-1.0, 0.0, -0.6)), V((-0.6, -0.2, -0.8))]
TWIST = [-20.0, 0.0, 15.0]                # 상체 비틀기(+ = 왼쪽으로 돎) — 백스윙 때 오른어깨가 뒤로
HIP_TWIST = 0.4                           # 골반은 상체 비틀기의 이 몫만
UP = V((0, 0, 1))
TOOL_K = 0.88 / 1.76                      # 몸 공간 길이(게임 88cm ÷ 몸 배율)
GRIP_Y = 35.0 / 88.0                      # 행 GripOffset Y −35cm = 원본 +0.398 (손잡이 쪽)
rig = L.Rig()
hip0 = rig.head0["Hips"]
feet = {s: rig.idleG4[s + "Foot"].copy() for s in ("Left", "Right")}
poles = {s: rig.head0[s + "Leg"] - (rig.head0[s + "UpLeg"] + rig.head0[s + "Foot"]) / 2 for s in ("Left", "Right")}


def lerp(a, b, t):
    return a + (b - a) * t


def key(f):
    """(앞 열쇠, 뒤 열쇠, 섞는 몫). 백스윙 → 타격은 빨라지고(ease-in), 타격 → 빠짐은 느려진다(ease-out)."""
    if f <= HIT:
        t = max(f - BACK, 0) / (HIT - BACK)
        return 0, 1, t * t
    t = min((f - HIT) / (FOLLOW - HIT), 1.0)
    return 1, 2, 1 - (1 - t) ** 2


def hold_weight(f):
    """든 자세(HoldOneHand f0)와 섞는 몫 — 1 이면 든 자세 그대로.
    시작은 f0 = 든 자세 → f1 = 바로 백스윙(섞지 않음): 든 자세(날 앞 오른쪽)와 백스윙(날 뒤) 사이 뼈 회전을 섞으면
    손 회전이 아래로 돌아 날이 땅에 박힌다(첫 시도 f1 날 −73°, 최저 −3.7cm). 끝(빠짐 → 든 자세)은 날이 같은 쪽이라 섞어도 수평이다."""
    if f == 0:
        return 1.0
    if f <= BACK:
        return 0.0
    if f <= FOLLOW:
        return 0.0
    return L.smooth(min((f - FOLLOW) / (SETTLE - FOLLOW), 1.0))


def body(f):
    a, b, t = key(f)
    axis = (Quaternion().slerp(AXIS[a].rotation_difference(AXIS[b]), t) @ AXIS[a]).normalized()
    xh = UP.cross(axis).normalized()      # 날 폭(손 뼈 X)을 베는 면(수평) 안에 — 날이 옆으로 눕는다
    G = Matrix((xh, axis, xh.cross(axis))).transposed()
    twist = lerp(TWIST[a], TWIST[b], t)
    A = {"Hips": Rz(twist * HIP_TWIST),
         "Spine": Rz(twist * .2), "Spine1": Rz(twist * .2), "Spine2": Rz(twist * .2),
         "Neck": Rz(-twist * .5), "Head": Rz(-twist * .5)}
    legs = {s: (feet[s], poles[s]) for s in ("Left", "Right")}
    arms = {"Right": (lerp(HAND[a], HAND[b], t), lerp(ELBOW[a], ELBOW[b], t))}
    return hip0, A, legs, arms, G, axis


_before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=os.path.join(RAW, "weapons", "hold_one_hand_v2.glb"), bone_heuristic='BLENDER')
_hold_arm = [o for o in bpy.data.objects if o not in _before and o.type == 'ARMATURE'][0]
rig.sc.frame_set(0)
HOLD = {pb.name: (pb.matrix_basis.to_quaternion(), pb.matrix_basis.to_translation()) for pb in _hold_arm.pose.bones}
_hold_act = _hold_arm.animation_data.action if _hold_arm.animation_data else None
for o in [o for o in bpy.data.objects if o not in _before]:
    bpy.data.objects.remove(o, do_unlink=True)
if _hold_act:
    bpy.data.actions.remove(_hold_act)


def pose(f):
    hips, A, legs, arms, G, _ = body(f)
    _, G4, _ = rig.solve(hips, A, legs=legs, arms=arms)
    Cp = G4["RightForeArm"].to_3x3() @ rig.idleG["RightForeArm"].inverted()
    A["RightHand"] = Cp.inverted() @ G @ rig.idleG["RightHand"].inverted()
    out, G4, reach = rig.solve(hips, A, legs=legs, arms=arms)
    w = hold_weight(f)
    if w > 0:
        for n, (q, loc) in list(out.items()):
            if n in HOLD:
                hq, hl = HOLD[n]
                if q.dot(hq) < 0:
                    hq = -hq
                out[n] = (q.slerp(hq, w), loc.lerp(hl, w))
    return out, reach


rig.bake("A_Player_Slash", N, pose)
sc, PB = rig.sc, rig.PB


def attach_tool():
    """마체테 메시를 오른손에 붙인다 — saw_thrust_make.py 와 같은 축(행 GripOffset Roll −90): 원본 −Y(날 끝) → 손 뼈 Y."""
    bpy.ops.import_scene.gltf(filepath=os.path.join(RAW, "../../Items/Tools/Machete/Machete_v1.glb"))
    t = [o for o in bpy.context.selected_objects if o.type == 'MESH'][0]
    t.parent = None
    con = t.constraints.new('CHILD_OF')
    con.target = rig.a; con.subtarget = "RightHand"; con.set_inverse_pending = False
    con.inverse_matrix = Matrix.Identity(4)
    t.matrix_basis = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0))).to_4x4() @ Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0))).to_4x4() \
        @ Matrix.Scale(TOOL_K, 4) @ Matrix.Translation((0, -GRIP_Y, 0))
    return t


tool = attach_tool()
lowest, low_wrist = 9.0, 9.0
for f in range(N + 1):
    _, _, _, arms, _, axis = body(f); sc.frame_set(f)
    bpy.context.view_layer.update()
    vs = [tool.matrix_world @ v.co for v in tool.data.vertices]
    low = min(v.z for v in vs)
    lowest = min(lowest, low)
    low_wrist = min(low_wrist, PB["RightHand"].head.z)
    tipv = max(vs, key=lambda v: (v - PB["RightHand"].head).length)
    d = (tipv - PB["RightHand"].head).normalized()
    print("FRAME %2d 오른손목 %s · 날 끝 %s · 날 방향 %s(수평에서 %.1f°) · 메시 최저 z %.3f" % (
        f, tuple(round(v, 3) for v in PB["RightHand"].head), tuple(round(v, 3) for v in tipv),
        tuple(round(v, 2) for v in d), math.degrees(math.asin(max(-1, min(1, d.z)))), low))
print("SLASH 손목 최저 z %.3f (허리 Spine %.3f) · 마체테 최저 z %.3f (발바닥 0) · 타격 f%d/%d = %.2f초" % (
    low_wrist, rig.head0["Spine"].z, lowest, HIT, N, HIT / L.FPS))
rig.seam(N)
rig.heights(N)
bpy.data.objects.remove(tool, do_unlink=True)
rig.a.select_set(True)
bpy.context.view_layer.objects.active = rig.a
rig.export(args[0])
if len(args) > 1:
    tool = attach_tool()
    base_world = rig.mesh_world
    rig.mesh_world = lambda: base_world() + [tool.matrix_world @ v.co for v in tool.data.vertices]
    bpy.app.handlers.frame_change_post.append(lambda *_: setattr(tool, "hide_render", False))
    rig.sheet(N, args[1])
