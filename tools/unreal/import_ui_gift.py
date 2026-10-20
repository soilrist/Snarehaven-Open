"""선물 버튼 아이콘 — 그려서 `/Game/Game/UI/Icons/T_UI_Gift` 로 들인다 (10-16 HUD 2차: 선물 버튼이 글자만이었다).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_ui_gift.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

오른쪽 아래 버튼 기둥의 선물 칸에 얹는다. 민트 상자 + 빨간 리본 십자 + 위 나비 고리 둘, 진갈색 테두리 (달력 아이콘 톤).
그리는 법·텍스처 설정은 `import_ui_calendar.py` 그대로(도형 판정 `color_at` 만 다르다).
"""
import math
import os
import struct
import zlib

import unreal

SIZE, SUPER = 256, 4
BOX_L, BOX_R, BOX_T, BOX_B = 0.14, 0.86, 0.42, 0.88       # 상자 몸
LID_L, LID_R, LID_T, LID_B = 0.10, 0.90, 0.30, 0.44       # 뚜껑 (몸보다 넓다)
RIBBON_HALF = 0.07                                        # 세로 리본 반폭
BOW_CENTERS, BOW_RX, BOW_RY, BOW_Y = (0.36, 0.64), 0.13, 0.09, 0.20   # 나비 고리 둘 (타원)
BOX, BOX_DARK, RIBBON = (0x7C, 0xC7, 0xA6), (0x5E, 0xA8, 0x88), (0xD8, 0x4F, 0x45)   # 민트 몸·뚜껑 그늘·빨간 리본
OUTLINE = 0.028   # 테두리 두께 (폭 비율)
OUTLINE_RGB = (0x4A, 0x38, 0x26)   # UI_TextOnButton 진갈색
OUT_PNG = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_UI_Gift.png"))
DEST_DIR = "/Game/Game/UI/Icons"
NAME = "T_UI_Gift"


def color_at(x, y):
    if any(((x - cx) / BOW_RX) ** 2 + ((y - BOW_Y) / BOW_RY) ** 2 <= 1.0 for cx in BOW_CENTERS):
        return RIBBON
    in_lid = LID_L <= x <= LID_R and LID_T <= y <= LID_B
    in_box = BOX_L <= x <= BOX_R and BOX_T <= y <= BOX_B
    if not (in_lid or in_box):
        return None
    if abs(x - 0.5) < RIBBON_HALF:
        return RIBBON
    return BOX_DARK if in_lid else BOX


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
unreal.log_warning("선물 아이콘 반입: {}/{} {}x{}".format(DEST_DIR, NAME, SIZE, SIZE))
