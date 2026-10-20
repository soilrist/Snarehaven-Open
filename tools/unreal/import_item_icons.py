"""M5-08 아이템 아이콘 반입 — `_Source/raw/Snare/Items/Icons/Icon_<id>.png` → `/Game/Game/UI/Icons/Items/T_Icon_<파스칼>`.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/import_item_icons.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본은 `_Source/raw/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 뿐이다.
출처는 바르코 생성 + 감독 후처리 — `docs/ASSET_LICENSES.md` 표.

**목록을 손으로 적지 않는다** — 폴더에 있는 `Icon_*.png` 를 전부 들인다 (DataDriven.md 1).
감독이 아이콘을 더 넣으면 이 스크립트는 그대로고 파일만 늘면 된다. 아이템 표의 `IconRef` 는
`create_data_tables.py` 가 **에셋이 실제로 있는 행만** 채운다(반대로 여기서 표를 건드리지 않는다) —
아이콘이 있는데 행이 없는 것(`hand_axe`·`gather_basket` 은 M9-04 에서 행이 선다)도, 행이 있는데
아이콘이 없는 것(자리표시 돼지·소·양 계열)도 지금은 정상이다.

**이름**: `Icon_fox_meat.png` → `T_Icon_FoxMeat`. 아이템 ID `item.fox_meat` ↔ 행 `Item_FoxMeat` 와
같은 파스칼 변환이라 이름에서 ID 를, ID 에서 이름을 기계적으로 만들 수 있다 (AssetNaming.md).
접두어 `T_` 는 `docs/engines/unreal.md` 텍스처 줄. 접미어(`_D`·`_N` 등)는 안 붙인다 — 그 표의
접미어는 머티리얼에 물리는 텍스처 채널 구분이고, 이건 UI 가 그대로 그리는 그림이다.

**텍스처 설정 셋** (감독 지시 2026-09-23 15:10):
  압축 `TC_EDITOR_ICON`(에디터 표시 이름 "UserInterface2D (RGB)") · 밉 없음 · sRGB 켬.
  UI 그림은 화면 픽셀에 1:1 로 붙어서 블록 압축을 쓰면 외곽선에 얼룩이 생기고, 축소본(밉)도
  쓸 일이 없다. 묶음(`lod_group`)도 `TEXTUREGROUP_UI` 로 둬 스트리밍에서 뺀다 — UI 아이콘이
  늦게 뜨면 빈 칸이 보인다.

**임포트 시점에는 설정이 안 먹는다** — `AssetImportTask` 에 `TextureFactory` 를 줘도 팩토리가
소스 파일을 보고 압축·sRGB 를 스스로 정한다. 그래서 들인 **뒤에** 셋을 덮어쓰고 다시 저장한다.
`set_editor_property` 가 에셋을 dirty 로 안 만드는 경우가 있어 `only_if_is_dirty=False` 로 저장한다
(루프#86 이 글꼴에서 겪은 그대로 — `import_ui_font.py` 주석).

재실행 안전: 이미 있으면 다시 임포트하지 않고 설정만 확인해 어긋난 것만 고친다.
"""
import os

import unreal


RAW_DIR = "~/UNREAL/Snarehaven/_Source/raw/Snare/Items/Icons"
DEST_DIR = "/Game/Game/UI/Icons/Items"
REPORT_PATH = "~/UNREAL/Snarehaven/logs/import_item_icons.txt"

# 이 셋이 "UI 텍스처 설정" 이다 (위 주석).
WANTED = {
    "compression_settings": unreal.TextureCompressionSettings.TC_EDITOR_ICON,
    "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
    "srgb": True,
    "lod_group": unreal.TextureGroup.TEXTUREGROUP_UI,
}

SAID = []


def say(msg: str) -> None:
    SAID.append(msg)
    unreal.log(msg)


