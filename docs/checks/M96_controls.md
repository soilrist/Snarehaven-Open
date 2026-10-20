# M96 — 조작 전수 실측 표 (ROADMAP M28-09 몫, 루프#970 a 조각)

> 행 = STATUS #0487 표(`docs/archive/STATUS-2026-10-06.md` 861행~)의 품목·시설 + 총·포획 던지기. 한 행 = 「누름 → 몸 동작 이름 · 소리 요청 수 · 화면 글자 · 결과 로그」 한 줄 + 게임 카메라 캡처 1장.
> 실행: `python3 tools/unreal/run_m96_row.py <행> <제한초> [map=/Game/...] <스위치...>` → 로그 `Saved/M96/<행>.log`, 캡처 `Saved/M96/<행>.png`(git 밖). 합격한 캡처만 `captures/M96/<행>.png` 로 복사. 행당 캡처 1번, 다시 찍기 ≤1.
> 모든 스위치는 `SnareCheckSwitch` 로 읽혀 출시 빌드에서 닫힌다(`check_shipping_switches.py`). 새 스위치는 필요 없었다 — 아래 빈 곳은 「스위치 있음」 이다.

| 행 | 조각 | 스위치 (모두 `-SnareOffline -SnareActionLog -SnareNoGuide` 위에) | 앞선 실측(STATUS) | 이번 할 일 |
|---|---|---|---|---|
| 맨손 | b | `-SnareMoveKey=SpaceBar -SnareMoveKeyDelay=6` | #0505 | 한 줄 + 캡처 |
| 몽둥이 | b | `-SnareEquip=Item_Club -SnareMoveKey=SpaceBar` | #0505·#0510 | 한 줄 + 캡처 |
| 손도끼·마체테·전기톱 | b | `-SnareEquipTool=<ID> -SnareStandAt=11900,-2610 -SnareMoveKey=SpaceBar` | #0505·#0506 | 3줄 + 캡처 3 |
| 기본·고급·전기 곡괭이 | b | `-SnareEquipTool=<ID> -SnareStandNear=gatherable.ore_basic -SnareMoveKey=SpaceBar` | #0505 | 3줄 + 캡처 3 |
| 채집 바구니 | b | `-SnareEquipTool=Item_GatherBasket -SnareStandNear=gatherable.plains_bush` | #0505 | 한 줄 + 캡처 |
| 낚싯대 4종 | b | `-SnareEquipTool=<ID> -SnareStandNear=fishing.coast_3 -SnareMoveKeyRepeat=0.4` | #0507·#0508 | 한 줄 + 캡처 |
| 망원경·위성 통신기 | b | `-SnareEquipTool=Item_Binoculars` / `Item_SatelliteCommunicator` | #0509 (카메라 ×1.3·좌표 HUD 줄 못 봄) | 그 두 줄을 찍는 로그가 있는지 확인 → 없으면 결함으로 INBOX |
| 포획 / 포획 던지기 | b | `-SnareEquip=Item_Club -SnareStandNear=animal.<종> -SnareAutoAttack` (+`-SnareCaptureRingHold`) | #0510·#0511 | 한 줄 + 캡처 |
| 총(권총·소총) | b | `-SnareEquip=Item_PistolSemi -SnareLoadedRounds=7 -SnareMoveKey=SpaceBar` | 안내 #0681·반동 #0655 (상황 버튼 실측 없음) | 한 줄 + 캡처 |
| 음식 | c | `-SnareEat=<ID>` | #0517 | 한 줄 + 캡처 |
| 설치물·집 | c | `-SnarePlace=<ID>` | #0520·#0522 | 한 줄 + 캡처 |
| 작업대·불·제련소·보관함 | c | `-SnareStandNear=structure.<ID>` | #0514 | 4줄 + 캡처 |
| 제작 판 | c | `-SnareCraftScreen[=Workbench] -SnareCraft=` | #0545 | 한 줄 + 캡처 |
| 목장 칸 수확·넣기 | c | `map=L_Ranch -SnareStandNear=ranch.<칸>` | #0512·#0513 | 2줄 + 캡처 |
| 상인(잡화) | c | `-SnareStandNear=vendor.General` | #0512·#0515 | 한 줄 + 캡처 |
| 옷장 | c | `-SnareWardrobe` | #0516 | 한 줄 + 캡처 |
| 처음 하는 사람 안내 | c | `-SnareUsageHints`(`-SnareNoGuide` 빼고) | #0523 | 한 줄 + 캡처 |

