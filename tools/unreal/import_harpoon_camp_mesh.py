"""스토리 ⑤ 작살단 캠프 메시 반입 — `make_harpoon_camp_mesh.py`(OBJ 원본) 다음에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_harpoon_camp_mesh.py" \
      -unattended -nosplash -nullrhi

Saved/Generated/HarpoonCamp_01.obj → /Game/Game/Environment/Props/Story/SM_Prop_HarpoonCamp_01.
`import_power_meshes.py` 와 같은 흐름(반입 → 이름 바꿈 → Nanite 끔 → 슬롯마다 `M_Nature_Matte` 인스턴스 색).
슬롯 색은 `make_harpoon_camp_mesh.py` 의 `SLOT_COLORS` 를 읽는다. 재실행 안전. 로그 `반입:` 줄이 크기·삼각형 증거다.
"""
import ast
import os

import unreal

STORY_DIR = "/Game/Game/Environment/Props/Story"
IMPORT_TEMP = STORY_DIR + "/_HarpoonCampImport"
HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE_DIR = os.path.join(HERE, "..", "..", "Saved", "Generated")
MATTE_PARENT = "/Game/Game/Environment/Nature/M_Nature_Matte"

# 원본 스크립트에서 색 표(`SLOT_COLORS`)만 읽는다 — 모듈을 실행하면 OBJ 를 다시 쓰고 Blender 쪽 import 가 필요해서 ast 로 뽑는다
_tree = ast.parse(open(os.path.join(HERE, "make_harpoon_camp_mesh.py")).read())
SLOT_COLORS = next(ast.literal_eval(n.value) for n in _tree.body
                   if isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") == "SLOT_COLORS")

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def _materials():
    parent = assets.load_asset(MATTE_PARENT)
    out = {}
    for slot, rgb in SLOT_COLORS.items():
        name = "MI_Prop_HarpoonCamp_" + slot
        path = STORY_DIR + "/" + name
        mi = assets.load_asset(path) if assets.does_asset_exist(path) else \
            tools.create_asset(name, STORY_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "DiffuseColor", unreal.LinearColor(*rgb, 1.0))
        assets.save_asset(path, only_if_is_dirty=False)
        out[slot] = mi
    return out


materials = _materials()
for name in ("HarpoonCamp_01", "HarpoonCage_01"):
    target = STORY_DIR + "/SM_Prop_" + name
    task = unreal.AssetImportTask()
    task.filename = os.path.abspath(os.path.join(SOURCE_DIR, name + ".obj"))
    task.destination_path = IMPORT_TEMP
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
        mesh = assets.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh) or mesh.get_name() != name:
            continue
        if assets.does_asset_exist(target):
            assets.delete_asset(target)
        assets.rename_asset(path.split(".")[0], target)
        mesh = assets.load_asset(target)
        for index, slot in enumerate(mesh.static_materials):
            mi = materials.get(str(slot.material_slot_name))
            if mi:
                mesh.set_material(index, mi)
            else:
                unreal.log_error("색 없는 슬롯: {} {}".format(target, slot.material_slot_name))
        nanite = mesh.get_editor_property("nanite_settings")
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
        assets.save_asset(target, only_if_is_dirty=False)
        box = mesh.get_bounding_box()
        unreal.log_warning("반입: {} 슬롯 {} 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f}".format(
            target, [str(s.material_slot_name) for s in mesh.static_materials], mesh.get_num_triangles(0),
            box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z))
assets.delete_directory(IMPORT_TEMP)
