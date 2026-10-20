"""꾸미기 모자 계열 틀 (루프#1362, 명세 `docs/specs/hat-families.md` 4장 — INBOX 지시 10-20 00:20 57종 다양화).

혼자 돌리지 않는다 — `hats_more_make.py` 가 머리 치수·`Builder`·`dome`·`inner` 를 정한 뒤 같은 이름공간으로 읽는다(exec).
행(`hat_*`)은 계열 틀 하나 + 치수 몇 개 + 장식만 고른다(57번 따로 빚기 금지). 아랫단 높이는 모든 계열이 `seat` 하나를 쓴다 —
게임이 얼굴 앞 아랫단을 눈썹선에 맞추고(`HatBrimLift`, 시험 `BrimAboveEyes`) 머리·머리카락이 안쪽 면을 못 뚫게(`NoHeadPenetration`)
재므로, 틀마다 아랫단·안쪽 면을 따로 지으면 계열마다 시험이 따로 깨진다.

조각 2 = `seat` + A 비니 계열, 조각 4(루프#1364) = D 물건(`object_base`·`lathe`·`torus`), 조각 5a(루프#1365) = `lathe` 기울임,
조각 5b(루프#1366) = `lathe` 돌림(`yaw`)·`side_plate`, 조각 6a(루프#1367) = B `brim`·C `cap`·운두 `crown_dome`·`crown_box`,
조각 6b-2(루프#1369) = 운두 `crown_cone`·`crown_plate`·띠 `band_pts`. E 고리는 왕관 하나라 틀 없이 그 행 그대로.
"""

SEAT_DROP = 0.02          # 테 = `zr` − 0.02 — 옛 털모자 행(곰·고양이·토끼·부엉이·`_plain_dome`)이 쓰던 값 그대로


def seat(th, drop=SEAT_DROP):
    """아랫단(테 밑) 높이 — 앞 눈썹 위·뒤 낮게(`zr`) 에서 drop 만큼 내림. 다섯 계열이 같은 함수."""
    return zr(th) - drop


def ear_base(x, y, t=0.02, swell=0.03):
    """`beanie(t, swell)` 껍질 겉면의 높이 — 몸 공간 x(옆)·y(앞뒤) 자리. 귀·뿔 밑동을 여기 묻는다(뜨지도 뚫지도 않게).
    평면은 `surf` 와 같은 초타원(지수 `SUPER_N`), 세로는 `dome` 과 같은 타원."""
    g = G + t
    r = (abs(x - CX) ** SUPER_N + abs((y - CY) / KY) ** SUPER_N) ** (1.0 / SUPER_N)
    return CZ + (HC + g + swell) * math.sqrt(max(0.0, 1 - (r / (HA + g)) ** 2))


def beanie(b, key, t=0.02, cuff=None, cuff_h=0.03, swell=0.03, drop=SEAT_DROP):
    """A 비니·두건 — 머리에 붙는 얇은 껍질(두께 t) + 접은 테(높이 cuff_h). 실루엣은 머리 윤곽 그대로라 꼴은 장식이 만든다.

    key: 껍질 색 키 또는 `key(th)`(줄무늬). cuff: 테 색 키(없으면 껍질과 같음). swell: 꼭대기 부풂.
    돌려주는 값: 껍질 꼭대기 높이."""
    def prof(th):
        z = seat(th, drop)
        ri, ro = head_r(z, G), head_r(z, G + t)
        k = key(th) if callable(key) else key
        c = cuff or k
        pts = inner(z, "inside", n=3)
        pts += [(ri + 0.004, z - 0.01, c), (ro + 0.012, z - 0.007, c), (ro + 0.014, z + cuff_h, c)]
        pts += [(r, zz, k) for r, zz in dome(G + t, z + cuff_h + 0.01, 9, stretch=swell)]
        return pts
    b.revolve(prof)
    return CZ + HC + G + t + swell


