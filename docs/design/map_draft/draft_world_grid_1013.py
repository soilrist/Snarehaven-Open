"""감독 초안: 「작은 세계」 군도 — 가운데 큰 한반도 섬 + 둘레 대륙 모양 섬들. 272×176, 글자는 make_world_grid.py 와 같다."""
import sys
from PIL import Image
import numpy as np
sys.path.insert(0, 'tools/unreal')
from make_world_map import _edt as edt, _components

def zoom(a, f, order=1):
    return np.array(Image.fromarray(a.astype(np.float32)).resize((a.shape[1]*f, a.shape[0]*f), Image.BILINEAR))
from PIL import Image, ImageDraw
ROWS, COLS = 176, 272
rng = np.random.default_rng(20261013)

def poly(pts, lon0, lat0, sx, sy, c0, r0):
    return [(c0 + (lo - lon0) * sx, r0 + (lat0 - la) * sy) for lo, la in pts]

KOREA = [(124.3,40.0),(125.4,40.6),(126.6,41.6),(128.2,41.9),(129.6,42.3),(130.6,42.4),(129.8,41.0),(128.6,40.2),
         (127.6,39.7),(128.3,38.6),(129.3,37.1),(129.5,35.6),(129.2,35.1),(128.3,34.8),(127.4,34.5),(126.3,34.4),
         (126.4,35.2),(126.7,35.9),(126.3,36.6),(126.9,37.3),(126.2,37.7),(125.2,37.8),(125.5,38.6),(125.1,39.3)]
def mask():
    img = Image.new("L", (COLS, ROWS), 0); d = ImageDraw.Draw(img)
    k = poly(KOREA, 124.0, 42.6, 13.0, 15.0, 96, 10)          # 한반도: 열 104~178, 행 16~144
    d.polygon(k, fill=255)
    d.ellipse((122, 156, 144, 165), fill=255)                   # 제주
    d.ellipse((168, 116, 172, 122), fill=255)                   # 울릉
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
    kor = (C >= 92) & (C <= 182) & (R >= 12) & (R <= 162)
    g[land & kor & (R + n*4 < 52)] = "S"                         # 북쪽 = 눈(개마고원)
    g[land & kor & (C + n*4 > 146) & (R > 92)] = "D"            # 남동 = 가을 숲(경상)
    g[land & kor & (C + n*4 < 128) & (R > 100)] = "W"           # 남서 = 늪(갯벌)
    g[land & (C < 100) & (R + n*4 < 40)] = "S"                  # 대륙 북쪽 눈
    g[land & (C > 182) & (R < 50) & (C < 228)] = "S"                        # 홋카이도 닮은 섬 눈
    g[land & (C > 176) & (R >= 50) & (R < 132)] = "D"           # 활 섬 = 단풍
    g[land & (C > 228) & (R < 82)] = "D"
    g[land & (C > 200) & (R > 130)] = "D"                       # 남동 섬
    g[land & (C < 82) & (R > 84) & (R + n*4 > 136)] = "W"
    for r, c in ((40,140),(80,128),(156,137),(60,50),(110,40),(150,234)):
        rr, cc = np.mgrid[0:ROWS,0:COLS]; lake = ((rr-r)/2.5)**2 + ((cc-c)/3.5)**2 < 1
        g[lake & land] = "L"
    near = edt(g == ".") < 1.6 + (n > 0.6)
    g[near & np.isin(g, ["G", "D"])] = "B"
    return g

land, n = mask(); g = paint(land, n)
_, cnt = _components(np.isin(g, list("SGDWB")))
print("뭍 {:.1%} · 섬 {}개".format(np.isin(g, list("SGDWBL")).mean(), sorted(cnt[1:], reverse=True)))
COL = {".": (70,140,190), "S": (240,244,248), "G": (126,180,90), "D": (210,140,60), "W": (90,120,80), "B": (232,214,160), "L": (110,175,215)}
img = np.array([[COL[ch] for ch in row] for row in g], np.uint8)
im = Image.fromarray(img).resize((COLS*4, ROWS*4), Image.NEAREST)
im.save(sys.argv[1]); open(sys.argv[2], "w").write("\n".join("".join(r) for r in g))
