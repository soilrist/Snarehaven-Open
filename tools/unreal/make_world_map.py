"""사냥터(L_Hunt) 지도 「작은 세계」 굽기 (시스템 python3 + numpy — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_world_grid.py            # 종류 격자 → data/world_grid.txt (지도 모양을 바꿀 땐 여기)
  python3 tools/unreal/make_world_map.py [미리보기.png]
    → tools/unreal/data/world_coast.bin · world_zone.bin · world_map.json

원본: `data/world_grid.txt` — 우리가 그린 272×176 칸 종류 격자(`make_world_grid.py`, 시드 고정 노이즈 해안).
(`.` 바다 · S 눈 · G 풀 · D 가을 · W 늪 · B 모래 · L 호수, 한 줄 = 한 행 RLE). 사용자 2026-10-03 "레퍼런스랑 너무 똑같다 · 우리만의 지도".
가운데 한반도 + 둘레 대륙 닮은 섬 군도 — 모양 설명은 make_world_grid.py 머리말.

크기: 격자 칸 하나 = `CELL_CM` 400cm → 1088m × 704m (🟡 DESIGN 1.3 — 달리기 555cm/s 로 가로 3분 16초).
방향: 지도 위 = 월드 +X(기본 카메라가 +X 를 본다), 지도 오른쪽 = 월드 +Y. 지도 가운데 = 월드 원점.

나오는 것 (`islands.py` 가 읽는다 — 언리얼 python 에 numpy 가 없어서 미리 굽는다):
  world_coast.bin  해안 거리장 int16 LE (cm, < 0 = 뭍, 호수도 물). 노드 (i, j) = 월드 (X0 + i·FIELD_CM, Y0 + j·FIELD_CM), i 가 바깥 순
  world_zone.bin   존 칸 uint8, 원본 칸(400cm) 하나에 하나. 아래 4비트 = ZONE_CODES, 0x10 = 모래 칸. i(월드 X) 가 바깥 순
  world_map.json   크기·원점·존 코드·거점 자리(격자 → 월드)
  Saved/Generated/T_World_Biome.png  바닥 색 무게(R 눈 · G 가을 · B 늪 · A 모래), 100cm 한 칸 — 땅 머티리얼(`setup_world_ground.py`)
  Saved/Generated/T_World_Coast.png  해안 거리(회색, 128 = 해안, 한 단 = COAST_TEX_CM), 같은 칸 — 물 셰이더(`setup_sea.py`)
  Saved/Generated/T_World_Map.png    지도 그림(미니맵·지도 화면), 같은 칸
  세 텍스처 모두 0행 = 월드 X 최대(지도 위), 0열 = 월드 Y 최소 → UV = ((y − Y0) / 가로, (X1 − x) / 세로)
"""
import json
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
GRID = os.path.join(DATA, "world_grid.txt")

CELL_CM = 400.0        # 격자 칸 하나 — 🟡 한 곳에서만 바꾼다
FIELD_CM = 200.0       # 해안 거리장 노드 간격
FINE = 4               # 모래·해안을 다듬는 속격자 = 원본 칸 / 4 (100cm)
BLUR_FINE = 2.0        # 해안 계단 다듬기 가우스 σ(속격자 칸) — 400cm 계단 모서리가 둥글게
CLAMP_CM = 30000.0
COAST_TEX_CM = 20.0    # 해안 거리 텍스처 한 단 — 128 ± 127단 = ±2540cm (물 셰이더가 쓰는 범위 1500 보다 넓다)
BIOME_SIGMA = 3.0      # 바닥 색 경계 다듬기 σ(100cm 칸) — 400cm 칸 계단이 둥근 곡선, 머티리얼이 0.5 에서 날카롭게 자른다
GEN_DIR = os.path.join(HERE, "..", "..", "Saved", "Generated")

# 존 코드 — `DT_ZoneTable` 행의 FieldCode 와 같다 (create_data_tables.py)
ZONE_CODES = {"plains": 1, "snow": 2, "forest": 3, "swamp": 4}
BEACH_BIT = 0x10