# ---------------------------------------------------------------- D 물건 (루프#1364, 명세 6장 조각 4)
def object_base(b, key, t=0.02, cuff=None, cuff_h=0.03, drop=SEAT_DROP):
    """D 물건 — 얕은 받침: 아랫단(`seat`)~정수리만 덮는 얇은 껍질 + 테, 꼭대기 부풂 0. 실루엣은 받침이 아니라 그 위에 올린
    물건이 만든다(명세 4장 — 물건이 머리 높이의 0.5 이상 솟음). 돌려주는 값: 받침 꼭대기 높이(물건을 여기 앉힌다)."""
    return beanie(b, key, t, cuff=cuff, cuff_h=cuff_h, swell=0.0, drop=drop)


def base_z(x, y, t=0.02):
    """`object_base(t)` 껍질 겉면 높이 — 물건 밑면을 여기 맞춘다(뜨지도 머리로 파고들지도 않게)."""
    return ear_base(x, y, t, swell=0.0)


def lathe(b, c, prof, key=None, n=16, ky=1.0, rib=None, lean=(0.0, 0.0), yaw=0.0):
    """물건 회전체(층 원통·타원체·잘린 원뿔) — 점 c 를 지나는 세로축 둘레로 단면 prof [(r, dz[, 색 키])] 를 n 번 돌린다.
    머리 껍질의 `revolve`(머리 꼴 초타원 둘레) 와 달리 둘레가 원(ky = 앞뒤 배율) — 물건은 머리 꼴을 따르지 않는다.
    단면은 아래 극(r=0)에서 바깥을 따라 위 극(r=0)으로 오른다. 색 키는 그 점에서 다음 점까지의 띠. rib(a, i): 둘레 각 a·i 번째 점의
    반지름 배율(호박 골). lean (dx, dy): 높이 dz 마다 고리 가운데를 (dx·dz, dy·dz) 만큼 옮김 — 고리는 수평 그대로라 밑 고리가
    머리 쪽으로 처지지 않고 꼭대기만 기운다(루프#1365 소라 나선이 뒤로 눕는 꼴). yaw: 앞뒤로 긴(ky) 둘레를 세로축 둘레로 돌린다 —
    배 몸을 비스듬히 두어 앞·옆 그림자 둘 다에 돛이 보이게(루프#1366 돛단배)."""
    c = Vector(c)
    cy_, sy_ = math.cos(yaw), math.sin(yaw)
    m = len(prof)
    cols, poles = [], {}
    for j in range(n):
        a = 2 * math.pi * j / n
        col = []
        for i, p in enumerate(prof):
            if p[0] == 0 and i in (0, m - 1):
                if i not in poles:
                    poles[i] = b.bm.verts.new(c + Vector((lean[0] * p[1], lean[1] * p[1], p[1])))
                col.append(poles[i])
                continue
            r = p[0] * (rib(a, i) if rib else 1.0)
            x, y = r * math.cos(a), r * ky * math.sin(a)
            col.append(b.bm.verts.new(c + Vector((x * cy_ - y * sy_ + lean[0] * p[1], x * sy_ + y * cy_ + lean[1] * p[1], p[1]))))
        cols.append(col)
    for j in range(n):
        j2 = (j + 1) % n
        u0, u1 = j / n, (j + 1) / n
        for i in range(m - 1):
            p, q = prof[i], prof[i + 1]
            b.face([cols[j][i], cols[j2][i], cols[j2][i + 1], cols[j][i + 1]], p[2] if len(p) > 2 else key,
                   [(u0, p[0], c.z + p[1]), (u1, p[0], c.z + p[1]), (u1, q[0], c.z + q[1]), (u0, q[0], c.z + q[1])])


