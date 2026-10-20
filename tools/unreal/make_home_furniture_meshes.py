"""M108-04 집 가구 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_home_furniture_meshes.py   → Saved/Generated/ArmChair_01.obj · Sofa_01.obj · TableRound_01.obj ·
                                                          TableLong_01.obj · BedSingle_01.obj · BedDouble_01.obj · Bookshelf_01.obj ·
                                                          Dresser_01.obj · KitchenCounter_01.obj · KitchenSink_01.obj ·
                                                          PottedTree_01.obj · PottedBerry_01.obj · FlowerBox_01.obj ·
                                                          PottedSprout_01.obj · RugRound_01.obj · RugRunner_01.obj ·
                                                          RugSquare_01.obj · Doormat_01.obj · FencePicket_01.obj ·
                                                          PathLog_01.obj · GardenArch_01.obj · Mailbox_01.obj ·
                                                          CandleStand_01.obj · OilLamp_01.obj · LanternPost_01.obj ·
                                                          GardenTorch_01.obj · StairsWood_01.obj · Generator_01.obj ·
                                                          Fridge_01.obj · FanStand_01.obj · Fridge_On_01.obj · FanStand_On_01.obj ·
                                                          FanBlade_01.obj (+ .mtl)

DESIGN 8.11 「건축가 꾸미기」 가구 종류 10 → 20 — 앞 다섯(앉기 2 · 탁자 2 · 침대 1, 루프#1385) · 뒤 다섯(침대 1 · 수납 2 · 주방 2, 루프#1386).
가구 20 → 40 의 식물 넷(화분 나무 · 열매 덤불 화분 · 꽃 상자 · 새싹 큰 화분, M108-05a 루프#1388) · 러그 넷(둥근 · 긴 복도 ·
네모 무늬 · 문 앞 깔개, M108-05b 루프#1389 — 두께 1.5, 무늬는 0.05~0.15 씩 위로 쌓아 겹면이 안 깜빡이게) · 바깥 넷(말뚝 울타리 ·
통나무 디딤 길 · 꽃 아치 · 우편함, M108-05d 루프#1391) · 조명 넷(세움 촛대 · 등잔대 · 기둥 등불 · 정원 횃불,
M108-05e 루프#1392 — 불꽃 칸 `Flame` 은 팔레트 끝에 붙임) · 나무 계단(M108-06b 루프#1394 — 3×1, 2층 바닥 275 까지) · 연료 발전기(M108-07b 루프#1403 — 1×1) ·
전기 가구 둘(냉장고 · 선풍기, M108-08b 루프#1410 — 꺼진 모습 · 켜짐 짝 `_On_01` 은 08b-2 루프#1411, 불 칸만 `Lit` · 날개 따로는 08d). 원본(레퍼런스 게임) 가구는 따르지 않는다
(Reference 6절): 꿀색 나무 틀 + **세이지 초록 천**(우리 새싹 색) + 크림 방석·이불, 곳곳에 새싹 두 잎(새싹 모자·벽시계와 같은 우리 표식).
그린 치수 cm = 표 `PlacedMeshWidthCm`(배율 1), **밑면 가운데 = (0, 0, 0), Z 위, 긴 변 = X**(놓인 가구 액터가 돌리지 않는다),
앉는 쪽·침대 옆 = **-Y**(언리얼로 반입하면 Y 가 뒤집혀 +Y). 면마다 정점을 따로 둬 한 면 한 색(world_environment.md "한 면 한 색").
슬롯 색은 `import_home_furniture_meshes.py` 가 팔레트 그림 한 장(섹션 1 — DESIGN 8.11 드로콜 줄)으로 입힌다.
"""
import math

from make_tree_meshes import Mesh
from make_wall_decor_meshes import _cyl, _disc_y, _prism_y
from make_weapon_meshes import _box, _hexa


def _leaf_y(mesh, slot, root, sign, length, width, rise, thick=1.2):
    """뿌리점에서 ±Y 로 뻗으며 위로 휘는 납작한 잎(마름모) — 앞뒤(X) 두께 thick. `make_top_decor_meshes._leaf` 의 Y 판."""
    x, y, z = root
    tip = (y + sign * length, z + rise)
    mid_lo = (y + sign * length * 0.45, z + rise * 0.15 - width * 0.35)
    mid_hi = (y + sign * length * 0.55, z + rise * 0.55 + width * 0.5)
    outline = [(y, z), mid_lo, tip, mid_hi]
    if sign < 0:
        outline = outline[::-1]
    _hexa(mesh, slot, [(x - thick * 0.5, py, pz) for py, pz in outline] + [(x + thick * 0.5, py, pz) for py, pz in outline])


def _leaf_x(mesh, slot, root, sign, length, width, rise, thick=1.2):
    """뿌리점에서 ±X 로 뻗는 잎 — 앞뒤(Y) 두께 thick."""
    x, y, z = root
    tip = (x + sign * length, z + rise)
    mid_lo = (x + sign * length * 0.45, z + rise * 0.15 - width * 0.35)
    mid_hi = (x + sign * length * 0.55, z + rise * 0.55 + width * 0.5)
    outline = [(x, z), mid_lo, tip, mid_hi]
    if sign < 0:
        outline = outline[::-1]
    _hexa(mesh, slot, [(px, y - thick * 0.5, pz) for px, pz in outline] + [(px, y + thick * 0.5, pz) for px, pz in outline])


def _leaf_flat(mesh, slot, root, angle, length, width, z1):
    """윗면에 눕힌 잎(마름모) — root 에서 angle(도, +X = 0) 쪽으로 length, 두께 = root z → z1. 탁자 윗면 수."""
    x, y, z0 = root
    a = math.radians(angle)
    ax, ay = math.cos(a), math.sin(a)
    px, py = -ay, ax
    outline = [(x, y), (x + ax * length * 0.45 + px * width * 0.5, y + ay * length * 0.45 + py * width * 0.5),
               (x + ax * length, y + ay * length), (x + ax * length * 0.55 - px * width * 0.5, y + ay * length * 0.55 - py * width * 0.5)]
    _hexa(mesh, slot, [(ox, oy, z0) for ox, oy in outline] + [(ox, oy, z1) for ox, oy in outline])


def _legs(mesh, slot, half_x, half_y, inset, size, z1):
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = sx * (half_x - inset - size * 0.5), sy * (half_y - inset - size * 0.5)
            _box(mesh, slot, cx - size * 0.5, cx + size * 0.5, cy - size * 0.5, cy + size * 0.5, 0.0, z1)


def _seat(mesh, half_x, depth, arm_w, seats, back_h):
    """안락의자·소파 공통 — 다리 · 나무 받침 · 팔걸이(천 + 나무 덮개) · 크림 방석 seats 개 · 초록 등받이. 앞 = -Y."""
    half_y = depth * 0.5
    _legs(mesh, "WoodDark", half_x, half_y, 2.0, 6.0, 12.0)
    _box(mesh, "Wood", -half_x, half_x, -half_y, half_y, 12.0, 28.0)
    for sx in (-1, 1):
        x0, x1 = sorted((sx * half_x, sx * (half_x - arm_w)))
        _box(mesh, "Fabric", x0, x1, -half_y, half_y, 28.0, 58.0)
        _box(mesh, "Wood", x0, x1, -half_y - 1.0, half_y, 58.0, 62.0)
    inner = half_x - arm_w
    back_y0 = half_y - 13.0
    span = 2.0 * inner / seats
    for i in range(seats):
        x0 = -inner + i * span + 0.8
        x1 = -inner + (i + 1) * span - 0.8
        _box(mesh, "Cushion", x0, x1, -half_y + 1.0, back_y0, 28.0, 43.0, top_inset=1.5)
        _box(mesh, "Fabric", x0, x1, back_y0 - 4.0, back_y0 + 1.0, 43.0, back_h - 6.0, top_inset=1.0)   # 등 방석
    _box(mesh, "Fabric", -inner, inner, back_y0, half_y, 28.0, back_h, top_inset=2.0)
    _box(mesh, "Wood", -inner, inner, back_y0 + 2.0, half_y + 0.5, back_h, back_h + 3.0)        # 등받이 위 나무 덮개
    # 새싹 두 잎 — 등받이 위 나무 덮개 가운데에서 솟는다(우리 표식)
    root = (0.0, half_y - 5.0, back_h + 3.0)
    _box(mesh, "Leaf", -0.8, 0.8, root[1] - 0.8, root[1] + 0.8, back_h + 3.0, back_h + 7.0)
    _leaf_x(mesh, "Leaf", (0.0, root[1], back_h + 6.0), -1, 9.0, 5.0, 6.0)
    _leaf_x(mesh, "Leaf", (0.0, root[1], back_h + 6.0), 1, 9.0, 5.0, 6.0)


def arm_chair():
    """안락의자 — 폭 80 × 깊이 76, 앉는 높이 43, 등 높이 88."""
    mesh = Mesh()
    _seat(mesh, 40.0, 76.0, 10.0, 1, 88.0)
    # 쿠션 하나 — 꿀색 작은 베개를 왼쪽 팔걸이에 기대어
    _box(mesh, "Accent", -29.0, -15.0, 10.0, 22.0, 43.0, 60.0, top_inset=2.0)
    return mesh


def sofa():
    """소파 — 폭 170 × 깊이 82, 방석 셋, 앉는 높이 43, 등 높이 86."""
    mesh = Mesh()
    _seat(mesh, 85.0, 82.0, 11.0, 3, 86.0)
    _box(mesh, "Accent", -73.0, -57.0, 12.0, 26.0, 43.0, 62.0, top_inset=2.0)
    _box(mesh, "Accent", 57.0, 73.0, 12.0, 26.0, 43.0, 62.0, top_inset=2.0)
    return mesh


