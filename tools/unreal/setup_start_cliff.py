"""M17-07 절벽 단 — `L_Hunt`·`L_Ranch` 의 PlayerStart 화면 안에 풀 덮인 바위 절벽 둔덕 하나를 세운다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_start_cliff.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

레퍼런스(steam_05·17)는 평지가 아니라 풀 둔덕이 회색 바위 절벽으로 끊긴다. 고원 메시(`SM_Ground_Plateau_02`, M15-04)는
이미 들어와 있으니 새 에셋 없이 그것 하나를 시작 화면 안 빈 자리(`OFFSETS` 순서대로 첫 빈 곳)에 세운다.
자리·크기는 전부 배치값(🟡). 바닥은 두 맵 모두 높이 0(흙길 원판 `TOP_Z` 1.5 와 같은 전제)이고 고원은 50cm 묻혀 있다.
그 자리에 든 `StartDecor_` 소품은 지우고, 다른 구조물이 걸리면 세우지 않고 경고만 남긴다. 재실행 안전: `StartCliff`·`StartCliffRock_` 를 지우고 다시.

M19-04: 절벽 머티리얼(`MI_Ground_Cliff`) 색을 `setup_plateaus.py` 의 `CLIFF_COLOR`(회색 쪽) 로 다시 칠하고,
단 밑동 카메라 쪽에 둥근 바위(`BASE_ROCKS`) 를 반쯤 박아 둔다 — 원본 절벽은 발치에 바위가 몇 개 붙는다(steam_05·17·18·19).
"""
import math
import os
import random
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup_plateaus  # noqa: E402  — `__main__` 가 아니면 머티리얼 함수만 빌려 온다

MESH = "/Game/Game/Environment/Ground/SM_Ground_Plateau_02"
SOURCE_RADIUS_CM = 500.0        # make_plateau_meshes.py SHAPES 반지름
RADIUS_CM = 300.0
FOOTPRINT = 1.3                 # 윤곽 물결 최대 ≈ 반지름 1.3배 (setup_plateaus.py 와 같다)
# 화면(X -620~940, Y ±1080) 안 후보 — 앞에서부터 건물·구조물이 안 걸리는 첫 자리를 쓴다
OFFSETS = [(720.0, -760.0), (720.0, 760.0), (-380.0, -820.0), (-380.0, 820.0), (760.0, 0.0), (-420.0, 0.0)]
LABEL = "StartCliff"
ROCK_LABEL = "StartCliffRock_"
# (메시, 높이 cm) — 밑동 바위 넷. 카메라는 -X 쪽에서 내려다보니 그쪽 반원에 몰아 둔다(각도 도, +X 기준 반시계)
BASE_ROCKS = [("SM_Nature_Boulder_01", 110.0), ("SM_Nature_Boulder_02", 80.0), ("SM_Nature_Boulder_01", 70.0),
              ("SM_Nature_Boulder_02", 95.0)]
BASE_ROCK_ANGLES = [150.0, 195.0, 235.0, 285.0]
BASE_ROCK_DIST = 1.15           # 반지름 배 — 윤곽 물결(≤1.3) 안팎이라 옆면에 반쯤 박힌다
SEED = 19004
NATURE_DIR = "/Game/Game/Environment/Nature"
DECOR_PREFIX = "StartDecor_"
MAPS = [
    ("/Game/Game/Maps/L_Hunt", "MinStagePlayerStart"),
    ("/Game/Game/Maps/L_Ranch", "RanchPlayerStart"),
]

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _build(level_path: str, start_label: str) -> None:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level_path)
    everything = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    for label in [k for k in everything if k == LABEL or k.startswith(ROCK_LABEL)]:
        actors.destroy_actor(everything.pop(label))
    start = everything.get(start_label)
    if start is None:
        raise RuntimeError("{} 에 {} 가 없다".format(level_path, start_label))
    at = start.get_actor_location()
    reach = RADIUS_CM * FOOTPRINT
    for offset in OFFSETS:
        x, y = float(at.x) + offset[0], float(at.y) + offset[1]
        blockers = []
        for label, actor in everything.items():
            if label.startswith((DECOR_PREFIX, "WorldGround_", "ZoneProps_", "DirtPath")) or label == start_label:
                continue
            if actor.get_class().get_name() in ("SeaActor", "PlayerStart", "Sky", "DirectionalLight", "SkyLight"):
                continue
            center, extent = actor.get_actor_bounds(False)
            if max(extent.x, extent.y) > 800.0 or extent.z < 5.0:
                continue            # 바닥 판·바다 같은 넓고 납작한 것은 대상이 아니다
            if math.hypot(center.x - x, center.y - y) < reach + max(extent.x, extent.y):
                blockers.append(label)
        if not blockers:
            break
        unreal.log("[StartCliff] {} 오프셋 {} 걸림: {}".format(level_path, offset, blockers[:3]))
    else:
        unreal.log_warning("[StartCliff] {} 빈 자리가 없어 세우지 않는다".format(level_path))
        return
    removed = 0
    for label, actor in everything.items():
        if label.startswith(DECOR_PREFIX):
            spot = actor.get_actor_location()
            if math.hypot(spot.x - x, spot.y - y) < reach + 60.0:
                actors.destroy_actor(actor)
                removed += 1

    scale = RADIUS_CM / SOURCE_RADIUS_CM
    cliff = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, 0.0), unreal.Rotator(0.0, 0.0, 40.0))
    cliff.static_mesh_component.set_static_mesh(unreal.EditorAssetLibrary.load_asset(MESH))
    cliff.set_actor_label(LABEL)
    cliff.set_actor_scale3d(unreal.Vector(scale, scale, 1.0))
    rocks = _base_rocks(level_path, x, y)
    unreal.log("[StartCliff] {} ({:.0f},{:.0f}) 반지름 {:.0f} — 소품 {} 지움, 밑동 바위 {}".format(
        level_path, x, y, RADIUS_CM, removed, rocks))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


def _base_rocks(level_path: str, x: float, y: float) -> int:
    rng = random.Random("{}:{}".format(SEED, level_path))
    for n, ((name, height), angle) in enumerate(zip(BASE_ROCKS, BASE_ROCK_ANGLES)):
        mesh = unreal.EditorAssetLibrary.load_asset("{}/{}".format(NATURE_DIR, name))
        box = mesh.get_bounding_box()
        scale = height / max(box.max.z - box.min.z, 1.0)
        dist = RADIUS_CM * BASE_ROCK_DIST * rng.uniform(0.95, 1.05)
        at = unreal.Vector(x + dist * math.cos(math.radians(angle)), y + dist * math.sin(math.radians(angle)),
                           -height * 0.2)   # 땅에 조금 묻는다
        rock = actors.spawn_actor_from_class(unreal.StaticMeshActor, at, unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
        rock.static_mesh_component.set_static_mesh(mesh)
        rock.set_actor_label("{}{:02d}".format(ROCK_LABEL, n + 1))
        rock.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return len(BASE_ROCKS)


if setup_plateaus.__name__ == "__main__":
    raise SystemExit("[StartCliff] setup_plateaus 가 전체 실행으로 불렸다 — 레벨을 건드렸을 수 있다")
cliff_mi = setup_plateaus._cliff_material()
color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(cliff_mi, "DiffuseColor")
unreal.log("[StartCliff] {} 색 ({:.3f},{:.3f},{:.3f})".format(setup_plateaus.CLIFF, color.r, color.g, color.b))
for level_path, start_label in MAPS:
    _build(level_path, start_label)
