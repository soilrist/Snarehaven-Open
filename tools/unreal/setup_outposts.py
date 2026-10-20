"""사냥터(L_Hunt) 거점 마을 — 하벤 만 지도 거점 14곳(`islands.OUTPOSTS`)마다 상인 좌판·오두막·모닥불·등불·벤치를 세운다.

레퍼런스 거점은 바닷가 공터에 NPC 상인 한 명과 작은 집·모닥불·등불이 선 곳이다(`docs/reference/world_map.md`).
시작 거점(헤이븐)에는 의뢰인도 선다. 거점마다 한 틀(아래 `LAYOUT`)을 바다 쪽을 보게 돌려 놓는다:
  상인이 바다(부두) 쪽을 보고 서고, 좌판은 그 뒤, 오두막은 더 안쪽, 모닥불·등불·벤치는 양옆.
틀의 모든 점이 뭍(해안 150cm 안쪽)이고 다른 액터(PlayerStart·포털·부두·광산·위험 구역)에서 떨어질 때까지
해안에서 안쪽으로 물린다. 자리를 잡으면 그 둘레 자연물 HISM 인스턴스(나무·바위·덤불)를 걷어 공터를 낸다.

순서: `setup_world_ground.py` → `setup_sea.py` → `setup_zones.py` → `setup_zone_portals.py` → `setup_mine.py` → 이 스크립트.
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \\
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_outposts.py" -unattended -nosplash \\
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \\
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

**좌표·크기는 레벨 배치값**(🟡, 게임 규칙 숫자가 아니다). 상인 종류는 DESIGN 8.14 「사냥터 거점 상인」 표와 같다.
재실행 안전: `Outpost_` 라벨 액터를 전부 지우고 다시 세운다(걷어낸 자연물은 `setup_zones.py` 를 다시 돌리면 돌아오고,
그때 이 액터들을 피한다).
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands  # noqa: E402

LEVEL = "/Game/Game/Maps/L_Hunt"
B = "/Game/Game/Environment/Buildings/"
P = "/Game/Game/Environment/Props/"
F = "/Game/Game/Environment/Furniture/"
LABEL_PREFIX = "Outpost_"
ROOF_TAG = "SnareHouseRoof"   # `setup_ranch_buildings.py` 와 같다 — 안에 들어가면 지붕이 숨는다

# 거점 → 상인 종류 (DESIGN 8.14 「사냥터 거점 상인」, 🟡). 부두·항구는 낚시, 눈 거점·비밀 가게는 옷, 꽃·섬은 농부.
KINDS = {
    "haven_outpost": "GENERAL", "maple_outpost": "GENERAL", "frost_camp": "GENERAL", "lakeside_outpost": "GENERAL",
    "gull_harbor": "FISHING", "reed_harbor": "FISHING", "otter_docks": "FISHING", "minnow_docks": "FISHING",
    "fern_harbor": "FISHING",
    "willow_outpost": "CLOTHES", "thimble_shop": "CLOTHES",
    "snowbell_post": "FARMING", "acorn_outpost": "FARMING", "moss_island": "FARMING",
}
QUEST_OUTPOST = islands.START_OUTPOST

# 틀 — (라벨 꼬리, 에셋, a = 바다 쪽 cm, b = 옆 cm, 바다 쪽 기준 yaw, 목표 최대변 cm 또는 None, 반폭 cm)
# 오두막 앞면(문)은 메시 -X — 바다 쪽을 보게 180 돌린다. 반폭은 겹침·뭍 검사용(메시 바닥 대략).
LAYOUT = [
    ("Cottage", B + "SM_Building_Cottage_01", -950.0, 250.0, 180.0, None, 330.0),
    ("Cottage_Roof", B + "SM_Building_CottageRoof_01", -950.0, 250.0, 180.0, None, 0.0),
    ("Campfire", F + "Campfire/SM_Furn_Campfire_01", 50.0, -520.0, 0.0, 130.0, 80.0),
    ("Bench", F + "Bench/SM_Furn_Bench_01", 50.0, -820.0, 90.0, 170.0, 100.0),
    ("Lantern_L", P + "SM_Prop_Lantern_01", 220.0, -260.0, 0.0, 220.0, 40.0),
    ("Lantern_R", P + "SM_Prop_Lantern_01", 220.0, 520.0, 0.0, 220.0, 40.0),
    ("Crates", P + "Harbor/SM_Prop_Crates_01", -350.0, -420.0, 20.0, 150.0, 90.0),
    ("Fence_0", B + "SM_Building_Fence_02", -1400.0, -150.0, 0.0, None, 120.0),
    ("Fence_1", B + "SM_Building_Fence_02", -1400.0, 650.0, 0.0, None, 120.0),
]
VENDOR_SPOT = (0.0, 0.0, 250.0)       # (a, b, 반폭) — 좌판이 상인 뒤 1.5m 에 선다
QUEST_SPOT = (0.0, 330.0, 80.0)       # 시작 거점 의뢰인 — 상인 옆
CLEAR_RADIUS_CM = 1700.0              # 이 반경 안 자연물을 걷는다(틀 가장 먼 점 ≈ 1450 + 여유)
KEEP_OFF_CM = 250.0                   # 다른 액터 둘레 비움
COAST_MARGIN_CM = 150.0               # 틀의 모든 점이 해안에서 이만큼 안쪽
# 시작 PlayerStart·자리표시 동물은 피하지 않고 시작 거점 틀에 맞춰 옮긴다(레퍼런스는 거점 상인 앞에서 깨어난다)
OBSTACLE_SKIP = ("ZoneProps_", "WorldGround_", "Sea", "AnimalSpread_", "HuntNature_", LABEL_PREFIX, "Gather_",
                 "MinStagePlayerStart", "HuntStage")
START_SPOT = (450.0, 0.0)             # 시작 자리 — 상인 앞(바다 쪽) 4.5m, 상인을 보고 선다
STAGE_ANIMALS = {"HuntStagePig": (300.0, -1250.0), "HuntStageCow": (0.0, -1300.0), "HuntStageSheep": (-300.0, -1250.0)}

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _obstacles():
    boxes = []
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith(OBSTACLE_SKIP) or actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            continue
        origin, extent = actor.get_actor_bounds(False)
        if min(extent.x, extent.y) > 2000.0 or max(extent.x, extent.y) < 1.0:
            continue
        boxes.append((label, origin.x, origin.y, extent.x, extent.y))
    return boxes


def _frame(cx, cy, nx, ny):
    """틀 좌표 (a, b) → 월드 (x, y)."""
    return lambda a, b: (cx + nx * a - ny * b, cy + ny * a + nx * b)


def _fits(to_world, obstacles, starter):
    spots = [(r[2], r[3], r[6]) for r in LAYOUT] + [VENDOR_SPOT]
    if starter:
        spots += [QUEST_SPOT, START_SPOT + (150.0,)] + [spot + (130.0,) for spot in STAGE_ANIMALS.values()]
    for a, b, half in spots:
        x, y = to_world(a, b)
        for sx, sy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            if not islands.on_land(x + sx * half, y + sy * half, COAST_MARGIN_CM):
                return "바다"
        for label, ox, oy, ex, ey in obstacles:
            if abs(x - ox) < ex + half + KEEP_OFF_CM and abs(y - oy) < ey + half + KEEP_OFF_CM:
                return label
    return None


def _site(outpost, obstacles):
    """(x, y, 바다 방향 nx, ny) — 거점 둘레에서 틀이 들어가는 가장 가까운 자리."""
    ox, oy = outpost["x"], outpost["y"]
    reason = None
    for margin in range(300, 4001, 200):
        for shift in (0.0, 600.0, -600.0, 1200.0, -1200.0):
            p = islands.land_point(ox, oy, margin=float(margin))
            if p is None:
                continue
            _, _, nx, ny = islands.nearest_coast(*p)
            cx, cy = p[0] - ny * shift, p[1] + nx * shift
            if not islands.on_land(cx, cy, float(margin) - 100.0):
                continue
            _, _, nx, ny = islands.nearest_coast(cx, cy)
            reason = _fits(_frame(cx, cy, nx, ny), obstacles, outpost["id"] == QUEST_OUTPOST)
            if reason is None:
                return cx, cy, nx, ny
    unreal.log_error("[Outpost] {} 자리 없음 (마지막 걸림: {})".format(outpost["id"], reason))
    return None


def _mesh_actor(label, path, x, y, yaw, target):
    mesh = unreal.load_asset(path)
    if mesh is None:
        unreal.log_error("[Outpost] 메시 없음: {}".format(path))
        return None
    size = mesh.get_bounding_box().max - mesh.get_bounding_box().min
    scale = 1.0 if target is None else target / max(size.x, size.y, size.z)
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, 0.0), unreal.Rotator(0.0, 0.0, yaw))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(mesh)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return actor


def _clear_nature(cx, cy):
    removed = 0
    for actor in actors.get_all_level_actors():
        if not actor.get_actor_label().startswith("ZoneProps_"):
            continue
        origin, extent = actor.get_actor_bounds(False)
        if abs(origin.x - cx) > extent.x + CLEAR_RADIUS_CM or abs(origin.y - cy) > extent.y + CLEAR_RADIUS_CM:
            continue
        for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            doomed = []
            for i in range(comp.get_instance_count()):
                t = comp.get_instance_transform(i, True).translation
                if math.hypot(t.x - cx, t.y - cy) < CLEAR_RADIUS_CM:
                    doomed.append(i)
            if doomed:
                comp.remove_instances(doomed)
                removed += len(doomed)
    return removed


def _vendor(cls, label, x, y, yaw, half_height, kind=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(x, y, half_height), unreal.Rotator(0.0, 0.0, yaw))
    actor.set_actor_label(label)
    if kind is not None:
        actor.set_editor_property("shop_kind", getattr(unreal.ShopKind, kind))
    return actor


unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
for old in actors.get_all_level_actors():
    if old.get_actor_label().startswith(LABEL_PREFIX):
        actors.destroy_actor(old)

half_height = unreal.get_default_object(unreal.SnarehavenCharacter).capsule_component.get_unscaled_capsule_half_height()
obstacles = _obstacles()
placed = 0
for outpost in islands.OUTPOSTS:
    site = _site(outpost, obstacles)
    if site is None:
        continue
    cx, cy, nx, ny = site
    to_world = _frame(cx, cy, nx, ny)
    sea_yaw = math.degrees(math.atan2(ny, nx))
    name = "".join(w.capitalize() for w in outpost["id"].split("_"))
    folder = "Outposts/" + name
    spawned = []
    for tail, path, a, b, yaw, target, _ in LAYOUT:
        x, y = to_world(a, b)
        actor = _mesh_actor("{}{}_{}".format(LABEL_PREFIX, name, tail), path, x, y, sea_yaw + yaw, target)
        if actor is not None:
            if tail.endswith("_Roof"):
                actor.tags = [ROOF_TAG]
            spawned.append(actor)
    vx, vy = to_world(*VENDOR_SPOT[:2])
    spawned.append(_vendor(unreal.ShopVendorActor, "{}{}_Vendor".format(LABEL_PREFIX, name), vx, vy, sea_yaw, half_height,
                           KINDS[outpost["id"]]))
    if outpost["id"] == QUEST_OUTPOST:
        qx, qy = to_world(*QUEST_SPOT[:2])
        spawned.append(_vendor(unreal.QuestGiverActor, "{}{}_QuestGiver".format(LABEL_PREFIX, name), qx, qy, sea_yaw, half_height))
        for actor in actors.get_all_level_actors():
            label = actor.get_actor_label()
            spot = START_SPOT if label == "MinStagePlayerStart" else STAGE_ANIMALS.get(label)
            if spot is not None:
                x, y = to_world(*spot)
                actor.set_actor_location_and_rotation(unreal.Vector(x, y, 100.0), unreal.Rotator(0.0, 0.0, sea_yaw + 180.0), False, False)
                unreal.log("[Outpost] {} → ({:.0f}, {:.0f})".format(label, x, y))
    for actor in spawned:
        actor.set_folder_path(folder)
    cleared = _clear_nature(cx, cy)
    placed += 1
    unreal.log("[Outpost] {} ({:.0f}, {:.0f}) 바다 yaw {:.0f} · {} · 액터 {}개 · 자연물 {}개 걷음".format(
        outpost["id"], cx, cy, sea_yaw, KINDS[outpost["id"]], len(spawned), cleared))

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("[Outpost] 저장: 거점 {}/{}곳".format(placed, len(islands.OUTPOSTS)))
