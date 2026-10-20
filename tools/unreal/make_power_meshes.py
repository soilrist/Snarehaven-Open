"""INBOX 보정(감독 10-06 02:45) 전력 가구 진짜 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_power_meshes.py   → Saved/Generated/SolarPanel_01.obj · Battery_01.obj · LampElectric_01.obj (+ .mtl)

레퍼런스 `steam_22.jpg`(집·풍차·태양광): 땅에 놓인 태양광 패널 = **남색 칸 8×10 을 흰 격자가 가르고, 밝은 테두리**를 두른
네모 판이 받침 위에 비스듬히 기울어 있다(위에서 내려다보는 카메라에 판 앞면이 통째로 보인다). 지붕 위 작은 패널도 같은 무늬.
배터리는 원본 화면에 놓인 모습이 없다(원본 자료 VN138 "전자 제품의 전원" 아이템만) → 🟡 장르 상식의 각진 축전지 통:
어두운 몸통 + 검은 뚜껑 + 빨강(+)·납빛(−) 단자 + 노란 번개 — 위에서 보면 단자·번개가 「배터리」 로 읽히게.
전등(ROADMAP M40-06)은 집 실내·굴 기름 등불을 빌려 쓰던 것을 **전구 달린 세움 전등**으로 — 원본 자료에 놓인 전등 화면이 없어
🟡 장르 상식: 둥근 받침 + 가는 기둥 + 크림 갓(위가 좁은 원뿔대, 위 구멍으로 노란 전구 빛) + 갓 밑으로 내민 노란 전구 + 전선 한 가닥.
면마다 정점을 따로 둬 한 면 한 색(world_environment.md "한 면 한 색"), 그린 치수 cm.
놓일 때 `ASnarePlacedStructureActor` 가 수평 긴 변을 표 폭(DESIGN 8.11 크기 표)에 맞추고 밑면·가운데를 맞추므로 원점은 아무 데나 둬도 된다
— 그래도 밑면 가운데에 둔다. 긴 변 = X. 슬롯 색은 `import_power_meshes.py` 가 입힌다.
"""
import math

from make_tree_meshes import Mesh
from make_weapon_meshes import _box, _hexa

# 태양광 패널 — 판 크기(그린 cm)와 기울기. steam_22 땅 패널: 가로 칸 10 · 세로 칸 8 정도, 앞이 낮고 뒤가 높게 ≈ 25°
PANEL_X, PANEL_Y, PANEL_T = 90.0, 66.0, 3.0
PANEL_TILT_DEG = 25.0
PANEL_LOW_Z = 22.0          # 판 앞(낮은) 모서리 밑 높이 — 받침 다리가 보이게
FRAME = 3.0                 # 테두리 폭
CELLS_X, CELLS_Y = 8, 6     # 칸 수 — 위에서 볼 때 칸이 3px 아래로 뭉개지지 않게 원본보다 성기게(🟡)
GAP = 1.4                   # 칸 사이 흰 격자 폭
CELL_RAISE = 0.6            # 칸이 흰 바탕 위로 솟은 높이(격자가 그림자 없이도 갈리게)


def _panel_point(u, v, w):
    """판 좌표(u = X 가로, v = 앞→뒤 판 위 거리, w = 판 두께 방향) → 월드. 앞 모서리가 -Y·낮은 쪽."""
    tilt = math.radians(PANEL_TILT_DEG)
    y0 = -PANEL_Y * math.cos(tilt) * 0.5
    return (u, y0 + v * math.cos(tilt) - w * math.sin(tilt), PANEL_LOW_Z + v * math.sin(tilt) + w * math.cos(tilt))


def _panel_box(mesh, slot, u0, u1, v0, v1, w0, w1):
    corners = [(u0, v0, w0), (u1, v0, w0), (u1, v1, w0), (u0, v1, w0), (u0, v0, w1), (u1, v0, w1), (u1, v1, w1), (u0, v1, w1)]
    _hexa(mesh, slot, [_panel_point(*c) for c in corners])


