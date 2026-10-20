"""레퍼런스 항구 — `L_Ranch` 남쪽(-Y) 해안에 콘크리트 부두·잔교 둘·배 네 척·부두 소품을 세운다 (2026-09-25 사용자 요청).

`tools/blender/make_harbor_ships.py`(OBJ) → `import_tree_meshes.py`(반입) → `setup_sea.py`·`setup_beach.py` 다음에 돌린다:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_harbor.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

레퍼런스 선착장(steam_10·steam_17) = 해안에 붙은 긴 콘크리트 부두 + 바다로 뻗은 잔교 + 잔교 곁에 댄 배. 메시 규칙(make_harbor_ships
머리말): 부두·잔교는 +X 가 바다·윗면 z 15, 배는 뱃머리 +X·수선 z 0. 남쪽 해안이라 요 -90(+X → -Y).
부두 x 200~2600 — 사냥터 포탈 부두(x ±133)와 연못 2(y -2050 까지)를 비킨다. 잔교 끝은 바다 벽(y -5500) 안.
배는 수선을 수면(바다 액터 z)에 맞춘다 — 큰 배 흘수 110 < 깊은 바닥 130. 부두·잔교·배는 복잡 충돌(밟고 막힘).
좌표는 전부 배치값(🟡). 재실행 안전: `Harbor_*` 라벨 액터를 지우고 다시 세운다.
"""
import unreal

LEVEL = "/Game/Game/Maps/L_Ranch"
ENV = "/Game/Game/Environment/"
MI_DIR = ENV + "Buildings/Harbor"
MATTE_PARENT = ENV + "Nature/M_Nature_Matte"
LABEL = "Harbor_"
DECK_Z = 15.0      # 부두·잔교 윗면(메시 z 15, 원점 = 섬 바닥 z 0)
# 슬롯 색 = make_harbor_ships.py 팔레트(OBJ .mtl Kd, 리니어) 그대로
SLOT_COLORS = {
    "Hull": (0.768, 0.651, 0.371), "Dark": (0.034, 0.032, 0.042), "Green": (0.028, 0.205, 0.084),
    "Cream": (0.863, 0.784, 0.610), "Glow": (1.0, 0.644, 0.195), "White": (0.905, 0.888, 0.839),
    "Blue": (0.047, 0.159, 0.631), "Tan": (0.546, 0.266, 0.102), "Glass": (0.028, 0.063, 0.125),
    "Navy": (0.016, 0.033, 0.114), "Orange": (0.871, 0.258, 0.028), "Concrete": (0.597, 0.565, 0.515),
    "Solar": (0.021, 0.033, 0.195), "Yellow": (0.807, 0.539, 0.050), "Wood": (0.584, 0.301, 0.117),
    "Cardboard": (0.687, 0.468, 0.195), "Tape": (0.847, 0.768, 0.578), "Metal": (0.205, 0.227, 0.254),
    "Mesh": (0.503, 0.533, 0.565),
}
V, B, P = ENV + "Vehicles/Harbor/SM_Vehicle_", ENV + "Buildings/Harbor/SM_Building_", ENV + "Props/Harbor/SM_Prop_"
# (라벨 꼬리, 메시, x, y, 높이 기준 "floor"|"deck"|"sea", 요, 충돌)
PLACEMENTS = [
    ("Quay", B + "Quay_01", 1400.0, -2600.0, "floor", -90.0, True),
    ("Pier_West", B + "Pier_01", 600.0, -2850.0, "floor", -90.0, True),
    ("Pier_East", B + "Pier_02", 1900.0, -2850.0, "floor", -90.0, True),
    ("Ship", V + "Ship_01", 2613.0, -4180.0, "sea", -90.0, True),
    ("Tugboat", V + "Tugboat_01", 1482.0, -3500.0, "sea", -90.0, True),
    ("Yacht", V + "Yacht_01", 995.0, -3550.0, "sea", -90.0, True),
    ("Motorboat", V + "Motorboat_01", 292.0, -3600.0, "sea", -90.0, True),
    ("Crates", P + "Crates_01", 950.0, -2430.0, "deck", 0.0, True),
    ("Pallet", P + "Pallet_01", 1150.0, -2420.0, "deck", 0.0, True),
    ("Drum_Yellow", P + "Drum_01", 1300.0, -2430.0, "deck", 0.0, True),
    ("Drum_Blue", P + "Drum_02", 1365.0, -2430.0, "deck", 0.0, True),
] + [("Fence_{}".format(i), P + "ChainFence_01", 1650.0 + 312.0 * i, -2385.0, "deck", 0.0, True) for i in range(3)] + [
    # M19-05 부두 양 끝 소품 무리 둘(레퍼런스 steam_10·11·12·14 — 부두 위 상자·드럼·팔레트 무더기). 육지 쪽 가장자리에 붙여
    # 가운데 걷는 길(y -2600 근처)과 잔교 입구(배 타는 칸, 아래 BOARDING_CLEAR_CM)는 비운다.
    ("West_Crates", P + "Crates_01", 300.0, -2440.0, "deck", 15.0, True),
    ("West_Drum_Blue", P + "Drum_02", 430.0, -2415.0, "deck", 0.0, True),
    ("West_Drum_Yellow", P + "Drum_01", 455.0, -2480.0, "deck", 0.0, True),
    ("West_Pallet", P + "Pallet_01", 290.0, -2545.0, "deck", 8.0, True),
    ("East_Pallet", P + "Pallet_01", 2510.0, -2460.0, "deck", -6.0, True),
    ("East_Crates", P + "Crates_01", 2490.0, -2460.0, "pallet", -6.0, True),
    ("East_Drum_Yellow", P + "Drum_01", 2390.0, -2500.0, "deck", 0.0, True),
    ("East_Drum_Blue", P + "Drum_02", 2450.0, -2545.0, "deck", 0.0, True),
]
BOARDING_CLEAR_CM = 200.0   # 잔교 입구·포탈 둘레 이 안엔 소품 금지(ROADMAP M19-05 합격선, 시험 SnarehavenHarborPropsTest.cpp 와 같은 값)
# 부두 자리 자연물 비우기 — `setup_zones.py` RANCH_KEEP_OUT 의 항구 줄과 같은 사각형 (x0, x1, y0, y1)
CLEAR = (150.0, 2650.0, -5500.0, -2250.0)

