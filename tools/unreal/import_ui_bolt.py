"""기력 한 칸 그림 「나무 눈금 + 잎」 — 그려서 `/Game/Game/UI/Icons/T_UI_Bolt` 로 들인다 (M98-d, DESIGN 7.1 체력·기력 줄).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_ui_bolt.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

예전 번개 모양(레퍼런스 steam_05 를 따라 그린 것)은 버렸다. 이제 나무 눈금 틀(진갈색 `#74512F` 둥근 사각 테두리) 안에
흰 잎 한 장(가운데 잎맥 진갈색)이다. HUD 가 칸마다 색을 곱하므로(켜짐 노랑·소진 옅은 갈색) 흰 잎만 물들고 틀은 어둡게 남는다.
파일 이름 `T_UI_Bolt`·HUD 코드(`UpdateStaminaBolts`)는 그대로 — 기능 불변. 표준 라이브러리만(PIL 없음). 재실행 안전.
텍스처 설정은 `import_item_icons.py` 의 UI 셋(TC_EDITOR_ICON·밉 없음·sRGB·UI 묶음) 그대로.
"""
import math
import os
import struct
import zlib

import unreal

WIDTH, HEIGHT, SUPER = 48, 60, 4
FRAME = (0x74, 0x51, 0x2F)   # 나무 눈금 틀 (DESIGN 7.1 칸 틀)
FRAME_RADIUS = 7.0           # 틀 모서리 (픽셀)
FRAME_THICK = 3.5            # 틀 두께 (픽셀)
LEAF_ANGLE = 0.55            # 잎이 오른쪽으로 기운 각 (라디안)
LEAF_HALF_LEN, LEAF_HALF_WID = 19.0, 8.5   # 잎 반길이·반폭 (픽셀)
VEIN = 1.1                   # 잎맥 반폭 (픽셀)
OUT_PNG = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "T_UI_Bolt.png"))
DEST_DIR = "/Game/Game/UI/Icons"


def frame_dist(x, y):
    """둥근 사각 틀 바깥선까지 안쪽으로 잰 거리 (음수 = 틀 밖)."""
    hx, hy = WIDTH / 2.0 - 1.0, HEIGHT / 2.0 - 1.0
    qx, qy = abs(x - WIDTH / 2.0) - (hx - FRAME_RADIUS), abs(y - HEIGHT / 2.0) - (hy - FRAME_RADIUS)
    outside = (max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2) ** 0.5 + min(max(qx, qy), 0.0) - FRAME_RADIUS
    return -outside


def leaf_coords(x, y):
    """잎 축 좌표 (길이 방향 u, 폭 방향 v)."""
    dx, dy = x - WIDTH / 2.0, y - HEIGHT / 2.0
    c, s = math.cos(LEAF_ANGLE), math.sin(LEAF_ANGLE)
    return dx * s - dy * c, dx * c + dy * s


def in_leaf(x, y):
    u, v = leaf_coords(x, y)
    if abs(u) >= LEAF_HALF_LEN:
        return False
    return abs(v) <= LEAF_HALF_WID * math.sin(math.pi * (u / LEAF_HALF_LEN * 0.5 + 0.5))


def draw():
    rows = []
    total = SUPER * SUPER
    for py in range(HEIGHT):
        row = bytearray([0])
        for px in range(WIDTH):
            frame = leaf = vein = 0
            for sy in range(SUPER):
                for sx in range(SUPER):
                    x, y = px + (sx + 0.5) / SUPER, py + (sy + 0.5) / SUPER
                    d = frame_dist(x, y)
                    if 0.0 <= d <= FRAME_THICK:
                        frame += 1
                    elif in_leaf(x, y):
                        if abs(leaf_coords(x, y)[1]) <= VEIN:
                            vein += 1
                        else:
                            leaf += 1
            covered = frame + leaf + vein
            if covered == 0:
                row += bytes(4)
                continue
            dark = frame + vein
            white = leaf
            rgb = [round((255 * white + c * dark) / covered) for c in FRAME]
            row += bytes(rgb + [round(255 * covered / total)])
        rows.append(bytes(row))
    raw = zlib.compress(b"".join(rows), 9)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(OUT_PNG), exist_ok=True)
    with open(OUT_PNG, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", raw) + chunk(b"IEND", b""))


draw()
task = unreal.AssetImportTask()
task.filename = OUT_PNG
task.destination_path = DEST_DIR
task.automated = True
task.replace_existing = True
task.save = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = unreal.EditorAssetLibrary.load_asset(DEST_DIR + "/T_UI_Bolt")
texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("srgb", True)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
unreal.EditorAssetLibrary.save_asset(DEST_DIR + "/T_UI_Bolt", only_if_is_dirty=False)
unreal.log_warning("눈금 잎 반입: {}/T_UI_Bolt {}x{}".format(DEST_DIR, WIDTH, HEIGHT))
