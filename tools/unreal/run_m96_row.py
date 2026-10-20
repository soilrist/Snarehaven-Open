import subprocess, sys, os
# M96 조작 전수 실측 한 행 — 사용: run_m96_row.py <행 이름> <제한 초> <스위치...>
# 로그 `Saved/M96/<행>.log`, 게임 카메라 캡처 `Saved/M96/<행>.png`(`-SnareCapture=`, 맥 -game 이라 렌더 켬). 행당 1번.
# `online` 을 주면 -SnareOffline 을 빼고 PlayFab 로그인 성공 줄이 로그에 뜬 걸 확인한다(실패 줄이 뜨거나 성공 줄이 없으면 실패 줄 출력 후 exit 1).
# 해상도는 기본 1280×720, `-ResX=1334 -ResY=616` 을 스위치로 주면 그 값(폰 가로 비율).
# 맵을 바꾸려면 스위치 앞에 `map=/Game/...` 한 개를 준다(기본 L_Hunt).
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
row, secs = sys.argv[1], int(sys.argv[2])
rest = sys.argv[3:]
mapname = "/Game/Game/Maps/L_Hunt"
if rest and rest[0].startswith("map="):
    mapname, rest = rest[0][4:], rest[1:]
online = "online" in rest  # 온라인 행: 로그인 응답을 기다려야 해서 기본 캡처 지연을 늘린다
rest = [a for a in rest if a != "online"]
if not any(a.startswith("-SnareCaptureDelay=") for a in rest):
    rest = rest + ["-SnareCaptureDelay=%d" % (25 if online else 10)]  # 기본 3초면 누름(-SnareMoveKeyDelay) 전에 닫힌다
guide = "--guide" in rest  # 처음 하는 사람 안내 행: -SnareNoGuide 를 빼고 저장값 Seen=False 로 덮는다
rest = [a for a in rest if a != "--guide"]
story_guide = "--story-guide" in rest  # 스토리 대사 판 행: -SnareNoGuide 는 빼되 처음 안내는 이미 본 것으로(Seen=True) — 도착 게이트 캡처용
rest = [a for a in rest if a != "--story-guide"]
guide = guide or story_guide
out = os.path.join(ROOT, "Saved", "M96")
os.makedirs(out, exist_ok=True)
args = ["~/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor",
        os.path.join(ROOT, "Snarehaven.uproject"), mapname, "-game", "-unattended", "-nosound", "-nosplash", "-RenderOffscreen", ] + ([] if any(a.startswith("-ResX=") for a in rest) else ["-ResX=1280", "-ResY=720"]) + ["-ForceRes",
        ] + ([] if online else ["-SnareOffline"]) + ([] if guide else ["-SnareNoGuide"]) + ["-SnareActionLog"] + rest + ([
        "-ini:GameUserSettings:[Snarehaven.Guide]:Seen=%s" % ("True" if story_guide else "False")] if guide else []) + [
        "-SnareCapture=M96/%s.png" % row, "-log", "-abslog=" + os.path.join(out, row + ".log"),
        "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False",
        "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False"]
logpath = os.path.join(out, row + ".log")
if os.path.exists(logpath):
    os.remove(logpath)  # 옛 로그의 로그인 줄을 새 실행으로 읽지 않게
p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
OK_LINE, FAIL_LINE = "PlayFab 로그인 성공", "PlayFab 로그인 실패"
def log_text():
    try:
        return open(logpath, errors="ignore").read()
    except OSError:
        return ""
login_failed = False
try:
    if online:  # 실패 줄이 뜨면 끝까지 기다리지 않고 닫는다
        import time
        end = time.time() + secs
        while p.poll() is None and time.time() < end:
            if FAIL_LINE in log_text():
                login_failed = True
                break
            time.sleep(1)
        if p.poll() is None and not login_failed:
            raise subprocess.TimeoutExpired(args, secs)
        if login_failed:
            raise subprocess.TimeoutExpired(args, 0)
    else:
        p.wait(timeout=secs)
except subprocess.TimeoutExpired:
    p.terminate()
    try:
        p.wait(timeout=20)
    except subprocess.TimeoutExpired:
        p.kill()
print("exit", p.returncode)
if online:
    txt = log_text()
    if login_failed or FAIL_LINE in txt or OK_LINE not in txt:
        print("온라인 로그인 실패 — " + ("; ".join(l for l in txt.splitlines() if FAIL_LINE in l) or "성공 줄 없음"))
        sys.exit(1)
    print([l for l in txt.splitlines() if OK_LINE in l][0])
