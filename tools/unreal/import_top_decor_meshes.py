"""M108-03 위에 올리는 소품 메시 반입 — `make_top_decor_meshes.py`(OBJ 원본) 다음, `create_data_tables.py` 앞에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_top_decor_meshes.py" \
      -unattended -nosplash -nullrhi (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

Saved/Generated/Mug_01.obj · FlowerPot_01.obj · Candle_01.obj → /Game/Game/Environment/Props/TableDecor/SM_Prop_Mug_01 ·
SM_Prop_FlowerPot_01 · SM_Prop_Candle_01. `import_wall_decor_meshes.py` 와 같은 흐름(반입 → 이름 바꿈 → Nanite 끔 → `decor_palette.py` 팔레트
그림 한 장 + `MI_Prop_TableDecor_Palette` 하나 — M108-03b 루프#1384, 섹션 1).
재실행 안전: 같은 이름 에셋을 지우고 다시 만든다. 로그 `반입:` 줄이 크기·삼각형 증거다.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decor_palette  # noqa: E402

DECOR_DIR = "/Game/Game/Environment/Props/TableDecor"
IMPORT_TEMP = DECOR_DIR + "/_TableDecorImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MESHES = {"Mug_01": "SM_Prop_Mug_01", "FlowerPot_01": "SM_Prop_FlowerPot_01", "Candle_01": "SM_Prop_Candle_01",
          "Radio_01": "SM_Prop_Radio_01", "Radio_On_01": "SM_Prop_Radio_On_01"}
# 슬롯 색(리니어) — 머그: 크림 몸·꿀색 띠·커피·새싹 잎. 화분: 테라코타(벽 선반 화분과 같은 색)·짙은 테·흙·줄기·잎·노란 봉오리.
# 촛대: 놋쇠·짙은 놋쇠·연한 크림 밀랍·흘러내린 밀랍·심지·주황 불꽃·노란 불 심
# M108-08b(루프#1410) 라디오 나무 상자 — 칸은 뒤에 붙인다(앞 메시 칸 번호 그대로), 벽 장식 나무와 같은 색.
# M108-08b-2(루프#1411) 켜진 다이얼 창 밝은 초록 `Lit`(집 가구 불 칸과 같은 색) — 맨 뒤
SLOT_COLORS = {"Body": (0.80, 0.72, 0.55), "Band": (0.95, 0.55, 0.05), "Coffee": (0.06, 0.025, 0.01), "Leaf": (0.13, 0.45, 0.08),
               "Pot": (0.55, 0.18, 0.06), "Rim": (0.45, 0.13, 0.04), "Soil": (0.08, 0.04, 0.02), "Stem": (0.10, 0.30, 0.05),
               "Bud": (1.0, 0.70, 0.10), "Brass": (0.55, 0.30, 0.03), "BrassDark": (0.20, 0.10, 0.02), "Wax": (0.95, 0.86, 0.62),
               "Drip": (0.98, 0.92, 0.75), "Wick": (0.03, 0.02, 0.02), "Flame": (1.0, 0.30, 0.01), "Glow": (1.0, 0.85, 0.20),
               "Wood": (0.42, 0.20, 0.07), "Lit": (0.30, 1.0, 0.22)}

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


palette = decor_palette.palette_material(DECOR_DIR, "TableDecor", SLOT_COLORS)
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
    target = "{}/{}".format(DECOR_DIR, MESHES[mesh.get_name()])
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
    unreal.log_warning("반입: {} 슬롯 {} 섹션 {} 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} · 밑 z {:.1f}(받침 윗면 = z 0)".format(
        target, [str(s.material_slot_name) for s in mesh.static_materials], mesh.get_num_sections(0), mesh.get_num_triangles(0),
        box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, box.min.z))
assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
