"""바위 충돌 켜기 — 맵에 깔린 바위 묶음(HISM)이 NO_COLLISION 이라 몸이 뚫고 지나가던 것을 고친다 (INBOX 결함 10-13 「돌 안으로 들어가버린다」).

실행 (절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/fix_rock_collision.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

바위 메시(Boulder·Rock)는 이미 볼록 충돌 1개를 갖고 있다. 이름이 Boulder·Rock 으로 시작하는 메시를 쓰는 컴포넌트 중 충돌이 꺼진 것만 `BlockAll` 로 켠다.
"""
import unreal

MAPS = ["/Game/Game/Maps/L_Ranch", "/Game/Game/Maps/L_Hunt"]
PREFIXES = ("SM_Nature_Boulder", "SM_Nature_Rock")

for path in MAPS:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path)
    fixed, instances = 0, 0
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
            if comp.static_mesh is None or not comp.static_mesh.get_name().startswith(PREFIXES):
                continue
            if comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                comp.set_collision_profile_name("BlockAll")
                fixed += 1
                instances += comp.get_instance_count() if isinstance(comp, unreal.InstancedStaticMeshComponent) else 1
    unreal.log_warning("바위 충돌 켬: {} — 컴포넌트 {} 개 · 바위 {} 개".format(path, fixed, instances))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
