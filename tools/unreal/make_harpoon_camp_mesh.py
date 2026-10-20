"""스토리 ⑤ 작살단 캠프 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_harpoon_camp_mesh.py   → Saved/Generated/HarpoonCamp_01.obj (+ .mtl)

시트 `docs/reference/story_harpoon_camp.md` 구성 그대로: 회색 천막 셋(녹슨 붉은 단·깃발) · 쇠창살 우리 셋(캠프 메시와 따로 `HarpoonCage_01` — 부술 수 있다) · 가운데 모닥불(돌 둘레·통나무·불꽃) ·
작살 걸이(나무틀 + 쇠 작살 넷) · 그물 말뚝 틀 · 덫 상자(쇠 턱 덫). 그린 치수 cm = 월드 크기(반입 배율 1), 원점 = 캠프 중심 바닥, 키 = +Z.
면마다 정점을 따로 둬 한 면 한 색. 슬롯 색 `SLOT_COLORS` 는 반입 스크립트(`import_harpoon_camp_mesh.py`)가 읽는다.
"""
import math

from make_power_meshes import _frustum
from make_tree_meshes import Mesh
from make_weapon_meshes import _box, _hexa

SLOTS = ["Canvas", "Hem", "Wood", "Iron", "Stone", "Fire", "Net", "Rope", "Flame"]
# 슬롯 색(리니어) — 회색 천 · 녹슨 붉은 단/깃발 · 짙은 나무 · 검은 쇠 · 돌 회갈색 · 주황 불꽃 · 황토 그물 · 밧줄 황토
SLOT_COLORS = {"Canvas": (0.17, 0.19, 0.20), "Hem": (0.30, 0.05, 0.03), "Wood": (0.13, 0.07, 0.035), "Iron": (0.05, 0.055, 0.065),
               "Stone": (0.24, 0.22, 0.19), "Fire": (1.0, 0.45, 0.05), "Net": (0.38, 0.30, 0.16), "Rope": (0.50, 0.34, 0.15), "Flame": (1.0, 0.85, 0.20)}

# 놓는 자리 (캠프 중심 기준 cm) — 천막은 북쪽(-Y) 줄, 우리는 남쪽(+Y) 줄
TENT_SPOTS = [(-450.0, -300.0), (0.0, -520.0), (450.0, -300.0)]
CAGE_SPOTS = [(-350.0, 350.0), (0.0, 450.0), (350.0, 350.0)]
FIRE_SPOT = (0.0, -40.0)
RACK_SPOT = (680.0, 120.0)
NET_SPOT = (-680.0, 100.0)
TRAP_SPOT = (200.0, 80.0)
# 천막 = 주름 잡힌 둥근 천(꼭짓점 16, 봉우리·골 번갈아) + 처마 단. 바깥 반경 TENT_R 가 게임 쪽 `TentRadiusCm` 과 같다. 주의: OBJ 반입이 Y 를 뒤집는다 — 여기 +Y 는 월드 -Y.
TENT_R, TENT_H = 205.0, 300.0


def _frustum_at(mesh, slot, cx, cy, z0, r0, z1, r1, sides, cap_slot=None, phase=0.0):
    """원점에 만든 원뿔대를 (cx, cy) 로 옮긴다 — `Mesh.band` 가 바깥 방향을 월드 원점 기준으로 재서, 처음부터 옆에 만들면 면이 뒤집힌다."""
    first = len(mesh.verts)
    lower, upper = mesh.ring(sides, r0, z0, phase=phase), mesh.ring(sides, r1, z1, phase=phase)
    mesh.group += 1
    mesh.band(slot, lower, upper)
    if cap_slot:
        mesh.group += 1
        mesh.fan(cap_slot, upper, mesh.v(0.0, 0.0, z1), 1.0)
    for i in range(first, len(mesh.verts)):
        x, y, z = mesh.verts[i]
        mesh.verts[i] = (x + cx, y + cy, z)


