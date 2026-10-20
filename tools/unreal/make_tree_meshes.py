"""M15-04 레퍼런스식 나무 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_tree_meshes.py   → Saved/Generated/Pine_05·Pine_06·Tree_04·Birch_01·Boulder_01·Boulder_02·BerryBush_01~04·ResinPine_01·Bush_02·Leaf_01·Mushroom_02·BerryBush_05~06·BeetPatch_01·Wheat_01.obj (+ .mtl)

소나무는 매끈한 원뿔 3단, 활엽수·자작나무·덤불은 매끈한 잎 공 뭉침이다(M98 — 술 치마 없음, 우리 모양).
슬롯 이름은 쿼터니우스 메시와 같게(`Leaves_Pine`·`Leaves_NormalTree`·`Bark_NormalTree`) — `setup_zones.py` 의
잎 MI 덮어쓰기 표를 그대로 쓴다. 단위 cm, Z 위(언리얼 OBJ 임포터는 축을 안 돌린다), 배치 스크립트가 목표 높이로 배율을 맞춘다.
"""
import math
import os
import random

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
SEED = 1505
TRUNK_SIDES = 10
TRUNK_RADIUS = 38.0   # 줄기 굵기 ≈ 맨 아래 단 지름의 ⅕
# (이름, [(단 밑 z, 단 높이, 반지름)]) — 키·치마 시작 높이를 정하는 틀(M98-b 부터 원뿔 3단이 이 범위를 채운다). 옛 설명: 줄기가 높이 ⅓ 까지 드러나고, 그 위로 납작한 우산 치마가 6~7단 겹친다.
# 단마다 아래 단보다 반지름이 ≈ 15% 좁고 단 높이의 ≈ 90% 만큼만 올라 — 단 가장자리 커튼 밑에 그늘 띠가 선다.
# 50° 카메라에서 줄기가 보이려면 맨 아래 술 끝 높이 × cos50 > 맨 아래 반지름 × sin50 (≈ 높이 > 1.2 × 반지름).
# 이전: 4~5단(단 높이 120~160, 반지름 50~180) — 단이 서로 묻혀 매끈한 원뿔 하나로 보였다.
PINES_RAW = [
    ("Pine_05", [(330.0, 110.0, 200.0), (425.0, 105.0, 170.0), (520.0, 100.0, 143.0), (610.0, 95.0, 118.0),
                 (695.0, 90.0, 94.0), (775.0, 90.0, 68.0), (850.0, 95.0, 40.0)]),
    ("Pine_06", [(300.0, 105.0, 185.0), (390.0, 100.0, 155.0), (480.0, 95.0, 128.0), (565.0, 90.0, 102.0),
                 (645.0, 88.0, 76.0), (725.0, 95.0, 48.0)]),
]
# M81-04 감독 보정(2026-10-09): 침엽수는 갈색 줄기가 또렷이 보인다 — 게임 카메라(55°)에서 줄기가 나무 키의 ≈25% 로 드러나게
# 치마 묶음을 위로 올린다. 맨 위(꼭대기)는 그대로 두고 단 위치·높이만 같은 비율로 눌러, 메시 높이(경계)가 안 바뀐다 —
# 맵에 이미 선 나무 키(목표 높이 ÷ 경계)가 그대로 맞는다. 치마 시작 높이: Pine_05 475 · Pine_06 415(둘 다 맨 아래 술 끝 ≈ 키의 46%).
SKIRT_START_Z = {"Pine_05": 475.0, "Pine_06": 415.0}


def _lift(name, tiers):
    bottom, top = tiers[0][0], tiers[-1][0] + tiers[-1][1]
    start = SKIRT_START_Z[name]
    k = (top - start) / (top - bottom)
    return [(start + (z - bottom) * k, h * k, r) for z, h, r in tiers]


PINES = [(name, _lift(name, tiers)) for name, tiers in PINES_RAW]
# 가지 활엽수 — 높이 드러난 줄기에서 가지가 갈라져 끝마다 잎 공 뭉치. (이름, 줄기 높이, [(뭉치 x, y, z, 반지름)])
BROADS = [
    ("Tree_04", 600.0, [(0.0, 0.0, 640.0, 150.0), (120.0, 50.0, 500.0, 125.0), (-110.0, -60.0, 430.0, 120.0),
                        (40.0, -120.0, 350.0, 105.0)]),
]
# 자작나무 — 흰 줄기 위로 매끈한 둥근 잎 뭉치 3~4개. (이름, 줄기 높이, [(뭉치 중심 x, y, z, 반지름)])
BIRCHES = [
    ("Birch_01", 440.0, [(0.0, 0.0, 480.0, 95.0), (75.0, 35.0, 400.0, 80.0), (-65.0, -40.0, 360.0, 75.0),
                         (25.0, -75.0, 300.0, 65.0)]),
]


