"""DESIGN 8.9·8.13 — 섬 둘레 바다(SeaActor)와 낚시 포인트 12곳(연못 2·강 3·해안 4·먼바다 3)을 두 레벨에 놓는다.

실행 (인자 둘 다 절대경로, `create_water_tables.py` 먼저 — 바다 폭을 그 표에서 읽는다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_sea.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행 안전: 라벨로 찾아 자리·값만 다시 맞춘다.
색·포인트 좌표는 레벨 배치(🟡)라 게임 규칙 숫자가 아니다. spotId 는 서버 `FISHING_SPOTS` 키와 같아야 한다.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

NATURE_GRASS = "/Game/Game/Environment/Nature/MI_Nature_Grass"
GROUND_DIR = "/Game/Game/Environment/Ground"
DIFFUSE_COLOR_PARAM = "DiffuseColor"
DIFFUSE_WEIGHT_PARAM = "DiffuseColorMapWeight"

# 물고기 그림자는 물보다 짙은 남색(단색 MI). 물은 아래 `M_Sea_Water` 셰이더.
MATERIALS = {
    "MI_Sea_FishShadow": unreal.LinearColor(0.010, 0.030, 0.090, 1.0),
    "MI_Sea_Foam": unreal.LinearColor(1.0, 1.0, 1.0, 1.0),     # 모래 둘레 흰 테 (`setup_beach.py`)
}

# 바다 물결 셰이더 — 레퍼런스 steam_10 물을 따라 한다: 짙은 파랑 · 해안 쪽 밝은 띠 · 흰 물결 획이 곳곳에서
# 나타났다 사라짐 · 반짝이 점 · 해안을 따라 숨 쉬듯 밀려왔다 빠지는 흰 거품 점선 두 줄.
# 텍스처는 `make_sea_waves.py` 가 만든다(R 얼룩 · G 획 · B 반짝이). 조명 없는 Unlit — 색을 steam_10 에서 잰
# sRGB 그대로 맞추려고(깊은 곳 (48,77,147) · 해안 띠 (81,113,186)) 그림자는 포기한다. 값은 전부 배치값(🟡).
WAVE_TEXTURE = GROUND_DIR + "/T_Sea_Waves"
WAVE_PNG = os.path.join(unreal.Paths.project_saved_dir(), "Generated", "T_Sea_Waves.png")
WATER_MATERIAL = GROUND_DIR + "/M_Sea_Water"
WATER_INSTANCE = GROUND_DIR + "/MI_Sea_Water"
TILE_CM = 2000.0          # 텍스처 한 장 = 20m — 기본 카메라에서 텍스처 1px ≈ 화면 1px
SHALLOW_CM = 700.0        # 해안에서 이 거리까지 밝은 띠가 옅어진다 (steam_10 밝은 띠 ≈ 캐릭터 키 3배)
# 톤매핑이 어두운 빨강·초록을 눌러 sRGB→리니어 그대로 넣으면 (6,56,148) 로 나왔다(첫 캡처) — 잰 곡선으로 올린 값
DEEP_COLOR = unreal.LinearColor(0.068, 0.105, 0.292, 1.0)
SHALLOW_COLOR = unreal.LinearColor(0.100, 0.168, 0.480, 1.0)
# 섬 가운데·반폭 기본값 — 연못·강처럼 섬 위 물은 해안 띠·거품이 없게 반폭을 크게 음수로 둔다(SeaActor 가 MID 로 덮는다)
NO_ISLAND_HALF = -1000000.0
WATER_HLSL = """
float2 p = WP.xy;
// 카메라가 +X 를 보고 있어 화면 가로 = 월드 Y — 텍스처 가로 획이 화면에서 가로로 눕게 돌린다
float2 uv = p.yx / TileCm;
float4 a = Texture2DSample(Tex, TexSampler, uv + float2(0.012, 0.005) * T);
float4 b = Texture2DSample(Tex, TexSampler, uv * 1.37 + float2(0.37, 0.61) + float2(-0.007, 0.010) * T);
float blot = a.r * 0.6 + b.r * 0.4;
float2 lp = p - Center.xy;
float2 q = abs(lp);
float along = (q.x > q.y) ? lp.y : lp.x;
float edge = max(q.x, q.y) - Half;
// 사냥터 — 해안 거리 텍스처(`T_World_Coast`, 1px = 100cm, 값 = 거리/20 + 128, 0행 = 월드 X 최대). 텍스처 밖은 먼 바다.
// along(거품 줄 좌표) = 해안 접선 쪽 월드 축 — 해안을 따라 점선이 흐른다
float o0 = 100.0;
if (FieldOn > 0.5)
{
    float2 fu = float2((p.y - Box.y) / Box.w, 1.0 - (p.x - Box.x) / Box.z);
    float2 du = float2(100.0 / Box.w, 0.0);
    float2 dv = float2(0.0, 100.0 / Box.z);
    float s0 = Texture2DSampleLevel(CoastTex, CoastTexSampler, fu, 0).r;
    float gy = Texture2DSampleLevel(CoastTex, CoastTexSampler, fu + du, 0).r - Texture2DSampleLevel(CoastTex, CoastTexSampler, fu - du, 0).r;
    float gx = Texture2DSampleLevel(CoastTex, CoastTexSampler, fu - dv, 0).r - Texture2DSampleLevel(CoastTex, CoastTexSampler, fu + dv, 0).r;
    bool inside = all(fu > 0.0) && all(fu < 1.0);
    edge = inside ? (s0 * 255.0 - 128.0) * 20.0 : 3000.0;
    along = (abs(gy) > abs(gx)) ? p.x : p.y;
    o0 = 30.0;   // 모래 원판이 없다 — 땅 메시 비탈이 바로 물에 든다
}
// 모래가 바다로 내민 거리 — `setup_beach.py` coast_out 과 같은 식. d = 모래 끝에서 잰 거리
float o = o0 + 25.0 * sin(along / 430.0 + 1.3) + 15.0 * sin(along / 190.0 + 0.4);
float d = edge - o;
float shallow = 1.0 - smoothstep(0.0, ShallowCm, d);
float3 col = lerp(Deep, Shallow, saturate(shallow * 0.9 + (blot - 0.5) * 0.3));
// 물결 획: 다른 얼룩 값으로 박자를 달리해 곳곳에서 나타났다 사라진다
float waveA = a.g * smoothstep(0.35, 0.85, 0.5 + 0.5 * sin(T * 1.3 + b.r * 9.0));
float waveB = b.g * smoothstep(0.35, 0.85, 0.5 + 0.5 * sin(T * 1.1 + a.r * 9.0 + 2.0));
float wave = saturate(waveA + waveB) * 0.9;
float spark = a.b * smoothstep(0.6, 1.0, 0.5 + 0.5 * sin(T * 4.0 + b.r * 30.0));
// 해안 거품 점선 두 줄(모래에 붙은 흰 테는 `setup_beach.py` 거품 원판). 곧은 자로 그은 선이 되지 않게 해안을 따라
// 출렁이고 토막 길이도 들쭉날쭉, 바깥 줄일수록 늦게·크게 숨 쉰다
float n = Texture2DSample(Tex, TexSampler, float2(along / 3000.0, 0.5)).r;
float n2 = Texture2DSample(Tex, TexSampler, float2(0.25, along / 1700.0)).r;
float wob = sin(along / 170.0 + n * 6.0) + 0.45 * sin(along / 53.0 + n2 * 4.0);
float r1 = 75.0 + sin(T * 0.7 - 1.2) * 16.0;
float f1 = (1.0 - smoothstep(3.5, 6.0, abs(d + wob * 14.0 - r1))) * step(0.3 + 0.4 * n2, frac(along / 130.0 + n * 2.5));
float r2 = 165.0 + sin(T * 0.7 - 2.4) * 24.0;
float f2 = (1.0 - smoothstep(3.5, 6.0, abs(d + wob * 22.0 - r2 + n * 20.0))) * step(0.45 + 0.35 * n, frac(along / 210.0 + 0.3 + n2 * 3.1)) * (0.55 + 0.45 * sin(T * 0.7 - 0.8));
float foam = max(f1, f2);
col = lerp(col, float3(1.0, 1.0, 1.0), max(wave, foam));
return col + spark * 0.8;
"""

# 섬 안 물(연못·강) — 레퍼런스 steam_05 강: 바다보다 밝은 청보라 (67,97,170), 물가로 갈수록 (100,141,203),
# 물가에 붙은 흰 실선 한 줄과 조금 떨어진 흰 점선 한 줄, 물속 바위. 거리는 액터 바운드로 잰다 — MI 하나로 크기가 다른
# 연못·강을 함께 칠한다. Round 1 = 연못(물가 거리는 `make_ranch_ground_meshes.py` 가 UV u 에 구움), 0 = 남북 띠(강, 폭 방향만). 색은 바다처럼 톤매핑 곡선으로 올린 값(🟡).
POND_MATERIAL = GROUND_DIR + "/M_Pond_Water"
POND_ROUND = GROUND_DIR + "/MI_Pond_Water"
POND_STRIP = GROUND_DIR + "/MI_River_Water"
POND_DEEP = unreal.LinearColor(0.091, 0.141, 0.403, 1.0)
POND_SHALLOW = unreal.LinearColor(0.146, 0.270, 0.610, 1.0)
POND_SHALLOW_CM = 180.0   # steam_05 밝은 띠 ≈ 캐릭터 키 1배
POND_HLSL = """
float2 lp = WP.xy - ObjPos.xy;
float2 h = Bounds.xy;   // ObjectBounds 는 반폭이다(첫 캡처: 지름 700 연못의 물이 반지름 175 로만 찼다)
float d = lerp(h.x - abs(lp.x), MeshUV.x * h.x, Round);   // 물가에서 안쪽으로 잰 거리 — 연못은 메시가 UV u 에 구워 둔 값(M19-02 굽이진 둘레)
float along = lerp(lp.y, atan2(lp.y, lp.x) * h.x, Round);
float2 uv = WP.yx / TileCm;
float4 a = Texture2DSample(Tex, TexSampler, uv + float2(0.010, 0.004) * T);
float4 b = Texture2DSample(Tex, TexSampler, uv * 1.37 + float2(0.37, 0.61) + float2(-0.006, 0.008) * T);
float blot = a.r * 0.6 + b.r * 0.4;
float shallow = 1.0 - smoothstep(0.0, ShallowCm, d);
float3 col = lerp(Deep, Shallow, saturate(shallow + (blot - 0.5) * 0.25));
float n = Texture2DSample(Tex, TexSampler, float2(along / 2000.0, 0.5)).r;
float rim = 1.0 - smoothstep(7.0, 10.0, d + sin(along / 60.0 + n * 6.0) * 2.0);
float r = 30.0 + sin(T * 0.8 + n * 3.0) * 5.0;
float dash = (1.0 - smoothstep(2.0, 4.0, abs(d - r + n * 10.0))) * step(0.45 + 0.3 * n, frac(along / 90.0 + n * 2.0));
float wave = saturate(a.g * smoothstep(0.4, 0.9, 0.5 + 0.5 * sin(T * 1.2 + b.r * 9.0))) * 0.6;
float spark = a.b * smoothstep(0.6, 1.0, 0.5 + 0.5 * sin(T * 4.0 + b.r * 30.0));
col = lerp(col, float3(1.0, 1.0, 1.0), max(max(rim, dash), wave));
return col + spark * 0.8;
"""
POND_ROCK = "/Game/Game/Environment/Nature/SM_Nature_Boulder_02"
POND_ROCK_MI = "/Game/Game/Environment/Nature/MI_Nature_Leafs_Rock"
# 물속 바위 (연못 중심에서 x, y, 크기 배율, 잠김 cm) — steam_05 처럼 물 위로 등만 내민다. 낚시찌(중심)는 비킨다
POND_ROCKS = [(190.0, 90.0, 0.9, 25.0), (-150.0, -170.0, 0.6, 20.0), (-60.0, 230.0, 0.45, 15.0)]

# 목장 섬 = 레벨 바닥 전체(Plane ×50 한 장, ±2500). 사냥터는 하벤 만 지도(`islands.py`) — 바다 벽 틀은 지도 전체를 덮는
# 정사각(가운데 원점 ± HALF_EXTENT), 해안·존은 거리장·존 칸 자료(`ASeaActor` Field)가 정한다.
SEA_Z = -10.0             # 수면을 섬 바닥 바로 아래 — 섬 가장자리에서 물이 이어져 보인다
SPOT_Z = SEA_Z + 2.0
LAND_WATER_Z = 1.0        # 연못·강 원판(섬 위) 높이
DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
COAST_TEXTURE = GROUND_DIR + "/T_World_Coast"
MAP_TEXTURE = "/Game/Game/UI/Map/T_World_Map"

# (라벨, spotId, x, y). 사냥터 자리(작은 세계 지도 2026-10-13 — 뭍 안 호수 셋이라 늪 자리 둘은 호수 가장자리에 겹쳐 둔다, 🟡)는 지도 자료에서 잰 값(python3 로 islands.py 를 돌려 뽑음, 🟡 배치값):
# 민물 = 뭍 안 호수 가운데(평원·가을 숲·얼음 호수, 호수도 바다 수면이 비친다), 늪 = 늪 호수 둘 가운데,
# 해안 = 시작 거점(헤이븐)·눈방울 거점 앞 해안에서 바다 쪽 ≈ 250cm(8.9 근거 칸), 먼바다 = 포구만 가운데·남동 바다(해안에서 2000cm 이상).
LEVELS = {
    "/Game/Game/Maps/L_Ranch": {
        "sea_center": (0.0, 0.0),
        "island_half": 2500.0,
        "spots": [
            ("FishSpot_pond_1", "pond_1", -1700.0, 1700.0),
            ("FishSpot_pond_2", "pond_2", 1700.0, -1700.0),
            ("FishSpot_coast_1", "coast_1", 2750.0, 600.0),
            ("FishSpot_coast_2", "coast_2", -600.0, -2750.0),
            ("FishSpot_far_1", "far_1", 4500.0, -1000.0),
        ],
        # 연못 물 원판 (라벨, x, y, 지름cm)
        "ponds": [("Pond_1", -1700.0, 1700.0, 700.0), ("Pond_2", 1700.0, -1700.0, 700.0)],
        "rivers": [],
    },
    "/Game/Game/Maps/L_Hunt": {
        "sea_center": (0.0, 0.0),
        "island_half": float(islands.HALF_EXTENT),
        "field": True,
        "spots": [
            ("FishSpot_river_1", "river_1", -21800.0, 39200.0),
            ("FishSpot_river_2", "river_2", -9000.0, -38400.0),
            ("FishSpot_river_3", "river_3", 19000.0, 1600.0),
            ("FishSpot_coast_3", "coast_3", 5857.0, -8376.0),
            ("FishSpot_coast_4", "coast_4", 13718.0, 3343.0),
            ("FishSpot_far_2", "far_2", 5468.0, -12324.0),
            ("FishSpot_far_3", "far_3", -28050.0, 11903.0),
            ("FishSpot_swamp_1", "swamp_1", -9000.0, -37900.0),
            ("FishSpot_swamp_2", "swamp_2", -21800.0, 39700.0),
        ],
        "ponds": [],
        "rivers": [],
        # 옛 작은 섬 판의 물 — 호수가 대신한다
        "remove": ("SwampPond_", "River_1"),
    },
}


def _field(sea):
    """`tools/unreal/data/world_*.bin` → ASeaActor 거리장·존 칸 + 해안·지도 텍스처."""
    import array
    coast = array.array("h")
    with open(os.path.join(DATA_DIR, "world_coast.bin"), "rb") as f:
        coast.frombytes(f.read())
    zone = array.array("B")
    with open(os.path.join(DATA_DIR, "world_zone.bin"), "rb") as f:
        zone.frombytes(f.read())
    sea.set_editor_property("field_origin_cm", unreal.Vector2D(islands.X0, islands.Y0))
    sea.set_editor_property("coast_cell_cm", float(islands.FIELD_CM))
    sea.set_editor_property("coast_field_size", unreal.IntPoint(islands.NI, islands.NJ))
    sea.set_editor_property("coast_field", list(coast))
    sea.set_editor_property("zone_cell_cm", float(islands.CELL_CM))
    sea.set_editor_property("zone_field_size", unreal.IntPoint(islands.ZI, islands.ZJ))
    sea.set_editor_property("zone_field", list(zone))
    sea.set_editor_property("coast_texture", _load(COAST_TEXTURE))
    sea.set_editor_property("map_texture", _load(MAP_TEXTURE))


def _import_world_textures():
    """T_World_Coast(물 셰이더 해안 거리) · T_World_Map(미니맵·지도 화면) — `make_world_map.py` 원본."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for path, png, gray in ((COAST_TEXTURE, "T_World_Coast.png", True), (MAP_TEXTURE, "T_World_Map.png", False)):
        source = os.path.join(unreal.Paths.project_saved_dir(), "Generated", png)
        if not os.path.exists(source):
            unreal.log_error("[Sea] 원본 없음 — make_world_map.py 먼저: {}".format(source))
            raise SystemExit(1)
        task = unreal.AssetImportTask()
        task.filename = source
        task.destination_path, task.destination_name = path.rsplit("/", 1)
        task.automated = True
        task.replace_existing = True
        task.save = False
        task.factory = unreal.TextureFactory()
        tools.import_asset_tasks([task])
        texture = _load(path)
        if gray:   # 값이 거리다 — 색 보정·압축 없이 8비트 그대로
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        else:
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def _load(path):
    return unreal.EditorAssetLibrary.load_asset(path)


