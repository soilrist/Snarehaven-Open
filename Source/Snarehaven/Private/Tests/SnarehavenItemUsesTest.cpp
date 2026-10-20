// ROADMAP M33-05 2층 검사 — DT_ItemTable 의 모든 행이 쓰는 곳을 하나 이상 가지는지 (되팔기 포함).
// 쓰는 곳 = 다른 표의 「쓰는」 열(레시피 재료·설계도·씨앗·상자 열기·채집 도구·거점 규칙·사용 안내)
//         + 행 자기 칸(레시피 태그·장비 칸·꾸미기·먹기·설치·펫·UseAction·들고 있으면·채우기·판매가 > 0)
//         + 표에 없는 코드 쓰임(CodeUses — 원본 파일의 선언 줄과 함께).
// 근거: 루프#505 헤드리스 탐침(loop/tmp/505/analyze_use.py) 이 쓴 열을 그대로 옮겼다. 얻는 열(떨굼·상자 풀 등)은 넣지 않는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Data.ItemUses; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemAction.h"
#include "Data/ItemRow.h"
#include "Data/RecipeRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* RecipeTablePath = TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable");

	// 「쓰는」 열. 열 목록이 비면 그 표의 모든 열을 본다(거점 규칙 — 탐침과 같게). bRowName 이면 행 이름도 본다
	// (사용 안내 행 이름 = `tool.item.xxx`).
	struct FUseColumns
	{
		const TCHAR* TablePath;
		TArray<FName> Columns;
		bool bRowName = false;
	};

	TArray<FUseColumns> GetUseColumns()
	{
		return {
			{ RecipeTablePath, { TEXT("Ingredients"), TEXT("RequiredBlueprintID") } },
			{ TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable"), { TEXT("SeedItemID") } },
			{ TEXT("/Game/Game/Data/Shop/DT_CrateTable.DT_CrateTable"), { TEXT("CrateItemID") } },
			{ TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable"),
				{ TEXT("RequiredToolItemID"), TEXT("BonusToolItemID"), TEXT("AlsoToolItemIDs") } },
			{ TEXT("/Game/Game/Data/Balance/DT_BaseRuleTable.DT_BaseRuleTable"), {} },
			{ TEXT("/Game/Game/Data/Guide/DT_UsageHints.DT_UsageHints"), { TEXT("AlsoItemIDs") }, true },
			// 8.6-c 총 행의 탄약 칸 — 재장전 때 `LoadAmmo` 가 그 아이템을 뺀다 (M39-01).
			{ ItemTablePath, { TEXT("AmmoItemID") } },
		};
	}

	// 표에 열이 없는, 코드가 쓰는 아이템. Anchor 부터 다음 「;」 까지 안에 그 ID 가 있어야 한다 (선언이 바뀌면 실패).
	struct FCodeUse
	{
		const TCHAR* ItemID;
		const TCHAR* File;
		const TCHAR* Anchor;
	};

	const FCodeUse CodeUses[] = {
		{ TEXT("item.berry_bait"), TEXT("Source/Snarehaven/Private/Gathering/TapFishingComponent.cpp"), TEXT("FName BerryBaitItemId(") },
		{ TEXT("item.worm_bait"), TEXT("Source/Snarehaven/Private/Gathering/TapFishingComponent.cpp"), TEXT("FName WormBaitItemId(") },
		{ TEXT("item.boat_motor"), TEXT("Source/Snarehaven/Private/World/WaterTravelComponent.cpp"), TEXT("FName BoatMotorItemId(") },
		{ TEXT("item.boat_motor_electric"), TEXT("Source/Snarehaven/Private/World/WaterTravelComponent.cpp"), TEXT("FName ElectricBoatMotorItemId(") },
		{ TEXT("item.boat_container"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var BOAT_CONTAINER_ITEM =") },
		{ TEXT("item.treasure_map"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var TREASURE_ITEM =") },
		{ TEXT("item.vault_pass"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var VAULT_PASS =") },
		{ TEXT("item.story_traveler_hat"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var STORY_CHAPTER_ITEMS =") },
		{ TEXT("item.story_ranch_signpost"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var STORY_CHAPTER_ITEMS =") },
		{ TEXT("item.story_boatman_scarf"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var STORY_CHAPTER_ITEMS =") },
		{ TEXT("item.story_broken_harpoon"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var STORY_CHAPTER_ITEMS =") },
		{ TEXT("item.story_lighthouse_lamp"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var STORY_CHAPTER_ITEMS =") },
		{ TEXT("item.crate_key"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var CRATE_KEY =") },
		// 8.17 고구마 — SellSweetPotato 가 SWEET_POTATO 를 받아 시세로 판다. 썩은 고구마는 rotSweetPotatoes 가 만드는 끝 상태(버리기만).
		{ TEXT("item.sweet_potato"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var SWEET_POTATO =") },
		{ TEXT("item.sweet_potato_rotten"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var SWEET_POTATO_ROTTEN =") },
		// 목줄 셋 = ExchangeCollar 가 CODEX_COLLARS 를 받는다.
		{ TEXT("item.collar_hunting"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var CODEX_COLLARS =") },
		{ TEXT("item.collar_farming"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var CODEX_COLLARS =") },
		{ TEXT("item.collar_fishing"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var CODEX_COLLARS =") },
		// 페인트 일곱 = PaintStructure 가 PAINT_ITEMS 를 받는다.
		{ TEXT("item.paint_white"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
		{ TEXT("item.paint_charcoal"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
		{ TEXT("item.paint_blue"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
		{ TEXT("item.paint_red"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
		{ TEXT("item.paint_yellow"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
		{ TEXT("item.paint_mint"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
		{ TEXT("item.paint_blush"), TEXT("tools/playfab/cloudscript_main.js"), TEXT("var PAINT_ITEMS =") },
	};

	void CollectItemIdsFromText(const FString& Text, TSet<FString>& OutItemIds)
	{
		const FRegexPattern ItemIdPattern(TEXT("item\\.[a-z0-9_]+"));
		FRegexMatcher Matcher(ItemIdPattern, Text);
		while (Matcher.FindNext())
		{
			OutItemIds.Add(Matcher.GetCaptureGroup(0));
		}
	}

	// 행 한 줄의 열 값을 글자로 내보내 「item.xxx」 를 모두 줍는다. 배열·구조체 열(Ingredients 등)도 같은 길.
	void CollectItemIdsFromRow(const UScriptStruct* RowStruct, const uint8* RowData, const TArray<FName>& Columns,
		TSet<FString>& OutItemIds)
	{
		for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (Columns.Num() > 0 && !Columns.Contains(Property->GetFName()))
			{
				continue;
			}

			FString ValueText;
			Property->ExportTextItem_Direct(ValueText, Property->ContainerPtrToValuePtr<void>(RowData), nullptr,
				nullptr, PPF_None);
			CollectItemIdsFromText(ValueText, OutItemIds);
		}
	}

	// 행 자기 칸으로 쓰이는가 — 탐침의 recipe_tag·equip·wear·eat·place·ranch·use_action·carried·fillable·되팔기.
	// 목장 데코(M35-05b)는 종류 칸(`RanchDecoKind`)이 있어야 쓰인다 — 서버 `SetRanchDeco` 가 그 칸에 적용한다.
	bool HasOwnUse(const FItemRow& Item, const TSet<ECraftTag>& UsedTags)
	{
		return (Item.CraftTag != ECraftTag::None && UsedTags.Contains(Item.CraftTag))
			|| Item.EquipSlot != EEquipSlot::None
			|| Item.ItemType == EItemType::Cosmetic || Item.ItemType == EItemType::PetCosmetic
			|| (Item.ItemType == EItemType::RanchDeco && !Item.RanchDecoKind.IsNone())
			|| Item.ItemType == EItemType::Food || Item.HealAmount > 0 || Item.BuffKind != EBuffKind::None
			|| Item.ItemType == EItemType::Placeable || Item.PlaceableFunction != EPlaceableFunction::None
			|| Item.FootprintX > 0
			|| Item.ItemType == EItemType::Pet
			|| Item.UseAction != EItemAction::None
			|| Item.bPassiveWhileCarried
			|| !Item.FilledItemID.IsNone()
			|| Item.SellPrice > 0;
	}

	TArray<FString> FindItemsWithoutUse(const TArray<const FItemRow*>& Items, const TSet<FString>& UseIds,
		const TSet<ECraftTag>& UsedTags)
	{
		TArray<FString> Missing;
		for (const FItemRow* Item : Items)
		{
			const FString ItemId = Item->ItemID.ToString();
			if (!UseIds.Contains(ItemId) && !HasOwnUse(*Item, UsedTags))
			{
				Missing.Add(ItemId);
			}
		}
		return Missing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemUsesTest, "Snarehaven.Data.ItemUses.EveryItemHasUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemUsesTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 을 연다"), ItemTable))
	{
		return false;
	}

	TArray<const FItemRow*> Items;
	for (const TPair<FName, uint8*>& RowPair : ItemTable->GetRowMap())
	{
		Items.Add(reinterpret_cast<const FItemRow*>(RowPair.Value));
	}
	// 표가 비어 조용히 통과하지 않게 (Verification.md) — 루프#505 탐침 때 396행.
	TestTrue(TEXT("아이템 행이 300개 넘게 있다"), Items.Num() > 300);

	TSet<FString> UseIds;
	for (const FUseColumns& Use : GetUseColumns())
	{
		const UDataTable* const Table = LoadObject<UDataTable>(nullptr, Use.TablePath);
		if (!TestNotNull(FString::Printf(TEXT("쓰는 표를 연다: %s"), Use.TablePath), Table))
		{
			continue;
		}
		for (const FName& Column : Use.Columns)
		{
			// 열 이름이 바뀌면 그 열이 조용히 빠진다 — 이름이 행 구조체에 있는지 먼저 본다.
			TestNotNull(FString::Printf(TEXT("%s 에 열 %s 가 있다"), Use.TablePath, *Column.ToString()),
				FindFProperty<FProperty>(Table->GetRowStruct(), Column));
		}
		const int32 Before = UseIds.Num();
		for (const TPair<FName, uint8*>& RowPair : Table->GetRowMap())
		{
			CollectItemIdsFromRow(Table->GetRowStruct(), RowPair.Value, Use.Columns, UseIds);
			if (Use.bRowName)
			{
				CollectItemIdsFromText(RowPair.Key.ToString(), UseIds);
			}
		}
		AddInfo(FString::Printf(TEXT("%s → 새 쓰는 곳 %d개"), Use.TablePath, UseIds.Num() - Before));
	}

	// 레시피 재료 칸이 태그로 받는 묶음 — 그 태그를 가진 아이템은 재료로 쓰인다.
	TSet<ECraftTag> UsedTags;
	if (const UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath))
	{
		for (const TPair<FName, uint8*>& RowPair : RecipeTable->GetRowMap())
		{
			for (const FRecipeIngredient& Ingredient : reinterpret_cast<const FRecipeRow*>(RowPair.Value)->Ingredients)
			{
				if (Ingredient.Tag != ECraftTag::None)
				{
					UsedTags.Add(Ingredient.Tag);
				}
			}
		}
	}
	TestTrue(TEXT("레시피가 받는 재료 태그가 있다"), UsedTags.Num() > 0);

	// 코드 쓰임 목록이 원본과 어긋나면(이름 바뀜·줄 지움) 거짓 통과가 된다 — 선언 줄 안에 그 ID 가 있는지 본다.
	TMap<FString, FString> FileCache;
	for (const FCodeUse& Code : CodeUses)
	{
		FString* Text = FileCache.Find(Code.File);
		if (Text == nullptr)
		{
			Text = &FileCache.Add(Code.File);
			TestTrue(FString::Printf(TEXT("%s 를 읽는다"), Code.File),
				FFileHelper::LoadFileToString(*Text, *(FPaths::ProjectDir() / Code.File)));
		}
		const int32 Start = Text->Find(Code.Anchor, ESearchCase::CaseSensitive);
		const int32 End = Start == INDEX_NONE ? INDEX_NONE : Text->Find(TEXT(";"), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, Start);
		const bool bDeclared = End != INDEX_NONE
			&& Text->Mid(Start, End - Start).Contains(FString::Printf(TEXT("\"%s\""), Code.ItemID));
		TestTrue(FString::Printf(TEXT("%s 의 「%s」 줄에 %s 가 있다"), Code.File, Code.Anchor, Code.ItemID), bDeclared);
		UseIds.Add(Code.ItemID);
	}

	const TArray<FString> Missing = FindItemsWithoutUse(Items, UseIds, UsedTags);
	for (const FString& ItemId : Missing)
	{
		AddInfo(FString::Printf(TEXT("쓰는 곳 없음: %s"), *ItemId));
	}
	AddInfo(FString::Printf(TEXT("아이템 %d행 · 쓰는 곳 ID %d개 · 재료 태그 %d개 · 쓰는 곳 없는 행 %d"),
		Items.Num(), UseIds.Num(), UsedTags.Num(), Missing.Num()));
	TestEqual(TEXT("쓰는 곳 없는 행 0"), Missing.Num(), 0);
	return true;
}

// 위 검사가 정말 잡는가 — 시험 전용 행으로 쓰는 곳 없는 행 하나를 먹인다 (Verification.md 6).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemUsesSelfTest, "Snarehaven.Data.ItemUses.CheckCatchesUnusedItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemUsesSelfTest::RunTest(const FString& Parameters)
{
	FItemRow Unused;
	Unused.ItemID = TEXT("item.test_no_use");

	FItemRow Sold;
	Sold.ItemID = TEXT("item.test_sold");
	Sold.SellPrice = 3;

	FItemRow Tagged;
	Tagged.ItemID = TEXT("item.test_tagged");
	Tagged.CraftTag = ECraftTag::Wood;

	FItemRow Ingredient;
	Ingredient.ItemID = TEXT("item.test_ingredient");

	// 레시피 재료 칸에서 ID 를 줍는다 — 재료 열이 빠지면 모든 재료 행이 「쓰는 곳 없음」 이 된다.
	FRecipeRow Recipe;
	FRecipeIngredient RecipeIngredient;
	RecipeIngredient.ItemID = Ingredient.ItemID;
	RecipeIngredient.Count = 2;
	Recipe.Ingredients.Add(RecipeIngredient);
	TSet<FString> UseIds;
	CollectItemIdsFromRow(FRecipeRow::StaticStruct(), reinterpret_cast<const uint8*>(&Recipe),
		{ TEXT("Ingredients") }, UseIds);
	TestTrue(TEXT("레시피 Ingredients 열에서 쓰는 곳을 줍는다"), UseIds.Contains(TEXT("item.test_ingredient")));

	const TSet<ECraftTag> UsedTags = { ECraftTag::Wood };
	const TArray<FString> Missing = FindItemsWithoutUse({ &Unused, &Sold, &Tagged, &Ingredient }, UseIds, UsedTags);
	TestEqual(TEXT("쓰는 곳 없는 행 1개를 잡는다"), Missing.Num(), 1);
	if (Missing.Num() == 1)
	{
		TestEqual(TEXT("잡은 행 = item.test_no_use"), Missing[0], FString(TEXT("item.test_no_use")));
	}

	// 태그가 있어도 레시피가 그 태그를 안 받으면 쓰임이 아니다.
	TestEqual(TEXT("받는 태그가 없으면 태그 행도 잡는다"),
		FindItemsWithoutUse({ &Tagged }, UseIds, TSet<ECraftTag>()).Num(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