def solar_panel():
    mesh = Mesh()
    half = PANEL_X * 0.5
    # 판 바탕(흰 격자색) — 테두리 안쪽 전체
    _panel_box(mesh, "Backing", -half + FRAME, half - FRAME, FRAME, PANEL_Y - FRAME, 0.0, PANEL_T)
    # 테두리 네 변 — 바탕보다 0.8 높게 둘러 판 가장자리가 밝은 선으로 서게
    rim = PANEL_T + 0.8
    _panel_box(mesh, "Frame", -half, half, 0.0, FRAME, 0.0, rim)
    _panel_box(mesh, "Frame", -half, half, PANEL_Y - FRAME, PANEL_Y, 0.0, rim)
    _panel_box(mesh, "Frame", -half, -half + FRAME, FRAME, PANEL_Y - FRAME, 0.0, rim)
    _panel_box(mesh, "Frame", half - FRAME, half, FRAME, PANEL_Y - FRAME, 0.0, rim)
    # 남색 칸 — 칸 사이 GAP 만큼 흰 바탕이 드러나 격자가 된다
    inner_x, inner_y = PANEL_X - 2 * FRAME - GAP, PANEL_Y - 2 * FRAME - GAP
    cell_x, cell_y = inner_x / CELLS_X, inner_y / CELLS_Y
    for i in range(CELLS_X):
        for j in range(CELLS_Y):
            u0 = -half + FRAME + GAP + i * cell_x
            v0 = FRAME + GAP + j * cell_y
            _panel_box(mesh, "Cell", u0, u0 + cell_x - GAP, v0, v0 + cell_y - GAP, PANEL_T, PANEL_T + CELL_RAISE)
    # 받침 — 판 밑 가로대 둘(앞·뒤)과 다리 넷. 다리는 판 밑면까지
    tilt = math.radians(PANEL_TILT_DEG)
    for v in (8.0, PANEL_Y - 8.0):
        _, y, z = _panel_point(0.0, v, 0.0)
        for x in (-half + 10.0, half - 13.0):
            _box(mesh, "Leg", x, x + 3.0, y - 1.5, y + 1.5, 0.0, z)
        _box(mesh, "Leg", -half + 8.0, half - 8.0, y - 1.5, y + 1.5, z - 3.0 * math.cos(tilt), z)
    # 바닥 발판 — 다리 넷을 잇는 두 줄(땅에 박힌 받침으로 읽히게)
    for x in (-half + 10.0, half - 13.0):
        _, y_front, _ = _panel_point(0.0, 8.0, 0.0)
        _, y_back, _ = _panel_point(0.0, PANEL_Y - 8.0, 0.0)
        _box(mesh, "Leg", x - 1.0, x + 4.0, y_front - 4.0, y_back + 4.0, 0.0, 2.0)
    return mesh


def battery():
    """축전지 통 — 50 × 32 × 34cm(그린 치수, 표 폭 50 이면 그대로). 뚜껑 위 단자 둘 + 노란 번개(위·앞). (−) 단자는 납빛 회색 — 검정은 검은 뚜껑에 묻혔다(루프#589 첫 캡처)."""
    mesh = Mesh()
    bx, by, bz = 25.0, 16.0, 28.0
    _box(mesh, "Case", -bx, bx, -by, by, 0.0, bz)
    # 뚜껑 — 몸통보다 1cm 넓고 6cm 두께, 윗모서리 깎음
    _box(mesh, "Lid", -bx - 1.0, bx + 1.0, -by - 1.0, by + 1.0, bz, bz + 6.0, top_inset=1.2)
    top = bz + 6.0
    # 단자 — 팔각 기둥 대신 각진 상자 둘(위에서 빨강·검정 점으로 읽히게 굵게)
    _box(mesh, "Plus", -bx + 5.0, -bx + 12.0, -3.5, 3.5, top, top + 5.0, top_inset=0.8)
    _box(mesh, "Minus", bx - 12.0, bx - 5.0, -3.5, 3.5, top, top + 5.0, top_inset=0.8)
    # 번개 — 뚜껑 위 가운데: 기울어진 평행사변형 둘을 엇갈려 지그재그(위에서 본 노란 ⚡). 그린 번개를 1.5배로
    # (루프#589 첫 캡처에서 1배는 4px 노란 점으로 뭉개졌다)
    z0, z1 = top, top + 0.6
    k = 1.5
    for quad in ([(-2.0, 8.0), (5.0, 8.0), (1.5, -1.0), (-5.5, -1.0)], [(-1.5, 1.5), (5.5, 1.5), (2.0, -8.0), (-1.0, -8.0)]):
        _hexa(mesh, "Bolt", [(x * k, y * k, z0) for x, y in quad] + [(x * k, y * k, z1) for x, y in quad])
    # 앞면(-Y) 노란 띠 — 옆에서 볼 때도 배터리 색이 읽히게
    _box(mesh, "Bolt", -bx + 3.0, bx - 3.0, -by - 0.6, -by, 9.0, 15.0)
    return mesh


def _frustum(mesh, slot, z0, r0, z1, r1, sides, cap_slot=None, bottom_slot=None):
    """Z 축 원뿔대 — 옆면 한 묶음(둥글게), 위·아래 마개는 슬롯을 주면 따로(None 이면 안 막음)."""
    lower, upper = mesh.ring(sides, r0, z0), mesh.ring(sides, r1, z1)
    mesh.group += 1
    mesh.band(slot, lower, upper)
    for rim, z, cap, out_z in ((upper, z1, cap_slot, 1.0), (lower, z0, bottom_slot, -1.0)):
        if cap:
            mesh.group += 1
            mesh.fan(cap, rim, mesh.v(0.0, 0.0, z), out_z)


