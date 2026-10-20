"""들판 광석 바위 검사 (DESIGN 8.16 「들판 광석 바위」) — 새 프로세스에서 `L_Hunt` 를 열어 단정한다.
(1) 종류 6개마다 행 `Gatherable_OreRock<종류>` 가 있고 지급 = 그 종류 덩어리 · 세 번 · 600초 · 곡괭이 요구가 설계서대로
(2) 맵에 종류마다 광석 바위 2개 이상, 각자 결정 덩이 4개가 붙어 있고 행 이름이 맞다
실행: setup_field_ore.py 와 같은 명령줄에서 -script 만 이 파일로. 통과하면 「들판 광석 바위 검사 통과」 경고 줄, 아니면 예외.
"""
import unreal

TABLE = "/Game/Game/Data/Gatherables/DT_GatherableTable"
EXPECT = {   # 종류 → (덩어리, 필요 곡괭이)
    "Coal": ("coal_chunk", "pickaxe_basic"), "Copper": ("copper_chunk", "pickaxe_basic"), "Iron": ("iron_chunk", "pickaxe_basic"),
    "Titanium": ("titanium_chunk", "pickaxe_advanced"), "Silver": ("silver_chunk", "pickaxe_advanced"), "Gold": ("gold_chunk", "pickaxe_advanced"),
}
bad = []
table = unreal.EditorAssetLibrary.load_asset(TABLE)
names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
col = lambda c: dict(zip(names, unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, c)))
grant, hits, resp, tool = col("GrantItemID"), col("HitsToFell"), col("RespawnSeconds"), col("RequiredToolItemID")
for kind, (chunk, pick) in EXPECT.items():
    row = "Gatherable_OreRock" + kind
    if row not in names:
        bad.append("행 없음 " + row)
        continue
    if grant[row] != "item." + chunk: bad.append("{} 지급 {}".format(row, grant[row]))
    if int(float(hits[row])) != 3: bad.append("{} 치는 수 {}".format(row, hits[row]))
    if int(float(resp[row])) != 600: bad.append("{} 다시 나기 {}".format(row, resp[row]))
    if tool[row] != "item." + pick: bad.append("{} 도구 {}".format(row, tool[row]))

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Game/Maps/L_Hunt")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
found = {}
attached = {}   # 바위 라벨 → 그 바위에 붙은 알갱이 라벨들
for a in actors:
    if "_Nugget" in a.get_actor_label():
        parent_actor = a.get_attach_parent_actor()
        if parent_actor is not None:
            attached.setdefault(parent_actor.get_actor_label(), []).append(a.get_actor_label())
for a in actors:
    label = a.get_actor_label()
    if label.startswith("FieldOre_") and "_Nugget" not in label:
        kind = label.split("_")[1]
        found[kind] = found.get(kind, 0) + 1
        if str(a.get_editor_property("gatherable_row_name")) != "Gatherable_OreRock" + kind:
            bad.append(label + " 행 이름 " + str(a.get_editor_property("gatherable_row_name")))
        if len(attached.get(label, [])) != 4: bad.append("{} 알갱이 {}".format(label, len(attached.get(label, []))))
# 지역별 분포 (DESIGN 8.16): `_F` 바위만 종류별 개수·지역 규칙을 단정 (옛 12개는 시작 거점 둘레라 예외)
import math, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import islands
FAR = {"Coal": (8, 2500.0, 25000.0, False), "Copper": (8, 2500.0, 25000.0, False), "Iron": (8, 2500.0, 25000.0, False),
       "Silver": (8, 25000.0, 45000.0, True), "Gold": (5, 25000.0, 45000.0, True), "Titanium": (5, 45000.0, 1.0e9, False)}
far_count = {}
for a in actors:
    m = re.match(r"^FieldOre_([A-Za-z]+)_F\d+$", a.get_actor_label())
    if not m:
        continue
    kind = m.group(1)
    far_count[kind] = far_count.get(kind, 0) + 1
    loc = a.get_actor_location()
    d = math.hypot(loc.x - islands.START[0], loc.y - islands.START[1])
    snow = islands.zone_at(loc.x, loc.y) == "snow"
    _, dmin, dmax, snow_ok = FAR[kind]
    if not ((snow_ok and snow) or (not snow and dmin <= d < dmax)):
        bad.append("{} 자리 어긋남(거리 {:.0f}cm · 설원 {})".format(a.get_actor_label(), d, snow))
for kind, (n, _, _, _) in FAR.items():
    if far_count.get(kind, 0) != n: bad.append("지역 분포 {} {} 개 ({} 이어야)".format(kind, far_count.get(kind, 0), n))
unreal.log_warning("지역별 광석 바위: {}".format(far_count))
for kind in EXPECT:
    if found.get(kind, 0) < 2: bad.append("맵 {} 바위 {} (2 이상)".format(kind, found.get(kind, 0)))
unreal.log_warning("들판 광석 바위 검사: 맵 {} · 어긋남 {} (0 이어야)".format(found, len(bad)))
if bad:
    raise RuntimeError("들판 광석 바위 검사 실패: " + "; ".join(bad))
unreal.log_warning("들판 광석 바위 검사 통과")
