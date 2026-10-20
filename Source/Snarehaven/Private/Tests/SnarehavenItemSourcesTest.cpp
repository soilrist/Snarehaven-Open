// ROADMAP M32-04 2층 검사 — DT_ItemTable 의 모든 행이 얻는 길을 하나 이상 가지는지.
// 얻는 길 = 데이터 표의 「주는」 열(떨굼·펫·상자 풀·작물·낚시·뽑기·채집·목장·레시피 결과·상점·위험구역)
//         + 표에 없는 서버 전용 출처(ServerOnlySources — cloudscript_main.js 줄 번호와 함께).
// 근거: 루프#500 헤드리스 탐침(loop/tmp/500/analyze.py) 이 쓴 열을 그대로 옮겼다. 쓰는 열(레시피 재료 등)은 넣지 않는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Data.ItemSources; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Data/ShopRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	// 「주는」 열. 열 목록이 비면 그 표의 모든 열을 본다(위험구역 — 탐침과 같게).
	struct FSourceColumns
	{
		const TCHAR* TablePath;
		TArray<FName> Columns;
	};

	TArray<FSourceColumns> GetSourceColumns()
	{
		return {
			{ TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"),
				{ TEXT("DeathDropItems"), TEXT("PetItemID"), TEXT("VariantPetItemID") } },
			{ TEXT("/Game/Game/Data/Shop/DT_CrateTable.DT_CrateTable"), { TEXT("ItemID") } },
			{ TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable"), { TEXT("ProducedItemID") } },
			{ TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable"), { TEXT("ProducedItemID") } },
			{ TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool"), { TEXT("ItemID") } },
			{ TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable"),
				{ TEXT("GrantItemID"), TEXT("GrantOptions"), TEXT("ExtraItemID") } },
			{ TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable"), { TEXT("ProducedItemID") } },
			{ TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable"), { TEXT("ResultItemID") } },
			{ TEXT("/Game/Game/Data/Shop/DT_ShopTable.DT_ShopTable"), { TEXT("ItemID") } },
			{ TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable"), {} },
		};
	}

	// 표에 열이 없는, 서버(CloudScript)만 주는 아이템. 줄 번호는 tools/playfab/cloudscript_main.js (루프#504 기준).
	struct FServerSource
	{
		const TCHAR* ItemID;
		const TCHAR* Where;
	};

	const FServerSource ServerOnlySources[] = {
		{ TEXT("item.boat_motor"), TEXT("CHEST_LOOT 1436") },
		{ TEXT("item.boat_motor_electric"), TEXT("CHEST_LOOT 1437") },
		{ TEXT("item.boat_container"), TEXT("CHEST_LOOT 1438") },
		{ TEXT("item.strawberry_hat"), TEXT("CHEST_LOOT 1423") },
		{ TEXT("item.looter_hat"), TEXT("CHEST_LOOT 1424") },
		{ TEXT("item.sprout_hat"), TEXT("ATTENDANCE_HAT 2445") },
		{ TEXT("item.chief_hat"), TEXT("BOSS_HAT 2504") },
		{ TEXT("item.tea_bag"), TEXT("CHEST_LOOT 1432") },
		{ TEXT("item.water_bottle"), TEXT("FILL_ITEMS 1059") },
		{ TEXT("item.water_canister"), TEXT("CHEST_LOOT 1428") },
		{ TEXT("item.blueprint_rod_bait"), TEXT("BLUEPRINT_IDS 1130 · CHEST_BLUEPRINT_CHANCE 1440") },
		{ TEXT("item.blueprint_rod_lucky"), TEXT("BLUEPRINT_IDS 1130 · CHEST_BLUEPRINT_CHANCE 1440") },
		{ TEXT("item.blueprint_grill"), TEXT("BLUEPRINT_IDS 1130 · CHEST_BLUEPRINT_CHANCE 1440") },
		{ TEXT("item.blueprint_workbench_adv"), TEXT("BLUEPRINT_IDS 1130 · CHEST_BLUEPRINT_CHANCE 1440") },
		{ TEXT("item.collar_hunting"), TEXT("CODEX_COLLARS 1603") },
		{ TEXT("item.collar_farming"), TEXT("CODEX_COLLARS 1603") },
		{ TEXT("item.collar_fishing"), TEXT("CODEX_COLLARS 1603") },
		{ TEXT("item.ruby"), TEXT("CHEST_LOOT 1418") },
		{ TEXT("item.sapphire"), TEXT("CHEST_LOOT 1419") },
		{ TEXT("item.amethyst"), TEXT("CHEST_LOOT 1420") },
		{ TEXT("item.aquamarine"), TEXT("CHEST_LOOT 1421") },
		{ TEXT("item.emerald"), TEXT("CHEST_LOOT 1422") },
		{ TEXT("item.treasure_map"), TEXT("CHEST_LOOT 1417") },
		{ TEXT("item.mystery_bag"), TEXT("CHEST_LOOT 1425") },
		{ TEXT("item.cash_bundle"), TEXT("CHEST_LOOT 1426") },
		{ TEXT("item.crate_autumn26"), TEXT("CRATE_CURRENT 1363") },
		// 8.1c 금고 열쇠패 — 우두머리 주 1장(ClaimBoss)·상자 굴림 5%(VAULT_PASS_CHANCE)
		{ TEXT("item.vault_pass"), TEXT("ClaimBoss 3098 · VAULT_PASS_CHANCE 1711") },
		// 8.17 고구마 시세 — 장수에게 사고(BuySweetPotato), 주가 바뀌면 안 판 것이 썩는다(rotSweetPotatoes)
		{ TEXT("item.sweet_potato"), TEXT("BuySweetPotato 2751 · SWEET_POTATO 2656") },
		{ TEXT("item.sweet_potato_rotten"), TEXT("rotSweetPotatoes 2722 · SWEET_POTATO_ROTTEN 2657") },
		// 메인 스토리 장 끝 꾸미기 — 장을 끝내면 서버가 준다(STORY_CHAPTER_ITEMS)
		{ TEXT("item.story_traveler_hat"), TEXT("STORY_CHAPTER_ITEMS 3214") },
		{ TEXT("item.story_ranch_signpost"), TEXT("STORY_CHAPTER_ITEMS 3214") },
		{ TEXT("item.story_boatman_scarf"), TEXT("STORY_CHAPTER_ITEMS 3214") },
		{ TEXT("item.story_broken_harpoon"), TEXT("STORY_CHAPTER_ITEMS 3214") },
		{ TEXT("item.story_lighthouse_lamp"), TEXT("STORY_CHAPTER_ITEMS 3214") },
		// 8.18 과일나무 — 따기·친구 나무 흔들기가 FRUITS 5종을 준다
		{ TEXT("item.pear"), TEXT("FRUITS 2798 · HarvestTree 2864 · PickFriendFruit 2912") },
		{ TEXT("item.peach"), TEXT("FRUITS 2798 · HarvestTree 2864 · PickFriendFruit 2912") },
		{ TEXT("item.cherry"), TEXT("FRUITS 2798 · HarvestTree 2864 · PickFriendFruit 2912") },
		{ TEXT("item.orange"), TEXT("FRUITS 2798 · HarvestTree 2864 · PickFriendFruit 2912") },
		{ TEXT("item.plum"), TEXT("FRUITS 2798 · HarvestTree 2864 · PickFriendFruit 2912") },
	};

	// 행 한 줄의 열 값을 글자로 내보내 「item.xxx」·「gacha.xxx」 를 모두 줍는다. 배열·구조체 열(GrantOptions 등)도 같은 길.
	// `gacha.` 는 M35-04 가챠 펫 의상 행(`DT_ItemTable`)의 출처가 `DT_GachaPool` 이라 같이 줍는다.
	void CollectItemIdsFromRow(const UScriptStruct* RowStruct, const uint8* RowData, const TArray<FName>& Columns,
		TSet<FString>& OutItemIds)
	{
		const FRegexPattern ItemIdPattern(TEXT("(?:item|gacha)\\.[a-z0-9_]+"));
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
			FRegexMatcher Matcher(ItemIdPattern, ValueText);
			while (Matcher.FindNext())
			{
				OutItemIds.Add(Matcher.GetCaptureGroup(0));
			}
		}
	}

	TArray<FString> FindItemsWithoutSource(const TArray<FString>& ItemIds, const TSet<FString>& SourceIds)
	{
		TArray<FString> Missing;
		for (const FString& ItemId : ItemIds)
		{
			if (!SourceIds.Contains(ItemId))
			{
				Missing.Add(ItemId);
			}
		}
		return Missing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemSourcesTest, "Snarehaven.Data.ItemSources.EveryItemHasSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemSourcesTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 을 연다"), ItemTable))
	{
		return false;
	}

	TArray<FString> ItemIds;
	for (const TPair<FName, uint8*>& RowPair : ItemTable->GetRowMap())
	{
		ItemIds.Add(reinterpret_cast<const FItemRow*>(RowPair.Value)->ItemID.ToString());
	}
	// 표가 비어 조용히 통과하지 않게 (Verification.md) — 루프#500 탐침 때 396행.
	TestTrue(TEXT("아이템 행이 300개 넘게 있다"), ItemIds.Num() > 300);

	TSet<FString> SourceIds;
	for (const FSourceColumns& Source : GetSourceColumns())
	{
		const UDataTable* const Table = LoadObject<UDataTable>(nullptr, Source.TablePath);
		if (!TestNotNull(FString::Printf(TEXT("출처 표를 연다: %s"), Source.TablePath), Table))
		{
			continue;
		}
		for (const FName& Column : Source.Columns)
		{
			// 열 이름이 바뀌면 그 열이 조용히 빠진다 — 이름이 행 구조체에 있는지 먼저 본다.
			TestNotNull(FString::Printf(TEXT("%s 에 열 %s 가 있다"), Source.TablePath, *Column.ToString()),
				FindFProperty<FProperty>(Table->GetRowStruct(), Column));
		}
		const int32 Before = SourceIds.Num();
		for (const TPair<FName, uint8*>& RowPair : Table->GetRowMap())
		{
			CollectItemIdsFromRow(Table->GetRowStruct(), RowPair.Value, Source.Columns, SourceIds);
		}
		AddInfo(FString::Printf(TEXT("%s → 새 출처 %d개"), Source.TablePath, SourceIds.Num() - Before));
	}

	// 서버 전용 목록이 서버 원본과 어긋나면(이름 바뀜·줄 지움) 거짓 통과가 된다 — 원본에 그 ID 가 있는지 본다.
	FString ServerScript;
	const FString ServerScriptPath = FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js");
	if (TestTrue(TEXT("cloudscript_main.js 를 읽는다"), FFileHelper::LoadFileToString(ServerScript, *ServerScriptPath)))
	{
		for (const FServerSource& Server : ServerOnlySources)
		{
			TestTrue(FString::Printf(TEXT("서버 원본에 %s 가 있다 (%s)"), Server.ItemID, Server.Where),
				ServerScript.Contains(FString::Printf(TEXT("\"%s\""), Server.ItemID)));
			SourceIds.Add(Server.ItemID);
		}
	}

	const TArray<FString> Missing = FindItemsWithoutSource(ItemIds, SourceIds);
	for (const FString& ItemId : Missing)
	{
		AddInfo(FString::Printf(TEXT("얻는 길 없음: %s"), *ItemId));
	}
	AddInfo(FString::Printf(TEXT("아이템 %d행 · 출처 ID %d개 · 얻는 길 없는 행 %d"),
		ItemIds.Num(), SourceIds.Num(), Missing.Num()));
	TestEqual(TEXT("얻는 길 없는 행 0"), Missing.Num(), 0);
	return true;
}

// 위 검사가 정말 잡는가 — 시험 전용 상점 행 하나와 출처 없는 아이템 하나를 먹인다 (Verification.md 6).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemSourcesSelfTest, "Snarehaven.Data.ItemSources.CheckCatchesSourcelessItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemSourcesSelfTest::RunTest(const FString& Parameters)
{
	FShopRow ShopRow;
	ShopRow.ItemID = TEXT("item.test_sold");
	ShopRow.Price = 7;

	TSet<FString> SourceIds;
	CollectItemIdsFromRow(FShopRow::StaticStruct(), reinterpret_cast<const uint8*>(&ShopRow), { TEXT("ItemID") },
		SourceIds);
	TestTrue(TEXT("상점 ItemID 열에서 출처를 줍는다"), SourceIds.Contains(TEXT("item.test_sold")));

	// 고른 열 밖은 줍지 않는다 — 쓰는 열이 출처로 새면 검사가 무의미해진다.
	TSet<FString> PriceOnly;
	CollectItemIdsFromRow(FShopRow::StaticStruct(), reinterpret_cast<const uint8*>(&ShopRow), { TEXT("Price") },
		PriceOnly);
	TestEqual(TEXT("Price 열만 보면 출처 0"), PriceOnly.Num(), 0);

	const TArray<FString> Missing = FindItemsWithoutSource({ TEXT("item.test_sold"), TEXT("item.test_no_source") },
		SourceIds);
	TestEqual(TEXT("출처 없는 행 1개를 잡는다"), Missing.Num(), 1);
	if (Missing.Num() == 1)
	{
		TestEqual(TEXT("잡은 행 = item.test_no_source"), Missing[0], FString(TEXT("item.test_no_source")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
