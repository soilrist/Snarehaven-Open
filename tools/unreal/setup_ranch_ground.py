"""M18-04 — `L_Ranch` 바닥·연못의 엔진 기본 도형을 우리 메시로 바꾼다 (M15-20 허용 목록 `map:L_Ranch:Plane·Cylinder`).

`make_ranch_ground_meshes.py`(OBJ 원본) 다음에 돌린다:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_ranch_ground.py" -unattended -nosplash -log

액터는 지우지 않고 메시만 갈아 끼운다 — 위치·머티리얼·충돌 설정은 `setup_ranch_stage.py`·`setup_sea.py` 가 정한 그대로.
배율만 고친다 — 바닥 ×50 → ×1(판이 이미 5000cm), 연못 z 0.02 → 1(옛 원기둥은 눌러 두께 2cm, 원판은 두께가 없다). 재실행 안전.
"""
import os

import unreal

GROUND_DIR = "/Game/Game/Environment/Ground"
IMPORT_TEMP = GROUND_DIR + "/_RanchGroundImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
LEVEL = "/Game/Game/Maps/L_Ranch"
# OBJ 이름 → (에셋 이름, 그 메시를 끼울 액터 라벨들)
MESHES = {
    "RanchFloor": ("SM_Ground_RanchFloor_01", ["RanchFloor"]),
    "PondDisc": ("SM_Ground_PondDisc_01", ["Pond_1", "Pond_2"]),
}

assets = unreal.EditorAssetLibrary


def _import():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    tasks = []
    for name in MESHES:
        task = unreal.AssetImportTask()
        task.filename = os.path.abspath(os.path.join(SOURCE_DIR, name + ".obj"))
        if not os.path.exists(task.filename):
            unreal.log_error("[RanchGround] 원본 없음 — make_ranch_ground_meshes.py 먼저: {}".format(task.filename))
            raise SystemExit(1)
        task.destination_path = IMPORT_TEMP
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    made = {}
    for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
        asset = assets.load_asset(path)
        source = asset.get_name()   # 이름을 바꾸면 get_name() 도 바뀐다 — 먼저 잡아 둔다
        if isinstance(asset, unreal.StaticMesh) and source in MESHES:
            target = "{}/{}".format(GROUND_DIR, MESHES[source][0])
            if assets.does_asset_exist(target):
                assets.delete_asset(target)
            assets.rename_asset(path.split(".")[0], target)
            made[source] = target
    assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
    return made


def _finish(path):
    mesh = assets.load_asset(path)
    # 반입기가 Nanite 를 켜 둔다 — 모바일 렌더러는 대체 메시(자동 단순화)를 그려 굽이진 연못이 1440 → 255 삼각형으로
    # 뭉개졌다(M19-02 첫 반입). 이 프로젝트는 Nanite 를 끈다(`import_player_hair.py` 와 같다)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.enabled = False
    mesh.set_editor_property("nanite_settings", nanite)
    # 옛 Plane 은 납작 상자 단순 충돌이었다 — 평판 하나라 복합 충돌을 그대로 쓴다(고원과 같다)
    mesh.get_editor_property("body_setup").set_editor_property(
        "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    box = mesh.get_bounding_box()
    unreal.log("[RanchGround] {} 크기 {:.0f}x{:.0f}x{:.1f} 삼각형 {}".format(
        path, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, mesh.get_num_triangles(0)))
    assets.save_asset(path, only_if_is_dirty=False)
    return mesh


made = _import()
if sorted(made) != sorted(MESHES):
    unreal.log_error("[RanchGround] 반입 실패: {}".format(made))
    raise SystemExit(1)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
by_label = {a.get_actor_label(): a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
for name, (_, labels) in MESHES.items():
    mesh = _finish(made[name])
    for label in labels:
        actor = by_label.get(label)
        if actor is None:
            unreal.log_error("[RanchGround] 액터 없음: {}".format(label))
            raise SystemExit(1)
        comp = actor.static_mesh_component
        material = comp.get_material(0)   # 갈아 끼우면 슬롯 머티리얼이 메시 기본으로 돌아간다 — 먼저 잡아 둔다
        comp.set_static_mesh(mesh)
        comp.set_material(0, material)
        if name == "PondDisc":
            scale = actor.get_actor_scale3d()
            actor.set_actor_scale3d(unreal.Vector(scale.x, scale.y, 1.0))
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)   # `setup_sea.py` _water_mesh 와 같다 — 물은 밟히지 않는다
        else:
            actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))   # 판이 이미 5000cm — 옛 Plane(100cm) 은 ×50
        unreal.log("[RanchGround] {} ← {} (머티리얼 {})".format(label, mesh.get_path_name(), material.get_path_name()))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("[RanchGround] L_Ranch 저장 완료")
