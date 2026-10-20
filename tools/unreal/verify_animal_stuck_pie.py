"""동물 바위 끼임 검사 (INBOX 지시 10-13 「동물 바위 끼임」) — PIE 로 L_Hunt 를 돌려 배회 동물이 걷는다면서 제자리인 수를 센다.

실행 (맵을 명령줄 첫 인자로, 경로 전부 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor ~/UNREAL/Snarehaven/Snarehaven.uproject /Game/Game/Maps/L_Hunt \
      -RenderOffscreen -nosplash -unattended -ResX=640 -ResY=360 \
      -ExecCmds="py ~/UNREAL/Snarehaven/tools/unreal/verify_animal_stuck_pie.py" \
      -abslog=~/UNREAL/Snarehaven/logs/animal_stuck.log
RUN_SECONDS 동안 PIE 를 돌리며 WINDOW_SECONDS 창마다 동물별로 배회로 걷던 시간과 그동안 간 거리를 잰다.
걷던 시간이 MIN_WALK_SECONDS 이상인데 간 거리가 기대의 STUCK_RATIO 미만이면 끼임 1건. 로그 「끼임 검사」 줄을 읽는다.
"""
import unreal

RUN_SECONDS = 180.0
WINDOW_SECONDS = 10.0
MIN_WALK_SECONDS = 2.0   # 창 안에서 이만큼은 걸어야 판정 (배회는 한 번에 1.5~4초만 걷는다)
STUCK_RATIO = 0.25       # 걸은 시간 × 배회 속도(최대 속도 × 0.5) 의 이 비율 미만만 갔으면 끼임
_state = {"elapsed": 0.0, "started": False, "done": False, "handle": None,
          "window": 0.0, "last": {}, "walk_t": {}, "walk_d": {}, "stuck": [], "windows": 0, "walking_windows": 0}


def _animals(world):
    return unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AnimalCharacter)


def _sample(world, dt):
    for a in _animals(world):
        name = a.get_name()
        loc = a.get_actor_location()
        old = _state["last"].get(name)
        _state["last"][name] = loc
        if old is None or not a.is_wander_walking():
            continue
        moved = ((loc.x - old.x) ** 2 + (loc.y - old.y) ** 2) ** 0.5
        if moved > 200.0:  # 순간이동(재등장)은 거리에서 뺀다
            continue
        _state["walk_t"][name] = _state["walk_t"].get(name, 0.0) + dt
        _state["walk_d"][name] = _state["walk_d"].get(name, 0.0) + moved


def _close_window(world):
    for a in _animals(world):
        name = a.get_name()
        _state["windows"] += 1
        walk_t = _state["walk_t"].get(name, 0.0)
        if walk_t < MIN_WALK_SECONDS:
            continue
        _state["walking_windows"] += 1
        speed = a.character_movement.max_walk_speed * 0.5
        want = speed * walk_t
        got = _state["walk_d"].get(name, 0.0)
        if got < want * STUCK_RATIO:
            loc = a.get_actor_location()
            _state["stuck"].append("{}@({:.0f},{:.0f}) 걸은 {:.1f}초 · 간 {:.0f}cm / 기대 {:.0f}cm".format(
                name, loc.x, loc.y, walk_t, got, want))
    _state["walk_t"], _state["walk_d"] = {}, {}


def _on_tick(delta_seconds):
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    _state["elapsed"] += delta_seconds
    if not _state["started"]:
        _state["started"] = True
        editor.editor_request_begin_play()
        _state["elapsed"] = 0.0
        return
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if world is None or _state["done"]:
        return
    _sample(world, delta_seconds)
    _state["window"] += delta_seconds
    if _state["window"] >= WINDOW_SECONDS:
        _state["window"] = 0.0
        _close_window(world)
    if _state["elapsed"] >= RUN_SECONDS:
        _state["done"] = True
        unreal.log_warning("끼임 검사: {} — 동물 {} 마리 · 창 {} 개 · 걷던 창 {} 개 · 끼임 {} (0 이어야) {}".format(
            world.get_name(), len(_animals(world)), _state["windows"], _state["walking_windows"],
            len(_state["stuck"]), " ".join(_state["stuck"][:10])))
        editor.editor_request_end_play()
        unreal.unregister_slate_post_tick_callback(_state["handle"])
        unreal.SystemLibrary.quit_editor()


_state["handle"] = unreal.register_slate_post_tick_callback(_on_tick)
