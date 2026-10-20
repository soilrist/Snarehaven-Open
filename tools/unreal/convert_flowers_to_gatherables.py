"""들판 꽃 = 캘 수 있다 — `L_Hunt`·`L_Ranch` 의 장식 들꽃 포기를 모두 지우고, 그 무리 자리에 채집 꽃 더미를 세운다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/convert_flowers_to_gatherables.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

왜: 보이는 꽃 대부분이 캘 수 없는 장식이라 「꽃 보이면 채집해 배표」 길이 없었다 (INBOX 결함 10-13 13:10, DESIGN 8.9 「들판 꽃 = 캘 수 있다」 줄).
`thin_flower_clumps.py` 로 솎은 무리(포기 ≤2)를 무리 중심 CLUMP_RADIUS_CM 로 다시 묶어, 사냥터는 MAX_HUNT_CLUMPS 곳(고정 시드 추첨)·목장은 전부 꽃 더미로 바꾼다.
장식 들꽃 메시는 0 이 된다(자리는 맨 풀 바닥). **꽃을 까는 스크립트를 다시 돌리면 `thin_flower_clumps.py` 뒤에 이것도 다시 돌린다.**
"""
import random

import unreal

MAPS = [("/Game/Game/Maps/L_Ranch", None), ("/Game/Game/Maps/L_Hunt", 300)]
FLOWER_MESH_PREFIXES = ("SM_Nature_WildFlower", "SM_Nature_Flower")
ROW_NAME = "Gatherable_FlowerPatch"
TABLE_PATH = "/Game/Game/Data/Gatherables/DT_GatherableTable"
LABEL = "Gather_Flower_"
CLUMP_RADIUS_CM = 70.0
PICK_SEED = 19004


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def _is_flower(mesh) -> bool:
    return mesh is not None and mesh.get_name().startswith(FLOWER_MESH_PREFIXES)


def _body_scale():
    table = unreal.load_asset(TABLE_PATH)
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    row = names.index(ROW_NAME)
    mesh_path = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "MeshRef")[row]
    height = float(unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "BodyHeightCm")[row])
    import re
    found = re.search(r"(/Game/[^.'\"]+)", mesh_path)
    box = unreal.load_asset(found.group(1)).get_bounding_box()
    return height / float(box.max.z - box.min.z)


def _convert(level_path, cap, scale):
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level_path)
    plants, by_comp, stray = [], {}, []
    for actor in _actors():
        for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if not _is_flower(comp.static_mesh):
                continue
            for index in range(comp.get_instance_count()):
                transform = comp.get_instance_transform(index, True)
                if isinstance(transform, tuple):
                    transform = transform[-1]
                plants.append((round(float(transform.translation.x), 1), round(float(transform.translation.y), 1)))
                by_comp.setdefault(comp, []).append(index)
        if isinstance(actor, unreal.StaticMeshActor) and _is_flower(actor.static_mesh_component.static_mesh):
            where = actor.get_actor_location()
            plants.append((round(float(where.x), 1), round(float(where.y), 1)))
            stray.append(actor)
    plants.sort()
    centers = []
    for x, y in plants:
        if not any(((cx - x) ** 2 + (cy - y) ** 2) ** 0.5 <= CLUMP_RADIUS_CM for cx, cy in centers):
            centers.append((x, y))
    total = len(centers)
    old = sum(1 for a in _actors() if isinstance(a, unreal.GatherableActor)
              and str(a.get_editor_property("gatherable_row_name")) == ROW_NAME and not a.get_actor_label().startswith(LABEL))
    if cap is not None:
        cap -= old          # 이미 선 꽃 더미도 상한 300 에 든다
    rng = random.Random(PICK_SEED)
    if cap is not None and len(centers) > cap:
        rng.shuffle(centers)
        centers = sorted(centers[:cap])
    for comp, indices in by_comp.items():
        comp.remove_instances(sorted(indices, reverse=True))
    for actor in stray:
        actor.destroy_actor()
    for n, (x, y) in enumerate(centers):
        actor = _actors_spawn(x, y, rng.uniform(0.0, 360.0))
        actor.set_actor_label("{}{}".format(LABEL, n + 1))
        actor.set_editor_property("gatherable_row_name", ROW_NAME)
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    left = sum(1 for a in _actors() for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent)
               if _is_flower(c.static_mesh) for _ in range(c.get_instance_count()))
    made = sum(1 for a in _actors() if isinstance(a, unreal.GatherableActor)
               and str(a.get_editor_property("gatherable_row_name")) == ROW_NAME)
    unreal.log_warning("[FlowerGather] {} 포기 {} → 무리 {} → 꽃 더미(옛것 포함) {} · 남은 장식 꽃 포기 {}".format(
        level_path, len(plants), total, made, left))


def _actors_spawn(x, y, yaw):
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        unreal.GatherableActor, unreal.Vector(x, y, 0.0), unreal.Rotator(0.0, 0.0, yaw))


scale = _body_scale()
for path, cap in MAPS:
    _convert(path, cap, scale)
