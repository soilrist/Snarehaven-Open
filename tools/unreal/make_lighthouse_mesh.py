"""스토리 ⑤ 등대 점등 조각 2 — 등대 탑 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_lighthouse_mesh.py   → Saved/Generated/Lighthouse_01.obj (+ .mtl)

시트 `docs/reference/story_lighthouse.md` 의 구성 그대로: 돌 기단 → 크림 몸통(위로 갈수록 가늘다) + 짙은 청록 줄 셋 → 밧줄 띠 →
나무 발코니와 난간 → 창 여덟 칸 랜턴 방(가운데 전구) → 녹슨 붉은 지붕 → 꼭대기 올가미 고리(우리 덫·올가미 모티프).
창(Window)과 전구(Bulb)는 따로 슬롯이라 조각 3 의 액터가 꺼짐/켜짐 색을 갈아 끼운다(5장 전: 어두운 창, 불 켠 밤: 노란 창).
면마다 정점을 따로 둬 한 면 한 색(world_environment.md "한 면 한 색"). 그린 치수 cm = 월드 크기(반입 배율 1), 원점 = 밑면 가운데, 키 = +Z.
슬롯 색 `SLOT_COLORS` 는 조각 3 반입 스크립트와 `tools/blender/render_lighthouse_preview.py` 가 같이 읽는다.
"""
import math

from make_power_meshes import _frustum
from make_tree_meshes import Mesh
from make_weapon_meshes import _box

SLOTS = ["Stone", "Body", "Band", "Rope", "Wood", "Window", "Bulb", "Roof", "Ring"]
# 슬롯 색(리니어) — 돌 회갈색 · 크림 흰 · 짙은 청록 · 밧줄 황토 · 짙은 나무 · 꺼진 창 청회색 · 노란 전구 · 녹슨 붉은 지붕 · 놋쇠 고리
SLOT_COLORS = {"Stone": (0.30, 0.27, 0.23), "Body": (0.80, 0.74, 0.62), "Band": (0.025, 0.16, 0.18), "Rope": (0.50, 0.34, 0.15),
               "Wood": (0.13, 0.07, 0.035), "Window": (0.02, 0.035, 0.055), "Bulb": (1.0, 0.70, 0.12),
               "Roof": (0.32, 0.055, 0.035), "Ring": (0.62, 0.46, 0.14)}

SIDES = 12
BASE_Z1 = 120.0
BASE_R0, BASE_R1 = 310.0, 265.0
BODY_Z1 = 1000.0
BODY_R0, BODY_R1 = 235.0, 150.0
BANDS = [(240.0, 400.0), (520.0, 680.0), (800.0, 940.0)]   # 몸통 청록 줄 (z0, z1)
GALLERY_Z0, GALLERY_Z1, GALLERY_R = 1000.0, 1030.0, 215.0
RAIL_H = 75.0
ROOM_R = 138.0
ROOM_Z0, ROOM_Z1 = 1030.0, 1180.0
ROOF_Z1, ROOF_R1 = 1265.0, 14.0
LANTERN_Z = 1105.0   # 불빛이 나올 높이 — 조각 3 의 스포트·점광이 이 높이에 붙는다


def _body_radius(z):
    return BODY_R0 + (BODY_R1 - BODY_R0) * (z - BASE_Z1) / (BODY_Z1 - BASE_Z1)


