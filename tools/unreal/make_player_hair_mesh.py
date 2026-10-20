"""M19-01 기본 머리카락 덩어리 원본 만들기 (시스템 python3 — 언리얼 밖에서 돌린다).

  python3 tools/unreal/make_player_hair_mesh.py   → Saved/Generated/PlayerHair.obj (+ .mtl)

레퍼런스 캐릭터는 모자를 안 쓰면 머리카락 **덩어리 하나**를 쓴다(시트 `character_player.md` "머리카락은 덩어리 하나(모자와 교체)",
`steam_06`·`steam_19`·`steam_21`). 우리 몸(`SK_Player_Body`)은 모자가 없으면 민머리 공이었다(`271_swing_body_strip.png`).

좌표는 **`Socket_Head` 공간 그대로**(몸 원본 단위 cm, 축 = 몸 컴포넌트 축) — 원점 = 정수리, +Z 위, **+Y 앞**(몸이 +Y 를 보고 서 있다,
`ASnarehavenCharacter` 가 -90° 돌려 세운다). 머리 공 치수는 지어내지 않고 원본 `tpose_mesh.glb` 에서 쟀다(루프#278):
키 1.0 에서 머리 정점(y > 0.15) 구 맞춤 중심 y 0.271 · 반지름 0.235 · 정수리 0.5 → 반입 몸(키 99.96cm)에서 정수리 아래 22.9cm 가 중심, 반지름 23.5cm.
코드(`UEquipmentComponent`)가 소켓에 붙이고 월드 배율을 몸 배율로 맞춘다 — 소켓 자체 배율(모자 자리표시 0.3·0.12)은 안 받는다.
**OBJ 반입이 Y 를 뒤집는다**(`make_island_meshes.py` 와 같다) — 파일에는 앞(+Y)을 -y 로 적는다. 안 뒤집었던 M19-01~#396 은
머리카락이 앞뒤 거꾸로 씌워져 긴 뒷머리(128°)가 얼굴을 덮었다(INBOX 12:40 ④, 2층 `HairLeavesEyesVisible` 48선 중 48 가림).

덩어리 모양: 공보다 조금 큰 껍질이 정수리부터 내려와 앞은 이마 위(극각 62°)에서 앞머리 술 다섯으로 끊기고,
옆은 귀 높이(96°), 뒤는 뒤통수 아래(128°)까지 덮는다. 끝은 안쪽(머리 속)으로 접어 두께가 보이게 한다.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_tree_meshes import OUT_DIR, Mesh  # noqa: E402
from make_player_box_head import HEAD_BOX_WIDTH, box_radius  # noqa: E402  머리가 둥근 상자다(INBOX 12:40 ②)

HEAD_CENTER_BELOW_CROWN_CM = 22.9   # 정수리 → 머리 공 중심 (tpose_mesh.glb 실측, 위 머리말)
HEAD_RADIUS_CM = 23.5               # 머리 공 반지름 (같은 실측)
HAIR_PUFF = 1.07                    # 껍질 반지름 = 머리 × 1.07 — 두께 ≈1.6cm(몸 배율 1.76 → 월드 ≈2.9cm). 1.03 은 피부와 깜빡였고 1.15 는 헬멧처럼 떴다
TUCK = 0.94                         # 끝을 접어 넣는 반지름 비 — 머리 표면 안쪽이라 안 보인다
FRONT_DEG, SIDE_DEG, BACK_DEG = 62.0, 96.0, 128.0   # 끝 극각: 앞 = 눈썹(≈83°) 위 이마 · 옆 = 귀 · 뒤 = 뒤통수 아래
BANG_COUNT = 5                      # 앞머리 술 — steam_19 앞머리는 넓적한 갈래 4~6
BANG_DEPTH_DEG = 9.0                # 술 끝이 홈보다 내려오는 극각
BANG_SPAN_DEG = 70.0                # 앞 ±70° 안에서만 술이 진다
OBJ_Y = -1.0                        # OBJ 반입 Y 뒤집힘을 미리 되돌린다(위 머리말)
SEGMENTS = 60                       # 둘레 — 술 다섯이 각마다 정점 2~3개
RINGS = 14                          # 정수리 → 끝


def edge_polar_deg(azimuth):
    """azimuth 0 = 앞(+Y). 앞 62° → 옆 96° → 뒤 128° 로 부드럽게, 앞쪽만 술 홈을 판다."""
    c = math.cos(azimuth)
    base = SIDE_DEG + (FRONT_DEG - SIDE_DEG) * max(c, 0.0) + (BACK_DEG - SIDE_DEG) * max(-c, 0.0)
    deg = math.degrees(abs(math.atan2(math.sin(azimuth), c)))
    if deg < BANG_SPAN_DEG:
        fade = math.cos(math.pi * 0.5 * deg / BANG_SPAN_DEG)
        teeth = abs(math.cos(math.pi * BANG_COUNT * deg / (2.0 * BANG_SPAN_DEG)))   # 0° 에 술 하나, 양옆으로 둘씩
        base += BANG_DEPTH_DEG * fade * (teeth ** 3 - 0.5)
    return base


def point(radius, polar_deg, azimuth):
    """공 위 자리를 몸 머리와 같은 둥근 상자(`make_player_box_head.box_radius`, X = 가로)로 민다."""
    t = math.radians(polar_deg)
    d = (math.sin(t) * math.sin(azimuth), math.sin(t) * math.cos(azimuth), math.cos(t))
    radius *= float(box_radius(d))
    return (radius * d[0],   # X = 옆
            OBJ_Y * radius * d[1],   # Y = 앞(파일에는 뒤집어 적는다)
            -HEAD_CENTER_BELOW_CROWN_CM + radius * d[2])


def hair():
    mesh = Mesh()
    outer_r = HEAD_RADIUS_CM * HAIR_PUFF
    centre = (0.0, 0.0, -HEAD_CENTER_BELOW_CROWN_CM)
    top = mesh.v(*point(outer_r, 0.0, 0.0))
    rings = []
    for i in range(1, RINGS + 1):
        ring = []
        for j in range(SEGMENTS):
            az = math.tau * j / SEGMENTS
            ring.append(mesh.v(*point(outer_r, edge_polar_deg(az) * i / RINGS, az)))
        rings.append(ring)

    def out(*ids):
        p = [sum(mesh.verts[k][a] for k in ids) / len(ids) for a in range(3)]
        return tuple(p[a] - centre[a] for a in range(3))

    for j in range(SEGMENTS):
        a, b = rings[0][j], rings[0][(j + 1) % SEGMENTS]
        mesh.tri("Hair", a, b, top, out(a, b, top))
    for i in range(RINGS - 1):
        for j in range(SEGMENTS):
            k = (j + 1) % SEGMENTS
            a, b, c, d = rings[i][j], rings[i][k], rings[i + 1][k], rings[i + 1][j]
            mesh.tri("Hair", a, b, c, out(a, b, c))
            mesh.tri("Hair", a, c, d, out(a, c, d))
    # 끝 접기 — 묶음을 갈라 모서리를 날카롭게(덩어리 두께가 선으로 읽힌다)
    mesh.group = 2
    tuck = [mesh.v(*point(HEAD_RADIUS_CM * TUCK, edge_polar_deg(math.tau * j / SEGMENTS), math.tau * j / SEGMENTS))
            for j in range(SEGMENTS)]
    rim = rings[-1]
    for j in range(SEGMENTS):
        k = (j + 1) % SEGMENTS
        # 접힌 면은 아래(끝 쪽)를 본다 — 바깥 껍질에서 공 중심 방향을 뺀 끝 진행 방향
        az = math.tau * (j + 0.5) / SEGMENTS
        t = math.radians(edge_polar_deg(az))
        down = (math.cos(t) * math.sin(az), OBJ_Y * math.cos(t) * math.cos(az), -math.sin(t))
        mesh.tri("Hair", rim[j], rim[k], tuck[k], down)
        mesh.tri("Hair", rim[j], tuck[k], tuck[j], down)
    return mesh


def _selfcheck(mesh):
    zs = [p[2] for p in mesh.verts]
    assert abs(max(zs) - (HEAD_RADIUS_CM * HAIR_PUFF - HEAD_CENTER_BELOW_CROWN_CM)) < 0.01, max(zs)
    front = [p for p in mesh.verts if p[1] * OBJ_Y > 0 and abs(p[0]) < 2.0]
    back = [p for p in mesh.verts if p[1] * OBJ_Y < 0 and abs(p[0]) < 2.0]
    # 앞은 이마 위에서 끊기고 뒤는 더 내려온다 — 얼굴(눈썹 ≈ 중심 위 0.12r)을 덮지 않는다
    assert min(p[2] for p in front) > -HEAD_CENTER_BELOW_CROWN_CM + 0.12 * HEAD_RADIUS_CM, min(p[2] for p in front)
    assert min(p[2] for p in back) < min(p[2] for p in front) - 10.0
    assert all(abs(p[0]) < HEAD_RADIUS_CM * HAIR_PUFF * HEAD_BOX_WIDTH + 0.01 for p in mesh.verts)


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    built = hair()
    _selfcheck(built)
    print("저장:", os.path.abspath(os.path.join(OUT_DIR, "PlayerHair.obj")), "삼각형", built.write("PlayerHair", ["Hair"]))
    print("[PlayerHair] 자체 검사 통과")
