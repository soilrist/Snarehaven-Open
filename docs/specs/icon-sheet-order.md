# 아이콘 시트 주문서 — 메시 없는 빈 행 21개 (M31-04 17 + M77 4)

> 루프#499 작성. 감독이 바르코 3×3 시트(워크플로우 `57c7773c-…`, nano-banana-2, 1:1)로 그려 반입한다.
> 근거: 헤드리스 탐침 `loop/tmp/495/noicon.log`(빈 행 30) − M31-01~03 이 렌더로 채운 13행 = **17행**. M31-02 에서 주문서로 돌린 행 0.
> **M77 갱신(루프#829):** 총 2행·꾸미기 2행은 렌더로 채웠고, 남은 주문서 칸 = 시트 1·2 의 17 + §4-b 탄약 1 + §4-c 꾸미기 3 = **21**. 아래 17 은 시트 1·2 만의 수. M31-05 단정이 허용할 빈 이름은 이 21행(`item.ammo`·`gacha.deco_dirt_path`·`deco_stone_fence`·`deco_starlight_bg` 포함)이다. 허용 목록은 `Source/Snarehaven/Private/Tests/SnarehavenItemIconTest.cpp` 의 `IconsAwaitingSheetIDs`(M78, 2층 `IconMatchesItemID` 실측 빈 행 21 = 목록 21) — 시트를 반입하면 그 줄을 지운다(안 지우면 시험이 실패).
> 2층 `Snarehaven.Data.Items.IconMatchesItemID` 마지막 실측(#0570) = 「아이템 행 396개 중 아이콘이 붙은 행 379개」 → 396 − 379 = 17, 칸 수와 같다.

## 1. 합격 기준 (한 문장)
두 장(9칸 + 8칸)을 반입한 뒤 2층 시험이 「아이콘이 붙은 행 396개」를 내고, 배낭에서 새 칸 하나를 캡처해 이웃 아이콘과 붓질이 같아 보인다.

## 2. 공통 그림 지시 (바르코 프롬프트 뒷부분 — 기존 `_Source/raw/Snare/Items/Icons/Icon_fox_meat.txt` 와 같은 글)
칸마다 `Game inventory item icon of <아래 표 「그릴 것(영문)」>.` 뒤에 그대로 붙인다:

```
Art style: cozy cartoon survival-game inventory icon - chunky simplified 2D sticker illustration with a very thick, uniform dark plum-brown outline (#3A2830) around the whole silhouette, soft painted shading with gentle gradients (one soft shadow tone, one soft highlight), smooth matte surfaces, slightly muted warm colors, a gentle 3/4 perspective that shows a little thickness and volume, big bold readable silhouette that still reads at 52 pixels, few small details, no tiny speckles, no realistic texture. Single item only, centered, filling about 80% of the square frame. Plain pure white background, no ground shadow, no drop shadow, no text, no circle or frame behind the item.
```

참고 그림(톤 고정): 당근·여우고기·꿀 — 메모리 「바르코 아이콘 3×3 시트」 1차 톤과 같게. 파일 이름은 `Icon_<ID 의 item. 뒤>.png` (예 `item.pig_meat` → `Icon_pig_meat.png`).

## 3. 시트 1 — 동물·음식·잡화 (9칸)

| 칸 | ID · 이름 | 레퍼런스 게임 비슷한 아이콘 (자료 파일 id) | 그릴 것 (한 줄) | 그릴 것 (영문) | 이웃 아이콘 |
|---|---|---|---|---|---|
| 1 | `item.pig_meat` 돼지고기 | 여우고기 (470) | 분홍빛 날고기 덩이 + 두꺼운 흰 비계 띠, 여우고기보다 둥글고 통통 | a chunky raw pork slab: soft pink meat with a thick creamy-white fat band along one side, rounded plump shape. Diagonal pose | `Icon_fox_meat.png` |
| 2 | `item.pig_hide` 돼지 가죽 | 늑대 털가죽 계열 (사냥 부산물) | 털 거의 없는 연분홍 가죽을 둘둘 만 두루마리 + 끈 하나 | a rolled-up hairless pale pink pig hide tied with one brown cord | `Icon_wolf_fur.png` |
| 3 | `item.cow_meat` 소고기 | 곰고기 (472) | 진한 붉은 스테이크, 대리석 무늬 비계 두세 줄, 뼈 없음 | a thick deep-red beef steak with two or three soft white marbling streaks, no bone. Diagonal pose | `Icon_bear_meat.png` |
| 4 | `item.cow_hide` 소가죽 | 늑대 털가죽 계열 | 흰 바탕 검은 얼룩 소가죽을 접어 둔 것(모서리 한 끝 말림) | a folded cow hide with white base and big black patches, one corner curled | `Icon_wolf_fur.png` |
| 5 | `item.sheep_meat` 양고기 | 염소고기 (476) | 갈비 두 대가 붙은 양갈비, 끝에 흰 뼈 둘 | a lamb rack piece with two short white rib bones sticking out, rosy meat with thin fat cap | `Icon_goat_meat.png` |
| 6 | `item.sheep_hide` 양가죽 | (양털과 겹치지 않게) | 뒷면 갈색 가죽이 보이게 반쯤 뒤집힌 크림색 곱슬 양털 가죽 — **양털 뭉치 아이콘과 다르게 납작한 판 모양** | a flat sheepskin pelt, cream curly fleece on top with the tan leather underside showing at a turned-up edge | `Icon_wool.png` (구별 대상) |
| 7 | `item.grilled_eel` 장어 구이 | 구운 생선류 (구운 잉어 등) | 꼬치 둘에 꿴 갈색 윤기 장어 토막 셋, 그을린 줄무늬 | three glossy brown grilled eel pieces on two wooden skewers, grill marks | `Icon_eel.png` · `Icon_grilled_carp.png` |
| 8 | `item.stair_kit` 계단 건축 부품 | 계단 건축 부품 (273) | 끈으로 묶은 짧은 나무판 다발 + 위에 계단 세 단 모양으로 쌓인 판, 쇠 꺾쇠 둘 | a bundle of short wooden planks tied with rope, stacked like three little stair steps, two small metal brackets | `Icon_plank.png` · `Icon_metal_parts.png` |
| 9 | `item.crate_key` 상자 열쇠 | (원본 = 상자 열쇠를 재화로 삼, DESIGN 1589) | 금빛 큰 고리 열쇠, 고리에 단풍잎 모양 장식 | a chunky golden key with a big round bow decorated with a small maple leaf | `Icon_storage_box.png` |

## 4. 시트 2 — 전력·부품 (8칸 + 빈 칸 1)

| 칸 | ID · 이름 | 레퍼런스 게임 비슷한 아이콘 (자료 파일 id) | 그릴 것 (한 줄) | 그릴 것 (영문) | 이웃 아이콘 |
|---|---|---|---|---|---|
| 1 | `item.solar_kit` 태양광 키트 | 태양광 키트 (46) | 짙은 파란 격자 패널 한 장 + 뒤 받침 다리, 모서리에 감긴 전선 | a small dark-blue grid solar panel on a folding stand with a coiled wire at the corner | `Icon_boat_motor_electric.png` |
| 2 | `item.battery` 배터리 | 배터리 (138) | 통통한 원통 건전지, 초록 띠 + 위 은색 단자, 옆에 번개 표시 | a chunky cylindrical battery with a green band, silver top terminal and a small lightning bolt mark | `Icon_boat_motor_electric.png` |
| 3 | `item.lamp_electric` 전등 | (원본 전등 가구) — 게임 메시 `SM_Prop_Lantern_01`(DESIGN 1226) | 가는 쇠기둥 위 따뜻한 주황 유리 등갓 — 게임 등불 메시와 같은 실루엣 | a slim standing electric lantern lamp: thin dark metal pole with a warm glowing orange glass shade on top | `Icon_storage_box.png` (가구 붓질) |
| 4 | `item.light_bulb` 전구 | 전구 (47) | 둥근 유리 전구 + 은색 나사 꼭지, 안에 주황 필라멘트 | a round glass light bulb with a silver screw base and a warm orange filament inside | `Icon_copper_ingot.png` |
| 5 | `item.polymer` 폴리머 합성물 | 폴리머 합성물 (270) | 반투명 호박색 판 두 장을 겹친 것 — 수지 접착제보다 매끈한 각진 판 | two stacked translucent amber resin sheets with smooth beveled edges | `Icon_resin.png` · `Icon_resin_adhesive.png` |
| 6 | `item.circuit_board` 전자 회로 기판 | 전자 회로 기판 (269) | 초록 기판 위 금빛 선 몇 줄 + 검은 칩 하나 | a small green circuit board with a few golden traces and one black chip | `Icon_copper_ingot.png` |
| 7 | `item.electric_motor` 전자 모터 | 전자 모터 (268) | 회색 원통 모터 + 앞 은색 축 + 구리 코일이 보이는 틈 | a compact grey cylindrical electric motor with a silver shaft and copper coil visible through a slot | `Icon_boat_motor_electric.png` |
| 8 | `item.crate_autumn26` 가을 꾸미기 상자 | (원본 = 계절 상자, DESIGN 1587) | 주황·갈색 나무 상자, 뚜껑에 단풍잎 무늬, 금빛 자물쇠 — 열쇠(시트 1 칸 9)와 한 짝 | a wooden loot crate painted in autumn orange and brown with a maple leaf emblem on the lid and a golden lock | `Icon_storage_box.png` |
| 9 | (빈 칸) | — | 다시 뽑기용 — 시트 1·2 에서 52px 로 안 읽히는 칸 하나를 여기서 다시 그린다 | — | — |

## 4-b. 시트 3 — 총 부속 (1칸, M77-02)

> 루프#827. `item.ammo` 는 게임 메시가 없어(총 메시 `SM_Weapon_*` 둘뿐) 렌더 불가 → 주문서 칸. 시트 2 의 빈 칸 9 에 같이 그려도 된다.

| 칸 | ID · 이름 | 레퍼런스 게임 비슷한 아이콘 | 그릴 것 (한 줄) | 그릴 것 (영문) | 이웃 아이콘 |
|---|---|---|---|---|---|
| 1 | `item.ammo` 탄약 | 원본 「Ammo [25]」 묶음 (DESIGN 1202) | 놋쇠빛 총알 세 개가 종이 상자 앞에 비스듬히 선 모습 — 총알 끝은 구리색 | three chunky brass bullet cartridges with copper tips standing in front of a small open cardboard ammo box | `Icon_metal_parts.png` |

## 4-c. 시트 4 — 가챠 꾸미기 (3칸, M77-03)

> 루프#828. 나무 울타리(`Fence_01`)·붉은 헛간(`Barn_02`)은 게임 메시를 렌더해 아이콘을 붙였다(`tools/blender/render_deco_icon.py`). 아래 셋은 **메시가 없어** 렌더 불가 → 주문서 칸 — 흙길·하늘은 머티리얼/조명 값뿐이고, 돌담 `Fence_02` 는 `make_house_meshes.py` 가 만든 메시라 원본 obj 가 없다.

| 칸 | ID · 이름 | 레퍼런스 게임 비슷한 아이콘 | 그릴 것 (한 줄) | 그릴 것 (영문) | 이웃 아이콘 |
|---|---|---|---|---|---|
| 1 | `gacha.deco_dirt_path` 흙길 타일 | (원본 = 바닥 타일) | 갈색 흙 사각 타일 한 장, 가장자리에 풀 몇 가닥, 가운데 발자국 | a square brown dirt path tile with a few grass tufts on the edges and footprints in the middle | `Icon_storage_box.png` |
| 2 | `gacha.deco_stone_fence` 돌담 울타리 스킨 | (원본 = 돌담) | 회색 돌을 쌓은 낮은 담 한 토막, 윗면 납작한 돌 | a short grey stacked-stone wall segment with flat capstones on top | `Icon_wood.png` |
| 3 | `gacha.deco_starlight_bg` 별빛 목장 배경 스킨 | (원본 = 밤하늘 배경) | 남색 밤하늘 둥근 창에 별 여러 개와 초승달, 아래에 작은 목장 지붕 실루엣 | a round navy night-sky window with several stars and a crescent moon, tiny ranch roof silhouette at the bottom | `Icon_storage_box.png` |

## 5. 반입 순서 (감독)
1. 자르기·투명화 → `_Source/raw/Snare/Items/Icons/Icon_<id>.png` + 출처 `.txt`(기존 형식).
2. 에디터 PIE 끈 뒤 `tools/unreal/import_item_icons.py` → `tools/unreal/create_data_tables.py` 커맨드릿 — 같이 재저장된 다른 표는 되돌리고 `DT_ItemTable` 만 커밋(#0570 과 같은 길).
3. 2층 `Automation RunTests Snarehaven.Data.Items` → 「아이콘이 붙은 행 396개」.
4. M31-05(빈 칸 0 단정)는 그 뒤 루프가 한다.

## 6. 하지 않을 것
- 메시 렌더로 대신 그리지 않는다 — 이 17행은 게임 메시가 없거나(재료·부품) 대용 메시라(전력 가구, INBOX 보정 02:45 「전력 가구 진짜 메시」) 렌더하면 대용품 그림이 된다.
- 기존 아이콘을 복사해 붙이지 않는다(가죽 셋·고기 셋이 서로 구별돼야 한다).
