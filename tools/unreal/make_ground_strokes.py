"""M15-04 바닥 획 텍스처 원본 만들기 (시스템 python3 + Pillow — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_ground_strokes.py   → Saved/Generated/T_Ground_Strokes.png

레퍼런스 바닥 = 단색 + 짧은 획을 성기게 뿌림(`docs/reference/world_environment.md` 「지형·바닥」). 흑백 한 장을 만들고
색은 머티리얼 인스턴스가 곱한다(칸마다 색만 다르다). 가장자리를 넘는 획은 반대쪽에 다시 그려 이음매 없이 반복된다.
값은 리니어(sRGB 끔)로 읽힌다: 바탕 0.80(± 알갱이) · 짙은 획 0.59 · 밝은 획 1.0.
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

# steam_22 풀밭: 고운 알갱이 바탕에, 같은 쪽으로 기운 짧은 빗금이 몇 m 넓이 무리로 모여 있고(밝은 무리·짙은 무리 따로)
# 무리 사이는 비어 있다. 고르게 뿌린 점(이전 값)은 화면에서 모래알처럼 보였다. 한 장 = 32m(`setup_ground_strokes.py` TILE_CM).
SIZE = 1024
PATCHES = 9            # 한 장(32m)에 무리 9개 — steam_22 무리 ≈ 가로 12m·세로 7m, 사이 빈 땅이 무리만큼
PATCH_STROKES = (35, 70)
PATCH_RADIUS = (110, 190)   # 텍셀 (1텍셀 ≈ 3.1cm)
LOOSE = 160            # 무리 밖에 드문드문
LENGTH = (9, 14)       # 획 ≈ 28~43cm — steam_22 획 ≈ 캐릭터 키의 1/4
WIDTH = 4
GRAIN = 14             # 알갱이 표준편차 — steam_22 풀 초록 표준편차 ≈ 9 (sRGB), 우리 3~5
BASE, DARK, LIGHT = 204, 150, 255
SEED = 1504

out = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_Ground_Strokes.png")
os.makedirs(os.path.dirname(out), exist_ok=True)
rng = random.Random(SEED)
# 알갱이: 반 크기 노이즈를 늘려 2텍셀 알갱이로(1텍셀은 화면에서 반짝인다). 반복되는 가장자리가 맞게 한 장 통째로 늘린다
half = Image.new("L", (SIZE // 2, SIZE // 2))
half.putdata([max(0, min(255, int(rng.gauss(BASE, GRAIN)))) for _ in range(half.width * half.height)])
image = half.resize((SIZE, SIZE), Image.NEAREST).filter(ImageFilter.BoxBlur(0.6))
draw = ImageDraw.Draw(image)


def _stroke(x, y, angle, value):
    length = rng.uniform(*LENGTH)
    # 화면 오른쪽 = 텍스처 +y, 화면 위 = 텍스처 +x(바닥 UV = 월드 XY) — 오른쪽 위로 angle 만큼 기운 빗금
    dx, dy = math.sin(angle) * length, math.cos(angle) * length
    for ox in (-SIZE, 0, SIZE):
        for oy in (-SIZE, 0, SIZE):
            draw.line((x + ox, y + oy, x + dx + ox, y + dy + oy), fill=value, width=WIDTH)


for n in range(PATCHES):
    cx, cy = rng.uniform(0, SIZE), rng.uniform(0, SIZE)
    radius = rng.uniform(*PATCH_RADIUS)
    angle = math.radians(rng.uniform(15, 35))
    value = LIGHT if n % 3 else DARK   # steam_22 는 밝은 무리가 짙은 무리의 두 배쯤
    for _ in range(rng.randint(*PATCH_STROKES)):
        # 가로(화면)로 길쭉한 무리
        _stroke(cx + rng.gauss(0, radius * 0.55), cy + rng.gauss(0, radius), angle + math.radians(rng.uniform(-5, 5)), value)
for _ in range(LOOSE):
    _stroke(rng.uniform(0, SIZE), rng.uniform(0, SIZE), math.radians(rng.uniform(10, 40)),
            DARK if rng.random() < 0.4 else LIGHT)
image.save(out)
print("저장:", os.path.abspath(out))