class Mesh:
    def __init__(self):
        self.verts, self.faces = [], []   # faces: (슬롯, a, b, c, 매끈 묶음) — 0부터
        self.group = 1
        self.uv = {}   # 정점 → (u, v) 를 직접 줄 때만(M19-02 연못 물가 거리). 없으면 아래 평면 UV

    def v(self, x, y, z):
        self.verts.append((x, y, z))
        return len(self.verts) - 1

    def _normal(self, a, b, c):
        pa, pb, pc = self.verts[a], self.verts[b], self.verts[c]
        u = [pb[i] - pa[i] for i in range(3)]
        w = [pc[i] - pa[i] for i in range(3)]
        return (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])

    def tri(self, slot, a, b, c, out):
        """out = 바깥 방향 — 법선이 반대면 감기 순서를 뒤집는다(OBJ 는 바깥에서 봐서 반시계가 앞면)."""
        n = self._normal(a, b, c)
        self.faces.append((slot, a, b, c, self.group) if sum(n[i] * out[i] for i in range(3)) >= 0 else (slot, a, c, b, self.group))

    def ring(self, count, radius, z, jitter=0.0, rng=None, phase=0.0, cx=0.0, cy=0.0):
        out = []
        for i in range(count):
            t = math.tau * i / count + phase
            r = radius * (1.0 + (rng.uniform(-jitter, jitter) if rng else 0.0))
            out.append(self.v(cx + r * math.cos(t), cy + r * math.sin(t), z))
        return out

    def band(self, slot, lower, upper):
        n = len(lower)
        for i in range(n):
            j = (i + 1) % n
            a, b, c, d = lower[i], lower[j], upper[j], upper[i]
            mid = [(self.verts[a][k] + self.verts[c][k]) * 0.5 for k in range(3)]
            out = (mid[0], mid[1], 0.0)
            self.tri(slot, a, b, c, out)
            self.tri(slot, a, c, d, out)

    def fan(self, slot, rim, tip, out_z):
        for i in range(len(rim)):
            a, b = rim[i], rim[(i + 1) % len(rim)]
            mid = [(self.verts[a][k] + self.verts[b][k]) * 0.5 for k in range(3)]
            self.tri(slot, a, b, tip, (0.0, 0.0, out_z) if out_z < 0 else (mid[0], mid[1], out_z))   # 밑면은 곧장 아래

    def write(self, name, slots):
        lines = ["mtllib {}.mtl".format(name), "o {}".format(name)]
        lines += ["v {:.2f} {:.2f} {:.2f}".format(*p) for p in self.verts]
        # UV 가 없으면 Interchange OBJ 번역기가 ensure 를 터뜨린다(`make_plateau_meshes.py`) — 평면 UV 하나씩
        lines += ["vt {:.4f} {:.4f}".format(*self.uv.get(i, (p[0] / 500.0, p[2] / 500.0))) for i, p in enumerate(self.verts)]
        # 법선을 직접 적는다 — Interchange OBJ 번역기는 `s`(매끈 묶음)를 모른다고 버려서 윗면·밑면 법선이 섞였다.
        # (정점, 묶음) 마다 면 법선 평균: 같은 묶음 안은 부드럽게, 묶음 경계(술 끝)는 날카롭게.
        sums = {}
        for _, a, b, c, g in self.faces:
            n = self._normal(a, b, c)
            for v in (a, b, c):
                acc = sums.setdefault((v, g), [0.0, 0.0, 0.0])
                for k in range(3):
                    acc[k] += n[k]
        keys = sorted(sums)
        index = {key: i + 1 for i, key in enumerate(keys)}
        for key in keys:
            n = sums[key]
            length = math.sqrt(sum(x * x for x in n)) or 1.0
            lines.append("vn {:.4f} {:.4f} {:.4f}".format(*(x / length for x in n)))
        for slot in slots:
            lines.append("usemtl " + slot)
            lines += ["f " + " ".join("{0}/{0}/{1}".format(v + 1, index[(v, g)]) for v in (a, b, c))
                      for s, a, b, c, g in self.faces if s == slot]
        with open(os.path.join(OUT_DIR, name + ".obj"), "w") as f:
            f.write("\n".join(lines) + "\n")
        with open(os.path.join(OUT_DIR, name + ".mtl"), "w") as f:
            f.write("".join("newmtl {}\nKd 0.5 0.5 0.5\n".format(s) for s in slots))
        return len(self.faces)


def _trunk(mesh, top_z, radius):
    bottom = mesh.ring(TRUNK_SIDES, radius * 1.35, -10.0)   # 밑동이 살짝 퍼진다
    mid = mesh.ring(TRUNK_SIDES, radius, 40.0)
    top = mesh.ring(TRUNK_SIDES, radius * 0.7, top_z)
    mesh.band("Bark_NormalTree", bottom, mid)
    mesh.band("Bark_NormalTree", mid, top)


