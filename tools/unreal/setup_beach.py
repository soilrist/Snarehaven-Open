"""M15-04 모래 해변 — 섬 가장자리 안쪽에 납작한 모래 원판을 이어 붙여 레퍼런스처럼 물결진 모래 띠를 만든다.

`setup_ground_strokes.py`(획 머티리얼)·`setup_sea.py`(바다) 다음에 돌린다:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_beach.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

사냥터는 여러 섬(`islands.py`)이라 섬마다 해안선을 따라 깐다. 레퍼런스 해안(steam_10)은 모래가 곧장 물에 닿는 구불구불한 곡선이다 — 네모 섬 가장자리가 그대로 보이지 않게 원판 바깥 끝을
`coast_out(t)` (60~140cm) 만큼 바다 위로 내민다. 내민 모래는 충돌이 있어 밟으면 물에 안 들어간다(`UWaterTravelComponent` 는
발 높이가 수면 아래일 때만 수영) — 140cm 는 첫 해저 계단 폭(`ASeaActor::ShelfStepWidthCm` 150) 안이라 물에서 40cm 턱으로 올라온다.
모래 원판마다 조금 큰 흰 거품 원판을 수면 바로 위·섬 바닥 아래에 깔아 모래 둘레에만 흰 테가 보이게 한다(섬 안은 바닥이 덮는다). 눈 칸 가장자리와 강 끝은 건너뛴다. 값은 전부 배치값(🟡). 재실행 안전: `Beach` 액터를 지우고 다시 만든다.
"""
import math
import os
import random
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

GROUND_DIR = "/Game/Game/Environment/Ground"
SAND = GROUND_DIR + "/MI_Ground_SandStrokes"
SAND_TINT = unreal.LinearColor(1.0, 0.61, 0.135, 1.0)  # steam_10 모래 (226,199,121). 1.0/0.80/0.36 은 햇빛·톤매핑에 바래 (230,225,190) 크림색(09-25 캡처) — 리니어 비로 G·B 를 내림
DIAMETER_CM = (400.0, 650.0)    # 뭍쪽 모래 깊이 = 지름 − coast_out ≈ 2.6~5.9m (steam_17 해변 ≈ 캐릭터 키 2~3배). 500~1000 은 8.6m 까지 파고들어 목장 앞이 노란 얼룩이었다(2026-09-25 캡처 house_in)
STEP_CM = 220.0                 # 가장 작은 원판(400)끼리도 ±60 흔들림에 틈이 안 나게
FOAM = "/Game/Game/Environment/Ground/MI_Sea_Foam"
FOAM_RIM_CM = 28.0              # 모래 둘레 흰 테 폭 (steam_10 바위·모래 둘레 거품 ≈ 7px ≈ 28cm)
FOAM_Z = -8.0                   # 수면(-10) 바로 위, 섬 바닥(0) 아래
TOP_Z = 1.5                     # 바닥(0) 바로 위 — 연못·강(1.0) 보다 위라 강 끝은 건너뛴다
THICK_CM = 1.0

LEVELS = {
    "/Game/Game/Maps/L_Ranch": {"center": (0.0, 0.0), "half": 2500.0, "skip": []},
    # L_Hunt 는 하벤 만 지도 땅(`setup_world_ground.py`, 2026-09-25)이 대신한다 — 옛 작은 섬 판을 다시 깔지 않는다.
}

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _sand_material():
    if not assets.does_asset_exist(SAND):
        folder, name = SAND.rsplit("/", 1)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = assets.load_asset(SAND)
    lib.set_material_instance_parent(mi, assets.load_asset(GROUND_DIR + "/M_Ground_Strokes"))
    lib.set_material_instance_vector_parameter_value(mi, "Tint", SAND_TINT)
    assets.save_asset(SAND, only_if_is_dirty=False)
    return mi


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


def _skipped(x, y, d, skip):
    r = d * 0.5
    return any(x + r > x0 and x - r < x1 and y + r > y0 and y - r < y1 for x0, x1, y0, y1 in skip)


def coast_out(t):
    """가장자리 t(섬 가운데 기준 cm)에서 모래가 바다로 내미는 거리. `setup_sea.py` WATER_HLSL 의 `o` 와 같은 식 — 바꾸면 둘 다."""
    return 100.0 + 25.0 * math.sin(t / 430.0 + 1.3) + 15.0 * math.sin(t / 190.0 + 0.4)


