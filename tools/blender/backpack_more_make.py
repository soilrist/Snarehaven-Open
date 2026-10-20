"""꾸미기 배낭 +3 (M83-01, 루프#855) — 버섯·선적 상자·꿀벌통 배낭, +3 (M84-01, 루프#864) — 수박·술통·고래 배낭, +3 (M88-01, 루프#889) — 딸기·고양이·잠수함 배낭을 절차적으로 만든다.

실행 (헤드리스만):
  HATS_TMP=<임시 폴더>/ ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      -P tools/blender/backpack_more_make.py [-- MeadowMushroomPack ...]

조립기·피벗·몸 쪽 면·내보내기는 `_Source/raw/Snare/Items/Backpacks/backpack_make.py`(스킨 조각 l)를 그대로 빌린다 —
그 파일의 `main_packs()` 만 빼고 불러와 `PACKS` 에 세 벌을 더한다. 그래서 `Socket_Back` 에 바로 붙고 몸 배율을 따른다.
이름·모양은 새것(레퍼런스 원본 이름·에셋을 쓰지 않는다). 앞 = −Y, 삼각형 한도 1,500.
"""
import math
import os

BACKPACK_MAKE = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Backpacks/backpack_make.py"
_here = globals().get("__file__")
globals()["__file__"] = BACKPACK_MAKE
_code = open(BACKPACK_MAKE, encoding="utf-8").read()
assert _code.rstrip().endswith("main_packs()"), "배낭 틀 끝이 main_packs() 이 아니다"
exec(compile(_code.rstrip()[:-len("main_packs()")], BACKPACK_MAKE, "exec"))
globals()["__file__"] = _here

MORE_ORDER = ["MeadowMushroomPack", "HarborCratePack", "MeadowBeehivePack", "MeadowWatermelonPack", "HarborBarrelPack", "HarborWhalePack",
              "MeadowStrawberryPack", "MeadowCatPack", "HarborSubmarinePack"]


def _on_ellipsoid(c, r, d, lift=0.97):
    d = Vector(d).normalized()
    return Vector((c[0] + r[0] * d.x * lift, c[1] + r[1] * d.y * lift, c[2] + r[2] * d.z * lift)), Vector((d.x / r[0], d.y / r[1], d.z / r[2])).normalized()


def pack_meadow_mushroom(b):
    """버섯 배낭(일반) — 크림 기둥 몸통 + 큰 빨간 갓(흰 점 여섯) + 기둥 앞 큰 눈 둘·분홍 볼."""
    ball(b, (0, -0.09, -0.04), (0.12, 0.085, 0.14), "stem", us=12, vs=8)
    cap_c, cap_r = (0, -0.09, 0.075), (0.175, 0.125, 0.095)
    ball(b, cap_c, cap_r, "cap", us=14, vs=8)
    for d in ((0, -0.45, 0.9), (-0.55, -0.3, 0.8), (0.55, -0.3, 0.8), (0.2, 0.25, 0.95), (-0.85, 0, 0.5), (0.85, -0.05, 0.5)):
        q, n = _on_ellipsoid(cap_c, cap_r, d)
        b.plate(circle(0.026, 8), 0.005, frame(q, n), "spot")
    for sgn in (-1, 1):
        eye(b, (sgn * 0.042, -0.165, -0.035), 0.026)
        ball(b, (sgn * 0.075, -0.158, -0.075), (0.017, 0.007, 0.012), "cheek", us=6, vs=3)
    b.tube([Vector((0.016 * math.cos(math.radians(a)), -0.172, -0.085 + 0.012 * math.sin(math.radians(a))))
            for a in (200, 240, 270, 300, 340)], 0.004, "mouth", sides=4)


