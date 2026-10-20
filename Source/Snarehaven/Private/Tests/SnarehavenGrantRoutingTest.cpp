// ROADMAP M3-06 2층 검사 — 호스트가 판정한 드롭·펫·채집이 **잡은 사람의 클라이언트로** 가는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
//
// 진짜 두 프로세스 왕복은 여기서 못 한다(넷드라이버가 없는 테스트 월드라 언제나 NM_Standalone 이다)
// — "호스트 로그엔 없고 게스트 로그엔 있다" 는 3층 몫이다. 2층이 보는 건 두 가지다:
//   1) 지급 경로가 **Client RPC 로 선언**돼 있는지 (언리얼이 폰의 주인 연결로만 보내는 길)
//   2) 그 RPC 몸통이 대기열만 쌓고 **보유 수량은 안 늘리는지** (CLAUDE.md 8)
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"
#include "UObject/Class.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestKillAnimal = TEXT("animal.test_prey");     // 사냥 처치 줄 (`animal.<종>`, 지시 ①)
	const FName TestNotAnimal = TEXT("item.test_drop_a");      // 아이템 ID 는 처치 줄로 안 받는다
	const FName TestPetItem = TEXT("item.test_pet");
	const FName TestGatherableID = TEXT("gatherable.test_bush");
	const FName TestGatherItem = TEXT("item.test_berry");
	constexpr int32 TestGatherCount = 3;

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSnarehavenGrantIsClientRpcTest,
	"Snarehaven.Inventory.Grant.QueuedOnOwningClientOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnarehavenGrantIsClientRpcTest::RunTest(const FString& Parameters)
{
	// 이름으로 찾는다 — 지급 경로가 Client RPC 를 안 거치게 되돌아가면 여기서 먼저 걸린다.
	const TArray<FName> GrantFunctions = {
		TEXT("Client_QueueKillGrant"),
		TEXT("Client_QueuePetGrant"),
		TEXT("Client_QueueGatherGrant"),
	};

	for (const FName& FunctionName : GrantFunctions)
	{
		UFunction* Function = ASnarehavenCharacter::StaticClass()->FindFunctionByName(FunctionName);
		TestNotNull(*FString::Printf(TEXT("%s 가 있어야 한다"), *FunctionName.ToString()), Function);
		if (Function == nullptr)
		{
			continue;
		}

		// FUNC_NetClient = 서버가 부르면 **그 폰을 가진 연결로만** 간다. 호스트에 있는 게스트
		// 복제본에서는 몸통이 안 돈다 (DESIGN 4.1 — 지급 요청은 잡은 사람 것이다).
		TestTrue(*FString::Printf(TEXT("%s 는 Client RPC 다"), *FunctionName.ToString()),
			Function->HasAllFunctionFlags(FUNC_Net | FUNC_NetClient));
		// 지급 요청은 한 줄도 떨구면 안 되니 Reliable 이어야 한다.
		TestTrue(*FString::Printf(TEXT("%s 는 Reliable 이다"), *FunctionName.ToString()),
			Function->HasAllFunctionFlags(FUNC_NetReliable));
		// 멀티캐스트면 방 안 모두에게 남의 전리품이 쌓인다.
		TestFalse(*FString::Printf(TEXT("%s 는 멀티캐스트가 아니다"), *FunctionName.ToString()),
			Function->HasAnyFunctionFlags(FUNC_NetMulticast));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSnarehavenGrantRpcQueuesOnlyTest,
	"Snarehaven.Inventory.Grant.ClientRpcQueuesWithoutHoldings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnarehavenGrantRpcQueuesOnlyTest::RunTest(const FString& Parameters)
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

	UInventoryComponent* Inventory = Player->GetInventory();
	TestNotNull(TEXT("플레이어에게 인벤토리 캐시가 있어야 한다"), Inventory);
	if (Inventory == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}

	// RPC 가 도착한 클라이언트에서 실제로 무엇을 하는지 — 몸통을 직접 부른다.
	Player->Client_QueueKillGrant_Implementation(TestKillAnimal);
	Player->Client_QueueKillGrant_Implementation(TestKillAnimal);
	Player->Client_QueueKillGrant_Implementation(TestNotAnimal);
	Player->Client_QueuePetGrant_Implementation(TestPetItem);
	Player->Client_QueueGatherGrant_Implementation(TestGatherableID, TestGatherItem, TestGatherCount, 0);

	TestEqual(TEXT("처치 두 마리가 종 ID 한 줄 2 로 대기열에"), Inventory->GetPendingCount(TestKillAnimal), 2);
	TestEqual(TEXT("아이템 ID 는 처치 줄로 안 쌓인다"), Inventory->GetPendingCount(TestNotAnimal), 0);
	TestEqual(TEXT("펫도 대기열에"), Inventory->GetPendingCount(TestPetItem), 1);
	TestEqual(TEXT("채집물은 개수 그대로 대기열에"), Inventory->GetPendingCount(TestGatherItem), TestGatherCount);

	// 백엔드가 확정하기 전이라 보유는 하나도 안 늘어야 한다 (CLAUDE.md 8 · DESIGN 4.5).
	TestEqual(TEXT("처치 보유 0"), Inventory->GetItemCount(TestKillAnimal), 0);
	TestEqual(TEXT("펫 보유 0"), Inventory->GetItemCount(TestPetItem), 0);
	TestEqual(TEXT("채집 보유 0"), Inventory->GetItemCount(TestGatherItem), 0);

	// 이름 없는 아이템·개수 0 은 대기열을 늘리지 않는다 (요청이 빈 줄로 백엔드에 가면 안 된다).
	const int32 LinesBefore = Inventory->GetPendingGrants().Num();
	Player->Client_QueuePetGrant_Implementation(NAME_None);
	Player->Client_QueueGatherGrant_Implementation(TestGatherableID, TestGatherItem, 0, 0);
	TestEqual(TEXT("빈 요청은 대기열을 늘리지 않는다"), Inventory->GetPendingGrants().Num(), LinesBefore);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
