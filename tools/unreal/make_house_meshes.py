"""M15-04 레퍼런스식 집·돌길 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_house_meshes.py   → Saved/Generated/Cottage_01·TentHouse_01·Path_01.obj (+ .mtl)

M98-a 법적 위험 정리: 우리 오두막은 **가로 통나무 벽(꿀갈색) + 짚 지붕(가로 짚단 줄, 처마에 밧줄 묶음)** 이다.
레퍼런스 박하 세로 판자·주황 기와 골은 버렸다. 모서리 기둥·박공 테두리·창틀은 진갈색 나무(Trim), 나무 문, 굴뚝.
앞마당 길은 돌 판석이 아니라 **나무 판자 길**(가로 판자를 길 방향으로 깐다, 슬롯 이름 Stone 은 재질 호환 때문에 유지).
면마다 정점을 따로 둬 한 면 한 색 각진 모양(world_environment.md "한 면 한 색"). 박공 삼각형이 -X(카메라 쪽)를 본다.
슬롯: Wall·Roof·Trim·Door·Glass (집), Stone (길). 색은 `setup_ranch_buildings.py` 가 슬롯마다 입힌다. 단위 cm, Z 위.
"""
import math
import os
import random

from make_tree_meshes import OUT_DIR, Mesh

SEED = 1506
WALL_X, WALL_Y, WALL_Z = 200.0, 270.0, 300.0   # 벽 반폭(깊이)·반폭(너비)·높이 — steam_22 벽 높이 ≈ 캐릭터 1.8배
GABLE_Z = 210.0          # 박공 높이 — 지붕 경사 약 38°
EAVE_OUT, ROOF_OUT, ROOF_THICK = 55.0, 40.0, 30.0   # 처마 옆 내밈 · 앞뒤 내밈 · 지붕 두께
STRAW_STEP = 38.0        # 지붕 짚단 줄 간격(경사 방향)
LOG_STEP = 30.0          # 벽 가로 통나무 줄 간격(높이 방향)
BOARD_STEP = 40.0        # 마루 판자 간격


def quad(mesh, slot, p, out):
    """네 점 p(둘레 순서) 한 면 — 면마다 새 정점·새 묶음이라 각지게 보인다."""
    mesh.group += 1
    a, b, c, d = (mesh.v(*q) for q in p)
    mesh.tri(slot, a, b, c, out)
    mesh.tri(slot, a, c, d, out)


def box(mesh, slot, x0, x1, y0, y1, z0, z1, bottom=False):
    quad(mesh, slot, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1))
    quad(mesh, slot, [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)], (-1, 0, 0))
    quad(mesh, slot, [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], (1, 0, 0))
    quad(mesh, slot, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0))
    quad(mesh, slot, [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], (0, 1, 0))
    if bottom:
        quad(mesh, slot, [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], (0, 0, -1))


def _roof_z(y):
    """벽 윗선(z=WALL_Z) 기준 지붕 윗면 높이 — 용마루(y=0)에서 처마로 곧게 내려간다."""
    return WALL_Z + GABLE_Z + ROOF_THICK - abs(y) * GABLE_Z / WALL_Y


def _roof_side(mesh, sign):
    """한쪽 경사 — 윗면(Roof)·밑면·처마 끝(Roof)·앞뒤 박공 끝(Trim, 크림 테두리)."""
    x0, x1 = -WALL_X - ROOF_OUT, WALL_X + ROOF_OUT
    ye = sign * (WALL_Y + EAVE_OUT)
    zr, ze = _roof_z(0.0), _roof_z(ye)
    top = [(x0, 0.0, zr), (x1, 0.0, zr), (x1, ye, ze), (x0, ye, ze)]
    low = [(x, y, z - ROOF_THICK) for x, y, z in top]
    quad(mesh, "Roof", top, (0, sign * 0.6, 1))
    quad(mesh, "Roof", low, (0, 0, -1))
    quad(mesh, "Roof", [top[2], top[3], low[3], low[2]], (0, sign, 0))
    quad(mesh, "Trim", [top[0], top[3], low[3], low[0]], (-1, 0, 0))
    quad(mesh, "Trim", [top[1], top[2], low[2], low[1]], (1, 0, 0))
    # 짚단 줄 — 경사를 가로지르는(처마와 나란한) 낮은 줄. 처마 끝에는 밧줄 묶음(Trim) 띠.
    for i in range(1, int(abs(ye) / STRAW_STEP)):
        y = sign * i * STRAW_STEP
        w, h = STRAW_STEP * 0.5, 7.0
        yl = y - sign * w
        quad(mesh, "Roof", [(x0, yl, _roof_z(yl)), (x1, yl, _roof_z(yl)), (x1, y, _roof_z(y) + h), (x0, y, _roof_z(y) + h)], (0, -sign * 0.6, 1))
    box(mesh, "Trim", x0, x1, min(ye - sign * 22.0, ye - sign * 6.0), max(ye - sign * 22.0, ye - sign * 6.0), ze - 4.0, ze + 6.0)
    # 용마루 덮개
    box(mesh, "Roof", x0, x1, -14.0, 14.0, zr - 6.0, zr + 14.0)


