"""꾸미기 모자 +5 (M83-02, 루프#856) — 도토리·비옷·황금 왕관·해적 두건·별빛 마법사 모자를 절차적으로 만든다.

실행 (헤드리스만):
  HATS_TMP=<임시 폴더>/ ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      -P tools/blender/hats_more_make.py [-- MeadowAcorn ...]

머리 타원체·받침점·텍스처·내보내기는 `_Source/raw/Snare/Items/Hats/hats_v2_make.py`(스킨 조각 f)를 그대로 빌린다 —
그 파일의 `main()` 만 빼고 불러와 `HATS` 에 다섯 벌을 더한다. 그래서 `Socket_Head` 에 바로 앉고 폭 규칙이 같다.
이름·모양은 새것(레퍼런스 원본 이름·에셋을 쓰지 않는다).
"""
import math
import os

HATS_MAKE = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Hats/hats_v2_make.py"
_here = globals().get("__file__")
globals()["__file__"] = HATS_MAKE
_code = open(HATS_MAKE, encoding="utf-8").read()
assert _code.rstrip().endswith("main()"), "모자 틀 끝이 main() 이 아니다"
exec(compile(_code.rstrip()[:-len("main()")], HATS_MAKE, "exec"))
globals()["__file__"] = _here
# 루프#1360: 틀의 RAW 가 옛 raw/Varco 를 가리켜 몸(BODY)이 없고 내보낸 모자가 반입 폴더(raw/Snare, import_equipment_weapons.py)와
# 어긋난다 — 내보내기·검수 몸 경로를 반입 스크립트와 같은 raw/Snare 로 고정한다.
RAW = "~/UNREAL/Snarehaven/_Source/raw/Snare/"
HATS_DIR = RAW + "Items/Hats/"
BODY = RAW + "Characters/Player/standing_idle_1.glb"

# 보정(루프#857): 타원체(0.2401·0.2308·0.2226)는 실제 머리와 달랐다 — 게임 몸(UE 참조 자세)의 머리는 폭 ±27.5·깊이 −20~+27cm 의
# 네모 둥근 상자(옆이 곧고 윗면이 평평)라 모자가 옆 3cm·뒤 3cm·위 4cm 모자랐다. 게임에서 몸 머리 꼭짓점(z≥74) 구한 값으로
# 그걸 모두 감싸는 가장 작은 둥근 상자(윗면 얕은 돔 + 둘레 4차 초타원)로 바꾼다 — 틀의 head_r·dome·surf 가 이 전역을 읽는다.
# 좌표는 Blender(앞 −Y) — UE 참조 자세는 Y 가 뒤집혀 있다(머리 뼈 UE Y +2.963 = Blender −0.0296).
CX, CY, CZ = -0.0060, -0.035, 0.92
HA, HB, HC = 0.2725, 0.240, 0.100
KY = HB / HA
SUPER_N = 4.0


def surf(th, r, z):
    c, s = math.cos(th), math.sin(th)
    e = 2.0 / SUPER_N
    return Vector((CX + r * math.copysign(abs(c) ** e, c), CY + r * KY * math.copysign(abs(s) ** e, s), z))


def dome(g, z0, n, stretch=0.0):
    """z0 에서 꼭대기(극)까지 오르는 둥근 상자 윗면(+g) 곡선 n 점. 틀의 `dome` 은 호 길이로 똑같이 나눠, 윗면이 얕은 이 머리에서는
    옆벽→윗면 어깨의 휜 곳을 현이 3cm 가까이 깎아 머리 살이 삐져나왔다 — 여기선 타원 각도로 나눠 어깨에 점을 더 모은다."""
    a, c = HA + g, HC + g + stretch
    pts = []
    if z0 < CZ:
        pts.append((a, z0))
        steps = n - 2
        p0 = 0.0
    else:
        steps = n - 1
        p0 = math.asin(min(1.0, (z0 - CZ) / c))
    for k in range(steps + 1):
        p = p0 + (math.pi / 2 - p0) * k / steps
        pts.append((a * math.cos(p), CZ + c * math.sin(p)))
    pts[-1] = (0.0, pts[-1][1])
    return pts


def inner(z, key, n=5):
    """안쪽 오목면 — 틀은 n=3 도 썼는데 점 셋이면 옆벽·어깨를 현이 가로질러 머리 살이 안쪽 면 밖으로 나온다. 최소 5점."""
    return [(r, zz, key) for r, zz in reversed(dome(G, z, max(n, 5)))]


# 루프#1362 — 계열 틀(`seat`·`beanie` …, 명세 docs/specs/hat-families.md)을 같은 이름공간에 올린다.
_FAM = os.path.join(os.path.dirname(os.path.abspath(_here or HATS_MAKE)), "hat_families.py")
exec(compile(open(_FAM, encoding="utf-8").read(), _FAM, "exec"))


# 한 벌만 둘레 분할을 줄인다 — 안쪽 면을 5점으로 늘린 만큼 삼각형이 모자 한도 1,500 을 넘는 모자.
S_BY_HAT = {"MeadowPineapple": 28, "MeadowFrog": 28, "HarborWhale": 28, "MeadowSunflower": 24, "HarborHorned": 28,
            "HarborBeanie": 28, "MeadowDonut": 28, "HarborRainbow": 26, "MeadowCactus": 26,
            "MeadowBucket": 28, "MeadowTopHat": 28, "MeadowUfo": 28, "MeadowMushroom": 28, "MeadowCap": 28,
            "MeadowWizard": 28, "HarborJellyfish": 28, "HarborCaptain": 28}

MORE_ORDER = ["MeadowAcorn", "HarborSouwester", "MeadowCrown", "HarborBandana", "MeadowWizard",
              "MeadowSunflower", "HarborDixie", "HarborHorned", "MeadowMushroom", "MeadowTopHat",
              "MeadowCowboy", "MeadowBear", "MeadowCatEars", "HarborTricorn", "MeadowPropeller",
              "MeadowBeret", "HarborFez", "MeadowBunny", "MeadowPumpkin", "HarborShark",
              "MeadowLadybug", "HarborLighthouse", "MeadowFlowerpot", "MeadowCarrot", "HarborJellyfish",
              "HarborSailorCap", "MeadowTeapot", "MeadowOwl", "HarborCrab", "MeadowCake",
              "MeadowSnail", "HarborBuoy", "MeadowPineapple", "HarborStarfish", "MeadowRocket",
              "MeadowDuck", "MeadowBeehive", "HarborConch", "MeadowCactus", "MeadowUfo",
              "MeadowPenguin", "MeadowDonut", "MeadowMouse", "HarborIgloo", "HarborRainbow", "HarborTreasure",
              "MeadowFox", "MeadowPig", "HarborSailboat", "MeadowCheese", "MeadowDragon"]


def pattern_scales(U, V, base, seam):
    """파인애플 비늘(루프#1364) — 비스듬히 엇갈린 마름모 칸 + 칸 가운데 짙은 눈. U 는 둘레(0~1 한 바퀴)라 대각선 칸 수가 정수여야 이음매가 맞는다."""
    a, c = U * 12 + V * 5, U * 12 - V * 5
    fa, fc = a % 1, c % 1
    puff = (np.sin(np.pi * fa) * np.sin(np.pi * fc)) ** 0.5
    rgb = np.array(base)[None, None] * (0.8 + 0.2 * puff)[..., None]
    line = (np.minimum(fa, 1 - fa) < 0.05) | (np.minimum(fc, 1 - fc) < 0.05)
    rgb[line] = seam
    rgb[(np.abs(fa - 0.5) < 0.1) & (np.abs(fc - 0.5) < 0.1)] = seam
    return rgb, puff


def star_outline(s):
    return [((s if k % 2 == 0 else s * 0.45) * math.cos(math.pi / 2 + k * math.pi / 5),
             (s if k % 2 == 0 else s * 0.45) * math.sin(math.pi / 2 + k * math.pi / 5)) for k in range(10)]


def hat_meadow_acorn(b):
    """도토리 모자(일반) — D 물건(루프#1364): 머리를 감싼 비늘무늬 깍정이(위 가장자리 밝은 두툼한 테가 머리 밖으로 벌어짐) + 그 안에서
    위로 솟아 끝이 뾰족해지는 매끈한 갈색 열매 + 꼭대기 짧은 꼭지. 옛 꼴은 깍정이 돔 하나라 부표·새싹과 같은 실루엣이었다."""
    def prof(th):
        z = seat(th, 0.004)
        ri, ro = head_r(z, G), head_r(z, G + 0.03)
        pts = inner(z, "inside")
        k1, k = round_k(th, 0.5), round_k(th)
        pts += [(ri + 0.006, z - 0.012, "cap"), (ro + 0.012, z - 0.008, "cap"), ((ro + 0.03) * k1, z + 0.05, "cap"),
                ((ro + 0.05) * k, z + 0.1, "cap"), ((ro + 0.062) * k, z + 0.122, "rim"), ((ro + 0.044) * k, z + 0.142, "rim"),
                ((ro + 0.004) * k, z + 0.146, "nut")]
        zb, zt, rb = z + 0.146, 1.37, (ro + 0.004) * k
        for i in range(1, 9):   # 어깨는 둥글고 끝으로 갈수록 곧게 좁아지는 열매 — (1−s)(1+1.5s)
            s = i / 8.0
            pts.append((0.0 if i == 8 else rb * min(1.04, (1 - s) * (1 + 1.5 * s)), zb + (zt - zb) * s, "nut"))
        return pts
    b.revolve(prof)
    b.blob(Matrix.Translation((CX, CY, 1.375)) @ Matrix.Diagonal((0.016, 0.016, 0.03, 1)), "stem", us=6, vs=4)


def hat_harbor_souwester(b):
    """비옷 모자(일반) — B 챙(루프#1369, 꼴 그대로): 둥근 노란 운두 + 뒤로 갈수록 길고 깊게 처지는 챙 + 남색 띠 + 앞 흰 단추."""
    def back(th):
        return max(0.0, -fw(th))          # 뒤쪽일수록 챙이 길다
    brim(b, crown_dome("yellow", 0.02, band=("band", 0.05, 0.01)), lambda th: 0.04 + 0.1 * back(th),
         lambda th, u: -(0.012 + 0.07 * back(th)) * u, thick=0.014, keys=("inside", "yellow", "yellow"), drop=0.0)
    p, n = dome_point(G + 0.02, TF, zr(TF) + 0.03)
    b.blob(Matrix.Translation(p + n * 0.006) @ Matrix.Diagonal((0.016, 0.016, 0.012, 1)), "button", us=6, vs=4)


def hat_meadow_crown(b):
    """황금 왕관(희귀) — 금 띠 + 뾰족한 여섯 갈래, 갈래 끝마다 작은 보석, 앞 갈래는 큼직한 붉은 보석."""
    t = 0.022

    def prof(th):
        z = zr(th) + 0.02
        ri, ro = head_r(z, G), head_r(z, G + t)
        return [(ri + 0.004, z - 0.012, "gold"), (ro + 0.006, z - 0.012, "gold"), (ro + 0.012, z + 0.02, "gold"),
                (ro + 0.012, z + 0.06, "gold"), (ri + 0.004, z + 0.06, "gold")]
    b.revolve(prof)
    for k in range(6):
        th = TF + 2 * math.pi * k / 6
        z0 = zr(th) + 0.02 + 0.06
        r0 = head_r(z0 - 0.02, G + t) + 0.012
        tall = 0.07 if k == 0 else 0.055
        b.blob(Matrix.Translation(surf(th, r0 - 0.002, z0 + tall * 0.35)) @ Matrix.Rotation(th, 4, "Z") @ Matrix.Diagonal((0.012, 0.03, tall * 0.8, 1)),
               "gold", us=6, vs=4)
        b.blob(Matrix.Translation(surf(th, r0 - 0.002, z0 + tall + 0.004)) @ Matrix.Diagonal((0.022, 0.022, 0.026, 1)),
               "ruby" if k == 0 else "gem", us=6, vs=4)
    for k in range(12):                    # 띠 둘레의 작은 알갱이 장식
        th = TF + 2 * math.pi * (k + 0.5) / 12
        z0 = zr(th) + 0.02 + 0.03
        p, n = dome_point(G + t, th, z0 + 0.0)
        b.blob(Matrix.Translation(p + n * 0.014) @ Matrix.Diagonal((0.011, 0.011, 0.011, 1)), "gem" if k % 2 else "pearl", us=5, vs=3)


def hat_harbor_bandana(b):
    """해적 두건(희귀) — A 비니(루프#1363): 머리에 딱 붙은 붉은 두건 + 앞 해골 배지 + 뒤통수 매듭과 늘어진 끝자락 둘."""
    t = 0.014
    beanie(b, "red", t, cuff_h=0.02, swell=0.02, drop=0.03)
    p, n = dome_point(G + t, TF, zr(TF) + 0.02)     # 해골 — 흰 동그라미 + 검은 눈 둘
    m = frame(p - n * 0.002, n)
    b.plate(circle(0.038, 14), 0.008, m, "white")
    for sgn in (-1, 1):
        b.plate(circle(0.01, 8), 0.005, m @ Matrix.Translation((sgn * 0.016, 0.004, 0.008)), "black")
    b.plate([(-0.008, -0.034), (0.008, -0.034), (0.006, -0.05), (-0.006, -0.05)], 0.006, m @ Matrix.Translation((0, 0, 0.006)), "white")
    knot, kn = dome_point(G + t, -TF, zr(-TF) - 0.01)   # 뒤 매듭
    b.blob(Matrix.Translation(knot + kn * 0.014) @ Matrix.Diagonal((0.03, 0.026, 0.026, 1)), "red", us=8, vs=5)
    for sgn in (-1, 1):
        m = frame(knot + kn * 0.03, Vector((sgn * 0.45, 0.9, -0.45)).normalized(), up=Vector((0, 0, 1)))
        b.plate(leaf_outline(0.11, 0.05, n=5), 0.01, m @ Matrix.Rotation(math.radians(sgn * 90), 4, "Z"), "red")


def hat_meadow_wizard(b):
    """별빛 마법사 모자(에픽) — B 챙(루프#1369, 꼴 그대로): 남색 뾰족 원뿔 운두(`crown_cone`) + 넓게 처진 챙 + 금 띠 + 원뿔 겉 금빛 별 넷."""
    drop, band, top, pw, rf = 0.01, 0.05, 1.27, 1.8, CZ + 0.05
    brim(b, crown_cone("cone", top, p=pw, band=("band", band), round_from=rf), 0.105, lambda th, u: -0.042 * u ** 1.2, thick=0.016,
         keys=("inside", "navy", "navy"), drop=drop)
    for th, zz, s in ((TF, 1.02, 0.045), (TF + math.radians(70), 1.07, 0.034), (TF - math.radians(70), 1.0, 0.03),
                      (TF + math.radians(140), 1.05, 0.032)):
        z = seat(th, drop)
        r = cone_r(th, head_r(z, G) + 0.02 - 0.005, z + band + 0.008, top, zz, pw, rf) + 0.004   # 원뿔 겉면 바로 위
        b.plate(star_outline(s), 0.01, frame(surf(th, r, zz), radial(th, 0.5)), "gold")
    zb = seat(TF, drop) + 0.025                  # 금 띠 한가운데 높이
    p = surf(TF, head_r(zb, G) + 0.02 + 0.016, zb)   # 띠 바깥면 위(머리 속이 아니다)
    b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.02, 0.02, 0.012, 1)), "gold", us=6, vs=4)


def hat_meadow_sunflower(b):
    """해바라기 모자(일반) — B 챙(루프#1369, 꼴 그대로): 씨앗 갈색 둥근 운두 + 초록 테·좁은 챙 + 그 둘레에 노란 꽃잎 열넷이 해처럼
    퍼진 꽃잎 챙 + 앞 초록 잎 하나."""
    t, drop = 0.02, 0.006
    brim(b, crown_dome("seed", 0.03, t=t, band=("leaf", 0.022)), 0.012, thick=0.012, keys=("inside", "leaf", "leaf"), drop=drop, us=())
    for k in range(14):
        th = TF + 2 * math.pi * (k + 0.5) / 14
        z = zr(th) + 0.002
        p = surf(th, head_r(z, G + t) + 0.02, z)
        b.plate(leaf_outline(0.15, 0.09, n=5), 0.014,
                Matrix.Translation(p) @ Matrix.Rotation(th, 4, "Z") @ Matrix.Rotation(-0.25, 4, "Y"), "petal")
    p, n = dome_point(G + t, TF, zr(TF) + 0.01)
    b.plate(leaf_outline(0.07, 0.035, n=5), 0.008,
            frame(p + n * 0.004, n) @ Matrix.Rotation(math.radians(-90), 4, "Z") @ Matrix.Translation((0, 0, 0)), "leaf")


def hat_harbor_dixie(b):
    """수병 모자(일반) — B 챙(루프#1367): 흰 낮은 둥근 운두 + 운두 높이 2/3 까지 위로 말려 올라가며 벌어진 챙(앞에서 위가 넓은
    컵 꼴 — 다른 챙 모자는 전부 아래가 넓다) + 챙 끝 남색 띠 + 앞 남색 닻 매듭."""
    brim(b, crown_dome("white", 0.0), 0.06, lambda th, u: 0.13 * u ** 1.6, thick=0.012,
         keys=("inside", "white", lambda u: "navy" if u > 0.8 else "white"), drop=0.014)
    zb = seat(TF, 0.014) + 0.07
    p = surf(TF, head_r(zb, G) + 0.02 + 0.035, zb)
    b.blob(Matrix.Translation(p + Vector((0, -0.012, 0))) @ Matrix.Diagonal((0.034, 0.012, 0.02, 1)), "navy", us=6, vs=4)
    b.blob(Matrix.Translation(p + Vector((0, -0.018, -0.03))) @ Matrix.Diagonal((0.012, 0.01, 0.026, 1)), "navy", us=5, vs=3)


