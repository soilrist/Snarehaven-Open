"""바위 충돌 검사 (INBOX 결함 10-13 「돌 안으로 들어가버린다」) — 새 프로세스에서 맵 둘을 열어
(1) 바위 메시를 쓰는 컴포넌트 중 충돌 꺼진 것 0 (2) 바위 메시마다 볼록 충돌 ≥1 (3) 컴포넌트가 폰(Pawn) 채널을 막는 응답인 것 0 아님 — 안 막는 컴포넌트 0 을 단정한다. 실제로 몸이 막히는지(캡슐 쓸기)는 커맨드릿 월드에 물리 장면이 없어 못 보므로 `verify_rock_collision_pie.py` 가 PIE 에서 한다.
실행: fix_rock_collision.py 와 같은 명령줄에서 -script 만 이 파일로. 통과하면 「바위 충돌 검사 통과」 경고 줄, 아니면 예외.
"""
import unreal

MAPS = ["/Game/Game/Maps/L_Ranch", "/Game/Game/Maps/L_Hunt"]
PREFIXES = ("SM_Nature_Boulder", "SM_Nature_Rock")

bad = 0
for path in MAPS:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path)
    open_comps, no_convex, not_pawn = 0, 0, 0
    meshes = set()
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = comp.static_mesh
            if mesh is None or not mesh.get_name().startswith(PREFIXES):
                continue
            meshes.add(mesh)
            if comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                open_comps += 1
            elif comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) != unreal.CollisionResponseType.ECR_BLOCK:
                not_pawn += 1
    for mesh in meshes:
        if len(mesh.get_editor_property("body_setup").get_editor_property("agg_geom").get_editor_property("convex_elems")) < 1:
            no_convex += 1
    unreal.log_warning("바위 충돌 검사: {} — 충돌 꺼진 컴포넌트 {} (0) · 폰을 안 막는 컴포넌트 {} (0) · 볼록 없는 메시 {} (0)".format(
        path.split("/")[-1], open_comps, not_pawn, no_convex))
    bad += open_comps + not_pawn + no_convex
if bad:
    raise RuntimeError("바위 충돌 검사 실패 {}".format(bad))
unreal.log_warning("바위 충돌 검사 통과")
