"""M108-02 벽 장식 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_wall_decor_meshes.py   → Saved/Generated/PictureFrame_01.obj · WallClock_01.obj · WallShelf_01.obj
                                                   · WallMirror_01 · WallMap_01 · WallAnchor_01 · WallWreath_01 · NeonSign_01 (+ .mtl)

DESIGN 8.11 「건축가 꾸미기」 벽 걸기 — 액자·벽시계·벽 선반. 같은 칸·같은 변에선 높이 50cm 마다 하나씩 걸리므로
(50 = 소품 가로 40cm 이하 + 틈 10) 셋 다 **가로 40cm 이하·세로 30cm 안팎**이다. 원본(레퍼런스 게임) 벽 장식은 따르지 않는다(Reference 6절):
액자 그림은 **우리 섬** — 하늘·해·바다 위 초록 섬과 빨강·흰 줄 등대(STORY.md 등대 불빛), 벽시계는 둥근 나무 테 + 크림 판 +
10시 10분 바늘 + 위에 새싹 두 잎(새싹 모자와 같은 우리 표식), 벽 선반은 쐐기 받침 둘 위 판자 + 화분·책 둘·꿀단지.
M108-05c(루프#1390) 넷 더함 — 아치 거울(나무 테 + 닦은 쇠 판 + 빛 줄 둘 + 새싹), 섬 지도(위아래 나무 막대 + 천 위 바다·섬·빨강 X·점 길),
닻(나무 가로대 + 쇠 몸·팔·갈고리 + 감은 밧줄), 들꽃 화환(잎 고리 + 빨강·노랑·흰 꽃 + 꿀색 리본). 넷 다 가로 40·세로 39.2 이하 —
1.5배로 놓여 가장 높은 걸이 200 에서도 벽 높이 260 안(시계 줄과 같은 셈).
M108-08b(루프#1410) 네온 간판 — 전기 가구 넷 중 벽에 거는 것. 짙은 나무 판 위 조개 모양 관(테두리 반원 + 부채살 넷 + 밑 귀 둘), 글자 없음.
꺼짐 = 관이 빛 없는 유리 칸(`Glass`) — 켜짐 짝 `_On_01` 이 분홍·하늘 칸으로 바꿈(08b-2). 가로 40(1.5배 → 60).
면마다 정점을 따로 둬 한 면 한 색(world_environment.md "한 면 한 색"), 그린 치수 cm, Z 위.
**벽에 닿는 뒷면 = y 0, 앞은 -Y**(언리얼로 반입하면 Y 가 뒤집혀 앞 = +Y) — 걸릴 때 이 뒷면을 벽에 댄다. 가로(긴 변) = X 라 놓인 가구 액터가 돌리지 않는다.
슬롯 색은 `import_wall_decor_meshes.py` 가 입힌다.
"""
import math

from make_tree_meshes import Mesh
from make_weapon_meshes import _box, _hexa

# 액자 — 바깥 40 × 30, 테 폭 4, 두께 3. 그림 판은 테 앞면보다 1.2 들어간다.
FRAME_W, FRAME_H, FRAME_BAR, FRAME_D = 40.0, 30.0, 4.0, 3.0
PIC_Y = -FRAME_D + 1.2

# 벽시계 — 지름 34, 몸 두께 4, 앞 테 1.
CLOCK_R, CLOCK_FACE_R, CLOCK_D = 17.0, 14.5, 4.0

# 벽 선반 — 판자 40 × 16 × 3, 판 윗면 높이 13(밑은 쐐기 받침).
SHELF_W, SHELF_DEPTH, SHELF_Z0, SHELF_T = 40.0, 16.0, 10.0, 3.0


def _rot_box(mesh, slot, cx, cz, angle, length, width, y0, y1):
    """X-Z 평면에서 (cx, cz) 를 밑동으로 angle(도, 12시 = 0, 시계 방향)으로 뻗는 납작한 막대 — 시계 바늘."""
    a = math.radians(angle)
    ux, uz = math.sin(a), math.cos(a)        # 뻗는 방향
    px, pz = uz, -ux                         # 옆 방향
    w = width * 0.5
    base = [(cx - px * w - ux * 1.5, cz - pz * w - uz * 1.5), (cx + px * w - ux * 1.5, cz + pz * w - uz * 1.5),
            (cx + px * w * 0.6 + ux * length, cz + pz * w * 0.6 + uz * length),
            (cx - px * w * 0.6 + ux * length, cz - pz * w * 0.6 + uz * length)]
    _hexa(mesh, slot, [(x, y0, z) for x, z in base] + [(x, y1, z) for x, z in base])