# M98-b 법적 위험 정리: 원본 소나무의 술 치마 6~7단·술 16가닥 수치를 버리고, 술 없는 매끈한 원뿔 3단으로 새로 짠다.
# (치마 시작 z 와 꼭대기 z 는 그대로 — 메시 높이가 안 바뀌어 맵에 선 나무 키가 맞는다.) (밑 위치 비, 높이 비, 반지름 비)
CONE_STEPS = [(0.0, 0.46, 1.0), (0.30, 0.42, 0.74), (0.58, 0.42, 0.50)]
CONE_SIDES = 24


def _cone(mesh, slot, z0, height, radius):
    """매끈한 원뿔 한 단 — 한 묶음(부드러운 법선), 밑면은 열어 둔다."""
    mesh.group += 1
    rim = mesh.ring(CONE_SIDES, radius, z0)
    apex = mesh.v(0.0, 0.0, z0 + height)
    mesh.fan(slot, rim, apex, 1.0)


def _pine(name, tiers, rng):
    mesh = Mesh()
    bottom, top = tiers[0][0], tiers[-1][0] + tiers[-1][1]
    max_r = tiers[0][2]
    span = top - bottom
    _trunk(mesh, top - span * 0.2, TRUNK_RADIUS)
    for start, h_ratio, r_ratio in CONE_STEPS:
        _cone(mesh, "Leaves_Pine", bottom + span * start, span * h_ratio, max_r * r_ratio)
    return mesh.write(name, ["Bark_NormalTree", "Leaves_Pine"])


def _ball(mesh, slot, cx, cy, cz, radius, rng):
    """매끈한 잎 공 하나(술 없음) — 중심 (cx, cy, cz), 살짝 납작."""
    squash = 0.85
    _boulder(mesh, cx, cy, radius, squash, rng, rows=6, seg=14, slot=slot, cz=cz - radius * squash * 0.55)


def _birch(name, trunk_height, puffs, rng):
    """자작나무 — 가는 흰 줄기, 가지 끝마다 매끈한 잎 공."""
    mesh = Mesh()
    bottom = mesh.ring(8, 16.0, -10.0)
    top = mesh.ring(8, 9.0, trunk_height)
    mesh.band("Bark_Birch", bottom, top)
    for cx, cy, cz, radius in puffs:
        if cx or cy:   # 줄기에서 뭉치까지 가는 가지
            mesh.band("Bark_Birch", mesh.ring(6, 6.0, cz - radius * 1.1), mesh.ring(6, 4.0, cz - radius * 0.3, cx=cx, cy=cy))
        _ball(mesh, "Leaves_NormalTree", cx, cy, cz, radius, rng)
    return mesh.write(name, ["Bark_Birch", "Leaves_NormalTree"])


def _broad(name, trunk_height, puffs, rng):
    mesh = Mesh()
    _trunk(mesh, trunk_height, 30.0)
    for cx, cy, cz, radius in puffs:
        if cx or cy:   # 줄기에서 뭉치 밑까지 굵은 가지
            mesh.band("Bark_NormalTree", mesh.ring(8, 14.0, cz - radius * 1.2), mesh.ring(8, 9.0, cz - radius * 0.3, cx=cx, cy=cy))
        _ball(mesh, "Leaves_NormalTree", cx, cy, cz, radius, rng)
    return mesh.write(name, ["Bark_NormalTree", "Leaves_NormalTree"])


# 바위는 각진 결정이 아니라 **둥글고 매끈한 회색 덩이**다 — 쿼터니우스 Rock_Medium 은
# 날 선 면이 햇빛에 허옇게 떠 결정처럼 보였다(w51). (이름, 반지름, 납작 비, 곁 덩이 [(dx, dy, 반지름 비)])
ROCKS = [("Boulder_01", 100.0, 0.70, [(70.0, 45.0, 0.55)]), ("Boulder_02", 80.0, 0.82, [])]


def _boulder(mesh, cx, cy, radius, squash, rng, rows=6, seg=12, slot="Rock", cz=0.0):
    """매끈한 덩이 하나 — 한 묶음(부드러운 법선), 반지름을 조금씩 흔든 타원체, 밑은 바닥에 묻는다(cz = 띄울 높이)."""
    mesh.group += 1
    wobble = [rng.uniform(0.88, 1.1) for _ in range(seg)]
    lift = cz + radius * squash * 0.55
    top = mesh.v(cx, cy, lift + radius * squash)
    prev = None
    for j in range(1, rows):
        polar = math.pi * j / rows
        ring = [mesh.v(cx + radius * wobble[i] * math.sin(polar) * math.cos(math.tau * i / seg),
                       cy + radius * wobble[i] * math.sin(polar) * math.sin(math.tau * i / seg),
                       lift + radius * squash * math.cos(polar)) for i in range(seg)]
        for i in range(seg):
            k = (i + 1) % seg
            if prev is None:
                mesh.tri(slot, ring[i], ring[k], top, (0.0, 0.0, 1.0))
            else:
                a, b, c, d = prev[i], prev[k], ring[k], ring[i]
                out = [(mesh.verts[a][q] + mesh.verts[c][q]) * 0.5 - (cx, cy, lift)[q] for q in range(3)]
                mesh.tri(slot, a, b, c, out)
                mesh.tri(slot, a, c, d, out)
        prev = ring
    bottom = mesh.v(cx, cy, lift - radius * squash)
    for i in range(seg):
        mesh.tri(slot, prev[i], prev[(i + 1) % seg], bottom, (0.0, 0.0, -1.0))


