"""M5-06b·M9-01·#0204·M13-43·스킨 c 장비 소켓 — `SK_Player_Body` 의 두 손 뼈에 `Socket_Weapon`·`Socket_Tool`, 머리 뼈에
`Socket_Head`(정수리)·`Socket_Face`(얼굴 장식 — 머리 앞면 가운데), 가슴 뼈에 `Socket_Back`(배낭) 을 만든다.

실행 (uproject·script 둘 다 반드시 절대경로 — docs/checks/unreal.md 알려진 함정):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
      ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/setup_equipment_socket.py" \
      -unattended -nosplash -stdout -AllowStdOutLogVerbosity \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" \
      -log

소켓 이름 `Socket_Weapon` 은 `DT_ItemTable` 의 `Item_Club` 행이 이미 가리키는 이름이다(M5-06a).
여기서 만드는 것이 그 이름의 실체다 — 없으면 `UEquipmentComponent` 가 붙일 자리를 못 찾는다.
`Socket_Tool` 은 아직 가리키는 행이 없다(도구 2종은 M9-04, 바르코 에셋이라 감독 대기) — 자리를 먼저
만들어 두면 도구 행이 들어오는 날 `EquipSocketName` 만 적으면 된다. **부위가 갈려 있으니 무기와 도구는
동시에 들린다** — "한 부위 한 개" 는 부위 안에서만이다(2층 `Equip.WeaponAndToolTogether`).

**소켓은 스켈레톤이 아니라 스켈레탈 메시에 붙는다.** ROADMAP 문구는 "스켈레톤에" 였지만 이 엔진의
파이썬 API 는 `USkeleton` 쪽 소켓을 아예 안 열어 준다(`Property 'Sockets' … is protected`,
`dir(skeleton)` 에 소켓 함수 0개 — 이 바퀴 탐색 로그 `logs/lap89_probe2.log`). 열려 있는 것은
`USkeletalMesh.add_socket` 하나뿐이고, 엔진은 부착할 때 **메시 소켓을 스켈레톤 소켓보다 먼저** 본다
(`USkeletalMeshComponent::GetSocketByName`). 몸 메시가 하나뿐이라 결과는 같다 — 몸을 여러 벌로
쪼개는 날(옷 레이어, M5-06 뒤)에 소켓을 스켈레톤으로 옮기는 일이 생긴다.

**뼈 이름은 지어내지 않았다.** 바르코 리그(Mixamo 짜임, 손가락 없는 23뼈)를 읽어 확인한 이름이다 —
`Root/Hips/Spine…/RightShoulder/RightArm/RightForeArm/RightHand` 와 그 왼쪽 짝 `LeftHand`
(`logs/lap89_probe.log`). 없는 뼈면 그 소켓을 만들지 않고 실패로 끝낸다 — 아래 로그가 손 뼈를 전부 찍는다.

**자리·각은 0 에서 시작한다** — 손 뼈 원점에 몽둥이 피벗(손잡이 끝)을 그대로 얹는다. 설계서에 없는
숫자라 지어내지 않고, 캡처를 보고 필요하면 그때 고친다(루프#73 이 목장 배치에서 쓴 방법 그대로).

재실행 안전: 같은 이름의 소켓이 이미 있으면 뼈·자리만 맞춰 두고 새로 만들지 않는다.
"""
import math

import unreal

MESH_PATH = "/Game/Game/Characters/Player/Snare/SK_Player_Body"

# (소켓 이름, 붙일 뼈). 부위가 늘면 여기 한 줄을 더한다.
SOCKETS = [
    ("Socket_Weapon", "RightHand"),  # M5-06b 무기 — 오른손
    ("Socket_Tool", "LeftHand"),     # M9-01 도구 — 왼손
    ("Socket_Head", "Head"),         # #0204 모자 — 머리 (자리는 아래 `head_top_transform`)
    ("Socket_Back", None),           # M13-43 배낭 — 가슴 뼈(목의 부모, 스켈레톤에서 읽는다) · 자리는 `back_transform`
    ("Socket_Face", "Head"),         # 스킨 c 얼굴 장식(`docs/specs/skin-parts.md` 5장 3) — 자리는 `face_front_transform`
]

