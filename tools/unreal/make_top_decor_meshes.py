"""M108-03 위에 올리는 소품 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_top_decor_meshes.py   → Saved/Generated/Mug_01.obj · FlowerPot_01.obj · Candle_01.obj · Radio_01.obj (+ .mtl)

DESIGN 8.11 「건축가 꾸미기」 위에 올리기 — 머그잔·작은 화분·촛대. 나무 탁자(폭 145) 위 슬롯 3 이라 한 슬롯 폭이 약 48 →
셋 다 **가로 40cm 이하**(소품 가로 한도). 실물(머그 9cm 남짓)의 약 ×1.8 — 다른 손 소품처럼 키 176 몸 옆에서 읽히는 레퍼런스 게임식 큰 소품.
원본(레퍼런스 게임) 소품은 따르지 않는다(Reference 6절): 머그는 크림 몸에 꿀색 띠 + 앞에 솟은 새싹 두 잎(새싹 모자·벽시계와 같은 우리 표식),
화분은 테라코타 + 새싹 두 큰 잎과 봉오리, 촛대는 놋쇠 접시·고리 손잡이 + 밀랍(연한 크림) 초 + 주황·노랑 불꽃.
면마다 정점을 따로 둬 한 면 한 색(world_environment.md "한 면 한 색"), 그린 치수 cm, **밑면 가운데 = (0, 0, 0), Z 위**.
M108-08b(루프#1410) 라디오 — 전기 가구 넷 중 위에 올리는 것. 나무 상자 + 크림 스피커 판(짙은 구멍 아홉) + 다이얼 창(꺼짐 = 짙은 칸 —
켜짐 짝 `_On_01` 이 밝은 칸으로 바꿈, 08b-2) + 놋쇠 단추 둘·더듬이 + 위 새싹 두 잎. 가로 24(1.5배 → 36, 한도 40 안). 상표·글 없음.
손잡이·잎은 X 쪽으로 뻗어 놓인 가구 액터가 돌리지 않아도 옆에서 읽힌다. 슬롯 색은 `import_top_decor_meshes.py` 가 입힌다.
"""
import math

from make_tree_meshes import Mesh
from make_wall_decor_meshes import _cyl
from make_weapon_meshes import _box, _hexa

# 머그잔 — 몸 지름 14, 높이 16, 손잡이까지 가로 20(띠 반지름 7.15 + 손잡이 끝 12.85).
MUG_R, MUG_H, MUG_WALL = 7.0, 16.0, 1.2

# 작은 화분 — 테라코타 아가리 지름 20, 높이 15(테 2), 새싹까지 약 34.
POT_R0, POT_R1, POT_H = 7.5, 10.0, 15.0

# 촛대 — 놋쇠 접시 지름 18(테 19.2), 초 지름 6 × 높이 18, 불꽃까지 약 33, 고리 손잡이까지 가로 23.
DISH_R, CANDLE_R, CANDLE_H = 9.0, 3.0, 18.0


def _leaf(mesh, slot, root, sign, length, width, rise, thick=0.8):
    """뿌리점에서 ±X 로 뻗으며 위로 휘는 납작한 잎(마름모) — 앞뒤 두께 thick."""
    x, y, z = root
    tip = (x + sign * length, z + rise)
    mid_lo = (x + sign * length * 0.45, z + rise * 0.15 - width * 0.35)
    mid_hi = (x + sign * length * 0.55, z + rise * 0.55 + width * 0.5)
    outline = [(x, z), mid_lo, tip, mid_hi]
    if sign < 0:
        outline = outline[::-1]
    _hexa(mesh, slot, [(px, y - thick * 0.5, pz) for px, pz in outline] + [(px, y + thick * 0.5, pz) for px, pz in outline])


