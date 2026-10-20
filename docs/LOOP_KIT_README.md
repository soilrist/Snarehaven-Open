# 루프 스타터 키트 v2.5.0 (확정)

**"이거 만들어!" 한마디면 알아서 만들고, 정말 필요할 때만 물어보는 AI 개발팀**을 게임 프로젝트에 심는 키트예요.

## 비유로 보면
- **감독 = 형의 비서.** 형은 클로드 데스크탑 Code 탭에서 이 비서하고만 대화해요.
- **루프 일꾼 = 밤새 일하는 직원.** 30분마다 새로 출근해서 일 한 조각을 하고 기록을 남겨요.
- **규칙 문서 = 회사 규정집.** 둘 다 같은 규정집을 봐요.
- **검사기와 훅 = 문 앞 검문소.** 규칙을 어긴 커밋은 아예 못 들어가요.
- **신호등 = 비서의 판단 기준.**
  - 🟢 알아서 해요.
  - 🟡 일단 하고 "이렇게 했어요"라고 알려요.
  - 🔴 방향, 돈, 삭제, 권리, 규칙, 사람만 할 수 있는 일(계정·결제)은 꼭 물어봐요. 숫자는 안 물어요 — 근거를 적고 알아서 정해요.

## v2.1 에서 바뀐 것
| 문제였던 것 | 이제 |
|---|---|
| 숫자마다 물어봐서 루프가 멈춤 | 루프가 레퍼런스·장르 관례로 정하고 `DESIGN.md` §6 에 근거를 적음. 형은 표만 보고 고치면 됨 |
| 결과물이 조잡·허접 | **레퍼런스 게임 하나를 완벽 복제한다는 자세** (`Reference.md`). 캐릭터·무기·퀘스트·가챠… 레퍼런스에 있는 종류는 전부 로드맵에. UI·캐릭터는 시트 먼저, 캡처를 레퍼런스와 나란히 대조 |
| 실행 안 하고 "통과"라고 거짓말 | "통과"는 실제 명령 출력을 STATUS 에 인용했을 때만. 감독이 로그와 대조. 이름·문자열 패턴으로 판단 금지 |
| 설계에 없는 걸 조용히 추가 | 설계서에 없는 것은 항상 🟡 `확인` |
| 계정 콘솔처럼 사람만 할 수 있는 일이 묻힘 | `[!] 사람:` 표시 → 감독 보고 **맨 앞** |
| "커밋 안 된 변경" 경고가 매 바퀴 | 루프 자신의 상태 파일은 제외, 남은 파일 목록을 `alerts.log` 에 같이 기록 |
| (v2.1.1) 일꾼이 자기 결과를 자기가 채점 | **Jev(TypeSafe) 제3 판정기** `tools/jev.py`: 통과 인용은 `evidence` 가 SUPPORTED 일 때만, 엔진 `Error:` 줄은 `noise` 로 거르고, 결함 지적은 `verdict` CONFIRMED 만 INBOX 로 |

## v2.2 에서 바뀐 것
| 문제였던 것 | 이제 |
|---|---|
| 바퀴 로그에 최종 답만 남아 "명령이 정말 돌았나" 대조 불가 | `loop.sh` 가 도구 호출·출력을 `logs/lap-<n>.jsonl` 로 남기고, 바퀴 끝에 `loop/lap_audit.py check` 가 STATUS 의 모든 `통과(\`인용\`)` 을 실제 출력과 대조 — 지어낸 인용은 `alerts.log` "검사 인용 불일치" |
| Jev 가 역검증("일부러 깨서 잡혔다") 인용을 항상 NOT_SUPPORTED | `tools/jev.py evidence "<주장>" "<인용>" fail` 세 번째 인자 (Snarehaven 실측 버그) |
| graphify 규칙이 없는 폴더(`graphify/`)를 가리킴 | `graphify-out/graph.json` 기준으로 통일, 설치 규칙 7-1 에서 그래프 생성, 감독이 마일스톤 끝에 `--update` |
| `develop` 이 아니면 `ctl.sh on` 이 거부 | 작업 트리가 깨끗하면 `develop` 로 옮기거나 만들어 줌 |
| 레퍼런스 게임을 모르면 kickoff 가 되물음 | `[!] 사람:` 로드맵 항목 + 장르 관례로 채우고 `〔가정: 레퍼런스 미확인〕` 표시, 다시 묻지 않음 |
| 스킬이 규칙 원문을 복사해 컨벤션 문서와 어긋남 | 스킬은 절차만, 규칙은 문서 포인터 (`kickoff`·`new-content`) |
| 키트가 AI 행동을 정말 바꾸는지 잴 방법 없음 | `evals/` 7 케이스 — `claude plugin eval` 로 with/without 비교 (아래 "키트 자체 검사") |

