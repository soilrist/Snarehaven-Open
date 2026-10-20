# 백엔드 — PlayFab (결정 2026-09-18)

> 재화가 걸린 판정(코인·젬 잔액, 인벤토리 원본, 목장 타이머·수확, 가챠, IAP 영수증)은 전부 여기서. 근거는 `docs/DESIGN.md` 4.1 권위 2단 표·4.8.

## 왜 PlayFab
- 공식 Unreal C++ SDK(마켓플레이스 플러그인) — 자체 HTTP 래퍼 안 짬
- 디바이스 ID 로그인 내장(LoginWithIOSDeviceID / LoginWithAndroidDeviceID) — 게스트 계정 요구(DESIGN 4.4)와 일치
- 플레이어 데이터 · 인벤토리 · 가상 재화 · CloudScript(서버 로직) · Google/Apple IAP 영수증 검증이 한 서비스에 있음 — 가챠·IAP 단계(DESIGN 10.5)에 추가 서버 없음
- 무료 티어(Development Mode)로 시작

## 🔴 설치 세션에서 못 한 것 (사람 또는 다음 감독 세션)
- [ ] 무료 티어 한도·요금제를 공식 요금 페이지에서 확인해 여기 표로 기록 (이 세션은 웹 조회 불가였음)
- [x] PlayFab 타이틀 생성 + Title ID 발급 → `Config/PlayFab.ini`(gitignore 됨)에 기록 완료(2026-09-18). 형식: `[/Script/PlayFab.PlayFabRuntimeSettings]` 아래 `TitleId=`. Secret Key 는 파일에도 안 적음(사람 관리). 플러그인 설치(M2-01) 때 이 파일을 로드하도록 배선
- [x] Unreal 플러그인 설치 경로 확정 — GitHub `PlayFab/UnrealMarketplacePlugin`(Apache-2.0, Fab 계정 불필요, `5.x/PlayFabPlugin/PlayFab` 폴더를 `Plugins/PlayFab/` 로). 감독 결정(2026-09-19). **내려받기는 사람 승인 뒤 감독이** 한다 — 루프는 안 받는다. UE 5.8 지원 여부는 받을 때 `PlayFab.uplugin` 의 EngineVersion 으로 확인
- [x] 플러그인 설치 완료(감독, 2026-09-20, 사용자 일괄 위임) — `PlayFab/UnrealMarketplacePlugin` 커밋 `8c6fb49`(2026-08-07, VersionName `1.192.260805`), **`5.7/PlayFabPlugin/PlayFab` 폴더**를 `Plugins/PlayFab/` 로(저장소에 5.8 폴더가 없음 — uplugin 에 EngineVersion 항목 없고 모듈 3개 `PlayFabCpp`·`PlayFab`·`PlayFabCommon` 은 소스라 5.8 에서 컴파일은 M2-01 1층이 확인). `LICENSE`(Apache-2.0) 동봉. 5.8 폴더가 올라오면 통째로 교체
  - **UE 5.8 에서 컴파일하려고 플러그인 안을 고친 것(루프#46, 2026-09-20).** 원인은 전부 하나 — UE 5.8 이 `FJsonObject` 의 키 타입을 `FString` 에서 `UE::FSharedString` 으로 바꿨다(`Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:99·237`). 엔진이 주는 우회 매크로 `UE_JSONOBJECT_LEGACY_STRING_KEYS=1` 은 **쓰지 않았다** — 그 매크로는 `FJsonObject` 의 베이스 클래스 자체를 갈아서(같은 헤더 :323~327) 이미 non-legacy 로 빌드된 엔진 `Json` 모듈과 레이아웃이 어긋난다. 그래서 플러그인 쪽을 5.8 API 에 맞췄다:
    | 고친 파일 | 자리 | 무엇을 | 왜 |
    |---|---|---|---|
    | `Source/PlayFabCpp/Private/Core/PlayFab*DataModels.cpp` (16개) | 646곳 | `TMap<FString, TSharedPtr<FJsonValue>>::TConstIterator It(X->Values)` → `auto It = (X->Values).CreateConstIterator()`, 그 블록 안 `It.Key()` → `FString(*It.Key())` | 반복자 타입을 이름으로 박아 둬서 키 타입이 바뀌자 안 맞는다. `CreateConstIterator()` 는 맵이 알아서 고른다 |
    | `Source/PlayFabCpp/Private/Core/PlayFabCppBaseModel.cpp` | 1곳 | `WriteIdentifierPrefix(Elem.Key)` → `FString(*Elem.Key)` | `TJsonWriter::WriteIdentifier` 에 `FSharedString` 오버로드가 없다 |
    | `Source/PlayFabCpp/Private/Core/PlayFabResultHandler.cpp` | 2곳 | `ErrorDetails.Add(val.Key, …)` → `Add(FString(*val.Key), …)` | 받는 쪽이 `TMap<FString, FString>` |
    | `Source/PlayFab/Private/PlayFabJsonObject.cpp` | 1곳 | `Values.GetKeys(TArray<FString>&)` → 직접 순회하며 `FString(*It.Key())` 담기 | `GetKeys` 는 맵의 키 타입 그대로만 받는다 |
    | `Source/PlayFab/Classes/PlayFabEnums.h` | 1곳 | `RemoveAt(0, 7, false)` → `RemoveAt(0, 7, EAllowShrinking::No)` | 위 JSON 과 무관한 별건 — `FString::RemoveAt` 의 bool 인자가 `EAllowShrinking` 로 바뀌었다 |

    전부 기계적 치환이고 PlayFab 의 로직은 건드리지 않았다. 5.8 폴더가 올라오면 이 수정째로 통째 교체한다.

## 데이터 모델 (MVP)
| 저장소 | 키 | 내용 |
|---|---|---|
| Player Data | `ranch` | `[{ "slot":0, "animalId":"animal.pig", "occupiedSinceUtc":"...", "stock":0 }, ... ]` 5칸. `animalId` 는 `DT_AnimalTable` 의 `AnimalID` **그대로**(`animal.pig`) — 2026-09-22 배포 검증에 쓴 `pig` 는 표가 서기 전의 짧은 자리표시였다 |
| Player Data | `fishing` | `ranch` 와 **같은 배열 구조**, 점유자 이름만 다르다 — `[{ "slot":0, "fishItemId":"item.crucian_carp", "occupiedSinceUtc":"...", "stock":0 }, ... ]` 5칸 (DESIGN 8.4) |
| Player Data | `codex` | 채워진 도감 칸의 종 ID 만 담은 **집합**(중복 없는 배열) — `["animal.pig", "item.carp", ... ]` 최대 9칸 (DESIGN 8.5 · `docs/CODEX.md` 표 B). 길이가 9 면 탈것 탑승 가능(8.3) |
| Internal Data | `starterKit` | (Internal Data — 다른 키처럼 `saveData` 로 적는다. 10-16 표 정정: 예전 표에 Player Data 로 잘못 적혀 있었다) 시작 장비 이미 받았다는 표시 — `{ "grantedAtUtc":"...", "items":[{ "itemId":"item.club", "count":1 }] }` (DESIGN 3.5 「시작 장비 지급」). **있으면 그 자체로 "이미 받음"** 이라, 키가 있는지만 보고 내용은 응답에 실어 보여 줄 뿐이다 |
| Internal Data | `base` | 텐트 배열 `[{ tentId, x, y, placedAtUtc, upgraded, upkeepUntilUtc, color, items:[{ structureId, itemId, x, y, rot, pin?, storage? }] }]` 최대 2개 (DESIGN 8.11). 기한 넘긴 텐트는 **다음 호출이 읽을 때** 지운다 |
| Internal Data | `drops` | 떨군 가방 `[{ dropId, items:[{itemId,count}], droppedAtUtc }]` — 600초 지난 것은 읽을 때 빠진다 (DESIGN 8.12) |
| Internal Data | `blueprints` | 배운 설계도 ID 배열 `["item.blueprint_grill", ...]` (DESIGN 8.10 ★ · 8.3 100% 보상) |
| Internal Data | `fishRoll` | 마지막 낚시 표 `{ token, fishItemId, spotId, used, lastRollUtc, lastCatchUtc }` — 표는 한 장만 산다 (DESIGN 8.13) |
| Internal Data | `achievements` | `{ counters:{ capture, hunt, fish, harvest, craft, house, codexPercent }, done:["ach.capture_1", ...] }` (DESIGN 8.15). 새 엔드포인트 없이 ClaimLoot·CatchFish·HarvestFishing·HarvestRanch·CraftItem·UpgradeHouse·RegisterPet·BuyItem(설계도) 성공 뒤 `bumpAchievements` 가 올리고, 새로 찬 업적의 코인(SC)을 **`done` 저장 뒤** 지급한다. 도감처럼 클라가 읽어 오는 호출은 아직 없다 — `FSnarePlayerSnapshot.Achievements` 칸만 있다 |
| Internal Data | `coop` | `{ hostId, atUtc }` — 마지막 `ReportCoop`(M21-06b). 남이 같은 방인지 셀 때 서버가 이 키를 읽는다 |
| Internal Data | `boss` | `{ lastWeek, hatGranted }` — 마지막 `ClaimBoss` 주 번호(UTC 월요일 경계)와 우두머리 모자를 받은 적 (M21-07d, DESIGN 10.6) |
| Internal Data | `boat` | `{ launched, launchedAtUtc }` — 띄운 구명보트 (DESIGN 8.9) |
| Internal Data | `ranchMeta` | **Revision 9 배포(09-24).** `{ upkeepUntilUtc, fame, slotCap }` — 목장 유지 기한·명성·연 칸 수 (DESIGN 3.2 **유지비·명성**). 없으면 `{null, 0, 5}`. `ranch` 배열 길이는 `slotCap` 까지 늘어난다 |
| Internal Data | `lootAt` | `{ atMs, day, n, coins }` — 마지막 `ClaimLoot` 시각(10초 간격), 그날(UTC) 받은 개수 `n`(상한 `LOOT_DAILY_MAX`), 재료·재료 가공품 판 코인 `coins`(상한 `LOOT_SELL_DAILY_COINS`). 옛 꼴(숫자 = 시각 ms)도 읽는다 (DESIGN 4.8) |
| Internal Data | `pinFails` | 주인 계정에 보관함별 `{ structureId: { n, untilMs } }` — 틀린 PIN 횟수와 잠금 끝 시각. 5번째에 10분 잠금, 맞으면 지움 (DESIGN 4.8) |
| Internal Data | `lock<이름>` | `{ token, untilMs }` — 지급 함수 처리 중 표시(`lockStarter`·`lockRanch`·`lockGacha`·`lockQuest`·`lockAttendance`·`lockBoss`·`lockLoot`·`lockDrops`·`lockReport`·`lockBlock`). 살아 있으면 같은 함수 두 번째 호출은 `busy`, 끝나면 지운다, 15초 뒤엔 무시 (DESIGN 4.8) |
| Internal Data | `reportDay` | `{ day, targets:[PlayFabId] }` — 오늘(UTC) 내가 신고한 사람. 같은 사람 하루 1번, 하루 10명(`REPORT_DAILY_MAX`, DESIGN 4.8) |
| Internal Data | `blocks` | `[{ id, name, atUtc }]` — 내 차단 목록(최대 100, `BLOCK_MAX`). 내 화면에서만 이름을 가리고 이모트·말풍선을 숨긴다 |
| Title Internal Data | `testerIds` | `GrantTesterKit` 를 받을 PlayFab ID JSON 배열. **값은 저장소·문서에 적지 않는다** — 게임 관리자에서 감독이 넣는다. 없거나 깨지면 아무도 못 받는다 |
| Virtual Currency | `SC` | 스네어 코인 — 타이틀 등록 2026-09-24(초기 지급 0, 시작 코인은 `GrantStarterKit`) |
| Virtual Currency | `GM` | 젬 (IAP 단계에서 활성) — 타이틀 등록 2026-09-24(초기 지급 0) |
| Inventory | 아이템 | 재료·생산 아이템·펫 |

## CloudScript 함수 (MVP)
| 함수 | 입력 | 판정 |
|---|---|---|
| `GrantStarterKit` | (없음) | `starterKit` 키가 **없을 때만** DESIGN 3.5 표의 시작 장비(`item.club` 1개)를 지급하고 **스네어 코인 5**(DESIGN 8.14 「시작 코인」, 같은 첫 1회)를 더한 뒤 표시를 적는다. 이미 있으면 `granted:false` 를 돌려준다(오류가 아니다). **지급이 먼저, 표시가 나중** — 표시를 먼저 적으면 지급이 실패한 계정이 영영 못 받는다 |
| `GrantTesterKit` | (없음) | **Rev 60 배포(10-17), v6 키트.** 기기 시험용 — 출시(TestFlight) 빌드도 로그인 뒤 한 번 부른다(판정은 서버). 타이틀 Internal Data `testerIds`(값은 문서에 안 적는다) 밖이면 `not_tester`. `testerKit.version` 이 `TESTER_KIT_VERSION` 이상이면 `{granted:false}`. 아니면 `TESTER_KIT`(v6: 총·탄은 `GUNS`, 먹는 것은 `RECIPES` heal·`RAW_HEAL`, 꾸미기는 꾸미기 표에서 모은 353줄·2115개, 로켓 30·탄 200)을 가진 만큼 빼고 한 번 600개까지 이어 주고, 다 준 뒤 표시·코인 500·탈것 8종 전부를 `mounts.species` 에 넣어 `{granted:true, items, coins, mounts}`. 키트를 바꾸면 버전을 올린다 |
| `RegisterPet` | animalId, slot, variant? | 목장 생산 표(DESIGN 3.2, `ANIMAL_PRODUCT_ITEM`)에 없는 종이면 `bad_args`, 그 종의 펫 아이템(`item.pet_<종>`)이 없으면 `not_enough_items` — 확인이 다 끝난 뒤 **펫 1개를 빼고**(#0190) 슬롯에 기록, `occupiedSinceUtc = now`. 계정 첫 펫(유지 시작 전·`ranchMeta.firstBatchUsed` 없음)이면 칸에 `firstBatch: true` — 첫 단위가 1분(`FIRST_BATCH_MINUTES`, DESIGN 3.2)에 서고 거두면 표시를 지운다(M21-01). **성공하면 `animalId` 를 `codex` 세트에 추가**(이미 있으면 그대로 — 집합이다). **M21-07b(Rev 39 배포 10-03):** `variant: true` 면 `item.pet_<종>_variant` 를 빼고(변종 없는 자리표시 돼지·소·양은 `bad_args`) 칸에 `variant: true` — 주기 ÷`VARIANT_CYCLE_DIVISOR` 1.5(30 → 20분, DESIGN 10.6), 생산물은 같은 종. 답에 `variant` |
| `HarvestRanch` | slot | `floor((now - occupiedSinceUtc) / 30min)` 을 상한 5 로 자르고 아이템 n개 + 코인 10n 지급, 타임스탬프를 `now - 나머지` 로 갱신 — **n 이 상한이면 나머지 0**(상한에서 생산이 멈춤, DESIGN 3.2 · 루프#1433 — 이월하면 며칠 둔 칸이 거두자마자 다시 5/5). **n ≥ 1 이면 그 칸의 생산물 ID 를 `codex` 세트에 추가** · **M109-04a(루프#1444, 배포 전):** 코인은 낚시터와 합쳐 하루 `HARVEST_DAILY_COINS` 900 까지(Internal Data `harvestCoins` `{day, coins}`), 넘으면 생산물만 — 답에 `coinsCapped`·`coinsLeftToday` |
| `RemovePet` | slot | **Rev 37 까지 배포됨(#0198 — `7b81396e` 판에 `handlers.RemovePet`).** 목장 칸의 펫을 빼 `item.pet_<종>` 1개로 돌린다. 슬롯이 정수 `0..slotCap-1` 밖이면 `bad_slot`, 펫이 없는 칸(빈 칸·작물 칸)이면 `empty_slot`, 유지가 끊겼으면 `locked`, 쌓인 생산물 자리가 가방에 없으면 `inventory_full` — 확인이 다 끝난 뒤 **쌓인 몫을 `HarvestRanch` 와 똑같이 수확**(생산물·코인·명성·도감·업적) → 펫 1개 지급 → `ranch[slot] = null`. `{removed, slot, animalId, petItemId, harvested, itemId, coins, fame}` (DESIGN 3.2 🟡). **M21-07b(Rev 39 배포 10-03):** 변종 칸이면 `item.pet_<종>_variant` 를 돌려주고 답에 `variant` |
| `GetTreasure` | (없음) | **Rev 37 까지 배포됨(#0200 — `7b81396e` 판에 `handlers.GetTreasure`).** 찾는 보물을 서버 기록 `treasure` 에서 돌려준다 — 존을 옮겨 캐릭터가 새로 서도 상자·미니맵 점을 다시 세우는 용도. 읽기만 하고 안 지운다. 마감이 지났으면 그때만 기록을 지우고 `{treasure:null}`, 없으면(연 뒤 포함) `{treasure:null}`, 있으면 `{treasure:{treasureId, zoneId, u, v, expiresUtc}}` (DESIGN 13.8 🟡) |
| `PlantCrop` | slot, seedItemId | **Revision 11 배포(09-24).** 거점 작물 칸에 씨앗 1개를 심는다. `item.seed_<작물>` → `item.<작물>` 이 DESIGN 8.2 재배 표에 없으면 `bad_args`, 슬롯이 `slotCap` 밖이면 `bad_slot`, 펫·작물이 선 칸이면 `slot_full`, 목장 유지가 끊겼으면 `locked`, 씨앗이 없으면 `not_enough_items` — 확인이 다 끝난 뒤 씨앗 1개를 빼고 `ranch[slot] = {slot, cropItemId, occupiedSinceUtc: now, stock: 0}`. 첫 배치면 유지 24시간 시작(`RegisterPet` 과 같다). 도감은 여기서 안 올린다(수확 때). `{success, slot, cropItemId, seedItemId}` |
| `HarvestCrop` | slot | **Revision 11 배포(09-24).** 원본(레퍼런스 게임)대로 **한 번 거두면 칸이 빈다** — 쌓임·상한·이월 없음(DESIGN 8.2). 작물이 없는 칸(빈 칸·펫 칸)은 `empty_slot`, 잠겼으면 `locked`, 심은 뒤 작물별 재배 시간(당근 15분·감자 30분·밀 60분)이 안 지났으면 `{harvested:0, message}`. 다 자랐으면 작물 1개 + 작물별 코인(5·10·20), 명성 +1, `codex` 에 작물 ID, 그리고 `ranch[slot] = null`. 다시 심으려면 `PlantCrop` 으로 씨앗 1개가 또 든다. `{harvested:1, itemId, coins, fame}` |
| `RegisterFishingSpot` | fishItemId, slot | `RegisterPet` 미러 — `fishing` 키의 그 슬롯이 비었으면 기록, `occupiedSinceUtc = now`. `fishItemId` 는 DESIGN 8.4 어종 표에 있는 이름만 받는다(모르는 이름이면 거절) |
| `HarvestFishing` | slot | `HarvestRanch` 미러 — 다만 나누는 수가 30분 고정이 아니라 **어종별 낚시 시간**이다: `floor((now - occupiedSinceUtc) / 낚시 시간)` 을 상한 5 로 자르고 아이템 n개 + 어종별 코인 × n 지급, 타임스탬프를 `now - 나머지` 로 갱신(n 이 상한이면 나머지 0 — 동물 칸과 같다, 루프#1433). **n ≥ 1 이면 그 칸의 어종 ID(`fishItemId`)를 `codex` 세트에 추가** · M109-04a 코인 하루 상한은 `HarvestRanch` 와 한 통 |
| `GetBaseState` | (없음) | **Revision 7 배포(09-24).** 읽기 전용 — 내 `ranch`·`fishing`·`base`(텐트)·`boat` 저장값을 그대로 `{ranch, fishing, tents, boat}` 로 돌려준다(없으면 빈 칸 5개·`[]`·`{launched:false}`). 판정·지급·만료 텐트 정리 없음 — 단 **작물 칸 `stock` 은 저장값이 아니라 다 자랐으면 1·아니면 0 으로 채워 보낸다(⑩, 저장 안 함)**. 목장 HUD 가 시각 계산 없이 `다 자람` 을 띄우는 값이다. 클라이언트 `URanchSyncComponent` 가 로그인 뒤 한 번, 목장·낚시터 요청 성공 뒤마다 불러 칸·텐트·배를 되살린다. **Rev 37 까지 배포됨(#0199):** 응답에 `hat` — 기록된 모자를 지금도 가지고 있으면 그 ID, 아니면 `""`(재접속 모자 복원) |
| `RollGacha` (10.5 단계) | currency | 확률·천장·중복 환급 계산 |
| `GetCodexRewards` | (없음) | **Revision 7 배포(09-24).** `codex` 를 다섯 카테고리(낚시·사냥·식물·설계도·요리)로 세어 `{categories, rewards:{farSea, dangerDock, specialMerchant}, entries}` — 낚시 50% 먼바다, 사냥+식물 50% 위험구역 선착장, 한 카테고리 100% 특별 상인 (DESIGN 8.3). `entries` = 찬 칸 목록(칸 밖 ID 뺌) → 클라 스냅샷 `CodexEntries`(M13-26, Revision 26 배포 09-24). `achievements` = 업적 저장값 그대로 `{counters, done}` → 클라 스냅샷 `Achievements`(M13-28, Revision 27 배포 09-25). **M21-07b(Rev 39 배포 10-03):** 여섯째 갈래 `categories.variant`(변종 펫 12칸, `ClaimLoot` 가 받을 때 찍음) — `CODEX_REWARD_EXCLUDED` 라 특별 상인·목줄·업적 도감 % 에 안 센다(DESIGN 10.6 「변종 도감」) |
| `CraftItem` | recipeId, times | **Revision 7 배포(09-24).** 설계도(★)·작업대(텐트) 확인 → 재료 전부 확인 → 칸 확인 → 재료 빼고 결과 지급. 태그 재료(`Meat`·`Hide`·`Feather`)는 싼 것부터, 가챠 소재는 맨 나중. 불 요리는 `codex` 에 추가 (DESIGN 8.10). **Rev 38 배포(10-03, M22 ⑥, 루프#311).** 수지 접착제·면 원단·밀가루·설탕·식물성 오일·티백 `place` workbench → anywhere — 배포 전 라이브는 텐트 없으면 `locked` |
| (기존 상수 변경) | | **Rev 37 까지 배포됨(M13-09, #0194 — `7b81396e` 판에 `item.resin_adhesive`·`item.life_jacket`).** `RECIPES` +5(13.8 작업대) — 수지 접착제 = 수지 2 · 면 원단 = 목화 3 · 방수포 = 면 원단 2 + 접착제 1 · 구명조끼 = 면 원단 2 + 방수포 1 · 텐트 = 방수포 4 + 나무판 6. 새 함수 없음. 카탈로그 **+4**: `item.resin_adhesive` · `item.cotton_fabric` · `item.tarp` · `item.life_jacket`(`item.tent` 는 이미 있다) |
| `ConsumeItem` | itemId | **Revision 7 배포(09-24).** 회복량이 있는 요리만 1개 빼고 `{heal}` — 체력은 응답 뒤 호스트가 채운다 |
| (기존 함수 변경) | | **Rev 37 까지 배포됨(M13-08, #0193 — `7b81396e` 판에 `typeof heal`·`item.energy_drink_mild`).** `ConsumeItem` — 회복량이 **0 이어도** 표에 있으면 먹는다(`typeof heal !== "number"` 만 거절 — 순한맛 드링크·핫팩처럼 버프만 있는 것). 버프(값·180초)는 응답 뒤 호스트가 `DT_ItemTable` 버프 칸으로 건다. `RECIPES` +6(드링크 3·커피 병·차 병 = 불, 생선 통조림 = 작업대 `Fish` 2) · `RAW_HEAL` +3(핫팩 0·에너지바 25·수통 20) · `SHOP_ITEMS` +3(핫팩 5·따뜻한 탄산 3·에너지바 3) · `sellPriceOf` 목장 생산물 2·낙각 3(DESIGN 8.14 — 표와 맞춤). 카탈로그 +9: `item.energy_drink_mild` · `item.energy_drink` · `item.energy_drink_strong` · `item.coffee_bottle` · `item.tea_bottle` · `item.heat_pack` · `item.snackbar` · `item.water_canister` · `item.canned_fish` (`item.hot_soda` 는 이미 라이브) — DESIGN 13.6 |
| `LoadAmmo` | gun, have | **Rev 53 라이브(10-14, 원래 M39-02, 루프#573).** 재장전 — `GUNS`(DT_ItemTable `MagazineSize`·`AmmoItemID`, node 시험이 맞댐)에 없는 총이면 `bad_args`, 총이 없으면 `no_gun`. `have` 는 0..탄창으로 자르고 `min(탄창 - have, 가진 탄약)` 발을 빼 `{gun, loaded, removed, left}` — 탄창이 덜 찼는데 탄약 0 이면 `not_enough_items`, 찼으면 0발 빼고 성공. 쏘기는 서버를 안 부른다 — 클라는 응답 `loaded` 로만 탄창을 채운다(DESIGN 8.6-c · 규칙 8) |
| `DropItem` | itemId, count | **Revision 7 배포(09-24).** 펫·가챠는 거절. 빼고 떨군 사람 `drops` 에 가방 기록, `{dropId, expiresAtUtc}` (600초) |
| `DropOnDeath` | (없음) | **Revision 7 배포(09-24).** 배낭 아이템 전부를 가방 하나로. 펫·가챠·코인·젬·도감·목장은 남는다 (DESIGN 8.12) |
| `PickupDrop` | ownerId, dropId | **Revision 7 배포(09-24).** 줍는 사람에게 먼저 지급하고, 성공해야 주인 `drops` 에서 지운다(지급 거절 = `grant_failed` + PlayFab 오류 이름, 가방 그대로 — 루프#567. `MoveFromStorage`·`RemoveStructure`·`StowBoat` 도 같은 순서) — 한 번만(두 번째는 `already_taken`), 600초 지나면 `expired`, 칸 없으면 가방이 그대로 남는다 |
| `PlaceTent` | x, y | **Revision 7 배포(09-24).** 텐트 1개 빼고 `base` 에 추가. 2개 한도·내 땅(16×16) 겹침 거절. 응답에 `upgradeDeadlineUtc`(24시간) (DESIGN 8.11) |
| `PlaceStructure` | tentId, itemId, x, y, rot, pin? | **Revision 7 배포(09-24).** 그 텐트 땅 안·빈 칸·가구 40 이하만. 보관함은 PIN 5자리 선택. **Rev 38 배포(10-03, M20-04a 루프#314).** `PLACEABLE_ITEMS` +3 `item.solar_kit`·`item.battery`·`item.lamp_electric` · `RECIPES` +1 전등(작업대 전구 1 + 금속 부품 1 + 나무판 1) · `FIXED_SELL_PRICE` 전등 5. 카탈로그 +1 `item.lamp_electric` (DESIGN 8.11 「집 2층·전력」) **M108-02(미배포):** 벽 장식(`item.picture_frame`·`wall_clock`·`wall_shelf`)은 `wall: {side 0~3, h 120~200}` 필요 — 집(`upgraded`) 3×3 바깥 변 칸에만(`not_house`·`not_wall`), 같은 칸·방향 높이 50 이상 벌어져야 함(`occupied`), 한도에서 0.5개. **M108-03(미배포):** 소품(`item.mug`·`flower_pot`·`candle`)은 `top: {host: structureId, slot}` 필요 — 받침은 나무 탁자(슬롯 0~2)·보관함(0~1)만(그 밖 `not_host`, 없는 받침 `not_found`), 같은 받침·슬롯 둘은 `occupied`. 저장 자리·돌림·반 칸은 받침 것(보낸 x,y 는 땅 안 판정에만), 한도에서 0.5개. **M108-06a(미배포):** 계단 `item.stairs_wood`(1×3, 돌림 90° 단위·온 칸만)은 2층 집(`floors: 2`)에만(`not_floor2`), 집 하나 1개(`stair_exists`), 집 3×3 안(`out_of_house`), 문 길·방을 안 막는 6 자리만(`blocks_path` — X 로 긴 왼·오른 줄 · Y 로 긴 뒷줄), 1층 바닥 가구 칸 덮으면 `occupied`(러그는 밑에 깔림). `floor: 2`(선택, 기본 1) = 2층 바닥 — 바닥 가구·러그만(`bad_args`), 계단 있어야(`no_stairs`), 집 3×3 안(`out_of_house`), 계단 구멍 칸 빼고(`stair_hole`), 겹침은 같은 층끼리만. 저장 줄에 `floor: 2`(1층은 칸 없음), 2층 받침 위 소품도 `floor: 2`. `RemoveStructure` 계단은 2층 가구가 남으면 `not_empty`. **M108-08a(미배포, 루프#1410):** 전기 가구 넷 `PLACEABLE_ITEMS` — 냉장고 `item.fridge`·선풍기 `item.fan_stand` 는 바닥 가구(1×1), 라디오 `item.radio` 는 `TOP_ITEMS`(받침 위만, 바닥 `bad_args`), 네온 간판 `item.neon_sign` 은 `WALL_ITEMS`(집 벽만). `RECIPES` +4 고급 작업대(DESIGN 8.11 「🟡 전기 가구」), 판매가 = 재료 합 + 1, 테스터 키트 +4. 전력 소모·켜짐은 서버 판정 없음(놓인 목록으로 각 기기가 셈). |
| `RemoveStructure` | tentId, structureId | **Revision 7 배포(09-24).** 가방으로 100% 회수. 찬 보관함은 거절. **M108-03(미배포):** 위에 소품이 있는 받침도 `not_empty` |
| `UpgradeHouse` | tentId, color, level | **Revision 7 배포(09-24).** 24시간 안에 나무 150 + 향나무 50 → 집, 유지 24시간으로 시작. 기한 지나면 텐트·가구가 `expired` 로 사라져 있다. **Rev 38 배포(10-03, M20-03a, #0379).** `level: 2` = 집(텐트는 `not_house`)에 계단 건축 부품 1 + 나무판 40 + 향나무판 20 + 철판 4 → 즉시 `floors: 2`(이미면 `already`), 응답 `{upgraded, tentId, level, color, upkeepUntilUtc, furnitureMax}` · 2층 집 `PlaceStructure` 한도 60 · `RECIPES` +1 `item.stair_kit`(고급 작업대 나무판 6 + 금속 부품 2). 카탈로그 +1 `item.stair_kit` (DESIGN 8.11) |
| `FeedUpkeep` | tentId, count | **Revision 7 배포(09-24).** 나무 1개 = 2시간, 200시간까지만 빼고 넣는다. 유지 0 이면 집·가구가 사라진다 |
| `FeedGenerator` | tentId, structureId, fuel, count | **M108-07a(미배포, 루프#1403).** 내 땅의 놓인 발전기 `item.generator` 에 연료 넣기 — `fuel` = `item.wood`(1개 10분)·`item.coal_chunk`(1개 30분), 그 밖은 `bad_args`. 놓인 줄 `fuelUntilUtc` 하나만 저장, 다 탔으면 지금부터 다시. 최대 12시간 — 남은 칸에 드는 개수만 빼고(`full` = 하나도 안 듦), 모자라면 `not_enough_items`, 발전기 아닌 가구 `not_generator`, 없는 텐트·가구 `not_found`. 답 `{used, fuelUntilUtc, nowUtc}`(`nowUtc` = 서버 시계 — 클라는 끝 − 지금 차이로 게임 서버 시계에 옮긴다). 연료는 늘 탄다(접속 끊김 상관없음), `RemoveStructure` 로 치우면 남은 연료는 사라진다. 같은 판에 `PLACEABLE_ITEMS`·`RECIPES`(고급 작업대 소형 전동기 1 + 철판 2 + 쇠붙이 부속 2)·테스터 키트 +1 `item.generator`, `GetBaseState` 응답에 `nowUtc`. 카탈로그 +1 `item.generator` (DESIGN 8.11 「연료 발전기 숫자」) |
| `FeedRanchUpkeep` | count | **Revision 9 배포(09-24).** 목장 유지 — 나무 1개 = 2시간, 200시간까지만 뺀다(`full`·`not_enough_items`·`bad_args`). 잠긴 목장에 넣으면 풀리고 잠긴 시간만큼 각 칸 `occupiedSinceUtc` 를 밀어 **잠긴 동안 생산이 멈춘 셈**이다. `{used, upkeepUntilUtc, unlocked}` |
| `ExpandRanchSlot` | (없음) | **Revision 9 배포(09-24).** k번째 확장 = 명성 50k + 코인 500k 소모, `slotCap` +1, 상한 10칸(`full`). 확인 순서 명성(`not_enough_fame`) → 코인(`not_enough_coins`) → 뺀다. `{slotCap, fame, cost:{fame, coins}}` |
| (기존 함수 변경) | | **Rev 37 까지 배포됨(M12-05 — `7b81396e` 판에 `ranchUpkeep`·`slotCap`).** `RegisterPet` — 첫 배치(유지 기록 없음)에 유지 24시간 시작, 슬롯 번호 상한은 `slotCap`, 유지가 끊겼으면 `locked`. `HarvestRanch` — 끊겼으면 `locked`(펫·재고는 남는다), 성공하면 수확 1개당 명성 1 을 더하고 응답에 `fame`. `GetBaseState` — 응답에 `ranchUpkeep:{upkeepUntilUtc, leftHours, locked, fame, slotCap}` 추가 |
| `MoveToStorage` / `MoveFromStorage` | tentId, structureId, itemId, count, ownerId?, pin? | **Revision 7 배포(09-24).** 보관함 20칸. 남의 보관함은 PIN 이 맞아야 한다. 빼는 쪽 먼저, 넣는 쪽 나중. `boat: true` 면 tentId·structureId 대신 배 보관함(보트 컨테이너 가진 주인만, 8칸, 데이터 키 `boatStorage` — `GetBaseState.boatStorage` 로 조회, M32-02 보정) |
| `RollFish` | spotId, bait? | **Revision 7 배포(09-24).** 5초 간격 · 낚싯대 필수 · 먼바다는 도감 낚시 50% 뒤. 그 자리 서식지 어종을 1 ÷ 낚시 시간 가중치로 골라 일회용 `token` (산딸기 미끼 = 희귀 절반 ×1.1). 미끼 1개 여기서 뺀다 (DESIGN 8.13) |
| `CatchFish` | token | **Revision 7 배포(09-24).** 표가 맞고 안 쓴 것만, 5초 간격. 1마리 지급 + `codex`. 칸이 없으면 표를 쓰지 않는다 |
| `BuyItem` | itemId, count | **Revision 7 배포(09-24).** 「사는 목록」 값 × count 차감 후 지급. 설계도(`item.blueprint_*`)는 특별 상인(100%) 뒤 100 코인, 한 번만 (DESIGN 8.14 · 8.3) |
| `SellItem` | itemId, count | **Revision 7 배포(09-24).** 「파는 값」 × count 를 SC 로. 값 0(나무·산딸기·재료 값 합 0 인 가공품·상점 물건·가챠)은 `not_sellable`. **Rev 38 배포(10-03, 루프#309, M22-04b).** 포획 펫(목장 종 15) 10 코인 — 배포 전 라이브는 펫을 `not_sellable` 로 거절한다 |
| `PayFerry` | dest(hunt·danger·base·island) | **Revision 8 배포(09-24).** 사냥존 5 · 위험구역 10(도감 사냥+식물 50% 뒤) · 돌아오기 0 · 숲 칸(island) 5 |
| `EquipHat` | itemId | **Rev 37 까지 배포됨(M13-04 — `7b81396e` 판에 `handlers.EquipHat`·`wornHatEffect`).** 가진 모자 1개를 Player Data `hat` 에 적는다 — 모자가 아니면 `bad_args`, 안 가졌으면 `not_enough_items`. **`itemId:""` 는 벗기(#0198)** — `hat` 을 비우고 `{hat:""}`. 효과는 서버가 `wornHatEffect`(기록한 모자를 **지금도 가졌을 때만**)로 판정: `HarvestCrop` 농부 30% +1(응답 `bonus`) · `SellItem` 상인 ×1.2(총액 내림) · `PayFerry` 여행자 ×0.5(올림) · `ClaimLoot` 나무꾼 나무·삼나무 ×2(한도는 곱하기 전). 몸 효과(딸기 회복·달리기 속도)는 호스트 — DESIGN 13.5 |
| `SetOutfit` | slot, itemId | **Rev 53 라이브(10-14, 원래 스킨 조각 d, 루프#455).** 꾸미기 한 칸 입기·벗기(`docs/specs/skin-parts.md` 5장 4). `slot` = `head`·`face`·`top`·`bottom`·`shoes`·`gloves`·`back`(클라 `EEquipSlot` 과 한 줄씩, `UBackpackComponent::GetOutfitSlotKey`) 아니면 `bad_slot` → `itemId` 가 `OUTFIT_ITEMS` 에 없거나 칸이 다르면 `bad_args` → 같은 것이면 `already` → 배낭에 없으면 `not_enough_items` → Player Data `outfit` `{칸: 아이템}` 에 적는다(빈 `itemId` = 그 칸 벗기). **아이템은 배낭에 그대로**(모자처럼 기록만). 답 `{slot, itemId, outfit}` — `outfit` 은 지금도 가진 것만. `GetBaseState` 도 `outfit` 을 같은 규칙으로 싣는다. 클라는 받아들인 뒤에만 `SetCosmeticByItemID` 로 덮개를 건다(CLAUDE.md 8). `OUTFIT_ITEMS` 는 조각 f~l 이 꾸미기 행과 같이 채운다 — 루프#458 조각 f 가 머리 칸 6벌(`item.meadow_bucket`·`meadow_cap`·`meadow_frog`·`harbor_beanie`·`harbor_captain`·`harbor_whale`)을, 루프#459 조각 g 가 얼굴 칸 6벌(`item.meadow_ladybug_nose`·`meadow_daisy_glasses`·`meadow_petal_goggles`·`harbor_eyepatch`·`harbor_sunglasses`·`harbor_diving_mask`)을, 루프#460 조각 h 가 상의 칸 6벌(`item.meadow_tee`·`meadow_knit`·`meadow_petal_blouse`·`harbor_stripe_tee`·`harbor_puffer`·`harbor_sailor`)을, 루프#462 조각 i 가 하의 칸 6벌(`item.meadow_shorts`·`meadow_corduroy`·`meadow_petal_skirt`·`harbor_cargo`·`harbor_rain_pants`·`harbor_bell_bottoms`)을, 루프#463 조각 j 가 신발 칸 6벌(`item.meadow_canvas`·`meadow_clogs`·`meadow_petal_boots`·`harbor_deck_shoes`·`harbor_rain_boots`·`harbor_flippers`)을을, 루프#464 조각 k 가 장갑 칸 6벌(`item.meadow_garden_gloves`·`meadow_mittens`·`meadow_petal_gloves`·`harbor_fingerless`·`harbor_rubber_gloves`·`harbor_crab_gloves`)을, 루프#465 조각 l 이 등 칸 6벌(`item.meadow_daypack`·`meadow_basket_pack`·`meadow_frog_pack`·`harbor_duffel`·`harbor_lifebuoy_pack`·`harbor_octopus_pack`)을 넣었다 — 조각 f~l 일곱 칸 42벌이 다 찼다. |
| `SetNickname` | name | **Rev 53 라이브(10-14, 원래 루프#521, 닉네임 지시 10-06 13:40).** 앞뒤 공백을 자르고 문자열이 아니면 `bad_args` → 2~12자 아니면 `bad_length`(답에 `min`·`max`) → 완성 한글·영문·숫자 말고 다른 글자면 `bad_chars` → `NICKNAME_BANNED_WORDS` 가 (소문자·숫자 뺀 꼴 포함) 들어 있으면 `banned_word` → Player Data `nickname` 에 적는다. 같은 이름도 받는다(재시도). 답 `{name}` — 클라는 이 이름 그대로 Client API `UpdateUserTitleDisplayName` 을 부르고(겹침은 PlayFab 이 거절), 그것까지 받아들여진 뒤 이름표를 바꾼다(DESIGN 2.2 「이름표」). |
| `LaunchBoat` / `StowBoat` | (없음) | **Revision 7 배포(09-24).** 구명보트 1척을 `boat` 에 띄우고 배낭에서 뺀다 / 거두면 배낭으로 (DESIGN 8.9) |
| `GetQuests` | (없음) | **Revision 29 배포(09-25).** 오늘(UTC 일) 의뢰 3개 `{day, quests:[{id,itemId,count,reward,done}]}` — 플레이어 ID 씨앗으로 풀 12개에서 겹치지 않게 고른다 (DESIGN 8.14 의뢰) |
| `CompleteQuest` | questId | **Revision 29 배포(09-25).** 오늘 의뢰가 아니면 `bad_args`, 이미 건넸으면 `already`, 모자라면 `not_enough_items`. 아이템 차감 → SC 보상 → Internal Data `quests.done` 에 적음 |
| `GetStory` / `AdvanceStory` | (없음) / step?, report? | **루프#1157, 배포 전(감독).** 메인 스토리(`docs/STORY.md`, 5장 25단계). `GetStory` → `{step(끝낸 수), total:25, done, chapter, current:{step,kind,have,need,needs?}}`. `AdvanceStory` — 종류별 판정(give=★ 재료 소모+옛 값 코인 · count=업적 카운터가 단계 시작 때보다 늘었나 · ranch=목장 동물 수 · report=클라 보고 `report:true`) 뒤 단계 +1, 장 끝(5·10·15·20·25)이면 코인 20·30·40·50·60. 실패: `story_done` · `story_locked`(step 불일치) · `story_short`(모자람, `have·need`). Internal Data `story` = `{step, base, paid}` — 처음 부르면 옛 `questChain` n단계를 n 번째 ★ 까지 끝난 것으로 이어받고, 이후 `CompleteQuestChainStep` 은 `chain_done` |
| `GetQuestChain` / `CompleteQuestChainStep` | (없음) / step? | **Rev 53 라이브(10-14, 원래 M105-03, 루프#1122).** 의뢰 줄기(DESIGN 8.14, 평생 한 번 5단계). `GetQuestChain` → `{step(받은 단계 수), total:5, done, current:{step,needs:[{itemId,count}],coins,itemId,itemCount}}`. `CompleteQuestChainStep` — 재료 소모 + SC + (5단계) `item.vault_pass` 1장 + 단계 +1 을 한 호출에서. 실패 코드: `chain_done`(이미 5단계 끝) · `chain_locked`(`step` 이 지금 단계가 아님 — 건너뛰기·재수령) · `chain_short`(재료 모자람, `itemId·need·have`) · `inventory_full`(열쇠패 자리 없음). 실패하면 아무것도 안 바뀐다. Internal Data `questChain` = `{step}` |
| `GetAttendance` / `ClaimAttendance` | (없음) | **Rev 38 배포(10-03, M21-05a).** 7일 누적 출석(DESIGN 11 「7일 출석」). Get = `{day, nextSlot(1~7), claimedToday, next, rewards}`. Claim = 오늘(UTC 일) 이미 받았으면 `already`, 씨앗·모자 칸에 자리가 없으면 `inventory_full`(칸 그대로) → 코인 또는 아이템 지급 → Internal Data `attendance {claims, lastDay, hatGranted}` → `{day, slot, reward}`. 빠진 날은 리셋 없음, 7일째 다음은 1일, 모자 두 번째부터 코인 150 |
| `ReportCoop` | hostId, peers | **Rev 38 배포(10-03, M21-06b1).** 같은 방 2인 이상 목장 생산 −10%(DESIGN 3.2 「같이 하기 생산」). 자기 `coop {hostId, atUtc}` 를 적고, `peers`(7명까지) 각자의 `coop` 을 **서버가 직접 읽어** 같은 hostId·120초 안인 사람만 센다(호스트가 보낸 인원 수는 안 믿는다). 나 포함 2명 이상이면 지난 보고 뒤 흐른 시간(최대 120초, 방을 바꾼 첫 보고는 0)의 1/9 만큼 덜 찬 동물 칸 시계를 당긴다 — 잠긴 목장은 그대로 → `{coop, players, creditedMs, bonusMs}`. 인자 오류는 `bad_args`(아무것도 안 바뀜) |
| `ClaimBoss` | (없음) | **Rev 38 배포(10-03, M21-07d).** 주간 우두머리 보상(DESIGN 10.6 「변종·우두머리 확률 고지」). 이번 주(UTC 월요일 00:00 경계) 이미 받았으면 `already_claimed` → `BOSS_HAT_CHANCE` 10% 굴림 → 당첨이고 모자를 받은 적 없으면 `item.chief_hat` 1(자리 없으면 `inventory_full`, 주 기록 안 남김), 받은 적 있으면 코인 150, 꽝은 보상 없음 → Internal Data `boss {lastWeek, hatGranted}` → `{week, won, reward}`(꽝은 `reward: null`). 처치 명중자마다 각자 부른다(M21-07f) |
| `GiftItem` / `GetGifts` / `AcceptGift` / `DeclineGift` | targetPlayerId, itemId, count / (없음) / giftId / giftId | **Rev 53 라이브(10-14, 원래 M104-05, 루프#1107).** 건네기(DESIGN 3.3 「건네기」) — 선물만, 받는 쪽 수락제. `GiftItem`: 보내는 순간 보낸 사람 배낭에서 `count`(1~99)개를 빼 받는 사람 Internal Data `gifts` 에 보관(보낸 사람은 `giftsOut` 에 기록) → `{sent, giftId, sentToday, limit}`. 실패(아무것도 안 바뀜): `bad_args`(없는 사람·개수) · `unknown_item`(`item.`/`gacha.` 로 시작 안 함) · `self_gift` · `excluded_item`(`gacha.*`·`item.pet_*`·목줄 3종·꾸미기 상점 상품) · `daily_limit`(하루 20건, UTC 날짜 `giftDay`) · `target_pending_full`(받는 사람 대기 10건) · `blocked`(받는 사람이 나를 차단, 10-18) · `not_enough_items`. `GetGifts`: 내 대기 `gifts[]` — 24시간 넘은 건 보낸 사람에게 돌려주고 빼며, 내가 보낸 만료 선물도 이때 돌려받는다. `AcceptGift`: 칸이 모자라면 `inventory_full`(선물은 대기에 남음), 만료면 `expired`(보낸 사람에게 돌아감), 없는 선물은 `bad_args`, 성공 `{accepted, itemId, count, from}`. `DeclineGift`: 보낸 사람에게 돌려주고 `{declined}`. 거리 1000cm·같은 방은 호스트 몫(M104-06) |
| `DeleteMyAccount` | confirm | **Rev 36 배포(10-02).** 라이브에선 `server.DeletePlayer` 가 예외라 항상 `fullyDeleted:false`(비우기 경로) — 마스터 계정 삭제는 `account_delete_requested` 이벤트를 보고 운영자가 처리. 계정 삭제(DESIGN 4.4 「계정 삭제」). `confirm` 이 `"DELETE"` 가 아니면 `bad_args`(아무것도 안 함). 먼저 타이틀 이벤트 `account_delete_requested`(`playFabId`·`requestedAtUtc`)를 남기고 `server.DeletePlayer` 로 타이틀 계정을 통째로 지운다 → `{deleted:true, fullyDeleted:true}`. 그게 예외면 인벤토리 전부 회수(25개씩)·재화 전부 0·Internal Data 키 전부 삭제(10개씩)로 비우고 `{deleted:true, fullyDeleted:false}`. 두 번 불러도 같은 답 |
| `ReportPlayer` / `BlockPlayer` / `UnblockPlayer` / `GetBlockList` | targetPlayerId, reason(`name`·`harass`·`cheat`·`other`), note(선택 ≤100자) / targetPlayerId, name(선택, 32자로 자름) / targetPlayerId / (없음) | **Rev 57 라이브(10-16 신고·차단, 앱스토어 1.2).** `ReportPlayer`: 타이틀 이벤트 `player_reported`(`reporterId`·`targetId`·`reason`·`note`·`atUtc`)를 남기고 `reportDay` 에 적는다 → `{reported, reportedToday, limit}`. 운영자는 게임 관리자 PlayStream 에서 이 이벤트를 본다(자동 제재 없음). 실패(아무것도 안 바뀜): `bad_args`(없는 사람·이유·메모 길이) · `self` · `already`(오늘 그 사람 이미 신고) · `daily_limit`(하루 10명) · `busy`. `BlockPlayer`: `blocks` 에 더한다, 이미 있으면 그대로 성공 → `{blocked, blocks, limit}`; 실패 `bad_args` · `self` · `full`(100명) · `busy`. `UnblockPlayer`: 없으면 `not_found` → `{unblocked, blocks, limit}`. `GetBlockList`: 로그인 때 클라가 부른다 → `{blocks, limit}` |

### 배포 대기
라이브는 **Rev 67 = `1b4ffa97` 판**(아래 「배포됨 Rev 67」). 다음 대기분은 `git log 1b4ffa97..HEAD -- tools/playfab/cloudscript_main.js` 로 찾는다.
- **사냥 드롭 서버 굴림 `ClaimLoot {kills}`(루프#1446, 감독 지시 10-20 ①)** — `kills:[{animalId, count}]` 를 받으면 서버 `KILL_DROPS`(생성기 동물 행과 node 시험이 맞댐)로 마리마다 1~3개, 최상급 5%(사냥꾼 모자 ×1.3, `wornHatEffect`)를 굴린다. `items` 와 같이 와도 된다. 한도는 마리 × 3 으로 미리 본다(`too_many`·`daily_limit`). 새 카탈로그 없음. **순서 주의(루프#1447):** 클라가 이제 사냥 처치를 `kills` 로만 보낸다 — 이 서버판(Rev 67 뒤)을 올리기 전 클라로 사냥하면 옛 서버가 `kills` 를 모르고 무시해 고기·전리품이 안 들어온다. 폰·PIE 사냥 확인 전에 먼저 배포.
- **찬 칸 수확 이월 막기(루프#1433, M28-09g)** — `HarvestRanch`·`HarvestFishing` 이 상한(5)만큼 거두면 넘친 시간을 이월하지 않는다. 라이브는 아직 이월해서 며칠 둔 칸은 누를 때마다 5개·50코인을 또 준다(온라인 실측 `Saved/M96/lh1433_harvest_wait.png` — 거둔 13초 뒤에도 5/5). **급하다** — 재화가 새는 길.
- **스킨 조각 d `SetOutfit`(루프#455)** — 새 함수 + `GetBaseState.outfit`. `OUTFIT_ITEMS` 가 비어 있어 올려도 지금은 모든 입기가 `bad_args` — 꾸미기 행(조각 f~l)·카탈로그가 들어온 판과 같이 올리면 된다. 급하지 않다.
  - **조각 f(루프#458)** — `OUTFIT_ITEMS` 머리 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_bucket` 들꽃 벙거지·`meadow_cap` 들꽃 챙모자·`meadow_frog` 청개구리 모자·`harbor_beanie` 줄무늬 방울 비니·`harbor_captain` 항구 선장모·`harbor_whale` 고래 모자, 젬 300 = 명세 4장 「모자 300」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·입을 수 있다.
  - **조각 g(루프#459)** — `OUTFIT_ITEMS` 얼굴 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_ladybug_nose` 무당벌레 코·`meadow_daisy_glasses` 데이지 동그란 안경·`meadow_petal_goggles` 꽃잎 고글·`harbor_eyepatch` 줄무늬 안대·`harbor_sunglasses` 항구 선글라스·`harbor_diving_mask` 잠수 마스크, 젬 200 = 명세 4장 「얼굴 장식 = 작은 부위 200」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·입을 수 있다.
  - **조각 h(루프#460)** — `OUTFIT_ITEMS` 상의 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_tee` 들꽃 반팔 티·`meadow_knit` 들꽃 니트·`meadow_petal_blouse` 꽃잎 주름 블라우스·`harbor_stripe_tee` 줄무늬 마린 티·`harbor_puffer` 항구 패딩·`harbor_sailor` 세일러 깃 셔츠, 젬 300 = 명세 4장 「상의 = 큰 부위 300」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·입을 수 있다.
  - **조각 i(루프#462)** — `OUTFIT_ITEMS` 하의 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_shorts` 들꽃 반바지·`meadow_corduroy` 들꽃 골덴 바지·`meadow_petal_skirt` 꽃잎 치마·`harbor_cargo` 항구 카고 바지·`harbor_rain_pants` 항구 비옷 바지·`harbor_bell_bottoms` 세일러 나팔바지, 젬 200 = 명세 4장 「하의 200」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·입을 수 있다.
  - **조각 j(루프#463)** — `OUTFIT_ITEMS` 신발 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_canvas` 들꽃 캔버스화·`meadow_clogs` 데이지 나막신·`meadow_petal_boots` 꽃잎 부츠·`harbor_deck_shoes` 항구 갑판화·`harbor_rain_boots` 항구 장화·`harbor_flippers` 항구 오리발, 젬 200 = 명세 4장 「신발·장갑·얼굴 장식 = 작은 부위 200」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·신을 수 있다.
  - **조각 k(루프#464)** — `OUTFIT_ITEMS` 장갑 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_garden_gloves` 들꽃 정원 장갑·`meadow_mittens` 데이지 벙어리장갑·`meadow_petal_gloves` 꽃잎 소매 장갑·`harbor_fingerless` 항구 반장갑·`harbor_rubber_gloves` 항구 고무장갑·`harbor_crab_gloves` 꽃게 집게 장갑, 젬 200 = 명세 4장 「신발·장갑·얼굴 장식 = 작은 부위 200」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·낄 수 있다.
  - **조각 l(루프#465)** — `OUTFIT_ITEMS` 등 칸 6벌. 카탈로그에 같은 ID 6개(`meadow_daypack` 들꽃 데이팩·`meadow_basket_pack` 버들 바구니 배낭·`meadow_frog_pack` 청개구리 배낭·`harbor_duffel` 줄무늬 더플 배낭·`harbor_lifebuoy_pack` 구명환 배낭·`harbor_octopus_pack` 문어 배낭, 젬 300 = 명세 4장 「배낭 300」, 희귀·에픽 웃돈은 감독 몫)를 더해야 살 수·멜 수 있다.
- **M21-07g 확률 고지(루프#344)** — `VARIANT_CHANCE = 0.03` 한 줄(고지·2층 맞대기용 사본, 어느 함수도 안 읽는다). 급하지 않다 — 다른 대기분과 같이 올리면 된다.
- **M21-07e 우두머리 모자(루프#342)** — `HAT_EFFECTS` +`item.chief_hat`(빈 효과 — `EquipHat` 이 받게). 카탈로그엔 이미 있다(357 → 361 때). 없으면 `ClaimBoss` 로 받은 모자를 `EquipHat` 이 `bad_args`(모자가 아니다)로 거절한다.
- **M21-07b 변종 펫 서버(루프#339)** — `ClaimLoot` 가 `VARIANT_PET_ITEMS` 12종을 받음 · `RegisterPet {variant}`·`RemovePet` 변종 칸 · 동물 칸 주기 `animalIntervalMinutes`(변종 ÷1.5) · 도감 `variant` 12칸(보상 계산 밖). **카탈로그 1.0 +12 먼저** — `item.pet_fox_variant`·`item.pet_goat_variant`·`item.pet_reindeer_variant`·`item.pet_elk_variant`·`item.pet_lynx_variant`·`item.pet_wolf_variant`·`item.pet_wolverine_variant`·`item.pet_bear_variant`·`item.pet_pheasant_variant`·`item.pet_peacock_variant`·`item.pet_ptarmigan_variant`·`item.pet_capercaillie_variant`(Durable·쌓기 끔, 다른 펫과 같은 꼴). 카탈로그 없이 올리면 변종 포획 `ClaimLoot` 가 없는 아이템 지급으로 실패한다. 클라 쪽(목장 배치 `variant` 인자·도감 탭)은 M21-07c.

**새 함수(`GetCodexRewards` 부터)의 실패 응답은 `{error, code}`** — code: `bad_args` · `not_wall` · `not_host` · `story_done` · `story_locked` · `story_short` · `chain_done` · `chain_locked` · `chain_short` · `not_enough_items` · `not_enough_coins` · `inventory_full` · `storage_full` · `expired` · `already_taken` · `token_invalid` · `too_fast` · `locked` · `limit` · `occupied` · `out_of_land` · `not_found` · `not_empty` · `already` · `not_house` · `full` · `protected` · `no_rod` · `not_sellable` · `not_launched` · `already_claimed` · `bad_chars` · `bad_length` · `bad_slot` · `not_in_room` · `pin_locked` · `banned_word` · `cap_reached` · `closed` · `busy` · `empty_slot` · `grant_failed` · `no_gun` · `no_key` · `no_treasure` · `not_enough_fame` · `not_enough_gems` · `not_tester` · `recharging` · `same_color` · `self` · `slot_full` · `too_many` · `too_soon` · `same_player` · `already_picked` · `not_ready` · `excluded_item` · `unknown_item` · `daily_limit` · `target_pending_full` · `self_gift` · `blocked` · `not_floor2` · `stair_exists` · `out_of_house` · `blocks_path` · `no_stairs` · `stair_hole` · `not_generator`. 확인이 하나라도 실패하면 **아무것도 안 바뀐다**(2층 검사가 실패마다 계정 전체를 호출 전후로 맞댄다). `addToCodex` 는 다섯 카테고리(`CODEX_CATEGORIES`, 지금 154칸) 안 ID 만 담는다 — 고기·펫 아이템·가축(`animal.pig` 등)은 칸이 아니라 저장하지 않는다(#0190). `CODEX_MAX_ENTRIES`(200)는 칸 수보다 넉넉한 안전장치일 뿐이다.

**도감은 새 엔드포인트를 만들지 않는다 (M10-04, DESIGN 8.5 · 결정 007).** 위 세 함수의 **이미 있는 성공 판정 자리**에 `codex` 추가를 한 줄씩 얹는 것이 전부다 — 클라이언트가 "다 모았다"고 부르는 길을 열면 탈것(8.3)이 공짜가 된다.

**사냥 칸이 포획 함수가 아니라 `RegisterPet` 에 걸린 이유:** 포획 성공 판정(`UCaptureComponent::TryCaptureTargetOnAuthority`)은 **호스트 권위일 뿐 CloudScript 가 아니다** — 백엔드는 그 순간을 모른다. 백엔드가 포획을 처음 아는 자리가 `RegisterPet`(잡은 동물을 목장 슬롯에 등록) 이라 거기에 걸었다. 대가: **잡고 목장에 넣지 않으면 도감이 안 찬다.** 잡은 동물이 목장 말고 갈 곳이 없는 지금은 차이가 없고, 생기면 그때 포획 전용 CloudScript 를 세운다.

**농사 칸은 `PlantCrop`·`HarvestCrop` 이 맡는다 (⑩)** — 작물 칸도 목장 칸과 한 배열(`ranch`)이고, 항목에 `animalId` 대신 `cropItemId` 가 있다. 빈 칸은 종류가 없어 펫을 넣으면 동물 칸, 씨앗을 심으면 작물 칸이 된다(DESIGN 8.2). 그래서 `RegisterPet` 은 작물이 선 칸을 `slot_full` 로, `HarvestRanch` 는 작물 칸을 "슬롯이 비어 있음" 으로 거절한다(**기존 함수 변경 — `RegisterPet`·`GetBaseState`(작물 칸 다 자람 `stock`) 도 같이 배포해야 한다**). `FeedRanchUpkeep` 의 잠긴 시간 밀기는 작물 칸에도 그대로 걸린다. 라이브에 아직 없으면 PlayFab 이 `CloudScriptNotFound` 로 답하고, 클라이언트(`URanchSyncComponent`)는 그 세션 동안 그 함수를 "서버 준비 중" 으로 끈다.

**낚시 시간·코인은 여기 옮겨 적지 않는다 — 원본은 DESIGN 8.4 표 하나다** (붕어 15분/코인 5 · 잉어 30분/10 · 메기 60분/20). 사본을 늘리면 어긋나고, 어긋난 쪽을 잡아 줄 검사가 없다 — 2층 `Snarehaven.Fishing.Table.MatchesDesign` 은 `DT_FishingTable` 과 DESIGN 8.4 만 맞대고 이 문서는 안 읽는다. CloudScript 는 그 표의 값을 그대로 쓴다.

**코인은 클라이언트 표에 없다.** `DT_FishingTable` 에는 낚시 시간과 상한만 있다 — 목장 코인 10 이 클라이언트 표에 없는 것과 같은 이유다(DESIGN 4.1 권위 2단 표 · CLAUDE.md 8). 지급액을 클라이언트가 들고 있으면 그게 곧 치트 경로라, 코인은 CloudScript 안에만 둔다.

**상한 5 는 목장·작물과 같은 숫자다** — 새 숫자가 아니라 3.2 의 재사용이라 `HarvestRanch` 의 자르는 코드를 그대로 쓴다.

**`GrantStarterKit` 도 아직 올라가지 않았다** — `tools/playfab/cloudscript_main.js` 에는 들어갔지만 콘솔 업로드·live 전환은 M2-03 과 같은 사람 몫이다. 그래서 실제 왕복(3층)은 못 본다. 대신 **핸들러를 그대로 실행하는 2층 검사**가 저장소 안에 있다: `node tools/playfab/test_cloudscript.js` — 가짜 `server` API 위에서 `GrantStarterKit` 을 불러 "새 계정 1회 · 두 번째 접속 0회 · 계정마다 따로 · 시작 코인 5 는 첫 1회만" 을 보고, 지급 아이템·개수는 **DESIGN 3.5 표를 읽어** 맞댄다(표를 못 읽으면 실패). 검사할 스크립트 경로를 인자로 주면 역검증용 사본도 돌릴 수 있다.

**낚시 두 함수(`RegisterFishingSpot`·`HarvestFishing`)는 아직 올라가지 않았다** — M10-03 은 명세(위 표)까지고, `tools/playfab/cloudscript_main.js` 에 넣어 콘솔에서 live 로 올리는 것은 M2-03 과 같은 사람 몫이다.

**배포 상태 (2026-09-22, 감독):** `tools/playfab/cloudscript_main.js` 를 Legacy CloudScript Revision 2 로 업로드·live 전환 완료. 임시 테스트 플레이어(`LoginWithCustomID` custom ID `cloudscript_verify_20260922`)로 세 함수 모두 `ExecuteCloudScript` 호출 확인:
- `RegisterPet(pig, slot0)` → `{"success":true,"slot":0,"animalId":"pig"}`
- `RegisterPet(cow, slot0)` (중복 슬롯) → `{"error":"슬롯이 이미 차 있음"}`
- `HarvestRanch(slot0)` (등록 직후, 30분 미경과) → `{"harvested":0,"message":"아직 생산 안 됨"}`
- `RollGacha(SC)` (재화 0) → `{"error":"재화 부족","need":500,"have":0}`

응답 전부 `"Revision":2` 로 찍혀 live 리비전이 호출됨을 확인. `ANIMAL_PRODUCT_ITEM` 의 `item.pig_meat` 등은 M2-03 카탈로그 확정 전 자리표시라 실제 지급 성공 경로(재화 채운 뒤 `RollGacha` 당첨, 30분 경과 후 `HarvestRanch`)는 아직 검증 못 함.

**배포 상태 (2026-09-23, 감독):** `tools/playfab/cloudscript_main.js` 를 Revision 4 로 업로드·live 전환 완료 — `ANIMAL_PRODUCT_ITEM` 키를 `animal.pig` 형태(콘솔에서 실제로 넘어오는 animalId 형식)로 고치고, `RegisterFishingSpot`·`HarvestFishing`(M10-03) 을 새로 얹었다. 임시 테스트 플레이어(`LoginWithCustomID` custom ID `cloudscript_verify_20260923`)로 다섯 함수 모두 `ExecuteCloudScript` 호출 확인, 응답 전부 `"Revision":4`:
- `RegisterPet(animal.pig, slot0)` → `{"success":true,"slot":0,"animalId":"animal.pig"}` — 이전 리비전에서 키 불일치로 못 잡던 케이스가 정상 통과
- `HarvestRanch(slot0)` (등록 직후) → `{"harvested":0,"message":"아직 생산 안 됨"}`
- `RegisterFishingSpot(item.crucian_carp, slot0)` → `{"success":true,"slot":0,"fishItemId":"item.crucian_carp"}`
- `HarvestFishing(slot0)` (등록 직후) → `{"harvested":0,"message":"아직 생산 안 됨"}`
- `RollGacha(SC)` (재화 0) → `{"error":"재화 부족","need":500,"have":0}`

카탈로그 아이템(`item.pig_meat`·낚시 3종·가챠 풀) 실제 지급 성공 경로는 M2-03 카탈로그 확정 전이라 여전히 검증 못 함 — 30분 경과 후 `HarvestRanch`/`HarvestFishing`, 재화 채운 뒤 `RollGacha` 당첨은 카탈로그 아이템이 실제로 존재해야 통과 가능.

**배포 상태 (2026-09-23, 감독 — Revision 5):** 사용자가 콘솔에 붙여넣어 live 전환. 바뀐 것은 데이터 두 덩이뿐이다 — `FISH_TABLE` 어종 3 → **21종**(DESIGN 8.4), `CODEX_MAX_ENTRIES` 9 → **27**(DESIGN 8.5). 핸들러 5개의 로직은 그대로다. 임시 테스트 플레이어 두 명(`rev5-verify-*`, `rev5-harvest-*`)으로 `ExecuteCloudScript` 실호출, 응답 전부 `"Revision":5`:
- `RegisterFishingSpot(item.swordfish, slot0)` → `{"success":true,"slot":0,"fishItemId":"item.swordfish"}` — Revision 4 에 없던 어종이 통과한 것이 곧 새 리비전이 live 라는 증거
- **21종 전수 확인** — DESIGN 8.4 의 어종 21개를 하나씩 `RegisterFishingSpot` 에 넣어 `알 수 없는 fishItemId` 가 하나도 안 나오는 것을 확인(**21/21**, 이미 찬 슬롯이라 전부 `슬롯이 이미 차 있음` 으로 되돌아옴)
- `RegisterFishingSpot(item.nope, slot1)` → `{"error":"알 수 없는 fishItemId: item.nope"}` — 표를 넓히면서 가드가 느슨해지지 않았음
- `HarvestFishing(slot0)` 빈 슬롯 → `{"error":"슬롯이 비어 있음"}` / `item.axolotl` 등록 직후 → `{"harvested":0,"message":"아직 생산 안 됨"}` — 새 어종도 `FISH_TABLE[...].intervalMinutes` 조회에 걸려 계산 경로를 탄다

**아직 검증 못 한 것:** 도감 27칸 컷오프(`addToCodex` 의 `CODEX_MAX_ENTRIES`)는 `HarvestFishing` 이 실제로 수확에 성공해야만 타는 경로다. 가장 짧은 어종이 15분이고 카탈로그 아이템도 아직 없어서 이번 회차에서는 못 밟았다 — M2-03 카탈로그 확정 뒤에 27번째 등록이 실제로 들어가는지 확인한다. 클라이언트 `DT_FishingTable` 은 아직 3행이라 나머지 18종을 등록할 길 자체가 없다(백엔드가 먼저 넓어져 있는 상태, 순서상 정상).

**배포 상태 (2026-09-24, 감독 — Revision 6):** 사용자 승인("올려", 00:10)으로 Game Manager 편집기에 `tools/playfab/cloudscript_main.js` 전체를 붙여넣고 "Save as revision 6 · Deploy this revision after save" 로 live 전환. 바뀐 것은 `GrantStarterKit` 핸들러·`STARTER_KIT` 표·`loadStarterStamp`/`saveStarterStamp` 뿐이다 — Revision 5 커밋(`f2358bc`) 대비 **추가 68줄 · 삭제 0줄**이라 기존 핸들러 5개(RegisterPet·HarvestRanch·RegisterFishingSpot·HarvestFishing·RollGacha)는 글자 그대로다. 올리기 전 2층 검사 `node tools/playfab/test_cloudscript.js` → `검사 결과: 6개 중 6개 통과 · 실패 0개`.
- 콘솔 API `servers/cloud-script` 응답 → `"Revision":6,"LatestRevision":6,"LiveRevision":6`, 파일 `main.js` 의 `handlers.*` 6개(`GrantStarterKit` 포함)
- 서버에 저장된 본문은 줄바꿈이 CRLF(453줄)로 바뀌어 있고, LF 로 되돌린 SHA-1 이 저장소 파일과 같다(`1550a5fa…`) — 붙여넣는 중에 빠지거나 바뀐 글자 없음

**새 아이템 ID 목록 도구 (M89).** 카탈로그에 넣을 ID 는 `python3 tools/playfab/list_catalog_ids.py --since <rev>` 로 뽑는다(`<rev>` 이후 `DT_ItemTable` 에 새로 생긴 ID·종류·등급·판매가·이름 표, 빼면 전체 표). 예전처럼 STATUS 새 ID 목록 4곳(#0914~#0953)을 뒤지지 않고 이 명령 한 번으로 대신한다.

**카탈로그 추가 — `item.club`.** 레거시 카탈로그 `1.0`(`GrantItemsToUser` 가 보는 곳)에 `item.club` 이 없어서(25개 — 목장 3 · 낚시 3 · 가챠 19) 감독이 추가했다: 표시 이름 `Club` · Durable · 클래스 없음 → 26개. 추가 전에는 실제 게임 실행을 하지 않았다 — 없는 아이템으로 부르면 `starterKit` 표시만 남아 그 계정이 몽둥이를 영영 못 받을 수 있어서다.

**실제 게임 실행(3층) — 통과.** `UnrealEditor-Cmd … /Game/Game/Maps/L_Hunt -game -RenderOffscreen -unattended` 헤드리스로 같은 기기 ID 를 두 번 실행했다.
- 1회차(첫 접속):
  ```
  [2026.09.23-22.32.06:281] [Backend] PlayFab 로그인 성공 — PlayFabId <숨김>
  [2026.09.23-22.32.06:519] [Backend] 시작 장비 지급을 물어본다 — CloudScript GrantStarterKit
  [2026.09.23-22.32.06:871] [Backend] 시작 장비를 받았다 — 1종 · 보유 상태를 다시 물어본다
  [2026.09.23-22.32.07:038] 시작 장비 장착 - item.club · 결과 들었다
  [2026.09.23-22.32.07:039] [Backend] 보유 상태 받음 — 칸 1개 · 코인 0 · 젬 0
  ```
- 2회차(같은 계정):
  ```
  [2026.09.23-22.32.32:898] [Backend] PlayFab 로그인 성공 — PlayFabId <숨김>
  [2026.09.23-22.32.33:137] [Backend] 보유 상태 받음 — 칸 1개 · 코인 0 · 젬 0
  [2026.09.23-22.32.33:435] [Backend] 시작 장비 — 이미 받은 계정이라 지급 0건
  ```
- 2회차 보유 칸이 1개 그대로라 중복 지급 없음.

**배포 상태 (2026-09-24, 감독 — Revision 7):** "올려" 승인(8.9~8.14 묶음)으로 `tools/playfab/cloudscript_main.js` 전체(1464줄)를 Game Manager 편집기에 넣고 "Save and deploy" 로 live 전환. 올리기 전 2층 검사 `node tools/playfab/test_cloudscript.js` → `검사 결과: 44개 중 44개 통과 · 실패 0개`.
- 콘솔 API `servers/cloud-script` → `"LatestRevision":7,"LiveRevision":7`, `handlers.*` 6개 → **28개**
- 서버 본문을 LF 로 되돌린 SHA-1 = 저장소 파일 SHA-1 = `559cceb3dfe8bf8dd5984bc37f815f066a9bcc70` (60824자)
- 임시 플레이어(`rev7-verify-*`·`rev7-sc-*`) `ExecuteCloudScript` 실호출, 응답 전부 `"Revision":7`:
  - `GetBaseState` → `{"ranch":[null×5],"fishing":[null×5],"tents":[],"boat":{"launched":false}}`
  - `GetCodexRewards` → 낚시 21 · 사냥 29 · 식물 6 · 설계도 4 · 요리 22, 보상 셋 다 `false`
  - `CraftItem(recipe.nope)` → `{"error":"알 수 없는 recipeId: recipe.nope","code":"bad_args"}`
  - `SellItem(item.club)` → `{"error":"팔 수 없는 아이템: item.club","code":"not_sellable"}`
  - `RollGacha(SC)` → `{"error":"재화 부족","need":500,"have":5}`
- **찾은 결함과 고친 것:** 첫 `GrantStarterKit` 이 `CloudScriptAPIRequestError` — 타이틀에 가상 화폐가 **하나도 없어서**(`GetUserInventory.VirtualCurrency = {}`) `AddUserVirtualCurrency("SC")` 가 거절됐다(몽둥이는 이미 지급된 뒤라 그 테스트 계정은 표시 없이 몽둥이만 받음 — 코드 주석의 "덜 나쁜 쪽" 그대로). 감독이 Economy › Currency (Legacy) 에 `SC`(Snare Coin, 초기 0) 등록 → 새 플레이어로 `{"granted":true,…,"coins":5}` · 재호출 `alreadyGranted:true` · 인벤토리 `["item.club"]` · `{"SC":5}`.
- `GM`(Gem, 초기 0)도 사용자 지시("gm등록해")로 등록 → 새 플레이어 `VirtualCurrency = {"SC":0,"GM":0}`, `RollGacha(GM)` → `{"error":"재화 부족","need":100,"have":0}` (API 오류 아님, 판정 경로 정상).

**배포 상태 (2026-09-24, 감독 — Revision 8):** 존 4칸 병합(`1c69482`)으로 `PayFerry` 에 `island`(숲 칸 입장료 5)가 추가돼 같은 방법으로 올렸다. Revision 7 대비 바뀐 줄 3줄(요금표·오류 문구·주석)뿐. 올리기 전 `test_cloudscript.js` → `검사 결과: 44개 중 44개 통과 · 실패 0개`.
- 콘솔 API → `LiveRevision` **8**, 서버 본문 LF SHA-1 = 저장소 파일 SHA-1 = `a7d5fc7a772f8cb7c97900d5273a84be711e9a1d` (60935자)
- 새 플레이어(`rev8-*`) 실호출, 응답 전부 `"Revision":8`: `GrantStarterKit` → `"coins":5` · `PayFerry({dest:"island"})` → `{"dest":"island","paid":5}` · `PayFerry({dest:"moon"})` → `{"error":"dest 는 hunt, danger, base, island","code":"bad_args"}` · 인벤토리 `{"SC":0,"GM":0}` (5 받고 5 냄)

**배포 상태 (2026-09-24, 감독 — Revision 9):** 거점 부패·명성 병합(`8e7bcac`, M12-05)으로 `FeedRanchUpkeep`·`ExpandRanchSlot` 새 함수와 `GetBaseState.ranchUpkeep`·`HarvestRanch.fame`·잠긴 목장 `locked` 실패가 들어가 같은 방법(크롬 붙여넣기 → Save and deploy)으로 올렸다. 올리기 전 `test_cloudscript.js` → `검사 결과: 50개 중 50개 통과 · 실패 0개`.
- 콘솔 API → `LiveRevision` **9**, 서버 본문 LF SHA-1 = 저장소 파일 SHA-1 = `779726b7d59196bc37e8445a715e77de3c351358` (65482자)
- 새 플레이어(`rev9-1790218380712`, 인벤토리 `{"SC":0,"GM":0}` · 아이템 0) 실호출, 응답 전부 `"Revision":9`:
  - `GetBaseState` → `"ranchUpkeep":{"upkeepUntilUtc":null,"leftHours":null,"locked":false,"fame":0,"slotCap":5}`
  - `FeedRanchUpkeep({})` → `{"error":"count 필요","code":"bad_args"}`
  - `FeedRanchUpkeep({count:1})` → `{"error":"나무가 모자란다","code":"not_enough_items","need":1,"have":0}`
  - `ExpandRanchSlot({})` → `{"error":"명성이 모자란다","code":"not_enough_fame","need":50,"have":0}`
- 성공 경로(나무 넣기·칸 확장 성공·잠긴 뒤 풀림)는 실계정으로 아직 안 봤다 — 2층 `test_cloudscript.js` 가 맡고, 실계정은 목장 HUD 3층 때 본다.

**카탈로그 확장 (2026-09-24, 감독):** 레거시 카탈로그 `1.0` 26개 → **159개**. 클라이언트 `DT_ItemTable` 의 item ID 139개를 전부 넣었다(없던 133개 추가, 기존 26개 설정은 그대로). 새 항목 형식은 `item.club` 과 같다 — Durable · 쌓기 끔 · 클래스 없음 · 표시 이름은 ID 를 영어 제목으로(`item.grilled_salmon` → `Grilled Salmon`). 서버 `cloudscript_main.js` 에 나오는 `item.*` 중 클라이언트 표에 없는 것은 0개. 옛 `item.cow_milk` 은 지우지 않고 남겼다(소 생산물은 이제 `item.milk`). 그래서 97행의 "`item.pig_meat` 등은 자리표시" 문구는 **옛 기록**이다 — 지금은 목장 생산물 15종 전부 카탈로그에 있다.

**배포 상태 (2026-09-24, 감독 — Revision 10) + 카탈로그 159 → 169:** 깃털 종별 분리·상급 트로피 ID 맞춤(STATUS #0173, M5-08b)으로 `HUNT_TROPHIES` 가 23개 평평한 배열로 바뀌어 같은 방법(크롬 붙여넣기 → Save and deploy)으로 올렸다. 올리기 전 `test_cloudscript.js` → `검사 결과: 50개 중 50개 통과 · 실패 0개`.
- 콘솔 API → `LiveRevision` **10**, 서버 본문 LF SHA-1 = 저장소 파일 SHA-1 = `d56ee419930bed0ca8a15ca7ab6ebae0291214fd` (65771자, 1576줄)
- 새 플레이어(`rev10-verify-20260924`) 실호출 `GetCodexRewards` → `"Revision":10` · `"hunt": {"have": 0, "total": 35, "percent": 0}` (배포 전 라이브는 `/29`)
- 카탈로그 `1.0` 에 새 ID 10개 추가(Durable·겹치기 없음, 표시 이름 = 영어 이름): `item.pheasant_feather` · `item.pristine_pheasant_feather` · `item.peacock_feather` · `item.pristine_peacock_feather` · `item.ptarmigan_feather` · `item.pristine_ptarmigan_feather` · `item.capercaillie_feather` · `item.pristine_capercaillie_feather` · `item.pristine_bear_paw` · `item.pristine_goat_horn`. 콘솔 API `items/all` → 169개 · 고유 ID 169 · 10개 모두 있음
- 옛 ID 4개(`item.feather` · `item.fine_feather` · `item.flawless_bear_paw` · `item.noble_goat_horn`)는 **지우지 않고 둔다** — 이미 받은 플레이어 인벤토리 항목이 카탈로그 없는 ID 가 되지 않게(`item.cow_milk` 와 같은 처리). 서버·클라는 더 이상 지급하지 않는다
- 추가는 입력 폼(`economy/catalogs/MS4w/items/new`) 을 페이지 JS 로 채워 저장했다 — 새로 뜬 폼에 키 입력을 넣으면 React 가 첫 렌더 때 지워서, 네이티브 value setter + `input` 이벤트로 넣고 값 확인 뒤 `Save item` 을 누른다

**배포 상태 (2026-09-24, 감독 — Revision 11):** 씨앗 심기·수확 병합(`62b9a34`, STATUS #0182, ⑩)으로 `PlantCrop`·`HarvestCrop` 새 함수와 `RegisterPet`(작물 칸 차단)·`GetBaseState`(작물 칸 `stock`) 변경을 같은 방법(크롬 붙여넣기 → Save and deploy)으로 올렸다. 올리기 전 `node tools/playfab/test_cloudscript.js` 55/55 · 자동화 325/325.
- 콘솔 API → `LiveRevision` **11**, 서버 본문 LF SHA-1 = 저장소 파일 SHA-1 = `d6245e753ead7cac18c9d0ac207c0255132cb6a8` (69267자, 1642줄)
- 새 플레이어(`rev11-…`) 실호출: `GetBaseState` → `"Revision":11` · `ranch:[null×5]` / `PlantCrop(slot 0, item.seed_carrot)` → `not_enough_items`(씨앗 0) / `PlantCrop(item.seed_rock)` → `bad_args` / `HarvestCrop(slot 0)` → `empty_slot`. 씨앗을 가진 성공 경로는 새 계정이 씨앗을 얻을 길(잡화점 씨앗 줄 = M13-01)이 아직 없어 `test_cloudscript.js` 로만 확인했다
- 카탈로그 변경 없음 — Client `GetCatalogItems(1.0)` 169개 안에 `item.seed_carrot·seed_potato·seed_wheat·carrot·potato·wheat` 6개 모두 true

**배포 상태 (2026-09-24, 감독 — Revision 12):** 작물 22·씨앗 24 병합(`b5d6e64`, STATUS #0185, M13-01)으로 작물 표 27줄·`seedOf` 예외·`CROP_HARVEST_COUNT`(나무 4)·잡화점 씨앗 24줄·씨앗 레시피·도감 식물 분류·판매가 0 변경을 같은 방법으로 올렸다. 올리기 전 node 56/56 · 자동화 326/326.
- 콘솔 API → `LiveRevision` **12**, 서버 본문 LF SHA-1 = 저장소 파일(`7251235`) SHA-1 = `82423c8533fab2e1c2b618a4ae1414da968c50af` (71971자, 1682줄)
- 카탈로그 1.0 **169 → 215** — 작물 22·씨앗 24 를 콘솔에서 더했다(Durable, 표시 이름 = ID 영문). Client `GetCatalogItems(1.0)` 로 서버 코드가 쓰는 작물·씨앗·상점·레시피 ID 를 다시 대조한 결과 `live 215 need 0`
- 새 플레이어(`rev12-…`) 실호출: `GrantStarterKit` → `"Revision":12` · `coins:5` / `BuyItem(item.seed_oak)` → `{"bought":"item.seed_oak","count":1,"cost":4}` / `PlantCrop(slot 1, item.seed_oak)` → `{"success":true,"slot":1,"cropItemId":"item.wood","seedItemId":"item.seed_oak"}`(예외 매핑) / `GetBaseState` → `ranch[1] = {cropItemId:"item.wood", stock:0}` · 남은 코인 `SC:1`. `BuyItem(item.seed_black_lotus)` 는 값 8 > 코인 5 라 `need 8 have 5` — 설계대로

**배포 상태 (2026-09-24, 감독 — Revision 13):** 채집점 병합(`e3a82c6`, STATUS #0186, M13-02)으로 `GATHER_ITEMS`(버섯 4·수지·꽃·베리)·`RAW_HEAL`(광대버섯 −20)·`ConsumeItem` 변경을 같은 방법으로 올렸다. 올리기 전 node 57/57 · 자동화 328/328.
- 콘솔 API → `LiveRevision` **13**, 서버 본문 LF SHA-1 = 저장소 파일(`e3a82c6`) SHA-1 = `e144af906b2f6b477db24353995361318caa4ec9` (72441자, 1691줄)
- 카탈로그 1.0 **215 → 220** — `item.chanterelle`·`item.penny_bun`·`item.yellowleg_bonnet`·`item.fly_amanita`·`item.resin`. 서버 코드의 작물·씨앗·상점·레시피·채집·날로 먹기 ID 전부를 `GetCatalogItems(1.0)` 와 대조 → `live 220 need 0`
- 새 플레이어(`rev13-…`) 실호출: `ClaimLoot`(꾀꼬리버섯 1·광대버섯 1·수지 2) → `"Revision":13` · `granted` 세 줄 그대로 / `ConsumeItem(item.fly_amanita)` → `{"itemId":"item.fly_amanita","heal":-20}` / 인벤토리 `item.chanterelle` 1 · `item.resin` 2. `ConsumeItem(item.chanterelle)` 는 날로 먹기 값이 없어 `bad_args` — 13.4 대로(광대버섯만 날로 먹는다)

**배포 상태 (2026-09-24, 감독 — Revision 14):** 요리 재료 병합(`e7230cd`, STATUS #0187, M13-06)으로 작업대 레시피 4(밀가루·설탕·식물성 오일·티백)·잡화점 4(소금·흑후추·딜·빈 병)·`RAW_HEAL` 한 표(13.3 작물 + 13.6 + 광대버섯)·새 `FillBottle`·산딸기잼 heal 10 을 같은 방법으로 올렸다. 올리기 전 node 60/60 · 자동화 329/329.
- 콘솔 API → `LiveRevision` **14**, 서버 본문 LF SHA-1 = 저장소 파일(`e7230cd`) SHA-1 = `d6e313f50ad8ed2805f6d7ee9a51a15506d72307` (74388자, 1725줄)
- 카탈로그 1.0 **220 → 229** — `item.flour`·`item.sugar`·`item.vegetable_oil`·`item.tea_bag`·`item.salt`·`item.black_pepper`·`item.dill`·`item.empty_bottle`·`item.water_bottle` (9건 모두 204). 대조에 `FILL_ITEMS` 를 더해 `live 229 need 0`
- 새 플레이어(`rev14-…`) 실호출: `GrantStarterKit` → `"coins":5` / `BuyItem(item.empty_bottle)` → `{"bought":"item.empty_bottle","count":1,"cost":1}` / `FillBottle(item.empty_bottle)` → `{"itemId":"item.water_bottle","count":1}` / `ConsumeItem(item.water_bottle)` → `"heal":15` / `BuyItem(item.salt)` → `ConsumeItem(item.salt)` → `"heal":5` / `FillBottle(item.salt)` → `bad_args`. 모두 `"Revision":14`, 끝 인벤토리 `item.club` 1 뿐(쓴 것 다 빠짐)

**배포 상태 (2026-09-24, 감독 — Revision 15):** 탈것 목줄 병합(`f6d8c6a`, STATUS #0188, M13-03)으로 `GetMounts`·`ExchangeCollar`·`AttachSaddle`·도감 100% 목줄 지급(`grantCodexCollars`)·안장 레시피 3(고급 작업대)을 같은 방법으로 올렸다. 올리기 전 node 64/64 · 자동화 332/332.
- 콘솔 API → `LiveRevision` **15**, 서버 본문 LF SHA-1 = 저장소 파일(`f6d8c6a`) SHA-1 = `382f6110e65e8a39ec8cce98ee0445208e180eef` (78615자)
- 카탈로그 1.0 **229 → 235** — `item.collar_hunting`·`item.collar_farming`·`item.collar_fishing`·`item.saddle_hunting`·`item.saddle_farming`·`item.saddle_fishing` (6건 모두 204). 대조에 `CODEX_COLLARS`·`SADDLE_ITEMS` 를 더해 `live 229 need 6` → 추가
- 새 플레이어(`rev15-…`) 실호출: `GrantStarterKit` → `"coins":5` / `GetMounts` → `{"mounts":[],"saddles":{},"species":[8종]}` / `ExchangeCollar(collar_hunting, wolf)` → `not_enough_items` / `ExchangeCollar(item.salt)` → `bad_args` / `AttachSaddle(saddle_hunting, wolf)` → `locked` / `AttachSaddle(…, animal.horse)` → `locked`. 모두 `"Revision":15`. 목줄을 실제로 바꾸는 성공 길은 도감 100% 계정이 필요해 실호출로 못 봤다 — node 시험(64/64)이 본다

**배포 상태 (2026-09-24, 감독 — Revision 16):** 요리 44 병합(`b39c85c`, STATUS #0189, M13-07)으로 불 레시피 44·`CRAFT_TAG_ITEMS`·`hasCraftTag`·태그 재료 판매가(`sellPriceOf`)·`RAW_HEAL` 따뜻한 탄산 25 를 같은 방법으로 올렸다. 올리기 전 node 67/67 · 자동화 333/333.
- 콘솔 API → `LiveRevision` **16**, 서버 본문 LF SHA-1 = 저장소 파일(`b39c85c`) SHA-1 = `2fa41f481d3262fdd56fb410122e01a3e5378c38` (83153자)
- 카탈로그 1.0 **235 → 280** — 요리 44 + `item.hot_soda` (45건 모두 204). 대조 `live 280 need 0`
- 새 플레이어(`rev16-…`) 실호출: `CraftItem(item.bread)` → `not_enough_items` `item.flour` need 2 / `CraftItem(item.roasted_vegetables)` → `not_enough_items` `Vegetable` need 2(태그 재료 길) / `CraftItem(item.no_such_dish)` → `bad_args` / `BuyItem(item.salt)` → `"cost":1` → `ConsumeItem(item.salt)` → `"heal":5`. 모두 `"Revision":16`. 요리 성공 길은 새 계정이 상점만으로 재료를 못 모아 실호출로 못 봤다 — node 시험이 본다

**배포 상태 (2026-09-24, 감독 — Revision 17):** 펫 배치 소모·도감 칸 필터 병합(`aa460ad`, STATUS #0190)으로 `RegisterPet` 종 확인·`item.pet_<종>` 1개 소모·`addToCodex` 카테고리 칸 필터를 올렸다. 카탈로그 변경 없음. 올리기 전 node 69/69 · 자동화 333/333.
- 콘솔 API → `LiveRevision` **17**, 서버 본문은 편집기가 CRLF 로 저장(85892자, SHA-1 `f09f8427a435a2f14ed2a5ebbe02d11555dd88ac`) — LF 로 되돌리면 저장소 파일(`aa460ad`) SHA-1 `6504e2bcd4cc16b33d65494477af0131ee0e50c9` (84004자)
- 새 플레이어(`rev17-…`) 실호출: `GrantStarterKit` → `"granted":true` / `RegisterPet(animal.fox, 0)` → `not_enough_items` `item.pet_fox` need 1 have 0 / `RegisterPet(animal.dragon, 0)` → `bad_args`. 모두 `"Revision":17`. 펫을 가진 계정의 성공 길은 포획이 필요해 실호출로 못 봤다 — node 시험이 본다

**배포 상태 (2026-09-24, 감독 — Revision 18):** 모자 9 병합(`749daa2`, STATUS #0191, M13-04)으로 `EquipHat`·`HAT_EFFECTS`·`wornHatEffect` 와 수확·판매·뱃삯·나무 효과, 잡화점 모자 7 을 올렸다. 올리기 전 node 72/72 · 자동화 335/335.
- 콘솔 API → `LiveRevision` **18**, 서버 본문(CRLF, 88410자) SHA-1 `465b5c47fe1a0b0cb610c08ea0a75d8f3684543b` = 저장소 파일 CRLF 변환 해시. LF 해시 `3ab6552ed5bdff282a4d16c2db9380963ad549af` (86473자)
- 카탈로그 1.0 **280 → 289** — 모자 9 (9건 모두 204). 대조 `live 289 need 0`
- 새 플레이어(`rev18-…`) 실호출: `EquipHat(item.farmer_hat)` 미보유 → `not_enough_items` / `EquipHat(item.salt)` → `bad_args` / `BuyItem(item.cook_hat)` → `"cost":5` → `EquipHat(item.cook_hat)` → `{"hat":"item.cook_hat"}`. 모두 `"Revision":18`. 농부·상인·여행자·나무꾼 효과 수치는 15코인 모자를 새 계정이 못 사 실호출로 못 봤다 — node 시험이 본다

**배포 상태 (2026-09-24, 감독 — Revision 19):** 도구 4 병합(`1922231`, STATUS #0192, M13-05)으로 `ClaimLoot` 도구 +1(전기톱 나무·마체테 채집, 가방 보유 + `min(toolUses, count)`, 나무꾼 ×2 전)과 잡화점 도구 4 를 올렸다. 올리기 전 node 74/74 · 자동화 340/340.
- 콘솔 API → `LiveRevision` **19**, 서버 본문(CRLF, 89082자) SHA-1 `405dfce5c619f550aeb74562bc195abc6e426890` = 저장소 파일 CRLF 변환 해시. LF 해시 `1cfa96679206448f034fc7282faf3e398f396d81` (87135자)
- 카탈로그 1.0 **289 → 293** — 도구 4. 대조 `live 293 need 0` (jev SUPPORTED p=0.71)
- 새 플레이어(`rev19-…`) 실호출: `GrantStarterKit` → `"granted":true,"coins":5` (jev SUPPORTED p=0.84) / `BuyItem(item.machete)` → `not_enough_coins` `"need":10` / `BuyItem(item.chainsaw)` → `"need":25` / `BuyItem(item.no_such_tool)` → `bad_args`. 모두 `"Revision":19`. 도구 +1 수치는 5코인 새 계정이 도구를 못 사 실호출로 못 봤다 — node 시험이 본다

**배포 상태 (2026-09-24, 감독 — Revision 20):** 음료·버프 병합(`b947b3c`, STATUS #0193, M13-08)으로 `ConsumeItem` heal 0(버프만) 허용, `RECIPES` +6 · `RAW_HEAL` +3 · `SHOP_ITEMS` +3(핫팩 5 · 따뜻한 탄산 3 · 에너지바 3), `sellPriceOf` 목장 생산물 2 · 낙각 3 을 올렸다. 올리기 전 node 76/76 · 자동화 343/343.
- 콘솔 API → `LiveRevision` **20**, 서버 본문(CRLF, 90723자) SHA-1 `069a889cf58fb4a507e68c856de27c7474ca728e` = 저장소 파일 CRLF 변환 해시. LF 해시 `0cb17c6fdd3a735659def3f923b20e9dce6be675` (88761자)
- 카탈로그 1.0 **293 → 302** — 음료·버프 9(`item.hot_soda` 는 이미 있음, 9건 모두 204). 대조 `live 302 need 0` (jev SUPPORTED p=0.70)
- 새 플레이어(`rev20-…`) 실호출(관찰 — jev NOT_SUPPORTED 0.44·0.68 이라 통과로 쓰지 않음): `BuyItem(item.heat_pack)` → `"cost":5` → `ConsumeItem(item.heat_pack)` → `{"itemId":"item.heat_pack","heal":0}` → 가방 `[["item.club",1]]` / `ConsumeItem(item.club)` → `bad_args`. 모두 `"Revision":20`. 버프 자체는 호스트가 표에서 건다 — 자동화 `Snarehaven.Buff.*` 가 본다

**배포 상태 (2026-09-24, 감독 — Revision 21):** 부품·텐트 병합(`4f2d2ad`, STATUS #0194, M13-09)의 `RECIPES` 작업대 5줄과 상자·보물 병합(`4b83c98`, STATUS #0195, M13-10)의 `CHEST_LOOT` 15줄 · `rollChest` · `OpenChest` · `UseTreasureMap` · `LOOT_SELL_PRICE` 를 올렸다. 올리기 전 node 81/81 · 자동화 345/345.
- 콘솔 API → `LiveRevision` **21**, 서버 본문(CRLF, 97360자) SHA-1 `996f74ec052fb93cef263573c93700f3b042f491` = 저장소 파일 CRLF 변환 해시. LF 해시 `4371f069c85ee5cd83b46b948b16624a509b0670` (95272자)
- 카탈로그 1.0 **302 → 314** — 부품 4(`item.resin_adhesive` · `item.cotton_fabric` · `item.tarp` · `item.life_jacket`) + 보석·보물 8(`item.ruby` · `item.sapphire` · `item.amethyst` · `item.aquamarine` · `item.emerald` · `item.treasure_map` · `item.mystery_bag` · `item.cash_bundle`), 12건 모두 204. 대조 `live 314 need 0` (jev SUPPORTED p=0.72)
- 새 플레이어(`rev21-…`) 실호출(관찰 — jev NOT_SUPPORTED 0.64·0.18 이라 통과로 쓰지 않음): `OpenChest({chest:1})` → `{"rolls":1,"items":[],"blueprints":[]}` → 다시 → `recharging` `"waitMs":596979` / `UseTreasureMap` → `not_enough_items` / (`rev21b-…`) `CraftItem({recipeId:"item.tent"})` → `locked` "작업대가 없다(텐트 먼저)". 모두 `"Revision":21`. 전리품 확률은 node 고정 씨앗 10만 번 굴림이 본다

**배포 상태 (2026-09-25, 감독 — Revision 34):** 늪 채집물(#0263)·늪 장어(#0264)·광부 모자 묶음 + 새 함수 `GrantTesterKit`(STATUS #0268). 테스터 키트 = 개발 빌드가 로그인 뒤 한 번 묻고, 서버가 테스터 목록(감독 아이폰 1명 — 10-16 부터 스크립트 하드코딩 대신 타이틀 Internal Data `testerIds`) 로 판정 — 목록 밖은 `not_tester`, 같은 `TESTER_KIT_VERSION` 두 번째는 `granted:false`. 주는 것: 탈것 8종 전부(`mounts.species`) · 코인 500 · 아이템 24줄(`TESTER_KIT`). 카탈로그 **342 → 351**(`item.mining_hat` · 늪 6 · `item.eel` · `item.grilled_eel`, 9건 모두 204). 대조 `서버 ID 250 · live 351 · 빠짐 3`(셋은 `item.grilled_`·`item.pet_`·`item.seed_` 접두사 조각 — 실제 ID 아님). 해시: 서버 본문 CRLF SHA-1 = 로컬 CRLF 해시 true, `LiveRevision` 34. 실호출(스크래치 smoke34.js) `GrantTesterKit` 새 계정 → `"code":"not_tester"` · `ClaimLoot item.honey_mushroom` → `"granted":[{"itemId":"item.honey_mushroom","count":1}]`.

**배포 상태 (2026-09-25, 감독 — Revision 33):** 섬 텐트 — `PlaceTent` 가 `zone`(`ranch` 기본·`hunt`)을 받아 텐트에 저장하고 응답에 돌려준다. 겹침은 같은 존끼리만, 한도 2개는 존 합산, 그 밖의 값은 `bad_args`. 실호출(스크래치 smoke33.js) `PlaceTent {x:0,y:0}` → `"zone":"ranch"` · 같은 칸 `zone:"hunt"` → `"zone":"hunt"` 성공 · `zone:"sea"` → `bad_args`. 카탈로그 변화 없음.

**이전 (Revision 32):** 평원 작물 — `GATHER_ITEMS` 에 `item.sugar_beet`·`item.wheat`(메제레움·라즈베리는 이미 있음). 실호출(스크래치 smoke32.js) `ClaimLoot` → 두 아이템 `granted`. 카탈로그 변화 없음.

**이전 (Revision 31):** 섬별 베리 채집 — `GATHER_ITEMS` 에 `item.blueberry`·`item.lingonberry`. 실호출(스크래치 smoke31.js) `ClaimLoot` → 두 아이템 `granted`. 카탈로그 변화 없음.

**이전 (Revision 30):** 채광·제련(`a789681`·`c6fe1ac`, M14)을 올렸다. `RECIPES` 제련 6줄(자리 `smelter` — 놓인 `item.smelter` 없으면 `locked`)·금속 부품·철판·고급 곡괭이·제련소, 잡화점 곡괭이 2, 덩어리 판매가 6, `ClaimLoot` 덩어리 6. 카탈로그 **+18 → 342**(누락 대조 18개 전부 204). 실호출(스크래치 smoke30.js): 제련소 전 `CraftItem item.copper_ingot` → `"code":"locked","need":"item.smelter"` · 텐트→판자 4→제련소 제작·설치 뒤 `"crafted":"item.copper_ingot","count":1`, 덩어리 2·석탄 1 차감.

**배포 상태 (2026-09-25, 감독 — Revision 29):** 의뢰인 병합(`60e0aa7`)으로 새 함수 `GetQuests` · `CompleteQuest` 를 올렸다. 새 아이템 없음(카탈로그 324 그대로). 크롬 붙여넣기 → Save and deploy.
- 콘솔 API → `LiveRevision` **29**, 서버 본문 = 저장소 파일 CRLF 변환(페이지 안 비교 true). CRLF SHA-1 `eeac0fee08063e7cc7ce90580dce8ba4347c1dcf`, LF SHA-1 `7ce39512d88da34bcb3415379306928d49a2550a`, 2277줄.
- 새 플레이어(`rev29c-…`) 실호출: `GetQuests` → 3개(순록고기 3·보상 5 포함) → `ClaimLoot` 순록고기 3 → 인벤 `SC 0`·고기 3 → `CompleteQuest` → `{"coins":5}` · 인벤 `SC 5`·고기 0 → 다시 → `already` → `GetQuests` 에 `done:true`. 다른 플레이어(`rev29-…`)에서 없는 아이템 → `not_enough_items`(인벤 불변), 없는 ID → `bad_args`.

**배포 상태 (2026-09-25, 감독 — Revision 28):** 셔츠·바지 병합(`eb8e598`, STATUS #0237, M9-05)으로 잡화점 `SHOP_ITEMS` 에 `item.flannel_shirt`·`item.work_pants`(각 5 코인·1벌)를 올렸다. 카탈로그 1.0 **322 → 324**(두 줄 204). 올린 파일 LF SHA-1 `2264f0d0…` · 서버 저장본(CRLF) `32a977ac…` · 2212줄. 새 플레이어(`rev28-…`) 실호출: `SellItem pig_hide 10` → 코인 20 · `BuyItem item.flannel_shirt` → `{"bought":"item.flannel_shirt","count":1,"cost":5}` · `BuyItem item.work_pants` → cost 5 · 인벤토리 `[["item.work_pants",1],["item.flannel_shirt",1]]` (jev SUPPORTED 0.74).

**배포 상태 (2026-09-25, 감독 — Revision 27):** 스냅샷 업적 병합(`6d5e0ce`, STATUS #0214, M13-28)으로 `GetCodexRewards` 응답에 `achievements`(업적 저장값 그대로 `{counters, done}`)를 더했다. 새 함수·새 아이템 없음(카탈로그 322). 올리기 전 node 93/93 · 자동화 380/380. 크롬 붙여넣기 배포 → 서버 조회 `LiveRevision 27` · 본문 SHA-1 = 로컬(CRLF) 같음. 실호출(새 계정): `GetCodexRewards Revision 27 achievements {"counters":{},"done":[]}` → `ClaimLoot` `item.pet_pig` 1 → `GetCodexRewards Revision 27 achievements {"counters":{"capture":1,"hunt":0,"codexPercent":0},"done":["ach.capture_1"]}` (jev SUPPORTED 0.84).

**배포 상태 (2026-09-24, 감독 — Revision 26):** 도감 칸 병합(`6e93430`, STATUS #0212, M13-26)으로 `GetCodexRewards` 응답에 `entries`(찬 칸 목록, 칸 밖 ID 뺌)를 더했다. 새 함수·새 아이템 없음(카탈로그 322). 올리기 전 node 92/92 · 자동화 379/379. 크롬 붙여넣기 배포 → 서버 조회 `LiveRevision 26` · 본문 SHA-1 = 로컬(CRLF) 같음. 실호출(새 계정): `GetCodexRewards Revision 26 entries []` → `ClaimLoot` `item.fox_tail` 1 → `GetCodexRewards Revision 26 entries ["item.fox_tail"]` (jev SUPPORTED 0.83).

**카탈로그 추가 (2026-09-24, 감독 — Revision 25 그대로):** 손도끼·바구니 병합(`052817e`, STATUS #0207, M9-04)으로 카탈로그 1.0 **321 → 322** — `item.gather_basket` 1건 204. 손도끼는 기존 `item.hatchet` 을 쓴다. CloudScript 는 바꾸지 않았다. 대조 `catalog 322` · `item.gather_basket true` · `item.hatchet true`.

**배포 상태 (2026-09-24, 감독 — Revision 25):** 모자 복원 병합(`2388b27`, STATUS #0199, M13-14)으로 `GetBaseState` 응답에 `hat`(지금 효과를 주는 모자 ID, 없으면 `""`)을 더했다. 새 함수·새 아이템 없음(카탈로그 321). 같이 합친 #0201(버프 HUD·보물 재물음)은 서버 변경 없음. 올리기 전 node 91/91 · 자동화 359/359.
- 콘솔 API → `LiveRevision` **25**, 서버 본문 SHA-1 = 저장소 파일 CRLF 변환 해시 `395f8fbc604998683737b9da83f32dc130293cbf`(페이지 안 비교 true). LF 해시 `8c7ba70be5b7ae7e6ed3cfd2a56de989c109ed9d`, 2201줄
- 새 플레이어(`rev25-…`) 실호출, 모두 `"Revision":25`: 사기 전 `GetBaseState` `{"hat":""}` → `EquipHat(item.merchant_hat)` 뒤 `{"hat":"item.merchant_hat"}` → `EquipHat("")` 뒤 `{"hat":""}` → 다시 쓰고 `DropItem(item.merchant_hat)` 뒤 `{"hat":""}` (jev SUPPORTED p=0.81)

**배포 상태 (2026-09-24, 감독 — Revision 24):** 보물 표식·도감 정리 병합(`b59b1aa`, STATUS #0200, M13-15)으로 새 함수 `GetTreasure` · 바뀐 도우미 `addToCodex`(칸 밖 옛 ID 거르기)를 올렸다. 새 아이템 없음(카탈로그 321). 올리기 전 node 90/90 · 자동화 354/354.
- 콘솔 API → `LiveRevision` **24**, 서버 본문 SHA-1 = 저장소 파일 CRLF 변환 해시 `69d8f4144a0835d37f08423a0dccc85482e4ec71`(페이지 안 비교 true). LF 해시 `1b9a6a76c99dacbf7272d745ebf0299031df342e`
- 새 플레이어(`rev24-…`, 상자 2개에서 지도가 나온 계정) 실호출, 모두 `"Revision":24`: `UseTreasureMap` → `zone.plains` 자리 · `GetTreasure` 가 같은 `treasureId`·`zoneId`·`u`·`v`(`same true`) · 그 id 로 `OpenChest` `"rolls":2` 뒤 `GetTreasure` → `{"treasure":null}` (jev SUPPORTED p=0.89). 칸 밖 옛 ID 정리는 그런 옛 계정이 없어 실호출 못 함 — node 시험이 본다
- 새 계정은 짧은 시간에 연달아 만들면 `LoginWithCustomID` 가 거절된다 — 계정 사이 4초·실패 시 10초 재시도로 풀었다

**배포 상태 (2026-09-24, 감독 — Revision 23):** 펫 빼기·모자 벗기 병합(`92aee1f`, STATUS #0198, M13-13)으로 새 함수 `RemovePet` · `EquipHat{itemId:""}` 벗기를 올렸다. 새 아이템 없음(카탈로그 321 그대로). 올리기 전 node 88/88 · 자동화 351/351.
- 콘솔 API → `LiveRevision` **23**, 서버 본문 SHA-1 = 저장소 파일 CRLF 변환 해시 `8bce55f145e77c3d0caf3ad6c462241a3e7079fe`(페이지 안 비교 true). LF 해시 `daeb6f0e684ec86a9b0fae137064b6465ffa8d3f`
- 새 플레이어(`rev23-…`) 실호출, 모두 `"Revision":23`:
  - `ClaimLoot(item.pet_pig)` → `RegisterPet(animal.pig, 0)` → `RemovePet(0)` `{"removed":true,"petItemId":"item.pet_pig","harvested":0}` → `GetBaseState` `{"ranch0":null}`, 가방에 `item.pet_pig` 1 (jev SUPPORTED p=0.82). 넣자마자 뺐으므로 `harvested` 0 — 30분 뒤 수확 계산은 node 시험이 본다
  - `RemovePet(0)` 다시 → `empty_slot` · `RemovePet(99)` → `bad_slot` (역검증 jev SUPPORTED p=0.80)
  - 상인 모자 쓰고 `SellItem(pig_hide ×3)` → `"coins":7` · `EquipHat("")` → `{"hat":""}` 뒤 같은 3개 → `"coins":6` (jev SUPPORTED p=0.72)

**배포 상태 (2026-09-24, 감독 — Revision 22):** 페인트 병합(`4ca3800`, STATUS #0196, M13-11)으로 `SHOP_ITEMS` 페인트 7(각 2코인) · `PAINT_ITEMS` · 새 함수 `PaintStructure` 를 올렸다. 올리기 전 node 85/85 · 자동화 346/346.
- 콘솔 API → `LiveRevision` **22**, 서버 본문(CRLF, 99227자) SHA-1 `4108ec987a0118f4339f6dc9cca5e5f71a215b12` = 저장소 파일 CRLF 변환 해시. LF 해시 `6921891b568985987c38503f34b6c71cd08d13c2` (97107자)
- 카탈로그 1.0 **314 → 321** — 페인트 7(`item.paint_white` · `item.paint_charcoal` · `item.paint_blue` · `item.paint_red` · `item.paint_yellow` · `item.paint_mint` · `item.paint_blush`), 7건 모두 204. 대조 `live 321 need 0` (jev SUPPORTED p=0.79)
- 새 플레이어(`rev22-…`) 실호출(관찰 — jev NOT_SUPPORTED 0.63 이라 통과로 쓰지 않음): `BuyItem(item.paint_red)` → `"cost":2` / `PaintStructure(paintId:item.club)` → `bad_args` / 없는 텐트 → `not_found` / 가방 `[["item.paint_red",1],["item.club",1]]`. 모두 `"Revision":22`. 성공 칠하기는 가구 놓인 계정이 없어 실호출 못 함 — node 시험이 본다

## 클라이언트 규칙
- 요청 → 응답 받은 뒤에만 UI 반영. 실패 시 큐에 넣고 재시도.
- 로컬 세이브는 캐시. 충돌 시 백엔드 우선.

### 계정 삭제 (DESIGN 4.4 「계정 삭제」 · Apple 5.1.1(v)·구글플레이)
| 무엇 | 어떻게 |
|---|---|
| 흐름 | 로비 `설정` → `계정 삭제` → 확인 두 번(`삭제 (1/2)`·`지운다 (2/2)`, 취소 있음) → `USnareBackendSubsystem::DeleteAccount` → CloudScript `DeleteMyAccount{confirm:"DELETE"}` → 답이 `deleted:true` 일 때만 기기 ID Unlink → 캐시 파일 `SnarePlayerCache` 삭제 → 로그아웃 → (자동 로그인 상황이면) 새 게스트로 다시 로그인. 실패면 캐시·로그인 그대로, 상태 줄에 사유 |
| 지워지는 것 | `DeletePlayer` 성공 시 **이 타이틀의 플레이어 계정 전체**(Internal Data — 목장·낚시터·텐트·도감·업적 등, 인벤토리, `SC`·`GM`, 통계) |
| 남는 것 — 사람 몫 | ① **마스터 플레이어 계정**(스튜디오 네임스페이스 단위)과 PlayStream 이벤트 기록은 `DeletePlayer` 로 안 지워진다. CloudScript 에서 Admin `DeleteMasterPlayerAccount` 를 부를 수 없으므로 운영자가 콘솔 *Players → Delete* 또는 Admin API 로 지운다. 대상은 타이틀 이벤트 `account_delete_requested` 로 찾는다(주기: 🟡 매주 — STORE_READINESS 표 E 3번 답이 오면 그걸 따른다). ② `DeletePlayer` 가 실패해 비우기로 대신했으면(`fullyDeleted:false`) 빈 계정이 남는다 — 같은 이벤트로 찾아 위와 같이 지운다 |
| 같은 기기로 다시 들어오면 | 클라이언트가 삭제 성공 뒤 플랫폼별 `UnlinkIOSDeviceID`/`UnlinkAndroidDeviceID`/`UnlinkCustomID` 로 기기 ID 연결을 끊는다 → 다음 `CreateAccount` 로그인이 **새 계정**을 만든다. `DeletePlayer` 로 세션이 이미 끊겨 Unlink 가 실패해도 다시 붙는 계정은 데이터가 빈 계정이라(시작 장비부터 다시) 지운 진행이 돌아오지 않는다 🟡 (실서버 미확인) |
| EOS | 처리 불필요 — EOS Connect 는 빈 토큰의 익명 기기 ID(PUID)만 쓰고 우리 개인 데이터를 EOS 에 두지 않는다. 필요해지면 `EOS_Connect_DeleteDeviceId` 로 기기 자격을 지울 수 있다 |
| 확인 | 2층 `node tools/playfab/test_cloudscript.js`(「4.4 계정 삭제」 두 줄) · 자동화 `Snarehaven.Account.Delete` 두 개. 실서버 `server.DeletePlayer` 호출 가능 여부는 **배포 뒤 3층에서 확인**(🟡 — 실패해도 비우기 길로 떨어진다) |

### 보유 상태 읽기 (M2-05, 루프#48)
| 무엇 | 어떻게 |
|---|---|
| 조회 | 로그인 성공 직후 `GetUserInventory` **한 번**. 인벤토리와 가상 재화가 같은 응답에 와서 코인만 따로 묻지 않는다 |
| 개수 | `FItemInstance.RemainingUses` 가 있으면 그 값, 없으면 인스턴스 한 줄 = 1개. 같은 `ItemId` 는 한 칸으로 모은다(슬롯형 — DESIGN 3.3) |
| 아이템 ID | PlayFab 카탈로그 `ItemId` 를 `DT_ItemTable` 의 `ItemID`(`item.pig_meat` …) 와 **같은 문자열**로 쓴다 (감독 확정 2026-09-20 — 카탈로그는 M2-03 콘솔 작업에서 만들 때 이 이름을 그대로 쓴다) |
| 캐시 파일 | `USnarePlayerCacheSave`, 슬롯 이름 `SnarePlayerCache`, 형식 번호 1. 형식 번호가 다르거나 주인(`PlayFabId`)이 지금 계정과 다르면 **읽지 않고 버린다** |
| 언제 적나 | 백엔드 응답이 왔을 때만. 로컬에서 만든 값은 파일에 남기지 않는다 — 다음 부팅에 진짜처럼 보인다 |
| 언제 읽나 | 부팅 때 한 번(표시 전용). 백엔드 응답이 오면 합치지 않고 통째로 덮어쓴다 (DESIGN 4.5 충돌 시 백엔드 우선) |

**배포됨 Rev 34 (2026-09-25, 감독 — 늪 채집물, STATUS #0263):** `GATHER_ITEMS` +6(`item.honey_mushroom` · `item.king_stropharia` · `item.oyster_mushroom` · `item.vipers_mane` · `item.bog_flower` · `item.water_lily`), `CRAFT_TAG_ITEMS.Mushroom` +3(식용 늪 버섯 셋 — 독사갈기버섯 제외). 새 함수 없음. 카탈로그 **+6**(위 여섯, DESIGN 13.4). 광부 모자와 함께 크롬 묶음 배포

**배포됨 Rev 34 (2026-09-25, 감독 — 늪 장어, STATUS #0264):** `FISH_TABLE` +`item.eel`(120분·40) · `GRILLED_HEAL` +`item.eel` 60 · `FISH_HABITAT.swamp` · `FISHING_SPOTS` +`swamp_1`·`swamp_2`(늪) · `fishWeights` 늪 = 민물 + 장어. 새 함수 없음. 카탈로그 **+2**(`item.eel` · `item.grilled_eel`). 위 묶음과 같이 크롬 배포

**배포됨 Rev 35 (2026-10-02, 감독 — 목장 동물 칸 재고, df7e622):** `GetBaseState` 가 동물 칸 `stock` 을 `animalHarvestUnits` 로 채운다. 새 함수 없음·카탈로그 변화 없음. 배포 전 라이브(Rev 34)가 7e5df99 판과 같음을 확인 뒤 develop 판을 크롬으로 올림. 검증: `LiveRevision`=35, 서버 본문(\r 제거) = 저장소 파일 일치. 실호출 LoginWithCustomID → ExecuteCloudScript `GetBaseState` → `code 200 · Revision 35 · Error 없음`, 키 `ranch·ranchUpkeep·fishing·tents·boat·hat`. 노드 시험 104/104

**배포됨 Rev 36 (2026-10-02, 감독 — 사용자 "올려"):** `origin/develop` 판(`eefa6ab` 이후 서버 파일 그대로, 2425줄) — `DeleteMyAccount`(계정 삭제)·M19-07 `fishingHarvestUnits`·M20 서버 변경(M20-02b 상자 전자 재료 포함)을 한 번에 올렸다. 배포 전 라이브 Rev 35 확인, node 109/109. 검증: `LiveRevision`=36, 서버 본문 = 로컬 파일(LF→CRLF) SHA-1 일치(115918자). 실호출(일회용 `rev36-del-…`): `GetBaseState` → `code 200 · Revision 36`, 키 `ranch·ranchUpkeep·fishing·tents·boat·hat` / `DeleteMyAccount({})` → `bad_args` / `DeleteMyAccount({confirm:"DELETE"})` → `{"deleted":true,"fullyDeleted":false}`(`server.DeletePlayer` 는 레거시 CloudScript 에서 예외 → 비우기 경로).

**배포됨 Rev 37 (2026-10-02, 감독 — 사용자 "올려"):** `7b81396e`(M21-01a 계정 첫 펫 첫 생산 1분) 판 서버 파일 그대로, 2439줄. 배포 전 라이브 Rev 36 확인. 검증: `LiveRevision`=37, 서버 본문 = 로컬 파일(LF→CRLF) SHA-1 일치(116772 UTF-16 글자 = 116750자 + 4바이트 글자 22개). 실호출(일회용 `rev37-…`): `GetBaseState` → `Revision 37` / `ClaimLoot(item.pet_pig)` → 받음 / `RegisterPet(animal.pig, 0)` → `success` / 즉시 `HarvestRanch` → `harvested 0` / 65초 뒤 → `{"harvested":1,"itemId":"item.truffle","coins":10,"fame":1}` / 바로 다시 → `harvested 0`(두 번째부터 30분) / `DeleteMyAccount` 로 정리. 새 아이템 없음 — 카탈로그 그대로 357.
- 카탈로그 1.0 **351 → 357** — M20-02 전자 재료 6종 `item.polymer`·`item.circuit_board`·`item.electric_motor`·`item.solar_kit`·`item.battery`·`item.light_bulb`(Durable·겹치기 끔, 표시 이름 영문). 서버 코드 `item.*` 258개 + `DT_ItemTable` 333개를 Client `GetCatalogItems(1.0)` 와 대조 → 실제 누락 0(남은 `item.pet_`·`item.seed_`·`item.grilled_` 는 코드 속 접두사 문자열)
- 이름 바꾼 14개(M21 법무, 보물꾼 모자 등)는 카탈로그 표시 이름을 안 바꿨다 — 카탈로그 이름은 ID 영문이라 게임 화면·빌드에 안 나가고, 게임은 `DT_ItemTable` 이름을 쓴다

**배포됨 Rev 47 (2026-10-10, 감독 붙여넣기·배포 클릭):** `35c10af6` 판 서버 파일 그대로(시험 191/191). Rev 46 뒤 서버 변경 — M83 꾸미기 23종(`OUTFIT_ITEMS`·`COSMETIC_GRADES`, 풀 68종). 카탈로그 1.0 에 같은 ID 23개 추가(423→446, `GetCatalogItems` 로 23개 모두 확인). 검증: `LiveRevision`=47, 서버 본문 = 로컬 파일(true). 실호출(일회용 새 계정): `SetOutfit` head `item.meadow_crown`·`item.harbor_horned` → `not_enough_items`(꾸미기로 인식) · head `item.meadow_patch_pants` → `bad_args`(다른 칸) · `GetWardrobeShop` 진열에 `item.harbor_sail_shorts`·`item.meadow_star_pants` 등장.

- 배포됨 Rev 61 (2026-10-18): `GiftItem` — 받는 사람이 보낸 사람을 차단했으면 `blocked` 로 거절. 서버 본문 일치 확인, 4283줄.
- 배포됨 Rev 62 (2026-10-18): 시작 코인 `STARTER_COINS` 5 → 10 (M107-05 결함 ②). 서버 본문 일치 확인, 4283줄.
- 배포됨 Rev 63 (2026-10-19): 스토리 5장 1단계·QUEST_POOL 재료 송로 → 알 (`e2f9cbe1`, 송로는 기본 플레이에서 못 얻음). 서버 본문 일치 확인, 4283줄.
- 배포됨 Rev 64 (2026-10-19): 아이템 표시 이름 11건 레퍼런스 게임 회피(`987350ec`, 루프#1304 — 서향 열매·접이식 낚싯대 등). 서버 본문 일치 확인, 4283줄.
- 배포됨 Rev 65 (2026-10-20): M108 꾸미기 서버 판정 — 15° 회전·반 칸 격자(`hx,hy`)·벽 장식(`wallHangFail`, 액자·벽시계·벽 선반)·탁자/보관함 위 소품 슬롯(`top`) (`aecdb4f5`·`ce6ae066`·`b5542f1c`·`5e45810b`). 시험 245/245 통과, 서버 본문 일치 확인(새로 고친 뒤 편집기 전체 복사 = 저장소 파일), 4371줄.
- 배포됨 Rev 66 (2026-10-20): M108 전기 가구·발전기·나무 계단·조명·마당 바깥·벽 장식·러그와 벽 장식 3·위 소품 3 제작법(`53dfb9c2` 까지). 서버 본문 일치 확인, 4573줄, `GetBaseState` 실호출 Revision 66 오류 없음.
- 배포됨 Rev 67 (2026-10-20): M109 수확 이월 결함 고침(`ceb38395`)·목장·낚시터 수확 코인 하루 상한 900(`1b4ffa97` 까지). 서버 본문 일치 확인, 4601줄, `GetBaseState` 실호출 Revision 67 오류 없음.
- 배포됨 Rev 60 (2026-10-17): 테스터 키트 v6 — 총·탄은 `GUNS`, 먹는 것은 `RECIPES`·`RAW_HEAL` 에서 모아 지급(로켓 30·탄 200). 서버 본문 일치 확인, 4280줄.
**배포됨 Rev 59 (2026-10-17, 감독 붙여넣기·배포 클릭):** `09c60eca` 판 서버 파일 그대로(4274줄, 2층 238/238) — 스토리 2장 8단계 건네기 당근 3·감자 2·밀 2(루프#1247 보정), 9단계 `petKinds` 종류 수 세기(`absolute`). 배포 뒤 드롭다운 「Revision 59 (live)」, 서버 본문 = 로컬 파일 일치(불리언). 카탈로그 변화 없음.

**배포됨 Rev 58 (2026-10-16, 감독 붙여넣기·배포 클릭):** `32704f0b` 판 서버 파일 그대로(`598e7bbc` 잡화점 당근·감자·밀 씨앗, 2층 237/237). 배포 전 라이브 = Rev 57 판 일치, 배포 뒤 서버 본문 = 로컬 파일 일치(불리언). 실호출(새 계정 `director_smoke58`): `GrantStarterKit` rev 58 coins 5 → `BuyItem item.seed_carrot x2` `{"bought":"item.seed_carrot","count":2,"cost":2}` · `item.seed_potato x1` cost 2 성공 · `item.seed_wheat` 는 `not_enough_coins need 4 have 1`(정상 판정).

**배포됨 Rev 57 (2026-10-16, 감독 붙여넣기·배포 클릭):** `ed14254a` 판 서버 파일 그대로(2층 237/237, 서버 본문 = 로컬 파일 글자 일치 true, `LiveRevision`=57). 배포 전 라이브는 Rev 56(이 문서에 기록 없는 판, 신고·차단·`testerIds` 둘 다 없었음). 바뀐 것 — 신고·차단 `ReportPlayer`·`BlockPlayer`·`UnblockPlayer`·`GetBlockList`, 테스터 목록을 스크립트 하드코딩 대신 타이틀 Internal Data `testerIds`(JSON 배열 2명: 감독 아이폰·사람 TF 계정, 값은 Game Manager 에만) 에서 읽기. 카탈로그 변화 없음. 실호출(일회용 `director_smoke57`, 모두 rev 57): `GrantTesterKit` → `not_tester` / `GetBlockList` → `{"blocks":[],"limit":100}` / `BlockPlayer`·`ReportPlayer`(잘못된 id) → `bad_args`.

**배포됨 Rev 53 (2026-10-14, 감독 붙여넣기·배포 클릭):** `2015a816` 판 서버 파일 그대로, 3828줄(시험 218/218, 서버 본문 189,483자 = 로컬 파일과 글자 일치, `LiveRevision`=53). Rev 51 뒤 서버 변경 — 건네기 `GiftItem`·`GetGifts`·`AcceptGift`·`DeclineGift`(M104-05), 금고 `OpenVault`·열쇠패 굴림(M104-08a), 의뢰 줄기 `GetQuestChain`·`CompleteQuestChainStep`(M105-03). (Rev 52 는 저장만 하고 배포 안 한 판 — 건너뜀.) 카탈로그 1.0 에 13개 추가(569→**582**): `item.vault_pass` 금고 열쇠패 + 총 상점(8.6-c) 총 9 `item.pistol_auto`·`rifle_burst`·`rifle_semi`·`shotgun`·`shotgun_auto`·`shotgun_short`·`smg`·`mg`·`crossbow` + 탄 3 `item.ammo_rifle`·`ammo_shotgun`·`arrow`. 서버 코드의 `"item.*"` 문자열 중 카탈로그 누락은 이제 접두사 3개(`item.pet_`·`item.seed_`·`item.grilled_`, 이어 붙이는 용)뿐. 실호출(새 계정, 모두 rev 53): `GetQuestChain` → step 0, 1단계 나무 30·산딸기 10·코인 10 / `CompleteQuestChainStep{step:1}` → `chain_short`(나무 0/30) / `{step:3}` → `chain_locked`(current 1) / `GetGifts` → `{"gifts":[],"limit":10}` / `OpenVault{}` → `bad_args`(ruin 1~3) / `BuyItem item.arrow` → `not_enough_coins`(새 계정 5코인 — 지급 경로는 카탈로그 조회로만 확인).

**배포됨 Rev 51 (2026-10-10, 감독 붙여넣기·배포 클릭):** `68b0169a` 판 서버 파일 그대로(시험 204/204, 페이지 안 SHA-1 일치 = true). Rev 50 뒤 서버 변경 — 과일나무 8.18(`GetFruitTrees`·`PlantFruit`·`HarvestTree`·`ChopTree`·`PickFriendFruit`, 과일 `SellItem` 값, 없는 방장 거부·방장 한도 3). 카탈로그 1.0 에 `item.pear`·`item.peach`·`item.cherry`·`item.orange`·`item.plum` 추가(564→569). 실호출(새 계정): `GetFruitTrees` → rev 51, native `item.peach`, 나무 2그루 · `PickFriendFruit`(가짜 hostId) → `{"error":"없는 방장이다","code":"bad_args"}` · `GetCatalogItems` 569, 과일 5개 모두 true.

**고구마 실계정 확인 (2026-10-11 09:01 KST = UTC 일요일, 라이브 Rev 51, 감독):** 고정 시험 계정. `GetSweetPotato` → `{"weekday":0,"slot":-1,"buyPrice":11,"cap":100}` · 코인 0 일 때 `BuySweetPotato {bundles:1}` → `not_enough_coins`(need 110) · 코인 만든 뒤 `BuySweetPotato` → `{"bought":"item.sweet_potato","count":10,"cost":110,"boughtThisWeek":10}`, 가방 SC 210→100·고구마 10. 팔기는 같은 계정으로 월요일(UTC) 확인.

**배포됨 Rev 50 (2026-10-10, 감독 붙여넣기·배포 클릭):** `0d9573b6` 판 서버 파일 그대로(시험 196/196, 페이지 안 SHA-1 일치 = true). Rev 49 뒤 서버 변경 — 고구마 시세 8.17(`GetSweetPotato`·`BuySweetPotato`·`SellSweetPotato`, `GetBaseState` 썩음). 카탈로그 1.0 에 `item.sweet_potato`·`item.sweet_potato_rotten` 추가(562→564). 실호출(새 계정, 토요일): `GetSweetPotato` → `{"rev":50,"weekday":6,"slot":10,"price":4,"prevPrice":5,"cap":100}` · `BuySweetPotato` → `closed` · `SellSweetPotato` → `not_enough_items`(have 0) · `GetCatalogItems` 두 ID 모두 true.

**배포됨 Rev 49 (2026-10-10, 감독 붙여넣기·배포 클릭):** `2f872e58` 판 서버 파일 그대로(시험 191/191). Rev 48 뒤 서버 변경 — M92 옷 14벌(`OUTFIT_ITEMS`·`COSMETIC_GRADES`, 풀 170→184). 카탈로그 1.0 에 M92 새 ID 14개 추가(548→562, `GetCatalogItems` 로 빠진 것 0). 검증: `LiveRevision`=49, 서버 본문 = 로컬 파일(true). 실호출(일회용 새 계정): `SetOutfit {slot:top, itemId:item.meadow_plum_tee}` → `not_enough_items`(서버가 꾸미기로 앎) · `item.harbor_dive_flag` bottom → `not_enough_items` · `item.not_real_x` → `bad_args` · `GetWardrobeShop` 이번 주 진열에 새 옷 `item.harbor_meteor`(젬 500) 등장.

**배포됨 Rev 48 (2026-10-10, 감독 붙여넣기·배포 클릭):** `81193c03` 판 서버 파일 그대로(시험 191/191). Rev 47 뒤 서버 변경 — M84~M91 꾸미기(`OUTFIT_ITEMS`·`COSMETIC_GRADES` 늘림). 카탈로그 1.0 에 빠진 DT 꾸미기 102개 추가(446→548, `GetCatalogItems` 로 102개 ID·표시 이름 모두 확인, 빠진 것 0). 검증: `LiveRevision`=48, 서버 본문 = 로컬 파일(true). 실호출(일회용 새 계정): `SetOutfit` head `item.meadow_crown` → `not_enough_items` · `item.meadow_patch_pants` → `bad_args`(다른 칸) · `GetWardrobeShop` 진열에 새 옷 `item.meadow_plaid_shorts`·`item.harbor_fish_tee` 등장. 젬 구매 끝까지(같은 날, 일회용 새 계정 + 콘솔 「Modify Virtual Currency」로 GM 500): `BuyItem {itemId:item.meadow_plaid_shorts, currency:GM}` → `{"bought":…,"cost":200,"currency":"GM"}`(GM 500→300) · `SetOutfit {slot:bottom}` → `{"outfit":{"bottom":"item.meadow_plaid_shorts"}}`. 칸 이름은 `OUTFIT_SLOTS` = head·face·top·bottom·shoes·gloves·back.

**배포됨 Rev 46 (2026-10-08, 감독 붙여넣기·배포 클릭):** `e550357e` 판 서버 파일 그대로. Rev 45 뒤 서버 변경 — `NICKNAME_MIN_LENGTH` 2→3(PlayFab 표시 이름 최소 3자와 맞춤). 검증: `LiveRevision`=46, 서버 본문에 `NICKNAME_MIN_LENGTH = 3` 포함. 실호출(일회용 새 계정): `SetNickname` 「곰돌」 → `bad_length`(min 3) · 「곰돌이」 → `{"name":"곰돌이"}` + `UpdateUserTitleDisplayName` 200 · 「Bear12」 → 200.

**배포됨 Rev 45 (2026-10-07, 감독 붙여넣기·사람 배포 클릭):** `fbb96969` 판 서버 파일 그대로. Rev 44 뒤 서버 변경 — `LoadAmmo`(탄약 소비 장전), 잡화점 권총·탄약, `GUNS` 소총 탄창 5, 소총 `RECIPES`. node 180/180. 검증: `LiveRevision`=45, 서버 본문 SHA-1 = 로컬(CRLF 변환) 일치, `LoadAmmo` 포함. 실호출(일회용 새 계정, `rev=45`): `LoadAmmo{gun:"item.pistol_semi"}` → `no_gun`. 카탈로그 3건(`item.pistol_semi` Semi-Auto Pistol·`item.ammo` Ammo·`item.rifle_modern` Modern Rifle) 감독 콘솔 등록(10-07, 카탈로그 423, 스크립트 아이템 누락 0). 실호출(새 계정, `rev=45`): `BuyItem` 권총 → `cost:30` · 탄약 → `count:25,cost:5` · `LoadAmmo{gun:"item.pistol_semi",have:0}` → `{"loaded":7,"removed":7,"left":18}`.

**배포됨 Rev 44 (2026-10-07, 감독 붙여넣기·사람 배포 클릭):** `6cbc6b14` 판 서버 파일 그대로. Rev 43 뒤 서버 변경 — `tryGiveItems` 도우미: `PickupDrop`·텐트 구조물 회수·보관함 꺼내기·배 넣기가 **지급 먼저, 성공해야 기록 삭제**(지급 실패 시 `grant_failed` + `playFabError`, 아이템 증발 막음). node 174/174. 검증: `LiveRevision`=44, 서버 본문(CR 제거) SHA-1 = 로컬 `349f413b…` 일치, `function tryGiveItems` 포함. 실호출(일회용 새 계정, 모두 `rev=44`): `PickupDrop{dropId:"nope"}` → `bad_args` / `MoveToStorage{boat:true}` → `locked`.

**배포됨 Rev 43 (2026-10-06, 감독):** `b70d8391` 판 서버 파일 그대로, 3088줄. Rev 42 뒤 서버 변경 — 배 보관함(`MoveToStorage`/`MoveFromStorage` `boat:true`, 배낭에 `item.boat_container` 있어야 열림, 주인만), 보물상자 `OpenChest`, 닉네임 `SetNickname`(2~12자), 가챠 희귀 소재 판매가(등급 코인 곡선) 등. 배포 전 카탈로그 1.0 을 378 → **420** (옷 42종 등), 서버 코드 ID 누락 0(남은 건 실재 아닌 접두어 `item.pet_`·`item.seed_`·`item.grilled_` 뿐). node 173/173. 검증: `LiveRevision`=43, 서버 본문(CR 제거) SHA-1 = 로컬 `527e07fe…` 일치. 실호출(일회용 새 계정, 모두 `rev=43`): `MoveToStorage{boat:true}` → `locked`(보트 컨테이너가 없다) / `SellItem(gacha.mat_hide_5)` → `not_enough_items`(have 0 — `not_sellable` 아님 = 판매가 붙음, 값 10 은 node 시험) / `SetNickname("가")` → `bad_length`(min 2, max 12).

**배포됨 Rev 42 (2026-10-04, 감독 — 상점 개편 C):** `0ebbbb00` 판 서버 파일 그대로, 2831줄. Rev 41 뒤 서버 변경 — 새 함수 `SetPetCosmetic {slot, itemId}`(동물 칸 꾸미기 1칸 씌우기·벗기기, 배낭 칸 먼저 검사), `RemovePet` 이 씌운 꾸미기도 돌려줌(답에 `cosmetic`), 상자 `item.crate_autumn26` 풀 8종(일반 15%×5 · 희귀 10%×2 · 에픽 5%, 합 100%), 옷장 상점 풀 +3, 동물 꾸미기는 여러 개 사도 된다(옷·모자만 `already`). 배포 전 카탈로그 1.0 에 `item.ranch_ribbon`·`item.ranch_necklace`·`item.ranch_hat` 를 넣어 375 → **378**, 서버 코드 ID 누락 0. node 141/141, 에디터 시험 86/86. 검증: `LiveRevision`=42, 서버 본문 = 로컬 파일 SHA-1 일치. 실호출(일회용 `director_smoke42`, 모두 `rev 42`): `RegisterPet(animal.pig, 0)` → `success` / `SetPetCosmetic(0, item.ranch_ribbon)` → `not_enough_items`(have 0) / `SetPetCosmetic(0, item.chief_hat)` → `bad_args`(동물 꾸미기 아님) / `SetPetCosmetic(0, "")` → `already`(씌운 것 없음) / `RemovePet(0)` → `{"removed":true,…,"cosmetic":""}`.

**배포됨 Rev 41 (2026-10-04, 감독 — 상점 개편 A·B):** `55e92846` 판 — 꾸미기 상자·열쇠(사냥·낚시 2% 드롭, 열쇠 잡화점 코인 50 또는 젬), `OpenCrate`, 주간 옷장 젬 상점 `GetWardrobeShop`·`BuyItem` 옷장 갈래. 실호출(일회용 `director_crate41`, 끝에서 끝까지): `ClaimLoot item.gold_chunk` 28번째에 상자 드롭 → `SellItem` 금 22 → 코인 176 → `BuyItem(item.crate_key, SC)` → `cost 50` → `OpenCrate(item.crate_autumn26)` → `{"itemId":"item.chief_hat","grade":"epic"}` → 배낭에 상자 0(소모됨).

**배포됨 Rev 40 (2026-10-04, 감독 — 배포 위임):** `666d016e` 판 서버 파일 그대로, 2666줄. Rev 39 뒤 서버 변경 2개 — M21-07e `HAT_EFFECTS` 에 `item.chief_hat`(겉모습만), M21-07g `VARIANT_CHANCE` 0.03 고지값. 카탈로그 추가 없음(373, `item.chief_hat` 이미 있음). 검증: `LiveRevision`=40, 서버 본문 = 로컬 파일(LF→CRLF) SHA-1 일치, `chief_hat` 포함. 실호출(일회용 `cloudscript_verify_rev40_…`, 모두 `rev 40`): `EquipHat item.chief_hat` → `not_enough_items`(모자로 인식 — Rev 39 는 `bad_args` "모자가 아니다"), `ClaimLoot item.chief_hat` → `bad_args`(주간 보상 전용이라 줍기 불가 — 의도), `EquipHat ""` → `{hat:""}`.

**배포됨 Rev 39 (2026-10-03, 감독 — 배포 위임):** `d507f49e` 판 서버 파일 그대로, 2662줄. Rev 38 뒤 서버 변경 2개 — M21-07a1 변종 펫 아이템 12(받기 허용·판매가), M21-07b `RegisterPet`·`RemovePet` 의 `variant`·변종 칸 주기 ÷1.5·도감 `variant` 12칸. 배포 전 카탈로그 1.0 에 변종 펫 12종(`item.pet_<종>_variant`, Durable)을 넣어 361 → **373**, 서버 코드 ID 누락 0. node 131/131. 검증: `LiveRevision`=39, 서버 본문 = 로컬 파일(LF→CRLF) SHA-1 일치. 실호출(일회용 `cloudscript_verify_rev39_…`, 모두 `Revision 39`):
- `ClaimLoot(item.pet_fox_variant ×2)` → `{"granted":[{"itemId":"item.pet_fox_variant","count":2}]}`
- `RegisterPet(animal.pig, variant)` → `{"error":"변종이 없는 종: animal.pig","code":"bad_args"}` / `RegisterPet(animal.fox, slot 0, variant)` → `{"success":true,"slot":0,"animalId":"animal.fox","variant":true}`
- `SellItem(item.pet_fox_variant, 1)` → `{"sold":"item.pet_fox_variant","count":1,"coins":10}`

**배포됨 Rev 38 (2026-10-03, 감독 — 사용자 "올려"):** `22f06df9` 판 서버 파일 그대로, 2639줄. Rev 37 뒤 쌓인 서버 변경 9개를 한 번에 올렸다 — M20-03a 집 2층·계단 키트, M22-01 `ClaimLoot` 허용 목록, M22-04b 펫 판매가 10, M22-06 배낭 제작 8, M20-04a 전력 가구, M21-05a·05c 7일 출석·새싹 모자, M21-06b1 `ReportCoop`, M21-07d `ClaimBoss`. 배포 전 라이브 Rev 37 확인, node 127/127. 검증: `LiveRevision`=38, 서버 본문 = 로컬 파일(LF→CRLF) SHA-1 일치. 실호출(일회용 `cloudscript_verify_rev38_…`, 모두 `Revision 38`):
- `ClaimLoot(item.made_up_thing)` → `{"error":"받을 수 없는 아이템: item.made_up_thing","code":"bad_args"}`(지어낸 ID 막힘) / `ClaimLoot(item.pet_pig)` → `{"granted":[{"itemId":"item.pet_pig","count":1}]}`
- `SellItem(item.pet_pig, 1)` → `{"sold":"item.pet_pig","count":1,"coins":10}`
- `GetAttendance` → `nextSlot 1 · claimedToday false`, 7째 칸 `item.sprout_hat` / `ClaimAttendance` → `{"slot":1,"reward":{"coins":50}}` / 다시 → `already`
- `ClaimBoss` → `{"week":2961,"won":false,"reward":null}`
- 카탈로그 1.0 **357 → 361** — `item.stair_kit`·`item.lamp_electric`·`item.sprout_hat`·`item.chief_hat`(Durable·겹치기 끔, 표시 이름 영문). 서버 코드 `item.*` 274개를 `GetCatalogItems(1.0)` 와 대조 → 실제 누락 0(남은 셋은 접두사 문자열). `item.chief_hat` 은 M21-07e(행·메시) 전이지만 `ClaimBoss` 당첨이 카탈로그에 없는 아이템을 주지 않게 같이 넣었다
