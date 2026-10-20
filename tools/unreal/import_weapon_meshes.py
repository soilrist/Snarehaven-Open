"""M39-06a·08b 사냥 총 메시 반입 — `make_weapon_meshes.py`(OBJ 원본) 다음, `create_data_tables.py` 앞에 돌린다.

  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_weapon_meshes.py" \
      -unattended -nosplash -nullrhi (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

Saved/Generated/PistolSemi_01.obj · RifleModern_01.obj → /Game/Game/Characters/Equipment/Weapons/SM_Weapon_PistolSemi_01 ·
`SM_Weapon_RifleModern_01` (몽둥이와 같은 폴더).
`import_tree_meshes.py` 와 같은 흐름(반입 → 이름 바꿈 → Nanite 끔)이고, 슬롯 색은 `setup_harbor.py` 처럼 `M_Nature_Matte`
인스턴스(`DiffuseColor`)를 슬롯마다 만든다. 충돌은 손에 붙일 때 `UEquipmentComponent` 가 끈다(몽둥이와 같다).
재실행 안전: 같은 이름 에셋을 지우고 다시 만든다. 로그 `반입:` 줄이 크기·삼각형 증거다(커맨드릿이 `unreal.log` 를 삼켜 warning 으로).
"""
import os

import unreal

WEAPON_DIR = "/Game/Game/Characters/Equipment/Weapons"
IMPORT_TEMP = WEAPON_DIR + "/_GunImport"
SOURCE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated")
MATTE_PARENT = "/Game/Game/Environment/Nature/M_Nature_Matte"
MESHES = {"PistolSemi_01": "SM_Weapon_PistolSemi_01", "RifleModern_01": "SM_Weapon_RifleModern_01",
          "Shotgun_01": "SM_Weapon_Shotgun_01", "Crossbow_01": "SM_Weapon_Crossbow_01",
          "PistolAuto_01": "SM_Weapon_PistolAuto_01", "Smg_01": "SM_Weapon_Smg_01",
          "RifleBurst_01": "SM_Weapon_RifleBurst_01", "RifleSemi_01": "SM_Weapon_RifleSemi_01",
          "ShotgunAuto_01": "SM_Weapon_ShotgunAuto_01", "ShotgunShort_01": "SM_Weapon_ShotgunShort_01",
          "MachineGun_01": "SM_Weapon_MachineGun_01", "Bazooka_01": "SM_Weapon_Bazooka_01"}
# 슬롯 색(리니어) — 레퍼런스 steam_01 총은 진회색 덩어리. 슬라이드는 한 톤 밝게 해 위에서 틀과 갈리게, 손잡이는 갈색 나무
SLOT_COLORS = {"Metal": (0.10, 0.105, 0.115), "Dark": (0.025, 0.025, 0.03), "Grip": (0.28, 0.12, 0.05), "Hole": (0.005, 0.005, 0.005), "Green": (0.05, 0.16, 0.05)}

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def _materials():
    parent = assets.load_asset(MATTE_PARENT)
    out = {}
    for slot, rgb in SLOT_COLORS.items():
        name = "MI_Weapon_Gun_" + slot
        path = WEAPON_DIR + "/" + name
        mi = assets.load_asset(path) if assets.does_asset_exist(path) else \
            tools.create_asset(name, WEAPON_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "DiffuseColor", unreal.LinearColor(*rgb, 1.0))
        assets.save_asset(path, only_if_is_dirty=False)
        out[slot] = mi
    return out


materials = _materials()
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
for path in assets.list_assets(IMPORT_TEMP, recursive=True, include_folder=False):
    mesh = assets.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh) or mesh.get_name() not in MESHES:
        continue
    target = "{}/{}".format(WEAPON_DIR, MESHES[mesh.get_name()])
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
    # 반입기가 Nanite 를 켜 둔다 — 이 프로젝트는 끈다(DESIGN 4.2, `import_tree_meshes.py` 와 같다)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.enabled = False
    mesh.set_editor_property("nanite_settings", nanite)
    assets.save_asset(target, only_if_is_dirty=False)
    box = mesh.get_bounding_box()
    unreal.log_warning("반입: {} 슬롯 {} 삼각형 {} 크기 x {:.1f} y {:.1f} z {:.1f} 범위 x {:.1f}~{:.1f} z {:.1f}~{:.1f}".format(
        target, [str(s.material_slot_name) for s in mesh.static_materials],
        mesh.get_num_triangles(0),
        box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, box.min.x, box.max.x, box.min.z, box.max.z))
assets.delete_directory(IMPORT_TEMP)   # 임포터가 만든 OBJ 머티리얼 — 쓰지 않는다
