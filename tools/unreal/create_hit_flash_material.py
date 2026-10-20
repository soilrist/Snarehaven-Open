"""M21-02 (DESIGN 3.1 「흰 깜빡임」) — 맞은 동물을 하얗게 덮는 머티리얼 `M_HitFlash` 를 만든다.

동물 몸 메시의 `OverlayMaterial` 자리에 깜빡임 시간 동안만 건다 (`AAnimalCharacter::Multicast_PlayHitReaction`).
Mobile 렌더러라 후처리 대신 **겹쳐 그리는 한 겹**이다: 언릿 · 반투명 · 발광 흰색 · 불투명도 `FlashOpacity`.
몸 위에 한 번 더 그릴 뿐이라 그리기 호출이 깜빡이는 0.08초 동안 하나 는다 (DESIGN 4.2 모바일 한도 안).

실행 (인자 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_hit_flash_material.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

재실행: 있으면 건너뛴다 (동물 CDO 가 붙잡고 있어 제자리 수정은 엔진이 죽는다 — 바꾸려면 .uasset 을 지우고 다시 돌린다).
단 스켈레탈 메시 사용 플래그만은 있는 에셋에도 켜고 저장한다 — 플래그가 없으면 게임에서 기본 머티리얼로 바뀌어
흰 깜빡임·변종 금빛이 아예 안 그려진다(루프#478, 로그 `missing usage flag SkeletalMesh!`).
"""
import unreal

FOLDER = "/Game/Game/VFX/Hit"
NAME = "M_HitFlash"
PATH = "{}/{}".format(FOLDER, NAME)

MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _must(ok, what):
    # 잇기가 조용히 실패하면 깜빡임이 검게 저장된다 — 여기서 멈춘다.
    if not ok:
        raise RuntimeError("hit flash material wiring failed: " + what)


def _ensure_skeletal_usage(mat):
    # 동물 몸은 스켈레탈 메시라 이 플래그가 없으면 겹 자리에 기본 머티리얼이 그려진다.
    if mat.get_editor_property("used_with_skeletal_mesh"):
        return False
    mat.set_editor_property("used_with_skeletal_mesh", True)
    MEL.recompile_material(mat)
    return True


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        mat = unreal.load_asset(PATH)
        if _ensure_skeletal_usage(mat):
            unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
            unreal.log("hit flash material usage fixed: {} used_with_skeletal_mesh=True".format(PATH))
            return
        unreal.log_warning("hit flash material exists, skip: {} (delete the .uasset to rebuild)".format(PATH))
        return
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, -100)
    color.set_editor_property("parameter_name", "FlashColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    _must(MEL.connect_material_property(color, "", MP.MP_EMISSIVE_COLOR), "color -> emissive")

    # 0.8: 몸 무늬가 살짝 비쳐야 "하얀 무언가" 가 아니라 "맞아서 번쩍인 그 동물" 로 읽힌다.
    opacity = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 100)
    opacity.set_editor_property("parameter_name", "FlashOpacity")
    opacity.set_editor_property("default_value", 0.8)
    _must(MEL.connect_material_property(opacity, "", MP.MP_OPACITY), "opacity -> opacity")

    mat.set_editor_property("used_with_skeletal_mesh", True)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    unreal.log("hit flash material saved: {} (expressions={})".format(PATH, MEL.get_num_material_expressions(mat)))


main()
