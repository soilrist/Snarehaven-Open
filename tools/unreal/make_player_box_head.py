"""INBOX 결함 2026-10-04 12:40 ② 머리 모양 — 몸 원본의 머리 공을 앞면 평평한 둥근 상자로 민다 (시스템 python3).

  python3 tools/unreal/make_player_box_head.py
    → _Source/raw/Snare/Characters/Player/standing_idle_1_boxhead.glb  (반입은 `import_player_character.py`)

원본 `standing_idle_1.glb`(gitignore, 바르코 리깅 몸)는 머리가 공이고 얼굴 그림(눈·눈썹·입)이 공 앞에 붙어 있어
55° 위 게임 카메라에서는 얼굴이 공 아래로 휘어 숨는다(`captures/395_face_walk2.png`). 레퍼런스 머리는 가로가 세로보다
넓고 앞면이 세로로 평평하다(INBOX 12:40 ②, `docs/reference/character_player.md`).

**glb 를 Blender 로 왕복하지 않는다** — 뼈 방향·노드 변환이 바뀌면 이미 반입한 스켈레톤·클립 33개와 어긋난다.
바이너리 안 POSITION·NORMAL 값만 같은 자리에 덮어쓰고 나머지(뼈·스킨·UV·그림·클립)는 한 바이트도 안 바꾼다.

모양: `Head` 뼈 무게가 있는 정점을 머리 공 중심에서 **초타원체** |x/a|^p + |y|^p + |z|^p = 1 로 민다(방향은 그대로,
반지름만 바꾼다 → 얼굴 그림이 같은 각도 자리에 남는다). p 가 클수록 면이 평평하고 모서리가 각진다. 위(정수리)·앞·뒤 반지름은
그대로라 정수리 높이(`Socket_Head`)·얼굴 앞 깊이가 안 바뀐다. 목 이음매는 `Head` 무게로 섞어 찢어지지 않는다.
값은 DESIGN 3.3 몸 표 「머리 모양」 행.

머리카락(`make_player_hair_mesh.py`)도 `box_radius` 를 같이 써 껍질이 새 머리를 덮는다.
"""
import json
import math
import os
import struct

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SOURCE = os.path.join(ROOT, "_Source", "raw", "Snare", "Characters", "Player", "standing_idle_1.glb")
OUTPUT = os.path.join(ROOT, "_Source", "raw", "Snare", "Characters", "Player", "standing_idle_1_boxhead.glb")

HEAD_BOX_EXPONENT = 2.5   # DESIGN 3.3 「머리 모양」 — 초타원체 지수(2 = 공). 2.5 = 모서리 없는 찹쌀떡(사람 10-11 「네모머리 맘에 안 든다」, 4 에서 낮춤)
HEAD_BOX_WIDTH = 1.12     # DESIGN 3.3 — 가로(좌우) 반지름 배율. 세로·앞뒤는 1(정수리·얼굴 깊이 그대로)
HEAD_BONE = "Head"
NECK_BONE = "Neck"          # 머리 공 아래 절반은 `Head`·`Neck` 무게가 나뉘어 있다 — 둘을 합쳐야 공 전체가 고르게 밀린다
FADE_ELEVATION_DEG = (-80.0, -60.0)   # 공 중심에서 이 아래쪽 방향(목·어깨)은 0 → 1 로 서서히 민다
FIT_MIN_WEIGHT = 0.99     # 공 맞춤에 쓰는 정점 — 머리 뼈만 따르는 것
FIT_TOP_FRACTION = 0.6    # 공 맞춤은 정수리에서 이 비율(머리 높이 기준) 위 정점만 — 목·턱 이음매 제외


def box_radius(direction, width=HEAD_BOX_WIDTH, exponent=HEAD_BOX_EXPONENT, side_axis=0):
    """단위 방향 → 초타원체 반지름 배율(공 = 1). `side_axis` = 가로(좌우) 축 번호."""
    d = np.asarray(direction, dtype=float)
    scale = np.ones(d.shape[-1])
    scale[side_axis] = width
    return 1.0 / np.power(np.sum(np.power(np.abs(d / scale), exponent), axis=-1), 1.0 / exponent)