def table_round():
    """둥근 탁자 — 지름 90, 윗면 76. 기둥 하나 + 십자 발. 윗면 가운데 크림 둥근 깔개 + 새싹 수(우리 표식)."""
    mesh = Mesh()
    r, top, sides = 45.0, 76.0, 16
    _box(mesh, "WoodDark", -32.0, 32.0, -4.0, 4.0, 0.0, 7.0)                       # 십자 발
    _box(mesh, "WoodDark", -4.0, 4.0, -32.0, 32.0, 0.0, 7.0)
    _cyl(mesh, "WoodDark", 0.0, 0.0, 7.0, 12.0, 10.0, 6.0, 8)
    _cyl(mesh, "Wood", 0.0, 0.0, 12.0, top - 6.0, 5.0, 5.0, 8)
    _cyl(mesh, "WoodDark", 0.0, 0.0, top - 6.0, top - 4.0, 6.0, 14.0, 8)
    _cyl(mesh, "WoodDark", 0.0, 0.0, top - 4.0, top - 1.5, r - 0.5, r, sides)   # 테
    _cyl(mesh, "Wood", 0.0, 0.0, top - 1.5, top, r, r - 1.5, sides)
    _cyl(mesh, "Cushion", 0.0, 0.0, top, top + 0.4, 22.0, 22.0, sides)            # 크림 깔개
    _leaf_flat(mesh, "Leaf", (0.0, -4.0, top + 0.4), 150.0, 16.0, 8.0, top + 0.8)       # 새싹 두 잎(위에서 V)
    _leaf_flat(mesh, "Leaf", (0.0, -4.0, top + 0.4), 30.0, 16.0, 8.0, top + 0.8)
    return mesh


def table_long():
    """긴 식탁 — 190 × 90, 윗면 76. 판자 셋(두 빛 나무) + 네 다리 + 띠 + 가운데 크림 식탁보 띠·새싹 수 둘."""
    mesh = Mesh()
    hx, hy, top = 95.0, 45.0, 76.0
    _legs(mesh, "WoodDark", hx, hy, 6.0, 8.0, top - 5.0)
    _box(mesh, "WoodDark", -hx + 10.0, hx - 10.0, -hy + 9.0, -hy + 11.0, top - 14.0, top - 5.0)
    _box(mesh, "WoodDark", -hx + 10.0, hx - 10.0, hy - 11.0, hy - 9.0, top - 14.0, top - 5.0)
    board = 2.0 * hy / 3.0
    for i, slot in enumerate(("Wood", "WoodLight", "Wood")):
        y0 = -hy + i * board + (0.4 if i else 0.0)
        y1 = -hy + (i + 1) * board - (0.4 if i < 2 else 0.0)
        _box(mesh, slot, -hx, hx, y0, y1, top - 5.0, top)
    _box(mesh, "Cushion", -hx + 25.0, hx - 25.0, -14.0, 14.0, top, top + 0.5)      # 크림 식탁보 띠
    for cx in (-40.0, 40.0):                                                         # 식탁보 위 새싹 수 둘
        _leaf_flat(mesh, "Leaf", (cx, -4.0, top + 0.5), 135.0, 13.0, 7.0, top + 0.9)
        _leaf_flat(mesh, "Leaf", (cx, -4.0, top + 0.5), 45.0, 13.0, 7.0, top + 0.9)
    return mesh


def bed_single():
    """1인 침대 — 길이 195 × 폭 95. 머리판 = +X 끝(높이 95, 새싹 두 잎), 발판 = -X 끝(55). 크림 요 · 초록 이불 · 흰 베개."""
    mesh = Mesh()
    hx, hy = 97.5, 47.5
    _legs(mesh, "WoodDark", hx, hy, 0.0, 7.0, 10.0)
    _box(mesh, "Wood", -hx + 6.0, hx - 9.0, -hy, hy, 10.0, 30.0)                      # 틀
    _box(mesh, "Wood", hx - 9.0, hx, -hy, hy, 10.0, 92.0, top_inset=0.0)             # 머리판
    _box(mesh, "WoodDark", hx - 10.0, hx + 0.0, -hy - 0.5, hy + 0.5, 92.0, 96.0)
    _box(mesh, "Wood", -hx, -hx + 6.0, -hy, hy, 10.0, 52.0)                           # 발판
    _box(mesh, "WoodDark", -hx, -hx + 6.5, -hy - 0.5, hy + 0.5, 52.0, 55.0)
    _box(mesh, "Cushion", -hx + 7.0, hx - 10.0, -hy + 3.0, hy - 3.0, 30.0, 46.0)      # 요
    _box(mesh, "Fabric", -hx + 7.0, 30.0, -hy + 1.0, hy - 1.0, 30.0, 48.0, top_inset=1.0)   # 이불
    _box(mesh, "Cushion", 24.0, 32.0, -hy + 1.0, hy - 1.0, 30.0, 49.5, top_inset=1.0)       # 접힌 이불 끝
    _box(mesh, "Pillow", 52.0, 82.0, -30.0, 30.0, 46.0, 56.0, top_inset=3.0)
    # 머리판 앞(-X 면)에 새싹 두 잎
    root = (hx - 9.5, 0.0, 72.0)
    _box(mesh, "Leaf", root[0] - 0.8, root[0] + 0.8, -0.8, 0.8, 62.0, 74.0)
    _leaf_y(mesh, "Leaf", root, -1, 13.0, 7.0, 8.0)
    _leaf_y(mesh, "Leaf", root, 1, 13.0, 7.0, 8.0)
    return mesh


def bed_double():
    """2인 침대 — 길이 195 × 폭 150(2×2 칸). 1인 침대와 같은 틀·색에 베개 둘, 머리판 위 새싹 두 잎."""
    mesh = Mesh()
    hx, hy = 97.5, 75.0
    _legs(mesh, "WoodDark", hx, hy, 0.0, 8.0, 10.0)
    _box(mesh, "Wood", -hx + 6.0, hx - 9.0, -hy, hy, 10.0, 30.0)                      # 틀
    _box(mesh, "Wood", hx - 9.0, hx, -hy, hy, 10.0, 100.0)                           # 머리판
    _box(mesh, "WoodDark", hx - 10.0, hx, -hy - 0.5, hy + 0.5, 100.0, 104.0)
    _box(mesh, "Wood", -hx, -hx + 6.0, -hy, hy, 10.0, 52.0)                           # 발판
    _box(mesh, "WoodDark", -hx, -hx + 6.5, -hy - 0.5, hy + 0.5, 52.0, 55.0)
    _box(mesh, "Cushion", -hx + 7.0, hx - 10.0, -hy + 3.0, hy - 3.0, 30.0, 46.0)      # 요
    _box(mesh, "Fabric", -hx + 7.0, 30.0, -hy + 1.0, hy - 1.0, 30.0, 48.0, top_inset=1.0)   # 이불
    _box(mesh, "Cushion", 24.0, 32.0, -hy + 1.0, hy - 1.0, 30.0, 49.5, top_inset=1.0)       # 접힌 이불 끝
    _box(mesh, "Accent", -20.0, 10.0, -hy + 1.5, hy - 1.5, 48.0, 49.0)                      # 이불 위 꿀색 띠
    for cy in (-35.0, 35.0):                                                                # 베개 둘
        _box(mesh, "Pillow", 52.0, 82.0, cy - 27.0, cy + 27.0, 46.0, 56.0, top_inset=3.0)
    # 머리판 위 나무 덮개 가운데에서 솟는 새싹 두 잎(소파 등받이와 같은 자리)
    root = (hx - 5.0, 0.0, 104.0)
    _box(mesh, "Leaf", root[0] - 0.8, root[0] + 0.8, -0.8, 0.8, 104.0, 108.0)
    _leaf_y(mesh, "Leaf", (root[0], 0.0, 107.0), -1, 11.0, 6.0, 7.0)
    _leaf_y(mesh, "Leaf", (root[0], 0.0, 107.0), 1, 11.0, 6.0, 7.0)
    return mesh


def _cabinet(mesh, hx, hy, top):
    """수납·주방 공통 몸통 — 짙은 굽(앞 들임) · 꿀색 옆판·뒤판 · 앞(-Y) 면은 서랍·문이 덮는다."""
    _box(mesh, "WoodDark", -hx + 2.0, hx - 2.0, -hy + 3.0, hy, 0.0, 8.0)             # 굽
    _box(mesh, "Wood", -hx, hx, -hy + 1.0, hy, 8.0, top)                               # 몸통


def _knob(mesh, slot, cx, z, y_front, size=3.0, depth=2.5):
    _box(mesh, slot, cx - size * 0.5, cx + size * 0.5, y_front - depth, y_front, z - size * 0.5, z + size * 0.5)


def _sprout_front(mesh, cx, y_front, z, scale=1.0):
    """앞(-Y) 면에 붙인 새싹 두 잎 — 줄기 + 좌우 잎(서랍장·조리대 문 표식)."""
    _box(mesh, "Leaf", cx - 0.7, cx + 0.7, y_front - 1.0, y_front, z - 6.0 * scale, z + 1.0)
    _leaf_x(mesh, "Leaf", (cx, y_front - 0.6, z), -1, 7.0 * scale, 4.0 * scale, 4.0 * scale)
    _leaf_x(mesh, "Leaf", (cx, y_front - 0.6, z), 1, 7.0 * scale, 4.0 * scale, 4.0 * scale)


