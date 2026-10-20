"""새싹 모자 아이콘 다시 그리기 (DIRECTOR_TODO 「새싹 모자 아이콘」 — 옛 아이콘은 뜨개 메시 워크벤치 렌더라 2D 그림 모자들과 붓질이 달랐다).

  1) 렌더 (Blender 헤드리스 — 기본 도형 + 셀 셰이딩 + 안쪽 선, 투명 배경 512x512, 앞 위 3/4):
     ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_sprout_hat_icon.py
       → Saved/Generated/hat_icon/SproutPaint.png
  2) 외곽선 — 모자 아이콘과 같은 후처리:
     python3 tools/blender/render_hat_icon.py outline SproutPaint sprout_hat
       → _Source/raw/Snare/Items/Icons/Icon_sprout_hat.png  (반입은 `tools/unreal/import_item_icons.py`)

그림 모자(farmer_hat·strawberry_hat)처럼 색 세 칸(그늘·바탕·빛) + 짙은 선으로 칠한다.
모양은 우리 것: 통통한 초록 비니 + 도톰한 접단 + 머리 위로 크게 벌어진 떡잎 두 장.
색은 눈대로 고른 표시 색(sRGB)이다 — 외곽선 단계가 밝기 ×1.3 을 하므로 여기서 미리 나눠 둔다.
"""
import math
import os

import bmesh
import bpy
from mathutils import Vector

OUT_DIR = "~/UNREAL/Snarehaven/Saved/Generated/hat_icon/"
SIZE = 512
BRIGHTEN = 1.3               # render_hat_icon.py outline 의 BRIGHTEN 과 같다
LINE_RGB = (58, 34, 40)      # 그림 아이콘 테두리 색(render_hat_icon.OUTLINE_RGB)

DOME = (84, 160, 60)        # 비니 몸통 — 연두 초록
CUFF = (50, 110, 50)         # 접단 — 한 톤 짙게
LEAF = (132, 204, 62)        # 떡잎 — 밝은 연두
STEM = (70, 138, 50)


def lin(c):
    """표시 색(0~255 sRGB) → 노드에 넣을 선형 값 (밝기 ×1.3 을 미리 나눈다)."""
    out = []
    for v in c:
        s = min(1.0, v / 255.0 / BRIGHTEN)
        out.append(s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4)
    return tuple(out) + (1.0,)


def toon(rgb):
    """그늘·바탕·빛 세 칸 셀 셰이딩 (빛 받는 양 → 계단 색 → 발광으로 그대로 찍는다)."""
    mat = bpy.data.materials.new("toon")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    diff = nt.nodes.new("ShaderNodeBsdfDiffuse")
    to_rgb = nt.nodes.new("ShaderNodeShaderToRGB")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    emit = nt.nodes.new("ShaderNodeEmission")
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    r, g, b = rgb
    shade = (r * 0.62, g * 0.64, b * 0.70)
    light = (r + (255 - r) * 0.28, g + (255 - g) * 0.28, b + (255 - b) * 0.22)
    cr = ramp.color_ramp
    cr.interpolation = "CONSTANT"
    cr.elements[0].position = 0.0
    cr.elements[0].color = lin(shade)
    cr.elements[1].position = 0.18
    cr.elements[1].color = lin(rgb)
    hi = cr.elements.new(0.62)
    hi.color = lin(light)
    nt.links.new(diff.outputs[0], to_rgb.inputs[0])
    nt.links.new(to_rgb.outputs[0], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], emit.inputs[0])
    nt.links.new(emit.outputs[0], out.inputs[0])
    return mat


def finish(o, rgb):
    bpy.ops.object.shade_smooth()
    o.data.materials.append(toon(rgb))
    return o


def blob(loc, scale, rgb, rot=(0, 0, 0), seg=48):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=seg // 2, location=loc, rotation=rot)
    o = bpy.context.object
    o.scale = scale
    return finish(o, rgb)


def stick(a, b, rgb, r):
    a, b = Vector(a), Vector(b)
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=(b - a).length, location=(a + b) / 2, vertices=24)
    o = bpy.context.object
    o.rotation_euler = (b - a).to_track_quat("Z", "Y").to_euler()
    return finish(o, rgb)


def leaf(side):
    """떡잎 한 장 — 납작한 타원을 바깥·위로 젖히고 앞(카메라)으로 살짝 숙인다. side = +1 오른쪽, −1 왼쪽."""
    rot = (math.radians(30), math.radians(-32 * side), math.radians(8 * side))
    loc = (side * 0.50, -0.06, 1.50)
    blob(loc, (0.58, 0.42, 0.08), LEAF, rot=rot, seg=48)
    blob(loc, (0.44, 0.028, 0.09), STEM, rot=rot, seg=24)    # 잎맥


def build():
    # 접단: 모서리 둥근 짧은 원통 (뜨개 비니의 접어 올린 단)
    bpy.ops.mesh.primitive_cylinder_add(radius=1.04, depth=0.40, location=(0, 0, 0.08), vertices=96)
    cuff = bpy.context.object
    bev = cuff.modifiers.new("bevel", "BEVEL")
    bev.width = 0.12
    bev.segments = 6
    finish(cuff, CUFF)
    # 몸통: 위가 둥근 반구 (아래 반은 잘라 낸다 — 접단 안에 앉는다)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, location=(0, 0, 0.2))
    dome = bpy.context.object
    bm = bmesh.new()
    bm.from_mesh(dome.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < -0.02], context="VERTS")
    bm.to_mesh(dome.data)
    bm.free()
    dome.scale = (0.98, 0.98, 0.84)
    finish(dome, DOME)
    # 줄기 + 떡잎 두 장 (모자 몸통 높이만큼 크게) + 갈래 눈(줄기 끝을 덮는다)
    stick((0, 0, 0.95), (0.02, -0.03, 1.46), STEM, 0.085)
    blob((0.02, -0.04, 1.47), (0.12, 0.12, 0.11), STEM, seg=24)
    leaf(+1)
    leaf(-1)


def main():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    build()
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    center = (lo + hi) / 2
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.view_settings.view_transform = "Standard"
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    # 안쪽 선(떡잎·접단 경계) — 그림 아이콘의 선 맛
    sc.render.use_freestyle = True
    sc.render.line_thickness_mode = "ABSOLUTE"
    sc.render.line_thickness = 3.0
    ls = sc.view_layers[0].freestyle_settings.linesets[0]
    ls.linestyle.color = lin(LINE_RGB)[:3]
    # 빛: 앞 왼쪽 위 해 하나
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sc.collection.objects.link(sun)
    sun.rotation_euler = Vector((-0.55, -0.6, 0.75)).to_track_quat("Z", "Y").to_euler()
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(hi - lo) * 1.3
    # 모자 아이콘(render_hat_icon.py)과 같은 각도: 앞에서 22° 위, 20° 오른쪽
    e, yaw = math.radians(22), math.radians(20)
    d = Vector((math.sin(yaw) * math.cos(e), -math.cos(yaw) * math.cos(e), math.sin(e)))
    cam.location = center + d * 6
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + "SproutPaint.png"
    bpy.ops.render.render(write_still=True)
    print("ICONRENDER", sc.render.filepath)


main()