def _rock(name, radius, squash, extras, rng):
    mesh = Mesh()
    _boulder(mesh, 0.0, 0.0, radius, squash, rng)
    for dx, dy, ratio in extras:
        _boulder(mesh, dx, dy, radius * ratio, squash * 1.1, rng)
    return mesh.write(name, ["Rock"])


# 베리 덤불 — 둥근 잎 공 서너 개가 땅에 붙어 모이고 겉에 주황 열매 알이 콕콕 박혔다.
# 채집 더미(`create_data_tables.py` GATHER_PILES)의 몸이다 — 팩 Bush_01 기본 머티리얼은 검붉은 가시 뭉치로 떴다(캡처 m1551).
# (이름, 열매 슬롯) · 뭉치 [(x, y, 중심 z, 반지름)]
# 섬마다 덤불이 다르다(DESIGN 13.4): 01 설원 클라우드베리(주황) · 02 평원 블랙베리(검보라) · 03 가을섬 링곤베리(빨강) · 04 가을섬 블루베리(파랑)
# · 05 평원 라즈베리(분홍빛 빨강) · 06 작은 섬 메제레움(자홍) — 05·06 은 `__main__` 맨 끝에서 만든다
BERRY_BUSHES = [("BerryBush_01", "BerryOrange"), ("BerryBush_02", "BerryDark"), ("BerryBush_03", "BerryRed"), ("BerryBush_04", "BerryBlue")]
BUSH_PUFFS = [(0.0, 0.0, 58.0, 62.0), (48.0, 26.0, 42.0, 46.0), (-42.0, -30.0, 40.0, 44.0), (18.0, -48.0, 36.0, 40.0)]
BERRIES = 16
BERRY_RADIUS = 8.0


def _berry_bush(name, berry_slot, rng, berries=BERRIES):
    mesh = Mesh()
    for cx, cy, cz, radius in BUSH_PUFFS:
        _ball(mesh, "Leaves_Bush", cx, cy, cz, radius, rng)
    for _ in range(berries):
        cx, cy, cz, radius = rng.choice(BUSH_PUFFS)
        around, up = rng.uniform(0.0, math.tau), rng.uniform(0.2, 1.0)   # 위에서 보이는 윗쪽 반구에만
        reach = radius * 0.92
        _boulder(mesh, cx + reach * math.cos(up) * math.cos(around), cy + reach * math.cos(up) * math.sin(around),
                 BERRY_RADIUS, 1.0, rng, rows=4, seg=8, slot=berry_slot, cz=cz + reach * math.sin(up) - BERRY_RADIUS * 0.55)
    return mesh.write(name, ["Leaves_Bush", berry_slot] if berries else ["Leaves_Bush"])


# 수지 나무(채집) — 팩 `Pine_04` 는 짙은 초록 뾰족 저폴리라 가을섬 매끈한 나무 사이에서 딴 화풍이었다(09-25 캡처 m1554).
# 같은 원뿔 소나무에 호박색 수지 방울 — 금빛 장식 나무와 초록 잎·방울로 갈린다. (단 번호, 둘레 각 비율, 반지름)
# 방울은 원뿔 겉면에 반쯤 묻는다 — 줄기에 붙이면 50° 카메라에서 잎에 가려 초록 원뿔만 보였다(m1555).
RESIN_TIERS = PINES_RAW[1][1]   # 수지 소나무의 키·치마 시작 높이 틀(원뿔 3단이 채운다)
RESIN_DROPS = [(0, 0.05, 36.0), (0, 0.55, 32.0), (1, 0.30, 32.0), (1, 0.80, 30.0), (2, 0.12, 28.0), (2, 0.62, 28.0),
               (3, 0.40, 24.0), (4, 0.90, 20.0)]


def _resin_pine(name, rng):
    mesh = Mesh()
    _trunk(mesh, RESIN_TIERS[-1][0], TRUNK_RADIUS)
    bottom, top = RESIN_TIERS[0][0], RESIN_TIERS[-1][0] + RESIN_TIERS[-1][1]
    span, max_r = top - bottom, RESIN_TIERS[0][2]
    cones = [(bottom + span * start, span * h_ratio, max_r * r_ratio) for start, h_ratio, r_ratio in CONE_STEPS]
    for z0, height, radius in cones:
        _cone(mesh, "Leaves_Pine", z0, height, radius)
    for tier, turn, radius in RESIN_DROPS:
        z0, height, cone_r = cones[tier % len(cones)]
        # 원뿔 겉면 밑에서 40% 높이(반지름 60%) — 둥근 방울 아래 절반이 잎에 묻힌다
        t = math.tau * turn
        _boulder(mesh, cone_r * 0.60 * math.cos(t), cone_r * 0.60 * math.sin(t), radius, 1.1, rng, rows=5, seg=8,
                 slot="Resin", cz=z0 + height * 0.40 - radius * 0.5)
    return mesh.write(name, ["Bark_NormalTree", "Leaves_Pine", "Resin"])


