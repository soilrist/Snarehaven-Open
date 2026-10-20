"""M5-05a UI 글꼴 임포트 — `Jua-Regular.ttf` → `Font_UI_Jua_Face`(FontFace) + `Font_UI_Jua`(Font).

실행 — **커맨드릿(`-run=pythonscript`)으로는 못 돌린다**: 글꼴 임포트가 Slate 폰트 캐시를 타서
(`UFontFace::CacheSubFaces`) Slate 없는 커맨드릿에서는 `CurrentApplication.IsValid()` 어서션으로 죽는다.
`-RenderOffscreen` 으로 에디터를 띄워 그 안에서 돌린다. 끝나면 스크립트가 스스로 에디터를 닫는다
(`-ExecCmds="py <경로>; Quit"` 는 안 된다 — 세미콜론까지 파일 이름으로 넘어간다).
uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -RenderOffscreen -ExecCmds="py ~/UNREAL/Snarehaven/tools/unreal/import_ui_font.py" \
      -unattended -nosplash -nopause \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

원본은 `_Source/raw/Font_Jua/`(gitignore)에 있고 커밋되는 것은 임포트 결과 `.uasset` 둘이다.
라이선스 SIL OFL 1.1 — `docs/ASSET_LICENSES.md` 표(상업적 재배포 허용, DESIGN 4.6 조건 충족).

**에셋이 둘인 이유**: 화면 글자(`FSlateFontInfo.FontObject`)에 물릴 수 있는 것은 `IFontProviderInterface`
를 구현한 `UFont` 뿐이다(`SlateFontInfo.cpp`) — `UFontFace` 는 못 물린다. 그래서 TTF 는 FontFace 로
들어오고, 임포트 팩토리가 그 얼굴을 기본 타입페이스로 쓰는 `UFont` 를 같이 만든다
(`UFontFileImportFactory::FactoryCreateBinary`, `batch_create_font_asset`). 팩토리가 붙이는 이름이
`<얼굴>_Font` 라 규칙(`docs/engines/unreal.md` 접두어 표에 글꼴 줄이 없어 Epic 공식 표의 `Font_` 를 따름,
🟡 INBOX `확인`)에 맞게 `Font_UI_Jua` 로 바꾼다. 이미 있으면 아무것도 하지 않는다(재실행 안전).
"""
import unreal

TTF = "~/UNREAL/Snarehaven/_Source/raw/Font_Jua/Jua-Regular.ttf"
FONT_DIR = "/Game/Game/UI/Fonts"
FACE_NAME = "Font_UI_Jua_Face"
FONT_NAME = "Font_UI_Jua"

face_path = "{}/{}".format(FONT_DIR, FACE_NAME)
font_path = "{}/{}".format(FONT_DIR, FONT_NAME)
created_font_path = "{}/{}_Font".format(FONT_DIR, FACE_NAME)  # 팩토리가 붙이는 이름

if unreal.EditorAssetLibrary.does_asset_exist(font_path) and unreal.EditorAssetLibrary.does_asset_exist(face_path):
    unreal.log("이미 있음: {} · {}".format(face_path, font_path))
else:
    factory = unreal.FontFileImportFactory()
    # 물어보는 대화상자 없이 얼굴과 같이 UFont 도 만든다 (헤드리스라 답할 사람이 없다).
    factory.set_editor_property("batch_create_font_asset", unreal.BatchCreateFontAsset.CREATE_IF_NO_FONT_EXISTS)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", TTF)
    task.set_editor_property("destination_path", FONT_DIR)
    task.set_editor_property("destination_name", FACE_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("factory", factory)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not unreal.EditorAssetLibrary.does_asset_exist(face_path):
        unreal.log_error("임포트 실패: {}".format(face_path))
    elif unreal.EditorAssetLibrary.does_asset_exist(created_font_path):
        if unreal.EditorAssetLibrary.rename_asset(created_font_path, font_path):
            unreal.log("이름 바꿈: {} → {}".format(created_font_path, font_path))
        else:
            unreal.log_error("이름 바꾸기 실패: {} → {}".format(created_font_path, font_path))
    elif not unreal.EditorAssetLibrary.does_asset_exist(font_path):
        unreal.log_error("UFont 가 안 만들어졌다: {}".format(created_font_path))

# 얼굴 데이터를 에셋 안에 넣는다(Inline). 기본값(LazyLoad)은 `_Source/raw/Font_Jua/Jua-Regular.ttf`
# **원본 경로**를 들고 있다가 글자를 처음 그릴 때 그 파일을 읽는다 — `_Source/` 는 gitignore 라 포장한
# 모바일 빌드에는 없고, 그러면 글자가 통째로 대체 글꼴로 돌아간다. 에디터에서만 맞는 글꼴이 되는 셈이다.
# `set_editor_property` 는 에셋을 dirty 로 표시하지 않는 경우가 있어 **조건 없이 저장한다**
# (`only_if_is_dirty=False`) — 루프#86 은 그냥 `save_asset` 만 불러 정책이 LazyLoad 로 남았다
# (루프#87 로그 `Font face '/Users/.../\_Source/raw/...' ... added` 가 그 증거다).
face = unreal.EditorAssetLibrary.load_asset(face_path)
if face is not None:
    face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)

for path in (face_path, font_path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    else:
        unreal.log_error("없음: {}".format(path))

# 이름을 바꾸면 옛 이름 자리에 리다이렉터(`.uasset`)가 남는다 — 아직 아무도 옛 이름을 안 쓰므로
# 커밋 전에 지운다(`docs/engines/unreal.md` "언리얼 파일 다루기" 2).
if unreal.EditorAssetLibrary.does_asset_exist(created_font_path):
    unreal.EditorAssetLibrary.delete_asset(created_font_path)
    unreal.log("리다이렉터 정리: {}".format(created_font_path))

unreal.log("UI 글꼴 임포트 완료")

# 헤드리스로 띄운 에디터는 스스로 닫아야 한다 (사람이 연 에디터에서 돌렸으면 그대로 둔다).
if "-unattended" in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.execute_console_command(None, "QUIT_EDITOR")
