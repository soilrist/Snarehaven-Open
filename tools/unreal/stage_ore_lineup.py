"""광석 바위 6종 + 보통 바위 1개를 나란히 세운다 — 비교 캡처용 임시 무대 (INBOX 보정 10-13). 캡처 뒤 맵을 `git checkout` 으로 되돌린다.
FieldOre_Coal_1 옆에 Y 방향 일렬. 캐릭터는 `-SnareStandAt=` 로 줄 가운데 앞에 세운다.
"""
import unreal

MAP = "/Game/Game/Maps/L_Hunt"
GAP = 260.0
subs = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
acts = {a.get_actor_label(): a for a in subs.get_all_level_actors()}
base = acts["FieldOre_Coal_1"].get_actor_location()
# 줄 둘레 나무·덤불·바위 묶음 인스턴스를 치운다(캡처용 — 맵은 캡처 뒤 되돌림)
for actor in subs.get_all_level_actors():
    for comp in actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
        for i in reversed(range(comp.get_instance_count())):
            t = comp.get_instance_transform(i, True)
            t = t[1] if isinstance(t, tuple) else t
            if abs(t.translation.x - base.x) < 900.0 and abs(t.translation.y - base.y) < 1500.0:
                comp.remove_instance(i)
order = ["Plain", "Coal", "Copper", "Iron", "Silver", "Gold", "Titanium"]
mid = (len(order) - 1) / 2.0
rock_mesh = unreal.EditorAssetLibrary.load_asset("/Game/Game/Environment/Nature/SM_Nature_Boulder_02")   # 몸통 행 MeshRef 와 같은 메시
for n, kind in enumerate(order):
    y = base.y + (n - mid) * GAP
    pos = unreal.Vector(base.x, y, base.z)
    if kind == "Plain":
        src = acts["FieldOre_Coal_1"]
        a = subs.spawn_actor_from_class(unreal.StaticMeshActor, pos, unreal.Rotator(0, 0, 0))
        a.static_mesh_component.set_static_mesh(rock_mesh)
        a.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        a.set_actor_scale3d(src.get_actor_scale3d())
        a.set_actor_label("Lineup_Plain")
        # 보통 바위 머티리얼 = 몸통 메시의 원래 머티리얼
        a.static_mesh_component.set_material(0, rock_mesh.get_material(0))
        continue
    src = acts["FieldOre_{}_1".format(kind)]
    # 같은 종류 바위와 덩이를 줄로 옮긴다: 바위 + 붙은 덩이 전부 같은 만큼 평행 이동
    src.set_actor_location(pos, False, False)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log_warning("[Lineup] 시작 {} · 줄 {}".format(base, order))
