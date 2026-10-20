"""INBOX 결함 2026-10-04 12:40 얼굴 그림 키우기 — 몸 텍스처의 얼굴을 다시 그린다 (시스템 python3).

  python3 tools/unreal/make_player_face_texture.py
    → _Source/raw/Snare/Characters/Player/T_Player_Body_BaseColor_Face.png  (반입은 `import_player_face_texture.py`)
    → _Source/raw/Snare/Characters/Player/T_Player_Body_BaseColor_Blink.png (눈 감은 짝 — `UPlayerFaceBlinkComponent` 가 잠깐 바꿔 낀다)
    → ..._Hurt.png(싫음) · ..._Down.png(기절 ✕ 눈) · ..._Happy.png(웃음 ^ ^) — M85 표정 3종, DESIGN 2.2 「표정 3종」

바르코 몸(`standing_idle_1.glb`)의 얼굴은 머리 앞면에 그려진 작은 점 눈·눈썹·코·입이라(눈 높이 ≈ 머리 높이의 1/9)
55° 게임 카메라의 머리 약 50px 안에서는 두 눈을 알아볼 수 없다(`captures/397_face_walk.png`). 레퍼런스는 평평한 앞면을
**큰 눈 2개(앞면 가운데 높이)·짧은 눈썹·작은 입**이 거의 채운다(`docs/reference/character_player.md`, INBOX 12:40 ②).

얼굴 그림은 UV 조각 여러 개에 흩어져 있어 그림판으로 못 고친다 → 상자 머리 glb 의 머리 앞면 삼각형을 UV 로 래스터해
텍셀마다 3D 자리를 구하고, 머리 공 중심 기준 **앞면 좌표**(u = 가로 / (반지름 × 가로 배율), v = 세로 / 반지름, 위 +)로
옛 얼굴을 피부색으로 지운 뒤 새 얼굴을 그린다. 3D 자리로 그리므로 UV 이음매를 건너도 모양이 이어진다.
값은 DESIGN 3.3 몸 표 「얼굴 그림」 행. 원본 그림 `T_Player_Body_BaseColor` 는 glb 안 0번 그림(4096², sRGB).
"""
import io
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_player_box_head import (FIT_MIN_WEIGHT, FIT_TOP_FRACTION, HEAD_BONE, HEAD_BOX_WIDTH, NECK_BONE,  # noqa: E402
                                  OUTPUT as BOXHEAD_GLB, SOURCE as BODY_GLB, _read_glb, _view, fit_sphere)

OUTPUT = os.path.join(os.path.dirname(BODY_GLB), "T_Player_Body_BaseColor_Face.png")
BLINK_OUTPUT = os.path.join(os.path.dirname(BODY_GLB), "T_Player_Body_BaseColor_Blink.png")
EXPRESSION_OUTPUTS = {name: os.path.join(os.path.dirname(BODY_GLB), "T_Player_Body_BaseColor_{}.png".format(name))
                      for name in ("Hurt", "Down", "Happy")}