# 거점 (이름, 열, 행, 시작 거점, 배 있음, 칸 글자) — 「작은 세계」 격자 자리. 전부 해안(바다)에서 약 3칸 안쪽, 배 자리가 선다.
# 시작 거점 넷 = 한반도(헤이븐 = 서해 쪽 호수 옆 · 눈방울 = 북쪽 눈 아래 · 버들 = 남서 · 단풍 = 남동 숲 앞) · 나머지 열 = 제주·울릉·둘레 섬
OUTPOSTS = [
    ("frost_camp", 40, 11, False, True, "S"),          # 북서 대륙(유라시아 끝) 눈
    ("thimble_shop", 91, 76, False, True, "G"),        # 한반도 서쪽 작은 섬
    ("gull_harbor", 210, 56, False, True, "D"),        # 일본 닮은 긴 활 섬
    ("snowbell_post", 140, 53, True, True, "G"),       # 한반도 북쪽 눈 아래
    ("reed_harbor", 66, 137, False, True, "W"),        # 아프리카 닮은 섬 남쪽 늪
    ("otter_docks", 175, 152, False, True, "D"),       # 큐슈 닮은 섬
    ("acorn_outpost", 50, 89, False, True, "G"),       # 아프리카 닮은 섬 북쪽 풀밭
    ("minnow_docks", 176, 117, False, True, "D"),      # 울릉
    ("lakeside_outpost", 234, 135, False, True, "D"),  # 호주 닮은 섬 북쪽 호수 위
    ("haven_outpost", 117, 76, True, True, "G"),       # 한반도 서해 쪽 호수 옆 — 퀘스트·시작점
    ("moss_island", 127, 159, False, True, "G"),       # 제주
    ("willow_outpost", 133, 111, True, True, "G"),     # 한반도 남서
    ("maple_outpost", 162, 92, True, True, "G"),       # 한반도 남동 숲 앞
    ("fern_harbor", 245, 27, False, True, "D"),        # 아메리카 닮은 섬
]
MAIN_OUTPOSTS = {"snowbell_post", "haven_outpost", "willow_outpost", "maple_outpost"}   # 한반도 위 (나머지는 섬)


def load_grid(path=GRID):
    rows = [l.strip() for l in open(path, encoding="utf-8") if l.strip() and not l.startswith("#")]
    g = np.array([list("".join(c * int(k or 1) for c, k in re.findall(r"([.SGDWBL])(\d*)", r))) for r in rows])
    assert g.shape == (176, 272), g.shape
    return g


def _components(mask):
    """4-이웃 덩어리 → 라벨 배열, 크기 목록."""
    label = np.zeros(mask.shape, np.int32)
    sizes = [0]
    h, w = mask.shape
    for r0 in range(h):
        for c0 in range(w):
            if mask[r0, c0] and not label[r0, c0]:
                n = len(sizes)
                stack, count = [(r0, c0)], 0
                label[r0, c0] = n
                while stack:
                    r, c = stack.pop()
                    count += 1
                    for rr, cc in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
                        if 0 <= rr < h and 0 <= cc < w and mask[rr, cc] and not label[rr, cc]:
                            label[rr, cc] = n
                            stack.append((rr, cc))
                sizes.append(count)
    return label, sizes


def _fill_unknown(g, unknown):
    """모르는 칸을 가장 가까운 아는 칸 종류로 (한 칸씩 번져 나간다)."""
    g = g.copy()
    unknown = unknown.copy()
    while unknown.any():
        grew = False
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            src = np.roll(np.roll(g, dr, 0), dc, 1)
            known = ~np.roll(np.roll(unknown, dr, 0), dc, 1)
            take = unknown & known
            g[take] = src[take]
            unknown &= ~take
            grew |= take.any()
        assert grew
    return g


def _blur(a, sigma):
    r = int(sigma * 3)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    a = np.pad(a, r, mode="edge")
    a = np.apply_along_axis(lambda v: np.convolve(v, k, "valid"), 0, a)
    return np.apply_along_axis(lambda v: np.convolve(v, k, "valid"), 1, a)


