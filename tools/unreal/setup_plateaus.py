"""M15-04 고원 — 풀 덮인 둔덕을 회색 바위 절벽으로 끊어 레퍼런스 섬의 층진 땅(steam_17·steam_22)을 만든다.

`make_plateau_meshes.py`(OBJ 원본) → `setup_ground_strokes.py` → `setup_zones.py` 다음에 돌린다
(`setup_zones.py` 가 존 소품을 새로 뿌리면 고원 자리 소품이 되살아나니 이 스크립트를 다시 돌린다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_plateaus.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

고원은 막힌 장애물이다(윗면으로 오르는 길 없음 — 레퍼런스도 절벽은 못 오른다). 자리에 있던 존 소품은 지우고 채집물은
가장자리 밖으로 밀어내며, 윗면에 나무 몇 그루를 올린다. 자리·크기는 전부 배치값(🟡) — 스폰·포털·강·목장 묶음을 피했다.
재실행 안전: `Plateau_` 액터를 지우고 다시 만든다.
"""
import math
import os
import random

import unreal

GROUND_DIR = "/Game/Game/Environment/Ground"
NATURE_DIR = "/Game/Game/Environment/Nature"
IMPORT_TEMP = GROUND_DIR + "/_PlateauImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MESHES = ["Plateau_01", "Plateau_02", "Plateau_03"]
SOURCE_RADIUS_CM = 500.0   # `make_plateau_meshes.py` SHAPES 반지름
HEIGHT_CM = 300.0          # 같은 파일 HEIGHT_CM
FOOTPRINT = 1.3            # 윤곽 물결 최대 ≈ 반지름 1.3배 — 이 안의 소품·채집물을 치운다
CLIFF = GROUND_DIR + "/MI_Ground_Cliff"
CLIFF_COLOR = unreal.LinearColor(0.27, 0.223, 0.216, 1.0)  # M19-04: sRGB (142,130,128) 채도 0.10 — 원본 절벽 실측 양지 (182,152,144)·(160,136,136) 은 채도 0.15~0.21 장밋빛 회색이라 색상(장밋빛)은 두고 채도만 회색 쪽으로(루프#277 대조 "한 판 회분홍"). 시트 `#8C8378` 는 갈색이라 안 씀. 이전 (0.26,0.18,0.19) sRGB 채도 0.15 · 그 전: steam_05·17 절벽 판 양지 (154~180,124~151,126~150)·그늘 (106,92,96) 장밋빛 회색 — (0.34,0.28,0.25) 는 (185,170,151) 크림색이었다(w37). 이전: 0.15 는 어두운 회갈(w31), (0.13,0.12,0.105) 는 황토색(w13)
GRASS_TOP = GROUND_DIR + "/MI_Ground_PlainsStrokes"       # 바닥과 같은 월드 좌표 획 — 윗면이 바닥과 같은 풀
TOP_BY_PROPS = {"ZoneProps_Forest": GROUND_DIR + "/MI_Ground_ForestStrokes",   # 존 바닥이 풀이 아닌 칸 — 가을섬 고원 윗면이 초록으로 떴다(w39)
                "ZoneProps_Snow": GROUND_DIR + "/MI_Ground_SnowStrokes"}
CLIFF_SNOW = GROUND_DIR + "/MI_Ground_CliffSnow"
CLIFF_SNOW_COLOR = unreal.LinearColor(0.21, 0.23, 0.40, 1.0)   # steam_18 눈 고원 절벽 (127~157,133~165,167~188) 푸른 회색
MATTE_PARENT = NATURE_DIR + "/M_Nature_Matte"   # `setup_zones.py` 가 만드는 무광 부모 — 존 소품과 같은 결
SEED = 1504
BASE_ROCK = "SM_Nature_Boulder_01"

