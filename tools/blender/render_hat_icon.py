"""모자 메시 → 아이템 아이콘 (M21-05c 새싹 모자 — 바르코 아이콘이 없는 모자용).

  1) 렌더 (Blender 헤드리스 — 투명 배경 512x512, 앞 위 3/4):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_hat_icon.py -- Sprout sprout_hat
       → Saved/Generated/hat_icon/<Name>.png
  2) 외곽선 (시스템 python3 + PIL — 다른 아이콘처럼 짙은 테두리, 긴 변 80%):
     python3 tools/blender/render_hat_icon.py outline Sprout sprout_hat
       → _Source/raw/Snare/Items/Icons/Icon_<id>.png  (반입은 `tools/unreal/import_item_icons.py`)

원본 메시 = `_Source/raw/Snare/Items/Hats/<Name>/Hat<Name>_v2.glb`(`hats_v2_make.py`).
스킨 조각 g 얼굴 장식도 같은 길 — 모자 원본이 없으면 `_Source/raw/Snare/Items/FaceAcc/<Name>/Face<Name>_v1.glb`(`face_make.py`)를 읽는다.
스킨 조각 h 상의도 — 둘 다 없으면 `_Source/raw/Snare/Items/Tops/<Name>/<Name>_rig.glb`(`tops_make.py`, 뼈대째 T자 그대로)를 읽는다.
스킨 조각 i 하의도 — 셋 다 없으면 `_Source/raw/Snare/Items/Bottoms/<Name>/<Name>_rig.glb`(`bottoms_make.py`)를 읽는다.
스킨 조각 j 신발도 — 넷 다 없으면 `_Source/raw/Snare/Items/Shoes/<Name>/<Name>_rig.glb`(`shoes_make.py`)를 읽는다.
스킨 조각 k 장갑도 — 다섯 다 없으면 `_Source/raw/Snare/Items/Gloves/<Name>/<Name>_rig.glb`(`gloves_make.py`)를 읽는다.
스킨 조각 l 배낭도 — 여섯 다 없으면 `_Source/raw/Snare/Items/Backpacks/<Name>/Backpack<Name>_v1.glb`(`backpack_make.py`)를 읽는다.
스킨 조각 n 목장 동물 리본·목걸이도 — 일곱 다 없으면 `_Source/raw/Snare/Items/RanchCosmetics/<Name>/<Name>_v1.glb`(`ranch_cosmetic_make.py`)를 읽는다.
M31-02 작업복(플란넬 셔츠·작업 바지)도 — 여덟 다 없으면 `_Source/raw/Snare/Items/Outfits/<Name>/<Name>_rig.glb` 를 읽는다.
다른 모자 아이콘(바르코 2D 그림)과 붓질은 다르다 — 색면·짙은 테두리·투명 배경·크기 규칙만 맞춘다.
"""
import math
import os
import sys

ROOT = "~/UNREAL/Snarehaven/"
HATS_DIR = ROOT + "_Source/raw/Snare/Items/Hats/"
FACES_DIR = ROOT + "_Source/raw/Snare/Items/FaceAcc/"
TOPS_DIR = ROOT + "_Source/raw/Snare/Items/Tops/"
BOTTOMS_DIR = ROOT + "_Source/raw/Snare/Items/Bottoms/"
SHOES_DIR = ROOT + "_Source/raw/Snare/Items/Shoes/"
GLOVES_DIR = ROOT + "_Source/raw/Snare/Items/Gloves/"
BACKPACKS_DIR = ROOT + "_Source/raw/Snare/Items/Backpacks/"
RANCH_DIR = ROOT + "_Source/raw/Snare/Items/RanchCosmetics/"
OUTFITS_DIR = ROOT + "_Source/raw/Snare/Items/Outfits/"
ICONS_DIR = ROOT + "_Source/raw/Snare/Items/Icons/"
OUT_DIR = ROOT + "Saved/Generated/hat_icon/"
SIZE = 512
FILL = 0.8                 # 긴 변 80% — 바르코 아이콘 후처리 규칙(Icon_*.txt 「긴 변 80%」)
OUTLINE_PX = 14            # 테두리 두께(512 기준) — Icon_strawberry_hat 테두리를 재어 맞춤
OUTLINE_RGB = (58, 34, 40)  # 같은 아이콘 테두리 색(짙은 자주 갈색)
BRIGHTEN = 1.3             # 워크벤치 스튜디오 빛이 2D 아이콘보다 어두워 색면을 한 단계 밝힌다(눈대중)

