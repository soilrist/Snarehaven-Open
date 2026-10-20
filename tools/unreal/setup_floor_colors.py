"""두 존 바닥색 — 사냥터는 짙은 숲 초록, 목장은 흙색 (INBOX 지시, 감독 2026-09-21 07:50).

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_floor_colors.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -abslog=~/UNREAL/Snarehaven/logs/<이름>.log

`setup_hunt_nature.py`(M5-02b)가 사냥터 바닥에 물려 둔 `MI_Nature_Grass` 는 Kenney 팩 원본 팔레트
(민트/청록 0.173/0.847/0.722)라 화면이 너무 밝았고, 목장 바닥(`RanchFloor`)은 아직 엔진 기본 회색이었다.
지시가 준 것은 **팔레트 방향**(사냥터 초록 vs 목장 흙색)과 범위뿐이라 정확한 값은 화면을 보고 고른다 —
나무 높이·배치처럼 🟡 로 정하고 INBOX `확인` 에 올리는 값이지 게임 규칙 숫자가 아니다.

**레벨은 목장만 저장한다** — 감독이 에디터로 `L_Hunt` 를 열어 둘 수 있어(INBOX 루프#75) 사냥터는
머티리얼 에셋만 고치고 레벨은 건드리지 않는다(바닥은 이미 그 머티리얼을 물고 있다).

여러 번 돌려도 같은 결과다(재실행 안전): 머티리얼이 이미 있으면 만들지 않고 값만 다시 맞춘다.
"""
import unreal

NATURE_DIR = "/Game/Game/Environment/Nature"
GROUND_DIR = "/Game/Game/Environment/Ground"

HUNT_MATERIAL = NATURE_DIR + "/MI_Nature_Grass"       # 사냥터 바닥 (M5-02b 가 물려 둔 것)
RANCH_MATERIAL = GROUND_DIR + "/MI_Ground_Dirt"       # 목장 바닥 (여기서 새로 만든다)

RANCH_LEVEL = "/Game/Game/Maps/L_Ranch"
RANCH_FLOOR_LABEL = "RanchFloor"

DIFFUSE_COLOR_PARAM = "DiffuseColor"             # 부모(FBX Legacy Lambert/Phong)의 베이스컬러 칸
DIFFUSE_WEIGHT_PARAM = "DiffuseColorMapWeight"   # 1 이면 텍스처가 색을 덮는다 — 단색 바닥은 0

# 지시(감독, 2026-09-21 07:50)가 준 범위 안에서 고른 리니어 RGB. 게임 규칙 숫자가 아니라 팔레트다.
# 첫 값(사냥터 0.065/0.250/0.080 · 목장 0.300/0.180/0.110)은 캡처에서 **밝은 풀색·뜬 황토**로 나왔다
# (`captures/76_hunt_floor_green.png`·`76_ranch_floor_dirt.png`) — 조명이 리니어 값보다 화면을 밝게 올린다.
# 그래서 범위 안에서 **초록은 채도를 낮추고(R·B 를 올리고 G 를 내림) 흙은 한 단 낮춘** 쪽으로 다시 골랐다.
HUNT_COLOR = unreal.LinearColor(0.080, 0.200, 0.100, 1.0)    # 채도 낮은 짙은 숲 초록
RANCH_COLOR = unreal.LinearColor(0.250, 0.155, 0.095, 1.0)   # 다진 흙 (황토·갈색)


def _load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        unreal.log_error("에셋을 못 읽었다: {}".format(path))
    return asset


def _paint(material, color) -> None:
    """베이스컬러를 칠하고 텍스처 가중치를 0 으로 둔다 — 안 그러면 텍스처가 색을 덮는다(루프#70)."""
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        material, DIFFUSE_COLOR_PARAM, color)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        material, DIFFUSE_WEIGHT_PARAM, 0.0)


# 1) 사냥터 — 이미 있는 머티리얼의 색만 바꾼다.
hunt_material = _load(HUNT_MATERIAL)
if hunt_material is not None:
    _paint(hunt_material, HUNT_COLOR)
    unreal.EditorAssetLibrary.save_asset(HUNT_MATERIAL, only_if_is_dirty=False)
    unreal.log("사냥터 바닥색: {} ← ({:.3f}, {:.3f}, {:.3f})".format(
        HUNT_MATERIAL, HUNT_COLOR.r, HUNT_COLOR.g, HUNT_COLOR.b))

# 2) 목장 — 흙색 머티리얼을 만들고(부모는 사냥터 것에서 빌려 온다) 바닥 액터에 물린다.
ranch_material = None
if hunt_material is not None:
    parent = hunt_material.get_editor_property("parent")
    if parent is None:
        unreal.log_error("부모를 못 찾았다: {}".format(HUNT_MATERIAL))
    else:
        if unreal.EditorAssetLibrary.does_asset_exist(RANCH_MATERIAL):
            ranch_material = _load(RANCH_MATERIAL)
            unreal.log("이미 있음: {}".format(RANCH_MATERIAL))
        else:
            ranch_material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                RANCH_MATERIAL.rsplit("/", 1)[-1], GROUND_DIR, unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew())
            if ranch_material is None:
                unreal.log_error("머티리얼 인스턴스를 못 만들었다: {}".format(RANCH_MATERIAL))
            else:
                unreal.log("만듦: {}".format(RANCH_MATERIAL))
        if ranch_material is not None:
            unreal.MaterialEditingLibrary.set_material_instance_parent(ranch_material, parent)
            _paint(ranch_material, RANCH_COLOR)
            unreal.EditorAssetLibrary.save_asset(RANCH_MATERIAL, only_if_is_dirty=False)
            unreal.log("목장 바닥색: {} ← ({:.3f}, {:.3f}, {:.3f})".format(
                RANCH_MATERIAL, RANCH_COLOR.r, RANCH_COLOR.g, RANCH_COLOR.b))

# 3) 목장 바닥 액터에 물리기 — 이 레벨만 저장한다.
if ranch_material is not None:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(RANCH_LEVEL)
    actors = {a.get_actor_label(): a for a in
              unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    floor = actors.get(RANCH_FLOOR_LABEL)
    if floor is None:
        unreal.log_error("바닥 액터 없음: {} ({})".format(RANCH_FLOOR_LABEL, RANCH_LEVEL))
    else:
        floor.static_mesh_component.set_material(0, ranch_material)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        unreal.log("목장 바닥 머티리얼: {} → {}".format(RANCH_FLOOR_LABEL, RANCH_MATERIAL))

# 4) 같은 프로세스에서 다시 읽어 확인 (저장됐다는 증거는 `verify_floor_colors.py` 가 새 프로세스에서 낸다).
for path in (HUNT_MATERIAL, RANCH_MATERIAL):
    material = unreal.EditorAssetLibrary.load_asset(path)
    if material is None:
        continue
    color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
        material, DIFFUSE_COLOR_PARAM)
    weight = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
        material, DIFFUSE_WEIGHT_PARAM)
    unreal.log("확인: {} {}=({:.3f}, {:.3f}, {:.3f}) 가중치={:.1f}".format(
        material.get_name(), DIFFUSE_COLOR_PARAM, color.r, color.g, color.b, weight))
