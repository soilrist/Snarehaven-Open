"""M13-24 (DESIGN 8.12·13.8) — 고른 가구 테두리 머티리얼 `M_Outline_Selected` 를 만든다.

Mobile 렌더러(Nanite·Lumen 끔)라 커스텀 뎁스 후처리 대신 메시 `OverlayMaterial` 로 **뒤집은 껍질**을 그린다:
  - 월드 위치 오프셋 (M13-34 에서 바꿈):
    - 옆(XY): 몸 가운데(ObjectPositionWS)에서 본 **축마다의 부호** × `OutlineThicknessCm` — 길쭉한 가구도 네 변이 같은 두께로 부푼다
      (예전 `normalize(정점 - 가운데)` 는 200×100×60 벤치에서 x 2.6 · y 1.3 · z 0.8cm 로 줄었다)
    - 위아래(Z): 가운데보다 위 정점은 `+OutlineThicknessCm`, 아래 정점은 **`+OutlineGroundLiftCm`(바닥 위로 띄움)**.
      예전처럼 아래로 부풀리면 껍질 밑면이 바닥 **아래**로 들어가, 위에서 내려다본 앞·옆 띠의 시선이 껍질 뒷면보다
      바닥에 먼저 닿아 가려진다(감독 3층 #0220 — 왼쪽 위 모서리만 보임). 밑면을 바닥 위에 두면 그 시선이 밑면 뒷면에 닿는다.
  - 양면 + 마스크: `TwoSidedSign` 이 뒷면(-1)일 때만 남긴다 → 부푼 껍질의 앞면은 몸을 덮지 않고 둘레만 보인다
  - 언릿 발광 = `OutlineColor`
색·두께·띄움 값은 코드(`ASnarePlacedStructureActor`)가 MID 로 넣는다 — 색 원본은 `SnareUiStyle`, 두께는 DESIGN 8.12 표,
띄움은 놓기 미리보기와 같은 `PreviewLiftCm`. 여기 기본값은 자리표시다(띄움 0 = 코드가 안 넣으면 시험이 잡는다).

실행 (인자 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_outline_material.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행: 있으면 건너뛴다 (가구 액터가 붙잡고 있어 제자리 수정은 엔진이 죽는다 — 바꾸려면 .uasset 을 지우고 다시 돌린다).
"""
import unreal

FOLDER = "/Game/Game/VFX/Outline"
NAME = "M_Outline_Selected"
PATH = "{}/{}".format(FOLDER, NAME)

MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _node(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def _must(ok, what):
    # 잇기가 조용히 실패하면 테두리가 안 부푼 채 저장된다 — 여기서 멈춘다.
    if not ok:
        raise RuntimeError("outline material wiring failed: " + what)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        # 가구 액터 CDO 가 이 머티리얼을 붙잡고 있어(루트) 노드를 지우면 엔진이 죽는다 — 다시 만들려면 파일을 먼저 지운다.
        unreal.log_warning("outline material exists, skip: {} (delete the .uasset to rebuild)".format(PATH))
        return
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)

    color = _node(mat, unreal.MaterialExpressionVectorParameter, -400, -200)
    color.set_editor_property("parameter_name", "OutlineColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.85, 0.3, 1.0))
    _must(MEL.connect_material_property(color, "", MP.MP_EMISSIVE_COLOR), "color, '', MP.MP_EMISSIVE_COLOR")

    # 뒷면만: TwoSidedSign(앞 +1 / 뒤 -1) × -1 → 뒤 +1 은 남고 앞 -1 은 잘린다.
    sign = _node(mat, unreal.MaterialExpressionTwoSidedSign, -600, 0)
    flip = _node(mat, unreal.MaterialExpressionMultiply, -400, 0)
    flip.set_editor_property("const_b", -1.0)
    _must(MEL.connect_material_expressions(sign, "", flip, "A"), "sign, '', flip, 'A'")
    _must(MEL.connect_material_property(flip, "", MP.MP_OPACITY_MASK), "flip -> OpacityMask")

    # 부풀리기 (M13-34): d = 정점 월드 위치 - 몸 가운데.
    world_pos = _node(mat, unreal.MaterialExpressionWorldPosition, -1400, 200)
    center = _node(mat, unreal.MaterialExpressionObjectPositionWS, -1400, 320)
    offset = _node(mat, unreal.MaterialExpressionSubtract, -1200, 250)
    _must(MEL.connect_material_expressions(world_pos, "", offset, "A"), "world_pos, '', offset, 'A'")
    _must(MEL.connect_material_expressions(center, "", offset, "B"), "center -> offset.B")
    thickness = _node(mat, unreal.MaterialExpressionScalarParameter, -1000, 500)
    thickness.set_editor_property("parameter_name", "OutlineThicknessCm")
    thickness.set_editor_property("default_value", 5.0)
    lift = _node(mat, unreal.MaterialExpressionScalarParameter, -1000, 620)
    lift.set_editor_property("parameter_name", "OutlineGroundLiftCm")
    lift.set_editor_property("default_value", 0.0)

    # 옆: sign(d.xy) × 두께 — 축마다 같은 두께.
    side_sign = _node(mat, unreal.MaterialExpressionSign, -1000, 200)
    _must(MEL.connect_material_expressions(offset, "", side_sign, ""), "offset -> sign")
    side_xy = _node(mat, unreal.MaterialExpressionComponentMask, -800, 200)
    side_xy.set_editor_property("r", True)
    side_xy.set_editor_property("g", True)
    side_xy.set_editor_property("b", False)
    side_xy.set_editor_property("a", False)
    _must(MEL.connect_material_expressions(side_sign, "", side_xy, ""), "sign -> mask xy")
    side_push = _node(mat, unreal.MaterialExpressionMultiply, -600, 200)
    _must(MEL.connect_material_expressions(side_xy, "", side_push, "A"), "mask xy -> push.A")
    _must(MEL.connect_material_expressions(thickness, "", side_push, "B"), "thickness -> push.B")

    # 위아래: d.z > 0 이면 +두께, 아니면 +띄움(밑면을 바닥 위로).
    dz = _node(mat, unreal.MaterialExpressionComponentMask, -1000, 350)
    dz.set_editor_property("r", False)
    dz.set_editor_property("g", False)
    dz.set_editor_property("b", True)
    dz.set_editor_property("a", False)
    _must(MEL.connect_material_expressions(offset, "", dz, ""), "offset -> mask z")
    up_or_lift = _node(mat, unreal.MaterialExpressionIf, -600, 400)
    up_or_lift.set_editor_property("const_b", 0.0)
    _must(MEL.connect_material_expressions(dz, "", up_or_lift, "A"), "mask z -> if.A")
    _must(MEL.connect_material_expressions(thickness, "", up_or_lift, "A > B"), "thickness -> if.A>B")
    _must(MEL.connect_material_expressions(lift, "", up_or_lift, "A == B"), "lift -> if.A==B")
    _must(MEL.connect_material_expressions(lift, "", up_or_lift, "A < B"), "lift -> if.A<B")

    push = _node(mat, unreal.MaterialExpressionAppendVector, -400, 300)
    _must(MEL.connect_material_expressions(side_push, "", push, "A"), "push xy -> append.A")
    _must(MEL.connect_material_expressions(up_or_lift, "", push, "B"), "if -> append.B")
    _must(MEL.connect_material_property(push, "", MP.MP_WORLD_POSITION_OFFSET), "push, '', MP.MP_WORLD_POSITION_OFFSET")

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    unreal.log("outline material saved: {} (expressions={})".format(PATH, MEL.get_num_material_expressions(mat)))


main()
