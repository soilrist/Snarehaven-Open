"""M104-08b3 잠긴 금고 폐허 배치 — `L_Hunt` 에 `ALootChestActor`(폐허 번호 1~3)를 놓는다(`.umap` 은 텍스트로 못 고친다).

실행은 `setup_danger_zones.py` 머리말과 같다(UnrealEditor-Cmd -run=pythonscript -script=<절대경로>).
자리는 DESIGN 8.1c 표 그대로: 설원·숲·늪 뭍에 한 곳씩, 시작 거점에서 직선 30000cm 이상, 해안 25m 안쪽.
번호는 서버 `OpenVault{ruin}` 와 짝이다 — 1 설원 · 2 숲 · 3 늪. 이미 있으면 새로 안 놓고 자리만 맞춘다(재실행 안전).
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

LEVEL_PATH = "/Game/Game/Maps/L_Hunt"
MIN_FROM_START_CM = 30000.0   # DESIGN 8.1c 「폐허 수·자리」
MARGIN_CM = 2500.0
# (번호, 존, 찾기 시작점) — 시작점은 존 안 뭍 한 점, 실제 자리는 land_point 가 존 안 가장 가까운 뭍으로 고른다
RUINS = [
    (1, "snow", (18800.0, -42400.0)),
    (2, "forest", (-29200.0, 37600.0)),
    (3, "swamp", (-29200.0, -36400.0)),
]


def _actors():
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert sub
    return sub


def _find(label):
    for actor in _actors().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


CLEAR_RADIUS_CM = 700.0  # 폐허 둘레 빈터 — 나무·덤불이 게임 카메라를 가리지 않게 (INBOX 보정 #1117 ②)


def _clear_around(x, y):
    removed = 0
    for actor in _actors().get_all_level_actors():
        if not actor.get_actor_label().startswith("ZoneProps_"):
            continue
        for comp in actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
            for i in reversed(range(comp.get_instance_count())):
                spot = comp.get_instance_transform(i, True).translation
                if math.hypot(spot.x - x, spot.y - y) < CLEAR_RADIUS_CM:
                    comp.remove_instance(i)
                    removed += 1
    return removed


levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
levels.load_level(LEVEL_PATH)
sx, sy = islands.START

for index, zone, seed in RUINS:
    point = islands.land_point(seed[0], seed[1], zone=zone, margin=MARGIN_CM)
    assert point is not None, zone
    dist = math.hypot(point[0] - sx, point[1] - sy)
    assert dist >= MIN_FROM_START_CM, (zone, dist)
    assert islands.zone_at(*point) == zone, zone
    label = "VaultRuin_{}".format(index)
    location = unreal.Vector(point[0], point[1], 100.0)
    actor = _find(label)
    if actor is None:
        actor = _actors().spawn_actor_from_class(unreal.LootChestActor, location)
        assert actor, label
        actor.set_actor_label(label)
    actor.set_actor_location(location, False, False)
    actor.set_editor_property("vault_ruin_index", index)
    unreal.log("vault: {} 빈터 — 치운 나무·덤불 {}개".format(label, _clear_around(point[0], point[1])))
    unreal.log("vault: {} zone={} at ({:.0f}, {:.0f}) from_start={:.0f}cm".format(label, zone, point[0], point[1], dist))

levels.save_current_level()
