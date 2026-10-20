"""바닥색 2층 검사 — 새 프로세스에서 **두 존의 바닥 머티리얼**을 다시 읽는다.

실행 (uproject·script 둘 다 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/verify_floor_colors.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

`setup_floor_colors.py` 가 색을 칠한 그 프로세스의 로그는 저장됐다는 증거가 못 된다(루프#53 함정).
여기서는 저장된 에셋과 레벨을 **다시 읽어** ① 두 머티리얼의 베이스컬러가 지시(감독, 2026-09-21 07:50)의
팔레트 범위 안인지 ② 텍스처 가중치가 0 이라 그 색이 실제로 나오는지 ③ 두 바닥 액터가 각자의
머티리얼을 물고 있는지 본다. 레벨은 **읽기만 하고 저장하지 않는다**(감독이 에디터로 `L_Hunt` 를 열어
둘 수 있다 — INBOX 루프#75).

색 숫자는 이 스크립트가 고른 것이 아니다 — 지시가 준 **범위**이고, 게임 규칙 숫자가 아니라 팔레트다.
"""
import unreal

HUNT_LEVEL = "/Game/Game/Maps/L_Hunt"
RANCH_LEVEL = "/Game/Game/Maps/L_Ranch"

DIFFUSE_COLOR_PARAM = "DiffuseColor"             # 부모(FBX Legacy Phong/Lambert)의 베이스컬러 칸
DIFFUSE_WEIGHT_PARAM = "DiffuseColorMapWeight"   # 1 이면 텍스처가 색을 덮는다 — 단색 바닥은 0 이어야 한다

# (존 이름, 레벨, 바닥 액터 라벨, 머티리얼, 리니어 RGB 범위)
# 범위는 INBOX 지시(감독, 2026-09-21 07:50) 원문 그대로다: 사냥터 = 채도 낮은 짙은 숲 초록,
# 목장 = 흙색(황토·갈색). 정확한 값은 화면을 보고 고르는 🟡 배치값 성격이다.
FLOORS = [
    ("사냥터", HUNT_LEVEL, "MinStageFloor",
     "/Game/Game/Environment/Nature/MI_Nature_Grass",
     ((0.05, 0.08), (0.20, 0.30), (0.05, 0.10))),
    ("목장", RANCH_LEVEL, "RanchFloor",
     "/Game/Game/Environment/Ground/MI_Ground_Dirt",
     ((0.25, 0.35), (0.15, 0.22), (0.08, 0.14))),
]

# 바닥 머티리얼을 메시가 같이 쓰고 있으면 색을 바꿀 때 그 메시까지 따라 바뀐다(루프#70 의 재사용 사고).
# 판정은 하지 않고 **누가 쓰는지 목록만** 남긴다.
SCAN_DIR = "/Game/Game/Environment"

RANGE_EPSILON = 0.001   # float32 반올림 여유 (색 범위는 판정 기준이지 게임 규칙 숫자가 아니다)

fail = 0


def _material_of(actor):
    component = actor.static_mesh_component
    material = component.get_material(0)
    return material.get_name() if material is not None else "(빔)"


for zone, level_path, label, material_path, ranges in FLOORS:
    material = unreal.EditorAssetLibrary.load_asset(material_path)
    if material is None:
        unreal.log_error("검사 실패 — 머티리얼이 없다: {}".format(material_path))
        fail += 1
        continue

    parent = material.get_editor_property("parent")
    color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
        material, DIFFUSE_COLOR_PARAM)
    weight = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
        material, DIFFUSE_WEIGHT_PARAM)
    unreal.log("검사 {} 머티리얼 {}: 부모={} {}=({:.3f}, {:.3f}, {:.3f}) 가중치={:.1f}".format(
        zone, material.get_name(), parent.get_name() if parent is not None else "(빔)",
        DIFFUSE_COLOR_PARAM, color.r, color.g, color.b, weight))

    for channel, value, (low, high) in zip("RGB", (color.r, color.g, color.b), ranges):
        # 경계는 조금 넉넉히 본다 — 머티리얼이 값을 float32 로 담아서 0.10 을 넣어도 0.1000000015 로
        # 돌아온다(루프#76 에서 실제로 경계에서만 실패했다). 지시의 범위도 "대략" 이다.
        if not (low - RANGE_EPSILON <= value <= high + RANGE_EPSILON):
            unreal.log_error("검사 실패 — {} {} {:.3f} 이 범위 {:.2f}~{:.2f} 밖".format(
                zone, channel, value, low, high))
            fail += 1
    if weight > 0.001:
        unreal.log_error("검사 실패 — {} 텍스처 가중치 {:.2f} (0 이어야 색이 보인다)".format(zone, weight))
        fail += 1

    if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
        unreal.log_error("검사 실패 — 레벨이 없다: {}".format(level_path))
        fail += 1
        continue
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level_path)
    actors = {a.get_actor_label(): a for a in
              unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    floor = actors.get(label)
    if floor is None:
        unreal.log_error("검사 실패 — 바닥 액터가 없다: {} ({})".format(label, level_path))
        fail += 1
        continue
    got = _material_of(floor)
    unreal.log("검사 {} 바닥 {}: 머티리얼={}".format(zone, label, got))
    if got != material.get_name():
        unreal.log_error("검사 실패 — {} 바닥이 {} 를 물었다 (기대 {})".format(
            zone, got, material.get_name()))
        fail += 1

# 누가 같은 머티리얼을 쓰는지 — 판정하지 않고 목록만.
wanted = {path.rsplit("/", 1)[-1] for _, _, _, path, _ in FLOORS}
for asset_path in unreal.EditorAssetLibrary.list_assets(SCAN_DIR, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not isinstance(asset, unreal.StaticMesh):
        continue
    hits = [str(slot.material_slot_name) for slot in asset.static_materials
            if slot.material_interface is not None and slot.material_interface.get_name() in wanted]
    if hits:
        unreal.log("확인: 메시 {} 가 바닥 머티리얼을 같이 쓴다 — 슬롯 {}".format(asset.get_name(), hits))

unreal.log("검사 결과: {}".format("전부 통과" if fail == 0 else "실패 {}건".format(fail)))
