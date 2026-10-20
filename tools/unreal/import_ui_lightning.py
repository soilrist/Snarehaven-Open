"""기력 줄 이름표 번개 — 그려서 `/Game/Game/UI/Icons/T_UI_Lightning` 로 들인다 (10-16 HUD 2차: 잎 눈금 줄이 기력인지 몰랐다).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_ui_lightning.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

기력 잎 눈금 줄(`T_UI_Bolt`) 왼쪽 끝에 하나만 얹는다. 노란 지그재그 번개, 진갈색 테두리. 칸 잎은 그대로 둔다(M98-d).
그리는 법·텍스처 설정은 `import_ui_calendar.py` 그대로(도형 판정 `color_at` 만 다르다).
"""
import math
import os
import struct
import zlib

import unreal

SIZE, SUPER = 256, 4
# 번개 다각형 (폭 비율, 시계 방향) — 위 오른쪽에서 왼쪽 아래로 꺾여 내려오는 지그재그
BOLT = [(0.62, 0.06), (0.26, 0.54), (0.48, 0.54), (0.36, 0.94), (0.76, 0.40), (0.53, 0.40), (0.68, 0.06)]
YELLOW = (0xF6, 0xC9, 0x3C)
OUTLINE = 0.035   # 테두리 두께 (폭 비율)
OUTLINE_RGB = (0x4A, 0x38, 0x26)   # UI_TextOnButton 진갈색
OUT_PNG = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_UI_Lightning.png"))
DEST_DIR = "/Game/Game/UI/Icons"
NAME = "T_UI_Lightning"


def color_at(x, y):
    inside = False
    for (x1, y1), (x2, y2) in zip(BOLT, BOLT[1:] + BOLT[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            inside = not inside
    return YELLOW if inside else None


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
unreal.log_warning("번개 아이콘 반입: {}/{} {}x{}".format(DEST_DIR, NAME, SIZE, SIZE))