def _edt(feature):
    """정확한 유클리드 거리 변환(칸 단위) — feature 칸까지 거리. Felzenszwalb 하한 포락선, 열 방향은 numpy·행 방향은 파이썬."""
    inf = 1e12
    h, w = feature.shape
    # 1단계: 열마다 위아래로 가장 가까운 feature 까지 거리²
    f = np.where(feature, 0.0, inf)
    d = np.full((h, w), inf)
    last = np.full(w, -inf)
    for r in range(h):
        last = np.where(feature[r], r, last)
        d[r] = (r - last) ** 2
    last = np.full(w, inf)
    for r in range(h - 1, -1, -1):
        last = np.where(feature[r], r, last)
        d[r] = np.minimum(d[r], (last - r) ** 2)
    d = np.minimum(d, inf)
    # 2단계: 행마다 1차원 제곱 거리 변환
    out = np.empty((h, w))
    xs = np.arange(w, dtype=float)
    for r in range(h):
        fr = d[r]
        v = [0]
        z = [-inf, inf]
        for q in range(1, w):
            while True:
                p = v[-1]
                s = ((fr[q] + q * q) - (fr[p] + p * p)) / (2.0 * q - 2.0 * p)
                if s <= z[-2]:
                    v.pop()
                    z.pop()
                    if not v:
                        v, z = [q], [-inf, inf]
                        break
                    continue
                v.append(q)
                z[-1] = s
                z.append(inf)
                break
        k = 0
        row = np.empty(w)
        for q in range(w):
            while z[k + 1] < q:
                k += 1
            row[q] = (q - v[k]) ** 2 + fr[v[k]]
        out[r] = row
    return np.sqrt(out)


def world_grids(g):
    """(해안 거리장 노드 배열[i, j] cm, 존 칸 배열[i, j] uint8) — i = 월드 X(지도 아래→위), j = 월드 Y(왼→오)."""
    rows, cols = g.shape
    world = g[::-1, :]                         # 지도 아래 행 = 월드 X 최소
    land = np.isin(world, list("SGDWB")).astype(float)
    # 속격자 100cm 로 늘려 흐리고 반에서 자른다 → 해안 계단이 둥근 곡선
    fine = np.kron(land, np.ones((FINE, FINE)))
    pad = 12
    fine = np.pad(fine, pad)                   # 지도 둘레 = 바다
    fine = _blur(fine, BLUR_FINE) >= 0.5
    step = CELL_CM / FINE
    inside = _edt(~fine) * step                # 뭍 칸 → 가장 가까운 물 칸
    outside = _edt(fine) * step
    sdf = np.where(fine, -(inside - step * 0.5), outside - step * 0.5)
    sdf = sdf[pad:-pad, pad:-pad]
    # 노드 = 둘레 속격자 칸 넷 평균 (노드 간격 FIELD_CM = 속격자 2칸)
    k = int(FIELD_CM / step)
    sp = np.pad(sdf, 1, mode="edge")
    nodes = (sp[:-1, :-1] + sp[1:, :-1] + sp[:-1, 1:] + sp[1:, 1:]) * 0.25   # (rows·4+1, cols·4+1) 모서리
    nodes = nodes[::k, ::k]
    nodes = np.clip(nodes, -CLAMP_CM, CLAMP_CM)

    zone = np.zeros(world.shape, np.uint8)
    zone[world == "G"] = ZONE_CODES["plains"]
    zone[world == "S"] = ZONE_CODES["snow"]
    zone[world == "D"] = ZONE_CODES["forest"]
    zone[world == "W"] = ZONE_CODES["swamp"]
    beach = world == "B"
    # 모래·물 칸 존 = 가장 가까운 뭍 존 (해안 다듬기로 뭍이 된 바다 칸도 존을 가진다)
    zc = _fill_unknown(zone.astype(np.int16), zone == 0).astype(np.uint8)
    zc[beach] |= BEACH_BIT
    return nodes, zc, sdf