# 낙엽 — 가을섬 바닥에 붉은 잎이 나무 밑·풀밭에 흩어져 있다. 다섯 갈래 단풍잎 한 장(폭 ≈36cm, 끝·홈 번갈아 10점), 가운데만 0.6cm 솟은 납작 부채.
LEAF_RADIUS = 18.0
LEAF_POINTS = 10


def _leaf(name):
    mesh = Mesh()
    rim = []
    for i in range(LEAF_POINTS):
        t = math.tau * i / LEAF_POINTS
        r = LEAF_RADIUS * (1.0 if i % 2 == 0 else 0.45)   # 다섯 갈래 끝 · 사이 홈
        rim.append(mesh.v(r * math.cos(t), r * math.sin(t), 0.0))
    mesh.fan("Leaf", rim, mesh.v(0.0, 0.0, 0.6), 1.0)
    return mesh.write(name, ["Leaf"])


# 살구버섯 무더기(채집 버섯 더미) — 노란 물결 깔때기 갓 대여섯이 짧은 갈색 대 위에 높이를 달리해 빽빽이 모이고,
# 바깥 갓은 밖으로 기울어 주황 밑면·대가 보인다. 팩 케니 `mushroom_redGroup` 은 탑다운에서 분홍 점으로 떴다.
# 곧게 선 납작 갓은 위에서 노란 육각 판(벌집)으로 보였다(m1560 첫 캡처). (x, y, 갓 반지름, 대 높이, 바깥 기울기 °)
CHANTERELLES = [(0.0, 0.0, 26.0, 30.0, 0.0), (-26.0, 16.0, 22.0, 22.0, 24.0), (24.0, 20.0, 21.0, 25.0, 20.0),
                (6.0, -27.0, 22.0, 18.0, 28.0), (-22.0, -20.0, 17.0, 12.0, 32.0), (30.0, -12.0, 15.0, 14.0, 30.0)]
CAP_LOBES = 5