def _ensure_materials():
    parent = _load(NATURE_GRASS).get_editor_property("parent")
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    made = {}
    for name, color in MATERIALS.items():
        path = "{}/{}".format(GROUND_DIR, name)
        mat = _load(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(
            name, GROUND_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mat, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat, DIFFUSE_COLOR_PARAM, color)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat, DIFFUSE_WEIGHT_PARAM, 0.0)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        made[name] = mat
    return made


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _find_or_spawn(actor_class, label, location):
    for actor in _actors().get_all_level_actors():
        if actor.get_actor_label() == label:
            actor.set_actor_location(location, False, False)
            return actor
    actor = _actors().spawn_actor_from_class(actor_class, location)
    actor.set_actor_label(label)
    return actor


def _water_mesh(label, mesh_path, location, scale, material):
    actor = _find_or_spawn(unreal.StaticMeshActor, label, location)
    comp = actor.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_static_mesh(_load(mesh_path))
    comp.set_material(0, material)
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    comp.set_editor_property("cast_shadow", False)
    actor.set_actor_scale3d(scale)


def _water_material():
    """T_Sea_Waves 반입 → M_Sea_Water(그래프를 매번 새로) → MI_Sea_Water 부모 교체."""
    if not os.path.exists(WAVE_PNG):
        unreal.log_error("[Sea] 원본 없음 — make_sea_waves.py 먼저: {}".format(WAVE_PNG))
        raise SystemExit(1)
    assets = unreal.EditorAssetLibrary
    lib = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    task = unreal.AssetImportTask()
    task.filename = WAVE_PNG
    task.destination_path = GROUND_DIR
    task.destination_name = WAVE_TEXTURE.rsplit("/", 1)[1]
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = unreal.TextureFactory()
    tools.import_asset_tasks([task])
    texture = _load(WAVE_TEXTURE)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    assets.save_asset(WAVE_TEXTURE, only_if_is_dirty=False)

    # 에셋은 두고 노드만 비워 새로 짠다 — 지우면 자식 MI 부모가 끊긴다
    material = _load(WATER_MATERIAL) if assets.does_asset_exist(WATER_MATERIAL) else tools.create_asset(
        WATER_MATERIAL.rsplit("/", 1)[1], GROUND_DIR, unreal.Material, unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(material)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    custom = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property("code", WATER_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ["WP", "T", "Tex", "TileCm", "Center", "Half", "ShallowCm", "Deep", "Shallow", "FieldOn", "Box", "CoastTex"]
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)

    def scalar(name, value, y):
        node = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    def vector(name, value, y):
        node = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    world = lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -800, -300)
    mask = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -600, -300)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", True)
    lib.connect_material_expressions(world, "", mask, "")
    time = lib.create_material_expression(material, unreal.MaterialExpressionTime, -600, -200)
    tex = lib.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -600, -100)
    tex.set_editor_property("parameter_name", "WaveTex")
    tex.set_editor_property("texture", texture)
    sources = {
        "WP": mask, "T": time, "Tex": tex,
        "TileCm": scalar("TileCm", TILE_CM, 0),
        "Center": vector("IslandCenter", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 100),
        "Half": scalar("IslandHalfCm", NO_ISLAND_HALF, 200),
        "ShallowCm": scalar("ShallowCm", SHALLOW_CM, 300),
        "Deep": vector("DeepColor", DEEP_COLOR, 400),
        "Shallow": vector("ShallowColor", SHALLOW_COLOR, 500),
        "FieldOn": scalar("FieldOn", 0.0, 600),
        "Box": vector("FieldBox", unreal.LinearColor(0.0, 0.0, 1.0, 1.0), 700),
    }
    # 해안 거리 텍스처 — `ASeaActor::Build` 가 MID 로 넣는다(목장은 FieldOn 0 이라 안 읽는다)
    coast = lib.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -600, 800)
    coast.set_editor_property("parameter_name", "CoastTex")
    coast.set_editor_property("texture", texture)
    sources["CoastTex"] = coast
    for name in names:
        # 벡터 파라미터 기본 출력("")은 RGB 셋뿐 — 틀(Box.zw)은 넷째 칸까지 쓰니 RGBA 로 잇는다(Metal: "swizzle 'w' out of bounds")
        if not lib.connect_material_expressions(sources[name], "RGBA" if name == "Box" else "", custom, name):
            unreal.log_error("[Sea] 셰이더 입력 연결 실패: {}".format(name))
            raise SystemExit(1)
    lib.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    assets.save_asset(WATER_MATERIAL, only_if_is_dirty=False)

    mi = _load(WATER_INSTANCE) if assets.does_asset_exist(WATER_INSTANCE) else tools.create_asset(
        WATER_INSTANCE.rsplit("/", 1)[1], GROUND_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi, material)
    lib.clear_all_material_instance_parameters(mi)
    assets.save_asset(WATER_INSTANCE, only_if_is_dirty=False)
    unreal.log("[Sea] 물결 셰이더: {} ← {}".format(WATER_INSTANCE, WATER_MATERIAL))
    return mi


