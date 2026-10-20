// ROADMAP M2-04c 2층 검사 — 목장 슬롯 상호작용이 명세(docs/specs/ranch-slot.md 2·6장) 대로인지:
// 사거리 안 가장 가까운 슬롯 하나만 하이라이트하고, 누르면 **요청만** 쌓이며(칸은 그대로),
// 배치할 펫이 없으면 아무것도 쌓지 않고, 목장 게임모드는 사냥 HUD 를 띄우지 않는지.
// 펫 아이템 ↔ 동물 ID 짝은 실제 `DT_AnimalTable` 에서 읽는다 — 기대값을 코드에 적지 않는다.
// M2-06b 부터는 어떤 종을 넣을지 플레이어가 고르므로, 고르기가 **요청에 실리는 동물만** 바꾸는지도 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchInteractComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "SnarehavenCharacter.h"
#include "UI/SnarePromptCardWidget.h"
#include "SnarehavenGameMode.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestRangeCm = 300.f;
	constexpr float TestIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestFarButInRangeDistanceCm = 250.f;
	constexpr float TestOutOfRangeDistanceCm = 1000.f;
	constexpr int32 TestNearSlotIndex = 0;
	constexpr int32 TestFarSlotIndex = 1;
	constexpr int32 TestStock = 3;
	const FName TestAnimalID = TEXT("animal.test_pet");

	// 실제 데이터 테이블 (M1-01). 어떤 아이템이 어떤 동물의 펫인지는 여기서만 온다.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr)
		{
			return nullptr;
		}

		// 목장 상호작용 사거리도 근접 공격 컴포넌트에서 빌려 오므로 테스트 값도 거기에 넣는다.
		Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
		return Player;
	}

	// 칸 번호는 "맵에 깔면서 인스턴스마다 정하는 값" 이라 런타임 setter 를 두지 않았다.
	// 커맨드릿(`setup_ranch_stage.py`)이 `set_editor_property` 로 넣는 것과 같은 길을 테스트도 쓴다.
	ARanchSlotActor* SpawnTestSlot(UWorld* World, float DistanceCm, int32 SlotIndex)
	{
		ARanchSlotActor* Slot =
			World->SpawnActor<ARanchSlotActor>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Slot == nullptr)
		{
			return nullptr;
		}

		if (FIntProperty* IndexProperty =
			CastField<FIntProperty>(ARanchSlotActor::StaticClass()->FindPropertyByName(TEXT("SlotIndex"))))
		{
			IndexProperty->SetPropertyValue_InContainer(Slot, SlotIndex);
		}
		return Slot;
	}

	// 백엔드가 "이 아이템을 갖고 있다" 고 보내 준 셈 치는 스냅샷 (캐시를 바꾸는 유일한 길).
	// 인벤토리 스냅샷은 **가진 칸만** 담는다 — 빈 칸을 섞으면 통째로 거절한다
	// (`UInventoryComponent::ApplyBackendSnapshot`, 목장 스냅샷이 칸 수 고정인 것과 다르다).
	bool GivePetToCache(UInventoryComponent* Inventory, FName ItemID)
	{
		FInventorySlot Slot;
		Slot.ItemID = ItemID;
		Slot.Count = 1;
		return Inventory->ApplyBackendSnapshot({ Slot });
	}

	// 백엔드가 채워 준 목장 스냅샷.
	void FillSlotFromBackend(URanchComponent* Ranch, int32 SlotIndex, FName AnimalID)
	{
		TArray<FRanchSlotState> Snapshot;
		Snapshot.SetNum(Ranch->GetSlotCount());
		Snapshot[SlotIndex].AnimalID = AnimalID;
		Snapshot[SlotIndex].OccupiedSinceUtc = FDateTime::UtcNow();
		Snapshot[SlotIndex].Stock = TestStock;
		Ranch->ApplyBackendSnapshot(Snapshot);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHighlightTest, "Snarehaven.Ranch.Interact.HighlightsNearestSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHighlightTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	ARanchSlotActor* NearSlot = SpawnTestSlot(World, TestNearDistanceCm, TestNearSlotIndex);
	ARanchSlotActor* FarSlot = SpawnTestSlot(World, TestFarButInRangeDistanceCm, TestFarSlotIndex);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("가까운 슬롯"), NearSlot)
		|| !TestNotNull(TEXT("먼 슬롯"), FarSlot))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchInteractComponent* Interact = Player->GetRanchInteract();
	if (!TestNotNull(TEXT("목장 상호작용 컴포넌트가 붙어 있다"), Interact))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 사거리가 두 군데로 갈리면 "동물은 닿는데 슬롯은 안 닿는" 차이가 생긴다.
	TestEqual(TEXT("목장 사거리는 자동 상호작용 사거리와 같다"),
		Interact->GetInteractRangeCm(), Player->GetMeleeAttack()->GetAttackRangeCm());

	TestEqual(TEXT("사거리 안 가장 가까운 슬롯이 대상"), Interact->RefreshTarget(), NearSlot);
	TestTrue(TEXT("가까운 슬롯만 하이라이트"), NearSlot->IsHighlighted());
	TestFalse(TEXT("먼 슬롯은 하이라이트 안 됨"), FarSlot->IsHighlighted());

	// 플레이어가 멀어지면 하이라이트가 꺼져야 한다 (켜 둔 채 두면 슬롯이 계속 빛난다).
	Player->SetActorLocation(FVector(-TestOutOfRangeDistanceCm, 0.f, 0.f));
	TestNull(TEXT("사거리 밖이면 대상이 없다"), Interact->RefreshTarget());
	TestFalse(TEXT("멀어지면 하이라이트가 꺼진다"), NearSlot->IsHighlighted());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchPromptCardTest, "Snarehaven.Ranch.Interact.PromptCardOnlyOnTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-03 — 레퍼런스 시트 `docs/reference/hud_ingame.md` 의 「상호작용 프롬프트: 대상 옆 작은 크림 카드