def _read_glb(path):
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and version == 2, "glb 아님"
    json_len, json_type = struct.unpack_from("<II", data, 12)
    assert json_type == 0x4E4F534A
    gltf = json.loads(data[20:20 + json_len])
    bin_off = 20 + json_len
    bin_len, bin_type = struct.unpack_from("<II", data, bin_off)
    assert bin_type == 0x004E4942
    return gltf, bytearray(data[bin_off + 8:bin_off + 8 + bin_len])


def _write_glb(path, gltf, binary):
    text = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    text += b" " * ((4 - len(text) % 4) % 4)
    binary = bytes(binary) + b"\0" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    with open(path, "wb") as out:
        out.write(struct.pack("<III", 0x46546C67, 2, total))
        out.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        out.write(struct.pack("<II", len(binary), 0x004E4942) + binary)


_COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
_DTYPES = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}


def _view(gltf, binary, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    dtype = np.dtype(_DTYPES[acc["componentType"]])
    comps = _COMPONENTS[acc["type"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    assert view.get("byteStride", dtype.itemsize * comps) == dtype.itemsize * comps, "띄엄 버퍼는 안 다룬다"
    arr = np.frombuffer(binary, dtype=dtype, count=acc["count"] * comps, offset=start).reshape(acc["count"], comps)
    return arr, start


def fit_sphere(points):
    """최소제곱 공 맞춤 → (중심, 반지름)."""
    a = np.hstack([2.0 * points, np.ones((len(points), 1))])
    b = np.sum(points * points, axis=1)
    sol = np.linalg.lstsq(a, b, rcond=None)[0]
    centre = sol[:3]
    return centre, math.sqrt(sol[3] + centre @ centre)


def main():
    gltf, binary = _read_glb(SOURCE)
    skin = gltf["skins"][0]
    names = [gltf["nodes"][k].get("name") for k in skin["joints"]]
    head_joint, neck_joint = names.index(HEAD_BONE), names.index(NECK_BONE)
    prim = gltf["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]
    pos, pos_off = _view(gltf, binary, attrs["POSITION"])
    nrm, nrm_off = _view(gltf, binary, attrs["NORMAL"])
    joints, _ = _view(gltf, binary, attrs["JOINTS_0"])
    weights, _ = _view(gltf, binary, attrs["WEIGHTS_0"])
    tris, _ = _view(gltf, binary, prim["indices"])
    tris = tris.reshape(-1, 3).astype(np.int64)
    pos = pos.astype(np.float64)

    w = np.sum(np.where(joints == head_joint, weights, 0.0), axis=1)
    # glTF 축: +Y 위, +Z 앞(얼굴), X 옆
    head = pos[w > FIT_MIN_WEIGHT]
    crown = head[:, 1].max()
    low = head[:, 1].min()
    centre, radius = fit_sphere(head[head[:, 1] > crown - FIT_TOP_FRACTION * (crown - low)])
    rel = pos - centre
    dist = np.linalg.norm(rel, axis=1)
    unit = rel / dist[:, None]
    pushed = centre + rel * box_radius(unit)[:, None]
    # 미는 정도 = (머리 + 목 무게) × 아래쪽 방향 서서히 — 목·어깨·셔츠는 안 움직인다
    neck_w = np.sum(np.where(joints == neck_joint, weights, 0.0), axis=1)
    elev = np.degrees(np.arcsin(np.clip(unit[:, 1], -1.0, 1.0)))
    lo, hi = FADE_ELEVATION_DEG
    t = np.clip((elev - lo) / (hi - lo), 0.0, 1.0)
    mask = np.clip(w + neck_w, 0.0, 1.0) * t * t * (3.0 - 2.0 * t)
    new_pos = pos + (pushed - pos) * mask[:, None]

    # 법선 — 같은 자리 정점(UV 이음매로 갈린 것)을 묶어 면 법선을 모은다
    keys = {}
    group = np.array([keys.setdefault(tuple(np.round(p, 5)), len(keys)) for p in pos])
    face = np.cross(new_pos[tris[:, 1]] - new_pos[tris[:, 0]], new_pos[tris[:, 2]] - new_pos[tris[:, 0]])
    acc = np.zeros((len(keys), 3))
    for corner in range(3):
        np.add.at(acc, group[tris[:, corner]], face)
    fresh = acc[group]
    fresh /= np.maximum(np.linalg.norm(fresh, axis=1), 1e-12)[:, None]
    old_n = nrm.astype(np.float64)
    # 원본 법선이 바깥을 보는지(감김 방향) 확인해 맞춘다
    if np.mean(np.sum(fresh[w > FIT_MIN_WEIGHT] * old_n[w > FIT_MIN_WEIGHT], axis=1)) < 0:
        fresh = -fresh
    new_n = old_n + (fresh - old_n) * mask[:, None]
    new_n /= np.maximum(np.linalg.norm(new_n, axis=1), 1e-12)[:, None]

    binary[pos_off:pos_off + new_pos.size * 4] = new_pos.astype(np.float32).tobytes()
    binary[nrm_off:nrm_off + new_n.size * 4] = new_n.astype(np.float32).tobytes()
    gltf["accessors"][attrs["POSITION"]]["min"] = new_pos.min(axis=0).astype(np.float32).tolist()
    gltf["accessors"][attrs["POSITION"]]["max"] = new_pos.max(axis=0).astype(np.float32).tolist()
    _write_glb(OUTPUT, gltf, binary)

    # 보고 + 자체 검사 — 단위는 몸 원본(키 ≈ 1.0)과 머리 반지름(r)
    moved = np.linalg.norm(new_pos - pos, axis=1)
    hp = new_pos[w > FIT_MIN_WEIGHT] - centre
    front = hp[(hp[:, 2] > 0) & (np.abs(hp[:, 0]) < 0.5 * radius) & (np.abs(hp[:, 1]) < 0.5 * radius)]
    old_front = (pos[w > FIT_MIN_WEIGHT] - centre)
    old_front = old_front[(old_front[:, 2] > 0) & (np.abs(old_front[:, 0]) < 0.5 * radius)
                          & (np.abs(old_front[:, 1]) < 0.5 * radius)]
    width = hp[:, 0].max() - hp[:, 0].min()
    height = hp[:, 1].max() - hp[:, 1].min()
    print("[BoxHead] 미는 정점 {} · 머리 정점 {} · 공 중심 ({:.4f}, {:.4f}, {:.4f}) · 반지름 {:.4f} · 정수리 {:.4f}".format(
        int(np.sum(mask > 0)), int(np.sum(w > FIT_MIN_WEIGHT)), *centre, radius, crown))
    print("[BoxHead] 앞면 가운데(±r/2) 깊이 흩어짐 {:.3f}r → {:.3f}r · 가로 {:.3f}r · 세로 {:.3f}r · 정수리 이동 {:.5f} · 가장 큰 이동 {:.3f}r".format(
        (old_front[:, 2].max() - old_front[:, 2].min()) / radius, (front[:, 2].max() - front[:, 2].min()) / radius,
        width / radius, height / radius, new_pos[:, 1].max() - crown, moved.max() / radius))
    assert abs(new_pos[:, 1].max() - crown) < 0.005, "정수리 높이가 바뀌었다(0.5cm 넘게)"
    assert (front[:, 2].max() - front[:, 2].min()) < 0.5 * (old_front[:, 2].max() - old_front[:, 2].min()), "앞면이 안 평평해졌다"
    assert width > height, "가로가 세로보다 넓지 않다"
    assert np.all(moved[mask == 0] == 0), "머리 밖 정점이 움직였다"
    print("[BoxHead] 저장 {} · 자체 검사 통과".format(OUTPUT))


if __name__ == "__main__":
    main()
