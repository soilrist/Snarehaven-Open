#!/bin/bash
# ─────────────────────────────────────────────
# 루프 일꾼 실행기 — 매 바퀴 새 claude 헤드리스 세션을 띄운다
# launchd 가 이 파일을 계속 켜 둔다 (loop/ctl.sh on)
# macOS 기본 bash 3.2 에서 돌도록 작성
# ─────────────────────────────────────────────
set -u

LOOP_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$LOOP_DIR/.." && pwd)"
cd "$ROOT" || exit 1
mkdir -p logs

# 라우터 별칭 함정 방지: 이 값들이 세팅돼 있으면 claude 가 지정 모델 대신 프록시 별칭으로 가서
# 바퀴가 헛바퀴로 끝난다 (MechaVanguard 2026-09-23 바퀴 #25~#30 실측)
ROUTER_ENV_UNSET=(-u ANTHROPIC_API_KEY -u ANTHROPIC_AUTH_TOKEN -u ANTHROPIC_BASE_URL \
  -u ANTHROPIC_MODEL -u ANTHROPIC_DEFAULT_SONNET_MODEL -u ANTHROPIC_DEFAULT_OPUS_MODEL \
  -u ANTHROPIC_DEFAULT_HAIKU_MODEL)

# 가벼운 바퀴가 사람 설정에서 가져갈 것: 동작을 바꾸는 값(env·autoCompactWindow) + rtk 출력 줄이기 훅.
# 왜: 이걸 빠뜨리면 컨텍스트 상한·자동 요약 기준이 풀려 바퀴가 220K 까지 불어난다 (Snarehaven #160 실측, 입력 2배).
# 계정마다 설정 폴더가 다르므로 그 계정 것을 읽는다 (default = ~/.claude).
lean_settings() {
  python3 - "$1/settings.json" <<'PY'
import json, shutil, sys
try: u = json.load(open(sys.argv[1]))
except Exception: u = {}
s = {k: u[k] for k in ("env", "autoCompactWindow") if k in u}
if shutil.which("rtk"):
    s["hooks"] = {"PreToolUse": [{"matcher": "Bash", "hooks": [{"type": "command", "command": "rtk hook claude"}]}]}
print(json.dumps(s))
PY
}

LOCK="$LOOP_DIR/.lock"
if [ -f "$LOCK" ] && kill -0 "$(cat "$LOCK" 2>/dev/null)" 2>/dev/null; then
  echo "이미 돌고 있음 (pid $(cat "$LOCK"))"
  exit 0
fi
echo $$ > "$LOCK"
trap 'rm -f "$LOCK"' EXIT

log()   { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a logs/loop.log; }
alert() {
  # 사람·감독이 봐야 할 일 — 깃에 안 들어가는 logs/alerts.log 에 쌓고 맥 알림
  echo "- [ ] 막힘(loop.sh, $(date '+%Y-%m-%d %H:%M')) **$1**" >> logs/alerts.log
  log "⚠ $1"
  if command -v osascript >/dev/null 2>&1; then
    osascript -e "display notification \"$1\" with title \"루프 ${LOOP_SLUG:-}\"" >/dev/null 2>&1 || true
  fi
}

# v2.3: 바퀴마다 노력 단계·턴·토큰·추정비용 칸이 붙는다. 옛 머리줄 파일은 이름을 바꿔 보관하고 새로 시작 (칸이 어긋나지 않게)
if [ -f logs/usage.csv ] && ! head -n 1 logs/usage.csv | grep -q ',turns,'; then
  mv logs/usage.csv logs/usage-pre23.csv || exit 1
