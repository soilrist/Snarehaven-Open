#!/bin/bash
# ─────────────────────────────────────────────
# 루프 설정 — 여기만 고치면 다음 바퀴부터 적용 (재시작 필요 없음)
# 사람이 직접 고칠 필요 없음: 감독에게 "30분마다로 바꿔줘" 라고 말하면 됨
# ─────────────────────────────────────────────

# 이 프로젝트 이름 (launchd 라벨·알림에 씀, 영어 소문자·하이픈)
LOOP_SLUG="${LOOP_SLUG:-snarehaven}"
LOOP_LABEL="com.${LOOP_SLUG}.loop"

# 바퀴가 "끝난 뒤" 쉬는 시간(초). 사람 규칙(2026-09-18): 저녁엔 일하지 않는다 —
# 오후 6시 이후는 1시간 간격, 사람이 바꾸라고 말하기 전엔 항상. 저녁 범위(18~09시)는 감독 가정 🟡.
# 낮(09~18시)은 900 — 사람 지시(2026-09-20 13:10) "화요일 01:00 까지 한도를 버텨야 한다".
# 300 이던 어제·오늘은 루프 계정이 5시간 한도에 하루 세 번 걸렸다(logs/usage.csv limit: 표시).
# 바퀴마다 이 파일을 다시 읽으므로 시각에 따라 자동 전환.
# 2026-09-21 14:39 사람 지시: "오늘 1시까지 토큰 다 소진해야 하니까" — 화요일 01:00 주간 한도
# 리셋 전까지는 저녁 규칙(18시 이후 1시간)을 끄고 빠르게 간다. 2026-09-21 21:xx 간격 900→600(10분)
# 으로 더 올림. 2026-09-21 21:xx "1시 풀리면 다시 1시간 간격" — 리셋 시각(01:00) 을 정확한 기준으로
# 써서(자정이 아니라) 넘으면 자동으로 평소 낮/저녁 규칙으로 돌아간다. 사람이 다시 말할 필요 없음.
# 2026-09-25 저녁 사람 지시: "루프 한시간간격으로 돌려 내가다시 말하기전까지" — 낮·저녁 구분 없이 항상 1시간.
# 2026-09-29 사람 지시: "쉬는시간 30분으로 줄이고" — 항상 30분.
# 2026-10-02 사람 지시: 토큰이 남아 "줄여" — 10분.
# 2026-10-19 23:50 사람 지시: 「nc계정 한도 먼저 싹 녹여」 — 쉬는 시간 1분, 오푸스·노력 high 로 nc 를 빨리 쓴다. 다 쓰면 default 로 넘어감.
LOOP_SLEEP_SEC=60

# 한 바퀴 최대 시간(초). 넘으면 끊고 다음 바퀴
LOOP_LAP_TIMEOUT_SEC=2400

# 모델 (2026-10-08 사람 지시 「모델도 키트대로」): 키트 기본 — 소넷 우선, 과부하 시 오푸스 (claude --fallback-model)
# 10-02 「루프 다 오푸스」를 대체. 기준선(오푸스·노력 기본, 20바퀴): 일한 바퀴당 턴 23.1·출력 13,080·추정 $1.12·헛바퀴 0
# 바퀴마다 이 파일을 다시 읽으므로 loop.sh 는 건드리지 않는다.
LOOP_MODEL="claude-opus-5-5"
LOOP_FALLBACK_MODEL="claude-sonnet-5-5"

# 한 바퀴 비용 상한(달러, 비우면 제한 없음) — claude --max-budget-usd
LOOP_MAX_BUDGET_USD=""

