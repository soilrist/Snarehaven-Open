# 기능 명세: 연료 발전기와 전력 판정 (M108-07)

- 상태: 초안 (루프#1402 — 숫자 전부 🟡, INBOX 확인 루프#1402)
- 마일스톤: M108 「건축가 꾸미기」 · 범위(커밋 scope): `build`
- DESIGN 연결: `DESIGN.md` 8.11 「집 2층·전력」(전력 단위·키트·배터리·전등·「전력 판정」) · 「건축가 꾸미기」 「연료 발전기」·「전력 판정」 줄 · INBOX 지시(사람 10-19 18:40) ④ 「연료 발전기(나무·석탄 넣으면 시간당 전력, 소리) + 전기 가구」

## 0. 지금 있는 것 (루프#1402 조사)
- 전력은 **8.11 「집 2층·전력」** 에 이미 있다: 땅(텐트) 하나마다 햇빛 집열판 `item.solar_kit` 공급 **10** · 배터리 `item.battery` **+10**(키트 1개 이상일 때만) · 전등 `item.lamp_electric` 소모 **5**. 공급 합 ≥ 소모 합이면 밤에 그 땅 전등 **전부** 켜짐, 모자라면 전부 꺼짐.
- 셈은 클라 `USnarePowerSubsystem`(0.5초마다, `SnarePowerSubsystem.cpp`)이 놓인 가구 액터를 땅마다 세어 한다 — 복제할 값 없음. 공급 셈 함수 `SnareBuildRules::PowerSupply(Rules, SolarKits, Batteries)`, 숫자는 `FBaseRuleRow` 「Power」 칸(`SolarKitPower`·`BatteryPower`·`LampPowerUse` …).
- 서버에 **시각으로 줄어드는 연료**의 본보기가 둘 있다: 집 유지 `FeedUpkeep {tentId, count}`(나무 1 = 2시간, 최대 200시간, `upkeepUntilUtc`) · 목장 유지 `FeedRanchUpkeep`. 둘 다 「끝나는 시각」 하나만 저장하고 호출 때 남은 시간을 셈(목장 타이머 방식).
- 클라 「나무 넣기」 단추 줄 본보기: 집 판 `UCraftingScreenWidget` 「유지비 나무 1/10개 (+n시간)」(`CraftingScreenWidget.cpp:978`) → `USnareBaseSubsystem::RequestFeedUpkeep`.
- 놓인 가구 기능 칸 `EPlaceableFunction`(`ItemRow.h:93`)에 발전기 자리 없음. 소리 합성 `tools/audio/make_sounds.py` 에 엔진 소리 틀 `make_engine(pulse_hz, noise_hi)`(전기톱 공회전)이 있다.
- 숫자 줄 충돌: 「건축가 꾸미기」 「연료 발전기」 줄은 **전등 10W** 로 적었지만 이미 쓰는 8.11 표·`DT_BaseRuleTable` 은 **전등 5**. → 아래 3절에서 **8.11 단위(전등 5)를 그대로** 두고 발전기 줄을 그 단위로 읽는다(이미 배포·시험된 숫자를 안 흔든다).

## 1. 합격 기준 (한 문장)
집 땅에 발전기를 놓고 나무나 석탄 덩어리를 넣으면 발전기가 소리를 내며 돌고 그 땅의 공급이 100 늘어 전등(밤)과 전기 가구가 켜지며, 연료가 다 타면 소리가 멎고 공급 합이 모자란 땅은 전부 꺼진다 — 연료 시각은 서버가 정하고 모든 기기가 같은 답을 본다.

## 2. 플레이어가 보는 것
- 놓기: 배낭 「집 꾸미기」 목록의 바닥 가구 하나(`item.generator`, 1×1). 텐트 땅 안 어디나(지금 놓기 길 그대로), 러그 위 놓임.
- 판: 발전기에 다가가 「쓰기」 → 판 「발전기」 — 줄: 「돌아감 · 연료 1시간 20분 남음」/「멈춤 — 연료 없음」 · 「이 땅 전력 공급 110 / 소모 85」(모자라면 붉은 글 「전력이 모자라 전부 꺼짐」) · 단추 「나무 1개 (+10분)」·「나무 10개 (+100분)」·「석탄 덩어리 1개 (+30분)」·「석탄 덩어리 10개 (+300분)」 · 「연료는 최대 12시간」.
- 도는 모습: 연료가 남으면 배기 통 위 작은 연기(모닥불 연기 재사용, 없으면 몸 떨림만) + 반복 엔진 소리(가까이 크게, 집 둘레 밖에서 안 들림). 멈추면 둘 다 꺼짐.
- 실패·예외: 연료 가득 = `full`(「연료가 가득이다」) · 나무·석탄 모자람 = `not_enough_items` · 남의 땅 발전기는 판에 넣기 단추가 없음(서버는 자기 `base` 만 읽어 `not_found`) · 연료가 아닌 것 = `bad_args`. 연료가 남은 발전기를 치우면 남은 연료는 사라진다(판에 「치우면 남은 연료는 사라진다」 한 줄).

## 3. 숫자 (전부 🟡 — 근거와 함께, DESIGN 8.11 「건축가 꾸미기」 「연료 발전기」 줄로 옮김)
| 이름 | 값 | 근거 |
|---|---|---|
| 전력 단위 | 8.11 그대로 — 전등 **5** · 키트 **10** · 배터리 **+10** | 이미 배포·시험된 표(`DT_BaseRuleTable`, 시험 `Snarehaven.Base.Power.LampsNeedSupplyAtNight`). 「연료 발전기」 줄의 「전등 10」은 이 단위와 어긋나 버림 |
| 발전기 출력 | 연료가 남으면 **100** | 「연료 발전기」 줄 값. 전기 가구 넷 한 벌(냉장고 40 + 네온 25 + 선풍기 20 + 라디오 15 = 100)을 발전기 하나가 딱 돌림 — 전등은 키트·배터리 몫, 남는 것 없이 「하나 더 놓으면 하나 더 필요」가 읽힌다 |
| 나무 1개 | **10분** | 「연료 발전기」 줄 값. 집 유지(나무 1 = 2시간)의 1/12 — 유지는 땅을 지키는 느린 비용, 발전기는 꾸민 집을 켜는 사치라 나무가 빨리 탄다 |
| 석탄 덩어리 `item.coal_chunk` 1개 | **30분**(나무 × 3) | 지시 「나무·석탄」. 석탄은 광맥 한 번에 최대 4개(`create_data_tables.py` `OreBasic`)라 나무보다 드물고, 장르 상식(화로 연료: 석탄이 나무보다 몇 배 오래 탐)에서 × 3. 코크스(`coal_ingot`)는 배터리 재료라 연료에서 뺌 |
| 연료 최대 | **12시간**(나무 72 · 석탄 24) | 하룻밤 자고 와도 켜져 있는 길이. 유지 최대 200시간보다 짧아 발전기는 「돌보는 물건」으로 남는다 |
| 연료가 타는 때 | **늘**(낮·밤·접속 끊김 상관없이 시계로) | 서버 「끝나는 시각」 하나로 셈(유지와 같은 길) — 밤에만 태우려면 서버가 게임 낮밤을 알아야 해 판정이 늘어난다. 냉장고·라디오는 낮에도 쓰니 늘 도는 것이 장르 상식 |
| 공급·켜짐 판정 | 땅 하나 공급 = 키트·배터리(8.11) + 연료가 남은 발전기 수 × 100 · 소모 = 전등 5 × 수 + 전기 가구(M108-08) 합. **공급 ≥ 소모면 전부 켜짐, 모자라면 전부 꺼짐**(8.11 한 줄 판정 그대로). 전등은 그대로 밤에만 불빛, 전기 가구는 낮밤 없이 | 8.11 「전력 단위」 줄 — 하나씩 나눠 켜면 폰에서 「왜 이것만 꺼졌지」가 안 읽힌다 |
| 발전기 수 | 땅 하나 **제한 없음**(가구 한도 1개 몫) | 한도는 가구 한도 40/60 이 이미 막는다. 새 숫자를 늘리지 않음 |
| 레시피 | 고급 작업대 **소형 전동기 1 + 철판 2 + 쇠붙이 부속 2** | 소형 전동기(8.11)의 둘째 쓰는 곳 — 지금은 전동 추진기뿐. 철판 2 = 몸통, 부속 2 = 배기·손잡이. 키트(회로판 + 철판 + 폴리머)보다 한 단 비쌈 — 출력이 키트의 10배 |
| 판매가 | 재료 판매가 합 + 1 | 8.16 「주괴 = 재료 합 + 1」 줄 |
| 소리 | 반복 엔진, 감쇠 안쪽 **300cm** · 바깥 **1500cm** · 음량 0.5 | 1500 = 텐트 땅 지름 남짓(집 마당 밖에서는 안 들림). 300 = 집 3×3 한 방. 전기톱 공회전보다 낮은 맥(파동 약 25Hz)으로 「돌아가는 기계」 |

## 4. 데이터·에셋
| 종류 | 이름 | 있음/만들 것 |
|---|---|---|
| 아이템 행 | `item.generator`(바닥 가구 1×1, 행 기능 **`Generator`** 새 값, 섹션 1) | 만들 것 — `create_data_tables.py` |
| 규칙 칸 | `FBaseRuleRow` 「Power」 칸에 `GeneratorItemID`·`GeneratorPower`(100)·`GeneratorFuelMinutes`(나무 10·석탄 30 맵)·`GeneratorFuelMaxHours`(12) | 만들 것 — 서버 상수와 같은 값(시험이 맞춰 봄) |
| 서버 | `GENERATOR_ITEM`·`GENERATOR_FUEL_MINUTES`·`GENERATOR_FUEL_MAX_HOURS` · 놓인 가구 줄에 `fuelUntilUtc` · 핸들러 `FeedGenerator {tentId, structureId, fuel, count}` → `{used, fuelUntilUtc, nowUtc}` · `GetBase` 응답에 `nowUtc` | 만들 것 — `cloudscript_main.js`·`test_cloudscript.js`·`docs/BACKEND.md` |
| 메시 | `SM_Furn_Generator_01`(집 가구 폴더 — 루프#1403 이 이름을 가구 길에 맞춤) — 파이썬 그린 메시(`make_home_furniture_meshes.py` 길), 팔레트 섹션 1 | 만들 것. 우리 모양: 세운 통(올리브 초록 통 + 크림 띠 + 짧은 굴뚝 + 앞 손잡이), 바퀴·빨강 몸 없음(Reference 6절) |
| 아이콘 | `T_Icon_Generator` | 만들 것 — 아이콘 렌더 길 |
| 소리 | `SFX_Generator_Loop_01` — `make_sounds.py` `make_engine` 틀 | 만들 것 |
| 클라 | 사본 `FSnarePlacedMirror.FuelUntilUtc` · 놓인 액터 복제 `GeneratorFuelEndServerTime`(호스트가 서버 응답 `fuelUntilUtc − nowUtc` 를 게임 서버 시계로 옮김 — 기기 시계 어긋남 없음) · `SnareBuildRules::PowerSupply` 에 발전기 수 · 판 `UCraftingScreenWidget` 발전기 줄 · `USnareBaseSubsystem::RequestFeedGenerator` | 만들 것 |

## 5. 조각 나누기
1. **07a 서버** — 상수·레시피·판매가·`FeedGenerator {tentId, structureId, fuel, count}`(인자 이름은 `RemoveStructure` 와 같은 `structureId` — 루프#1403; 가득 `full`·모자람 `not_enough_items`·발전기 아님 `not_generator`·없는 텐트·가구 `not_found`·연료 아님 `bad_args`)·놓인 줄 `fuelUntilUtc`·`GetBase` `nowUtc` · 서버 시험 · BACKEND 문서 · DESIGN 표 옮김. 합격: 서버 시험(미배포 — 배포는 감독).
2. **07b 행·메시·아이콘** — `item.generator` 행(`Generator`)·레시피(설계서 = 클라 = 서버)·규칙 칸 넷·메시·아이콘. 합격: 표 행 시험 + 모델 캡처.
3. **07c 전력 셈·판** — 사본 `FuelUntilUtc` → 액터 복제 끝 시각 → `USnarePowerSubsystem` 공급 셈에 발전기 · 발전기 판(연료 넣기 줄) · 검사 스위치 `-SnareGeneratorDemo`(연료 넣은 발전기 + 전등 4 땅, 서버 없이). 합격: 시험(연료 있음 → 공급 110·전등 켜짐 / 끝 시각 지남 → 꺼짐) + 연료 넣은 캡처.
4. **07d 소리·연기** — 합성·반입·돌 때만 반복(감쇠 300/1500)·멈추면 끔. 합격: 시험(돌 때 소리 컴포넌트 재생 중·멈추면 아님) + 귀 확인 감독.
→ 넷 끝나면 M108-07 `[x]`, M108-08 전기 가구가 소모 칸을 쓴다.

## 6. 검사 방법
- 1층(기계): `python3 tools/check_conventions.py` · 에디터 빌드 · 서버 `node tools/playfab/test_cloudscript.js`.
- 2층(동작): 서버 시험 「FeedGenerator …(M108-07a)」(나무 3 → 30분, 석탄 넣고 최대 넘침 잘림, 가득 거절, 발전기 아닌 가구 거절) · 클라 시험 `Snarehaven.Base.Power.Generator`(가짜 서버 시계로 끝 시각 앞·뒤).
- 3층(눈): `-SnareGeneratorDemo` 밤 게임 카메라 — 발전기 옆 전등 넷 켜짐 / 끝 시각 지난 판 꺼짐 · 발전기 판 글.

## 7. 하지 않을 것
- 전기 가구 다섯(냉장고·라디오·선풍기·네온 간판·전등 연결)은 M108-08.
- 배선·전선 그리기, 땅끼리 전력 나누기, 발전기 켜기/끄기 스위치(연료가 있으면 돈다).
- 연료 상자 자동 넣기·연료 아이템 새로 만들기.
