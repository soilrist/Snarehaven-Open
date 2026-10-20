"""M39-07c (DESIGN 8.6-c 「맞는 순간 퍽·튀김」) — 총알이 닿는 순간의 퍽 공 머티리얼 `M_ImpactPuff` 를 만든다.

`M_HitFlash` 는 불투명도가 한 값이라 공 전체가 같은 진하기 → 게임 카메라에서 「하얀 원판」 으로 돼지를 통째로
덮었다(감독 07:45 보정). 이건 **가운데 진하고 가장자리 0** 인 공이다: 언릿 · 반투명 · 발광 `FlashColor` ·
불투명도 = `FlashOpacity` × (카메라를 보는 정도)^3. 공의 법선이 카메라를 볼수록 1, 윤곽(옆을 볼수록) 0 이라
화면에서 원 반지름 r 의 진하기 ≈ (1 − r²)^1.5 — r 0.5 에서 0.65, 0.8 에서 0.22. 맞은 동물 몸이 가장자리 너머로 보인다.
쓰는 곳: `ASnareGunTracerActor` 의 퍽(총알·꼬리·불꽃은 그대로 `M_HitFlash`) + 동물 튀김 조각(인스턴스 메시,
`EdgeFalloff` 0 = 고른 발광 — 조명 받는 조각은 나무 그늘에서 검은 덩이로 보였다, 루프#585 캡처).

실행 (인자 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_impact_puff_material.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행: 있으면 건너뛴다 (바꾸려면 .uasset 을 지우고 다시 돌린다).
"""
import unreal

FOLDER = "/Game/Game/VFX/Hit"
NAME = "M_ImpactPuff"
PATH = "{}/{}".format(FOLDER, NAME)
# 가장자리로 갈수록 옅어지는 세기(`EdgeFalloff` 기본값) — 1 이면 윤곽 근처에서만 옅고, 3 이면 공의 바깥 절반이 비친다.
# 0 이면 어디나 1 (고른 발광 — 튀김 조각).
EDGE_FALLOFF_POWER = 3.0

MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _must(ok, what):
    # 잇기가 조용히 실패하면 공이 검게·불투명하게 저장된다 — 여기서 멈춘다.
    if not ok:
        raise RuntimeError("impact puff material wiring failed: " + what)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        unreal.log_warning("impact puff material exists, skip: {} (delete the .uasset to rebuild)".format(PATH))
        return
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -200)
    color.set_editor_property("parameter_name", "FlashColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    _must(MEL.connect_material_property(color, "", MP.MP_EMISSIVE_COLOR), "color -> emissive")

    # 프레넬(지수 1·바탕 0) = 1 − (법선·카메라) → 뒤집으면 「카메라를 보는 정도」.
    fresnel = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -800, 100)
    fresnel.set_editor_property("exponent", 1.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.0)
    facing = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -600, 100)
    _must(MEL.connect_material_expressions(fresnel, "", facing, ""), "fresnel -> one minus")
    falloff = MEL.create_material_expression(mat, unreal.MaterialExpressionPower, -400, 100)
    _must(MEL.connect_material_expressions(facing, "", falloff, "Base"), "one minus -> power")
    edge = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 250)
    edge.set_editor_property("parameter_name", "EdgeFalloff")
    edge.set_editor_property("default_value", EDGE_FALLOFF_POWER)
    _must(MEL.connect_material_expressions(edge, "", falloff, "Exp"), "edge falloff -> power exp")

    opacity = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 300)
    opacity.set_editor_property("parameter_name", "FlashOpacity")
    opacity.set_editor_property("default_value", 1.0)
    multiply = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 200)
    _must(MEL.connect_material_expressions(falloff, "", multiply, "A"), "power -> multiply A")
    _must(MEL.connect_material_expressions(opacity, "", multiply, "B"), "opacity -> multiply B")
    _must(MEL.connect_material_property(multiply, "", MP.MP_OPACITY), "multiply -> opacity")

    mat.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    unreal.log("impact puff material saved: {} (expressions={})".format(PATH, MEL.get_num_material_expressions(mat)))


main()