# 계정 순서: 한도에 걸리면 다음 계정. "default" = 기본 로그인(개인 Max)
# 팀 계정 폴더가 없으면 자동으로 건너뜀
# 2026-09-20 13:10 감독 결정: default 는 사람·감독이 쓰는 계정이라 루프에서 뺌(화요일 01:00 주간 한도 보호).
# 2026-09-21 14:39 사람 지시로 되돌림: "맥스계정 알아서 돌려" — 화요일 01:00 리셋 전까지 주간 한도를
# 남기지 말고 다 쓴다. default 를 nc 보다 앞에 둬서 먼저 소진하고, 한도 걸리면 nc 로 넘어간다.
# 감독(이 대화)도 같은 default 계정을 쓰므로 루프와 토큰을 나눠 쓴다 — 의도된 트레이드오프.
# 화요일 01:00 지나면 다시 뺄지 판단(예전 결정으로 돌아갈지는 그때 사람 확인).
# 2026-09-25 저녁 사람 지시: "엔씨계정거 가져다써" — 루프는 nc 계정만 쓴다(default 는 사람·감독 몫).
# 2026-09-29 13:20 사람 지시: nc 가 한도일 때 맥스(default)로 이어 돈다 — nc 먼저, 한도면 default.
# 2026-10-03 사람 지시: "맥스계정으로 계속 돌리면 되지" — 루프는 맥스(default)만 쓴다. nc 는 안 쓴다.
# 2026-10-08 01:30 사람 지시: "엔씨계정거 당겨서 써라 / 그거 다 소진하면 다시 맥스써" — nc 먼저, 한도·만료면 default.
# 2026-10-11 사람 지시: 「구독 한도 걸리면만 API로 넘어가게」 — 맨 끝 "api" = API 키 폴백.
# 키는 아래 파일에 사람이 직접 넣는다(한 줄, 저장소 밖). 파일이 없거나 비면 건너뜀.
LOOP_ACCOUNTS="$HOME/.claude-loop-nc default api"
LOOP_API_KEY_FILE="$HOME/.config/snarehaven/anthropic_api_key"
# API 바퀴 한 번 비용 상한(달러) — 구독 바퀴 평균 $1.12(위 기준선)의 약 3배. 감독 결정 2026-10-11
LOOP_API_MAX_BUDGET_USD="3"

# 헛바퀴(커밋 0) 연속 몇 번이면 경고 / 그 두 배면 스스로 멈춤
LOOP_MAX_IDLE=3
LOOP_IDLE_SLEEP_SEC=3600

# 모든 계정이 한도일 때 쉬는 시간(초)
LOOP_LIMIT_SLEEP_SEC=3600

# 헤드리스 권한 — 위험 명령은 .claude/settings.json 의 deny 로 막혀 있음
LOOP_PERMISSION_FLAGS="--dangerously-skip-permissions"

# claude 실행 파일 (launchd 는 PATH 가 짧아서 직접 찾아 줌)
export PATH="$HOME/.local/bin:$HOME/.claude/local:/opt/homebrew/bin:/usr/local/bin:$HOME/.npm-global/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

# 안드로이드 패키징용 JDK — Android Studio JBR(25)는 언리얼 Gradle 8.13과 안 맞음(major version 69 미지원)
# 2026-09-22 감독이 brew install openjdk@17 로 설치, 여기 export 해야 루프(비로그인 bash)가 봄 — ~/.zshrc 는 안 읽음
export JAVA_HOME="/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"
LOOP_CLAUDE_BIN="$(command -v claude || echo claude)"

# 시험용: 1 이면 claude 를 부르지 않고 흉내만 / 최대 바퀴 수(비우면 무한)
LOOP_DRY_RUN="${LOOP_DRY_RUN:-0}"
LOOP_MAX_LOOPS="${LOOP_MAX_LOOPS:-}"

# ── 키트 v2.5.0 새 칸 (2026-10-06) ──
# 노력 단계 (claude --effort): 키트 기본값 — 평소 medium, 직전 바퀴가 일한 커밋 없이 끝났을 때만 high
# 10-08 사람 「상황에 따라 바꿔 쓰라고 키트에 있지 않나」로 켬(같은 날 모델도 키트대로 — 둘을 함께 바꿔 효과는 묶어서 본다). 기준선(켜기 전 20바퀴): 일한 바퀴당 턴 23.1·출력 13,080·추정 $1.12·헛바퀴 0
# 5바퀴 넘게 쌓이면 ctl.sh cost 로 비교, 헛바퀴·STATUS 실패가 늘면 되돌린다 (DIRECTOR 감시 & 검수 5)
LOOP_EFFORT="high"
LOOP_EFFORT_RETRY="xhigh"
# 가벼운 바퀴: 1 이면 사람 전역 설정(개인 훅·전역 스킬·플러그인·전역 MCP)을 바퀴에 싣지 않는다. MCP 는 loop/mcp.json 것만. 끄려면 0
LOOP_LEAN=1
# 가벼운 바퀴에서 일꾼이 쓰는 기본 도구 (비우면 loop.sh 기본 12개)
LOOP_TOOLS=""
# 바퀴 전 점검 명령 (비우면 건너뜀). 0 이 아니면 그 바퀴는 쉰다
LOOP_PREFLIGHT_CMD=""
LOOP_PREFLIGHT_SLEEP_SEC=600

# 한글 로그가 안 깨지게
export LANG="${LANG:-en_US.UTF-8}"
export LC_ALL="${LC_ALL:-en_US.UTF-8}"
