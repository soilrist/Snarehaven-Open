"""메뉴 톱니 ⚙ 아이콘 — 그려서 `/Game/Game/UI/Icons/T_UI_Gear` 로 들인다 (감독 2026-09-25, DESIGN 2.2 「설정」).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_ui_gear.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

폰에는 ESC 가 없어 오른쪽 아래 지도 버튼 위에 메뉴 버튼을 둔다(레퍼런스 모바일도 톱니). 크림 네모 버튼 위에 얹히므로
쇳빛으로 칠하고 진갈색(`UI_TextOnButton` #4A3826) 테두리를 두른 톱니바퀴 + 가운데 구멍 (아이템 아이콘 톤). 그리는 법·텍스처 설정은 `import_ui_bolt.py` 그대로.
"""
import math
import os
import struct
import zlib

import unreal

SIZE, SUPER = 256, 4
TEETH = 8
R_TOOTH, R_BODY, R_HOLE = 0.46, 0.35, 0.14   # 반지름 (폭 비율)
TOOTH_SHARE = 0.5   # 한 마디에서 이가 차지하는 몫
STEEL, STEEL_LIGHT = (0x8E, 0x9B, 0xA3), (0xB9, 0xC4, 0xC9)   # 쇳빛 몸 + 윗쪽 밝은 결
OUTLINE = 0.028   # 테두리 두께 (폭 비율)
OUTLINE_RGB = (0x4A, 0x38, 0x26)   # UI_TextOnButton 진갈색
OUT_PNG = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_UI_Gear.png"))
DEST_DIR = "/Game/Game/UI/Icons"
NAME = "T_UI_Gear"


def color_at(x, y):
    dx, dy = x - 0.5, y - 0.5
    r = math.hypot(dx, dy)
    if r < R_HOLE or r > R_TOOTH:
        return None
    if r > R_BODY:
        phase = (math.atan2(dy, dx) / (2 * math.pi) * TEETH + 0.25) % 1.0
        if abs(phase - 0.5) >= TOOTH_SHARE * 0.5:
            return None
    return STEEL_LIGHT if y < 0.5 - dx * 0.4 - 0.12 and r > R_HOLE + 0.06 and r < R_BODY - 0.02 else STEEL


def sample(x, y):
    """색칠한 칸이면 그 색, 바로 바깥 `OUTLINE` 두께 안이면 진갈색 테두리, 아니면 None."""
    color = color_at(x, y)
    if color:
        return color
    for k in range(8):
        angle = k * math.pi / 4
        if color_at(x + OUTLINE * math.cos(angle), y + OUTLINE * math.sin(angle)):
            return OUTLINE_RGB
    return None


def draw():
    rows = []
    for py in range(SIZE):
        row = bytearray([0])
        for px in range(SIZE):
            hits = [c for c in (sample((px + (sx + 0.5) / SUPER) / SIZE, (py + (sy + 0.5) / SUPER) / SIZE)
                                for sy in range(SUPER) for sx in range(SUPER)) if c]
            if hits:
                rgb = [round(sum(c[i] for c in hits) / len(hits)) for i in range(3)]
                row += bytes(rgb + [round(255 * len(hits) / (SUPER * SUPER))])
            else:
                row += bytes(4)
        rows.append(bytes(row))
    raw = zlib.compress(b"".join(rows), 9)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(OUT_PNG), exist_ok=True)
    with open(OUT_PNG, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", raw) + chunk(b"IEND", b""))


draw()
task = unreal.AssetImportTask()
task.filename = OUT_PNG
task.destination_path = DEST_DIR
task.automated = True
task.replace_existing = True
task.save = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = unreal.EditorAssetLibrary.load_asset(DEST_DIR + "/" + NAME)
texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("srgb", True)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
unreal.EditorAssetLibrary.save_asset(DEST_DIR + "/" + NAME, only_if_is_dirty=False)
unreal.log_warning("톱니 반입: {}/{} {}x{}".format(DEST_DIR, NAME, SIZE, SIZE))
