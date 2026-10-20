// ROADMAP M10-02 2층 검사 — 낚시터 거점 슬롯이 목장과 **같은 짜임**이면서 **다른 거점**인지.
// 보는 것 셋: ① 플레이어에게 목장·낚시터가 따로 붙고 종류로 갈라 잡히는지(같은 클래스라
// `FindComponentByClass` 로는 못 가른다) ② 낚시 칸이 백엔드 스냅샷으로만 차고 수확은 **요청만**
// 쌓이는지 ③ 칸 종류와 안 맞는 ID 가 든 스냅샷을 통째로 거절하는지 (DESIGN 8.4 · CLAUDE.md 8).
//
// **"클라이언트가 시간을 세지 않는다" 는 여기서 다시 안 본다** — 낚시터는 목장과 같은
// `URanchComponent` 라 `Snarehaven.Ranch.Slot.NoClientSideTimer` 가 이미 그 소스를 훑는다.
// 어종 ID 는 실제 `DT_FishingTable`(M10-01)에서 읽는다 — 기대값을 검사에 적지 않는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/FishingRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ranch/RanchComponent.h"
#include "SnarehavenCharacter.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestSlotIndex = 1;
	constexpr int32 TestStock = 2;
	const FName TestAnimal = TEXT("animal.test_pet");

	const TCHAR* FishingTablePath = TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable");

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

	URanchComponent* SpawnLooseStation(UWorld* World, ERanchStationKind StationKind)
	{
		AActor* Holder = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Holder == nullptr)
		{
			return nullptr;
		}

		URanchComponent* Station = NewObject<URanchComponent>(Holder);
		Station->SetStationKind(StationKind);
		Station->RegisterComponent();
		return Station;
	}

	/** 표에 실제로 있는 어종 하나. 없으면 NAME_None 이고 부르는 쪽이 검사를 세운다. */
	FName FindFirstFishItemID()
	{
		const UDataTable* const Table = LoadObject<UDataTable>(nullptr, FishingTablePath);
		if (Table == nullptr)
		{
			return NAME_None;
		}

		for (const FName& RowName : Table->GetRowNames())
		{
			if (const FFishingRow* const Row = Table->FindRow<FFishingRow>(RowName, TEXT("FishingSlotTest")))
			{
				if (!Row->ProducedItemID.IsNone())
				{
					return Row->ProducedItemID;
				}
			}
		}
		return NAME_None;
	}

	// 백엔드가 보내 줬다고 치는 낚시터 스냅샷. 칸 수는 컴포넌트가 아는 값을 그대로 쓴다.
	TArray<FRanchSlotState> MakeFishingSnapshot(const URanchComponent* Station, int32 SlotIndex, FName FishItemID)
	{
		TArray<FRanchSlotState> Snapshot;
		Snapshot.SetNum(Station->GetSlotCount());
		if (Snapshot.IsValidIndex(SlotIndex))
		{
			Snapshot[SlotIndex].Kind = ERanchSlotKind::Fishing;
			Snapshot[SlotIndex].FishItemID = FishItemID;
			Snapshot[SlotIndex].OccupiedSinceUtc = FDateTime::UtcNow();
			Snapshot[SlotIndex].Stock = TestStock;
		}
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishingStationTest, "Snarehaven.Fishing.Slot.SeparateFromRanch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishingStationTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어가 스폰돼야 한다"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchComponent* const RanchStation = URanchComponent::FindStation(Player, ERanchStationKind::Ranch);
	URanchComponent* const FishingStation = URanchComponent::FindStation(Player, ERanchStationKind::Fishing);

	if (!TestNotNull(TEXT("목장 거점이 붙어 있다"), RanchStation)
		|| !TestNotNull(TEXT("낚시터 거점이 붙어 있다"), FishingStation))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 같은 클래스가 둘이라 "찾긴 찾았다" 만으로는 증거가 안 된다 — 서로 다른 객체여야 한다.
	TestTrue(TEXT("목장과 낚시터는 서로 다른 컴포넌트다"), RanchStation != FishingStation);
	TestTrue(TEXT("접근자도 같은 낚시터를 준다"), Player->GetFishing() == FishingStation);
	TestTrue(TEXT("목장 거점의 종류는 목장"), RanchStation->GetStationKind() == ERanchStationKind::Ranch);
	TestTrue(TEXT("낚시터 거점의 종류는 낚시터"), FishingStation->GetStationKind() == ERanchStationKind::Fishing);
	TestEqual(TEXT("낚시터 칸 수는 목장과 같다 (DESIGN 8.4)"),
		FishingStation->GetSlotCount(), RanchStation->GetSlotCount());

	const FName FishItemID = FindFirstFishItemID();
	if (!TestTrue(TEXT("DT_FishingTable 에서 어종 하나를 읽는다"), !FishItemID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 낚시터 스냅샷이 목장 칸까지 채우면 두 거점이 사실은 한 통이라는 뜻이다.
	TestTrue(TEXT("낚시터 스냅샷이 반영된다"),
		FishingStation->ApplyBackendSnapshot(MakeFishingSnapshot(FishingStation, TestSlotIndex, FishItemID)));
	TestEqual(TEXT("낚시터 칸에 어종이 들어갔다"),
		FishingStation->GetSlot(TestSlotIndex)->GetOccupantID(), FishItemID);
	TestTrue(TEXT("목장 같은 번호 칸은 그대로 비어 있다"), RanchStation->IsSlotEmpty(TestSlotIndex));

	DestroyTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishingSlotRequestTest,
	"Snarehaven.Fishing.Slot.RequestsDoNotChangeCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishingSlotRequestTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	URanchComponent* const Station = SpawnLooseStation(World, ERanchStationKind::Fishing);
	if (!TestNotNull(TEXT("낚시터 컴포넌트가 만들어져야 한다"), Station))
	{
		DestroyTestWorld(World);
		return false;
	}

	const FName FishItemID = FindFirstFishItemID();
	if (!TestTrue(TEXT("DT_FishingTable 에서 어종 하나를 읽는다"), !FishItemID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("낚시 칸은 스냅샷으로 찬다"),
		Station->ApplyBackendSnapshot(MakeFishingSnapshot(Station, TestSlotIndex, FishItemID)));

	// 펫은 동물 생산 칸에만 들어간다 — 낚시터 등록(`RegisterFishingSpot`)은 `BACKEND.md` 에 아직
	// 없어 이 조각 범위 밖이다 (작물 칸과 같은 자리).
	TestFalse(TEXT("낚시 칸에는 펫 배치 요청을 쌓지 않는다"),
		Station->QueueRegisterPet(TestSlotIndex, TestAnimal));
	TestEqual(TEXT("거절된 요청은 대기열에 없다"), Station->GetPendingRequests().Num(), 0);

	// 수확은 **요청만** 쌓인다 — 칸은 백엔드가 새 스냅샷을 줄 때까지 그대로다 (CLAUDE.md 8).
	TestTrue(TEXT("찬 낚시 칸은 수확 요청을 쌓는다"), Station->QueueHarvest(TestSlotIndex));
	TestEqual(TEXT("요청 한 건이 쌓였다"), Station->GetPendingRequests().Num(), 1);
	TestEqual(TEXT("요청은 수확이다"),
		Station->GetPendingRequests()[0].Kind, ERanchRequestKind::Harvest);
	TestEqual(TEXT("요청을 쌓아도 어종은 그대로다"),
		Station->GetSlot(TestSlotIndex)->GetOccupantID(), FishItemID);
	TestEqual(TEXT("요청을 쌓아도 쌓인 개수는 그대로다"),
		Station->GetSlot(TestSlotIndex)->Stock, TestStock);

	// 같은 칸을 두 번 누르면 이중 지급으로 보인다 (목장과 같은 규칙).
	TestFalse(TEXT("같은 칸의 두 번째 수확 요청은 거절된다"), Station->QueueHarvest(TestSlotIndex));
	TestEqual(TEXT("대기열은 여전히 한 건"), Station->GetPendingRequests().Num(), 1);

	DestroyTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishingSlotSnapshotTest,
	"Snarehaven.Fishing.Slot.RejectsMismatchedSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishingSlotSnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	URanchComponent* const Station = SpawnLooseStation(World, ERanchStationKind::Fishing);
	if (!TestNotNull(TEXT("낚시터 컴포넌트가 만들어져야 한다"), Station))
	{
		DestroyTestWorld(World);
		return false;
	}

	const FName FishItemID = FindFirstFishItemID();
	if (!TestTrue(TEXT("DT_FishingTable 에서 어종 하나를 읽는다"), !FishItemID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 낚시 칸인데 펫 ID 가 같이 들어 있다 — 백엔드와 해석이 어긋난 스냅샷이라 통째로 거절한다.
	TArray<FRanchSlotState> FishWithPet = MakeFishingSnapshot(Station, TestSlotIndex, FishItemID);
	FishWithPet[TestSlotIndex].AnimalID = TestAnimal;
	TestFalse(TEXT("낚시 칸에 펫 ID 가 섞인 스냅샷은 거절한다"), Station->ApplyBackendSnapshot(FishWithPet));

	// ② 어종 없이 재고만 들어 있다 — 빈 칸에 재고가 붙은 것이라 역시 거절한다.
	TArray<FRanchSlotState> GhostFish = MakeFishingSnapshot(Station, TestSlotIndex, FishItemID);
	GhostFish[TestSlotIndex].FishItemID = NAME_None;
	TestFalse(TEXT("어종이 빈 칸에 재고가 붙은 스냅샷은 거절한다"), Station->ApplyBackendSnapshot(GhostFish));

	// ③ 칸 수가 다르면 반쯤 반영하지 않는다 (목장과 같은 규칙).
	TArray<FRanchSlotState> TooShort = MakeFishingSnapshot(Station, TestSlotIndex, FishItemID);
	TooShort.Pop();
	TestFalse(TEXT("칸 수가 다른 스냅샷은 거절한다"), Station->ApplyBackendSnapshot(TooShort));

	// 거절당한 셋 뒤에도 캐시는 처음 그대로여야 한다 — 반쯤 반영되면 "내 물고기가 사라졌다" 로 보인다.
	TestTrue(TEXT("거절 뒤에도 칸은 비어 있다"), Station->IsSlotEmpty(TestSlotIndex));

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
