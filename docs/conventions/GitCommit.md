# 커밋 메시지 (확정)

> 기준: [Conventional Commits 1.0](https://www.conventionalcommits.org/). 요약은 한국어.
> **왜 이 형식:** 기계가 한 줄 정규식으로 검사할 수 있고, 감독이 `git log` 만 보고 "무슨 종류·어느 시스템·어느 조각·몇 바퀴째"를 알 수 있다.
> **자동 차단:** `.githooks/commit-msg` 가 형식이 틀리면 커밋을 거절한다. `.githooks/pre-commit` 이 검사기 오류가 있으면 거절한다.

## 모양
```
타입(범위): 무엇을 했는지 한 줄

(선택) 왜 — 무엇은 diff 에 있다

Slice: M1-03
Loop: 128
Inbox: 고블린 이동속도
```

첫 줄 검사 (훅):
```
^(feat|fix|data|art|refactor|perf|test|docs|build|chore|revert|merge)(\([a-z0-9-]+\))?!?: .+$
```
+ 요약 2~72**글자**(한글 1자 = 1글자), 끝에 마침표 금지. (글자 수 검사는 python3 로 — 루프 커밋은 python3 없으면 거절)

## 타입 (이 중에서만)
| 타입 | 언제 |
|---|---|
| `feat` | 새 기능 |
| `fix` | 버그 수정 |
| `data` | 콘텐츠 데이터만 (SO, CSV) — 우리 추가 |
| `art` | 그림·애니·VFX·사운드 에셋 — 우리 추가 |
| `refactor` | 동작 안 바뀌는 코드 정리 |
| `perf` | 성능 |
| `test` | 테스트만 |
| `docs` | 문서만 (STATUS·INBOX·설계서 포함) |
| `build` | 빌드 설정·패키지·asmdef |
| `chore` | 루프 설정·깃 설정 등 기타 |
| `revert` | 되돌리기 |
| `merge` | 브랜치 합치기 — 우리 추가 |

## 범위 (괄호 안)
소문자·하이픈 한 단어. 시스템 이름: `combat`, `player`, `ui`, `input`, `camera`, `inventory`, `gathering`, `ranch`, `enemy`, `skill`, `quest`, `dialogue`, `audio`, `map`, `save`, `data`, `net`, `backend`, `loop`, `status`, `inbox`, `design`, `roadmap`, `conventions`, `reference`.
새 범위가 필요하면: 목록 안 단어로 먼저 표현해 보고, 정말 없으면 루프는 가장 가까운 범위를 쓰고 INBOX `질문`으로 추가를 제안한다. 목록 수정은 사람 승인 후 감독이.

## 꼬리말 (검증용 — 루프 커밋은 필수)
| 꼬리말 | 언제 | 예 |
|---|---|---|
| `Loop:` | 루프 커밋 **전부 필수**, 값 = 이번 바퀴 번호 (훅이 대조) | `Loop: 128` |
| `Checks:` | 루프 커밋 중 `feat·fix·data·art·refactor·perf·test·build` 는 **필수** (훅이 검사) | `Checks: L1 pass, L2 pass, L3 skip(editor off)` |
| `Slice:` | ROADMAP 조각을 진행한 커밋이면 필수 (감독이 검수) | `Slice: M1-03` |
| `Inbox:` | INBOX 항목을 처리한 커밋 | `Inbox: 고블린 이동속도` |

→ 감독은 `git log --grep "Slice: M1-03"` 으로 조각 하나의 커밋을 전부 모은다.

## 규칙
1. **요약 72글자 이내, 마침표 없음, 무엇이 바뀌었는지 짧게** ("update", "작업", "수정" 한 단어 금지).
2. **커밋 하나 = 조각 하나.** 섞지 않는다.
3. 호환이 깨지면(세이브 형식 등) 타입 뒤 `!`: `feat(save)!: 세이브 형식 v2`
4. **`git add -A` / `git add .` 금지.** 파일 이름으로 지정. (`.claude/settings.json` 에서도 막음)
5. 1층 검사(컴파일·검사기) 통과 후 **캡처 보기 전에 일단 커밋** 허용 — 캡처에서 문제 나오면 같은 바퀴에서 `fix` 로 이어서.
6. 훅을 우회하는 `--no-verify` 금지. 훅이 틀렸다고 생각되면 INBOX `막힘`.

## 감독이 거절하는 커밋
- 루프 커밋인데 조각 작업에 `Slice:` 없음 (`Loop:`·`Checks:` 는 훅이 막음)
- 여러 조각이 섞임 / `.meta` 짝 없음
- `Checks:` 에 "pass" 인데 STATUS 에 실제 명령 출력 인용이 없거나, `logs/lap-<번호>.log` 에 그 명령이 없음
- UI·캐릭터·아이템 조각인데 레퍼런스 시트 대조 없이 3층 "pass"