# DESIGN 3.3 「얼굴 그림」 — 앞면 좌표(u 가로 ±1 = 머리 옆 끝, v 세로 ±1 = 머리 공 반지름)
EYE_CENTER_U = 0.40        # 두 눈 가운데 — 옛 눈(±0.33)과 거의 같은 자리, 앞면 평평한 띠(±0.5) 안
EYE_CENTER_V = -0.04       # 앞면 가운데 높이 — 머리카락 앞머리 끝(v ≈ 0.35)과 턱 사이 보이는 앞면의 위쪽 절반
EYE_HALF_U = 0.19         # 눈 반 너비 — M97-c-4: 실제 폭:높이 1:1.35 (0.21 / 1.35 / 가로 배율 1.12), 옛 0.12 는 1:1.56
EYE_HALF_V = 0.30          # 눈 반 높이 — 옛 눈(≈0.09)의 약 2.3배, 세로로 긴 타원
# 볼 점 (M97-c-4, DESIGN 7.1 「캐릭터 머리·얼굴」) — 지름 = 눈 폭 × 0.8, #E8A08A 불투명 0.6, 눈 아래 바깥쪽
CHEEK_U, CHEEK_V = 0.62, -0.42
CHEEK_HALF_V = (2 * EYE_HALF_U * HEAD_BOX_WIDTH) * 0.8 / 2
CHEEK_HALF_U = CHEEK_HALF_V / HEAD_BOX_WIDTH   # 앞면 좌표 u 는 가로 배율로 나눈 값 — 실제로 동그랗게
CHEEK_ALPHA = 0.6
CHEEK_RGB = np.array([214, 104, 100], dtype=float)   # 피부(241,163,121)가 이미 살구색이라 #E8A08A 는 안 보임 — 같은 계열 더 진한 장미색 🟡
HIGHLIGHT = (0.05, 0.12, 0.05)     # 눈 속 큰 흰 점 (눈 가운데에서 바깥쪽 u, 위 v, 반지름)
HIGHLIGHT2 = (-0.05, -0.14, 0.024)  # 작은 흰 점 — 안쪽 아래 (애니풍 반짝임 둘)
BROW_V = 0.38              # 눈썹 높이 — 앞머리 끝 바로 아래
BROW_HALF_U, BROW_HALF_T = 0.085, 0.022
MOUTH_V, MOUTH_HALF_U, MOUTH_HALF_T, MOUTH_SAG = -0.48, 0.07, 0.018, 0.035
# 눈 감은 짝 — 눈 자리에 아래로 휜 짧은 선(웃으며 감은 눈 「︶」), 눈과 같은 색. 선 폭 = 눈썹보다 조금 굵게
CLOSED_HALF_U, CLOSED_HALF_T, CLOSED_SAG = 0.16, 0.03, 0.06
# 표정 3종 (M85-01) — 눈·눈썹·입 모양만 바꾼다. 같은 자리·같은 색이라 지운 바탕 위에 그대로 덮인다
HURT_BROW_TILT = 0.05      # 싫음 눈썹 — 안쪽 끝이 이만큼 올라가고 바깥 끝이 내려간다(보통은 반대로 바깥이 살짝 내려옴)
HURT_MOUTH_SAG = -0.03     # 입꼬리 내림 — 음수 = 가운데가 위로 솟은 「∩」
CROSS_HALF = 0.14          # 기절 ✕ 눈 반 크기(앞면 좌표) — 눈 타원(0.12×0.21)에 들어가는 정사각
CROSS_HALF_T = 0.024
DOWN_MOUTH_HALF_U, DOWN_MOUTH_SAG = 0.05, -0.012   # 기절 입 — 작고 거의 곧은 선
HAPPY_EYE_HALF_U, HAPPY_EYE_SAG = 0.16, -0.07       # 웃는 눈 「^」 — 감은 눈(︶)을 뒤집은 모양
HAPPY_MOUTH_HALF_U, HAPPY_MOUTH_HALF_T, HAPPY_MOUTH_SAG = 0.12, 0.024, 0.075   # 활짝 웃는 입 — 평소의 약 1.7배 너비
EXPRESSIONS = ("Hurt", "Down", "Happy")
ERASE_U, ERASE_V = (0.85, (-0.75, 0.55))   # 옛 얼굴을 지우는 앞면 구역
ERASE_COLOR_DIST = 22.0    # 피부 가운데 색에서 이만큼(0~255 RGB 거리) 떨어진 텍셀 = 옛 얼굴 그림
FRONT_MIN = 0.3            # 앞 성분(앞뒤 / 반지름) 이 이보다 커야 앞면 — 뒤통수에 안 그린다
EDGE = 0.012               # 가장자리 부드럽게 하는 폭(앞면 좌표)
# 팔레트 — 옛 그림에서 고른 색(눈 짙은 밤색·눈썹 갈색), 입은 눈보다 한 단 밝게
EYE_RGB = np.array([48, 28, 34], dtype=float)
SHINE_RGB = np.array([250, 246, 240], dtype=float)
BROW_RGB = np.array([132, 72, 44], dtype=float)
MOUTH_RGB = np.array([96, 44, 44], dtype=float)