# 레벨 → [(x, y, 반지름 cm, 존 소품 액터 라벨, 윗면 나무 메시)]
LEVELS = {
    # L_Hunt 는 하벤 만 지도 땅(`setup_world_ground.py`, 2026-09-25)이 대신한다 — 옛 작은 섬 판을 다시 깔지 않는다.
    "/Game/Game/Maps/L_Ranch": [
        # 목장 묶음(x -300~1350, y -1100~1100)·포털 길·연못(±1700) 을 피한다
        (-1650.0, -700.0, 550.0, "ZoneProps_Ranch", "SM_Nature_Pine_05"),
        (1850.0, 1650.0, 450.0, "ZoneProps_Ranch", "SM_Nature_Pine_06"),
        (-700.0, 1900.0, 480.0, "ZoneProps_Ranch", "SM_Nature_Tree_04"),
    ],
}

assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _import_meshes():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    tasks = []
    for name in MESHES:
        task = unreal.AssetImportTask()
        task.filename = os.path.abspath(os.path.join(SOURCE_DIR, name + ".obj"))
        task.destination_path = IMPORT_TEMP
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    meshes = []
    for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
        asset = assets.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            target = "{}/SM_Ground_{}".format(GROUND_DIR, asset.get_name())
            if assets.does_asset_exist(target):
                assets.delete_asset(target)
            assets.rename_asset(path.split(".")[0], target)
            meshes.append(target)
    assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
    return sorted(meshes)


def _cliff_material(path=CLIFF, color=CLIFF_COLOR):
    if not assets.does_asset_exist(path):
        folder, name = path.rsplit("/", 1)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = assets.load_asset(path)
    lib = unreal.MaterialEditingLibrary
    lib.set_material_instance_parent(mi, assets.load_asset(MATTE_PARENT))
    lib.set_material_instance_vector_parameter_value(mi, "DiffuseColor", color)
    assets.save_asset(path, only_if_is_dirty=False)
    return mi


def _finish_mesh(path, cliff, grass):
    mesh = assets.load_asset(path)
    for index, slot in enumerate(mesh.static_materials):
        mesh.set_material(index, grass if "Grass" in str(slot.material_slot_name) else cliff)
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    box = mesh.get_bounding_box()
    unreal.log("[Plateau] {} 크기 {:.0f}x{:.0f}x{:.0f} 삼각형 {} 슬롯 {}".format(
        path, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, mesh.get_num_triangles(0),
        [str(s.material_slot_name) for s in mesh.static_materials]))
    assets.save_asset(path, only_if_is_dirty=False)
    return mesh


def _props_actor(label):
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    raise SystemExit("[Plateau] 존 소품 액터 없음: {} — setup_zones.py 를 먼저 돌린다".format(label))


def _clear_and_top(props, x, y, reach, top_mesh, rng, radius):
    removed = 0
    top_comp = rock_comp = spare_tree = None
    for comp in props.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
        for i in reversed(range(comp.get_instance_count())):
            spot = comp.get_instance_transform(i, True).translation
            if math.hypot(spot.x - x, spot.y - y) < reach:
                comp.remove_instance(i)
                removed += 1
        if comp.static_mesh and comp.static_mesh.get_name() == top_mesh and comp.get_instance_count() > 0:
            top_comp = comp
        if comp.static_mesh and comp.static_mesh.get_name().startswith(("SM_Nature_Pine", "SM_Nature_Tree")) \
                and comp.get_instance_count() > 0:
            spare_tree = comp
        if comp.static_mesh and comp.static_mesh.get_name() == BASE_ROCK and comp.get_instance_count() > 0:
            rock_comp = comp
    added = 0
    top_comp = top_comp or spare_tree   # 존이 그 나무를 안 심었으면(해변·구역 제외로 0그루) 다른 나무로 윗면을 채운다
    if top_comp is not None:
        sample = top_comp.get_instance_transform(0, True).scale3d
        for _ in range(2 + int(radius // 250)):
            angle, dist = rng.uniform(0, math.tau), rng.uniform(0.0, radius * 0.55)
            top_comp.add_instance(unreal.Transform(
                unreal.Vector(x + dist * math.cos(angle), y + dist * math.sin(angle), HEIGHT_CM),
                unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)), sample * rng.uniform(0.85, 1.15)), True)
            added += 1
    if rock_comp is not None:
        # steam_17 — 절벽 발치에 회색 바위가 몇 개씩 붙는다
        sample = rock_comp.get_instance_transform(0, True).scale3d
        for _ in range(3 + int(radius // 300)):
            angle = rng.uniform(0, math.tau)
            dist = radius * rng.uniform(1.0, 1.15)
            rock_comp.add_instance(unreal.Transform(
                unreal.Vector(x + dist * math.cos(angle), y + dist * math.sin(angle), 0.0),
                unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)), sample * rng.uniform(0.8, 1.4)), True)
    return removed, added


