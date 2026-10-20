"""꾸미기 얼굴 장식 +4 (M87-01, 루프#883) — 콧수염·코 반창고·금테 외눈 안경·별빛 안경을 절차적으로 만든다.

실행 (헤드리스만):
  HATS_TMP=<임시 폴더>/ ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      -P tools/blender/faces_more_make.py [-- MeadowMustache ...]

얼굴 둘레·받침점·텍스처·내보내기는 `_Source/raw/Snare/Items/FaceAcc/face_make.py`(스킨 조각 g)를 그대로 빌린다 —
그 파일의 `main_faces()` 만 빼고 불러와 `FACES` 에 네 벌을 더한다. 그래서 `Socket_Face` 에 바로 앉고 치수 규칙이 같다.
이름·모양은 새것(레퍼런스 원본 이름·에셋을 쓰지 않는다).
"""
import math

FACE_MAKE = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/FaceAcc/face_make.py"
_here = globals().get("__file__")
globals()["__file__"] = FACE_MAKE
_code = open(FACE_MAKE, encoding="utf-8").read()
assert _code.rstrip().endswith("main_faces()"), "얼굴 장식 틀 끝이 main_faces() 가 아니다"
exec(compile(_code.rstrip()[:-len("main_faces()")], FACE_MAKE, "exec"))
globals()["__file__"] = _here

MORE_ORDER = ["MeadowMustache", "HarborBandage", "HarborMonocle", "MeadowStarShades"]


def face_y(x, gap):
    """얼굴 앞면 y — 머리 둘레 초타원의 앞쪽(−Y)을 x 에서 읽고 gap 만큼 앞으로 부풀린다."""
    a, b = BAND_A + gap, BAND_B + gap
    r = min(abs(x - BAND_C[0]) / a, 0.999)
    return BAND_C[1] - b * (1 - r ** BAND_N) ** (1.0 / BAND_N)


def star_outline(r_out, r_in, n=5):
    pts = []
    for k in range(2 * n):
        a = math.pi / 2 + math.pi * k / n
        r = r_out if k % 2 == 0 else r_in
        pts.append((r * math.cos(a), r * math.sin(a)))
    return pts


# ---------------------------------------------------------------- 네 벌
def face_mustache(b):
    """콧수염(일반) — 코 밑 가운데 굵은 덩이 + 양쪽으로 처졌다 끝이 말려 올라가는 갈색 팔자 수염."""
    z0 = 0.722
    b.blob(Matrix.Translation(Vector((HX, face_y(HX, 0.0) - 0.008, z0 + 0.002))) @ Matrix.Diagonal((0.03, 0.012, 0.016, 1)), "brown", us=10, vs=6)
    for sgn in (-1, 1):
        path = [(0.02, 0.718), (0.05, 0.708), (0.085, 0.708), (0.115, 0.714), (0.13, 0.728), (0.122, 0.741)]
        pts = [Vector((HX + sgn * dx, face_y(HX + sgn * dx, 0.0) - 0.007, zz)) for dx, zz in path]
        b.tube(pts, 0.0105, "brown", sides=6)
        b.blob(Matrix.Translation(pts[-1] + Vector((0, -0.002, 0.004))) @ Matrix.Diagonal((0.011, 0.01, 0.011, 1)), "dark", us=6, vs=4)
    # 윗입술 쪽 밝은 결 두 줄
    for sgn in (-1, 1):
        a = Vector((HX + sgn * 0.03, face_y(HX + sgn * 0.03, 0.0) - 0.0145, 0.724))
        c = Vector((HX + sgn * 0.085, face_y(HX + sgn * 0.085, 0.0) - 0.0145, 0.714))
        b.tube([a, c], 0.0032, "light", sides=4)


def face_bandage(b):
    """코 반창고(일반) — 코 위로 엇갈려 붙은 살구색 반창고 둘 + 가운데 거즈 + 작은 점."""
    cx, cz = HX, 0.758
    y = face_y(cx, 0.0)
    for k, ang in enumerate((0.5, -0.5)):
        m = front_frame(cx, cz, y - 0.002 - 0.0065 * k) @ Matrix.Rotation(ang, 4, "Z")
        b.plate(rounded_rect(0.125, 0.036, 0.012, n=3), 0.0065, m, "tape", inset=0.2)
        for dx in (-0.045, -0.032, 0.032, 0.045):
            b.plate(circle(0.0028, 6), 0.0012, m @ Matrix.Translation((dx, 0.0, 0.0065)), "dot", inset=0.0)
    gauze = front_frame(cx, cz, y - 0.0145)
    b.plate(rounded_rect(0.04, 0.03, 0.007, n=2), 0.004, gauze, "gauze", inset=0.2)