def torus(b, mat, R, r, key, n=20, m=8):
    """D 고리 물건(도넛) — mat(자리·기울기) 의 로컬 xy 평면에 놓인 원환(큰 반지름 R·관 반지름 r).
    key: 색 키 또는 `key(a, q)`(큰 둘레 각 a · 관 단면 각 q — 0 바깥, π/2 위)."""
    V = [[b.bm.verts.new(mat @ Vector(((R + r * math.cos(q)) * math.cos(a), (R + r * math.cos(q)) * math.sin(a), r * math.sin(q))))
          for q in (2 * math.pi * i / m for i in range(m))] for a in (2 * math.pi * j / n for j in range(n))]
    z4 = [(0, 0, 0)] * 4
    for j in range(n):
        for i in range(m):
            k = key(2 * math.pi * (j + 0.5) / n, 2 * math.pi * (i + 0.5) / m) if callable(key) else key
            b.face([V[j][i], V[(j + 1) % n][i], V[(j + 1) % n][(i + 1) % m], V[j][(i + 1) % m]], k, z4)


def round_k(th, w=1.0):
    """`revolve` 반지름 배율 — 머리 꼴 초타원 둘레(`surf`, 모서리가 각짐)를 그 높이에서 타원(앞뒤 KY)으로 편다. 둥근 물건(호박·
    도토리·케이크·주전자)을 머리 꼴 둘레로 돌리면 네모 상자로 보인다(루프#1364 첫 판). w: 0 = 머리 꼴 그대로(아랫단 — 머리에
    붙어야 함) · 1 = 원. 모서리에서 0.84 배로 줄어드니, 머리 높이 안에서 w=1 로 쓰는 점은 머리 반지름의 1.19 배 이상이어야 한다."""
    e = 2.0 / SUPER_N
    return 1.0 + w * ((abs(math.cos(th)) ** (2 * e) + abs(math.sin(th)) ** (2 * e)) ** -0.5 - 1.0)


def side_plate(b, outline, thick, c, yaw, key, inset=0.2):
    """세로로 선 판(돛·뚜껑·무지개 띠) — 외곽 (u, v) 의 u 는 yaw 방향(0 = 오른쪽 +X) 수평, v 는 위. 두께는 그 수직(앞 −Y 쪽,
    yaw=0 이면 앞)으로 c 에서 thick 만큼. `plate` 는 판 면을 법선으로 세우는데, 앞에서 보는 판은 법선이 앞이라 `frame` 의 위·옆
    축을 매번 따로 셈하던 것(#1365 이전 돛 `Rotation(90, X)`)을 한 곳에 모은다."""
    m = Matrix.Translation(c) @ Matrix.Rotation(yaw, 4, "Z") @ Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    b.plate(outline, thick, m, key, inset=inset)