def _chanterelle(mesh, cx, cy, radius, stem_h, tilt, rng):
    # 버섯 하나를 제자리(밑동 원점)에서 만들고 밑동을 축으로 바깥(무더기 가운데 반대쪽)으로 기울인다
    out_t = math.atan2(cy, cx) if (cx or cy) else 0.0
    ux, uy = math.cos(out_t), math.sin(out_t)
    ca, sa = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))

    def v(lx, ly, lz):
        along = lx * ux + ly * uy
        side_x, side_y = lx - along * ux, ly - along * uy
        along, lz = along * ca + lz * sa, -along * sa + lz * ca
        return mesh.v(cx + side_x + along * ux, cy + side_y + along * uy, lz)

    up = (ux * sa, uy * sa, ca)
    mesh.group += 1
    stem_r = radius * 0.24
    base = [v(stem_r * 1.25 * math.cos(math.tau * i / 8), stem_r * 1.25 * math.sin(math.tau * i / 8), 0.0) for i in range(8)]
    neck = [v(stem_r * math.cos(math.tau * i / 8), stem_r * math.sin(math.tau * i / 8), stem_h) for i in range(8)]
    for i in range(8):
        a, b, c, d = base[i], base[(i + 1) % 8], neck[(i + 1) % 8], neck[i]
        out = (mesh.verts[a][0] - cx, mesh.verts[a][1] - cy, 0.0)
        mesh.tri("Stem", a, b, c, out)
        mesh.tri("Stem", a, c, d, out)
    mesh.group += 1
    # 물결 깔때기 갓 — 가운데가 오목하고 테두리가 가장 높다. 밑면(주름)은 테두리에서 대 목으로 좁아진다
    phase = rng.uniform(0.0, math.tau)
    seg = 20
    rim, mid = [], []
    for i in range(seg):
        t = math.tau * i / seg
        lobe = math.sin(CAP_LOBES * t + phase)
        r = radius * (1.0 + 0.08 * lobe)
        rim.append(v(r * math.cos(t), r * math.sin(t), stem_h + radius * (0.40 + 0.10 * lobe)))
        mid.append(v(radius * 0.55 * math.cos(t), radius * 0.55 * math.sin(t), stem_h + radius * 0.38))
    dip = v(0.0, 0.0, stem_h + radius * 0.24)
    down = tuple(-x for x in up)
    for i in range(seg):
        j = (i + 1) % seg
        mesh.tri("Cap", mid[i], mid[j], dip, up)
        mesh.tri("Cap", mid[i], rim[j], mid[j], up)
        mesh.tri("Cap", mid[i], rim[i], rim[j], up)
        k, l = neck[i * 8 // seg], neck[j * 8 // seg]
        mesh.tri("Gill", rim[i], rim[j], l, down)
        if k != l:
            mesh.tri("Gill", rim[i], l, k, down)


def _mushrooms(name, rng):
    mesh = Mesh()
    for cx, cy, radius, stem_h, tilt in CHANTERELLES:
        _chanterelle(mesh, cx, cy, radius, stem_h, tilt, rng)
    return mesh.write(name, ["Cap", "Gill", "Stem"])


# 평원 야생 작물 — 나무위키 레퍼런스 평원 "꿩과 밀, 사탕무, 베리류, 꽃 종류가 스폰된다". 탑다운에서 덤불·꽃과 갈리게
# 사탕무 = 땅에 반쯤 묻힌 검붉은 뿌리 셋 + 위로 벌어진 넓은 잎, 밀 = 금빛 이삭 달린 가는 줄기 한 다발.
BEETS = [(0.0, -6.0, 21.0), (40.0, 24.0, 17.0), (-34.0, 28.0, 18.0)]   # (x, y, 뿌리 반지름) — 15cm 뿌리는 탑다운에서 잎에 가려 초록 풀로만 보였다(m1564)
BEET_LEAVES = 6


def _blade(mesh, slot, cx, cy, cz, turn, length, width, rise):
    """넓적한 잎 한 장 — 밑동(cx,cy,cz)에서 turn 방향으로 뻗고 끝이 rise 만큼 솟는다. 가운데 잎맥이 살짝 도드라진다."""
    mesh.group += 1
    ux, uy = math.cos(turn), math.sin(turn)
    px, py = -uy, ux

    def at(along, side, lift):
        return mesh.v(cx + ux * along + px * side, cy + uy * along + py * side, cz + lift)
    base, tip = at(0.0, 0.0, 0.0), at(length, 0.0, rise)
    left, right = at(length * 0.45, width * 0.5, rise * 0.55), at(length * 0.45, -width * 0.5, rise * 0.55)
    rib = at(length * 0.45, 0.0, rise * 0.55 + width * 0.12)
    for a, b in ((left, base), (base, right), (right, tip), (tip, left)):
        mesh.tri(slot, a, b, rib, (0.0, 0.0, 1.0))


def _beet_patch(name, rng):
    mesh = Mesh()
    for cx, cy, radius in BEETS:
        _boulder(mesh, cx, cy, radius, 0.9, rng, rows=5, seg=10, slot="Beet", cz=-radius * 0.5)   # 윗머리 반이 땅 위
        phase = rng.uniform(0.0, math.tau)
        for i in range(BEET_LEAVES):
            _blade(mesh, "Leaves_Bush", cx, cy, radius * 0.7, phase + math.tau * i / BEET_LEAVES,
                   radius * 1.9, radius * 0.9, radius * 1.6)   # 잎은 뿌리 둘레로 뻗는다 — 가운데 붉은 머리가 위에서 보이게
    return mesh.write(name, ["Leaves_Bush", "Beet"])


WHEAT_STALKS = 44
WHEAT_REACH = 58.0    # 다발 반지름 — 18줄기·30cm 는 장식 금빛 풀 포기와 같은 크기라 밀로 안 읽혔다(m1564)


def _wheat(name, rng):
    mesh = Mesh()
    for i in range(WHEAT_STALKS):
        t, r = rng.uniform(0.0, math.tau), WHEAT_REACH * math.sqrt(rng.uniform(0.0, 1.0))
        x, y, h = r * math.cos(t), r * math.sin(t), rng.uniform(55.0, 75.0)
        lean = r * 0.35    # 바깥 줄기는 밖으로 기운다 — 다발이 위로 벌어진다
        tx, ty = x + lean * math.cos(t), y + lean * math.sin(t)
        mesh.group += 1
        low = [mesh.v(x + 1.2 * math.cos(math.tau * k / 4), y + 1.2 * math.sin(math.tau * k / 4), 0.0) for k in range(4)]
        top = [mesh.v(tx + 0.8 * math.cos(math.tau * k / 4), ty + 0.8 * math.sin(math.tau * k / 4), h) for k in range(4)]
        for k in range(4):
            j = (k + 1) % 4
            out = (math.cos(math.tau * (k + 0.5) / 4), math.sin(math.tau * (k + 0.5) / 4), 0.0)
            mesh.tri("Stalk", low[k], low[j], top[j], out)
            mesh.tri("Stalk", low[k], top[j], top[k], out)
        _boulder(mesh, tx, ty, 5.0, 3.0, rng, rows=5, seg=6, slot="Grain", cz=h - 10.0)
    return mesh.write(name, ["Stalk", "Grain"])


# 광산 버팀목 (DESIGN 8.16 「광산」) — 원본 광산 굴은 나무 기둥 둘 + 윗보 틀이 굴을 따라 선다. 입구 틀도 같은 메시.
# 굴 방향 = X, 틀 폭 = Y(안쪽 360 — 두 사람이 비켜 지난다), 높이 240(캐릭터 170 보다 한 뼘 위)
FRAME_INNER, FRAME_POST, FRAME_HEIGHT, FRAME_BEAM = 360.0, 26.0, 240.0, 30.0


def _box(mesh, slot, x0, x1, y0, y1, z0, z1):
    """각진 상자 하나 — 면마다 묶음이 달라 모서리가 날카롭다."""
    corners = {(i, j, k): mesh.v((x0, x1)[i], (y0, y1)[j], (z0, z1)[k]) for i in (0, 1) for j in (0, 1) for k in (0, 1)}
    for axis in range(3):
        for side in (0, 1):
            mesh.group += 1
            quad = [key for key in ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0), (0, 0, 1), (0, 1, 1), (1, 1, 1), (1, 0, 1))
                    if key[axis] == side]
            quad.sort(key=lambda key: math.atan2(key[(axis + 2) % 3] - 0.5, key[(axis + 1) % 3] - 0.5))
            out = [0.0, 0.0, 0.0]
            out[axis] = 1.0 if side else -1.0
            a, b, c, d = (corners[key] for key in quad)
            mesh.tri(slot, a, b, c, out)
            mesh.tri(slot, a, c, d, out)


