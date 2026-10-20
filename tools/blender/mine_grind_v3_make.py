# 고급·전기 곡괭이 쓰기 v3 (INBOX 결함 2026-10-20 00:10 ④ 「곡괭이를 들면 몸이 앞으로 크게 숙여짐」, 루프#1354):
#   든 자세를 옛 A_Player_HoldTwoHand(Mixamo 2hand Idle — 무릎·허리를 굽힌 칼 싸움 자세)에서 기본 곡괭이와 같은
#   A_Player_HoldHeavyPickaxe(`tools/blender/hold_one_hand_v2_make.py` SNARE_HOLD_HEAVY=1 — 대기 v2 숨 몸, 오른손 엉덩이 옆, 숙임 15°)로
#   바꿨다. 쓰기 클립의 첫·끝 장은 든 자세여야 이음매가 없으므로(시험 AdvancedElectricGrindInFront ④⑥) 섞는 든 자세만
#   weapons/hold_heavy_pickaxe.glb 첫 장으로 바꿔 다시 굽는다.
#   나머지(댄 자세·떨림·길이 1.5초·타격 f6)는 원본 `weapons/mine_grind_make.py`(루프#386·#387) 그대로.
# 사용: Blender -b --factory-startup -P tools/blender/mine_grind_v3_make.py -- <출력.glb> [연락표.png]
#   리그·원본은 `SNARE_PLAYER_RAW_DIR`(기본 = 메인 체크아웃 `_Source/raw/Snare/Characters/Player`)에서 읽기만 한다.
import sys, os, math
RAW = os.environ.get("SNARE_PLAYER_RAW_DIR", "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player")
sys.path.insert(0, RAW)
import bpy
import move_v2_lib as L
from move_v2_lib import V, Rx, Rz
from mathutils import Matrix
args = sys.argv[sys.argv.index("--") + 1:]
N = 45                    # 1.5초 = 한 번 누름의 정해진 길이(레퍼런스 게임 전기톱 시트 약 1.5초)
THRUST = 6                # 0.2초 = 타격 장
BLEND = 4                 # 든 자세 → 몸 앞 자세로 섞는 장(이 뒤로는 든 자세 몫 0)
RET = 36                  # 1.2초부터 든 자세로 돌아오기 시작 → N 장에 든 자세 몫 1
RET2 = 42                 # v3: 오른손 IK 목표(손목·쥐는 각)도 숙임과 같은 몫(phase)으로 든 자세 값 ↔ 댄 자세 값을 오가고, 뼈마다 섞기는 f0~BLEND·RET2~N 만
                          #   (v2 는 IK 목표가 처음부터 댄 자세라 f0~4·f36~45 섞기 중간 장에 손 회전이 따로 돌아 전기 곡괭이 날이 발보다 0.7·2.4cm 아래로 내려갔다 — 루프#1354)
HAND_R = V((-0.04, -0.16, 0.37))          # 뒷손(오른손) = 자루 끝, 허리(Spine z 0.378) 높이 몸 앞
AXIS = V((0.3, -0.88, -0.36)).normalized()   # 자루: 몸 앞(조금 왼쪽)으로 눕혀 조금 아래로(짧은 팔 두 손이 닿게)
HEAD_UP = V((0, 0, 1))                    # 머리 폭(손 뼈 X)을 세워 아래쪽 날이 광맥을 향하게
LEFT_GAP = 0.06           # 앞손(왼손) = 자루를 따라 6cm 앞
ELBOW = {"Right": V((-1, 0.3, -0.7)), "Left": V((1, 0.1, -0.6))}
LEAN, TWIST = 26.0, -20.0 # 상체 앞숙임(척추 셋 합) · 오른쪽으로 돎(왼어깨가 앞으로 나와 앞손이 닿는다)
HIP_LEAN, HIP_DROP = 4.0, 0.02            # 골반 앞숙임 · 무릎 굽혀 버팀
JIT, JIT_DEG = 0.004, 1.5                 # 버티는 떨림: 손 ±0.4cm · 자루 각 ±1.5°
TOOL_LEN = 0.50 / 1.76                    # 몸 공간 길이(게임 50cm ÷ 몸 배율)
GRIP_T = 0.1                              # 쥐는 점 = 손잡이 끝에서 길이 0.1 안쪽(DESIGN 3.1)
rig = L.Rig()
hip0 = rig.head0["Hips"]
feet = {s: rig.idleG4[s + "Foot"].copy() for s in ("Left", "Right")}
poles = {s: rig.head0[s + "Leg"] - (rig.head0[s + "UpLeg"] + rig.head0[s + "Foot"]) / 2 for s in ("Left", "Right")}
left = V((1, 0, 0))


