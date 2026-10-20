"""M0-02 부트 맵 생성 — 헤드리스로 L_Boot/L_Title/L_Hunt 빈 레벨을 만든다.

실행 (사람 개입 없이, 에디터 UI 없이):
  <UE>/Engine/Binaries/Mac/UnrealEditor-Cmd Snarehaven.uproject \
      -run=pythonscript -script="tools/unreal/create_boot_maps.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

이미 있는 맵은 건드리지 않는다 (재실행해도 안전).
"""
import unreal

MAP_PATHS = [
    "/Game/Game/Maps/L_Boot",
    "/Game/Game/Maps/L_Title",
    "/Game/Game/Maps/L_Hunt",
]


def create_map(asset_path: str) -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log("skip (already exists): {}".format(asset_path))
        return

    package_path, asset_name = asset_path.rsplit("/", 1)
    # WorldFactory.world_type 는 UPROPERTY 가 아니라 Python 에 안 보인다 — 기본값(Inactive)이어도
    # 저장된 .umap 에는 안 남는다(OpenLevel 때마다 엔진이 다시 정한다), 그래서 그냥 둔다.
    factory = unreal.WorldFactory()

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    new_asset = asset_tools.create_asset(asset_name, package_path, unreal.World, factory)
    if new_asset is None:
        unreal.log_error("failed to create: {}".format(asset_path))
        return

    unreal.EditorAssetLibrary.save_loaded_asset(new_asset)
    unreal.log("created: {}".format(asset_path))


for path in MAP_PATHS:
    create_map(path)