def _disc_y(mesh, slot, cx, cz, r, y0, y1, sides, cap_slot=None):
    """Y 축 원통(벽에서 앞으로 나온 둥근 판) — 옆면 한 묶음, 앞(-Y)·뒤 마개."""
    def ring(y):
        return [mesh.v(cx + r * math.cos(math.tau * i / sides), y, cz + r * math.sin(math.tau * i / sides)) for i in range(sides)]
    back, front = ring(y1), ring(y0)
    mesh.group += 1
    for i in range(sides):
        j = (i + 1) % sides
        t = math.tau * (i + 0.5) / sides
        out = (math.cos(t), 0.0, math.sin(t))
        mesh.tri(slot, back[i], back[j], front[j], out)
        mesh.tri(slot, back[i], front[j], front[i], out)
    for rim, y, out_y in ((front, y0, -1.0), (back, y1, 1.0)):
        mesh.group += 1
        center = mesh.v(cx, y, cz)
        for i in range(sides):
            mesh.tri(cap_slot or slot, rim[i], rim[(i + 1) % sides], center, (0.0, out_y, 0.0))


def _annulus_y(mesh, slot, cx, cz, r0, r1, y0, y1, sides):
    """Y 축 고리(시계 앞 테) — 안·바깥 옆면과 앞면."""
    def ring(r, y):
        return [mesh.v(cx + r * math.cos(math.tau * i / sides), y, cz + r * math.sin(math.tau * i / sides)) for i in range(sides)]
    for r, sign in ((r1, 1.0), (r0, -1.0)):
        a, b = ring(r, y1), ring(r, y0)
        mesh.group += 1
        for i in range(sides):
            j = (i + 1) % sides
            t = math.tau * (i + 0.5) / sides
            out = (sign * math.cos(t), 0.0, sign * math.sin(t))
            mesh.tri(slot, a[i], a[j], b[j], out)
            mesh.tri(slot, a[i], b[j], b[i], out)
    inner, outer = ring(r0, y0), ring(r1, y0)
    mesh.group += 1
    for i in range(sides):
        j = (i + 1) % sides
        mesh.tri(slot, inner[i], outer[i], outer[j], (0.0, -1.0, 0.0))
        mesh.tri(slot, inner[i], outer[j], inner[j], (0.0, -1.0, 0.0))