def mug():
    """크림 머그 — 몸·꿀색 띠·안쪽 커피·고리 손잡이(+X)·앞(-Y)에 새싹 두 잎 무늬."""
    mesh = Mesh()
    _cyl(mesh, "Body", 0.0, 0.0, 0.0, MUG_H * 0.62, MUG_R * 0.94, MUG_R, 14)
    _cyl(mesh, "Band", 0.0, 0.0, MUG_H * 0.62, MUG_H * 0.80, MUG_R + 0.15, MUG_R + 0.15, 14)
    _cyl(mesh, "Body", 0.0, 0.0, MUG_H * 0.80, MUG_H, MUG_R, MUG_R, 14, cap_slot="Coffee")
    # 아가리 테 — 커피 면 위로 0.6 솟은 고리(안쪽이 들여다보여 「잔」 으로 읽힘)
    _cyl(mesh, "Body", 0.0, 0.0, MUG_H, MUG_H + 0.6, MUG_R, MUG_R, 14)
    _cyl(mesh, "Coffee", 0.0, 0.0, MUG_H + 0.6, MUG_H + 0.64, MUG_R - MUG_WALL, MUG_R - MUG_WALL, 14)
    # 손잡이 — +X 쪽 디귿 고리(위·아래 팔 + 바깥 기둥)
    hx0, hx1, ht = MUG_R - 0.6, MUG_R + 5.85, 1.8
    _box(mesh, "Body", hx0, hx1, -1.2, 1.2, MUG_H * 0.72, MUG_H * 0.72 + ht)
    _box(mesh, "Body", hx0, hx1, -1.2, 1.2, MUG_H * 0.22, MUG_H * 0.22 + ht)
    _box(mesh, "Body", hx1 - ht, hx1, -1.2, 1.2, MUG_H * 0.22, MUG_H * 0.72 + ht)
    # 앞 새싹 무늬 — 줄기 + 두 잎, 몸 앞면에서 0.5 솟음
    fy = -MUG_R - 0.2
    _box(mesh, "Leaf", -0.5, 0.5, fy - 0.4, fy + 0.6, MUG_H * 0.18, MUG_H * 0.45)
    for sign in (-1.0, 1.0):
        _leaf(mesh, "Leaf", (0.0, fy - 0.1, MUG_H * 0.42), sign, 4.0, 2.4, 2.2, thick=0.8)
    return mesh


def flower_pot():
    """테라코타 화분 — 몸·두꺼운 테·흙 + 가운데 새싹(줄기·큰 두 잎·작은 두 잎·노란 봉오리)."""
    mesh = Mesh()
    _cyl(mesh, "Pot", 0.0, 0.0, 0.0, POT_H - 2.5, POT_R0, POT_R1 - 0.8, 12)
    _cyl(mesh, "Rim", 0.0, 0.0, POT_H - 2.5, POT_H, POT_R1, POT_R1, 12, cap_slot="Soil")
    _cyl(mesh, "Soil", 0.0, 0.0, POT_H, POT_H + 0.4, POT_R1 - 1.2, POT_R1 - 1.6, 12)
    base = POT_H + 0.4
    _box(mesh, "Stem", -0.8, 0.8, -0.8, 0.8, base, base + 13.0)
    # 큰 두 잎(±X) — 새싹 모자와 같은 우리 표식, 위 카메라에서도 실루엣이 벌어진다
    for sign in (-1.0, 1.0):
        _leaf(mesh, "Leaf", (0.0, 0.0, base + 9.0), sign, 11.0, 6.0, 6.0, thick=1.2)
    # 작은 두 잎(±Y 쪽으로 기운 짧은 잎) — 앞·옆에서 덤불 부피
    for sign in (-1.0, 1.0):
        y = sign * 1.6
        _hexa(mesh, "Leaf", [(-2.2, y, base + 3.0), (2.2, y, base + 3.0), (2.2, y + sign * 0.8, base + 3.0), (-2.2, y + sign * 0.8, base + 3.0),
                             (-1.4, y + sign * 5.5, base + 8.0), (1.4, y + sign * 5.5, base + 8.0),
                             (1.4, y + sign * 6.3, base + 8.0), (-1.4, y + sign * 6.3, base + 8.0)])
    # 봉오리 — 줄기 끝 노란 덩어리
    _cyl(mesh, "Bud", 0.0, 0.0, base + 12.5, base + 15.5, 1.6, 2.4, 8)
    _cyl(mesh, "Bud", 0.0, 0.0, base + 15.5, base + 17.5, 2.4, 0.6, 8)
    return mesh


def candle():
    """놋쇠 촛대 — 접시·가운데 받침·고리 손잡이(+X) + 밀랍 초·흘러내린 밀랍·심지·두 겹 불꽃."""
    mesh = Mesh()
    _cyl(mesh, "Brass", 0.0, 0.0, 0.0, 1.2, DISH_R, DISH_R, 14)
    _cyl(mesh, "Brass", 0.0, 0.0, 1.2, 2.2, DISH_R, DISH_R + 0.6, 14, cap_slot="BrassDark")   # 접시 테
    _cyl(mesh, "Brass", 0.0, 0.0, 2.2, 5.0, CANDLE_R + 1.6, CANDLE_R + 1.0, 10)              # 초 꽂이 받침
    # 고리 손잡이 — 접시 테에서 +X 로 나온 둥근 귀(네모 고리)
    ex0, ex1 = DISH_R - 0.4, DISH_R + 4.4
    _box(mesh, "Brass", ex0, ex1, -2.4, -1.2, 1.0, 2.2)
    _box(mesh, "Brass", ex0, ex1, 1.2, 2.4, 1.0, 2.2)
    _box(mesh, "Brass", ex1 - 1.2, ex1, -2.4, 2.4, 1.0, 2.2)
    # 밀랍 초 + 흘러내린 자국 둘
    z0 = 5.0
    _cyl(mesh, "Wax", 0.0, 0.0, z0, z0 + CANDLE_H, CANDLE_R, CANDLE_R * 0.95, 10)
    for ang, length in ((-50.0, 6.0), (130.0, 9.0)):
        a = math.radians(ang)
        cx, cy = math.cos(a) * CANDLE_R, math.sin(a) * CANDLE_R
        _box(mesh, "Drip", cx - 0.7, cx + 0.7, cy - 0.7, cy + 0.7, z0 + CANDLE_H - length, z0 + CANDLE_H + 0.3)
    top = z0 + CANDLE_H
    _box(mesh, "Wick", -0.25, 0.25, -0.25, 0.25, top, top + 1.4)
    # 불꽃 — 바깥 주황 물방울 + 안쪽 노랑 심
    _cyl(mesh, "Flame", 0.0, 0.0, top + 1.0, top + 4.0, 0.8, 2.6, 8)
    _cyl(mesh, "Flame", 0.0, 0.0, top + 4.0, top + 9.5, 2.6, 0.2, 8)
    _cyl(mesh, "Glow", 0.0, -1.6, top + 1.8, top + 5.4, 0.6, 1.3, 6)
    return mesh


