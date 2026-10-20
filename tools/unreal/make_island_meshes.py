"""사냥터 땅 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_island_meshes.py   → Saved/Generated/WorldGround_<행>_<열>.obj (+ .mtl)

지도(704m × 1088m)는 `make_world_map.py` 가 구운 해안 거리장(`islands.py`, 200cm 노드)을 그대로 따른다.
높이: 뭍(d ≤ 0) = 0, 바다 쪽은 450cm 에 걸쳐 -140 까지 비탈(수면 -10 · 해저 판 -140, `ASeaActor`).
**모래 턱이 없다** — 옛 모래 원판(윗면 33~56cm)이 발 높이 45 를 넘어 물에서 해변으로 못 올라왔다. 모래는 땅 텍스처 색이다.
타일(128m)마다 한 메시 — 화면 밖 타일은 안 그린다. 네 꼭짓점이 다 해저 깊이인 칸은 빼고(해저 판이 받친다),
평평한 뭍 칸은 큰 사각형으로 합친다(z = 0 평면 위라 T 이음매가 틈을 만들지 않는다). 좌표는 월드 cm — 액터는 원점·배율 1.
OBJ 반입이 Y 를 뒤집는다 — 정점 y 를 미리 뒤집어 적는다. 슬롯 하나(`Ground`) — 바닥 머티리얼은 월드 좌표로 칠한다.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402
from make_tree_meshes import OUT_DIR, Mesh  # noqa: E402

SLOPE_CM = 450.0      # 해안에서 바다 쪽으로 이만큼 가면 해저 깊이
DEEP_Z = -140.0       # 해저 판 윗면(수면 -10 − 130)
TILE_CELLS = 64       # 타일 한 변 = 64 × 200cm = 128m
UP = (0.0, 0.0, 1.0)


def node_z(d):
    return 0.0 if d <= 0.0 else -min(-DEEP_Z, d * -DEEP_Z / SLOPE_CM)


def height(x, y):
    return node_z(islands.signed_distance(x, y))


def _tile(ti, tj):
    ni, nj, f = islands.NI, islands.NJ, islands.FIELD_CM
    i0, j0 = ti * TILE_CELLS, tj * TILE_CELLS
    i1, j1 = min(i0 + TILE_CELLS, ni - 1), min(j0 + TILE_CELLS, nj - 1)
    d = lambda i, j: islands.COAST[i * nj + j]
    pos = lambda i, j: (islands.X0 + i * f, -(islands.Y0 + j * f))
    mesh, index = Mesh(), {}

    def vert(i, j):
        if (i, j) not in index:
            index[(i, j)] = mesh.v(*pos(i, j), node_z(d(i, j)))
        return index[(i, j)]

    def rect(ia, ib, ja, jb):   # 평평한 뭍 사각형 — 칸 [ia, ib) × [ja, jb)
        v = [mesh.v(*pos(i, j), 0.0) for i, j in ((ia, ja), (ib, ja), (ib, jb), (ia, jb))]
        mesh.tri("Ground", v[0], v[1], v[2], UP)
        mesh.tri("Ground", v[0], v[2], v[3], UP)

    open_rects = {}   # (ja, jb) → 시작 행 — 같은 열 구간이 다음 행에서도 이어지면 세로로 늘린다
    for i in range(i0, i1):
        runs, start = [], None
        for j in range(j0, j1 + 1):
            corners = ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)) if j < j1 else ()
            land = bool(corners) and all(d(a, b) <= 0 for a, b in corners)
            if land and start is None:
                start = j
            if not land and start is not None:
                runs.append((start, j))
                start = None
            if corners and not land and not all(d(a, b) >= SLOPE_CM for a, b in corners):
                v = [vert(a, b) for a, b in corners]
                mesh.tri("Ground", v[0], v[1], v[2], UP)
                mesh.tri("Ground", v[0], v[2], v[3], UP)
        for run in list(open_rects):
            if run not in runs:
                rect(open_rects.pop(run), i, *run)
        for run in runs:
            open_rects.setdefault(run, i)
    for run, ia in open_rects.items():
        rect(ia, i1, *run)
    return mesh


def build():
    tiles = {}
    for ti in range((islands.NI - 1 + TILE_CELLS - 1) // TILE_CELLS):
        for tj in range((islands.NJ - 1 + TILE_CELLS - 1) // TILE_CELLS):
            mesh = _tile(ti, tj)
            if mesh.faces:
                tiles["WorldGround_{}_{}".format(ti, tj)] = mesh
    return tiles


def _selfcheck(tiles):
    assert node_z(-5.0) == 0.0 and node_z(SLOPE_CM) == DEEP_Z and node_z(10000.0) == DEEP_Z
    # 해안에서 물가까지 최대 경사 — 노드 200cm 에 140/450 비율이면 한 칸 62cm 내려간다(걸어 오를 수 있는 경사 ≈ 17°)
    assert DEEP_Z / SLOPE_CM > -0.36
    for name, mesh in tiles.items():
        assert all(n[2] >= 0 for n in (mesh._normal(a, b, c) for _, a, b, c, _ in mesh.faces)), name   # 전부 위를 본다
    # 시작 거점 땅은 어느 타일엔가 있다
    for o in islands.OUTPOSTS:
        ti, tj = int((o["x"] - islands.X0) // (islands.FIELD_CM * TILE_CELLS)), int((o["y"] - islands.Y0) // (islands.FIELD_CM * TILE_CELLS))
        assert "WorldGround_{}_{}".format(ti, tj) in tiles, o["id"]


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    for old in os.listdir(OUT_DIR):
        if old.startswith(("WorldGround_", "IslandGround_")):
            os.remove(os.path.join(OUT_DIR, old))
    built = build()
    _selfcheck(built)
    total = 0
    for name, tile_mesh in sorted(built.items()):
        total += tile_mesh.write(name, ["Ground"])
    print("타일 {}장 · 삼각형 {}".format(len(built), total))