def hat_harbor_horned(b):
    """뿔 투구(희귀) — A 비니(루프#1363): 철빛 둥근 투구 + 어두운 이마 띠와 못 + 양옆으로 휘어 오른 아이보리 뿔 둘."""
    t = 0.022
    beanie(b, "steel", t, cuff="band", cuff_h=0.034, swell=0.02, drop=0.018)
    zb = zr(0.0) + 0.0
    for sgn in (-1, 1):                   # 뿔 — 굵은 뿌리에서 바깥·위로 휘며 가늘어지는 관 셋
        r0 = HA + G + t + 0.004
        pts = [Vector((CX + sgn * (r0 + 0.012 + 0.11 * u ** 0.7), CY, zb + 0.01 + 0.24 * u ** 1.7)) for u in
               [i / 9.0 for i in range(10)]]
        b.tube(pts[:4], 0.042, "horn", sides=6)
        b.tube(pts[3:7], 0.03, "horn", sides=6)
        b.tube(pts[6:], 0.018, "horn", sides=6)
    for k in range(8):                    # 띠의 못
        th = TF + 2 * math.pi * (k + 0.5) / 8
        p, n = dome_point(G + t, th, seat(th, 0.018) + 0.016)
        b.blob(Matrix.Translation(p + n * 0.014) @ Matrix.Diagonal((0.009, 0.009, 0.009, 1)), "gold", us=5, vs=3)


def hat_meadow_mushroom(b):
    """버섯 모자(희귀) — B 챙(루프#1367): 둥글게 부푼 붉은 갓 운두 + 운두보다 넓게 펼쳐 끝이 아래로 둥글게 말려 내려간 갓 가장자리(우산 꼴) +
    안쪽 크림색 주름 + 흰 점 일곱(갓 위로 도드라짐) + 갓 위에 비스듬히 돋은 아기 버섯 하나."""
    h, drop = 0.06, 0.012
    brim(b, crown_dome("red", h), 0.12, lambda th, u: (0.02 * u - 0.13 * u ** 3) * (1 - 0.6 * max(0.0, fw(th)) ** 2), thick=0.016,
         keys=("gills", "gills", "red"), drop=drop)
    for dth, k, s in ((0, 2, 0.04), (50, 4, 0.032), (-50, 3, 0.036), (105, 3, 0.036), (-105, 5, 0.03), (160, 4, 0.036), (-160, 2, 0.034)):
        th = TF + math.radians(dth)
        pts = dome(G + 0.02, seat(th, drop) + 0.006, 8, stretch=h)   # 갓 윗면과 같은 곡선 위에 얹는다
        (r0, z0), (r1, z1) = pts[k - 1], pts[k + 1]
        nr, nz = z1 - z0, -(r1 - r0)
        nl = math.hypot(nr, nz)
        n = (radial(th) * (nr / nl) + Vector((0, 0, nz / nl))).normalized()
        p = surf(th, pts[k][0], pts[k][1])
        b.blob(frame(p + n * 0.004, n) @ Matrix.Diagonal((s, s, 0.016, 1)), "dot", us=6, vs=4)
    # 갓 위 오른쪽 앞에 비스듬히 돋은 아기 버섯 — 둥근 갓 모자(상인·딸기·마법사 챙)와 그림자를 가른다.
    th = TF + math.radians(55)
    pts = dome(G + 0.02, seat(th, drop) + 0.006, 8, stretch=h)
    p = surf(th, pts[5][0], pts[5][1])
    tip = p + radial(th) * 0.05 + Vector((0, 0, 0.1))
    b.tube([p - Vector((0, 0, 0.01)), p + radial(th) * 0.02 + Vector((0, 0, 0.06)), tip], 0.016, "gills", sides=6)
    m = frame(tip, (radial(th) * 0.35 + Vector((0, 0, 1))).normalized())
    b.blob(m @ Matrix.Diagonal((0.065, 0.065, 0.04, 1)), "red", us=8, vs=5)
    b.blob(m @ Matrix.Translation((0.02, -0.02, 0.034)) @ Matrix.Diagonal((0.014, 0.014, 0.008, 1)), "dot", us=5, vs=3)


def hat_meadow_tophat(b):
    """꽃 실크햇(에픽) — B 챙(루프#1367): 머리 높이만큼 솟은 검은 원통 운두(허리 살짝 들어가고 꼭대기 벌어짐) + 좁고 옆이 살짝
    말린 챙 + 보라 띠 + 띠 앞 분홍 꽃·초록 잎 둘. 윗면은 머리 꼭대기 안쪽 면 위(루프#1360), 머리 어깨 위로는 둘레를 원으로."""
    top = CZ + HC + G + 0.24
    band = 0.05
    brim(b, crown_box("black", top, flare=0.03, waist=0.014, band=("band", band), round_from=CZ + 0.05), 0.055,
         lambda th, u: 0.035 * u * u * side(th) ** 2, thick=0.01, keys=("inside", "black", "black"))
    zb = seat(TF) + band / 2
    c = surf(TF, head_r(zb, G) + 0.02 + 0.03, zb)
    for sgn in (-1, 1):
        b.plate(leaf_outline(0.07, 0.03, n=5), 0.008,
                frame(c + Vector((sgn * 0.03, -0.004, -0.004)), Vector((sgn * 0.5, -1.0, 0.0))) @ Matrix.Rotation(math.radians(0 if sgn > 0 else 180), 4, "Z"), "leaf")
    for k in range(5):
        a = math.radians(90 + 72 * k)
        b.blob(Matrix.Translation(c + Vector((0.022 * math.cos(a), -0.008, 0.022 * math.sin(a)))) @ Matrix.Diagonal((0.017, 0.01, 0.017, 1)),
               "pink", us=6, vs=4)
    b.blob(Matrix.Translation(c + Vector((0, -0.016, 0))) @ Matrix.Diagonal((0.012, 0.01, 0.012, 1)), "sun", us=6, vs=4)


def hat_meadow_cowboy(b):
    """카우보이 모자(일반) — B 챙(루프#1367): 옆이 높이 말려 올라간 넓은 챙(앞에서 U 꼴, 앞뒤는 살짝 처짐) + 가운데 앞뒤로 꺼진
    높은 운두(앞에서 꼭대기 골이 보임) + 짙은 띠 + 앞 금빛 버클."""
    h = 0.11
    dome_c = crown_dome("brown", h, band=("band", 0.035))

    def crown(th, z, rc):
        out = []
        for r, zz, k in dome_c(th, z, rc):
            x = r * math.cos(th)                                   # 옆 거리 — 가운데 골은 앞뒤로 길다
            dent = 0.05 * math.exp(-(x / 0.075) ** 2) * sstep(z + 0.09, z + 0.2, zz)
            out.append((r, zz - dent, k))
        return out
    brim(b, crown, lambda th: 0.15 + 0.03 * fw(th) ** 2,
         lambda th, u: u * u * (0.11 * side(th) ** 2 - 0.022 * fw(th) ** 2 * (0.25 if fw(th) > 0 else 1.0)),
         thick=0.012, keys=("inside", "brown", "brown"))
    zb = seat(TF) + 0.018
    p = surf(TF, head_r(zb, G) + 0.02 + 0.008, zb)
    b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.026, 0.01, 0.018, 1)), "gold", us=6, vs=4)


def hat_meadow_bear(b):
    """곰 모자(일반) — A 비니(루프#1362): 갈색 털 껍질 + 옆 위로 크게 솟은 두툼한 둥근 귀 둘(안쪽 살구색) + 앞으로 나온 살구색 주둥이·검은 코."""
    top = beanie(b, "fur")
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.2, CY + 0.02
        c = Vector((x, y, ear_base(x, y) + 0.06))
        b.blob(Matrix.Translation(c) @ Matrix.Diagonal((0.1, 0.07, 0.1, 1)), "fur", us=10, vs=6)
        b.blob(Matrix.Translation(c + Vector((sgn * -0.006, -0.058, 0.0))) @ Matrix.Diagonal((0.064, 0.02, 0.062, 1)), "ear", us=8, vs=4)
    p, n = dome_point(G + 0.02, TF, seat(TF) + 0.07)
    b.blob(frame(p + n * 0.03, n) @ Matrix.Diagonal((0.075, 0.055, 0.05, 1)), "ear", us=8, vs=5)
    b.blob(frame(p + n * 0.078, n) @ Matrix.Translation((0, 0.018, 0)) @ Matrix.Diagonal((0.03, 0.022, 0.018, 1)), "nose", us=6, vs=4)
def hat_meadow_catears(b):
    """고양이 귀 모자(희귀) — A 비니(루프#1362): 주황 줄무늬 껍질 + 곧게 선 뾰족 삼각뿔 귀 둘(안쪽 분홍) + 이마의 어두운 줄 셋."""
    stripe = lambda th: "stripe" if int(((th - TF) % (2 * math.pi)) / (2 * math.pi) * 12) % 2 else "fur"
    top = beanie(b, stripe, t=0.018, cuff="rim", cuff_h=0.022)
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.15, CY + 0.0
        z0 = ear_base(x, y, t=0.018) - 0.03
        _cone(b, (x, y, z0), (x + sgn * 0.04, y - 0.01, z0 + 0.27), 0.1, "fur", squash=0.6)
        _cone(b, (x, y - 0.05, z0 + 0.03), (x + sgn * 0.036, y - 0.044, z0 + 0.2), 0.056, "ear", squash=0.25)
    zb = seat(TF) + 0.05
    for dth in (-14, 0, 14):
        p, n = dome_point(G + 0.018, TF + math.radians(dth), zb)
        b.plate([(-0.006, -0.026), (0.006, -0.026), (0.004, 0.026), (-0.004, 0.026)], 0.006,
                frame(p - n * 0.002, n), "stripe")
def hat_harbor_tricorn(b):
    """삼각 모자(희귀) — B 챙(루프#1367): 검은 운두 + 위에서 보면 세모인 챙 — 앞·뒤좌·뒤우 세 모서리가 길게 뻗어 운두 높이까지
    솟고(앞에서 양옆 뿔 둘), 모서리 사이 벽은 낮게 접힘 + 챙 끝 금테 + 앞 모서리 붉은 단추·흰 깃털."""
    def corner(th):
        return ((1 + math.cos(3 * (th - TF))) / 2) ** 3
    brim(b, crown_dome("black", 0.045), lambda th: 0.05 + 0.11 * corner(th),
         lambda th, u: u ** 1.5 * (0.035 + 0.15 * corner(th)), thick=0.012,
         keys=("inside", "black", lambda u: "gold" if u > 0.8 else "black"), drop=0.01)
    zb = seat(TF, 0.01) + 0.11
    p = surf(TF, head_r(seat(TF, 0.01), G) + 0.02 + 0.15, zb)
    b.blob(Matrix.Translation(p + Vector((0, 0.01, 0))) @ Matrix.Diagonal((0.02, 0.014, 0.02, 1)), "ruby", us=6, vs=4)
    b.plate(leaf_outline(0.16, 0.05, n=5), 0.008,
            Matrix.Translation(p + Vector((0.012, 0.0, 0.01))) @ Matrix.Rotation(math.radians(55), 4, "Z") @ Matrix.Rotation(math.radians(-30), 4, "Y"), "plume")


def hat_meadow_propeller(b):
    """프로펠러 모자(에픽) — C 캡(루프#1367): 빨강·노랑·파랑·초록 네 쪽 둥근 캡 + 흰 앞챙 + 꼭대기 높은 기둥 위 긴 날개 둘
    (45° — 앞·옆 그림자 둘 다에 날개가 보임)."""
    t = 0.016
    cap(b, crown_dome(lambda th: "c%d" % (int(((th - TF + math.pi / 4) % (2 * math.pi)) / (math.pi / 2)) % 4), 0.04, t=t),
        0.1, droop=0.012, keys=("inside", "white", "white"), drop=0.02)
    top = CZ + HC + G + t + 0.04
    b.tube([(CX, CY, top - 0.01), (CX, CY, top + 0.09)], 0.008, "white", sides=6)
    b.blob(Matrix.Translation((CX, CY, top + 0.03)) @ Matrix.Diagonal((0.022, 0.022, 0.016, 1)), "white", us=6, vs=4)
    b.blob(Matrix.Translation((CX, CY, top + 0.09)) @ Matrix.Diagonal((0.016, 0.016, 0.012, 1)), "white", us=6, vs=4)
    for ang, key in ((45, "c0"), (225, "c2")):
        b.plate(leaf_outline(0.26, 0.06, n=5), 0.008,
                Matrix.Translation((CX, CY, top + 0.09)) @ Matrix.Rotation(math.radians(ang), 4, "Z") @ Matrix.Rotation(math.radians(-8), 4, "Y"), key)


def hat_meadow_beret(b):
    """베레모(일반) — C 캡 계열의 앞챙 없는 납작 원판(루프#1368): 와인색 원판이 오른쪽 뒤(20°)로 크게 기울어 처지고 반대쪽은 머리에
    붙는다 + 안쪽 가는 띠 + 꼭대기 짧은 꼭지. 옛 꼴은 사방 같은 폭으로 부푼 원판이라 채굴 안전모와 0.861 — 한쪽으로 기운 원판이
    앞·옆 그림자 둘 다에서 비대칭으로 보이게(기울기 쪽을 옆 정면이 아니라 뒤로 20° 돌림)."""
    th_t = math.radians(20)                              # 기운 쪽 — 오른쪽(0)에서 뒤(+Y)로 20°
    zc = CZ + HC + G + 0.055                             # 원판 윗면 가운데(극) — 모든 둘레 같은 높이

    def prof(th):
        z = seat(th, 0.01)
        ri = head_r(z, G)
        rc = ri + 0.02
        c = math.cos(th - th_t)
        k = max(0.0, c) ** 1.5                           # 기운 쪽일수록 1
        over = 0.03 + 0.11 * k                           # 원판이 띠 밖으로 나온 양
        dz = 0.03 * max(0.0, -c) - 0.06 * k              # 기운 쪽은 처지고 반대쪽은 머리에 붙어 오름 — 원판 면이 기운다
        R, zr_ = rc + over, z + 0.03 + dz
        # 원판 둘레를 머리 꼴 초타원 → 원으로(`round_k`) — 위에서 네모 상자로 보이지 않게. 줄어든 둘레가 띠 + 3cm 안으로는 못 들어감.
        f = round_k(th)
        w = 1.0 if f >= 1.0 else max(0.0, min(1.0, (1 - (rc + 0.03) / R) / (1 - f)))
        rk = round_k(th, w)
        pts = inner(z, "inside") + [
            (ri + 0.006, z - 0.012, "band"), (rc + 0.004, z - 0.01, "band"), (rc + 0.006, z + 0.016, "band"),
            (rc + over * 0.6 * rk, max(z + 0.012, zr_ - 0.012), "wine"), (R * rk, zr_, "wine"), ((R + 0.004) * rk, zr_ + 0.018, "wine")]
        r0, z0 = (R - 0.012) * rk, zr_ + 0.034
        pts.append((r0, z0, "wine"))
        for i in range(1, 7):                            # 윗면 — 띠 끝에서 가운데로 납작하게, 머리 윗면 + 2.4cm 아래로는 안 내려감
            s_ = i / 7
            r = r0 * (1 - s_)
            zh = CZ + (HC + G + 0.024) * math.sqrt(max(0.0, 1 - (r / (HA + G + 0.024)) ** 2))
            pts.append((r, max(z0 + (zc - z0) * (1 - (1 - s_) ** 2), zh), "wine"))
        return pts + [(0.0, zc, "wine")]
    b.revolve(prof)
    b.blob(Matrix.Translation((CX, CY, zc + 0.004)) @ Matrix.Diagonal((0.014, 0.014, 0.018, 1)), "stem", us=6, vs=4)


def hat_harbor_fez(b):
    """페즈(일반) — D 물건(루프#1365): 아랫단은 머리 꼴로 붙고 위로 갈수록 둥글게 좁아지는 높은 붉은 잘린 원뿔 + 평평한 윗면 가운데
    검은 단추 + 거기서 옆으로 늘어진 금빛 술. 옛 꼴은 머리 폭 낮은 원통이라 채굴 모자와 같은 실루엣이었다."""
    zt = 1.17
    rt = 0.18

    def prof(th):
        z = seat(th, 0.012)
        ri, ro = head_r(z, G), head_r(z, G + 0.02)
        pts = inner(z, "inside") + [(ri + 0.006, z - 0.012, "red"), (ro + 0.012, z - 0.01, "band"), (ro + 0.014, z + 0.025, "red")]
        r0, z1 = ro + 0.014, z + 0.025
        for i in range(1, 6):   # 곧은 옆선 — 머리 꼴(w=0) → 원(w=1), 머리 살보다 늘 2cm 밖
            s = i / 5.0
            zz = z1 + (zt - z1) * s
            r = (r0 * (1 - s) + rt * s) * round_k(th, s)
            pts.append((max(r, head_r(zz, G + 0.02)), zz, "red"))
        pts += [(rt * 0.92 * round_k(th), zt + 0.006, "red"), (0.0, zt + 0.008, "red")]
        return pts
    b.revolve(prof)
    top = zt + 0.008
    b.blob(Matrix.Translation((CX, CY, top + 0.004)) @ Matrix.Diagonal((0.022, 0.022, 0.012, 1)), "black", us=6, vs=4)
    b.tube([(CX, CY, top + 0.006), (CX + 0.09, CY, top + 0.012), (CX + 0.16, CY, top - 0.01), (CX + 0.19, CY, top - 0.08),
            (CX + 0.2, CY, top - 0.15)], 0.008, "gold", sides=5)
    b.blob(Matrix.Translation((CX + 0.2, CY, top - 0.19)) @ Matrix.Diagonal((0.022, 0.022, 0.05, 1)), "gold", us=6, vs=4)


