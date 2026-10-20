# 기능 명세: 올가미 조이기 링 (M21-09)

- 상태: 초안 (루프#333 — 숫자·구조 전부 🟡, INBOX `확인(루프#333)`)
- 마일스톤: M21 · 범위(커밋 scope): `hunt`(판정·도주) · `net`(게스트 길) · `ui`(링 화면)
- DESIGN 연결: `DESIGN.md` 3.1 「올가미 링」 줄들(숫자 원본) · 3.1 「포획 성공률」 · 4.1 권위 표(포획 판정 = 호스트)
- ROADMAP 원문: M21-09 「🟡 올가미 조이기 링 — 줄어드는 원, 성공 창 0.6초(희귀·큰 종 0.4초), 실패해도 HP 유지·동물 1.5초 도주 뒤 다시 대기. 레퍼런스 게임에 없는 우리만의 핵심 동작(법적 차별화). DESIGN 3 "포획 100%" 를 바꾸는 것이라 감독 🟡 — 사용자가 싫다 하면 되돌린다. 합격: 2층(창 안 → 포획, 창 밖 → 실패·HP 유지·1.5초 뒤 Capturable) + `animal_capture_failed` 사유 `timing`.」
- 지금 길: 포획 버튼 → `UCaptureComponent::TryCaptureTarget` → 호스트 `TryCaptureTargetOnAuthority` → `UAnimalStateComponent::TryCapture`(Capturable 이면 즉시 Captured). 게스트는 `Server_TryCaptureTarget()` 인자 없이.

## 1. 합격 기준 (한 문장)
> 체력을 깎아 포획 버튼이 켜진 동물 앞에서 포획을 누르면 동물 둘레에 원이 줄어들고, 원이 목표 원에 닿기 전 마지막 0.6초(큰 종·변종 0.4초) 안에 한 번 더 누르면 잡히며, 그 밖에 누르거나 놓치면 HP 는 그대로인 채 동물이 1.5초 달아났다가 다시 멈춰 포획을 기다린다.

## 2. 플레이어가 보는 것
- 입력: **같은 포획 버튼 두 번** — 첫 누름 = 링 시작, 둘째 누름 = 조이기. 새 버튼 없음(오른쪽 아래 버튼 자리 그대로, 손가락 위치를 안 바꾼다).
- 화면: 동물 발밑(화면 좌표로 옮긴 자리)에 고정 **목표 원**(크림 테) + 바깥에서 줄어드는 **올가미 원**(흰 테). 올가미 원이 성공 창에 들어오면 목표 원이 초록으로 바뀐다. 둘째 누름이 창 안이면 지금의 포획 순간 연출(`SFX_Cinch`·가벼운 진동·`포획 성공 - 이름` 배너) 그대로. 창 밖·시간 초과면 HUD 가운데 배너 `놓쳤다!` 1.5초(포획 성공 배너와 같은 자리·글꼴), 동물은 달아나는 클립(`FleeAnimRef`)으로 1.5초.
- 실패·예외:
  - 링 도중 대상이 바뀌거나(하이라이트 이동·사거리 밖) 동물이 죽거나 다른 사람이 먼저 잡으면 링은 **조용히 사라진다** — 실패로 세지 않고 도주도 없다.
  - 링 도중 공격 버튼은 그대로 쓸 수 있다(조작을 빼앗지 않는다 — 3.1 스태미나 줄과 같은 선). 때려서 죽으면 위 줄대로 사라진다.
  - 도주 1.5초 동안 동물은 `Capturable` 이 아니라 포획 버튼이 꺼진다. 도주 중 **반격하지 않는다**(3.1 「Capturable 동안 공격 안 한다」 와 같은 이유). 맞으면 HP 는 깎이고 0 이면 죽는다.
  - 둘이 같은 동물에 각자 링을 돌리면 먼저 성공한 사람이 잡는다. 늦은 사람의 둘째 누름은 이미 끝난 동물이라 `already_finished`(지금 사유 그대로)이고 도주를 일으키지 않는다.
  - 되돌리기: `DT_PlayerCombatTable` `Player` 행 `CaptureRingSeconds` 를 0 으로 두면 링 없이 첫 누름에 즉시 포획(지금 동작) — 사용자가 싫다 하면 코드 없이 표 한 칸으로 되돌린다.

## 3. 숫자 — 원본은 DESIGN 3.1 「올가미 링」 줄들 (여기는 요약, 다르면 DESIGN 이 이긴다)
| 이름 | 값 | 단위 | 비고 |
|---|---|---|---|
| 링 길이 (올가미 원이 목표 원에 닿기까지) | 1.5 | 초 | 창 0.6 이 링의 40% — 캐주얼 모바일에서 한 번 보고 맞출 만한 비율 |
| 성공 창 | 0.6 | 초 | ROADMAP 원문. 링 끝 직전 `[1.5−0.6, 1.5]` |
| 성공 창 (희귀·큰 종) | 0.4 | 초 | ROADMAP 원문. 큰 종 = `MaxHP` ≥ 140(엘크·스라소니·늑대·울버린·곰 — 3.1 「동물 체력」 의 큰 쪽 셋 단계), 희귀 = 변종(M21-07a) |
| 큰 종 기준 HP | 140 | HP | 새 표 칸을 만들지 않고 이미 있는 `MaxHP` 로 가른다 |
| 실패 도주 시간 | 1.5 | 초 | ROADMAP 원문. 도주 속도는 3.1 「동물 도주 속도」 45cm/s 그대로라 약 67cm — 사거리 150 안팎이라 한두 걸음 다가가면 다시 닿는다 |
| 게스트 판정 시각 허용 오차 | 0.25 | 초 | 아래 5-3. 폰 망 흔들림(지터) 몫 — 링 길이의 1/6 |
| 실패 배너 | 1.5 | 초 | 도주 시간과 같게 — 배너가 사라지는 순간 다시 누를 수 있다 |

## 4. 데이터·에셋
| 종류 | 이름(규칙대로) | 있음/만들 것 |
|---|---|---|
| 표 칸 | `FPlayerCombatRow` `CaptureRingSeconds`·`CaptureWindowSeconds`·`CaptureWindowHardSeconds`·`CaptureHardMinHP`·`CaptureEscapeSeconds`·`CaptureRingToleranceSeconds` (미정 -1, 생성기가 DESIGN 값) | 만들 것 (09a) |
| 상태 | `EAnimalState::Escaping` (도주 1.5초 — Fight 를 빌리지 않는다: Fight 는 「HP > 25%」·반격 종의 반격이 붙어 있다) | 만들 것 (09b) |
| 화면 | `USnareCaptureRingWidget` — 원 두 개는 `FSlateRoundedBoxBrush` 테두리 원(새 텍스처 0) | 만들 것 (09d) |
| 소리 | 성공 `SFX_Cinch`(있음) · 실패는 새 소리 없음(배너만) | 있음 |
| 분석 | `animal_capture_failed` `reason` 에 `timing`(창 밖·시간 초과) — `docs/ANALYTICS.md` 표 글 한 줄 | 만들 것 (09a) |

## 5. 조각 나누기
1. **M21-09a 링 판정(호스트)** — 표 칸 6개 + 생성기 · 순수 함수 `SnareCaptureRing::Judge(Elapsed, RingSeconds, WindowSeconds)` → `InWindow`/`Early`/`Timeout` · `UCaptureComponent`: 대상 Capturable 일 때 첫 누름 = 링 시작(대상·시작 시각 기억, `OnCaptureRingStarted`), 둘째 누름 = 판정, 링 길이 지나도록 안 누르면 `Timeout` 실패 · 창 = 큰 종(`MaxHP` ≥ 140)·변종이면 0.4 · 실패 = `animal_capture_failed` `timing`(HP 그대로, 상태 그대로 — 도주는 09b) · `CaptureRingSeconds` 0 이면 지금처럼 즉시 · 기존 시험(`CaptureTest`·`HuntHudTest`·`ActionClipTest`·`FirstFiveMinutesTest`·`AnimalReplicationTest`)은 시험용 「창 안에서 누르기」 도우미로. 2층: 창 안 → Captured · 이른 누름·시간 초과 → 실패·HP 그대로 · 큰 종 창 0.4 · 길이 0 → 즉시. **`create_data_tables.py`·`AnimalStateComponent.*`(변종 읽기) 가 #329 잔여 미커밋이라 그 정리 뒤.**
2. **M21-09b 실패 도주(호스트·복제)** — `UAnimalStateComponent::BeginEscape(Seconds)` → `Escaping`(복제) · `AAnimalCharacter::Tick` 이 Escaping 이면 Fight 와 같은 도주 이동(이탈 한계 500 그대로)·반격 없음 · 시간이 차면 `Capturable` 로 돌아와 멈춤. 09a 실패가 이것을 부른다. 2층: 실패 → Escaping → 1.5초 뒤 Capturable · HP 그대로 · 도주 중 버튼 꺼짐 · 도주 중 맞아 0 → Dead.
3. **M21-09c 게스트 길** — 링은 **누른 사람 기기 시계**로 돈다(왕복 지연이 창 0.4초를 먹지 않게). 첫 누름 `Server_BeginCaptureRing()`(호스트가 그 사람·대상·호스트 시각 기억), 둘째 누름 `Server_ResolveCaptureRing(float Elapsed)` — 호스트는 ① 같은 사람·같은 대상의 링이 있는지 ② 호스트가 잰 간격과 `Elapsed` 의 차가 허용 오차 0.25초 안인지 보고, 맞으면 `Elapsed` 로 판정 · 틀리면 `timing`. 4.1 대로 판정은 호스트, 펫 지급은 그대로 서버 `ClaimLoot`. 2층: 오차 안 → 기기 시각으로 판정 · 오차 밖 → 실패 · 링 없이 Resolve → 무시.
4. **M21-09d 링 화면** — `USnareCaptureRingWidget`(사냥 HUD 가 만들고 `OnCaptureRingStarted`·판정 알림을 듣는다) · 동물 발밑 화면 자리에 목표 원·올가미 원, 창 안이면 초록 · 실패 배너 `놓쳤다!` 1.5초 · 첫 실행 안내 포획 장 글을 `포획 버튼을 누르고 원이 초록일 때 한 번 더!` 로. 2층: 링 반지름이 시간에 따라 줄고 창 안에서 초록 · 3층 캡처 1장(창 안 초록 순간, `-SnareCaptureRingHold` 같은 검사 스위치로 한 번 찍고 끝). **`HuntHudWidget.cpp` 가 #329 잔여라 그 정리 뒤.**

## 6. 검사 방법
- 1층(기계): 빌드 · `tools/check_conventions.py --changed`
- 2층(동작): `Snarehaven.Capture.Ring.*`(판정 함수 경계 · 창 안/밖/시간 초과 · 큰 종 창 · 길이 0 되돌리기 · 실패 사유 `timing`) · `Snarehaven.Animal.Escape.*`(1.5초 뒤 Capturable · HP 그대로) · `Snarehaven.Capture.Ring.Guest*`(허용 오차) · `Snarehaven.HuntHud.CaptureRing.*`(반지름·색). 숫자는 시험 전용 값으로(밸런스가 바뀌어도 안 깨지게), 표 = DESIGN 글자 맞대기 하나만 실제 값.
- 3층(눈): 링이 창 안(초록)인 순간 캡처 한 장 — 한 번 찍고 끝(INBOX 상시 지시).

## 7. 하지 않을 것
- 성공 확률 굴림 — 링은 타이밍만이다(3.1 「확률 롤 없음」 의 뜻을 지킨다).
- 실패 횟수 제한·실패 시 HP 회복·동물이 완전히 도망 — 원문이 「HP 유지·1.5초 뒤 다시 대기」 다.
- 종마다 다른 링 길이·창 — 원문은 「보통 0.6 / 희귀·큰 0.4」 두 단계뿐이다.
- 링 화면을 다른 사람 기기에 보이기 — 링은 누른 사람 것이다(남에겐 동물이 잡히거나 달아나는 결과만 복제로 보인다).
- 새 소리·새 텍스처 — 있는 `SFX_Cinch`·둥근 상자 붓으로 한다.