## v2.3 에서 바뀐 것
| 문제였던 것 | 이제 |
|---|---|
| 바퀴가 얼마를 쓰는지 "시간"만 남아, 설정을 바꿔도 나아졌는지 알 수 없음 | `logs/usage.csv` 에 바퀴마다 **노력 단계·턴 수·토큰·추정비용** 칸 추가. `loop/ctl.sh cost` 가 "일한 바퀴 하나당" 평균과 헛바퀴가 쓴 몫을 보여 줌 (형은 감독에게 "얼마나 썼어?") |
| 모델만 고르고 "힘을 얼마나 쓸지"는 못 정함 | `env.sh` 에 `LOOP_EFFORT`(평소 `medium`) · `LOOP_EFFORT_RETRY`(직전 바퀴가 일한 커밋 없이 끝났을 때만 `high`) — claude `--effort` |
| 설정을 감으로 바꿈 | 감독 규칙: 한 번에 하나만 바꾸고 `ctl.sh cost` 로 전후 비교, 안 나아지면 되돌림 |
| 감독이 일꾼의 자기평가를 먼저 읽고 검수 | diff·캡처를 먼저 보고 판단을 정한 뒤에 STATUS 를 읽음 (만든 눈 ≠ 검사하는 눈) |
| 일꾼이 파일을 하나씩 읽어 턴이 늘어남 | PROMPT 규칙 3: 서로 기다릴 필요 없는 읽기·검색은 한 번에 묶고, 읽은 파일은 다시 안 읽음 |
| 프로젝트 고유의 버릇까지 규칙으로 쌓여 문서가 길어짐 | `CLAUDE.md` "함정" 절 (20줄 이내) — 규칙 대신 "하면 깨지는 것" 한 줄 |
| 키트 규칙을 고칠 때 기준이 "with > without" 한 줄뿐 | "키트 자체 검사"에 고치는 순서 추가: 한 번에 한 문서, 전후 점수 비교, 시험 문제의 정답 문장을 규칙에 베끼지 않기 |
| 옛 claude 가 새 옵션을 몰라 바퀴가 조용히 헛돎 | `--effort` 를 모르면 `alerts.log` 에 원인과 고치는 법을 바로 올림. 옛 `usage.csv` 는 `usage-pre23.csv` 로 보관 |

근거: Anthropic 개발자 글(claude.dev) *Spending your effort* · *What a task costs on Opus 5.5* · *Automating eval design and hillclimbing* · *A harness for every task* · *The new rules of context engineering*.