fi
[ -f logs/usage.csv ] || echo "time,lap,account,model,exit,commits,seconds,note,effort,turns,in_tok,out_tok,cache_read,cost_usd" > logs/usage.csv
# 호출마다 모델이 받은 컨텍스트 크기 (가벼운 바퀴가 실제로 줄였는지 재는 숫자)
[ -f logs/context.csv ] || echo "time,lap,first,peak,calls,input_total,output_total" > logs/context.csv
LAP_FILE="logs/lap"
[ "${LOOP_DRY_RUN:-0}" = "1" ] && LAP_FILE="logs/lap.dry"   # 시험 바퀴는 번호를 따로 셈
[ -f "$LAP_FILE" ] || echo 0 > "$LAP_FILE"

# 시간 제한 실행 (macOS 에는 timeout 명령이 없음)
# 감시자는 5초마다 살아 있는지만 보다가 claude 가 끝나면 스스로 끝남 (sleep 이 새지 않음)
run_with_timeout() {
  local secs="$1"; shift
  "$@" &
  local pid=$!
  (
    waited=0
    while kill -0 "$pid" 2>/dev/null; do
      sleep 5; waited=$((waited + 5))
      if [ "$waited" -ge "$secs" ]; then
        pkill -TERM -P "$pid" 2>/dev/null; kill -TERM "$pid" 2>/dev/null
        sleep 15
        pkill -KILL -P "$pid" 2>/dev/null; kill -KILL "$pid" 2>/dev/null
        break
      fi
    done
  ) >/dev/null 2>&1 &
  local watcher=$!
  wait "$pid"
  local rc=$?
  wait "$watcher" 2>/dev/null
  return $rc
}

# 쉬는 동안에도 STOP/WRAPUP 을 30초마다 확인
nap() {
  local left="$1"
  while [ "$left" -gt 0 ]; do
    if [ -f "$LOOP_DIR/STOP" ] || [ -f "$LOOP_DIR/WRAPUP" ]; then return 0; fi
    if [ "$left" -gt 30 ]; then sleep 30; left=$((left - 30)); else sleep "$left"; left=0; fi
  done
}

# 계정 한도 판정 — 문구는 자주 바뀌니 넓게 잡고, 최종 판단은 "커밋이 0개였나"와 합쳐서 한다 (아래)
# 로그인 만료도 같이 본다 — 그 계정은 다시 로그인할 때까지 못 쓰니 다음 계정으로 넘긴다 (Snarehaven 10-03: 만료된 계정으로 바퀴가 5초씩 헛돌았다)
LIMIT_RE='Failed to authenticate|OAuth session expired|OAuth token has expired|run /login|usage.?limit|session limit|hit your|rate.?limit|limit.?reached|hit your limit|weekly limit|quota|credit balance|insufficient credit|out of credits|too many requests|overloaded|resets? at|billing|invalid.?api.?key|\b429\b|\b529\b'

idle=0
count=0
pre_fail=0

