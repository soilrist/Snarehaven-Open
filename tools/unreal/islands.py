"""사냥터(L_Hunt) 지도 판정 — `make_world_map.py` 가 구운 하벤 만 지도 자료를 읽는다. 언리얼 밖(python3)·안(배치 스크립트) 둘 다 import 한다.

  python3 tools/unreal/islands.py   → 자체 검사와 거점·크기를 찍는다

자료(`data/`): world_coast.bin 해안 거리장(int16 cm, < 0 = 뭍, 호수도 물) · world_zone.bin 존 칸(uint8) · world_map.json.
같은 자료를 C++ `ASeaActor`(CoastField·ZoneField — `setup_sea.py` 가 넣는다)가 들고 게임에서 같은 식(쌍선형)으로 잰다.
numpy 없이 표준 라이브러리만 쓴다(언리얼 python).
"""
import array
import json
import math
import os

_DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

with open(os.path.join(_DATA, "world_map.json"), encoding="utf-8") as _f:
    META = json.load(_f)

FIELD_CM = META["field_cm"]
CELL_CM = META["cell_cm"]
X0, Y0 = META["origin_cm"]
NI, NJ = META["coast_size"]           # 노드 수 (월드 X, Y)
ZI, ZJ = META["zone_size"]            # 존 칸 수 (월드 X, Y)
ZONE_CODES = META["zone_codes"]       # 이름 → 코드
ZONE_NAMES = {v: k for k, v in ZONE_CODES.items()}
BEACH_BIT = META["beach_bit"]
OUTPOSTS = META["outposts"]           # [{id, x, y, starter, boat}]
# 사냥터 시작 자리 = 시작 거점 헤이븐(평원, 해안 7m 안쪽) — PlayerStart·상인 노점이 여기 선다
START_OUTPOST = "haven_outpost"
START = next((o["x"], o["y"]) for o in OUTPOSTS if o["id"] == START_OUTPOST)
BOUNDS = (X0, X0 + (NI - 1) * FIELD_CM, Y0, Y0 + (NJ - 1) * FIELD_CM)   # x0, x1, y0, y1 (월드 cm)
HALF_EXTENT = max(-X0, BOUNDS[1], -Y0, BOUNDS[3])   # 바다 벽 틀 반폭(정사각) — `ASeaActor::IslandHalfExtentCm`

COAST = array.array("h")
with open(os.path.join(_DATA, "world_coast.bin"), "rb") as _f:
    COAST.frombytes(_f.read())
ZONE = array.array("B")
with open(os.path.join(_DATA, "world_zone.bin"), "rb") as _f:
    ZONE.frombytes(_f.read())
assert len(COAST) == NI * NJ and len(ZONE) == ZI * ZJ


def signed_distance(x, y):
    """해안까지 거리(cm, < 0 = 뭍). 노드 쌍선형 — 자료 밖은 바다(먼 값)."""
    fi, fj = (x - X0) / FIELD_CM, (y - Y0) / FIELD_CM
    if not (0.0 <= fi < NI - 1 and 0.0 <= fj < NJ - 1):
        return float(META.get("clamp_cm", 30000.0))
    i, j = int(fi), int(fj)
    ti, tj = fi - i, fj - j
    k = i * NJ + j
    a, b, c, d = COAST[k], COAST[k + 1], COAST[k + NJ], COAST[k + NJ + 1]
    return (a * (1 - tj) + b * tj) * (1 - ti) + (c * (1 - tj) + d * tj) * ti


def zone_code(x, y):
    """존 칸 코드(아래 4비트 = ZONE_CODES, BEACH_BIT = 모래). 자료 밖은 0."""
    i, j = int(math.floor((x - X0) / CELL_CM)), int(math.floor((y - Y0) / CELL_CM))
    if not (0 <= i < ZI and 0 <= j < ZJ):
        return 0
    return ZONE[i * ZJ + j]


def zone_at(x, y):
    """존 이름('plains'·'snow'·'forest'·'swamp') 또는 None."""
    return ZONE_NAMES.get(zone_code(x, y) & 0x0F)