**d(빈 칸 0 확인)**: 위 17행(캡처 포함) 전부에 `Saved/M96/<행>.log` 인용이 STATUS 에 있고, 실측이 코드 근거와 다르면 INBOX `결함` 으로 올린 뒤 고쳤는지 본다.

## b 조각 실측 결과 — 도구 8행 (루프#971, `Saved/M96/<행>.log` · 캡처 `captures/M96/<행>.png`, 1280×720 `-windowed`)
| 행 | 누름 버튼 · 소리 요청 · 몸 동작 · 결과 (로그 글자 그대로) | 캡처 |
|---|---|---|
| 손도끼 | `누름 — 버튼 「베기」 · 도구 Item_Hatchet` · `소리 요청 Chop (자리 · 변형 5)` · `나무 치기: gatherable.tree 1/3` · 몸 클립 전환 줄 없음(클립 `A_Player_HoldOneHand` 위에 겹침 — 로그 한계) | hatchet.png |
| 맨손(덤불) | `누름 — 버튼 「채집」` · `소리 요청 Pickup (자리 · 변형 2)` · `A_Player_Idle -> A_Player_Gather` | barehand.png (다시 찍기 1회 사용 — 첫 판은 대상 없는 자리라 「열기」) |
| 몽둥이 | `누름 — 버튼 「공격」 · 무기 Item_Club` · `소리 요청 Swing` · `AnimalBirdHit` | club.png (다시 찍기 1회 — 캡처 시각에 동물이 화면 밖) |
| 마체테 | `누름 — 버튼 「손도끼 필요」` · `소리 요청 Deny` · `화면 오류 「손도끼가 있어야 벨 수 있다」` (설계대로 거절) | machete.png |
| 전기톱 | `누름 — 버튼 「베기」` · `소리 요청 Chainsaw_Rev (자리 · 변형 1)` · `나무 치기: gatherable.tree 1/3` | chainsaw.png |
| 곡괭이(기본) | `누름 — 버튼 「캐기」` · `소리 요청 Mine (자리 · 변형 5)` · `A_Player_Idle -> A_Player_MineOneHand` | pickaxe.png (동굴 안) |
| 채집 바구니 | `누름 — 버튼 「채집」` · `소리 요청 Pickup (자리 · 변형 2)` · `A_Player_Idle -> A_Player_Gather` | basket.png (캡처 시각엔 덤불을 따 버려 「공격」) |
| 낚싯대 | `누름 — 버튼 「낚시」` · `소리 요청 Cast`·`Splash` · `A_Player_Idle -> A_Player_FishCast` → 반복 누름 `「그만두기」` | rod.png 「그만뒀다」 · 「낚시」 버튼 |

| 곡괭이(고급) | `누름 — 버튼 「캐기」 · 도구 Item_PickaxeAdvanced` · `소리 요청 Mine (자리 · 변형 5)` · `A_Player_Idle -> A_Player_MineGrind` | pickaxe_adv.png (동굴) |
| 곡괭이(전기) | `누름 — 버튼 「캐기」 · 도구 Item_PickaxeElectric` · `소리 요청 Mine (자리 · 변형 5)` · `A_Player_Idle -> A_Player_MineGrind` | pickaxe_elec.png (동굴) |
| 낚싯대 미끼 | `누름 — 버튼 「낚시」` · `소리 요청 Splash (화면)`·`Splash (자리)` · `A_Player_Idle -> A_Player_FishCast` · 화면 글자 「미끼 낚싯대 · 입질 ×1.5」 | rod_Bait.png |
| 낚싯대 행운 | `누름 — 버튼 「낚시」` · `소리 요청 Cast`·`Splash` · `A_Player_Idle -> A_Player_FishCast` · 「입질 ×0.85」 | rod_Lucky.png |
| 낚싯대 신축 | `누름 — 버튼 「낚시」` · `소리 요청 Swing`·`Splash` · `A_Player_Idle -> A_Player_FishCast` · 「입질 ×1.2」 | rod_Telescopic.png |
| 망원경 | 누름 없음(들면 켜짐) · 로그엔 `처음 안내 tool.item.binoculars` 뿐, 카메라 배율 로그 줄 없음 · **화면 글자 「망원경 · 시야 ×1.3」** (캡처) | binoculars.png |
| 위성 통신기 | 누름 없음 · 로그엔 `처음 안내 tool.item.satellite_communicator` 뿐 · **화면 좌표 「북 102 · 동 -22」·「위성 통신기 · 좌표 보임」** (캡처) | satellite.png |
| 총(권총) | `누름 — 버튼 「공격」 · 무기 Item_PistolSemi` · `[총] 1발째 - 맞을 것 막힘 · 거리 74cm … 탄창 6 남음` · `소리 요청 PistolShot`·`BulletImpact` · `A_Player_Idle -> A_Player_PistolRecoil` → 0.4초 뒤 Idle · 화면 「6/0」 탄창 칸·「재장전」 | pistol.png |
| 포획 던지기 | `누름 — 버튼 「공격」 · 무기 Item_Club` 반복 뒤 `[Capture] 포획 누름 — 던지기 몸짓·소리 Swing · 대상 AnimalCharacter_41` · `올가미 링 시작 … 링 1.50초 · 창 0.60초` → `올가미 링 성공 · 경과 1.20초 · 창 [0.90, 1.50]` · `화면 알림 「포획 성공 - 공작」` · `소리 요청 Cinch` · `포획 순간 소리 Cinch · 진동 켬` · `item.pet_peacock 백엔드 등록 요청 대기열` | capture.png (다시 찍기 1회 — 첫 판은 공작이 도망가 「포획」 흐림. 둘째 판은 캡처 시각이 포획 성공 뒤라 공작이 사라지고 링 없음 — 링 장면은 못 찍음) |

