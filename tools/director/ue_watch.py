# 에디터 시작 때 `"-ExecCmds=py <이 파일 절대경로>"` 로 태운다(`-ExecutePythonScript` 는 스크립트 뒤 QUIT_EDITOR 를 불러 에디터가 꺼진다). 원격 실행(멀티캐스트)이 안 잡힐 때의 대체 길:
# Saved/DirectorCmd/cmd.py 가 생기면 게임 스레드에서 실행하고 출력을 out.txt 로 남긴 뒤 cmd.py 를 지운다.
import unreal, os, io, contextlib, traceback
D = os.path.join(unreal.Paths.project_saved_dir(), "DirectorCmd"); os.makedirs(D, exist_ok=True)
CMD, OUT = os.path.join(D, "cmd.py"), os.path.join(D, "out.txt")
_acc = [0.0]
def _tick(dt):
    _acc[0] += dt
    if _acc[0] < 0.25 or not os.path.exists(CMD): return
    _acc[0] = 0.0
    src = open(CMD).read(); os.remove(CMD); buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try: exec(compile(src, CMD, "exec"), {"__name__": "__director__"})
        except Exception: traceback.print_exc(file=buf)
    open(OUT + ".tmp", "w").write(buf.getvalue()); os.replace(OUT + ".tmp", OUT)
unreal.register_slate_post_tick_callback(_tick)
unreal.log("감독 명령 감시 시작: " + D)
