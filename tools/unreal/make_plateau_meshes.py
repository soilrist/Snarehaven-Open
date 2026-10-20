"""M15-04 고원(바위 절벽 둔덕) 메시 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_plateau_meshes.py   → Saved/Generated/Plateau_01~03.obj (+ .mtl)

레퍼런스 섬은 평지가 아니라 풀 덮인 고원이 회색 바위 절벽으로 끊긴다(steam_17·steam_22 — 절벽 높이 ≈ 캐릭터 2배).
둥근 덩어리 윤곽을 사인파 셋으로 만들고, 옆면은 판마다 정점을 따로 둔 각진 바위 판, 맨 위는 풀이 둥근 턱으로 덮는다.
윗면은 `Grass` 슬롯(바닥과 같은 월드 좌표 획 머티리얼을 끼워 이음매가 없다), 옆면은 `Cliff` 슬롯. 단위 cm, Z 위.
"""
import math
import os
import random

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
SEED = 1504
SIDES = 18         # 판 수 — steam_17 절벽은 넓은 판 몇 장이 꺾인 모양(24 는 둥근 원통)
HEIGHT_CM = 300.0      # 캐릭터 키(≈170cm)의 약 2배 — steam_17 절벽
SINK_CM = 50.0         # 바닥 밑으로 묻어 틈이 안 보이게
# (이름, 반지름 cm) — 배치 스크립트가 배율로 크기를 다시 맞추므로 모양 차이만 중요하다
SHAPES = [("Plateau_01", 500.0), ("Plateau_02", 500.0), ("Plateau_03", 500.0)]
# 옆면 고리 (높이 비율, 반지름 비율, 흔들림, 슬롯) — steam_05·steam_17: 풀이 둥근 턱으로 절벽 위를 덮고(위 두 칸),
# 그 밑은 각진 세로 판 바위. 아래가 조금 넓다.
RINGS = [(-SINK_CM / HEIGHT_CM, 1.04, 0.03, "Cliff"), (0.40, 1.00, 0.06, "Cliff"), (0.86, 0.99, 0.04, "Grass"),
         (0.95, 1.01, 0.0, "Grass"), (1.0, 0.97, 0.0, None)]


def _outline(rng):
    waves = [(k, rng.uniform(0.05, 0.16) / k ** 0.3, rng.uniform(0, math.tau)) for k in (2, 3, 5)]
    return [1.0 + sum(a * math.sin(k * t + p) for k, a, p in waves)
            for t in (math.tau * i / SIDES for i in range(SIDES))]


def _face(*corners):
    return "f " + " ".join("{0}/{0}".format(c) for c in corners)   # 정점 번호 = UV 번호


def _write(name, radius, rng):
    base = _outline(rng)
    rings = []
    for height, scale, wobble, _ in RINGS:
        ring = []
        for i, r in enumerate(base):
            t = math.tau * i / SIDES
            rr = radius * r * scale * (1.0 + rng.uniform(-wobble, wobble))
            ring.append((rr * math.cos(t), rr * math.sin(t), height * HEIGHT_CM))
        rings.append(ring)
    # 옆면 판마다 정점을 따로 둔다 — 반입기가 법선을 다시 계산해도 이웃 판과 섞이지 않아 각진 바위 판으로 보인다
    # (정점을 나눠 쓰면 한 덩어리 매끈한 벽 — w37 은 레퍼런스 절벽보다 시멘트 벽처럼 보였다)
    verts, faces = [], {"Cliff": [], "Grass": []}
    for k, (_, _, _, slot) in enumerate(RINGS[:-1]):
        for i in range(SIDES):
            j = (i + 1) % SIDES
            first = len(verts) + 1   # OBJ 는 1부터
            verts += [rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]]
            faces[slot] += [_face(first, first + 1, first + 2), _face(first, first + 2, first + 3)]
    top = len(verts) + 1
    verts += rings[-1] + [(0.0, 0.0, HEIGHT_CM)]
    center = len(verts)
    faces["Grass"] += [_face(top + i, top + (i + 1) % SIDES, center) for i in range(SIDES)]
    lines = ["mtllib {}.mtl".format(name), "o {}".format(name), "s off"]
    lines += ["v {:.2f} {:.2f} {:.2f}".format(x, y, z) for x, y, z in verts]   # 언리얼 OBJ 임포터는 축을 안 돌린다 — Z 위 그대로
    # UV 가 없으면 Interchange OBJ 번역기가 ensure(UVs.IsValidIndex) 를 터뜨려 커맨드릿이 exit 1 — 평면 UV 를 정점마다 하나씩
    lines += ["vt {:.4f} {:.4f}".format(x / 1000.0, y / 1000.0) for x, y, _ in verts]
    for slot in ("Cliff", "Grass"):
        lines += ["usemtl " + slot] + faces[slot]
    with open(os.path.join(OUT_DIR, name + ".obj"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(OUT_DIR, name + ".mtl"), "w") as f:
        f.write("newmtl Cliff\nKd 0.55 0.53 0.50\nnewmtl Grass\nKd 0.26 0.55 0.30\n")
    return sum(len(v) for v in faces.values())

os.makedirs(OUT_DIR, exist_ok=True)
rng = random.Random(SEED)
for shape_name, shape_radius in SHAPES:
    print("저장:", os.path.abspath(os.path.join(OUT_DIR, shape_name + ".obj")), "삼각형", _write(shape_name, shape_radius, rng))