def _edge_points(cx, cy, half, rng):
    """네 변을 따라 (x, y, 지름). 원판 바깥 끝이 가장자리 + coast_out 에 닿는다."""
    points = []
    for axis, sign in ((0, 1), (0, -1), (1, 1), (1, -1)):
        t = -half
        while t <= half:
            d = rng.uniform(*DIAMETER_CM)
            along = t + rng.uniform(-60.0, 60.0)
            inset = half + coast_out(along) - d * 0.5
            along = max(-inset, min(inset, along))   # 모서리에서도 바깥으로 안 나가게
            x, y = (cx + sign * inset, cy + along) if axis == 0 else (cx + along, cy + sign * inset)
            points.append((x, y, d, ""))
            t += STEP_CM
    return points


def _island_edge_points(rng):
    """섬마다 해안선을 한 바퀴 — 가운데에서 각 θ 로 뻗어 만난 해안점에서 바깥(거리장 기울기)으로 coast_out 만큼 내민다."""
    points = []
    for name, icx, icy, _, _, _, _ in islands.ISLANDS:
        theta, last, gap = 0.0, None, STEP_CM
        while theta < math.tau:
            hx, hy = islands.coast_crossing(icx, icy, math.cos(theta), math.sin(theta))
            theta += 20.0 / max(math.hypot(hx - icx, hy - icy), 1.0)   # 긴 변을 비스듬히 지날 때도 촘촘히 — 간격은 아래서 잰다
            if last and math.hypot(hx - last[0], hy - last[1]) < gap:
                continue
            last, gap = (hx, hy), STEP_CM + rng.uniform(-60.0, 60.0)
            d = rng.uniform(*DIAMETER_CM)
            gx = islands.signed_distance(hx + 5.0, hy) - islands.signed_distance(hx - 5.0, hy)
            gy = islands.signed_distance(hx, hy + 5.0) - islands.signed_distance(hx, hy - 5.0)
            g = math.hypot(gx, gy) or 1.0
            push = coast_out(islands.coast(hx, hy)[1]) - d * 0.5   # WATER_HLSL 의 o 와 같은 along
            points.append((hx + gx / g * push, hy + gy / g * push, d, name))
    return points


material = _sand_material()
cylinder = assets.load_asset("/Engine/BasicShapes/Cylinder")
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
for level, layout in LEVELS.items():
    level_subsystem.load_level(level)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == "Beach":
            actors.destroy_actor(actor)
    cx, cy = layout["center"]
    beach = actors.spawn_actor_from_class(unreal.Actor, unreal.Vector(cx, cy, 0.0))
    beach.set_actor_label("Beach")
    comp = _add_hism(beach)
    comp.set_static_mesh(cylinder)
    comp.set_material(0, material)
    comp.set_collision_profile_name("BlockAll")
    comp.set_editor_property("cast_shadow", False)
    foam = _add_hism(beach)
    foam.set_static_mesh(cylinder)
    foam.set_material(0, assets.load_asset(FOAM))
    foam.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    foam.set_editor_property("cast_shadow", False)
    rng = random.Random(1504)
    count = 0
    edge = _island_edge_points(rng) if layout.get("islands") else _edge_points(cx, cy, layout["half"], rng)
    for x, y, d, name in edge:
        # 작은 섬은 온통 모래 — 눈 칸 경계 상자에 걸친 `islet_a` 가 한 귀퉁이만 모래가 빠져 초록·흰 네모가 드러났다(m1565)
        if not name.startswith("islet") and _skipped(x, y, d, layout["skip"]):
            continue
        # 기본 원기둥 = 지름 100 · 높이 100, 가운데가 원점
        comp.add_instance(unreal.Transform(
            location=unreal.Vector(x - cx, y - cy, TOP_Z - THICK_CM * 0.5),
            rotation=unreal.Rotator(0.0, 0.0, 0.0),
            scale=unreal.Vector(d / 100.0, d / 100.0, THICK_CM / 100.0)), False)
        f = d + FOAM_RIM_CM * 2.0
        foam.add_instance(unreal.Transform(
            location=unreal.Vector(x - cx, y - cy, FOAM_Z - THICK_CM * 0.5),
            rotation=unreal.Rotator(0.0, 0.0, 0.0),
            scale=unreal.Vector(f / 100.0, f / 100.0, THICK_CM / 100.0)), False)
        count += 1
    level_subsystem.save_current_level()
    unreal.log("[Beach] {} 모래 원판 {}개".format(level, count))