def bookshelf():
    """책장 — 폭 90 × 깊이 35 × 높이 180. 칸 넷, 칸마다 색 다른 책등(초록·꿀색·크림·딸기·밝은 나무), 맨 위 화분 하나."""
    mesh = Mesh()
    hx, hy, top = 45.0, 17.5, 180.0
    _box(mesh, "WoodDark", -hx, hx, -hy, hy, 0.0, 8.0)                                 # 굽
    _box(mesh, "Wood", -hx, -hx + 3.0, -hy, hy, 8.0, top)                              # 옆판 둘
    _box(mesh, "Wood", hx - 3.0, hx, -hy, hy, 8.0, top)
    _box(mesh, "WoodDark", -hx + 3.0, hx - 3.0, hy - 2.0, hy, 8.0, top)                # 뒤판(칸 안이 어둡게)
    _box(mesh, "Wood", -hx - 1.0, hx + 1.0, -hy - 1.0, hy, top, top + 3.0)             # 윗판
    shelves = (8.0, 50.0, 92.0, 134.0)
    for z in shelves[1:]:
        _box(mesh, "Wood", -hx + 3.0, hx - 3.0, -hy, hy - 2.0, z - 2.5, z)
    colors = ("Fabric", "Accent", "Cushion", "Berry", "WoodLight", "Fabric", "Berry", "Accent")
    inner = hx - 3.0
    for row, z0 in enumerate(shelves):
        x = -inner + 2.0 + row * 3.0
        i = row * 2
        limit = inner - (14.0 if row % 2 == 0 else 4.0)
        while x < limit:
            w = 6.0 + (i * 7 % 4)                                                      # 책 두께 6~9
            h = 28.0 + (i * 5 % 9)                                                     # 책 높이 28~36
            if x + w > limit:
                break
            _box(mesh, colors[i % len(colors)], x, x + w - 0.4, -hy + 3.0, hy - 4.0, z0, z0 + h)
            x += w
            i += 1
        if row % 2 == 0:                                                               # 짝수 칸 끝에 기울인 책 대신 꿀단지 하나
            _cyl(mesh, "Accent", inner - 7.0, 0.0, z0, z0 + 12.0, 5.0, 4.0, 8)
            _cyl(mesh, "Cushion", inner - 7.0, 0.0, z0 + 12.0, z0 + 14.0, 3.0, 3.0, 8)
    # 맨 위 작은 화분 + 새싹 두 잎(우리 표식)
    _cyl(mesh, "Berry", 22.0, 0.0, top + 3.0, top + 15.0, 7.0, 9.0, 8)
    _box(mesh, "Leaf", 21.2, 22.8, -0.8, 0.8, top + 15.0, top + 21.0)
    _leaf_x(mesh, "Leaf", (22.0, 0.0, top + 20.0), -1, 10.0, 6.0, 6.0)
    _leaf_x(mesh, "Leaf", (22.0, 0.0, top + 20.0), 1, 10.0, 6.0, 6.0)
    return mesh


def dresser():
    """서랍장 — 폭 90 × 깊이 45 × 높이 85. 서랍 셋(가운데만 밝은 나무), 놋쇠 손잡이 둘씩, 윗서랍 가운데 새싹 두 잎."""
    mesh = Mesh()
    hx, hy, top = 45.0, 22.5, 82.0
    _cabinet(mesh, hx, hy, top)
    _box(mesh, "WoodDark", -hx - 1.5, hx + 1.5, -hy - 1.5, hy, top, top + 3.0)         # 윗판(앞으로 내민 테)
    front = -hy + 1.0
    rows = ((10.0, 33.0), (35.0, 57.0), (59.0, 80.0))
    for i, (z0, z1) in enumerate(rows):
        _box(mesh, "WoodLight" if i == 1 else "Wood", -hx + 3.0, hx - 3.0, front - 1.5, front, z0, z1, top_inset=0.0)
        for cx in (-22.0, 22.0):
            _knob(mesh, "Accent", cx, (z0 + z1) * 0.5, front - 1.5)
    _sprout_front(mesh, 0.0, front - 1.5, 72.0, 0.8)
    return mesh


def kitchen_counter():
    """조리대 — 폭 95 × 깊이 60 × 높이 90. 문 둘(초록 칠) · 회색 돌 윗판 · 문 위 새싹 두 잎 수. 윗판은 비워 둔다 — 받침 슬롯 2
    (M108-04c)에 플레이어가 소품을 올린다(빵·쟁반을 그려 두면 슬롯 소품과 겹치고 메시 꼭대기가 올라가 소품이 뜬다)."""
    mesh = Mesh()
    hx, hy, top = 47.5, 30.0, 86.0
    _cabinet(mesh, hx, hy, top)
    _box(mesh, "Stone", -hx - 1.0, hx + 1.0, -hy - 2.0, hy, top, top + 4.0)           # 돌 윗판
    front = -hy + 1.0
    for sx in (-1, 1):                                                                 # 문 둘
        x0, x1 = sorted((sx * 2.0, sx * (hx - 3.0)))
        _box(mesh, "Fabric", x0, x1, front - 1.5, front, 11.0, top - 4.0)
        _knob(mesh, "Metal", sx * 6.0, top - 16.0, front - 1.5, size=2.5)
    _sprout_front(mesh, -24.0, front - 1.5, 40.0, 0.9)
    _sprout_front(mesh, 24.0, front - 1.5, 40.0, 0.9)
    return mesh


def kitchen_sink():
    """개수대 — 조리대와 같은 몸통(폭 95 × 깊이 60 × 높이 90), 돌 윗판 가운데 쇠 개수통(움푹) + 뒤 수도꼭지."""
    mesh = Mesh()
    hx, hy, top = 47.5, 30.0, 86.0
    bx0, bx1, by0, by1, z0, z1 = -26.0, 26.0, -18.0, 14.0, top, top + 4.0      # 개수통 구멍 x -26..26, y -18..14
    floor = top - 14.0                                                                 # 개수통 바닥(움푹 18cm)
    # 몸통은 개수통 바닥 아래까지만 — 위까지 채우면 몸통 윗면이 개수통을 덮는다. 그 위는 구멍 둘레 네 조각
    _cabinet(mesh, hx, hy, floor - 1.0)
    _box(mesh, "Wood", -hx, hx, -hy + 1.0, by0, floor - 1.0, top)
    _box(mesh, "Wood", -hx, hx, by1, hy, floor - 1.0, top)
    _box(mesh, "Wood", -hx, bx0, by0, by1, floor - 1.0, top)
    _box(mesh, "Wood", bx1, hx, by0, by1, floor - 1.0, top)
    front = -hy + 1.0
    for sx in (-1, 1):
        x0, x1 = sorted((sx * 2.0, sx * (hx - 3.0)))
        _box(mesh, "Fabric", x0, x1, front - 1.5, front, 11.0, top - 4.0)
        _knob(mesh, "Metal", sx * 6.0, top - 16.0, front - 1.5, size=2.5)
    _sprout_front(mesh, -24.0, front - 1.5, 40.0, 0.9)
    _sprout_front(mesh, 24.0, front - 1.5, 40.0, 0.9)
    # 돌 윗판을 개수통 구멍 둘레 네 조각으로
    _box(mesh, "Stone", -hx - 1.0, hx + 1.0, -hy - 2.0, by0, z0, z1)
    _box(mesh, "Stone", -hx - 1.0, hx + 1.0, by1, hy, z0, z1)
    _box(mesh, "Stone", -hx - 1.0, bx0, by0, by1, z0, z1)
    _box(mesh, "Stone", bx1, hx + 1.0, by0, by1, z0, z1)
    _box(mesh, "MetalDark", bx0, bx1, by0, by1, floor - 1.0, floor)
    _box(mesh, "Metal", bx0, bx1, by0, by0 + 1.0, floor, z1 - 0.2)                     # 개수통 벽 넷
    _box(mesh, "Metal", bx0, bx1, by1 - 1.0, by1, floor, z1 - 0.2)
    _box(mesh, "Metal", bx0, bx0 + 1.0, by0, by1, floor, z1 - 0.2)
    _box(mesh, "Metal", bx1 - 1.0, bx1, by0, by1, floor, z1 - 0.2)
    _cyl(mesh, "WoodDark", 0.0, -2.0, floor, floor + 0.3, 2.5, 2.5, 8)                  # 물 빠지는 구멍
    # 수도꼭지 — 뒤 가운데 기둥 + 앞으로 굽은 꼭지 + 손잡이 둘
    _cyl(mesh, "Metal", 0.0, 21.0, z1, z1 + 24.0, 2.2, 2.0, 8)
    _box(mesh, "Metal", -1.8, 1.8, 6.0, 22.5, z1 + 21.0, z1 + 25.0)
    _box(mesh, "Metal", -1.8, 1.8, 6.0, 9.5, z1 + 15.0, z1 + 21.0)
    for cx in (-9.0, 9.0):
        _cyl(mesh, "Accent", cx, 21.0, z1, z1 + 6.0, 2.5, 2.5, 8)
    return mesh


def _blob(mesh, layers, cx=0.0, cy=0.0, sides=10):
    """잎 덩어리 — (z0, z1, r0, r1, 슬롯) 층을 쌓은 원뿔대들. 층마다 두 초록을 번갈아 위 카메라에서 덩어리 결이 보인다."""
    for z0, z1, r0, r1, slot in layers:
        _cyl(mesh, slot, cx, cy, z0, z1, r0, r1, sides)


def _tub(mesh, half, z1):
    """네모 나무 화분 — 짙은 굽 · 꿀색 몸 · 위 짙은 테 · 흙(짙은 나무색)."""
    _box(mesh, "WoodDark", -half + 2.0, half - 2.0, -half + 2.0, half - 2.0, 0.0, 4.0)
    _box(mesh, "Wood", -half, half, -half, half, 4.0, z1 - 3.0)
    _box(mesh, "WoodDark", -half - 1.0, half + 1.0, -half - 1.0, half + 1.0, z1 - 3.0, z1)
    _box(mesh, "WoodDark", -half + 2.0, half - 2.0, -half + 2.0, half - 2.0, z1, z1 + 0.5)


def potted_tree():
    """화분 나무 — 네모 나무 화분 44 · 줄기 · 둥근 잎 덩어리 지름 60 · 높이 150. 화분 앞 꿀색 띠 + 새싹 두 잎(우리 표식)."""
    mesh = Mesh()
    _tub(mesh, 21.0, 36.0)
    _box(mesh, "Accent", -21.5, 21.5, -21.5, 21.5, 16.0, 20.0)                        # 화분 허리 꿀색 띠
    _cyl(mesh, "WoodDark", 0.0, 0.0, 36.0, 92.0, 4.5, 3.0, 8)                           # 줄기
    _box(mesh, "WoodDark", -1.5, 14.0, -1.5, 1.5, 70.0, 73.0)                           # 곁가지
    _blob(mesh, ((86.0, 104.0, 14.0, 27.0, "Leaf"), (104.0, 124.0, 27.0, 30.0, "LeafLight"),
                 (124.0, 140.0, 30.0, 22.0, "Leaf"), (140.0, 150.0, 22.0, 7.0, "LeafLight")))
    _blob(mesh, ((70.0, 78.0, 4.0, 8.0, "LeafLight"), (78.0, 84.0, 8.0, 3.0, "Leaf")), cx=15.0, sides=6)   # 곁가지 끝 잎뭉치
    _sprout_front(mesh, 0.0, -22.0, 30.0, 0.8)
    return mesh