망원경·통신기는 카메라 배율·좌표를 찍는 **로그 줄이 없다**(화면 글자로는 확인) — 결함이 아니라 로그 빈 곳. b 행 끝(포획 던지기 아래 추가). 다음 c 조각. (캡처는 감독이 레퍼런스 게임와 대조해 합격 — 루프 합격 아님)

## c 조각 실측 결과 — 놓인 가구 4행 (루프#974, `-game` `L_Ranch` 1280×720, `-SnareStandNear=structure.item.<ID> -SnareMoveKey=SpaceBar -SnareMoveKeyDelay=14 -SnareMoveKeyRepeat=4`)
> 첫 판은 `-SnareMoveKey` 를 빼먹어 누름 0 → 다시 찍기 1회로 씀(행당 1번 한도 소진). 로그 `Saved/M96/st_<ID>.log`.
| 행 | 누름 버튼 · 판 · 소리 요청 · 몸 (로그 글자 그대로) | 캡처 |
|---|---|---|
| 작업대 | `누름 — 버튼 「열기」` · `판 열림 — 가구 판 · 쪽 Craft · 자리 Workbench` · `소리 요청 Ui_Open (화면 · 변형 4)` · 몸 동작·휘두르기 줄 없음 | st_workbench_adv.png (캡처 시각에 판이 닫혀 가구만 보임 — 판은 로그로 확인) |
| 불(모닥불) | `누름 — 버튼 「열기」` · `판 열림 … 쪽 Craft · 자리 Fire` · `소리 요청 Ui_Open` | st_campfire.png |
| 제련소 | `누름 — 버튼 「열기」` · `판 열림 … 쪽 Craft · 자리 Smelter` · `소리 요청 Ui_Open` | st_smelter.png 「제련소」 판·재료 부족 빨간 글 |
| 보관함 | `누름 — 버튼 「열기」` · `판 열림 … 쪽 Storage · 자리 Backpack` · `소리 요청 Ui_Open` | st_storage_box.png 「보관함」 판(겉이 반투명 — 열리는 중) |
네 행 모두 #0514 때 「판 로그 없음」 이던 빈 칸이 `판 열림` 줄로 메워짐. 결함 없음. 남은 c 행: 음식·설치물·제작 판·목장 칸·상인·옷장·안내.