def _window(mesh, x_face, y0, y1, z0, z1):
    """앞면(-X) 창 — 크림 틀 안에 짙은 유리, 십자 창살."""
    box(mesh, "Trim", x_face - 10.0, x_face, y0 - 12.0, y1 + 12.0, z0 - 12.0, z1 + 12.0)
    box(mesh, "Glass", x_face - 13.0, x_face - 10.0, y0, y1, z0, z1)
    ym, zm = (y0 + y1) * 0.5, (z0 + z1) * 0.5
    box(mesh, "Trim", x_face - 16.0, x_face - 13.0, ym - 4.0, ym + 4.0, z0, z1)
    box(mesh, "Trim", x_face - 16.0, x_face - 13.0, y0, y1, zm - 4.0, zm + 4.0)


WALL_T = 15.0            # 벽 두께 — 속이 빈 집(실내)
DOOR_HW, DOOR_H = 58.0, 208.0   # 문 반폭·높이 — 캡슐(반지름 약 42)이 지나간다
GABLE_WIN = (-48.0, 48.0, 340.0, 410.0)   # 박공 창 (y0, y1, z0, z1)
SIDE_WIN = (130.0, 45.0, 130.0, 220.0)    # 앞면 양옆 창 (가운데 |y|, 반폭, z0, z1)
CHIMNEY = (40.0, 120.0, 110.0, 190.0, 60.0, 110.0)   # 굴뚝 (x0, x1, y0, y1, 벽 윗선 위 밑, 박공 꼭대기 위 끝)

# 위 치수 묶음 — 거점 오두막(`Cottage_01`)이 기본. `_use_dims` 로 갈아 끼워 같은 함수로 다른 크기 집을 만든다.
COTTAGE_DIMS = dict(WALL_X=WALL_X, WALL_Y=WALL_Y, WALL_Z=WALL_Z, GABLE_Z=GABLE_Z, EAVE_OUT=EAVE_OUT, ROOF_OUT=ROOF_OUT,
                    GABLE_WIN=GABLE_WIN, SIDE_WIN=SIDE_WIN, CHIMNEY=CHIMNEY)
# M108-02 집이 된 텐트(3×3 칸 = 300cm, DESIGN 8.11) — 벽 안쪽 면이 발자국 바깥 변(±150)이라 벽 장식이 벽 칸 변 자리에
# 그대로 붙는다. 벽 260 = 걸 높이 최고 200 + 장식 세로 40 + 틈 20. 박공은 오두막과 같은 경사(약 38°).
TENT_HOUSE_DIMS = dict(WALL_X=165.0, WALL_Y=165.0, WALL_Z=260.0, GABLE_Z=130.0, EAVE_OUT=35.0, ROOF_OUT=25.0,
                       GABLE_WIN=(-28.0, 28.0, 300.0, 345.0), SIDE_WIN=(112.0, 24.0, 110.0, 180.0),
                       CHIMNEY=(30.0, 80.0, 60.0, 110.0, 40.0, 80.0))


def _use_dims(dims):
    globals().update(dims)