## v2.4 에서 바뀐 것 — 실제 프로젝트 기록에서 찾은 약점
세 프로젝트의 STATUS·INBOX·사고기록(바퀴 약 600개)을 읽고, 실제로 터진 것만 고쳤다.
| 기록에 있던 일 | 이제 |
|---|---|
| STATUS "지금 상태"가 바퀴마다 한 문단씩 불어 300줄·13만 글자 — 일꾼이 매 바퀴 그걸 읽고 시작 | "지금 상태"는 15줄 현황판. 25줄을 넘으면 **루프 커밋 훅이 거절**. `loop/ctl.sh tidy` 가 넘친 항목과 오래된 바퀴를 `docs/archive/` 로 옮김 (한 줄도 안 버림) |
| 안 닫힌 `확인` 69개가 INBOX 에 2주 넘게 쌓임 — 일꾼이 INBOX 를 통째로 읽음 | 일꾼은 **자기 몫 줄만** grep 으로 읽음. `확인` 은 사람이 아니라 **감독이 검산해서 닫음**. 12,000자·10개를 넘으면 `ctl.sh size`·`alerts.log` 가 알림 |
| "항상 이렇게 해" 같은 상시 규칙이 INBOX 에 영구히 남음 | 상시 규칙은 컨벤션 문서나 `CLAUDE.md` 로 옮기고 INBOX 에서 지움 |
| 눈 검사(3층)가 사실상 사라짐 — 최근 100바퀴 중 통과 7번, 나머지는 "못 함(감독 몫)" 이거나 "캡처는 봤는데 판정기가 인정 안 함" | 3층은 Jev 에 묻지 않음. **캡처를 실제로 열었는지를 loop.sh 가 도구 기록으로 대조** — 안 열고 "3층 통과"면 경고. 일꾼이 넘긴 칸은 감독이 직접 봄 |
| 사람이 먼저 발견: 도구가 너무 작다 · 얼굴 대신 정수리만 보인다 · 도구 없으면 아무 안내 없이 "하나도 안 됨" · 폰에서 닿는 버튼이 없다 · 뒤로 가기로 창이 안 닫힌다 · 건물 뒤에 갇힌다 — 전부 시험은 통과 | 새 규칙 문서 **`PlayerEye.md`**: 처음 하는 사람 · 목표 기기의 손 · 게임 카메라에서 보이나 · 장르 상식 · 갇힘 없음. STATUS 에 `플레이어 눈:` 줄, 마일스톤마다 `처음 하는 사람 통 플레이` 조각 |
| "원본 자세랑 맞추라니까 몇 번 설명하냐" — 시트에 숫자가 없어 매번 "맞췄다"로 통과 | 레퍼런스 시트에 **게임 카메라에서의 크기·자세를 숫자로** (캐릭터 키 대비 %, 동작 시간). 대조는 게임 카메라 캡처로만 |
| 사람이 한 말을 감독이 기록에서 빠뜨려 같은 말을 다시 하게 함 | 사람 말은 **그 응답 안에서 따옴표 그대로** INBOX + 커밋, 응답 끝에 "받은 말 n개 → INBOX n줄" 대조. 같은 말 두 번째면 사고기록 |
| 에디터가 죽은 채로 바퀴가 돌아 검사 전부 "못 함" | `env.sh` `LOOP_PREFLIGHT_CMD`: 바퀴 전에 싼 명령 하나로 확인, 실패하면 claude 를 부르지 않고 쉼 |
| 수백 조각이 쌓인 뒤에야 "기본 조작이 안 된다" | 마일스톤 끝 `[!] 사람: 🎮 5분 해 보기`. **사람이 안 해 본 마일스톤이 2개 쌓이면 감독이 루프를 멈추고** 요청 (사람이 "그냥 계속"이라 하면 다시 켬) |

**이미 돌던 프로젝트를 v2.4 이상으로 올릴 때** (감독이 순서대로): ① `loop/ctl.sh wrapup` 으로 멈춤 ② 키트 파일 덮어쓰기(설치 규칙 2 — `loop/env.sh` 는 기존 값을 지키고 새 줄 `LOOP_EFFORT`·`LOOP_EFFORT_RETRY`·`LOOP_LEAN`·`LOOP_TOOLS`·`LOOP_PREFLIGHT_CMD` 만 옮김. **덮어쓰기 전에 프로젝트의 `loop/loop.sh`·`loop/lap_audit.py` 를 키트와 diff 해서, 프로젝트에서만 고친 줄이 있으면 먼저 키트에 올린다**) ③ `loop/ctl.sh size` → `loop/ctl.sh tidy` → `docs(status): 오래된 기록 보관` 커밋 ④ INBOX 의 `확인` 을 전부 검산해 닫고, 상시 규칙 줄을 `CLAUDE.md` 로 옮김 ⑤ `LOOP_PREFLIGHT_CMD` 를 이 프로젝트에 맞게 채움 ⑥ 다시 켬. ③을 건너뛰면 일꾼의 첫 STATUS 커밋이 훅에 거절된다 (거절 메시지가 `tidy` 를 안내한다).

