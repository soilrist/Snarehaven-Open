#!/bin/bash
# 루프 감시 — launchd 가 15분마다 부른다. 루프가 꺼져 있는데 INBOX 에 루프 몫(→루프 열린 줄)이 남았으면 다시 켠다.
# 사람이 일부러 세울 땐 loop/HOLD 를 만든다 (감시가 건드리지 않음).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 0
LOG="$ROOT/loop/watchdog.log"
[ -f loop/HOLD ] && exit 0
if [ -f loop/.lock ] && kill -0 "$(cat loop/.lock)" 2>/dev/null; then exit 0; fi
n=$(grep -cE '^- \[ \] .*→루프' docs/feedback/INBOX.md 2>/dev/null)
[ "${n:-0}" -gt 0 ] || exit 0
echo "$(date '+%F %T') 루프 꺼짐 + INBOX 루프 몫 ${n}줄 → 다시 켬" >> "$LOG"
bash loop/ctl.sh on >> "$LOG" 2>&1