def _floor(mesh):
    """실내 마루 — 바닥 높이 2cm(가구가 z=0 에 그대로 선다), 판자 두 톤이 +Y 로 번갈아."""
    x0, x1, y0, y1 = -WALL_X + WALL_T, WALL_X - WALL_T, -WALL_Y + WALL_T, WALL_Y - WALL_T
    x, dark = x0, False
    while x < x1:
        xe = min(x + BOARD_STEP, x1)
        box(mesh, "FloorDark" if dark else "Floor", x, xe, y0, y1, -10.0, 2.0)
        x, dark = xe, not dark


def _back_side_walls(mesh):
    """뒷벽·옆벽·옆면 통나무 줄·모서리 기둥 — 1층 몸통과 위층(`cottage_upper`)이 같이 쓴다."""
    box(mesh, "Wall", WALL_X - WALL_T, WALL_X, -WALL_Y, WALL_Y, -10.0, WALL_Z)          # 뒷벽
    for side in (-1, 1):                                                                 # 옆벽
        box(mesh, "Wall", -WALL_X, WALL_X, min(side * WALL_Y, side * (WALL_Y - WALL_T)),
            max(side * WALL_Y, side * (WALL_Y - WALL_T)), -10.0, WALL_Z)
    # 옆면 가로 통나무 줄 — 벽 바깥에 반 뼘 튀어나온 긴 통나무가 높이 방향으로 쌓인다
    z = LOG_STEP
    while z < WALL_Z - 10.0:
        for side in (-1, 1):
            yf = side * WALL_Y
            box(mesh, "Wall", -WALL_X, WALL_X, min(yf, yf + side * 3.5), max(yf, yf + side * 3.5), z - 4.0, z + 4.0)
        z += LOG_STEP
    for cx in (-WALL_X, WALL_X):
        for cy in (-WALL_Y, WALL_Y):
            box(mesh, "Trim", cx - 14.0, cx + 14.0, cy - 14.0, cy + 14.0, -10.0, WALL_Z + 8.0)


def cottage_shell(name):
    """집 몸통 — 늘 보이는 부분: 마루·뒷벽·옆벽·모서리 기둥·밑돌 띠·디딤돌. 앞벽·지붕은 `cottage_cover` 가 덮는다.
    레퍼런스처럼 안에 들어가면 덮개가 숨어 방이 위에서 보인다(`USnareHouseRoofSubsystem`)."""
    mesh = Mesh()
    fx = -WALL_X
    _floor(mesh)
    _back_side_walls(mesh)
    # 밑돌 띠 — 벽 바깥 둘레만(안은 마루). 앞은 문 자리를 비운다.
    o = 8.0
    box(mesh, "Trim", WALL_X - WALL_T, WALL_X + o, -WALL_Y - o, WALL_Y + o, -10.0, 22.0)
    for side in (-1, 1):
        box(mesh, "Trim", -WALL_X - o, WALL_X + o, min(side * (WALL_Y + o), side * (WALL_Y - WALL_T)),
            max(side * (WALL_Y + o), side * (WALL_Y - WALL_T)), -10.0, 22.0)
        box(mesh, "Trim", fx - o, fx + WALL_T, min(side * DOOR_HW, side * WALL_Y), max(side * DOOR_HW, side * WALL_Y), -10.0, 22.0)
    box(mesh, "Trim", fx - 70.0, fx, -80.0, 80.0, -10.0, 4.0)                            # 디딤돌
    return mesh.write(name, ["Wall", "Trim", "Floor", "FloorDark"])