def potted_berry():
    """열매 덤불 화분 — 둥근 나무통(지름 50, 테 둘) · 낮고 넓은 덤불 지름 70 · 빨간 열매 여덟. 높이 약 72."""
    mesh = Mesh()
    _cyl(mesh, "Wood", 0.0, 0.0, 0.0, 30.0, 23.0, 25.0, 12)                             # 통
    for z in (5.0, 24.0):
        _cyl(mesh, "WoodDark", 0.0, 0.0, z, z + 3.0, 23.6 + (z / 30.0) * 2.0, 23.8 + ((z + 3.0) / 30.0) * 2.0, 12)   # 테
    _cyl(mesh, "WoodDark", 0.0, 0.0, 30.0, 30.5, 24.0, 24.0, 12)                        # 흙
    _blob(mesh, ((30.0, 36.0, 23.0, 30.0, "Leaf"), (36.0, 46.0, 30.0, 34.0, "LeafLight"), (46.0, 56.0, 34.0, 31.0, "Leaf"),
                 (56.0, 65.0, 31.0, 22.0, "LeafLight"), (65.0, 72.0, 22.0, 6.0, "Leaf")), sides=12)
    for i in range(8):                                                                   # 열매 — 덤불 겉에 반쯤 묻힌 작은 덩이
        t = math.tau * (i + 0.25) / 8
        r, z = (31.0, 50.0) if i % 2 == 0 else (22.0, 63.0)
        x, y = r * math.cos(t), r * math.sin(t) * 0.95
        _cyl(mesh, "Petal", x, y, z - 3.0, z + 3.0, 3.5, 3.0, 6)
    return mesh


def flower_box():
    """꽃 상자 — 긴 나무 상자 95 × 40 × 35 · 꽃 두 줄(빨강·노랑 번갈아) 높이 약 60 · 앞 판에 새싹 두 잎."""
    mesh = Mesh()
    hx, hy, top = 47.5, 20.0, 35.0
    _box(mesh, "WoodDark", -hx + 3.0, hx - 3.0, -hy + 2.0, hy - 2.0, 0.0, 5.0)           # 발
    _box(mesh, "Wood", -hx, hx, -hy, hy, 5.0, top - 3.0)
    _box(mesh, "WoodLight", -hx + 4.0, hx - 4.0, -hy - 1.0, -hy, 9.0, top - 7.0)         # 앞 판자 밝은 띠
    _box(mesh, "WoodDark", -hx - 0.5, hx + 0.5, -hy - 1.0, hy + 1.0, top - 3.0, top)
    _box(mesh, "WoodDark", -hx + 2.0, hx - 2.0, -hy + 2.0, hy - 2.0, top, top + 0.5)     # 흙
    _sprout_front(mesh, 0.0, -hy - 1.0, 21.0, 0.8)
    i = 0
    for row_y, h0 in ((-7.0, 50.0), (8.0, 58.0)):                                         # 앞줄 낮게 · 뒷줄 높게
        for k in range(6):
            cx = -36.0 + k * 14.4 + (0.0 if row_y < 0 else 7.0)
            if cx > hx - 8.0:
                continue
            h = h0 + (k * 7 % 5)
            _box(mesh, "Leaf", cx - 0.8, cx + 0.8, row_y - 0.8, row_y + 0.8, top, h)      # 줄기
            _leaf_x(mesh, "LeafLight", (cx, row_y, top + 8.0), 1 if k % 2 else -1, 8.0, 4.0, 5.0)
            petal = "Petal" if i % 2 == 0 else "Accent"
            _cyl(mesh, petal, cx, row_y, h, h + 3.0, 2.5, 6.0, 6)                         # 꽃잎 접시
            _cyl(mesh, "WoodDark" if petal == "Petal" else "WoodLight", cx, row_y, h + 3.0, h + 4.0, 2.0, 1.5, 6)   # 꽃술
            i += 1
    return mesh


def potted_sprout():
    """새싹 큰 화분 — 테라코타 화분(아가리 지름 44 · 높이 40) · 우리 새싹 두 큰 잎(펼친 폭 74) · 가운데 꿀색 봉오리. 높이 약 110."""
    mesh = Mesh()
    _cyl(mesh, "Clay", 0.0, 0.0, 0.0, 34.0, 16.0, 20.0, 12)
    _cyl(mesh, "Clay", 0.0, 0.0, 34.0, 40.0, 22.0, 22.0, 12)                            # 아가리 테
    _cyl(mesh, "WoodDark", 0.0, 0.0, 40.0, 40.5, 20.5, 20.5, 12)                        # 흙
    _box(mesh, "Leaf", -1.5, 1.5, -1.5, 1.5, 40.0, 82.0)                                # 줄기
    _leaf_x(mesh, "Leaf", (0.0, 0.0, 78.0), -1, 37.0, 22.0, 26.0, thick=2.0)
    _leaf_x(mesh, "LeafLight", (0.0, 0.0, 78.0), 1, 37.0, 22.0, 26.0, thick=2.0)
    _cyl(mesh, "Accent", 0.0, 0.0, 80.0, 90.0, 3.0, 5.0, 8)                             # 봉오리
    _cyl(mesh, "Accent", 0.0, 0.0, 90.0, 96.0, 5.0, 1.0, 8)
    return mesh


def _diamond(mesh, slot, cx, cy, rx, ry, z0, z1):
    """눕힌 마름모 판 — 러그 무늬. 가운데 (cx, cy), X 반폭 rx · Y 반폭 ry, 두께 z0 → z1."""
    outline = [(cx - rx, cy), (cx, cy - ry), (cx + rx, cy), (cx, cy + ry)]
    _hexa(mesh, slot, [(x, y, z0) for x, y in outline] + [(x, y, z1) for x, y in outline])


def _sprout_flat(mesh, cx, cy, z0, size):
    """윗면에 눕힌 새싹 두 잎(위에서 V) — 러그 수. 둥근 탁자 깔개 수와 같은 모양."""
    _leaf_flat(mesh, "Leaf", (cx, cy - size * 0.25, z0), 150.0, size, size * 0.5, z0 + 0.3)
    _leaf_flat(mesh, "Leaf", (cx, cy - size * 0.25, z0), 30.0, size, size * 0.5, z0 + 0.3)


def rug_round():
    """둥근 러그 — 지름 180 · 두께 1.5. 세이지 초록 테 · 크림 고리 · 초록 가는 고리 · 꿀색 가운데 · 새싹 두 잎 수."""
    mesh = Mesh()
    for z0, z1, r, slot in ((0.0, 1.2, 90.0, "Fabric"), (1.2, 1.35, 78.0, "Cushion"), (1.35, 1.45, 50.0, "Fabric"),
                            (1.45, 1.5, 46.0, "Cushion"), (1.5, 1.55, 22.0, "Accent")):
        _cyl(mesh, slot, 0.0, 0.0, z0, z1, r, r, 24)
    _sprout_flat(mesh, 0.0, 0.0, 1.55, 14.0)
    for i in range(8):                                                       # 크림 고리 위 초록 점 여덟
        t = math.tau * (i + 0.5) / 8
        _diamond(mesh, "Fabric", 64.0 * math.cos(t), 64.0 * math.sin(t), 5.0, 5.0, 1.35, 1.45)
    return mesh


def rug_runner():
    """긴 복도 러그 — 270 × 80 · 두께 1.5. 크림 바탕 · 초록 테 · 초록 띠 셋 · 띠 사이 꿀색 마름모 · 양끝 크림 술."""
    mesh = Mesh()
    hx, hy = 127.0, 40.0
    _box(mesh, "Fabric", -hx, hx, -hy, hy, 0.0, 1.2)
    _box(mesh, "Cushion", -hx + 6.0, hx - 6.0, -hy + 6.0, hy - 6.0, 1.2, 1.35)
    for cx in (-80.0, 0.0, 80.0):
        _box(mesh, "Fabric", cx - 7.0, cx + 7.0, -hy + 6.0, hy - 6.0, 1.35, 1.45)
    for cx in (-40.0, 40.0, -112.0, 112.0):
        _diamond(mesh, "Accent", cx, 0.0, 9.0 if abs(cx) < 100 else 5.0, 14.0 if abs(cx) < 100 else 10.0, 1.35, 1.45)
    _sprout_flat(mesh, 0.0, 0.0, 1.45, 10.0)
    for sx in (-1, 1):                                                       # 술 — 끝마다 아홉 가닥
        for k in range(9):
            cy = -hy + 6.0 + k * (2.0 * hy - 12.0) / 8.0
            x0, x1 = sorted((sx * hx, sx * (hx + 8.0)))
            _box(mesh, "Cushion", x0, x1, cy - 1.2, cy + 1.2, 0.0, 0.6)
    return mesh


def rug_square():
    """네모 무늬 러그 — 280 × 190 · 두께 1.5. 빨간 테 · 크림 바탕 · 초록 큰 마름모 · 크림 작은 마름모 · 네 귀 새싹 두 잎 수."""
    mesh = Mesh()
    hx, hy = 140.0, 95.0
    _box(mesh, "Petal", -hx, hx, -hy, hy, 0.0, 1.2)
    _box(mesh, "Cushion", -hx + 14.0, hx - 14.0, -hy + 14.0, hy - 14.0, 1.2, 1.3)
    _box(mesh, "Accent", -hx + 20.0, hx - 20.0, -hy + 20.0, hy - 20.0, 1.3, 1.35)   # 꿀색 안 테
    _box(mesh, "Cushion", -hx + 23.0, hx - 23.0, -hy + 23.0, hy - 23.0, 1.35, 1.4)
    _diamond(mesh, "Fabric", 0.0, 0.0, 80.0, 55.0, 1.4, 1.5)
    _diamond(mesh, "Cushion", 0.0, 0.0, 40.0, 27.0, 1.5, 1.55)
    _sprout_flat(mesh, 0.0, 0.0, 1.55, 13.0)
    for sx in (-1, 1):
        for sy in (-1, 1):
            _sprout_flat(mesh, sx * (hx - 45.0), sy * (hy - 40.0), 1.4, 11.0)
    return mesh


