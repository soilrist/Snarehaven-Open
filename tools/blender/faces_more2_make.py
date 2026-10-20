"""꾸미기 얼굴 장식 +4 둘째 묶음 (M87-02, 루프#884) — 항구 얼굴 두건·뺨 별 스티커·하트 안경·흰 수염을 절차적으로 만든다.

실행 (헤드리스만):
  HATS_TMP=<임시 폴더>/ ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      -P tools/blender/faces_more2_make.py [-- HarborKerchief ...]

`faces_more_make.py`(M87-01)의 `main_more()` 만 빼고 불러와 — 그 안에서 `face_make.py` 틀도 같이 불린다 — `FACES` 에 네 벌을 더한다.
그래서 `Socket_Face` 에 바로 앉고 치수 규칙이 같다. 이름·모양은 새것(레퍼런스 원본 이름·에셋을 쓰지 않는다).
"""
import math

FACES_MORE = "~/UNREAL/Snarehaven/tools/blender/faces_more_make.py"
_here2 = globals().get("__file__")
globals()["__file__"] = FACES_MORE
_code2 = open(FACES_MORE, encoding="utf-8").read()
assert _code2.rstrip().endswith("main_more()"), "얼굴 장식 +4 틀 끝이 main_more() 가 아니다"
exec(compile(_code2.rstrip()[:-len("main_more()")], FACES_MORE, "exec"))
globals()["__file__"] = _here2

MORE2_ORDER = ["HarborKerchief", "MeadowCheekStars", "MeadowHeartShades", "HarborWhiteBeard"]


def heart_outline(width, n=24):
    """하트 외곽 — 폭 width, 가운데(0,0), 위가 +y."""
    s = width / 32.0
    pts = []
    for k in range(n):
        t = 2 * math.pi * k / n
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        pts.append((x * s, (y - 2.5) * s))
    return pts


# ---------------------------------------------------------------- 네 벌
def cloth_gap(z):
    """두건 천이 앞면에서 뜨는 틈 — 턱 쪽은 머리가 둥글게 들어가니 따라 들어온다."""
    return 0.006 - 0.02 * max(0.0, (0.74 - z) / 0.12) ** 2