def face_monocle(b):
    """금테 외눈 안경(희귀) — 오른쪽 눈 금 테 + 하늘빛 유리 + 테 바깥에서 귀 쪽 볼을 따라 내려가는 금 사슬."""
    cx = HX + EX
    y = face_y(cx, 0.0) - 0.014
    ring(b, cx, EZ, 0.057, 0.055, y, 0.0065, "gold", n=20)
    ring(b, cx, EZ, 0.062, 0.06, y + 0.002, 0.0028, "dark", n=20, sides=4)
    b.plate(circle(0.054, 20), 0.004, front_frame(cx, EZ, y + 0.003), "glass")
    b.plate(rounded_rect(0.026, 0.008, 0.003, n=2), 0.002, front_frame(cx - 0.02, EZ + 0.024, y - 0.0012) @ Matrix.Rotation(0.6, 4, "Z"), "shine")
    # 사슬 — 테 오른쪽에서 시작해 머리 옆·뒤를 한 바퀴 돌아 왼쪽 앞 관자놀이에 걸린다(한쪽만 길면 좌우가 기운다), 토막마다 금·짙은 금
    n = 22
    t0, t1 = -math.pi / 4, 5 * math.pi / 4
    pts = [Vector((cx + 0.057, y + 0.004, EZ - 0.006))]
    pts += [band_point(t0 + (t1 - t0) * k / n, 0.01, EZ - 0.006 + 0.02 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    for k in range(len(pts) - 1):
        b.tube([pts[k], pts[k + 1]], 0.0038, ("gold", "dark")[k % 2], sides=4)
    b.blob(Matrix.Translation(pts[-1]) @ Matrix.Diagonal((0.011, 0.011, 0.011, 1)), "gold", us=6, vs=4)


def face_star_shades(b):
    """별빛 안경(에픽) — 눈마다 노란 별 테 + 분홍 별 렌즈 + 금빛 다리 + 코다리 + 테 바깥 작은 반짝이 별."""
    y = face_y(HX, 0.0) - 0.02
    for sgn in (-1, 1):
        cx = HX + sgn * (EX + 0.004)
        b.plate(star_outline(0.075, 0.037), 0.008, front_frame(cx, EZ + 0.004, y), "sun", inset=0.15)
        b.plate(star_outline(0.057, 0.028), 0.006, front_frame(cx, EZ + 0.004, y - 0.0065), "lens", inset=0.15)
        b.plate(rounded_rect(0.022, 0.007, 0.0025, n=2), 0.0015,
                front_frame(cx - 0.014, EZ + 0.02, y - 0.0128) @ Matrix.Rotation(0.6, 4, "Z"), "shine")
        b.plate(star_outline(0.018, 0.008), 0.003, front_frame(cx + sgn * 0.066, EZ + 0.058, y - 0.004), "sun", inset=0.1)
    b.tube([Vector((HX - 0.04, y + 0.002, EZ + 0.016)), Vector((HX, y - 0.004, EZ + 0.024)), Vector((HX + 0.04, y + 0.002, EZ + 0.016))],
           0.0052, "sun", sides=5)
    temples(b, "gold", y, rad=0.0055, z=EZ + 0.02)


FACES.update({
    "MeadowMustache": dict(build=face_mustache,
                           pal={"brown": ("#6B3F1F", 0.85, 0), "dark": ("#3C2210", 0.85, 0), "light": ("#9A6A3C", 0.9, 0)}),
    "HarborBandage": dict(build=face_bandage,
                          pal={"tape": ("#F1C79A", 0.8, 0), "dot": ("#D49A66", 0.85, 0), "gauze": ("#F9F6EC", 0.95, 0)}),
    "HarborMonocle": dict(build=face_monocle,
                          pal={"gold": ("#E8BE50", 0.35, 0.6), "dark": ("#B58A22", 0.4, 0.6), "glass": ("#BDEBF2", 0.08, 0.1),
                               "shine": ("#FFFFFF", 0.1, 0)}),
    "MeadowStarShades": dict(build=face_star_shades,
                             pal={"sun": ("#F7C61B", 0.45, 0.1), "lens": ("#EE5FA7", 0.1, 0.2), "shine": ("#FFFFFF", 0.1, 0),
                                  "gold": ("#E8BE50", 0.35, 0.6)}),
})


def main_more():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    os.makedirs(TMP, exist_ok=True)
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else MORE_ORDER
    infos = []
    for name in only:
        infos.append(export_face(name, build_face(name)))
        print("FACEINFO", json.dumps(infos[-1], ensure_ascii=False))
    global BODY
    BODY = BOX_BODY
    cam = setup_body()
    for info in infos:
        render_face(info["name"], cam)


main_more()
