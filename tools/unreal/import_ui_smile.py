"""이모트 😊 버튼 아이콘 — 그려서 `/Game/Game/UI/Icons/T_UI_Smile` 로 들인다 (감독 2026-09-25, DESIGN 2.2 「이모트」).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_ui_smile.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

톱니 버튼 위 같은 기둥의 크림 네모 버튼에 얹는다. 노란 얼굴 + 진갈색 두 눈·웃는 입, 진갈색 테두리 (아이템 아이콘 톤).
그리는 법·텍스처 설정은 `import_ui_gear.py` 그대로(도형 판정 `inside` 만 다르다).
"""
import math
import os
import struct
import zlib

import unreal

SIZE, SUPER = 256, 4
R_OUT, R_IN = 0.46, 0.38            # 얼굴 고리 (폭 비율)
EYE_Y, EYE_DX, EYE_R = 0.40, 0.13, 0.055
MOUTH_R, MOUTH_W = 0.20, 0.045      # 입 = 가운데 원호 아래쪽 반
FACE, FACE_LIGHT, FEATURE = (0xF0, 0xB8, 0x3C), (0xF8, 0xD5, 0x6E), (0x4A, 0x38, 0x26)   # 노란 얼굴 + 윗쪽 밝은 결 + 눈·입
OUTLINE = 0.028   # 테두리 두께 (폭 비율)
OUTLINE_RGB = (0x4A, 0x38, 0x26)   # UI_TextOnButton 진갈색
OUT_PNG = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_UI_Smile.png"))
DEST_DIR = "/Game/Game/UI/Icons"
NAME = "T_UI_Smile"


def color_at(x, y):
    dx, dy = x - 0.5, y - 0.5
    r = math.hypot(dx, dy)
    if r > R_OUT:
        return None
    if any(math.hypot(x - (0.5 + s * EYE_DX), y - EYE_Y) < EYE_R for s in (-1, 1)):
        return FEATURE
    my = y - 0.52   # 입 원의 가운데는 눈 조금 아래
    if my > 0.02 and abs(math.hypot(dx, my) - MOUTH_R) < MOUTH_W * 0.5:
        return FEATURE
    return FACE_LIGHT if y < 0.5 - dx * 0.5 - 0.2 else FACE


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
unreal.log_warning("웃는 얼굴 반입: {}/{} {}x{}".format(DEST_DIR, NAME, SIZE, SIZE))
