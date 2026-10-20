"""M15-04 레퍼런스식 나무 메시 반입 — `make_tree_meshes.py`(OBJ 원본) 다음, `setup_zones.py` 앞에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_tree_meshes.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

Saved/Generated/<이름>.obj → /Game/Game/Environment/Nature/SM_Nature_<이름> (집·돌길은 Buildings/SM_Building_<이름>). 슬롯 기본 머티리얼은 여름 잎·껍질
(존마다 색은 `setup_zones.py` 가 슬롯 이름으로 덮어쓴다). 재실행 안전: 같은 이름 에셋을 지우고 다시 만든다.
"""
import os

import unreal

NATURE_DIR = "/Game/Game/Environment/Nature"
IMPORT_TEMP = NATURE_DIR + "/_TreeImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MESHES = ["Pine_05", "Pine_06", "Tree_04", "Birch_01", "Boulder_01", "Boulder_02", "Cottage_01", "CottageRoof_01", "Path_01", "Planter_01", "Pot_01", "Hedge_01", "Fence_02", "WildFlower_01", "WildFlower_02", "BerryBush_01", "BerryBush_02", "BerryBush_03", "BerryBush_04", "BerryBush_05", "BerryBush_06", "BeetPatch_01", "Wheat_01", "MineFrame_01", "ResinPine_01", "Bush_02", "Leaf_01", "Mushroom_02", "SwampMushroom_01", "BogFlower_01", "WaterLily_01"]
# `make_house_meshes.py` 집·돌길은 건물 폴더로 — 슬롯 색은 `setup_ranch_buildings.py` 가 입힌다
TARGETS = {name: "/Game/Game/Environment/Buildings/SM_Building_" + name
           for name in ("Cottage_01", "CottageRoof_01", "Path_01", "Planter_01", "Pot_01", "Hedge_01", "Fence_02")}
# M108-02 집이 된 텐트 오두막(몸통·덮개) — 거점 오두막과 같은 슬롯 색 MI(`setup_ranch_buildings.py` 가 만든 것)를 반입하며 바로 건다.
# 레벨에 놓는 집이 아니라 `ASnarePlacedStructureActor` 가 세우므로 그 스크립트의 배치 목록엔 없다
TENT_HOUSE = ("TentHouse_01", "TentHouseRoof_01", "TentHouseFront_01", "TentHouseUpperRoof_01", "TentHouseUpper_01", "TentHouseFloor2_01")
# M108-06c 2층 집 — 위층 벽(발이 2층 끝에서 떨어지지 않게)·2층 바닥 판은 복잡 충돌(속 빈 벽이라 볼록 껍질이면 방이 막힌다).
# 무엇을 막을지는 액터가 정한다(폰만)
TENT_HOUSE_COMPLEX = ("TentHouseUpper_01", "TentHouseFloor2_01")
MESHES += list(TENT_HOUSE)
TARGETS.update({name: "/Game/Game/Environment/Buildings/SM_Building_" + name for name in TENT_HOUSE})
TENT_HOUSE_SLOTS = {slot: "/Game/Game/Environment/Buildings/MI_Building_Cottage" + slot
                    for slot in ("Wall", "Roof", "Trim", "Door", "Floor", "FloorDark", "Glass")}
# `tools/blender/make_harbor_ships.py` 항구·배 — 슬롯 색·배치·충돌은 `setup_harbor.py` 가 한다
HARBOR = {"Tugboat_01": "Vehicles/Harbor/SM_Vehicle_", "Motorboat_01": "Vehicles/Harbor/SM_Vehicle_",
          "Yacht_01": "Vehicles/Harbor/SM_Vehicle_", "Ship_01": "Vehicles/Harbor/SM_Vehicle_",
          "Quay_01": "Buildings/Harbor/SM_Building_", "Pier_01": "Buildings/Harbor/SM_Building_",
          "Pier_02": "Buildings/Harbor/SM_Building_", "Drum_01": "Props/Harbor/SM_Prop_", "Drum_02": "Props/Harbor/SM_Prop_",
          "Pallet_01": "Props/Harbor/SM_Prop_", "Crates_01": "Props/Harbor/SM_Prop_", "ChainFence_01": "Props/Harbor/SM_Prop_"}
MESHES += list(HARBOR)
TARGETS.update({name: "/Game/Game/Environment/" + folder + name for name, folder in HARBOR.items()})
# `make_island_meshes.py` 사냥터 땅 타일(128m) — 월드 좌표 그대로, 발이 서도록 복잡 충돌. 바닥 머티리얼은 `setup_world_ground.py` 가 입힌다
WORLD_GROUND = sorted(f[:-4] for f in os.listdir(SOURCE_DIR) if f.startswith("WorldGround_") and f.endswith(".obj"))
MESHES += WORLD_GROUND
TARGETS.update({name: "/Game/Game/Environment/Ground/SM_Ground_World_" + name[len("WorldGround_"):] for name in WORLD_GROUND})
# `SNARE_MESHES=Cottage_01,CottageRoof_01` 처럼 주면 그것만 다시 반입한다 — 나머지 에셋이 괜히 다시 저장되지 않게
if os.environ.get("SNARE_MESHES"):
    MESHES = [name for name in MESHES if name in os.environ["SNARE_MESHES"].split(",")]
