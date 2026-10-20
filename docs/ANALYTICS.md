# ANALYTICS — 분석 이벤트 현황표 (M8-00)

> **이 문서는 "무엇을 · 언제 · 누가 보내나" 를 한 장으로 모은 것이다.** 값을 정하는 곳이 아니다 —
> 이벤트 이름은 M8-01 이 `DT_AnalyticsEventTable` 에, 보내는 코드는 M8-02(통로)·M8-03~06(자리)이 쓴다.
> 근거: `docs/DESIGN.md` 11(재화 소스/싱크)·12(분석/로그 줄) · `docs/BACKEND.md` · 조각 목록은 `docs/ROADMAP.md` M8 절.
> **범위 밖:** 샘플링 비율·보관 기간·대시보드 구성 — 이 문서에 **새 숫자를 적지 않는다**(지금 0개). 수집 항목 신고는 M8-07(`docs/STORE_READINESS.md` **표 A** — 표 B 는 "수집하지 않는 것" 이라 보내는 이벤트가 들어갈 자리가 아니다, 루프#115).
> 마지막 갱신: M8-05b (2026-09-24 — `gacha_pull_requested` 가 실제로 나간다, 표 A 열 줄 전부). 그 앞은 루프#146 (2026-09-23 — 표 A 에 `player_died` 한 줄을 더했다, M11-03). 그 앞은 루프#115 (2026-09-22 — 표 D 의 M8-07 줄과 위 가리킴만 고쳤다, 이벤트 목록은 그대로). 그 앞은 루프#114 (2026-09-22, 표 A 의 **여덟 줄**이 실제로 나간다 — 코어 루프 셋 + 목장 둘 + 잔액 변화 하나 + 세션·존 둘). 표의 코드 줄은 **이 시점의 `develop`** 을 말한다.

## A. 이벤트 목록

**지금 보내는 이벤트는 10개다**(코어 루프 셋 루프#111 · 목장 둘 루프#112 · 잔액 변화 하나 루프#113 · 세션·존 둘 루프#114 · 사망 하나 루프#146 · 가챠 요청 하나 M8-05b 2026-09-24) — 표 A 의 모든 줄이 실제로 나간다.

| 이벤트 이름 | 언제 | 칸(payload) | 보내는 쪽 | 지금 그 자리 | 조각 |
|---|---|---|---|---|---|
| `animal_killed` | 동물이 마지막 타를 맞고 죽은 순간 | `pet_item_id` · `drop_count` · `zone_id` | 클라이언트 (**호스트 권위**) | **보낸다** — `Combat/MeleeAttackComponent.cpp:166` (`AttackOnceOnAuthority` 안, `OnAnimalKilled.Broadcast` 바로 뒤) | M8-03 |
| `animal_capture_succeeded` | 포획이 실제로 성립한 순간 | `pet_item_id` · `zone_id` | 클라이언트 (**호스트 권위**) | **보낸다** — `Combat/CaptureComponent.cpp:314` (`CaptureTargetNowOnAuthority` 안, `OnAnimalCaptured.Broadcast` 뒤 — 즉시 포획·링 성공이 같이 탄다) | M8-03 |
| `animal_capture_failed` | 포획 버튼을 눌렀는데 성립하지 않은 순간 (임계값 위 · 이미 끝난 동물 · 올가미 링 타이밍 실패) | `pet_item_id` · `zone_id` · `reason` | 클라이언트 (**호스트 권위**) | **보낸다** — `Combat/CaptureComponent.cpp:169` (`ReportCaptureFailureOnAuthority` — `TryCaptureTargetOnAuthority`·`CaptureTargetNowOnAuthority`·`ResolveCaptureRingAt` 의 실패 길이 모인다) | M8-03 |
| `ranch_pet_register_requested` | 목장 슬롯에 펫을 놓아 달라고 **요청**한 순간 | `slot_index` · `animal_id` | 클라이언트 (요청자 본인) | **보낸다** — `Ranch/RanchComponent.cpp:145` (`QueueRegisterPet` — 요청을 쌓은 뒤, 부르는 곳은 `RanchInteractComponent.cpp:225`) | M8-04 |
| `ranch_harvest_requested` | 수확해 달라고 **요청**한 순간 | `slot_index` | 클라이언트 (요청자 본인) | **보낸다** — `Ranch/RanchComponent.cpp:186` (`QueueHarvest` — 요청을 쌓은 뒤, 부르는 곳은 `RanchInteractComponent.cpp:213`. **목장 거점에서만 보낸다** — 낚시터 거점은 이 이름을 쓰지 않는다, M10-02) | M8-04 |
| `gacha_pull_requested` | 뽑기 버튼을 누른 순간 (1회 / 10연차) | `pull_count` · `currency` | 클라이언트 (요청자 본인) | **보낸다** — `UI/GachaScreenWidget.cpp:268` (`RequestPull` — `RollGacha` 를 부르기 **직전**, 부르는 곳은 젬 1회·코인 1회·10연차 버튼. 결과·환급은 싣지 않는다) | M8-05b |
| `currency_balance_changed` | 잔액 캐시가 실제로 바뀐 순간 | `currency` · `new_balance` · `balance_source`(백엔드 응답 / 로컬 캐시) | 클라이언트 (**관찰만**) | **보낸다** — `Backend/SnareBackendSubsystem.cpp:261` (`WriteBalanceChangedEvent` — 부르는 곳은 `ApplyPlayerSnapshot:240`·`:241`, 캐시를 바꾸는 **유일한** 길) | M8-05a |
| `session_started` | PlayFab 로그인이 성공한 순간 | `is_new_account` | 클라이언트 | **보낸다** — `Backend/SnareBackendSubsystem.cpp:197` (`FinishLoginSucceeded` 안, 보유 상태를 묻기 전) | M8-06 |
| `zone_travelled` | 포탈로 존을 떠나는 순간 | `from_zone_id` · `to_zone_id` | 클라이언트 (**호스트 권위** — `ServerTravel` 을 부르는 쪽) | **보낸다** — `World/ZonePortalActor.cpp:210` (`TravelToTargetZone` 안, `ServerTravel` 이 **받아들여진 뒤**) | M8-06 |
| `player_died` | 플레이어 체력이 0 이 된 순간 | `zone_id` · `lost_pending_count` | 클라이언트 (죽은 본인) | **보낸다** - `SnarehavenCharacter.cpp:771` (`HandlePlayerDied` 안, 지급 대기열을 버린 **뒤**) | M11-03 |

**`reason` 칸의 값 셋은 루프가 골랐다**(🟡 루프#111 — 표는 칸 **이름**까지만 정하고 값은 안 정한다):
`above_threshold`(체력이 아직 포획 문턱 위) · `already_finished`(이미 죽었거나 포획된 동물) · `no_state`(동물에 상태 컴포넌트가 없다 — 보통 버그) · `timing`(올가미 링 둘째 누름이 성공 창보다 이르거나 링이 다 줄도록 안 눌렀다 — M21-09a, HP 그대로·동물은 도주 1.5초) · `escaping`(그 도주 중에 눌렀다 — M21-09b, 버튼은 꺼져 있어 늦게 닿은 게스트 요청뿐).
**포획 버튼을 눌렀는데 대상이 없을 때는 아무것도 안 보낸다** — 그건 포획 실패가 아니라 시도 자체가 없던 것이다.

**`balance_source` 칸의 값 둘도 루프가 골랐다**(🟡 루프#113 — `reason` 과 같은 이유. 칸 이름은 #0204 에 `source` 에서 바꿨다 — PlayFab 이 `source` 를 예약 필드라며 이벤트를 통째로 거절한다. **예약 이름**(`Source`·`SourceType`·`EventName`·`EventNamespace`·`EventId`·`Entity`·`Timestamp`·`History`·`CustomTags`·`Reserved`·`PlayFabEnvironment`, 대소문자 무관)은 칸 이름으로 쓰지 않는다 — 2층 `Snarehaven.Analytics.Payload.NoReservedField`):
`backend`(백엔드 응답으로 바뀐 잔액) · `local_cache`(부팅 때 올린 표시용 캐시로 바뀐 잔액).
**재화 하나가 바뀔 때마다 한 줄씩**이다 — 코인·젬이 같은 응답에서 같이 바뀌면 두 줄이 나간다. 값이 그대로면 안 보낸다(응답 횟수가 아니라 **변화**를 센다).
**로그인 뒤 첫 응답도 변화로 센다** — 캐시가 0 에서 실제 잔액으로 바뀐 순간이라 `balance_source=backend` 한 줄이 나간다.
**차액(얼마가 오갔나)은 싣지 않는다** — 소비·지급의 원장은 백엔드가 쓴다(표 C).

**`is_new_account` 는 `true`/`false` 두 글자다**(🟡 루프#114 — 칸 값은 전부 문자열이라 불리언도 글자로 나간다, 표 B "칸 값").
**세션은 앱을 켠 순간이 아니라 로그인이 성공한 순간 시작된다** — 로그인 전 이벤트는 통로가 버리니(표 B) 그보다 앞에서 보내면 한 줄도 안 나간다.
**존 이동은 `ServerTravel` 이 받아들여진 뒤에만 한 줄**이다 — 거절된 길에서 보내면 일어나지 않은 이동이 세어진다. 방 하나에 한 줄이다(호스트만 보낸다).

**`lost_pending_count` 는 "버린 지급 **요청** 줄 수" 다**(🟡 루프#146 — 칸 값은 루프가 고른다, 위 `reason`·`source` 와 같은 이유): 죽을 때 잃는 것은 아직 백엔드가 확정하지 않은 지급 대기열뿐이고(DESIGN 3.1), 그 대기열에 몇 줄이 있었는지를 싣는다. **아이템 이름도 개수도 싣지 않는다** — 무엇이 실제로 폐기됐는지는 백엔드 원장이 쓸 일이다(표 C). 보유 인벤토리·재화는 사망으로 한 칸도 바뀌지 않으므로 이 칸은 잔액 숫자가 아니다.
**사망은 `클라이언트` 가 보낸다**(호스트 권위가 아니다): 생존 값(`UPlayerVitalsComponent`)은 복제되지 않아 각자 자기 폰에서 죽고, 죽은 본인 하나만 이 줄을 보낸다 — 한 사건이 두 번 세어질 자리가 없다.

**목장 두 줄의 `animal_id` 와 코어 루프 세 줄의 `pet_item_id` 는 다른 값이다**(루프#112): 목장에 실리는 것은 종 ID `animal.pig`(`DT_AnimalTable` — `URanchComponent::QueueRegisterPet` 이 받는 값)이고, 코어 루프에 실리는 것은 포획 아이템 ID `item.pet_pig` 다. 같은 칸 이름을 쓰면 값 종류가 섞여 분석에서 갈라낼 수 없다.

**칸 이름 하나는 잠정이다:** 동물 **종**을 가리키는 값이 코드에 없다 — `UAnimalStateComponent` 가 들고 있는 이름은 `PetItemID`(예: `item.pet_pig`)와 드롭 목록뿐이다(`AnimalStateComponent.h:110`·`:103`). 그래서 칸 이름을 `pet_item_id` 로 뒀다. 종 ID 가 생기면 그때 `animal_id` 로 바꾼다(M8-01 표가 한 곳에서 갈아끼운다).

## B. 보내는 통로

| 무엇 | 지금 | 앞으로 |
|---|---|---|
| 나가는 길 | **섰다**(루프#110) — `USnareBackendSubsystem::WriteGameEvent(FName EventID, const TMap<FName, FString>& Payload)` **하나**. 이 함수 밖에서 보내면 2층 `Snarehaven.Analytics.Channel.SinglePath` 가 운다 | 그대로. **부르는 자리 10개**(코어 루프 셋 루프#111 · 목장 둘 루프#112 · 잔액 변화 하나 루프#113 · 세션·존 둘 루프#114 · 사망 하나 루프#146 · 가챠 요청 하나 M8-05b). 남은 자리 없음 |
| 쓰는 API | 플러그인에 이미 있다 — `PlayFabClientAPI.h:981` `WritePlayerEvent`(요청 구조체 `FWriteClientPlayerEventRequest`(`PlayFabClientDataModels.h:12957`): `EventName` · `Body`(이름-값 JSON) · `Timestamp`) | 그대로. **새 SDK·새 서비스를 붙이지 않는다**(계정·약관·키는 Autonomy 🔴 3) |
| 로그인 전 · Title ID 가 없을 때 | 로그인 자체가 건너뛰어진다(`SnareBackendSubsystem.cpp:115`) · 이벤트는 **조용히 버리고 로그 한 줄**(`[Analytics] 로그인 전이라 '<이름>' 이벤트를 버린다`) | 그대로. 큐에 쌓지 않는다 — 분석은 지급이 아니라 관찰이라, 나중에 몰아 보내면 시각만 어긋난다. 보내기가 실패해도 다시 보내지 않는다(재전송은 한 사건을 두 번 세게 만든다) |
| 칸 값 | **전부 문자열**로 싣는다(`TMap<FName, FString>` -> `FJsonKeeper`) — 칸마다 타입을 고르게 두면 같은 칸이 어떤 자리에서는 수, 어떤 자리에서는 글자로 나간다 | 숫자 칸이 정말 필요해지면 그때 표의 칸 목록에 타입을 더한다 |
| 이름 꼴 | 없음 | **밑줄**(`animal_killed`) — 플러그인 주석이 권하는 `subject_verb_object` 꼴이다(`PlayFabClientDataModels.h:12965`). 프로젝트 ID 규칙(`zone.hunt` 같은 점)과 다른 이유는 이 이름이 **PlayFab 에 그대로 나가는 값**이기 때문이다. 점을 받는지는 서버가 답할 일이라 안 써 봤다 |

## C. 클라이언트가 보내지 않는 것 (백엔드 원장)

**얼마가 오갔는지의 원본은 백엔드가 쓴다** (CLAUDE.md 8 · DESIGN 4.1·4.8). 클라이언트 이벤트는 **"플레이어가 무엇을 눌렀나"** 까지다.

| 원장 항목 | 누가 쓰나 | 지금 그 자리 |
|---|---|---|
| 목장 수확 지급(아이템 n · 코인 10n) | CloudScript `HarvestRanch`(BACKEND.md) | **없음** — M2-03(사람·PlayFab 콘솔) |
| 슬롯 배치 기록(`occupiedSinceUtc`) | CloudScript `RegisterPet` | **없음** — M2-03 |
| 가챠 결과·천장·중복 환급 | CloudScript `RollGacha` | **없음** — M7-06(사람 몫) |
| 젬 구매(IAP 영수증·주문) | PlayFab 영수증 검증 | **없음** — M7-08(결제·스토어 계정은 루프 금지) |
| 목장 슬롯 확장 | 아직 정해지지 않았다 | **없음** — 기능 자체가 없다. DESIGN 11 은 싱크로 세어 두었지만(`docs/DESIGN.md:112` "슬롯 확장 = 재화로 구매, 가격 곡선은 콘텐츠 확장 단계에서 결정") ROADMAP 에 조각이 없다 |

**이 표가 반려 기준이다:** 위 다섯 줄의 값(수확량·코인·등급·환급·영수증)을 **클라이언트가 이벤트 칸에 적으면 반려**다. 클라이언트가 보내는 것은 요청과 관찰뿐이다.

## D. 이후 조각이 고칠 자리

| 조각 | 고칠 파일 | 이 표의 어느 칸이 바뀌나 |
|---|---|---|
| M8-01 `DT_AnalyticsEventTable` | `Content/Game/Data/Analytics/` · `tools/unreal/create_data_tables.py` · 새 행 구조체 | A 의 **이름·칸**이 표의 행이 된다 (문서와 표가 어긋나면 2층이 운다 — M7-02 와 같은 방식) |
| ~~M8-02 `WriteGameEvent`~~ **끝(루프#110)** | `Backend/SnareBackendSubsystem.{h,cpp}` · `Tests/SnarehavenAnalyticsEventTest.cpp` | B 의 "나가는 길" 이 **없다 -> 함수 하나** |
| ~~M8-03 코어 루프~~ **끝(루프#111)** | `Combat/MeleeAttackComponent.cpp` · `Combat/CaptureComponent.{h,cpp}` · `World/ZonePortalActor.{h,cpp}`(`zone_id` 를 되짚는 `FindCurrentZoneID`) · `Tests/SnarehavenAnalyticsEventTest.cpp` | A 위 세 줄의 "지금 그 자리" 가 **부르는 줄**이 됐다 |
| ~~M8-04 목장~~ **끝(루프#112)** | `Ranch/RanchComponent.cpp` · `tools/unreal/create_data_tables.py`(칸 이름) · `Tests/SnarehavenAnalyticsEventTest.cpp` | A 목장 두 줄의 "지금 그 자리" 가 **부르는 줄**이 됐다 · 배치 줄의 칸 `pet_item_id` → **`animal_id`** |
| ~~M8-05a 재화~~ **끝(루프#113)** | `Backend/SnareBackendSubsystem.{h,cpp}`(`WriteBalanceChangedEvent`) · `Tests/SnarehavenAnalyticsEventTest.cpp` | A 잔액 줄의 "지금 그 자리" 가 **부르는 줄**이 됐다 |
| M8-05b 가챠 | `UI/GachaScreenWidget.cpp`(M7-06 뒤) | A 가챠 줄 — 버튼이 켜질 때까지 "없음" 그대로다 |
| ~~M8-06 세션·존~~ **끝(루프#114)** | `Backend/SnareBackendSubsystem.cpp` · `World/ZonePortalActor.cpp` · `Tests/SnarehavenAnalyticsEventTest.cpp`(권위 검사를 `if (!HasAuthority())` 몸통까지 넓힘) | A 아래 두 줄의 "지금 그 자리" 가 **부르는 줄**이 됐다 |
| ~~M8-07 수집 항목 신고~~ **끝(루프#115)** | `docs/STORE_READINESS.md`(표 A 에 분석 이벤트 줄 + 이벤트 이름 목록 · 표 B 의 SDK 줄 정정) · `Tests/SnarehavenAnalyticsEventTest.cpp` | 이 문서의 내용은 안 바뀌었다 — **`STORE_READINESS.md` 표 A 의 목록이 이 문서 표 A 와 같은 목록**이고, 어긋나면 2층 `Snarehaven.Analytics.Disclosure.StoreReadinessMatchesDocument` 가 운다 |

**이 문서는 코드가 바뀌면 같이 고친다.** 줄 번호가 밀리는 것은 표 A·B 뿐이고, 표 C 는 DESIGN 11·BACKEND.md 를 옮겨 적은 것이라 설계가 바뀔 때만 손댄다.
