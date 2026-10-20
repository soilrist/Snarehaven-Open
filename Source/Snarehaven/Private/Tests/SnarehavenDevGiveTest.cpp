// INBOX 2026-09-29 14:55 — 개발용 오프라인 지급 `Snare.Give` (`Backend/SnareDevGiveCommand.cpp`).
// 잡는 것: ① 오프라인(백엔드 스냅샷 없음)에서만 칸이 생긴다 ② 백엔드가 스냅샷을 준 실행에서는 거절한다(CLAUDE.md 8)
// ③ 콘솔 명령이 등록돼 있고 지급할 도구 행이 실제 아이템 표에 있다.
// 출시 구성에서 명령이 없다는 것은 `#if !UE_BUILD_SHIPPING` 컴파일 가드이고, 출시 빌드를 돌리지 않는 2층은 이를 못 본다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"

#if !UE_BUILD_SHIPPING

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDevGiveOfflineTest, "Snarehaven.Backend.DevGive.OfflineOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDevGiveOfflineTest::RunTest(const FString& Parameters)
{
	const FName TestItem(TEXT("item.test_give"));
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));

	TestFalse(TEXT("시작은 오프라인"), Backend->IsSnapshotFromBackend());
	TestFalse(TEXT("개수 0 은 거절"), Backend->DevGrantOffline(TestItem, 0));
	TestTrue(TEXT("오프라인 지급"), Backend->DevGrantOffline(TestItem, 2));
	TestTrue(TEXT("같은 아이템은 한 칸에 합친다"), Backend->DevGrantOffline(TestItem, 3));
	const TArray<FInventorySlot>& Slots = Backend->GetPlayerSnapshot().InventorySlots;
	TestEqual(TEXT("칸 하나"), Slots.Num(), 1);
	TestEqual(TEXT("개수 합"), Slots.Num() == 1 ? Slots[0].Count : -1, 5);
	TestFalse(TEXT("지급이 백엔드 응답으로 둔갑하지 않는다"), Backend->IsSnapshotFromBackend());

	FSnarePlayerSnapshot FromServer;
	FromServer.PlayFabId = TEXT("test.owner");
	Backend->ApplyPlayerSnapshot(FromServer, /*bFromBackend=*/true);
	TestFalse(TEXT("백엔드 스냅샷 뒤에는 지급 거절"), Backend->DevGrantOffline(TestItem, 1));
	TestEqual(TEXT("거절하면 칸이 안 생긴다"), Backend->GetPlayerSnapshot().InventorySlots.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDevGiveCommandTest, "Snarehaven.Backend.DevGive.CommandAndToolRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDevGiveCommandTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("Snare.Give 명령 등록"), IConsoleManager::Get().FindConsoleObject(TEXT("Snare.Give")));

	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	for (const TCHAR* Id : { TEXT("item.chainsaw"), TEXT("item.hatchet"), TEXT("item.pickaxe_basic"),
		TEXT("item.rod_bait"), TEXT("item.satellite_communicator") })
	{
		TestNotNull(FString::Printf(TEXT("표에 %s 행"), Id), SnareItemRows::FindByItemID(ItemTable, FName(Id)));
	}
	return true;
}

#endif
#endif
