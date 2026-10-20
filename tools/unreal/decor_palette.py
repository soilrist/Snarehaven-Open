"""M108-03b 장식 메시를 재질 1개(섹션 1)로 — 슬롯 색을 팔레트 그림 한 장의 칸으로 옮긴다 (DESIGN 8.11 「가구 한도 드로콜 재계산」).

`import_wall_decor_meshes.py`·`import_top_decor_meshes.py` 가 부른다(언리얼 안 python). 흐름:
 1. 슬롯 색표(리니어) → 4px 칸 8×8 팔레트 PNG(sRGB) → `T_Prop_<몸통>Palette`(최근접 거르기·밉 없음·무압축 — 칸 색이 그대로 나온다)
 2. `M_Nature_Matte` 복사본 `M_Nature_MattePalette` — `DiffuseColor` 자리에 `PaletteMap` 그림(베이스컬러·발광 바닥 둘 다). 빛·거칠기는 그대로
 3. `MI_Prop_<몸통>_Palette` — 부모 위 재질, `PaletteMap` = 1 의 그림
 4. OBJ 원본의 `usemtl <슬롯>` 면마다 UV 를 그 슬롯 칸 가운데 하나로 바꾸고 `usemtl Palette` 하나로 합친 사본을 `Saved/Generated/Palette/` 에 쓴다
섹션이 슬롯 수(4~8)만큼이던 것이 1 이 된다 — 놓인 장식 하나 = 드로콜 1. 슬롯 이름이 색표에 없으면 멈춘다(색 빠짐을 조용히 넘기지 않는다).
"""
import os
import struct
import zlib

import unreal

NATURE_DIR = "/Game/Game/Environment/Nature"
MATTE_PATH = NATURE_DIR + "/M_Nature_Matte"
PALETTE_MATERIAL_PATH = NATURE_DIR + "/M_Nature_MattePalette"
PALETTE_PARAM = "PaletteMap"
GRID, CELL = 8, 4                      # 칸 8×8 = 64색까지, 칸 4px → 32×32 그림
GENERATED_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated"))
PALETTE_OBJ_DIR = os.path.join(GENERATED_DIR, "Palette")

MEL = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def _srgb_byte(linear):
    c = min(max(linear, 0.0), 1.0)
    s = c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055
    return int(round(s * 255.0))


def _write_png(path, size, pixel):
    rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(size)) for y in range(size))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def _cell_uv(index):
    """칸 index 가운데의 UV. 반입기가 OBJ 의 v 를 뒤집으므로(v' = 1 - v) PNG 위에서 index//GRID 번째 줄이 나오게 1 - v 로 적는다."""
    cx, cy = index % GRID, index // GRID
    return (cx + 0.5) / GRID, 1.0 - (cy + 0.5) / GRID


def _palette_texture(dest_dir, body, slots):
    if len(slots) > GRID * GRID:
        raise RuntimeError("팔레트 칸 모자람: {} > {}".format(len(slots), GRID * GRID))
    colors = [tuple(_srgb_byte(v) for v in rgb) + (255,) for rgb in slots.values()]
    def pixel(x, y):
        i = (y // CELL) * GRID + x // CELL
        return colors[i] if i < len(colors) else (255, 0, 255, 255)
    os.makedirs(PALETTE_OBJ_DIR, exist_ok=True)
    name = "T_Prop_{}Palette".format(body)
    png = os.path.join(PALETTE_OBJ_DIR, name + ".png")
    _write_png(png, GRID * CELL, pixel)
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = dest_dir
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = unreal.TextureFactory()
    tools.import_asset_tasks([task])
    path = dest_dir + "/" + name
    tex = assets.load_asset(path)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)   # 무압축 — ASTC 가 칸 색을 섞지 않게
    tex.set_editor_property("never_stream", True)
    assets.save_asset(path, only_if_is_dirty=False)
    return tex