def _mine_frame(name):
    mesh = Mesh()
    half, post = FRAME_INNER * 0.5, FRAME_POST * 0.5
    for sign in (-1.0, 1.0):
        y = sign * (half + post)
        _box(mesh, "MineWood", -post, post, y - post, y + post, 0.0, FRAME_HEIGHT)
    _box(mesh, "MineWood", -post - 2.0, post + 2.0, -half - FRAME_POST - 12.0, half + FRAME_POST + 12.0,
         FRAME_HEIGHT - 4.0, FRAME_HEIGHT + FRAME_BEAM)
    return mesh.write(name, ["MineWood"])


# 늪 채집물 (DESIGN 13.4) — 레퍼런스 Wild Growth Update(2026-04): 늪에만 나는 버섯 넷, 늪꽃(Bog Flower), 수련(Water Lily).
# 버섯 = 가는 줄기 위 꿀색 갓 한 무더기(뽕나무버섯 모양), 늪꽃 = 갈대 포기 위 보라 꽃대, 수련 = 물 웅덩이에 뜬 잎 + 흰 꽃.
SWAMP_MUSHROOMS = [(0.0, 0.0, 14.0, 30.0), (18.0, 10.0, 11.0, 24.0), (-15.0, 12.0, 10.0, 21.0), (6.0, -19.0, 12.0, 26.0),
                   (-20.0, -10.0, 8.0, 16.0), (26.0, -12.0, 7.0, 14.0), (-4.0, 26.0, 8.0, 18.0)]   # (x, y, 갓 반지름, 높이)


def _swamp_mushrooms(name, rng):
    mesh = Mesh()
    for cx, cy, radius, h in SWAMP_MUSHROOMS:
        _boulder(mesh, cx, cy, radius * 0.28, h / (radius * 0.28) * 0.5, rng, rows=4, seg=6, slot="Stem", cz=0.0)
        _boulder(mesh, cx, cy, radius, 0.38, rng, rows=5, seg=10, slot="Resin", cz=h - radius * 0.2)
    return mesh.write(name, ["Stem", "Resin"])


BOG_REEDS = 16
BOG_SPIKES = 5


def _bog_flower(name, rng):
    mesh = Mesh()
    for i in range(BOG_REEDS):
        t = math.tau * i / BOG_REEDS + rng.uniform(-0.2, 0.2)
        _blade(mesh, "Leaves_Bush", rng.uniform(-8.0, 8.0), rng.uniform(-8.0, 8.0), 0.0, t,
               rng.uniform(30.0, 42.0), 7.0, rng.uniform(45.0, 65.0))   # 갈대 잎은 곧게 솟는다
    for i in range(BOG_SPIKES):
        t, r = math.tau * i / BOG_SPIKES + rng.uniform(-0.3, 0.3), rng.uniform(6.0, 16.0)
        x, y = r * math.cos(t), r * math.sin(t)
        _boulder(mesh, x, y, 1.6, 22.0, rng, rows=4, seg=5, slot="Leaves_Bush", cz=0.0)          # 꽃대
        _boulder(mesh, x, y, 6.0, 2.4, rng, rows=5, seg=8, slot="Purple", cz=rng.uniform(58.0, 72.0))   # 보라 꽃 이삭
    return mesh.write(name, ["Leaves_Bush", "Purple"])


LILY_POOL = 80.0     # 물 웅덩이 반지름 — 늪 물웅덩이와 같은 재질이라 잎이 물에 뜬 것으로 읽힌다
LILY_PADS = [(-28.0, -18.0, 26.0), (30.0, -8.0, 22.0), (-6.0, 32.0, 24.0), (34.0, 36.0, 15.0)]   # (x, y, 잎 반지름)
LILY_PETALS = 8
LILY_FLOAT = 10.0    # 늪 진흙 땅이 칸 바닥보다 솟아 있어 z 1 웅덩이는 진흙에 묻혀 가장자리만 보였다(m1573 첫 캡처) — 통째로 띄운다