def hat_meadow_bunny(b):
    """토끼 모자(희귀) — A 비니(루프#1362): 하얀 털 껍질 + 머리 높이의 두 배로 곧게 선 긴 귀 둘(안쪽 분홍, 한쪽은 끝이 앞으로 꺾임) + 이마의 분홍 코."""
    beanie(b, "fur")
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.09, CY + 0.02
        z0 = ear_base(x, y)
        m = Matrix.Translation((x, y, z0 + 0.2)) @ Matrix.Rotation(math.radians(sgn * -10), 4, "Y")
        b.blob(m @ Matrix.Diagonal((0.058, 0.036, 0.23, 1)), "fur", us=8, vs=7)
        b.blob(m @ Matrix.Translation((0, -0.028, 0.01)) @ Matrix.Diagonal((0.032, 0.012, 0.17, 1)), "ear", us=8, vs=5)
    p, n = dome_point(G + 0.02, TF, seat(TF) + 0.08)
    b.blob(frame(p + n * 0.006, n) @ Matrix.Diagonal((0.024, 0.017, 0.014, 1)), "ear", us=6, vs=4)
def hat_meadow_pumpkin(b):
    """호박 모자(희귀) — D 물건(루프#1364): 머리를 통째로 넣은 납작하고 골 깊은 호박(허리가 테보다 머리 밖으로 크게 불룩, 윗면은 꼭지
    둘레로 오목) + 갈색 꼭지 + 초록 잎 하나와 덩굴. 옛 꼴은 머리 돔에 골만 얕게 판 것이라 베레와 같은 실루엣이었다."""
    zc, a, c = 0.99, 0.405, 0.2

    def prof(th):
        z = seat(th, 0.01)
        ri, ro = head_r(z, G), head_r(z, G + 0.03)
        rib = 1.0 + 0.055 * (1.0 + math.cos(8 * th))
        pts = inner(z, "inside") + [(ri + 0.006, z - 0.012, "orange"), (ro + 0.014, z - 0.008, "orange")]
        p0 = math.asin(max(-1.0, (z + 0.01 - zc) / c))
        for k in range(1, 11):
            p = p0 + (math.pi / 2 - p0) * k / 11
            r = max(ro + 0.014, a * math.cos(p)) if p < 0 else a * math.cos(p)   # 아래 반만 테 밖으로 — 위 반은 꼭지로 좁아짐
            w = min(1.0, (r - ro - 0.014) / (a - ro - 0.014)) if k < 6 else 1.0   # 골은 테에서 0 · 허리부터 꼭지까지 그대로
            pts.append((r * (1 + (rib - 1) * w) * round_k(th, min(1.0, (k - 1) / 2.0)), zc + c * math.sin(p), "orange"))
        pts += [(0.06, zc + c - 0.02, "orange"), (0.0, zc + c - 0.035, "orange")]
        return pts
    b.revolve(prof)
    top = zc + c - 0.035
    b.tube([(CX, CY, top - 0.02), (CX, CY, top + 0.08), (CX + 0.04, CY, top + 0.16)], 0.034, "stem", sides=6)
    b.plate(leaf_outline(0.2, 0.12, n=5), 0.008,
            Matrix.Translation((CX + 0.02, CY + 0.01, top + 0.03)) @ Matrix.Rotation(math.radians(40), 4, "Z") @ Matrix.Rotation(math.radians(-35), 4, "Y"), "leaf")
    b.tube([(CX - 0.012, CY, top + 0.03), (CX - 0.08, CY - 0.03, top + 0.04), (CX - 0.12, CY - 0.03, top + 0.09), (CX - 0.08, CY - 0.03, top + 0.11)],
           0.007, "leaf", sides=4)


def hat_harbor_shark(b):
    """상어 모자(에픽) — A 비니(루프#1363): 쇳빛 껍질(흰 배 테) + 꼭대기에서 뒤로 기운 높은 등지느러미(앞뒤로 긴 판) + 뒤통수에서 뒤로 뻗은 초승달 꼬리지느러미 + 옆 아래로 뻗은 가슴지느러미 둘 + 이마 흰 눈 둘 + 테 위 흰 이빨."""
    t = 0.02
    top = beanie(b, "gray", t, cuff="belly", cuff_h=0.036, swell=0.03)
    spin = Matrix.Rotation(math.radians(90), 4, "Z") @ Matrix.Rotation(math.radians(90), 4, "X")   # 판 x → 뒤(+Y) · y → 위
    m = Matrix.Translation((CX + 0.016, CY - 0.02, top - 0.04)) @ spin
    b.plate([(-0.13, 0.0), (0.11, 0.0), (0.09, 0.06), (0.17, 0.29), (0.05, 0.22), (-0.06, 0.1)], 0.032, m, "gray")
    q, k = dome_point(G + t, -TF, CZ - 0.02)
    m = Matrix.Translation((CX + 0.014, q.y - 0.03, q.z)) @ spin
    b.plate([(0.0, -0.03), (0.08, -0.03), (0.16, -0.12), (0.12, 0.0), (0.18, 0.15), (0.08, 0.04), (0.0, 0.03)], 0.028, m, "gray")
    for sgn in (-1, 1):
        th = TF + sgn * math.radians(80)
        q, k = dome_point(G + t, th, seat(th) + 0.06)
        _cone(b, q - k * 0.01, q + Vector((sgn * 0.13, 0.05, -0.08)), 0.06, "gray", squash=0.35, sides=5)
    zr0 = seat(TF)
    for sgn in (-1, 1):
        p, n = dome_point(G + t, TF + sgn * math.radians(26), zr0 + 0.085)
        b.blob(frame(p + n * 0.004, n) @ Matrix.Diagonal((0.026, 0.026, 0.012, 1)), "white", us=6, vs=4)
        b.blob(frame(p + n * 0.013, n) @ Matrix.Diagonal((0.013, 0.013, 0.008, 1)), "black", us=6, vs=4)
    for dth in (-30, -15, 0, 15, 30):
        p, n = dome_point(G + t, TF + math.radians(dth), zr0 + 0.022)
        b.plate([(-0.012, 0.012), (0.012, 0.012), (0.0, -0.028)], 0.006, frame(p + n * 0.004, n), "white")


def hat_meadow_ladybug(b):
    """무당벌레 모자(일반) — A 비니(루프#1363): 빨간 껍질(검은 테) + 앞이마 검은 머리판 + 까만 점 여섯 + 이마에서 V 자로 머리 폭 밖까지 벌어진 굵은 더듬이 둘(끝에 큰 공)."""
    t = 0.02
    top = beanie(b, "red", t, cuff="black", cuff_h=0.02, swell=0.03)
    zr0 = seat(TF)
    p, n = dome_point(G + t, TF, zr0 + 0.05)
    b.blob(frame(p, n) @ Matrix.Diagonal((0.15, 0.055, 0.02, 1)), "black", us=10, vs=5)
    for dth, dz in ((0, 0.13), (-52, 0.075), (52, 0.075), (-100, 0.1), (100, 0.1), (180, 0.12)):
        p, n = dome_point(G + t, TF + math.radians(dth), zr0 + dz)
        b.blob(frame(p + n * 0.003, n) @ Matrix.Diagonal((0.034, 0.034, 0.012, 1)), "black", us=6, vs=4)
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.05, CY - 0.08
        z0 = ear_base(x, y, t) - 0.01
        pts = [(x, y, z0), (x + sgn * 0.05, y - 0.03, z0 + 0.08), (x + sgn * 0.14, y - 0.05, z0 + 0.16),
               (x + sgn * 0.25, y - 0.05, z0 + 0.21)]
        b.tube(pts, 0.014, "black", sides=5)
        b.blob(Matrix.Translation(Vector(pts[-1]) + Vector((sgn * 0.02, 0, 0.015))) @ Matrix.Diagonal((0.042, 0.042, 0.042, 1)),
               "black", us=8, vs=5)


def hat_harbor_lighthouse(b):
    """등대 모자(희귀) — D 물건(루프#1365): 얕은 흰 받침 위에 머리보다 가는 빨강·흰 줄 탑(위로 살짝 좁아짐) + 탑보다 넓은 검은 난간 +
    노란 등불 방 + 빨간 뾰족 지붕 + 앞 창. 옛 꼴은 머리 폭 원통이라 딸기 모자와 같은 실루엣이었다."""
    top = object_base(b, "white", 0.02, cuff="red", cuff_h=0.03)
    z0 = top - 0.06
    prof = [(0.0, 0.0, "white"), (0.19, 0.0, "white")]
    for i in range(1, 5):
        prof.append((0.19 - 0.012 * i, 0.075 * i, "red" if i % 2 else "white"))
    prof += [(0.17, 0.3, "black"), (0.2, 0.305, "black"), (0.2, 0.33, "black"), (0.12, 0.335, "yellow"), (0.12, 0.42, "yellow"),
             (0.17, 0.425, "red"), (0.17, 0.44, "red"), (0.07, 0.5, "red"), (0.0, 0.53, "red")]
    lathe(b, (CX, CY, z0), prof, n=16, ky=KY)
    zw = z0 + 0.17
    b.blob(Matrix.Translation((CX, CY - 0.172 * KY, zw)) @ Matrix.Diagonal((0.026, 0.012, 0.036, 1)), "black", us=6, vs=4)
    b.blob(Matrix.Translation((CX, CY, z0 + 0.38)) @ Matrix.Diagonal((0.07, 0.07, 0.03, 1)), "yellow", us=8, vs=4)


def hat_meadow_flowerpot(b):
    """화분 모자(일반) — D 물건(루프#1365 — 이미 따로 읽혀 꼴 그대로, 아랫단만 `seat`): 위로 벌어진 토분 + 굵은 테 + 흙 + 새싹 잎 둘과 분홍 꽃 한 송이."""
    def prof(th):
        z = seat(th, 0.012)
        ri = head_r(z, G)
        rc = ri + 0.02
        return inner(z, "inside") + [
            (ri + 0.006, z - 0.012, "clay"), (rc + 0.0, z - 0.01, "clay"), (rc + 0.06, z + 0.17, "clay"),
            (rc + 0.085, z + 0.175, "rim"), (rc + 0.085, z + 0.245, "rim"), (rc + 0.05, z + 0.25, "soil"),
            (0.0, z + 0.25, "soil")]
    b.revolve(prof)
    top = zr(0.0) - 0.012 + 0.25
    b.tube([(CX, CY, top), (CX, CY, top + 0.08), (CX + 0.01, CY, top + 0.17)], 0.011, "leaf", sides=5)
    for ang in (30, 210):
        b.plate(leaf_outline(0.14, 0.06, n=5), 0.008,
                Matrix.Translation((CX, CY, top + 0.07)) @ Matrix.Rotation(math.radians(ang), 4, "Z") @ Matrix.Rotation(math.radians(-25), 4, "Y"), "leaf")
    c = Vector((CX + 0.01, CY, top + 0.19))
    for k in range(6):
        a = math.radians(60 * k)
        b.blob(Matrix.Translation(c + Vector((0.035 * math.cos(a), 0.035 * math.sin(a), 0))) @ Matrix.Diagonal((0.025, 0.025, 0.012, 1)), "pink", us=6, vs=4)
    b.blob(Matrix.Translation(c + Vector((0, 0, 0.004))) @ Matrix.Diagonal((0.022, 0.022, 0.015, 1)), "sun", us=6, vs=4)


def hat_meadow_carrot(b):
    """당근 모자(희귀) — D 물건(루프#1364 — 이미 따로 읽혀 꼴 그대로, 아랫단만 `seat`): 주황 원뿔에 짙은 줄 마디 + 꼭대기에서 뻗는 초록 잎 다섯."""
    def prof(th):
        z = seat(th, 0.012)
        ri = head_r(z, G)
        rc = ri + 0.02
        knots = [(z + 0.05, rc + 0.01), (1.0, rc - 0.03), (1.1, rc - 0.11), (1.22, 0.11), (1.34, 0.0)]
        pts = inner(z, "inside") + [(ri + 0.006, z - 0.012, "orange"), (rc + 0.01, z - 0.01, "orange"), (rc + 0.012, z + 0.05, "orange")]
        n = 9
        for i in range(1, n + 1):
            zz = knots[0][0] + (knots[-1][0] - knots[0][0]) * i / n
            for (z0, r0), (z1, r1) in zip(knots, knots[1:]):
                if z0 <= zz <= z1:
                    rr = r0 + (r1 - r0) * (zz - z0) / (z1 - z0)
                    break
            pts.append((rr if i < n else 0.0, zz, "dark" if i % 3 == 0 else "orange"))
        return pts
    b.revolve(prof)
    tip = Vector((CX, CY, 1.34))
    for k in range(5):
        b.plate(leaf_outline(0.16, 0.05, n=5), 0.008,
                Matrix.Translation(tip) @ Matrix.Rotation(math.radians(72 * k + 20), 4, "Z") @ Matrix.Rotation(math.radians(-55), 4, "Y"), "leaf")


def hat_harbor_jellyfish(b):
    """해파리 모자(에픽) — B 챙(루프#1369, 꼴 그대로): 보랏빛 갓 운두 + 둘레 열 물결로 출렁이는 두툼한 갓 가장자리(짧은 챙) + 하얀 점 +
    가장자리에 늘어진 분홍 촉수 여덟."""
    t, drop = 0.03, 0.01

    def rib(th):
        return 1.0 + 0.045 * math.cos(10 * th)

    def crown(th, z, rc):
        k = rib(th)
        return [((head_r(z, G + t) + 0.026) * k, z + 0.03, "bell")] + [
            (r * k if r > 0.001 else r, zz, "bell") for r, zz in dome(G + t + 0.02, z + 0.05, 9, stretch=0.03)]
    brim(b, crown, lambda th: (head_r(seat(th, drop), G + t) + 0.026) * rib(th) - head_r(seat(th, drop), G) - 0.02,
         lambda th, u: 0.012 * u, thick=lambda u: 0.012 + 0.012 * u, keys=("inside", "rim", "rim"), drop=drop, us=())
    zr0 = seat(TF, drop)
    for dth, dz in ((0, 0.14), (-45, 0.11), (45, 0.11), (-95, 0.13), (95, 0.13), (180, 0.14)):
        p, n = dome_point(G + t + 0.02, TF + math.radians(dth), zr0 + dz)
        b.blob(frame(p + n * 0.003, n) @ Matrix.Diagonal((0.022, 0.022, 0.01, 1)), "dot", us=6, vs=4)
    for k in range(8):
        th = 2 * math.pi * k / 8 + 0.2
        z = seat(th, drop)
        r = head_r(z, G + t) + 0.05
        pts = [(CX + (r + dr) * math.cos(th), CY + (r + dr) * KY * math.sin(th), z + dz) for dr, dz in ((0.0, 0.0), (0.012, -0.03), (-0.006, -0.06), (0.01, -0.09))]
        b.tube(pts, 0.007, "tent", sides=4)


def hat_harbor_sailor_cap(b):
    """세일러 모자(일반) — B 챙(루프#1369, 꼴 그대로 — 앞챙이 없어 C `cap` 대신 테만 있는 `brim`): 흰 밑단 위 감색 띠 + 위로
    벌어지며 오르는 하얀 운두(`crown_box`, 앞이 높게 기운 납작 윗면) + 앞 금빛 단추 + 뒤로 늘어진 감색 리본 둘."""
    drop = 0.012
    brim(b, crown_box("white", lambda th: seat(th, drop) + 0.25, flare=0.05, waist=-0.012, band=("navy", 0.06, 0.03)), 0.01,
         thick=0.01, keys=("inside", "white", "white"), drop=drop, us=())
    zf = seat(TF, drop) + 0.045
    b.blob(Matrix.Translation(surf(TF, head_r(zf, G) + 0.02 + 0.016, zf)) @ Matrix.Diagonal((0.03, 0.01, 0.03, 1)), "gold", us=6, vs=4)
    zb = seat(math.pi / 2, drop)
    rb = head_r(zb, G) + 0.02
    for off in (-0.2, 0.2):
        th2 = math.pi / 2 + off * 1.3
        b.tube([surf(math.pi / 2 + off, rb + 0.015, zb + 0.045), surf(th2, rb + 0.05, zb - 0.05), surf(th2, rb + 0.045, zb - 0.15)],
               0.016, "navy", sides=4)


def hat_harbor_captain(b):
    """항구 선장모(희귀) — C 캡(루프#1369, 꼴 그대로 — 틀 `hats_v2_make.py` 행을 덮어 씀): 줄무늬 띠 위로 벌어져 머리보다 넓고 납작한
    흰 윗판(`crown_plate`, 앞으로 기욺 — 가운데는 머리 안쪽 면 꼭대기 1.03 위로: 같은 높이면 안쪽 남색이 비친다) + 앞 ±55° 검은
    앞챙(아래로 비스듬히) + 금 끈 + 앞 금 닻 배지."""
    cap(b, crown_plate("white", lambda th: 0.3 + 0.016 * fw(th), lambda th: 1.0 + 0.03 * fw(th), 1.045, band=("band", 0.062)),
        0.095, span=math.radians(55), droop=0.06, keys=("inside", "visor", "visor"), drop=0.0)
    pts = [surf(th, head_r(zr(th), G) + 0.03, zr(th) + 0.012)
           for th in (TF + math.radians(-60 + 120 * k / 8) for k in range(9))]
    b.tube(pts, 0.0065, "gold")
    for p in (pts[0], pts[-1]):
        b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.012, 0.012, 0.012, 1)), "gold", us=6, vs=4)
    p = surf(TF, head_r(zr(TF), G) + 0.03, zr(TF) + 0.04)
    m = frame(p, radial(TF, 0.25))
    b.plate(circle(0.04, 18), 0.008, m, "white")
    b.plate(anchor_outline(0.032), 0.014, m @ Matrix.Translation((0, 0, 0.004)), "gold")


