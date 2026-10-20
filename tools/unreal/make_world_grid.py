"""사냥터(L_Hunt) 종류 격자 「작은 세계」 그리기 (시스템 python3 + numpy — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_world_grid.py
    → tools/unreal/data/world_grid.txt (272×176 칸, 한 줄 = 한 행 RLE) — `make_world_map.py` 가 읽는다

사람 2026-10-13 "저거 너무 이상한데.. 그래도 섬들이 있어야지.. 한국도 넣고 세계지도 넣어도 되겠네": 가운데 가장 큰 섬 = 한반도
(북 눈·가운데 평원·남동 단풍·남서 늪) + 제주·울릉, 둘레에 대륙을 닮은 섬 여섯(북서 유라시아 끝·북쪽 홋카이도 닮음·동쪽 긴 활 섬
(일본 닮음)·큐슈 닮은 섬·남서 아프리카 닮음·남동 호주 닮음·동쪽 끝 아메리카 닮음)과 작은 섬들. 감독 초안 `docs/design/map_draft/` 를 옮겼다.
해안선은 시드 고정 값 노이즈라 같은 코드면 같은 지도다.

글자: `.` 바다 · S 눈 · G 풀 · D 가을 · W 늪 · B 모래 · L 호수. 행 0 = 지도 위(북) · 열 0 = 지도 왼쪽(서).
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from make_world_map import _edt as edt, _components   # noqa: E402

OUT = os.path.join(HERE, "data", "world_grid.txt")
ROWS, COLS = 176, 272
SEED = 20261013

def zoom(a, f, order=1):
    return np.array(Image.fromarray(a.astype(np.float32)).resize((a.shape[1]*f, a.shape[0]*f), Image.BILINEAR))

def poly(pts, lon0, lat0, sx, sy, c0, r0):
    return [(c0 + (lo - lon0) * sx, r0 + (lat0 - la) * sy) for lo, la in pts]

KOREA = [(124.3,40.0),(125.4,40.6),(126.6,41.6),(128.2,41.9),(129.6,42.3),(130.6,42.4),(129.8,41.0),(128.6,40.2),
         (127.6,39.7),(128.3,38.6),(129.3,37.1),(129.5,35.6),(129.2,35.1),(128.3,34.8),(127.4,34.5),(126.3,34.4),
         (126.4,35.2),(126.7,35.9),(126.3,36.6),(126.9,37.3),(126.2,37.7),(125.2,37.8),(125.5,38.6),(125.1,39.3)]
def mask(rng):
    img = Image.new("L", (COLS, ROWS), 0); d = ImageDraw.Draw(img)
    k = poly(KOREA, 124.0, 42.6, 13.0, 15.0, 96, 10)          # 한반도: 열 104~178, 행 16~144
    d.polygon(k, fill=255)
    d.ellipse((122, 156, 144, 165), fill=255)                   # 제주
    d.ellipse((171, 113, 179, 123), fill=255)                   # 울릉(동해 쪽, 한반도 해안과 떨어지게)
    # 한반도 북쪽 경계 = 바다 물길(압록·두만을 바다로) — 대륙과 떼어 섬으로
    d.polygon([(10,8),(86,8),(92,28),(84,48),(62,60),(36,62),(14,50)], fill=255)        # 대륙(유라시아 끝)
    d.polygon([(186,30),(196,20),(208,26),(204,46),(192,50)], fill=255)                 # 북쪽 섬(홋카이도 닮음)
    d.polygon([(208,52),(222,58),(230,80),(218,104),(198,120),(186,128),(180,122),(196,104),(206,84)], fill=255)  # 긴 활 섬(일본 닮음)
    d.polygon([(168,146),(184,138),(194,146),(188,156),(174,156)], fill=255)            # 큐슈 닮은 섬
    d.polygon([(14,92),(54,86),(78,98),(74,124),(56,166),(40,168),(30,140),(12,116)], fill=255)  # 남서 큰 섬(아프리카 닮음)
    d.polygon([(206,140),(236,132),(262,144),(258,166),(226,168),(208,158)], fill=255)  # 남동 섬(호주 닮음)
    d.polygon([(232,30),(256,22),(266,40),(256,70),(244,80),(238,60)], fill=255)        # 동쪽 끝 섬(아메리카 닮음)
    for c, r, a in ((90,78,5),(96,120,4),(186,160,4),(150,168,3),(246,104,5),(120,6+6,0)):
        if a: d.ellipse((c-a, r-a, c+a, r+a), fill=255)                                 # 작은 섬들
    land = np.array(img) > 0
    # 해안 흔들기: 저해상 노이즈로 경계 깎고 붙이기
    n = zoom(rng.uniform(-1,1,(ROWS//8+1, COLS//8+1)), 8, order=1)[:ROWS,:COLS] + 0.5*zoom(rng.uniform(-1,1,(ROWS//3+1, COLS//3+1)), 3, order=1)[:ROWS,:COLS]
    sd = edt(~land) - edt(land)
    land = sd + n * 2.2 > 0
    land[:5] = land[-5:] = False; land[:, :5] = land[:, -5:] = False
    return land, n

def paint(land, n):
    R, C = np.mgrid[0:ROWS, 0:COLS]
    g = np.full((ROWS, COLS), ".", "<U1"); g[land] = "G"
    kor = (C >= 92) & (C <= 182) & (R >= 12) & (R <= 150) & ~((R >= 134) & (C >= 164))
    g[land & kor & (R + n*4 < 52)] = "S"                         # 북쪽 = 눈(개마고원)
    g[land & kor & (C + n*4 > 146) & (R > 92)] = "D"            # 남동 = 가을 숲(경상)
    g[land & kor & (C + n*4 < 128) & (R > 100)] = "W"           # 남서 = 늪(갯벌)
    g[land & (C < 100) & (R + n*4 < 40)] = "S"                  # 대륙 북쪽 눈
    g[land & (C > 182) & (R < 50) & (C < 228)] = "S"                        # 홋카이도 닮은 섬 눈
    g[land & (C > 176) & (R >= 50) & (R < 132)] = "D"           # 활 섬 = 단풍
    g[land & (C > 228) & (R < 82)] = "D"
    g[land & (C > 200) & (R > 130)] = "D"                       # 남동 섬
    g[land & (C >= 164) & (R >= 134) & (R <= 160)] = "D"        # 큐슈 닮은 섬 전부 가을(초록 줄 정리)
    g[land & (C < 82) & (R > 84) & (R + n*4 > 136)] = "W"
    for r, c in ((40,140),(80,124),(156,137),(60,50),(110,40),(142,234)):
        rr, cc = np.mgrid[0:ROWS,0:COLS]; lake = ((rr-r)/2.5)**2 + ((cc-c)/3.5)**2 < 1
        g[lake & land] = "L"
    near = edt(g == ".") < 1.6 + (n > 0.6)
    g[near & np.isin(g, ["G", "D"])] = "B"
    return g


def rle(g):
    out = []
    for row in g:
        parts, k = [], 0
        while k < len(row):
            j = k
            while j < len(row) and row[j] == row[k]:
                j += 1
            parts.append(row[k] + (str(j - k) if j - k > 1 else ""))
            k = j
        out.append("".join(parts))
    return out


def main():
    rng = np.random.default_rng(SEED)
    land, n = mask(rng)
    label, sizes = _components(land)
    land = np.isin(label, [k for k in range(1, len(sizes)) if sizes[k] >= 10])      # 한두 칸 부스러기는 바다로
    g = paint(land, n)
    is_land = np.isin(g, list("SGDWB"))
    _, sizes = _components(is_land)
    pieces = sorted(sizes[1:], reverse=True)
    share = is_land.mean()
    assert 0.33 <= share <= 0.45, share
    assert len(pieces) == 14 and pieces[-1] >= 10, pieces                           # 큰 섬 일곱 + 작은 섬들
    assert g[80, 130] != "." and g[100, 140] != ".", "한반도 한가운데가 뭍"
    assert (g[:5] == ".").all() and (g[-5:] == ".").all() and (g[:, :5] == ".").all() and (g[:, -5:] == ".").all()
    for ch in "SGDWBL":
        assert (g == ch).any(), ch
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("# 작은 세계(한반도 군도) — make_world_grid.py 가 구운 것. 손으로 고치지 말고 스크립트를 고친다\n")
        f.write("\n".join(rle(g)) + "\n")
    print("격자 {}×{} · 뭍 {:.1%} · 덩어리 {}".format(COLS, ROWS, share, pieces))


if __name__ == "__main__":
    main()