while :; do
  # 설정은 매 바퀴 다시 읽는다 (재시작 없이 변경 반영)
  # shellcheck source=/dev/null
  . "$LOOP_DIR/env.sh"
  [ -n "${LOOP_FORCE_SLEEP_SEC:-}" ] && LOOP_SLEEP_SEC="$LOOP_FORCE_SLEEP_SEC"

  if [ -f "$LOOP_DIR/STOP" ]; then
    rm -f "$LOOP_DIR/WRAPUP"
    log "STOP 파일 → 종료"
    exit 0
  fi
  # 매 바퀴가 커밋된 상태로 끝나므로, 바퀴 사이의 WRAPUP 은 곧 STOP
  if [ -f "$LOOP_DIR/WRAPUP" ]; then
    rm -f "$LOOP_DIR/WRAPUP"; touch "$LOOP_DIR/STOP"
    log "WRAPUP → 바퀴 사이라 바로 멈춤"
    exit 0
  fi

  branch="$(git branch --show-current 2>/dev/null)"
  if [ "$branch" != "develop" ]; then
    if [ "$LOOP_DRY_RUN" = "1" ]; then
      log "(시험) 브랜치가 develop 이 아님($branch) — 실제로는 여기서 멈춤"
    else
      alert "브랜치가 develop 이 아님($branch) — 루프 멈춤"
      touch "$LOOP_DIR/STOP"
      exit 0
    fi
  fi

  # 바퀴 전 점검: 엔진·에디터·MCP 가 죽어 있으면 claude 를 부르지 않는다 (LOOP_PREFLIGHT_CMD, 비우면 건너뜀)
  # 왜: 에디터가 죽은 채로 바퀴를 돌면 검사가 전부 "못 함" 인 바퀴에 사용량만 쓴다
  if [ "$LOOP_DRY_RUN" != "1" ] && [ -n "${LOOP_PREFLIGHT_CMD:-}" ]; then
    if run_with_timeout "${LOOP_PREFLIGHT_TIMEOUT_SEC:-120}" bash -c "$LOOP_PREFLIGHT_CMD" > logs/preflight.log 2>&1; then
      pre_fail=0
    else
      pre_fail=$((pre_fail + 1))
      # 처음과 그 뒤 6번째마다만 알린다 (같은 경고로 alerts.log 를 채우지 않게)
      if [ "$pre_fail" -eq 1 ] || [ $((pre_fail % 6)) -eq 0 ]; then
        alert "바퀴 전 점검 실패 ${pre_fail}번째 — 바퀴를 돌지 않고 쉼: $(tail -n 1 logs/preflight.log | cut -c1-120)"
      fi
      count=$((count + 1))
      if [ -n "${LOOP_MAX_LOOPS:-}" ] && [ "$count" -ge "$LOOP_MAX_LOOPS" ]; then
        log "최대 바퀴 수($LOOP_MAX_LOOPS) 도달 → 종료"
        exit 0
      fi
      nap "${LOOP_PREFLIGHT_SLEEP_SEC:-600}"
      continue
    fi
  fi

  lap=$(( $(cat "$LAP_FILE") + 1 ))
  echo "$lap" > "$LAP_FILE"
  export LOOP_LAP="$lap"
  pre="$(git rev-parse HEAD)"
  started=$(date +%s)
  out="logs/lap-$(printf '%05d' "$lap").log"
  raw="logs/lap-$(printf '%05d' "$lap").jsonl"   # 도구 호출·결과까지 전부 (감사용) — .log 는 사람이 읽는 요약
  ln -sf "$(basename "$out")" logs/lap-latest.log   # 고정 경로 — 감독·검사 스크립트가 파일 이름을 추측하지 않게

  prompt="너는 루프 일꾼이다. 바퀴 번호는 ${lap} 이다. loop/PROMPT.md 를 읽고 그 순서대로 이번 바퀴 하나만 수행하고 끝내라. 커밋 꼬리말 Loop: ${lap} 을 잊지 마라."

  # 노력 단계: 평소엔 LOOP_EFFORT, 직전 바퀴가 일한 커밋 없이 끝났으면(실패·헛바퀴) LOOP_EFFORT_RETRY 로 한 단계 올린다
  # 왜: 높은 단계는 토큰을 더 쓰지만 재시도 한 번을 줄이면 본전 — 그래서 막혔을 때만 올린다
  effort="${LOOP_EFFORT:-}"
  [ "$idle" -ge 1 ] && [ -n "${LOOP_EFFORT_RETRY:-}" ] && effort="$LOOP_EFFORT_RETRY"

  set -- -p "$prompt" --model "$LOOP_MODEL" --output-format stream-json --verbose
  [ -n "$effort" ] && set -- "$@" --effort "$effort"
  [ -n "${LOOP_FALLBACK_MODEL:-}" ] && set -- "$@" --fallback-model "$LOOP_FALLBACK_MODEL"
  [ -n "${LOOP_MAX_BUDGET_USD:-}" ] && set -- "$@" --max-budget-usd "$LOOP_MAX_BUDGET_USD"
  [ -f "$LOOP_DIR/mcp.json" ] && set -- "$@" --mcp-config "$LOOP_DIR/mcp.json"
  # 가벼운 바퀴 (LOOP_LEAN=1, 기본): 사람 전역 설정(개인 훅·전역 스킬·플러그인·전역 MCP)을 바퀴에 올리지 않는다.
  # 프로젝트 .claude/ 설정·스킬·CLAUDE.md 는 그대로. MCP 는 loop/mcp.json 에 적은 것만.
  # 왜: 바퀴 시작 컨텍스트 38~48K 중 절반이 루프가 안 쓰는 것이었다 → 모든 호출마다 다시 실림 (Snarehaven 실측).
  # 그래서 일꾼이 써야 하는 스킬은 전역(~/.claude/skills)이 아니라 프로젝트 .claude/skills/ 에 둔다.
  if [ "${LOOP_LEAN:-1}" = "1" ]; then
    set -- "$@" --setting-sources project,local --strict-mcp-config \
      --tools "${LOOP_TOOLS:-Bash,Read,Edit,Write,Glob,Grep,Skill,ToolSearch,Monitor,TaskStop,WebSearch,WebFetch}"
  fi
  # shellcheck disable=SC2086
  set -- "$@" $LOOP_PERMISSION_FLAGS

  used_account="-"
  rc=0
  note=""
  all_limited=1
  log "바퀴 #$lap 시작 (모델 $LOOP_MODEL, 노력 ${effort:-기본})"

  for acct in $LOOP_ACCOUNTS; do
    if [ "$acct" = "api" ]; then
      # API 폴백: 키 파일이 있을 때만. 키는 명령줄(ps)에 안 보이게 export 로만 넘긴다
      [ -s "${LOOP_API_KEY_FILE:-}" ] || continue
    elif [ "$acct" != "default" ] && [ ! -d "$acct" ]; then
      continue
    fi
    used_account="$(basename "$acct")"
    if [ "$LOOP_DRY_RUN" = "1" ]; then
      used_account="dry"
      echo "[DRY] $LOOP_CLAUDE_BIN $*" > "$out"
      rc=0
    elif [ "$acct" = "api" ]; then
      log "구독 계정 모두 한도 → API 키 폴백 (상한 \$${LOOP_API_MAX_BUDGET_USD:-없음})"
      budget=(); [ -z "${LOOP_MAX_BUDGET_USD:-}" ] && [ -n "${LOOP_API_MAX_BUDGET_USD:-}" ] && budget=(--max-budget-usd "$LOOP_API_MAX_BUDGET_USD")
      lean=(); [ "${LOOP_LEAN:-1}" = "1" ] && lean=(--settings "$(lean_settings "$HOME/.claude")")
      ( export ANTHROPIC_API_KEY="$(tr -d '[:space:]' < "$LOOP_API_KEY_FILE")"
        # 키를 쓰는 바퀴는 OAuth 로그인 폴더를 빈 폴더로 돌려 구독 대신 키로만 붙게 한다
        export CLAUDE_CONFIG_DIR="$LOOP_DIR/tmp/api-config"; mkdir -p "$CLAUDE_CONFIG_DIR"
        run_with_timeout "$LOOP_LAP_TIMEOUT_SEC" env -u ANTHROPIC_AUTH_TOKEN -u ANTHROPIC_BASE_URL \
          -u ANTHROPIC_MODEL -u ANTHROPIC_DEFAULT_SONNET_MODEL -u ANTHROPIC_DEFAULT_OPUS_MODEL -u ANTHROPIC_DEFAULT_HAIKU_MODEL \
          "$LOOP_CLAUDE_BIN" "$@" ${budget[@]+"${budget[@]}"} ${lean[@]+"${lean[@]}"} > "$raw" 2> "$out" )
      rc=$?
    elif [ "$acct" = "default" ]; then
      lean=(); [ "${LOOP_LEAN:-1}" = "1" ] && lean=(--settings "$(lean_settings "$HOME/.claude")")
      run_with_timeout "$LOOP_LAP_TIMEOUT_SEC" env -u CLAUDE_CONFIG_DIR "${ROUTER_ENV_UNSET[@]}" "$LOOP_CLAUDE_BIN" "$@" ${lean[@]+"${lean[@]}"} > "$raw" 2> "$out"
      rc=$?
    else
      lean=(); [ "${LOOP_LEAN:-1}" = "1" ] && lean=(--settings "$(lean_settings "$acct")")
      run_with_timeout "$LOOP_LAP_TIMEOUT_SEC" env "${ROUTER_ENV_UNSET[@]}" CLAUDE_CONFIG_DIR="$acct" "$LOOP_CLAUDE_BIN" "$@" ${lean[@]+"${lean[@]}"} > "$raw" 2> "$out"
      rc=$?
    fi
    # 사람이 읽을 요약(.log) = stderr + 최종 답 + 실행한 명령 목록. 한도 문구 판정도 이 파일로.
    [ "$LOOP_DRY_RUN" = "1" ] || python3 "$LOOP_DIR/lap_audit.py" render "$raw" >> "$out" 2>&1

    # 한도 = 한도 문구가 있고 + 실제로 커밋을 하나도 못 남겼을 때 (종료 코드만 믿지 않는다 — 0 으로 끝나며 한도 문구만 남긴 경우가 있었다)
    if [ "$(git rev-list --count "$pre..HEAD" 2>/dev/null || echo 0)" = "0" ] && grep -Eiq "$LIMIT_RE" "$out"; then
      log "계정 $used_account 한도 → 다음 계정 (rc=$rc)"
      note="limit:$used_account"
      continue
    fi
    all_limited=0
    break
  done

  secs=$(( $(date +%s) - started ))
  commits="$(git rev-list --count "$pre..HEAD" 2>/dev/null || echo 0)"
  # 진짜 일한 커밋 = 기록(docs(status)) 말고 다른 커밋
  work="$(git log --format=%s "$pre..HEAD" 2>/dev/null | grep -vc '^docs(status)' || true)"
  work="${work:-0}"
  [ $rc -eq 143 ] || [ $rc -eq 137 ] && note="${note} timeout"
  # 턴·토큰·추정비용 (기록의 마지막 result 줄에서). 시험 바퀴·기록 없음이면 빈 칸
  spent=",,,,"
  if [ "$LOOP_DRY_RUN" != "1" ] && [ -f "$raw" ]; then
    spent="$(python3 "$LOOP_DIR/lap_audit.py" usage "$raw" 2>/dev/null)"
    [ -n "$spent" ] || spent=",,,,"
  fi
  echo "$(date '+%Y-%m-%d %H:%M:%S'),$lap,$used_account,$LOOP_MODEL,$rc,$commits/$work,$secs,$note,$effort,$spent" >> logs/usage.csv
  [ "$LOOP_DRY_RUN" = "1" ] || echo "$(date '+%Y-%m-%d %H:%M:%S'),$lap,$(python3 "$LOOP_DIR/lap_audit.py" context "$raw" 2>/dev/null || echo 0,0,0,0,0)" >> logs/context.csv
  log "바퀴 #$lap 끝 — 종료코드 $rc, 커밋 $commits(일 $work), ${secs}초 ${note}"
  # 옛 claude 가 --effort 를 모르면 매 바퀴가 시작도 못 하고 끝난다 — 원인을 바로 알려 준다
  if [ "$LOOP_DRY_RUN" != "1" ] && grep -q "unknown option '--effort'" "$out" 2>/dev/null; then
    alert "claude 가 --effort 옵션을 모름 — claude 를 업데이트하거나 loop/env.sh 의 LOOP_EFFORT·LOOP_EFFORT_RETRY 를 비울 것"
  elif [ "$LOOP_DRY_RUN" != "1" ] && grep -q "unknown option" "$out" 2>/dev/null; then
    alert "claude 가 옵션을 모름: $(grep -o "unknown option '[^']*'" "$out" | head -n 1) — claude 를 업데이트할 것 (가벼운 바퀴 옵션이면 loop/env.sh 의 LOOP_LEAN=0 으로 임시 회피)"
  fi

  # STATUS 에 적힌 "통과(`인용`)" 이 실제 도구 출력에 있었는지 기계로 대조 (Verification.md 1 — jev 가 UNAVAILABLE 이어도 이건 돈다)
  if [ "$LOOP_DRY_RUN" != "1" ] && [ "$commits" != "0" ]; then
    audit="$(python3 "$LOOP_DIR/lap_audit.py" check "$raw" "$pre" 2>&1)" || alert "바퀴 #$lap 검사 인용 불일치 — $audit"
    log "감사: $audit"
  fi

  # 매 바퀴 다시 읽히는 문서(STATUS "지금 상태"·INBOX)가 불었는지 — 하루에 한 번만 알린다
  if [ "$LOOP_DRY_RUN" != "1" ]; then
    today="$(date '+%Y-%m-%d')"
    if [ "$(cat logs/size_alert_day 2>/dev/null)" != "$today" ]; then
      grown="$(python3 "$LOOP_DIR/lap_audit.py" size "$ROOT" 2>&1)" || { alert "문서가 불어 매 바퀴 읽는 값이 커짐 — $grown"; echo "$today" > logs/size_alert_day; }
    fi
  fi

  # 작업 트리에 커밋 안 된 루프 흔적이 남았는지
  # 제외: loop/env.sh(감독 설정) · loop/ logs/ captures/tmp/ 의 추적 안 되는 파일(루프 자신의 상태 파일)
  # 남은 파일 목록을 alerts.log 에 같이 적는다 — "매 바퀴 경고만 뜨고 원인을 모르는" 일이 없게
  dirty="$(git status --porcelain 2>/dev/null | grep -vE ' loop/env\.sh$|^\?\? (loop/|logs/|captures/tmp/)')"
  if [ "$LOOP_DRY_RUN" != "1" ] && [ -n "$dirty" ]; then
    alert "바퀴 #$lap 뒤에 커밋 안 된 변경이 남음 — 감독 확인 필요"
    printf '%s\n' "$dirty" | sed 's/^/    /' >> logs/alerts.log
  fi

  count=$((count + 1))
  if [ -n "${LOOP_MAX_LOOPS:-}" ] && [ "$count" -ge "$LOOP_MAX_LOOPS" ]; then
    log "최대 바퀴 수($LOOP_MAX_LOOPS) 도달 → 종료"
    exit 0
  fi

  if [ "$LOOP_DRY_RUN" != "1" ] && [ "$all_limited" = "1" ]; then
    alert "모든 계정이 한도 — ${LOOP_LIMIT_SLEEP_SEC}초 쉼"
    nap "$LOOP_LIMIT_SLEEP_SEC"
    continue
  fi

  if [ "$work" = "0" ] && [ "$LOOP_DRY_RUN" != "1" ]; then
    idle=$((idle + 1))
  else
    idle=0
  fi

  if [ "$idle" -ge $((LOOP_MAX_IDLE * 2)) ]; then
    alert "헛바퀴 ${idle}번 연속 — 스스로 멈춤. logs/lap-*.log 확인 필요"
    touch "$LOOP_DIR/STOP"
    exit 0
  elif [ "$idle" -ge "$LOOP_MAX_IDLE" ]; then
    alert "헛바퀴 ${idle}번 연속 — 쉬는 시간을 ${LOOP_IDLE_SLEEP_SEC}초로 늘림"
    nap "$LOOP_IDLE_SLEEP_SEC"
    continue
  fi

  nap "$LOOP_SLEEP_SEC"
done