def cottage_cover(name, door=True, roof=True):
    """집 덮개 — 앞벽(문 뚫림)·창·박공·지붕·굴뚝. 안에 들어가면 통째로 숨는다(충돌은 남아 앞벽이 계속 막는다).
    M108-06c 2층 집은 이 덮개를 둘로 가른다: `door=True, roof=False` = 1층 앞벽(`TentHouseFront_01`),
    `door=False, roof=True` = 위층 앞벽(가운데 창 하나)·박공·지붕(`TentHouseUpperRoof_01`, 2층 바닥 높이만큼 올려 세운다)."""
    mesh = Mesh()
    fx = -WALL_X
    if door:
        box(mesh, "Wall", fx, fx + WALL_T, -WALL_Y, -DOOR_HW, -10.0, WALL_Z)
        box(mesh, "Wall", fx, fx + WALL_T, DOOR_HW, WALL_Y, -10.0, WALL_Z)
        box(mesh, "Wall", fx, fx + WALL_T, -DOOR_HW, DOOR_HW, DOOR_H, WALL_Z)
    else:
        box(mesh, "Wall", fx, fx + WALL_T, -WALL_Y, WALL_Y, -10.0, WALL_Z)
    if roof:
        for x, out in ((-WALL_X, -1), (WALL_X, 1)):                                      # 박공 삼각형 앞·뒤
            mesh.group += 1
            mesh.tri("Wall", mesh.v(x, -WALL_Y, WALL_Z), mesh.v(x, WALL_Y, WALL_Z), mesh.v(x, 0.0, WALL_Z + GABLE_Z), (out, 0, 0))
    z = LOG_STEP                                                                         # 앞면 통나무 줄 — 문 자리는 건너뛴다
    while z < WALL_Z - 10.0:
        if not door:
            box(mesh, "Wall", fx - 3.5, fx, -WALL_Y, WALL_Y, z - 4.0, z + 4.0)
            z += LOG_STEP
            continue
        for y0, y1 in ((-WALL_Y, -DOOR_HW - 16.0), (DOOR_HW + 16.0, WALL_Y)):
            box(mesh, "Wall", fx - 3.5, fx, y0, y1, z - 4.0, z + 4.0)
        if z > DOOR_H + 16.0:
            box(mesh, "Wall", fx - 3.5, fx, -DOOR_HW - 16.0, DOOR_HW + 16.0, z - 4.0, z + 4.0)
        z += LOG_STEP
    if roof:
        _roof_side(mesh, -1)
        _roof_side(mesh, 1)
    if door:
        # 문틀(크림) + 안쪽으로 활짝 열려 앞벽 안쪽에 붙은 나무 문. 방 쪽으로 116cm 튀어나와 있던 때는 문짝이 탁자·의자와
        # 함께 집 A 안을 둘로 갈라 한쪽 46칸에 못 갔다(HouseFurniture.WalkInsideAndOut). 안에서는 덮개째 숨어 안 보인다.
        for side in (-1, 1):
            box(mesh, "Trim", fx - 10.0, fx, min(side * DOOR_HW, side * (DOOR_HW + 14.0)), max(side * DOOR_HW, side * (DOOR_HW + 14.0)), 0.0, DOOR_H + 14.0)
        box(mesh, "Trim", fx - 10.0, fx, -DOOR_HW - 14.0, DOOR_HW + 14.0, DOOR_H, DOOR_H + 14.0)
        # 열린 문짝은 옆벽 안쪽 면에서 멈춘다 — 작은 집에선 문 폭 둘이 안 들어간다
        box(mesh, "Door", fx + WALL_T, fx + WALL_T + 6.0, DOOR_HW, min(3.0 * DOOR_HW, WALL_Y - WALL_T), 2.0, DOOR_H)
    if roof:
        _window(mesh, fx, *GABLE_WIN)
    wy, whw, wz0, wz1 = SIDE_WIN
    if door:
        for side in (-1, 1):
            _window(mesh, fx, side * wy - whw, side * wy + whw, wz0, wz1)
    else:
        _window(mesh, fx, -UPPER_WIN_HW, UPPER_WIN_HW, wz0, wz1)                         # 위층 앞벽 — 가운데 창 하나
    if roof:
        cx0, cx1, cy0, cy1, cbase, ctop = CHIMNEY
        box(mesh, "Trim", cx0, cx1, cy0, cy1, WALL_Z + cbase, WALL_Z + GABLE_Z + ctop)   # 굴뚝
        box(mesh, "Glass", cx0 + 12.0, cx1 - 12.0, cy0 + 12.0, cy1 - 12.0, WALL_Z + GABLE_Z + ctop - 10.0, WALL_Z + GABLE_Z + ctop + 2.0)
    slots = ["Wall"] + (["Roof"] if roof else []) + ["Trim"] + (["Door"] if door else []) + ["Glass"]
    return mesh.write(name, slots)


