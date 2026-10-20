"""출석 달력 버튼 아이콘 — 그려서 `/Game/Game/UI/Icons/T_UI_Calendar` 로 들인다 (루프#487, DESIGN 11 「출석 달력 화면」).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_ui_calendar.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

오른쪽 아래 버튼 기둥 맨 위 크림 네모 버튼에 얹는다. 종이색 몸 + 빨간 윗띠 + 쇠 고리 둘 + 날짜 칸 3×2, 진갈색 테두리 (아이템 아이콘 톤).
그리는 법·텍스처 설정은 `import_ui_smile.py` 그대로(도형 판정 `inside` 만 다르다).
"""
import math
import os
import struct
import zlib

import unreal

SIZE, SUPER = 256, 4
BODY_L, BODY_R, BODY_T, BODY_B = 0.12, 0.88, 0.20, 0.88   # 달력 몸 (폭 비율)
BAND_B = 0.38                                             # 윗띠 아래 변
RING_X, RING_W, RING_T, RING_B = (0.32, 0.68), 0.07, 0.08, 0.28   # 위로 솟은 고리 둘
DOT_XS, DOT_YS, DOT_HALF = (0.30, 0.50, 0.70), (0.53, 0.72), 0.055  # 날짜 칸 네모 3×2
PAPER, RED, RED_DARK, METAL, DOT = (0xF6, 0xEC, 0xD2), (0xC8, 0x52, 0x3C), (0xA6, 0x3E, 0x2C), (0x9A, 0xA4, 0xAA), (0x6B, 0x55, 0x3E)   # 종이·빨간 윗띠·고리·날짜 칸
OUTLINE = 0.028   # 테두리 두께 (폭 비율)
OUTLINE_RGB = (0x4A, 0x38, 0x26)   # UI_TextOnButton 진갈색
OUT_PNG = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_UI_Calendar.png"))
DEST_DIR = "/Game/Game/UI/Icons"
NAME = "T_UI_Calendar"


def color_at(x, y):
    if any(abs(x - rx) < RING_W * 0.5 and RING_T <= y <= RING_B for rx in RING_X):
        return METAL
    if not (BODY_L <= x <= BODY_R and BODY_T <= y <= BODY_B):
        return None
    if y <= BAND_B:
        return RED_DARK if y > BAND_B - 0.03 else RED
    return DOT if any(abs(x - dx) < DOT_HALF and abs(y - dy) < DOT_HALF for dx in DOT_XS for dy in DOT_YS) else PAPER


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
unreal.log_warning("달력 아이콘 반입: {}/{} {}x{}".format(DEST_DIR, NAME, SIZE, SIZE))