def picture_frame():
    """나무 테 액자 — 그림은 하늘·해·바다·초록 섬·등대(우리 섬). 그림 조각은 앞으로 조금씩 솟아 그림자 없이도 갈린다."""
    mesh = Mesh()
    hw, b = FRAME_W * 0.5, FRAME_BAR
    # 테 네 막대 — 앞 바깥 모서리를 깎아(top_inset 대신 앞쪽 0.8 좁힘) 나무 몰딩으로
    _box(mesh, "Wood", -hw, hw, -FRAME_D, 0.0, FRAME_H - b, FRAME_H)
    _box(mesh, "Wood", -hw, hw, -FRAME_D, 0.0, 0.0, b)
    _box(mesh, "Wood", -hw, -hw + b, -FRAME_D, 0.0, b, FRAME_H - b)
    _box(mesh, "Wood", hw - b, hw, -FRAME_D, 0.0, b, FRAME_H - b)
    # 테 안쪽 짙은 띠(그림과 테 사이 그림자 선) — 위 카메라에서도 그림 판이 테와 갈리게
    _box(mesh, "Trim", -hw + b, hw - b, PIC_Y - 0.3, 0.0, b, b + 0.8)
    _box(mesh, "Trim", -hw + b, hw - b, PIC_Y - 0.3, 0.0, FRAME_H - b - 0.8, FRAME_H - b)
    # 그림 바탕 — 위 하늘, 아래 바다
    x0, x1 = -hw + b, hw - b
    sea_top = 12.5
    _box(mesh, "Sky", x0, x1, PIC_Y, 0.0, sea_top, FRAME_H - b - 0.8)
    _box(mesh, "Sea", x0, x1, PIC_Y, 0.0, b + 0.8, sea_top)
    # 해 — 오른쪽 위 노란 네모
    _box(mesh, "Sun", 8.5, 13.0, PIC_Y - 0.4, PIC_Y, 18.5, 23.0)
    # 섬 — 바다 위 초록 언덕(윗면이 좁은 사다리꼴 덩어리)
    isl = [(-13.0, 9.0), (3.0, 9.0), (-1.0, 15.0), (-10.0, 15.0)]
    _hexa(mesh, "Land", [(x, PIC_Y, z) for x, z in isl] + [(x, PIC_Y - 0.6, z) for x, z in isl])
    # 등대 — 섬 위 빨강·흰 줄 탑 + 노란 불
    tx0, tx1 = -7.4, -4.6
    for k, slot in enumerate(("Tower", "White", "Tower")):
        _box(mesh, slot, tx0 + k * 0.2, tx1 - k * 0.2, PIC_Y - 1.0, PIC_Y, 15.0 + k * 2.2, 17.2 + k * 2.2)
    _box(mesh, "Sun", tx0 + 0.3, tx1 - 0.3, PIC_Y - 1.1, PIC_Y, 21.6, 23.2)
    _box(mesh, "Trim", tx0 - 0.2, tx1 + 0.2, PIC_Y - 1.2, PIC_Y, 23.2, 24.0)
    return mesh


def wall_clock():
    """둥근 나무 벽시계 — 몸 + 크림 판 + 눈금 12 + 10시 10분 바늘 + 빨강 가운데 못 + 위에 새싹 두 잎."""
    mesh = Mesh()
    cz = CLOCK_R
    _disc_y(mesh, "Wood", 0.0, cz, CLOCK_R, -CLOCK_D + 0.8, 0.0, 20)
    _disc_y(mesh, "Face", 0.0, cz, CLOCK_FACE_R, -CLOCK_D + 0.4, -CLOCK_D + 0.8, 20)
    _annulus_y(mesh, "Wood", 0.0, cz, CLOCK_FACE_R, CLOCK_R, -CLOCK_D - 0.6, -CLOCK_D + 0.8, 20)
    fy = -CLOCK_D + 0.4
    for h in range(12):
        a = math.radians(h * 30.0)
        big = h % 3 == 0
        r, half_w, half_l = 12.0, (0.9 if big else 0.55), (1.6 if big else 0.9)
        x, z = r * math.sin(a), cz + r * math.cos(a)
        ux, uz = math.sin(a), math.cos(a)
        px, pz = uz, -ux
        quad = [(x - px * half_w - ux * half_l, z - pz * half_w - uz * half_l), (x + px * half_w - ux * half_l, z + pz * half_w - uz * half_l),
                (x + px * half_w + ux * half_l, z + pz * half_w + uz * half_l), (x - px * half_w + ux * half_l, z - pz * half_w + uz * half_l)]
        _hexa(mesh, "Hand", [(qx, fy - 0.3, qz) for qx, qz in quad] + [(qx, fy, qz) for qx, qz in quad])
    _rot_box(mesh, "Hand", 0.0, cz, -60.0, 7.0, 1.8, fy - 0.8, fy)     # 시침 10시
    _rot_box(mesh, "Hand", 0.0, cz, 60.0, 10.5, 1.3, fy - 1.2, fy - 0.8)  # 분침 2시(10분)
    _disc_y(mesh, "Pin", 0.0, cz, 1.3, fy - 1.8, fy, 8)
    # 새싹 — 테 꼭대기에서 솟는 줄기 + 좌우로 벌어진 두 잎
    top = 2.0 * CLOCK_R
    _box(mesh, "Leaf", -0.7, 0.7, -2.6, -1.2, top - 0.6, top + 3.4)
    for sign in (-1.0, 1.0):
        leaf = [(0.0, top + 2.6), (sign * 2.5, top + 3.1), (sign * 5.0, top + 5.2), (sign * 1.6, top + 4.6)]
        if sign > 0:
            leaf = leaf[::-1]
        _hexa(mesh, "Leaf", [(x, -2.8, z) for x, z in leaf] + [(x, -1.0, z) for x, z in leaf])
    return mesh