def pack_harbor_crate(b):
    """선적 나무상자 배낭(일반) — 판자 결 상자 + 앞 가새(X) 두 판 + 위 밧줄 손잡이 + 가운데 닻 도장 + 모서리 금 못."""
    rbox(b, at(0, -0.09, 0), (0.14, 0.09, 0.15), "plank", e1=0.14, e2=0.14, nu=16, nv=8)
    for ang in (math.radians(40), math.radians(-40)):
        c, s = math.cos(ang), math.sin(ang)
        pts = [(-0.11, -0.014), (0.11, -0.014), (0.11, 0.014), (-0.11, 0.014)]
        rot = [(x * c - y * s, x * s + y * c) for x, y in pts]
        b.plate(rot, 0.005, frame(Vector((0, -0.19, 0)), FWD), "brace", inset=0.1)
    b.plate(circle(0.042, 12), 0.006, frame(Vector((0, -0.195, 0)), FWD), "white")
    b.plate(anchor_outline(0.034), 0.004, frame(Vector((0, -0.2, 0)), FWD), "gold", inset=0.1)
    b.tube([Vector((-0.05, -0.09, 0.145)), Vector((-0.045, -0.09, 0.2)), Vector((0, -0.09, 0.22)),
            Vector((0.045, -0.09, 0.2)), Vector((0.05, -0.09, 0.145))], 0.01, "rope", sides=5)
    for sx in (-0.115, 0.115):
        for sz in (-0.12, 0.12):
            ball(b, (sx, -0.19, sz), 0.011, "gold", us=5, vs=3)


def pack_meadow_beehive(b):
    """꿀벌통 배낭(희귀) — 호박색 층층 꿀벌통(층마다 두 가지 노랑) + 앞 입구 구멍 + 위 꼭지 + 앞에 앉은 꿀벌."""
    layers = ((-0.13, 0.115, "honey"), (-0.06, 0.15, "honey2"), (0.02, 0.152, "honey"), (0.09, 0.115, "honey2"), (0.15, 0.07, "honey"))
    for z, rx, key in layers:
        ball(b, (0, -0.09, z), (rx, rx * 0.66, 0.062), key, us=12, vs=6)
    ball(b, (0, -0.09, 0.205), (0.022, 0.022, 0.03), "wax", us=6, vs=4)
    b.plate(circle(0.045, 10), 0.006, frame(Vector((0, -0.1855, -0.02)), FWD), "hole")
    bee = Vector((0.085, -0.19, 0.075))
    ball(b, tuple(bee), (0.03, 0.022, 0.022), "yellow", us=8, vs=5)
    for dx in (-0.008, 0.012):
        ball(b, (bee.x + dx, bee.y - 0.004, bee.z), (0.006, 0.022, 0.0225), "black", us=5, vs=4)
    ball(b, (bee.x - 0.036, bee.y, bee.z), (0.012, 0.014, 0.014), "black", us=6, vs=4)
    for sgn in (-1, 1):
        ball(b, (bee.x + sgn * 0.012, bee.y + 0.004, bee.z + 0.026), (0.016, 0.004, 0.012), "wing", us=6, vs=3)


def pack_meadow_watermelon(b):
    """수박 배낭(일반) — 줄무늬 초록 수박 한 덩이 + 위 꼭지 + 앞 한 조각 잘라낸 빨간 속(검은 씨) + 겉껍질 띠."""
    swap = Matrix(((0, 0, 1, 0), (0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 0, 1)))
    rbox(b, at(0, -0.1, 0) @ swap, (0.14, 0.1, 0.17), "rind", e1=0.55, e2=0.9, nu=16, nv=10)
    b.plate(circle(0.085, 14), 0.006, frame(Vector((0, -0.2, 0.0)), FWD), "white")
    b.plate(circle(0.072, 14), 0.006, frame(Vector((0, -0.2055, 0.0)), FWD), "flesh")
    for x, z in ((-0.03, 0.02), (0.03, 0.02), (0.0, -0.025), (-0.035, -0.03), (0.035, -0.03)):
        ball(b, (x, -0.2125, z), (0.007, 0.004, 0.011), "seed", us=5, vs=3)
    b.tube([Vector((0, -0.1, 0.165)), Vector((0.012, -0.1, 0.2)), Vector((0.035, -0.1, 0.215))], 0.011, "stem", sides=5)
    ball(b, (-0.022, -0.1, 0.19), (0.04, 0.012, 0.016), "leaf", us=6, vs=3)


