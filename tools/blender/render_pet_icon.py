"""동물 메시 → 펫 아이템 아이콘 (INBOX 보정 10-06 02:05 — `item.pet_<종>`·`item.eel` 빈 칸).

  1) 렌더 (Blender 헤드리스 — 투명 배경 512x512, 옆 3/4 조금 위):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_pet_icon.py -- Fox
       → Saved/Generated/hat_icon/PetFox.png   (장어는 `-- Eel` → Eel.png)
  2) 외곽선 — 모자 아이콘과 같은 후처리를 그대로 쓴다:
     python3 tools/blender/render_hat_icon.py outline PetFox pet_fox
       → _Source/raw/Snare/Items/Icons/Icon_pet_fox.png  (반입은 `tools/unreal/import_item_icons.py`)
  변종 펫(`item.pet_<종>_variant`, M30-03) — 1) 의 렌더에 게임과 같은 금빛 겹을 섞는다(시스템 python3 + PIL):
     python3 tools/blender/render_pet_icon.py variant Fox
       → Saved/Generated/hat_icon/PetFoxVariant.png
     python3 tools/blender/render_hat_icon.py outline PetFoxVariant pet_fox_variant

펫 원본 메시 = `_Source/raw/Snare/Characters/Animals/<종>/<종>_Mesh.glb`(게임 `SK_Animal_<종>` 의 원본).
게임 몸 색은 `T_Animal_<종>_D.png`(색 고친 판)라 glb 안 그림 대신 그 텍스처를 입힌다(같은 UV).
장어는 물고기 3D 메시가 없다(물고기 아이콘은 전부 바르코 2D) — 여기서 몸통 관·등지느러미를 절차로 만든다.
"""
import math
import os
import sys

ROOT = "~/UNREAL/Snarehaven/"
ANIMALS_DIR = ROOT + "_Source/raw/Snare/Characters/Animals/"
# 가축(M31-01)은 Quaternius 팩 FBX 가 게임 `SK_Animal_<종>` 원본이다(`tools/unreal/import_animal_livestock.py`·
# `import_animal_pig.py`). 게임은 FBX 재질 색을 그대로 쓰니(`MI_Animal_*`, 덮어쓰기 없음) 렌더도 그 색으로 칠한다.
LIVESTOCK_DIR = ROOT + "_Source/raw/Quaternius_FarmAnimals/FBX/"
LIVESTOCK = ("Pig", "Cow", "Sheep")
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
# 변종 겉모습 — DESIGN 10.6 「변종 겉모습」 줄 그대로(게임은 `M_HitFlash` 겹, 금빛 · 불투명도 0.6)
VARIANT_GOLD = (1.0, 0.78, 0.2)
VARIANT_OPACITY = 0.6

if __name__ == "__main__" and len(sys.argv) >= 3 and sys.argv[1] == "variant":
    from PIL import Image
    name = "Pet" + sys.argv[2]
    im = Image.open(OUT_DIR + name + ".png").convert("RGBA")
    # 게임 겹은 빛을 받아 명암이 남는다 — 평평한 금빛을 덮으면 눈·무늬가 지워져(첫 판 `loop/tmp/494`) 금빛에
    # 그 자리 밝기를 곱한 뒤 몸 색 ×0.4 + 금빛 ×0.6 으로 섞는다. 0.25 = 눈동자도 검정 대신 짙은 금빛(눈대중).
    rgb = im.convert("RGB")
    lum = rgb.convert("L")
    gold = Image.merge("RGB", [lum.point(lambda v, c=c: min(255, round(c * 255 * (0.25 + v / 255))))
                               for c in VARIANT_GOLD])
    out = Image.blend(rgb, gold, VARIANT_OPACITY).convert("RGBA")
    out.putalpha(im.getchannel("A"))
    out.save(OUT_DIR + name + "Variant.png")
    print("VARIANT", OUT_DIR + name + "Variant.png")
    sys.exit(0)

import bpy
from mathutils import Vector

SIZE = 512
# 장어 색 — 레퍼런스 장어 그림(어두운 올리브 등·누런 배)과 다른 물고기 아이콘 붓질에 맞춘 눈대중
EEL_BACK = (0.22, 0.28, 0.13, 1.0)
EEL_BELLY = (0.78, 0.66, 0.30, 1.0)


def load_animal(species):
    folder = ANIMALS_DIR + species + "/"
    bpy.ops.import_scene.gltf(filepath=folder + species + "_Mesh.glb")
    tex = folder + "T_Animal_{}_D.png".format(species)
    if os.path.exists(tex):
        img = bpy.data.images.load(tex)
        for mat in bpy.data.materials:
            if mat.use_nodes:
                for node in mat.node_tree.nodes:
                    if node.type == "TEX_IMAGE":
                        node.image = img
    return "Pet" + species


