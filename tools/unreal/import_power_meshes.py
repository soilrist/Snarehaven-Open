"""INBOX 보정(감독 10-06 02:45) 전력 가구 메시 반입 — `make_power_meshes.py`(OBJ 원본) 다음, `create_data_tables.py` 앞에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_power_meshes.py" \
      -unattended -nosplash -nullrhi (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

Saved/Generated/SolarPanel_01.obj · Battery_01.obj · LampElectric_01.obj → /Game/Game/Environment/Props/Power/SM_Prop_SolarPanel_01 · SM_Prop_Battery_01 ·
SM_Prop_LampElectric_01(M40-06 — 등불 빌림 끝).
`import_weapon_meshes.py` 와 같은 흐름(반입 → 이름 바꿈 → Nanite 끔)이고, 색은 M108-03b(루프#1384)부터 슬롯마다 재질이 아니라
`decor_palette.py` 팔레트 그림 한 장 + `MI_Prop_Power_Palette` 하나(섹션 1 = 놓인 전력 가구 하나에 드로콜 1).
재실행 안전: 같은 이름 에셋을 지우고 다시 만든다. 로그 `반입:` 줄이 크기·삼각형 증거다.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decor_palette  # noqa: E402

POWER_DIR = "/Game/Game/Environment/Props/Power"
IMPORT_TEMP = POWER_DIR + "/_PowerImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MESHES = {"SolarPanel_01": "SM_Prop_SolarPanel_01", "Battery_01": "SM_Prop_Battery_01", "LampElectric_01": "SM_Prop_LampElectric_01"}
# 슬롯 색(리니어) — steam_22 패널: 짙은 남색 칸 · 흰 격자 · 밝은 회백 테두리 · 회색 받침. 배터리: 어두운 청회 몸통·검은 뚜껑·빨강/납빛 단자·노란 번개
# 전등: 짙은 쇠 받침 · 회색 기둥 · 크림 갓 · 노란 전구 · 검은 전선
SLOT_COLORS = {"Backing": (0.62, 0.64, 0.68), "Frame": (0.48, 0.50, 0.54), "Cell": (0.010, 0.018, 0.085),
               "Leg": (0.09, 0.09, 0.10), "Case": (0.035, 0.050, 0.075), "Lid": (0.012, 0.012, 0.014),
               "Plus": (0.55, 0.025, 0.015), "Minus": (0.20, 0.21, 0.23), "Bolt": (0.85, 0.50, 0.015),
               "LampBase": (0.030, 0.032, 0.036), "Pole": (0.16, 0.16, 0.17), "Shade": (0.78, 0.66, 0.42),
               "Bulb": (1.0, 0.72, 0.12), "Wire": (0.008, 0.008, 0.008)}

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


palette = decor_palette.palette_material(POWER_DIR, "Power", SLOT_COLORS)
tasks = []
for name in MESHES:
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
    target = "{}/{}".format(POWER_DIR, MESHES[mesh.get_name()])
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
    unreal.log_warning("반입: {} 슬롯 {} 섹션 {} 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f}".format(
        target, [str(s.material_slot_name) for s in mesh.static_materials], mesh.get_num_sections(0), mesh.get_num_triangles(0),
        box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z))
assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