def pack_harbor_barrel(b):
    """술통 배낭(일반) — 통통한 나무통(세로 판자 결) + 위아래 쇠 테 둘씩 + 앞 마개 + 어깨 밧줄 고리."""
    rbox(b, at(0, -0.1, 0), (0.14, 0.1, 0.17), "wood", e1=0.55, e2=0.8, nu=16, nv=10)
    for z, k in ((-0.11, 0.9), (-0.045, 0.99), (0.045, 0.99), (0.11, 0.9)):
        b.tube([Vector((0.145 * k * math.cos(a), -0.1 + 0.103 * k * math.sin(a), z)) for a in [2 * math.pi * i / 14 for i in range(15)]],
               0.008, "iron", sides=4)
    b.plate(circle(0.026, 10), 0.006, frame(Vector((0, -0.2, 0.0)), FWD), "iron")
    ball(b, (0, -0.208, 0.0), (0.014, 0.01, 0.014), "cork", us=6, vs=4)
    b.tube([Vector((-0.06, -0.1, 0.165)), Vector((-0.05, -0.1, 0.215)), Vector((0, -0.1, 0.235)),
            Vector((0.05, -0.1, 0.215)), Vector((0.06, -0.1, 0.165))], 0.01, "rope", sides=5)


def pack_harbor_whale(b):
    """고래 배낭(희귀) — 파란 통통 고래(흰 배·위 물줄기) + 큰 눈·볼 + 등 위로 솟은 꼬리 지느러미 + 옆 작은 지느러미."""
    ball(b, (0, -0.1, 0.0), (0.15, 0.1, 0.17), "skin", us=14, vs=9)
    ball(b, (0, -0.145, -0.05), (0.115, 0.065, 0.11), "belly", us=12, vs=7)
    for sgn in (-1, 1):
        eye(b, (sgn * 0.065, -0.19, 0.045), 0.036)
        ball(b, (sgn * 0.105, -0.172, 0.0), (0.02, 0.008, 0.014), "cheek", us=6, vs=3)
        ball(b, (sgn * 0.155, -0.1, -0.03), (0.03, 0.05, 0.016), "fin", us=6, vs=4)
    b.tube([Vector((-0.022, -0.2, -0.01)), Vector((0, -0.204, -0.02)), Vector((0.022, -0.2, -0.01))], 0.005, "mouth", sides=4)
    b.tube([Vector((0, -0.1, 0.15)), Vector((0, -0.1, 0.2)), Vector((0, -0.1, 0.235))], 0.014, "skin", sides=5)
    for sgn in (-1, 1):
        ball(b, (sgn * 0.04, -0.1, 0.255), (0.04, 0.012, 0.024), "fin", us=6, vs=4)
    for dx in (-0.02, 0.02):
        ball(b, (dx, -0.1, 0.285), (0.008, 0.008, 0.016), "spout", us=5, vs=3)
    ball(b, (0, -0.1, 0.31), (0.012, 0.012, 0.012), "spout", us=5, vs=3)


def pack_meadow_strawberry(b):
    """딸기 배낭(일반) — 빨간 끝뾰족 딸기 한 알 + 노란 씨 여럿 + 위 초록 꽃받침(잎 넷) + 꼭지."""
    ball(b, (0, -0.1, -0.01), (0.14, 0.1, 0.16), "berry", us=14, vs=9)
    ball(b, (0, -0.1, -0.15), (0.07, 0.06, 0.05), "berry", us=10, vs=5)
    for x, z in ((-0.06, 0.07), (0.06, 0.07), (0.0, 0.1), (-0.085, -0.01), (0.085, -0.01), (-0.035, -0.015), (0.035, -0.015),
                 (0.0, -0.06), (-0.06, -0.08), (0.06, -0.08), (0.0, 0.03)):
        ball(b, (x, -0.19 + 0.012 * (abs(x) / 0.085) ** 2, z), (0.007, 0.004, 0.011), "seed", us=5, vs=3)
    for ang in (0, 72, 144, 216, 288):
        a = math.radians(ang)
        ball(b, (0.055 * math.cos(a), -0.1 + 0.04 * math.sin(a), 0.155), (0.04, 0.024, 0.01), "leaf", us=6, vs=3)
    b.tube([Vector((0, -0.1, 0.15)), Vector((0.005, -0.1, 0.19)), Vector((0.025, -0.1, 0.21))], 0.01, "stem", sides=5)


