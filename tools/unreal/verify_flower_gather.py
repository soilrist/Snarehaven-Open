"""들판 꽃 = 캘 수 있다 — 2층 검사. **새 프로세스**에서 두 맵을 열어 (1) 장식 들꽃 메시 0 (2) 사냥터 꽃 더미 ≤300 (3) 목장 꽃 더미 ≥1 (4) 꽃 더미 Z 가 땅과 ≤10cm 차 (5) 바위·나무·건물과 겹침 0 을 센다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_flower_gather.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log
"""
import sys

sys.path.insert(0, "~/UNREAL/Snarehaven/tools/unreal")
import unreal  # noqa: E402
import flower_ground as fg  # noqa: E402

ROW_NAME = "Gatherable_FlowerPatch"
CAP = 300
PREFIXES = ("SM_Nature_WildFlower", "SM_Nature_Flower")
EXPECT = [("/Game/Game/Maps/L_Ranch", 1, CAP), ("/Game/Game/Maps/L_Hunt", 1, CAP)]

fail = 0
for path, low, high in EXPECT:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path)
    decor, piles = 0, 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if comp.static_mesh is not None and comp.static_mesh.get_name().startswith(PREFIXES):
                decor += comp.get_instance_count()
        if isinstance(actor, unreal.StaticMeshActor) and actor.static_mesh_component.static_mesh is not None \
                and actor.static_mesh_component.static_mesh.get_name().startswith(PREFIXES):
            decor += 1
        if isinstance(actor, unreal.GatherableActor) and str(actor.get_editor_property("gatherable_row_name")) == ROW_NAME:
            piles += 1
    grounds, obstacles = fg.collect()
    road = fg.path_discs()
    sunk, overlapped, onroad = 0, 0, 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(actor, unreal.GatherableActor) and str(actor.get_editor_property("gatherable_row_name")) == ROW_NAME:
            where = actor.get_actor_location()
            ground = fg.ground_z(grounds, where.x, where.y)
            sunk += 1 if ground is None or abs(where.z - ground) > 10.0 else 0
            overlapped += 1 if fg.overlaps(obstacles, where.x, where.y) else 0
            onroad += 1 if fg.on_path(road, where.x, where.y) else 0
    ok = decor == 0 and low <= piles <= high and sunk == 0 and overlapped == 0 and onroad == 0
    unreal.log_warning("{} {} — 장식 들꽃 {} (0 이어야) · 꽃 더미 {} ({}~{}) · 땅과 Z 차 >10cm {} (0 이어야) · 바위·나무 겹침 {} (0 이어야) · 흙길 위 {} (0 이어야) · 길 원판 {}".format(
        "통과" if ok else "실패", path, decor, piles, low, high, sunk, overlapped, onroad, len(road)))
    fail += 0 if ok else 1
if fail:
    unreal.log_error("꽃 채집 검사 실패 {}건".format(fail))
    raise SystemExit(1)
unreal.log_warning("꽃 채집 검사 통과 — 맵 {}개".format(len(EXPECT)))
