# EQUIPMENT — 장비 현황표 (M9-00)

> **이 문서는 "장비 부위가 무엇이고 · 무엇이 그 부위를 채우고 · 지금 무엇이 있고 없나" 를 한 장으로 모은 것이다.** 값을 정하는 곳이 아니다 —
> 무엇을 만드는지는 `docs/DESIGN.md` 5절 각주 「장비 로스터」가, 옷 표현 방식은 `docs/decisions/006` 이, 붙이는 방식은 3.5 가 이미 정했다. 이 문서는 그 셋을 **옮겨 놓고 자리를 가리킬 뿐**이다.
> 근거: `docs/DESIGN.md` 3.5(장비 착용 시각화)·5절 각주 「장비 로스터」 · `docs/decisions/006-modular-equipment-layers.md` · 조각 목록은 `docs/ROADMAP.md` M9 절.
> **범위 밖:** 장비 스탯(공격력·채집 속도), 신발·장갑 같은 로스터에 없는 부위, 확장 **후보**로만 적힌 사냥용 도끼 — **이 문서에 새 숫자를 적지 않는다**(지금 0개). 표의 코드 줄은 **이 시점의 `develop`** 을 말한다.
> 마지막 갱신: 루프#125 (2026-09-22 — M9-01 왼손 소켓·M9-02 옷을 겹치는 길까지 반영).

## A. 장비 로스터 (DESIGN 5절 각주)

**장비에는 스탯이 없다.** 3.1 근접 공격은 "무기 없음(맨손)·종별 차등 없음" 이고 3.3 채집은 손 터치만으로 된다 — 그래서 무기도 도구도 **순수 시각 효과**이고, 이 표에 능력치 열이 아예 없다.

| 부위 (`EEquipSlot`) | 항목 | 상태 | 근거 |
|---|---|---|---|
| `Weapon` | 몽둥이 | **있다** — 1차 검증 대상 (M5-06a~c 로 닫힘) | DESIGN 5절 로스터 「무기」 |
| `Tool` | 손도끼 (마른 나무용) | **있다** — M9-04, 행 `Item_Hatchet`(`item.hatchet`) · `SM_Tool_HandAxe_01` 45cm | DESIGN 5절 로스터 「도구」 · 3.3 채집 대상 2종 |
| `Tool` | 채집 바구니 (베리용) | **있다** — M9-04, 행 `Item_GatherBasket`(`item.gather_basket`) · `SM_Tool_Basket_01` 35cm | 〃 |
| `OutfitTop` | 상의 1벌 | 없다 — M9-05 (감독 대기, 바르코 생성) | 결정 006 · DESIGN 3.5 |
| `OutfitBottom` | 하의 1벌 | 없다 — M9-05 (감독 대기, 바르코 생성) | 〃 |

**사냥용 도끼는 조각으로 만들지 않는다** — 로스터가 "이후 확장 **후보**" 라고만 적었다. 후보는 확정이 아니라서 여기 행이 없다(항목을 지어내지 않는다).
**동시 장착은 부위마다 1개씩**(무기 하나 + 도구 하나, DESIGN 3.5 🟡) — 여러 개를 몸에 주렁주렁 차는 것은 나중이다.

## B. 부위 열거형과 데이터 모양

| 무엇 | 값 | 왜 |
|---|---|---|
| 부위 열거형 | `EEquipSlot` **5개** — `None`·`Weapon`·`Tool`·`OutfitTop`·`OutfitBottom` (`Source/Snarehaven/Public/Data/ItemRow.h:27`) | 결정 006 이 **부위별 모듈형 레이어**를 골랐다 — 상의만 갈아입히려면 슬롯이 처음부터 부위로 갈려 있어야 한다 |
| 저장소 | `DT_ItemTable` 에 **열을 늘렸다** — 새 표를 만들지 않았다 (`FItemRow` 의 `EquipSlot`·`EquipMeshRef`·`EquipSocketName`·`EquipOutfitMeshRef`) | **메시 칸이 둘인 것은 부위가 붙이는 종류를 정하기 때문이다**(M9-02) — 손은 스태틱, 옷은 스켈레탈.  사냥·목장·채집이 이미 이 표 하나로 돈다. 장비만 새 표로 빼면 "아이템인데 두 군데서 관리" 가 된다 (DESIGN 3.5 · DataDriven 5) |
| 장비인지 가르는 값 | `ItemType == EItemType::Equipment` **그리고** `EquipSlot != None` | 둘 중 하나만 보면 열만 비어 있는 행이 장비로 샌다. 붙이는 쪽도 HUD 쪽도 이 두 조건을 같이 본다 |
| 장착 상태가 사는 곳 | `UEquipmentComponent` 의 `SlotMeshes`(손, `:93`)·`OutfitMeshes`(옷, `:97`)·`SlotItems`(행 이름, `:101`) 세 `TMap` (`EquipmentComponent.h`) — **복제 안 됨** | 장착은 **클라이언트 로컬 시각 효과**다. 보유 원본은 백엔드 인벤토리에 있고(CLAUDE.md 8) 이 컴포넌트는 "그중 하나를 들고 있다" 만 그린다 |
| 재화·인벤토리 | **안 탄다** — 장착/해제가 잔액·인벤토리 값을 한 칸도 안 바꾼다 | DESIGN 3.5 · CLAUDE.md 8. 이 마일스톤에서 인벤토리·잔액을 **쓰는** 코드가 한 줄이라도 생기면 반려다 |