def _push_gatherables(x, y, reach):
    moved = 0
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, unreal.GatherableActor):
            continue
        spot = actor.get_actor_location()
        dist = math.hypot(spot.x - x, spot.y - y)
        if dist < reach:
            ux, uy = ((spot.x - x) / dist, (spot.y - y) / dist) if dist > 1.0 else (1.0, 0.0)
            actor.set_actor_location(unreal.Vector(x + ux * (reach + 150.0), y + uy * (reach + 150.0), spot.z),
                                     False, False)
            moved += 1
    return moved


def _place(level, spots, mesh_paths):
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level)
    meshes = [assets.load_asset(p) for p in mesh_paths]   # 레벨을 열면 앞서 쥔 에셋 참조가 풀린다 — 다시 읽는다
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith("Plateau_"):
            actors.destroy_actor(actor)
    rng = random.Random("{}:{}".format(SEED, level))
    for n, (x, y, radius, props_label, top_mesh) in enumerate(spots):
        scale = radius / SOURCE_RADIUS_CM
        # spawn_actor_from_object 는 커맨드릿에서 "No actor was spawned" 로 None 을 준다 — 빈 StaticMeshActor 에 메시를 끼운다
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, 0.0),
                                              unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
        actor.static_mesh_component.set_static_mesh(meshes[n % len(meshes)])
        actor.set_actor_label("Plateau_{}".format(n + 1))
        actor.set_actor_scale3d(unreal.Vector(scale, scale, 1.0))
        if props_label in TOP_BY_PROPS:
            actor.static_mesh_component.set_material_by_name("Grass", assets.load_asset(TOP_BY_PROPS[props_label]))
        if props_label == "ZoneProps_Snow":
            actor.static_mesh_component.set_material_by_name("Cliff", assets.load_asset(CLIFF_SNOW))
        reach = radius * FOOTPRINT
        removed, added = _clear_and_top(_props_actor(props_label), x, y, reach, top_mesh, rng, radius)
        moved = _push_gatherables(x, y, reach)
        unreal.log("[Plateau] {} Plateau_{} ({:.0f},{:.0f}) 반지름 {:.0f} — 소품 {} 지움, 윗면 나무 {}, 채집물 {} 밀어냄".format(
            level, n + 1, x, y, radius, removed, added, moved))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


if __name__ == "__main__":   # `setup_start_cliff.py` 가 `_cliff_material` 만 빌려 쓴다
    mesh_paths = _import_meshes()
    if len(mesh_paths) != len(MESHES):
        raise SystemExit("[Plateau] 메시 {}개가 아니라 {}개 임포트됨: {}".format(len(MESHES), len(mesh_paths), mesh_paths))
    cliff_mi = _cliff_material()
    _cliff_material(CLIFF_SNOW, CLIFF_SNOW_COLOR)
    grass_mi = assets.load_asset(GRASS_TOP)
    for mesh_path in mesh_paths:
        _finish_mesh(mesh_path, cliff_mi, grass_mi)
    for level_path, level_spots in LEVELS.items():
        _place(level_path, level_spots, mesh_paths)
