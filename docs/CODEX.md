# CODEX — 낚시·도감 현황표 (M10-00)

> **이 문서는 "도감 39칸이 무엇이고 · 무엇이 그 칸을 채우고 · 지금 무엇이 있고 없나" 를 한 장으로 모은 것이다.** 값을 정하는 곳이 아니다 —
> 어종·시간·코인은 `docs/DESIGN.md` 8.4 가, 도감 구조는 8.5 가, 탑승 조건은 8.3(결정 007)이 이미 정했다. 이 문서는 그 셋을 **옮겨 놓고 자리를 가리킬 뿐**이다.
> 근거: `docs/DESIGN.md` 8.1(위험구역)·8.2(작물)·8.3(탑승 조건)·8.4(낚시)·8.5(도감) · `docs/decisions/007-mount-codex-completion.md` · `docs/BACKEND.md` · 조각 목록은 `docs/ROADMAP.md` M10 절.
> **범위 밖:** 탈것 이동 속도·메시, 존별 도감, 어종 22종째 — **이 문서에 새 숫자를 적지 않는다**(지금 0개). 표의 코드 줄은 **이 시점의 `develop`** 을 말한다.
> 마지막 갱신: 루프#163 (2026-09-23, 동물 3 → 15종 · 도감 27 → 39칸 — 감독 INBOX 지시 2026-09-23 16:55, DESIGN 8.7 바르코 12종). 그 앞: 루프#152 (어종 3 → 21종 · 도감 9 → 27칸).

## A. 도감 39칸 로스터 (DESIGN 8.5)

도감은 **MVP 범위 전역 하나**다. 존별이 아니다 — 존이 사냥존·거점존 둘뿐이라 존 ID 열은 나중에 더한다(결정 007).

| 카테고리 | 칸 | 종 ID | 채우는 사건 | 근거 |
|---|---|---|---|---|
| 사냥 | 1 | `animal.pig` | 포획 성공 | DESIGN 3.2 · 8.1 (자리표시 — M5-07 끝에 빠진다) |
| 사냥 | 2 | `animal.cow` | 포획 성공 | DESIGN 3.2 (자리표시 — M5-07 끝에 빠진다) |
| 사냥 | 3 | `animal.sheep` | 포획 성공 | DESIGN 3.2 (자리표시 — M5-07 끝에 빠진다) |
| 사냥 | 4 | `animal.fox` | 포획 성공 | DESIGN 8.7 (여우) |
| 사냥 | 5 | `animal.goat` | 포획 성공 | DESIGN 8.7 (염소) |
| 사냥 | 6 | `animal.reindeer` | 포획 성공 | DESIGN 8.7 (순록) |
| 사냥 | 7 | `animal.elk` | 포획 성공 | DESIGN 8.7 (엘크) |
| 사냥 | 8 | `animal.lynx` | 포획 성공 | DESIGN 8.7 (스라소니) |
| 사냥 | 9 | `animal.wolf` | 포획 성공 | DESIGN 8.7 (늑대) |
| 사냥 | 10 | `animal.wolverine` | 포획 성공 | DESIGN 8.7 (울버린) |
| 사냥 | 11 | `animal.bear` | 포획 성공 | DESIGN 8.7 (곰) |
| 사냥 | 12 | `animal.pheasant` | 포획 성공 | DESIGN 8.7 (꿩) |
| 사냥 | 13 | `animal.peacock` | 포획 성공 | DESIGN 8.7 (공작) |
| 사냥 | 14 | `animal.ptarmigan` | 포획 성공 | DESIGN 8.7 (뇌조) |
| 사냥 | 15 | `animal.capercaillie` | 포획 성공 | DESIGN 8.7 (큰뇌조) |
| 농사 | 16 | `item.carrot` | 작물 수확 성공 | DESIGN 8.2 |
| 농사 | 17 | `item.potato` | 작물 수확 성공 | DESIGN 8.2 |
| 농사 | 18 | `item.wheat` | 작물 수확 성공 | DESIGN 8.2 |
| 낚시 | 19 | `item.crucian_carp` | 낚시 수확 성공 | DESIGN 8.4 (붕어) |
| 낚시 | 20 | `item.salmon` | 낚시 수확 성공 | DESIGN 8.4 (연어) |
| 낚시 | 21 | `item.cave_crab` | 낚시 수확 성공 | DESIGN 8.4 (동굴 게) |
| 낚시 | 22 | `item.carp` | 낚시 수확 성공 | DESIGN 8.4 (잉어) |
| 낚시 | 23 | `item.perch` | 낚시 수확 성공 | DESIGN 8.4 (농어) |
| 낚시 | 24 | `item.pike` | 낚시 수확 성공 | DESIGN 8.4 (강꼬치고기) |
| 낚시 | 25 | `item.whitefish` | 낚시 수확 성공 | DESIGN 8.4 (백어) |
| 낚시 | 26 | `item.haddock` | 낚시 수확 성공 | DESIGN 8.4 (해덕대구) |
| 낚시 | 27 | `item.cod` | 낚시 수확 성공 | DESIGN 8.4 (대구) |
| 낚시 | 28 | `item.catfish` | 낚시 수확 성공 | DESIGN 8.4 (메기) |
| 낚시 | 29 | `item.trout` | 낚시 수확 성공 | DESIGN 8.4 (송어) |
| 낚시 | 30 | `item.tuna` | 낚시 수확 성공 | DESIGN 8.4 (참치) |
| 낚시 | 31 | `item.stingray` | 낚시 수확 성공 | DESIGN 8.4 (가오리) |
| 낚시 | 32 | `item.rainbow_trout` | 낚시 수확 성공 | DESIGN 8.4 (무지개송어) |
| 낚시 | 33 | `item.redfish` | 낚시 수확 성공 | DESIGN 8.4 (점성어) |
| 낚시 | 34 | `item.black_grouper` | 낚시 수확 성공 | DESIGN 8.4 (검은 참바리) |
| 낚시 | 35 | `item.arctic_char` | 낚시 수확 성공 | DESIGN 8.4 (북극 곤들매기) |
| 낚시 | 36 | `item.swordfish` | 낚시 수확 성공 | DESIGN 8.4 (황새치) |
| 낚시 | 37 | `item.misty_grouper` | 낚시 수확 성공 | DESIGN 8.4 (잿빛 참바리) |
| 낚시 | 38 | `item.salmon_shark` | 낚시 수확 성공 | DESIGN 8.4 (악상어) |
| 낚시 | 39 | `item.axolotl` | 낚시 수확 성공 | DESIGN 8.4 (우파루파) |

