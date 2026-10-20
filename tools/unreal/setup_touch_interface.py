"""레퍼런스처럼 화면을 비운다 — 엔진 기본 가상 조이스틱을 복사해 이동 스틱 하나만 남기고, 손을 떼면 안 보이게 한다.

실행 (절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_touch_interface.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

`Config/DefaultInput.ini` 의 DefaultTouchInterface 가 이 에셋을 가리킨다. 오른쪽 스틱은 아무 입력에도 안 묶여 있어
(IMC_Default 는 Gamepad_Left2D 만 받는다) 뺀다. 값은 DESIGN 3.3 「조이스틱」 줄.
"""
import unreal

SRC = "/Engine/MobileResources/HUD/LeftVirtualJoystickOnly.LeftVirtualJoystickOnly"
DST = "/Game/Game/UI/TI_Snare_Joystick"
INACTIVE_OPACITY = 0.25  # 쉬는 동안 희미하게 — 처음 하는 사람이 왼쪽 아래가 이동 자리임을 안다(10-16 HUD 검수)
ACTIVE_OPACITY = 0.6
TIME_UNTIL_DEACTIVE = 0.3

assets = unreal.EditorAssetLibrary
if not assets.does_asset_exist(DST):
    # 커맨드렛에선 엔진 콘텐츠가 에셋 레지스트리에 없어 duplicate_asset(경로) 가 실패한다 — 객체로 복제
    unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
        DST.rsplit("/", 1)[1], DST.rsplit("/", 1)[0], unreal.load_asset(SRC))
ti = assets.load_asset(DST)
controls = [c for c in ti.get_editor_property("controls")
            if c.get_editor_property("main_input_key").get_editor_property("key_name") == "Gamepad_LeftX"]
ti.set_editor_property("controls", controls)
ti.set_editor_property("inactive_opacity", INACTIVE_OPACITY)
ti.set_editor_property("active_opacity", ACTIVE_OPACITY)
ti.set_editor_property("time_until_deactive", TIME_UNTIL_DEACTIVE)
assets.save_asset(DST, only_if_is_dirty=False)
unreal.log_warning("[Touch] {} 컨트롤 {} · 쉴 때 {} · 누를 때 {}".format(DST, len(controls), INACTIVE_OPACITY, ACTIVE_OPACITY))