def lighthouse():
    mesh = Mesh()
    # 기단 — 아래가 넓은 돌 원뿔대, 밑·윗면 막음. 땅에 박힌 돌 둘레띠
    _frustum(mesh, "Stone", 0.0, BASE_R0, BASE_Z1, BASE_R1, SIDES, cap_slot="Stone", bottom_slot="Stone")
    _frustum(mesh, "Stone", 0.0, BASE_R0 + 14.0, 30.0, BASE_R0 + 4.0, SIDES, cap_slot="Stone")
    # 몸통 — 크림 원뿔대, 위로 가늘다
    _frustum(mesh, "Body", BASE_Z1, BODY_R0, BODY_Z1, BODY_R1, SIDES)
    # 청록 줄 셋 — 몸통보다 3cm 두껍게 둘러 줄무늬가 갈리게(우리 것: 흰 빨강 줄무늬가 아니라 크림 + 청록)
    for z0, z1 in BANDS:
        _frustum(mesh, "Band", z0, _body_radius(z0) + 3.0, z1, _body_radius(z1) + 3.0, SIDES)
    # 밧줄 띠 — 기단 윗둘레·몸통 한가운데·발코니 밑 (목장 나무판과 밧줄 주제)
    for z in (BASE_Z1, 460.0, BODY_Z1 - 40.0):
        r = _body_radius(z) if z > BASE_Z1 else BASE_R1
        _frustum(mesh, "Rope", z - 9.0, r + 7.0, z + 9.0, r + 7.0, SIDES)
    # 문 — 기단 앞(-Y)에 박힌 짙은 나무 판 + 위 문턱 돌
    _box(mesh, "Wood", -60.0, 60.0, -BASE_R0 * 0.9, -BASE_R0 * 0.9 + 60.0, 0.0, 100.0, top_inset=0.0)
    # 발코니 — 나무 원판 + 난간 기둥 열둘 + 밧줄 난간(윗줄)
    _frustum(mesh, "Wood", GALLERY_Z0, GALLERY_R, GALLERY_Z1, GALLERY_R, SIDES, cap_slot="Wood", bottom_slot="Wood")
    for i in range(SIDES):
        t = math.tau * (i + 0.5) / SIDES
        cx, cy = (GALLERY_R - 10.0) * math.cos(t), (GALLERY_R - 10.0) * math.sin(t)
        _box(mesh, "Wood", cx - 5.0, cx + 5.0, cy - 5.0, cy + 5.0, GALLERY_Z1, GALLERY_Z1 + RAIL_H)
    _frustum(mesh, "Rope", GALLERY_Z1 + RAIL_H - 8.0, GALLERY_R - 4.0, GALLERY_Z1 + RAIL_H + 4.0, GALLERY_R - 4.0, SIDES)
    # 랜턴 방 — 아래 테(나무) · 창 띠(8각, 꺼짐 색) · 위 테(나무) · 모서리 기둥 여덟
    _frustum(mesh, "Wood", ROOM_Z0, ROOM_R + 6.0, ROOM_Z0 + 16.0, ROOM_R + 6.0, 8, cap_slot="Wood")
    _frustum(mesh, "Window", ROOM_Z0 + 16.0, ROOM_R, ROOM_Z1 - 14.0, ROOM_R, 8)
    _frustum(mesh, "Wood", ROOM_Z1 - 14.0, ROOM_R + 6.0, ROOM_Z1, ROOM_R + 6.0, 8, cap_slot="Wood")
    for i in range(8):
        t = math.tau * i / 8
        cx, cy = ROOM_R * math.cos(t), ROOM_R * math.sin(t)
        _box(mesh, "Wood", cx - 7.0, cx + 7.0, cy - 7.0, cy + 7.0, ROOM_Z0 + 16.0, ROOM_Z1 - 14.0)
    # 전구 — 방 한가운데 노란 기둥(밤에 창 안쪽이 밝게 보이도록 창 가까이까지)
    _frustum(mesh, "Bulb", ROOM_Z0 + 40.0, 62.0, ROOM_Z1 - 40.0, 62.0, 8, cap_slot="Bulb", bottom_slot="Bulb")
    # 지붕 — 녹슨 붉은 뾰족 원뿔대 + 처마 둘레
    _frustum(mesh, "Roof", ROOM_Z1, ROOM_R + 18.0, ROOF_Z1, ROOF_R1, 8, cap_slot="Roof", bottom_slot="Roof")
    # 꼭대기 올가미 고리 — 세운 놋쇠 고리(상자 열 개로 둥글게)
    ring_r, ring_z = 34.0, ROOF_Z1 + 34.0
    for i in range(10):
        t = math.tau * i / 10
        cx, cz = ring_r * math.cos(t), ring_z + ring_r * math.sin(t)
        _box(mesh, "Ring", cx - 6.0, cx + 6.0, -4.0, 4.0, cz - 6.0, cz + 6.0)
    return mesh


if __name__ == "__main__":
    mesh = lighthouse()
    triangles = mesh.write("Lighthouse_01", SLOTS)
    xs, ys, zs = zip(*mesh.verts)
    print("Lighthouse_01.obj 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} (랜턴 높이 {:.0f})".format(
        triangles, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), LANTERN_Z))