def doormat():
    """문 앞 깔개 — 90 × 60 · 두께 1.5. 짙은 테 · 밝은 짚 바탕 · 짚 결 줄 · 가운데 새싹 두 잎."""
    mesh = Mesh()
    hx, hy = 45.0, 30.0
    _box(mesh, "WoodDark", -hx, hx, -hy, hy, 0.0, 1.2)
    _box(mesh, "WoodLight", -hx + 5.0, hx - 5.0, -hy + 5.0, hy - 5.0, 1.2, 1.5)
    for cx in (-28.0, -20.0, 20.0, 28.0):                                    # 짚 결
        _box(mesh, "Wood", cx - 1.0, cx + 1.0, -hy + 8.0, hy - 8.0, 1.5, 1.6)
    _sprout_flat(mesh, 0.0, 0.0, 1.5, 14.0)
    return mesh


def _wedge_x(mesh, slot, x0, x1, cy, cz, r_in, r_out, a0, a1, half_t):
    """XZ 평면 둥근 띠 한 토막(아치) — 가운데 (cy 의 Y 두께 ±half_t, 중심 높이 cz), 반지름 r_in→r_out, 각 a0→a1(도, +X = 0)."""
    def p(r, a):
        t = math.radians(a)
        return x0 + r * math.cos(t), cz + r * math.sin(t)
    pts = [p(r_in, a0), p(r_out, a0), p(r_out, a1), p(r_in, a1)]
    _hexa(mesh, slot, [(px, cy - half_t, pz) for px, pz in pts] + [(px, cy + half_t, pz) for px, pz in pts])


def _picket(mesh, slot, cx, y0, y1, half_w, z0, z1, tip):
    """뾰족 머리 말뚝 — 몸(상자) + 위 세모 머리(앞뒤 Y 두께 y0→y1). 머리 꼭짓점 높이 z1 + tip."""
    _box(mesh, slot, cx - half_w, cx + half_w, y0, y1, z0, z1)
    outline = [(cx - half_w, z1), (cx + half_w, z1), (cx + 0.01, z1 + tip), (cx - 0.01, z1 + tip)]
    _hexa(mesh, slot, [(px, y0, pz) for px, pz in outline] + [(px, y1, pz) for px, pz in outline])


def fence_picket():
    """말뚝 울타리 — 폭 100(칸 폭, 이어 놓으면 붙음) · 흰 뾰족 말뚝 일곱(높이 86) · 뒤(+Y) 꿀색 가로대 둘 · 양끝 짙은 기둥 + 꿀색 모자.
    가운데 말뚝 앞에 새싹 두 잎(우리 표식). 옛 나무 울타리(`fence_wood`, 갈색 판자)와 실루엣·색이 갈린다."""
    mesh = Mesh()
    for sx in (-1, 1):                                                                    # 기둥 + 모자
        cx = sx * 44.0
        _box(mesh, "WoodDark", cx - 5.0, cx + 5.0, -5.0, 5.0, 0.0, 88.0)
        _box(mesh, "Accent", cx - 6.0, cx + 6.0, -6.0, 6.0, 88.0, 92.0)
    for z in (20.0, 58.0):                                                                # 가로대
        _box(mesh, "Wood", -39.0, 39.0, 1.0, 5.0, z, z + 7.0)
    for k in range(7):
        cx = -33.0 + k * 11.0
        _picket(mesh, "Pillow", cx, -2.0, 1.0, 3.8, 4.0, 74.0 + (3.0 if k == 3 else 0.0), 9.0)
    _sprout_front(mesh, 0.0, -2.0, 62.0, 0.8)
    return mesh


def path_log():
    """통나무 디딤 길 — 1칸 안 둥근 통나무 단면 넷(지름 44 · 40 · 26 · 24, 두께 2.5) · 짙은 껍질 테 · 밝은 속 · 나이테 · 짙은 심.
    양끝(X ±48)까지 닿아 이어 놓으면 길이 끊기지 않는다. 사이사이 풀 두 포기. 러그 층(가구 밑·아치 밑에 깔림)."""
    mesh = Mesh()
    for cx, cy, r in ((-26.0, -18.0, 22.0), (18.0, 8.0, 20.0), (36.0, -30.0, 12.0), (-14.0, 30.0, 13.0)):
        _cyl(mesh, "WoodDark", cx, cy, 0.0, 2.0, r, r, 16)                                # 껍질
        _cyl(mesh, "WoodLight", cx, cy, 2.0, 2.4, r - 2.5, r - 2.5, 16)                   # 속
        _cyl(mesh, "Wood", cx, cy, 2.4, 2.45, r * 0.62, r * 0.62, 16)                     # 나이테
        _cyl(mesh, "WoodLight", cx, cy, 2.45, 2.5, r * 0.52, r * 0.52, 16)
        _cyl(mesh, "WoodDark", cx, cy, 2.5, 2.55, r * 0.14, r * 0.14, 8)                  # 심
    for gx, gy in ((-40.0, 22.0), (0.0, -34.0), (40.0, 26.0), (2.0, 32.0)):              # 풀 포기
        _leaf_y(mesh, "LeafLight", (gx, gy, 0.0), -1, 6.0, 3.0, 8.0)
        _leaf_y(mesh, "LeafLight", (gx, gy, 0.0), 1, 6.0, 3.0, 8.0)
        _leaf_x(mesh, "Leaf", (gx, gy, 0.0), 1, 5.0, 3.0, 10.0)
    return mesh


def garden_arch():
    """꽃 아치 — 폭 100 · 깊이 36 · 꼭대기 228. 양쪽 격자 기둥(앞뒤 기둥 둘 + 가로살) · 반원 머리(반지름 50, 앞뒤 둘 + 가로살 일곱) ·
    머리·기둥을 감은 넝쿨 잎과 꽃(빨강·노랑·흰) · 꼭대기 새싹 두 잎. 가운데 80 은 비어 걸어 지난다(가구는 충돌 없음)."""
    mesh = Mesh()
    spring = 172.0                                                                        # 머리가 시작되는 높이
    for sx in (-1, 1):
        cx = sx * 45.0
        for cy in (-15.0, 15.0):
            _box(mesh, "Pillow", cx - 3.0, cx + 3.0, cy - 3.0, cy + 3.0, 0.0, spring)     # 기둥
        for z in range(30, int(spring), 34):                                              # 가로살
            _box(mesh, "Wood", cx - 2.0, cx + 2.0, -13.0, 13.0, z, z + 3.0)
    for cy in (-15.0, 15.0):                                                              # 반원 머리 앞뒤
        for i in range(12):
            _wedge_x(mesh, "Pillow", 0.0, 0.0, cy, spring, 42.0, 48.0, i * 15.0, (i + 1) * 15.0, 3.0)
    for i in range(7):                                                                    # 머리 가로살
        t = math.radians(15.0 + i * 25.0)
        x, z = 45.0 * math.cos(t), spring + 45.0 * math.sin(t)
        _box(mesh, "Wood", x - 2.0, x + 2.0, -18.0, 18.0, z - 1.5, z + 1.5)
    k = 0
    for i in range(14):                                                                   # 넝쿨 — 머리 위 잎 덩이 + 꽃
        t = math.radians(14.0 + i * 152.0 / 13.0)                                      # 14° ~ 166° 좌우 같게
        r = 42.0 + (3.0 if i % 2 else 0.0) * math.sin(t)
        x, z = r * math.cos(t), spring + r * math.sin(t)
        side = -16.0 if i % 2 == 0 else 16.0
        _cyl(mesh, "Leaf" if i % 3 else "LeafLight", x, side * 0.5, z - 4.0, z + 6.0, 8.5, 5.0, 6)
        if i % 3 == 1:
            petal = ("Petal", "Accent", "Pillow")[k % 3]
            k += 1
            _cyl(mesh, petal, x, side, z + 3.0, z + 7.0, 2.5, 7.0, 6)
            _cyl(mesh, "WoodLight" if petal != "Accent" else "WoodDark", x, side, z + 7.0, z + 8.0, 2.2, 1.4, 6)
    for sx in (-1, 1):                                                                    # 기둥을 타고 오르는 잎
        for j, z in enumerate((40.0, 78.0, 116.0, 150.0)):
            y = -17.0 if (j + (sx > 0)) % 2 == 0 else 17.0
            _leaf_x(mesh, "Leaf", (sx * 45.0, y, z), -sx, 9.0, 5.0, 7.0, thick=1.5)
            _leaf_x(mesh, "LeafLight", (sx * 45.0, y, z + 10.0), sx, 3.5, 4.0, 6.0, thick=1.5)
        _cyl(mesh, "Petal" if sx < 0 else "Accent", sx * 44.0, -18.5, 96.0, 100.0, 2.5, 6.0, 6)
    top = spring + 48.0
    _box(mesh, "Leaf", -0.9, 0.9, -0.9, 0.9, top, top + 4.0)                              # 꼭대기 새싹
    _leaf_x(mesh, "Leaf", (0.0, 0.0, top + 3.0), -1, 10.0, 5.0, 6.0, thick=1.5)
    _leaf_x(mesh, "Leaf", (0.0, 0.0, top + 3.0), 1, 10.0, 5.0, 6.0, thick=1.5)
    return mesh