## C. 장비가 몸에 붙는 길 (지금 한 줄기뿐)

| 단계 | 어디서 | 하는 일 |
|---|---|---|
| 1. 부른다 | 화면 버튼 `UHuntHudWidget::ToggleEquipButtonItem(부위)`(`HuntHudWidget.cpp:383`) / 헤드리스 `-SnareEquip=<행 이름>`(`SnarehavenCharacter.cpp:337`)·`-SnareTouch=equip[:부위]`(`:1097`) | 셋 다 **같은 함수**로 들어간다 — 사람 손과 검사가 다른 길을 타지 않는다. 부위는 인자로 받는다(루프#126) |
| 2. 표를 읽는다 | `UEquipmentComponent::EquipItem`(`EquipmentComponent.cpp:31`) | 행 이름(`Item_Club`)으로 찾는다 — `ItemID`(`item.club`)가 아니다. 장비 아닌 행·빈 메시·빈 소켓 이름이면 아무것도 안 하고 `false` |
| 3. 몸을 찾는다 | `FindBodyMesh`(`:25`) | 주인의 `USkeletalMeshComponent` 하나. 없으면 붙일 자리가 없다 |
| 4. 소켓을 확인한다 | `Body->DoesSocketExist(SocketName)`(`:100`) | **오타를 여기서 잡는다** — 표에 적힌 이름이 실제 소켓과 다르면 조용히 공중에 붙지 않고 거절한다 |
| 5. 붙인다 | `AttachToComponent(..., SnapToTargetNotIncludingScale, 소켓)`(`:130`) | 부위에 이미 뭔가 들려 있으면 그 메시를 갈아 끼운다 (한 부위 = 한 개) |
| 6. 뗀다 | `UnequipSlot`(`:195`) | 컴포넌트를 지우지 않고 메시만 비운다 — 다시 들 때 새로 만들지 않는다 (손·옷 양쪽) |

**소켓은 스켈레톤이 아니라 몸 메시에 달려 있다** — `Socket_Weapon`(오른손 `RightHand`)·`Socket_Tool`(왼손 `LeftHand`)은 `SK_Player_Body` 에 있다(목록은 `tools/unreal/setup_equipment_socket.py:39` 의 `SOCKETS` 한 줄씩). 파이썬 API 가 `USkeleton` 쪽 소켓을 안 열어 주기 때문이고, 몸 메시가 하나뿐이라 결과는 같다(루프#89).

**옷은 이 여섯 단계를 타지 않는다** (M9-02) — 4·5단계(소켓 확인·소켓에 매달기) 대신 `EquipOutfitMesh`(`:148`)가 몸 자체에 붙이고(`:181`) `SetLeaderPoseComponent`(`:188`)로 몸 뼈를 따라가게 한다. **몸 메시를 갈아 끼우지 않는다** — 그 위에 한 겹 더 얹는 것이다(결정 006 부위별 모듈형 레이어). 갈리는 자리는 `EquipItem` 안의 `IsOutfitSlot(Slot)` 하나다.

## D. 지금 있는 것 / 아직 없는 것

| 부위 | 지금 있는 것 | 아직 없는 것 |
|---|---|---|
| 부위 열거형 | **5개 다 있다** (`ItemRow.h:27`) — 이름만 서 있고 `Weapon` 말고는 채운 것이 없다 | 없음 (열거형은 늘리지 않는다 — 로스터에 없는 부위는 안 만든다) |
| 소켓 | **둘** — `Socket_Weapon`(오른손 `RightHand`)·`Socket_Tool`(왼손 `LeftHand`), 둘 다 `SK_Player_Body` 에 (루프#124) | 없음 — 옷은 소켓을 쓰지 않는다(몸에 겹친다). 로스터에 손 부위가 더 없다 |
| 표 행 | 무기 `Item_Club`(`EquipSlot=Weapon` · `Socket_Weapon` · `SM_Weapon_Club_01`) · 도구 `Item_Hatchet`·`Item_GatherBasket`(`EquipSlot=Tool` · `Socket_Tool`, M9-04) — 13.5 도구 4 는 손도끼 메시를 자리표시로 든다(🟡) | 옷 2행 (M9-05, `EquipOutfitMeshRef` 를 채운다) — 전부 에셋이 있어야 선다. **옷 행의 삼각형 한도 검사는 아직 없다**(M9-05 몫 — 스켈레탈은 삼각형 세는 API 가 달라 행이 생길 때 붙인다) |
| 붙이는 길 | **두 갈래** (루프#125) — 손은 스태틱(`SlotMeshes`, `EquipmentComponent.h:93`), 옷은 스켈레탈(`OutfitMeshes`, `:97`) + `SetLeaderPoseComponent`. 갈리는 자리는 `IsOutfitSlot` 하나다 | 없음 — 옷 **행**이 없을 뿐 길은 섰다(M9-05 가 행을 채우면 `EquipItem` 이 그대로 탄다) |
| 에셋 | 몽둥이 `Equipment/Weapons/SM_Weapon_Club_01` · 손도끼 `Equipment/Tools/HandAxe/SM_Tool_HandAxe_01` · 바구니 `Equipment/Tools/Basket/SM_Tool_Basket_01` (전부 `tools/unreal/import_equipment_weapons.py`) | 옷 1벌(M9-05) — **전부 바르코 생성이고 감독 몫**이다(DESIGN 3.5 · 5절 각주). 옷은 `SK_Player_Body` 스켈레톤에 맞는 스킨 웨이트를 가져야 한다(결정 006 「감수하는 것」) |
| 화면 버튼 | **부위마다 한 칸, 넷** (루프#126) — `EquipButton_Weapon`·`_Tool`·`_OutfitTop`·`_OutfitBottom` (사냥 HUD 왼쪽 위에서 아래로 그 순서, `HuntHudWidget.cpp:674`). 부위 목록·위젯 이름은 한 곳에서 나온다(`:259`·`:265`) — 3층 스위치와 2층 검사가 같은 이름을 쓴다. 후보는 부위별로 "든 것이 먼저, 없으면 그 부위 보유 첫 장비"(`:314`), 가리킬 것이 없는 부위는 접힌다(`:458`) | 없음 — 도구·옷 **행**이 서면(M9-04·05) 그 버튼이 저절로 뜬다 |
| 남의 화면 | **안 보인다** — `UEquipmentComponent` 에 복제(Replicated) 칸이 하나도 없다. 헤더 주석이 "M5-06c 뒤 · DESIGN 3.4" 로 미뤄 뒀다 | 부위별 장착 **이름만** 복제 (M9-06, 🟡 DESIGN 3.5 에는 없다). 메시는 각자 표에서 찾는다 — **에셋 참조를 회선에 싣지 않는다** |
| 2층 검사 | **여섯** — `Snarehaven.Item.Equipment.RowsAreWearable` · `…Socket.ExistsOnHandBone`(두 소켓) · `…Equip.AttachesToSocket` · `…Equip.WeaponAndToolTogether`(루프#124) · `…Equip.OutfitLayersOverBody`(루프#125) · `Snarehaven.HuntHud.Equip.ButtonsSplitBySlot`(루프#126, `SnarehavenHuntHudTest.cpp:654`) | 복제 검사 (M9-06) |

**에셋이 없어도 시스템 조각은 선다** — M9-01·02·03·06 은 자리표시(몽둥이·몸 메시)로 검사한다(자리표시는 🟢, Autonomy). 에셋이 필요한 M9-04·05 만 감독 대기다.

## E. 이후 조각이 고칠 자리

| 조각 | 하는 일 | 이 문서에서 갱신할 줄 |
|---|---|---|
| M9-01 | 왼손 소켓 `Socket_Tool` (`setup_equipment_socket.py` 안에서 — 새 스크립트 없다) (**끝남** — 루프#124) | 표 D 소켓 줄 · 표 C 각주 |
| M9-02 | 부위별 스태틱/스켈레탈 분기 + `SetLeaderPoseComponent` (**끝남** — 루프#125) | 표 D 붙이는 길 줄 · 표 B 장착 상태 줄 |
| M9-03 | 장비 버튼을 부위 단위로 (**끝남** — 루프#126) | 표 D 화면 버튼 줄 · 표 C 1단계 |
| M9-04 | 도구 2종 에셋 + 표 행 2개 | 표 A `Tool` 두 줄 · 표 D 표 행·에셋 줄 |
| M9-05 | 옷 1벌 에셋 + 표 행 2개 (`EquipOutfitMeshRef`) + 옷 삼각형 한도 검사 | 표 A `Outfit*` 두 줄 · 표 D 표 행·에셋 줄 |
| M9-06 | 부위별 장착 이름 복제 | 표 D 남의 화면 줄 · 표 B 장착 상태 줄(복제 여부) |
| M5-06a~c | 무기 1종 검증 (**끝남** — 루프#88·#89·#92) | 표 A `Weapon` 줄 · 표 C 전체의 전제 |