def textures(zone, sdf):
    """(바닥 색 RGBA, 해안 회색, 지도 그림 RGB) uint8 — 0행 = 월드 X 최대."""
    z = np.kron(zone, np.ones((FINE, FINE), np.uint8))
    chans = [(z & 0x0F) == ZONE_CODES["snow"], (z & 0x0F) == ZONE_CODES["forest"],
             (z & 0x0F) == ZONE_CODES["swamp"], (z & BEACH_BIT) > 0]
    pad = int(BIOME_SIGMA * 3)
    biome = np.stack([_blur(np.pad(c.astype(float), pad, mode="edge"), BIOME_SIGMA)[pad:-pad, pad:-pad] for c in chans], -1)
    biome = np.round(np.clip(biome, 0, 1) * 255).astype(np.uint8)[::-1]
    coast = np.clip(np.round(sdf / COAST_TEX_CM) + 128, 0, 255).astype(np.uint8)[::-1]
    w = biome.astype(float) / 255.0
    col = lambda rgb: np.array(rgb, float)
    img = np.broadcast_to(col((104, 162, 84)), w.shape[:2] + (3,)).copy()      # 풀
    for k, rgb in enumerate(((236, 240, 246), (222, 146, 64), (88, 120, 70), (232, 212, 150))):
        a = np.clip((w[..., k:k + 1] - 0.4) / 0.2, 0, 1)
        img = img * (1 - a) + col(rgb) * a
    d = sdf[::-1][..., None]
    sea = np.clip(d / 1200.0, 0, 1)
    water = col((110, 160, 214)) * (1 - sea) + col((52, 96, 160)) * sea
    img = np.where(d > 0, water, img)
    return biome, coast, np.round(img).astype(np.uint8)


def write_textures(zone, sdf):
    from PIL import Image
    os.makedirs(GEN_DIR, exist_ok=True)
    biome, coast, img = textures(zone, sdf)
    Image.fromarray(biome, "RGBA").save(os.path.join(GEN_DIR, "T_World_Biome.png"))
    Image.fromarray(coast, "L").save(os.path.join(GEN_DIR, "T_World_Coast.png"))
    Image.fromarray(square_map(img), "RGB").save(os.path.join(GEN_DIR, "T_World_Map.png"))
    return biome.shape


def square_map(img):
    """지도 그림을 먼 바다로 위아래 채워 정사각(가운데 ± 긴 반폭)으로 — `ASeaActor::GetMapBounds` 와 같은 틀.
    거점마다 진갈색 테두리 흰 점을 찍는다."""
    rows, cols = img.shape[:2]
    top = (cols - rows) // 2
    out = np.empty((cols, cols, 3), np.uint8)
    out[:] = (52, 96, 160)
    out[top:top + rows] = img
    half = cols * 50.0   # 1 화소 = 1m — 그림 가운데가 월드 원점
    yy, xx = np.ogrid[:cols, :cols]
    for name, c0, r0, *_ in OUTPOSTS:
        x, y = world_xy(c0, r0)
        d2 = (yy - (top + (rows * 50.0 - x) / 100.0)) ** 2 + (xx - (y + half) / 100.0) ** 2
        out[d2 <= 7 ** 2] = (74, 48, 30)
        out[d2 <= 4 ** 2] = (250, 244, 228)
    return out


def world_xy(col, row, rows=176, cols=272):
    """원본 격자 칸 가운데 → 월드 (x, y) cm."""
    return (rows * 0.5 - (row + 0.5)) * CELL_CM, ((col + 0.5) - cols * 0.5) * CELL_CM


