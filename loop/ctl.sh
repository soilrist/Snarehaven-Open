#!/bin/bash
# ─────────────────────────────────────────────
# 루프 스위치 — 사람은 직접 안 써도 됨. 감독에게 "켜/꺼/상태" 라고 말하면 감독이 실행
#   on      루프 켜기 (launchd 등록, 로그인해도 자동 시작)
#   wrapup  하던 조각 마무리하고 멈추기 (권장)
#   off     다음 바퀴부터 멈추기 (진행 중인 바퀴는 끝까지)
#   kill    지금 즉시 강제 종료 + launchd 해제
#   status  상태 보기
#   cost    최근 20바퀴의 "일한 바퀴 하나당" 턴·토큰·추정비용 (cost 50 처럼 바퀴 수 지정 가능)
#   size    매 바퀴 다시 읽히는 문서(STATUS "지금 상태"·INBOX)가 한도를 넘었는지
#   tidy    STATUS 의 오래된 기록을 docs/archive/ 로 옮김 (tidy --dry 는 미리 보기)
#   try     시험 두 바퀴 (claude 안 부름, 설치 확인용)
# ─────────────────────────────────────────────
set -u
LOOP_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$LOOP_DIR/.." && pwd)"
# shellcheck source=/dev/null
. "$LOOP_DIR/env.sh"
PLIST="$HOME/Library/LaunchAgents/${LOOP_LABEL}.plist"
UID_NUM="$(id -u)"

write_plist() {
  mkdir -p "$HOME/Library/LaunchAgents" "$ROOT/logs"
  cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>${LOOP_LABEL}</string>
  <key>ProgramArguments</key>
  <array><string>/bin/bash</string><string>${LOOP_DIR}/loop.sh</string></array>
  <key>WorkingDirectory</key><string>${ROOT}</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>
  <key>ThrottleInterval</key><integer>60</integer>
  <key>StandardOutPath</key><string>${ROOT}/logs/launchd.out.log</string>
  <key>StandardErrorPath</key><string>${ROOT}/logs/launchd.err.log</string>
  <key>EnvironmentVariables</key>
  <dict><key>PATH</key><string>${PATH}</string><key>LOOP_SLUG</key><string>${LOOP_SLUG}</string></dict>
</dict>
</plist>
PL
}

running_pid() {
  if [ -f "$LOOP_DIR/.lock" ] && kill -0 "$(cat "$LOOP_DIR/.lock")" 2>/dev/null; then cat "$LOOP_DIR/.lock"; fi
}