# ---------------------------------------------------------------- B 챙 · C 캡 (루프#1367, 명세 6장 조각 6)
def brim(b, crown, width, lift=None, thick=0.012, keys=("inside", "brim", "brim"), drop=0.012, us=(0.35, 0.7)):
    """B 챙 — 아랫단(`seat`) 안쪽 면 → 챙 아랫면 → 챙 끝 → 챙 윗면 → 운두. 챙 모자끼리 꼴이 갈리는 칸은 넷뿐이라 한 함수에 둔다.

    width: 챙 폭(운두 밑 바깥에서) — 수 또는 width(th)(삼각·캡의 앞챙). lift(th, u): 챙 윗면 높이(u 0 = 운두 밑 · 1 = 끝,
    + 말림 · − 처짐). thick: 챙 두께 — 수 또는 thick(u)(UFO 접시처럼 안쪽이 두꺼운 렌즈). crown(th, z, rc): 운두 단면
    [(r, z, 색 키)] — 운두 밑(rc, z) 위에서 꼭대기 극(r=0)까지(`crown_dome`·`crown_box` 또는 행이 만든 것). keys: (안쪽, 챙 아래, 챙 위).
    게임은 얼굴 앞(두 눈 너비) 가장 낮은 꼭짓점을 눈썹선에 맞추므로(`HatBrimLift`) 앞챙을 처지게 하면 모자 전체가 그만큼 올라간다 —
    처짐은 옆·뒤에 두고 앞은 얕게."""
    k_in, k_under, k_top = keys
    lift = lift or (lambda th, u: 0.0)
    tk = thick if callable(thick) else (lambda u: thick)
    key = lambda k, u: k(u) if callable(k) else k

    def prof(th):
        z = seat(th, drop)
        ri = head_r(z, G)
        rc = ri + 0.02
        w = width(th) if callable(width) else width
        # 챙 윗면 곡선을 따라 두께만큼 법선 아래로 — 위로 말린 챙(수병·삼각)도 벽 두께가 0 이 되지 않는다.
        uu = (0.0,) + us + (1.0,)
        top = [(rc + w * u, z + lift(th, u)) for u in uu]
        under = []
        for i, u in enumerate(uu):
            (r0, z0), (r1, z1) = top[max(0, i - 1)], top[min(len(uu) - 1, i + 1)]
            d = math.hypot(r1 - r0, z1 - z0) or 1.0
            under.append((top[i][0] + (z1 - z0) / d * tk(u), top[i][1] - (r1 - r0) / d * tk(u)))
        pts = inner(z, k_in)
        pts.append((ri + 0.006, z - tk(0.0), key(k_under, 0.0)))
        pts += [(under[i][0], under[i][1], key(k_under, u)) for i, u in enumerate(uu) if i > 0]
        tr, tz = top[-1][0] - top[-2][0], top[-1][1] - top[-2][1]
        tl = math.hypot(tr, tz) or 1.0
        pts.append(((top[-1][0] + under[-1][0]) / 2 + 0.006 * tr / tl, (top[-1][1] + under[-1][1]) / 2 + 0.006 * tz / tl,
                    key(k_top, 1.0)))   # 챙 끝 — 곡선이 나아가던 쪽으로 둥글게
        pts += [(top[i][0], top[i][1], key(k_top, uu[i])) for i in range(len(uu) - 1, 0, -1)]
        return pts + crown(th, z, rc)
    b.revolve(prof)


def band_pts(band, key, z, rc):
    """운두 밑 띠 — band (색 키, 위끝 높이[, 아래끝 높이]). 아래끝을 주면 그 밑은 운두 색(세일러 흰 밑단 위 감색 띠)."""
    if len(band) > 2:
        return [(rc + 0.002, z + 0.004, key), (rc + 0.003, z + band[2], band[0]), (rc + 0.004, z + band[1], band[0])]
    return [(rc + 0.002, z + 0.004, band[0]), (rc + 0.004, z + band[1], band[0])]


def crown_dome(key, h, t=0.02, band=None):
    """둥근 운두 — 머리 윗면 + 틈 t 를 위로 h 만큼 부풀린 돔. key: 색 키 또는 key(th)(프로펠러 네 쪽). band: `band_pts`."""
    def f(th, z, rc):
        pts, z1 = [], z
        k = key(th) if callable(key) else key
        if band:
            pts += band_pts(band, k, z, rc)
            z1 = z + band[1]
        return pts + [(r, zz, k) for r, zz in dome(G + t, z1 + 0.006, 8, stretch=h)]
    return f


def cone_r(th, r0, z0, top, zz, p, round_from=None):
    """`crown_cone` 겉면 반지름 — 둘레 각 th·높이 zz 에서(장식을 원뿔 겉에 붙일 때 같은 식을 쓴다)."""
    s = min(1.0, max(0.0, (zz - z0) / (top - z0)))
    w = 0.0 if round_from is None else min(1.0, max(0.0, (zz - round_from) / (top - round_from)))
    return r0 * (1.0 - s ** p) * round_k(th, w)


def crown_cone(key, top, p=1.8, inset=0.005, band=None, n=6, round_from=None):
    """원뿔 운두(마법사) — 운두 밑(띠 위) 반지름 rc − inset 에서 절대 높이 top 의 꼭짓점까지 r = r0·(1 − s^p) 로 좁아진다
    (p > 1 이면 아래가 불룩한 원뿔). round_from: 이 높이부터 둘레를 원으로(`crown_box` 와 같음 — 위에서 네모 뿔로 보이지 않게).
    장식 자리는 `cone_r(th, rc − inset, z0, top, zz, p, round_from)` — z0 = 운두 밑 + 띠 + 0.008."""
    def f(th, z, rc):
        pts = band_pts(band, key, z, rc) if band else []
        z0 = z + (band[1] if band else 0.0) + 0.008
        r0 = rc - inset
        for k in range(n):
            zz = z0 + (top - z0) * k / n
            pts.append((cone_r(th, r0, z0, top, zz, p, round_from), zz, key))
        return pts + [(0.0, top, key)]
    return f