# 스킨 c 얼굴 장식 자리 — 참조 자세 메시 공간(cm, 몸 앞 = +Y)의 **머리 앞면 가운데**. 이 엔진 파이썬은 스켈레탈 메시
# 정점을 안 열어 주고(GeometryScript 플러그인 꺼짐, 루프#454 탐색) 2층 `Snarehaven.Item.Cosmetic.FaceSocketOnFaceFront` 가
# 몸 정점에서 잰 값을 옮긴다: 머리 띠(정수리 100.12 아래 10~30cm, |X|≤35) 정점 295개 중 가로 가운데 절반의 가장 앞 Y,
# 높이 = 띠 가운데(눈높이 — 고글·안경 자리). 머리 메시를 다시 받으면 그 시험이 1cm 넘게 어긋났다고 알려 준다.
FACE_FRONT_CM = (-0.580, 26.901, 80.117)

# M13-43 배낭 자리 (DESIGN 3.5 「도구 크기」 하이킹 배낭 행).
BACKPACK_PATH = "/Game/Game/Characters/Equipment/Backpacks/HikingBackpack/SM_Backpack_HikingBackpack_01"
# M13-44 가슴 뼈에서 등 표면까지(메시 공간 cm) — DESIGN 3.5 하이킹 배낭 행 "등 표면 10.6cm".
# 옛 값 조끼 상자 깊이 44 의 절반(22)은 조끼 두께까지 넣어 배낭이 등에서 떴다(STATUS #0238). 12.5(감독 Blender 실측)는
# 지금 ìì²´ ì ì 몸 메시에서 위 등 쪽이 월드 3~6cm 떴다(감독 옆면 캡처 m_w3) → 실제 몸 꼭짓점 실측 평균으로 10.6(루프#849).
BACK_SURFACE_CM = 10.2
# M13-44 배낭은 서 있는 클립(`ASnarehavenCharacter` 가 거는 Idle) 첫 프레임의 가슴 뼈로 자리를 잡는다 — 참조 자세(T자)로
# 잡으면 Idle 에서 가슴 뼈가 약 2° 기울어 배낭 모서리가 등을 1.1cm(월드) 뚫었다(2층 첫 실행). 🟡
IDLE_ANIM_PATH = "/Game/Game/Characters/Player/Snare/A_Player_Idle"
# 🟡 배낭 메시의 앞(주머니 쪽)은 glTF +Z → 메시 +Y 로 가정한다. 몸 앞도 메시 +Y 라 180 돌려 주머니가 뒤를 보게 한다.
# 감독 3층에서 끈이 바깥으로 보이면 0 으로 바꾼다.
BACKPACK_YAW = 180.0

# #0204 모자 자리표시(엔진 원기둥 100cm)를 모자 크기로 줄이는 배율. 🟡 모습뿐인 팔레트 값 — 진짜 모자 메시가
# 오면 1.0 으로 되돌린다. 게임 규칙 숫자가 아니라 DESIGN 에 적지 않는다.
# #0206 부착이 소켓 배율을 안 쓴다(`SnapToTargetNotIncludingScale`) — 모자 크기는 `UEquipmentComponent::HatWorldSize` 가 정한다.
HAT_PLACEHOLDER_SCALE = unreal.Vector(0.3, 0.3, 0.12)

# 손 뼈에 그대로 얹는다 (위 설명 — 설계서에 없는 숫자를 만들지 않는다).
RELATIVE_LOCATION = unreal.Vector(0.0, 0.0, 0.0)
RELATIVE_ROTATION = unreal.Rotator(0.0, 0.0, 0.0)

fail = 0

mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
if not isinstance(mesh, unreal.SkeletalMesh):
    unreal.log_error("실패 — 몸 메시를 못 읽음: {}".format(MESH_PATH))
    raise SystemExit(1)

# 뼈 이름을 먼저 전부 모은다 — 아래에서 소켓마다 "붙일 뼈가 진짜 있나" 를 이 목록으로 본다.
def bone_names(bone):
    """뿌리에서 내려가며 뼈 이름을 모은다 — `get_bone_children` 은 한 단계만 준다(재귀 인자 없음)."""
    found = [bone]
    for child in mesh.get_bone_children(bone):
        found += bone_names(str(child))
    return found


bones = bone_names("Root")


