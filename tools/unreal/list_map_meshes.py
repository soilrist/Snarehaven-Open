"""읽기 전용 — `L_Ranch` 의 정적 메시 액터를 메시 경로별로 세어 로그 `[MapMeshes]` 줄로 찍는다 (M17-11 전제 확인).

실행 (절대경로 필수, docs/checks/unreal.md):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/list_map_meshes.py" -unattended -nosplash -log
"""
import collections

import unreal

MAP = "/Game/Game/Maps/L_Ranch"

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
counts = collections.Counter()
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp is None or comp.static_mesh is None:
        continue
    mat = comp.get_material(0)
    counts[(comp.static_mesh.get_path_name(), mat.get_path_name() if mat else "-")] += 1
lines = []
for (mesh, mat), n in sorted(counts.items()):
    lines.append("[MapMeshes] {} x{} mat={}".format(mesh, n, mat))
lines.append("[MapMeshes] 종류 {}".format(len(counts)))
open("~/UNREAL/Snarehaven/logs/253_map_meshes.txt", "w").write("\n".join(lines) + "\n")

# 울타리 후보 메시 크기 (cm, 배율 1) — 옆 울타리 교체 간격을 정하는 근거
sizes = []
for name in ("SM_Building_Fence_02", "SM_Building_Hedge_01"):
    mesh = unreal.EditorAssetLibrary.load_asset("/Game/Game/Environment/Buildings/" + name)
    box = mesh.get_bounding_box()
    sizes.append("[MapMeshes] 크기 {} = {:.0f} x {:.0f} x {:.0f}".format(
        name, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z) + " 경계 x {:.0f}~{:.0f}".format(box.min.x, box.max.x))
open("~/UNREAL/Snarehaven/logs/253_map_meshes.txt", "a").write("\n".join(sizes) + "\n")