# 전등 — 그린 치수 cm, 수평 가장 넓은 곳 = 갓 밑 지름 19 = 표 폭 19(DESIGN 8.11 크기 표) 라 놓일 때 배율 1.
# 키 124 ≈ 옛 등불(폭 19 에 높이 약 132)과 같은 사람 키(176)의 2/3~3/4.
LAMP_SHADE_R0, LAMP_SHADE_R1 = 9.5, 5.0    # 갓 밑·위 반지름
LAMP_SHADE_Z0, LAMP_SHADE_Z1 = 102.0, 124.0
LAMP_BASE_R, LAMP_BASE_T = 8.5, 3.0


def lamp_electric():
    mesh = Mesh()
    # 받침 — 납작한 팔각 판, 윗면이 살짝 좁아 둥근 쇠 받침으로
    _frustum(mesh, "LampBase", 0.0, LAMP_BASE_R, LAMP_BASE_T, LAMP_BASE_R - 1.0, 12, cap_slot="LampBase", bottom_slot="LampBase")
    # 기둥 — 받침에서 갓 안까지
    _frustum(mesh, "Pole", LAMP_BASE_T, 1.2, LAMP_SHADE_Z0 + 6.0, 1.0, 8, cap_slot="Pole")
    # 갓 — 크림 원뿔대(밑 넓고 위 좁게), 옆면만. 위 구멍 = 노란 전구 빛 판(위 카메라에서 불 켜는 물건으로 읽히게)
    _frustum(mesh, "Shade", LAMP_SHADE_Z0, LAMP_SHADE_R0, LAMP_SHADE_Z1, LAMP_SHADE_R1, 12)
    _frustum(mesh, "Shade", LAMP_SHADE_Z0, LAMP_SHADE_R0 - 0.6, LAMP_SHADE_Z1, LAMP_SHADE_R1 - 0.6, 12)   # 안쪽 벽(밑에서 봐도 비지 않게)
    _frustum(mesh, "Bulb", LAMP_SHADE_Z1 - 1.0, LAMP_SHADE_R1 - 0.6, LAMP_SHADE_Z1 - 0.4, LAMP_SHADE_R1 - 0.6, 12, cap_slot="Bulb")
    # 갓 아래 테 — 짙은 띠(갓 밑 가장자리가 하늘·풀 위에서 갈리게)
    _frustum(mesh, "Pole", LAMP_SHADE_Z0, LAMP_SHADE_R0, LAMP_SHADE_Z0 + 1.5, LAMP_SHADE_R0 - 0.1, 12)
    # 전구 — 갓 밑으로 반쯤 내민 노란 팔면 알(꼭지 + 몸)
    _frustum(mesh, "Pole", LAMP_SHADE_Z0 + 2.0, 1.4, LAMP_SHADE_Z0 + 6.0, 1.4, 8)                       # 소켓
    _frustum(mesh, "Bulb", LAMP_SHADE_Z0 - 4.5, 2.0, LAMP_SHADE_Z0 - 1.5, 3.6, 10, bottom_slot="Bulb")   # 알 아래 반
    _frustum(mesh, "Bulb", LAMP_SHADE_Z0 - 1.5, 3.6, LAMP_SHADE_Z0 + 2.0, 1.6, 10)                       # 알 위 반
    # 전선 한 가닥 — 기둥 밑에서 받침 위를 지나 가장자리로 내려와 땅에 짧게(표 폭 19 를 넘지 않게 받침 반지름 안)
    _box(mesh, "Wire", 1.0, LAMP_BASE_R - 0.8, -0.5, 0.5, LAMP_BASE_T - 0.6, LAMP_BASE_T + 0.6)
    _box(mesh, "Wire", LAMP_BASE_R - 0.8, LAMP_BASE_R + 0.2, -0.5, 0.5, 0.0, LAMP_BASE_T + 0.6)
    _box(mesh, "Wire", LAMP_BASE_R - 1.5, LAMP_BASE_R + 0.2, -6.5, -0.5, 0.0, 1.0)
    return mesh


if __name__ == "__main__":
    for name, build, slots in (("SolarPanel_01", solar_panel, ["Backing", "Frame", "Cell", "Leg"]),
                               ("Battery_01", battery, ["Case", "Lid", "Plus", "Minus", "Bolt"]),
                               ("LampElectric_01", lamp_electric, ["LampBase", "Pole", "Shade", "Bulb", "Wire"])):
        mesh = build()
        triangles = mesh.write(name, slots)
        xs, ys, zs = zip(*mesh.verts)
        print("{}.obj 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f}".format(
            name, triangles, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))
