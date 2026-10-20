"""여행자 모자를 둥근 캡으로 다시 만든다 (루프#1333, INBOX 보정 #1330 「네모난 파란 상자」).

실행 (헤드리스만):
  HATS_TMP=<임시 폴더>/ ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      -P tools/blender/traveler_cap_make.py

머리 틀·내보내기는 `hats_more_make.py`(→ `hats_v2_make.py`)를 그대로 빌린다(끝의 `main_more()` 만 뺀다).
옛 모양은 정수리가 평평한 원판이라 위에서 네모 상자로 읽혔다 — 크라운을 둥근 돔으로, 평면은 덜 각진 곡선(지수 3)으로 바꾼다.
"""
import math
import os

MORE = "~/UNREAL/Snarehaven/tools/blender/hats_more_make.py"
_here = globals().get("__file__")
globals()["__file__"] = MORE
_code = open(MORE, encoding="utf-8").read()
assert _code.rstrip().endswith("main_more()"), "hats_more_make.py 끝이 main_more() 이 아니다"
exec(compile(_code.rstrip()[:-len("main_more()")], MORE, "exec"))
globals()["__file__"] = _here

# 반입 도구가 읽는 폴더(`import_equipment_weapons.py` RAW)로 쓴다 — 틀의 RAW 는 옛 Varco 경로다.
HATS_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Hats/"
BODY = "~/UNREAL/Snarehaven/_Source/raw/Snare/Characters/Player/standing_idle_1.glb"
CROWN_RISE = 0.06   # 머리 윗면 위로 솟는 돔 높이(m) — 머리 높이의 1/3 이하


def hat_traveler_round(b):
    """여행자 캡 — 남색 둥근 돔 + 금 테 + 앞 챙 + 윗 단추."""
    global SUPER_N
    old_n = SUPER_N
    SUPER_N = 2.6   # 평면을 덜 각지게(머리 모서리와는 틈 3cm 안쪽)
    try:
        def prof(th):
            z = zr(th)
            ri = head_r(z, G)
            rbd = ri + 0.022
            return inner(z, "inside") + [
                (ri + 0.006, z - 0.01, "band"), (rbd + 0.014, z - 0.004, "band"), (rbd + 0.017, z + 0.05, "navy")] \
                + [(r, zz, "navy") for r, zz in dome(G + 0.036, z + 0.06, 11, stretch=CROWN_RISE)]
        b.revolve(prof)
        bill(b, math.radians(55), 0.09, 0.012, "visor", g=G + 0.012, zoff=0.014, thick=0.013)   # 챙을 거의 수평·위로 — 눈썹 선 위(INBOX #1334)
        pts = [surf(th, head_r(zr(th), G) + 0.03, zr(th) + 0.034)
               for th in (TF + math.radians(-62 + 124 * k / 8) for k in range(9))]
        b.tube(pts, 0.0065, "gold")
        for p in (pts[0], pts[-1]):
            b.blob(Matrix.Translation(p) @ Matrix.Diagonal((0.011, 0.011, 0.011, 1)), "gold", us=6, vs=4)
        top = CZ + HC + G + 0.036 + CROWN_RISE
        b.blob(Matrix.Translation((CX, CY, top + 0.004)) @ Matrix.Diagonal((0.016, 0.016, 0.012, 1)), "gold", us=6, vs=4)
    finally:
        SUPER_N = old_n


HATS["Traveler"]["build"] = hat_traveler_round


def main_cap():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    os.makedirs(TMP, exist_ok=True)
    info = export("Traveler", build_hat("Traveler"))
    print("HATINFO", json.dumps(info, ensure_ascii=False))
    render_check("Traveler", info, setup_body())


main_cap()