def head_top_transform():
    """머리 뼈 기준 "정수리" 자리 — 숫자를 지어내지 않고 몸 메시에서 읽는다.
    기준 자세의 머리 뼈 위치(X·Y)에 메시 경계 맨 위(Z = 정수리)를 붙이고, 축은 몸 축(위 = +Z)에 맞춘다
    (뼈 축이 기울어 있어도 납작한 배율이 위아래로 걸리게)."""
    pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.skeleton)
    head = unreal.AnimPoseExtensions.get_bone_pose(pose, "Head", unreal.AnimPoseSpaces.WORLD)
    bounds = mesh.get_bounds()
    top_z = bounds.origin.z + bounds.box_extent.z
    head_loc = head.translation
    want = unreal.Transform(unreal.Vector(head_loc.x, head_loc.y, top_z), unreal.Rotator(0.0, 0.0, 0.0),
                            HAT_PLACEHOLDER_SCALE)
    rel = want.make_relative(head)
    unreal.log_warning("머리 뼈 Z {:.1f} · 정수리 Z {:.1f} → 소켓 {}".format(head_loc.z, top_z, rel))
    return rel
unreal.log_warning("뼈 {}개 — 손 뼈 {}".format(len(bones), [b for b in bones if "Hand" in b]))


def face_front_transform():
    """머리 뼈 기준 얼굴 앞면 가운데(`FACE_FRONT_CM`). 축은 몸 축(앞 = +Y, 위 = +Z) — 얼굴 장식 메시는 몸과 같은 축으로 만든다."""
    pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.skeleton)
    head = unreal.AnimPoseExtensions.get_bone_pose(pose, "Head", unreal.AnimPoseSpaces.WORLD)
    want = unreal.Transform(unreal.Vector(*FACE_FRONT_CM), unreal.Rotator(0.0, 0.0, 0.0), unreal.Vector(1.0, 1.0, 1.0))
    rel = want.make_relative(head)
    unreal.log_warning("머리 뼈 {} · 얼굴 앞면 {} → 소켓 {}".format(head.translation, want.translation, rel))
    return rel


def chest_bone():
    """목 뼈의 부모 = 가슴(위 척추) 뼈. 이름을 지어내지 않고 스켈레톤에서 읽는다."""
    for bone in bones:
        if "Neck" in [str(c) for c in mesh.get_bone_children(bone)]:
            return bone
    unreal.log_error("실패 — 목 뼈(Neck)의 부모를 못 찾았다: {}".format(MESH_PATH))
    raise SystemExit(1)


def back_transform(bone_name):
    """가슴 뼈 높이, 등 쪽(메시 −Y)으로 등 표면(`BACK_SURFACE_CM`) + 배낭 깊이 절반 뒤에 배낭 경계 가운데가 오게 한다
    — 배낭 몸 쪽 면이 등 표면에 딱 닿는다. 가슴 뼈는 Idle 첫 프레임 자세에서 읽는다(서 있는 화면에서 곧게 선다).
    몸 배율은 붙일 때(`bEquipFollowsBodyScale`) 소켓 배율로 따라오므로 여기는 메시 공간 cm 그대로다."""
    backpack = unreal.EditorAssetLibrary.load_asset(BACKPACK_PATH)
    if not isinstance(backpack, unreal.StaticMesh):
        unreal.log_error("실패 — 배낭 메시를 못 읽음 (import_equipment_weapons.py 먼저): {}".format(BACKPACK_PATH))
        raise SystemExit(1)
    b = backpack.get_bounds()
    depth = b.box_extent.y * 2.0
    idle = unreal.EditorAssetLibrary.load_asset(IDLE_ANIM_PATH)
    if not isinstance(idle, unreal.AnimSequence):
        unreal.log_error("실패 — Idle 클립을 못 읽음: {}".format(IDLE_ANIM_PATH))
        raise SystemExit(1)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(idle, 0.0, unreal.AnimPoseEvaluationOptions())
    chest = unreal.AnimPoseExtensions.get_bone_pose(pose, bone_name, unreal.AnimPoseSpaces.WORLD)
    c = chest.translation
    rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=BACKPACK_YAW)
    center = unreal.Vector(c.x, c.y - (BACK_SURFACE_CM + depth / 2.0), c.z)
    # 메시 피벗 → 경계 가운데 오프셋을 돌려서 뺀다 — 소켓 원점에 피벗이 오니 가운데가 `center` 에 오게.
    yaw = math.radians(BACKPACK_YAW)
    o = b.origin
    origin = unreal.Vector(o.x * math.cos(yaw) - o.y * math.sin(yaw), o.x * math.sin(yaw) + o.y * math.cos(yaw), o.z)
    want = unreal.Transform(center - origin, rot, unreal.Vector(1.0, 1.0, 1.0))
    rel = want.make_relative(chest)
    body = mesh.get_bounds()
    unreal.log_warning("가슴 뼈 {} 자리 {} · 몸 경계 높이 {:.1f} · 등 표면 {:.1f} · 배낭 깊이 {:.1f} → 배낭 가운데 {} · 소켓 {}".format(
        bone_name, c, body.box_extent.z * 2.0, BACK_SURFACE_CM, depth, center, rel))
    return rel