def write(g, nodes, zone):
    rows, cols = g.shape
    x0, y0 = -rows * 0.5 * CELL_CM, -cols * 0.5 * CELL_CM
    with open(os.path.join(DATA, "world_coast.bin"), "wb") as f:
        f.write(np.round(nodes).astype("<i2").tobytes())
    with open(os.path.join(DATA, "world_zone.bin"), "wb") as f:
        f.write(zone.astype(np.uint8).tobytes())
    meta = {
        "source": "world_grid.txt (작은 세계, make_world_grid.py)",
        "cell_cm": CELL_CM, "field_cm": FIELD_CM, "origin_cm": [x0, y0],
        "zone_size": [rows, cols], "coast_size": list(nodes.shape),
        "zone_codes": ZONE_CODES, "beach_bit": BEACH_BIT,
        "outposts": [
            {"id": name, "x": round(world_xy(c, r)[0]), "y": round(world_xy(c, r)[1]), "starter": starter, "boat": boat}
            for name, c, r, starter, boat, _ in OUTPOSTS
        ],
    }
    with open(os.path.join(DATA, "world_map.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
    return meta


def preview(path, nodes, zone):
    from PIL import Image
    colors = {1: (88, 170, 80), 2: (235, 238, 245), 3: (220, 140, 60), 4: (70, 110, 70)}
    z = np.pad(np.kron(zone, np.ones((2, 2), np.uint8)), ((0, 1), (0, 1)), mode="edge")
    img = np.zeros(nodes.shape + (3,), np.uint8)
    img[:] = (40, 60, 140)
    for code, col in colors.items():
        img[((z & 0x0F) == code) & (nodes < 0)] = col
    img[((z & BEACH_BIT) > 0) & (nodes < 0)] = (240, 215, 150)
    img[(nodes >= 0) & (nodes < 150)] = (110, 140, 210)
    Image.fromarray(img[::-1, :]).save(path)


def _check_outposts(g):
    """거점 칸 = 제 존 뭍, 3칸(배 있음 = 바다, 없음 = 호수) 안팎에 물 · 시작 거점 = 평원 · 한반도 밖 거점 = 섬."""
    sea = _edt(g == ".")
    lake = _edt(g == "L")
    label, sizes = _components(np.isin(g, list("SGDWB")))
    main = int(np.argmax(sizes))
    for name, c, r, starter, boat, cls in OUTPOSTS:
        assert g[r, c] == cls, (name, g[r, c], cls)
        water = sea[r, c] if boat else lake[r, c]
        assert 1.5 <= water <= 5.0, (name, water)
        assert (g[r, c] != "L") and (label[r, c] > 0), name
        assert not starter or cls == "G", name
        assert (label[r, c] == main) == (name in MAIN_OUTPOSTS), name


def _selfcheck(g, nodes, zone, meta):
    assert nodes.shape == (g.shape[0] * 2 + 1, g.shape[1] * 2 + 1), nodes.shape
    _check_outposts(g)
    # 가장자리 노드는 전부 바다
    assert (nodes[0] > 0).all() and (nodes[-1] > 0).all() and (nodes[:, 0] > 0).all() and (nodes[:, -1] > 0).all()
    # 시작 거점은 전부 뭍 근처(해안 2칸 안) — 부두가 선다
    x0, y0 = meta["origin_cm"]
    for o in meta["outposts"]:
        i, j = int(round((o["x"] - x0) / FIELD_CM)), int(round((o["y"] - y0) / FIELD_CM))
        near = nodes[max(i - 10, 0): i + 11, max(j - 10, 0): j + 11]
        assert (near < 0).any() and (near > 0).any(), o["id"]
    land = (nodes < 0).mean()
    assert 0.25 < land < 0.6, land
    return land


if __name__ == "__main__":
    grid = load_grid()
    field, zones, fine_sdf = world_grids(grid)
    meta = write(grid, field, zones)
    tex_shape = write_textures(zones, fine_sdf)
    assert tex_shape[:2] == (grid.shape[0] * FINE, grid.shape[1] * FINE), tex_shape
    land_share = _selfcheck(grid, field, zones, meta)
    if len(sys.argv) > 1:
        preview(sys.argv[1], field, zones)
    print("격자 {}×{} · 해안 거리장 {}×{} · 존 칸 {}×{} · 뭍 {:.1%} · 거점 {} 자리 검사 통과".format(
        grid.shape[1], grid.shape[0], field.shape[0], field.shape[1], zones.shape[0], zones.shape[1], land_share, len(meta["outposts"])))
