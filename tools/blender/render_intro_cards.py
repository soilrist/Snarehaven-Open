"""첫 실행 그림 카드 3장 그림 렌더 (INBOX 정체성 ② · docs/STORY.md 1·6절). 기본 도형만, 640x360.

  1) 렌더: ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
         --python tools/blender/render_intro_cards.py
       → Saved/Generated/intro/IntroCard01~03.png (저장소 기준 — 이 파일 위치에서 찾는다)
       → Saved/Generated/intro/TitleBackdrop.png (타이틀 바탕 — 등대 섬 풍경, 가운데는 판이 가린다)
  2) 반입: tools/unreal/import_intro_cards.py → /Game/Game/UI/Intro/T_IntroCard_01~03 · T_TitleBackdrop
① 난파해 깨어남(부서진 배·해변·꺼진 등대) ② 올가미로 양 포획 ③ 목장 온기가 언덕 등대를 켬.
외부 에셋 없음, 모양은 우리 것(Reference 6절).
"""
import math
import os

import bpy

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")) + "/"
OUT_DIR = ROOT + "Saved/Generated/intro/"
W, H = 1280, 720

COL = {
    "sand": (0.98, 0.82, 0.5), "sea": (0.12, 0.5, 0.75), "grass": (0.35, 0.68, 0.22),
    "hill": (0.3, 0.6, 0.2), "wood": (0.55, 0.36, 0.18), "dark_wood": (0.35, 0.22, 0.11),
    "white": (0.95, 0.94, 0.9), "black": (0.12, 0.11, 0.11), "red": (0.8, 0.2, 0.16),
    "skin": (0.95, 0.75, 0.6), "navy": (0.16, 0.22, 0.38), "rope": (0.82, 0.66, 0.38),
    "gold": (1.0, 0.82, 0.25), "beam": (1.0, 0.93, 0.6), "gray": (0.55, 0.57, 0.6),
    "pink": (0.95, 0.62, 0.62), "rock": (0.55, 0.53, 0.5), "leaf": (0.18, 0.55, 0.2),
}
_mats = {}
SKY = (0.25, 0.6, 1.0)  # 밝은 하늘색


def put(obj, color):
    if color not in _mats:
        m = bpy.data.materials.new(color)
        m.diffuse_color = COL[color] + (1,)
        _mats[color] = m
    obj.data.materials.append(_mats[color])
    return obj


def cyl(loc, r, d, color, rot=(0, 0, 0), verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d, location=loc, rotation=rot)
    return put(bpy.context.object, color)


def cone(loc, r1, r2, d, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=r1, radius2=r2, depth=d, location=loc, rotation=rot)
    return put(bpy.context.object, color)


def box(loc, size, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(location=loc, rotation=rot)
    o = bpy.context.object
    o.scale = size
    return put(o, color)


def ball(loc, r, color, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc)
    o = bpy.context.object
    o.scale = scale
    bpy.ops.object.shade_smooth()
    return put(o, color)


def torus(loc, R, r, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, location=loc, rotation=rot)
    return put(bpy.context.object, color)


def ground(color, size=60):
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0, 0, 0))
    return put(bpy.context.object, color)


def sheep(x, y, yaw=0.0, s=1.0):
    c, sn = math.cos(yaw), math.sin(yaw)

    def at(dx, dy, dz):
        return (x + (dx * c - dy * sn) * s, y + (dx * sn + dy * c) * s, dz * s)

    ball(at(0, 0, 0.75), 0.55 * s, "white", (1.35, 1.0, 0.95))
    ball(at(0.75, 0, 0.95), 0.27 * s, "black", (1.2, 0.9, 1.0))
    for ex in (-0.12, 0.12):
        ball(at(0.98, ex, 1.02), 0.05 * s, "white")
    for dx in (-0.35, 0.35):
        for dy in (-0.25, 0.25):
            cyl(at(dx, dy, 0.25), 0.07 * s, 0.5 * s, "black")


def pig(x, y, s=1.0):
    ball((x, y, 0.6 * s), 0.5 * s, "pink", (1.3, 1.0, 0.9))
    ball((x + 0.65 * s, y, 0.7 * s), 0.3 * s, "pink")
    cyl((x + 0.93 * s, y, 0.68 * s), 0.12 * s, 0.1 * s, "red", (0, math.radians(90), 0))
    for dx in (-0.3, 0.3):
        for dy in (-0.22, 0.22):
            cyl((x + dx * s, y + dy * s, 0.2 * s), 0.08 * s, 0.4 * s, "pink")


