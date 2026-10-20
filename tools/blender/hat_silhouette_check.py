"""모자 실루엣 닮음 재기 (루프#1361, INBOX 지시 10-20 00:20 꾸미기 모자 57종 다양화의 합격 잣대).

실행 (헤드리스만):
  ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      -P tools/blender/hat_silhouette_check.py [-- MeadowDonut MeadowMouse ...]

`_Source/raw/Snare/Items/Hats/<이름>/Hat<이름>_v<N>.glb`(가장 높은 N — 반입 스크립트가 받는 것) 70개를 읽어
앞(X·Z)·옆(Y·Z) 그림자를 64×64 칸으로 칠한다. 게임은 모자를 폭 하나로 균일 배율하므로(`HatWidthCm`) 여기서도
폭을 1 로 맞추고 아랫단을 0 에 둔다 — 같은 꼴이면 크기·색이 달라도 겹침(IoU)이 1 에 가깝다.
두 모자의 닮음 = 앞 IoU 와 옆 IoU 의 평균. 서로 셋 다 닮음 ≥ `SIMILAR`(시험 전용 값)인 삼각을
「같은 실루엣 셋 이상」(지시의 불합격 줄)으로 센다.

출력: 표준 출력 `SIL …` 줄 + `Saved/HatSilhouette/report.json` + 그림 `Saved/HatSilhouette/sheet.png`(앞·옆 그림자 격자).
"""
import bpy
import json
import math
import os
import re
import sys

import numpy as np

ROOT = "~/UNREAL/Snarehaven/"
HATS_DIR = ROOT + "_Source/raw/Snare/Items/Hats/"
OUT = ROOT + "Saved/HatSilhouette/"
N = 64                 # 그림자 칸 수(한 변)
SPAN = 1.6             # 폭 1 기준 그림 한 변 — 폭의 1.6 배 높이까지(마법사 모자·로켓)
SIMILAR = 0.85         # 시험 전용 — 이 이상이면 「같은 실루엣」(곰·고양이 귀 0.937 은 같다, 마법사 0.794·왕관 0.567 은 다르다 — 1361 첫 잼)
# 일·보상 모자 13 — 꾸미기 다양화 대상이 아니다(비교 상대로만 넣는다)
WORK = {"Hunter", "Fisher", "Farmer", "Lumberjack", "Strawberry", "Cook", "Looter", "Traveler", "Merchant", "Runner",
        "Mining", "Sprout", "Chief"}


def latest_glb(name):
    d = HATS_DIR + name + "/"
    best, bv = None, -1
    for f in os.listdir(d):
        m = re.fullmatch(r"Hat%s_v(\d+)\.glb" % re.escape(name), f)
        if m and int(m.group(1)) > bv:
            best, bv = d + f, int(m.group(1))
    return best


def load_tris(path):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)
    bpy.ops.import_scene.gltf(filepath=path)
    tris = []
    for o in bpy.context.scene.objects:
        if o.type != "MESH":
            continue
        me = o.data
        me.calc_loop_triangles()
        mw = o.matrix_world
        co = np.array([(mw @ v.co)[:] for v in me.vertices])
        idx = np.array([t.vertices[:] for t in me.loop_triangles])
        tris.append(co[idx])
    return np.concatenate(tris)            # (T, 3, 3)


def raster(tri2):
    """tri2 (T,3,2) 를 0..1 정규 좌표 N×N 칸에 칠한다(칸 가운데가 삼각형 안이면 1)."""
    mask = np.zeros((N, N), bool)
    g = (np.arange(N) + 0.5) / N
    for a, b, c in tri2:
        lo = np.floor(np.minimum(np.minimum(a, b), c) * N).astype(int).clip(0, N - 1)
        hi = np.ceil(np.maximum(np.maximum(a, b), c) * N).astype(int).clip(0, N)
        if hi[0] <= lo[0] or hi[1] <= lo[1]:
            continue
        xs, ys = np.meshgrid(g[lo[0]:hi[0]], g[lo[1]:hi[1]], indexing="ij")
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-12:
            continue
        l1 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
        l2 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
        inside = (l1 >= 0) & (l2 >= 0) & (l1 + l2 <= 1)
        mask[lo[0]:hi[0], lo[1]:hi[1]] |= inside
    return mask