assets = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)


def _materials():
    parent = assets.load_asset(MATTE_PARENT)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    out = {}
    for slot, rgb in SLOT_COLORS.items():
        name = "MI_Harbor_" + slot
        path = MI_DIR + "/" + name
        mi = assets.load_asset(path) if assets.does_asset_exist(path) else \
            tools.create_asset(name, MI_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "DiffuseColor", unreal.LinearColor(*rgb, 1.0))
        assets.save_asset(path, only_if_is_dirty=False)
        out[slot] = mi
    return out


def _prepare_mesh(path, materials, collide):
    mesh = assets.load_asset(path)
    if mesh is None:
        unreal.log_error("메시 없음: {} — make_harbor_ships.py → import_tree_meshes.py 먼저".format(path))
        return None
    for index, slot in enumerate(mesh.static_materials):
        mi = materials.get(str(slot.material_slot_name))
        if mi:
            mesh.set_material(index, mi)
        else:
            unreal.log_error("색 없는 슬롯: {} {}".format(path, slot.material_slot_name))
    if collide:
        body = mesh.get_editor_property("body_setup")
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    assets.save_asset(path.split(".")[0], only_if_is_dirty=False)
    return mesh


sea = next((a for a in actors.get_all_level_actors() if a.get_actor_label() == "Sea"), None)   # setup_sea.py 라벨
if sea is None:
    raise RuntimeError("L_Ranch 에 바다가 없다 — setup_sea.py 먼저")
PALLET_TOP = 28.5   # 팔레트 두 장 겹친 윗면(make_harbor_ships.py pallet: 14.5 + 14) — 상자 더미를 그 위에 얹는다
height = {"floor": 0.0, "deck": DECK_Z, "pallet": DECK_Z + PALLET_TOP, "sea": sea.get_actor_location().z}

for actor in actors.get_all_level_actors():
    if actor.get_actor_label().startswith(LABEL):
        actors.destroy_actor(actor)

# 부두 자리에 선 나무·바위 인스턴스를 뺀다(충돌 기둥 포함) — 뒤에서부터 지워야 번호가 안 밀린다
removed = 0
for actor in actors.get_all_level_actors():
    if not actor.get_actor_label().startswith("ZoneProps_"):
        continue
    for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
        for i in reversed(range(comp.get_instance_count())):
            t = comp.get_instance_transform(i, True).translation
            if CLEAR[0] < t.x < CLEAR[1] and CLEAR[2] < t.y < CLEAR[3]:
                comp.remove_instance(i)
                removed += 1

materials = _materials()
for tail, path, x, y, base, yaw, collide in PLACEMENTS:
    mesh = _prepare_mesh(path, materials, collide)
    if mesh is None:
        continue
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, height[base]), unreal.Rotator(0.0, 0.0, yaw))
    actor.set_actor_label(LABEL + tail)
    if base == "sea":
        actor.tags = ["SnareFloat"]   # ASeaActor::FloatTag — 수면에서 천천히 흔들린다(DESIGN 8.9 「항구」)
    comp = actor.static_mesh_component
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)   # Static 이면 "LIGHTING NEEDS TO BE REBUILT" 가 캡처에 찍힌다(루프#26)
    comp.set_static_mesh(mesh)
    comp.set_collision_profile_name("BlockAll" if collide else "NoCollision")
    o, e = actor.get_actor_bounds(False)
    unreal.log_warning("[Harbor] {} ({:.0f},{:.0f},{:.0f}) 요 {:.0f} 상자 x {:.0f}..{:.0f} y {:.0f}..{:.0f} z {:.0f}..{:.0f} 삼각형 {}".format(
        LABEL + tail, x, y, height[base], yaw, o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z,
        mesh.get_num_triangles(0)))

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()

# 밟히나 — 부두·잔교 윗면에 위에서 쏜 선이 z 15 에 닿아야 한다
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for name, x, y in (("부두", 1100.0, -2700.0), ("서쪽 잔교 끝", 600.0, -4000.0), ("동쪽 잔교 끝", 1900.0, -5300.0)):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 500.0), unreal.Vector(x, y, -500.0),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    z = hit.to_tuple()[4].z if hit and hit.to_tuple()[0] else None
    unreal.log_warning("[Harbor] 밟기 {} ({:.0f},{:.0f}) 맞은 z {}".format(name, x, y, z))
unreal.log_warning("[Harbor] 세움 {} · 비운 자연물 인스턴스 {}".format(len(PLACEMENTS), removed))