def _cyl(mesh, slot, cx, cy, z0, z1, r0, r1, sides, cap_slot=None):
    """세운 원뿔대(병·화분) — 옆면 한 묶음 + 위 마개."""
    lower, upper = mesh.ring(sides, r0, z0, cx=cx, cy=cy), mesh.ring(sides, r1, z1, cx=cx, cy=cy)
    mesh.group += 1
    for i in range(sides):
        j = (i + 1) % sides
        t = math.tau * (i + 0.5) / sides
        out = (math.cos(t), math.sin(t), 0.0)
        mesh.tri(slot, lower[i], lower[j], upper[j], out)
        mesh.tri(slot, lower[i], upper[j], upper[i], out)
    mesh.group += 1
    mesh.fan(cap_slot or slot, upper, mesh.v(cx, cy, z1), 1.0)


def wall_shelf():
    """쐐기 받침 둘 위 판자 + 화분(초록 잎 덩어리)·책 둘·꿀단지 — 선반은 비어 있으면 「판자」 로만 읽혀 물건을 얹는다."""
    mesh = Mesh()
    hw = SHELF_W * 0.5
    top = SHELF_Z0 + SHELF_T
    _box(mesh, "Wood", -hw, hw, -SHELF_DEPTH, 0.0, SHELF_Z0, top)
    _box(mesh, "Trim", -hw, hw, -SHELF_DEPTH - 0.6, -SHELF_DEPTH, SHELF_Z0 + 0.4, top - 0.4)   # 앞 모서리 짙은 띠
    for x in (-14.0, 12.0):
        x0, x1 = x, x + 2.4
        _hexa(mesh, "Trim", [(x0, -1.2, 0.0), (x1, -1.2, 0.0), (x1, 0.0, 0.0), (x0, 0.0, 0.0),
                             (x0, -12.0, SHELF_Z0), (x1, -12.0, SHELF_Z0), (x1, 0.0, SHELF_Z0), (x0, 0.0, SHELF_Z0)])
    # 화분 — 테라코타 원뿔대 + 초록 잎 셋(위로 갈라진 덩어리)
    _cyl(mesh, "Pot", -13.0, -8.0, top, top + 7.0, 3.4, 4.4, 10, cap_slot="Soil")
    for dx, dz, r in ((-1.8, 9.5, 3.2), (1.9, 10.5, 3.0), (0.0, 13.0, 3.3)):
        _cyl(mesh, "Leaf", -13.0 + dx, -8.0, top + dz - r, top + dz + r * 0.4, r * 0.55, r, 8)
        _cyl(mesh, "Leaf", -13.0 + dx, -8.0, top + dz + r * 0.4, top + dz + r * 1.1, r, r * 0.3, 8)
    # 책 둘 — 세운 빨강·파랑, 파랑은 살짝 기울어 빨강에 기댐
    _box(mesh, "Book1", -3.0, 0.4, -13.0, -2.0, top, top + 12.0)
    _hexa(mesh, "Book2", [(0.6, -12.5, top), (3.2, -12.5, top), (3.2, -2.5, top), (0.6, -2.5, top),
                          (1.6, -12.5, top + 10.0), (4.2, -12.5, top + 10.0), (4.2, -2.5, top + 10.0), (1.6, -2.5, top + 10.0)])
    # 꿀단지 — 노란 병 + 갈색 뚜껑
    _cyl(mesh, "Honey", 12.0, -8.0, top, top + 6.5, 3.6, 3.2, 10)
    _cyl(mesh, "Trim", 12.0, -8.0, top + 6.5, top + 8.2, 3.4, 3.4, 10)
    return mesh


