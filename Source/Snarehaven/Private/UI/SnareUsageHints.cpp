#include "UI/SnareUsageHints.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Data/ItemRow.h"
#include "Data/UsageHintRow.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareFirstRunGuideWidget.h"
#include "UI/SnareScreenCloser.h"
#include "World/WaterTravelComponent.h"
#include "World/QuestGiverActor.h"
#include "World/ShopVendorActor.h"
#include "World/SnareHookNpcActor.h"

namespace SnareUsageHints
{
const TCHAR* const TablePath = TEXT("/Game/Game/Data/Guide/DT_UsageHints.DT_UsageHints");
const TCHAR* const DefaultSection = TEXT("Snarehaven.Hints");
const TCHAR* const TestSection = TEXT("Snarehaven.HintsTest");

namespace
{
	const TCHAR* SeenKey = TEXT("Seen");

	TArray<FString> ReadSeen(const TCHAR* Section)
	{
		FString Joined;
		if (GConfig != nullptr)
		{
			GConfig->GetString(Section, SeenKey, Joined, GGameUserSettingsIni);
		}
		TArray<FString> Keys;
		Joined.ParseIntoArray(Keys, TEXT(","), /*InCullEmpty=*/true);
		return Keys;
	}

	/** 놓는 것 기능 → 행 이름 `place.<EPlaceableFunction>` (표에 있는지는 안 본다). */
	FName PlaceKey(EPlaceableFunction Function)
	{
		const FString FunctionName = StaticEnum<EPlaceableFunction>()->GetNameStringByValue(static_cast<int64>(Function));
		return FName(*FString::Printf(TEXT("place.%s"), *FunctionName));
	}
}

const UDataTable* LoadTable()
{
	return LoadObject<UDataTable>(nullptr, TablePath);
}

const FUsageHintRow* FindRow(const UDataTable* Table, FName Key)
{
	if (Table == nullptr || Key.IsNone())
	{
		return nullptr;
	}
	static const FString Context(TEXT("SnareUsageHints"));
	return Table->FindRow<FUsageHintRow>(Key, Context, /*bWarnIfRowMissing=*/false);
}

TArray<FName> GetGuidePageKeys(const UDataTable* Table)
{
	TArray<FName> Keys;
	if (Table == nullptr)
	{
		return Keys;
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		if (!reinterpret_cast<const FUsageHintRow*>(Pair.Value)->GuideTitle.IsEmpty())
		{
			Keys.Add(Pair.Key);
		}
	}
	Keys.Sort([Table](const FName& A, const FName& B)
	{
		const int32 OrderA = FindRow(Table, A)->GuideOrder;
		const int32 OrderB = FindRow(Table, B)->GuideOrder;
		return OrderA != OrderB ? OrderA < OrderB : A.LexicalLess(B);
	});
	return Keys;
}

FName FindKeyForItem(const UDataTable* Table, FName ItemID)
{
	if (Table == nullptr || ItemID.IsNone())
	{
		return NAME_None;
	}
	const FName Own(*FString::Printf(TEXT("tool.%s"), *ItemID.ToString()));
	if (FindRow(Table, Own) != nullptr)
	{
		return Own;
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FUsageHintRow* const Row = reinterpret_cast<const FUsageHintRow*>(Pair.Value);
		if (Row != nullptr && Row->AlsoItemIDs.Contains(ItemID))
		{
			return Pair.Key;
		}
	}
	return NAME_None;
}

FName FindKeyForPlaceable(const UDataTable* Table, EPlaceableFunction Function)
{
	if (Function == EPlaceableFunction::None)
	{
		return NAME_None;
	}
	const FName Key = PlaceKey(Function);
	return FindRow(Table, Key) != nullptr ? Key : NAME_None;
}

bool IsHintSeen(FName Key, const TCHAR* Section)
{
	return !Key.IsNone() && ReadSeen(Section).Contains(Key.ToString());
}

void MarkHintSeen(FName Key, const TCHAR* Section)
{
	if (Key.IsNone() || GConfig == nullptr)
	{
		return;
	}
	TArray<FString> Keys = ReadSeen(Section);
	if (Keys.Contains(Key.ToString()))
	{
		return;
	}
	Keys.Add(Key.ToString());
	GConfig->SetString(Section, SeenKey, *FString::Join(Keys, TEXT(",")), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ResetForTest(const TCHAR* Section)
{
	if (GConfig != nullptr)
	{
		GConfig->EmptySection(Section, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FName PickApproachKey(ESnareInteractVerb Verb, bool bNearGeneralShop, EPlaceableFunction NearFunction)
{
	if (Verb == ESnareInteractVerb::Harvest || Verb == ESnareInteractVerb::Place)
	{
		return FName(TEXT("ranch.slot"));
	}
	if (NearFunction != EPlaceableFunction::None)
	{
		return PlaceKey(NearFunction);
	}
	return bNearGeneralShop ? FName(TEXT("vendor.shop")) : FName(NAME_None);
}

FName FindApproachKey(const ASnarehavenCharacter* Character)
{
	if (!IsValid(Character))
	{
		return NAME_None;
	}
	bool bNearGeneralShop = false;
	for (TActorIterator<AShopVendorActor> It(Character->GetWorld()); It && !bNearGeneralShop; ++It)
	{
		bNearGeneralShop = !It->IsA<AQuestGiverActor>() && !It->IsA<ASnareHookNpcActor>() && It->GetShopKind() == EShopKind::General
			&& It->IsPawnInTalkRange(Character);
	}
	// 「열기」 대상 놓인 물건의 기능 — 버튼이 그 물건을 가리킬 때만(낚시·채집이 이기면 그쪽 글이 맞다).
	const ESnareInteractVerb Verb = Character->GetInteractVerb();
	const USnareBaseBuildComponent* const BaseBuild = Character->FindComponentByClass<USnareBaseBuildComponent>();
	EPlaceableFunction NearFunction = Verb == ESnareInteractVerb::Open && IsValid(BaseBuild)
		? BaseBuild->GetInteractTargetFunction() : EPlaceableFunction::None;
	// 배는 땅에 놓이지 않고 물가에서 띄운다(DESIGN 8.9) — 「배 띄우기」 버튼이 뜨는 자리가 배에 다가간 순간이다.
	const UWaterTravelComponent* const Water = Character->FindComponentByClass<UWaterTravelComponent>();
	if (NearFunction == EPlaceableFunction::None && IsValid(Water) && Water->CanLaunchBoat())
	{
		NearFunction = EPlaceableFunction::Boat;
	}
	return PickApproachKey(Verb, bNearGeneralShop, NearFunction);
}

bool IsBlocked(UWorld* World)
{
	TArray<UUserWidget*> Guides;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Guides, USnareFirstRunGuideWidget::StaticClass(), /*TopLevelOnly=*/false);
	for (const UUserWidget* const Guide : Guides)
	{
		if (!CastChecked<USnareFirstRunGuideWidget>(Guide)->IsFinished())
		{
			return true;
		}
	}
	return FSnareScreenCloser::HasOpenScreen(World);
}

const TCHAR* ResolveSection(const TCHAR* Override)
{
	return Override != nullptr ? Override : (GIsAutomationTesting ? TestSection : DefaultSection);
}

const FUsageHintRow* ClaimRow(const UDataTable* Table, FName Key, const TCHAR* Section)
{
	const FUsageHintRow* const Row = FindRow(Table, Key);
	if (Row == nullptr || Row->Line.IsEmpty() || Row->ShowSeconds <= 0.f || IsHintSeen(Key, Section))
	{
		return nullptr;
	}
	MarkHintSeen(Key, Section);
	return Row;
}
}
