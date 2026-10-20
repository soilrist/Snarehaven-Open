"""INBOX 결함 10-20 00:10 · DESIGN 3.5 「모자 밑 머리카락」 — 모자를 쓰면 머리카락을 **모자 아랫단 아래만** 그리는 머티리얼.

  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/make_hair_under_hat_material.py" -unattended -nosplash -log

모자를 쓰면 머리카락을 통째로 숨겨서 작은 모자(페즈·베레·부엉이…) 아래 뒤통수가 살색 민머리였다(`lh1350_Head5_b` 등).
머리카락 껍질은 모자 안쪽 틈보다 두꺼워 그대로 두면 모자 윗면을 뚫는다(#1336). 그래서 같은 머리카락 메시를 쓰되
**메시 공간 높이 `HairTopZ` 보다 위는 그리지 않는다**(마스크) — 높이는 코드(`UEquipmentComponent::UpdateHair`)가
모자 뒤쪽 아랫단에서 재 넣는다. 머리카락 메시 원점 = 정수리(`make_player_hair_mesh.py`)라 높이 = 정수리 기준 메시 cm.

모양·색은 기존 `MI_Player_Hair`(부모 FBX Legacy Lambert — 거칠기·반사 비연결 = 기본값)와 같게: `DiffuseColor` 를 바탕색에,
나머지는 기본값. 상인처럼 색을 바꾼 머리카락도 같은 칸 이름(`DiffuseColor`)을 쓴다. 재실행 안전(지우고 다시 만든다).
"""
import unreal

DEST_DIR = "/Game/Game/Characters/Player/Hair"
NAME = "M_Player_HairUnderHat"
PATH = DEST_DIR + "/" + NAME
# 팔레트 — `import_player_hair.py` 의 HAIR_COLOR 와 같은 짙은 밤색(인스턴스가 덮어쓴다)
HAIR_COLOR = unreal.LinearColor(0.105, 0.042, 0.016, 1.0)
NO_CLIP_Z = 1000.0   # 기본 = 자르지 않음(정수리보다 한참 위) — 칸을 안 넣은 인스턴스는 머리카락 전부

assets = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

if assets.does_asset_exist(PATH):
    assets.delete_asset(PATH)
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DEST_DIR, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)


def node(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


color = node(unreal.MaterialExpressionVectorParameter, -400, -200)
color.set_editor_property("parameter_name", "DiffuseColor")
color.set_editor_property("default_value", HAIR_COLOR)
mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

# 마스크: 메시 공간 Z 가 HairTopZ 보다 낮으면 1(그림), 높으면 0(안 그림)
local = node(unreal.MaterialExpressionLocalPosition, -800, 200)
z_only = node(unreal.MaterialExpressionComponentMask, -600, 200)
z_only.set_editor_property("r", False)
z_only.set_editor_property("g", False)
z_only.set_editor_property("b", True)
z_only.set_editor_property("a", False)

def link(src, dst, pin):
    """연결이 안 되면 엔진은 조용히 기본 머티리얼로 그린다(첫 반입: 「Missing If AGreaterThanB input」) — 실패를 바로 멈춘다."""
    if not mel.connect_material_expressions(src, "", dst, pin):
        unreal.log_error("[HairUnderHat] 연결 실패: {} → {}".format(src.get_name(), pin))
        raise SystemExit(1)


link(local, z_only, "")
top = node(unreal.MaterialExpressionScalarParameter, -600, 350)
top.set_editor_property("parameter_name", "HairTopZ")
top.set_editor_property("default_value", NO_CLIP_Z)
one = node(unreal.MaterialExpressionConstant, -400, 450)
one.set_editor_property("r", 1.0)
zero = node(unreal.MaterialExpressionConstant, -400, 550)
zero.set_editor_property("r", 0.0)
cut = node(unreal.MaterialExpressionIf, -200, 250)
link(z_only, cut, "A")
link(top, cut, "B")
link(zero, cut, "A > B")   # 핀 이름은 표시 이름이다(`AGreaterThanB` 로는 안 붙는다)
link(one, cut, "A == B")
link(one, cut, "A < B")
mel.connect_material_property(cut, "", unreal.MaterialProperty.MP_OPACITY_MASK)

mel.recompile_material(mat)
assets.save_asset(PATH, only_if_is_dirty=False)
unreal.log_warning("[HairUnderHat] {} 만듦 — 섞기 {} · 칸 {} {}".format(
    PATH, mat.get_editor_property("blend_mode"), mel.get_scalar_parameter_names(mat), mel.get_vector_parameter_names(mat)))