def person(loc, yaw=0.0, lying=False):
    x, y, z = loc
    if lying:
        cyl((x, y, z + 0.25), 0.25, 1.1, "red", (0, math.radians(90), yaw))
        ball((x + 0.75 * math.cos(yaw), y + 0.75 * math.sin(yaw), z + 0.28), 0.25, "skin")
        cyl((x - 0.75 * math.cos(yaw), y - 0.75 * math.sin(yaw), z + 0.2), 0.16, 0.6, "navy",
            (0, math.radians(90), yaw))
        return
    cyl((x, y, z + 0.45), 0.13, 0.9, "navy")
    cyl((x, y, z + 1.2), 0.28, 0.7, "red")
    ball((x, y, z + 1.8), 0.26, "skin")
    cone((x, y, z + 2.05), 0.3, 0.05, 0.3, "dark_wood")


def tree(x, y, s=1.0):
    cyl((x, y, 0.6 * s), 0.15 * s, 1.2 * s, "dark_wood")
    ball((x, y, 1.6 * s), 0.8 * s, "leaf")


def lighthouse(x, y, z, lit):
    cyl((x, y, z + 1.4), 0.55, 2.8, "white")
    for h in (0.6, 1.8):
        cyl((x, y, z + h), 0.57, 0.4, "red")
    cyl((x, y, z + 2.9), 0.7, 0.18, "navy")
    cyl((x, y, z + 3.25), 0.4, 0.55, "gold" if lit else "gray")
    cone((x, y, z + 3.8), 0.6, 0.0, 0.55, "red")
    if lit:
        # 켜진 등불 — 큰 빛 공과 옆으로 뻗는 빛살 (안개를 밀어내는 등대).
        ball((x, y, z + 3.25), 0.75, "beam")
        for deg in (50, 75, 100, 125, -50, -75, -100, -125):
            a = math.radians(deg)
            cyl((x + 1.6 * math.sin(a), y - 0.6, z + 3.25 + 1.6 * math.cos(a)), 0.07, 1.1, "gold", (0, a, 0))


def card_wreck():
    ground("sand")
    bpy.ops.mesh.primitive_plane_add(size=120, location=(0, 67, 0.02))
    put(bpy.context.object, "sea")
    # 부서진 배 두 동강 + 흩어진 판자.
    box((-1.6, 1.8, 0.45), (1.3, 0.55, 0.4), "wood", (0, math.radians(12), math.radians(20)))
    box((0.9, 2.6, 0.35), (0.8, 0.5, 0.35), "dark_wood", (math.radians(-15), 0, math.radians(-30)))
    cyl((-1.2, 1.9, 1.3), 0.08, 2.0, "dark_wood", (math.radians(25), math.radians(20), 0))
    for px, py, yaw in ((1.8, 0.4, 30), (-2.8, 0.2, -50), (2.6, 1.6, 80), (-0.4, -0.8, 10)):
        box((px, py, 0.06), (0.6, 0.12, 0.05), "wood", (0, 0, math.radians(yaw)))
    person((0.1, 0.5, 0.0), yaw=math.radians(15), lying=True)
    for rx, ry, rs in ((3.6, 3.8, 0.6), (-4.0, 4.4, 0.8), (4.6, 1.0, 0.4)):
        ball((rx, ry, 0.1), rs, "rock", (1.2, 1.0, 0.6))
    # 멀리 꺼진 등대.
    ball((-5.5, 16, -2.0), 5.0, "hill", (1.4, 1.0, 0.7))
    lighthouse(-5.5, 16, 1.4, lit=False)
    return SKY


def card_snare():
    ground("grass")
    sheep(0.8, 1.2, yaw=math.radians(200), s=1.2)
    # 올가미 — 양 몸통에 감긴 고리와 사람 손까지 이어진 밧줄.
    torus((0.95, 1.2, 0.95), 0.62, 0.06, "rope", (0, math.radians(90), math.radians(20)))
    person((-2.4, -0.4, 0.0), yaw=0.0)
    p0, p1 = (-2.1, -0.3, 1.25), (0.5, 1.0, 0.95)
    mid = [(p0[i] + p1[i]) / 2 for i in range(3)]
    ln = math.dist(p0, p1)
    yaw = math.atan2(p1[1] - p0[1], p1[0] - p0[0])
    pitch = math.atan2(p1[2] - p0[2], math.hypot(p1[0] - p0[0], p1[1] - p0[1]))
    cyl(tuple(mid), 0.035, ln, "rope", (0, math.radians(90) - pitch, yaw))
    for tx, ty, ts in ((-4.5, 6, 1.4), (3.8, 7, 1.6), (6.0, 3.5, 1.1), (-6.5, 2.5, 1.2)):
        tree(tx, ty, ts)
    for gx, gy in ((2.6, -0.6), (-0.6, 2.9), (3.4, 2.2)):
        ball((gx, gy, 0.1), 0.25, "leaf", (1.2, 1.2, 0.6))
    return SKY