def face_kerchief(b):
    """항구 얼굴 두건(일반) — 코 밑에서 턱까지 가리는 남색 천(아래로 좁아지는 V, 두께 있는 한 장) + 흰 점 + 머리 뒤 끈과 매듭."""
    rows = [(0.738, 0.17), (0.718, 0.165), (0.698, 0.155), (0.678, 0.14), (0.658, 0.12), (0.638, 0.095), (0.618, 0.07), (0.600, 0.04)]
    cols, thick = 12, 0.012
    z4 = [(0, 0, 0)] * 4
    fr, bk = [], []
    for z, half in rows:
        gap = cloth_gap(z)
        xs = [HX + half * (2 * k / cols - 1) for k in range(cols + 1)]
        fr.append([b.bm.verts.new(Vector((x, face_y(x, gap) - thick, z))) for x in xs])
        bk.append([b.bm.verts.new(Vector((x, face_y(x, gap), z))) for x in xs])
    for r in range(len(rows) - 1):
        for k in range(cols):
            b.face([fr[r + 1][k], fr[r + 1][k + 1], fr[r][k + 1], fr[r][k]], "navy", z4)
            b.face([bk[r][k], bk[r][k + 1], bk[r + 1][k + 1], bk[r + 1][k]], "navy", z4)
        b.face([fr[r][0], fr[r + 1][0], bk[r + 1][0], bk[r][0]], "navy", z4)
        b.face([fr[r + 1][cols], fr[r][cols], bk[r][cols], bk[r + 1][cols]], "navy", z4)
    for k in range(cols):
        b.face([fr[0][k], fr[0][k + 1], bk[0][k + 1], bk[0][k]], "navy", z4)
        b.face([bk[-1][k], bk[-1][k + 1], fr[-1][k + 1], fr[-1][k]], "navy", z4)
    # 흰 점무늬 — 천 겉에
    for dx, z in ((-0.07, 0.715), (0.0, 0.725), (0.07, 0.715), (-0.045, 0.675), (0.045, 0.675), (0.0, 0.64)):
        p = Vector((HX + dx, face_y(HX + dx, cloth_gap(z)) - thick - 0.001, z))
        b.plate(circle(0.008, 8), 0.002, frame(p, FWD), "white", inset=0.1)
    # 끈 — 천 윗줄 양끝에서 머리 뒤로 한 바퀴 (남색), 뒤 가운데 매듭 + 꼬리 둘
    t0, t1 = -math.pi / 2 + 0.78, -math.pi / 2 - 0.78 + 2 * math.pi
    n = 14
    pts = [band_point(t0 + (t1 - t0) * k / n, 0.008, 0.738) for k in range(n + 1)]
    ends = [Vector((HX + sx * 0.17, face_y(HX + sx * 0.17, 0.006) - thick * 0.5, 0.738)) for sx in (1, -1)]
    pts = [ends[0]] + pts + [ends[1]]
    b.tube(pts, 0.006, "navy", sides=4)
    # 루프#1358 — 매듭은 머리카락 겉에 붙이고(틈 0.016 → 0.010), 꼬리는 뒤로 뻗지 않고 뒤통수 곡면을 따라 내려간다
    # (옛 꼬리는 뒤·아래로 곧게 뻗어 머리카락 밖 6.3cm 에 떴다 — 2층 `FaceStrapHugsHead`).
    knot = band_point(math.pi / 2, 0.010, 0.738)
    b.blob(Matrix.Translation(knot) @ Matrix.Diagonal((0.022, 0.016, 0.02, 1)), "white", us=6, vs=4)
    for sgn in (-1, 1):
        b.tube([knot, head_point(math.pi / 2 - sgn * 0.12, 0.010, 0.738 - 0.045),
                head_point(math.pi / 2 - sgn * 0.18, 0.010, 0.738 - 0.09)], 0.008, "navy", sides=4)


def head_point(t, gap, z):
    """둘레 자리 t·높이 z 의 머리 겉(머리카락 포함) 점 + 틈 — 눈 높이 둘레(`band_point`)를 머리 초타원체 높이만큼 오므린다."""
    centre = Vector((BAND_C[0], BAND_C[1], z))
    d = band_point(t, 0.0, z) - centre
    k = max(0.0, 1.0 - abs((z - HEAD_CZ) / HEAD_R) ** BAND_N) ** (1.0 / BAND_N)
    return centre + d * k + d.normalized() * gap


def face_cheek_stars(b):
    """뺨 별 스티커(일반) — 양 뺨에 분홍 별 하나씩 + 노란 작은 별 + 하얀 반짝 점."""
    for sgn in (-1, 1):
        x = HX + sgn * 0.115
        y = face_y(x, 0.0) - 0.008
        b.plate(star_outline(0.04, 0.019), 0.004, front_frame(x, 0.708, y), "pink", inset=0.2)
        b.plate(star_outline(0.016, 0.0075), 0.003, front_frame(x + sgn * 0.04, 0.732, face_y(x + sgn * 0.04, 0.0) - 0.008), "sun", inset=0.2)
        b.plate(circle(0.005, 6), 0.002, front_frame(x - sgn * 0.012, 0.718, y - 0.0085), "white", inset=0.0)
        b.plate(circle(0.0055, 6), 0.002, front_frame(x - sgn * 0.034, 0.684, face_y(x - sgn * 0.034, 0.0) - 0.008), "sun", inset=0.0)