def _water_lily(name, rng):
    mesh = Mesh()
    mesh.group += 1
    rim = mesh.ring(20, LILY_POOL, 1.0, jitter=0.06, rng=rng)
    mesh.fan("SwampWater", rim, mesh.v(0.0, 0.0, 1.0), 1.0)
    for cx, cy, radius in LILY_PADS:
        mesh.group += 1
        notch = rng.uniform(0.0, math.tau)   # 수련 잎은 한쪽이 V 로 갈라진다
        pts = [notch + 0.35 + (math.tau - 0.7) * k / 11 for k in range(12)]
        center = mesh.v(cx, cy, 3.0)
        edge = [mesh.v(cx + radius * math.cos(a), cy + radius * math.sin(a), 3.0) for a in pts]
        for k in range(len(edge) - 1):
            mesh.tri("Leaves_Bush", center, edge[k], edge[k + 1], (0.0, 0.0, 1.0))
    fx, fy = 4.0, 2.0
    for i in range(LILY_PETALS):
        _blade(mesh, "White", fx, fy, 4.0, math.tau * i / LILY_PETALS, 16.0, 8.0, 10.0)
    for i in range(LILY_PETALS - 3):
        _blade(mesh, "White", fx, fy, 6.0, math.tau * (i + 0.5) / (LILY_PETALS - 3), 10.0, 6.0, 12.0)
    _boulder(mesh, fx, fy, 4.5, 0.8, rng, rows=4, seg=8, slot="Yellow", cz=6.0)
    mesh.verts = [(x, y, z + LILY_FLOAT) for x, y, z in mesh.verts]
    return mesh.write(name, ["SwampWater", "Leaves_Bush", "White", "Yellow"])


def _selfcheck():
    """감기 순서 검사 — 위를 보는 삼각형은 위쪽 법선이어야 한다."""
    m = Mesh()
    a, b, c = m.v(0, 0, 0), m.v(1, 0, 0), m.v(0, 1, 0)
    m.tri("x", a, c, b, (0.0, 0.0, 1.0))
    assert m.faces[0] == ("x", a, b, c, 1), m.faces


if __name__ == "__main__":
    _selfcheck()
    os.makedirs(OUT_DIR, exist_ok=True)
    rng = random.Random(SEED)
    for pine_name, pine_tiers in PINES:
        print("저장:", pine_name, "삼각형", _pine(pine_name, pine_tiers, rng))
    for broad_name, broad_trunk, broad_balls in BROADS:
        print("저장:", broad_name, "삼각형", _broad(broad_name, broad_trunk, broad_balls, rng))
    for birch_name, birch_trunk, birch_puffs in BIRCHES:
        print("저장:", birch_name, "삼각형", _birch(birch_name, birch_trunk, birch_puffs, rng))
    for rock_name, rock_radius, rock_squash, rock_extras in ROCKS:
        print("저장:", rock_name, "삼각형", _rock(rock_name, rock_radius, rock_squash, rock_extras, rng))
    for bush_name, berry_slot in BERRY_BUSHES:
        print("저장:", bush_name, "삼각형", _berry_bush(bush_name, berry_slot, rng))
    print("저장: ResinPine_01 삼각형", _resin_pine("ResinPine_01", rng))
    # 장식 덤불 — 베리 덤불과 같은 잎 공 뭉치, 열매 없음. 팩 `Bush_01` 은 칸 잎색을 입혀도 매끈한 솜뭉치로 떴다(m1555 숲 위쪽 주황 공)
    print("저장: Bush_02 삼각형", _berry_bush("Bush_02", None, rng, berries=0))
    print("저장: Leaf_01 삼각형", _leaf("Leaf_01"))
    print("저장: Mushroom_02 삼각형", _mushrooms("Mushroom_02", rng))
    # 아래는 앞 메시들의 난수 흐름을 안 바꾸게 맨 끝에 — 평원 라즈베리(05)·작은 섬 메제레움(06) 덤불, 평원 사탕무·밀
    print("저장: BerryBush_05 삼각형", _berry_bush("BerryBush_05", "BerryPink", rng))
    print("저장: BerryBush_06 삼각형", _berry_bush("BerryBush_06", "BerryMezereon", rng))
    print("저장: BeetPatch_01 삼각형", _beet_patch("BeetPatch_01", rng))
    print("저장: Wheat_01 삼각형", _wheat("Wheat_01", rng))
    print("저장: MineFrame_01 삼각형", _mine_frame("MineFrame_01"))
    print("저장: SwampMushroom_01 삼각형", _swamp_mushrooms("SwampMushroom_01", rng))
    print("저장: BogFlower_01 삼각형", _bog_flower("BogFlower_01", rng))
    print("저장: WaterLily_01 삼각형", _water_lily("WaterLily_01", rng))