def on_beach(x, y):
    return bool(zone_code(x, y) & BEACH_BIT)


def coast(x, y):
    """(거리, along, 존 이름) — 옛 섬 목록 판과 같은 모양. along = 해안 무늬 좌표(월드 대각)."""
    return signed_distance(x, y), (x + y) * 0.7071, zone_at(x, y)


def on_land(x, y, margin=0.0):
    """해안에서 margin 이상 안쪽 뭍인지."""
    return signed_distance(x, y) < -margin


def coast_crossing(x, y, dx, dy, step=25.0, limit=60000.0):
    """(x, y) 뭍에서 (dx, dy) 방향으로 걸어 처음 바다가 되는 해안 점."""
    t = 0.0
    while t < limit and signed_distance(x + dx * t, y + dy * t) < 0.0:
        t += step
    return x + dx * t, y + dy * t


def nearest_coast(x, y, rays=72):
    """(해안 점 x, y, 바다 쪽 단위 방향 dx, dy) — 뭍 한 점에서 가장 가까운 해안. 부두·선착장 자리."""
    best = None
    for n in range(rays):
        a = n * math.tau / rays
        dx, dy = math.cos(a), math.sin(a)
        cx, cy = coast_crossing(x, y, dx, dy)
        d = math.hypot(cx - x, cy - y)
        if best is None or d < best[0]:
            best = (d, cx, cy)
    _, cx, cy = best
    gx = signed_distance(cx + 50.0, cy) - signed_distance(cx - 50.0, cy)
    gy = signed_distance(cx, cy + 50.0) - signed_distance(cx, cy - 50.0)
    n = math.hypot(gx, gy) or 1.0
    return cx, cy, gx / n, gy / n


def land_point(x, y, zone=None, margin=300.0, reach=6000.0, step=200.0):
    """(x, y) 둘레에서 가장 가까운 뭍 점(해안 margin 안쪽, 존 제한 가능). 없으면 None."""
    best = None
    r = 0.0
    while r <= reach and best is None:
        n = max(1, int(math.tau * r / step))
        for k in range(n):
            a = k * math.tau / n
            px, py = x + r * math.cos(a), y + r * math.sin(a)
            if on_land(px, py, margin) and (zone is None or zone_at(px, py) == zone):
                best = (px, py)
                break
        r += step
    return best


def _selfcheck():
    x0, x1, y0, y1 = BOUNDS
    # 지도 둘레는 바다, 뭍은 절반쯤
    for x, y in ((x0, y0), (x1, y1), (x0, y1), (x1, y0), (0.0, y0 + 10.0)):
        assert signed_distance(x, y) > 0.0, (x, y)
    land = sum(1 for v in COAST if v < 0) / float(len(COAST))
    assert 0.25 < land < 0.6, land
    # 노드 위에서는 자료 값 그대로(쌍선형 원점 검사)
    assert signed_distance(X0 + 100 * FIELD_CM, Y0 + 200 * FIELD_CM) == COAST[100 * NJ + 200]
    # 시작 거점은 뭍 둘레 — 부두를 놓을 해안이 가깝다
    for o in OUTPOSTS:
        p = land_point(o["x"], o["y"], margin=300.0)
        assert p is not None, o["id"]
        cx, cy, dx, dy = nearest_coast(*p)
        assert signed_distance(cx + dx * 300.0, cy + dy * 300.0) > 0.0, o["id"]
    # 존 넷이 다 있다
    assert {ZONE[k] & 0x0F for k in range(len(ZONE))} >= set(ZONE_CODES.values())
    return land


if __name__ == "__main__":
    share = _selfcheck()
    print("지도 {:.0f}m × {:.0f}m · 뭍 {:.0%} · 거점 {}곳 (시작 {})".format(
        (BOUNDS[1] - BOUNDS[0]) / 100.0, (BOUNDS[3] - BOUNDS[2]) / 100.0, share, len(OUTPOSTS),
        ", ".join(o["id"] for o in OUTPOSTS if o["starter"])))
    print("자체 검사 통과")
