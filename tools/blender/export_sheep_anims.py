"""M41-03 양 걷기·달리기 뽑기 — `Blends/Sheep.blend` 의 액션을 FBX 하나씩 내보낸다.

실행:
  ~/Desktop/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      ~/UNREAL/Snarehaven/_Source/raw/Quaternius_FarmAnimals/Blends/Sheep.blend \
      --python ~/UNREAL/Snarehaven/tools/blender/export_sheep_anims.py

원본 `Sheep.fbx` 에는 Idle·Jump 뿐이라(M5-04a) 감독이 Death 를 같은 blend 에서 뽑았다(루프#131).
걷기·달리기도 같은 꼴 — 메시+아마추어를 담고 액션 하나만 굽는다. 받는 쪽은
`tools/unreal/import_animal_livestock.py` `ANIM_IMPORTS["Sheep"]`(애니 전용 임포트, 메시는 안 받음).
blend 의 액션 이름(블렌더로 열어 확인): Death 0~26 · Idle 0~150 · Jump 0~27 · Run 0~20 · Walk 0~64 · WalkSlow 0~48.
"""
import bpy

OUT_DIR = "~/UNREAL/Snarehaven/_Source/raw/Quaternius_FarmAnimals/FBX"

# (blend 액션 이름, 내보낼 파일) — 돼지 원본 FBX(`Pig_Anim_Walk`·`Pig_Anim_Run`)와 같은 이름 꼴.
EXPORTS = [
    ("Walk", "Sheep_Anim_Walk.fbx"),
    ("Run", "Sheep_Anim_Run.fbx"),
]

armature = bpy.data.objects["Armature"]
scene = bpy.context.scene

for action_name, file_name in EXPORTS:
    action = bpy.data.actions.get(action_name)
    if action is None:
        raise SystemExit("액션이 없다: {}".format(action_name))
    armature.animation_data.action = action
    start, end = action.frame_range
    scene.frame_start = int(start)
    scene.frame_end = int(end)
    path = "{}/{}".format(OUT_DIR, file_name)
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=False,
        object_types={"ARMATURE", "MESH"},
        add_leaf_bones=False,
        bake_anim=True,
        bake_anim_use_all_actions=False,   # 지금 걸린 액션 하나만 — 다른 액션이 테이크로 섞이지 않게
        bake_anim_use_nla_strips=False,
        bake_anim_force_startend_keying=True,
    )
    print("내보냄: {} ← {} ({}~{})".format(path, action_name, int(start), int(end)))