SLOT_DEFAULTS = {"Leaves_Pine": "MI_Nature_Leafs_Pine", "Leaves_NormalTree": "MI_Nature_Leafs_Broad",
                 "Bark_NormalTree": "MI_Nature_Leafs_Bark", "Bark_Birch": "MI_Nature_Leafs_BirchBark",
                 "Rock": "MI_Nature_Leafs_Rock",
                 # 베리 덤불 — 채집 더미는 메시 기본 머티리얼로 그려진다. 열매 MI 는 `setup_zones.py` MATERIALS
                 "Leaves_Bush": "MI_Nature_Leafs_Bush", "BerryOrange": "MI_Nature_Leafs_FlowerOrange", "BerryDark": "MI_Nature_Leafs_BerryDark", "BerryRed": "MI_Nature_Leafs_BerryRed", "BerryBlue": "MI_Nature_Leafs_FlowerBlue", "BerryPink": "MI_Nature_Leafs_BerryPink", "BerryMezereon": "MI_Nature_Leafs_Mezereon",
                 "Beet": "MI_Nature_Leafs_Beet", "Stalk": "MI_Nature_Leafs_Gold", "Grain": "MI_Nature_Leafs_Wheat", "Resin": "MI_Nature_Leafs_Resin", "MineWood": "MI_Nature_Leafs_Bark", "Leaf": "MI_Nature_Leafs_Red",
                 "Cap": "MI_Nature_Leafs_Chanterelle", "Gill": "MI_Nature_Leafs_Resin", "Stem": "MI_Nature_Leafs_Bark",
                 "SwampWater": "/Game/Game/Environment/Ground/MI_Ground_SwampWater",   # 수련 물 웅덩이 — 늪 물웅덩이와 같은 물
                 # 꽃 슬롯 — 채집 꽃 더미(표 `MeshRef`)는 메시 기본 머티리얼로 그려진다. 머티리얼은 `setup_ranch_buildings.py` 가 만든다
                 **{slot: "/Game/Game/Environment/Buildings/MI_Building_Flower" + slot for slot in ("Leaf", "Red", "Yellow", "White", "Purple", "Center")}}

assets = unreal.EditorAssetLibrary

tasks = []
for name in MESHES:
    task = unreal.AssetImportTask()
    task.filename = os.path.abspath(os.path.join(SOURCE_DIR, name + ".obj"))
    task.destination_path = IMPORT_TEMP
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
    mesh = assets.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh):
        continue
    target = TARGETS.get(mesh.get_name(), "{}/SM_Nature_{}".format(NATURE_DIR, mesh.get_name()))
    if assets.does_asset_exist(target):
        assets.delete_asset(target)
    assets.rename_asset(path.split(".")[0], target)
    mesh = assets.load_asset(target)
    slots = TENT_HOUSE_SLOTS if mesh.get_name()[len("SM_Building_"):] in TENT_HOUSE else SLOT_DEFAULTS
    for index, slot in enumerate(mesh.static_materials):
        mi = slots.get(str(slot.material_slot_name))
        if mi:
            mesh.set_material(index, assets.load_asset(mi if mi.startswith("/") else "{}/{}".format(NATURE_DIR, mi)))
    # 반입기가 Nanite 를 켜 둔다 — 모바일 렌더러는 Nanite 대체 메시(자동 단순화)를 그려서 섬 땅이 5442→354 삼각형으로 뭉개져
    # 모래 원판 위로 초록 쐐기가 솟았다. 이 프로젝트는 Nanite 를 끈다(DESIGN 4.2, `import_nature_assets.py` 와 같다)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.enabled = False
    mesh.set_editor_property("nanite_settings", nanite)
    short = mesh.get_name()[len("SM_Building_"):]
    if mesh.get_name().startswith("SM_Ground_") or short in TENT_HOUSE_COMPLEX:
        mesh.get_editor_property("body_setup").set_editor_property(
            "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    assets.save_asset(target, only_if_is_dirty=False)
    box = mesh.get_bounding_box()
    unreal.log("반입: {} 슬롯 {} 높이 {:.0f} 폭 {:.0f} 범위 x {:.0f}~{:.0f} y {:.0f}~{:.0f}".format(
        target, [str(s.material_slot_name) for s in mesh.static_materials], box.max.z - box.min.z, box.max.x - box.min.x,
        box.min.x, box.max.x, box.min.y, box.max.y))
assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