def mailbox():
    """우편함 — 짙은 기둥(높이 100) + 밑 받침 · 세이지 초록 통(46 × 26, 둥근 지붕 꼭대기 135) · -X 끝 꿀색 문 + 쇠 손잡이 ·
    앞(-Y) 빨간 깃발 · 지붕 위 새싹 두 잎. 꾸미기만(편지 기능 없음)."""
    mesh = Mesh()
    _box(mesh, "WoodDark", -14.0, 14.0, -14.0, 14.0, 0.0, 4.0)                            # 받침
    _box(mesh, "WoodDark", -4.0, 4.0, -4.0, 4.0, 4.0, 100.0)                              # 기둥
    _box(mesh, "Wood", -12.0, 12.0, -6.0, 6.0, 96.0, 100.0)                               # 받침판
    z0, half_y, r = 100.0, 13.0, 13.0
    _box(mesh, "Fabric", -22.0, 22.0, -half_y, half_y, z0, z0 + 22.0)
    cz = z0 + 22.0
    for i in range(6):                                                                    # 둥근 지붕 — 축 X, 반원 여섯 토막(속 채움)
        a0, a1 = math.radians(i * 30.0), math.radians((i + 1) * 30.0)
        pa, pb = (r * math.cos(a0), cz + r * math.sin(a0)), (r * math.cos(a1), cz + r * math.sin(a1))
        c = (0.0, cz)
        _hexa(mesh, "Fabric", [(-22.0, c[0], c[1]), (22.0, c[0], c[1]), (22.0, pa[0], pa[1]), (-22.0, pa[0], pa[1]),
                               (-22.0, c[0], c[1]), (22.0, c[0], c[1]), (22.0, pb[0], pb[1]), (-22.0, pb[0], pb[1])])
    _box(mesh, "Accent", -23.0, -22.0, -11.0, 11.0, z0 + 2.0, z0 + 30.0)                  # 문
    _box(mesh, "Metal", -25.0, -23.0, -2.0, 2.0, z0 + 14.0, z0 + 18.0)                    # 손잡이
    _box(mesh, "Wood", 22.0, 23.0, -12.0, 12.0, z0 + 1.0, z0 + 20.0)                      # 뒤 판
    _box(mesh, "MetalDark", 8.0, 10.0, -half_y - 2.0, -half_y, z0 + 8.0, z0 + 34.0)       # 깃대
    _box(mesh, "Petal", 10.0, 20.0, -half_y - 2.0, -half_y - 0.5, z0 + 26.0, z0 + 34.0)   # 깃발
    top = cz + r
    _box(mesh, "Leaf", -0.8, 0.8, -0.8, 0.8, top, top + 4.0)
    _leaf_x(mesh, "Leaf", (0.0, 0.0, top + 3.0), -1, 8.0, 4.0, 5.0)
    _leaf_x(mesh, "Leaf", (0.0, 0.0, top + 3.0), 1, 8.0, 4.0, 5.0)
    return mesh


def _flame(mesh, cx, cy, z0, r, h):
    """불꽃 — 노란 안 불(원뿔) + 둘레 꿀색 겉불(낮은 원뿔). 밤에 놓인 가구 액터가 이 높이 근처에 점광을 켠다(8.11 「비전기 조명」)."""
    _cyl(mesh, "Accent", cx, cy, z0, z0 + h * 0.3, r * 0.7, r * 1.35, 8)                 # 겉불 아래 부풂
    _cyl(mesh, "Accent", cx, cy, z0 + h * 0.3, z0 + h * 0.6, r * 1.35, r * 0.4, 8)
    _cyl(mesh, "Flame", cx, cy, z0 + h * 0.05, z0 + h * 0.35, r * 0.6, r * 1.1, 8)       # 안 불 물방울
    _cyl(mesh, "Flame", cx, cy, z0 + h * 0.35, z0 + h, r * 1.1, 0.2, 8)


def candle_stand():
    """세움 촛대 — 팔 폭 40 · 꼭대기 126. 짙은 나무 둥근 받침 · 꿀색 나무 기둥 · 가로 팔 · 꿀색 초받침 셋(가운데가 높음) ·
    흰 초 셋 + 불꽃 · 받침 앞(-Y) 새싹 두 잎. 집 안 바닥에 서는 사람 허리~가슴 높이 불빛."""
    mesh = Mesh()
    _cyl(mesh, "WoodDark", 0.0, 0.0, 0.0, 4.0, 14.0, 12.0, 12)                            # 받침
    _cyl(mesh, "Wood", 0.0, 0.0, 4.0, 98.0, 3.2, 2.6, 8)                                  # 기둥
    _cyl(mesh, "WoodLight", 0.0, 0.0, 40.0, 44.0, 4.5, 4.5, 8)                            # 기둥 마디
    _box(mesh, "Wood", -17.0, 17.0, -2.0, 2.0, 94.0, 98.0)                                # 가로 팔
    for cx, z in ((-16.0, 98.0), (16.0, 98.0), (0.0, 104.0)):
        if cx == 0.0:
            _cyl(mesh, "Wood", 0.0, 0.0, 98.0, z, 2.6, 2.6, 8)                           # 가운데 받침 목
        _cyl(mesh, "Accent", cx, 0.0, z, z + 2.5, 3.0, 4.5, 8)                           # 초받침
        _cyl(mesh, "Pillow", cx, 0.0, z + 2.5, z + 14.0, 2.4, 2.4, 8)                    # 초
        _flame(mesh, cx, 0.0, z + 14.0, 2.8, 11.0)
    _sprout_front(mesh, 0.0, -12.0, 5.0, 0.6)
    return mesh


def oil_lamp():
    """등잔대 — 폭 30 · 꼭대기 86. 짙은 나무 네모 받침 · 꿀색 네모 기둥 · 둥근 나무 쟁반 · 테라코타 등잔 접시 + 심지 불꽃 ·
    기둥 앞(-Y) 새싹 두 잎. 촛대보다 낮고 굵은 「옛 등잔」 실루엣."""
    mesh = Mesh()
    _box(mesh, "WoodDark", -15.0, 15.0, -15.0, 15.0, 0.0, 5.0)                            # 받침
    _box(mesh, "WoodLight", -11.0, 11.0, -11.0, 11.0, 5.0, 7.0)
    _box(mesh, "Wood", -2.5, 2.5, -2.5, 2.5, 7.0, 68.0)                                   # 기둥
    _cyl(mesh, "WoodDark", 0.0, 0.0, 68.0, 71.0, 11.0, 12.0, 12)                          # 쟁반
    _cyl(mesh, "Clay", 0.0, 0.0, 71.0, 76.0, 5.0, 8.0, 10)                                # 등잔 접시
    _cyl(mesh, "WoodDark", 0.0, 0.0, 76.0, 76.3, 6.5, 6.5, 10)                            # 기름
    _box(mesh, "Clay", 7.0, 11.0, -1.2, 1.2, 74.0, 76.0)                                  # 부리
    _flame(mesh, 0.0, 0.0, 76.0, 3.6, 14.0)
    _sprout_front(mesh, 0.0, -2.5, 40.0, 0.6)
    return mesh


def lantern_post():
    """기둥 등불 — 폭 40 · 꼭대기 176. 짙은 나무 기둥(받침 + 꿀색 모자) · +X 로 뻗은 가로 팔 · 고리에 매단 쇠 틀 등불(네 기둥 ·
    위 지붕 · 밑판) 안 흰 초 + 불꽃 · 기둥 앞(-Y) 새싹 두 잎. 마당·길가용."""
    mesh = Mesh()
    _box(mesh, "Stone", -10.0, 10.0, -10.0, 10.0, 0.0, 6.0)                               # 돌 받침
    _box(mesh, "WoodDark", -4.0, 4.0, -4.0, 4.0, 6.0, 170.0)                              # 기둥
    _box(mesh, "Accent", -5.5, 5.5, -5.5, 5.5, 170.0, 176.0)                              # 모자
    _box(mesh, "Wood", -4.0, 30.0, -2.0, 2.0, 158.0, 163.0)                               # 가로 팔
    _box(mesh, "Wood", 4.0, 6.0, -2.0, 2.0, 146.0, 158.0)                                 # 팔 받침대(세로)
    lx = 22.0                                                                             # 등불 가운데 X
    _box(mesh, "MetalDark", lx - 0.6, lx + 0.6, -0.6, 0.6, 150.0, 158.0)                  # 고리
    for dx in (-7.0, 7.0):
        for dy in (-7.0, 7.0):
            _box(mesh, "MetalDark", lx + dx - 0.8, lx + dx + 0.8, dy - 0.8, dy + 0.8, 124.0, 145.0)
    _box(mesh, "MetalDark", lx - 8.5, lx + 8.5, -8.5, 8.5, 121.0, 124.0)                  # 밑판
    _hexa(mesh, "MetalDark", [(lx - 9.0, -9.0, 145.0), (lx + 9.0, -9.0, 145.0), (lx + 9.0, 9.0, 145.0), (lx - 9.0, 9.0, 145.0),
                              (lx - 1.0, -1.0, 151.0), (lx + 1.0, -1.0, 151.0), (lx + 1.0, 1.0, 151.0), (lx - 1.0, 1.0, 151.0)])
    _cyl(mesh, "Pillow", lx, 0.0, 124.0, 133.0, 3.0, 3.0, 8)                              # 초
    _flame(mesh, lx, 0.0, 133.0, 3.4, 10.0)
    _sprout_front(mesh, 0.0, -4.0, 60.0, 0.7)
    return mesh


def garden_torch():
    """정원 횃불 — 폭 22 · 꼭대기 177. 둥근 돌 받침 · 밝은 나무 장대(짙은 마디 넷) · 위로 벌어진 나무 통 + 꿀색 밧줄 띠 ·
    큰 불꽃(겉불 꿀색 + 안 불 노랑) · 장대 앞(-Y) 새싹 두 잎. 마당 길가용, 넷 중 가장 큰 불꽃."""
    mesh = Mesh()
    _cyl(mesh, "Stone", 0.0, 0.0, 0.0, 7.0, 11.0, 8.0, 10)                                # 돌 받침
    _cyl(mesh, "WoodLight", 0.0, 0.0, 7.0, 140.0, 2.4, 2.2, 8)                            # 장대
    for z in (40.0, 75.0, 110.0):
        _cyl(mesh, "WoodDark", 0.0, 0.0, z, z + 2.0, 2.9, 2.9, 8)                         # 마디
    _cyl(mesh, "Wood", 0.0, 0.0, 140.0, 154.0, 3.0, 7.0, 10)                              # 통
    _cyl(mesh, "Accent", 0.0, 0.0, 145.0, 148.0, 5.0, 5.8, 10)                            # 밧줄 띠
    _cyl(mesh, "WoodDark", 0.0, 0.0, 154.0, 154.3, 6.5, 6.5, 10)                          # 통 속
    _flame(mesh, 0.0, 0.0, 153.0, 7.0, 24.0)                                              # 큰 불꽃
    _sprout_front(mesh, 0.0, -2.4, 30.0, 0.6)
    return mesh


