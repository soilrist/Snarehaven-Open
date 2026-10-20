"""가림 페이드 — 카메라와 캐릭터 사이를 막는 나무가 디더로 뚫리게 하는 머티리얼을 만든다 (INBOX 결함 10-13 「나무 뒤 플레이어 가림」).

실행 (절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/create_view_fade_material.py" \
      -unattended -nosplash -nullrhi \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

만드는 것:
 1. `MPC_ViewFade` — 파라미터 모음. `ViewFadeTarget`(xyz 캐릭터 가슴, w 반지름) · `ViewFadeCamera`(xyz 카메라, w 켜짐 0~1).
    값은 `USnareViewFadeSubsystem` 이 매 프레임 쓴다.
 2. `M_Nature_MatteFade` — `M_Nature_Matte` 복사본 + 마스크드 + 불투명도 마스크(Custom HLSL). 식은 `SnareViewFade::Coverage`(C++)와 같다.
 3. 나무 잎·껍질 머티리얼 인스턴스의 부모를 위 머티리얼로 바꾼다 (모양·색은 그대로).
재실행 안전: 있으면 만들기는 건너뛰고 부모 바꾸기만 다시 맞춘다. 모바일 렌더러: 마스크드 + 디더(화면 좌표 잡음)라 반투명 정렬 문제 없음.
숫자(반지름 250 · 앞 40+80 · 가장자리 0.4 · 최대 0.85)는 `SnareViewFade.h` 와 DESIGN 3.3 가림 페이드 표가 원본이다.
"""
import unreal

FOLDER = "/Game/Game/Environment/Nature"
MPC_PATH = FOLDER + "/MPC_ViewFade"
SRC_PATH = FOLDER + "/M_Nature_Matte"
FADE_PATH = FOLDER + "/M_Nature_MatteFade"
# 나무 잎·껍질로 쓰는 인스턴스 (`setup_zones.py` 의 나무 슬롯 + `import_tree_meshes.py` 기본값)
TREE_INSTANCES = ["Pine", "Bark", "BirchBark", "Broad", "Lime", "Gold", "Orange", "Lilac"]

# 가림 페이드 넓히기(INBOX 10-13): 큰 바위·집·목장 건물·광산 입구. 경로는 FOLDER 의 형제 폴더 기준.
# 높이 130cm 미만(꽃·울타리·길·화단·바닥)은 넣지 않는다. 한계: 무늬 바위 `MI_Nature_Rocks`(SM_Nature_Rock_01/02, 무늬 그림 쓰는 Phong)는 무늬가 사라져 제외.
BUILDING_FOLDER = "/Game/Game/Environment/Buildings"
EXTRA_INSTANCES = [FOLDER + "/MI_Nature_Leafs_" + n for n in ("Rock", "MineRock")] + [
    BUILDING_FOLDER + "/MI_Building_" + n for n in (
        "CottageWall", "CottageTrim", "CottageRoof", "CottageDoor", "CottageGlass", "BarnRoofRed",   # 집
        "White", "LightRed", "DarkRed", "RoofBlack", "Brown", "Grey", "DarkGrey")]                       # 헛간·닭장·사일로·풍차·우물

RADIUS, AHEAD_START, AHEAD_RAMP, EDGE_SOFT, MAX_COVERAGE = 250.0, 40.0, 80.0, 0.4, 0.85

HLSL = """
float3 ToCam = Cam - Tgt;
float Len = max(length(ToCam), 1.0);
float3 Dir = ToCam / Len;
float3 V = WP - Tgt;
float Along = dot(V, Dir);
float Perp = length(V - Dir * Along);
float Ahead = saturate((Along - %(a0)s) / %(a1)s);
float Edge = saturate((Rad - Perp) / (Rad * %(es)s));
float Cover = Ahead * Edge * saturate(Str) * %(mx)s;
float Noise = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy, float2(0.06711056, 0.00583715))));
return Noise >= Cover ? 1.0 : 0.0;
""" % {"a0": AHEAD_START, "a1": AHEAD_RAMP, "es": EDGE_SOFT, "mx": MAX_COVERAGE}

MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _must(ok, what):
    if not ok:
        raise RuntimeError("view fade wiring failed: " + what)


def _make_collection():
    if unreal.EditorAssetLibrary.does_asset_exist(MPC_PATH):
        return unreal.load_asset(MPC_PATH)
    mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MPC_ViewFade", FOLDER, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    params = []
    for name in ("ViewFadeTarget", "ViewFadeCamera"):
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        params.append(p)
    mpc.set_editor_property("vector_parameters", params)
    unreal.EditorAssetLibrary.save_asset(MPC_PATH, only_if_is_dirty=False)
    return mpc


def _make_material(mpc):
    if unreal.EditorAssetLibrary.does_asset_exist(FADE_PATH):
        mat = unreal.load_asset(FADE_PATH)
        # 숫자가 바뀌었을 수 있다 — Custom 노드 식만 갈아 끼운다(배선은 그대로)
        node = MEL.get_material_property_input_node(mat, MP.MP_OPACITY_MASK)
        if isinstance(node, unreal.MaterialExpressionCustom) and node.get_editor_property("code") != HLSL:
            node.set_editor_property("code", HLSL)
            MEL.recompile_material(mat)
            unreal.EditorAssetLibrary.save_asset(FADE_PATH, only_if_is_dirty=False)
            unreal.log_warning("view fade material code updated: " + FADE_PATH)
        return mat
    _must(unreal.EditorAssetLibrary.duplicate_asset(SRC_PATH, FADE_PATH) is not None, "duplicate")
    mat = unreal.load_asset(FADE_PATH)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)

    def coll(param, x):
        n = MEL.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, 600)
        n.set_editor_property("collection", mpc)
        n.set_editor_property("parameter_name", param)
        return n

    target, camera = coll("ViewFadeTarget", -900), coll("ViewFadeCamera", -900)
    camera.set_editor_property("parameter_name", "ViewFadeCamera")
    wpos = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, 300)
    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -400, 450)
    custom.set_editor_property("code", HLSL)
    custom.set_editor_property("description", "ViewFade")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for n in ("WP", "Cam", "Tgt", "Rad", "Str"):
        i = unreal.CustomInput()
        i.set_editor_property("input_name", n)
        inputs.append(i)
    custom.set_editor_property("inputs", inputs)

    # 벡터 → xyz / w 갈라 쓰기 (마스크 노드)
    def mask(src, channels):
        m = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -650, 600)
        for c, on in zip("rgba", channels):
            m.set_editor_property(c, on)
        _must(MEL.connect_material_expressions(src, "", m, ""), "mask")
        return m

    cam_xyz, cam_w = mask(camera, (1, 1, 1, 0)), mask(camera, (0, 0, 0, 1))
    tgt_xyz, tgt_w = mask(target, (1, 1, 1, 0)), mask(target, (0, 0, 0, 1))
    for src, name in ((wpos, "WP"), (cam_xyz, "Cam"), (tgt_xyz, "Tgt"), (tgt_w, "Rad"), (cam_w, "Str")):
        _must(MEL.connect_material_expressions(src, "", custom, name), "custom " + name)
    _must(MEL.connect_material_property(custom, "", MP.MP_OPACITY_MASK), "custom -> opacity mask")
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FADE_PATH, only_if_is_dirty=False)
    unreal.log("view fade material saved: {} (expressions={})".format(FADE_PATH, MEL.get_num_material_expressions(mat)))
    return mat


def main():
    mpc = _make_collection()
    mat = _make_material(mpc)
    changed = 0
    for path in ["{}/MI_Nature_Leafs_{}".format(FOLDER, name) for name in TREE_INSTANCES] + EXTRA_INSTANCES:
        name = path.split("/")[-1]
        mi = unreal.load_asset(path)
        if mi is None:
            raise RuntimeError("missing " + path)
        if mi.get_editor_property("parent") != mat:
            MEL.set_material_instance_parent(mi, mat)
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
            changed += 1
        unreal.log("view fade parent: {} -> {}".format(name, mi.get_editor_property("parent").get_name()))
    unreal.log("view fade done: reparented {}".format(changed))


main()