// 36px, 대상이 없으면 안 보임」 을 목장 칸에서 본다. 판정은 이름이 아니라 **컴포넌트의 보임 상태와
// 월드 높이**로 한다 — 카드가 하이라이트와 다른 스위치를 타면 외곽선이 켜진 칸과 카드가 어긋난다.
bool FSnarehavenRanchPromptCardTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	ARanchSlotActor* NearSlot = SpawnTestSlot(World, TestNearDistanceCm, TestNearSlotIndex);
	ARanchSlotActor* FarSlot = SpawnTestSlot(World, TestFarButInRangeDistanceCm, TestFarSlotIndex);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("가까운 슬롯"), NearSlot)
		|| !TestNotNull(TEXT("먼 슬롯"), FarSlot))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchInteractComponent* Interact = Player->GetRanchInteract();
	UWidgetComponent* const NearCard = NearSlot->GetPromptCard();
	UWidgetComponent* const FarCard = FarSlot->GetPromptCard();
	if (!TestNotNull(TEXT("상호작용 컴포넌트"), Interact) || !TestNotNull(TEXT("가까운 칸 프롬프트"), NearCard)
		|| !TestNotNull(TEXT("먼 칸 프롬프트"), FarCard))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 대상이 정해지기 전에는 어느 칸에도 안 뜬다 (시트: "대상이 없으면 안 보임").
	TestFalse(TEXT("고르기 전에는 프롬프트가 없다"), NearCard->IsVisible());
	TestFalse(TEXT("먼 칸도 마찬가지"), FarCard->IsVisible());

	// ② 자동 상호작용이 고른 칸에만 뜬다 — 하이라이트와 같은 칸이어야 한다.
	TestEqual(TEXT("사거리 안 가장 가까운 칸이 대상"), Interact->RefreshTarget(), NearSlot);
	TestTrue(TEXT("고른 칸에 프롬프트가 뜬다"), NearCard->IsVisible());
	TestEqual(TEXT("프롬프트와 하이라이트가 같은 칸"), NearCard->IsVisible(), NearSlot->IsHighlighted());
	TestFalse(TEXT("고르지 않은 칸엔 안 뜬다"), FarCard->IsVisible());

	// ③ 카드는 말뚝 위 하늘에 뜬다 — 말뚝(120cm)보다 높아야 몸·펫에 가리지 않는다.
	//    이름표(캡슐 꼭대기 +60cm ≈ 236cm)보다는 낮아야 같은 줄에서 겹치지 않는다.
	const float CardHeightCm = NearCard->GetComponentLocation().Z - NearSlot->GetActorLocation().Z;
	TestTrue(TEXT("카드가 말뚝 꼭대기보다 위"), CardHeightCm > 120.f);
	TestTrue(TEXT("카드가 머리 위 이름표보다 아래"), CardHeightCm < 236.f);

	// ④ 화면 공간이라 멀어져도 글자가 안 작아진다 (이름표와 같은 길).
	TestEqual(TEXT("화면 공간 카드"), NearCard->GetWidgetSpace(), EWidgetSpace::Screen);
	TestEqual(TEXT("띄우는 위젯은 프롬프트 카드"), NearCard->GetWidgetClass(),
		TSubclassOf<UUserWidget>(USnarePromptCardWidget::StaticClass()));

	// ⑤ 대상이 옮겨 가면 카드도 따라간다 — 앞 칸에 남아 있으면 어느 칸을 누르는지 알 수 없다.
	Player->SetActorLocation(FVector(TestFarButInRangeDistanceCm, 0.f, 0.f));
	TestEqual(TEXT("가까워진 칸으로 대상이 옮겨 간다"), Interact->RefreshTarget(), FarSlot);
	TestTrue(TEXT("옮겨 간 칸에 프롬프트가 뜬다"), FarCard->IsVisible());
	TestFalse(TEXT("떠난 칸의 프롬프트는 꺼진다"), NearCard->IsVisible());

	// ⑥ 사거리 밖으로 나가면 화면이 다시 빈다.
	Player->SetActorLocation(FVector(-TestOutOfRangeDistanceCm, 0.f, 0.f));
	TestNull(TEXT("사거리 밖이면 대상이 없다"), Interact->RefreshTarget());
	TestFalse(TEXT("멀어지면 프롬프트가 꺼진다"), FarCard->IsVisible());

	AddInfo(FString::Printf(TEXT("[RanchPrompt] 칸 위 %.0fcm 크림 카드 · 대상 칸만 켜짐 · 대상 없으면 0개"),
		CardHeightCm));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchRegisterRequestTest,
	"Snarehaven.Ranch.Interact.EmptySlotQueuesRegisterOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchRegisterRequestTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	ARanchSlotActor* Slot = SpawnTestSlot(World, TestNearDistanceCm, TestNearSlotIndex);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchInteractComponent* Interact = Player->GetRanchInteract();
	URanchComponent* Ranch = Player->GetRanch();
	UInventoryComponent* Inventory = Player->GetInventory();
	if (!TestNotNull(TEXT("상호작용 컴포넌트"), Interact) || !TestNotNull(TEXT("목장 캐시"), Ranch)
		|| !TestNotNull(TEXT("인벤토리 캐시"), Inventory))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 가진 펫이 없으면 요청조차 쌓지 않는다 (명세 2장 실패·예외).
	TestFalse(TEXT("펫이 없으면 배치 요청을 쌓지 않는다"), Interact->TryInteractNearest());
	TestEqual(TEXT("대기열은 그대로 0건"), Ranch->GetPendingRequests().Num(), 0);
	// M28-04: 아무것도 안 쌓인 누름엔 몸짓도 없다.
	TestEqual(TEXT("못 넣은 누름엔 몸짓 0"), Player->GetItemActionCount(), 0);

	// 실제 표에서 "펫이 되는 동물" 을 찾아 그 펫 아이템을 캐시에 넣는다 — 기대값을 코드에 적지 않는다.
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 을 읽는다"), AnimalTable))
	{
		DestroyTestWorld(World);
		return false;
	}

	FName ExpectedAnimalID = NAME_None;
	FName PetItemID = NAME_None;
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row != nullptr && !Row->PetItemID.IsNone())
		{
			ExpectedAnimalID = Row->AnimalID;
			PetItemID = Row->PetItemID;
			break;
		}
	}
	if (!TestFalse(TEXT("표에 펫이 되는 동물이 한 종은 있다"), ExpectedAnimalID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("펫을 가진 캐시를 백엔드가 보내 준다"), GivePetToCache(Inventory, PetItemID));
	TestEqual(TEXT("펫 아이템으로 동물 ID 를 찾는다"), Interact->GetSelectedPlacementID(), ExpectedAnimalID);

	TestTrue(TEXT("빈 칸에 배치 요청이 쌓인다"), Interact->TryInteractNearest());
	// M28-04: 넣기 누름에 줍기 몸짓 — 펫 보유는 서버 답 전이라 그대로다. 시험 월드엔 조종자가 없어
	// 멀티캐스트가 같은 몸에서 한 번 더 세므로 「몇 번」이 아니라 「늘었나」로 본다 (ActionClipTest 와 같다).
	const int32 AfterRegister = Player->GetItemActionCount();
	TestTrue(TEXT("넣기 누름 → 몸짓이 돈다"), AfterRegister > 0);
	TestEqual(TEXT("넣기 몸짓은 줍기 클립"), Player->GetLastItemAction(), EItemAction::Gather);
	TestEqual(TEXT("서버 답 전엔 펫 보유 그대로 1"), Inventory->GetItemCount(PetItemID), 1);
	if (!TestEqual(TEXT("대기 중인 요청 1건"), Ranch->GetPendingRequests().Num(), 1))
	{
		// 여기서 멈추지 않고 아래 [0] 을 읽으면 배열 범위 체크가 프로세스를 죽여 **나머지 테스트가 통째로
		// 안 돌아간다** (루프#31 에서 실제로 51개에서 끊겼다). 실패는 실패로 보고하고 빠져나온다.
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("요청은 배치다"), Ranch->GetPendingRequests()[0].Kind, ERanchRequestKind::RegisterPet);
	TestEqual(TEXT("요청의 동물은 표에서 온 것"), Ranch->GetPendingRequests()[0].AnimalID, ExpectedAnimalID);

	// **칸은 그대로여야 한다** — 배치가 눈에 보이면 백엔드 응답 전에 믿은 것이다 (CLAUDE.md 8).
	TestTrue(TEXT("요청을 쌓아도 칸은 여전히 비어 있다"), Ranch->IsSlotEmpty(TestNearSlotIndex));

	// 응답이 오기 전에 두 번 누른 경우 (이중 지급으로 보이는 것을 막는다).
	TestFalse(TEXT("같은 칸에 두 번째 요청은 거절"), Interact->TryInteractNearest());
	TestEqual(TEXT("대기 중인 요청은 그대로 1건"), Ranch->GetPendingRequests().Num(), 1);
	TestEqual(TEXT("거절된 누름엔 몸짓이 늘지 않는다"), Player->GetItemActionCount(), AfterRegister);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchNothingToPlaceTest,
	"Snarehaven.Ranch.Interact.EmptySlotPressNeverSwings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 전수 표 실측 #0512 — 빈 칸 앞 버튼 「넣기」 인데 넣을 것이 없으면 상황 버튼이 허공 주먹질로 샜다.
// 상황 버튼(`BeginAttack`)으로 눌러 휘두르기 0 · 「없다」 알림 1 · 넣을 게 생기면 요청 · 요청 중 다시 눌러도 휘두르기 0.
bool FSnarehavenRanchNothingToPlaceTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	ARanchSlotActor* Slot = SpawnTestSlot(World, TestNearDistanceCm, TestNearSlotIndex);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchInteractComponent* Interact = Player->GetRanchInteract();
	URanchComponent* Ranch = Player->GetRanch();
	UInventoryComponent* Inventory = Player->GetInventory();
	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	if (!TestNotNull(TEXT("상호작용 컴포넌트"), Interact) || !TestNotNull(TEXT("목장 캐시"), Ranch)
		|| !TestNotNull(TEXT("인벤토리 캐시"), Inventory) || !TestNotNull(TEXT("근접 공격"), Melee))
	{
		DestroyTestWorld(World);
		return false;
	}

	Interact->RefreshTarget();
	TestEqual(TEXT("빈 칸 앞 버튼은 넣기"), Player->GetInteractVerb(), ESnareInteractVerb::Place);

	// 넣을 것 없음 — 휘두르지 않고 무엇이 없는지 알린다.
	Player->BeginAttack();
	const int32 SwingsEmpty = Melee->GetSwingCount();
	const int32 NoticesEmpty = Interact->GetNothingToPlaceCount();
	AddInfo(FString::Printf(TEXT("[NothingToPlace] 빈손 누름 → 휘두르기 %d · 없다 알림 %d · 요청 %d"),
		SwingsEmpty, NoticesEmpty, Ranch->GetPendingRequests().Num()));
	TestEqual(TEXT("넣을 것 없는 누름은 휘두르지 않는다"), SwingsEmpty, 0);
	TestEqual(TEXT("넣을 것 없는 누름은 「없다」 알림 1번"), NoticesEmpty, 1);
	TestEqual(TEXT("요청은 0건"), Ranch->GetPendingRequests().Num(), 0);

	// 넣을 것이 생기면 요청이 쌓이고, 요청 중에 다시 눌러도 휘두르지 않는다 — 알림도 더 없다.
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	FName PetItemID = NAME_None;
	if (IsValid(AnimalTable))
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row != nullptr && !Row->PetItemID.IsNone())
			{
				PetItemID = Row->PetItemID;
				break;
			}
		}
	}
	if (!TestFalse(TEXT("표에 펫이 되는 동물이 한 종은 있다"), PetItemID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("펫을 가진 캐시를 백엔드가 보내 준다"), GivePetToCache(Inventory, PetItemID));
	Player->BeginAttack();
	Player->BeginAttack();
	AddInfo(FString::Printf(TEXT("[NothingToPlace] 펫 누름 둘 → 휘두르기 %d · 없다 알림 %d · 요청 %d"),
		Melee->GetSwingCount(), Interact->GetNothingToPlaceCount(), Ranch->GetPendingRequests().Num()));
	TestEqual(TEXT("넣기 요청 1건"), Ranch->GetPendingRequests().Num(), 1);
	TestEqual(TEXT("요청 중 누름도 휘두르지 않는다"), Melee->GetSwingCount(), 0);
	TestEqual(TEXT("펫이 있으면 「없다」 알림이 늘지 않는다"), Interact->GetNothingToPlaceCount(), 1);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHarvestRequestTest,
	"Snarehaven.Ranch.Interact.OccupiedSlotQueuesHarvest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHarvestRequestTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	ARanchSlotActor* Slot = SpawnTestSlot(World, TestNearDistanceCm, TestNearSlotIndex);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchInteractComponent* Interact = Player->GetRanchInteract();
	URanchComponent* Ranch = Player->GetRanch();
	if (!TestNotNull(TEXT("상호작용 컴포넌트"), Interact) || !TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 칸이 차는 유일한 길 — 백엔드 스냅샷.
	FillSlotFromBackend(Ranch, TestNearSlotIndex, TestAnimalID);
	TestFalse(TEXT("스냅샷을 받으면 칸이 찬다"), Ranch->IsSlotEmpty(TestNearSlotIndex));

	// 찬 칸을 누르면 배치가 아니라 수확이다. 펫을 안 갖고 있어도 수확은 된다 (수확은 보유와 무관).
	TestEqual(TEXT("누르기 전 몸짓 0"), Player->GetItemActionCount(), 0);
	TestTrue(TEXT("찬 칸에서 수확 요청이 쌓인다"), Interact->TryInteractNearest());
	// M28-04: 수확 누름에 줍기 몸짓 (지급은 서버 답 뒤 `RanchSyncComponent` 몫).
	TestTrue(TEXT("수확 누름 → 몸짓이 돈다"), Player->GetItemActionCount() > 0);
	TestEqual(TEXT("수확 몸짓은 줍기 클립"), Player->GetLastItemAction(), EItemAction::Gather);
	if (!TestEqual(TEXT("대기 중인 요청 1건"), Ranch->GetPendingRequests().Num(), 1))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("요청은 수확이다"), Ranch->GetPendingRequests()[0].Kind, ERanchRequestKind::Harvest);

	// 쌓인 개수는 백엔드가 줄인다 — 누른 것만으로 재고가 줄면 안 된다 (DESIGN 4.1).
	const FRanchSlotState* State = Ranch->GetSlot(TestNearSlotIndex);
	if (TestNotNull(TEXT("칸 상태를 읽는다"), State))
	{
		TestEqual(TEXT("수확 요청만으로 재고가 줄지 않는다"), State->Stock, TestStock);
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchPetPickerTest,
	"Snarehaven.Ranch.Interact.PetPickerCyclesOwnedPets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchPetPickerTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	ARanchSlotActor* Slot = SpawnTestSlot(World, TestNearDistanceCm, TestNearSlotIndex);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchInteractComponent* Interact = Player->GetRanchInteract();
	URanchComponent* Ranch = Player->GetRanch();
	UInventoryComponent* Inventory = Player->GetInventory();
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("상호작용 컴포넌트"), Interact) || !TestNotNull(TEXT("목장 캐시"), Ranch)
		|| !TestNotNull(TEXT("인벤토리 캐시"), Inventory) || !TestNotNull(TEXT("DT_AnimalTable"), AnimalTable))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 가진 게 없으면 고를 것도 없다 — 이름을 지어내지 않는다.
	TestEqual(TEXT("펫이 없으면 목록이 비어 있다"), Interact->GetOwnedPetAnimalIDs().Num(), 0);
	TestTrue(TEXT("펫이 없으면 고른 펫도 없다"), Interact->GetSelectedPlacementID().IsNone());
	TestTrue(TEXT("펫이 없으면 다음으로 돌려도 없다"), Interact->SelectNextPlacement().IsNone());

	// 표에서 펫이 되는 동물을 **표 순서대로** 모은다 — 기대값을 코드에 적지 않는다.
	TArray<FName> TablePetAnimalIDs;
	TArray<FInventorySlot> OwnedSlots;
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row == nullptr || Row->PetItemID.IsNone())
		{
			continue;
		}

		TablePetAnimalIDs.Add(Row->AnimalID);

		FInventorySlot OwnedSlot;
		OwnedSlot.ItemID = Row->PetItemID;
		OwnedSlot.Count = 1;
		OwnedSlots.Add(OwnedSlot);
	}
	if (!TestTrue(TEXT("표에 펫이 되는 동물이 한 종은 있다"), TablePetAnimalIDs.Num() > 0))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 백엔드가 "이 펫들을 갖고 있다" 고 보내 준 셈 (캐시를 바꾸는 유일한 길).
	TestTrue(TEXT("펫을 가진 캐시를 백엔드가 보내 준다"), Inventory->ApplyBackendSnapshot(OwnedSlots));
	const TArray<FName> OwnedPets = Interact->GetOwnedPetAnimalIDs();
	if (TestEqual(TEXT("가진 펫 종 수가 표와 같다"), OwnedPets.Num(), TablePetAnimalIDs.Num()))
	{
		for (int32 Index = 0; Index < OwnedPets.Num(); ++Index)
		{
			// 순서까지 본다 — "첫 펫" 규칙(DESIGN 3.2)이 표 순서를 뜻하기 때문이다.
			TestEqual(TEXT("가진 펫 목록은 표 순서 그대로"), OwnedPets[Index], TablePetAnimalIDs[Index]);
		}
	}
	TestEqual(TEXT("고른 적이 없으면 첫 펫"), Interact->GetSelectedPlacementID(), TablePetAnimalIDs[0]);

	// 종 수만큼 돌리면 처음으로 돌아온다 — 버튼 하나로 가진 종을 다 볼 수 있어야 한다.
	const int32 PetKindCount = TablePetAnimalIDs.Num();
	for (int32 Step = 0; Step < PetKindCount; ++Step)
	{
		const FName Expected = TablePetAnimalIDs[(Step + 1) % PetKindCount];
		TestEqual(TEXT("다음 종으로 돈다"), Interact->SelectNextPlacement(), Expected);
		TestEqual(TEXT("고른 종이 그대로 남는다"), Interact->GetSelectedPlacementID(), Expected);
	}
	TestEqual(TEXT("한 바퀴 돌면 처음 종"), Interact->GetSelectedPlacementID(), TablePetAnimalIDs[0]);

	// **고르기는 칸을 건드리지 않는다** — 바뀌는 것은 요청에 실리는 동물뿐이다 (CLAUDE.md 8).
	TestTrue(TEXT("고르기만으로는 요청도 안 쌓인다"), Ranch->GetPendingRequests().Num() == 0);
	TestTrue(TEXT("고르기만으로는 칸이 차지 않는다"), Ranch->IsSlotEmpty(TestNearSlotIndex));

	// 지정해서 고르기 — 순환 없이 그 종으로 바로 가고, 없는 것은 거절한다.
	const FName LastPetID = TablePetAnimalIDs.Last();
	TestTrue(TEXT("가진 종은 바로 고른다"), Interact->SelectPlacement(LastPetID));
	TestEqual(TEXT("지정한 종이 골라졌다"), Interact->GetSelectedPlacementID(), LastPetID);
	TestFalse(TEXT("없는 종은 거절한다"), Interact->SelectPlacement(FName(TEXT("test.no_such_animal"))));
	TestEqual(TEXT("거절하면 고른 것이 그대로"), Interact->GetSelectedPlacementID(), LastPetID);
	TestFalse(TEXT("NAME_None 도 거절한다"), Interact->SelectPlacement(NAME_None));

	const FName ChosenAnimalID = Interact->SelectNextPlacement();
	TestTrue(TEXT("배치 요청이 쌓인다"), Interact->TryInteractNearest());
	if (!TestEqual(TEXT("대기 중인 요청 1건"), Ranch->GetPendingRequests().Num(), 1))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("요청에 실린 동물은 고른 종"), Ranch->GetPendingRequests()[0].AnimalID, ChosenAnimalID);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudGateTest, "Snarehaven.Ranch.GameMode.HidesHuntHud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudGateTest::RunTest(const FString& Parameters)
{
	// 맵 이름 문자열이 아니라 게임모드가 답한다 (감독 지시 2026-09-19).
	TestTrue(TEXT("사냥 게임모드는 사냥 HUD 를 띄운다"),
		GetDefault<ASnarehavenGameMode>()->ShouldShowHuntHud());
	TestFalse(TEXT("목장 게임모드는 사냥 HUD 를 띄우지 않는다"),
		GetDefault<ARanchGameMode>()->ShouldShowHuntHud());

	// 목장 게임모드도 플레이어 폰은 같다 — HUD 만 다르고 이동·조이스틱은 그대로여야 한다.
	TestEqual(TEXT("목장도 같은 플레이어 폰을 쓴다"),
		GetDefault<ARanchGameMode>()->DefaultPawnClass, GetDefault<ASnarehavenGameMode>()->DefaultPawnClass);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchPromptCardIconTest, "Snarehaven.Ranch.Interact.PromptCardIconNoBang",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 결함 10-15 16:50 「! 상점 열기 이상함」 — 공용 안내 카드에 「!」 글자가 없고 행동 아이콘(그림이 실린)이 있는지.
// 상점·의뢰 누르는 카드는 `Snarehaven.Shop.Screen.OpenCardBesideTarget` 이 같은 것을 본다.
bool FSnarehavenRanchPromptCardIconTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	USnarePromptCardWidget* const Card = CreateWidget<USnarePromptCardWidget>(World, USnarePromptCardWidget::StaticClass());
	if (!TestNotNull(TEXT("목장 칸 안내 카드"), Card))
	{
		DestroyTestWorld(World);
		return false;
	}
	Card->TakeWidget();

	int32 BangCount = 0;
	Card->WidgetTree->ForEachWidget([&BangCount](UWidget* Widget)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(Widget);
		BangCount += Text != nullptr && Text->GetText().ToString().Contains(TEXT("!")) ? 1 : 0;
	});
	const UImage* const Icon = Cast<UImage>(Card->WidgetTree->FindWidget(TEXT("PromptIcon")));
	const UObject* const IconTexture = Icon != nullptr ? Icon->GetBrush().GetResourceObject() : nullptr;
	AddInfo(FString::Printf(TEXT("[PromptIcon] 「!」 글자 %d개 · 아이콘 %s"), BangCount,
		IconTexture != nullptr ? *IconTexture->GetName() : TEXT("없음")));
	TestEqual(TEXT("안내 카드에 「!」 글자 없음"), BangCount, 0);
	TestNotNull(TEXT("행동 아이콘 그림이 실렸다"), IconTexture);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