def _palette_material():
    if assets.does_asset_exist(PALETTE_MATERIAL_PATH):
        return assets.load_asset(PALETTE_MATERIAL_PATH)
    if assets.duplicate_asset(MATTE_PATH, PALETTE_MATERIAL_PATH) is None:
        raise RuntimeError("복사 실패: " + MATTE_PATH)
    mat = assets.load_asset(PALETTE_MATERIAL_PATH)
    color = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)   # DiffuseColor 벡터
    glow = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)  # DiffuseColor × AmbientFloor
    if not isinstance(color, unreal.MaterialExpressionVectorParameter) or not isinstance(glow, unreal.MaterialExpressionMultiply):
        raise RuntimeError("M_Nature_Matte 배선이 예상과 다르다: {} / {}".format(color, glow))
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    tex.set_editor_property("parameter_name", PALETTE_PARAM)
    tex.set_editor_property("texture", assets.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if not MEL.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("PaletteMap -> 베이스컬러")
    if not MEL.connect_material_expressions(tex, "RGB", glow, "A"):
        raise RuntimeError("PaletteMap -> 발광 곱 A")
    MEL.delete_material_expression(mat, color)
    MEL.recompile_material(mat)
    assets.save_asset(PALETTE_MATERIAL_PATH, only_if_is_dirty=False)
    return mat


def palette_material(dest_dir, body, slots):
    """슬롯 색표 → 팔레트 그림 + `MI_Prop_<body>_Palette`. 돌려주는 MI 를 메시 0번 슬롯에 넣는다."""
    tex = _palette_texture(dest_dir, body, slots)
    parent = _palette_material()
    name = "MI_Prop_{}_Palette".format(body)
    path = dest_dir + "/" + name
    mi = assets.load_asset(path) if assets.does_asset_exist(path) else \
        tools.create_asset(name, dest_dir, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    MEL.set_material_instance_texture_parameter_value(mi, PALETTE_PARAM, tex)
    assets.save_asset(path, only_if_is_dirty=False)
    return mi


def palette_obj(source_path, slots):
    """슬롯별 OBJ → 팔레트 칸 UV + `usemtl Palette` 하나인 사본. 사본 경로를 돌려준다(파일 이름 = 원본 — 메시 이름이 같게)."""
    order = list(slots)
    out, slot = [], None
    with open(source_path) as f:
        lines = f.read().splitlines()
    out.append("mtllib Palette.mtl")
    for i in range(len(order)):
        out.append("vt {:.5f} {:.5f}".format(*_cell_uv(i)))
    used = set()
    for line in lines:
        if line.startswith(("mtllib ", "vt ")):
            continue
        if line.startswith("usemtl "):
            slot = line.split(None, 1)[1].strip()
            if slot not in slots:
                raise RuntimeError("색표에 없는 슬롯: {} {}".format(os.path.basename(source_path), slot))
            if not used:
                out.append("usemtl Palette")
            used.add(slot)
            continue
        if line.startswith("f "):
            vt = order.index(slot) + 1
            corners = []
            for c in line.split()[1:]:
                p = c.split("/")
                corners.append("{}/{}/{}".format(p[0], vt, p[2]) if len(p) == 3 else "{}/{}".format(p[0], vt))
            out.append("f " + " ".join(corners))
            continue
        out.append(line)
    os.makedirs(PALETTE_OBJ_DIR, exist_ok=True)
    with open(os.path.join(PALETTE_OBJ_DIR, "Palette.mtl"), "w") as f:
        f.write("newmtl Palette\nKd 1 1 1\n")
    dest = os.path.join(PALETTE_OBJ_DIR, os.path.basename(source_path))
    with open(dest, "w") as f:
        f.write("\n".join(out) + "\n")
    unreal.log_warning("팔레트 OBJ: {} 슬롯 {} → 칸 {}".format(os.path.basename(source_path), sorted(used),
                                                               [order.index(s) for s in sorted(used)]))
    return dest