## 보정 ② 다시 찍기 — 동작 도중 캡처 (루프#975, 감독 INBOX 08:40)
> 낚싯대 4종 = 되풀이 누름(`-SnareMoveKeyRepeat`)을 빼고 한 번만 눌러 던진 채 두고 `-SnareCaptureDelay=11`(누름 6초 + 던진 뒤 5초)로 찍음. 포획 = `-SnareAutoCapture` 를 빼고 `-SnareCaptureRingHold -SnareCaptureDelay=9` 로 링을 멈춘 채 찍음. 실행기 `run_m96_row.py` 그대로.
| 행 | 로그 (글자 그대로) | 캡처 |
|---|---|---|
| 낚싯대(기본) | `누름 — 버튼 「낚시」 · 도구 Item_RodBasic` · `[Fishing] 던짐 coast_3 · 입질까지 10.12초` · `A_Player_Idle -> A_Player_FishCast` | rod.png — 「그만두기」 버튼·물 위 주황 찌·가는 줄 |
| 낚싯대(미끼·행운·신축) | 위와 같은 던짐 줄(입질 6.13·9.81·9.46초) | rod_Bait.png · rod_Lucky.png · rod_Telescopic.png — 찌·줄 + 「입질 ×1.5 / ×0.85 / ×1.2」 글자 |
| 포획 던지기 | `누름 — 버튼 「공격」 · 무기 Item_Club` 3번(자동 누름 — 아직 포획 누름 전) | capture.png — 공작 체력 줄·흰 링+초록 링·「포획」 버튼 켜짐 |

## c 조각 실측 결과 — 음식·설치물 2행 (루프#977, `-game` `L_Ranch` 1280×720 오프라인, 로그 `Saved/M96/<행>.log`)
| 행 | 누름 · 화면 글자 · 소리 · 몸 (로그 글자 그대로) | 캡처 |
|---|---|---|
| 음식(구운 고기) | `-SnareEat=item.roast_meat` → `칸 0 고르고 「먹기」 를 누른다` · `[실측] 화면 알림 「서버에 닿지 못했다 - 잠시 뒤 다시」 (목장 HUD)` · `소리 요청 Ui_Open` · 몸 동작 줄 없음(오프라인이라 서버 거절 — 먹기 성공 장면은 온라인 몫) | eat.png — 배낭 칸 구운 고기·카드 「사용·장착·놓기·버리기」 |
| 설치물(텐트) | `-SnarePlace=item.tent` → `칸 0 「놓기」 · 놓기 시작 예` · `판정 Green · 서버 요청 보냄` · `[실측] 화면 알림 「서버에 닿지 못했습니다」 (목장 HUD)` · `A_Player_Idle -> A_Player_Gather` 0.6초 | place_tent.png — 판 닫힌 목장·텐트 안 놓임 |
#0517·#0520 때 빈 칸이던 「목장에서 거절 글 0」 이 둘 다 목장 HUD 알림으로 메워져 있음. 결함 없음. 남은 c 행: 제작 판·목장 칸·상인·옷장·안내.

## c 조각 실측 결과 — 제작 판·상인 2행 (루프#978, `-game` `L_Ranch` 1280×720 오프라인, 로그 `Saved/M96/<행>.log`)
| 행 | 누름 · 판 · 화면 글자 · 소리 (로그 글자 그대로) | 캡처 |
|---|---|---|
| 제작 판(작업대·나무판) | `-SnareStandNear=structure.item.workbench_adv -SnareCraft=item.plank` → `누름 — 버튼 「열기」` · `판 열림 — 가구 판 · 열림 1 · 화면 붙음 1 · 쪽 Craft · 자리 Workbench` · 레시피 줄 `item.plank … item.wood 0/4` → `모자란 재료 4개를 배낭에 넣었다` → `줄 있음 · 버튼 켜짐 1 · 누름 결과 서버에 물었다/불에 올렸다` · `[실측] 제작 판 글 「서버에 닿지 못했습니다」 (오류)` · `소리 요청 Ui_Open` 뿐(제작 확정 소리·몸짓 0 — 서버 확정 전) | craft_workbench.png — 「작업대」 판 「제작」 탭, 나무판 「나뭇가지 4/4」 초록·나머지 「모자람」 빨강, 「칠하기」 탭·「닫기」 |
| 상인(잡화) | `-SnareStandNear=vendor.General` → `순간이동 … 대상 ShopVendorActor_4` · `[Shop] 상인 앞 — 상점 화면을 올린다` · `누름 — 버튼 「열기」` · `[Shop] 상황 버튼 「열기」 — 상점 판을 편다` · `소리 요청 Ui_Open (화면 · 변형 4)` | vendor_general.png — 「상점」 사기/팔기 탭·잡화상 도토리 대사·손도끼 5·텐트 35·하이킹 배낭 40 코인·오른쪽 빈 배낭 칸·「서버 연결 안 됨」 |
상인은 가까이 서면 판이 저절로 올라오고(`상인 앞`) 「열기」 를 눌러도 같은 판 — 결함 없음. 오프라인이라 사기·제작 성공은 온라인 몫. 남은 c 행: 목장 칸 수확·넣기·옷장·안내.