# M108-06c 2층 집 (명세 `docs/specs/house-stairs.md` 3절 🟡) — 2층 바닥 윗면 = 집 바닥 + 1층 벽 + 판 두께.
# 위층 메시(벽·덮개·바닥 판)는 원점이 2층 바닥 윗면이라 액터가 이 높이만큼 올려 세운다(`SnareBuildRules::UpperFloorZCm`).
UPPER_FLOOR_T = 15.0
UPPER_WIN_HW = 40.0      # 위층 앞벽 가운데 창 반폭 — 1층 앞 창(반폭 24) 둘 자리를 하나로


def cottage_upper(name):
    """위층 몸 — 뒷벽·옆벽·통나무 줄·모서리 기둥(1층과 같은 모양) + 1층·2층 사이 바닥 띠(바깥 둘레, 판 두께를 가린다).
    앞벽은 덮개(`cottage_cover(door=False)`) 몫이라 2층에 서면 지붕과 함께 숨는다(06e). 발이 2층 바닥 끝에서 떨어지지 않게
    충돌은 반입이 복잡 충돌로 켠다."""
    mesh = Mesh()
    _back_side_walls(mesh)
    o, z0, z1 = 6.0, -UPPER_FLOOR_T - 8.0, 6.0                                           # 바닥 띠 — 1층 벽 윗선 아래까지
    box(mesh, "Trim", WALL_X - WALL_T, WALL_X + o, -WALL_Y - o, WALL_Y + o, z0, z1)
    box(mesh, "Trim", -WALL_X - o, -WALL_X + WALL_T, -WALL_Y - o, WALL_Y + o, z0, z1)
    for side in (-1, 1):
        box(mesh, "Trim", -WALL_X - o, WALL_X + o, min(side * (WALL_Y + o), side * (WALL_Y - WALL_T)),
            max(side * (WALL_Y + o), side * (WALL_Y - WALL_T)), z0, z1)
    return mesh.write(name, ["Wall", "Trim"])


def floor_tile(name, size):
    """2층 바닥 판 한 칸(`size` = 거점 칸 한 변) — 윗면 z 0, 두께 `UPPER_FLOOR_T`, 판자 두 톤이 +Y 로 번갈아(1층 마루와 같은 색).
    액터가 3×3 칸 중 계단이 없는 칸에만 하나씩 깐다(계단 구멍). 판자 4장이라 칸끼리 이어 깔아도 무늬가 맞는다."""
    mesh = Mesh()
    h, step = size * 0.5, size / 4.0
    for i in range(4):
        box(mesh, "FloorDark" if i % 2 else "Floor", -h + i * step, -h + (i + 1) * step, -h, h, -UPPER_FLOOR_T, 0.0, bottom=True)
    return mesh.write(name, ["Floor", "FloorDark"])


def path(name, rng, rows=8, tile=62.0, gap=9.0):
    """나무 판자 길 — 길 방향(+X)으로 긴 판자 셋을 나란히, 이음매는 줄마다 어긋나고 두께가 조금씩 흔들린다. +X 로 뻗는다."""
    mesh = Mesh()
    total = rows * (tile + gap)
    for c in (-1, 0, 1):
        x = 0.0
        while x < total:
            xe = min(x + rng.uniform(1.6, 2.4) * tile, total)
            cy = c * (tile * 0.62 + 3.0)
            box(mesh, "Stone", x, xe - 4.0, cy - tile * 0.31, cy + tile * 0.31, -4.0, 5.0 + rng.uniform(-1.0, 1.0))
            x = xe
    return mesh.write(name, ["Stone"])


FLOWER_SLOTS = ["Box", "Soil", "Leaf", "Red", "Yellow", "White", "Purple", "Center"]


