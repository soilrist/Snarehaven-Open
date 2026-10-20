# 아이템 표시 이름 차별화 표 (INBOX 지시 2026-10-11 23:30 · 루프#1048)

레퍼런스 게임 자료(`_Source/reference/reference/vinternote_items_360.txt`)의 한국어 이름과 **글자까지 같던** 우리 표시 이름 139개. 판정 둘 — **일반 낱말**(어느 게임에나 있는 사물·생물·광물·요리 이름 → 유지) / **레퍼런스 게임 고유 꼴**(레퍼런스 게임의 묶음·조합 이름, 「○○ 모자」 시리즈, 「기본/고급/전기 ○○」 등급 짜임, 「페인트: 색」 짜임 → **바꿈**). ItemID·PlayFab 카탈로그 ID 는 그대로(서버·저장 안 깨짐), `create_data_tables.py` 표시 이름만 바꿨다. 설명 글(description)은 우리 표에 칸이 없어(`Description` 0건) 해당 없음. 시험 `Snarehaven.Data.ItemNames.NoreferenceOnlyNames` 가 이 표의 `바꿈` 줄 「지금 이름」이 표시 이름에 0개인지 읽어 단정한다 — **표 형식을 바꾸면 시험도 고친다.**

| 우리 ID | 지금 이름 | 판정 | 새 이름 |
|---|---|---|---|
| item.carrot | 당근 | 일반 낱말 | (유지) |
| item.potato | 감자 | 일반 낱말 | (유지) |
| item.wheat | 밀 | 일반 낱말 | (유지) |
| item.salmon | 연어 | 일반 낱말 | (유지) |
| item.cave_crab | 동굴 게 | 일반 낱말 | (유지) |
| item.perch | 농어 | 일반 낱말 | (유지) |
| item.pike | 강꼬치고기 | 일반 낱말 | (유지) |
| item.whitefish | 백어 | 일반 낱말 | (유지) |
| item.haddock | 해덕대구 | 일반 낱말 | (유지) |
| item.cod | 대구 | 일반 낱말 | (유지) |
| item.catfish | 메기 | 일반 낱말 | (유지) |
| item.trout | 송어 | 일반 낱말 | (유지) |
| item.tuna | 참치 | 일반 낱말 | (유지) |
| item.stingray | 가오리 | 일반 낱말 | (유지) |
| item.rainbow_trout | 무지개송어 | 일반 낱말 | (유지) |
| item.redfish | 점성어 | 일반 낱말 | (유지) |
| item.black_grouper | 검은 참바리 | 일반 낱말 | (유지) |
| item.arctic_char | 북극 곤들매기 | 일반 낱말 | (유지) |
| item.swordfish | 황새치 | 일반 낱말 | (유지) |
| item.misty_grouper | 잿빛 참바리 | 바꿈 | 안개바리 |
| item.salmon_shark | 악상어 | 일반 낱말 | (유지) |
| item.axolotl | 우파루파 | 일반 낱말 | (유지) |
| item.fox_meat | 여우고기 | 일반 낱말 | (유지) |
| item.fox_tail | 여우 꼬리 | 일반 낱말 | (유지) |
| item.goat_meat | 염소고기 | 일반 낱말 | (유지) |
| item.goat_horn | 염소 뿔 | 일반 낱말 | (유지) |
| item.reindeer_antler | 순록 뿔 | 일반 낱말 | (유지) |
| item.wolf_tooth | 늑대 이빨 | 일반 낱말 | (유지) |
| item.bear_meat | 곰고기 | 일반 낱말 | (유지) |
| item.bird_meat | 새고기 | 일반 낱말 | (유지) |
| item.pheasant_feather | 꿩 깃털 | 일반 낱말 | (유지) |
| item.peacock_feather | 공작새 깃털 | 일반 낱말 | (유지) |
| item.ptarmigan_feather | 뇌조 깃털 | 일반 낱말 | (유지) |
| item.cedar | 향나무 | 일반 낱말 | (유지) |
| item.copper_chunk | 구리 덩어리 | 일반 낱말 | (유지) |
| item.iron_chunk | 철 덩어리 | 일반 낱말 | (유지) |
| item.titanium_chunk | 티타늄 덩어리 | 일반 낱말 | (유지) |
| item.silver_chunk | 은 덩어리 | 일반 낱말 | (유지) |
| item.gold_chunk | 금 덩어리 | 일반 낱말 | (유지) |
| item.coal_ingot | 석탄 주괴 | 바꿈 | 코크스 |
| item.copper_ingot | 구리 주괴 | 일반 낱말 | (유지) |
| item.iron_ingot | 철 주괴 | 일반 낱말 | (유지) |
| item.titanium_ingot | 티타늄 주괴 | 일반 낱말 | (유지) |
| item.gold_ingot | 금 주괴 | 일반 낱말 | (유지) |
| item.metal_parts | 금속 부품 | 바꿈 | 쇠붙이 부속 |
| item.sheet_metal | 철판 | 일반 낱말 | (유지) |
| item.solar_kit | 태양광 키트 | 바꿈 | 햇빛 집열판 |
| item.light_bulb | 전구 | 일반 낱말 | (유지) |
| item.battery | 배터리 | 일반 낱말 | (유지) |
| item.polymer | 폴리머 합성물 | 바꿈 | 굳힘 수지 |
| item.circuit_board | 전자 회로 기판 | 바꿈 | 회로판 |
| item.electric_motor | 전자 모터 | 바꿈 | 소형 전동기 |
| item.stair_kit | 계단 건축 부품 | 바꿈 | 층계 짜맞춤 |
| item.resin_adhesive | 수지 접착제 | 바꿈 | 끈끈이 풀 |
| item.cotton_fabric | 면 원단 | 바꿈 | 무명천 |
| item.tarp | 방수포 | 바꿈 | 기름 먹인 천 |
| item.life_jacket | 구명조끼 | 일반 낱말 | (유지) |
| item.boat_motor | 보트 모터 | 바꿈 | 배 추진기 |
| item.boat_motor_electric | 전기 보트 모터 | 바꿈 | 전동 추진기 |
| item.boat_container | 보트 컨테이너 | 바꿈 | 배 짐칸 |
| item.fisher_hat | 어부 모자 | 바꿈 | 물가 챙갓 |
| item.farmer_hat | 농부 모자 | 바꿈 | 밭두렁 갓 |
| item.hunter_hat | 사냥 모자 | 바꿈 | 길잡이 두건 |
| item.lumberjack_hat | 나무꾼 모자 | 바꿈 | 도끼꾼 털모자 |
| item.strawberry_hat | 딸기 모자 | 바꿈 | 딸기 꼭지 베레 |
| item.merchant_hat | 상인 모자 | 바꿈 | 장사치 중절 |
| item.mining_hat | 채굴 모자 | 바꿈 | 갱도 투구 |
| item.hatchet | 손도끼 | 일반 낱말 | (유지) |
| item.chainsaw | 전기톱 | 바꿈 | 전동 톱 |
| item.machete | 마체테 | 일반 낱말 | (유지) |
| item.binoculars | 망원경 | 일반 낱말 | (유지) |
| item.pickaxe_basic | 기본 곡괭이 | 바꿈 | 나무자루 곡괭이 |
| item.pickaxe_advanced | 고급 곡괭이 | 바꿈 | 강철 곡괭이 |
| item.pickaxe_electric | 전기 곡괭이 | 바꿈 | 전동 곡괭이 |
| item.pistol_semi | 반자동 권총 | 바꿈 | 단발 권총 |
| item.smg | 기관단총 | 일반 낱말 | (유지) |
| item.rifle_semi | 반자동 소총 | 일반 낱말 | (유지) |
| item.shotgun_auto | 자동 산탄총 | 바꿈 | 연사 산탄총 |
| item.mg | 기관총 | 일반 낱말 | (유지) |
| item.crossbow | 석궁 | 일반 낱말 | (유지) |
| item.shotgun | 산탄총 | 일반 낱말 | (유지) |
| item.rifle_modern | 현대 소총 | 바꿈 | 신식 소총 |
| item.flour | 밀가루 | 일반 낱말 | (유지) |
| item.sugar | 설탕 | 일반 낱말 | (유지) |
| item.vegetable_oil | 식물성 오일 | 바꿈 | 식물 기름 |
| item.tea_bag | 티백 | 바꿈 | 찻잎 주머니 |
| item.salt | 소금 | 일반 낱말 | (유지) |
| item.black_pepper | 흑후추 | 일반 낱말 | (유지) |
| item.dill | 딜 | 일반 낱말 | (유지) |
| item.empty_bottle | 빈 병 | 일반 낱말 | (유지) |
| item.water_bottle | 물 병 | 바꿈 | 샘물 병 |
| item.coffee_bottle | 커피 병 | 바꿈 | 커피 보온병 |
| item.heat_pack | 핫팩 | 바꿈 | 손난로 |
| item.snackbar | 에너지바 | 바꿈 | 곡식 막대 |
| item.water_canister | 수통 | 일반 낱말 | (유지) |
| item.canned_fish | 생선 통조림 | 바꿈 | 절임 생선통 |
| item.blueberry | 블루베리 | 일반 낱말 | (유지) |
| item.lingonberry | 링곤베리 | 일반 낱말 | (유지) |
| item.blackberry | 블랙베리 | 일반 낱말 | (유지) |
| item.mezereon | 메제레움 | 일반 낱말 | (유지) |
| item.onion | 양파 | 일반 낱말 | (유지) |
| item.cucumber | 오이 | 일반 낱말 | (유지) |
| item.cabbage | 양배추 | 일반 낱말 | (유지) |
| item.kingcup | 메리골드 | 일반 낱말 | (유지) |
| item.windflower | 아네모네 | 일반 낱말 | (유지) |
| item.hepatica | 노루귀 | 일반 낱말 | (유지) |
| item.poppy | 양귀비 | 일반 낱말 | (유지) |
| item.pumpkin | 호박 | 일반 낱말 | (유지) |
| item.canola | 카놀라 | 일반 낱말 | (유지) |
| item.coffee_beans | 커피콩 | 일반 낱말 | (유지) |
| item.cloudberry | 클라우드베리 | 일반 낱말 | (유지) |
| item.black_lotus | 검은 연꽃 | 일반 낱말 | (유지) |
| item.apple | 사과 | 일반 낱말 | (유지) |
| item.bread | 빵 | 일반 낱말 | (유지) |
| item.carrot_bread | 당근빵 | 바꿈 | 당근 화덕빵 |
| item.cake | 케이크 | 일반 낱말 | (유지) |
| item.sandwich | 샌드위치 | 일반 낱말 | (유지) |
| item.pumpkin_pie | 호박 파이 | 바꿈 | 호박 속 구이 |
| item.berry_pie | 베리 파이 | 바꿈 | 열매 타르트 |
| item.fish_soup | 생선 수프 | 바꿈 | 물고기 탕 |
| item.meat_soup | 고기 수프 | 바꿈 | 고깃국 |
| item.berry_salad | 베리 샐러드 | 바꿈 | 열매 무침 |
| item.mushroom_salad | 버섯 샐러드 | 바꿈 | 버섯 무침 |
| item.roasted_mushrooms | 구운 버섯 | 바꿈 | 숯불 버섯 |
| item.fried_meat | 튀긴 고기 | 바꿈 | 지짐 고기 |
| item.fish_and_vegetables | 생선과 채소 | 바꿈 | 물고기 채소찜 |
| item.cabbage_roll | 양배추 롤 | 바꿈 | 양배추쌈 |
| item.onion_rings | 양파 링 | 바꿈 | 양파 튀김 고리 |
| item.chanterelle | 살구버섯 | 일반 낱말 | (유지) |
| item.ruby | 루비 | 일반 낱말 | (유지) |
| item.sapphire | 사파이어 | 일반 낱말 | (유지) |
| item.amethyst | 자수정 | 일반 낱말 | (유지) |
| item.aquamarine | 아쿠아마린 | 일반 낱말 | (유지) |
| item.emerald | 에메랄드 | 일반 낱말 | (유지) |
| item.treasure_map | 보물 지도 | 일반 낱말 | (유지) |
| item.paint_white | 페인트: 하얀색 | 바꿈 | 하양 물감 |
| item.paint_charcoal | 페인트: 목탄 | 바꿈 | 숯검정 물감 |
| item.paint_blue | 페인트: 파란색 | 바꿈 | 파랑 물감 |
| item.paint_red | 페인트: 빨간색 | 바꿈 | 빨강 물감 |
| item.energy_drink_mild | 에너지 드링크 순한맛 | 바꿈 | 기운 탄산 연한 |
| item.energy_drink | 에너지 드링크 보통맛 | 바꿈 | 기운 탄산 보통 |
| item.energy_drink_strong | 에너지 드링크 강한맛 | 바꿈 | 기운 탄산 진한 |
| item.rod_telescopic | 텔레스코픽 릴 | 바꿈 | 접이식 낚싯대 |
| item.ciabatta_topped | 토핑 치아바타 | 바꿈 | 채소 얹은 치아바타 |
| item.mezereon | 메제레움 | 바꿈 | 서향 열매 |
| item.seed_mezereon | 메제레움 씨앗 | 바꿈 | 서향 씨앗 |
| item.mezereon_jam | 메제레움잼 | 바꿈 | 서향잼 |
| item.hiking_backpack | 하이킹 배낭 | 바꿈 | 등산 짐가방 |
| item.rod_bait | 미끼 낚싯대 | 바꿈 | 미끼통 낚싯대 |
| item.chanterelle_soup | 살구버섯 수프 | 바꿈 | 살구버섯 죽 |
| item.bear_sausage | 곰 소시지 | 바꿈 | 곰고기 훈제 순대 |
| item.cook_hat | 요리 모자 | 바꿈 | 주방 두건 |

2차(감독 보정 2026-10-19 · 루프#1304) — 띄어쓰기·부호만 다른 9건(에너지 드링크 3·텔레스코픽 릴·토핑 치아바타·메제레움 3·하이킹 배낭·미끼 낚싯대·살구버섯 수프·곰 소시지·요리 모자, 레퍼런스 게임 `하이킹 백팩`·`텔레스코픽 릴 낚싯대`·`살구버섯 스프` 등)을 위 표에 더했다. 메제레움 덤불(채집 더미 이름)은 「서향 덤불」 로 같이 바꿨다(아이템 아님이라 표 밖). 다시 판정한 둘: 「석탄 주괴」(레퍼런스 게임 고유 번역 — 설명이 「고농축 석탄」, 다른 주괴와 달리 현실 낱말이 아니다 → 코크스), 「잿빛 참바리」(레퍼런스 게임 번역 그대로 → 안개바리). 시험은 이제 띄어쓰기·부호를 지우고도 비교한다.

페인트 노랑·민트·블러셔 3종은 레퍼런스 게임 자료에 같은 이름이 없었지만 「페인트: 색」 짜임을 한 줄로 맞추려고 같이 `노랑 물감`·`박하 물감`·`볼연지 물감` 으로 바꿨다(표 밖).
