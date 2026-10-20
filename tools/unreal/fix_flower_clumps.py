"""꽃 더미 보정 — 땅에 붙이고(Z), 바위·나무·건물과 겹친 더미는 버리고 새 자리를 골라 채우고, 더미마다 꽃 색을 섞는다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/fix_flower_clumps.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

왜: 꽃 채집 3층 불합격(INBOX 10-13 16:15) — Z=0 으로 묻힘·바위 위 놓임·노란 점 하나. `convert_flowers_to_gatherables.py` 는 장식 포기가 이미 없어 다시 못 돈다 → 이미 선 꽃 더미를 고친다.
자리 판정은 `flower_ground.py`(검사와 같은 코드). 고정 시드라 다시 돌려도 같은 자리.
"""
import random
import sys

sys.path.insert(0, "~/UNREAL/Snarehaven/tools/unreal")
import unreal  # noqa: E402
import flower_ground as fg  # noqa: E402

MAPS = [("/Game/Game/Maps/L_Ranch", 13), ("/Game/Game/Maps/L_Hunt", 300)]
ROW_NAME = "Gatherable_FlowerPatch"
SEED = 19005
COLOR_SLOT = 1       # SM_Nature_WildFlower_02: 0 잎 · 1 꽃잎 · 2 가운데
COLORS = [("Yellow", 4), ("White", 3), ("Red", 2), ("Purple", 2)]   # (재질 끝 이름, 가중치) — 노랑·흰·빨강·보라
MIN_GAP_CM = 160.0   # 꽃 더미 사이 최소 거리 (몸 폭 ≈ 130cm)
JITTER_CM = (500.0, 2500.0)
Z_SNAP_EPS = 0.5


def _piles():
    return [a for a in fg._actors() if isinstance(a, unreal.GatherableActor)
            and str(a.get_editor_property("gatherable_row_name")) == ROW_NAME]


def _mats():
    base = "/Game/Game/Environment/Buildings/MI_Building_Flower"
    return [(unreal.load_asset(base + name), weight) for name, weight in COLORS]


def _pick_color(rng, mats):
    return rng.choices([m for m, _ in mats], weights=[w for _, w in mats])[0]


def _fix(path, target):
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path)
    rng = random.Random("{}:{}".format(SEED, path))
    mats = _mats()
    grounds, obstacles = fg.collect()
    road = fg.path_discs()
    piles = _piles()
    keep, dropped = [], []
    for actor in piles:
        where = actor.get_actor_location()
        z = fg.ground_z(grounds, where.x, where.y)
        bad = z is None or z < 0.0 or fg.overlaps(obstacles, where.x, where.y) or fg.on_path(road, where.x, where.y)
        (dropped if bad else keep).append(actor)
    for actor in dropped:
        actor.destroy_actor()
    spots = [(a.get_actor_location().x, a.get_actor_location().y) for a in keep]
    seeds = list(spots)
    added = 0
    tries = 0
    while len(spots) < target and seeds and tries < 200000:
        tries += 1
        cx, cy = rng.choice(seeds)
        angle = rng.uniform(0.0, 6.283185)
        dist = rng.uniform(*JITTER_CM)
        x, y = cx + dist * __import__("math").cos(angle), cy + dist * __import__("math").sin(angle)
        z = fg.ground_z(grounds, x, y)
        if z is None or z < 0.0 or fg.overlaps(obstacles, x, y) or fg.on_path(road, x, y):
            continue
        if any(((sx - x) ** 2 + (sy - y) ** 2) ** 0.5 < MIN_GAP_CM for sx, sy in spots):
            continue
        spots.append((x, y))
        actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
            unreal.GatherableActor, unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, rng.uniform(0.0, 360.0)))
        actor.set_actor_label("{}{}".format(fg.FLOWER_LABEL, 1000 + added))
        actor.set_editor_property("gatherable_row_name", ROW_NAME)
        keep.append(actor)
        added += 1
    for actor in keep:
        where = actor.get_actor_location()
        z = fg.ground_z(grounds, where.x, where.y)
        if abs(where.z - z) > Z_SNAP_EPS:
            actor.set_actor_location(unreal.Vector(where.x, where.y, z), False, False)
        body = actor.get_editor_property("body")
        body.set_material(COLOR_SLOT, _pick_color(rng, mats))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log_warning("[FlowerFix] {} 옛 더미 {} → 겹침·물속으로 버림 {} · 새로 채움 {} · 지금 {}".format(
        path, len(piles), len(dropped), added, len(_piles())))


for map_path, count in MAPS:
    _fix(map_path, count)
