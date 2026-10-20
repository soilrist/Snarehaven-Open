"""M0-03/M1-03/M1-05/M1-10b Enhanced Input 에셋 — IA_Move(Axis2D)·IA_Attack(Bool)·IA_Capture(Bool)·IA_Pause(Bool)
+ IMC_Default 을 헤드리스로 만든다.

실행 (uproject·script 둘 다 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_input_assets.py" \
      -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

이동 키는 Gamepad_Left2D 를 맵핑한다. 엔진 기본 터치 인터페이스
(/Engine/MobileResources/HUD/DefaultVirtualJoysticks) 의 왼쪽 스틱이 Gamepad_LeftX/LeftY 를
내보내고, 그 둘이 짝을 이루는 2D 키가 Gamepad_Left2D 라서 가상 조이스틱이 그대로 IA_Move 로 들어온다.
M1-10a(감독 지시 2026-09-20): 데스크톱에서 마우스로는 조이스틱을 못 끌기 때문에 키보드 이동을 같이 맵핑한다 —
W/A/S/D 와 방향키 여덟 개. 키보드 키는 1D 라 Axis2D 의 X 로만 들어오므로 모디파이어로 방향을 만든다:
위(W·Up)=Swizzle(YXZ) · 아래(S·Down)=Swizzle(YXZ)+Negate · 왼쪽(A·Left)=Negate · 오른쪽(D·Right)=없음.
마우스 이동은 넣지 않는다.
공격 키(M1-03)는 Gamepad_FaceButton_Bottom(패드) + LeftMouseButton(에디터 PIE 확인용) 둘을 맵핑한다.
스페이스바(INBOX 18:20 ③, 2026-10-04)도 IA_Attack 에 건다 — 화면 상황 버튼(공격·채집·베기·포획·낚시…)과
**같은** `BeginAttack` 으로 들어가 따로 판정을 만들지 않는다.
포획 키(M1-05)는 Gamepad_FaceButton_Right(패드 B) + RightMouseButton 둘 — 공격과 손가락이 겹치지 않게 다른 버튼에 둔다.
진짜 모바일 공격·포획 버튼은 화면 위젯이라 M1-08 사냥 HUD 조각에서 이 액션들에 연결한다.
일시정지 키(M1-10b, 감독 지시 2026-09-20)는 Escape + Gamepad_Special_Right(패드 Start) 둘 —
누르면 `WBP_PauseMenu` 가 뜨고 다시 누르면 닫힌다. 월드는 멈추지 않는다(멀티, DESIGN 3.4).
이미 있으면 덮어쓰지 않는다(재실행해도 안전).
"""
import unreal

ASSET_DIR = "/Game/Game/Input"
AXIS_KEY = "Gamepad_Left2D"
# (키 이름, 위/아래 축인가, 반대 방향인가) — 모디파이어를 이 두 칸으로 만든다.
MOVE_KEYBOARD_KEYS = (
    ("W", True, False),
    ("S", True, True),
    ("A", False, True),
    ("D", False, False),
    ("Up", True, False),
    ("Down", True, True),
    ("Left", False, True),
    ("Right", False, False),
)
ATTACK_KEYS = ("Gamepad_FaceButton_Bottom", "LeftMouseButton", "SpaceBar")
CAPTURE_KEYS = ("Gamepad_FaceButton_Right", "RightMouseButton")
PAUSE_KEYS = ("Escape", "Gamepad_Special_Right")


def _get_or_create(name: str, asset_class, factory):
    path = "{}/{}".format(ASSET_DIR, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("skip (already exists): {}".format(path))
        return unreal.EditorAssetLibrary.load_asset(path), False
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ASSET_DIR, asset_class, factory)
    if asset is None:
        unreal.log_error("failed to create: {}".format(path))
        return None, False
    unreal.log("created: {}".format(path))
    return asset, True


def _axis2d_value_type():
    # Python 쪽 enum 이름 표기가 버전마다 갈려서(AXIS2_D / AXIS2D) 있는 쪽을 쓴다.
    for candidate in ("AXIS2_D", "AXIS2D", "Axis2D"):
        value = getattr(unreal.InputActionValueType, candidate, None)
        if value is not None:
            return value
    raise RuntimeError("EInputActionValueType Axis2D 를 Python 에서 못 찾음")


def _make_key(name: str) -> unreal.Key:
    # FKey 는 Python 생성자 인자를 안 받는다(TypeError) — 빈 구조체를 만들고 KeyName 만 채운다.
    key = unreal.Key()
    key.set_editor_property("key_name", name)  # KeyName 은 Python 속성으로는 안 보이고 set_editor_property 로만 들어간다
    unreal.log("key: {}".format(key))
    return key


