"""
에디터 Play(PIE)를 **사람이 Play 버튼을 누르는 길 그대로** 켜고 몇 초 둔 뒤 끄는 검사용 스크립트.

왜 필요한가: 감독이 2026-09-24 에 올린 결함(① 에디터 Play 에서 시작 몽둥이가 안 나온다)은 **PIE 에서만**
나는 것이라 `-game` 실행으로는 재현도 확인도 안 된다. 커맨드릿(`-run=pythonscript`)에도 틱 루프가 없어
PIE 가 진행되지 않는다 — 그래서 에디터를 띄운 채 `-ExecCmds="py ..."` 로 이 스크립트를 태운다.

실행 (경로는 전부 절대경로 — 상대경로는 Binaries/Mac 기준으로 풀린다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -RenderOffscreen -nosplash -unattended -ResX=1280 -ResY=720 \
      -ExecCmds="py ~/UNREAL/Snarehaven/tools/unreal/run_pie_check.py" \
      -abslog=~/UNREAL/Snarehaven/logs/pie_<이름>.log

경로에 따옴표를 겹쳐 넣지 않는다 — `py "<경로>"` 로 주면 명령줄에는 그대로 실려도 파이썬이 한 줄도 안 돈다
(루프#172 실측: `LogPython` 에 실행 줄이 없다).

무엇을 보는가: 로그의 `[PIE검사]` 줄(켰다·상태·껐다)과, 그 사이에 게임 코드가 남긴 줄들.
켜는 것 말고는 아무것도 하지 않는다 — 판정은 로그를 읽는 사람(또는 다음 grep)이 한다.
"""
import unreal

# 몇 초 동안 PIE 를 켜 둘까. 백엔드 왕복(로그인 → 보유 상태 → 시작 장비)이 `-game` 에서 1.7초쯤
# 걸렸으므로(logs/game_loop172_regression.engine.log) 넉넉히 둔다.
PIE_SECONDS = 12.0

_state = {"elapsed": 0.0, "started": False, "handle": None}


def _log(message):
    unreal.log("[PIE검사] {0}".format(message))


def _level_editor():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _finish():
    editor = _level_editor()
    _log("끈다 — PIE 중인가: {0}".format(editor.is_in_play_in_editor()))
    editor.editor_request_end_play()
    if _state["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(_state["handle"])
        _state["handle"] = None
    unreal.SystemLibrary.quit_editor()


def _on_tick(delta_seconds):
    _state["elapsed"] += delta_seconds
    if not _state["started"]:
        # 첫 틱에서 켠다 — 스크립트가 도는 동안(에디터 시작 중)에는 Play 요청이 먹지 않는다.
        _state["started"] = True
        _log("에디터 Play 를 요청한다")
        _level_editor().editor_request_begin_play()
        return

    if _state["elapsed"] >= PIE_SECONDS:
        _finish()


_log("시작 — {0}초 동안 PIE 를 켜 둔다".format(PIE_SECONDS))
_state["handle"] = unreal.register_slate_post_tick_callback(_on_tick)
