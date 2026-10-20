"""레퍼런스 항구·배 메시 — Blender 헤드리스로 만든다 (반입 `import_tree_meshes.py` HARBOR · 배치 `tools/unreal/setup_harbor.py`).

  ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/blender/make_harbor_ships.py
      → Saved/Generated/<이름>.obj (+ .mtl)  · Saved/Generated/harbor_preview/<이름>.png · all.png
  뒤에 `-- --no-render` 를 붙이면 OBJ 만 만든다.

형식: 단위 cm, Z 위, 배는 뱃머리 +X · 수선 z 0, 부두·잔교는 +X 가 바다 · 윗면 z 15 ·
밑 z -150. 재질 슬롯 이름(usemtl)은 `setup_harbor.py` SLOT_COLORS 키와 같다.

만드는 법: 선체는 단면(초타원 빌지 + 방현 띠 + 뱃전 벽 + 갑판 캠버)을 18~24 개 스테이션으로 로프트한다. 스테이션마다
폭·용골·갑판 높이를 매끈한 함수로 주어 선수 휘어오름(sheer)·선수 들림·고물 판이 곡면으로 이어진다. 상자형은 전부
베벨, 창은 불리언으로 판 홈 + 안쪽 유리판, 난간·밧줄·돛대 줄은 관(tube). 법선: 40° 넘는 모서리만 날카롭게 +
면적 가중 법선(WeightedNormal) — 베벨 덕에 큰 면이 평평하게 읽히고 모서리는 부드럽게 빛을 받는다.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(ROOT, "Saved", "Generated")
PREVIEW_DIR = os.path.join(OUT_DIR, "harbor_preview")
DECK_TOP = 15.0       # 부두 윗면 = 뭍 +15 (DESIGN 8.14)
BOTTOM = -150.0       # 부두·잔교 밑(물에 박힘)
RIM = 18.0            # 부두 빨간 테두리 폭
SHARP_DEG = 40.0      # 이보다 꺾인 모서리만 날카롭게

# 레퍼런스 색(steam_10·11·17 에서 뽑음, sRGB). 미리보기 렌더 전용 — 언리얼 쪽 색은 반입 스크립트가 입힌다.
PALETTE = {
    "Hull": "E3D3A4", "Dark": "34323A", "Green": "2F7D52", "Cream": "EFE5CD", "Glow": "FFD27A",
    "White": "F4F2EC", "Blue": "3D6FD0", "Tan": "C38D5A", "Glass": "2F4763", "Navy": "22335F",
    "Orange": "F08B2F", "Concrete": "CBC6BE", "Wood": "C99560", "Yellow": "E8C23F", "Cardboard": "D8B67A",
    "Tape": "EDE3C8", "Metal": "7D838A", "Mesh": "BCC1C6", "Solar": "28337A",
}


# ───────────────────────────── 기하 도구 ─────────────────────────────

def _hex(h):
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def loft(rings, slot_fn, cap0=None, cap1=None, closed=True):
    """같은 점 개수의 고리들을 차례로 잇는다. slot_fn(i, j) = 고리 i→i+1, 점 j→j+1 칸의 슬롯 번호. cap = 끝 면 슬롯."""
    bm = bmesh.new()
    vs = [[bm.verts.new(p) for p in r] for r in rings]
    n = len(rings[0])
    for i in range(len(vs) - 1):
        for j in range(n if closed else n - 1):
            k = (j + 1) % n
            f = bm.faces.new((vs[i][j], vs[i][k], vs[i + 1][k], vs[i + 1][j]))
            f.material_index = slot_fn(i, j)
    for ring, cap in ((vs[0], cap0), (vs[-1], cap1)):
        if cap is not None:
            bm.faces.new(ring).material_index = cap
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.05)
    return bm


def rounded_rect(x0, x1, y0, y1, r, seg=4):
    """둥근 모서리 사각형 둘레(반시계)."""
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(seg + 1):
            a = math.radians(a0 + 90.0 * i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


class Model:
    """한 메시. 부품 bmesh 를 합쳐 두었다가 finish() 에서 삼각형화·날카로운 모서리·UV 를 넣고 오브젝트로 만든다."""

    def __init__(self, name, slots):
        self.name, self.slots = name, list(slots)
        self.bm = bmesh.new()

    def s(self, slot):
        if slot not in self.slots:
            self.slots.append(slot)
        return self.slots.index(slot)

    # ── 합치기 ──
    def add(self, bm, slot=None, M=None):
        if slot is not None:
            for f in bm.faces:
                f.material_index = self.s(slot)
        if M is not None:
            bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        vm = {v: self.bm.verts.new(v.co) for v in bm.verts}
        for f in bm.faces:
            try:
                self.bm.faces.new([vm[v] for v in f.verts]).material_index = f.material_index
            except ValueError:
                pass
        bm.free()

    # ── 기본 도형 ──
    def box_bm(self, x0, x1, y0, y1, z0, z1, r=0.0, seg=1):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            v.co = Vector((x0 + (v.co.x + 0.5) * (x1 - x0), y0 + (v.co.y + 0.5) * (y1 - y0), z0 + (v.co.z + 0.5) * (z1 - z0)))
        if r > 0:
            r = min(r, 0.49 * min(x1 - x0, y1 - y0, z1 - z0))
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=r, segments=seg, profile=0.5, affect='EDGES', clamp_overlap=True)
        return bm

    def box(self, slot, x0, x1, y0, y1, z0, z1, r=0.0, seg=1, M=None):
        self.add(self.box_bm(x0, x1, y0, y1, z0, z1, r, seg), slot, M)

    def prism_bm(self, footprint, z0, z1, r=0.0, seg=1, top=None):
        """바닥 다각형을 세운 기둥. top = 윗면 다각형(같은 점 수) — 앞이 기운 선실용."""
        top = top or footprint
        bm = loft([[(x, y, z0) for x, y in footprint], [(x, y, z1) for x, y in top]], lambda i, j: 0, 0, 0)
        if r > 0:
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=r, segments=seg, profile=0.5, affect='EDGES', clamp_overlap=True)
        return bm

    def lathe(self, slot, profile, sides=12, at=(0, 0, 0), M=None, closed=False):
        """(반지름, 높이) 윤곽을 Z 축으로 돌린다. 반지름 0 인 끝점은 평평한 뚜껑이 된다."""
        cap0 = cap1 = None
        prof = list(profile)
        if not closed:
            if prof[0][0] <= 0.0:
                prof.pop(0)
                cap0 = 0
            if prof[-1][0] <= 0.0:
                prof.pop()
                cap1 = 0
        rings = [[(r * math.cos(math.tau * k / sides), r * math.sin(math.tau * k / sides), z) for k in range(sides)]
                 for r, z in prof]
        if closed:
            rings.append(rings[0])
        bm = loft(rings, lambda i, j: 0, cap0, cap1)
        self.add(bm, slot, (M or Matrix.Identity(4)) if M is not None else Matrix.Translation(Vector(at)))

    def tube(self, slot, path, r, sides=6):
        """점 목록을 따라가는 관 — 난간·밧줄·돛대 줄·붐."""
        path = [Vector(p) for p in path]
        rings = []
        for i, p in enumerate(path):
            t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
            up = Vector((0, 0, 1)) if abs(t.z) < 0.9 else Vector((1, 0, 0))
            a = t.cross(up).normalized()
            b = t.cross(a)
            rings.append([p + (a * math.cos(math.tau * k / sides) + b * math.sin(math.tau * k / sides)) * r
                          for k in range(sides)])
        self.add(loft(rings, lambda i, j: 0, 0, 0), slot)

    def plate(self, slot, corners, normal, t=1.5, out=0.5):
        """네 꼭짓점 판(창유리·태양광판) — 면에서 out 만큼 띄우고 두께 t."""
        n = Vector(normal).normalized()
        a = [Vector(c) + n * out for c in corners]
        b = [c + n * t for c in a]
        self.add(loft([a, b], lambda i, j: 0, 0, 0), slot)

    def torus(self, slot, R, r, center, axis='Y', seg=10, sides=6):
        prof = [(R + r * math.cos(math.tau * k / sides), r * math.sin(math.tau * k / sides)) for k in range(sides)]
        rot = {'X': Matrix.Rotation(math.pi / 2, 4, 'Y'), 'Y': Matrix.Rotation(math.pi / 2, 4, 'X'), 'Z': Matrix.Identity(4)}[axis]
        self.lathe(slot, prof, seg, M=Matrix.Translation(Vector(center)) @ rot, closed=True)

    # ── 창 ──
    def windowed(self, body_bm, body_slot, windows, pane_slot, depth=4.0, rad=4.0):
        """body 를 불리언으로 창 홈을 파서 합친다. windows = [(중심 x,y,z, 바깥 법선 (nx,ny), 폭, 높이)].
        홈 안쪽 벽은 body 색, 바닥에 pane_slot 유리판."""
        for f in body_bm.faces:
            f.material_index = self.s(body_slot)
        cut = bmesh.new()
        panes = []
        for c, (nx, ny), w, h in windows:
            nrm = Vector((nx, ny, 0)).normalized()
            tan = Vector((-nrm.y, nrm.x, 0))
            M = Matrix.Translation(Vector(c)) @ Matrix(((nrm.x, tan.x, 0, 0), (nrm.y, tan.y, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
            one = self.box_bm(-depth, 20.0, -w / 2, w / 2, -h / 2, h / 2, rad, 1)
            bmesh.ops.transform(one, matrix=M, verts=one.verts)
            vm = {v: cut.verts.new(v.co) for v in one.verts}
            for f in one.faces:
                cut.faces.new([vm[v] for v in f.verts]).material_index = self.s(body_slot)
            one.free()
            back = Vector(c) - nrm * (depth - 0.4)
            panes.append([back + tan * (-w / 2) + Vector((0, 0, -h / 2)), back + tan * (w / 2) + Vector((0, 0, -h / 2)),
                          back + tan * (w / 2) + Vector((0, 0, h / 2)), back + tan * (-w / 2) + Vector((0, 0, h / 2))])
        self.add(_boolean_difference(body_bm, cut, self.slots))
        pi = self.s(pane_slot)
        for q in panes:
            vs = [self.bm.verts.new(p) for p in q]
            self.bm.faces.new(vs).material_index = pi

    # ── 마무리 ──
    def finish(self):
        bm = self.bm
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.01)
        bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=0.01)
        bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
        thr = math.radians(SHARP_DEG)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            e.smooth = not (len(e.link_faces) != 2 or e.calc_face_angle(0.0) > thr)
        uv = bm.loops.layers.uv.new("UVMap")
        for f in bm.faces:
            ax = max(range(3), key=lambda k: abs(f.normal[k]))
            u_, v_ = {0: (1, 2), 1: (0, 2), 2: (0, 1)}[ax]
            for lp in f.loops:
                lp[uv].uv = (lp.vert.co[u_] / 500.0, lp.vert.co[v_] / 500.0)
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for slot in self.slots:
            me.materials.append(material(slot))
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        wn = ob.modifiers.new("WeightedNormal", 'WEIGHTED_NORMAL')
        wn.keep_sharp = True
        wn.mode = 'FACE_AREA'
        return ob


def _boolean_difference(body_bm, cut_bm, slots):
    mats = [material(s) for s in slots]
    obs = []
    for nm, b in (("_body", body_bm), ("_cut", cut_bm)):
        me = bpy.data.meshes.new(nm)
        b.to_mesh(me)
        b.free()
        for m in mats:
            me.materials.append(m)
        ob = bpy.data.objects.new(nm, me)
        bpy.context.scene.collection.objects.link(ob)
        obs.append(ob)
    mod = obs[0].modifiers.new("cut", 'BOOLEAN')
    mod.object, mod.operation, mod.solver, mod.material_mode = obs[1], 'DIFFERENCE', 'EXACT', 'INDEX'
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obs[0].evaluated_get(dg)
    out = bmesh.new()
    out.from_mesh(ev.to_mesh())
    ev.to_mesh_clear()
    for ob in obs:
        me = ob.data
        bpy.data.objects.remove(ob)
        bpy.data.meshes.remove(me)
    return out


_MATS = {}


def material(slot):
    if slot in _MATS:
        return _MATS[slot]
    m = bpy.data.materials.new(slot)
    m.use_nodes = True
    col = _hex(PALETTE.get(slot, "999999"))
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*col, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.75 if slot not in ("Glass", "Solar") else 0.25
    if slot == "Glow":
        bsdf.inputs["Emission Color"].default_value = (*col, 1.0)
        bsdf.inputs["Emission Strength"].default_value = 1.6
    m.diffuse_color = (*col, 1.0)
    _MATS[slot] = m
    return m


# ───────────────────────────── 선체 ─────────────────────────────

def hull(m, x0, L, B, keel, deck0, sheer, bul, wall, n, bands, rail, bul_slot, cap_slot, inner_slot, deck_slot,
         bow_start=0.6, bow_p=1.8, stern_hw=0.85, stern_round=0.0, keel_bow=None, keel_stern=None, stern_rise=0.0, camber=3.0,
         N=18, K=6, tip=4.0, transom_slot=None):
    """단면 로프트 선체. bands = [(윗선 z | None, 슬롯)] 아래→위. rail = (높이, 돌출, 슬롯) 갑판 높이 방현 띠.
    반환: (deck_z(x), hw(x)) — 선실·소품을 갑판에 앉힐 때 쓴다."""
    rh, ro, rslot = rail

    def params(u):
        hw = B * (stern_hw + (1 - stern_hw) * math.sin(min(u / 0.25, 1.0) * math.pi / 2))
        if u > bow_start:
            s = (u - bow_start) / (1 - bow_start)
            hw *= max(0.0, 1 - s ** bow_p) ** (1 / bow_p)
        if u < stern_round:                               # 둥근 고물 — 고물 판이 좁아지며 곡면으로 닫힌다
            hw *= max(0.5, math.sqrt(1 - ((stern_round - u) / stern_round) ** 2))
        hw = max(hw, tip)
        deck = deck0 + sheer * max(0.0, (u - 0.35) / 0.65) ** 2 + stern_rise * max(0.0, (0.15 - u) / 0.15) ** 2
        k = keel
        kb = bow_start - 0.1
        if keel_bow is not None and u > kb:
            k += (keel_bow - keel) * ((u - kb) / (1 - kb)) ** 2
        if keel_stern is not None and u < 0.12:
            k += (keel_stern - keel) * ((0.12 - u) / 0.12) ** 1.5
        return hw, k, deck

    def band_slot(z):
        for top, slot in bands:
            if top is None or z < top:
                return m.s(slot)
        return m.s(bands[-1][1])

    def half(hw, k, d):
        zr0, zr1 = d - rh * 0.5, d + rh * 0.5
        H = d - k
        th_max = math.acos(min(1.0, ((d - zr0) / H) ** (n / 2)))
        zs = [d - H * math.cos(th_max * i / K) ** (2 / n) for i in range(1, K + 1)]
        for top, _ in bands[:-1]:
            zs.append(min(max(top, k + 0.6), zr0 - 0.6))
        zs.sort()
        pts, sl = [(0.0, k)], []
        for z in zs:
            c = max(0.0, (d - z) / H)
            y = hw * max(0.0, 1 - c ** n) ** (1 / n)
            sl.append(band_slot((pts[-1][1] + z) * 0.5))
            pts.append((y, z))
        hw = pts[-1][0]
        for fy, fz in ((0.6, 0.12), (1.0, 0.35), (1.0, 0.65), (0.6, 0.88)):
            sl.append(m.s(rslot))
            pts.append((hw + ro * fy, zr0 + rh * fz))
        w = min(wall, hw * 0.5)
        ch = min(2.5, w * 0.3)
        seq = [((hw, zr1), rslot), ((hw, d + bul - ch), bul_slot), ((hw - ch, d + bul), cap_slot),
               ((hw - w + ch, d + bul), cap_slot), ((hw - w, d + bul - ch), cap_slot), ((hw - w, d), inner_slot),
               (((hw - w) * 0.5, d + camber * 0.75), deck_slot), ((0.0, d + camber), deck_slot)]
        for p, slot in seq:
            sl.append(m.s(slot))
            pts.append(p)
        return pts, sl

    rings, sls = [], []
    for i in range(N):
        u = 0.5 - 0.5 * math.cos(math.pi * i / (N - 1))
        hw, k, d = params(u)
        pts, sl = half(hw, k, d)
        x = x0 + L * u
        right = [(x, y, z) for y, z in pts]
        left = [(x, -y, z) for y, z in reversed(pts[1:-1])]
        rings.append(right + left)
        sls.append(sl + list(reversed(sl)))      # 칸 색은 스테이션마다 — 이물에선 띠가 용골 위로 몰려 달라진다
    transom = m.s(transom_slot or bands[-1][1])
    m.add(loft(rings, lambda i, j: sls[i][j], transom, m.s(bands[-1][1])))

    def at(x):
        u = min(max((x - x0) / L, 0.0), 1.0)
        return params(u)

    return (lambda x: at(x)[2] + camber), (lambda x: at(x)[0])


# ───────────────────────────── 배 ─────────────────────────────

def tugboat():
    """steam_10·11 예인선 — 620×312. 뾰족한 이물·둥근 고물, 빨간 선체·검은 방현 띠와 뱃전 윗면·빨간 안벽·초록 갑판.
    크림 조타실은 고물 쪽 — 뒤 세 면(뒷면·양 비스듬 면)에 노랗게 빛나는 큰 창, 둘레로 튀어나온 지붕, 지붕 위 둥근
    통과 돛대(가로대·꼭대기 등), 돛대 줄은 이물·고물로. 앞갑판 크림 해치, 이물 난간, 양 뱃전 폐타이어."""
    m = Model("Tugboat_01", ["Hull", "Navy", "Green", "Cream", "Glow"])
    deck, hw = hull(m, -310, 620, 150, -38, 70, 30, 24, 9, 2.6, [(None, "Hull")], (16, 6, "Navy"),
                    "Hull", "Navy", "Hull", "Green", bow_start=0.55, bow_p=1.9, stern_hw=0.9, stern_round=0.14,
                    keel_bow=22, keel_stern=10, stern_rise=4, N=18, K=5)
    # 조타실 — 앞면 x 20 평평, 뒤는 깎은 세 면(x -150 → -205)
    fp = [(20, -94), (20, 94), (-150, 94), (-205, 50), (-205, -50), (-150, -94)]
    z0, z1 = deck(-100) - 6, 232
    body = m.prism_bm(fp, z0, z1, 7, 2)
    wz = 170
    wins = [((-205, 0, wz), (-1, 0), 70, 72)]
    for s in (-1, 1):
        wins.append(((-177.5, s * 72, wz), (-44, s * 55), 50, 72))       # 비스듬한 뒤 모서리
        wins.append(((-95, s * 94, wz + 6), (0, s), 54, 52))
        wins.append(((20, s * 45, wz + 10), (1, 0), 56, 40))              # 앞을 내다보는 창
    m.windowed(body, "Cream", wins, "Glow")
    m.box("Navy", -40, 0, 94, 96.5, z0 + 12, z0 + 122, 3)                     # 오른쪽 문
    roof = [(34, -106), (34, 106), (-156, 106), (-218, 56), (-218, -56), (-156, -106)]
    m.add(m.prism_bm(roof, z1, z1 + 15, 6, 2), "Cream")
    top = z1 + 15
    m.lathe("Cream", [(0, top - 2), (32, top - 2), (32, top + 16), (27, top + 25), (14, top + 30), (0, top + 31)], 14, at=(-120, 0, 0))
    # 돛대 — 조타실 지붕 앞쪽, 가로대·꼭대기 등, 줄은 이물 끝과 고물 양쪽으로
    mx = -10
    m.lathe("Cream", [(0, top - 2), (8, top - 2), (6, top + 240), (0, top + 242)], 8, at=(mx, 0, 0))
    m.box("Cream", mx - 5, mx + 5, -58, 58, top + 180, top + 190, 2)
    m.lathe("Glow", [(0, top + 240), (6, top + 242), (6, top + 252), (0, top + 256)], 8, at=(mx, 0, 0))
    mt = Vector((mx, 0, top + 232))
    for p in ((300, 0, deck(300) + 40), (-214, 52, top), (-214, -52, top)):
        m.tube("Dark", [mt, p], 1.2, 4)
    # 앞갑판 해치
    d = deck(130)
    m.box("Cream", 70, 200, -66, 66, d - 6, d + 32, 8, 2)
    m.box("Cream", 78, 192, -58, 58, d + 32, d + 38, 3)
    # 이물 난간 — 이물 끝을 감싸는 짧은 난간
    path = []
    for i in range(4):
        x = 215 + 80 * i / 3
        path.append((x, hw(x) - 8, deck(x) + 24 + 30))
    full = path + [(300, 0, deck(300) + 54)] + [(x, -y, z) for x, y, z in reversed(path)]
    m.tube("Cream", full, 2.2, 5)
    for x, y, z in path[::3]:
        for s in (-1, 1):
            m.tube("Cream", [(x, s * y, deck(x) + 20), (x, s * y, z)], 1.8, 4)
    # 계선주 — 고물 양쪽·이물
    for x, y in ((-265, 70), (-265, -70), (235, 0)):
        dz = deck(x)
        m.lathe("Dark", [(0, dz - 2), (11, dz - 2), (11, dz + 22), (15, dz + 25), (15, dz + 30), (0, dz + 30)], 8, at=(x, y, 0))
    # 뱃전 폐타이어 — 뱃전 밖에 세로로 걸림
    for x in (-170, -20, 130):
        for s in (-1, 1):
            m.torus("Dark", 17, 6.5, (x, s * (hw(x) + 12), deck(x) - 6), 'Y', 8, 5)
    return m.finish()


def motorboat():
    """스피드보트 — 610×220. 흰 V 선체·파란 띠 둘·파란 방현 띠, 티크 갑판, 감싸는 앞유리, 가운데 조종대,
    크림 버킷 좌석 둘·뒷벤치, 뒤 선외기(검정 몸통·흰 뚜껑), 이물 난간."""
    m = Model("Motorboat_01", ["White", "Blue", "Tan", "Glass", "Cream", "Dark"])
    deck, hw = hull(m, -270, 575, 105, -28, 56, 24, 12, 6, 1.35,
                    [(6, "Blue"), (26, "White"), (36, "Blue"), (None, "White")], (10, 5, "Blue"),
                    "White", "White", "White", "Tan", bow_start=0.42, bow_p=1.6, stern_hw=0.93, keel_bow=44,
                    keel_stern=-16, N=16, K=5, tip=3)
    # 앞유리 — 이물 쪽으로 볼록한 호, 뒤로 25° 기움
    arc = []
    for i in range(9):
        a = math.radians(-70 + 140 * i / 8)
        x = 70 + 38 * math.cos(a)
        y = (hw(70) - 8) * math.sin(a)
        arc.append((x, y))
    rings = []
    for x, y in arc:
        z = deck(x) + 12
        rings.append([Vector((x, y, z)), Vector((x - 16, y * 0.97, z + 36)), Vector((x - 18, y * 0.97, z + 36)), Vector((x - 2, y, z))])
    m.add(loft(rings, lambda i, j: 0, 0, 0), "Glass")
    m.tube("White", [(x - 17, y * 0.97, deck(x) + 49) for x, y in arc], 1.8, 5)
    # 조종대·핸들
    d = deck(40)
    m.box("White", 15, 70, -38, 38, d - 4, d + 50, 8, 2)
    m.box("Dark", 22, 34, -26, 26, d + 50, d + 53, 2)
    m.torus("Dark", 11, 2, (10, -18, d + 44), 'X', 10, 4)
    # 버킷 좌석 둘
    d = deck(-30)
    for y in (-48, 48):
        m.box("Cream", -50, -8, y - 24, y + 24, d - 4, d + 24, 7, 2)
        m.box("Cream", -60, -44, y - 24, y + 24, d + 16, d + 62, 7, 2, M=_rot_about((-52, y, d + 20), 'Y', -10))
    # 뒷벤치(엔진 앞)
    d = deck(-220)
    m.box("Cream", -235, -175, -hw(-220) + 14, hw(-220) - 14, d - 4, d + 26, 8, 2)
    m.box("Cream", -256, -236, -hw(-240) + 14, hw(-240) - 14, d + 14, d + 52, 8, 2)
    # 선외기
    d = deck(-270)
    m.box("Dark", -300, -262, -23, 23, d + 2, d + 58, 11, 2)
    m.box("White", -302, -260, -25, 25, d + 44, d + 62, 9, 2)
    m.box("Blue", -300.5, -261.5, -24, 24, d + 30, d + 36, 2)
    m.box("Dark", -292, -278, -7, 7, -26, d + 4, 3)
    m.box("Dark", -305, -270, -14, 14, -18, -13, 2)
    m.box("Dark", -266, -258, -14, 14, d - 6, d + 8, 2)
    # 이물 난간 — 뱃전 따라 이물 끝까지
    path = []
    for i in range(7):
        x = 150 + 138 * i / 6
        path.append((x, hw(x) - 5, deck(x) + 34))
    full = path + [(296, 0, deck(296) + 34)] + [(x, -y, z) for x, y, z in reversed(path)]
    m.tube("White", full, 1.8, 5)
    for x, y, z in path[::2]:
        for s in (-1, 1):
            m.tube("White", [(x, s * y, deck(x) + 10), (x, s * y, z)], 1.4, 4)
    return m.finish()


def _rot_about(p, axis, deg):
    p = Vector(p)
    return Matrix.Translation(p) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-p)


def _tapered(front, hw, back, nose=110.0, nose_w=0.45):
    """선실 바닥 모양 — 뒤는 네모, 앞은 비스듬히 좁아지는 코."""
    return [(back, -hw), (front - nose, -hw), (front, -hw * nose_w), (front, hw * nose_w), (front - nose, hw), (back, hw)]


def yacht():
    """큰 모터 요트 — 1200×390. 흰 선체·남색 수선/창 띠, 티크 갑판, 앞이 기운 2층 선실(짙은 통창), 하드탑·레이더
    아치, 이물 난간, 앞갑판 선탠 매트, 뒤 수영 발판과 조종실 소파."""
    m = Model("Yacht_01", ["White", "Navy", "Tan", "Glass", "Dark", "Cream"])
    deck, hw = hull(m, -560, 1160, 190, -46, 108, 44, 16, 8, 1.8,
                    [(14, "Navy"), (62, "White"), (82, "Navy"), (None, "White")], (10, 5, "White"),
                    "White", "White", "White", "Tan", bow_start=0.52, bow_p=1.7, stern_hw=0.94, keel_bow=86,
                    keel_stern=4, N=22, K=6, tip=4)
    m.box("Tan", -600, -556, -150, 150, 22, 36, 4, 1)                  # 수영 발판
    m.box("White", -566, -552, -150, 150, 0, 36, 3, 1)
    # 1층 선실 — 앞이 70 뒤로 기운 창
    d1 = deck(-330) - 4
    f0, f1 = _tapered(270, 162, -340), _tapered(195, 150, -312, 95)
    top1 = 232
    lvl1 = m.prism_bm(f0, d1, top1, 6, 2, top=f1)
    wins = []
    for s in (-1, 1):
        for x in (-240, -110, 20):
            wins.append(((x, s * 157, 180), (0, s), 112, 56))
    wins.append(((-326, 0, 172), (-1, 0), 150, 90))
    m.windowed(lvl1, "White", wins, "Glass", depth=5, rad=6)
    _sloped_glass(m, f0, f1, d1, top1, 0.30, 0.86, 0.9)
    # 2층(플라이브리지)
    f2, f3 = _tapered(120, 128, -240, 90), _tapered(70, 120, -225, 70)
    top2 = 305
    lvl2 = m.prism_bm(f2, top1, top2, 5, 2, top=f3)
    wins = [((x, s * 124, 268), (0, s), 90, 36) for s in (-1, 1) for x in (-170, -60)]
    m.windowed(lvl2, "White", wins, "Glass", depth=4, rad=5)
    _sloped_glass(m, f2, f3, top1, top2, 0.25, 0.85, 0.85)
    # 하드탑·기둥·레이더 아치
    m.add(m.prism_bm(_tapered(150, 140, -380, 80, 0.6), top2 + 22, top2 + 36, 5, 2), "White")
    for x in (-360, -250):
        for s in (-1, 1):
            m.tube("White", [(x, s * 120, top2 - 10), (x, s * 120, top2 + 24)], 4, 8)
    arch = [(-150, s * 90, top2 + 36) for s in (-1,)] + [(-165, -60, top2 + 92), (-170, 0, top2 + 100),
                                                         (-165, 60, top2 + 92), (-150, 90, top2 + 36)]
    m.tube("White", arch, 7, 8)
    m.lathe("White", [(0, top2 + 100), (22, top2 + 100), (22, top2 + 108), (16, top2 + 118), (0, top2 + 121)], 12, at=(-170, 0, 0))
    m.tube("Dark", [(-170, -40, top2 + 96), (-170, -40, top2 + 150)], 1.5, 4)
    m.tube("Dark", [(-170, 40, top2 + 96), (-170, 40, top2 + 135)], 1.5, 4)
    # 앞갑판 선탠 매트(비스듬)
    d = deck(380)
    m.box("Cream", 300, 470, -80, 80, d - 4, d + 14, 8, 2, M=_rot_about((300, 0, d), 'Y', -5))
    m.box("White", 285, 300, -84, 84, d - 4, d + 12, 4, 1)
    # 조종실 소파(U자)·탁자
    d = deck(-470)
    m.box("Cream", -545, -515, -140, 140, d - 4, d + 38, 7, 2)
    for s in (-1, 1):
        m.box("Cream", -515, -400, s * 140 - 30 * s - 15, s * 140 - 30 * s + 15, d - 4, d + 34, 6, 2)
    m.box("Tan", -480, -420, -40, 40, d + 26, d + 32, 3, 1)
    m.tube("Tan", [(-450, 0, d), (-450, 0, d + 26)], 5, 6)
    # 이물 난간
    path = []
    for i in range(9):
        x = 60 + 520 * i / 8
        path.append((x, hw(x) - 7, deck(x) + 16 + 42))
    full = path + [(585, 0, deck(585) + 58)] + [(x, -y, z) for x, y, z in reversed(path)]
    m.tube("White", full, 2.2, 5)
    for x, y, z in path[::2]:
        for s in (-1, 1):
            m.tube("White", [(x, s * y, deck(x) + 14), (x, s * y, z)], 1.6, 4)
    # 닻·계선주
    m.box("Dark", 560, 590, -10, 10, deck(575) - 30, deck(575) - 5, 3, 1)
    for x in (-520, 440):
        for s in (-1, 1):
            dz = deck(x)
            m.lathe("Dark", [(0, dz - 2), (7, dz - 2), (7, dz + 12), (10, dz + 14), (10, dz + 18), (0, dz + 18)], 8, at=(x, s * (hw(x) - 24), 0))
    return m.finish()


def _sloped_glass(m, f0, f1, z0, z1, t0, t1, wfrac):
    """앞이 기운 선실의 코 세 면(비스듬 앞면·양 모서리)에 짙은 통창 판."""
    def lerp(a, b, t):
        return Vector((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, 0))
    for ia, ib in ((2, 3), (1, 2), (3, 4)):
        corners = []
        for t, (i, j) in ((t0, (ia, ib)), (t0, (ib, ia)), (t1, (ib, ia)), (t1, (ia, ib))):
            z = z0 + (z1 - z0) * t
            p = lerp(f0[i], f1[i], t) + Vector((0, 0, z))
            q = lerp(f0[j], f1[j], t) + Vector((0, 0, z))
            corners.append(p + (q - p) * (1 - wfrac) * 0.5)
        a, b, c = corners[0], corners[1], corners[3]
        nrm = (b - a).cross(c - a)
        mid = sum(corners, Vector()) / 4
        if nrm.dot(Vector((mid.x - (f0[0][0] + f0[2][0]) * 0.5, mid.y, 0))) < 0:
            nrm = -nrm
        m.plate("Glass", corners, nrm, 1.5, 0.6)


def ship():
    """대형 화물선 — 2600×910 · 높이 ~1150. 빨간 선체 하단·흰 띠·초록 상단, 빨간 갑판·초록 해치, 3열 컨테이너,
    뒤쪽 흰 4층 선교(층마다 창, 조타실 날개), 빨강·흰·검정 띠 굴뚝, 주황 폐쇄형 구명정·흰 대빗, 좌현 크레인 둘,
    이물 돛대·닻 구멍, 구상 선수."""
    m = Model("Ship_01", ["Hull", "Navy", "Dark", "White", "Green", "Cream", "Glow", "Glass", "Blue", "Orange"])
    deck, hw = hull(m, -1300, 2600, 450, -110, 300, 80, 45, 14, 4.0,
                    [(24, "Navy"), (190, "Hull"), (212, "White"), (None, "Green")], (16, 5, "White"),
                    "Green", "White", "Green", "Hull", bow_start=0.70, bow_p=1.9, stern_hw=0.80, keel_bow=70,
                    keel_stern=-20, stern_rise=10, N=24, K=7, tip=6, transom_slot="Green")
    # 선교 — 세 층 + 조타실, 컨테이너(2단 856)보다 높게
    base = deck(-1000) - 6
    levels = [(-1185, -800, 385, base, 460), (-1165, -815, 365, 460, 620), (-1145, -830, 345, 620, 780)]
    for li, (xa, xb, h, za, zb) in enumerate(levels):
        body = m.box_bm(xa, xb, -h, h, za, zb, 9, 2)
        wz = (za + zb) * 0.5 + 8
        wins = [((xb, -h + (i + 0.5) * 2 * h / 7, wz), (1, 0), 60, 52) for i in range(7)]
        for i in range(5):
            x = xa + (i + 0.5) * (xb - xa) / 5
            wins += [((x, s * h, wz), (0, s), 50, 52) for s in (-1, 1)]
        wins.append(((xa, 0, wz), (-1, 0), 90, 52))
        m.windowed(body, "White", wins, "Glow" if li < 2 else "Glass", depth=5, rad=5)
        m.box("White", xa - 6, xb + 6, -h - 6, h + 6, zb - 2, zb + 6, 3, 1)   # 층 사이 처마
    # 조타실 + 날개
    za, zb = 780, 915
    body = m.box_bm(-1060, -845, -445, 445, za, zb, 9, 2)
    wins = [((-845, -390 + 780 * (i + 0.5) / 9, 850), (1, 0), 70, 60) for i in range(9)]
    wins += [((x, s * 445, 850), (0, s), 50, 54) for x in (-1000, -905) for s in (-1, 1)]
    m.windowed(body, "White", wins, "Glass", depth=5, rad=5)
    m.box("White", -1072, -833, -455, 455, zb, zb + 14, 5, 1)
    m.box("Dark", -990, -900, -40, 40, zb + 14, zb + 20, 2)
    m.tube("Cream", [(-950, 0, zb + 14), (-950, 0, zb + 200)], 7, 8)          # 선교 돛대
    m.box("Dark", -962, -938, -130, 130, zb + 140, zb + 152, 3, 1)             # 레이더 막대
    m.lathe("Glow", [(0, zb + 200), (8, zb + 202), (8, zb + 214), (0, zb + 218)], 8, at=(-950, 0, 0))
    # 굴뚝 — L3 지붕 위 뒤쪽, 뒤로 기운 둥근 네모 고리 로프트(빨강·흰 띠·빨강·검은 꼭대기)
    rings = []
    for z in (780, 975, 1010, 1095, 1150):
        lean = -(z - 780) * 0.12
        rings.append([(x + lean - 1100, y, z) for x, y in rounded_rect(-50, 50, -85, 85, 30, 3)])
    slots = [m.s("Hull"), m.s("White"), m.s("Hull"), m.s("Navy")]
    m.add(loft(rings, lambda i, j: slots[i], None, m.s("Dark")))
    # 구명정(폐쇄형, 주황) — 2층 옆, 1층 지붕 위에 걸림 + 대빗
    for s in (-1, 1):
        y = s * 415
        rings = []
        for i in range(9):
            t = i / 8
            x = -1100 + 200 * t
            rr = max(0.12, math.sin(math.pi * t) ** 0.55)
            rings.append([(x, y + 48 * rr * math.cos(a), 502 + 34 * rr * math.sin(a) * (1.25 if math.sin(a) > 0 else 1))
                          for a in (math.tau * k / 10 for k in range(10))])
        m.add(loft(rings, lambda i, j: 0, 0, 0), "Orange")
        m.box("White", -1050, -950, y - 14, y + 14, 538, 548, 5, 1)
        for x in (-1080, -920):
            m.tube("White", [(x, s * 372, 462), (x, s * 374, 585), (x, y, 580)], 5, 6)
            m.tube("Dark", [(x, y, 580), (x, y, 545)], 1.2, 4)
    # 컨테이너 — 두 베이 × 세 줄, 초록 해치 받침 위 1~2단
    colors = ["Blue", "Orange", "Tan", "Green", "White", "Orange", "Blue", "Yellow", "Cream", "Green", "Orange", "Blue"]
    bays = [(-470, [-255, 0, 255], [2, 2, 2]), (250, [-255, 0, 255], [2, 1, 2])]
    ci = 0
    for cx, cols, tiers in bays:
        zc = deck(cx + 305) + 38
        m.box("Green", cx - 318, cx + 318, -387, 387, deck(cx - 318) - 6, zc, 5, 1)
        for cy, nt in zip(cols, tiers):
            for t in range(nt):
                _container(m, colors[ci % len(colors)], cx, cy, zc + t * 259)
                ci += 1
    # 크레인 둘 — 베이 사이 중심선, 크림 받침·주황 운전실·고물 쪽으로 누운 지브·갈고리
    for cx in (-110, 640):
        d = deck(cx)
        m.lathe("Cream", [(0, d - 4), (30, d - 4), (26, d + 560), (36, d + 570), (0, d + 570)], 10, at=(cx, 0, 0))
        m.box("Orange", cx - 55, cx + 50, -45, 45, d + 570, d + 660, 10, 2)
        m.box("Glass", cx + 30, cx + 52, -35, 35, d + 605, d + 645, 3, 1)
        M = Matrix.Translation(Vector((cx - 20, 0, d + 610))) @ Matrix.Rotation(math.pi, 4, 'Z') @ Matrix.Rotation(math.radians(-22), 4, 'Y')
        m.box("Orange", 0, 560, -17, 17, -18, 18, 6, 1, M=M)
        tip = M @ Vector((560, 0, 0))
        m.tube("Dark", [tip, tip - Vector((0, 0, 150))], 1.5, 4)
        m.box("Dark", tip.x - 12, tip.x + 12, -12, 12, tip.z - 175, tip.z - 150, 3, 1)
    # 이물 — 돛대·등·닻 감개·닻 구멍·계선주
    d = deck(1160)
    m.tube("Cream", [(1160, 0, d), (1160, 0, d + 430)], 9, 8)
    m.box("Cream", 1150, 1170, -120, 120, d + 330, d + 344, 3, 1)
    m.lathe("Glow", [(0, d + 430), (10, d + 432), (10, d + 446), (0, d + 450)], 8, at=(1160, 0, 0))
    for s in (-1, 1):
        m.box("Dark", 1040, 1110, s * 110 - 40, s * 110 + 40, deck(1075) - 4, deck(1075) + 45, 10, 2)
        m.lathe("Dark", [(0, 0), (22, 0), (22, 6), (0, 6)], 10,
                M=Matrix.Translation(Vector((1120, s * (hw(1120) - 4), 245))) @ Matrix.Rotation(-s * math.pi / 2, 4, 'X'))
    for x in (-1250, -1200, 1000):
        for s in (-1, 1):
            dz = deck(x)
            m.lathe("Dark", [(0, dz - 2), (16, dz - 2), (16, dz + 30), (22, dz + 34), (22, dz + 42), (0, dz + 42)], 8,
                    at=(x, s * (hw(x) - 45), 0))
    return m.finish()


def _container(m, slot, cx, cy, z0):
    """20피트 컨테이너(610×244×259) — 모서리 깎은 상자 + 옆면 골 세 줄 + 문짝 쪽 짙은 틀."""
    m.box(slot, cx - 305, cx + 305, cy - 122, cy + 122, z0, z0 + 259, 4, 1)
    for s in (-1, 1):
        for x in (-170, 0, 170):
            m.box(slot, cx + x - 40, cx + x + 40, cy + s * 122 - 3 * (s < 0), cy + s * 122 + 3 * (s > 0), z0 + 20, z0 + 239)
    m.box("Dark", cx + 303, cx + 308, cy - 110, cy + 110, z0 + 14, z0 + 245)


# ───────────────────────────── 항구 ─────────────────────────────

def slab(m, x0, x1, y0, y1, r):
    """steam_10 부두 판 — 옅은 콘크리트 윗면, 빨간 테두리 띠(윗면 18 + 둥근 모서리 + 옆면), 물속 짙은 받침(12 안으로)."""
    prof = [(12, BOTTOM, "Dark"), (12, -30, "Dark"), (0, -30, "Wood"), (0, 5, "Wood"), (1.2, 10, "Wood"),
            (3.8, 13.6, "Wood"), (7.5, DECK_TOP, "Wood"), (RIM, DECK_TOP, None)]
    rings = []
    for inset, z, _ in prof:
        rr = max(r - inset, 1.0)
        rings.append([(x, y, z) for x, y in rounded_rect(x0 + inset, x1 - inset, y0 + inset, y1 - inset, rr, 5)])
    slots = [m.s(s) for _, _, s in prof[:-1]]
    m.add(loft(rings, lambda i, j: slots[i], m.s("Dark"), m.s("Concrete")))


def bollard(m, x, y):
    """계선주 — 넓은 받침, 허리가 잘록한 기둥, 버섯 머리."""
    z = DECK_TOP
    m.lathe("Dark", [(0, z - 1), (22, z - 1), (22, z + 4), (15, z + 6), (13, z + 22), (15, z + 30), (20, z + 33),
                     (20, z + 39), (16, z + 42), (0, z + 42)], 10, at=(x, y, 0))


def lamp(m, x, y):
    """steam_10 잔교 끝 태양광 가로등 — 크림 기둥·빨간 띠·크림 머리, 비스듬한 파란 태양광판(크림 테두리·격자)."""
    z = DECK_TOP
    m.box("Cream", x - 16, x + 16, y - 16, y + 16, z - 1, z + 7, 3, 1)
    m.lathe("Cream", [(0, z + 6), (8, z + 6), (7, z + 140), (0, z + 141)], 10, at=(x, y, 0))
    m.lathe("Navy", [(0, z + 38), (9, z + 38), (9, z + 54), (0, z + 54)], 10, at=(x, y, 0))
    m.box("Cream", x - 18, x + 18, y - 20, y + 20, z + 136, z + 154, 5, 2)
    M = _rot_about((x, y, z + 160), 'Y', -18)
    m.box("Cream", x - 34, x + 34, y - 42, y + 42, z + 154, z + 161, 3, 1, M=M)
    m.box("Solar", x - 30, x + 30, y - 38, y + 38, z + 161, z + 163, 1, 1, M=M)
    for gx in (-10, 10):
        m.box("Cream", x + gx - 0.8, x + gx + 0.8, y - 38, y + 38, z + 162.5, z + 163.5, M=M)
    for gy in (-19, 0, 19):
        m.box("Cream", x - 30, x + 30, y + gy - 0.8, y + gy + 0.8, z + 162.5, z + 163.5, M=M)


def quay(depth=500.0, length=2400.0):
    m = Model("Quay_01", ["Concrete", "Wood", "Dark"])
    slab(m, -depth * 0.5, depth * 0.5, -length * 0.5, length * 0.5, 50.0)
    for y in range(-1000, 1001, 400):
        bollard(m, depth * 0.5 - 45.0, float(y))
    return m.finish()


def pier(name, length, half_width, lamps):
    m = Model(name, ["Concrete", "Wood", "Dark", "Solar", "Cream", "Navy"])
    slab(m, 0.0, length, -half_width, half_width, 70.0)
    for x in range(250, int(length) - 100, 350):
        for s in (-1, 1):
            bollard(m, float(x), s * (half_width - 40.0))
    for lx, ly in lamps:
        lamp(m, lx, ly)
    return m.finish()


def drum(name, slot):
    """드럼통 — 지름 60 · 높이 88, 굴림 띠 둘, 위아래 말린 테, 오목한 뚜껑 + 마개."""
    m = Model(name, [slot])
    m.lathe(slot, [(0, 0), (28, 0), (30, 1.5), (30, 26), (31.6, 27.5), (31.6, 30.5), (30, 32), (30, 56), (31.6, 57.5),
                   (31.6, 60.5), (30, 62), (30, 86), (29.2, 88), (27.6, 88), (27, 86.2), (0, 86.2)], 16)
    m.lathe(slot, [(0, 85), (5, 85), (5, 89.5), (0, 89.5)], 8, at=(15, 6, 0))
    return m.finish()


def pallet():
    """나무 팔레트 두 장 겹침 — 120×100. 윗판 다섯(모서리 깎음), 받침 토막 아홉, 밑판 셋."""
    m = Model("Pallet_01", ["Wood"])
    for layer in range(2):
        z = layer * 14.5
        M = _rot_about((0, 0, 0), 'Z', 4 if layer else 0)
        for y in (-43, 0, 43):
            m.box("Wood", -60, 60, y - 7, y + 7, z, z + 2.5, M=M)
            for x in (-52, 0, 52):
                m.box("Wood", x - 7, x + 7, y - 7, y + 7, z + 2.5, z + 10.5, M=M)
        for i in range(5):
            x = -50 + 25 * i
            m.box("Wood", x - 9, x + 9, -50, 50, z + 10.5, z + 14, 1.2, 1, M=M)
    return m.finish()


def crates():
    """종이 상자 더미 — 셋이 두 단, 테이프가 윗면을 가로질러 옆으로 내려온다."""
    m = Model("Crates_01", ["Cardboard", "Tape"])
    for x, y, z, s, rot in ((0, 0, 0, 56, 0), (60, 6, 0, 50, 6), (26, 4, 56, 46, -8)):
        h = s * 0.5
        M = _rot_about((x, y, 0), 'Z', rot)
        m.box("Cardboard", x - h, x + h, y - h, y + h, z, z + s, 2.5, 1, M=M)
        m.box("Tape", x - 5, x + 5, y - h - 0.4, y + h + 0.4, z + s - 0.2, z + s + 0.4, M=M)
        for sy in (-1, 1):
            m.box("Tape", x - 5, x + 5, y + sy * (h + 0.4) - 0.3 * (sy > 0), y + sy * (h + 0.4) + 0.3 * (sy < 0) - (0.3 if sy > 0 else 0) + (0.3 if sy > 0 else 0),
                  z + s - 14, z + s, M=M)
    return m.finish()


def chain_fence(length=300.0, height=160.0):
    """철망 울타리 한 토막 — 둥근 기둥 둘(모자), 윗봉·밑줄, 45° 마름모 철망."""
    m = Model("ChainFence_01", ["Metal", "Mesh"])
    h = length * 0.5
    for x in (-h, h):
        m.lathe("Metal", [(0, 0), (6, 0), (4.5, 2), (4.5, height + 6), (5.5, height + 8), (3, height + 11), (0, height + 11.5)], 8, at=(x, 0, 0))
    m.tube("Metal", [(-h, 0, height), (h, 0, height)], 2.6, 6)
    m.tube("Metal", [(-h, 0, 8), (h, 0, 8)], 1.2, 4)
    xa, xb, za, zb = -h + 5, h - 5, 9.0, height - 2.0
    step = 24.0
    for sgn in (1, -1):
        c = -(zb - za) - (xb - xa)
        while c < (xb - xa) + (zb - za):
            # 선: z - za = sgn*(x - xa) - c  → 사각형 안으로 자른다
            pts = []
            for x in (xa, xb):
                z = za + sgn * (x - xa) - c
                if za <= z <= zb:
                    pts.append((x, z))
            for z in (za, zb):
                x = xa + (z - za + c) * sgn
                if xa <= x <= xb:
                    pts.append((x, z))
            pts = sorted(set((round(px, 3), round(pz, 3)) for px, pz in pts))
            if len(pts) >= 2 and (pts[0][0] - pts[-1][0]) ** 2 + (pts[0][1] - pts[-1][1]) ** 2 > 16:
                m.tube("Mesh", [(pts[0][0], 0, pts[0][1]), (pts[-1][0], 0, pts[-1][1])], 0.8, 4)
            c += step
    return m.finish()


# ───────────────────────────── 내보내기·미리보기 ─────────────────────────────

def export(ob):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.wm.obj_export(filepath=os.path.join(OUT_DIR, ob.name + ".obj"), export_selected_objects=True,
                          forward_axis='Y', up_axis='Z', global_scale=1.0, apply_modifiers=True,
                          export_uv=True, export_normals=True, export_materials=True, export_triangulated_mesh=True,
                          export_smooth_groups=False, export_colors=False, path_mode='AUTO')


def stats(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    me = ob.evaluated_get(dg).to_mesh()
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    xs, ys, zs = zip(*[v.co[:] for v in me.vertices])
    ob.evaluated_get(dg).to_mesh_clear()
    return tris, (min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs))


def _setup_render():
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.eevee.taa_render_samples = 32
    sc.render.resolution_x, sc.render.resolution_y = 800, 600
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'Standard'
    sc.render.use_stamp = True
    for p in sc.render.bl_rna.properties:
        if p.identifier.startswith("use_stamp_") and p.identifier != "use_stamp_note":
            try:
                setattr(sc.render, p.identifier, False)
            except (AttributeError, TypeError):
                pass
    sc.render.use_stamp_note = True
    sc.render.stamp_font_size = 22
    w = bpy.data.worlds.new("W")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.64, 0.78, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.9
    sc.world = w
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", 'SUN'))
    sun.data.energy = 2.7
    sun.data.angle = math.radians(6)
    sun.rotation_euler = (math.radians(38), 0, math.radians(-35))
    sc.collection.objects.link(sun)
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    cam.data.type = 'ORTHO'
    cam.data.clip_end = 100000
    sc.collection.objects.link(cam)
    sc.camera = cam
    water = bpy.data.materials.new("Water")
    water.use_nodes = True
    water.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*_hex("3656C4"), 1)
    water.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.35
    ground = bpy.data.materials.new("Ground")
    ground.use_nodes = True
    ground.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*_hex("C9C3B6"), 1)
    planes = {}
    for nm, mat, z in (("WaterPlane", water, 0.0), ("GroundPlane", ground, -0.2)):
        bm = bmesh.new()
        bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=40000)
        me = bpy.data.meshes.new(nm)
        bm.to_mesh(me)
        me.materials.append(mat)
        ob = bpy.data.objects.new(nm, me)
        ob.location.z = z
        sc.collection.objects.link(ob)
        planes[nm] = ob
    return cam, planes


def _frame(cam, obs, az, el, pad=1.12):
    """az·el(도) 방향에서 obs 를 정사영으로 꽉 차게."""
    bpy.context.view_layer.update()          # 막 옮긴 복제본의 matrix_world 갱신
    a, e = math.radians(az), math.radians(el)
    fwd = -Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)))
    rot = fwd.to_track_quat('-Z', 'Y')
    cam.rotation_euler = rot.to_euler()
    R = rot.to_matrix()
    pts = [ob.matrix_world @ Vector(c) for ob in obs for c in ob.bound_box]
    cx = [(R.transposed() @ p) for p in pts]
    xs, ys, zs = [p.x for p in cx], [p.y for p in cx], [p.z for p in cx]
    mid = R @ Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, max(zs) + 20000))
    cam.location = mid
    sc = bpy.context.scene
    aspect = sc.render.resolution_x / sc.render.resolution_y
    cam.data.ortho_scale = max(max(xs) - min(xs), (max(ys) - min(ys)) * aspect) * pad


def _render(path, note):
    sc = bpy.context.scene
    sc.render.stamp_note_text = note
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def _load(path):
    import numpy as np
    img = bpy.data.images.load(path)
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)


def _save(arr, path):
    h, w, _ = arr.shape
    img = bpy.data.images.new("out", w, h, alpha=True)
    img.pixels.foreach_set(arr.ravel())
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


def previews(objs):
    import numpy as np
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    cam, planes = _setup_render()
    tmp = os.path.join(PREVIEW_DIR, "_tmp")
    os.makedirs(tmp, exist_ok=True)
    boats = {"Tugboat_01", "Motorboat_01", "Yacht_01", "Ship_01"}
    props = {"Drum_01", "Drum_02", "Pallet_01", "Crates_01", "ChainFence_01"}
    tiles = []

    def only(show, water):
        for ob in bpy.context.scene.objects:
            if ob.type == 'MESH' and ob.name not in planes:
                ob.hide_render = ob not in show
        planes["WaterPlane"].hide_render = not water
        planes["GroundPlane"].hide_render = water

    for ob in objs:
        tris = stats(ob)[0]
        water = ob.name not in props
        only([ob], water)
        _frame(cam, [ob], -58, 50)
        a = os.path.join(tmp, ob.name + "_a.png")
        _render(a, "{}  {} tris  3/4 (50°)".format(ob.name, tris))
        only([ob], False)
        planes["GroundPlane"].hide_render = True
        _frame(cam, [ob], -90, 0 if ob.name in boats else 20)
        b = os.path.join(tmp, ob.name + "_b.png")
        _render(b, "{}  side".format(ob.name))
        A, Bm = _load(a), _load(b)
        _save(np.concatenate([A, Bm], axis=1), os.path.join(PREVIEW_DIR, ob.name + ".png"))
        tiles.append(A)

    # 항구 장면 — steam_10 구도: 바다 쪽(+X)에서 뭍을 50° 내려다봄
    by = {ob.name: ob for ob in objs}
    scene_obs = []

    def place(name, loc, rz=0.0):
        o = by[name].copy()
        o.location = loc
        o.rotation_euler = (0, 0, math.radians(rz))
        bpy.context.scene.collection.objects.link(o)
        scene_obs.append(o)
        return o

    place("Quay_01", (0, 0, 0))
    place("Pier_01", (250, 0, 0))
    place("Tugboat_01", (820, -345, 0), 180)
    place("Motorboat_01", (700, 290, 0), 180)
    place("Drum_01", (-170, -260, DECK_TOP))
    place("Drum_02", (-170, -195, DECK_TOP))
    place("Pallet_01", (-150, -420, DECK_TOP), 90)
    place("Crates_01", (-165, -560, DECK_TOP))
    for i in range(3):
        place("ChainFence_01", (-235, -700 + 300 * i + 150, DECK_TOP), 90)
    only(scene_obs, True)
    focus = [o for o in scene_obs if o.name.split(".")[0] not in ("Quay_01",)]
    _frame(cam, focus, 0, 50, 1.02)
    hs = os.path.join(PREVIEW_DIR, "harbor_scene.png")
    _render(hs, "harbor scene (steam_10 구도)")
    tiles.append(_load(hs))

    # 배 네 척 크기 비교
    fleet = []
    for name, y in (("Motorboat_01", -900), ("Tugboat_01", -300), ("Yacht_01", 500), ("Ship_01", 2000)):
        o = by[name].copy()
        o.location = (0, y, 0)
        bpy.context.scene.collection.objects.link(o)
        fleet.append(o)
    only(fleet, True)
    _frame(cam, fleet, -58, 50, 1.05)
    fl = os.path.join(PREVIEW_DIR, "fleet.png")
    _render(fl, "fleet scale")
    tiles.append(_load(fl))

    cols = 4
    h, w, _ = tiles[0].shape
    while len(tiles) % cols:
        tiles.append(np.ones((h, w, 4), dtype=np.float32))
    rows = [np.concatenate(tiles[i:i + cols], axis=1) for i in range(0, len(tiles), cols)]
    _save(np.concatenate(list(reversed(rows)), axis=0), os.path.join(PREVIEW_DIR, "all.png"))


def _selfcheck():
    assert len(rounded_rect(0, 100, 0, 100, 10)) == 20
    bm = loft([[(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 0, 1), (1, 0, 1), (0, 1, 1)]], lambda i, j: 0, 0, 0)
    assert len(bm.faces) == 5
    bm.free()


def main():
    _selfcheck()
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    os.makedirs(OUT_DIR, exist_ok=True)
    builders = [tugboat, motorboat, yacht, ship, quay,
                lambda: pier("Pier_01", 1300.0, 160.0, [(1180.0, 90.0)]),
                lambda: pier("Pier_02", 2600.0, 210.0, [(2470.0, -130.0), (2470.0, 130.0)]),
                lambda: drum("Drum_01", "Yellow"), lambda: drum("Drum_02", "Blue"), pallet, crates, chain_fence]
    objs = [b() for b in builders]
    print("\n| 메시 | 삼각형 | X | Y | Z | 슬롯 |")
    for ob in objs:
        export(ob)
        tris, xr, yr, zr = stats(ob)
        print("| {} | {} | {:.0f}..{:.0f} ({:.0f}) | {:.0f}..{:.0f} ({:.0f}) | {:.0f}..{:.0f} | {} |".format(
            ob.name, tris, xr[0], xr[1], xr[1] - xr[0], yr[0], yr[1], yr[1] - yr[0], zr[0], zr[1],
            ", ".join(ms.name for ms in ob.data.materials)))
    if "--no-render" not in sys.argv:
        previews(objs)


if __name__ == "__main__":
    main()
