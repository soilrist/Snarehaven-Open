"""M17-07 흙길 — `L_Hunt`·`L_Ranch` 의 PlayerStart 에서 뻗는 구불구불한 흙길을 납작한 원판으로 깐다.

실행 (절대경로 필수, docs/checks/unreal.md):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_dirt_path.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

레퍼런스(steam_10)는 풀밭 위에 색이 다른 흙길이 가로지른다. 기존 흙 머티리얼(`MI_Ground_Dirt`)과 기본 원기둥만 쓴다.
충돌 없음(걷는 데 방해 안 됨) · 그림자 없음 · 바닥(0) 바로 위 `TOP_Z`. 값은 전부 배치값(🟡). 재실행 안전: `DirtPath` 액터를 지우고 다시 만든다.

M82-01: 원판을 120cm 마다 지름·옆걸음을 무작위로 흔들어 깔면 가장자리가 동그라미를 겹친 물결로 보였다(w03). 이제 ① 40cm 마다
부드러운 사인으로만 흔들어 가장자리가 매끈하고 ② 가운데 흙 아래에 더 넓은 띠 둘(`MI_Ground_DirtEdge1`·`2` — 흙색과 풀색을 섞은 중간색)을
먼저 깔아 가장자리가 풀로 번져 간다. 불투명 원판만 쓴다(반투명은 겹친 곳이 진해진다).
"""
import math

import unreal

DIRT = "/Game/Game/Environment/Ground/MI_Ground_Dirt"
LEVELS = (
    ("/Game/Game/Maps/L_Hunt", "MinStagePlayerStart"),
    ("/Game/Game/Maps/L_Ranch", "RanchPlayerStart"),
)
LABEL = "DirtPath"
LENGTH_CM = 2400.0     # 시작점에서 한쪽 끝까지 — 카메라 절두체(가로 약 16m)를 가로지르게
STEP_CM = 40.0         # 이전 120 — 지름 300 에서 이음새 물결 깊이 13cm → 1.3cm
DIAMETER_CM = 300.0    # 길 폭 평균. 아래 사인 둘이 ±70 안에서 천천히 굵었다 가늘어진다(무작위 흔들림 없음)
DIAMETER_WAVES = ((45.0, 310.0, 0.0), (25.0, 137.0, 1.3))   # (진폭, 파장/2π cm, 위상)
BEND_CM = 260.0        # 굽이 진폭(이전 값 그대로) + 작은 사인 하나
BEND_SMALL = (50.0, 170.0)
TOP_Z = 1.5            # 바닥(0) 바로 위 — 모래(1.5) 와 같은 높이, 서로 안 겹치는 자리
THICK_CM = 1.0
GRASS = unreal.LinearColor(0.044, 0.128, 0.050, 1.0)   # 풀밭 `MI_Ground_PlainsStrokes` Tint 0.055/0.160/0.063 × 텍스처 평균 0.8
# 층 (MI 이름, 지름 배율, 흙색 비율, 높이 더함 cm) — 아래 층부터. 바깥 띠일수록 풀색에 가깝다
LAYERS = (
    ("MI_Ground_DirtEdge1", 1.55, 0.30, 0.0),
    ("MI_Ground_DirtEdge2", 1.27, 0.62, 0.3),
    (None, 1.0, 1.0, 0.6),   # 가운데 = 흙 MI 그대로
)
DIFFUSE_COLOR_PARAM = "DiffuseColor"
DIFFUSE_WEIGHT_PARAM = "DiffuseColorMapWeight"

assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _add_hism(actor):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = subsystem.k2_gather_subobject_data_for_instance(actor)[0]
    handle, fail = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
        parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent))
    if not fail.is_empty():
        raise RuntimeError("HISM 추가 실패: {}".format(fail))
    sub = unreal.SubobjectDataBlueprintFunctionLibrary
    comp = sub.get_associated_object(sub.get_data(handle))
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    return comp


def _edge_material(name, ratio):
    """흙 MI 와 같은 부모로 흙색:풀색 = ratio:1-ratio 단색 MI 를 만들거나(있으면 색만 다시) 돌려준다."""
    dirt = assets.load_asset(DIRT)
    path = DIRT.rsplit("/", 1)[0] + "/" + name
    mel = unreal.MaterialEditingLibrary
    base = mel.get_material_instance_vector_parameter_value(dirt, DIFFUSE_COLOR_PARAM)
    color = unreal.LinearColor(*(a * ratio + b * (1.0 - ratio) for a, b in ((base.r, GRASS.r), (base.g, GRASS.g), (base.b, GRASS.b))), 1.0)
    mi = assets.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DIRT.rsplit("/", 1)[0], unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, dirt.get_editor_property("parent"))
    mel.set_material_instance_vector_parameter_value(mi, DIFFUSE_COLOR_PARAM, color)
    mel.set_material_instance_scalar_parameter_value(mi, DIFFUSE_WEIGHT_PARAM, 0.0)
    assets.save_asset(path, only_if_is_dirty=False)
    unreal.log("[DirtPath] {} ← ({:.3f}, {:.3f}, {:.3f})".format(name, color.r, color.g, color.b))
    return mi


def _centre(s):
    """시작점에서 Y 로 s cm 간 자리의 X 옆걸음과 지름. s 가 음수여도 이어진다."""
    x = BEND_CM * math.sin(s / 520.0) + BEND_SMALL[0] * math.sin(s / BEND_SMALL[1] + 0.7)
    d = DIAMETER_CM + sum(a * math.sin(s / w + p) for a, w, p in DIAMETER_WAVES)
    return x, d


MATERIALS = [(_edge_material(name, ratio) if name else assets.load_asset(DIRT)) for name, _scale, ratio, _dz in LAYERS]

for level, start_label in LEVELS:
    levels.load_level(level)
    start = None
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == LABEL:
            actors.destroy_actor(actor)
        elif actor.get_actor_label() == start_label:
            start = actor.get_actor_location()
    if start is None:
        raise RuntimeError("{} 에 {} 가 없다".format(level, start_label))
    path = actors.spawn_actor_from_class(unreal.Actor, unreal.Vector(0.0, 0.0, 0.0))
    path.set_actor_label(LABEL)
    count = 0
    for (name, scale, _ratio, dz), material in zip(LAYERS, MATERIALS):
        comp = _add_hism(path)
        comp.set_static_mesh(assets.load_asset("/Engine/BasicShapes/Cylinder"))
        comp.set_material(0, material)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_editor_property("cast_shadow", False)
        s = -LENGTH_CM
        while s <= LENGTH_CM:
            # 주축 Y 로 뻗으며 X 로 굽는다 — 화면 어느 방향이든 시작점 곁을 지난다
            x, diameter = _centre(s)
            d = diameter * scale
            comp.add_instance(unreal.Transform(
                location=unreal.Vector(start.x + x, start.y + s, TOP_Z + dz - THICK_CM * 0.5),
                rotation=unreal.Rotator(0.0, 0.0, 0.0),
                scale=unreal.Vector(d / 100.0, d / 100.0, THICK_CM / 100.0)), False)
            count += 1
            s += STEP_CM
    levels.save_current_level()
    unreal.log("[DirtPath] {} 흙 원판 {}개 · 층 {} (시작점 {:.0f},{:.0f})".format(level, count, len(LAYERS), start.x, start.y))