def _ellipse_sd(u, v, cu, cv, hu, hv):
    """타원 대략 부호 거리(앞면 좌표, 안쪽 음수)."""
    k = np.sqrt(((u - cu) / hu) ** 2 + ((v - cv) / hv) ** 2)
    return (k - 1.0) * min(hu, hv)


def _segment_sd(u, v, a, b, half_t):
    pa = np.stack([u - a[0], v - a[1]], -1)
    ba = np.array([b[0] - a[0], b[1] - a[1]])
    h = np.clip((pa @ ba) / (ba @ ba), 0.0, 1.0)
    d = pa - h[..., None] * ba
    return np.sqrt(np.sum(d * d, -1)) - half_t


def _cover(sd):
    return np.clip(0.5 - sd / EDGE, 0.0, 1.0)


def _arc_sd(u, v, cu, cv, half_u, half_t, sag):
    """가운데가 `sag` 만큼 아래로 처진 짧은 곡선(입·감은 눈)의 대략 부호 거리."""
    k = np.clip((u - cu) / half_u, -1.0, 1.0)
    line_v = cv - sag * (1.0 - k * k)
    return np.where(np.abs(u - cu) <= half_u, np.abs(v - line_v) - half_t,
                    np.hypot(np.abs(u - cu) - half_u, v - cv) - half_t)


def face_layers(u, v, closed=False, kind=None):
    """앞면 좌표 → [(덮음 0~1, 색)] 아래부터 위 순서. `closed` 면 눈 대신 감은 눈 선(흰 점 없음).
    `kind` 가 `Hurt`·`Down`·`Happy` 면 표정 3종(M85) — 눈·눈썹·입 모양만 다르다."""
    layers = []
    eyes = np.zeros_like(u)
    shine = np.zeros_like(u)
    brows = np.zeros_like(u)
    for side in (-1.0, 1.0):
        cu = side * EYE_CENTER_U
        if kind == "Down":
            # ✕ — 눈 자리에 가운데 교차하는 선 두 개
            for sy in (-1.0, 1.0):
                eyes = np.maximum(eyes, _cover(_segment_sd(
                    u, v, (cu - CROSS_HALF, EYE_CENTER_V - sy * CROSS_HALF),
                    (cu + CROSS_HALF, EYE_CENTER_V + sy * CROSS_HALF), CROSS_HALF_T)))
        elif kind == "Happy":
            eyes = np.maximum(eyes, _cover(_arc_sd(u, v, cu, EYE_CENTER_V, HAPPY_EYE_HALF_U, CLOSED_HALF_T, HAPPY_EYE_SAG)))
        elif closed:
            eyes = np.maximum(eyes, _cover(_arc_sd(u, v, cu, EYE_CENTER_V, CLOSED_HALF_U, CLOSED_HALF_T, CLOSED_SAG)))
        else:
            eyes = np.maximum(eyes, _cover(_ellipse_sd(u, v, cu, EYE_CENTER_V, EYE_HALF_U, EYE_HALF_V)))
            su, sv, sr = HIGHLIGHT
            shine = np.maximum(shine, _cover(np.hypot(u - (cu + side * su), v - (EYE_CENTER_V + sv)) - sr))
            su2, sv2, sr2 = HIGHLIGHT2
            shine = np.maximum(shine, _cover(np.hypot(u - (cu + side * su2), v - (EYE_CENTER_V + sv2)) - sr2))
        if kind == "Hurt":
            # 싫음 — 안쪽(가운데 쪽) 끝이 올라가고 바깥 끝이 내려간 「ㅅ」 눈썹
            inner = (cu - side * BROW_HALF_U, BROW_V + HURT_BROW_TILT)
            outer = (cu + side * BROW_HALF_U, BROW_V - HURT_BROW_TILT)
            brows = np.maximum(brows, _cover(_segment_sd(u, v, inner, outer, BROW_HALF_T)))
        elif kind != "Down":
            # 눈썹은 바깥쪽이 살짝 내려온 짧은 막대
            brows = np.maximum(brows, _cover(_segment_sd(u, v, (cu - side * BROW_HALF_U, BROW_V + 0.01),
                                                         (cu + side * BROW_HALF_U, BROW_V - 0.01), BROW_HALF_T)))
    # 입 — 평소 작게 웃는 곡선(가운데가 아래로)
    if kind == "Hurt":
        mouth_sd = _arc_sd(u, v, 0.0, MOUTH_V, MOUTH_HALF_U, MOUTH_HALF_T, HURT_MOUTH_SAG)
    elif kind == "Down":
        mouth_sd = _arc_sd(u, v, 0.0, MOUTH_V, DOWN_MOUTH_HALF_U, MOUTH_HALF_T, DOWN_MOUTH_SAG)
    elif kind == "Happy":
        mouth_sd = _arc_sd(u, v, 0.0, MOUTH_V + 0.02, HAPPY_MOUTH_HALF_U, HAPPY_MOUTH_HALF_T, HAPPY_MOUTH_SAG)
    else:
        mouth_sd = _arc_sd(u, v, 0.0, MOUTH_V, MOUTH_HALF_U, MOUTH_HALF_T, MOUTH_SAG)
    cheeks = np.zeros_like(u)
    for side in (-1.0, 1.0):
        cheeks = np.maximum(cheeks, _cover(_ellipse_sd(u, v, side * CHEEK_U, CHEEK_V, CHEEK_HALF_U, CHEEK_HALF_V)))
    layers.append((cheeks * CHEEK_ALPHA, CHEEK_RGB))
    layers.append((brows, BROW_RGB))
    layers.append((_cover(mouth_sd), MOUTH_RGB))
    layers.append((eyes, EYE_RGB))
    layers.append((shine * eyes, SHINE_RGB))
    return layers


