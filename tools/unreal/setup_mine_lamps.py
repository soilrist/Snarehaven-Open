"""M17-03 굴 등불 교체 — `L_Hunt` 의 `Mine_Lamp_0~8` 를 엔진 큐브에서 `SM_Prop_Lantern_01` 로 바꾼다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_mine_lamps.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

왜: 굴 등불이 주황 발광 네모 기둥(엔진 큐브)이었다. 불빛(`Mine_Light_*` 포인트 라이트)은 그대로 두고 몸만 우리 등불 메시로 바꾼다.
재실행 안전: 이미 등불 메시면 크기·높이만 다시 맞춘다. 굴 전체를 다시 짓는 `setup_mine.py` 는 같은 값을 쓴다.
"""
import unreal

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"
LANTERN = "/Game/Game/Environment/Props/SM_Prop_Lantern_01"
LABEL_PREFIX = "Mine_Lamp_"
TARGET_HEIGHT_CM = 55.0         # 벽에 걸린 등불 높이 — 굴 천장 300cm 에 대해 사람 손 크기(레벨 배치값, 게임 규칙 숫자 아님 — 🟡)
HANG_BOTTOM_Z_CM = 150.0        # 등불 바닥 높이 — 포인트 라이트(190)가 등불 안쪽에 오도록

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL_PATH)
mesh = unreal.load_asset(LANTERN)
if mesh is None:
    raise RuntimeError("등불 메시 없음: " + LANTERN)
bounds = mesh.get_bounds()
height = float(bounds.box_extent.z) * 2.0
scale = TARGET_HEIGHT_CM / height
bottom = float(bounds.origin.z) - float(bounds.box_extent.z)   # 메시 원점 기준 바닥 높이(배율 전)
changed = 0
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if not actor.get_actor_label().startswith(LABEL_PREFIX):
        continue
    comp = actor.static_mesh_component
    comp.set_static_mesh(mesh)
    comp.set_material(0, mesh.get_material(0))   # 큐브에 덮었던 발광 재질을 걷고 등불 제 재질로
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    where = actor.get_actor_location()
    actor.set_actor_location(unreal.Vector(where.x, where.y, HANG_BOTTOM_Z_CM - bottom * scale), False, False)
    changed += 1
    unreal.log_warning("LAMP {} scale={:.3f} z={:.1f}".format(actor.get_actor_label(), scale, HANG_BOTTOM_Z_CM - bottom * scale))
unreal.log_warning("LAMP_DONE changed={} height={:.1f} bottom={:.1f}".format(changed, height, bottom))
if changed:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