def load_livestock(species):
    bpy.ops.import_scene.fbx(filepath=LIVESTOCK_DIR + species + ".fbx")
    # 작업대 렌더는 노드가 아니라 재질 표시 색을 읽는다 — FBX 바탕색을 표시 색으로 옮긴다.
    for mat in bpy.data.materials:
        bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.use_nodes else None
        if bsdf:
            mat.diffuse_color = tuple(bsdf.inputs["Base Color"].default_value)
    return "Pet" + species


def make_eel():
    """S 자로 굽은 관 — 머리 굵고 꼬리 가늘게, 등은 짙게·배는 누렇게(정점 색), 등지느러미 띠."""
    n_seg, n_ring, length = 48, 16, 1.0
    verts, faces, cols = [], [], []
    for i in range(n_seg + 1):
        t = i / n_seg
        x = (t - 0.5) * length
        y = _eel_y(t)                                 # 옆으로 S 자
        r = _eel_r(t) * min(1.0, 0.45 + t * 10)      # 머리 끝은 둥글게 모임
        for j in range(n_ring):
            a = 2 * math.pi * j / n_ring
            verts.append((x, y + r * math.cos(a) * 0.85, r * math.sin(a)))
            cols.append(EEL_BACK if math.sin(a) > 0.3 else EEL_BELLY)
    for i in range(n_seg):
        for j in range(n_ring):
            a, b = i * n_ring + j, i * n_ring + (j + 1) % n_ring
            faces.append((a, b, b + n_ring, a + n_ring))
    # 등·배 지느러미 — 몸 3할 뒤부터 꼬리 끝까지 얇은 띠
    base = len(verts)
    fin = []
    for i in range(int(n_seg * 0.3), n_seg + 1):
        t = i / n_seg
        x = (t - 0.5) * length
        y = _eel_y(t)
        r = _eel_r(t)
        h = 0.04 * (1.0 - 0.3 * t)
        fin.append([(x, y, r * 0.9), (x, y, r + h), (x, y, -r * 0.9), (x, y, -r - h * 0.7)])
    for k, ring in enumerate(fin):
        verts.extend(ring)
        cols.extend([EEL_BACK] * 4)
        if k:
            p, q = base + (k - 1) * 4, base + k * 4
            faces.append((p, q, q + 1, p + 1))
            faces.append((p + 2, p + 3, q + 3, q + 2))
    mesh = bpy.data.meshes.new("Eel")
    mesh.from_pydata(verts, [], faces)
    layer = mesh.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
    for k, c in enumerate(cols):
        layer.data[k].color = c
    for p in mesh.polygons:
        p.use_smooth = True
    obj = bpy.data.objects.new("Eel", mesh)
    bpy.context.scene.collection.objects.link(obj)
    # 눈 — 머리 양옆 흰자 + 검은 눈동자(큰 눈 귀엽게, 레퍼런스 동물 그림체)
    t = 0.04
    x, y, r = (t - 0.5) * length, _eel_y(t), _eel_r(t) * min(1.0, 0.45 + t * 10)
    for side in (1, -1):
        for rad, out, col in ((0.022, 0.80, (0.95, 0.95, 0.92, 1.0)), (0.013, 0.98, (0.03, 0.03, 0.03, 1.0))):
            bpy.ops.mesh.primitive_uv_sphere_add(radius=rad, location=(x, y + side * r * out * 0.85, r * 0.35))
            ec = bpy.context.active_object.data.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
            for d in ec.data:
                d.color = col
    # 머리가 왼쪽 위·꼬리가 오른쪽 아래로 비스듬히 — 다른 물고기 아이콘 자세(Icon_arctic_char.txt 「Diagonal pose」)
    for o in bpy.data.objects:
        if o.type == "MESH":
            o.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.join()
    bpy.context.active_object.rotation_euler = (0.0, math.radians(28), 0.0)
    return "Eel"


def _eel_y(t):
    return 0.12 * math.sin(t * math.pi * 2.0)


def _eel_r(t):
    return 0.075 * (1.0 - 0.5 * t) * min(1.0, (1.0 - t) * 5.0 + 0.04)   # 꼬리 끝은 뾰족하게


def main():
    species = sys.argv[sys.argv.index("--") + 1]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    if species == "Eel":
        name = make_eel()
    elif species in LIVESTOCK:
        name = load_livestock(species)
    else:
        name = load_animal(species)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    center = (lo + hi) / 2
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "VERTEX" if species == "Eel" else "MATERIAL" if species in LIVESTOCK else "TEXTURE"
    sh.show_cavity = True
    sh.show_shadows = False
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(hi - lo) * 1.3
    # 동물 glb 는 앞 = −Y(모자와 같음). 펫은 옆모습이 종을 가장 잘 읽히니 왼쪽 앞 3/4·조금 위에서 본다.
    # 장어는 앞에서 조금 위로 — S 자 몸과 옆구리 누런 배가 같이 읽힌다.
    e, yaw = (math.radians(35), math.radians(0)) if species == "Eel" else (math.radians(15), math.radians(-55))
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    cam.location = center + d * 4
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


main()
