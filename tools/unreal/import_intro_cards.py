"""첫 실행 그림 카드 3장 반입 — `Saved/Generated/intro/IntroCard0N.png` → `/Game/Game/UI/Intro/T_IntroCard_0N`,
타이틀 바탕 `TitleBackdrop.png` → `T_TitleBackdrop`.

그림은 `tools/blender/render_intro_cards.py` 가 만든다. UI 텍스처 설정 넷(TC_EDITOR_ICON·밉 없음·sRGB·
TEXTUREGROUP_UI)은 `import_item_icons.py` 와 같고, 임포트 뒤 덮어쓰고 only_if_is_dirty=False 로 저장한다.

실행 (uproject·script 절대경로):
  UnrealEditor-Cmd <저장소>/Snarehaven.uproject -run=pythonscript -script=<저장소>/tools/unreal/import_intro_cards.py \
      -unattended -nosplash "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False"
"""
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC_DIR = os.path.join(ROOT, "Saved", "Generated", "intro")
DEST_DIR = "/Game/Game/UI/Intro"
WANTED = {
    "compression_settings": unreal.TextureCompressionSettings.TC_EDITOR_ICON,
    "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
    "srgb": True,
    "lod_group": unreal.TextureGroup.TEXTUREGROUP_UI,
}

PAIRS = [("IntroCard{:02d}.png".format(n), "T_IntroCard_{:02d}".format(n)) for n in (1, 2, 3)]
PAIRS.append(("TitleBackdrop.png", "T_TitleBackdrop"))  # 타이틀 바탕 (LobbyWidget)

for file_name, name in PAIRS:
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SRC_DIR, file_name)
    task.destination_path = DEST_DIR
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = "{}/{}".format(DEST_DIR, name)
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if tex is None:
        unreal.log_error("INTROIMPORT 실패 {}".format(path))
        continue
    for prop, wanted in WANTED.items():
        tex.set_editor_property(prop, wanted)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    unreal.log_warning("INTROIMPORT {} {}x{}".format(path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
