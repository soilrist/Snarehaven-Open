"""바다 물결 텍스처 원본 만들기 (시스템 python3 + Pillow — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_sea_waves.py   → Saved/Generated/T_Sea_Waves.png

레퍼런스 바다(`steam_05`·`steam_10`) = 짙은 파랑 바탕 + 흰 짧은 물결 획 + 반짝이는 점 + 밝고 어두운 얼룩.
채널 셋에 나눠 담고 머티리얼(`setup_sea.py` 의 `M_Sea_Water`)이 두 방향으로 흘려 섞는다:
  R = 부드러운 얼룩(0~1) — 색 흔들림과 획이 켜지고 꺼지는 박자
  G = 흰 물결 획(가로로 긴 짧은 선, 살짝 휨) — steam_10 획 30~60px · 굵기 2~3px (텍스처 18~44px ≈ 화면 25~60px)
  B = 반짝이 점(1~2px)
한 장이 바다 `TileCm`(2000cm) 를 덮는다 — 기본 카메라(6000cm·30°)에서 1px ≈ 4cm 라 텍스처 1px ≈ 화면 1px.
가장자리를 넘는 획·얼룩은 반대쪽에 다시 그려 이음매 없이 반복된다.
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

SIZE = 512
SEED = 2509
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_Sea_Waves.png")


def _wrapped(draw_fn):
    """(dx, dy) 9칸 복사로 그려 가장자리 넘는 것이 반대쪽에 이어지게."""
    for dx in (-SIZE, 0, SIZE):
        for dy in (-SIZE, 0, SIZE):
            draw_fn(dx, dy)


def blotches(rng):
    img = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(img)
    for _ in range(26):
        x, y, r = rng.uniform(0, SIZE), rng.uniform(0, SIZE), rng.uniform(40, 110)
        v = rng.randint(90, 255)
        _wrapped(lambda dx, dy: draw.ellipse((x + dx - r * 1.6, y + dy - r, x + dx + r * 1.6, y + dy + r), fill=v))
    # 흐림도 이음매 없이 — 세 장 이어 붙여 흐린 뒤 가운데를 자른다
    big = Image.new("L", (SIZE * 3, SIZE * 3))
    for i in range(3):
        for j in range(3):
            big.paste(img, (i * SIZE, j * SIZE))
    big = big.filter(ImageFilter.GaussianBlur(38))
    return big.crop((SIZE, SIZE, SIZE * 2, SIZE * 2))


def strokes(rng):
    img = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(img)
    for _ in range(34):
        x, y = rng.uniform(0, SIZE), rng.uniform(0, SIZE)
        length = rng.uniform(18, 44)
        bend = rng.uniform(-4, 4)
        tilt = rng.uniform(-0.12, 0.12)
        pts = []
        for k in range(9):
            t = k / 8.0
            pts.append((x + (t - 0.5) * length, y + (t - 0.5) * length * tilt + bend * math.sin(t * math.pi)))
        width = rng.choice((2, 2, 3))
        _wrapped(lambda dx, dy: draw.line([(px + dx, py + dy) for px, py in pts], fill=255, width=width, joint="curve"))
    return img.filter(ImageFilter.GaussianBlur(0.6))


def sparkles(rng):
    img = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(img)
    for _ in range(60):
        x, y = rng.uniform(0, SIZE), rng.uniform(0, SIZE)
        r = rng.choice((0.8, 1.0, 1.4))
        _wrapped(lambda dx, dy: draw.ellipse((x + dx - r, y + dy - r, x + dx + r, y + dy + r), fill=255))
    return img


def main():
    rng = random.Random(SEED)
    image = Image.merge("RGB", (blotches(rng), strokes(rng), sparkles(rng)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    image.save(OUT)
    def share(channel):
        return sum(image.getchannel(channel).histogram()[129:]) / (SIZE * SIZE)
    coverage = share("G")
    print("저장 {} — 획 덮음 {:.1%}, 반짝이 {:.2%}".format(os.path.abspath(OUT), coverage, share("B")))
    assert 0.003 < coverage < 0.05, "획이 너무 많거나 적다"


if __name__ == "__main__":
    main()
