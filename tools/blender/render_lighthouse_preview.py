"""등대 탑 메시 obj → 정면·옆·위 3/4 미리보기 (스토리 ⑤ 등대 점등 조각 2, Blender 헤드리스).

  python3 tools/unreal/make_lighthouse_mesh.py
  ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/blender/render_lighthouse_preview.py
    → Saved/Generated/lighthouse_preview/front.png · side.png · top34.png

게임 카메라 캡처가 아니다(Reference.md §4) — 메시 모양 점검용. 3층 대조는 조각 3 에서 월드에 세운 뒤 오프라인 캡처로 한다.
슬롯 색은 `tools/unreal/make_lighthouse_mesh.py` 의 SLOT_COLORS 를 읽는다. 사람 키(176cm) 막대를 옆에 세워 크기를 가늠하게 한다.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

ROOT = "~/UNREAL/Snarehaven/"
sys.path.insert(0, ROOT + "tools/unreal")
from make_lighthouse_mesh import SLOT_COLORS  # noqa: E402

OUT_DIR = ROOT + "Saved/Generated/lighthouse_preview/"
os.makedirs(OUT_DIR, exist_ok=True)


def _material(name, rgb, emit=False):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    bsdf.inputs["Roughness"].default_value = 0.85
    return mat


def main():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    bpy.ops.wm.obj_import(filepath=ROOT + "Saved/Generated/Lighthouse_01.obj", forward_axis='Y', up_axis='Z')
    tower = bpy.context.selected_objects[0]
    mats = {slot: _material(slot, rgb) for slot, rgb in SLOT_COLORS.items()}
    for i, slot in enumerate(tower.material_slots):
        key = slot.material.name.split(".")[0] if slot.material else None
        if key in mats:
            slot.material = mats[key]
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 600, 900
    sc.view_settings.view_transform = 'Standard'
    world = bpy.data.worlds.new("W")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.64, 0.78, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.9
    sc.world = world
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", 'SUN'))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(45), 0, math.radians(-35))
    sc.collection.objects.link(sun)
    # 눈밭 바닥 + 176cm 사람 막대
    bpy.ops.mesh.primitive_plane_add(size=6000, location=(0, 0, -0.5))
    bpy.context.object.data.materials.append(_material("Snow", (0.78, 0.76, 0.86)))
    bpy.ops.mesh.primitive_cylinder_add(radius=22, depth=176, location=(-520, -120, 88))
    bpy.context.object.data.materials.append(_material("Person", (0.9, 0.2, 0.1)))
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    cam.data.type = 'ORTHO'
    cam.data.clip_end = 100000
    sc.collection.objects.link(cam)
    sc.camera = cam
    for name, az, el, scale, center_z in (("front", -90, 3, 1500, 640), ("side", 0, 3, 1500, 640), ("top34", -60, 58, 1500, 560)):
        a, e = math.radians(az), math.radians(el)
        fwd = -Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)))
        cam.rotation_euler = fwd.to_track_quat('-Z', 'Y').to_euler()
        cam.location = Vector((0, 0, center_z)) - fwd * 8000
        cam.data.ortho_scale = scale
        sc.render.filepath = OUT_DIR + name + ".png"
        bpy.ops.render.render(write_still=True)
    print("렌더 끝:", OUT_DIR)


main()