def _slope(mesh, slot, y0, y1, x0, x1, z0_low, z0_high, z1_low, z1_high):
    """기운 판 — X 를 따라 아래 선 z0_low → z0_high, 위 선 z1_low → z1_high 로 오르는 Y 두께 판(계단 옆판·난간대)."""
    _hexa(mesh, slot, [(x0, y0, z0_low), (x1, y0, z0_high), (x1, y1, z0_high), (x0, y1, z0_low),
                       (x0, y0, z1_low), (x1, y0, z1_high), (x1, y1, z1_high), (x0, y1, z1_low)])


def stairs_wood():
    """나무 계단 — 길이 300(긴 변 X, 발자국 3×1) · 폭 96 · 2층 바닥 275 까지 디딤 12단(단 높이 22.9 · 깊이 25, 꿀색·밝은 나무 번갈아,
    단마다 짙은 챌판) · 짙은 옆판 둘 · -Y 쪽 난간(기둥 셋 + 기운 손잡이, 디딤 위 90) · 아래 기둥 앞 새싹 두 잎.
    낮은 끝 = -X, 높은 끝 = +X(2층 바닥 윗면과 같은 275). 오르는 몸은 06d 의 매끈한 비탈 충돌이 받친다(DESIGN 8.11 「계단·위층」)."""
    mesh = Mesh()
    hx, rise, steps, half_w = 150.0, 275.0, 12, 40.0
    run = 2.0 * hx / steps
    for i in range(steps):
        x0 = -hx + i * run
        top = rise * (i + 1) / steps
        _box(mesh, "Wood" if i % 2 == 0 else "WoodLight", x0, min(hx, x0 + run + 2.0), -half_w, half_w, top - 4.0, top)  # 디딤판
        _box(mesh, "WoodDark", x0, x0 + 1.5, -half_w + 1.0, half_w - 1.0, top - rise / steps, top - 4.0)  # 챌판
    for y0, y1 in ((-half_w - 5.0, -half_w), (half_w, half_w + 5.0)):
        _slope(mesh, "WoodDark", y0, y1, -hx, hx, 0.0, rise - 24.0, 24.0, rise)  # 옆판
    post_y0, post_y1, rail_h = -half_w - 8.0, -half_w - 2.0, 90.0
    def side_top(x):
        return 24.0 + (x + hx) / (2.0 * hx) * (rise - 24.0)
    for px0 in (-hx, -3.0, hx - 6.0):
        _box(mesh, "WoodDark", px0, px0 + 6.0, post_y0, post_y1, side_top(px0) - 10.0, side_top(px0 + 6.0) + rail_h)   # 난간 기둥
    _slope(mesh, "WoodLight", post_y0 - 0.5, post_y1 + 0.5, -hx, hx,
           side_top(-hx) + rail_h - 2.0, side_top(hx) + rail_h - 2.0, side_top(-hx) + rail_h + 4.0, side_top(hx) + rail_h + 4.0)  # 손잡이
    _sprout_front(mesh, -hx + 5.0, post_y0, side_top(-hx) + 45.0, 0.6)
    return mesh


def generator():
    """연료 발전기 — 폭 64(긴 변 X, 발자국 1×1) · 꼭대기 118. 짙은 쇠 받침 썰매 · 세운 세이지 초록 통(크림 띠 둘) ·
    위 짙은 쇠 뚜껑 + 꿀색 나무 연료 뚜껑(나무를 넣는 곳) · 뒤(+Y) 오른쪽 짧은 배기 굴뚝(쇠 모자) ·
    앞(-Y) 크림 계기판(꿀색 눈금판 + 짙은 바늘) · 앞 쇠 손잡이 · 계기판 위 새싹 두 잎. 바퀴·빨강 몸 없음(Reference 6절 — 흔한
    빨강 바퀴 발전기와 다른 우리 꼴). 연기는 07d 가 배기 굴뚝 위(+10, +8, 118)에 붙인다(DESIGN 8.11 「연료 발전기 숫자」)."""
    mesh = Mesh()
    _box(mesh, "MetalDark", -32.0, 32.0, -24.0, 24.0, 0.0, 4.0)                          # 썰매 받침
    for y0 in (-22.0, 16.0):
        _box(mesh, "MetalDark", -32.0, 32.0, y0, y0 + 6.0, 4.0, 8.0)                     # 받침 가로대
    _cyl(mesh, "Fabric", 0.0, 0.0, 8.0, 84.0, 26.0, 26.0, 16)                            # 통
    for z in (28.0, 64.0):
        _cyl(mesh, "Cushion", 0.0, 0.0, z, z + 6.0, 26.8, 26.8, 16)                      # 크림 띠
    _cyl(mesh, "MetalDark", 0.0, 0.0, 84.0, 89.0, 26.0, 23.0, 16)                        # 뚜껑
    _cyl(mesh, "Wood", -8.0, -6.0, 89.0, 93.0, 8.0, 8.0, 10)                             # 연료 뚜껑
    _box(mesh, "WoodDark", -11.0, -5.0, -7.0, -5.0, 93.0, 95.0)                          # 뚜껑 손잡이
    _cyl(mesh, "MetalDark", 10.0, 8.0, 89.0, 114.0, 4.0, 4.0, 10)                        # 배기 굴뚝
    _cyl(mesh, "Metal", 10.0, 8.0, 114.0, 118.0, 6.0, 5.0, 10)                           # 굴뚝 모자
    _box(mesh, "Cushion", -13.0, 13.0, -28.5, -24.0, 40.0, 60.0)                         # 계기판
    _box(mesh, "Accent", -9.0, -1.0, -29.5, -28.5, 46.0, 56.0)                           # 눈금판
    _box(mesh, "MetalDark", -5.6, -4.4, -30.0, -29.5, 49.0, 55.0)                        # 바늘
    _box(mesh, "Berry", 3.0, 9.0, -30.0, -28.5, 47.0, 53.0)                              # 켜기 단추
    for x0 in (-18.0, 14.0):
        _box(mesh, "Metal", x0, x0 + 4.0, -33.0, -25.0, 76.0, 80.0)                      # 손잡이 다리
    _box(mesh, "Metal", -18.0, 18.0, -35.0, -31.0, 76.0, 80.0)                           # 손잡이
    _sprout_front(mesh, 0.0, -26.8, 64.5, 0.7)
    return mesh


def fridge(lit=False):
    """냉장고 — 폭 64(긴 변 X, 발자국 1×1) · 깊이 54(손잡이까지 60 — 폭보다 작아야 액터가 안 돌린다) · 키 170(사람 176 아래 어깨 높이 문). 크림 몸 + 둥근 세운 모서리 넷 ·
    짙은 쇠 굽 · 세이지 초록 윗 마개 · 앞(-Y) 세이지 초록 문 둘(위 작은 칸 · 아래 큰 칸, 짙은 틈) · 오른쪽 세로 쇠 손잡이 둘 ·
    위 문 왼쪽 위 불 칸(꺼짐 = 짙은 쇠 · `lit` 이면 밝은 초록 `Lit` — 켜짐 짝 `_On_01`, 몸은 같은 꼴) · 아래 문 새싹 두 잎. 상표·글 없음(Reference 6절)."""
    mesh = Mesh()
    _box(mesh, "MetalDark", -29.0, 29.0, -24.0, 24.0, 0.0, 6.0)                          # 굽
    _box(mesh, "Cushion", -32.0, 32.0, -21.0, 21.0, 6.0, 166.0)                          # 몸(X 로 넓은 판)
    _box(mesh, "Cushion", -26.0, 26.0, -27.0, 27.0, 6.0, 166.0)                          # 몸(Y 로 넓은 판)
    for cx in (-26.0, 26.0):
        for cy in (-21.0, 21.0):
            _cyl(mesh, "Cushion", cx, cy, 6.0, 166.0, 6.0, 6.0, 8)                       # 둥근 모서리
    _box(mesh, "Fabric", -30.0, 30.0, -25.0, 25.0, 166.0, 170.0)                         # 윗 마개
    _box(mesh, "Fabric", -25.0, 25.0, -28.0, -27.0, 116.0, 162.0)                        # 위 문
    _box(mesh, "Fabric", -25.0, 25.0, -28.0, -27.0, 10.0, 112.0)                         # 아래 문
    _box(mesh, "MetalDark", -26.0, 26.0, -27.6, -27.0, 112.0, 116.0)                     # 문 틈
    for z0, z1 in ((122.0, 152.0), (72.0, 104.0)):
        _box(mesh, "Metal", 18.0, 21.0, -32.0, -28.0, z1 - 2.0, z1)                      # 손잡이 위 받침
        _box(mesh, "Metal", 18.0, 21.0, -32.0, -28.0, z0, z0 + 2.0)                      # 손잡이 아래 받침
        _box(mesh, "Metal", 18.0, 21.0, -33.0, -31.0, z0, z1)                            # 세로 손잡이
    _box(mesh, "Lit" if lit else "MetalDark", -21.0, -13.0, -28.8, -28.0, 151.0, 157.0)  # 불 칸
    _sprout_front(mesh, -8.0, -28.0, 88.0, 0.9)
    return mesh


def _fan_blade(mesh, slot, cz, angle, y0, y1):
    """선풍기 날개 하나 — (0, cz) 둘레 angle(도, +X = 0, 반시계)로 뻗는 넓고 둥근 주걱 잎(뿌리 6 · 가운데 17 · 끝 둥글게, 끝 반지름 23.5).
    루프#1412 보정: 좁은 육각 잎(가운데 13 · 끝 24)이 게임 카메라에서 새싹 잎으로 읽혀 넓히고 끝을 둥글렸다."""
    a = math.radians(angle)
    ux, uz = math.cos(a), math.sin(a)
    px, pz = -uz, ux
    pts = []
    for r, half in ((3.5, -3.0), (9.0, -7.0), (15.0, -8.5), (20.0, -7.0), (22.8, -4.0), (23.5, 0.0),
                    (22.8, 4.0), (20.0, 7.0), (15.0, 8.5), (9.0, 7.0), (3.5, 3.0)):
        pts.append((ux * r + px * half, cz + uz * r + pz * half))
    _prism_y(mesh, slot, pts, y0, y1)