def head_frame():
    """원본 몸(공 머리)에서 머리 공 중심·반지름 — `make_player_box_head.py` 와 같은 맞춤."""
    gltf, binary = _read_glb(BODY_GLB)
    names = [gltf["nodes"][k].get("name") for k in gltf["skins"][0]["joints"]]
    attrs = gltf["meshes"][0]["primitives"][0]["attributes"]
    pos = _view(gltf, binary, attrs["POSITION"])[0].astype(np.float64)
    joints = _view(gltf, binary, attrs["JOINTS_0"])[0]
    weights = _view(gltf, binary, attrs["WEIGHTS_0"])[0]
    w = np.sum(np.where(joints == names.index(HEAD_BONE), weights, 0.0), axis=1)
    head = pos[w > FIT_MIN_WEIGHT]
    crown, low = head[:, 1].max(), head[:, 1].min()
    return fit_sphere(head[head[:, 1] > crown - FIT_TOP_FRACTION * (crown - low)])


def main():
    centre, radius = head_frame()
    gltf, binary = _read_glb(BOXHEAD_GLB)
    names = [gltf["nodes"][k].get("name") for k in gltf["skins"][0]["joints"]]
    prim = gltf["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]
    pos = _view(gltf, binary, attrs["POSITION"])[0].astype(np.float64)
    uv = _view(gltf, binary, attrs["TEXCOORD_0"])[0].astype(np.float64)
    joints = _view(gltf, binary, attrs["JOINTS_0"])[0]
    weights = _view(gltf, binary, attrs["WEIGHTS_0"])[0]
    tris = _view(gltf, binary, prim["indices"])[0].reshape(-1, 3).astype(np.int64)
    hw = np.sum(np.where(np.isin(joints, [names.index(HEAD_BONE), names.index(NECK_BONE)]), weights, 0.0), axis=1)

    image_index = gltf["materials"][prim["material"]]["pbrMetallicRoughness"]["baseColorTexture"]["index"]
    view = gltf["bufferViews"][gltf["images"][gltf["textures"][image_index]["source"]]["bufferView"]]
    start = view.get("byteOffset", 0)
    base = Image.open(io.BytesIO(bytes(binary[start:start + view["byteLength"]]))).convert("RGB")
    img = np.asarray(base, dtype=float).copy()
    size_y, size_x = img.shape[:2]

    # 앞면 텍셀 모으기 — 머리 무게가 있고 앞을 보는 삼각형만
    rel = (pos - centre) / radius
    pick = (np.max(hw[tris], axis=1) > 0.5) & (np.max(rel[tris][:, :, 2], axis=1) > FRONT_MIN)
    px_all, py_all, p3_all = [], [], []
    for tri in tris[pick]:
        t_uv = uv[tri] * [size_x, size_y]
        lo = np.floor(t_uv.min(0)).astype(int) - 1
        hi = np.ceil(t_uv.max(0)).astype(int) + 1
        xs, ys = np.meshgrid(np.arange(lo[0], hi[0] + 1), np.arange(lo[1], hi[1] + 1))
        pts = np.stack([xs.ravel() + 0.5, ys.ravel() + 0.5], -1)
        m = np.array([t_uv[1] - t_uv[0], t_uv[2] - t_uv[0]]).T
        if abs(np.linalg.det(m)) < 1e-9:
            continue
        bc = np.linalg.solve(m, (pts - t_uv[0]).T).T
        bary = np.stack([1 - bc[:, 0] - bc[:, 1], bc[:, 0], bc[:, 1]], -1)
        inside = np.all(bary > -0.02, axis=1)   # 2% 넓혀 조각 이음매 틈을 메운다
        bary = np.clip(bary[inside], 0.0, None)
        bary /= bary.sum(1, keepdims=True)
        px_all.append(xs.ravel()[inside])
        py_all.append(ys.ravel()[inside])
        p3_all.append(bary @ rel[tri])
    px = np.clip(np.concatenate(px_all), 0, size_x - 1)
    py = np.clip(np.concatenate(py_all), 0, size_y - 1)
    p3 = np.concatenate(p3_all)
    u = p3[:, 0] / HEAD_BOX_WIDTH
    v = p3[:, 1]
    front = p3[:, 2] > FRONT_MIN

    # 옛 얼굴 지우기 — 구역 안에서 피부 가운데 색과 먼 텍셀(+2px 번짐)을 피부색으로
    zone = front & (np.abs(u) < ERASE_U) & (v > ERASE_V[0]) & (v < ERASE_V[1])
    colours = img[py[zone], px[zone]]
    skin = np.median(colours, axis=0)
    far = np.linalg.norm(colours - skin, axis=1) > ERASE_COLOR_DIST
    mask = np.zeros((size_y, size_x), dtype=np.uint8)
    mask[py[zone][far], px[zone][far]] = 255
    mask = np.asarray(Image.fromarray(mask).filter(ImageFilter.MaxFilter(5))) > 0
    zone_mask = np.zeros((size_y, size_x), dtype=bool)
    zone_mask[py[zone], px[zone]] = True
    erase = mask & zone_mask
    img[erase] = skin

    # 새 얼굴 그리기 — 눈 뜬 그림과 눈 감은 짝을 같은 지운 바탕에
    fu, fv, fx, fy = u[front], v[front], px[front], py[front]
    erased = img.copy()
    drawn = [(False, None, OUTPUT), (True, None, BLINK_OUTPUT)]
    drawn += [(False, name, EXPRESSION_OUTPUTS[name]) for name in EXPRESSIONS]
    expression_img = {}
    for closed, kind, path in drawn:
        img = erased.copy()
        for cover, rgb in face_layers(fu, fv, closed, kind):
            hit = cover > 0
            cur = img[fy[hit], fx[hit]]
            img[fy[hit], fx[hit]] = cur + (rgb - cur) * cover[hit, None]
        Image.fromarray(np.clip(img + 0.5, 0, 255).astype(np.uint8)).save(path)
        if kind is not None:
            expression_img[kind] = img
        if path == BLINK_OUTPUT:
            blink_img = img
    img = blink_img
    # 감은 짝(마지막 `img`)에서 뜬 눈 타원 안 · 감은 선 밖 텍셀이 전부 피부색이어야 한다 — 옛 눈·새 눈이 비치면 깜빡여도 눈이 남는다
    left_open = 0
    for side in (-1.0, 1.0):
        cu = side * EYE_CENTER_U
        rim = (_ellipse_sd(fu, fv, cu, EYE_CENTER_V, EYE_HALF_U, EYE_HALF_V) < 0) & (
            _arc_sd(fu, fv, cu, EYE_CENTER_V, CLOSED_HALF_U, CLOSED_HALF_T + 2 * EDGE, CLOSED_SAG) > 0)
        left_open += int(np.sum(np.linalg.norm(img[fy[rim], fx[rim]] - skin, axis=1) > 2 * ERASE_COLOR_DIST))

    # 보고 + 자체 검사
    eye_px = int(np.sum(face_layers(fu, fv)[3][0] > 0.5))
    print("[Face] 머리 공 중심 ({:.4f}, {:.4f}, {:.4f}) · 반지름 {:.4f} · 앞면 삼각형 {} · 앞면 텍셀 {}".format(
        *centre, radius, int(pick.sum()), int(front.sum())))
    print("[Face] 피부 가운데 색 ({:.0f}, {:.0f}, {:.0f}) · 지운 텍셀 {} · 새 눈 텍셀 {}".format(*skin, int(erase.sum()), eye_px))
    assert int(erase.sum()) > 0, "옛 얼굴을 못 찾았다"
    for side in (-1.0, 1.0):
        near = (np.abs(fu - side * EYE_CENTER_U) < 0.02) & (np.abs(fv - EYE_CENTER_V) < 0.02)
        assert near.any(), "눈 가운데 자리가 앞면 텍셀에 없다 (u {})".format(side * EYE_CENTER_U)
    shut_px = int(np.sum(face_layers(fu, fv, True)[3][0] > 0.5))
    print("[Face] 감은 눈 선 텍셀 {} (뜬 눈의 {:.0%})".format(shut_px, shut_px / max(eye_px, 1)))
    print("[Face] 감은 짝 눈 자리에 남은 짙은 텍셀 {}".format(left_open))
    assert 0 < shut_px < eye_px, "감은 눈 선이 없거나 뜬 눈보다 크다"
    assert left_open == 0, "감은 짝에 눈이 남았다"
    # 표정 3종 — 눈 뜬 그림과 얼굴 자리만 달라야 하고(밖은 같다), 셋이 서로 달라야 한다
    base = Image.open(OUTPUT).convert("RGB")
    base_arr = np.asarray(base, dtype=float)
    kinds = list(EXPRESSIONS)
    for kind in kinds:
        saved = np.asarray(Image.open(EXPRESSION_OUTPUTS[kind]).convert("RGB"), dtype=float)
        changed = np.abs(saved - base_arr).sum(2) > 30
        outside = changed.copy()
        outside[fy, fx] = False   # 앞면 텍셀 밖에서 바뀐 것
        print("[Face] {} 바뀐 텍셀 {} ({:.3%}) · 앞면 밖 바뀜 {}".format(kind, int(changed.sum()), changed.mean(), int(outside.sum())))
        assert changed.sum() > 0, "{} 가 눈 뜬 그림과 같다".format(kind)
        assert outside.sum() == 0, "{} 가 얼굴 밖을 바꿨다".format(kind)
    for a in range(len(kinds)):
        for b in range(a + 1, len(kinds)):
            ia = np.asarray(Image.open(EXPRESSION_OUTPUTS[kinds[a]]).convert("RGB"), dtype=float)
            ib = np.asarray(Image.open(EXPRESSION_OUTPUTS[kinds[b]]).convert("RGB"), dtype=float)
            diff = int((np.abs(ia - ib).sum(2) > 30).sum())
            print("[Face] {} ↔ {} 다른 텍셀 {}".format(kinds[a], kinds[b], diff))
            assert diff > 0, "{} 와 {} 가 같다".format(kinds[a], kinds[b])
    print("[Face] 저장 {} · {} · 표정 {} · 자체 검사 통과".format(OUTPUT, BLINK_OUTPUT, ", ".join(kinds)))


if __name__ == "__main__":
    main()
