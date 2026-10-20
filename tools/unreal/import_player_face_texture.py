"""INBOX 결함 2026-10-04 12:40 얼굴 그림 키우기 — `make_player_face_texture.py` 가 그린 몸 바탕색을 `T_Player_Body_BaseColor` 에 다시 반입.

눈 감은 짝 `T_Player_Body_BaseColor_Blink`(`UPlayerFaceBlinkComponent` 가 잠깐 바꿔 낀다)와
표정 3종 `_Hurt`·`_Down`·`_Happy`(M85, DESIGN 2.2 「표정 3종」)도 같이 받는다.

`make_player_face_texture.py` 다음에 돌린다:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/import_player_face_texture.py" -unattended -nosplash -log

같은 에셋 자리에 덮어 받고(`replace_existing`) 기존 설정(sRGB·압축·LOD 묶음)은 그대로 둔다(`replace_existing_settings=False`).
몸 메시·머티리얼(`MI_Player_Body`)은 안 건드린다 — 머티리얼이 이 텍스처를 그대로 가리킨다. 재실행 안전.
"""
import os

import unreal

TEXTURE_PATH = "/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor"
BLINK_PATH = "/Game/Game/Characters/Player/Snare/T_Player_Body_BaseColor_Blink"
SOURCE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "_Source", "raw", "Snare", "Characters",
                                          "Player"))
SOURCE = os.path.join(SOURCE_DIR, "T_Player_Body_BaseColor_Face.png")
BLINK_SOURCE = os.path.join(SOURCE_DIR, "T_Player_Body_BaseColor_Blink.png")
EXPRESSIONS = ("Hurt", "Down", "Happy")
# 감은 짝은 0.1초 남짓만 보인다 — 게임 카메라에서 몸이 약 100px 라 1024 로 줄여도 차이가 안 보이고, 폰 메모리에
# 4096² 한 장을 더 올리지 않는다(DESIGN 3.3 「눈 깜빡임」).
BLINK_MAX_SIZE = 1024

assets = unreal.EditorAssetLibrary


def _settings(texture):
    return {name: texture.get_editor_property(name) for name in ("srgb", "compression_settings", "lod_group")}


def _import(source, path, settings, replace_settings):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = path.rsplit("/", 1)[0]
    task.destination_name = path.rsplit("/", 1)[1]
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = replace_settings
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = assets.load_asset(path)
    after = _settings(texture)
    for name, value in settings.items():
        if after[name] != value:
            texture.set_editor_property(name, value)
    return texture


EXPRESSION_SOURCES = {name: os.path.join(SOURCE_DIR, "T_Player_Body_BaseColor_{}.png".format(name)) for name in EXPRESSIONS}

for source in (SOURCE, BLINK_SOURCE) + tuple(EXPRESSION_SOURCES.values()):
    if not os.path.exists(source):
        unreal.log_error("[PlayerFace] 원본 없음 — make_player_face_texture.py 먼저: {}".format(source))
        raise SystemExit(1)
if not assets.does_asset_exist(TEXTURE_PATH):
    unreal.log_error("[PlayerFace] 텍스처 없음 — import_player_character.py 먼저: {}".format(TEXTURE_PATH))
    raise SystemExit(1)
before = _settings(assets.load_asset(TEXTURE_PATH))
texture = _import(SOURCE, TEXTURE_PATH, before, False)
assets.save_asset(TEXTURE_PATH, only_if_is_dirty=False)
unreal.log_warning("[PlayerFace] {} 크기 {}x{} · 원본 {} · 설정 전 {} · 후 {}".format(
    TEXTURE_PATH, texture.blueprint_get_size_x(), texture.blueprint_get_size_y(),
    texture.get_editor_property("asset_import_data").get_first_filename(), before, _settings(texture)))

# 눈 감은 짝 — 눈 뜬 그림과 같은 설정 + 크기만 줄인다
blink = _import(BLINK_SOURCE, BLINK_PATH, before, True)
blink.set_editor_property("max_texture_size", BLINK_MAX_SIZE)
assets.save_asset(BLINK_PATH, only_if_is_dirty=False)
unreal.log_warning("[PlayerFace] {} 크기 {}x{} · 최대 {} · 원본 {} · 설정 {}".format(
    BLINK_PATH, blink.blueprint_get_size_x(), blink.blueprint_get_size_y(), blink.get_editor_property("max_texture_size"),
    blink.get_editor_property("asset_import_data").get_first_filename(), _settings(blink)))

# 표정 3종 — 감은 짝과 같은 설정·같은 1024 (몸이 게임 카메라에서 약 100px)
for name in EXPRESSIONS:
    path = "{}_{}".format(TEXTURE_PATH, name)
    face = _import(EXPRESSION_SOURCES[name], path, before, True)
    face.set_editor_property("max_texture_size", BLINK_MAX_SIZE)
    assets.save_asset(path, only_if_is_dirty=False)
    unreal.log_warning("[PlayerFace] {} 크기 {}x{} · 최대 {} · 원본 {} · 설정 {}".format(
        path, face.blueprint_get_size_x(), face.blueprint_get_size_y(), face.get_editor_property("max_texture_size"),
        face.get_editor_property("asset_import_data").get_first_filename(), _settings(face)))
