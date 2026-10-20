"""가림 페이드 적용 범위 검사 — 큰 메시가 쓰는 머티리얼 중 가림 페이드를 못 받는 것이 0 인지 (INBOX 10-13 가림 페이드 넓히기).

실행: create_view_fade_material.py 와 같은 명령줄에서 -script 만 이 파일로. 통과하면 「가림 페이드 범위 검사 통과」 경고 줄, 아니면 예외.
기준: Nature·Buildings 의 정적 메시 중 높이 130cm 이상인 것의 모든 머티리얼 슬롯은 부모가 `M_Nature_MatteFade` 여야 한다.
예외(이름으로 적음): 바닥·항구(`MI_Building_CottageFloor*`·`MI_Harbor_*`)·무늬 바위(`MI_Nature_Rocks`)·나무 열매 덤불 등 Matte 가 아닌 부모.
"""
import unreal

FOLDERS = ["/Game/Game/Environment/Nature", "/Game/Game/Environment/Buildings"]
MIN_HEIGHT_CM = 130.0
FADE = "M_Nature_MatteFade"
EXEMPT_PREFIX = ("MI_Building_CottageFloor", "MI_Harbor_", "MI_Nature_Rocks", "MI_Ground_")
# 사냥터 옛 나무(Pine_01~04·Tree_01~03, Phong·Lambert 무늬 없는 색)는 이번 범위 밖 — 목록에 안 넣는다
BIG_ASSET_PREFIXES = ("SM_Nature_Pine_05", "SM_Nature_Pine_06", "SM_Nature_Tree_04", "SM_Nature_Birch", "SM_Nature_Boulder",
                      "SM_Nature_MineFrame", "SM_Building_")


def parent_name(mat):
    if isinstance(mat, unreal.MaterialInstance):
        p = mat.get_editor_property("parent")
        return p.get_name() if p else None
    return mat.get_name()


missing, checked = [], 0
for folder in FOLDERS:
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True):
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh) or mesh.get_bounds().box_extent.z * 2.0 < MIN_HEIGHT_CM:
            continue
        if not mesh.get_name().startswith(BIG_ASSET_PREFIXES):
            continue
        for slot in mesh.static_materials:
            mat = slot.material_interface
            if mat is None or mat.get_name().startswith(EXEMPT_PREFIX):
                continue
            checked += 1
            if parent_name(mat) != FADE:
                missing.append("{} 슬롯 {} 부모 {}".format(mesh.get_name(), mat.get_name(), parent_name(mat)))
unreal.log_warning("가림 페이드 범위 검사: 슬롯 {} 개 확인 · 빠진 것 {} (0 이어야)".format(checked, len(missing)))
for m in missing:
    unreal.log_warning("가림 페이드 빠짐: " + m)
if missing:
    raise RuntimeError("가림 페이드 빠진 큰 메시 {} 개".format(len(missing)))
unreal.log_warning("가림 페이드 범위 검사 통과")