def hat_meadow_teapot(b):
    """찻주전자 모자(일반) — D 물건(루프#1364): 머리를 넣은 크림색 둥근 주전자 몸(허리가 불룩·어깨가 좁아짐) + 파란 띠 + 위로 솟은
    뚜껑과 금빛 꼭지 + 오른쪽 위로 휘어 오른 긴 주둥이 + 왼쪽 큰 고리 손잡이. 옛 꼴은 몸이 낮아 베레와 닮았다."""
    z0 = seat(0.0, 0.012)

    def prof(th):
        z = seat(th, 0.012)
        ri = head_r(z, G)
        rc = ri + 0.02
        k = round_k(th)
        return inner(z, "inside") + [(ri + 0.006, z - 0.012, "cream"), (rc + 0.015, z - 0.01, "cream")] + [(r * k, zz, key) for r, zz, key in (
            (rc + 0.065, z0 + 0.07, "cream"), (rc + 0.08, z0 + 0.13, "blue"), (rc + 0.075, z0 + 0.165, "cream"), (rc + 0.035, z0 + 0.23, "cream"),
            (rc - 0.04, z0 + 0.27, "cream"), (0.15, z0 + 0.282, "blue"), (0.14, z0 + 0.3, "cream"), (0.08, z0 + 0.35, "cream"), (0.0, z0 + 0.365, "cream"))]
    b.revolve(prof)
    rc = head_r(z0, G) + 0.02
    b.blob(Matrix.Translation((CX, CY, z0 + 0.4)) @ Matrix.Diagonal((0.036, 0.036, 0.04, 1)), "gold", us=6, vs=4)
    b.tube([(CX + rc + 0.05, CY, z0 + 0.09), (CX + rc + 0.11, CY, z0 + 0.15), (CX + rc + 0.14, CY, z0 + 0.25), (CX + rc + 0.18, CY, z0 + 0.32)],
           0.03, "cream", sides=6)
    b.tube([(CX - rc - 0.05, CY, z0 + 0.24), (CX - rc - 0.12, CY, z0 + 0.23), (CX - rc - 0.14, CY, z0 + 0.14),
            (CX - rc - 0.12, CY, z0 + 0.05), (CX - rc - 0.05, CY, z0 + 0.04)], 0.024, "blue", sides=5)


def hat_meadow_owl(b):
    """부엉이 모자(희귀) — A 비니(루프#1362): 갈색 깃털 껍질 + 짙은 테 + 이마에서 앞으로 나온 큰 얼굴판(눈 둘·주황 부리) + 옆으로 뻗은 깃 뿔 둘."""
    beanie(b, "brown", cuff="dark")
    p, n = dome_point(G + 0.02, TF, seat(TF) + 0.1)
    face = frame(p + n * 0.035, n)
    b.blob(face @ Matrix.Diagonal((0.21, 0.11, 0.05, 1)), "tan", us=12, vs=5)
    for sgn in (-1, 1):
        e = face @ Matrix.Translation((sgn * 0.085, 0.01, 0.04))
        b.blob(e @ Matrix.Diagonal((0.07, 0.07, 0.022, 1)), "white", us=10, vs=4)
        b.blob(e @ Matrix.Translation((0, 0, 0.018)) @ Matrix.Diagonal((0.034, 0.034, 0.012, 1)), "black", us=6, vs=4)
    b.blob(face @ Matrix.Translation((0, -0.05, 0.05)) @ Matrix.Diagonal((0.03, 0.045, 0.04, 1)), "orange", us=6, vs=4)
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.2, CY - 0.02
        z0 = ear_base(x, y) - 0.03
        _cone(b, (x, y, z0), (x + sgn * 0.17, y - 0.02, z0 + 0.16), 0.075, "brown", squash=0.35)
        _cone(b, (x, y + 0.03, z0 - 0.01), (x + sgn * 0.13, y + 0.03, z0 + 0.08), 0.05, "dark", squash=0.35)
def hat_harbor_crab(b):
    """게 모자(희귀) — A 비니(루프#1363): 빨간 등딱지 껍질(어두운 테) + 주황 점 + 눈자루 위의 눈 둘 + 양옆으로 든 큰 집게발 둘."""
    t = 0.02
    top = beanie(b, "red", t, cuff="dark", cuff_h=0.03, swell=0.03) - 0.03
    zr0 = seat(TF)
    for dth, dz in ((70, 0.06), (-70, 0.06), (140, 0.08), (-140, 0.08), (180, 0.04)):
        p, n = dome_point(G + t, TF + math.radians(dth), zr0 + dz)
        b.blob(frame(p + n * 0.003, n) @ Matrix.Diagonal((0.024, 0.024, 0.01, 1)), "orange", us=6, vs=4)
    for sgn in (-1, 1):
        base = Vector((CX + sgn * 0.06, CY - 0.1, top - 0.03))
        b.tube([base, base + Vector((sgn * 0.01, -0.01, 0.06))], 0.01, "red", sides=4)
        c = base + Vector((sgn * 0.01, -0.01, 0.08))
        b.blob(Matrix.Translation(c) @ Matrix.Diagonal((0.03, 0.03, 0.03, 1)), "white", us=6, vs=4)
        b.blob(Matrix.Translation(c + Vector((0, -0.022, 0.0))) @ Matrix.Diagonal((0.014, 0.012, 0.014, 1)), "black", us=6, vs=4)
        z0 = seat(0.0) + 0.06
        r0 = head_r(z0, G + t) + 0.012
        a = Vector((CX + sgn * r0, CY, z0))
        b.tube([a, a + Vector((sgn * 0.035, -0.03, 0.05)), a + Vector((sgn * 0.05, -0.05, 0.12))], 0.016, "red", sides=4)
        e = a + Vector((sgn * 0.05, -0.05, 0.16))
        b.blob(Matrix.Translation(e) @ Matrix.Diagonal((0.055, 0.04, 0.055, 1)), "red", us=6, vs=4)
        b.blob(Matrix.Translation(e + Vector((-sgn * 0.01, -0.02, 0.05))) @ Matrix.Diagonal((0.02, 0.02, 0.03, 1)), "red", us=5, vs=3)


def hat_meadow_cake(b):
    """케이크 모자(에픽) — D 물건(루프#1364): 머리를 넣은 분홍 스펀지 아래층(곧은 옆벽·평평한 윗면, 윗가장자리 흰 크림이 흘러내림) +
    위에 작은 둘째 층 + 딸기 셋 + 가운데 노란 초와 불꽃. 옛 꼴은 두 층이 머리 돔 높이 안에 눌려 호박과 같은 실루엣이었다."""
    ztop = 1.075

    def prof(th):
        z = seat(th, 0.012)
        ri = head_r(z, G)
        rw = (head_r(CZ - 0.1, G) + 0.08) * round_k(th)   # 둥근 옆벽 — 모서리에서도 머리 밖(머리 반지름 × 1.19 이상)
        return inner(z, "inside") + [
            (ri + 0.006, z - 0.012, "sponge"), (rw, z - 0.01, "sponge"), (rw, ztop - 0.06, "cream"),
            (rw + 0.004, ztop - 0.01, "cream"), (rw - 0.015, ztop, "cream"), (0.0, ztop, "cream")]
    b.revolve(prof)
    for k in range(8):
        th = 2 * math.pi * k / 8 + 0.2
        r = head_r(CZ - 0.1, G) + 0.08 + 0.008
        b.blob(Matrix.Translation((CX + r * math.cos(th), CY + r * KY * math.sin(th), ztop - 0.07 - 0.012 * (k % 2))) @ Matrix.Diagonal((0.03, 0.03, 0.04, 1)),
               "cream", us=5, vs=3)
    zt2 = ztop + 0.13
    lathe(b, (CX, CY, ztop - 0.005), [(0.0, 0.0, "sponge"), (0.2, 0.0, "sponge"), (0.2, 0.095, "cream"), (0.205, 0.115, "cream"),
                                      (0.185, 0.13, "cream"), (0.0, 0.135, "cream")], n=20, ky=KY)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.9
        p = Vector((CX + 0.13 * math.cos(a), CY + 0.13 * KY * math.sin(a), zt2 + 0.03))
        b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.036, 0.036, 0.042, 1)), "berry", us=6, vs=4)
        b.blob(Matrix.Translation(p + Vector((0, 0, 0.036))) @ Matrix.Diagonal((0.022, 0.022, 0.008, 1)), "leaf", us=5, vs=3)
    b.tube([(CX, CY, zt2 - 0.01), (CX, CY, zt2 + 0.11)], 0.015, "candle", sides=5)
    b.blob(Matrix.Translation((CX, CY, zt2 + 0.15)) @ Matrix.Diagonal((0.02, 0.02, 0.036, 1)), "flame", us=6, vs=4)


def hat_meadow_snail(b):
    """달팽이 모자(일반) — D 물건(루프#1366): 연두 몸 받침(앞에 눈자루 둘) + 머리 오른쪽 위에 옆으로 세운 큰 갈색 껍질 공(바깥 면에
    소용돌이 줄) — 앞에서는 한쪽으로 치우친 큰 공, 옆에서는 동그란 껍질이 실루엣. 옛 꼴은 둥근 모자 위 작은 공이라 보물 상자와 같았다."""
    t = 0.02
    object_base(b, "green", t, cuff="belly")
    c = Vector((CX + 0.11, CY + 0.06, 1.24))
    R = Vector((0.15, 0.23, 0.23))                       # 껍질 반지름 — 축은 옆(X), 밑이 받침에 3cm 묻힘
    b.blob(Matrix.Translation(c) @ Matrix.Diagonal((R.x, R.y, R.z, 1)), "shell", us=12, vs=8)
    pts = []
    for k in range(22):                                  # 소용돌이 — 바깥(+X) 면에 1.75 바퀴 감기는 줄
        f = k / 21
        a = math.pi * 3.5 * f
        rho = 0.2 * (1 - f) + 0.02
        y, z = rho * math.cos(a), rho * math.sin(a)
        x = R.x * math.sqrt(max(0.0, 1 - (rho / R.y) ** 2)) + 0.004
        pts.append(c + Vector((x, y, z)))
    b.tube(pts, 0.016, "swirl", sides=4)
    for sgn in (-1, 1):                                  # 눈자루 — 앞 이마에서 위로
        x = CX - 0.05 + sgn * 0.05
        p0 = Vector((x, CY - 0.17, base_z(x, CY - 0.17, t) - 0.01))
        p1 = p0 + Vector((sgn * 0.02, -0.04, 0.14))
        b.tube([p0, p1], 0.012, "green", sides=5)
        b.blob(Matrix.Translation(p1 + Vector((0, 0, 0.02))) @ Matrix.Diagonal((0.026, 0.026, 0.026, 1)), "white", us=6, vs=4)
        b.blob(Matrix.Translation(p1 + Vector((0, -0.02, 0.022))) @ Matrix.Diagonal((0.011, 0.008, 0.011, 1)), "black", us=5, vs=3)


def hat_harbor_buoy(b):
    """부표 모자(일반) — D 물건(루프#1365): 머리를 덮은 얕은 빨강 받침 위에 머리 밖으로 벌어진 뜨개 치마(흰 띠) + 위로 좁아지는
    빨강·흰 줄 원뿔 몸 + 검은 기둥 + 노란 불빛 + 꼭대기 표지 공. 옛 꼴은 줄무늬 돔 하나라 새싹 모자와 같은 실루엣이었다."""
    top = object_base(b, "red", 0.02, cuff="white", cuff_h=0.03)
    z0 = top - 0.07
    stripes = ("red", "white", "red", "white")
    prof = [(0.0, 0.0, "red"), (0.2, 0.0, "red"), (0.29, 0.035, "red"), (0.3, 0.055, "white"), (0.27, 0.075, "white"),
            (0.2, 0.08, "red")]
    for i in range(1, 5):   # 원뿔 몸 — 띠 넷
        prof.append((0.2 - 0.03 * i, 0.08 + 0.055 * i, stripes[i % 4]))
    prof += [(0.11, 0.315, "black"), (0.0, 0.32, "black")]
    lathe(b, (CX, CY, z0), prof, n=16, ky=KY)
    zt = z0 + 0.32
    b.tube([(CX, CY, zt - 0.01), (CX, CY, zt + 0.1)], 0.016, "black", sides=6)
    b.blob(Matrix.Translation((CX, CY, zt + 0.13)) @ Matrix.Diagonal((0.04, 0.04, 0.045, 1)), "yellow", us=6, vs=4)
    b.tube([(CX, CY, zt + 0.17), (CX, CY, zt + 0.2)], 0.008, "black", sides=5)
    b.blob(Matrix.Translation((CX, CY, zt + 0.23)) @ Matrix.Diagonal((0.035, 0.035, 0.035, 1)), "red", us=6, vs=4)
    for k in range(4):     # 치마 둘레 밧줄 고리
        a = TF + math.pi / 2 * k + math.pi / 4
        p = Vector((CX + 0.3 * math.cos(a), CY + 0.3 * KY * math.sin(a), z0 + 0.045))
        b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.022, 0.022, 0.022, 1)), "rope", us=5, vs=3)


def hat_meadow_pineapple(b):
    """파인애플 모자(희귀) — D 물건(루프#1364): 머리를 넣은 세로로 긴 노란 열매(비늘 무늬 — 무늬 그림이라 점 덩어리 없음) + 꼭대기에서
    곧게 솟아 부채처럼 퍼지는 뾰족 잎 왕관(긴 잎 다섯·짧은 잎 넷). 옛 꼴은 머리 돔 + 옆으로 눕힌 잎이라 소라와 같은 실루엣이었다."""
    zm, zt = 0.97, 1.36

    def prof(th):
        z = seat(th, 0.02)
        ri, ro = head_r(z, G), head_r(z, G + 0.02)
        a = (ro + 0.05) * round_k(th)
        pts = inner(z, "inside")
        pts += [(ri + 0.004, z - 0.01, "dark"), (ro + 0.012, z - 0.008, "dark"), (ro + 0.02, z + 0.03, "skin"), (a, zm - 0.04, "skin")]
        for k in range(9):
            p = math.pi / 2 * k / 8
            pts.append((0.0 if k == 8 else a * math.cos(p), zm + (zt - zm) * math.sin(p), "skin" if k < 7 else "dark"))
        return pts
    b.revolve(prof)
    for k in range(9):
        long_ = k % 2 == 0
        a = math.radians(40 * k + 10)
        base = Vector((CX, CY, zt - 0.02))
        b.plate(leaf_outline(0.36 if long_ else 0.24, 0.085, n=5), 0.01,
                Matrix.Translation(base) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Rotation(math.radians(-72 if long_ else -55), 4, "Y"), "leaf")


def hat_harbor_starfish(b):
    """불가사리 모자(희귀) — D 물건(루프#1366): 얕은 주황 받침 위에 앞을 보고 선 머리 폭만 한 두툼한 다섯 갈래 별(아래 두 팔로 정수리를
    딛고 서고 뒤로 조금 젖힘) + 짙은 테두리 별 + 팔마다 흰 돌기 + 가운데 눈 둘. 앞에서 별 꼴이 그대로 실루엣. 옛 꼴은 둥근 모자 위
    납작한 별 판이라 딸기 모자와 같았다."""
    t = 0.02
    object_base(b, "orange", t, cuff="dark")
    s = 0.3
    n = Vector((0, -1, 0.3)).normalized()                # 앞을 보고 뒤로 17° 젖힘
    foot = s * 0.81                                      # 아래 팔 끝까지 내려가는 거리 (−54°·−126°)
    xf = s * 0.59
    pos = Vector((CX, CY + 0.03, base_z(CX + xf, CY, t) - 0.02 + foot))
    b.plate(star_outline(s + 0.015), 0.035, frame(pos - n * 0.02, n), "dark", inset=0.2)
    b.plate(star_outline(s), 0.075, frame(pos, n), "orange", inset=0.55)
    m = frame(pos + n * 0.07, n)
    for k in range(5):                                   # 팔마다 흰 돌기 둘
        a = math.pi / 2 + 2 * math.pi * k / 5
        for d in (0.13, 0.2):
            b.blob(m @ Matrix.Translation((d * math.cos(a), d * math.sin(a), 0.0)) @ Matrix.Diagonal((0.016, 0.016, 0.012, 1)), "white", us=5, vs=3)
    for sgn in (-1, 1):                                  # 눈
        b.blob(m @ Matrix.Translation((sgn * 0.035, 0.02, 0.0)) @ Matrix.Diagonal((0.022, 0.026, 0.012, 1)), "white", us=6, vs=4)
        b.blob(m @ Matrix.Translation((sgn * 0.035, 0.015, 0.01)) @ Matrix.Diagonal((0.011, 0.014, 0.008, 1)), "black", us=5, vs=3)