**사냥 칸은 종 ID(`animal.*`)이고 농사·낚시 칸은 아이템 ID(`item.*`)다** — 한 줄로 통일하지 않은 이유는 채우는 사건이 다루는 값이 원래 다르기 때문이다. 포획이 다루는 것은 동물 종(`DT_AnimalTable`)이고, 수확이 다루는 것은 생산 아이템(`DT_ItemTable`)이다. 억지로 한 종류로 맞추면 `animal.pig` 와 `item.pet_pig` 를 섞어 쓴 M8-04 의 사고(`docs/ANALYTICS.md` 표 A 끝줄)를 그대로 반복한다.
**39칸이 전부 차면 탈것 탑승 가능**(DESIGN 8.3) — 그 판정은 M10-06 이 세운다. **칸 수는 이 문서에도 코드에도 상수로 박지 않는다** — 세 표의 행 수 합이 곧 칸 수고(DESIGN 8.5), 위 표는 그 합을 이 시점에 펼쳐 본 사본이다. 낚시 21줄은 사용자 지시 2026-09-23 으로 3 → 21종이 된 결과이고(DESIGN 8.4), 사냥 15줄은 같은 날 동물 전면 교체로 바르코 12종이 더해진 결과다(DESIGN 8.7). **자리표시 돼지·소·양 3줄은 Quaternius 에셋을 지우는 M5-07 마지막 조각에서 빠져 36칸이 된다.** **M13-01 에서 작물이 3 → 27종(DESIGN 13.3)이 돼 60칸이 됐다** — 위 표는 그 앞 시점 사본이다.

## B. 도감 데이터 모양 (Player Data `codex`)