def _prism_y(mesh, slot, pts, y0, y1):
    """X-Z 볼록 다각형 pts 를 Y 로 밀어낸 판(아치·삼각 갈고리·섬) — 옆면은 변마다, 앞(-Y)·뒤 마개는 부채꼴."""
    cx = sum(p[0] for p in pts) / len(pts)
    cz = sum(p[1] for p in pts) / len(pts)
    n = len(pts)
    for i in range(n):
        (ax, az), (bx, bz) = pts[i], pts[(i + 1) % n]
        mesh.group += 1
        out = ((ax + bx) * 0.5 - cx, 0.0, (az + bz) * 0.5 - cz)
        a0, b0, b1, a1 = mesh.v(ax, y0, az), mesh.v(bx, y0, bz), mesh.v(bx, y1, bz), mesh.v(ax, y1, az)
        mesh.tri(slot, a0, b0, b1, out)
        mesh.tri(slot, a0, b1, a1, out)
    for y, out_y in ((y0, -1.0), (y1, 1.0)):
        mesh.group += 1
        rim = [mesh.v(x, y, z) for x, z in pts]
        center = mesh.v(cx, y, cz)
        for i in range(n):
            mesh.tri(slot, rim[i], rim[(i + 1) % n], center, (0.0, out_y, 0.0))


def _bar(mesh, slot, p0, p1, width, y0, y1):
    """X-Z 두 점 p0→p1 을 잇는 납작한 막대(닻 팔·밧줄·지도 X) — 끝은 반 폭만큼 더 뻗어 이음매가 안 벌어진다."""
    dx, dz = p1[0] - p0[0], p1[1] - p0[1]
    length = math.hypot(dx, dz) or 1.0
    ux, uz = dx / length, dz / length
    px, pz = -uz * width * 0.5, ux * width * 0.5
    ex, ez = ux * width * 0.5, uz * width * 0.5
    quad = [(p0[0] - ex - px, p0[1] - ez - pz), (p0[0] - ex + px, p0[1] - ez + pz),
            (p1[0] + ex + px, p1[1] + ez + pz), (p1[0] + ex - px, p1[1] + ez - pz)]
    _hexa(mesh, slot, [(x, y0, z) for x, z in quad] + [(x, y1, z) for x, z in quad])


def _arch(half_w, z0, spring, sides=10):
    """밑이 네모, 위가 반원인 아치 윤곽(X-Z) — 반원 가운데 높이 spring, 반지름 half_w."""
    pts = [(half_w, z0)]
    for i in range(sides + 1):
        t = math.pi * i / sides
        pts.append((half_w * math.cos(t), spring + half_w * math.sin(t)))
    pts.append((-half_w, z0))
    return pts


def _sprout(mesh, top, y_front, y_back):
    """새싹 두 잎(우리 표식, 벽시계와 같은 모양) — top 에서 줄기가 솟는다. 꼭대기 = top + 5.2."""
    _box(mesh, "Leaf", -0.7, 0.7, y_front + 0.2, y_back, top - 0.6, top + 3.4)
    for sign in (-1.0, 1.0):
        leaf = [(0.0, top + 2.6), (sign * 2.5, top + 3.1), (sign * 5.0, top + 5.2), (sign * 1.6, top + 4.6)]
        if sign > 0:
            leaf = leaf[::-1]
        _hexa(mesh, "Leaf", [(x, y_front, z) for x, z in leaf] + [(x, y_back, z) for x, z in leaf])


# 아치 거울 — 테 가로 30 · 아치 꼭대기 34 + 새싹(39.2). 판은 테 앞면보다 0.3 솟아 빛을 받는다.
MIRROR_HW, MIRROR_SPRING, MIRROR_D, MIRROR_BAR = 15.0, 19.0, 3.0, 3.5


