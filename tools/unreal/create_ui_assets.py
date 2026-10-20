"""화면 위젯 블루프린트를 헤드리스로 만든다 (`WBP_HUD_Hunt`·`WBP_HUD_Ranch`·`WBP_Lobby`·`WBP_PauseMenu`).

실행 (uproject 는 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_ui_assets.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

부모 클래스는 C++ 의 `UHuntHudWidget`·`URanchHudWidget`·`ULobbyWidget`·`UPauseMenuWidget`. 위젯 트리는 여기서 채우지 않는다 —
트리가 비어 있으면 C++ 이 자리표시로 세우고(디자인이 없어도 HUD 가 뜬다), 사람이 에디터에서
디자인을 얹으면 그때부터 그 트리가 이긴다. 이미 있으면 건드리지 않는다(재실행해도 안전).

에셋이 없으면 `SnarehavenCharacter`·`ASnarehavenGameMode` 생성자의 `FClassFinder` 가 CDO 를 만들 때마다
`LogUObjectGlobals: Error: ... Failed to find` 를 찍는다 — 그래서 둘 다 있어야 한다.
"""
import unreal

UI_DIR = "/Game/Game/UI"

# (에셋 이름, 부모 C++ 위젯 클래스)
HUD_WIDGETS = [
    ("WBP_HUD_Hunt", unreal.HuntHudWidget),
    ("WBP_HUD_Ranch", unreal.RanchHudWidget),
    ("WBP_Lobby", unreal.LobbyWidget),
    ("WBP_PauseMenu", unreal.PauseMenuWidget),  # M1-10b ESC 일시정지 메뉴
]


def _ensure_hud_widget(asset_name: str, parent_class: type) -> None:
    asset_path = "{}/{}".format(UI_DIR, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log("이미 있음: {}".format(asset_path))
    else:
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)

        tools = unreal.AssetToolsHelpers.get_asset_tools()
        if tools.create_asset(asset_name, UI_DIR, None, factory) is None:
            unreal.log_error("생성 실패: {}".format(asset_path))
            return
        unreal.EditorAssetLibrary.save_asset(asset_path)
        unreal.log("생성: {}".format(asset_path))

    # UBlueprint.ParentClass 도 UClass.is_child_of 도 파이썬에 없다 — 생성된 클래스의 CDO 가
    # 부모 타입인지로 본다. 만든 바퀴뿐 아니라 매 실행 확인한다.
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    generated = blueprint.generated_class() if blueprint else None
    cdo = unreal.get_default_object(generated) if generated else None
    if isinstance(cdo, parent_class):
        unreal.log("확인: {} 는 {} 파생".format(asset_path, parent_class.__name__))
    else:
        unreal.log_error("부모 불일치: {} 가 {} 파생이 아니다".format(asset_path, parent_class.__name__))


for name, parent in HUD_WIDGETS:
    _ensure_hud_widget(name, parent)

unreal.log("UI 에셋 정리 완료")