def silhouettes(tris):
    pts = tris.reshape(-1, 3)
    w = pts[:, 0].max() - pts[:, 0].min()
    cx = (pts[:, 0].max() + pts[:, 0].min()) / 2
    cy = (pts[:, 1].max() + pts[:, 1].min()) / 2
    z0 = pts[:, 2].min()
    t = (tris - np.array([cx, cy, z0])) / w            # 폭 1 · 가운데 0 · 아랫단 0
    front = np.stack([t[..., 0] / SPAN + 0.5, t[..., 2] / SPAN], -1)
    side = np.stack([t[..., 1] / SPAN + 0.5, t[..., 2] / SPAN], -1)
    h = (pts[:, 2].max() - z0) / w
    d = (pts[:, 1].max() - pts[:, 1].min()) / w
    return raster(front), raster(side), h, d


def iou(a, b):
    u = np.logical_or(a, b).sum()
    return float(np.logical_and(a, b).sum() / u) if u else 1.0


def save_sheet(names, masks):
    cols = 10
    rows = math.ceil(len(names) / cols)
    cw = N * 2 + 6
    img = np.ones((rows * (N + 6), cols * cw, 4), np.float32)
    for k, n in enumerate(names):
        r, c = divmod(k, cols)
        f, s = masks[n]
        col = (0.15, 0.15, 0.2) if n not in WORK else (0.55, 0.35, 0.15)
        for j, m in enumerate((f, s)):
            x0 = c * cw + j * N
            y0 = (rows - 1 - r) * (N + 6)
            blk = img[y0:y0 + N, x0:x0 + N]
            on = m.T                                    # [z][x]
            for ch in range(3):
                blk[..., ch] = np.where(on, col[ch], 0.93 if j == 0 else 0.88)
    im = bpy.data.images.new("sheet", img.shape[1], img.shape[0], alpha=True)
    im.pixels = img.ravel()
    im.filepath_raw = OUT + "sheet.png"
    im.file_format = "PNG"
    im.save()


def main():
    os.makedirs(OUT, exist_ok=True)
    only = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else None
    names = sorted(d for d in os.listdir(HATS_DIR) if os.path.isdir(HATS_DIR + d) and latest_glb(d))
    if only:
        names = [n for n in names if n in only]
    masks, info = {}, {}
    for n in names:
        f, s, h, d = silhouettes(load_tris(latest_glb(n)))
        masks[n] = (f, s)
        info[n] = {"glb": os.path.basename(latest_glb(n)), "height_per_width": round(h, 3), "depth_per_width": round(d, 3)}
    sim = {}
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            sim[(a, b)] = (iou(masks[a][0], masks[b][0]) + iou(masks[a][1], masks[b][1])) / 2
    for n in names:
        best = max(((sim.get((n, o)) or sim.get((o, n)), o) for o in names if o != n), default=(0.0, ""))
        info[n]["nearest"], info[n]["nearest_sim"] = best[1], round(best[0], 3)
    # 「같은 실루엣 셋」 = 서로 셋 다 닮음 ≥ SIMILAR 인 삼각(사슬로 번지는 묶음은 쓰지 않는다 — A~B·B~C 만으로 A~C 는 닮지 않을 수 있다)
    def s2(a, b):
        return sim.get((a, b)) or sim.get((b, a))
    near = {n: {o for o in names if o != n and s2(n, o) >= SIMILAR} for n in names}
    triads = []
    for i, a in enumerate(names):
        for b in sorted(o for o in near[a] if o > a):
            for c in sorted(o for o in near[a] & near[b] if o > b):
                if any(x not in WORK for x in (a, b, c)):
                    triads.append((a, b, c))
    cosm = [n for n in names if n not in WORK]
    fail = sorted({x for t in triads for x in t if x not in WORK})
    for n in names:
        print("SIL {:<18} {} 높이/폭 {:.2f} 깊이/폭 {:.2f} 가장 닮은 {:<18} {:.3f} · 닮은 수 {}".format(
            n, "일" if n in WORK else "꾸", info[n]["height_per_width"], info[n]["depth_per_width"],
            info[n]["nearest"], info[n]["nearest_sim"], len(near[n])))
    mean_nn = sum(info[n]["nearest_sim"] for n in cosm) / max(1, len(cosm))
    print("SIL 요약: 모자 {} (꾸미기 {}) · 닮음 기준 {:.2f} · 같은 실루엣 셋(삼각) {} · 삼각에 든 꾸미기 {} · 꾸미기 가장 닮은 평균 {:.3f}".format(
        len(names), len(cosm), SIMILAR, len(triads), len(fail), mean_nn))
    with open(OUT + "report.json", "w", encoding="utf-8") as fp:
        json.dump({"similar": SIMILAR, "hats": info, "triads": triads, "cosmetic_in_triads": fail,
                   "cosmetic_mean_nearest": round(mean_nn, 3)}, fp, ensure_ascii=False, indent=1)
    save_sheet(names, masks)


main()
