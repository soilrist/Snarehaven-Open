"""머리 행 모자 메시에 CPU 접근(`allow_cpu_access`)을 켠다 (INBOX 결함 10-20 ② · DESIGN 3.5 「모자 아랫단」, 루프#1352).

`UEquipmentComponent::ApplyEquipVisual` 이 모자를 걸 때 얼굴 앞 아랫단 꼭짓점을 실제로 읽어 눈썹선 위로 올린다.
에디터는 늘 읽히지만 게임(iOS·Android) 빌드는 이 칸을 켠 메시만 CPU 에 꼭짓점이 남는다 — 안 켜면 올림이 0 이라 폰에서만 눈을 덮는다.
`DT_ItemTable` 의 머리(`Head`) 행을 다 돌므로 새 모자를 넣은 뒤 다시 돌리면 된다(2층 `Snarehaven.Hat.Visual.BrimAboveEyes` 가 빠진 메시를 잡는다).

실행 (헤드리스, 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/set_hat_cpu_access.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log
"""
import unreal

ITEM_TABLE = "/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"


def main():
    table = unreal.load_asset(ITEM_TABLE)
    slots = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "EquipSlot")
    meshes = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "EquipMeshRef")
    paths = sorted({mesh for slot, mesh in zip(slots, meshes) if slot == "Head" and mesh not in ("", "None")})
    changed = 0
    for path in paths:
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            raise RuntimeError("모자 메시가 아니다: " + path)
        if mesh.get_editor_property("allow_cpu_access"):
            continue
        mesh.set_editor_property("allow_cpu_access", True)
        if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
            raise RuntimeError("저장 실패: " + path)
        changed += 1
    unreal.log_warning("모자 메시 CPU 접근: 머리 행 메시 {}개 중 새로 켬 {}개".format(len(paths), changed))


main()