def wall_mirror():
    """나무 테 아치 거울 — 유리 아이템이 없어 닦은 쇠 판(옅은 하늘 회색) + 비스듬한 흰 빛 줄 둘 + 밑 받침 턱 + 꼭대기 새싹."""
    mesh = Mesh()
    _prism_y(mesh, "Wood", _arch(MIRROR_HW, 0.0, MIRROR_SPRING), -MIRROR_D, 0.0)
    inner = MIRROR_HW - MIRROR_BAR
    _prism_y(mesh, "Glass", _arch(inner, MIRROR_BAR, MIRROR_SPRING), -MIRROR_D - 0.3, -MIRROR_D + 0.5)
    _bar(mesh, "Shine", (-7.0, 9.0), (2.0, 24.0), 2.6, -MIRROR_D - 0.5, -MIRROR_D - 0.2)
    _bar(mesh, "Shine", (-2.0, 7.0), (5.0, 18.5), 1.2, -MIRROR_D - 0.5, -MIRROR_D - 0.2)
    _box(mesh, "Trim", -MIRROR_HW, MIRROR_HW, -MIRROR_D - 2.5, 0.0, 0.0, 1.8)      # 밑 받침 턱(짙은 나무)
    _sprout(mesh, MIRROR_SPRING + MIRROR_HW, -MIRROR_D + 0.2, -1.0)
    return mesh


def wall_map():
    """섬 지도 — 위아래 나무 막대에 걸린 천(양피 빛) + 바다 + 우리 섬 둘(밝은 해변·짙은 숲) + 빨강 X + 점 길 + 귀퉁이 해(방위표)."""
    mesh = Mesh()
    hw = 17.0
    _box(mesh, "Paper", -hw, hw, -0.8, 0.0, 2.5, 27.5)
    for z in (2.4, 27.6):   # 막대 — 천 앞에 가로지른 나무 막대 + 양끝 짙은 손잡이
        _box(mesh, "Wood", -18.0, 18.0, -4.0, 0.0, z - 2.0, z + 2.0)
        for x0, x1 in ((-20.0, -18.0), (18.0, 20.0)):
            _box(mesh, "Trim", x0, x1, -4.4, 0.0, z - 2.4, z + 2.4)
    py = -1.0
    _box(mesh, "Sea", -15.0, 15.0, py, -0.8, 4.8, 25.2)
    big = [(-12.0, 12.0), (-8.0, 7.5), (-1.0, 7.0), (3.0, 10.5), (2.0, 17.5), (-4.0, 21.0), (-10.0, 18.5)]
    _prism_y(mesh, "Leaf2", big, py - 0.4, py)
    hill = [(-9.0, 13.0), (-5.0, 10.0), (-1.0, 12.0), (-2.0, 17.0), (-7.0, 18.0)]
    _prism_y(mesh, "Land", hill, py - 0.8, py - 0.4)
    small = [(7.0, 15.0), (10.0, 13.5), (12.5, 16.0), (11.0, 19.5), (7.5, 19.0)]
    _prism_y(mesh, "Leaf2", small, py - 0.4, py)
    # 빨강 X — 작은 섬 위
    _bar(mesh, "Pin", (8.3, 15.3), (11.5, 18.7), 1.1, py - 1.0, py - 0.4)
    _bar(mesh, "Pin", (8.3, 18.7), (11.5, 15.3), 1.1, py - 1.0, py - 0.4)
    # 점 길 — 큰 섬 오른쪽 끝에서 X 까지
    for i in range(4):
        t = (i + 0.5) / 4.0
        x, z = 3.5 + (7.8 - 3.5) * t, 12.0 + (16.0 - 12.0) * t + math.sin(t * math.pi) * 1.5
        _box(mesh, "Hand", x - 0.45, x + 0.45, py - 0.6, py, z - 0.45, z + 0.45)
    # 귀퉁이 해(방위표 자리) — 노란 네모 위 짙은 점
    _box(mesh, "Sun", 10.5, 13.5, py - 0.4, py, 21.0, 24.0)
    _box(mesh, "Hand", 11.6, 12.4, py - 0.6, py - 0.4, 22.1, 22.9)
    return mesh