def pack_meadow_cat(b):
    """고양이 배낭(일반) — 주황 통통 고양이 몸 + 큰 눈·분홍 코·수염 + 위 뾰족 귀 둘(안쪽 분홍) + 옆 줄무늬 꼬리 + 흰 배."""
    ball(b, (0, -0.1, 0.0), (0.15, 0.1, 0.16), "fur", us=14, vs=9)
    ball(b, (0, -0.15, -0.06), (0.1, 0.055, 0.08), "belly", us=10, vs=6)
    for sgn in (-1, 1):
        ball(b, (sgn * 0.09, -0.1, 0.16), (0.045, 0.03, 0.06), "fur", us=6, vs=4)
        ball(b, (sgn * 0.09, -0.125, 0.16), (0.026, 0.01, 0.04), "ear", us=6, vs=3)
        eye(b, (sgn * 0.06, -0.18, 0.05), 0.034)
        ball(b, (sgn * 0.1, -0.17, -0.0), (0.02, 0.008, 0.014), "cheek", us=6, vs=3)
        for dz in (-0.012, 0.012):
            b.tube([Vector((sgn * 0.06, -0.195, -0.015 + dz)), Vector((sgn * 0.12, -0.19, -0.015 + dz * 3))], 0.002, "whisker", sides=3)
    ball(b, (0, -0.2, 0.0), (0.018, 0.01, 0.012), "nose", us=6, vs=4)
    b.tube([Vector((0.15, -0.05, -0.1)), Vector((0.22, -0.05, -0.05)), Vector((0.24, -0.05, 0.05)), Vector((0.2, -0.05, 0.12))], 0.026, "fur", sides=6)
    for z in (-0.05, 0.03):
        ball(b, (0.205 + (0.015 if z > 0 else 0), -0.05, z), (0.03, 0.03, 0.01), "stripe", us=6, vs=3)


def pack_harbor_submarine(b):
    """잠수함 배낭(희귀) — 노란 알약 선체 + 앞 둥근 창 둘(테두리) + 위 잠망경(L자) + 해치 + 옆 지느러미 + 빨간 띠."""
    ball(b, (0, -0.1, 0.0), (0.15, 0.1, 0.19), "hull", us=14, vs=9)
    b.tube([Vector((0.152 * math.cos(a), -0.1 + 0.102 * math.sin(a), -0.07)) for a in [2 * math.pi * i / 14 for i in range(15)]], 0.012, "red", sides=4)
    for z in (0.07, -0.02):
        b.plate(circle(0.05, 12), 0.006, frame(Vector((0, -0.2, z)), FWD), "rim")
        b.plate(circle(0.037, 12), 0.006, frame(Vector((0, -0.2055, z)), FWD), "glass")
    b.tube([Vector((0.04, -0.1, 0.17)), Vector((0.04, -0.1, 0.27)), Vector((0.04, -0.14, 0.285))], 0.012, "rim", sides=5)
    b.plate(circle(0.03, 10), 0.01, frame(Vector((0, -0.1, 0.19)), Vector((0, 0, 1))), "rim")
    for sgn in (-1, 1):
        ball(b, (sgn * 0.16, -0.1, -0.1), (0.04, 0.05, 0.016), "red", us=6, vs=4)