def back(f):
    """돌아오기 몫 — RET 0 → RET2 1 (IK 목표를 든 자세 값으로 옮기는 정도)."""
    return L.smooth(min(max(f - RET, 0) / (RET2 - RET), 1.0))


def phase(f):
    """0 = 든 자세 쪽, 1 = 댄 자세. 빨리 나가 댄 자리에 붙는다(ease-out). 돌아올 땐 back 만큼 다시 0 으로."""
    t = min(f / THRUST, 1.0)
    return (1 - (1 - t) ** 3) * (1 - back(f))


def hold_weight(f):
    """든 자세(HoldHeavyPickaxe f0)와 섞는 몫 — f0 은 든 자세 그대로라 든 자세 → 쓰기 이음매가 없다."""
    if f >= RET2:
        return L.smooth((f - RET2) / (N - RET2))
    if f >= RET:
        return 0.0
    return 1 - L.smooth(min(f / BLEND, 1.0))


def shake(f):
    """댄 뒤에만 켜지는 떨림 — 30fps 에서 장마다 방향이 바뀌어야 보인다(15Hz·7.5Hz 섞음, SawThrust 와 같은 식)."""
    w = min(max(f - THRUST, 0) / 2.0, 1.0) * (1 - back(f))
    return w * ((-1) ** f) * .6, w * math.cos(math.pi * f / 2), w * math.sin(math.pi * f / 2)


def grip(f):
    _, _, sa = shake(f)
    axis = (Matrix.Rotation(math.radians(JIT_DEG * sa), 3, left) @ AXIS).normalized()
    xh = (HEAD_UP - axis * HEAD_UP.dot(axis)).normalized()
    G = Matrix((xh, axis, xh.cross(axis))).transposed()
    r = 1 - phase(f)
    if r > 0:
        G = G.to_quaternion().slerp(HOLD_GRIP.to_quaternion(), r).to_matrix()
        axis = G.col[1].normalized()
    return axis, G


def body(f):
    p = phase(f)
    sx, sz, _ = shake(f)
    axis, G = grip(f)
    hips = hip0 + V((0, 0, -HIP_DROP * p))
    lean, twist = LEAN * p, TWIST * p
    A = {"Hips": Rx(HIP_LEAN * p),
         "Spine": Rz(twist * .3) @ Rx(lean * .3), "Spine1": Rz(twist * .3) @ Rx(lean * .3), "Spine2": Rz(twist * .4) @ Rx(lean * .4),
         "Neck": Rz(-twist * .4) @ Rx(-(lean + HIP_LEAN * p) * .35), "Head": Rz(-twist * .4) @ Rx(-(lean + HIP_LEAN * p) * .35)}
    hr = HAND_R.lerp(HOLD_WRIST, 1 - phase(f)) + V((JIT * sx, 0, JIT * sz))
    hl = hr + axis * LEFT_GAP
    legs = {s: (feet[s], poles[s]) for s in ("Left", "Right")}
    arms = {"Right": (hr, ELBOW["Right"]), "Left": (hl, ELBOW["Left"])}
    return hips, A, legs, arms, G, axis


# 든 자세(HoldHeavyPickaxe) 첫 장을 뼈마다 읽어 둔다(mine_one_hand_make.py 와 같은 방법).
_before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=os.path.join(RAW, "weapons", "hold_heavy_pickaxe.glb"), bone_heuristic='BLENDER')
_hold_arm = [o for o in bpy.data.objects if o not in _before and o.type == 'ARMATURE'][0]
rig.sc.frame_set(0)
HOLD = {pb.name: (pb.matrix_basis.to_quaternion(), pb.matrix_basis.to_translation()) for pb in _hold_arm.pose.bones}
HOLD_WRIST = _hold_arm.pose.bones["RightHand"].head.copy()                       # 돌아오기 IK 목표 — 든 자세 오른손목
HOLD_GRIP = _hold_arm.pose.bones["RightHand"].matrix.to_3x3().normalized()       # 든 자세 오른손 회전
print("HOLD 든 자세 오른손목 %s · 긴 축 %s" % (tuple(round(v, 3) for v in HOLD_WRIST), tuple(round(v, 2) for v in HOLD_GRIP.col[1])))
_hold_act = _hold_arm.animation_data.action if _hold_arm.animation_data else None
for o in [o for o in bpy.data.objects if o not in _before]:
    bpy.data.objects.remove(o, do_unlink=True)