for socket_name, bone_name in SOCKETS:
    bone_name = bone_name or chest_bone()
    # 붙일 뼈가 진짜 있는지부터 — 없는 뼈에 소켓을 만들면 에디터에서는 조용히 만들어지고
    # 런타임 부착만 실패한다(찾기 어려운 쪽으로 깨진다).
    if bone_name not in bones:
        unreal.log_error("실패 — 뼈 `{}` 가 이 스켈레톤에 없다: {}".format(bone_name, MESH_PATH))
        raise SystemExit(1)

    socket = mesh.find_socket(socket_name)
    if socket is None:
        # `add_socket` 은 이름이 아니라 **소켓 객체**를 받는다. 바깥(outer)을 메시로 두지 않으면
        # 저장할 때 딸려 가지 않는다 — 소켓은 메시 패키지 안에 사는 UObject 다.
        socket = unreal.SkeletalMeshSocket(mesh)
        mesh.add_socket(socket)
        # `socket_name`·`bone_name` 은 파이썬에서 **읽기 전용**이다(루프#89 의 2·3차 실행이 그렇게 죽었다).
        # 열려 있는 길은 이름은 `rename_socket`, 붙일 뼈는 `set_socket_parent` 뿐이다.
        blank_name = socket.get_editor_property("socket_name")
        mesh.rename_socket(blank_name, socket_name)
        socket = mesh.find_socket(socket_name)
        if socket is None:
            unreal.log_error("실패 — 소켓 이름을 `{}` 로 못 바꿨다 (붙일 때 이름 `{}`)".format(
                socket_name, blank_name))
            raise SystemExit(1)
        unreal.log_warning("소켓을 새로 만들었다: {}".format(socket_name))
    else:
        unreal.log_warning("소켓이 이미 있다 — 뼈·자리만 맞춘다: {}".format(socket_name))

    # 붙일 뼈는 소켓이 아니라 **메시를 같이 넘겨** 정한다 (`set_socket_parent(skeletal_mesh, bone_name)`).
    socket.set_socket_parent(mesh, bone_name)
    if socket_name == "Socket_Head":
        socket.set_socket_local_transform(head_top_transform())
    elif socket_name == "Socket_Back":
        socket.set_socket_local_transform(back_transform(bone_name))
    elif socket_name == "Socket_Face":
        socket.set_socket_local_transform(face_front_transform())
    else:
        socket.set_socket_local_transform(unreal.Transform(RELATIVE_LOCATION, RELATIVE_ROTATION, unreal.Vector(1.0, 1.0, 1.0)))

if not unreal.EditorAssetLibrary.save_asset(MESH_PATH):
    unreal.log_error("실패 — 메시를 저장 못 했다: {}".format(MESH_PATH))
    fail += 1

# 같은 프로세스의 메모리를 다시 읽는 것은 저장 증거가 못 된다(루프#53 함정) —
# 여기서는 개수·뼈만 찍고, 새 프로세스에서 확인하는 일은 2층 검사가 한다.
for socket_name, bone_name in SOCKETS:
    check = mesh.find_socket(socket_name)
    unreal.log_warning("소켓 {}개 · `{}` -> 뼈 `{}` · 자리 {}".format(
        mesh.num_sockets(), socket_name,
        check.get_editor_property("bone_name") if check else "(없다)",
        check.get_editor_property("relative_location") if check else "-"))
    if check is None:
        unreal.log_error("실패 — 만든 소켓을 다시 못 찾았다: {}".format(socket_name))
        fail += 1

unreal.log_warning("setup_equipment_socket 끝 — 실패 {}건".format(fail))