def _flower(mesh, cx, cy, z0, height, petal, rng):
    """큰 꽃 한 송이 — 줄기·바닥 잎 넷·꽃잎 다섯(위로 젖힌 마름모)·가운데 심(steam_22 화분 꽃)."""
    box(mesh, "Leaf", cx - 3.0, cx + 3.0, cy - 3.0, cy + 3.0, z0, z0 + height)
    turn = rng.uniform(0.0, math.tau)
    for i in range(4):   # 잎 — 바닥에서 비스듬히 펼친 넓은 마름모
        t = turn + math.tau * (i + 0.5) / 4
        c, s = math.cos(t), math.sin(t)
        tip = (cx + c * 42.0, cy + s * 42.0, z0 + 10.0)
        mid = z0 + height * 0.35
        mesh.group += 1
        a, b = mesh.v(cx, cy, mid), mesh.v(*tip)
        l, r = mesh.v(cx + c * 20.0 - s * 13.0, cy + s * 20.0 + c * 13.0, mid - 4.0), mesh.v(cx + c * 20.0 + s * 13.0, cy + s * 20.0 - c * 13.0, mid - 4.0)
        mesh.tri("Leaf", a, l, b, (0, 0, 1))
        mesh.tri("Leaf", a, b, r, (0, 0, 1))
    top = z0 + height
    for i in range(5):   # 꽃잎 — 심에서 바깥으로, 끝이 조금 들린다
        t = turn + math.tau * i / 5
        c, s = math.cos(t), math.sin(t)
        mesh.group += 1
        a, b = mesh.v(cx, cy, top), mesh.v(cx + c * 26.0, cy + s * 26.0, top + 8.0)
        l, r = mesh.v(cx + c * 13.0 - s * 10.0, cy + s * 13.0 + c * 10.0, top + 2.0), mesh.v(cx + c * 13.0 + s * 10.0, cy + s * 13.0 - c * 10.0, top + 2.0)
        mesh.tri(petal, a, l, b, (0, 0, 1))
        mesh.tri(petal, a, b, r, (0, 0, 1))
    box(mesh, "Center", cx - 6.0, cx + 6.0, cy - 6.0, cy + 6.0, top - 2.0, top + 6.0)


def _bed(mesh, hx, hy, h, wall):
    """돌 틀 화단 — 네 벽(Box) 안에 흙(Soil)."""
    box(mesh, "Box", -hx, hx, -hy, -hy + wall, 0.0, h)
    box(mesh, "Box", -hx, hx, hy - wall, hy, 0.0, h)
    box(mesh, "Box", -hx, -hx + wall, -hy + wall, hy - wall, 0.0, h)
    box(mesh, "Box", hx - wall, hx, -hy + wall, hy - wall, 0.0, h)
    quad(mesh, "Soil", [(-hx + wall, -hy + wall, h - 6.0), (hx - wall, -hy + wall, h - 6.0),
                        (hx - wall, hy - wall, h - 6.0), (-hx + wall, hy - wall, h - 6.0)], (0, 0, 1))


def planter(name, rng):
    """문 앞 돌길 옆 화단(steam_22) — 220×140 돌 틀에 빨강·하양·노랑·보라 큰 꽃 네 송이. +X 로 길다."""
    mesh = Mesh()
    _bed(mesh, 110.0, 70.0, 45.0, 14.0)
    for (fx, fy), petal in zip(((-55, -22), (-5, 25), (40, -25), (75, 20)), ("Red", "White", "Yellow", "Purple")):
        _flower(mesh, fx + rng.uniform(-6, 6), fy + rng.uniform(-6, 6), 39.0, rng.uniform(38.0, 52.0), petal, rng)
    return mesh.write(name, FLOWER_SLOTS)


def pot(name, rng):
    """문 옆 화분(steam_22 문 양옆) — 네모 화분 하나에 빨강·보라 꽃 두 송이."""
    mesh = Mesh()
    _bed(mesh, 34.0, 34.0, 40.0, 8.0)
    _flower(mesh, -8.0, -6.0, 34.0, 30.0, "Red", rng)
    _flower(mesh, 10.0, 9.0, 34.0, 22.0, "Purple", rng)
    return mesh.write(name, FLOWER_SLOTS)


def wild_flower(name, petal, rng):
    """들꽃 포기(steam_18) — 바닥 잎 위에 같은 색 꽃 세 송이가 삼각으로 모인다. 사냥터 풀밭에 무리로 흩는다."""
    mesh = Mesh()
    turn = rng.uniform(0.0, math.tau)
    for i in range(3):
        t = turn + math.tau * i / 3
        _flower(mesh, math.cos(t) * 30.0, math.sin(t) * 30.0, 0.0, rng.uniform(26.0, 38.0), petal, rng)
    return mesh.write(name, FLOWER_SLOTS)


