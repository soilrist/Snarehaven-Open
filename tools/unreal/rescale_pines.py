"""M81-04 보정 — 침엽수(Pine_05·06) 키를 캐릭터 ×4.5 로 키운다. 맵을 다시 만들지 않고 이미 선 나무만 그 자리에서 키운다.

실행 (uproject·-script 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/rescale_pines.py" -unattended -nosplash -log

L_Hunt·L_Ranch 의 HISM 인스턴스와 StaticMeshActor 중 Pine_05·06 만 균일 배율로 키운다(발밑 자리는 그대로, 줄기 충돌 원기둥은 안 건드림).
다시 돌려도 안전하다 — 이미 새 키 범위인 나무(옛 최대 높이와 새 최소 높이의 중간 이상)는 건너뛴다.
`setup_zones.py`·`setup_start_decor.py`·`setup_plateaus.py` 의 목표 높이 숫자도 같은 배율로 고쳐 두었다(처음부터 다시 심어도 같은 키).
"""
import unreal

MAPS = ["/Game/Game/Maps/L_Hunt", "/Game/Game/Maps/L_Ranch"]
OLD_MAX_CM = {"SM_Nature_Pine_05": 560.0 * 1.15, "SM_Nature_Pine_06": 500.0 * 1.15}   # 옛 목표 높이 최대(+15% 흔들림)
NEW_TARGET_CM = {"SM_Nature_Pine_05": 792.0, "SM_Nature_Pine_06": 707.0}               # 176cm × 4.5 / 같은 비율 두 번째 메시
JITTER = 0.15
FACTOR = 792.0 / 560.0   # 두 메시 모두 ≈ 1.414


def _mesh_height(mesh):
    box = mesh.get_bounding_box()
    return float(box.max.z - box.min.z)


def _threshold(name):
    return (OLD_MAX_CM[name] + NEW_TARGET_CM[name] * (1.0 - JITTER)) * 0.5


def _grow(scale, height_per_unit, name):
    """현재 높이가 옛 범위면 FACTOR 배, 아니면 None."""
    if scale.z * height_per_unit >= _threshold(name):
        return None
    return unreal.Vector(scale.x * FACTOR, scale.y * FACTOR, scale.z * FACTOR)


level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
grown = skipped = 0
for path in MAPS:
    level_subsystem.load_level(path)
    for actor in actors_subsystem.get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = comp.static_mesh
            name = mesh.get_name() if mesh else ""
            if name not in OLD_MAX_CM:
                continue
            per_unit = _mesh_height(mesh)
            if isinstance(comp, unreal.HierarchicalInstancedStaticMeshComponent):
                for i in range(comp.get_instance_count()):
                    t = comp.get_instance_transform(i, False)
                    new = _grow(t.scale3d, per_unit, name)
                    if new is None:
                        skipped += 1
                        continue
                    t.scale3d = new
                    comp.update_instance_transform(i, t, False, i == comp.get_instance_count() - 1)
                    grown += 1
            elif isinstance(actor, unreal.StaticMeshActor):
                new = _grow(actor.get_actor_scale3d(), per_unit, name)
                if new is None:
                    skipped += 1
                    continue
                actor.set_actor_scale3d(new)
                grown += 1
    level_subsystem.save_current_level()
    unreal.log_warning("[RescalePines] {} 끝 — 키운 것 {} · 건너뜀 {}".format(path, grown, skipped))
unreal.log_warning("[RescalePines] 합계 키운 것 {} · 건너뜀 {}".format(grown, skipped))
