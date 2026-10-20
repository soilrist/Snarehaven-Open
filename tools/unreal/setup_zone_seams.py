"""M15-04 존 경계 — 사냥터 네 칸이 맞닿는 곧은 직선을 레퍼런스처럼 구불구불한 경계로 바꾼다.

`setup_ground_strokes.py`(획 머티리얼) 다음에 돌린다:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_zone_seams.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

레퍼런스 눈·풀 경계(steam_18)는 같은 높이에서 둥근 혀가 서로 파고드는 곡선이고, 경계 근처에 반대편 땅 조각이 섬처럼 떠 있다.
우리 칸 바닥은 5000cm 판 넷이라 경계가 자로 그은 선이었다(09-25 캡처 m1549). 바닥 머티리얼이 월드 좌표 UV 라
같은 머티리얼 원판을 경계 너머에 깔면 이음매 없이 이어진다 — 경계 곡선 `wave(t)` 가 가리키는 쪽 칸의 땅을 원판으로 밀어 넣는다.
충돌 없음(바닥 0.4~0.7cm 위, 발은 바닥 판에 선다). 칸 판정(`DT_ZoneTable` 사각형)은 그대로다 — 보이는 경계만 바꾼다.
값은 전부 배치값(🟡). 재실행 안전: `ZoneSeams` 액터를 지우고 다시 만든다.
"""
import math
import os
import random
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

GROUND_DIR = "/Game/Game/Environment/Ground"
LEVEL = "/Game/Game/Maps/L_Hunt"
# 쓰지 않는다 — L_Hunt 는 하벤 만 지도 땅(`setup_world_ground.py`, 2026-09-25)이라 네 칸 곧은 경계가 없다. 돌리면 옛 이음매 원판을 새 땅 위에 깐다.
unreal.log_error("[ZoneSeams] 폐기된 스크립트 — setup_world_ground.py 를 쓴다")
raise SystemExit(1)
HALF = 2500.0                 # 칸 반폭 (DT_ZoneTable RegionHalfExtentCm)
ISLAND = (-2500.0, 7500.0)    # 경계선을 따라가는 범위 — 원판이 바다로 나가는지는 `islands.py` 해안으로 잰다
STEP_CM = 140.0               # 경계를 따라 원판 간격 — 원판(최소 지름 ~760)끼리 겹쳐 틈이 없다
MIN_BULGE_CM = 60.0           # 이보다 얕은 혀는 안 깐다(곧은 선 그대로)
ISLETS = 7                    # 경계당 반대편 땅 섬 조각 수 (steam_18 풀밭 속 눈 조각)
THICK_CM = 1.0
CORNER_CM = 900.0             # 가운데 교차점에서 이 거리 안은 혀가 비례로 작아진다

# 칸 → (바닥 MI). 좌표는 DT_ZoneTable RegionCenterCm.
CELLS = {
    (0.0, 0.0): GROUND_DIR + "/MI_Ground_PlainsStrokes",
    (5000.0, 0.0): GROUND_DIR + "/MI_Ground_SnowStrokes",
    (0.0, 5000.0): GROUND_DIR + "/MI_Ground_ForestStrokes",
    (5000.0, 5000.0): GROUND_DIR + "/MI_Ground_SwampStrokes",
}
# 경계선: (축, 선 위치, 따라가는 범위, 높이 z). 축 0 = x 가 일정한 선(설원·늪 쪽 세로선), 1 = y 가 일정한 선.
# 두 선이 가운데 (2500, 2500) 에서 겹쳐 z 를 다르게 둔다(강 1.0·모래 1.5 보다 아래).
SEAMS = [(0, 2500.0, ISLAND, 0.4), (1, 2500.0, ISLAND, 0.7)]

assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def wave(t, phase):
    """경계 곡선 — 선에서 벗어난 거리(cm, ± 는 어느 칸 쪽인지). 긴 물결 + 짧은 물결 둘을 겹친다."""
    return 420.0 * math.sin(t / 900.0 + phase) + 220.0 * math.sin(t / 330.0 + phase * 2.3)


def cell_material(x, y):
    cx = 0.0 if x < 2500.0 else 5000.0
    cy = 0.0 if y < 2500.0 else 5000.0
    return CELLS[(cx, cy)]