def hat_meadow_rocket(b):
    """로켓 모자(에픽) — D 물건(루프#1365 — 이미 따로 읽혀 꼴 그대로, 아랫단만 `seat`): 흰 원통 몸체 + 빨간 뾰족 머리 + 앞 파란 둥근 창 + 아래 빨간 날개 셋."""
    def prof(th):
        z = seat(th, 0.012)
        ri = head_r(z, G)
        rc = ri + 0.02
        return inner(z, "inside") + [
            (ri + 0.006, z - 0.012, "white"), (rc + 0.01, z - 0.01, "white"), (rc + 0.012, z + 0.16, "white"),
            (rc + 0.012, z + 0.175, "red"), (rc - 0.01, z + 0.25, "red"), (rc - 0.07, z + 0.32, "red"),
            (rc - 0.14, z + 0.375, "red"), (0.0, z + 0.44, "red")]
    b.revolve(prof)
    zb = zr(TF) - 0.012 + 0.06
    p = surf(TF, head_r(zb, G) + 0.02 + 0.014, zb)
    b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.036, 0.014, 0.036, 1)), "gray", us=8, vs=4)
    b.blob(Matrix.Translation(p + Vector((0, -0.008, 0.0))) @ Matrix.Diagonal((0.027, 0.01, 0.027, 1)), "blue", us=8, vs=4)
    for k in range(3):
        th = TF + math.radians(120 * k + 60)
        z = zr(th) - 0.012 + 0.03
        r0 = head_r(z, G) + 0.02 + 0.01
        base = surf(th, r0, z)
        m = Matrix.Translation(base) @ Matrix.Rotation(th - math.pi / 2 + math.pi, 4, "Z") @ Matrix.Rotation(math.radians(90), 4, "X")
        b.plate([(0.0, -0.03), (0.07, -0.07), (0.07, 0.05), (0.0, 0.11)], 0.012, m, "red")


def hat_meadow_duck(b):
    """오리 모자(일반) — A 비니(루프#1363): 노란 껍질 + 이마에서 앞으로 길게 뻗은 주황 부리(위·아래 두 겹) + 부리 위 눈 둘 + 꼭대기 부채꼴 깃털 볏 + 옆으로 뻗은 작은 날개 둘 + 뒤통수에서 뒤·위로 솟은 꽁지."""
    t = 0.02
    top = beanie(b, "yellow", t, swell=0.04)
    zr0 = seat(TF)
    p, n = dome_point(G + t, TF, zr0 + 0.075)
    f = frame(p, n)
    b.blob(f @ Matrix.Translation((0, 0.01, 0.08)) @ Matrix.Diagonal((0.085, 0.024, 0.11, 1)), "orange", us=8, vs=5)
    b.blob(f @ Matrix.Translation((0, -0.022, 0.06)) @ Matrix.Diagonal((0.07, 0.018, 0.085, 1)), "orange", us=8, vs=4)
    for sgn in (-1, 1):
        q, m = dome_point(G + t, TF + sgn * math.radians(28), zr0 + 0.14)
        b.blob(frame(q + m * 0.004, m) @ Matrix.Diagonal((0.024, 0.024, 0.012, 1)), "white", us=6, vs=4)
        b.blob(frame(q + m * 0.013, m) @ Matrix.Diagonal((0.013, 0.013, 0.008, 1)), "black", us=5, vs=3)
    for dx, h in ((-0.05, 0.11), (0.0, 0.16), (0.05, 0.11)):        # 꼭대기 깃털 볏 — 납작한 뿔 셋이 부채꼴로
        _cone(b, (CX + dx * 0.6, CY + 0.01, top - 0.03), (CX + dx * 1.6, CY - 0.03, top + h), 0.035, "yellow", squash=0.45, sides=5)
    for sgn in (-1, 1):                                             # 옆 작은 날개 — 바깥·뒤·위로
        th = TF + sgn * math.radians(95)
        q, m = dome_point(G + t, th, seat(th) + 0.07)
        _cone(b, q - m * 0.01, q + Vector((sgn * 0.15, 0.07, 0.08)), 0.07, "yellow", squash=0.35, sides=5)
    q, m = dome_point(G + t, -TF, seat(-TF) + 0.09)                 # 꽁지 — 뒤·위로 납작한 뿔
    _cone(b, q - m * 0.01, q + Vector((0, 0.15, 0.1)), 0.07, "yellow", squash=0.35, sides=5)


def hat_meadow_beehive(b):
    """벌집 모자(일반) — D 물건(루프#1365): 짚을 감아 쌓은 종 — 아랫단이 머리 밖으로 벌어지고 위로 갈수록 좁아지는 볼록한 테 여섯 층 +
    둥근 꼭대기 손잡이 + 앞 아랫단 검은 출입구 + 옆에 붙은 벌. 옛 꼴은 줄무늬 돔이라 이글루와 같은 실루엣이었다."""
    zt = 1.38
    layers = 6

    def prof(th):
        z = seat(th, 0.02)
        ri, ro = head_r(z, G), head_r(z, G + 0.03)
        pts = inner(z, "inside") + [(ri + 0.004, z - 0.01, "dark"), (ro + 0.03, z - 0.008, "dark")]
        for i in range(layers):   # 층마다 홈(짙은 색) → 볼록한 테(꿀색)
            s0, s1 = i / layers, (i + 0.55) / layers
            for s, key, bulge in ((s0, "dark", 0.0), (s1, "honey", 0.022)):
                zz = z + 0.01 + (zt - 0.05 - z) * s
                w = min(1.0, s * 3)
                bell = (ro + 0.1) * (1 - 0.28 * min(1.0, s * 4) - 0.5 * s ** 2.2)   # 종 — 아랫단이 치마처럼 벌어지고 위는 둥글게
                r = max((bell + bulge) * round_k(th, w), head_r(zz, G + 0.03) + 0.01)
                pts.append((r, zz, key))
        pts += [(0.09 * round_k(th), zt - 0.02, "honey"), (0.0, zt, "honey")]
        return pts
    b.revolve(prof)
    z = seat(TF, 0.02) + 0.05                                   # 출입구 — 앞 치마 바깥면, 아랫단 바로 위 (눈 위)
    p = surf(TF, (head_r(z, G + 0.03) + 0.1) * 0.9, z)
    b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.05, 0.016, 0.032, 1)), "black", us=8, vs=4)
    b.blob(Matrix.Translation((CX, CY, zt + 0.015)) @ Matrix.Diagonal((0.04, 0.04, 0.03, 1)), "honey", us=6, vs=4)
    c = Vector((CX + 0.3, CY - 0.06, 1.12))   # 옆 벌
    b.blob(Matrix.Translation(c) @ Matrix.Diagonal((0.03, 0.04, 0.028, 1)), "bee", us=6, vs=4)
    b.blob(Matrix.Translation(c) @ Matrix.Diagonal((0.032, 0.012, 0.03, 1)), "black", us=5, vs=4)
    for sgn in (-1, 1):
        b.blob(Matrix.Translation(c + Vector((sgn * 0.022, 0.006, 0.03))) @ Matrix.Diagonal((0.022, 0.012, 0.01, 1)), "white", us=6, vs=3)


def hat_harbor_conch(b):
    """소라 모자(희귀) — D 물건(루프#1365): 얕은 분홍 받침 위에 뒤로 비스듬히 누운 나선 원뿔 껍데기(층마다 한 바퀴 도는 턱 + 어깨 혹 여섯)
    + 앞 오른쪽으로 벌어진 분홍 입술. 옛 꼴은 분홍 돔 위 작은 덩이 다섯이라 보물 상자와 같은 실루엣이었다."""
    top = object_base(b, "pink", 0.02, cuff="shell", cuff_h=0.03)
    z0 = top - 0.07
    prof = [(0.0, 0.0, "cream"), (0.15, 0.0, "cream"), (0.22, 0.05, "cream"), (0.235, 0.085, "shell"), (0.2, 0.12, "cream"),
            (0.155, 0.175, "shell"), (0.11, 0.23, "cream"), (0.07, 0.28, "shell"), (0.035, 0.32, "cream"), (0.0, 0.35, "cream")]
    lean = (0.0, 0.55)
    lathe(b, (CX, CY + 0.02, z0), prof, n=18, ky=KY, lean=lean,
          rib=lambda a, i: 1.0 - 0.14 * (((a - TF) / (2 * math.pi) + 0.37 * i) % 1.0) if 2 <= i <= 8 else 1.0)
    for k in range(6):     # 어깨 혹
        a = TF + 2 * math.pi * k / 6 + 0.3
        dz = 0.085
        p = Vector((CX + 0.235 * math.cos(a), CY + 0.02 + 0.235 * KY * math.sin(a) + lean[1] * dz, z0 + dz + 0.02))
        b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.028, 0.028, 0.04, 1)), "shell", us=5, vs=3)
    a = TF + math.radians(60)   # 입술 — 앞 오른쪽 껍데기 밑동에서 바깥으로 불룩 벌어진 분홍 테
    base = Vector((CX + 0.225 * math.cos(a), CY + 0.02 + 0.225 * KY * math.sin(a), z0 + 0.09))
    b.blob(Matrix.Translation(base) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Diagonal((0.05, 0.13, 0.1, 1)), "lip", us=8, vs=5)


def hat_meadow_cactus(b):
    """선인장 모자(희귀) — D 물건(루프#1366): 모래빛 받침(조약돌) 위에 머리 높이보다 높이 솟은 골진 기둥(골 8) + 양옆으로 높이를 달리해
    뻗었다 위로 꺾인 팔 둘 + 꼭대기 분홍 꽃 + 흰 가시. 앞에서 촛대 꼴. 옛 꼴은 둥근 모자 위 작은 기둥이라 보물 상자와 같았다."""
    t = 0.02
    object_base(b, "sand", t, cuff="pebble")
    c = (CX, CY + 0.01, 1.0)
    prof = [(0.0, 0.0), (0.1, 0.0), (0.108, 0.14), (0.104, 0.3), (0.09, 0.39), (0.055, 0.44), (0.0, 0.455)]
    lathe(b, c, prof, key="green", n=16, rib=lambda a, i: 1.0 + 0.1 * math.cos(8 * a) if 0 < i < 6 else 1.0)
    for sgn, z, out, up in ((1, 1.13, 0.22, 0.2), (-1, 1.21, 0.2, 0.15)):   # 팔 — 오른쪽 낮게·길게, 왼쪽 높게
        p = [Vector((CX + sgn * dx, CY + 0.01 + dy, z + dz)) for dx, dy, dz in
             ((0.07, 0.0, 0.0), (out - 0.05, 0.0, 0.0), (out, -0.01, 0.04), (out, -0.02, up))]
        b.tube(p, 0.052, "green", sides=8)
        b.blob(Matrix.Translation(p[-1]) @ Matrix.Diagonal((0.052, 0.052, 0.045, 1)), "green", us=8, vs=4)
    zt = c[2] + 0.455
    for k in range(5):                                   # 꽃
        a = 2 * math.pi * k / 5
        b.blob(Matrix.Translation((CX + 0.03 * math.cos(a), CY + 0.01 + 0.03 * math.sin(a), zt)) @ Matrix.Diagonal((0.03, 0.03, 0.018, 1)), "pink", us=5, vs=3)
    b.blob(Matrix.Translation((CX, CY + 0.01, zt + 0.012)) @ Matrix.Diagonal((0.016, 0.016, 0.012, 1)), "yellow", us=5, vs=3)
    for k, z in enumerate((1.1, 1.2, 1.3, 1.16, 1.26)):  # 흰 가시 — 앞 골 끝
        a = TF + (k - 2) * 0.4
        b.blob(Matrix.Translation((CX + 0.11 * math.cos(a), CY + 0.01 + 0.11 * math.sin(a), z)) @ Matrix.Diagonal((0.008, 0.008, 0.008, 1)), "white", us=4, vs=3)
    for k in range(4):                                   # 조약돌
        a = TF + math.radians(70 + 70 * k)
        x, y = CX + 0.22 * math.cos(a), CY + 0.2 * math.sin(a)
        b.blob(Matrix.Translation((x, y, base_z(x, y, t))) @ Matrix.Diagonal((0.03, 0.025, 0.018, 1)), "pebble", us=5, vs=3)


def hat_meadow_ufo(b):
    """UFO 모자(에픽) — B 챙(루프#1367): 머리보다 훨씬 넓고 가장자리가 얇은 회색 접시(렌즈 단면) + 가운데 높이 솟은 작은 초록
    유리 돔과 외계인 + 가장자리 노란 불빛 여섯 + 돔 꼭대기 안테나."""
    rb = 0.13                                                     # 유리 돔 반지름
    a, c = HA + G + 0.02, HC + G + 0.02

    def crown(th, z, rc):
        pts = [(rc - 0.004, z + 0.045, "gray")]
        for r in (0.27, 0.24, 0.2, 0.16):                          # 접시 윗면 = 머리 윗면 + 틈(머리 살이 안 뚫게)
            pts.append((r, CZ + c * math.sqrt(max(0.0, 1 - (r / a) ** 2)), "dark" if r < 0.17 else "gray"))
        zb = CZ + c * math.sqrt(1 - (rb / a) ** 2)
        for k in range(6):
            q = math.pi / 2 * k / 5
            pts.append((rb * math.cos(q), zb + 0.02 + rb * 1.15 * math.sin(q), "glass"))
        return pts
    brim(b, crown, 0.17, lambda th, u: 0.05 * (1 - u) - 0.012, thick=lambda u: 0.012 + 0.05 * (1 - u) ** 1.5,
         keys=("inside", "gray", lambda u: "dark" if u > 0.9 else "gray"))
    zb = CZ + c * math.sqrt(1 - (rb / a) ** 2)
    b.blob(Matrix.Translation((CX, CY, zb + 0.06)) @ Matrix.Diagonal((0.045, 0.04, 0.05, 1)), "alien", us=8, vs=5)
    for sgn in (-1, 1):
        b.blob(Matrix.Translation((CX + sgn * 0.018, CY - 0.035, zb + 0.075)) @ Matrix.Diagonal((0.013, 0.008, 0.018, 1)), "black", us=5, vs=3)
    tip = zb + 0.02 + rb * 1.15
    b.tube([(CX, CY, tip - 0.005), (CX, CY, tip + 0.05)], 0.005, "dark", sides=5)
    b.blob(Matrix.Translation((CX, CY, tip + 0.055)) @ Matrix.Diagonal((0.014, 0.014, 0.014, 1)), "yellow", us=5, vs=3)
    for k in range(6):
        th = TF + math.radians(60 * k + 30)
        z = seat(th)
        p = surf(th, head_r(z, G) + 0.02 + 0.17 * 0.75, z + 0.05 * 0.25 - 0.012 + 0.006)
        b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.018, 0.018, 0.012, 1)), "yellow", us=5, vs=3)


def _plain_dome(b, key, t=0.02, stretch=0.03, inside="inside", lift=0.0):
    """M91 공통 — 머리에 얹는 둥근 모자 몸통(한 색). lift = 테 높이를 올리는 양(눈썹 위로)."""
    def prof(th):
        z = zr(th) - 0.02 + lift
        ri, ro = head_r(z, G), head_r(z, G + t)
        pts = inner(z, inside, n=3)
        pts += [(ri + 0.004, z - 0.01, key), (ro + 0.012, z - 0.008, key), (ro + 0.014, z + 0.03, key)]
        pts += [(r, zz, key) for r, zz in dome(G + t, z + 0.04, 9, stretch=stretch)]
        return pts
    b.revolve(prof)


def hat_meadow_penguin(b):
    """펭귄 모자(일반) — A 비니(루프#1363, 테를 눈썹 위로 0.05 올림 — 얼굴판이 눈을 안 가리게): 검은 껍질 + 이마 앞면에 하트꼴 흰 얼굴판(위 가운데가 파인 두 봉우리)·판 안쪽 위 검은 눈 둘·판 아래쪽 아래로 뾰족한 주황 부리 + 양옆 검은 작은 날개 둘. 꼭대기엔 아무것도 없다."""
    t = 0.02
    drop = -0.05
    beanie(b, "black", t, swell=0.03, drop=drop)
    p, n = dome_point(G + t, TF, seat(TF, drop) + 0.04)
    f = frame(p + n * 0.012, n)
    k = 1.25
    heart = [(-0.115, 0.02), (-0.10, 0.058), (-0.06, 0.068), (-0.022, 0.05), (0.0, 0.022), (0.022, 0.05), (0.06, 0.068), (0.10, 0.058),
             (0.115, 0.02), (0.08, -0.03), (0.0, -0.068), (-0.08, -0.03)]
    b.plate([(x * k, y * k) for x, y in heart], 0.026, f, "white")
    for sgn in (-1, 1):
        b.blob(f @ Matrix.Translation((sgn * 0.052 * k, 0.034 * k, 0.03)) @ Matrix.Diagonal((0.02 * k, 0.024 * k, 0.012, 1)), "black", us=6, vs=4)
    b.plate([(-0.026 * k, 0.0), (0.026 * k, 0.0), (0.0, -0.048 * k)], 0.014, f @ Matrix.Translation((0, 0.004 * k, 0.022)), "orange")
    for sgn in (-1, 1):
        th = TF + sgn * math.radians(88)
        p, n = dome_point(G + t, th, seat(th, drop))
        fw_ = frame(p + n * 0.006, n) @ Matrix.Rotation(math.radians(-38), 4, "X")
        b.plate([(-0.03, 0.03), (0.03, 0.03), (0.026, -0.03), (0.0, -0.085), (-0.026, -0.03)], 0.03, fw_, "black")