def _strut(mesh, slot, p0, p1, t):
    """p0→p1 로 뻗는 가는 막대(밧줄·말뚝) — 단면 t 정사각, 옆 방향은 수평 수직."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    n = math.hypot(dx, dy) or 1.0
    px, py = -dy / n * t, dx / n * t
    def quad(p):
        return [(p[0] - px, p[1] - py, p[2] - t), (p[0] + px, p[1] + py, p[2] - t), (p[0] + px, p[1] + py, p[2] + t), (p[0] - px, p[1] - py, p[2] + t)]
    _hexa(mesh, slot, quad(p0) + quad(p1))


def _tent(mesh, cx, cy):
    # 주름 천막: 16각 꼭짓점이 봉우리(반경 TENT_R-5)·골(−28) 번갈아 — 면마다 따로 둬 주름이 각져 보인다. 처마 단(붉은)은 아래 40cm, 살짝 퍼짐.
    count, hem_h = 16, 42.0
    def ring(radius_hi, radius_lo, z):
        return [(cx + (radius_hi if i % 2 == 0 else radius_lo) * math.cos(math.tau * i / count),
                 cy + (radius_hi if i % 2 == 0 else radius_lo) * math.sin(math.tau * i / count), z) for i in range(count)]
    def facets(slot, lower, upper, cap=None):
        for i in range(count):
            j = (i + 1) % count
            mesh.group += 1
            a, b, c, d = (mesh.v(*p) for p in (lower[i], lower[j], upper[j], upper[i]))
            mid = ((lower[i][0] + lower[j][0]) * 0.5 - cx, (lower[i][1] + lower[j][1]) * 0.5 - cy, 0.0)
            mesh.tri(slot, a, b, c, mid)
            mesh.tri(slot, a, c, d, mid)
    hem_lo, hem_hi = ring(TENT_R, TENT_R - 28.0, 0.0), ring(TENT_R - 6.0, TENT_R - 30.0, hem_h)
    facets("Hem", hem_lo, hem_hi)
    canopy_hi = ring(24.0, 16.0, TENT_H)
    facets("Canvas", hem_hi, canopy_hi)
    top = [mesh.v(*p) for p in canopy_hi]
    mesh.group += 1
    mesh.fan("Canvas", top, mesh.v(cx, cy, TENT_H + 6.0), 1.0)
    # 꼭대기 장대 + 붉은 깃발 + 처마 안쪽 말뚝 줄(네 모서리) — 천막을 땅에 묶은 느낌
    _box(mesh, "Wood", cx - 5.0, cx + 5.0, cy - 5.0, cy + 5.0, TENT_H, TENT_H + 140.0)
    _box(mesh, "Hem", cx + 5.0, cx + 85.0, cy - 2.0, cy + 2.0, TENT_H + 75.0, TENT_H + 135.0, top_inset=0.0)
    for k in range(4):
        t = math.pi / 4 + k * math.pi / 2
        stake = (cx + (TENT_R + 40.0) * math.cos(t), cy + (TENT_R + 40.0) * math.sin(t))
        _strut(mesh, "Wood", (stake[0], stake[1], 0.0), (stake[0], stake[1], 38.0), 5.0)
        _strut(mesh, "Rope", (stake[0], stake[1], 30.0), (cx + 130.0 * math.cos(t), cy + 130.0 * math.sin(t), 150.0), 2.5)
    # 문 — +Y 면에 짙은 틈과 걷어 올린 천 문설주
    _box(mesh, "Wood", cx - 45.0, cx + 45.0, cy + TENT_R * 0.80, cy + TENT_R * 0.80 + 8.0, 0.0, 130.0)


def _cage(mesh, cx, cy):
    w, h = 100.0, 150.0
    _box(mesh, "Wood", cx - w - 10.0, cx + w + 10.0, cy - w - 10.0, cy + w + 10.0, 0.0, 14.0)     # 바닥판
    for sx in (-1, 1):
        for sy in (-1, 1):
            _box(mesh, "Wood", cx + sx * w - 7.0, cx + sx * w + 7.0, cy + sy * w - 7.0, cy + sy * w + 7.0, 14.0, h)
    _box(mesh, "Iron", cx - w - 12.0, cx + w + 12.0, cy - w - 12.0, cy + w + 12.0, h, h + 14.0)     # 지붕판
    for i in range(7):                                                                           # 앞(+Y)·뒤·옆 쇠창살
        t = -w + 25.0 + i * (2 * w - 50.0) / 6.0
        for y in (cy - w, cy + w):
            _box(mesh, "Iron", cx + t - 2.5, cx + t + 2.5, y - 2.5, y + 2.5, 14.0, h)
        for x in (cx - w, cx + w):
            _box(mesh, "Iron", x - 2.5, x + 2.5, cy + t - 2.5, cy + t + 2.5, 14.0, h)
    _box(mesh, "Iron", cx - w, cx + w, cy + w - 3.0, cy + w + 3.0, 70.0, 78.0)                     # 앞 가로 쇠막대


def _blob(mesh, slot, cx, cy, r, h):
    """둥근 덩어리(돌) — 배 부른 두 단 원뿔대를 쌓는다. 모서리 8개라 상자보다 둥글다."""
    _frustum_at(mesh, slot, cx, cy, 0.0, r * 0.80, h * 0.55, r, 8, phase=0.2)
    _frustum_at(mesh, slot, cx, cy, h * 0.55, r, h, r * 0.55, 8, cap_slot=slot, phase=0.2)


def _campfire(mesh, cx, cy):
    for i in range(10):                                                                          # 돌 둘레 — 크기·높이 들쭉날쭉한 둥근 덩어리
        t = math.tau * i / 10
        sx, sy = cx + 72.0 * math.cos(t), cy + 72.0 * math.sin(t)
        _blob(mesh, "Stone", sx, sy, 15.0 + 4.0 * ((i * 7) % 3), 20.0 + 6.0 * ((i * 5) % 3))
    for k in range(3):                                                                           # 모아 세운 통나무 셋 (별 모양)
        t = k * math.tau / 3 + 0.4
        _strut(mesh, "Wood", (cx + 55.0 * math.cos(t), cy + 55.0 * math.sin(t), 8.0), (cx + 8.0 * math.cos(t), cy + 8.0 * math.sin(t), 40.0), 9.0)
    _frustum_at(mesh, "Fire", cx, cy, 24.0, 30.0, 95.0, 5.0, 8, cap_slot="Fire")                  # 바깥 불꽃(주황)
    for k, (dx, dy, h) in enumerate(((16.0, 6.0, 80.0), (-14.0, 10.0, 68.0), (2.0, -16.0, 74.0))):  # 곁 불꽃 셋 — 키가 달라 너울거림
        _frustum_at(mesh, "Fire", cx + dx, cy + dy, 24.0, 15.0, h, 2.0, 6, cap_slot="Fire")
    _frustum_at(mesh, "Flame", cx, cy, 26.0, 18.0, 122.0, 1.0, 8, cap_slot="Flame")                # 속 불꽃(노랑, 가장 높음)


def _harpoon_rack(mesh, cx, cy):
    _box(mesh, "Wood", cx - 110.0, cx - 98.0, cy - 6.0, cy + 6.0, 0.0, 170.0)                     # 기둥 둘 + 가로대 둘
    _box(mesh, "Wood", cx + 98.0, cx + 110.0, cy - 6.0, cy + 6.0, 0.0, 170.0)
    for z in (60.0, 130.0):
        _box(mesh, "Wood", cx - 110.0, cx + 110.0, cy - 5.0, cy + 5.0, z, z + 10.0)
    for i in range(4):                                                                           # 쇠 작살 넷 — 자루(나무) + 갈고리 촉(쇠)
        x = cx - 75.0 + i * 50.0
        _box(mesh, "Wood", x - 3.0, x + 3.0, cy - 3.0, cy + 3.0, 20.0, 215.0)
        _frustum_at(mesh, "Iron", x, cy, 215.0, 9.0, 255.0, 1.0, 4, cap_slot="Iron")
        _box(mesh, "Iron", x - 14.0, x + 14.0, cy - 2.0, cy + 2.0, 214.0, 220.0)


def _net_frame(mesh, cx, cy):
    for x in (-130.0, 130.0):                                                                    # 말뚝 둘 + 윗줄 + 늘어진 그물
        _box(mesh, "Wood", cx + x - 7.0, cx + x + 7.0, cy - 7.0, cy + 7.0, 0.0, 190.0)
    _box(mesh, "Rope", cx - 130.0, cx + 130.0, cy - 3.0, cy + 3.0, 175.0, 185.0)
    _box(mesh, "Net", cx - 125.0, cx + 125.0, cy - 2.0, cy + 2.0, 40.0, 175.0, top_inset=0.0)
    for i in range(5):                                                                           # 그물 위 굵은 밧줄 가로 다섯
        z = 60.0 + i * 25.0
        _box(mesh, "Rope", cx - 125.0, cx + 125.0, cy - 4.0, cy + 4.0, z, z + 5.0)


def _trap_crate(mesh, cx, cy):
    _box(mesh, "Wood", cx - 45.0, cx + 45.0, cy - 35.0, cy + 35.0, 0.0, 55.0)                     # 나무 상자 + 쇠 띠
    _box(mesh, "Iron", cx - 47.0, cx + 47.0, cy - 37.0, cy + 37.0, 20.0, 28.0)
    _box(mesh, "Iron", cx - 47.0, cx + 47.0, cy - 37.0, cy + 37.0, 42.0, 50.0)
    for i in range(6):                                                                           # 위에 얹은 쇠 턱 이빨
        x = cx - 40.0 + i * 16.0
        _frustum_at(mesh, "Iron", x, cy, 55.0, 6.0, 80.0, 1.0, 4, cap_slot="Iron")


def camp():
    mesh = Mesh()
    for spot in TENT_SPOTS:
        _tent(mesh, *spot)
    _campfire(mesh, *FIRE_SPOT)
    _harpoon_rack(mesh, *RACK_SPOT)
    _net_frame(mesh, *NET_SPOT)
    _trap_crate(mesh, *TRAP_SPOT)
    return mesh


def cage():
    """우리 하나 (원점 = 우리 중심 바닥) — 부술 수 있는 별도 액터 `SnareCampCageActor` 가 셋 세운다. 자리는 `CAGE_SPOTS`."""
    mesh = Mesh()
    _cage(mesh, 0.0, 0.0)
    return mesh


if __name__ == "__main__":
    print("HarpoonCage_01.obj 삼각형 {}".format(cage().write("HarpoonCage_01", SLOTS)))
    mesh = camp()
    triangles = mesh.write("HarpoonCamp_01", SLOTS)
    xs, ys, zs = zip(*mesh.verts)
    print("HarpoonCamp_01.obj 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f}".format(
        triangles, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))
