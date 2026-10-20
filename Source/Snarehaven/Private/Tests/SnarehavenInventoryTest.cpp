// ROADMAP M1-04 2층 검사 — 인벤토리 캐시가 백엔드 권위를 지키는지(보유는 스냅샷으로만, 획득은 대기열로만),
// 그리고 동물이 죽을 때 DT_AnimalTable 범위 안에서 드롭이 굴러 죽인 플레이어의 대기열에 쌓이는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestItemA = TEXT("item.test_a");
	const FName TestItemB = TEXT("item.test_b");
	constexpr int32 TestSnapshotCountA = 7;
	constexpr int32 TestSnapshotCountB = 2;
	constexpr int32 TestGrantCount = 3;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 10.f;
	constexpr int32 TestRollRepeats = 50;

	// 실제 데이터 테이블 (M1-01). 드롭 개수·종류는 여기서만 온다.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");

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

	FInventorySlot MakeSlot(FName ItemID, int32 Count)
	{
		FInventorySlot Slot;
		Slot.ItemID = ItemID;
		Slot.Count = Count;
		return Slot;
	}

	UInventoryComponent* SpawnLooseInventory(UWorld* World)
	{
		AActor* Holder = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Holder == nullptr)
		{
			return nullptr;
		}

		UInventoryComponent* Inventory = NewObject<UInventoryComponent>(Holder);
		Inventory->RegisterComponent();
		return Inventory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenInventorySnapshotTest, "Snarehaven.Inventory.Cache.SnapshotFillsCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenInventorySnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UInventoryComponent* Inventory = SpawnLooseInventory(World);
	TestNotNull(TEXT("인벤토리 컴포넌트가 만들어져야 한다"), Inventory);

	TArray<FInventorySlot> Snapshot;
	Snapshot.Add(MakeSlot(TestItemA, TestSnapshotCountA));
	Snapshot.Add(MakeSlot(TestItemB, TestSnapshotCountB));

	TestTrue(TEXT("성한 스냅샷은 반영된다"), Inventory->ApplyBackendSnapshot(Snapshot));
	TestEqual(TEXT("칸 수가 스냅샷과 같다"), Inventory->GetSlots().Num(), Snapshot.Num());
	TestEqual(TEXT("A 보유 수량이 스냅샷 값"), Inventory->GetItemCount(TestItemA), TestSnapshotCountA);
	TestEqual(TEXT("B 보유 수량이 스냅샷 값"), Inventory->GetItemCount(TestItemB), TestSnapshotCountB);
	TestEqual(TEXT("없는 아이템은 0"), Inventory->GetItemCount(TEXT("item.not_there")), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenInventoryOverflowTest, "Snarehaven.Inventory.Cache.RejectsBadSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenInventoryOverflowTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UInventoryComponent* Inventory = SpawnLooseInventory(World);
	TestNotNull(TEXT("인벤토리 컴포넌트가 만들어져야 한다"), Inventory);

	TArray<FInventorySlot> Good;
	Good.Add(MakeSlot(TestItemA, TestSnapshotCountA));
	Inventory->ApplyBackendSnapshot(Good);

	// 개수가 0 이하인 칸이 섞인 스냅샷도 통째로 거절한다 — 반쯤 반영하면 물건이 사라져 보인다.
	TArray<FInventorySlot> Broken;
	Broken.Add(MakeSlot(TestItemB, 0));
	TestFalse(TEXT("빈 칸이 섞인 스냅샷은 거절"), Inventory->ApplyBackendSnapshot(Broken));

	TestEqual(TEXT("거절당한 뒤에도 앞선 캐시는 그대로"), Inventory->GetItemCount(TestItemA), TestSnapshotCountA);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenInventoryPetLinesTest, "Snarehaven.Inventory.Cache.LinesBeyondBagSlotsApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenInventoryPetLinesTest::RunTest(const FString& Parameters)
{
	// M28-09f — 스냅샷 줄 수는 가방 칸 수가 아니다. 가방 칸 한도만큼의 줄 + 칸을 안 쓰는 펫 줄 셋(온라인 실측
	// `lh1432_bag` 「인벤토리 스냅샷이 23 칸인데 한도는 20 칸이다」)을 서버가 보내면 그대로 받아야 한다.
	UWorld* World = CreateTestWorld();
	UInventoryComponent* Inventory = SpawnLooseInventory(World);
	TestNotNull(TEXT("인벤토리 컴포넌트가 만들어져야 한다"), Inventory);

	const FName TestPetID = TEXT("item.pet_test_goat");
	constexpr int32 TestPetLines = 3;
	TArray<FInventorySlot> Snapshot;
	for (int32 Index = 0; Index < Inventory->GetSlotCount(); ++Index)
	{
		Snapshot.Add(MakeSlot(FName(*FString::Printf(TEXT("item.test_%d"), Index)), 1));
	}
	for (int32 Index = 0; Index < TestPetLines; ++Index)
	{
		Snapshot.Add(MakeSlot(FName(*FString::Printf(TEXT("%s_%d"), *TestPetID.ToString(), Index)), 1));
	}
	AddInfo(FString::Printf(TEXT("[M28-09f] 스냅샷 줄 %d · 가방 칸 %d"), Snapshot.Num(), Inventory->GetSlotCount()));

	TestTrue(TEXT("칸 한도보다 줄이 많은 서버 스냅샷도 반영된다"), Inventory->ApplyBackendSnapshot(Snapshot));
	TestEqual(TEXT("캐시 줄 수 = 스냅샷 줄 수"), Inventory->GetSlots().Num(), Snapshot.Num());
	TestEqual(TEXT("마지막 펫 줄도 캐시에 있다"),
		Inventory->GetItemCount(FName(*FString::Printf(TEXT("%s_%d"), *TestPetID.ToString(), TestPetLines - 1))), 1);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenInventoryPendingTest, "Snarehaven.Inventory.Cache.PendingIsNotOwned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenInventoryPendingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UInventoryComponent* Inventory = SpawnLooseInventory(World);
	TestNotNull(TEXT("인벤토리 컴포넌트가 만들어져야 한다"), Inventory);

	// 이 검사가 CLAUDE.md 8 의 핵심이다 — 획득 요청은 보유 수량을 절대 늘리지 않는다.
	TestTrue(TEXT("지급 요청이 대기열에 쌓인다"), Inventory->QueuePendingGrant(TestItemA, TestGrantCount));
	TestEqual(TEXT("대기 수량은 요청한 만큼"), Inventory->GetPendingCount(TestItemA), TestGrantCount);
	TestEqual(TEXT("보유 수량은 그대로 0"), Inventory->GetItemCount(TestItemA), 0);
	TestEqual(TEXT("보유 칸도 안 생긴다"), Inventory->GetSlots().Num(), 0);

	// 같은 아이템은 한 줄로 합친다.
	Inventory->QueuePendingGrant(TestItemA, TestGrantCount);
	TestEqual(TEXT("같은 아이템은 한 줄"), Inventory->GetPendingGrants().Num(), 1);
	TestEqual(TEXT("대기 수량은 합산"), Inventory->GetPendingCount(TestItemA), TestGrantCount * 2);

	TestFalse(TEXT("개수 0 요청은 무시"), Inventory->QueuePendingGrant(TestItemB, 0));
	TestFalse(TEXT("이름 없는 아이템 요청은 무시"), Inventory->QueuePendingGrant(NAME_None, TestGrantCount));

	// 백엔드가 확정해 스냅샷을 준 뒤에야 보유가 생기고, 그때 대기열을 비운다.
	TArray<FInventorySlot> Confirmed;
	Confirmed.Add(MakeSlot(TestItemA, TestGrantCount * 2));
	TestTrue(TEXT("백엔드 스냅샷 반영"), Inventory->ApplyBackendSnapshot(Confirmed));
	TestEqual(TEXT("스냅샷만으로는 대기열이 안 비워진다"), Inventory->GetPendingCount(TestItemA), TestGrantCount * 2);
	Inventory->ClearPendingGrants();
	TestEqual(TEXT("확정 후 대기열은 비어 있다"), Inventory->GetPendingGrants().Num(), 0);
	TestEqual(TEXT("보유는 백엔드가 준 값"), Inventory->GetItemCount(TestItemA), TestGrantCount * 2);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDeathDropUnsetTest, "Snarehaven.Inventory.Drop.UnsetNumbersStayOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDeathDropUnsetTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("동물이 스폰돼야 한다"), Animal);

	// 드롭 목록은 있지만 개수 범위가 아직 미정(-1)인 행. 추측해서 굴리면 안 된다 (Verification.md 7).
	FAnimalRow Row;
	Row.AnimalID = TEXT("animal.test_dummy");
	Row.MaxHP = TestMaxHP;
	Row.CaptureThresholdPct = TestCaptureThresholdPct;
	Row.DeathDropItems = { TestItemA };
	UAnimalStateComponent* State = Animal->GetAnimalState();
	TestTrue(TEXT("체력 숫자는 성해서 초기화된다"), State->InitializeFromRow(Row));

	State->ApplyDamage(TestMaxHP);
	TestTrue(TEXT("죽었다"), State->GetState() == EAnimalState::Dead);
	TestEqual(TEXT("개수 범위가 미정이면 드롭이 없다"), State->GetDeathDrops().Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPigDropRangeTest, "Snarehaven.Inventory.Drop.PigUsesTableRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPigDropRangeTest::RunTest(const FString& Parameters)
{
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	TestNotNull(TEXT("DT_AnimalTable 이 있어야 한다"), AnimalTable);
	if (AnimalTable == nullptr)
	{
		return false;
	}

	const FAnimalRow* PigRow = AnimalTable->FindRow<FAnimalRow>(PigRowName, TEXT("FSnarehavenPigDropRangeTest"));
	TestNotNull(TEXT("Animal_Pig 행이 있어야 한다"), PigRow);
	if (PigRow == nullptr)
	{
		return false;
	}

	UWorld* World = CreateTestWorld();

	// 무작위라서 한 번만 보면 놓친다 — 여러 번 죽여 보고 **전부** 행의 범위 안인지 센다.
	// 기대값을 코드에 적지 않고 행에서 읽는다 (사람이 밸런스를 바꿔도 이 검사는 안 깨진다).
	int32 MinSeen = MAX_int32;
	int32 MaxSeen = 0;
	for (int32 Repeat = 0; Repeat < TestRollRepeats; ++Repeat)
	{
		AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			continue;
		}

		UAnimalStateComponent* State = Animal->GetAnimalState();
		State->InitializeFromRow(*PigRow);
		State->ApplyDamage(PigRow->MaxHP);

		const TArray<FName>& Drops = State->GetDeathDrops();
		MinSeen = FMath::Min(MinSeen, Drops.Num());
		MaxSeen = FMath::Max(MaxSeen, Drops.Num());

		for (const FName& Dropped : Drops)
		{
			TestTrue(TEXT("드롭 ID 는 행의 목록 안에 있어야 한다"), PigRow->DeathDropItems.Contains(Dropped));
		}

		Animal->Destroy();
	}

	TestTrue(TEXT("드롭 개수가 행의 최솟값 이상"), MinSeen >= PigRow->DeathDropCountMin);
	TestTrue(TEXT("드롭 개수가 행의 최댓값 이하"), MaxSeen <= PigRow->DeathDropCountMax);
	AddInfo(FString::Printf(TEXT("[animal.pig] 드롭 %d~%d개 (행 범위 %d~%d · 종류 %d가지)"),
		MinSeen, MaxSeen, PigRow->DeathDropCountMin, PigRow->DeathDropCountMax, PigRow->DeathDropItems.Num()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenKillQueuesDropTest, "Snarehaven.Inventory.Drop.KillQueuesPendingGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenKillQueuesDropTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();

	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("플레이어가 스폰돼야 한다"), Player);
	if (Player == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}

	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);

	AAnimalCharacter* Animal =
		World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	TestNotNull(TEXT("동물이 스폰돼야 한다"), Animal);

	// 테스트 전용 값으로 두 대면 죽는 동물. 드롭은 한 종류라 개수만 세면 된다.
	FAnimalRow Row;
	Row.AnimalID = TEXT("animal.test_dummy");
	Row.MaxHP = TestMaxHP;
	Row.CaptureThresholdPct = TestCaptureThresholdPct;
	Row.DeathDropItems = { TestItemA };
	Row.DeathDropCountMin = TestGrantCount;
	Row.DeathDropCountMax = TestGrantCount;
	Animal->GetAnimalState()->InitializeFromRow(Row);

	while (!Animal->GetAnimalState()->IsFinished())
	{
		TestTrue(TEXT("사거리 안이라 때려진다"), Player->GetMeleeAttack()->AttackOnce());
	}

	TestTrue(TEXT("때려서 죽었다"), Animal->GetAnimalState()->GetState() == EAnimalState::Dead);

	UInventoryComponent* Inventory = Player->GetInventory();
	TestNotNull(TEXT("플레이어에게 인벤토리 캐시가 있어야 한다"), Inventory);
	// 지시(감독 10-20 19:30) ① — 쌓이는 것은 호스트가 굴린 드롭이 아니라 「이 종 한 마리」다. 드롭은 서버 `ClaimLoot {kills}` 가 굴린다.
	TestEqual(TEXT("처치가 종 ID 한 줄로 대기열에 쌓인다"), Inventory->GetPendingCount(Row.AnimalID), 1);
	TestEqual(TEXT("호스트가 굴린 드롭 아이템은 안 쌓인다"), Inventory->GetPendingCount(TestItemA), 0);
	// 백엔드 응답 전이라 보유 수량은 0 이어야 한다 (CLAUDE.md 8 · DESIGN 4.5).
	TestEqual(TEXT("백엔드 응답 전에는 보유가 안 늘어난다"), Inventory->GetItemCount(TestItemA), 0);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