def hat_meadow_donut(b):
    """도넛 모자(일반) — D 물건(루프#1364): 빵색 얕은 받침 + 그 위에 앞으로 살짝 기울여 얹은 머리보다 넓은 도넛 고리(윗면 분홍 크림이
    물결로 흘러내림 · 가운데 구멍으로 받침이 보임) + 크림 위 알록달록 가루. 옛 꼴은 돔에 크림 덮개라 이글루와 같은 실루엣이었다."""
    t = 0.02
    object_base(b, "hole", t, cuff="dough")
    R, r, tilt = 0.225, 0.1, math.radians(12)
    rot = Matrix.Rotation(tilt, 4, "X")
    ring = [rot @ Vector((R * math.cos(a), R * 0.92 * math.sin(a), 0)) for a in (2 * math.pi * k / 24 for k in range(24))]
    zc = sum(base_z(CX + p.x, CY + p.y, t) - p.z + r - 0.03 for p in ring) / len(ring)   # 관 밑이 받침에 평균 3cm 묻히게
    mat = Matrix.Translation((CX, CY, zc)) @ rot @ Matrix.Diagonal((1.0, 0.92, 1.0, 1))
    torus(b, mat, R, r, lambda a, q: "icing" if math.sin(q) > -0.2 + 0.25 * math.sin(7 * a) else "dough", n=22, m=8)
    cols = ("red", "blue", "yellow", "white", "green")
    for k in range(12):
        a, q = k * 2.4, math.pi / 2 + (((k * 7) % 5) - 2) * 0.35
        p = mat @ Vector(((R + r * math.cos(q)) * math.cos(a), (R + r * math.cos(q)) * math.sin(a), r * math.sin(q) + 0.004))
        b.blob(Matrix.Translation(p) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Diagonal((0.026, 0.011, 0.011, 1)), cols[k % 5], us=4, vs=3)


def hat_meadow_mouse(b):
    """쥐 모자(희귀) — A 비니(루프#1362): 회색 껍질 + 머리 옆에 뒤로 기울어 붙은 크고 납작한 타원 귀 둘(안쪽 분홍) + 뒤로 말려 올라간 꼬리 + 앞 코·눈·수염."""
    beanie(b, "gray")
    for sgn in (-1, 1):
        c = Vector((CX + sgn * 0.3, CY + 0.03, 1.0))
        m = Matrix.Translation(c) @ Matrix.Rotation(math.radians(sgn * -40), 4, "Y") @ Matrix.Rotation(math.radians(sgn * 25), 4, "Z")
        b.blob(m @ Matrix.Diagonal((0.13, 0.026, 0.115, 1)), "gray", us=10, vs=5)
        b.blob(m @ Matrix.Translation((0, -0.02, 0)) @ Matrix.Diagonal((0.095, 0.012, 0.082, 1)), "pink", us=10, vs=4)
    tail = [(CX, CY + 0.2, 1.02), (CX, CY + 0.3, 1.04), (CX, CY + 0.37, 1.1), (CX, CY + 0.38, 1.18), (CX, CY + 0.33, 1.23), (CX, CY + 0.28, 1.2)]
    b.tube(tail, 0.014, "pink", sides=6)
    zr0 = seat(TF)
    p, n = dome_point(G + 0.02, TF, zr0 + 0.05)
    b.blob(frame(p + n * 0.012, n) @ Matrix.Diagonal((0.026, 0.022, 0.018, 1)), "pink", us=6, vs=4)
    for sgn in (-1, 1):
        p, n = dome_point(G + 0.02, TF + sgn * math.radians(24), zr0 + 0.105)
        b.blob(frame(p + n * 0.012, n) @ Matrix.Diagonal((0.014, 0.014, 0.01, 1)), "black", us=5, vs=3)
        for dz in (-0.012, 0.012):
            p, n = dome_point(G + 0.02, TF + sgn * math.radians(20), zr0 + 0.05 + dz)
            b.blob(frame(p + n * 0.008, n) @ Matrix.Diagonal((0.024, 0.005, 0.005, 1)), "white", us=4, vs=3)
def hat_harbor_igloo(b):
    """이글루 모자(희귀) — D 물건(루프#1365): 머리보다 넓게 벌어진 낮은 눈 반구(얼음빛 이음 줄 넷) + 앞으로 튀어나온 굴(둥근 지붕 반원통) +
    굴 입구 어두운 구멍. 옛 꼴은 머리 돔 높이의 줄무늬 돔이라 벌집과 같은 실루엣이었다."""
    zt = 1.22

    def prof(th):
        z = seat(th, 0.02)
        ri, ro = head_r(z, G), head_r(z, G + 0.03)
        R = ro + 0.07
        pts = inner(z, "inside") + [(ri + 0.004, z - 0.01, "snow"), (R, z - 0.008, "snow")]
        n = 10
        for i in range(1, n + 1):   # 낮은 반구 — 아랫단은 머리 꼴, 꼭대기로 갈수록 원
            p = math.pi / 2 * i / n
            zz = z + (zt - z) * math.sin(p)
            r = 0.0 if i == n else max(R * math.cos(p) * round_k(th, min(1.0, i / 3.0)), head_r(zz, G + 0.03))
            pts.append((r, zz, "ice" if i in (2, 4, 6, 8) else "snow"))
        return pts
    b.revolve(prof)
    a = TF + math.radians(45)   # 굴 — 앞 오른쪽으로 비스듬히: 앞·옆 어느 쪽에서도 반구 밖으로 보인다
    zc = seat(a, 0.02) + 0.115
    d = radial(a)
    e = surf(a, head_r(seat(a, 0.02), G + 0.03) + 0.07, zc)   # 반구 아랫단 바깥 자리
    b.tube([e - d * 0.14, e + d * 0.2], 0.115, "snow", sides=10)
    b.plate(circle(0.08, 12), 0.01, frame(e + d * 0.202, d), "dark")
    b.blob(Matrix.Translation(e + d * 0.1 + Vector((0, 0, 0.11))) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Diagonal((0.1, 0.13, 0.016, 1)), "ice", us=8, vs=3)


def hat_harbor_rainbow(b):
    """무지개 모자(희귀) — D 물건(루프#1366): 하늘색 받침 위에 머리를 옆으로 가로질러 걸친 큰 무지개 아치(네 색 띠, 앞뒤 두께 14cm) +
    양쪽 발밑 구름 덩이. 앞에서 가운데가 뚫린 아치가 실루엣. 옛 꼴은 둥근 모자 위 작은 고리라 보물 상자와 같았다."""
    t = 0.02
    object_base(b, "sky", t, cuff="cloud")
    zc, r0, w = 1.0, 0.2, 0.045                          # 아치 가운데 높이 · 안 반지름 · 띠 폭
    c = Vector((CX, CY + 0.07, zc))
    for k, col in enumerate(("green", "yellow", "orange", "red")):
        ri, ro = r0 + w * k, r0 + w * (k + 1)
        arc = [math.pi * j / 8 for j in range(9)]
        outline = [(ro * math.cos(a), ro * math.sin(a)) for a in arc] + [(ri * math.cos(a), ri * math.sin(a)) for a in reversed(arc)]
        side_plate(b, outline, 0.14, c, 0.0, col, inset=0.0)
    for sgn in (-1, 1):                                  # 구름 — 아치 발을 덮는 덩이 셋
        x = CX + sgn * (r0 + 2 * w)
        for dx, dy, dz, rr in ((0.0, 0.0, 0.0, 0.085), (sgn * 0.07, 0.02, -0.03, 0.065), (-sgn * 0.05, -0.05, -0.02, 0.06)):
            b.blob(Matrix.Translation((x + dx, CY + dy, zc - 0.02 + dz)) @ Matrix.Diagonal((rr * 1.2, rr * 1.3, rr, 1)), "cloud", us=7, vs=4)


def hat_harbor_treasure(b):
    """보물 상자 모자(에픽) — D 물건(루프#1366): 얕은 나무 받침 위에 머리 폭만 한 네모 상자(금 띠 둘·앞 자물쇠) + 뒤로 젖혀 열린 둥근
    뚜껑 + 상자 위로 수북한 금화 더미 + 앞으로 흘러내린 금화. 옆에서 뒤로 젖힌 뚜껑이 실루엣. 옛 꼴은 둥근 모자 위 작은 상자라
    달팽이와 같았다."""
    t = 0.02
    object_base(b, "wood", t, cuff="gold")
    W, D, H = 0.25, 0.17, 0.14                           # 상자 반폭 · 반깊이 · 높이
    z0 = base_z(CX, CY, t) - 0.04                        # 밑 — 받침 정수리에 4cm 묻힘(모서리는 받침 밖으로)
    box = [(-W, -D), (W, -D), (W, D), (-W, D)]
    b.plate(box, H, Matrix.Translation((CX, CY, z0)), "chest", inset=0.05)
    for sx in (-0.15, 0.15):                             # 금 띠
        b.plate([(sx - 0.022, -D - 0.006), (sx + 0.022, -D - 0.006), (sx + 0.022, D + 0.006), (sx - 0.022, D + 0.006)], H + 0.006,
                Matrix.Translation((CX, CY, z0)), "gold", inset=0.0)
    b.blob(Matrix.Translation((CX, CY - D - 0.012, z0 + H - 0.035)) @ Matrix.Diagonal((0.034, 0.014, 0.04, 1)), "gold", us=6, vs=4)
    zt = z0 + H
    b.blob(Matrix.Translation((CX, CY - 0.01, zt)) @ Matrix.Diagonal((W * 0.85, D * 0.85, 0.07, 1)), "coin", us=10, vs=5)
    lid = [(2 * D * j / 8, 0.08 * math.sin(math.pi * j / 8)) for j in range(9)]
    m = (Matrix.Translation((CX, CY + D, zt)) @ Matrix.Rotation(math.radians(-105), 4, "X")
         @ Matrix(((0, 0, -1, W), (-1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1))))
    b.plate(lid, 2 * W, m, "chest", inset=0.0)           # 뚜껑 — 뒤 모서리 경첩에서 105° 젖힘
    b.plate([(0.02, 0.0), (2 * D - 0.02, 0.0), (2 * D - 0.02, 0.012), (0.02, 0.012)], 2 * W + 0.01, m @ Matrix.Translation((0, 0, -0.005)), "gold", inset=0.0)
    for dx, dz, tilt in ((-0.1, 0.0, 30), (0.05, -0.04, -20), (0.14, 0.02, 50)):   # 앞으로 흘러내린 금화
        p = Vector((CX + dx, CY - D - 0.01, zt + dz))
        b.plate(circle(0.03, 8), 0.008, frame(p, Vector((0, -1, 0.6))) @ Matrix.Rotation(math.radians(tilt), 4, "Z"), "coin", inset=0.3)


def _cone(b, base, tip, rad, key, squash=1.0, sides=4):
    """뾰족 원뿔(귀·뿔·가시) — base 중심에서 tip 점까지. squash 는 밑면 한 방향(앞뒤)을 납작하게 눌러 귀처럼 만든다."""
    base, tip = Vector(base), Vector(tip)
    ax = (tip - base).normalized()
    u = Vector((1, 0, 0)) if abs(ax.x) < 0.9 else Vector((0, 1, 0))
    u = (u - ax * u.dot(ax)).normalized()
    w = ax.cross(u)
    ring = [b.bm.verts.new(base + rad * (u * math.cos(2 * math.pi * k / sides) + w * squash * math.sin(2 * math.pi * k / sides)))
            for k in range(sides)]
    apex = b.bm.verts.new(tip)
    z3 = [(0, 0, 0)] * 3
    for k in range(sides):
        b.face([ring[k], ring[(k + 1) % sides], apex], key, z3)
    b.face(ring[::-1], key, [(0, 0, 0)] * sides)


def hat_meadow_fox(b):
    """여우 모자(일반) — A 비니(루프#1362): 주황 껍질 + 바깥으로 길게 뻗은 뾰족 삼각뿔 귀 둘(안쪽 어두움·끝 검정) + 앞 흰 주둥이·검은 코 + 눈 둘."""
    beanie(b, "orange")
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.17, CY + 0.0
        z0 = ear_base(x, y) - 0.03
        tip = (x + sgn * 0.17, y, z0 + 0.25)
        _cone(b, (x, y, z0), tip, 0.105, "orange", squash=0.45)
        _cone(b, (x, y - 0.035, z0 + 0.02), (x + sgn * 0.13, y - 0.028, z0 + 0.19), 0.055, "dark", squash=0.3)
        _cone(b, (x + sgn * 0.137, y, z0 + 0.2), tip, 0.03, "black", squash=0.45)
    zr0 = seat(TF)
    p, n = dome_point(G + 0.02, TF, zr0 + 0.05)
    b.blob(frame(p + n * 0.012, n) @ Matrix.Diagonal((0.085, 0.06, 0.035, 1)), "white", us=7, vs=4)
    b.blob(frame(p + n * 0.034, n) @ Matrix.Diagonal((0.022, 0.017, 0.014, 1)), "black", us=5, vs=3)
    for sgn in (-1, 1):
        p, n = dome_point(G + 0.02, TF + sgn * math.radians(24), zr0 + 0.105)
        b.blob(frame(p + n * 0.012, n) @ Matrix.Diagonal((0.014, 0.014, 0.01, 1)), "black", us=5, vs=3)
def hat_meadow_pig(b):
    """돼지 모자(일반) — A 비니(루프#1362): 분홍 껍질 + 앞으로 크게 튀어나온 둥근 코(콧구멍 둘) + 옆으로 접혀 처진 세모 귀 둘 + 눈 둘."""
    beanie(b, "pink")
    for sgn in (-1, 1):
        x, y = CX + sgn * 0.22, CY - 0.02
        z0 = ear_base(x, y)
        _cone(b, (x, y, z0 - 0.01), (x + sgn * 0.2, y - 0.06, z0 - 0.07), 0.1, "pinkdark", squash=0.3)
    zr0 = seat(TF)
    p, n = dome_point(G + 0.02, TF, zr0 + 0.07)
    b.blob(frame(p + n * 0.05, n) @ Matrix.Diagonal((0.095, 0.075, 0.07, 1)), "snout", us=10, vs=5)
    for sgn in (-1, 1):
        b.blob(frame(p + n * 0.118 + Vector((sgn * 0.03, 0, 0)), n) @ Matrix.Diagonal((0.016, 0.024, 0.01, 1)), "pinkdark", us=4, vs=3)
        q, m = dome_point(G + 0.02, TF + sgn * math.radians(30), zr0 + 0.15)
        b.blob(frame(q + m * 0.012, m) @ Matrix.Diagonal((0.016, 0.016, 0.01, 1)), "black", us=5, vs=3)
def hat_harbor_sailboat(b):
    """돛단배 모자(희귀) — D 물건(루프#1366): 파란 물결 받침 위에 앞뒤로 머리보다 긴 나무 배 몸(빨간 띠, 30° 비스듬히) + 돛대 + 배 길이
    방향으로 선 큰 흰 돛·앞 돛 + 꼭대기 깃발. 비스듬히 두어 앞·옆 그림자 둘 다에 돛이 보인다. 옛 꼴은 둥근 모자 위 작은 돛이라
    무지개와 같았다."""
    t = 0.02
    top = object_base(b, "blue", t, cuff="sail")
    yaw = math.radians(30)
    c = Vector((CX, CY, top - 0.07))                     # 배 밑 — 받침에 7cm 묻힘(머리공 밖)
    hull = [(0.0, 0.0, "wood"), (0.07, 0.005, "wood"), (0.12, 0.03, "wood"), (0.14, 0.07, "red"), (0.148, 0.1, "wood"),
            (0.135, 0.11, "deck"), (0.0, 0.11, "deck")]
    lathe(b, c, hull, n=16, ky=2.8, yaw=yaw)            # 앞뒤 길이 = 0.148 × 2.8 = 0.41
    d = Vector((-math.sin(yaw), math.cos(yaw), 0))      # 배 길이 방향(뒤) — lathe 의 +y 를 yaw 만큼 돌린 축
    zd = c.z + 0.11
    b.tube([(CX, CY, zd - 0.02), (CX, CY, zd + 0.46)], 0.014, "mast", sides=5)
    ang = yaw + math.pi / 2                              # side_plate 의 u 축을 배 길이(뒤)로
    side_plate(b, [(0.02, 0.04), (0.3, 0.04), (0.02, 0.44)], 0.012, Vector((CX, CY, zd)), ang, "sail", inset=0.1)
    side_plate(b, [(-0.02, 0.06), (-0.3, 0.04), (-0.02, 0.36)], 0.012, Vector((CX, CY, zd)), ang, "sail", inset=0.1)
    side_plate(b, [(0.0, 0.0), (0.1, 0.025), (0.0, 0.05)], 0.01, Vector((CX, CY, zd + 0.41)), ang, "red", inset=0.1)


def hat_meadow_cheese(b):
    """치즈 모자(희귀) — D 물건(루프#1364 — 이미 따로 읽혀 꼴 그대로, 몸통만 `object_base`): 크림색 얕은 받침 위에 세운 노란 치즈 쐐기(앞에서 삼각형 · 앞면 큰 구멍 셋 · 아래 껍질 띠) + 모자 앞 작은 구멍."""
    t = 0.02
    top = object_base(b, "cream", t)
    thick = 0.2
    z0 = top - 0.005
    mat = Matrix.Translation((CX, CY + thick * 0.5, z0)) @ Matrix.Rotation(math.radians(90), 4, "X")
    b.plate([(-0.21, 0.02), (0.2, 0.0), (0.2, 0.29)], thick, mat, "cheese", inset=0.1)
    front = CY - thick * 0.5 - 0.004
    for dx, dz, r in ((0.12, 0.15, 0.038), (0.04, 0.1, 0.03), (0.15, 0.225, 0.022), (-0.07, 0.06, 0.022), (0.13, 0.07, 0.02)):
        b.blob(Matrix.Translation((CX + dx, front, z0 + dz)) @ Matrix.Diagonal((r, 0.012, r, 1)), "hole", us=7, vs=3)
    b.blob(Matrix.Translation((CX + 0.188, front, z0 + 0.145)) @ Matrix.Diagonal((0.012, 0.012, 0.14, 1)), "rind", us=4, vs=4)
    zr0 = seat(TF)
    p, n = dome_point(G + t, TF, zr0 + 0.05)
    b.blob(frame(p + n * 0.012, n) @ Matrix.Diagonal((0.04, 0.032, 0.02, 1)), "cheese", us=6, vs=4)
    b.blob(frame(p + n * 0.026, n) @ Matrix.Diagonal((0.014, 0.014, 0.01, 1)), "hole", us=5, vs=3)


