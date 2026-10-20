"""M13-27 UI 글꼴 대체 칸 — `Font_UI_Jua` 복합 글꼴의 대체 타입페이스에 엔진 `RobotoBold` 얼굴을 물린다.

Jua 에는 ASCII·한글·몇몇 기호만 있어 `·`(U+00B7)·`—`(U+2014)·`…`(U+2026) 같은 문장부호가 없다 —
Slate 는 없는 글자를 Jua 로 억지로 그려 **네모**가 나온다(미니맵 좌표 `북 0 · 동 0`, 감독 `ScreenShot00013`).
대체 칸을 채우면 기본 얼굴에 없는 글자만 Roboto 로 그린다(`FCompositeFontCache::GetFontDataForCodepoint`).
Roboto Bold 인 이유: Jua 가 굵은 둥근 글씨라 Regular 보다 굵기가 맞다. 엔진 글꼴을 참조만 하므로 새 라이선스
기록은 없다(엔진 콘텐츠, 쿠킹 때 같이 들어간다).

대체 칸은 `UPROPERTY()` 뿐이라 파이썬이 직접 못 쓴다 — `USnareUiFontLibrary::SetFallbackFace` 가 대신 쓴다.
`--clear` 를 붙이면 대체 칸을 비운다(역검증용 — 2층 `Snarehaven.Ui.Font.*` 가 Fail 해야 한다).

실행(`import_ui_font.py` 와 같다 — 글꼴은 Slate 캐시를 타서 커맨드릿 불가, 에디터를 -RenderOffscreen 으로 띄운다):
  F1="-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False"
  F2="-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False"
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <uproject 절대경로> "$F1" "$F2" \\
      -RenderOffscreen -ExecCmds="py <이 파일 절대경로> [--clear]" -unattended -nosplash -nopause -log
"""
import sys

import unreal

FONT_PATH = "/Game/Game/UI/Fonts/Font_UI_Jua"
FALLBACK_FACE_PATH = "/Engine/EngineFonts/Faces/RobotoBold"

clear = "--clear" in sys.argv
font = unreal.EditorAssetLibrary.load_asset(FONT_PATH)
face = None if clear else unreal.EditorAssetLibrary.load_asset(FALLBACK_FACE_PATH)

if font is None or (not clear and face is None):
    unreal.log_error("글꼴을 못 찾았다: {} / {}".format(FONT_PATH, FALLBACK_FACE_PATH))
elif not unreal.SnareUiFontLibrary.set_fallback_face(font, face):
    unreal.log_error("대체 칸 쓰기 실패: {}".format(FONT_PATH))
else:
    # set 이 dirty 표시를 하지만 조건 없이 저장한다 (`import_ui_font.py` 루프#86 교훈).
    saved = unreal.EditorAssetLibrary.save_asset(FONT_PATH, only_if_is_dirty=False)
    unreal.log("UI 글꼴 대체 칸 = {} · 저장 {}".format("비움" if clear else FALLBACK_FACE_PATH, saved))

if "-unattended" in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.execute_console_command(None, "QUIT_EDITOR")