## v2.5 에서 바뀐 것 — 프로젝트에만 있던 것을 키트로 되돌려 넣음
Snarehaven 에서 쓰던 키트(그쪽 이름으로 v2.3.0·v2.3.1, 2026-09-23)가 이 저장소에 올라오지 않아, 저장소의 v2.3·v2.4 에는 그 내용이 빠져 있었다. v2.4 를 그대로 덮어썼다면 가벼운 바퀴가 사라졌을 것이다. **v2.5 = 저장소 v2.4 + 프로젝트의 v2.3.1 + 영상 레퍼런스.**
| 빠져 있던 것 | 이제 |
|---|---|
| 바퀴가 사람 전역 설정(개인 훅·전역 스킬 20여 개·플러그인·전역 MCP)까지 싣고 시작 — 루프가 안 쓰는 것이 모든 호출마다 다시 실림 | **가벼운 바퀴** `LOOP_LEAN=1`(기본): `--setting-sources project,local --strict-mcp-config --tools <루프가 쓰는 12개>`. 프로젝트 `.claude/`·스킬·`CLAUDE.md` 는 그대로, 엔진 MCP 는 `loop/mcp.json` 에 적은 것만. `rtk` 가 있으면 출력 줄이기 훅만 다시 건다 |
| 가벼운 바퀴가 사람 설정의 컨텍스트 상한·자동 요약 기준까지 끊어 바퀴가 220K 로 불어남 (Snarehaven #160) | 계정별 `settings.json` 의 `env`·`autoCompactWindow` 만 바퀴로 옮긴다 (`lean_settings`) |
| 줄었는지 잴 숫자가 없음 | 바퀴마다 `logs/context.csv` 에 첫 호출·최대·호출 수·입력 합·출력 합. `loop/ctl.sh cost` 끝에 "바퀴 시작·최대 컨텍스트 평균" 한 줄 |
| 셸에 프록시 변수(`ANTHROPIC_BASE_URL` 등)가 있으면 바퀴가 엉뚱한 모델로 가 헛바퀴 (MechaVanguard) | `loop.sh` 가 실행 직전에 그 변수 7개를 지운다 |
| 로그인이 만료된 계정으로 바퀴가 5초씩 헛돎 (Snarehaven 10-03) | 한도 판정에 로그인 만료 문구 추가 → 다음 계정으로 넘어감 |
| `확인` 줄에 STATUS 내용을 통째로 다시 적음 | `InboxFormat.md` 12: 2문장 이내 + 근거 위치 하나 |
| 움직임(걷기·도구 쓰기)을 정지 캡처로만 대조 — "영상 보고 참고해"가 사람 지시로만 있었음 | `Reference.md` 2-1 **동작 관찰표**: 감독이 레퍼런스 영상 프레임을 잘라 직접 보고 `영상 ID + 시각`·주기·자세를 표로. 못 본 칸은 `확인 못 함`. 전 품목 한 번에 |
| 스킬이 코드(`new-script`)·데이터(`new-content`)뿐 — 정작 로드맵 조각의 절반 이상인 화면·모델·아이콘·동작에는 작업 카드가 없어, 일꾼이 규칙 문서 네 개를 알아서 찾아 읽어야 했음 | 스킬 **`new-visual`**(시트 → 만들기 → 게임 카메라 캡처 열어 대조 → 플레이어 눈) · **`reference-sheet`**(시트·동작 관찰표 만드는 순서와 프레임 자르는 명령). 규칙은 그대로 `Reference.md`·`PlayerEye.md` 에 있고 스킬은 순서만 |
| 옛 claude 가 `--effort` 말고 다른 새 옵션을 몰라도 조용히 헛돎 | 모르는 옵션이면 어느 옵션인지 `alerts.log` 에 올림 |
| 프로젝트에서 고친 키트 파일이 저장소로 안 돌아옴 (이번 일의 원인) | 감독 규칙: 키트에서 온 파일을 고쳤으면 보고 끝에 "키트 저장소에도 올릴까요?" |

**이름이 겹치는 v2.3**: 프로젝트의 `docs/LOOP_KIT_README.md` 에 v2.3.1 이라고 적혀 있으면 그건 "가벼운 바퀴" 판이다. 이 저장소의 v2.3.0(비용 측정·노력 단계)과 다른 것이고, v2.5 에는 둘 다 들어 있다.
**일꾼이 써야 하는 스킬은 프로젝트 `.claude/skills/` 에 둔다.** 가벼운 바퀴의 일꾼은 사람 전역 스킬(`~/.claude/skills`)을 못 본다. 영상 보기처럼 전역에만 있는 스킬은 감독(Code 탭 대화)만 쓴다.

## v2.6 반영됨 — Snarehaven 에서 v2.5 를 쓰며 보완한 것 (10-06~10-19 → 키트 v2.6.0, soilrist/loop-starter-kit PR #5 머지 2026-10-19)
1. **3층을 감독에게 넘겨 루프 몫 0** → 검사 스위치 + 헤드리스 행 실행기(`tools/unreal/run_m96_row.py`), PROMPT 「못 함(감독)」은 실기기 손 감각·실결제·사람 계정만(25e5d3b3), CLAUDE.md 함정 줄(d428fe64).
2. **루프가 스스로 섬** → PROMPT 2-4 「감독 확인 대기는 막힘 아님」 + `loop/watchdog.sh`·launchd 15분, 일부러 세울 땐 `loop/HOLD`(98b00054).
3. **한도에 걸려 멈춤** → `loop/env.sh` 계정 순서 + 모두 한도일 때만 API 키 폴백(4f884098), 소넷 우선·노력 medium/high(2f65a5fb·1f7ef401).
4. **에디터는 통과·출시 빌드만 깨짐** → `check_conventions` R16(`#endif` 뒤 시험), `tools/check_shipping_switches.py`(검사 스위치 출시 빌드에서 닫힘, 19ad2a98).
5. **겉모습·기록 구멍** → Reference 6절 「놀이는 같게, 겉모습은 다르게」(c707a1f0), Verification 13·14, 계정 번호 `<숨김>` 함정, `lap_audit.py` 글자 줄 건너뜀(a125919b).
- 남은 것: 2층 UI 시험은 계산 함수가 아니라 **실제 배치 사각형**으로(겹침·화면 밖 0, 이 게임 INBOX 10-19 지시) · 감독 몫 일감은 `DIRECTOR_TODO.md` 로 분리.

## 확정된 규칙
| 항목 | 정한 것 | 이유 |
|---|---|---|
| 레퍼런스 | 주 레퍼런스 1개 고정, 구조·콘텐츠·UI 흐름은 그대로, 에셋 원본·이름은 새로 — `docs/conventions/Reference.md` | 기준이 구체적일수록 결과가 좋다 |
| 커밋 | `feat(combat): 3연타 추가` + 꼬리말 `Loop:` `Checks:` `Slice:` | 훅이 형식·바퀴 번호·검사 기록까지 기계로 검증, 조각별 추적 |
| 에셋 이름 | 접두어 방식 (예 Unity `SPR_Player_Hero_Run_01`) — 엔진별 구체 규칙은 `docs/engines/<엔진>.md` | 이름이 ID처럼 검색됨 |
| 코드 이름 | 엔진별 공식 가이드 (Unity `m_`/`k_`/`s_`, Unreal `b`/PascalCase, Godot snake_case) — `docs/engines/<엔진>.md` | 각 엔진 커뮤니티가 이미 읽을 줄 아는 규칙 |

## 들어 있는 것
| 파일 | 하는 일 |
|---|---|
| `CLAUDE.md` | 모든 AI가 제일 먼저 읽는 한 장 |
| `docs/conventions/` | 규칙 원문 (이름·폴더·코드·데이터·커밋·브랜치·버전·검사·INBOX·**자율 판단**·**레퍼런스 충실**·**플레이어 눈**) |
| `docs/reference/` | 레퍼런스 시트 (화면·캐릭터마다 한 장 — 만들기 전에 쓰고, 캡처와 나란히 대조) |
| `.claude/skills/` | `kickoff`(한마디→설계·로드맵), `new-script`(코드), `new-content`(데이터), `new-visual`(보이는 것), `reference-sheet`(레퍼런스 시트·동작 관찰표), `commit`, `inbox`, `release` |
| `.claude/agents/convention-checker.md` | 읽기 전용 규칙 점검 AI |
| `.claude/settings.json` | 위험 명령 차단 (`git add -A`, `push --force`, `rm -rf` …) |
| `.githooks/` | 커밋 메시지 형식·규칙 위반을 **커밋 순간에 막음** |
| `tools/check_conventions.py` | 이름·엔진별 사이드카 파일·코드 접두어·안 채운 숫자·레퍼런스 시트 유무 자동 검사 (점검한 규칙 목록 출력) |
| `tools/jev.py` | Jev(TypeSafe System One) 제3 판정기 — `evidence`(통과 인용이 진짜인가) · `noise`(엔진 에러 줄이 잡음인가) · `verdict`(결함 지적이 진짜인가) · `selftest`. 키는 Keychain `typesafe-api-key` 또는 `TYPESAFE_API_KEY` |
| `loop/` | `PROMPT.md`(일꾼), `DIRECTOR.md`(비서), `loop.sh`·`env.sh`·`ctl.sh`(맥 launchd 실행기), `lap_audit.py`(바퀴 기록 렌더·STATUS 인용 대조·눈 검사 대조·턴/토큰/추정비용 집계·문서 크기 점검과 보관) |
| `evals/` · `.claude-plugin/` | 키트 자체 시험용 (`claude plugin eval`) — **프로젝트에는 복사하지 않음** |
| `docs/DESIGN.md` `ROADMAP.md` `STATUS.md` `feedback/INBOX.md` | 기획서·조각 목록·바퀴 기록·우편함 |
| `docs/templates/` | 기능 명세·버그·결정·사고기록 양식 |

---

## 1단계: 설치 (맥 → 클로드 데스크탑 → Code 탭)
압축을 풀어 둔 뒤, **프로젝트 폴더를 연 Code 탭 세션**에 아래를 그대로 붙여넣으세요.

```
루프 스타터 키트 v2.5.0 을 이 프로젝트(〔프로젝트 이름〕)에 설치해 줘. 키트 위치: 〔압축 푼 loop-starter-kit 폴더 경로〕
키트의 README.md "설치 규칙"을 먼저 읽고 그대로 따라 줘.
끝나면 결과를 5줄로 보고하고, 내가 정해야 할 것만 모아서 물어봐.
루프는 아직 켜지 마.
```

## 설치 규칙 (설치하는 AI가 읽는 부분)
1. **먼저 살펴보기**
   - 어떤 엔진인지 확인한다 (`Assets/`+`ProjectSettings/`=Unity, `*.uproject`=Unreal, `project.godot`=Godot). 애매하면 물어본다.
   - git 저장소 여부, 엔진 프로젝트 위치(루트 / 하위 폴더), 엔진 버전을 확인한다 — 구체 확인 방법은 `docs/engines/<엔진>.md`.
   - 이미 있는 `CLAUDE.md`·`docs/`·`loop/`·`.gitignore`·기획 문서도 확인한다.
   - 발견한 걸 먼저 보여 준다.
2. **덮어쓰지 않기**
   - 이미 있는 파일은 차이를 보여 주고 합칠 방법을 제안한다. 사람 승인 후에만 합친다.
   - 옛 루프 파일(`loop/`)이 있으면 `loop/_v1_backup/` 으로 옮긴 뒤 새 파일을 넣는다. 지우지 않는다.
3. **복사**
   - 키트 파일을 복사하되 `README.md` 는 `docs/LOOP_KIT_README.md` 로 넣는다. `evals/`·`.claude-plugin/` 은 키트 자체 시험용 — 복사하지 않는다.
   - 실행 권한: `chmod +x loop/*.sh .githooks/*`
4. **이름 맞추기**
   - `loop/env.sh` 의 `LOOP_SLUG` 를 프로젝트 이름(영어 소문자·하이픈, 예 `joseon-sword`)으로 바꾼다.
   - 문서 속 예시 네임스페이스 `MyGame` 은 프로젝트 이름(PascalCase, 예 `JoseonSword`)으로 바꿔도 된다. 🟢
5. **CLAUDE.md 〔〕 칸 채우기**
   - 알 수 있는 건 채운다.
   - 게임 소개처럼 모르는 건 🔴 질문 목록에 올린다.
6. **git 설정**
   - `git config core.hooksPath .githooks`
   - `develop` 브랜치가 없으면 만들고, **`git checkout develop` 으로 옮겨 둔다** (루프는 develop 에서만 돈다).
   - `git lfs version` 이 실패하면 `.gitattributes` 의 `#LFS-BEGIN`~`#LFS-END` 사이 줄을 주석 처리한다 (Unreal 은 `.uasset`/`.umap` 이 이 블록에 있어 LFS 가 특히 중요 — 안 되면 🔴 질문).
   - **Unity 일 때만**: 설치된 Unity 버전의 `UnityYAMLMerge` 경로를 찾아(맥: `/Applications/Unity/Hub/Editor/<버전>/Unity.app/Contents/Tools/UnityYAMLMerge`)
     `git config merge.unityyamlmerge.name "Unity SmartMerge"` 와
     `git config merge.unityyamlmerge.driver "'<경로>' merge -p %O %B %A %A"` 를 설정한다. 못 찾으면 보고만.
     `Project Settings > Editor > Asset Serialization` 이 Force Text 인지 확인하고, 아니면 🔴 질문에 올린다.
   - Unreal/Godot 은 이 단계 불필요 (씬·에셋이 텍스트/바이너리 다루는 방식이 달라 `.gitattributes` 설정으로 충분 — `docs/engines/<엔진>.md`).
7. **검사**
   - `python3 tools/check_conventions.py` 를 돌린다 (엔진 프로젝트가 하위 폴더면 `--root <폴더>`).
   - 기존 파일의 위반은 **지금 고치지 않는다.** 목록만 보여 주고, ROADMAP 맨 앞에 `M0-00 기존 파일 규칙 맞추기` 조각으로 넣는다. 파일 이동은 루프가 에디터·엔진 MCP 로 한다.
   - 7-1. **코드 그래프**: `command -v graphify` 가 있으면 `/graphify .` 로 `graphify-out/` 을 만들고 `.gitignore` 에 `/graphify-out/` 을 넣는다 (그래프는 커밋하지 않음). 없으면 이 단계는 건너뛰고 보고에 한 줄 적는다 — 루프는 그래프가 있을 때만 `graphify query` 를 쓴다. 갱신은 감독이 마일스톤 끝에 `/graphify . --update`.
   - 7-2. **일꾼이 쓸 것은 프로젝트로**: 가벼운 바퀴의 일꾼은 사람 전역 스킬·플러그인·MCP 를 못 본다. ① 일꾼이 **엔진 MCP 로 일해야 하면**(예: Unity `unityMCP`) 그 서버 설정을 `loop/mcp.json` 에 적는다 — 루트 `.mcp.json` 과 `~/.claude.json` 것은 일꾼에게 안 간다. 명령줄·커맨드릿으로만 일하면 만들지 않는다. ② 전역(`~/.claude/skills`)의 엔진 스킬 중 **일꾼이 쓸 것만** 프로젝트 `.claude/skills/` 로 복사한다. 전부 넣지 않는다 — 스킬 목록은 모든 호출에 실린다. 감독만 쓰는 것(영상 보기 `watch` · 과설계 점검 · 그림 그리기)은 옮기지 않는다. ③ `CLAUDE.md` "프로젝트" 절의 "만드는 도구" 칸을 채운다. 확인은 `loop/ctl.sh status` 의 "일꾼 MCP" 줄.
8. **루프 시험**
   - `loop/ctl.sh try` 로 claude 를 부르지 않는 시험 두 바퀴를 돌린다 (번호는 `logs/lap.dry` 에 따로 셈).
   - `logs/usage.csv` 에 `dry` 두 줄이 생겼는지 확인한다. `python3 loop/lap_audit.py selftest` 가 `SELFTEST_OK` 인지도 본다.
   - 이미 루프가 돌던 프로젝트를 올리는 경우: 옛 `loop/env.sh` 에 `LOOP_EFFORT`·`LOOP_EFFORT_RETRY` 두 줄이 없으면 키트의 값을 옮겨 적는다 (없어도 루프는 돌고, 노력 단계만 claude 기본값이 된다).
   - `command -v claude` 와 `~/.claude-loop-nc` 폴더가 있는지 확인해 보고한다.
   - `python3 tools/jev.py selftest` 가 `SELFTEST_OK` 인지 확인한다. `UNAVAILABLE` 이면 키·SDK(`~/.claude/scripts/.venv`) 문제 — 고치지 말고 보고만 (루프는 `통과(jev 미확인)` 로 돌 수 있음).
9. **커밋**
   - `chore(loop): 루프 스타터 키트 v2.5.0 설치` 로 커밋한다 (훅 통과 확인).
   - 사람이 확인한 뒤에만 커밋한다.
10. **보고**: 설치된 것 / 합친 파일 / 기존 위반 수 / 사람이 정할 것.

---

## 2단계: 쓰는 법 (전부 Code 탭에서 말로)
| 형이 할 말 | 일어나는 일 |
|---|---|
| **"〔프로젝트 이름〕 이렇게 만들자: 〔한두 줄〕"** | 비서가 설계서 초안을 쓰고 질문 묶음을 보냄. 형이 답하면 로드맵까지 만듦 |
| **"켜"** | 준비 점검 후 루프 시작 (로그인하면 자동으로 다시 켜짐) |
| **"어때?"** | 3줄 보고 |
| **"고블린 더 빠르게"** | 비서가 바로 루프에게 지시. 루프가 값을 정해 `DESIGN.md` §6 에 근거와 함께 적음 |
| **"알아서 해"** | 비서 추천대로 진행하고 결정을 기록 |
| **"꺼"** | 하던 조각 마무리하고 멈춤 |

## 키트 자체 검사 (키트를 고친 사람이 돌리는 것)
`evals/` 의 8 케이스는 "규칙 문서·스킬이 있을 때 vs 없을 때" AI 답이 달라지는지를 `claude plugin eval` 로 잰다 (숫자 되묻기, 신호등, 커밋 형식, 검사기 보고, INBOX 형식, kickoff 질문, 검사 정직성, 플레이어 눈). 키트 루트에서:

```bash
env ANTHROPIC_BASE_URL=https://api.anthropic.com ANTHROPIC_AUTH_TOKEN= ANTHROPIC_MODEL= ANTHROPIC_DEFAULT_OPUS_MODEL= ANTHROPIC_DEFAULT_SONNET_MODEL= ANTHROPIC_DEFAULT_HAIKU_MODEL= claude plugin eval . --scaffold --no-publish --trust-plugin --judge-model sonnet --runs 2 --ablation with-without
```

앞의 `env …=` 는 settings.json 이 옴니라우터 별칭을 가리킬 때 공식 API 로 되돌리는 것 — 그대로 두면 판정 모델을 못 찾아 실패한다. 결과는 `evals/results/` (커밋 안 함). 기준: with 점수가 without 보다 높아야 하고, 떨어진 케이스는 그 규칙 문서를 고친 뒤 다시 잰다.

**규칙 문서·PROMPT·스킬을 고칠 때의 순서** (감으로 고치지 않기):
1. 고치기 **전에** 위 명령으로 점수를 한 번 잰다 (기준 점수).
2. **한 번에 한 문서, 한 가지만** 고친다. 왜: 둘을 같이 고치면 무엇이 효과였는지 모른다.
3. 다시 잰다. 겨냥한 케이스가 오르고 **다른 케이스가 안 떨어졌을 때만** 유지한다. 안 올랐거나 다른 게 떨어졌으면 되돌린다. `--runs 2` 사이의 흔들림보다 작은 차이는 오른 게 아니다.
4. 시험 문제(`evals/*/case.yaml`)의 상황·정답 문장을 규칙 문서에 **그대로 베껴 넣지 않는다.** 왜: 그 케이스만 맞히고 실제 바퀴에서는 안 지켜지는 "시험 문제 외우기"가 된다. 규칙은 원리로 적는다.
5. 실제 프로젝트에서 새로 본 실수는 규칙을 고치기 전에 **새 케이스로 먼저 추가**한다 (지금 규칙으로는 틀린다는 걸 확인한 뒤 고친다).
6. 2~3번 고쳐도 안 오르는 케이스는 규칙이 아니라 **케이스나 채점 기준(`graders/criteria.md`)이 틀렸을 수 있다** — 그쪽을 먼저 다시 읽는다.

실제 루프의 설정(모델·노력 단계·쉬는 시간)은 eval 이 아니라 `loop/ctl.sh cost` 의 전후 비교로 같은 순서를 따른다 (`loop/DIRECTOR.md` 감시 & 검수 5).

## 출처
- Unity `.gitignore`: github/gitignore
- C# 이름 규칙: Unity 공식 *Naming and code style tips* / *C# Style Guide (Unity 6)*
- 폴더: Unity 공식 *Organizing your project*
- 에셋 접두어: justinwasilenko/Unity-Style-Guide
- Unreal `.gitignore`/`.gitattributes`: github/gitignore (UnrealEngine.gitignore) + Epic 공식 *Setting Up Unreal Engine Projects With Perforce/Git* 가이드
- C++ 이름 규칙: Epic 공식 *Coding Standard*
- Godot `.gitignore`: godotengine/godot (프로젝트 템플릿) + github/gitignore (Godot.gitignore)
- GDScript 이름 규칙: Godot 공식 *GDScript style guide*
- 커밋: Conventional Commits 1.0
- 버전: SemVer 2.0
- 변경기록: Keep a Changelog 1.1
- Jev 판정기: TypeSafe 공식 문서 docs.typesafe.ai (System One · Python SDK · citation_check 쿡북), 플러그인 `typesafe@typesafe-ai`
- 설정 구조(CLAUDE.md·규칙 문서·스킬·검사 에이전트·MCP 경계): "유니티 팀 프로젝트 AI 설정" 교재 (엔진 무관 구조로 일반화)