if __name__ == "__main__" and len(sys.argv) >= 4 and sys.argv[1] == "outline":
    from PIL import Image, ImageEnhance, ImageFilter
    name, item = sys.argv[2], sys.argv[3]
    im = Image.open(OUT_DIR + name + ".png").convert("RGBA")
    a = im.getchannel("A")
    im = ImageEnhance.Brightness(im.convert("RGB")).enhance(BRIGHTEN).convert("RGBA")
    im.putalpha(a)
    im = im.crop(im.getchannel("A").getbbox())
    inner = int(SIZE * FILL) - 2 * OUTLINE_PX
    s = inner / max(im.size)
    im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.alpha_composite(im, ((SIZE - im.width) // 2, (SIZE - im.height) // 2))
    alpha = canvas.getchannel("A").point(lambda a: 255 if a > 40 else 0)
    ring = alpha.filter(ImageFilter.MaxFilter(OUTLINE_PX * 2 + 1)).filter(ImageFilter.GaussianBlur(1.2))
    out = Image.new("RGBA", (SIZE, SIZE), OUTLINE_RGB + (0,))
    out.putalpha(ring)
    out.alpha_composite(canvas)
    path = ICONS_DIR + "Icon_{}.png".format(item)
    out.save(path)
    print("저장:", path, out.size)
    sys.exit(0)

import bpy
from mathutils import Vector


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    name = args[0]
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    source = HATS_DIR + "{0}/Hat{0}_v2.glb".format(name)
    if not os.path.exists(source):
        source = FACES_DIR + "{0}/Face{0}_v1.glb".format(name)
    if not os.path.exists(source):
        source = TOPS_DIR + "{0}/{0}_rig.glb".format(name)
    if not os.path.exists(source):
        source = BOTTOMS_DIR + "{0}/{0}_rig.glb".format(name)
    if not os.path.exists(source):
        source = SHOES_DIR + "{0}/{0}_rig.glb".format(name)
    if not os.path.exists(source):
        source = GLOVES_DIR + "{0}/{0}_rig.glb".format(name)
    if not os.path.exists(source):
        source = BACKPACKS_DIR + "{0}/Backpack{0}_v1.glb".format(name)
    if not os.path.exists(source):
        source = RANCH_DIR + "{0}/{0}_v1.glb".format(name)
    if not os.path.exists(source):
        source = OUTFITS_DIR + "{0}/{0}_rig.glb".format(name)
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    if source.startswith(GLOVES_DIR):    # 장갑은 양손이 팔 길이만큼 떨어져 있다 — 한 손만 화면에 잡는다
        # (glTF 가져오기가 만든 뼈 모양 `Icosphere` ±1m 도 범위에서 뺀다)
        pts = [o.matrix_world @ v.co for o in meshes if o.name.startswith("Gloves") for v in o.data.vertices]
        pts = [p for p in pts if p.x > 0]
    if source.startswith(OUTFITS_DIR):   # 작업복 뼈대 glb 도 뼈 모양 `Icosphere` 가 범위를 키워 옷이 작게 찍힌다 — 옷 메시만
        pts = [o.matrix_world @ v.co for o in meshes if not o.name.startswith("Icosphere") for v in o.data.vertices]
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    center = (lo + hi) / 2
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "TEXTURE"
    sh.show_cavity = True
    sh.show_shadows = False
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(hi - lo) * 1.3
    # glTF 가져오기는 +Y 위(Blender Z 위)로 돌려 놓는다 — 모자 앞 = −Y. 앞에서 조금 위·오른쪽에서 본다.
    e, yaw = math.radians(22), math.radians(20)
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    cam.location = center + d * 4
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


main()
