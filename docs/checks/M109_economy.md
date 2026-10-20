# M109 경제 장부 — 코인(SC)·젬(GM) 들어오는 길·나가는 길 (루프#1440)

원본은 서버 상수다(CLAUDE.md 8) — 이 표는 `tools/playfab/cloudscript_main.js` 를 읽어 옮긴 것이고, 값이 다르면 CloudScript 가 이긴다(M109-01 이 맞대기 시험으로 묶는다). 줄 번호는 #1440 시점.

**줄 수 근거 (grep 실측, #1440):** `AddUserVirtualCurrency` 16 · `SubtractUserVirtualCurrency` 5 · `spendCoins(` 5 = 26곳. 그중 `spendCoins` 정의 줄(4216)과 그 몸 안의 `SubtractUserVirtualCurrency`(4217)는 도우미라 빼고, 그 도우미를 부르는 4곳을 각각 한 줄로 센다 → **표 24줄 = 들어옴 16 + 나감 8** (놀이 밖 2줄 포함).

빈도: 한 번 = 계정 평생 한 번(또는 항목마다 한 번) · 하루 = 서버 UTC `questDay()` 하루 · 주 = 서버 주 · 되풀이 = 조건만 맞으면 몇 번이든.

## 들어오는 길 (소스)

| # | 줄 | 핸들러(함수) | 재화 | 상수 | 값 | 빈도 | 상한 | 놀이 |
|---|---|---|---|---|---|---|---|---|
| 1 | 163 | `GrantStarterKit` | SC | `STARTER_COINS` | 10 | 한 번(`starterKit` 표시) | — | 놀이 |
| 2 | 258 | `GrantTesterKit` | SC | `TESTER_KIT_COINS` | 500 | 한 번(`TESTER_KIT_VERSION` 마다) | 테스터 목록(`testerIds()`)만 | **놀이 밖** |
| 3 | 377 | `grantAnimalHarvest` ← `HarvestRanch`·`RemovePet` | SC | `COIN_PER_UNIT` | 10 × 생산 단위 | 되풀이(동물 생산 간격마다) | 칸당 `HARVEST_CAP_UNITS` 5 단위에서 생산 멈춤 · 칸 `RANCH_SLOT_COUNT` 5 ~ `RANCH_SLOT_MAX` 10 · 낚시터와 합쳐 하루 `HARVEST_DAILY_COINS` 900(넘으면 생산물만) | 놀이 |
| 4 | 646 | `HarvestCrop` | SC | `CROP_HARVEST_COINS` | 작물마다 5~40(한 번 거둘 때) | 되풀이(`CROP_CYCLE_MINUTES` 15~120분) | 목장 칸 수 | 놀이 |
| 5 | 721 | `HarvestFishing` | SC | `FISH_TABLE[].coinPerUnit` | 어종마다 5~60 × 생산 단위 | 되풀이(어종 `intervalMinutes`) | 칸당 `FISHING_CAP_UNITS` 5 · 칸 `FISHING_SLOT_COUNT` 5 · 동물 칸과 합쳐 하루 `HARVEST_DAILY_COINS` 900 | 놀이 |
| 6 | 826 | `RollGacha` 코인 경품 | SC | `GACHA_COIN_PRIZES` | 50 · 150 · 400 | 뽑기마다(뽑기 값은 #17) | 뽑기 값이 막음 | 놀이 |
| 7 | 841 | `RollGacha` 겹친 경품 되돌림 | SC | `GRADE_DUP_REFUND` | 보통 50 · 희귀 150 · 영웅 400 · 전설 1000 | 뽑기마다(이미 가진 것) | 뽑기 값이 막음 | 놀이 |
| 8 | 998 | `bumpAchievements` | SC | `ACHIEVEMENTS[].coins` | 13개 합 475(개당 5~100) | 한 번(업적마다) | 합 475 | 놀이 |
| 9 | 3205 | `SellItem` | SC | `sellPriceOf`(`SELL_PRICE`·`FIXED_SELL_PRICE`·`LOOT_SELL_PRICE`·`ORE_CHUNK_PRICE`·`GATHER_SELL_PRICE`) · 과일 `FRUIT_PRICE_NATIVE`/`FRUIT_PRICE_OTHER` | 개당 값 × 개수 × 장사치 중절 `sellMul`(내림) · 과일 8 / 40 · 펫 `SELL_PRICE.pet` 10 · 가공품 = 재료 값 합 + 1 | 되풀이 | 사냥·채집 재료(와 그걸 넣어 만든 것)만 하루 `LOOT_SELL_DAILY_COINS` 2000 · 그 밖(작물·물고기·과일·전기 부품·뽑기 재료)은 없음 | 놀이 |
| 10 | 3351 | `SellSweetPotato` | SC | `sweetPotatoWeek().prices` | 주 기준가 9~11 × 칸 배율 0.4~4.0(반올림, 1 이상) × 개수 | 월~토 하루 2칸 시세 | 살 수 있는 양 = 주 `POTATO_WEEKLY_CAP` 100개 · 지난 주 것은 썩음 | 놀이 |
| 11 | 3564 | `CompleteQuest` | SC | `QUEST_REWARD_MUL` · `QUEST_UNSELLABLE_COIN` | ⌈파는 값 × 개수 × 1.5⌉ · 못 파는 것은 개당 0.5 | 하루 | 하루 `QUEST_PER_DAY` 3개 | 놀이 |
| 12 | 3620 | `CompleteQuestChainStep`(옛 의뢰 줄기) | SC | `QUEST_CHAIN[].coins` | 10 · 12 · 16 · 20 · 22 = 80 | 한 번(단계마다) | 메인 스토리를 시작한 계정은 막힘(`chain_done`) | 놀이 |
| 13 | 3740 | `AdvanceStory` 「건네기」 단계 | SC | `QUEST_CHAIN[].coins`(#12 와 같은 줄) | 10 · 12 · 16 · 20 · 22 = 80 | 한 번(`paid` 에 없는 줄기만) | #12 와 합쳐 한 줄기당 한 번 | 놀이 |
| 14 | 3752 | `AdvanceStory` 장 끝 | SC | `STORY_CHAPTER_COINS` | 20 · 30 · 40 · 50 · 60 = 200 | 한 번(장마다) | 합 200 | 놀이 |
| 15 | 3820 | `ClaimAttendance` | SC | `ATTENDANCE_REWARDS` · `ATTENDANCE_HAT_REPEAT_COINS` | 7칸 중 코인 칸 50 · 50 · 100 · 100 · 150 = 450 · 7일째 모자를 이미 가졌으면 150 | 하루 | 하루 1칸 | 놀이 |
| 16 | 3854 | `ClaimBoss` 모자 되풀이 | SC | `BOSS_HAT_REPEAT_COINS` · `BOSS_HAT_CHANCE` | 150(확률 0.1, 모자를 이미 받은 뒤) | 주 | 주 1번 | 놀이 |

젬(GM) 들어오는 길은 CloudScript 에 **없다** — `"GM"` 을 더하는 `AddUserVirtualCurrency` 0곳. 젬은 IAP(카탈로그 구매, M7-08 사람 몫)로만 들어온다(DESIGN 11 소스 표 「젬 구매(IAP)」).

## 나가는 길 (싱크)

| # | 줄 | 핸들러(함수) | 재화 | 상수 | 값 | 빈도 | 상한 | 놀이 |
|---|---|---|---|---|---|---|---|---|
| 17 | 797 | `RollGacha` 뽑기 값 | GM·SC | `SINGLE_COST` · `MULTI_COST_GM` | 한 번 GM 100 / SC 500 · 10연차 GM 900(코인 불가) | 되풀이 | — | 놀이 |
| 18 | 2941 | `ExpandRanchSlot` (`spendCoins`) | SC | `RANCH_EXPAND_COIN_STEP`(+ 명성 `RANCH_EXPAND_FAME_STEP`) | k번째 = 500·k → 500 · 1000 · 1500 · 2000 · 2500 = 7500 | 한 번(칸마다) | 5번(칸 5 → `RANCH_SLOT_MAX` 10) | 놀이 |
| 19 | 3142 | `buyWardrobeItem` ← `BuyItem`(GM) | GM | `WARDROBE_SHOP_POOL`(184벌) | 벌마다 200~500 | 되풀이(이번 주 진열 `WARDROBE_SHOP_SLOTS` 8) | 옷·모자는 한 벌씩(동물 꾸미기는 여럿) | 놀이 |
| 20 | 3162 | `BuyItem` 설계도 (`spendCoins`) | SC | `BLUEPRINT_PRICE` | 100 | 한 번(설계도마다) | 설계도 4개 = 400 · 도감 한 갈래 먼저 | 놀이 |
| 21 | 3183 | `BuyItem` 상점 | SC·GM | `SHOP_ITEMS[].price`(88줄, 씨앗 값 = 작물 값 ÷ `SELL_PRICE.cropDivisor` 5) · 열쇠 `CRATE_KEY_GEM_PRICE` | 줄마다 1~300 × 개수 · 열쇠 GM 10 | 되풀이 | 가방 칸 | 놀이 |
| 22 | 3329 | `BuySweetPotato` (`spendCoins`) | SC | `sweetPotatoWeek().base` · `POTATO_BUNDLE` | 주 기준가 9~11 × 10개 묶음 | 일요일만 | 주 `POTATO_WEEKLY_CAP` 100개(최대 1100) | 놀이 |
| 23 | 3883 | `PayFerry` (`spendCoins`) | SC | `FERRY_FARES` | 사냥터 5 · 위험구역 10 · 섬 5 · 돌아오기 0(여행자 모자 `fareMul` 올림) | 되풀이(건널 때마다) | — | 놀이 |
| 24 | 4164 | `wipePlayer` ← `DeleteMyAccount` | 지갑 전부 | — | 가진 만큼 전부 | 한 번(`DeletePlayer` 실패했을 때) | — | **놀이 밖** |

## 한눈에 (판정은 M109-03 몫 — 여기선 세기만)

- 놀이 소스 15줄 중 **되풀이·하루·주** = 10줄(#3·4·5·6·7·9·10·11·15·16 — 그중 #6·7 뽑기 경품은 뽑기 값 #17 을 낸 뒤에만), **한 번** = 5줄(#1·8·12·13·14, 합 10 + 475 + 80 + 200 = 765 — #12·13 은 같은 80 을 한쪽에서만).
- 놀이 싱크 7줄 중 **되풀이** = 5줄(#17·19·21·22·23), **한 번** = 2줄(#18 합 7500 · #20 합 400).
- 하루 상한이 있는 소스 = #9 재료 판매(2000)·#11 의뢰(3개)·#15 출석(1칸) 셋 + M109-04a(루프#1444)부터 #3·5 동물·낚시터 수확 코인 합 900. 밭(#4)은 칸 수와 칸당 상한만 있고 하루 총량은 없다(씨앗 값 #21 이 싱크) — 하루 벌이 셈은 M109-02 모의 셈.
- 코인을 계속 빨아들이는 싱크는 뽑기(500)·상점·고구마·배 삯 넷 — 다 쓴 뒤(칸 10·설계도 4) 남는 코인 셈은 M109-02·03.