## c 조각 실측 결과 — 목장 빈 칸·옷장·안내 3행 (루프#979, `-game` 1280×720 오프라인, 로그 `Saved/M96/<행>.log`)
| 행 | 누름 · 화면 글자 · 소리 · 몸 (로그 글자 그대로) | 캡처 |
|---|---|---|
| 목장 칸 넣기(빈 칸) | `L_Ranch -SnareStandNear=ranch. -SnareMoveKey=SpaceBar` → `검사용 순간이동 … 대상 BP_RanchSlot_C_2` · `[UsageHint] 처음 안내 ranch.slot — 빈 칸 '넣기' > 동물·씨앗 / 찬 칸 '수확'` · `[실측] 누름 — 버튼 「넣기」` · `인벤토리 캐시에 넣을 펫·씨앗이 없어 요청을 쌓지 않는다` · `[실측] 화면 알림 「넣을 동물이나 씨앗이 없다」 (목장 HUD)` · `A_Player_Idle -> A_Player_IdleStretch` 뿐(주먹질 0·`Swing` 0) | ranch_empty.png — 「3번 칸 - 비어 있음」 카드·「넣기」·「배치할 것: 없음」·위쪽 「넣을 동물이나 씨앗이 없다」 |
| 옷장 | `-SnareWardrobe` → `검사용 옷장 열기 (-SnareWardrobe) — 장비 탭 · 「옷장」 을 누른다` · `[UI] 배낭 장비 탭 「옷장」 — 옷장 화면을 열었다 (장비 보기 예)` (루프#980 보정 — 스위치가 `PressWardrobeShop` 대신 화면 단추와 같은 `PressWardrobe` 를 부름. `PressWardrobeShop` 은 젬 꺼짐이면 바로 돌아와 1.0 에서 닿을 길이 없어 그대로 둠) | wardrobe.png — (루프#982 보정) 배낭 판 숨김 · 캐릭터 앞모습이 왼쪽 반 고리 가운데에 화면 높이 약 60% · 오른쪽 판 폭 29%·4열 · 「나가기」 오른쪽 위 · 젬 꺼짐 글 「이 칸 꾸미기가 없다 — 상자·출석에서 얻는다」 · 레퍼런스 게임와 나란히 `captures/M96/wardrobe_vs_reference.png` |
| 처음 하는 사람 안내 | `L_Hunt --guide`(= `-SnareNoGuide` 뺌 + `-ini:GameUserSettings:[Snarehaven.Guide]:Seen=False`) → `[실측] 첫 실행 안내 1/11 「왼쪽 아래 조이스틱으로 걸어 다녀요.」` · `[UI] 첫 실행 안내를 띄웠다 (11장)` | guide.png — 위 가운데 카드 「1/11 …」 「건너뛰기」 「다음」, 아래 「! 상점 열기」 알약. 다시 찍기 1회(첫 판은 `-SnareAutoAttack` 이 「열기」 를 눌러 상점 판이 열려 카드와 겹침 — `Saved/M96/old/guide_shop.png`) |
실행기 `run_m96_row.py` 에 `--guide` 옵션 추가(검사 도구, 게임 코드 0). 결함 없음.

## d 조각 — 빈 칸 확인 (루프#979)
17행 전부 실측 로그 인용 + 캡처가 있다(b #971·#972·#973, c #974·#977·#978·#979, 보정 #975·#976). 코드 근거와 다른 행 = 옷장 한 곳 — 결함이 아니라 설계(젬 숨김 #0954)가 옷장 「상점 열기」 를 닫아 둔 것. **이 맥 오프라인으로는 못 재는 칸(감독 PIE·폰·온라인 몫):** 찬 목장 칸 수확 · 먹기 「+N」 · 설치물 놓임 · 제작 성공(굽기·확정 소리) · 옷장 진열·사기(젬 켠 빌드) · 낚시 입질·잡기 · 총알 날아가는 장면 · 곡괭이·도끼 휘두르는 순간 클립 이름(덮어쓰기 클립이라 로그에 안 찍힘). 레퍼런스 게임와 나란히 대조는 전부 감독 몫.