def _add_hism(actor, mesh, material):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = subsystem.k2_gather_subobject_data_for_instance(actor)[0]
    handle, fail = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
        parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent))
    if not fail.is_empty():
        raise RuntimeError("HISM 추가 실패: {}".format(fail))
    sub = unreal.SubobjectDataBlueprintFunctionLibrary
    comp = sub.get_associated_object(sub.get_data(handle))
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_static_mesh(mesh)
    comp.set_material(0, assets.load_asset(material))
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    comp.set_editor_property("cast_shadow", False)
    return comp


def _inside_island(x, y, r):
    """원판 전체가 뭍 — 바다 위(수면 -10)에 뜬 원판이 안 보이게."""
    return islands.on_land(x, y, margin=r)


def discs():
    """(재질, x, y, 지름, z) 목록."""
    out = []
    rng = random.Random(1525)
    for axis, line, (t0, t1), z in SEAMS:
        phase = rng.uniform(0.0, 6.28)

        def at(along, off):   # 선 기준 (따라감, 벗어남) → 월드 (x, y)
            return (line + off, along) if axis == 0 else (along, line + off)

        t = t0
        while t <= t1:
            # 네 칸이 만나는 가운데 (2500, 2500) 로 갈수록 혀를 줄여 0 으로 — 곧은 직각 모서리(두 번째 캡처) 대신 한 점으로 모인다
            taper = min(1.0, abs(t - 2500.0) / CORNER_CM)
            b = wave(t, phase) * taper
            if abs(b) >= MIN_BULGE_CM * taper:
                # b > 0 이면 선 아래쪽(−) 칸의 땅이 위쪽(+) 칸으로 b 만큼 파고든다
                src = at(t, -1.0 if b > 0 else 1.0)
                # 큰 원판의 바깥 끝만 선을 넘게 — 원판이 작으면 둥근 알이 줄줄이 보였다(첫 캡처 늪·숲 경계)
                d = abs(b) + 100.0 + 600.0 * taper
                x, y = at(t + rng.uniform(-30.0, 30.0), math.copysign(1.0, b) * (abs(b) - d * 0.5))
                # 가운데 네 칸 모서리 근처는 원판이 대각선 칸까지 덮어 엉뚱한 땅이 생긴다 — 거기만 곧은 선으로 둔다
                if _inside_island(x, y, d * 0.5) and abs(t - 2500.0) > d * 0.5:
                    out.append((cell_material(*src), x, y, d, z))
            t += STEP_CM
        # 반대편 땅 섬 조각 — 경계에서 3~9m 안쪽, 지름 2.5~6m
        for _ in range(ISLETS):
            along = rng.uniform(t0 + 600.0, t1 - 600.0)
            if abs(along - 2500.0) < 1200.0:   # 네 칸 모서리 — 위와 같은 까닭
                continue
            side = rng.choice((-1.0, 1.0))
            off = side * rng.uniform(300.0, 900.0)
            src = at(along, -side)
            base = rng.uniform(250.0, 600.0)
            for _ in range(5):   # 원판 다섯 개를 사방으로 흩어 겹친다 — 한 줄로 놓으면 애벌레 사슬로 보였다(평면도)
                x, y = at(along + rng.uniform(-0.4, 0.4) * base, off + rng.uniform(-0.3, 0.3) * base)
                d = base * rng.uniform(0.6, 1.0)
                if _inside_island(x, y, d * 0.5):
                    out.append((cell_material(*src), x, y, d, z + 0.05))
    return out


def main():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == "ZoneSeams":
            actors.destroy_actor(actor)
    root = actors.spawn_actor_from_class(unreal.Actor, unreal.Vector(0.0, 0.0, 0.0))
    root.set_actor_label("ZoneSeams")
    cylinder = assets.load_asset("/Engine/BasicShapes/Cylinder")
    comps = {}
    counts = {}
    for material, x, y, d, z in discs():
        comp = comps.get(material) or comps.setdefault(material, _add_hism(root, cylinder, material))
        # 기본 원기둥 = 지름 100 · 높이 100, 가운데가 원점
        comp.add_instance(unreal.Transform(
            location=unreal.Vector(x, y, z - THICK_CM * 0.5),
            rotation=unreal.Rotator(0.0, 0.0, 0.0),
            scale=unreal.Vector(d / 100.0, d / 100.0, THICK_CM / 100.0)), False)
        name = material.rsplit("/", 1)[1]
        counts[name] = counts.get(name, 0) + 1
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log_warning("[ZoneSeams] 원판 {}".format(" · ".join("{} {}".format(k, v) for k, v in sorted(counts.items()))))


main()