def face_heart_shades(b):
    """하트 안경(희귀) — 눈마다 흰 하트 테 + 붉은 분홍 하트 렌즈 + 하이라이트 + 코다리 + 흰 다리."""
    y = face_y(HX, 0.0) - 0.02
    for sgn in (-1, 1):
        cx = HX + sgn * (EX + 0.002)
        outline = heart_outline(0.12)
        pts = [Vector((cx + px, y, EZ + 0.004 + pz)) for px, pz in outline] + [Vector((cx + outline[0][0], y, EZ + 0.004 + outline[0][1]))]
        b.tube(pts, 0.0058, "white", sides=4)
        b.plate(heart_outline(0.108), 0.004, front_frame(cx, EZ + 0.004, y + 0.003), "lens", inset=0.15)
        b.plate(rounded_rect(0.024, 0.008, 0.003, n=2), 0.002,
                front_frame(cx - 0.02, EZ + 0.026, y - 0.0012) @ Matrix.Rotation(0.6, 4, "Z"), "shine")
    b.tube([Vector((HX - 0.04, y + 0.002, EZ + 0.026)), Vector((HX, y - 0.004, EZ + 0.034)), Vector((HX + 0.04, y + 0.002, EZ + 0.026))],
           0.005, "white", sides=4)
    temples(b, "white", y, rad=0.0055, z=EZ + 0.03)


def face_white_beard(b):
    """흰 수염(에픽) — 뺨에서 턱 아래로 풍성하게 내려와 뾰족하게 끝나는 수염 + 말린 콧수염 + 숱 많은 눈썹."""
    def put(dx, z, rx, ry, rz, key, gap=0.016):
        x = HX + dx
        b.blob(Matrix.Translation(Vector((x, face_y(x, 0.0) - gap, z))) @ Matrix.Diagonal((rx, ry, rz, 1)), key, us=8, vs=5)
    # 턱수염 — 양 뺨 → 턱 → 아래 뾰족 끝 (겹치는 큰 덩이)
    for sgn in (-1, 1):
        put(sgn * 0.145, 0.70, 0.05, 0.034, 0.06, "snow", gap=0.004)
        put(sgn * 0.11, 0.64, 0.055, 0.036, 0.055, "frost", gap=0.004)
        put(sgn * 0.06, 0.59, 0.055, 0.038, 0.05, "snow", gap=0.006)
    put(0.0, 0.61, 0.075, 0.04, 0.075, "frost", gap=0.01)
    put(0.0, 0.55, 0.05, 0.04, 0.075, "snow", gap=0.006)
    # 콧수염 — 가운데 덩이 + 양쪽 끝이 말려 올라감
    put(0.0, 0.722, 0.04, 0.018, 0.022, "snow", gap=-0.004)
    for sgn in (-1, 1):
        path = [(0.02, 0.716), (0.05, 0.707), (0.088, 0.709), (0.118, 0.72), (0.128, 0.736)]
        pts = [Vector((HX + sgn * dx, face_y(HX + sgn * dx, 0.0) - 0.008, zz)) for dx, zz in path]
        b.tube(pts, 0.015, "snow", sides=5)
        # 눈썹 — 눈 위에 흰 덩이 둘
        put(sgn * (EX + 0.004), EZ + 0.058, 0.044, 0.012, 0.014, "snow", gap=0.006)


FACES.update({
    "HarborKerchief": dict(build=face_kerchief,
                          pal={"navy": ("#26336E", 0.75, 0), "white": ("#F4F1E8", 0.75, 0)}),
    "MeadowCheekStars": dict(build=face_cheek_stars,
                             pal={"pink": ("#F4789F", 0.5, 0), "sun": ("#F7C61B", 0.45, 0.1), "white": ("#FFFFFF", 0.3, 0)}),
    "MeadowHeartShades": dict(build=face_heart_shades,
                              pal={"white": ("#F7F6F0", 0.45, 0), "lens": ("#E8365F", 0.08, 0.3), "shine": ("#FFFFFF", 0.1, 0)}),
    "HarborWhiteBeard": dict(build=face_white_beard,
                             pal={"snow": ("#F4F2EC", 0.9, 0), "frost": ("#E9ECF1", 0.9, 0)}),
})


def main_more2():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    os.makedirs(TMP, exist_ok=True)
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else MORE2_ORDER
    infos = []
    for name in only:
        infos.append(export_face(name, build_face(name)))
        print("FACEINFO", json.dumps(infos[-1], ensure_ascii=False))
    global BODY
    BODY = BOX_BODY
    cam = setup_body()
    for info in infos:
        render_face(info["name"], cam)


main_more2()
