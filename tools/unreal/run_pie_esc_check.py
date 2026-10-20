"""
M37-05 — 에디터 Play(PIE)에서 판마다 ESC 한 번 실측. `run_pie_check.py` 와 같은 길(사람이 Play 를 누르는 길)로
목장 맵 PIE 를 켜고, 몇 초 뒤 콘솔 명령 `Snare.Test.EscEveryPanel` 을 PIE 월드에 보낸 다음 끄고 에디터를 닫는다.
판정은 게임 코드가 남긴 `[EscLive]` 줄(판마다 한 줄 + 끝 줄 `통과 n · 실패 m`)을 읽는다.

실행 (절대경로, 따옴표 겹치지 않기 — run_pie_check.py 머리 주석 참고):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -RenderOffscreen -nosplash -unattended -SnareOffline -ResX=1280 -ResY=720 \
      -ExecCmds="py ~/UNREAL/Snarehaven/tools/unreal/run_pie_esc_check.py" \
      -abslog=~/UNREAL/Snarehaven/logs/pie_esc.log
"""
import unreal

MAP_PATH = "/Game/Game/Maps/L_Ranch"
# PIE 가 뜨고 캐릭터·HUD 가 붙을 때까지 기다리는 시간(run_pie_check.py 는 12초 둔다).
RUN_AFTER_SECONDS = 10.0
QUIT_AFTER_SECONDS = 14.0

_state = {"elapsed": 0.0, "started": False, "ran": False, "handle": None}


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
        _state["started"] = True
        _log("목장 맵을 열고 에디터 Play 를 요청한다")
        _level_editor().load_level(MAP_PATH)
        _level_editor().editor_request_begin_play()
        return

    if not _state["ran"] and _state["elapsed"] >= RUN_AFTER_SECONDS:
        _state["ran"] = True
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        _log("Snare.Test.EscEveryPanel 보냄 — PIE 월드: {0}".format(world.get_name() if world else "없음"))
        unreal.SystemLibrary.execute_console_command(world, "Snare.Test.EscEveryPanel")
        return

    if _state["elapsed"] >= QUIT_AFTER_SECONDS:
        _finish()


_log("시작 — {0}초 뒤 판마다 ESC, {1}초 뒤 끈다".format(RUN_AFTER_SECONDS, QUIT_AFTER_SECONDS))
_state["handle"] = unreal.register_slate_post_tick_callback(_on_tick)
