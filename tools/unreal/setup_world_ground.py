"""사냥터(L_Hunt) 하벤 만 지도 땅 — 땅 타일(지금 46장)을 깔고 옛 작은 섬 판(존 바닥·모래 원판·이음매·고원)을 걷어낸다.

순서: `python3 tools/unreal/make_world_map.py` → `python3 tools/unreal/make_island_meshes.py` →
`SNARE_MESHES=<WorldGround_* 이름들> import_tree_meshes.py` → 이 스크립트 → `setup_sea.py` → `setup_zones.py`.
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_world_ground.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

땅 색은 칸 머티리얼 넷 대신 `M_Ground_World` 한 장 — `T_World_Biome`(R 눈 · G 가을 · B 늪 · A 모래 무게, 1px = 100cm)로
평원 초록에 존 색을 섞고 풀 획(`T_Ground_Strokes`)을 곱한다. 색은 `setup_ground_strokes.py`·`setup_beach.py` 가 캡처로 맞춘 값 그대로.
시작 거점(`islands.START`)에 PlayerStart 와 자리표시 동물을 옮긴다. 재실행 안전: 라벨로 찾아 다시 맞춘다.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

LEVEL = "/Game/Game/Maps/L_Hunt"
GROUND_DIR = "/Game/Game/Environment/Ground"
BIOME_TEXTURE = GROUND_DIR + "/T_World_Biome"
STROKE_TEXTURE = GROUND_DIR + "/T_Ground_Strokes"
MATERIAL = GROUND_DIR + "/M_Ground_World"
TILE_MESH = GROUND_DIR + "/SM_Ground_World_{}"
TILE_CM = 3200.0   # `setup_ground_strokes.py` TILE_CM 과 같다
TINTS = {          # `setup_ground_strokes.py` TINTS · `setup_beach.py` SAND_TINT 와 같은 값 (캡처로 맞춘 팔레트, 🟡)
    "Plains": unreal.LinearColor(0.055, 0.160, 0.063, 1.0),
    "Snow": unreal.LinearColor(0.600, 0.700, 1.000, 1.0),
    "Forest": unreal.LinearColor(1.050, 0.500, 0.120, 1.0),
    "Swamp": unreal.LinearColor(0.180, 0.220, 0.030, 1.0),
    "Sand": unreal.LinearColor(1.000, 0.610, 0.135, 1.0),
}
GROUND_HLSL = """
float2 p = WP.xy;
float s = Texture2DSample(Stroke, StrokeSampler, p / TileCm).r;
float2 fu = float2((p.y - Box.y) / Box.w, 1.0 - (p.x - Box.x) / Box.z);
float4 w = Texture2DSample(Biome, BiomeSampler, fu);
float3 c = Plains;
c = lerp(c, Snow, smoothstep(0.3, 0.7, w.r));
c = lerp(c, Forest, smoothstep(0.3, 0.7, w.g));
c = lerp(c, Swamp, smoothstep(0.3, 0.7, w.b));
c = lerp(c, Sand, smoothstep(0.3, 0.7, w.a));
return c * s;
"""
# 옛 작은 섬 판 — 라벨 앞머리로 걷어낸다
OLD_PREFIXES = ("MinStageFloor", "ZoneFloor_", "Beach", "ZoneSeams", "Plateau_", "WorldGround_")
START_LABEL = "MinStagePlayerStart"
STAGE_ANIMALS = {"HuntStagePig": (600.0, 0.0), "HuntStageCow": (300.0, -260.0), "HuntStageSheep": (300.0, 260.0)}

assets = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _import_biome():
    source = os.path.join(unreal.Paths.project_saved_dir(), "Generated", "T_World_Biome.png")
    if not os.path.exists(source):
        unreal.log_error("[WorldGround] 원본 없음 — make_world_map.py 먼저: {}".format(source))
        raise SystemExit(1)
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path, task.destination_name = BIOME_TEXTURE.rsplit("/", 1)
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = unreal.TextureFactory()
    tools.import_asset_tasks([task])
    texture = assets.load_asset(BIOME_TEXTURE)
    texture.set_editor_property("srgb", False)   # 색이 아니라 무게
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    assets.save_asset(BIOME_TEXTURE, only_if_is_dirty=False)
    return texture


def _material(biome):
    material = assets.load_asset(MATERIAL) if assets.does_asset_exist(MATERIAL) else \
        tools.create_asset(MATERIAL.rsplit("/", 1)[1], GROUND_DIR, unreal.Material, unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(material)
    custom = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property("code", GROUND_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ["WP", "Stroke", "Biome", "TileCm", "Box"] + list(TINTS)
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)

    def node(cls, name, y, **props):
        n = lib.create_material_expression(material, cls, -600, y)
        n.set_editor_property("parameter_name", name)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    x0, x1, y0, y1 = islands.BOUNDS
    sources = {
        "WP": lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -600, -200),
        "Stroke": node(unreal.MaterialExpressionTextureObjectParameter, "StrokeTex", -100, texture=assets.load_asset(STROKE_TEXTURE)),
        "Biome": node(unreal.MaterialExpressionTextureObjectParameter, "BiomeTex", 0, texture=biome),
        "TileCm": node(unreal.MaterialExpressionScalarParameter, "TileCm", 100, default_value=TILE_CM),
        # 바닥 색 텍스처가 덮는 틀 (x0, y0, 크기 x, 크기 y) — 존 칸 176×272 × 400cm = 거리장 노드 틀과 같다
        "Box": node(unreal.MaterialExpressionVectorParameter, "FieldBox", 200, default_value=unreal.LinearColor(
            x0, y0, islands.ZI * islands.CELL_CM, islands.ZJ * islands.CELL_CM)),
    }
    for i, (name, color) in enumerate(TINTS.items()):
        sources[name] = node(unreal.MaterialExpressionVectorParameter, name + "Tint", 300 + 100 * i, default_value=color)
    for name in names:
        # 벡터 파라미터 기본 출력("")은 RGB 셋뿐 — 틀(Box.w)은 넷째 칸까지 쓰니 RGBA 로 잇는다(Metal: "swizzle 'w' out of bounds")
        if not lib.connect_material_expressions(sources[name], "RGBA" if name == "Box" else "", custom, name):
            unreal.log_error("[WorldGround] 셰이더 입력 연결 실패: {}".format(name))
            raise SystemExit(1)
    for value, prop, y in ((1.0, unreal.MaterialProperty.MP_ROUGHNESS, 200), (0.0, unreal.MaterialProperty.MP_SPECULAR, 300)):
        constant = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -200, y)
        constant.set_editor_property("r", value)
        lib.connect_material_property(constant, "", prop)
    lib.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.recompile_material(material)
    assets.save_asset(MATERIAL, only_if_is_dirty=False)
    return material


def _find(label):
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


material = _material(_import_biome())
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.load_level(LEVEL)

removed = 0
for actor in actors.get_all_level_actors():
    if actor.get_actor_label().startswith(OLD_PREFIXES):
        actors.destroy_actor(actor)
        removed += 1

tiles = sorted(n[len("SM_Ground_World_"):] for n in (a.rsplit("/", 1)[1].split(".")[0] for a in assets.list_assets(GROUND_DIR, False))
               if n.startswith("SM_Ground_World_"))
# 지도가 바뀌어 원본(OBJ)이 없어진 타일은 옛 땅이다 — 깔지 않고 에셋째 지운다
_sources = os.path.join(unreal.Paths.project_saved_dir(), "Generated")
for key in [k for k in tiles if not os.path.exists(os.path.join(_sources, "WorldGround_{}.obj".format(k)))]:
    assets.delete_asset(TILE_MESH.format(key))
    tiles.remove(key)
    unreal.log("[WorldGround] 옛 타일 지움: SM_Ground_World_{}".format(key))
for key in tiles:
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
    actor.set_actor_label("WorldGround_" + key)
    comp = actor.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_static_mesh(assets.load_asset(TILE_MESH.format(key)))
    comp.set_material(0, material)
    actor.set_folder_path("WorldGround")

sx, sy = islands.land_point(*islands.START, margin=400.0)
_find(START_LABEL).set_actor_location(unreal.Vector(sx, sy, 100.0), False, False)
for label, (dx, dy) in STAGE_ANIMALS.items():
    animal = _find(label)
    if animal:
        animal.set_actor_location(unreal.Vector(sx + dx, sy + dy, 100.0), False, False)

level_subsystem.save_current_level()
unreal.log("[WorldGround] 저장: 타일 {}장 · 옛 판 {}개 걷어냄 · 시작 ({:.0f}, {:.0f})".format(len(tiles), removed, sx, sy))
