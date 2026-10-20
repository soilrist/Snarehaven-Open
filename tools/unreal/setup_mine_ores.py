"""DESIGN 8.16 광산 광맥만 다시 놓는다 — `setup_zones.py` 를 다시 돌리면 `Gather_` 라벨을 전부 지워 광맥(`Gather_Mine_*`)도 사라진다
(10-19 감독 확인: 맵에 `ore_basic` 0개). 굴·문은 건드리지 않고 `Gather_Mine_` 만 지웠다가 다시 놓는다. 재실행 안전.
좌표·행은 `setup_mine.py` 의 `MINE`·`ORES` 와 같다(바꾸면 둘 다).

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/setup_mine_ores.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)
"""
import random
import re

import unreal

GATHER_TABLE = "/Game/Game/Data/Gatherables/DT_GatherableTable"
MINE = (70000.0, 0.0)
SEED = 816
ORES = {"Gatherable_OreBasic": [(2150.0, -600.0), (2300.0, 650.0), (2750.0, -150.0), (3150.0, -650.0), (3250.0, 500.0), (2600.0, 750.0)],
        "Gatherable_OreAdvanced": [(2150.0, 2450.0), (2350.0, 3000.0), (3100.0, 2400.0), (3250.0, 3000.0)]}

subs = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.load_map("/Game/Game/Maps/L_Hunt")
for old in subs.get_all_level_actors():
    if old.get_actor_label().startswith("Gather_Mine_"):
        subs.destroy_actor(old)

table = unreal.EditorAssetLibrary.load_asset(GATHER_TABLE)
names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
meshes = dict(zip(names, unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "MeshRef")))
heights = dict(zip(names, unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "BodyHeightCm")))
rng = random.Random(SEED)
placed = 0
for row, spots in ORES.items():
    mesh = unreal.load_asset(re.search(r"(/Game/[^.'\"]+)", meshes[row]).group(1))
    box = mesh.get_bounding_box()
    scale = float(heights[row]) / float(box.max.z - box.min.z)   # `setup_zones.py` 채집 더미와 같은 배율
    for n, (x, y) in enumerate(spots):
        actor = subs.spawn_actor_from_class(unreal.GatherableActor, unreal.Vector(MINE[0] + x, MINE[1] + y, 0.0),
                                            unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
        actor.set_actor_label("Gather_Mine_{}_{}".format(row.split("_", 1)[1], n + 1))
        actor.set_editor_property("gatherable_row_name", row)
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        placed += 1

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
counts = {}
for a in subs.get_all_level_actors():
    if a.get_actor_label().startswith("Gather_Mine_"):
        row = str(a.get_editor_property("gatherable_row_name"))
        counts[row] = counts.get(row, 0) + 1
unreal.log_warning("[MineOres] 놓음 {} · 저장 뒤 확인 {}".format(placed, counts))
