"""M15-04 바닥 획 머티리얼 — 레퍼런스처럼 단색 바닥에 짧은 풀 획을 뿌린다(`L_Hunt` 네 칸 · `L_Ranch`).

먼저 `python3 tools/unreal/make_ground_strokes.py` 로 원본 PNG 를 만든다. 그다음:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_ground_strokes.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

바닥은 100cm 판을 칸 크기로 늘린 것이라 판 UV 로는 획이 50배 늘어난다 — 월드 좌표 XY ÷ `TileCm` 을 UV 로 쓴다.
색은 칸마다 MI 의 `Tint` 하나(흑백 텍스처 × Tint). 값은 전부 배치값(🟡). 재실행 안전: 있으면 값만 다시 맞춘다.
"""
import os

import unreal

GROUND_DIR = "/Game/Game/Environment/Ground"
TEXTURE = GROUND_DIR + "/T_Ground_Strokes"
MATERIAL = GROUND_DIR + "/M_Ground_Strokes"
SOURCE_PNG = os.path.join(unreal.Paths.project_saved_dir(), "Generated", "T_Ground_Strokes.png")
TILE_CM = 3200.0  # 텍스처 한 장(1024)이 덮는 땅 — 획 무리 9개가 한 장에 들어가 반복이 안 보인다. 이전: 800(512, 고른 점)

# MI 경로 → 리니어 Tint (텍스처 평균 ≈ 0.8 이라 원하는 색 ÷ 0.8)
TINTS = {
    GROUND_DIR + "/MI_Ground_PlainsStrokes": unreal.LinearColor(0.055, 0.160, 0.063, 1.0),   # 레퍼런스 풀 sRGB ≈ (50,125,65) 청록 기운 — 파랑 0.04 는 톤매핑에서 형광 초록(w5). 0.24 초록은 양지 (37,145,75)·(43,153,81) 로 steam_17·22 양지 (42,112~114,58~77) 보다 밝았고 0.126 은 (50,96,68) 탁했다(w50)
    GROUND_DIR + "/MI_Ground_ForestStrokes": unreal.LinearColor(1.050, 0.500, 0.120, 1.0),   # 가을섬 금빛 땅 — steam_01 (243~249,192~197,108~115). 0.82/0.50/0.25 는 (216,192,144) 모래색(w40), 0.45/0.175/0.05 는 (185,120,47) 적갈색(w39)
    GROUND_DIR + "/MI_Ground_SnowStrokes": unreal.LinearColor(0.600, 0.700, 1.000, 1.0),     # 눈 = 푸른 흰색 — steam_18 (176~197,179~208,201~230). 0.88/0.85/1.0(시트 #EAE6F5)은 하늘빛 4.5 에 (218,218,224) 맨 흰색(w40)
    GROUND_DIR + "/MI_Ground_SwampStrokes": unreal.LinearColor(0.180, 0.220, 0.030, 1.0),    # 늪 = steam_23 자작나무 들판 올리브 연두 (109~133,127~145,28~36). 0.14/0.15/0.056 은 (100,108,54) 탁한 녹갈색(w44), 0.19/0.233/0.016 은 (122,145,0) 파랑 0(w45)
}
FLOORS = {
    # L_Hunt 는 하벤 만 지도 땅(`setup_world_ground.py`, 2026-09-25)이 대신한다 — 옛 작은 섬 판을 다시 깔지 않는다.
    # 레퍼런스 거점도 풀밭이다(steam_00·steam_22) — 흙 바닥(DESIGN 2.1 전 값) 대신 평원과 같은 풀
    "/Game/Game/Maps/L_Ranch": {"RanchFloor": GROUND_DIR + "/MI_Ground_PlainsStrokes"},
}

lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary


def _import_texture():
    if not os.path.exists(SOURCE_PNG):
        unreal.log_error("[GroundStrokes] 원본 없음 — make_ground_strokes.py 먼저: {}".format(SOURCE_PNG))
        raise SystemExit(1)
    task = unreal.AssetImportTask()
    task.filename = SOURCE_PNG
    task.destination_path = GROUND_DIR
    task.destination_name = TEXTURE.rsplit("/", 1)[1]
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = unreal.TextureFactory()
    tools.import_asset_tasks([task])
    texture = assets.load_asset(TEXTURE)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    assets.save_asset(TEXTURE, only_if_is_dirty=False)
    return texture


def _material(texture):
    # 그래프를 매번 새로 짠다 — 노드가 겹겹이 쌓이지 않게. 에셋은 지우지 않고 노드만 비운다: 지우면 이 표 밖의
    # 자식 MI(`setup_beach.py` 모래)의 부모가 끊겨 회색 기본 머티리얼로 그려진다
    material = assets.load_asset(MATERIAL) if assets.does_asset_exist(MATERIAL) else \
        tools.create_asset(MATERIAL.rsplit("/", 1)[1], GROUND_DIR, unreal.Material, unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(material)
    # 모래 원판(`setup_beach.py`)은 HISM — 이 쓰임 표시가 없으면 게임(-game·쿠킹)에선 회색 기본 머티리얼로 그려진다
    material.set_editor_property("used_with_instanced_static_meshes", True)
    world = lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -900, 0)
    mask = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -700, 0)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    tile = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 150)
    tile.set_editor_property("parameter_name", "TileCm")
    tile.set_editor_property("default_value", TILE_CM)
    divide = lib.create_material_expression(material, unreal.MaterialExpressionDivide, -500, 0)
    sample = lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -300, 0)
    sample.set_editor_property("parameter_name", "StrokeTex")
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    tint = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, 250)
    tint.set_editor_property("parameter_name", "Tint")
    multiply = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 0)
    rough = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -100, 200)
    rough.set_editor_property("r", 1.0)
    spec = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -100, 300)
    spec.set_editor_property("r", 0.0)
    lib.connect_material_expressions(world, "", mask, "")
    lib.connect_material_expressions(mask, "", divide, "A")
    lib.connect_material_expressions(tile, "", divide, "B")
    lib.connect_material_expressions(divide, "", sample, "UVs")
    lib.connect_material_expressions(sample, "R", multiply, "A")
    lib.connect_material_expressions(tint, "", multiply, "B")
    lib.connect_material_property(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    lib.recompile_material(material)
    assets.save_asset(MATERIAL, only_if_is_dirty=False)
    return material


def _instances(material):
    for path, color in TINTS.items():
        folder, name = path.rsplit("/", 1)
        mi = assets.load_asset(path) if assets.does_asset_exist(path) else \
            tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        lib.set_material_instance_parent(mi, material)
        lib.set_material_instance_vector_parameter_value(mi, "Tint", color)
        assets.save_asset(path, only_if_is_dirty=False)


texture = _import_texture()
_instances(_material(texture))
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for level, floors in FLOORS.items():
    level_subsystem.load_level(level)
    found = 0
    for actor in actors.get_all_level_actors():
        path = floors.get(actor.get_actor_label())
        if path:
            actor.static_mesh_component.set_material(0, assets.load_asset(path))
            found += 1
    if found != len(floors):
        unreal.log_error("[GroundStrokes] {} 바닥 {}/{} 만 찾음".format(level, found, len(floors)))
        raise SystemExit(1)
    level_subsystem.save_current_level()
    unreal.log("[GroundStrokes] {} 바닥 {}장 → 획 머티리얼".format(level, found))