PACKS.update({
    "MeadowMushroomPack": dict(build=pack_meadow_mushroom,
                               pal={"stem": ("#F3E8CC", 0.85, 0), "cap": ("#E2423A", 0.6, 0), "spot": ("#FBF8EE", 0.7, 0),
                                    "eye": ("#FFFFFF", 0.4, 0), "black": ("#1E1C1C", 0.3, 0), "shine": ("#FFFFFF", 0.1, 0),
                                    "cheek": ("#F49AB0", 0.7, 0), "mouth": ("#7A3A2A", 0.6, 0)}),
    "HarborCratePack": dict(build=pack_harbor_crate,
                            pal={"brace": ("#7A4A2A", 0.85, 0), "white": ("#F7F6F0", 0.6, 0), "gold": ("#E8BE50", 0.35, 0.6),
                                 "rope": ("#D8C08E", 0.9, 0)},
                            pkeys={"plank": ("A", "z", -0.15, 0.15)},
                            patterns={"A": (pattern_stripes, ("#C9985E", "#A87A44"), 0.95)}),
    "MeadowBeehivePack": dict(build=pack_meadow_beehive,
                              pal={"honey": ("#F2B53A", 0.7, 0), "honey2": ("#D99422", 0.7, 0), "wax": ("#8A5A32", 0.85, 0),
                                   "hole": ("#3A2412", 0.9, 0), "yellow": ("#FFD54A", 0.6, 0), "black": ("#1E1C1C", 0.5, 0),
                                   "wing": ("#EAF4FA", 0.3, 0)}),
    "MeadowWatermelonPack": dict(build=pack_meadow_watermelon,
                                 pal={"white": ("#F2F4D6", 0.8, 0), "flesh": ("#E8444E", 0.5, 0), "seed": ("#1E1C1C", 0.4, 0),
                                      "stem": ("#5E8A3A", 0.8, 0), "leaf": ("#7CC54E", 0.7, 0)},
                                 pkeys={"rind": ("A", "z", -0.14, 0.14)},
                                 patterns={"A": (pattern_stripes, ("#4FA03C", "#2E6E2A"), 0.9)}),
    "HarborBarrelPack": dict(build=pack_harbor_barrel,
                             pal={"iron": ("#4A4F5A", 0.4, 0.6), "cork": ("#C9985E", 0.85, 0), "rope": ("#D8C08E", 0.9, 0)},
                             pkeys={"wood": ("A", "z", -0.17, 0.17)},
                             patterns={"A": (pattern_stripes, ("#A0683A", "#865230"), 0.95)}),
    "HarborWhalePack": dict(build=pack_harbor_whale,
                            pal={"skin": ("#4F86C6", 0.6, 0), "belly": ("#EAF1F8", 0.7, 0), "fin": ("#3A68A8", 0.6, 0),
                                 "eye": ("#FFFFFF", 0.4, 0), "black": ("#1E1C1C", 0.3, 0), "shine": ("#FFFFFF", 0.1, 0),
                                 "cheek": ("#F49AB0", 0.7, 0), "mouth": ("#26336E", 0.6, 0), "spout": ("#BFE3F5", 0.3, 0)}),
    "MeadowStrawberryPack": dict(build=pack_meadow_strawberry,
                                 pal={"berry": ("#E03A48", 0.55, 0), "seed": ("#FFE08A", 0.6, 0), "leaf": ("#4F9A3A", 0.75, 0),
                                      "stem": ("#5E8A3A", 0.8, 0)}),
    "MeadowCatPack": dict(build=pack_meadow_cat,
                          pal={"fur": ("#F09A4A", 0.85, 0), "belly": ("#FFF1DC", 0.85, 0), "ear": ("#F5A9B8", 0.7, 0),
                               "eye": ("#FFFFFF", 0.4, 0), "black": ("#1E1C1C", 0.3, 0), "shine": ("#FFFFFF", 0.1, 0),
                               "cheek": ("#F49AB0", 0.7, 0), "whisker": ("#FFF8EE", 0.6, 0), "nose": ("#E8708A", 0.6, 0),
                               "stripe": ("#C9742A", 0.85, 0)}),
    "HarborSubmarinePack": dict(build=pack_harbor_submarine,
                                pal={"hull": ("#F2C230", 0.5, 0), "red": ("#D8453A", 0.55, 0), "rim": ("#5A606C", 0.4, 0.5),
                                     "glass": ("#8FD3F0", 0.15, 0)}),
})


def main_more():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    os.makedirs(TMP, exist_ok=True)
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else MORE_ORDER
    for name in only:
        print("PACKINFO", json.dumps(export_pack(name, build_pack(name)), ensure_ascii=False))


main_more()