def _block(mesh, slot, x0, x1, y0, y1, h, bevel):
    """모서리를 깎은 네모 덩어리 — 옆면은 곧게, 윗 테두리는 비스듬히(각뿔대)."""
    zb = h - bevel
    for (ax, ay), (bx, by), out in (((x0, y0), (x1, y0), (0, -1, 0)), ((x1, y0), (x1, y1), (1, 0, 0)),
                                    ((x1, y1), (x0, y1), (0, 1, 0)), ((x0, y1), (x0, y0), (-1, 0, 0))):
        quad(mesh, slot, [(ax, ay, 0.0), (bx, by, 0.0), (bx, by, zb), (ax, ay, zb)], out)
        ix = lambda x: x + bevel if x == x0 else x - bevel
        iy = lambda y: y + bevel if y == y0 else y - bevel
        quad(mesh, slot, [(ax, ay, zb), (bx, by, zb), (ix(bx), iy(by), h), (ix(ax), iy(ay), h)], (out[0], out[1], 1))
    quad(mesh, slot, [(x0 + bevel, y0 + bevel, h), (x1 - bevel, y0 + bevel, h), (x1 - bevel, y1 - bevel, h),
                      (x0 + bevel, y1 - bevel, h)], (0, 0, 1))


def _leaf_fringe(mesh, slot, x0, x1, y0, y1, z, rng, step=14.0):
    """네모 둘레에 아래로 늘어진 잎 술 — 반 뼘 밖으로 내민 작은 삼각형(steam_22 산울타리 윗 테두리 잎 끝)."""
    for (ax, ay), (bx, by), (ox, oy) in (((x0, y0), (x1, y0), (0, -1)), ((x1, y0), (x1, y1), (1, 0)),
                                         ((x1, y1), (x0, y1), (0, 1)), ((x0, y1), (x0, y0), (-1, 0))):
        n = max(1, int(math.hypot(bx - ax, by - ay) / step))
        for i in range(n):
            t0, t1 = i / n, (i + 1) / n
            tm = (t0 + t1) * 0.5 + rng.uniform(-0.15, 0.15) / n
            push, drop = rng.uniform(4.0, 7.0), rng.uniform(16.0, 26.0)   # 9~16cm 술은 한 화면 거리에서 안 보였다
            p = lambda t, dz, out: mesh.v(ax + (bx - ax) * t + ox * out, ay + (by - ay) * t + oy * out, z + dz)
            mesh.group += 1
            mesh.tri(slot, p(t0, 3.0, 0.5), p(t1, 3.0, 0.5), p(tm, -drop, push), (ox, oy, 0.3))


def hedge(name, rng):
    """깎은 산울타리(steam_22 왼쪽) — 네모 덤불 둘을 나란히, 높이가 조금씩 다르다. 200×70, +Y 로 길다.
    매끈한 각뿔대만으로는 초록 벽돌로 떴다 — 윗 테두리를 따라 잎 술을 늘어뜨려 잎 덩이로 보이게 한다."""
    mesh = Mesh()
    fringe_rng = random.Random(SEED + 1)   # 따로 굴려 뒤에 만드는 꽃 메시의 난수 순서를 안 밀어낸다
    for y0 in (-100.0, 0.0):
        h, bevel = rng.uniform(88.0, 100.0), 20.0   # 둥근 베개 덩이(steam_22) — 14 는 각진 벽돌로 떴다
        _block(mesh, "Hedge", -35.0, 35.0, y0 + 2.0, y0 + 98.0, h, bevel)
        _leaf_fringe(mesh, "Hedge", -35.0, 35.0, y0 + 2.0, y0 + 98.0, h - bevel, fringe_rng)
    return mesh.write(name, ["Hedge"])


