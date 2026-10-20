"""M19-03 꽃 융단 솎기 — `L_Hunt`·`L_Ranch` 의 들꽃을 3~6송이 작은 무리로 띄엄띄엄 남긴다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/thin_flower_clumps.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

왜: `setup_zones.py` 의 꽃밭(MEADOWS, 지름 5m 에 포기 수십)과 σ130 꽃 군락이 땅을 덮는 융단이 됐다
(`captures/274_ranch_ground.png`). 레퍼런스 꽃은 3~7송이 작은 무리가 풀 사이에 띄엄띄엄이다(`steam_00`·`steam_18`·`steam_22`).
**꽃을 까는 스크립트(`setup_zones.py`·`setup_start_decor.py`)를 다시 돌리면 이것도 뒤이어 다시 돌린다.**

새로 심지 않고 **지우기만** 한다 — 남는 꽃은 이미 바닥·장애물 검사를 거친 자리라 다시 볼 것이 없다.
고정 시드로 섞은 순서대로 훑으며: 남긴 무리 중심 반경 CLUMP_RADIUS_CM 안이고 그 무리가 덜 찼으면 합류,
어느 무리 중심에서도 CLUMP_GAP_CM 넘게 떨어졌으면 새 무리, 그 밖은 지운다(포아송 원판 솎기).
캐는 꽃 더미(`GatherableActor`, 몸 메시가 꽃)는 지우지 않고 먼저 무리 하나로 세워 둔다 — 장식 꽃이 그 옆에 붙어 큰 덩이가 되지 않게.
숫자는 DESIGN 8.9 「들꽃 무리」 줄(배치값 🟡).
"""
import random

import unreal

MAPS = ["/Game/Game/Maps/L_Ranch", "/Game/Game/Maps/L_Hunt"]
FLOWER_MESH_PREFIXES = ("SM_Nature_WildFlower", "SM_Nature_Flower")
THIN_SEED = 19003
PLANTS_PER_CLUMP = 2        # 포기 하나 = 꽃 세 송이(`make_house_meshes.py` wild_flower) → 무리 3~6송이 (원본 3~7)
CLUMP_RADIUS_CM = 70.0      # 무리 중심에서 포기까지 — 포기 폭(≈110cm)의 절반 남짓이라 두 포기가 붙어 한 덩이로 보인다
CLUMP_GAP_CM = 380.0        # 무리 중심끼리 — 무리 사이 포기 끝과 끝이 ≈130cm 떠 풀이 보인다


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def _is_flower(mesh) -> bool:
    return mesh is not None and mesh.get_name().startswith(FLOWER_MESH_PREFIXES)


def _gather_flower_spots():
    """몸 메시가 꽃인 채집 더미 자리 — 행의 `MeshRef` 로 판단한다(라벨·행 이름 아님)."""
    spots = []
    for actor in _actors():
        if not isinstance(actor, unreal.GatherableActor):
            continue
        table = actor.get_editor_property("gatherable_table")
        row_name = str(actor.get_editor_property("gatherable_row_name"))
        if table is None or not row_name:
            continue
        names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
        meshes = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "MeshRef")
        mesh_path = dict(zip([str(n) for n in names], meshes)).get(row_name, "")
        leaf = mesh_path.rsplit(".", 1)[-1].strip("'\"")
        if leaf.startswith(FLOWER_MESH_PREFIXES):
            where = actor.get_actor_location()
            spots.append((float(where.x), float(where.y)))
    return spots


def _candidates():
    """장식 꽃 포기 전부 — (x, y, 지우는 법 키). 키 = ("ism", 컴포넌트, 인덱스) 또는 ("actor", 액터)."""
    found = []
    for actor in _actors():
        for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if not _is_flower(comp.static_mesh):
                continue
            for index in range(comp.get_instance_count()):
                transform = comp.get_instance_transform(index, True)
                if isinstance(transform, tuple):
                    transform = transform[-1]
                found.append((float(transform.translation.x), float(transform.translation.y), ("ism", comp, index)))
        if isinstance(actor, unreal.StaticMeshActor) and _is_flower(actor.static_mesh_component.static_mesh):
            where = actor.get_actor_location()
            found.append((float(where.x), float(where.y), ("actor", actor, 0)))
    return found


class Clumps:
    """무리 중심 — CLUMP_GAP_CM 칸 격자로 이웃만 본다(사냥터 꽃 1만 4천 포기)."""

    def __init__(self):
        self.grid = {}

    def _cell(self, x, y):
        return int(x // CLUMP_GAP_CM), int(y // CLUMP_GAP_CM)

    def nearest(self, x, y):
        cx, cy = self._cell(x, y)
        best, best_d = None, None
        for i in (cx - 1, cx, cx + 1):
            for j in (cy - 1, cy, cy + 1):
                for clump in self.grid.get((i, j), ()):
                    d = ((clump[0] - x) ** 2 + (clump[1] - y) ** 2) ** 0.5
                    if best_d is None or d < best_d:
                        best, best_d = clump, d
        return best, best_d

    def add(self, x, y, plants):
        clump = [x, y, plants]
        self.grid.setdefault(self._cell(x, y), []).append(clump)
        return clump


def _thin(level_path: str) -> None:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level_path)
    clumps = Clumps()
    gathers = _gather_flower_spots()
    for x, y in gathers:
        clumps.add(x, y, 1)
    items = sorted(_candidates(), key=lambda item: (round(item[0], 1), round(item[1], 1), item[2][2]))
    random.Random(THIN_SEED).shuffle(items)
    kept, removed, made = 0, [], 0
    for x, y, key in items:
        clump, distance = clumps.nearest(x, y)
        if clump is not None and distance <= CLUMP_RADIUS_CM and clump[2] < PLANTS_PER_CLUMP:
            clump[2] += 1
            kept += 1
        elif clump is None or distance > CLUMP_GAP_CM:
            clumps.add(x, y, 1)
            made += 1
            kept += 1
        else:
            removed.append(key)

    by_comp = {}
    for kind, owner, index in removed:
        if kind == "ism":
            by_comp.setdefault(owner, []).append(index)
        else:
            owner.destroy_actor()
    for comp, indices in by_comp.items():
        comp.remove_instances(sorted(indices, reverse=True))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log_warning("[FlowerClumps] {} 포기 {} → 남김 {} · 지움 {} · 새 무리 {} · 꽃 더미 무리 {}".format(
        level_path, len(items), kept, len(removed), made, len(gathers)))


for path in MAPS:
    _thin(path)
