"""M108-02 벽 장식 메시 반입 — `make_wall_decor_meshes.py`(OBJ 원본) 다음, `create_data_tables.py` 앞에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_wall_decor_meshes.py" \
      -unattended -nosplash -nullrhi (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

Saved/Generated/PictureFrame_01.obj · WallClock_01.obj · WallShelf_01.obj → /Game/Game/Environment/Props/WallDecor/SM_Prop_PictureFrame_01 ·
SM_Prop_WallClock_01 · SM_Prop_WallShelf_01, M108-05c(루프#1390) WallMirror_01 · WallMap_01 · WallAnchor_01 · WallWreath_01 → SM_Prop_<같은 이름>. `import_power_meshes.py` 와 같은 흐름(반입 → 이름 바꿈 → Nanite 끔)이고, 색은 M108-03b(루프#1384)부터
슬롯마다 재질이 아니라 `decor_palette.py` 팔레트 그림 한 장 + `MI_Prop_WallDecor_Palette` 하나(섹션 1 = 놓인 장식 하나에 드로콜 1).
재실행 안전: 같은 이름 에셋을 지우고 다시 만든다. 로그 `반입:` 줄이 크기·삼각형 증거다.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decor_palette  # noqa: E402

DECOR_DIR = "/Game/Game/Environment/Props/WallDecor"
IMPORT_TEMP = DECOR_DIR + "/_WallDecorImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MESHES = {"PictureFrame_01": "SM_Prop_PictureFrame_01", "WallClock_01": "SM_Prop_WallClock_01", "WallShelf_01": "SM_Prop_WallShelf_01",
          "WallMirror_01": "SM_Prop_WallMirror_01", "WallMap_01": "SM_Prop_WallMap_01", "WallAnchor_01": "SM_Prop_WallAnchor_01",
          "WallWreath_01": "SM_Prop_WallWreath_01", "NeonSign_01": "SM_Prop_NeonSign_01",  # 네온 간판 M108-08b — 새 칸 없음(꺼짐 관 = Glass)
          "NeonSign_On_01": "SM_Prop_NeonSign_On_01"}  # 켜짐 짝 M108-08b-2 — 칸 `NeonPink`·`NeonSky` 맨 뒤
# 슬롯 색(리니어) — 나무는 집 통나무 벽과 같은 꿀갈색 계열(`make_house_meshes.py`), 테·받침은 진갈색.
# 액자 그림: 하늘·바다·해·섬·등대(빨강·흰 줄). 시계: 크림 판·검은 바늘·빨강 못·새싹 잎. 선반: 테라코타 화분·흙·빨강/파랑 책·꿀.
# M108-05c 칸은 뒤에 붙인다(앞 메시 칸 번호 그대로): 거울 판(옅은 하늘 회색)·빛 줄(흰빛) · 지도 천(양피 빛)·밝은 섬 초록 · 닻 쇠·밧줄
SLOT_COLORS = {"Wood": (0.42, 0.20, 0.07), "Trim": (0.10, 0.045, 0.02), "Sky": (0.35, 0.62, 0.85), "Sea": (0.04, 0.22, 0.48),
               "Sun": (1.0, 0.70, 0.10), "Land": (0.12, 0.42, 0.10), "Tower": (0.60, 0.05, 0.03), "White": (0.85, 0.85, 0.80),
               "Face": (0.85, 0.76, 0.55), "Hand": (0.03, 0.025, 0.02), "Pin": (0.65, 0.06, 0.04), "Leaf": (0.13, 0.45, 0.08),
               "Pot": (0.55, 0.18, 0.06), "Soil": (0.08, 0.04, 0.02), "Book1": (0.55, 0.06, 0.05), "Book2": (0.05, 0.15, 0.50),
               "Honey": (0.95, 0.55, 0.05),
               "Glass": (0.42, 0.58, 0.68), "Shine": (0.92, 0.95, 1.0), "Paper": (0.78, 0.62, 0.36), "Leaf2": (0.30, 0.62, 0.10),
               "Iron": (0.07, 0.075, 0.085), "Rope": (0.50, 0.34, 0.14),
               "NeonPink": (1.0, 0.16, 0.55), "NeonSky": (0.22, 0.78, 1.0)}

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


palette = decor_palette.palette_material(DECOR_DIR, "WallDecor", SLOT_COLORS)
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
    # OBJ 의 뒷면 y 0 · 앞 -Y 는 반입하며 Y 가 뒤집혀 **언리얼에선 뒷면 y 0 · 앞 +Y** 가 된다(루프#1371 첫 반입 y 0.0~3.0)
    unreal.log_warning("반입: {} 슬롯 {} 섹션 {} 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} · y {:.1f}~{:.1f}(벽 = y 0, 앞 = +Y)".format(
        target, [str(s.material_slot_name) for s in mesh.static_materials], mesh.get_num_sections(0), mesh.get_num_triangles(0),
        box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, box.min.y, box.max.y))
assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