def wall_anchor():
    """닻 — 꼭대기 고리 + 나무 가로대 + 쇠 몸 + 굽은 팔 + 세모 갈고리 둘 + 몸에 감은 밧줄 넷(작살단 바닷가 집 장식)."""
    mesh = Mesh()
    _annulus_y(mesh, "Iron", 0.0, 34.6, 2.2, 4.0, -3.0, 0.0, 12)
    _box(mesh, "Iron", -1.7, 1.7, -3.0, 0.0, 4.0, 31.0)                      # 몸
    _box(mesh, "Wood", -11.0, 11.0, -4.0, 0.0, 26.5, 29.5)                  # 가로대
    for sx in (-1.0, 1.0):
        _box(mesh, "Trim", min(sx * 11.0, sx * 12.4), max(sx * 11.0, sx * 12.4), -4.4, 0.0, 26.0, 30.0)
    # 굽은 팔 — 가운데 (0, 15) 반지름 13 의 아래 호(200° → 340°)
    arc = [(13.0 * math.cos(math.radians(a)), 15.0 + 13.0 * math.sin(math.radians(a))) for a in range(200, 341, 20)]
    for p0, p1 in zip(arc, arc[1:]):
        _bar(mesh, "Iron", p0, p1, 3.0, -3.0, 0.0)
    for sx in (-1.0, 1.0):   # 갈고리 — 팔 끝에서 바깥 위로 세모
        tri = [(sx * 16.0, 9.0), (sx * 9.5, 10.0), (sx * 14.8, 17.5)]
        _prism_y(mesh, "Iron", tri, -3.2, 0.0)
    for k in range(4):       # 밧줄 — 몸을 비스듬히 감음
        z = 12.0 + k * 3.2
        _bar(mesh, "Rope", (-2.8, z), (2.8, z + 1.6), 1.6, -4.0, -2.4)
    return mesh


def wall_wreath():
    """들꽃 화환 — 잎 고리(두 초록 번갈아) + 빨강·노랑·흰 꽃 다섯 + 밑 꿀색 리본 매듭과 꼬리 둘. 지름 36."""
    mesh = Mesh()
    cz, r_in, r_out = 19.0, 10.0, 18.0
    _annulus_y(mesh, "Leaf", 0.0, cz, r_in, r_out, -3.0, 0.0, 18)
    for k in range(14):      # 잎 — 고리를 따라 시계 방향으로 누운 잎
        phi = k * 360.0 / 14
        a = math.radians(phi)
        x, z = 14.0 * math.sin(a), cz + 14.0 * math.cos(a)
        _rot_box(mesh, "Leaf2" if k % 2 else "Leaf", x, z, phi + 100.0, 6.5, 4.2, -4.6, -3.0)
    for phi, petal in ((0, "Pin"), (65, "Sun"), (125, "White"), (235, "Sun"), (295, "Pin")):
        a = math.radians(phi)
        x, z = 14.0 * math.sin(a), cz + 14.0 * math.cos(a)
        for j in range(5):   # 꽃잎 다섯 — 둥근 판 하나면 위에서 너트처럼 읽혔다(루프#1390 첫 렌더)
            b = math.radians(phi + j * 72.0)
            _disc_y(mesh, petal, x + 2.0 * math.sin(b), z + 2.0 * math.cos(b), 1.6, -5.4, -4.4, 7)
        _disc_y(mesh, "Honey" if petal != "Sun" else "Trim", x, z, 1.2, -6.0, -5.2, 7)
    # 리본 — 아래(φ 180 = z 5) 매듭 + 좌우 고리 세모 + 꼬리 둘(밑 z 0)
    knot_z = cz - 14.0
    _box(mesh, "Honey", -1.6, 1.6, -6.0, -3.0, knot_z - 1.6, knot_z + 1.6)
    for sx in (-1.0, 1.0):
        _prism_y(mesh, "Honey", [(sx * 1.4, knot_z), (sx * 6.5, knot_z + 3.0), (sx * 6.5, knot_z - 3.0)], -5.4, -3.2)
        _bar(mesh, "Honey", (sx * 0.8, knot_z - 1.2), (sx * 3.2, 0.7), 1.6, -5.2, -3.4)
    return mesh


# 네온 간판 — 판 반지름 20(가로 39.4) · 조개 가운데 높이 5 · 관 반지름 17 · 관 굵기 1.6.
NEON_R, NEON_CZ, NEON_TUBE_R, NEON_TUBE = 20.0, 5.0, 17.0, 1.6