def _ring_y(mesh, slot, cz, r_in, r_out, y0, y1, sides=28):
    """Y 축 고리(선풍기 망 테) — (0, cz) 둘레 안·바깥 반지름, 앞(-Y)·뒤 면 + 바깥·안 띠."""
    def ring(r, y):
        return [mesh.v(r * math.cos(math.tau * i / sides), y, cz + r * math.sin(math.tau * i / sides)) for i in range(sides)]
    of, ob, inf, inb = ring(r_out, y0), ring(r_out, y1), ring(r_in, y0), ring(r_in, y1)
    for outer, inner, out_y in ((of, inf, -1.0), (ob, inb, 1.0)):     # 앞·뒤 납작 면
        mesh.group += 1
        for i in range(sides):
            j = (i + 1) % sides
            mesh.tri(slot, outer[i], outer[j], inner[j], (0.0, out_y, 0.0))
            mesh.tri(slot, outer[i], inner[j], inner[i], (0.0, out_y, 0.0))
    for front, back, sign in ((of, ob, 1.0), (inf, inb, -1.0)):        # 바깥 띠 · 안 띠
        mesh.group += 1
        for i in range(sides):
            j = (i + 1) % sides
            t = math.tau * (i + 0.5) / sides
            out = (sign * math.cos(t), 0.0, sign * math.sin(t))
            mesh.tri(slot, front[i], front[j], back[j], out)
            mesh.tri(slot, front[i], back[j], back[i], out)


def _bar_y(mesh, slot, cz, angle, r0, r1, half, y0, y1):
    """망 살 하나 — (0, cz) 에서 angle 로 r0 → r1 뻗는 폭 2·half 막대를 Y 로 y0 → y1 밀어냄."""
    a = math.radians(angle)
    ux, uz = math.cos(a), math.sin(a)
    px, pz = -uz * half, ux * half
    pts = [(ux * r0 - px, cz + uz * r0 - pz), (ux * r1 - px, cz + uz * r1 - pz),
           (ux * r1 + px, cz + uz * r1 + pz), (ux * r0 + px, cz + uz * r0 + pz)]
    _prism_y(mesh, slot, pts, y0, y1)


def _tilt_x(mesh, first, pivot_z, degrees):
    """정점 first 부터를 (y 0, pivot_z) 지나는 X 축 둘레로 돌린다 — 양수면 앞(-Y)이 위를 본다. 감기는 강체 회전이라 그대로."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    for i in range(first, len(mesh.verts)):
        x, y, z = mesh.verts[i]
        dz = z - pivot_z
        mesh.verts[i] = (x, y * c + dz * s, pivot_z - y * s + dz * c)


FAN_HUB_Z = 86.0        # 머리(날개 축) 가운데 높이 — 08d 날개 메시 원점
FAN_TILT = 20.0         # 머리를 뒤로 젖힌 각 — 위에서 내려보는 게임 카메라에 날개 면이 보이게(세운 선풍기 머리 젖힘)


def fan_stand(lit=False):
    """세운 선풍기 — 키 약 113 · 망 지름 51 · 날개 지름 47(세 잎, 넓고 둥근 주걱). 둥근 나무 받침(짙은 테) · 밝은 나무 기둥(꿀색 마디) ·
    머리(뒤로 20° 젖힘): 흰 모터 통(Y 축, 뒤 짙은 쇠 마개) · 하늘색 세 잎 · 회색 쇠 망(앞 고리·안 고리·살 여섯 · 뒤 고리 · 옆 살 여덟) ·
    앞 꿀색 둥근 배지 · 받침 윗면 앞 불 칸(꺼짐 = 짙은 쇠 · `lit` 이면 밝은 초록 — 켜짐 짝 `_On_01`, 08b-2).
    루프#1412 보정(감독 10-20 13:55): 초록 세 잎·가는 줄기·새싹이 화분으로 읽혀 → 날개 넓게·망 테·모터 통 키움·흰/하늘/회색.
    날개는 08d-1(루프#1414)이 몸에서 떼어 `fan_blade` 따로 메시로 — 놓인 액터가 머리 가운데(0, 0, `FAN_HUB_Z`)에 `FAN_TILT` 젖혀 붙이고
    켜진 때만 돌린다(행 칸 `PlacedSpin*`). 우리 꼴: 나무 받침·기둥 + 꿀색 배지."""
    mesh = Mesh()
    hub_z = FAN_HUB_Z
    _cyl(mesh, "Wood", 0.0, 0.0, 0.0, 4.0, 17.0, 16.0, 14)                              # 받침
    _cyl(mesh, "WoodDark", 0.0, 0.0, 0.0, 1.5, 17.6, 17.6, 14)                          # 받침 테
    _cyl(mesh, "WoodLight", 0.0, 0.0, 4.0, 74.0, 2.6, 2.4, 8)                           # 기둥
    _cyl(mesh, "Accent", 0.0, 0.0, 70.0, 74.0, 3.4, 3.4, 8)                             # 마디
    _box(mesh, "Lit" if lit else "MetalDark", -2.5, 2.5, -14.0, -10.0, 4.0, 4.8)        # 불 칸(받침 윗면 앞)
    head = len(mesh.verts)
    _box(mesh, "WoodLight", -2.2, 2.2, 1.5, 6.0, 72.0, hub_z - 7.0)                     # 목(기둥 → 모터 통 밑)
    _disc_y(mesh, "Pillow", 0.0, hub_z, 9.0, -4.0, 12.0, 14)                            # 모터 통
    _disc_y(mesh, "MetalDark", 0.0, hub_z, 7.0, 12.0, 14.5, 12)                         # 뒤 마개
    _disc_y(mesh, "Metal", 0.0, hub_z, 4.5, -7.5, -4.0, 10)                             # 축
    _ring_y(mesh, "Metal", hub_z, 23.8, 25.5, -10.6, -9.0)                              # 앞 망 테
    _ring_y(mesh, "Metal", hub_z, 12.2, 13.4, -10.4, -9.4)                              # 앞 안 고리
    _ring_y(mesh, "Metal", hub_z, 23.8, 25.5, 1.5, 3.0)                                 # 뒤 망 테
    for k in range(6):                                                                  # 앞 망 살
        _bar_y(mesh, "Metal", hub_z, 30.0 + 60.0 * k, 4.0, 24.2, 0.6, -10.4, -9.4)
    for k in range(8):                                                                  # 옆 살(앞 테 ↔ 뒤 테)
        _bar_y(mesh, "Metal", hub_z, 22.5 + 45.0 * k, 23.9, 25.3, 0.7, -9.0, 1.5)
    _disc_y(mesh, "Accent", 0.0, hub_z, 4.8, -12.0, -10.4, 12)                          # 앞 배지
    _tilt_x(mesh, head, hub_z, FAN_TILT)
    return mesh


def fan_blade():
    """선풍기 날개 셋 — 몸(`fan_stand`)에서 뗀 하늘색 주걱 잎. **원점 = 머리 가운데(축), 젖히지 않은 꼴**(잎 면 = XZ, 축 = Y, 앞 -Y):
    놓인 액터가 몸 메시의 (0, 0, `FAN_HUB_Z`)에 X 축 둘레 `FAN_TILT` 젖혀 붙이고 자기 Y 축으로 돌린다(M108-08d-1 루프#1414).
    잎 자리(앞 6.6 → 5.2)는 몸에 붙어 있던 때와 같다 — 앞 망(-10.4)·축(-7.5)과 안 겹친다."""
    mesh = Mesh()
    for angle in (90.0, 210.0, 330.0):
        _fan_blade(mesh, "Sky", 0.0, angle, -6.6, -5.2)
    return mesh


# (원본 이름, 만드는 함수) — 슬롯 순서는 한 줄(팔레트 칸 순서와 무관 — 색표가 이름으로 찾는다)
FURNITURE = (("ArmChair_01", arm_chair), ("Sofa_01", sofa), ("TableRound_01", table_round),
             ("TableLong_01", table_long), ("BedSingle_01", bed_single), ("BedDouble_01", bed_double),
             ("Bookshelf_01", bookshelf), ("Dresser_01", dresser), ("KitchenCounter_01", kitchen_counter),
             ("KitchenSink_01", kitchen_sink), ("PottedTree_01", potted_tree), ("PottedBerry_01", potted_berry),
             ("FlowerBox_01", flower_box), ("PottedSprout_01", potted_sprout), ("RugRound_01", rug_round),
             ("RugRunner_01", rug_runner), ("RugSquare_01", rug_square), ("Doormat_01", doormat),
             ("FencePicket_01", fence_picket), ("PathLog_01", path_log), ("GardenArch_01", garden_arch), ("Mailbox_01", mailbox),
             ("CandleStand_01", candle_stand), ("OilLamp_01", oil_lamp), ("LanternPost_01", lantern_post),
             ("GardenTorch_01", garden_torch), ("StairsWood_01", stairs_wood), ("Generator_01", generator),
             ("Fridge_01", fridge), ("FanStand_01", fan_stand),
             ("Fridge_On_01", lambda: fridge(lit=True)), ("FanStand_On_01", lambda: fan_stand(lit=True)),
             ("FanBlade_01", fan_blade))
SLOTS = ["Wood", "WoodDark", "WoodLight", "Fabric", "Cushion", "Accent", "Pillow", "Leaf", "Berry", "Stone", "Metal", "MetalDark",
         "LeafLight", "Petal", "Clay", "Flame", "Lit", "Sky"]


if __name__ == "__main__":
    for name, build in FURNITURE:
        mesh = build()
        used = [s for s in SLOTS if any(f[0] == s for f in mesh.faces)]
        triangles = mesh.write(name, used)
        xs, ys, zs = zip(*mesh.verts)
        print("{}.obj 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} · x {:.1f}..{:.1f} y {:.1f}..{:.1f} 밑 z {:.1f}".format(
            name, triangles, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), min(xs), max(xs), min(ys), max(ys), min(zs)))