def radio(lit=False):
    """나무 상자 라디오 — 가로 24 · 깊이 14 · 몸 높이 15(더듬이 끝 28). 짙은 놋쇠 발 넷 · 앞(-Y) 왼쪽 크림 스피커 판 + 짙은 구멍 3×3 ·
    오른쪽 위 다이얼 창(꺼짐 = 짙은 칸 · `lit` 이면 밝은 초록 `Lit` — 켜짐 짝 `Radio_On_01`, 08b-2) + 놋쇠 바늘 · 오른쪽 아래 놋쇠 단추 둘 · 위 오른쪽 놋쇠 더듬이 + 노란 끝 · 위 왼쪽 새싹 두 잎."""
    mesh = Mesh()
    for x0 in (-11.0, 8.0):
        for y0 in (-6.0, 3.0):
            _box(mesh, "BrassDark", x0, x0 + 3.0, y0, y0 + 3.0, 0.0, 1.5)                # 발
    _box(mesh, "Wood", -12.0, 12.0, -7.0, 7.0, 1.5, 15.0)                               # 상자
    _box(mesh, "Body", -11.0, 1.0, -7.6, -7.0, 3.0, 13.0)                               # 스피커 판
    for i in range(3):
        for k in range(3):
            hx, hz = -8.5 + i * 3.5, 5.0 + k * 3.0
            _box(mesh, "Wick", hx - 0.8, hx + 0.8, -7.9, -7.6, hz - 0.8, hz + 0.8)     # 스피커 구멍
    _box(mesh, "Lit" if lit else "Wick", 3.0, 10.5, -7.6, -7.0, 9.0, 13.0)               # 다이얼 창
    _box(mesh, "Brass", 6.0, 6.6, -7.9, -7.6, 9.4, 12.6)                                # 다이얼 바늘
    for kx in (4.5, 8.5):
        _cyl(mesh, "Brass", kx, -7.5, 3.5, 6.5, 1.4, 1.4, 8)                            # 단추(세운 원통, 앞에 반쯤 나옴)
    _box(mesh, "Brass", 8.6, 9.4, 1.6, 2.4, 15.0, 27.0)                                 # 더듬이
    _cyl(mesh, "Bud", 9.0, 2.0, 27.0, 28.0, 1.0, 0.6, 6)                                # 더듬이 끝
    _box(mesh, "Stem", -6.6, -5.4, -0.6, 0.6, 15.0, 19.0)                               # 새싹 줄기
    for sign in (-1.0, 1.0):
        _leaf(mesh, "Leaf", (-6.0, 0.0, 18.0), sign, 4.0, 2.4, 2.2, thick=0.8)
    return mesh


if __name__ == "__main__":
    for name, build, slots in (("Mug_01", mug, ["Body", "Band", "Coffee", "Leaf"]),
                               ("FlowerPot_01", flower_pot, ["Pot", "Rim", "Soil", "Stem", "Leaf", "Bud"]),
                               ("Candle_01", candle, ["Brass", "BrassDark", "Wax", "Drip", "Wick", "Flame", "Glow"]),
                               ("Radio_01", radio, ["Wood", "Body", "Wick", "Brass", "BrassDark", "Bud", "Stem", "Leaf"]),
                               ("Radio_On_01", lambda: radio(lit=True),
                                ["Wood", "Body", "Wick", "Brass", "BrassDark", "Bud", "Stem", "Leaf", "Lit"])):
        mesh = build()
        triangles = mesh.write(name, slots)
        xs, ys, zs = zip(*mesh.verts)
        print("{}.obj 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f}".format(
            name, triangles, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))