def crown_plate(key, r_top, z_top, z_cen, band=None):
    """넓은 윗판 운두(선장모) — 띠 위에서 바깥으로 벌어져 머리보다 넓은 납작한 윗판(반지름 r_top·테 높이 z_top, 둘 다 수 또는
    th 함수 — 앞으로 기울임)에 이르고, 윗면은 가운데 z_cen 으로 살짝 부푼다."""
    def f(th, z, rc):
        rt = r_top(th) if callable(r_top) else r_top
        zt = z_top(th) if callable(z_top) else z_top
        pts = band_pts(band, key, z, rc) if band else []
        z1 = z + (band[1] if band else 0.0)
        return pts + [(rc + 0.012, z1 + 0.012, key), (rt - 0.02, zt - 0.03, key), (rt + 0.008, zt - 0.012, key),
                      (rt + 0.002, zt + 0.008, key), (rt - 0.03, zt + 0.016, key),
                      (rt * 0.5, (zt + 0.016 + z_cen) / 2, key), (0.0, z_cen, key)]
    return f


def crown_box(key, top_, flare=0.0, waist=0.0, band=None, n=5, round_from=None):
    """곧은 운두(실크햇·수병 컵·세일러) — 운두 밑 rc 에서 절대 높이 top_(수 또는 th 함수) 까지 오르는 옆벽, 꼭대기 평평. flare: 꼭대기에서 벌어지는 양,
    waist: 허리(가운데 높이)에서 들어가는 양. 꼭대기는 머리 꼭대기 안쪽 면(CZ+HC+G) 위여야 안쪽 면이 뚫고 나오지 않는다(루프#1360).
    round_from: 이 높이부터 꼭대기까지 둘레를 머리 꼴 초타원 → 원으로 편다(`round_k` — 머리 위로 솟은 원통이 네모 상자로 보이지
    않게; 머리 높이 안에서는 모서리가 0.84 배로 줄어 머리 살에 닿으니 머리 어깨 위에서 시작)."""
    def f(th, z, rc):
        pts = band_pts(band, key, z, rc) if band else []
        z0 = z + (band[1] if band else 0.0)
        top = top_(th) if callable(top_) else top_

        def rk(zz):
            return 1.0 if round_from is None else round_k(th, min(1.0, max(0.0, (zz - round_from) / (top - round_from))))
        for k in range(n + 1):
            s = k / n
            zz = z0 + (top - z0) * s
            pts.append(((rc + flare * s * s - waist * math.sin(math.pi * s)) * rk(zz), zz, key))
        r_top = (rc + flare) * rk(top)
        return pts + [(r_top * 0.6, top + 0.004, key), (0.0, top + 0.006, key)]
    return f


def cap(b, crown, visor, span=math.radians(70), droop=0.02, keys=("inside", "visor", "visor"), drop=0.012):
    """C 캡 — 머리에 붙는 돔 + 앞챙 하나: 앞(TF) ±span 에서만 챙이 visor 까지(코사인 꼴로 줄며) 나오고, 나머지는 테만.
    droop: 앞챙 끝이 내려가는 양(작게 — 앞이 처지면 `HatBrimLift` 가 모자를 올린다)."""
    def width(th):
        d = abs(math.atan2(math.sin(th - TF), math.cos(th - TF)))
        return 0.006 + (visor - 0.006) * (math.cos(d / span * math.pi / 2) ** 0.6 if d < span else 0.0)
    brim(b, crown, width, lambda th, u: -droop * u * u * (width(th) / visor), thick=0.01, keys=keys, drop=drop)