| 무엇 | 값 | 왜 |
|---|---|---|
| 저장소 | PlayFab **Player Data** 키 `codex` (`ranch` 와 같은 자리 — `docs/BACKEND.md` 데이터 모델 표) | 재화가 아니라 진행도다. 인벤토리·가상 재화와 섞지 않는다 |
| 모양 | 채워진 칸의 ID 만 담은 **집합**(중복 없는 배열) | "몇 칸 찼나" 는 길이로 답이 나오고, 같은 종을 두 번 잡아도 늘지 않는다 |
| 채우는 쪽 | **백엔드만** — 기존 성공 판정에 한 줄씩 얹는다(표 C) | 클라이언트가 "다 모았다"고 우기면 탈것이 공짜가 된다 (DESIGN 4.8 · CLAUDE.md 8) |
| 읽는 쪽 | 클라이언트는 스냅샷으로 **읽기만** 한다 | 인벤토리 캐시(M2-05)와 같은 규칙 — 응답 전에는 표시만 하고 로컬에서 칸을 채우지 않는다 |
| 완성 판정 | 배열 길이가 **세 표의 행 수 합**(지금 39) | 클라이언트가 세는 것이 아니라 **백엔드 스냅샷의 길이를 읽는다**(M10-06). 상수 39 를 박으면 어종·동물이 또 늘 때 코드가 따라 는다 |

**새 엔드포인트를 만들지 않는다**(결정 007) — 도감 전용 CloudScript 함수(`AddCodexEntry` 같은 것)를 두면 그것이 곧 "클라이언트가 칸을 채우는 길" 이 된다.

## C. 칸을 채우는 자리 — 세 곳뿐 (DESIGN 8.5)

| 카테고리 | 백엔드 성공 판정 | 지금 그 자리 | 조각 |
|---|---|---|---|
| 사냥 | `RegisterPet` — **포획 함수가 아니다.** `TryCaptureTargetOnAuthority()`(`Private/Combat/CaptureComponent.cpp`)는 호스트 권위일 뿐 CloudScript 가 아니라 백엔드가 그 순간을 모른다 | `docs/BACKEND.md` 함수 표에 한 줄 섰다 — 성공하면 `animalId` 를 `codex` 에 추가. 대가: 잡고 목장에 넣어야 찬다 | **명세 끝(M10-04)** · 구현은 M2-03(콘솔) |
| 농사 | `HarvestRanch` — 작물 슬롯도 목장 슬롯과 한 배열이라 같은 함수가 맡는다 | `docs/BACKEND.md` 함수 표에 한 줄 섰다 — n ≥ 1 이면 그 칸의 생산물 ID 를 추가. 작물 전용 함수 이름은 아직 없다(M2-03 몫) | **명세 끝(M10-04)** · 슬롯은 M6-05 |
| 낚시 | `HarvestFishing` | `docs/BACKEND.md` 함수 표에 `fishing` 키와 함께 섰다(M10-03) — n ≥ 1 이면 어종 ID 를 추가(M10-04). 슬롯은 `URanchComponent` 낚시터로 이미 있다(M10-02) | **명세 끝(M10-03·04)** · 구현은 M2-03(콘솔) |

**"지금 그 자리" 는 코드를 넣을 곳을 가리킬 뿐, 도감 코드가 있다는 뜻이 아니다.** M10-03·04 는 **문서만** 쓴다 — PlayFab 콘솔에 CloudScript 를 넣는 것은 M2-03(사람 몫)이다.

## D. 지금 있는 것 / 아직 없는 것

