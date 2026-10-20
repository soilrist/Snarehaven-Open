"""꽃 더미 자리 검사 공용 — 땅 높이·바위/나무/건물 겹침. `fix_flower_clumps.py`(고치기)와 `verify_flower_gather.py`(검사)가 같이 쓴다.

왜: 꽃 채집 3층 불합격(INBOX 10-13 16:15) — 더미가 Z=0 으로 땅(≈90) 속에 묻히고, 바위 위·속에 놓였다.
"""
import unreal

SKIP_ISM = ("Grass", "Pebble", "Flower", "Leaf", "Cylinder", "Puddle")
FLOWER_LABEL = "Gather_Flower_"
CLEAR_CM = 70.0           # 꽃 더미 반경 (무리 묶음 반경과 같다)
PATH_LABEL = "DirtPath"      # setup_dirt_path.py 가 붙이는 이름
GROUND_PREFIXES = ("WorldGround_", "ZoneFloor_", "RanchGround", "Ranch_Ground")
HUGE_CM = 3000.0          # 이보다 큰 액터는 장애물로 안 센다(바다·하늘 둘레)
BIG_PLANE_CM = 800.0      # 이보다 두 변이 다 크면 판(땅·바다) — 겹침 장애물이 아니라 땅


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def collect(extra_ground_prefixes=()):
    """(땅 상자 목록, 장애물 원 목록 (x, y, 반경)) — 장애물은 꽃 더미가 아닌 것만."""
    grounds, obstacles = [], []
    for actor in _actors():
        label = actor.get_actor_label()
        if label.startswith(FLOWER_LABEL):
            continue
        for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            mesh = comp.static_mesh
            if mesh is None or any(word in mesh.get_name() for word in SKIP_ISM):
                continue
            box = mesh.get_bounding_box()
            half = max(box.max.x - box.min.x, box.max.y - box.min.y) * 0.5
            for index in range(comp.get_instance_count()):
                transform = comp.get_instance_transform(index, True)
                if isinstance(transform, tuple):
                    transform = transform[-1]
                scale = max(float(transform.scale3d.x), float(transform.scale3d.y))
                radius = half * scale
                if 15.0 <= radius <= 600.0:     # 바다 물결 판 같은 큰 것은 장애물이 아니다
                    obstacles.append((float(transform.translation.x), float(transform.translation.y), radius))
        if actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            continue
        if isinstance(actor, (unreal.GatherableActor, unreal.PointLight)):
            if isinstance(actor, unreal.GatherableActor):
                where = actor.get_actor_location()
                obstacles.append((float(where.x), float(where.y), 60.0))
            continue
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        origin, extent = actor.get_actor_bounds(False)
        if min(extent.x, extent.y) > BIG_PLANE_CM:
            grounds.append((origin.x - extent.x, origin.x + extent.x, origin.y - extent.y, origin.y + extent.y, origin.z + extent.z))
        elif max(extent.x, extent.y) > HUGE_CM or label.startswith("StartDecor_Grass"):
            continue        # 바다·하늘·벽 같은 맵 전체 판, 맨 풀 장식
        else:
            obstacles.append((float(origin.x), float(origin.y), max(extent.x, extent.y)))
    return grounds, obstacles


def path_discs():
    """흙길 가운데 층 원판 목록 (x, y, 반경) — `setup_dirt_path.py` 의 `DirtPath` 액터 마지막 HISM(= 흙 MI 그대로인 층)."""
    discs = []
    for actor in _actors():
        if actor.get_actor_label() != PATH_LABEL:
            continue
        comps = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
        if not comps:
            continue
        comp = comps[-1]
        for index in range(comp.get_instance_count()):
            transform = comp.get_instance_transform(index, True)
            if isinstance(transform, tuple):
                transform = transform[-1]
            discs.append((float(transform.translation.x), float(transform.translation.y), 50.0 * float(transform.scale3d.x)))
    return discs


def on_path(discs, x, y):
    """꽃 더미 중심이 흙길 가운데 층 위(반경 + 더미 반경 절반 안)면 참."""
    return any(((dx - x) ** 2 + (dy - y) ** 2) ** 0.5 < r + CLEAR_CM * 0.5 for dx, dy, r in discs)


def ground_z(grounds, x, y):
    tops = [g[4] for g in grounds if g[0] <= x <= g[1] and g[2] <= y <= g[3]]
    return max(tops) if tops else None


def overlaps(obstacles, x, y):
    return any(((ox - x) ** 2 + (oy - y) ** 2) ** 0.5 < r + CLEAR_CM * 0.5 for ox, oy, r in obstacles)