def _pond_materials(texture):
    """M_Pond_Water(바운드로 물가 거리) → 연못용 MI(Round 1)·강용 MI(Round 0)."""
    assets = unreal.EditorAssetLibrary
    lib = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = _load(POND_MATERIAL) if assets.does_asset_exist(POND_MATERIAL) else tools.create_asset(
        POND_MATERIAL.rsplit("/", 1)[1], GROUND_DIR, unreal.Material, unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(material)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    custom = lib.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property("code", POND_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ["WP", "T", "Tex", "TileCm", "ObjPos", "Bounds", "MeshUV", "Round", "ShallowCm", "Deep", "Shallow"]
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)

    def param(cls, name, value, y):
        node = lib.create_material_expression(material, cls, -600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    world = lib.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -800, -300)
    tex = lib.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -600, -100)
    tex.set_editor_property("parameter_name", "WaveTex")
    tex.set_editor_property("texture", texture)
    sources = {
        "WP": world,
        "T": lib.create_material_expression(material, unreal.MaterialExpressionTime, -600, -200),
        "Tex": tex,
        "TileCm": param(unreal.MaterialExpressionScalarParameter, "TileCm", TILE_CM, 0),
        "ObjPos": lib.create_material_expression(material, unreal.MaterialExpressionObjectPositionWS, -800, 100),
        "Bounds": lib.create_material_expression(material, unreal.MaterialExpressionObjectBounds, -800, 200),
        "MeshUV": lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -800, 250),
        "Round": param(unreal.MaterialExpressionScalarParameter, "Round", 1.0, 300),
        "ShallowCm": param(unreal.MaterialExpressionScalarParameter, "ShallowCm", POND_SHALLOW_CM, 400),
        "Deep": param(unreal.MaterialExpressionVectorParameter, "DeepColor", POND_DEEP, 500),
        "Shallow": param(unreal.MaterialExpressionVectorParameter, "ShallowColor", POND_SHALLOW, 600),
    }
    for name in names:
        if not lib.connect_material_expressions(sources[name], "", custom, name):
            unreal.log_error("[Sea] 연못 셰이더 입력 연결 실패: {}".format(name))
            raise SystemExit(1)
    lib.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    assets.save_asset(POND_MATERIAL, only_if_is_dirty=False)

    made = {}
    for path, round_value in ((POND_ROUND, 1.0), (POND_STRIP, 0.0)):
        mi = _load(path) if assets.does_asset_exist(path) else tools.create_asset(
            path.rsplit("/", 1)[1], GROUND_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        lib.set_material_instance_parent(mi, material)
        lib.clear_all_material_instance_parameters(mi)
        lib.set_material_instance_scalar_parameter_value(mi, "Round", round_value)
        assets.save_asset(path, only_if_is_dirty=False)
        made[path] = mi
    unreal.log("[Sea] 연못·강 셰이더: {} · {}".format(POND_ROUND, POND_STRIP))
    return made


def _pond_rocks(label, x, y):
    mesh = _load(POND_ROCK)
    rock_mi = _load(POND_ROCK_MI)
    for i, (dx, dy, scale, sink) in enumerate(POND_ROCKS):
        rock = _find_or_spawn(unreal.StaticMeshActor, "{}_Rock_{}".format(label, i + 1),
                              unreal.Vector(x + dx, y + dy, LAND_WATER_Z - sink))
        comp = rock.static_mesh_component
        comp.set_static_mesh(mesh)
        for slot in range(comp.get_num_materials()):
            comp.set_material(slot, rock_mi)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        rock.set_actor_rotation(unreal.Rotator(0.0, 0.0, 70.0 * i + 20.0), False)
        rock.set_actor_scale3d(unreal.Vector(scale, scale, scale * 0.7))


# `setup_pond_material.py` 가 위 함수만 빌려 쓴다 — 가져올 땐 레벨을 건드리지 않는다
if __name__ == "__main__":
    _import_world_textures()
    materials = _ensure_materials()
    materials["MI_Sea_Water"] = _water_material()
    pond_materials = _pond_materials(_load(WAVE_TEXTURE))
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    for level_path, layout in LEVELS.items():
        level_subsystem.load_level(level_path)

        sea_x, sea_y = layout["sea_center"]
        sea = _find_or_spawn(unreal.SeaActor, "Sea", unreal.Vector(sea_x, sea_y, SEA_Z))
        sea.set_editor_property("water_material", materials["MI_Sea_Water"])
        sea.set_editor_property("island_half_extent_cm", layout["island_half"])
        sea.set_editor_property("island_floor_z", 0.0)
        if layout.get("field"):
            _field(sea)
        for actor in _actors().get_all_level_actors():
            if actor.get_actor_label().startswith(layout.get("remove", ())):
                unreal.log("[Sea] 옛 물 걷어냄: {}".format(actor.get_actor_label()))
                _actors().destroy_actor(actor)
        for label, x, y, diameter in layout["ponds"]:
            # 지름 100 원판(M18-04 `make_ranch_ground_meshes.py` → `setup_ranch_ground.py`) — 옛 납작 원기둥 자리
            _water_mesh(label, GROUND_DIR + "/SM_Ground_PondDisc_01", unreal.Vector(x, y, LAND_WATER_Z),
                        unreal.Vector(diameter / 100.0, diameter / 100.0, 1.0), pond_materials[POND_ROUND])
            _pond_rocks(label, x, y)
        for label, x, y, width, length in layout["rivers"]:
            _water_mesh(label, "/Engine/BasicShapes/Plane", unreal.Vector(x, y, LAND_WATER_Z),
                        unreal.Vector(width / 100.0, length / 100.0, 1.0), pond_materials[POND_STRIP])

        for label, spot_id, x, y in layout["spots"]:
            on_land = spot_id.startswith(("pond_", "river_", "swamp_"))
            spot = _find_or_spawn(unreal.FishingSpotActor, label, unreal.Vector(x, y, LAND_WATER_Z + 2.0 if on_land else SPOT_Z))
            spot.set_editor_property("spot_id", spot_id)
            spot.set_editor_property("shadow_material", materials["MI_Sea_FishShadow"])
            unreal.log("[Sea] {} {} ({:.0f}, {:.0f})".format(level_path, spot_id, x, y))

        level_subsystem.save_current_level()
        unreal.log("[Sea] 저장: {} 바다 폭 {} · 포인트 {}".format(
            level_path, sea.get_editor_property("built_sea_width_cm"), len(layout["spots"])))
