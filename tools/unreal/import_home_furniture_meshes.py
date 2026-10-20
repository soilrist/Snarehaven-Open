"""M108-04 집 가구 메시 반입 — `make_home_furniture_meshes.py`(OBJ 원본) 다음, `create_data_tables.py` 앞에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_home_furniture_meshes.py" \
      -unattended -nosplash -nullrhi (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

Saved/Generated/<이름>_01.obj → /Game/Game/Environment/Furniture/Home/SM_Furn_<이름>_01. `import_top_decor_meshes.py` 와 같은 흐름
(반입 → 이름 바꿈 → Nanite 끔 → `decor_palette.py` 팔레트 그림 한 장 + `MI_Prop_HomeFurn_Palette` 하나 — 섹션 1, DESIGN 8.11 드로콜 줄).
재실행 안전: 같은 이름 에셋을 지우고 다시 만든다. 로그 `반입:` 줄이 크기·섹션·삼각형 증거다.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decor_palette  # noqa: E402

FURN_DIR = "/Game/Game/Environment/Furniture/Home"
IMPORT_TEMP = FURN_DIR + "/_HomeFurnImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MESHES = {name: "SM_Furn_" + name for name in ("ArmChair_01", "Sofa_01", "TableRound_01", "TableLong_01", "BedSingle_01",
                                               "BedDouble_01", "Bookshelf_01", "Dresser_01", "KitchenCounter_01", "KitchenSink_01",
                                               "PottedTree_01", "PottedBerry_01", "FlowerBox_01", "PottedSprout_01",
                                               "RugRound_01", "RugRunner_01", "RugSquare_01", "Doormat_01",
                                               "FencePicket_01", "PathLog_01", "GardenArch_01", "Mailbox_01",
                                               "CandleStand_01", "OilLamp_01", "LanternPost_01", "GardenTorch_01",
                                               "StairsWood_01", "Generator_01", "Fridge_01", "FanStand_01",
                                               "Fridge_On_01", "FanStand_On_01", "FanBlade_01")}
# 슬롯 색(리니어) — 꿀색 나무·짙은 나무(다리·테)·밝은 판자, 세이지 초록 천(우리 새싹 색), 크림 방석·요, 꿀색 베개(머그 띠와 같은 색),
# 흰 베개, 새싹 잎(머그·화분과 같은 색). M108-04b(루프#1386): 딸기색 책등·화분, 회색 돌 윗판, 쇠(손잡이·개수통·수도꼭지)·짙은 쇠(개수통 바닥)
# — 칸은 뒤에 붙여 앞 여덟 칸 자리가 그대로다(앞 다섯 메시 UV 안 바뀜). M108-05a(루프#1388): 밝은 잎(덩어리 결)·꽃잎 빨강·테라코타
# — 또 뒤에 붙인다. M108-05e(루프#1392): 불꽃 노랑(조명 넷의 안 불 — 밤 점광이 비추는 자리) — 맨 뒤.
# M108-08b-2(루프#1411): 전기 가구 켜짐 불 칸 밝은 초록 `Lit`(냉장고·선풍기 `_On_01`) — 또 맨 뒤.
# 루프#1412(감독 보정 10-20 13:55): 선풍기 날개 하늘색 `Sky`(초록 잎이 새싹으로 읽혀) — 맨 뒤
SLOT_COLORS = {"Wood": (0.45, 0.20, 0.06), "WoodDark": (0.18, 0.07, 0.02), "WoodLight": (0.62, 0.32, 0.11),
               "Fabric": (0.26, 0.40, 0.19), "Cushion": (0.80, 0.72, 0.55), "Accent": (0.95, 0.55, 0.05),
               "Pillow": (0.92, 0.90, 0.85), "Leaf": (0.13, 0.45, 0.08), "Berry": (0.40, 0.08, 0.06),
               "Stone": (0.42, 0.42, 0.40), "Metal": (0.55, 0.57, 0.60), "MetalDark": (0.12, 0.13, 0.14),
               "LeafLight": (0.30, 0.62, 0.10), "Petal": (0.80, 0.08, 0.07), "Clay": (0.62, 0.17, 0.06),
               "Flame": (1.0, 0.78, 0.18), "Lit": (0.30, 1.0, 0.22), "Sky": (0.42, 0.70, 0.92)}

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


palette = decor_palette.palette_material(FURN_DIR, "HomeFurn", SLOT_COLORS)
tasks = []
# `IMPORT_ONLY=<원본 이름>,…`(M108-08b-2) 면 그 메시만 다시 반입한다 — 안 바뀐 메시를 다시 저장해 커밋을 어지럽히지 않게. 팔레트는 늘 다시 만든다
_only = [n for n in os.environ.get("IMPORT_ONLY", "").split(",") if n]
for name in MESHES:
    if _only and name not in _only:
        continue
    task = unreal.AssetImportTask()
    task.filename = decor_palette.palette_obj(os.path.abspath(os.path.join(SOURCE_DIR, name + ".obj")), SLOT_COLORS)
    task.destination_path = IMPORT_TEMP
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)
tools.import_asset_tasks(tasks)
for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
    mesh = assets.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh) or mesh.get_name() not in MESHES:
        continue
    target = "{}/{}".format(FURN_DIR, MESHES[mesh.get_name()])
    if assets.does_asset_exist(target):
        assets.delete_asset(target)
    assets.rename_asset(path.split(".")[0], target)
    mesh = assets.load_asset(target)
    if len(mesh.static_materials) != 1:
        unreal.log_error("슬롯이 하나가 아니다: {} {}".format(target, [str(m.material_slot_name) for m in mesh.static_materials]))
    mesh.set_material(0, palette)
    # 반입기가 Nanite 를 켜 둔다 — 이 프로젝트는 끈다(DESIGN 4.2, `import_tree_meshes.py` 와 같다)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.enabled = False
    mesh.set_editor_property("nanite_settings", nanite)
    assets.save_asset(target, only_if_is_dirty=False)
    box = mesh.get_bounding_box()
    unreal.log_warning("반입: {} 슬롯 {} 섹션 {} 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} · 밑 z {:.1f}".format(
        target, [str(s.material_slot_name) for s in mesh.static_materials], mesh.get_num_sections(0), mesh.get_num_triangles(0),
        box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, box.min.z))
assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
