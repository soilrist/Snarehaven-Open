"""M18-04 목장 바닥판·연못 원판 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_ranch_ground_meshes.py   → Saved/Generated/RanchFloor.obj · PondDisc.obj (+ .mtl)

`L_Ranch` 의 마지막 엔진 기본 도형 둘(바닥 `Plane` ×50, 연못 `Cylinder` 납작 ×2)을 대신한다(M15-20 허용 목록).
모양은 옛 도형과 같다 — 바닥은 ±2500cm 평평한 사각 판(z = 0, `ASeaActor` 목장 섬 반폭 2500 과 같다),
연못은 가장 먼 둘레가 반지름 50cm 인 납작 판(z = 0, 배치 스크립트가 배율 7 로 가장 먼 곳 지름 700).
M19-02: 둘레는 원이 아니라 레퍼런스 `steam_05`·`steam_10`·`steam_16` 처럼 굽이진 물가 — 각도마다 반지름을 사인 넷으로 흔든다(시드 고정).
연못 셰이더(`setup_sea.py` POND_HLSL)가 물가까지 거리를 원으로 못 재니, 정점마다 「물가까지 거리 ÷ x 반폭」을 UV u 에 굽는다
(셰이더가 u × `ObjectBounds` x 반폭 = 월드 cm 로 되돌린다). 슬롯 하나 — 바닥 `Ground`, 물 `Water`(머티리얼은 배치 스크립트가 끼운다).
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_tree_meshes import OUT_DIR, Mesh  # noqa: E402

FLOOR_HALF_CM = 2500.0   # 옛 Plane(100cm) × 50
FLOOR_CELL_CM = 500.0    # 칸 — 평평해서 잘게 쪼갤 까닭이 없다(옛 Plane 은 한 장)
DISC_RADIUS_CM = 50.0    # 가장 먼 둘레 — 옛 Cylinder 지름 100
DISC_SIDES = 96          # 지름 700 연못에서 변 하나 ≈ 23cm — 굽이(7번 사인 한 물결 ≈ 310cm)가 각져 보이지 않는다
# 둘레 굽이 (사인 차수, 진폭 = 평균 반지름 비) — 반지름 최대/최소 ≈ 1.37 (ROADMAP M19-02 합격선 ≥ 1.25).
# 진폭을 1.5배로 키우면 비가 1.5 를 넘어 연못이 평균 지름 560 아래로 줄어든다(낚시하러 설 물가가 좁아짐)
POND_WOBBLE = [(2, 0.08), (3, 0.06), (5, 0.035), (7, 0.02)]
POND_SEED = 19034        # 물속 바위(`setup_sea.py` POND_ROCKS)가 물가에서 70cm 넘게 떨어지는 첫 시드들 중 하나
# 안쪽 고리(가장자리 = 1) — 물가 흰 실선(8cm)·점선(30cm)이 고리 사이 직선 보간에 뭉개지지 않게 가장자리에 촘촘히
POND_RINGS = [1.0, 0.97, 0.92, 0.84, 0.72, 0.55, 0.35, 0.15]
POND_SCALE = 7.0         # `setup_sea.py` LEVELS 연못 지름 700 ÷ 100 — 자체 검사가 월드 cm 로 바위 거리를 잴 때만 쓴다
POND_ROCK_XY = [(190.0, 90.0), (-150.0, -170.0), (-60.0, 230.0)]   # `setup_sea.py` POND_ROCKS (월드 cm, 연못 중심 기준)
POND_ROCK_CLEAR_CM = 60.0   # 가장 큰 바위(Boulder_02 ×0.9) 반폭 ≈ 45 + 물가 실선·점선 자리
MIN_WOBBLE_RATIO = 1.25
UP = (0.0, 0.0, 1.0)


def floor():
    mesh = Mesh()
    n = int(round(2 * FLOOR_HALF_CM / FLOOR_CELL_CM))
    grid = [[mesh.v(-FLOOR_HALF_CM + i * FLOOR_CELL_CM, -FLOOR_HALF_CM + j * FLOOR_CELL_CM, 0.0) for j in range(n + 1)]
            for i in range(n + 1)]
    for i in range(n):
        for j in range(n):
            a, b, c, d = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
            mesh.tri("Ground", a, b, c, UP)
            mesh.tri("Ground", a, c, d, UP)
    return mesh


def _pond_shape():
    """각도 → 반지름(cm, 메시 공간). 가장 먼 곳이 DISC_RADIUS_CM."""
    rng = random.Random(POND_SEED)
    phases = [rng.uniform(0.0, math.tau) for _ in POND_WOBBLE]

    def raw(t):
        return 1.0 + sum(a * math.sin(k * t + p) for (k, a), p in zip(POND_WOBBLE, phases))
    peak = max(raw(math.tau * i / 3600) for i in range(3600))
    return lambda t: raw(t) * DISC_RADIUS_CM / peak


def disc():
    mesh = Mesh()
    radius = _pond_shape()
    angles = [math.tau * i / DISC_SIDES for i in range(DISC_SIDES)]
    rim_x = [radius(t) * math.cos(t) for t in angles]
    x_half = (max(rim_x) - min(rim_x)) * 0.5   # `ObjectBounds` x 반폭(메시 공간) — 피벗이 상자 가운데가 아니어도 셰이더는 반폭만 쓴다
    rings = []
    for k in POND_RINGS:
        ring = []
        for t in angles:
            r = radius(t)
            ring.append(mesh.v(k * r * math.cos(t), k * r * math.sin(t), 0.0))
            mesh.uv[ring[-1]] = ((1.0 - k) * r / x_half, 0.0)
        rings.append(ring)
    for outer, inner in zip(rings, rings[1:]):
        for i in range(DISC_SIDES):
            j = (i + 1) % DISC_SIDES
            mesh.tri("Water", outer[i], outer[j], inner[j], UP)
            mesh.tri("Water", outer[i], inner[j], inner[i], UP)
    center = mesh.v(0.0, 0.0, 0.0)
    mesh.uv[center] = (sum(radius(t) for t in angles) / DISC_SIDES / x_half, 0.0)
    mesh.fan("Water", rings[-1], center, 1.0)
    mesh.pond = (radius, x_half)
    return mesh


def _pond_selfcheck(mesh):
    radius, x_half = mesh.pond
    rim = [radius(math.tau * i / 3600) for i in range(3600)]
    ratio = max(rim) / min(rim)
    assert ratio >= MIN_WOBBLE_RATIO, ratio
    assert abs(max(rim) - DISC_RADIUS_CM) < 0.01, max(rim)
    clear = [radius(math.atan2(y, x)) * POND_SCALE - math.hypot(x, y) for x, y in POND_ROCK_XY]
    assert min(clear) >= POND_ROCK_CLEAR_CM, clear
    assert all(u >= 0.0 for u, _ in mesh.uv.values())
    assert all(mesh._normal(a, b, c)[2] > 0 for _, a, b, c, _ in mesh.faces)
    print("[RanchGround] 연못 둘레 반지름 {:.1f}~{:.1f}cm(×{:.0f} 월드 {:.0f}~{:.0f}) 비 {:.3f} · 바위-물가 최소 {:.0f}cm · x 반폭 {:.1f}".format(
        min(rim), max(rim), POND_SCALE, min(rim) * POND_SCALE, max(rim) * POND_SCALE, ratio, min(clear), x_half))


def _selfcheck(mesh, half):
    xs = [p[0] for p in mesh.verts]
    ys = [p[1] for p in mesh.verts]
    assert abs(max(xs) - half) < 0.01 and abs(min(xs) + half) < 0.01, (min(xs), max(xs))
    assert abs(max(ys) - half) < 0.01 and abs(min(ys) + half) < 0.01, (min(ys), max(ys))
    assert all(p[2] == 0.0 for p in mesh.verts)
    assert all(mesh._normal(a, b, c)[2] > 0 for _, a, b, c, _ in mesh.faces)   # 전부 위를 본다


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, built, slot, half in (("RanchFloor", floor(), "Ground", FLOOR_HALF_CM),
                                    ("PondDisc", disc(), "Water", None)):
        _pond_selfcheck(built) if half is None else _selfcheck(built, half)
        print("저장:", os.path.abspath(os.path.join(OUT_DIR, name + ".obj")), "삼각형", built.write(name, [slot]))
    print("[RanchGround] 자체 검사 통과")