if _hold_act:
    bpy.data.actions.remove(_hold_act)


def pose(f):
    hips, A, legs, arms, G, _ = body(f)
    _, G4, _ = rig.solve(hips, A, legs=legs, arms=arms)
    for s in ("Left", "Right"):
        fa = s + "ForeArm"
        Cp = G4[fa].to_3x3() @ rig.idleG[fa].inverted()
        A[s + "Hand"] = Cp.inverted() @ G @ rig.idleG[s + "Hand"].inverted()
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


rig.bake("A_Player_MineGrind", N, pose)
sc, PB = rig.sc, rig.PB
TOOL = L.P + "../../Items/Tools/PickaxeAdvanced/PickaxeAdvanced_v1.glb"


def attach_tool():
    """고급 곡괭이 메시를 오른손에 붙인다 — 메시 Z(긴 축, 머리 +Z) → 손 뼈 Y · 메시 X(머리 폭) → 손 뼈 X · 메시 Y → 손 뼈 −Z."""
    bpy.ops.import_scene.gltf(filepath=TOOL)
    t = [o for o in bpy.context.selected_objects if o.type == 'MESH'][0]
    t.parent = None
    zs = [v.co.z for v in t.data.vertices]
    z0, z1 = min(zs), max(zs)
    k = TOOL_LEN / (z1 - z0)
    gz = z0 + (z1 - z0) * GRIP_T
    con = t.constraints.new('CHILD_OF')
    con.target = rig.a; con.subtarget = "RightHand"; con.set_inverse_pending = False
    con.inverse_matrix = Matrix.Identity(4)
    t.matrix_basis = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0))).to_4x4() @ Matrix.Scale(k, 4) @ Matrix.Translation((0, 0, -gz))
    return t


tool = attach_tool()
low_local = min((v.co for v in tool.data.vertices), key=lambda c: c.x)   # 머리 한쪽 날 끝(손 뼈 X 반대쪽 = 아래)
worst = 0.0
for f in range(N + 1):
    _, _, _, arms, _, axis = body(f); sc.frame_set(f)
    if BLEND <= f <= RET:
        for s in ("Left", "Right"):
            worst = max(worst, (PB[s + "Hand"].head - arms[s][0]).length)
    if f in (0, THRUST, THRUST + 1, RET, RET2, N):
        bpy.context.view_layer.update()
        low = tool.matrix_world @ low_local
        print("FRAME %2d 오른손목 %s · 왼손목 %s · 아래 날 끝 %s · 긴 축 %s" % (
            f, tuple(round(v, 3) for v in PB["RightHand"].head), tuple(round(v, 3) for v in PB["LeftHand"].head),
            tuple(round(v, 3) for v in low), tuple(round(v, 2) for v in L.ax(PB["RightHand"].matrix))))
print("GRIP 손목-목표 최대 %.2fcm (f%d~%d) · 타격 f%d/%d장 = %.2f초 · 돌아오기 f%d~%d · 길이 %.2f초 · 곡괭이 몸 공간 길이 %.3f" % (
    worst * 100, BLEND, RET, THRUST, N, THRUST / L.FPS, RET, N, N / L.FPS, TOOL_LEN))
rig.heights(N)
# 내보낼 땐 곡괭이를 빼고 몸만 — 반입 스크립트는 몸 클립만 받는다.
bpy.data.objects.remove(tool, do_unlink=True)
rig.a.select_set(True)
bpy.context.view_layer.objects.active = rig.a
rig.export(args[0])
if len(args) > 1:
    tool = attach_tool()
    base_world = rig.mesh_world
    rig.mesh_world = lambda: base_world() + [tool.matrix_world @ v.co for v in tool.data.vertices]

    def show_tool(scene, *_):
        tool.hide_render = False

    bpy.app.handlers.frame_change_post.append(show_tool)
    rig.sheet(N, args[1])
