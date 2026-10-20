"""바위·나무·집·울타리가 몸을 막는지 PIE 에서 잰다 (INBOX 결함 10-13 「돌 안으로 들어가버린다」) — 커맨드릿 월드엔 물리 장면이 없어 쓸기가 늘 빈손이라 PIE 로 한다.

실행 (맵을 명령줄 첫 인자로, 경로 전부 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor ~/UNREAL/Snarehaven/Snarehaven.uproject /Game/Game/Maps/L_Hunt \
      -RenderOffscreen -nosplash -unattended -ResX=640 -ResY=360 \
      -ExecCmds="py ~/UNREAL/Snarehaven/tools/unreal/verify_rock_collision_pie.py" \
      -abslog=~/UNREAL/Snarehaven/logs/<이름>.log
Play 를 켜고 WAIT_SECONDS 뒤 바위 표본마다 바깥에서 중심으로 플레이어 캡슐 쓸기를 쏴 막히는지 센다. 로그 「쓸기 검사」 줄(갈래별)을 읽는다.
"""
import unreal

# 갈래 이름 → 메시 이름 앞부분 (INBOX 보정 10-13: 바위 말고 나무·집·목장 건물·울타리도 같은 쓸기로 뚫림 0 단정)
CATEGORIES = {
    "바위": ("SM_Nature_Boulder", "SM_Nature_Rock"),
    "나무": ("SM_Nature_Pine", "SM_Nature_Tree"),
    "집": ("SM_Building_Cottage_", "SM_Building_Windmill", "SM_Building_Silo", "SM_Building_Well"),
    "목장 건물": ("SM_Building_Barn", "SM_Building_Coop"),
    "울타리": ("SM_Building_Fence", "SM_Building_Hedge"),
}
CAPSULE_RADIUS, CAPSULE_HALF = 34.0, 88.0   # 플레이어 캡슐
SAMPLE = 400
WAIT_SECONDS = 6.0
_state = {"elapsed": 0.0, "started": False, "done": False, "handle": None}


def _collect(world):
    found = {name: [] for name in CATEGORIES}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = comp.static_mesh
            if mesh is None:
                continue
            mesh_name = mesh.get_name()
            for name, prefixes in CATEGORIES.items():
                if not mesh_name.startswith(prefixes):
                    continue
                ext = mesh.get_bounds().box_extent
                if isinstance(comp, unreal.InstancedStaticMeshComponent):
                    for i in range(comp.get_instance_count()):
                        t = comp.get_instance_transform(i, True)
                        found[name].append((t.translation, t.scale3d, ext, mesh_name))
                else:
                    t = comp.get_world_transform()
                    found[name].append((t.translation, t.scale3d, ext, mesh_name))
                break
    return found


def _measure():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_game_world()
    for name, items in _collect(world).items():
        step = max(1, len(items) // SAMPLE)
        tested, blocked, passed = 0, 0, []
        for loc, scale, ext, mesh_name in items[::step]:
            far = max(ext.x * scale.x, ext.y * scale.y) + 150.0
            z = loc.z + 60.0
            hit = unreal.SystemLibrary.capsule_trace_single(
                world, unreal.Vector(loc.x + far, loc.y, z), unreal.Vector(loc.x, loc.y, z), CAPSULE_RADIUS, CAPSULE_HALF,
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
            tested += 1
            if hit:
                blocked += 1
            elif len(passed) < 5:
                passed.append("{}@({:.0f},{:.0f})".format(mesh_name, loc.x, loc.y))
        unreal.log_warning("{} 쓸기 검사: {} — {} {} 개 중 표본 {} · 막힘 {} · 뚫림 {} (0 이어야) {}".format(
            name, world.get_name(), name, len(items), tested, blocked, tested - blocked, " ".join(passed)))


def _on_tick(delta_seconds):
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    _state["elapsed"] += delta_seconds
    if not _state["started"]:
        _state["started"] = True
        editor.editor_request_begin_play()
        _state["elapsed"] = 0.0
        return
    if _state["elapsed"] >= WAIT_SECONDS and not _state["done"]:
        _state["done"] = True
        try:
            _measure()
        except Exception as error:  # 로그에 남기고 끈다
            unreal.log_error("쓸기 검사 예외: {}".format(error))
        editor.editor_request_end_play()
        unreal.unregister_slate_post_tick_callback(_state["handle"])
        unreal.SystemLibrary.quit_editor()


_state["handle"] = unreal.register_slate_post_tick_callback(_on_tick)