case "${1:-status}" in
  on)
    if [ -f "$PLIST.disabled" ]; then
      echo "✖ 이 루프는 영구 정지로 표시돼 있음 ($PLIST.disabled). 사람이 명시적으로 풀기 전엔 켜지 않음."
      exit 1
    fi
    cd "$ROOT" || exit 1
    [ "$(git config core.hooksPath)" = ".githooks" ] || { echo "✖ 훅 미설치: git config core.hooksPath .githooks"; exit 1; }
    if [ -n "$(running_pid)" ]; then
      echo "✔ 이미 돌고 있음 (pid $(running_pid)) — STOP/WRAPUP 표시만 지움"
      rm -f "$LOOP_DIR/STOP" "$LOOP_DIR/WRAPUP"
      exit 0
    fi
    # 루프 자신의 상태 파일(loop/·logs/·captures/tmp/ 의 추적 안 되는 파일)은 "커밋 안 된 변경"이 아니다 — 재시작을 스스로 막던 원인
    [ -z "$(git status --porcelain | grep -vE ' loop/env\.sh$|^\?\? (loop/|logs/|captures/tmp/)')" ] || { echo "✖ 커밋 안 된 변경이 있음 — 먼저 커밋"; git status --short; exit 1; }
    # develop 이 아니면 (트리가 깨끗하니) 옮겨 준다. 없으면 지금 브랜치에서 만든다 — "develop 이 아님"으로 켜기를 거부하던 것을 자동화
    if [ "$(git branch --show-current)" != "develop" ]; then
      if git show-ref --verify --quiet refs/heads/develop; then git checkout -q develop || exit 1; echo "✔ develop 로 이동"
      else git checkout -q -b develop || exit 1; echo "✔ develop 브랜치를 만들어 이동"; fi
    fi
    rm -f "$LOOP_DIR/STOP" "$LOOP_DIR/WRAPUP"
    write_plist
    if launchctl print "gui/$UID_NUM/$LOOP_LABEL" >/dev/null 2>&1; then
      launchctl bootout "gui/$UID_NUM/$LOOP_LABEL" 2>/dev/null || true
      for _ in 1 2 3 4 5 6 7 8 9 10; do
        launchctl print "gui/$UID_NUM/$LOOP_LABEL" >/dev/null 2>&1 || break
        sleep 1
      done
    fi
    launchctl bootstrap "gui/$UID_NUM" "$PLIST" && echo "✔ 켜짐: $LOOP_LABEL (쉬는 시간 ${LOOP_SLEEP_SEC}초)"
    ;;
  wrapup)
    touch "$LOOP_DIR/WRAPUP"
    echo "✔ 마무리 요청 — 다음 바퀴가 하던 조각을 끝내고 멈춤"
    ;;
  off)
    touch "$LOOP_DIR/STOP"
    echo "✔ 멈춤 요청 — 진행 중인 바퀴가 끝나면 멈춤"
    ;;
  kill)
    touch "$LOOP_DIR/STOP"
    launchctl bootout "gui/$UID_NUM/$LOOP_LABEL" 2>/dev/null || true
    pid="$(running_pid)"; [ -n "$pid" ] && pkill -TERM -P "$pid" 2>/dev/null; [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null
    echo "✔ 강제 종료 + launchd 해제"
    ;;
  status)
    cd "$ROOT" || exit 1
    pid="$(running_pid)"
    echo "라벨      : $LOOP_LABEL"
    echo "실행 중   : ${pid:-아니오}"
    echo "STOP/WRAPUP: $( [ -f "$LOOP_DIR/STOP" ] && echo STOP ) $( [ -f "$LOOP_DIR/WRAPUP" ] && echo WRAPUP )"
    echo "마지막 바퀴: $(cat logs/lap 2>/dev/null || echo 0)"
    echo "설정      : 모델 $LOOP_MODEL / 노력 ${LOOP_EFFORT:-기본}(막히면 ${LOOP_EFFORT_RETRY:-그대로}) / 쉬는 시간 ${LOOP_SLEEP_SEC}초 / 계정 $LOOP_ACCOUNTS / 가벼운 바퀴 $([ "${LOOP_LEAN:-1}" = "1" ] && echo 켬 || echo 끔)"
    if [ "${LOOP_LEAN:-1}" = "1" ]; then
      echo "일꾼 MCP  : $([ -f "$LOOP_DIR/mcp.json" ] && echo "loop/mcp.json 에 적힌 것만" || echo "없음 — 일꾼은 명령줄로만 일함 (엔진 MCP 를 써야 하면 loop/mcp.json 을 만든다. 루트 .mcp.json·전역 설정은 일꾼에게 안 감)")"
    else
      echo "일꾼 MCP  : 전역·프로젝트 설정 그대로 (가벼운 바퀴 끔)"
    fi
    echo "--- 최근 바퀴"; tail -n 5 logs/usage.csv 2>/dev/null || echo "(없음)"
    echo "--- 경고";     tail -n 5 logs/alerts.log 2>/dev/null || echo "(없음)"
    ;;
  cost)
    cd "$ROOT" || exit 1
    python3 "$LOOP_DIR/lap_audit.py" report logs/usage.csv "${2:-20}"; rc=$?
    python3 "$LOOP_DIR/lap_audit.py" ctxreport logs/context.csv "${2:-20}"
    exit $rc
    ;;
  size)
    cd "$ROOT" || exit 1
    python3 "$LOOP_DIR/lap_audit.py" size .
    ;;
  tidy)
    # STATUS 의 오래된 바퀴·"지금 상태"에서 넘친 항목을 docs/archive/ 로 옮긴다 (버리지 않음). 루프가 도는 중엔 하지 않는다
    cd "$ROOT" || exit 1
    if [ -n "$(running_pid)" ] && [ "${2:-}" != "--dry" ]; then
      echo "✖ 루프가 도는 중 — 먼저 ctl.sh wrapup 으로 멈춘 뒤 (일꾼이 같은 파일을 고치고 있을 수 있음). 미리 보기는 ctl.sh tidy --dry"
      exit 1
    fi
    python3 "$LOOP_DIR/lap_audit.py" tidy . ${2:+"$2"}
    ;;
  try)
    LOOP_DRY_RUN=1 LOOP_MAX_LOOPS=2 LOOP_FORCE_SLEEP_SEC=1 bash "$LOOP_DIR/loop.sh"
    ;;
  *)
    echo "사용: ctl.sh on|wrapup|off|kill|status|cost|size|tidy|try"; exit 1 ;;
esac
