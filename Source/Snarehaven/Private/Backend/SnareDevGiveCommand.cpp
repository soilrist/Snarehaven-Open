// 개발용 `Snare.Give <행이름|ItemID> [수량]` 콘솔 명령 — 오프라인 에디터 Play 에서 배낭을 채워 도구·무기를 들어 본다.
// 출시 구성에는 통째로 컴파일되지 않는다. 백엔드가 스냅샷을 준 실행에서는 거절된다 (`DevGrantOffline`).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Backend/SnareBackendSubsystem.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Snarehaven.h"

namespace
{
	void RunGive(const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		USnareBackendSubsystem* const Backend = GameInstance != nullptr
			? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
		if (Backend == nullptr || Args.Num() < 1)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("[Give] 사용법: Snare.Give <행이름|ItemID> [수량]"));
			return;
		}

		const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
		if (!IsValid(ItemTable))
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("[Give] 아이템 표를 못 읽었다"));
			return;
		}

		// 행 이름(`Item_Chainsaw`)이든 ItemID(`item.chainsaw`)든 받는다 — 지급은 늘 ItemID 로 한다.
		const FItemRow* Row = ItemTable->FindRow<FItemRow>(FName(*Args[0]), TEXT("Snare.Give"), false);
		if (Row == nullptr)
		{
			Row = SnareItemRows::FindByItemID(ItemTable, FName(*Args[0]));
		}
		if (Row == nullptr)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("[Give] 표에 없는 아이템이다: %s"), *Args[0]);
			return;
		}

		const int32 Count = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : 1;
		const bool bGiven = Backend->DevGrantOffline(Row->ItemID, Count);
		UE_LOG(LogSnarehaven, Log, TEXT("[Give] %s x %d — %s"), *Row->ItemID.ToString(), Count,
			bGiven ? TEXT("배낭에 넣었다") : TEXT("못 넣었다"));
	}

	FAutoConsoleCommandWithWorldAndArgs GiveCommand(
		TEXT("Snare.Give"), TEXT("개발용: 오프라인 배낭에 아이템을 넣는다. Snare.Give <행이름|ItemID> [수량]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunGive));
}

#endif