def hat_meadow_dragon(b):
    """용 모자(에픽) — A 비니(루프#1363): 초록 껍질 + 위로 솟았다 뒤로 휘어 끝이 뒤·위를 향한 뿔 둘 + 이마에서 뒷목까지 이어진 주황 가시 줄(뒤로 갈수록 크고 테 밑까지) + 앞 코(콧구멍) + 노란 눈 둘."""
    t = 0.02
    beanie(b, "green", t, swell=0.04)
    for sgn in (-1, 1):                   # 뿔 — 굵은 관 둘 + 끝 원뿔로 점점 가늘게
        x0, y0 = CX + sgn * 0.12, CY - 0.03
        z0 = ear_base(x0, y0, t, 0.04) - 0.02
        pts = [Vector((x0 + sgn * dx, y0 + dy, z0 + dz)) for dx, dy, dz in
               ((0, 0, 0), (0.01, 0.015, 0.07), (0.025, 0.05, 0.14), (0.04, 0.11, 0.19), (0.055, 0.17, 0.21), (0.065, 0.23, 0.22),
                (0.075, 0.29, 0.24))]
        b.tube(pts[:3], 0.045, "horn", sides=6)
        b.tube(pts[2:5], 0.036, "horn", sides=6)
        _cone(b, pts[4] - (pts[5] - pts[4]) * 0.3, pts[6], 0.034, "horn", sides=6)
    spikes = []                           # 가시 — (밑 점, 바깥 방향, 높이)
    for y, h in ((-0.1, 0.06), (-0.02, 0.08), (0.06, 0.1)):
        z = ear_base(CX, CY + y, t, 0.04)
        spikes.append((Vector((CX, CY + y, z)), Vector((0, 0.25, 1)).normalized(), h))
    for z, h in ((CZ + 0.05, 0.11), (CZ - 0.02, 0.11), (seat(-TF) + 0.02, 0.09)):
        q, k = dome_point(G + t, -TF, z)
        spikes.append((q, (k + Vector((0, 0, 0.35))).normalized(), h))
    for q, k, h in spikes:
        _cone(b, q - k * 0.012, q + k * h, 0.028, "orange", squash=1.8, sides=4)
    zr0 = seat(TF)
    p, n = dome_point(G + t, TF, zr0 + 0.045)
    b.blob(frame(p + n * 0.02, n) @ Matrix.Diagonal((0.07, 0.05, 0.04, 1)), "lightgreen", us=7, vs=4)
    for sgn in (-1, 1):
        b.blob(frame(p + n * 0.056 + Vector((sgn * 0.024, 0, 0)), n) @ Matrix.Diagonal((0.011, 0.014, 0.01, 1)), "black", us=4, vs=3)
        q, m = dome_point(G + t, TF + sgn * math.radians(26), zr0 + 0.115)
        b.blob(frame(q + m * 0.016, m) @ Matrix.Diagonal((0.034, 0.034, 0.03, 1)), "yellow", us=6, vs=4)
        b.blob(frame(q + m * 0.042, m) @ Matrix.Diagonal((0.008, 0.022, 0.006, 1)), "black", us=4, vs=3)


HATS.update({
    "MeadowBeret": dict(build=hat_meadow_beret,
                        pal={"inside": ("#4A1424", 0.95, 0), "wine": ("#8E2A44", 0.85, 0), "band": ("#3A1020", 0.8, 0),
                             "stem": ("#3A1020", 0.8, 0)}),
    "HarborFez": dict(build=hat_harbor_fez,
                      pal={"inside": ("#5A1410", 0.95, 0), "red": ("#C0322A", 0.8, 0), "band": ("#8A1E1A", 0.8, 0), "gold": ("#E8BE50", 0.35, 0.6),
                           "black": ("#1E1A18", 0.5, 0)}),
    "MeadowBunny": dict(build=hat_meadow_bunny,
                        pal={"inside": ("#C9C6BA", 0.95, 0), "fur": ("#F4F2EA", 0.95, 0), "ear": ("#F4A6B4", 0.9, 0)}),
    "MeadowPumpkin": dict(build=hat_meadow_pumpkin,
                          pal={"inside": ("#8A4A10", 0.95, 0), "orange": ("#E8591A", 0.7, 0), "stem": ("#4E6A22", 0.85, 0),
                               "leaf": ("#3E9A2E", 0.7, 0)}),
    "HarborShark": dict(build=hat_harbor_shark,
                        pal={"inside": ("#4A525C", 0.95, 0), "gray": ("#7F8A98", 0.6, 0), "belly": ("#E4E8EC", 0.7, 0),
                             "white": ("#FBFBF6", 0.5, 0), "black": ("#1E1A18", 0.4, 0)}),
    "MeadowAcorn": dict(build=hat_meadow_acorn,
                        pal={"inside": ("#5A3A20", 0.95, 0), "rim": ("#D9B77A", 0.85, 0), "stem": ("#4A2C16", 0.8, 0),
                             "nut": ("#B06A30", 0.38, 0)},
                        pkeys={"cap": ("A", "z", 0.8, 0.99)},
                        patterns={"A": (pattern_quilt, ("#9A6A3C", "#6B4226"), 0.9)}),
    "HarborSouwester": dict(build=hat_harbor_souwester,
                            pal={"inside": ("#B38A12", 0.95, 0), "yellow": ("#F5C518", 0.5, 0), "band": ("#2E3F86", 0.7, 0),
                                 "button": ("#F7F6F0", 0.5, 0)}),
    "MeadowCrown": dict(build=hat_meadow_crown,
                        pal={"gold": ("#E8BE50", 0.35, 0.6), "ruby": ("#D6283A", 0.2, 0.1), "gem": ("#4FC3E8", 0.2, 0.1),
                             "pearl": ("#FBF8EE", 0.3, 0)}),
    "HarborBandana": dict(build=hat_harbor_bandana,
                          pal={"inside": ("#7A1E1E", 0.95, 0), "red": ("#C93A3A", 0.8, 0), "white": ("#F7F6F0", 0.6, 0),
                               "black": ("#1E1C1C", 0.4, 0)}),
    "MeadowWizard": dict(build=hat_meadow_wizard,
                         pal={"inside": ("#1A1F4A", 0.95, 0), "navy": ("#2A3278", 0.75, 0), "band": ("#E8BE50", 0.4, 0.5),
                              "gold": ("#F2C84B", 0.35, 0.6)},
                         pkeys={"cone": ("A", "z", 0.9, 1.27)},
                         patterns={"A": (pattern_stripes, ("#2A3278", "#323C8C"), 0.8)}),
    "MeadowSunflower": dict(build=hat_meadow_sunflower,
                            pal={"inside": ("#4A2E14", 0.95, 0), "seed": ("#5A3A1C", 0.9, 0), "petal": ("#F7C61B", 0.6, 0),
                                 "leaf": ("#4E8A2E", 0.7, 0)}),
    "HarborDixie": dict(build=hat_harbor_dixie,
                         pal={"inside": ("#C9C6BA", 0.95, 0), "white": ("#F4F2EA", 0.8, 0), "navy": ("#1F2F66", 0.7, 0)}),
    "HarborHorned": dict(build=hat_harbor_horned,
                         pal={"inside": ("#3A3D44", 0.9, 0), "steel": ("#9AA3AE", 0.35, 0.7), "band": ("#4A4F58", 0.45, 0.6),
                              "horn": ("#EADFC0", 0.55, 0), "gold": ("#E8BE50", 0.35, 0.6)}),
    "MeadowMushroom": dict(build=hat_meadow_mushroom,
                           pal={"gills": ("#E8D3A8", 0.9, 0), "red": ("#D23A32", 0.6, 0), "dot": ("#FBF8EE", 0.6, 0)}),
    "MeadowTopHat": dict(build=hat_meadow_tophat,
                         pal={"inside": ("#15151C", 0.95, 0), "black": ("#24242E", 0.55, 0), "band": ("#7A3BA8", 0.55, 0),
                              "leaf": ("#4E8A2E", 0.7, 0), "pink": ("#F08CB4", 0.6, 0), "sun": ("#F7C61B", 0.6, 0)}),
    "MeadowCowboy": dict(build=hat_meadow_cowboy,
                         pal={"inside": ("#4A2E18", 0.95, 0), "brown": ("#9A6A3C", 0.8, 0), "band": ("#3E2412", 0.8, 0),
                              "gold": ("#E8BE50", 0.35, 0.6)}),
    "MeadowBear": dict(build=hat_meadow_bear,
                       pal={"inside": ("#4A2E18", 0.95, 0), "fur": ("#8A5A32", 0.95, 0), "ear": ("#E9B98C", 0.9, 0),
                            "nose": ("#1E1A18", 0.4, 0)}),
    "MeadowCatEars": dict(build=hat_meadow_catears,
                          pal={"inside": ("#9A5A18", 0.95, 0), "fur": ("#F29A3A", 0.95, 0), "stripe": ("#B8641C", 0.95, 0),
                               "rim": ("#FBE3B8", 0.9, 0), "ear": ("#F4A6B4", 0.9, 0)}),
    "HarborTricorn": dict(build=hat_harbor_tricorn,
                          pal={"inside": ("#15151C", 0.95, 0), "black": ("#24242E", 0.6, 0), "gold": ("#E8BE50", 0.35, 0.6),
                               "ruby": ("#D6283A", 0.3, 0.1), "plume": ("#F7F6F0", 0.85, 0)}),
    "MeadowPropeller": dict(build=hat_meadow_propeller,
                            pal={"inside": ("#6A6A74", 0.95, 0), "white": ("#F7F6F0", 0.7, 0), "c0": ("#E8392F", 0.7, 0),
                                 "c1": ("#F7C61B", 0.7, 0), "c2": ("#2F6FD6", 0.7, 0), "c3": ("#38A84A", 0.7, 0)}),
    "MeadowLadybug": dict(build=hat_meadow_ladybug,
                          pal={"inside": ("#8E1F1F", 0.95, 0), "red": ("#E03A2E", 0.55, 0), "black": ("#1E1A1A", 0.5, 0)}),
    "HarborLighthouse": dict(build=hat_harbor_lighthouse,
                             pal={"inside": ("#B9B6AA", 0.95, 0), "white": ("#F4F2EA", 0.8, 0), "red": ("#C93A3A", 0.7, 0),
                                  "black": ("#24242E", 0.5, 0), "yellow": ("#FFE066", 0.3, 0)}),
    "MeadowFlowerpot": dict(build=hat_meadow_flowerpot,
                            pal={"inside": ("#6E3A1E", 0.95, 0), "clay": ("#C9693A", 0.9, 0), "rim": ("#B05A2E", 0.9, 0),
                                 "soil": ("#4A2E18", 0.95, 0), "leaf": ("#4E9A34", 0.7, 0), "pink": ("#F08CB4", 0.6, 0),
                                 "sun": ("#F7C61B", 0.6, 0)}),
    "MeadowCarrot": dict(build=hat_meadow_carrot,
                         pal={"inside": ("#9A4A10", 0.95, 0), "orange": ("#F08A24", 0.7, 0), "dark": ("#C86A14", 0.8, 0),
                              "leaf": ("#4E9A34", 0.7, 0)}),
    "HarborJellyfish": dict(build=hat_harbor_jellyfish,
                            pal={"inside": ("#5A3A8A", 0.95, 0), "bell": ("#B88AF0", 0.4, 0), "rim": ("#E5A6E8", 0.5, 0),
                                 "dot": ("#FFFFFF", 0.5, 0), "tent": ("#F08CC8", 0.5, 0)}),
    "HarborSailorCap": dict(build=hat_harbor_sailor_cap,
                         pal={"inside": ("#B9B6AA", 0.95, 0), "white": ("#F7F6F0", 0.8, 0), "navy": ("#23366B", 0.7, 0),
                              "gold": ("#E8BE50", 0.35, 0.6)}),
    "MeadowTeapot": dict(build=hat_meadow_teapot,
                         pal={"inside": ("#8C8574", 0.95, 0), "cream": ("#F3E8CF", 0.45, 0), "blue": ("#3F78C4", 0.45, 0),
                              "gold": ("#E8BE50", 0.35, 0.6)}),
    "MeadowOwl": dict(build=hat_meadow_owl,
                      pal={"inside": ("#4A3220", 0.95, 0), "brown": ("#8A5A34", 0.9, 0), "dark": ("#5A3A22", 0.9, 0),
                           "tan": ("#D8B684", 0.85, 0), "white": ("#FBFAF2", 0.5, 0), "black": ("#1E1A1A", 0.4, 0),
                           "orange": ("#F0922A", 0.6, 0)}),
    "HarborCrab": dict(build=hat_harbor_crab,
                       pal={"inside": ("#7A1E1A", 0.95, 0), "red": ("#DD3A2A", 0.55, 0), "dark": ("#A82A20", 0.6, 0),
                            "orange": ("#F59A3A", 0.6, 0), "white": ("#FBFAF2", 0.5, 0), "black": ("#1E1A1A", 0.4, 0)}),
    "MeadowCake": dict(build=hat_meadow_cake,
                       pal={"inside": ("#8A5A6A", 0.95, 0), "sponge": ("#F2A6BE", 0.8, 0), "cream": ("#FFF6EA", 0.6, 0),
                            "berry": ("#D6283A", 0.45, 0), "leaf": ("#4E9A34", 0.7, 0), "candle": ("#F7D44A", 0.6, 0),
                            "flame": ("#FF8A1E", 0.3, 0)}),
    "MeadowSnail": dict(build=hat_meadow_snail,
                        pal={"inside": ("#4E7A2A", 0.95, 0), "green": ("#8CCB4A", 0.8, 0), "belly": ("#C9E49A", 0.85, 0),
                             "shell": ("#B8793C", 0.8, 0), "swirl": ("#E6B97A", 0.8, 0), "white": ("#FBFAF2", 0.5, 0),
                             "black": ("#1E1A1A", 0.4, 0)}),
    "HarborBuoy": dict(build=hat_harbor_buoy,
                       pal={"inside": ("#8A2A24", 0.95, 0), "red": ("#D9382E", 0.6, 0), "white": ("#F7F6F0", 0.7, 0),
                            "black": ("#24242E", 0.5, 0), "yellow": ("#FFE066", 0.3, 0), "rope": ("#C9A66B", 0.9, 0)}),
    "MeadowPineapple": dict(build=hat_meadow_pineapple,
                            pal={"inside": ("#9A7A12", 0.95, 0), "yellow": ("#F5C42A", 0.7, 0), "dark": ("#B8741C", 0.8, 0),
                                 "leaf": ("#3E9A3A", 0.7, 0)},
                            pkeys={"skin": ("A", "z", 0.82, 1.27)},
                            patterns={"A": (pattern_scales, ("#F5C42A", "#A8641A"), 0.7)}),
    "HarborStarfish": dict(build=hat_harbor_starfish,
                           pal={"inside": ("#8A3A1A", 0.95, 0), "orange": ("#F2803A", 0.7, 0), "dark": ("#C85A22", 0.8, 0),
                                "white": ("#FBF1DC", 0.6, 0), "black": ("#1E1A1A", 0.4, 0)}),
    "MeadowRocket": dict(build=hat_meadow_rocket,
                         pal={"inside": ("#A9A79E", 0.95, 0), "white": ("#F4F4F0", 0.55, 0), "red": ("#E0392E", 0.55, 0),
                              "gray": ("#8A929C", 0.4, 0.6), "blue": ("#4FB6F0", 0.2, 0.1)}),
    "MeadowDuck": dict(build=hat_meadow_duck,
                       pal={"inside": ("#9A7A12", 0.95, 0), "yellow": ("#FFD83A", 0.7, 0), "orange": ("#F58A1E", 0.6, 0),
                            "white": ("#FBFAF2", 0.5, 0), "black": ("#1E1A1A", 0.4, 0)}),
    "MeadowBeehive": dict(build=hat_meadow_beehive,
                          pal={"inside": ("#7A5A12", 0.95, 0), "honey": ("#F0B02A", 0.7, 0), "dark": ("#B87A1C", 0.8, 0),
                               "black": ("#2A1E14", 0.5, 0), "white": ("#FBFAF2", 0.5, 0),
                               "bee": ("#F7C61B", 0.6, 0)}),
    "HarborConch": dict(build=hat_harbor_conch,
                        pal={"inside": ("#9A5A66", 0.95, 0), "pink": ("#F4A6B4", 0.6, 0), "cream": ("#FFF0DC", 0.5, 0),
                             "shell": ("#F2C09A", 0.5, 0), "white": ("#FFFFFF", 0.3, 0),
                             "lip": ("#F28CA0", 0.45, 0)}),
    "MeadowCactus": dict(build=hat_meadow_cactus,
                         pal={"inside": ("#2E5A22", 0.95, 0), "green": ("#4EA83A", 0.8, 0), "pink": ("#F2689A", 0.6, 0),
                              "white": ("#FBFAF2", 0.5, 0), "sand": ("#E6C88A", 0.9, 0), "pebble": ("#A88A62", 0.9, 0),
                              "yellow": ("#FFE066", 0.4, 0)}),
    "MeadowUfo": dict(build=hat_meadow_ufo,
                      pal={"inside": ("#6A6E78", 0.95, 0), "gray": ("#AEB4BE", 0.4, 0.6), "dark": ("#5A606C", 0.5, 0.5),
                           "glass": ("#7CE0A8", 0.15, 0.1), "alien": ("#5FD06A", 0.6, 0), "black": ("#1E1A1A", 0.4, 0),
                           "yellow": ("#FFE066", 0.3, 0)}),
    "MeadowPenguin": dict(build=hat_meadow_penguin,
                          pal={"inside": ("#20242C", 0.95, 0), "black": ("#14161C", 0.7, 0), "white": ("#FFFFFF", 0.6, 0),
                               "orange": ("#F58A1E", 0.6, 0)}),
    "MeadowDonut": dict(build=hat_meadow_donut,
                        pal={"inside": ("#7A5428", 0.95, 0), "dough": ("#D9A05A", 0.8, 0), "icing": ("#FF6FB0", 0.5, 0), "hole": ("#4A2C14", 0.9, 0),
                             "red": ("#E0392E", 0.5, 0), "blue": ("#4FA6F0", 0.5, 0), "yellow": ("#FFE066", 0.5, 0),
                             "white": ("#FFFFFF", 0.5, 0), "green": ("#5FD06A", 0.5, 0)}),
    "MeadowMouse": dict(build=hat_meadow_mouse,
                        pal={"inside": ("#6A6E78", 0.95, 0), "gray": ("#B4B8C2", 0.8, 0), "pink": ("#F4A6B4", 0.6, 0),
                             "black": ("#1E1A1A", 0.4, 0), "white": ("#FBFAF2", 0.5, 0)}),
    "HarborIgloo": dict(build=hat_harbor_igloo,
                        pal={"inside": ("#7C9AB4", 0.95, 0), "ice": ("#BFE0F4", 0.4, 0), "snow": ("#F4FAFF", 0.6, 0),
                             "dark": ("#2A3A4E", 0.6, 0)}),
    "HarborRainbow": dict(build=hat_harbor_rainbow,
                          pal={"inside": ("#4A86B0", 0.95, 0), "sky": ("#8CCBF0", 0.6, 0), "red": ("#E8402E", 0.5, 0),
                               "orange": ("#F58A1E", 0.5, 0), "yellow": ("#FFD83A", 0.5, 0), "green": ("#4EA83A", 0.5, 0),
                               "cloud": ("#FFFFFF", 0.7, 0)}),
    "HarborTreasure": dict(build=hat_harbor_treasure,
                           pal={"inside": ("#3A2412", 0.95, 0), "wood": ("#8A5A2E", 0.85, 0), "chest": ("#A8702E", 0.8, 0),
                                "gold": ("#F2C23A", 0.3, 0.7), "coin": ("#FFD84A", 0.25, 0.7)}),
    "MeadowFox": dict(build=hat_meadow_fox,
                      pal={"inside": ("#7A3A14", 0.95, 0), "orange": ("#E8782A", 0.8, 0), "dark": ("#5A2A18", 0.8, 0),
                           "white": ("#FBF4E8", 0.7, 0), "black": ("#1E1A1A", 0.4, 0)}),
    "MeadowPig": dict(build=hat_meadow_pig,
                      pal={"inside": ("#9A5A6E", 0.95, 0), "pink": ("#F6B0C0", 0.8, 0), "pinkdark": ("#E88AA2", 0.7, 0),
                           "snout": ("#F49AB0", 0.6, 0), "black": ("#1E1A1A", 0.4, 0)}),
    "HarborSailboat": dict(build=hat_harbor_sailboat,
                           pal={"inside": ("#4A2E16", 0.95, 0), "wood": ("#9A6430", 0.85, 0), "mast": ("#5A3A1C", 0.8, 0),
                                "sail": ("#FAF6EC", 0.7, 0), "red": ("#E0392E", 0.5, 0), "blue": ("#2F6FB8", 0.6, 0),
                                "deck": ("#D8A868", 0.85, 0)}),
    "MeadowCheese": dict(build=hat_meadow_cheese,
                         pal={"inside": ("#8A6A20", 0.95, 0), "cream": ("#F2E2A0", 0.8, 0), "cheese": ("#F6C838", 0.6, 0),
                              "hole": ("#C8921C", 0.7, 0), "rind": ("#E8A82A", 0.7, 0)}),
    "MeadowDragon": dict(build=hat_meadow_dragon,
                         pal={"inside": ("#1E4A24", 0.95, 0), "green": ("#3E9E48", 0.6, 0), "lightgreen": ("#7CCB6A", 0.6, 0),
                              "horn": ("#F0E6C8", 0.5, 0), "orange": ("#F2822A", 0.5, 0), "yellow": ("#FFE066", 0.3, 0),
                              "black": ("#1E1A1A", 0.4, 0)}),
})