def picket(name, length=589.0, height=110.0, spans=5):
    """밧줄 울타리(DESIGN §7.1 「목장 나무판과 밧줄」) — 굵은 나무 기둥 여섯 개 + 기둥 사이 처지는 밧줄 두 줄.
    기둥 머리는 넓은 모자, 기둥마다 밧줄 감김 매듭. +X 로 길다, 가운데 기준. 슬롯: Wood(기둥) · Rope(밧줄)."""
    mesh = Mesh()
    cap = 10.0
    half = length * 0.5 - cap
    gap = (2.0 * half) / spans
    centers = [-half + i * gap for i in range(spans + 1)]
    for cx in centers:
        box(mesh, "Wood", cx - 7.0, cx + 7.0, -7.0, 7.0, 0.0, height - 10.0)
        box(mesh, "Wood", cx - cap, cx + cap, -cap, cap, height - 10.0, height)
    for z_rope in (78.0, 42.0):
        for cx in centers:
            box(mesh, "Rope", cx - 9.0, cx + 9.0, -9.0, 9.0, z_rope - 5.0, z_rope + 5.0)   # 기둥에 감은 매듭
        pieces = 6
        for i in range(spans):
            x0 = centers[i] + 9.0
            step = (gap - 18.0) / pieces
            for k in range(pieces):
                t = (k + 0.5) / pieces
                sag = 11.0 * (1.0 - (2.0 * t - 1.0) ** 2)   # 가운데로 갈수록 처진다
                zc = z_rope - 2.0 - (11.0 - sag)
                box(mesh, "Rope", x0 + k * step, x0 + (k + 1) * step, -3.0, 3.0, zc - 3.0, zc + 3.0)
    return mesh.write(name, ["Wood", "Rope"])


def _selfcheck():
    """박공 끝·용마루 높이 검사 — 처마는 벽 윗선 아래, 용마루는 박공 꼭대기 위."""
    assert _roof_z(WALL_Y + EAVE_OUT) - ROOF_THICK < WALL_Z, _roof_z(WALL_Y + EAVE_OUT)
    assert _roof_z(0.0) - ROOF_THICK == WALL_Z + GABLE_Z
    assert 2.0 * DOOR_HW > 2.0 * 42.0 + 20.0, "문이 캡슐보다 좁다"
    assert 2.0 * DOOR_HW < WALL_X * 2.0 - WALL_T * 2.0, "열린 문이 뒷벽에 닿는다"
    assert GABLE_WIN[3] + 12.0 < WALL_Z + GABLE_Z * (1.0 - (GABLE_WIN[1] + 12.0) / WALL_Y), "박공 창이 박공 밖으로 나간다"
    assert UPPER_WIN_HW + 12.0 < WALL_Y - WALL_T, "위층 창이 옆벽에 걸친다"
    assert SIDE_WIN[0] + SIDE_WIN[1] + 12.0 < WALL_Y - WALL_T and SIDE_WIN[0] - SIDE_WIN[1] - 12.0 > DOOR_HW + 14.0, "앞 창이 옆벽·문틀에 걸친다"


if __name__ == "__main__":
    _selfcheck()
    os.makedirs(OUT_DIR, exist_ok=True)
    print("저장: Cottage_01 삼각형", cottage_shell("Cottage_01"))
    print("저장: CottageRoof_01 삼각형", cottage_cover("CottageRoof_01"))
    _use_dims(TENT_HOUSE_DIMS)
    _selfcheck()
    print("저장: TentHouse_01 삼각형", cottage_shell("TentHouse_01"))
    print("저장: TentHouseRoof_01 삼각형", cottage_cover("TentHouseRoof_01"))
    print("저장: TentHouseFront_01 삼각형", cottage_cover("TentHouseFront_01", roof=False))
    print("저장: TentHouseUpperRoof_01 삼각형", cottage_cover("TentHouseUpperRoof_01", door=False))
    print("저장: TentHouseUpper_01 삼각형", cottage_upper("TentHouseUpper_01"))
    print("저장: TentHouseFloor2_01 삼각형", floor_tile("TentHouseFloor2_01", (WALL_Y - WALL_T) * 2.0 / 3.0))
    _use_dims(COTTAGE_DIMS)
    rng = random.Random(SEED)
    print("저장: Path_01 삼각형", path("Path_01", rng))
    print("저장: Planter_01 삼각형", planter("Planter_01", rng))
    print("저장: Pot_01 삼각형", pot("Pot_01", rng))
    print("저장: Hedge_01 삼각형", hedge("Hedge_01", rng))
    print("저장: Fence_02 삼각형", picket("Fence_02"))
    print("저장: WildFlower_01 삼각형", wild_flower("WildFlower_01", "White", rng))
    print("저장: WildFlower_02 삼각형", wild_flower("WildFlower_02", "Yellow", rng))
