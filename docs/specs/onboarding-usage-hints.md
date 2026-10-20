# 기능 명세: 처음 들기·처음 다가가기 안내 한 줄

- 상태: 초안 (루프#422 작성 — 🟡 가정은 INBOX `확인` 으로 감독에게 올림)
- 마일스톤: M28 (조각 M28-05, 표는 M28-06 안내서와 같이 씀) · 범위(커밋 scope): `ui`
- DESIGN 연결: `DESIGN.md` 12 「온보딩/튜토리얼」 (가)(나) · 13 「퀘스트·안내」 Guide Book · 3.3 (떠 있는 시간 숫자를 적을 자리)

## 1. 합격 기준 (한 문장)
> 처음 하는 사람이 도구를 처음 손에 들거나 시설 앞에 처음 서면, 상황 버튼 위에 「무엇을 누르면 무엇이 된다」 한 줄이 6초 떠 있고, 같은 키는 이 기기에서 두 번 다시 뜨지 않는다.

(ROADMAP M28-05 합격 「첫 들기 → 한 줄 · 두 번째 0 · 저장된 키 다시 읽으면 0」 을 그대로 옮겼다. 감독 가정 (가) 그대로이고 사람 칸은 비어 있지 않다.)

## 2. 플레이어가 보는 것
- **입력:** 따로 누를 것이 없다. 계기는 둘뿐이다.
  1. **처음 들기:** 도구 행을 손에 든 순간(`UEquipmentComponent::OnEquipmentChanged` 가 왔을 때 손 부위 `GetEquippedRow` 의 `ItemID` 가 표에 있으면). 상황 버튼이 도구를 자동으로 들 때(루프#416 `BeginAttack → EquipItem`)도 같은 알림이라 똑같이 뜬다.
  2. **처음 다가가기:** 상황 버튼 동사(`ASnarehavenCharacter::GetInteractVerb`)가 바뀐 순간. 동사 `Open` 이면 대상 놓인 물건의 `PlaceableFunction`(Workbench·Campfire·Grill·Smelter·Storage)으로, `Harvest`·`Place` 면 목장 칸으로 키를 고른다. 상인은 `AShopVendorActor::UpdateProximity` 가 처음 true 가 된 순간.
- **화면:** 상황 버튼 바로 위, 지금 「…필요 — 잡화점」 크림 알약(`UHuntHudWidget::NeedToolHint`)과 **같은 자리·같은 모양**의 알약에 한 줄. 도구 필요 알약이 떠 있으면 그쪽이 이긴다(해야 할 일을 막는 쪽이 먼저) — 안내 한 줄은 기다렸다가 필요 알약이 내려가면 남은 시간만큼 뜬다. 소리 없음(알림 줄 소리와 겹치지 않게).
- **글 예시 (표 `Line` 칸):**
  | 키 | 한 줄 |
  |---|---|
  | `tool.item.rod_basic` (낚싯대 넷 공통) | 물가 낚시 포인트 앞에서 「낚시」 → 찌가 잠기면 연타 |
  | `tool.item.hatchet` | 나무 앞에서 「베기」 — 한 번에 원목 3~5개 |
  | `tool.item.pickaxe_basic` (곡괭이 셋 공통) | 광맥 앞에서 「캐기」 — 돌·광석이 나와요 |
  | `tool.item.chainsaw` | 나무 앞에서 「베기」 한 번에 1.5초 동안 썰어요 |
  | `tool.item.binoculars` | 들고 있으면 멀리 있는 동물 이름이 보여요 |
  | `tool.item.satellite_communicator` | 들고 있으면 지도에 내 위치가 떠요 |
  | `tool.item.club` | 동물 앞에서 「공격」 → 체력이 낮아지면 「포획」 |
  | `place.Workbench` | 「열기」 → 재료를 골라 만들기 |
  | `place.Campfire`·`place.Grill` | 「열기」 → 날고기·물고기를 넣으면 시간이 지나 요리 |
  | `place.Smelter` | 「열기」 → 광석을 넣으면 주괴로 녹아요 |
  | `place.Storage` | 「열기」 → 가방 물건을 넣어 두는 칸 |
  | `ranch.slot` | 빈 칸 「넣기」 → 동물·씨앗 / 찬 칸 「수확」 |
  | `vendor.shop` | 「상점 열기」 → 코인으로 도구·텐트를 사요 |

  글 내용(기능 설명)은 지금 코드의 동작을 옮긴 것이고 구현 조각에서 실제 동작과 다시 맞춘다. 망원경 = 카메라를 더 멀리(DESIGN 453 줌 줄 「최대 1.3 = 망원경 거리」), 위성 통신기 = 가진 동안 미니맵 줄 좌표(DESIGN 472·13.5), 바구니 = 채집물 「채집」(DESIGN 419~420)에서 옮겼다. 바구니가 더 많이 딴다는 말은 구현 조각이 13.5 산출 줄을 확인해 틀리면 글을 고친다 — 기능을 새로 만들지 않는다.
- **실패·예외:**
  - 첫 실행 안내(`USnareFirstRunGuideWidget`)가 떠 있는 동안은 안 띄우고 **본 것으로 치지도 않는다**(안내가 끝난 뒤 다시 들거나 다가가면 그때 뜬다). 두 안내가 겹치면 글이 화면을 덮는다.
  - 판(제작·상점·배낭)이 열려 있으면 안 띄운다(같은 이유, 본 것으로 안 침).
  - 한 번에 하나. 떠 있는 동안 다른 키가 오면 **줄 세우지 않고 버린다**(본 것으로 안 침 — 다음 기회에 뜸). 줄을 세우면 들기·다가가기가 몰릴 때 6초씩 밀려 엉뚱한 때 뜬다.
  - 게스트도 자기 기기에서 똑같이 뜬다(화면 일이라 복제 없음). 남의 캐릭터가 든 것은 계기가 아니다(내 폰의 `OnEquipmentChanged` 만 듣는다).

## 3. 숫자
| 이름 | 값 | 단위 | 근거 |
|---|---|---|---|
| 한 줄 떠 있는 시간 | 6 | 초 | 표 가장 긴 줄(쉼표 포함 약 28자)을 모바일에서 읽는 시간. 한국어 묵독 약 5자/초 → 5.6초를 올림. 알림 줄 `ShowNoticeBanner` 2초는 「+1 잉어」 같은 4~6자용이라 그대로 못 쓴다. 레퍼런스 게임 원본엔 이 한 줄이 없어(안내는 Guide Book 책) 레퍼런스 값이 없다 → 🟡 |
| 다시 보여 주는 횟수 | 0 (키마다 1번) | 회 | ROADMAP M28-05 「두 번째부터 안 뜸」 |

구현 조각이 이 값을 DESIGN 3.3 표에 `값 | 근거` 로 옮기고, 표 행(`DT_UsageHints`)에 둔다 — 코드에 박지 않는다(DataDriven). (이번 바퀴는 `docs/DESIGN.md` 에 남의 미커밋 변경이 있어 명세에만 적었다.)

## 4. 데이터·에셋
| 종류 | 이름(규칙대로) | 있음/만들 것 |
|---|---|---|
| 행 구조체 | `FUsageHintRow` (`Source/Snarehaven/Public/Data/UsageHintRow.h`) — `HintKey`(FName, 행 이름과 같음) · `Line`(FText, 한 줄) · `ShowSeconds`(float, 6) · **M28-06 몫 칸:** `GuideTitle`(FText) · `GuideSteps`(TArray<FText>, 3줄) · `GuideIconItemID`(FName — 아이콘은 `DT_ItemTable` 것 재사용) · `GuideOrder`(int32) | 만들 것 |
| DataTable | `DT_UsageHints` (다른 `DT_*` 와 같은 폴더) — 위 15줄. 만들기는 헤드리스 에디터 파이썬(`tools/unreal/create_water_tables.py` 의 `ensure_table` 과 같은 방식) | 만들 것 |
| 키 고르기 | 도구는 **그 줄의 대표 키**로 모은다: 낚싯대 넷 → `tool.item.rod_basic`, 곡괭이 셋 → `tool.item.pickaxe_basic`. 고르는 법은 표 칸 하나(`FItemRow` 에 새 칸을 만들지 않고, `FUsageHintRow` 에 `AlsoItemIDs`(TArray<FName>) 칸) | 만들 것 |
| 본 목록 저장 | 🟡 **이 기기 `GameUserSettings.ini` 의 `[Snarehaven.Hints] Seen=키1,키2,…`** — 첫 실행 안내 `IsGuideSeen`/`SetGuideSeen` 과 같은 길 | 만들 것 |
| 화면 | `UHuntHudWidget` 에 알약 하나(C++ 이 만드는 최소 트리 — 기존 `NeedToolHint` 옆), WBP 수정 없음 | 만들 것 |

**저장을 PlayFab 이 아니라 기기에 두는 이유 (🟡, ROADMAP 줄·DESIGN 12 (가) 와 다름 → INBOX `확인`):**
1. 클라이언트에 PlayFab 플레이어 데이터 쓰기 길이 아직 하나도 없다(`Source/` 에 `UpdateUserData` 0, 모든 쓰기는 CloudScript `CallServerFunction`). PlayFab 에 두려면 새 CloudScript 함수 + 크롬 배포(감독 몫)가 들어 조각이 루프 혼자 못 끝난다.
2. 바로 옆 첫 실행 안내가 이미 기기 ini 다 — 같은 성격의 「봤음」 두 개가 서로 다른 곳에 있으면 계정을 지우거나 기기를 바꿀 때 한쪽만 다시 뜬다.
3. 재화·인벤토리·타이머가 아니라 규칙 8(백엔드 판정) 밖이고, 잃어도 안내 한 줄이 다시 뜰 뿐이다.
→ 감독이 「기기 바꿔도 안 떠야 한다」고 하면 나중 조각(아래 5-④)에서 CloudScript `MarkHintsSeen` + 스냅샷 `seenHints` 로 옮긴다. 읽는 쪽은 `IsHintSeen(Key)` 한 함수라 저장소만 바뀐다.

## 5. 조각 나누기 (루프가 채움, 사람이 확인)
1. **M28-05a 표와 저장** — `FUsageHintRow` + `DT_UsageHints` 15줄(헤드리스 생성 — 위 표에서 `place.Campfire`·`place.Grill` 은 글이 같아도 두 줄) + 정적 `USnareUsageHints::IsHintSeen/MarkHintSeen/ResetForTest`(ini) + `FindHintForItem(ItemID)`·`FindHintForPlaceable(EPlaceableFunction)`. DESIGN 3.3 에 숫자 줄. 2층: 표 줄 수 15 · 대표 키 모으기(rod_lucky → rod_basic 줄) · 표시 → 저장 → ini 다시 읽으면 본 것.
2. **M28-05b 처음 들기** — `UHuntHudWidget` 이 내 `OnEquipmentChanged` 를 듣고 알약 띄우기 + 첫 실행 안내·판 열림 막기 + 필요 알약 우선. 2층: 처음 손도끼 들기 → 한 줄 1 · 다시 들기 → 0 · 안내 떠 있는 중 들기 → 0 이고 본 것 아님 · 자동 들기(`BeginAttack`)도 1. 역검증. 3층 캡처 1장(낚싯대 처음 든 화면).
3. **M28-05c 처음 다가가기** — 동사 바뀜(`Open`+기능·`Harvest`/`Place`)·상인 근접에서 같은 알약. 2층: 작업대 앞 → 한 줄 · 두 번째 → 0 · 목장 칸 · 상인. 역검증.
4. (감독이 원할 때만) **M28-05d 계정에 저장** — 위 4절 끝 줄.

M28-06 안내서는 5-1 의 같은 표 `Guide*` 칸을 읽는다 — 5-1 이 칸을 미리 만들어 두지만 글은 M28-06 이 채운다.

## 6. 검사 방법
- 1층(기계): 에디터·게임 타겟 빌드 · `tools/check_conventions.py --changed --engine unreal`
- 2층(동작): 새 `Snarehaven.UI.UsageHints.*` — 월드 없이 `UHuntHudWidget::BindToCharacter` + 장비 컴포넌트에 `EquipItem` 을 주입해 알약 글자·보임을 읽는다. 매 시험 앞뒤 `ResetForTest` 로 ini 키를 지워 이 기기의 진짜 기록을 건드리지 않는다(시험 전용 섹션 이름 `[Snarehaven.HintsTest]` 를 쓰게 섹션을 인자로 받는다).
- 3층(눈): PIE 에서 낚싯대 처음 든 화면 캡처 1장 — 알약이 상황 버튼 위, 도구 필요 알약과 같은 모양, 글자 잘림 없음.

## 7. 하지 않을 것
- 화살표·손가락 그림·화면 어둡게 하기 같은 튜토리얼 연출(원본에 없음, 설계서에 없음).
- 옷장 줄 — 옷장은 다가가는 물건이 아니라 배낭 판 버튼(`UBackpackScreenWidget::PressWardrobeShop`)이라 「다가가기」 계기가 없다. 안내서(M28-06) 쪽에만 둔다.
- 낚시·농사·옷 상인 각각의 줄 — 잡화점 하나(`vendor.shop`)로 시작하고, 상인 종류(`EShopKind`)별 글은 감독이 원하면 줄만 더한다.
- 안내 다시 보기 설정 — 안내서(M28-06)가 그 역할.
- PlayFab 저장(5-④ 전까지).