def hat_meadow_frog(b):
    """청개구리 모자(희귀) — A 비니(루프#1362): 연두 껍질 + 꽃무늬 접은 테 + 꼭대기 앞에 크게 솟은 눈알 둘(흰자·눈동자·빛점) + 볼 분홍."""
    beanie(b, "skin", t=0.03, cuff="cuff", cuff_h=0.06, swell=0.02)
    for sgn in (-1, 1):
        th = TF + sgn * math.radians(32)
        x, y = CX + sgn * 0.13, CY - 0.15
        c = Vector((x, y, ear_base(x, y, t=0.03, swell=0.02) + 0.06))
        b.blob(Matrix.Translation(c) @ Matrix.Diagonal((0.1, 0.095, 0.1, 1)), "skin", us=10, vs=6)
        f = (radial(th) * 0.5 + Vector((0, -1, 0)) * 0.5 + Vector((0, 0, 0.3))).normalized()
        eye = c + f * 0.09
        b.plate(circle(0.05, 14), 0.012, frame(eye, f), "eye_w")
        b.plate(circle(0.03, 10), 0.01, frame(eye + f * 0.01, f), "pupil")
        b.plate(circle(0.008, 5), 0.004, frame(eye + f * 0.02 + Vector((sgn * 0.008, 0, 0.012)), f), "eye_w")
    for sgn in (-1, 1):
        th = TF + sgn * math.radians(58)
        p, n = dome_point(G + 0.03, th, seat(th) + 0.08)
        b.blob(frame(p, n) @ Matrix.Diagonal((0.026, 0.018, 0.006, 1)), "cheek", us=8, vs=4)


def hat_harbor_beanie(b):
    """항구 줄무늬 방울 비니(일반) — A 비니(루프#1363): 남색·흰 줄 껍질 + 높게 접은 골지 테 + 꼭대기에서 솟아 옆으로 늘어진 뜨개 꼬리 + 꼬리 끝 머리 옆에 매달린 큰 흰 방울."""
    t = 0.03
    top = beanie(b, "stripe", t, cuff="cuff", cuff_h=0.064, swell=0.05)
    pts = [Vector((CX + dx, CY + dy, top + dz)) for dx, dy, dz in
           ((0.0, 0.0, -0.04), (0.02, 0.0, 0.04), (0.08, 0.01, 0.09), (0.16, 0.02, 0.1), (0.23, 0.03, 0.06), (0.28, 0.04, -0.01))]
    b.tube(pts[:3], 0.07, "stripe", sides=8)
    b.tube(pts[2:5], 0.05, "stripe", sides=8)
    b.tube(pts[4:], 0.034, "stripe", sides=6)
    c = pts[-1] + Vector((0.02, 0.0, -0.05))
    for k in range(6):   # 방울 — 공 여섯을 뭉쳐 보풀진 덩어리로
        a = 2 * math.pi * k / 5
        off = Vector((math.cos(a) * 0.026, math.sin(a) * 0.026, 0.01 * math.sin(3 * a))) if k else Vector((0, 0, 0.0))
        b.blob(Matrix.Translation(c + off) @ Matrix.Diagonal((0.05, 0.05, 0.048, 1)), "pom", us=8, vs=5)


def hat_harbor_whale(b):
    """고래 모자(에픽) — D 물건(루프#1366): 남색 고래 몸 — 머리를 감싼 몸이 뒤로 부풀어 머리보다 앞뒤로 길고(배는 흰 줄무늬 테) + 뒤
    끝에서 위로 휘어 솟은 꼬리와 옆으로 넓은 꼬리지느러미 + 정수리 물줄기 + 옆 눈. 옆에서 긴 몸과 솟은 꼬리, 앞에서 꼬리지느러미가
    실루엣. 옛 꼴은 머리 돔 + 작은 꼬리라 새싹 모자와 같았다(틀 `hats_v2_make.py` 행을 덮어 씀)."""
    t = 0.034
    zt = CZ + HC + G + t + 0.04

    def prof(th):
        z = seat(th, 0.014)
        ri, ro = head_r(z, G), head_r(z, G + t)
        back = max(0.0, -fw(th)) ** 1.5                  # 뒤쪽일수록 1 — 몸이 뒤로 길게
        pts = inner(z, "inside")
        pts += [(ri + 0.006, z - 0.012, "cuff"), (ro + 0.008, z - 0.008, "cuff"), (ro + 0.012, z + 0.03, "cuff"),
                (ro + 0.008, z + 0.058, "cuff"), (ro + 0.002, z + 0.064, "belly")]
        k = "belly"
        for r, zz in dome(G + t, z + 0.068, 9, stretch=0.04):
            if zz > z + 0.11:
                k = "body"
            f = max(0.0, min(1.0, (zz - z) / (zt - z)))
            pts.append((r + 0.17 * back * math.sin(math.pi * f) ** 0.8 if r > 0 else 0.0, zz, k))
        return pts
    b.revolve(prof)
    yb = CY + HB + G + t + 0.17                          # 몸 뒤 끝
    zb = (seat(-TF, 0.014) + zt) / 2
    tail = [Vector((CX, yb - 0.06, zb)), Vector((CX, yb + 0.03, zb + 0.05)), Vector((CX, yb + 0.07, zb + 0.15)), Vector((CX, yb + 0.06, zb + 0.24))]
    b.tube(tail[:2], 0.07, "body", sides=8)
    b.tube(tail[1:], 0.05, "body", sides=8)
    fluke = [(0.0, -0.03), (0.08, 0.0), (0.17, 0.07), (0.2, 0.13), (0.13, 0.1), (0.05, 0.08), (0.0, 0.05),
             (-0.05, 0.08), (-0.13, 0.1), (-0.2, 0.13), (-0.17, 0.07), (-0.08, 0.0)]
    b.plate(fluke, 0.03, frame(tail[-1] + Vector((0, -0.015, -0.03)), Vector((0, 1, 0.5))), "body", inset=0.3)
    for k, (dx, dz, rr) in enumerate(((0.0, 0.07, 0.035), (-0.05, 0.12, 0.03), (0.05, 0.12, 0.03), (0.0, 0.16, 0.03))):   # 물줄기
        b.blob(Matrix.Translation((CX + dx, CY - 0.02, zt + dz)) @ Matrix.Diagonal((rr, rr, rr * 1.3, 1)), "water", us=6, vs=4)
    b.tube([(CX, CY - 0.02, zt - 0.02), (CX, CY - 0.02, zt + 0.1)], 0.02, "water", sides=5)
    for sgn in (-1, 1):   # 눈 — 옆 앞 55°
        th = TF + sgn * math.radians(55)
        p, n = dome_point(G + t, th, 0.9)
        b.plate(circle(0.034, 10), 0.008, frame(p - n * 0.002, n), "eye_w")
        b.plate(circle(0.024, 8), 0.008, frame(p + n * 0.004, n), "pupil")


def hat_meadow_bucket(b):
    """들꽃 벙거지(일반) — B 챙(루프#1367): 낮고 둥근 꽃무늬 운두 + 연두 띠 + 사방으로 비스듬히 크게 처진 챙(앞은 얕게) + 띠 옆에
    꽂아 운두 위로 솟은 큰 들꽃 한 송이(줄기·잎) — 그림자 한쪽에 꽃이 솟아 낚시꾼 벙거지와 갈린다."""
    brim(b, crown_dome("crown", 0.0, t=0.022, band=("band", 0.04)), 0.13,
         lambda th, u: -0.085 * u ** 1.1 * (1 - 0.7 * max(0.0, fw(th)) ** 2), thick=0.012,
         keys=("inside", "brim", "brim"), drop=0.0)
    th = TF + math.radians(120)                                  # 왼쪽 뒤 띠에 꽂음
    z = seat(th, 0.0) + 0.02
    p0 = surf(th, head_r(z, G) + 0.02 + 0.008, z)
    out = radial(th)
    p1 = p0 + out * 0.07 + Vector((0, 0, 0.12))
    p2 = p0 + out * 0.1 + Vector((0, 0, 0.27))
    b.tube([p0, p1, p2], 0.008, "band", sides=5)
    b.plate(leaf_outline(0.09, 0.04, n=5), 0.006, frame(p1, Vector((-0.3, -1, 0.2))) @ Matrix.Rotation(math.radians(30), 4, "Z"), "band")
    n = Vector((-0.55, -1.0, 0.25)).normalized()                 # 앞·옆 둘 다에 꽃 얼굴
    m = frame(p2, n)
    b.plate(flower_outline(0.085), 0.012, m, "petal")
    b.blob(m @ Matrix.Translation((0, 0, 0.012)) @ Matrix.Diagonal((0.03, 0.03, 0.016, 1)), "heart", us=8, vs=4)


def hat_meadow_cap(b):
    """들꽃 챙모자(일반) — C 캡(루프#1368): 꽃무늬 둥근 캡 + 길게 나온 연두 앞챙(0.2) + 뒤 목을 덮어 내려오며 벌어지는 크림색
    햇볕 가리개 + 앞 꽃 배지. 옛 꼴은 달리기 모자와 같은 캡(앞챙 0.13)이라 0.972 — 옆에서 앞으로 긴 챙과 뒤로 처진 가리개,
    앞에서 머리 양옆 아래로 벌어진 가리개가 실루엣(틀 `hats_v2_make.py` 행을 덮어 씀)."""
    t = 0.024
    cap(b, crown_dome("panel", 0.016, t=t), 0.2, span=math.radians(62), droop=0.018,
        keys=("inside", "visor", "visor"), drop=0.0)
    back, full, span = TF + math.pi, math.radians(60), math.radians(105)

    def flap(th):                                        # 뒤 가리개 단면 — 운두 겉 테에서 아래 바깥으로
        # 뒤 ±60° 는 제 길이, 정옆을 15° 지나서야 줄어든다 — 게임은 뒤 반의 가장 낮은 아랫단 아래만 머리카락을 남기므로(`UpdateHair`)
        # 정옆에서 가리개가 짧으면 그 사이 옆머리가 맨살로 보인다(첫 판 시험 `NoBareBackOfHead` 둘레 93° 살색 선 2).
        d = abs(math.atan2(math.sin(th - back), math.cos(th - back)))
        w = math.cos(min(1.0, max(0.0, d - full) / (span - full)) * math.pi / 2) ** 0.5
        z0 = seat(th, 0.0) + 0.012
        r0 = head_r(z0, G + t) + 0.002
        L, out = 0.02 + 0.1 * w, 0.012 + 0.04 * w
        return [(r0, z0), (r0 + out, z0 - L), (r0 + out + 0.01, z0 - L), (r0 + 0.01, z0 + 0.004)]
    b.sweep(flap, back - span, back + span, 16, "flap")
    top = CZ + HC + G + t + 0.016
    b.blob(Matrix.Translation((CX, CY, top)) @ Matrix.Diagonal((0.022, 0.022, 0.012, 1)), "visor")
    flower_badge(b, TF, 0.94, G + t, s=0.05)


HATS["MeadowFrog"]["build"] = hat_meadow_frog   # 틀(hats_v2_make.py)의 청개구리를 A 비니로 덮어 쓴다
HATS["HarborBeanie"]["build"] = hat_harbor_beanie   # 틀의 줄무늬 비니도 A 비니로 덮어 쓴다(루프#1363)
HATS["HarborWhale"]["build"] = hat_harbor_whale     # 틀의 고래도 D 물건으로 덮어 쓴다(루프#1366)
HATS["MeadowBucket"]["build"] = hat_meadow_bucket   # 틀의 들꽃 벙거지도 B 챙으로 덮어 쓴다(루프#1367)
HATS["MeadowBucket"]["pkeys"] = {"crown": ("A", "z", 0.86, 1.06)}   # 꽃무늬 높이 = 새 운두(띠 위 ~ 꼭대기 1.05)
HATS["MeadowCap"]["build"] = hat_meadow_cap         # 틀의 들꽃 챙모자도 C 캡으로 덮어 쓴다(루프#1368)
HATS["MeadowCap"]["pal"]["flap"] = ("#F3E8CC", 0.95, 0)            # 뒤 가리개 = 꽃무늬 바탕 크림색
HATS["HarborCaptain"]["build"] = hat_harbor_captain   # 틀의 선장모도 C 캡으로 덮어 쓴다(루프#1369)


def main_more():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    os.makedirs(TMP, exist_ok=True)
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else MORE_ORDER
    infos = []
    for name in only:
        os.makedirs(HATS_DIR + name, exist_ok=True)
        globals()["S"] = S_BY_HAT.get(name, 32)
        infos.append(export(name, build_hat(name)))
        print("HATINFO", json.dumps(infos[-1], ensure_ascii=False))
    cam = setup_body()
    for info in infos:
        render_check(info["name"], info, cam)


main_more()