def _bool_value_type():
    for candidate in ("BOOLEAN", "Boolean", "Bool"):
        value = getattr(unreal.InputActionValueType, candidate, None)
        if value is not None:
            return value
    raise RuntimeError("EInputActionValueType Boolean 을 Python 에서 못 찾음")


def _mapping(action, key_name: str, modifiers=None) -> unreal.EnhancedActionKeyMapping:
    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("action", action)
    mapping.set_editor_property("key", _make_key(key_name))
    if modifiers:
        mapping.set_editor_property("modifiers", modifiers)
    return mapping


def _swizzle_yxz(owner):
    # 키보드 1D 값은 Axis2D 의 X 로 들어온다 — 위/아래 키는 X 와 Y 를 맞바꿔 Y 로 보낸다.
    # 모디파이어는 Instanced UObject 다 — 기본 생성자(unreal.InputModifierSwizzleAxis())로 만들면
    # Transient 패키지에 붙어 에셋과 같이 저장되지 않는다(루프#52 에서 저장 뒤 전부 null 이었다).
    # 그래서 IMC 에셋을 Outer 로 주고 만든다.
    modifier = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=owner)
    for candidate in ("YXZ", "Y_X_Z"):
        order = getattr(unreal.InputAxisSwizzle, candidate, None)
        if order is not None:
            modifier.set_editor_property("order", order)
            return modifier
    raise RuntimeError("EInputAxisSwizzle YXZ 를 Python 에서 못 찾음")


def _keyboard_move_mappings(action, owner):
    mappings = []
    for key_name, is_vertical, is_negative in MOVE_KEYBOARD_KEYS:
        modifiers = []
        if is_vertical:
            modifiers.append(_swizzle_yxz(owner))
        if is_negative:
            modifiers.append(unreal.new_object(unreal.InputModifierNegate, outer=owner))
        mappings.append(_mapping(action, key_name, modifiers))
    return mappings


move_action, _ = _get_or_create("IA_Move", unreal.InputAction, unreal.InputAction_Factory())
if move_action is not None:
    move_action.set_editor_property("value_type", _axis2d_value_type())
    unreal.EditorAssetLibrary.save_loaded_asset(move_action)

attack_action, _ = _get_or_create("IA_Attack", unreal.InputAction, unreal.InputAction_Factory())
if attack_action is not None:
    attack_action.set_editor_property("value_type", _bool_value_type())
    unreal.EditorAssetLibrary.save_loaded_asset(attack_action)

capture_action, _ = _get_or_create("IA_Capture", unreal.InputAction, unreal.InputAction_Factory())
if capture_action is not None:
    capture_action.set_editor_property("value_type", _bool_value_type())
    unreal.EditorAssetLibrary.save_loaded_asset(capture_action)

pause_action, _ = _get_or_create("IA_Pause", unreal.InputAction, unreal.InputAction_Factory())
if pause_action is not None:
    pause_action.set_editor_property("value_type", _bool_value_type())
    unreal.EditorAssetLibrary.save_loaded_asset(pause_action)

context, created = _get_or_create("IMC_Default", unreal.InputMappingContext, unreal.InputMappingContext_Factory())
if (context is not None and move_action is not None and attack_action is not None and capture_action is not None
        and pause_action is not None):
    # 매핑 목록은 통째로 다시 쓴다 — 액션이 늘 때마다 여기 한 곳만 보면 되도록.
    mappings = [_mapping(move_action, AXIS_KEY)]
    mappings += _keyboard_move_mappings(move_action, context)
    mappings += [_mapping(attack_action, key_name) for key_name in ATTACK_KEYS]
    mappings += [_mapping(capture_action, key_name) for key_name in CAPTURE_KEYS]
    mappings += [_mapping(pause_action, key_name) for key_name in PAUSE_KEYS]
    mapping_data = unreal.InputMappingContextMappingData()
    mapping_data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", mapping_data)
    unreal.EditorAssetLibrary.save_loaded_asset(context)
    unreal.log("IMC_Default: 매핑 {}개 저장 완료".format(len(mappings)))

    # 저장된 에셋을 다시 읽어 매핑을 찍는다 (검사 로그 — Verification.md "못 한 검사는 통과가 아니다").
    for entry in context.get_editor_property("default_key_mappings").get_editor_property("mappings"):
        entry_action = entry.get_editor_property("action")
        modifier_names = [
            m.get_class().get_name() if m is not None else "None"
            for m in entry.get_editor_property("modifiers")
        ]
        unreal.log("확인: {} <- {} (value_type={}, 모디파이어 {})".format(
            entry_action.get_name(),
            entry.get_editor_property("key").get_editor_property("key_name"),
            entry_action.get_editor_property("value_type"),
            modifier_names or "없음"))