| 부위 | 지금 있는 것 | 아직 없는 것 |
|---|---|---|
| 낚시 데이터 | **섰다(M10-01 · 루프#152 가 21행으로)** — `DT_FishingTable` 21행 + `DT_ItemTable` 어류 21행. 둘 다 `tools/unreal/create_data_tables.py` 를 `-run=pythonscript` 로 돌려 만든다 | 없음 — 2층 `Snarehaven.Fishing.Table.MatchesDesign` 이 DESIGN 8.4 를 읽어 맞댄다(`DT_FishingTable 행 21개를 본다`) |
| 낚시 슬롯 | **없다.** 빌릴 짜임은 있다 — `URanchComponent`(`Source/Snarehaven/Private/Ranch/RanchComponent.cpp`: `ApplyBackendSnapshot:36` · `QueueRegisterPet:73` · `QueueHarvest:112`)와 `FRanchSlotState`(ID·`OccupiedSinceUtc`·`Stock` 세 칸) | 낚시터 슬롯 5칸 컴포넌트 (M10-02) |
| 낚시 백엔드 | **명세는 섰다(M10-03)** — `docs/BACKEND.md` 에 Player Data `fishing` 키와 `RegisterFishingSpot`/`HarvestFishing` 함수 두 줄 | **올라간 코드**가 없다 — `tools/playfab/cloudscript_main.js` 에 넣어 콘솔에서 live 로 올리는 것은 M2-03(사람 몫) |
| 작물 | **없다** — 숫자만 확정(DESIGN 8.2), 슬롯·아이템은 M6-05 몫 | 도감 농사 3칸을 실제로 채우려면 M6-05 가 먼저 서야 한다 |
| 도감 데이터 | **명세는 섰다(M10-04)** — `docs/BACKEND.md` 데이터 모델 표에 `codex` 키 한 줄, 채우는 자리 셋도 함수 표 안에 | **올라간 코드**가 없다 — 같은 M2-03 몫 |
| 도감 읽는 길 | **섰다(M10-05)** — `FSnarePlayerSnapshot::CodexEntries`(`Source/Snarehaven/Public/Backend/SnarePlayerSnapshot.h`, 중복 없는 `TArray<FName>`). 캐시 형식은 3 으로 올렸다(`USnarePlayerCacheSave::CurrentFormatVersion`) — 2 로 쓰인 캐시는 도감이 빈 채로 실려 다 모은 사람이 0/27 로 보인다 | **채워 보낼 백엔드 코드**가 없다 — 같은 M2-03 몫이라 지금은 늘 빈 배열이 온다 |
| 도감 화면 | **섰다(M10-05)** — `UCodexScreenWidget`(`Source/Snarehaven/Public/UI/CodexScreenWidget.h`). 39칸은 **코드에 적혀 있지 않다**: `DT_AnimalTable`·`DT_CropProductTable`·`DT_FishingTable` 에서 읽고 이름만 `DT_ItemTable` 에서 가져온다. 백엔드에게 못 들었으면 `0 / 27` 이 아니라 `- / 27`. 줄이 30개(머리글 3 + 칸 27)라 **세로 칸 셋으로 나눠 담는다**(루프#152 — 한 줄로 쌓으면 캡처에서 아래가 잘린다) | **게임 안 입구**가 없다 — 여는 길은 검사 스위치 `-SnareCodexScreen` 뿐(M10-06 이 탑승 게이팅과 같이 낸다) |
| 탈것 | **없다** — ROADMAP M6-06 이 M10-06 을 기다린다 | 탑승 게이팅 판정 (M10-06) → 메시·속도는 그 뒤 별도 질문 |

**목장 슬롯도 아직 백엔드에서 채워지지 않는다** — `URanchComponent::ApplyBackendSnapshot` 을 부르는 곳은 2층 검사뿐이고(`SnarehavenRanchHudTest.cpp`), 실제 응답을 옮기는 코드는 `ranch` 키를 읽는 자리와 함께 M2-03 뒤에 선다. 낚시 슬롯(M10-02)도 같은 상태로 서는 것이 정상이다 — 2층까지 세우고 3층 실데이터는 M2-03 뒤다.

## E. 이후 조각이 고칠 자리

| 조각 | 무엇을 | 이 문서의 어디를 고치나 |
|---|---|---|
| M10-01 | `DT_FishingTable` 21행 + 어류 아이템 21행 (루프#152 가 3 → 21) | 표 D 낚시 데이터 줄 |
| M10-02 | 낚시터 슬롯 5칸 (목장 짜임 재사용) | 표 D 낚시 슬롯 줄 |
| M10-03 | `docs/BACKEND.md` 에 `fishing` 키·함수 둘 | 표 C 낚시 줄 · 표 D 낚시 백엔드 줄 |
| M10-04 | `docs/BACKEND.md` 성공 판정 셋에 codex 한 줄씩 | 표 C 전체 |
| M10-05 | 도감 화면 39칸 진행률 | 표 D 도감 읽는 길·화면 줄 |
| M10-06 | 탑승 게이팅(**세 표의 행 수 합**과 맞댄다 — 지금 39) | 표 D 탈것 줄 · ROADMAP M6-06 |
| M6-05 | 작물 슬롯·아이템 3종 | 표 D 작물 줄 (도감 농사 3칸의 전제) |