def card_ranch():
    ground("grass")
    # 울타리 고리.
    for i in range(14):
        a = i / 14 * math.tau
        x, y = 3.0 * math.cos(a), 0.6 + 2.0 * math.sin(a)
        cyl((x, y, 0.4), 0.07, 0.8, "dark_wood")
        a2 = (i + 1) / 14 * math.tau
        x2, y2 = 3.0 * math.cos(a2), 0.6 + 2.0 * math.sin(a2)
        ln = math.hypot(x2 - x, y2 - y)
        for h in (0.35, 0.65):
            box(((x + x2) / 2, (y + y2) / 2, h), (ln / 2, 0.035, 0.04), "wood",
                (0, 0, math.atan2(y2 - y, x2 - x)))
    sheep(-1.0, 0.8, yaw=math.radians(20), s=0.9)
    sheep(1.2, 1.3, yaw=math.radians(160), s=0.8)
    pig(0.3, -0.3, s=0.9)
    # 온기 — 목장에서 등대 쪽으로 떠오르는 불씨.
    for i in range(7):
        t = i / 6
        ball((0.2 - 2.6 * t, 1.0 + 9.0 * t, 1.6 + 3.2 * t), 0.16 - 0.06 * t, "gold")
    ball((-3.5, 14, -2.0), 5.0, "hill", (1.4, 1.0, 0.75))
    lighthouse(-3.5, 14, 1.6, lit=True)
    return SKY  # 다른 장과 같은 밝은 하늘색 (감독 지적 ④ — 노을빛은 크림색으로 바래 보였다)


def card_title():
    # 타이틀 바탕 — 바다 위 섬. 왼쪽 언덕에 켜진 등대, 오른쪽에 작은 목장. 가운데는 메뉴 판이 덮는다.
    ground("sea", 200)
    ball((0, 4, -1.0), 9.0, "sand", (2.2, 1.0, 0.16))
    ball((-7.5, 6, -2.0), 4.5, "hill", (1.3, 1.0, 0.75))
    lighthouse(-7.5, 6, 1.3, lit=True)
    for i in range(10):
        a = i / 10 * math.tau
        cyl((7.0 + 1.8 * math.cos(a), 3.0 + 1.2 * math.sin(a), 0.75), 0.07, 0.8, "dark_wood")
    sheep(6.6, 3.2, yaw=math.radians(200), s=0.7)
    pig(7.6, 2.6, s=0.6)
    for tx, ty, ts in ((-3.5, 9, 1.3), (3.5, 9.5, 1.5), (11, 6, 1.2), (-12, 3, 1.1)):
        tree(tx, ty, ts)
    return SKY, (0.0, -20.0, 6.5), 80


CARDS = (("IntroCard01", card_wreck), ("IntroCard02", card_snare), ("IntroCard03", card_ranch), ("TitleBackdrop", card_title))


def render(name, build):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    got = build()
    sky, cam_loc, cam_pitch = got if isinstance(got[0], tuple) else (got, (0.0, -9.5, 4.2), 79)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.studio_light = "outdoor.sl"  # 위에서 비추는 빛 — 땅이 어둡게 죽지 않는다
    sh.color_type = "MATERIAL"
    sh.show_cavity = True
    sh.show_shadows = not name.startswith("Title")  # 넓은 바다 평면에 그림자 띠가 생긴다
    sh.shadow_intensity = 0.35
    sh.background_type = "VIEWPORT"
    sh.background_color = sky
    sc.render.film_transparent = False
    # AgX/Filmic 은 색을 빼서 칙칙해진다 — Standard + 노출 조금 올림.
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.35
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.resolution_percentage = 100
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 32
    cam.data.clip_end = 200
    cam.location = cam_loc
    cam.rotation_euler = (math.radians(cam_pitch), 0, 0)
    os.makedirs(OUT_DIR, exist_ok=True)
    sc.render.filepath = OUT_DIR + name + ".png"
    bpy.ops.render.render(write_still=True)
    print("INTRORENDER", sc.render.filepath)


for n, b in CARDS:
    render(n, b)
