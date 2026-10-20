"""STORY-5-IGNITE 보정(INBOX 감독 10-20 16:20) — 낮에도 보이는 등대 빛줄기 머티리얼 `M_LighthouseBeam` 을 만든다.

`ASnareLighthouseActor` 의 빛줄기 메시(엔진 원뿔 `/Engine/BasicShapes/Cone`)에 건다. 스포트 빛은 땅을 비출 뿐이라
낮엔 안 보였다(`lh1421_ignite_11.png`). 그래서 빛 자체를 **가산 · 언릿 · 양면** 한 겹으로 그린다.
  - 길이 방향으로 옅어짐: 원뿔 메시 공간 Z(꼭짓점 +50 = 랜턴, 바닥 -50 = 끝 — 루프#1423 실측)를 0~1 로 바꿔 끝으로 갈수록 0.
  - 가장자리 부드럽게: |메시 법선 · 카메라 쪽| — 원뿔 옆 윤곽(빗겨 보이는 면)이 0 이라 띠 가장자리가 흐려진다.
  - 세기 `BeamStrength`(코드가 밝기 0~1 에 맞춰 넣음) × 색 `BeamColor`(랜턴 점광과 같은 따뜻한 빛).

실행 (인자 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_lighthouse_beam_material.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      -log

재실행: 있으면 건너뛴다 (등대 액터 CDO 가 붙잡고 있다 — 바꾸려면 .uasset 을 지우고 다시 돌린다).
"""
import unreal

FOLDER = "/Game/Game/VFX/Lighthouse"
NAME = "M_LighthouseBeam"
PATH = "{}/{}".format(FOLDER, NAME)
# 원뿔 메시 공간 Z 범위 (엔진 Cone: 바닥 -50 · 꼭짓점 +50)
CONE_BASE_Z = -50.0
CONE_HEIGHT = 100.0

MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _must(ok, what):
    # 잇기가 조용히 실패하면 엔진은 기본 머티리얼(회색 원뿔)로 그린다 — 여기서 멈춘다.
    if not ok:
        unreal.log_error("[LighthouseBeam] 연결 실패: " + what)
        raise SystemExit(1)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        unreal.log_warning("[LighthouseBeam] 이미 있음, 건너뜀: {} (다시 만들려면 .uasset 을 지운다)".format(PATH))
        return
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)

    def node(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def link(src, dst, pin, what):
        _must(MEL.connect_material_expressions(src, "", dst, pin), what)

    # 길이 방향 옅어짐 = saturate((Z - 바닥) / 높이) — 랜턴 1, 끝 0
    local = node(unreal.MaterialExpressionLocalPosition, -1200, 0)
    z_only = node(unreal.MaterialExpressionComponentMask, -1000, 0)
    for ch, on in (("r", False), ("g", False), ("b", True), ("a", False)):
        z_only.set_editor_property(ch, on)
    link(local, z_only, "", "local -> mask")
    shift = node(unreal.MaterialExpressionAdd, -850, 0)
    shift.set_editor_property("const_b", -CONE_BASE_Z)
    link(z_only, shift, "A", "mask -> add")
    scale = node(unreal.MaterialExpressionMultiply, -700, 0)
    scale.set_editor_property("const_b", 1.0 / CONE_HEIGHT)
    link(shift, scale, "A", "add -> mul")
    along = node(unreal.MaterialExpressionSaturate, -550, 0)
    link(scale, along, "", "mul -> saturate")

    # 가장자리 = |법선 · 카메라 쪽|
    normal = node(unreal.MaterialExpressionVertexNormalWS, -1000, 250)
    camera = node(unreal.MaterialExpressionCameraVectorWS, -1000, 350)
    dot = node(unreal.MaterialExpressionDotProduct, -850, 300)
    link(normal, dot, "A", "normal -> dot")
    link(camera, dot, "B", "camera -> dot")
    edge = node(unreal.MaterialExpressionAbs, -700, 300)
    link(dot, edge, "", "dot -> abs")

    shape = node(unreal.MaterialExpressionMultiply, -400, 150)
    link(along, shape, "A", "along -> shape")
    link(edge, shape, "B", "edge -> shape")

    color = node(unreal.MaterialExpressionVectorParameter, -400, -200)
    color.set_editor_property("parameter_name", "BeamColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.72, 0.3, 1.0))
    strength = node(unreal.MaterialExpressionScalarParameter, -400, 350)
    strength.set_editor_property("parameter_name", "BeamStrength")
    strength.set_editor_property("default_value", 1.0)
    tint = node(unreal.MaterialExpressionMultiply, -200, -100)
    link(color, tint, "A", "color -> tint")
    link(shape, tint, "B", "shape -> tint")
    out = node(unreal.MaterialExpressionMultiply, 0, 0)
    link(tint, out, "A", "tint -> out")
    link(strength, out, "B", "strength -> out")
    _must(MEL.connect_material_property(out, "", MP.MP_EMISSIVE_COLOR), "out -> emissive")

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    unreal.log_warning("[LighthouseBeam] {} 만듦 — 섞기 {} · 양면 {} · 칸 {} {} · 노드 {}".format(
        PATH, mat.get_editor_property("blend_mode"), mat.get_editor_property("two_sided"),
        MEL.get_scalar_parameter_names(mat), MEL.get_vector_parameter_names(mat), MEL.get_num_material_expressions(mat)))


main()