# **이 엔진(5.8) 커맨드릿 로그는 `Log` 등급을 파일에 안 적는다**(루프#157 실측) — 결과는 고정 경로
# 보고 파일에 적고 STATUS 인용도 거기서 딴다. 경로만 Warning 으로 알린다.
def report() -> None:
    import datetime
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with open(REPORT_PATH, "w", encoding="utf-8") as handle:
        handle.write("# {}\n".format(stamp))
        handle.write("\n".join(SAID) + "\n")
    unreal.log_warning("보고 파일: {} ({}줄)".format(REPORT_PATH, len(SAID)))


def asset_name_for(item_id: str) -> str:
    """`fox_meat` → `T_Icon_FoxMeat`. `create_data_tables.py` 의 행 이름 변환과 같은 규칙이다."""
    return "T_Icon_" + "".join(part.capitalize() for part in item_id.split("_"))


def import_one(file_path: str, asset_name: str) -> bool:
    task = unreal.AssetImportTask()
    task.filename = file_path
    task.destination_path = DEST_DIR
    task.destination_name = asset_name  # 파일 이름(`Icon_fox_meat`) 대신 규칙에 맞는 이름으로 바로
    task.automated = True               # 옵션 창을 띄우지 않는다(헤드리스)
    task.replace_existing = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return unreal.EditorAssetLibrary.does_asset_exist("{}/{}".format(DEST_DIR, asset_name))


def apply_settings(asset_path: str) -> list:
    """UI 설정 셋을 덮어쓰고 바꾼 칸 이름을 돌려준다. 이미 맞으면 빈 목록."""
    texture = unreal.EditorAssetLibrary.load_asset(asset_path)
    if texture is None:
        unreal.log_error("에셋을 못 읽었다: {}".format(asset_path))
        return []

    changed = []
    for prop, wanted in WANTED.items():
        if texture.get_editor_property(prop) != wanted:
            texture.set_editor_property(prop, wanted)
            changed.append(prop)
    if changed:
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    return changed


REIMPORT = set(filter(None, os.environ.get("REIMPORT_ICONS", "").split(",")))


def main() -> None:
    if not os.path.isdir(RAW_DIR):
        unreal.log_error("원본 폴더가 없다: {}".format(RAW_DIR))
        return

    sources = sorted(name for name in os.listdir(RAW_DIR)
                     if name.startswith("Icon_") and name.endswith(".png"))
    say("원본 {}개 — {}".format(len(sources), RAW_DIR))

    imported = 0
    already = 0
    fixed = 0
    failed = 0
    for file_name in sources:
        item_id = file_name[len("Icon_"):-len(".png")]
        asset_name = asset_name_for(item_id)
        asset_path = "{}/{}".format(DEST_DIR, asset_name)

        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            if asset_name in REIMPORT:     # 그림을 다시 그린 아이콘만 덮어쓴다 (환경변수 REIMPORT_ICONS=이름,이름)
                import_one(os.path.join(RAW_DIR, file_name), asset_name)
            already += 1
        elif import_one(os.path.join(RAW_DIR, file_name), asset_name):
            imported += 1
        else:
            failed += 1
            say("실패: {} → {}".format(file_name, asset_path))
            unreal.log_error("임포트 실패: {}".format(asset_path))
            continue

        changed = apply_settings(asset_path)
        if changed:
            fixed += 1
        texture = unreal.EditorAssetLibrary.load_asset(asset_path)
        say("{} ← {} · {}x{} · 압축={} · 밉={} · sRGB={} · 묶음={}{}".format(
            asset_name, file_name,
            texture.blueprint_get_size_x(), texture.blueprint_get_size_y(),
            texture.get_editor_property("compression_settings"),
            texture.get_editor_property("mip_gen_settings"),
            texture.get_editor_property("srgb"),
            texture.get_editor_property("lod_group"),
            " (설정 고침: {})".format(", ".join(changed)) if changed else ""))

    say("최종: 원본 {}개 · 새로 들임 {}개 · 이미 있음 {}개 · 설정 고침 {}개 · 실패 {}개".format(
        len(sources), imported, already, fixed, failed))
    if failed:
        unreal.log_error("임포트 실패 {}개 — 보고 파일을 보라".format(failed))


main()
report()