def neon_sign(lit=False):
    """조개 네온 간판 — 짙은 나무 부채꼴 판 + 쇠 받침 넷 + 유리 관: 테두리 반원(10~170°) · 경첩에서 테까지 옆선 둘 · 부채살 넷 · 밑 귀 둘 · 꼭대기 새싹.
    꺼짐 = 관 전부 빛 없는 `Glass`. `lit` 이면 켜짐 짝(`NeonSign_On_01`, 08b-2) — 테두리·옆선 `NeonPink` · 부채살·밑 귀 `NeonSky`, 몸은 같은 꼴."""
    mesh = Mesh()
    rim, ray = ("NeonPink", "NeonSky") if lit else ("Glass", "Glass")
    board = [(-5.0, 0.0), (5.0, 0.0)] + [(NEON_R * math.cos(math.radians(a)), NEON_CZ + NEON_R * math.sin(math.radians(a)))
                                         for a in range(10, 171, 10)]
    _prism_y(mesh, "Trim", board, -1.5, 0.0)                                            # 판
    tube_y0, tube_y1 = -3.5, -2.0
    for bx, bz in ((-12.0, 8.0), (12.0, 8.0), (-5.0, 18.0), (5.0, 18.0)):
        _box(mesh, "Iron", bx - 0.6, bx + 0.6, tube_y1, -1.5, bz - 0.6, bz + 0.6)         # 관 받침
    arc = [(NEON_TUBE_R * math.cos(math.radians(a)), NEON_CZ + NEON_TUBE_R * math.sin(math.radians(a))) for a in range(10, 171, 20)]
    for p0, p1 in zip(arc, arc[1:]):
        _bar(mesh, rim, p0, p1, NEON_TUBE, tube_y0, tube_y1)                             # 테두리
    hinge = (0.0, 3.0)
    for a in (10.0, 170.0, 50.0, 75.0, 105.0, 130.0):
        r = NEON_TUBE_R if a in (10.0, 170.0) else NEON_TUBE_R - 3.0
        tip = (r * math.cos(math.radians(a)), NEON_CZ + r * math.sin(math.radians(a)))
        _bar(mesh, rim if a in (10.0, 170.0) else ray, hinge, tip, NEON_TUBE, tube_y0, tube_y1)  # 옆선 둘 · 부채살 넷
    for sign in (-1.0, 1.0):
        _bar(mesh, ray, (sign * 1.5, 1.5), (sign * 4.5, 1.5), NEON_TUBE, tube_y0, tube_y1)  # 밑 귀
    _sprout(mesh, NEON_CZ + NEON_R - 0.5, -1.5, -0.2)
    return mesh


if __name__ == "__main__":
    for name, build, slots in (("PictureFrame_01", picture_frame, ["Wood", "Trim", "Sky", "Sea", "Sun", "Land", "Tower", "White"]),
                               ("WallClock_01", wall_clock, ["Wood", "Face", "Hand", "Pin", "Leaf"]),
                               ("WallShelf_01", wall_shelf, ["Wood", "Trim", "Pot", "Soil", "Leaf", "Book1", "Book2", "Honey"]),
                               ("WallMirror_01", wall_mirror, ["Wood", "Trim", "Glass", "Shine", "Leaf"]),
                               ("WallMap_01", wall_map, ["Wood", "Trim", "Paper", "Sea", "Leaf2", "Land", "Pin", "Hand", "Sun"]),
                               ("WallAnchor_01", wall_anchor, ["Iron", "Wood", "Trim", "Rope"]),
                               ("WallWreath_01", wall_wreath, ["Leaf", "Leaf2", "Pin", "Sun", "White", "Honey", "Trim"]),
                               ("NeonSign_01", neon_sign, ["Trim", "Iron", "Glass", "Leaf"]),
                               ("NeonSign_On_01", lambda: neon_sign(lit=True), ["Trim", "Iron", "NeonPink", "NeonSky", "Leaf"])):
        mesh = build()
        low = min(p[2] for p in mesh.verts)   # 밑면 = z 0(걸이 높이 = 장식 밑면) — 리본 꼬리·닻 호 끝이 살짝 넘는 것을 맞춘다
        mesh.verts = [(x, y, z - low) for x, y, z in mesh.verts]
        triangles = mesh.write(name, slots)
        xs, ys, zs = zip(*mesh.verts)
        print("{}.obj 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} · x {:.1f}~{:.1f} y {:.1f}~{:.1f} z {:.1f}~{:.1f}".format(
            name, triangles, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs),
            min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
