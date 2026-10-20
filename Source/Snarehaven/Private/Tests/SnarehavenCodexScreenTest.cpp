// ROADMAP M10-05 2층 검사 — 도감 화면(`UCodexScreenWidget`) 여섯 탭(M21-07c1 `변종` 더함)의 칸이 **표에서 오고**, 찬 칸이
// **백엔드 스냅샷에서만** 오는지.
// 이 검사가 잡는 것: ① 화면 로스터가 세 표(`DT_AnimalTable`·`DT_CropProductTable`·`DT_FishingTable`)와
// 어긋나는 것 — 칸을 코드에 적어 두고 표만 고치는 사고 ② 백엔드에게 못 들었는데 진행률을 `0 / 9` 로
// 보여 주는 것("하나도 못 모았다" 와 "아직 모른다" 를 같은 화면으로 만드는 사고, Verification.md 7)
// ③ **화면이 스스로 칸을 채우는 것** — 다 모았다고 우길 수 있으면 그게 곧 탈것(M10-06)을 공짜로
// 여는 길이다 (DESIGN 4.8 · CLAUDE.md 8).
// 화면에서 글자가 실제로 읽히는 모양(네모가 아닌지·잘리지 않는지)은 3층(캡처)이 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"
#include "Components/TextBlock.h"
#include "Crafting/SnareCraftRules.h"
#include "Dom/JsonObject.h"
#include "Data/AnimalRow.h"
#include "Data/CropProductRow.h"
#include "Data/FishingRow.h"
#include "Data/ItemRow.h"
#include "Data/RecipeRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "UI/WorldMapScreenWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"

namespace
{
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* CropTablePath = TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable");
	const TCHAR* FishingTablePath = TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* RecipeTablePath = TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable");

	/**
	 * DESIGN 8.5 · `docs/CODEX.md` 표 A 가 정한 61칸(사냥 12 · 농사 27 · 낚시 22) — 탈것 조건(M10-06)의 칸이기도 하다.
	 * **이 목록은 설계서의 사본이지 화면의 사본이 아니다** — 화면과 표가 나란히 틀려도
	 * (한 표가 통째로 안 실리는 사고) 여기가 운다.
	 * 설계서가 칸을 늘리면 이 줄도 같이 고친다 — 그것이 칸 수가 한 곳에만 적혀 있다는 뜻이다.
	 * 9 → 27 은 사용자 지시 2026-09-23 으로 어종이 3 → 21 종이 된 결과다(DESIGN 8.4).
	 * 27 → 39 는 같은 날 지시의 동물 전면 교체다 — 바르코 12종이 들어왔고(M5-07c),
	 * 39 → 36 은 M5-07e — 자리표시 돼지·소·양 3종은 기본 플레이에 안 서므로 칸에서 뺐다(행은 남는다, DESIGN 8.7).
	 * 36 → 60 은 M13-01 — 작물 3 → 27종(DESIGN 13.3, 산딸기·참나무(`item.wood`) 포함)이라 농사 칸이 24 늘었다.
	 * 60 → 61 은 늪 장어(DESIGN 8.4, 2026-09-25).
	 */
	const TCHAR* DesignRoster[] = {
		TEXT("animal.fox"), TEXT("animal.goat"), TEXT("animal.reindeer"),
		TEXT("animal.elk"), TEXT("animal.lynx"), TEXT("animal.wolf"),
		TEXT("animal.wolverine"), TEXT("animal.bear"), TEXT("animal.pheasant"),
		TEXT("animal.peacock"), TEXT("animal.ptarmigan"), TEXT("animal.capercaillie"),
		TEXT("item.carrot"), TEXT("item.potato"), TEXT("item.wheat"),
		TEXT("item.berry"), TEXT("item.blueberry"), TEXT("item.lingonberry"),
		TEXT("item.blackberry"), TEXT("item.mezereon"), TEXT("item.sugar_beet"),
		TEXT("item.onion"), TEXT("item.cucumber"), TEXT("item.chili"),
		TEXT("item.cabbage"), TEXT("item.kingcup"), TEXT("item.windflower"),
		TEXT("item.hepatica"), TEXT("item.poppy"), TEXT("item.fireweed"),
		TEXT("item.pumpkin"), TEXT("item.cotton"), TEXT("item.canola"),
		TEXT("item.tea_leaves"), TEXT("item.coffee_beans"), TEXT("item.wood"),
		TEXT("item.cloudberry"), TEXT("item.black_lotus"), TEXT("item.apple"),
		TEXT("item.crucian_carp"), TEXT("item.salmon"), TEXT("item.cave_crab"),
		TEXT("item.carp"), TEXT("item.perch"), TEXT("item.pike"),
		TEXT("item.whitefish"), TEXT("item.haddock"), TEXT("item.cod"),
		TEXT("item.catfish"), TEXT("item.trout"), TEXT("item.tuna"),
		TEXT("item.stingray"), TEXT("item.rainbow_trout"), TEXT("item.redfish"),
		TEXT("item.black_grouper"), TEXT("item.arctic_char"), TEXT("item.swordfish"),
		TEXT("item.misty_grouper"), TEXT("item.salmon_shark"), TEXT("item.axolotl"),
		TEXT("item.eel"),
	};

	// 검사 전용 — 백엔드가 "이 둘은 찼다" 고 답한 셈 치는 값이다 (Verification.md 6).
	// 서로 다른 묶음에서 하나씩 고른 이유: 한 묶음만 보면 카테고리를 헷갈려 세는 버그를 못 잡는다.
	const FName TestCollectedA(TEXT("animal.fox"));
	const FName TestCollectedB(TEXT("item.catfish"));

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

	/**
	 * 게임 인스턴스 서브시스템은 `ClassWithin` 이 `UGameInstance` 라 아무 데나 만들 수 없다
	 * (`SnarehavenBalanceHudTest` 와 같은 방식). `Initialize` 는 부르지 않는다 — 검사 한 번에
	 * PlayFab 계정을 만들지 않기 위해서다.
	 */
	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	/** 백엔드가 보내 준 셈 치는 상태 한 벌. 도감을 바꾸는 길은 이것 하나뿐이다 (CLAUDE.md 8). */
	FSnarePlayerSnapshot MakeTestSnapshot(const TArray<FName>& CodexEntries)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.CodexEntries = CodexEntries;
		return Snapshot;
	}

	/** 화면에 뜬 글자 전부를 한 덩어리로. 어느 위젯에 적혔든 "화면에 그 글자가 있다" 를 보는 눈이다. */
	FString CollectScreenText(const UUserWidget& Screen)
	{
		FString All;
		Screen.WidgetTree->ForEachWidget([&All](UWidget* Child)
		{
			if (const UTextBlock* const Text = Cast<UTextBlock>(Child))
			{
				All += Text->GetText().ToString();
				All += TEXT("\n");
			}
		});
		return All;
	}

	/** 도감 칸 원판 전부(M25-02) — 칸 번호 순. 원판은 `CodexSlotDisc_<n>` 이름의 둥근 Border 다. */
	TArray<const UBorder*> CollectSlotDiscs(const UUserWidget& Screen)
	{
		TArray<const UBorder*> Discs;
		for (int32 Index = 0; Screen.WidgetTree != nullptr; ++Index)
		{
			const UBorder* const Disc = Screen.WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("CodexSlotDisc_%d"), Index)));
			if (Disc == nullptr)
			{
				break;
			}
			Discs.Add(Disc);
		}
		return Discs;
	}

	/** 모은 칸으로 칠해진 원판 수 — 이름이 아니라 **원판 색**으로 센다(빈 칸 색 `UiEmptySlot` 이 아니면 모은 칸). */
	int32 CountCollectedDiscs(const UUserWidget& Screen)
	{
		int32 Count = 0;
		const FColor Empty = SnareUiStyle::UiEmptySlot().ToFColorSRGB();
		for (const UBorder* const Disc : CollectSlotDiscs(Screen))
		{
			Count += Disc->Background.TintColor.GetSpecifiedColor().ToFColorSRGB() != Empty ? 1 : 0;
		}
		return Count;
	}

	/** 서버 `var <Name> = [ "a", "b", ... ];` 한 배열의 따옴표 문자열 전부. 못 찾으면 빈 배열. */
	TArray<FName> ReadServerArray(const FString& Script, const FString& VarName)
	{
		TArray<FName> IDs;
		const int32 Start = Script.Find(FString::Printf(TEXT("var %s = ["), *VarName), ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return IDs;
		}
		const int32 End = Script.Find(TEXT("];"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		TArray<FString> Parts;
		Script.Mid(Start, End - Start).ParseIntoArray(Parts, TEXT("\""), /*CullEmpty=*/false);
		// 따옴표로 자르면 홀수 번째 조각이 문자열 안쪽이다.
		for (int32 Index = 1; Index < Parts.Num(); Index += 2)
		{
			IDs.Add(FName(*Parts[Index]));
		}
		return IDs;
	}

	/** 도감 화면 하나를 실제 표로 세워 놓고 사냥 탭 칸 ID 를 모은다. */
	TSet<FName> CollectScreenHuntIDs(UCodexScreenWidget& Screen)
	{
		TSet<FName> IDs;
		for (const FCodexSlotView& Slot : Screen.BuildSlots())
		{
			if (Slot.Category == ECodexCategory::Hunt)
			{
				IDs.Add(Slot.EntryID);
			}
		}
		return IDs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexRosterTest, "Snarehaven.Codex.Screen.RosterMatchesTables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexRosterTest::RunTest(const FString& Parameters)
{
	UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	UDataTable* const CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	UDataTable* const FishingTable = LoadObject<UDataTable>(nullptr, FishingTablePath);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable)
		|| !TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable)
		|| !TestNotNull(TEXT("DT_FishingTable 로드"), FishingTable)
		|| !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable)
		|| !TestNotNull(TEXT("DT_RecipeTable 로드"), RecipeTable))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 반환값을 붙잡고 있어야 한다 — 놓으면 그 자리에서 해제된다 (`SnarehavenRanchHudTest` 와 같은 함정).
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// 화면이 탭별로 센 칸.
	TSet<FName> ScreenIDs[static_cast<int32>(ECodexCategory::Count)];
	int32 ScreenTotal = 0;
	for (const FCodexSlotView& Slot : Screen->BuildSlots())
	{
		ScreenIDs[static_cast<int32>(Slot.Category)].Add(Slot.EntryID);
		++ScreenTotal;
	}

	// 표에서 따로 센 탭별 칸 — 화면 코드를 부르지 않고 표 행만 본다 (서버 `CODEX_CATEGORIES` 와 같은 규칙).
	TSet<FName> TableIDs[static_cast<int32>(ECodexCategory::Count)];
	TSet<FName> PlaceholderIDs;
	TMap<FName, ECraftTag> ItemTags;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		if (const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value))
		{
			ItemTags.Add(Row->ItemID, Row->CraftTag);
			if (Row->CraftTag == ECraftTag::Wood || Row->CraftTag == ECraftTag::Cedar || Row->CraftTag == ECraftTag::Berry)
			{
				TableIDs[static_cast<int32>(ECodexCategory::Plant)].Add(Row->ItemID);
			}
		}
	}
	for (const TPair<FName, uint8*>& Pair : FishingTable->GetRowMap())
	{
		if (const FFishingRow* const Row = reinterpret_cast<const FFishingRow*>(Pair.Value))
		{
			TableIDs[static_cast<int32>(ECodexCategory::Fishing)].Add(Row->ProducedItemID);
		}
	}
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		if (const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value))
		{
			// 자리표시 종은 칸이 아니다 (M5-07e) — 표에는 남아도 화면에 떠선 안 된다(아래 ③).
			if (Row->bPlaceholderSpecies || Row->bStorySpecies)
			{
				PlaceholderIDs.Add(Row->AnimalID);
				continue;
			}
			TableIDs[static_cast<int32>(ECodexCategory::Hunt)].Add(Row->AnimalID);
			// 변종 — 기본 플레이 종마다 변종 펫 칸 하나 (M21-07c1, DESIGN 10.6).
			if (!Row->VariantPetItemID.IsNone())
			{
				TableIDs[static_cast<int32>(ECodexCategory::Variant)].Add(Row->VariantPetItemID);
			}
			for (const FName& DropID : Row->DeathDropItems)
			{
				const ECraftTag* const Tag = ItemTags.Find(DropID);
				if (Tag == nullptr || (*Tag != ECraftTag::Meat && *Tag != ECraftTag::Hide))
				{
					TableIDs[static_cast<int32>(ECodexCategory::Hunt)].Add(DropID);
				}
			}
		}
	}
	for (const TPair<FName, uint8*>& Pair : CropTable->GetRowMap())
	{
		if (const FCropProductRow* const Row = reinterpret_cast<const FCropProductRow*>(Pair.Value))
		{
			TableIDs[static_cast<int32>(ECodexCategory::Plant)].Add(Row->ProducedItemID);
		}
	}
	for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
	{
		if (const FRecipeRow* const Row = reinterpret_cast<const FRecipeRow*>(Pair.Value))
		{
			if (!Row->RequiredBlueprintID.IsNone())
			{
				TableIDs[static_cast<int32>(ECodexCategory::Blueprint)].Add(Row->RequiredBlueprintID);
			}
			if (Row->Station == ERecipeStation::Fire)
			{
				TableIDs[static_cast<int32>(ECodexCategory::Cooking)].Add(Row->ResultItemID);
			}
		}
	}

	int32 TableTotal = 0;
	for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
	{
		const ECodexCategory Category = static_cast<ECodexCategory>(Index);
		const FString TabText = Screen->GetTabText(Category).ToString().Replace(TEXT("\n"), TEXT(" "));
		AddInfo(FString::Printf(TEXT("탭 %d [%s]: 화면 %d칸 / 표 %d칸"),
			Index, *TabText, ScreenIDs[Index].Num(), TableIDs[Index].Num()));

		// ① 탭 칸 수 = 표에서 센 칸 수 (한 탭이 통째로 빠지거나 더 만들어 내는 사고를 둘 다 잡는다).
		TestEqual(*FString::Printf(TEXT("탭 %d 칸 수가 표 행 수와 같다"), Index), ScreenIDs[Index].Num(), TableIDs[Index].Num());
		for (const FName& TableID : TableIDs[Index])
		{
			TestTrue(*FString::Printf(TEXT("표의 %s 가 탭 %d 에 있다"), *TableID.ToString(), Index),
				ScreenIDs[Index].Contains(TableID));
		}
		// 탭 머리글의 분모도 같은 수다.
		TestTrue(*FString::Printf(TEXT("탭 %d 글자 분모가 %d 다"), Index, TableIDs[Index].Num()),
			TabText.EndsWith(FString::Printf(TEXT("/%d"), TableIDs[Index].Num())));
		TestTrue(*FString::Printf(TEXT("탭 %d 는 빈 탭이 아니다"), Index), TableIDs[Index].Num() > 0);
		TableTotal += TableIDs[Index].Num();
	}
	TestEqual(TEXT("화면 전체 칸 수가 여섯 탭 표 칸 수의 합과 같다 (중복 없음)"), ScreenTotal, TableTotal);

	// ② 설계서가 정한 동물·작물·어류 칸이 여전히 다 있다 — 표 하나가 통째로 안 실려 화면과 표가 나란히 틀린 경우.
	TSet<FName> AllScreenIDs;
	for (const TSet<FName>& Set : ScreenIDs)
	{
		AllScreenIDs.Append(Set);
	}
	for (const TCHAR* Expected : DesignRoster)
	{
		TestTrue(*FString::Printf(TEXT("DESIGN 8.5 의 %s 칸이 화면에 있다"), Expected),
			AllScreenIDs.Contains(FName(Expected)));
	}

	// ③ 기본 플레이에 안 서는 자리표시 종은 어느 탭에도 칸이 없다.
	for (const FName& PlaceholderID : PlaceholderIDs)
	{
		TestFalse(*FString::Printf(TEXT("자리표시 %s 는 도감 칸이 아니다"), *PlaceholderID.ToString()),
			AllScreenIDs.Contains(PlaceholderID));
	}

	// ④ 칸 이름이 ID 그대로 남아 있지 않은지 — `DT_ItemTable` 을 못 찾으면 화면이 `item.carrot` 으로 뜬다.
	//    그 자체는 일부러 둔 안전장치(이름을 지어내지 않는다)지만, 평소 화면에서는 보이면 안 된다.
	for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
	{
		Screen->SetSelectedCategory(static_cast<ECodexCategory>(Index));
		const FString ScreenText = CollectScreenText(*Screen);
		TestFalse(*FString::Printf(TEXT("탭 %d 에 날 ID(`item.`)가 그대로 뜨지 않는다"), Index), ScreenText.Contains(TEXT("item.")));
		TestFalse(*FString::Printf(TEXT("탭 %d 에 날 ID(`animal.`)가 그대로 뜨지 않는다"), Index), ScreenText.Contains(TEXT("animal.")));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexProgressTest, "Snarehaven.Codex.Screen.ProgressFollowsBackend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexProgressTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// ① 아직 아무에게도 안 물어봤다 — `0 / 9` 가 아니라 `- / 9` 여야 한다.
	const FString BeforeBind = Screen->GetProgressText().ToString();
	AddInfo(FString::Printf(TEXT("백엔드 전 진행률: [%s]"), *BeforeBind));
	TestTrue(*FString::Printf(TEXT("백엔드 전에는 찬 칸이 `-` 다 (실제 \"%s\")"), *BeforeBind),
		BeforeBind.Contains(TEXT("도감 -")));
	TestEqual(TEXT("백엔드 전에 모은 칸 원판이 하나도 없다"), CountCollectedDiscs(*Screen), 0);
	TestTrue(TEXT("백엔드 전 탭 글자는 `-/` 다"), Screen->GetTabText(ECodexCategory::Hunt).ToString().Contains(TEXT("-/")));

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	TestTrue(TEXT("검사용 스냅샷이 반영된다"),
		Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA, TestCollectedB}), /*bFromBackend=*/true));

	Screen->BindToBackend(Backend);

	// ② 붙이는 순간 화면이 백엔드가 말한 만큼만 센다.
	const FString AfterBind = Screen->GetProgressText().ToString();
	const FString AfterBindText = CollectScreenText(*Screen);
	AddInfo(FString::Printf(TEXT("붙인 뒤 진행률: [%s]\n%s"), *AfterBind, *AfterBindText));
	TestTrue(*FString::Printf(TEXT("찬 칸이 2개로 뜬다 (실제 \"%s\")"), *AfterBind),
		AfterBind.Contains(TEXT("도감 2")));

	// 탭 머리글 진척은 서버 `GetCodexRewards` categories 몫이다 — 스냅샷만 있고 서버 답이 없으면
	// 스냅샷을 세어 `1/` 로 채우지 않고 `-/` 로 둔다 (Tabs.ProgressFollowsServer 가 서버 답 뒤를 본다).
	const FString HuntTab = Screen->GetTabText(ECodexCategory::Hunt).ToString();
	const FString FishTab = Screen->GetTabText(ECodexCategory::Fishing).ToString();
	AddInfo(FString::Printf(TEXT("사냥 탭 [%s] / 낚시 탭 [%s]"), *HuntTab, *FishTab));
	TestTrue(TEXT("서버 답 전 사냥 탭은 스냅샷으로 세지 않는다"), HuntTab.Contains(TEXT("\n-/")));
	TestTrue(TEXT("서버 답 전 낚시 탭은 스냅샷으로 세지 않는다"), FishTab.Contains(TEXT("\n-/")));

	// 모은 칸 원판(M25-02)은 고른 탭 것만 그린다 — 낚시 탭(처음 탭)에 1개, 사냥 탭으로 바꾸면 1개.
	TestEqual(TEXT("낚시 탭 모은 칸 원판"), CountCollectedDiscs(*Screen), 1);
	Screen->SetSelectedCategory(ECodexCategory::Hunt);
	TestEqual(TEXT("사냥 탭 모은 칸 원판"), CountCollectedDiscs(*Screen), 1);
	Screen->SetSelectedCategory(ECodexCategory::Plant);
	TestEqual(TEXT("식물 탭 모은 칸 원판"), CountCollectedDiscs(*Screen), 0);

	// ③ 스냅샷이 바뀌면 화면이 따라간다 — 다시 그리라고 따로 부르지 않는다(알림 배선이 빠졌으면 여기서 운다).
	TestTrue(TEXT("두 번째 스냅샷이 반영된다"),
		Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true));

	const FString AfterChange = Screen->GetProgressText().ToString();
	AddInfo(FString::Printf(TEXT("바뀐 뒤 진행률: [%s]"), *AfterChange));
	TestTrue(*FString::Printf(TEXT("찬 칸이 1개로 줄어든다 (실제 \"%s\")"), *AfterChange),
		AfterChange.Contains(TEXT("도감 1")));

	// ④ 0 은 "모른다" 가 아니다 — 백엔드가 "하나도 없다" 고 답했으면 `-` 가 아니라 `0` 으로 떠야 한다.
	TestTrue(TEXT("빈 도감 스냅샷이 반영된다"),
		Backend->ApplyPlayerSnapshot(MakeTestSnapshot({}), /*bFromBackend=*/true));
	const FString AfterEmpty = Screen->GetProgressText().ToString();
	AddInfo(FString::Printf(TEXT("빈 도감 진행률: [%s]"), *AfterEmpty));
	TestTrue(*FString::Printf(TEXT("백엔드가 준 빈 도감은 `0` 으로 뜬다 (실제 \"%s\")"), *AfterEmpty),
		AfterEmpty.Contains(TEXT("도감 0")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexNeverFillsLocallyTest, "Snarehaven.Codex.Screen.NeverFillsLocally",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexNeverFillsLocallyTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}

	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	TestTrue(TEXT("검사용 스냅샷이 반영된다"),
		Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true));
	Screen->BindToBackend(Backend);

	// 몇 번을 다시 그려도 백엔드가 든 도감은 그대로다 — 화면이 칸을 늘리는 길이 없다는 뜻이다
	// (CLAUDE.md 8 · DESIGN 4.8). 늘릴 수 있으면 M10-06 의 탈것이 공짜가 된다.
	Screen->RefreshDisplay();
	Screen->RefreshDisplay();
	Screen->RefreshDisplay();

	const TArray<FName>& AfterRedraw = Backend->GetPlayerSnapshot().CodexEntries;
	AddInfo(FString::Printf(TEXT("다시 그린 뒤 백엔드 도감 %d칸"), AfterRedraw.Num()));
	TestEqual(TEXT("화면을 다시 그려도 백엔드 도감 칸 수는 그대로다"), AfterRedraw.Num(), 1);
	TestTrue(TEXT("화면을 다시 그려도 백엔드 도감 내용은 그대로다"), AfterRedraw.Contains(TestCollectedA));

	// 3층 캡처용 자리표시(`-SnareCodexDemo=`)도 같다 — 화면은 앞 9칸을 찬 것처럼 그려도
	// **백엔드가 든 도감은 1칸 그대로**여야 한다. 여기가 뚫리면 명령줄 한 줄로 M10-06 의 탈것이 열린다.
	Screen->SetDemoFilledCount(9);
	const FString DemoText = CollectScreenText(*Screen);
	AddInfo(FString::Printf(TEXT("자리표시 9칸 화면:\n%s"), *DemoText));
	TestTrue(TEXT("자리표시는 화면 진행률을 9 로 올린다"), DemoText.Contains(TEXT("도감 9")));
	TestEqual(TEXT("자리표시를 켜도 백엔드 도감 칸 수는 그대로다"),
		Backend->GetPlayerSnapshot().CodexEntries.Num(), 1);

	// 표가 통째로 없을 때 — 포장 빌드에서 에셋을 빠뜨리는 사고. 그때 화면이 `0 / 0` 을 보여 주면
	// **다 모은 화면**(분자 = 분모)이 되어 버린다. 칸이 0개면 진행률도 `- / 0` 여야 안전하다.
	Screen->SetSourceTables(nullptr, nullptr, nullptr, nullptr, nullptr);
	const FString NoTableText = Screen->GetProgressText().ToString();
	AddInfo(FString::Printf(TEXT("표 없는 진행률: [%s]"), *NoTableText));
	TestTrue(TEXT("표가 없으면 칸 수가 0 으로 뜬다"), NoTableText.Contains(TEXT("/ 0")));
	TestEqual(TEXT("표가 없으면 원판 칸이 하나도 없다"), CollectSlotDiscs(*Screen).Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexRewardsTest, "Snarehaven.Codex.Tabs.RewardsFollowServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 50%·100% 보상(먼바다·위험구역 선착장·특별 상인)은 서버 `GetCodexRewards` 답만 따른다.
// 답 전에는 `-`(모른다), 답이 잠김이면 잠김 — 화면이 탭 비율을 세서 "열렸다" 고 하지 않는다.
bool FSnarehavenCodexRewardsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// 부르면 적어 두기만 하는 가짜 서버 — 답은 아래에서 손으로 준다.
	TArray<FString> CalledNames;
	TArray<USnareBackendSubsystem::FServerCallDone> PendingDone;
	USnareBackendSubsystem::TestServerOverride =
		[&CalledNames, &PendingDone](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
			USnareBackendSubsystem::FServerCallDone OnDone)
		{
			CalledNames.Add(FunctionName);
			PendingDone.Add(MoveTemp(OnDone));
		};
	auto MakeRewards = [](bool bFarSea, bool bDangerDock, bool bSpecialMerchant)
	{
		const TSharedPtr<FJsonObject> Rewards = MakeShared<FJsonObject>();
		Rewards->SetBoolField(TEXT("farSea"), bFarSea);
		Rewards->SetBoolField(TEXT("dangerDock"), bDangerDock);
		Rewards->SetBoolField(TEXT("specialMerchant"), bSpecialMerchant);
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetObjectField(TEXT("rewards"), Rewards);
		return Reply;
	};

	// 반쯤 찬 척하는 스냅샷 — 화면이 이것만 보고 열림이라 하면 안 된다.
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA, TestCollectedB}), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	const FString BeforeText = Screen->GetRewardText().ToString();
	AddInfo(FString::Printf(TEXT("답 전 보상 [%s] / 부른 함수 [%s]"), *BeforeText, *FString::Join(CalledNames, TEXT(","))));
	TestEqual(TEXT("붙이면 보상을 한 번 묻는다"), FString::Join(CalledNames, TEXT(",")), FString(TEXT("GetCodexRewards")));
	TestFalse(TEXT("답 전에는 보상을 모른다"), Screen->AreRewardsKnown());
	TestFalse(TEXT("답 전에는 위험구역 선착장이 안 열렸다"), Screen->IsDangerDockOpen());
	TestFalse(TEXT("답 전에는 `열림` 이 안 뜬다"), BeforeText.Contains(TEXT("열림")));
	TestFalse(TEXT("답 전에는 `잠김` 도 안 뜬다"), BeforeText.Contains(TEXT("잠김")));

	// ① 서버: 먼바다만 열림(낚시 50%), 위험구역은 잠김.
	if (PendingDone.Num() == 1)
	{
		PendingDone[0](true, MakeRewards(true, false, false), FString());
	}
	const FString LockedText = Screen->GetRewardText().ToString();
	AddInfo(FString::Printf(TEXT("잠김 답 뒤 보상 [%s]"), *LockedText));
	TestTrue(TEXT("답 뒤에는 보상을 안다"), Screen->AreRewardsKnown());
	TestTrue(TEXT("먼바다 열림"), LockedText.Contains(TEXT("먼바다 열림")));
	TestTrue(TEXT("위험구역 선착장 잠김"), LockedText.Contains(TEXT("위험구역 선착장 잠김")));
	TestFalse(TEXT("잠김 답이면 선착장이 안 열렸다"), Screen->IsDangerDockOpen());

	// ② 스냅샷이 바뀌면 다시 묻고, 서버가 열림이라 하면 그때 열린다.
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	TestEqual(TEXT("스냅샷이 바뀌면 다시 묻는다"), PendingDone.Num(), 2);
	if (PendingDone.Num() == 2)
	{
		PendingDone[1](true, MakeRewards(true, true, false), FString());
	}
	const FString OpenText = Screen->GetRewardText().ToString();
	AddInfo(FString::Printf(TEXT("열림 답 뒤 보상 [%s]"), *OpenText));
	TestTrue(TEXT("서버가 열림이라 하면 선착장이 열린다"), Screen->IsDangerDockOpen());
	TestTrue(TEXT("위험구역 선착장 열림"), OpenText.Contains(TEXT("위험구역 선착장 열림")));
	TestTrue(TEXT("특별 상인 잠김"), OpenText.Contains(TEXT("특별 상인 잠김")));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexRewardRowHiddenTest, "Snarehaven.Codex.Tabs.RewardRowHiddenUntilServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M27-05 — 서버 답 전엔 보상 줄을 접는다(`먼바다 -  |  위험구역 선착장 -  |  특별 상인 -` 가 떠 있었다, `378_codex_round.png`).
// 잡는 것: ① 모름 → 줄 Collapsed ② 변종 탭은 확률 고지라 답 전에도 보임 ③ 답 뒤 → 보이고 `-` 0, 글은 서버 값 그대로
// ④ 답을 못 받으면(실패) 접힌 채.
bool FSnarehavenCodexRewardRowHiddenTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	UWidget* const Row = Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("RewardText")) : nullptr;
	if (!TestNotNull(TEXT("보상 줄 위젯"), Row))
	{
		DestroyTestWorld(World);
		return false;
	}

	TArray<USnareBackendSubsystem::FServerCallDone> PendingDone;
	USnareBackendSubsystem::TestServerOverride =
		[&PendingDone](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
			USnareBackendSubsystem::FServerCallDone OnDone)
		{
			PendingDone.Add(MoveTemp(OnDone));
		};
	auto VisName = [](ESlateVisibility Vis)
	{
		return StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(Vis));
	};

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetSelectedCategory(ECodexCategory::Hunt);

	// ① 답 전 — 사냥 탭 보상 줄은 접힌다.
	const ESlateVisibility Before = Row->GetVisibility();
	AddInfo(FString::Printf(TEXT("답 전 사냥 탭 보상 줄 [%s] 글 [%s]"), *VisName(Before), *Screen->GetRewardText().ToString()));
	TestEqual(TEXT("답 전에는 보상 줄이 접힌다"), Before, ESlateVisibility::Collapsed);

	// ② 변종 탭은 고지 줄이라 답 전에도 보인다.
	Screen->SetSelectedCategory(ECodexCategory::Variant);
	const ESlateVisibility VariantVis = Row->GetVisibility();
	AddInfo(FString::Printf(TEXT("답 전 변종 탭 줄 [%s]"), *VisName(VariantVis)));
	TestNotEqual(TEXT("변종 탭 고지 줄은 답 전에도 보인다"), VariantVis, ESlateVisibility::Collapsed);
	Screen->SetSelectedCategory(ECodexCategory::Hunt);
	TestEqual(TEXT("사냥 탭으로 돌아오면 다시 접힌다"), Row->GetVisibility(), ESlateVisibility::Collapsed);

	// ④ 실패 답 — 모르는 채라 접힌 채.
	if (TestEqual(TEXT("붙이면 한 번 묻는다"), PendingDone.Num(), 1))
	{
		PendingDone[0](false, nullptr, TEXT("test"));
	}
	TestEqual(TEXT("실패 답 뒤에도 접힌 채"), Row->GetVisibility(), ESlateVisibility::Collapsed);

	// ③ 다시 물어 답이 오면 보이고 `-` 가 없다 — 글은 서버 값 그대로.
	Screen->RequestRewards();
	if (TestEqual(TEXT("실패 뒤 다시 묻는다"), PendingDone.Num(), 2))
	{
		const TSharedPtr<FJsonObject> Rewards = MakeShared<FJsonObject>();
		Rewards->SetBoolField(TEXT("farSea"), false);
		Rewards->SetBoolField(TEXT("dangerDock"), true);
		Rewards->SetBoolField(TEXT("specialMerchant"), false);
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetObjectField(TEXT("rewards"), Rewards);
		PendingDone[1](true, Reply, FString());
	}
	const ESlateVisibility After = Row->GetVisibility();
	const FString AfterText = CastChecked<UTextBlock>(Row)->GetText().ToString();
	AddInfo(FString::Printf(TEXT("답 뒤 보상 줄 [%s] 글 [%s]"), *VisName(After), *AfterText));
	TestNotEqual(TEXT("답 뒤에는 보상 줄이 보인다"), After, ESlateVisibility::Collapsed);
	TestFalse(TEXT("답 뒤 줄에 `-` 가 없다"), AfterText.Contains(TEXT("-")));
	TestTrue(TEXT("먼바다 잠김 = 서버 값"), AfterText.Contains(TEXT("먼바다 잠김")));
	TestTrue(TEXT("위험구역 선착장 열림 = 서버 값"), AfterText.Contains(TEXT("위험구역 선착장 열림")));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

// #0174 — 화면을 한 번 열면 `GetCodexRewards` 는 **한 번만** 나간다. 감독 3층 캡처에서 3번 나갔다:
// 스냅샷 알림은 재화·시작 장비 왕복에도 오는데, 알림마다 다시 물었다. 잡는 것: ① 답 전에 알림·재요청이
// 와도 안 늘어난다 ② 답 뒤 **같은 칸** 알림에도 안 늘어난다(실제 사고) ③ 칸이 바뀌면 그때만 다시 묻는다
// ④ 답 전엔 `-`, 답 뒤엔 서버 값(CLAUDE.md 8) ⑤ 판이 HUD 를 덮고 눌림을 먹는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexAskOnceTest, "Snarehaven.Codex.Tabs.AsksServerOncePerOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexAskOnceTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	TArray<USnareBackendSubsystem::FServerCallDone> PendingDone;
	USnareBackendSubsystem::TestServerOverride =
		[&PendingDone](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			PendingDone.Add(MoveTemp(OnDone));
		};
	auto MakeReply = []()
	{
		const TSharedPtr<FJsonObject> Hunt = MakeShared<FJsonObject>();
		Hunt->SetNumberField(TEXT("have"), 5);
		Hunt->SetNumberField(TEXT("total"), 29);
		Hunt->SetNumberField(TEXT("percent"), 17);
		const TSharedPtr<FJsonObject> Categories = MakeShared<FJsonObject>();
		Categories->SetObjectField(TEXT("hunt"), Hunt);
		const TSharedPtr<FJsonObject> Rewards = MakeShared<FJsonObject>();
		Rewards->SetBoolField(TEXT("farSea"), false);
		Rewards->SetBoolField(TEXT("dangerDock"), false);
		Rewards->SetBoolField(TEXT("specialMerchant"), false);
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetObjectField(TEXT("categories"), Categories);
		Reply->SetObjectField(TEXT("rewards"), Rewards);
		return Reply;
	};

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	TestEqual(TEXT("열면 한 번 묻는다"), PendingDone.Num(), 1);

	// ① 답 전 — 재화·시작 장비 왕복이 스냅샷을 두 번 더 알리고, 화면을 다시 연다.
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->RequestRewards();
	TestEqual(TEXT("답 전 알림·다시 열기에 요청이 안 늘어난다"), PendingDone.Num(), 1);
	const FString BeforeHunt = Screen->GetTabText(ECodexCategory::Hunt).ToString();
	TestTrue(TEXT("답 전 사냥 탭은 `-/` 다"), BeforeHunt.Contains(TEXT("\n-/")));

	// ② 답 뒤 — 서버 값을 그리고, 같은 칸 알림에는 다시 안 묻는다.
	if (PendingDone.Num() >= 1)
	{
		PendingDone[0](true, MakeReply(), FString());
	}
	const FString AfterHunt = Screen->GetTabText(ECodexCategory::Hunt).ToString();
	AddInfo(FString::Printf(TEXT("답 전 [%s] / 답 뒤 [%s] / 요청 %d번"), *BeforeHunt, *AfterHunt, PendingDone.Num()));
	TestTrue(TEXT("답 뒤 사냥 탭은 서버 5/29 17% 다"), AfterHunt.Contains(TEXT("\n5/29 17%")));
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	Screen->RequestRewards();
	TestEqual(TEXT("답 뒤 같은 칸 알림에도 요청이 안 늘어난다"), PendingDone.Num(), 1);

	// ③ 칸이 바뀌면 보상도 바뀔 수 있다 — 그때만 다시 묻는다.
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA, TestCollectedB}), /*bFromBackend=*/true);
	TestEqual(TEXT("칸이 바뀌면 다시 묻는다"), PendingDone.Num(), 2);

	// ⑤ 전체 화면 판 — 있고, 눌림을 먹는다(`Visible`). 없거나 눌림을 흘리면 뒤 HUD 버튼이 눌린다.
	UWidget* const Backdrop = Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("CodexBackdrop")) : nullptr;
	UWidget* const Panel = Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("CodexPanel")) : nullptr;
	UWidget* const Close = Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("CodexCloseButton")) : nullptr;
	TestTrue(TEXT("바탕이 눌림을 먹는다"), Backdrop != nullptr && Backdrop->GetVisibility() == ESlateVisibility::Visible);
	TestTrue(TEXT("판이 눌림을 먹는다"), Panel != nullptr && Panel->GetVisibility() == ESlateVisibility::Visible);
	TestNotNull(TEXT("닫기 버튼"), Close);

	// ⑥ 제목 줄 버튼(#0176) — 업적·닫기가 **한 가로 상자**에 같은 세로 정렬로 있다.
	// 캔버스에 따로 꽂으면 슬롯마다 높이를 따로 갖게 돼 둘이 약 30px 갈라졌다.
	UWidget* const Achievement = Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("AchievementButton")) : nullptr;
	const UHorizontalBoxSlot* const AchievementSlot = Achievement != nullptr ? Cast<UHorizontalBoxSlot>(Achievement->Slot) : nullptr;
	const UHorizontalBoxSlot* const CloseSlot = Close != nullptr ? Cast<UHorizontalBoxSlot>(Close->Slot) : nullptr;
	AddInfo(FString::Printf(TEXT("제목 줄 부모: 업적 [%s] / 닫기 [%s]"),
		Achievement != nullptr && Achievement->GetParent() != nullptr ? *Achievement->GetParent()->GetName() : TEXT("없음"),
		Close != nullptr && Close->GetParent() != nullptr ? *Close->GetParent()->GetName() : TEXT("없음")));
	TestTrue(TEXT("업적·닫기가 같은 가로 상자(한 줄)에 있다"),
		AchievementSlot != nullptr && CloseSlot != nullptr && Achievement->GetParent() == Close->GetParent());
	TestTrue(TEXT("업적·닫기의 세로 정렬이 같다"),
		AchievementSlot != nullptr && CloseSlot != nullptr
		&& AchievementSlot->GetVerticalAlignment() == CloseSlot->GetVerticalAlignment());

	// ⑦ 탭 막대·칸 목록·보상 줄이 판 안쪽 사각형 안에 선다(#0179) — 슬레이트가 실제로 배치한 자리로 잰다.
	// 화면 폭은 슬레이트 단위(짧은 변 1080 = 배율 1): 4:3 창 1440 · 1280×720 창(배율 0.667) 1920 · 모바일 가로 2340.
	// 전엔 탭·목록이 1440 고정이라 4:3 에서 판 테두리를 넘었다.
	constexpr float PanelOutline = SnareUiStyle::ScreenPanelOutlineWidth; // 판 테두리 두께 — 글자가 테두리 선 위에 얹히면 안쪽이 아니다.
	UWidget* const Wide[] = {
		Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("TabBar")) : nullptr,
		Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("SlotList")) : nullptr,
		Screen->WidgetTree != nullptr ? Screen->WidgetTree->FindWidget(TEXT("RewardText")) : nullptr,
	};
	for (const FVector2D ViewSize : {FVector2D(1440.f, 1080.f), FVector2D(1920.f, 1080.f), FVector2D(2340.f, 1080.f)})
	{
		TSet<TSharedRef<SWidget>> Targets;
		// #0199 탭 글자도 잰다 — 버튼·글자 한 쌍씩.
		TArray<TPair<UWidget*, UWidget*>> TabPairs;
		for (int32 Index = 0; Screen->WidgetTree != nullptr && Index < static_cast<int32>(ECodexCategory::Count); ++Index)
		{
			TabPairs.Emplace(Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CodexTabButton_%d"), Index))),
				Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CodexTabText_%d"), Index))));
		}
		TArray<UWidget*> AllTargets = {Panel, Wide[0], Wide[1], Wide[2]};
		for (const TPair<UWidget*, UWidget*>& Pair : TabPairs)
		{
			AllTargets.Add(Pair.Key);
			AllTargets.Add(Pair.Value);
		}
		for (UWidget* const Target : AllTargets)
		{
			if (Target != nullptr && Target->GetCachedWidget().IsValid())
			{
				Targets.Add(Target->GetCachedWidget().ToSharedRef());
			}
		}
		Slate->SlatePrepass(1.f);
		// 슬레이트가 그릴 때와 같은 `ArrangeChildren` 을 뿌리부터 내려가며 부른다(`FindChildGeometries` 는 protected).
		TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Targets, &Arranged](const FArrangedWidget& Node)
		{
			if (Targets.Contains(Node.Widget))
			{
				Arranged.Add(Node.Widget, Node);
			}
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Slate, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
		auto RectOf = [&Arranged](UWidget* Target) -> TOptional<FSlateRect>
		{
			const FArrangedWidget* const Found = Target != nullptr && Target->GetCachedWidget().IsValid()
				? Arranged.Find(Target->GetCachedWidget().ToSharedRef()) : nullptr;
			return Found != nullptr ? Found->Geometry.GetLayoutBoundingRect() : TOptional<FSlateRect>();
		};
		const TOptional<FSlateRect> PanelRect = RectOf(Panel);
		if (!TestTrue(FString::Printf(TEXT("%.0f 폭에서 판 자리를 잰다"), ViewSize.X), PanelRect.IsSet()))
		{
			continue;
		}
		const FSlateRect Inner = PanelRect->InsetBy(FMargin(PanelOutline));
		for (UWidget* const Target : Wide)
		{
			const TOptional<FSlateRect> Rect = RectOf(Target);
			const FString Name = Target != nullptr ? Target->GetName() : TEXT("없음");
			AddInfo(FString::Printf(TEXT("%.0f 폭: 판 안쪽 [%.0f~%.0f, %.0f~%.0f] / %s [%.0f~%.0f, %.0f~%.0f]"),
				ViewSize.X, Inner.Left, Inner.Right, Inner.Top, Inner.Bottom, *Name,
				Rect.IsSet() ? Rect->Left : -1.f, Rect.IsSet() ? Rect->Right : -1.f,
				Rect.IsSet() ? Rect->Top : -1.f, Rect.IsSet() ? Rect->Bottom : -1.f));
			TestTrue(FString::Printf(TEXT("%.0f 폭에서 %s 이 판 안쪽 사각형 안에 있다"), ViewSize.X, *Name),
				Rect.IsSet() && Rect->Left >= Inner.Left && Rect->Right <= Inner.Right
				&& Rect->Top >= Inner.Top && Rect->Bottom <= Inner.Bottom);
		}

		// #0199 탭 글자가 제 버튼 안에 다 들어간다 — 받은 자리가 바라는 크기보다 작으면 글자가 넘치거나 잘린 것.
		TestEqual(FString::Printf(TEXT("%.0f 폭 탭 6개"), ViewSize.X), TabPairs.Num(), static_cast<int32>(ECodexCategory::Count));
		for (const TPair<UWidget*, UWidget*>& Pair : TabPairs)
		{
			const FArrangedWidget* const Text = Pair.Value != nullptr && Pair.Value->GetCachedWidget().IsValid()
				? Arranged.Find(Pair.Value->GetCachedWidget().ToSharedRef()) : nullptr;
			const TOptional<FSlateRect> ButtonRect = RectOf(Pair.Key);
			const FVector2D Want = Text != nullptr ? Text->Widget->GetDesiredSize() : FVector2D::ZeroVector;
			const FVector2D Got = Text != nullptr ? Text->Geometry.GetLocalSize() : FVector2D::ZeroVector;
			const FSlateRect TextRect = Text != nullptr ? Text->Geometry.GetLayoutBoundingRect() : FSlateRect();
			const FString Name = Pair.Value != nullptr ? Pair.Value->GetName() : TEXT("없음");
			AddInfo(FString::Printf(TEXT("%.0f 폭: %s 바람 %.0fx%.0f 받음 %.0fx%.0f 화면 [%.0f~%.0f] 버튼 [%.0f~%.0f]"),
				ViewSize.X, *Name, Want.X, Want.Y, Got.X, Got.Y, TextRect.Left, TextRect.Right,
				ButtonRect.IsSet() ? ButtonRect->Left : -1.f, ButtonRect.IsSet() ? ButtonRect->Right : -1.f));
			TestTrue(FString::Printf(TEXT("%.0f 폭 %s 글이 안 잘린다"), ViewSize.X, *Name),
				Text != nullptr && Got.X + 0.5f >= Want.X && Got.Y + 0.5f >= Want.Y);
			TestTrue(FString::Printf(TEXT("%.0f 폭 %s 글이 버튼 안"), ViewSize.X, *Name),
				Text != nullptr && ButtonRect.IsSet() && TextRect.Left >= ButtonRect->Left - 0.5f
				&& TextRect.Right <= ButtonRect->Right + 0.5f && TextRect.Top >= ButtonRect->Top - 0.5f
				&& TextRect.Bottom <= ButtonRect->Bottom + 0.5f);
		}
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

// M10-05b — 탭 머리글 진척(`have/total percent%`)은 서버 `GetCodexRewards` 의 categories 를 그대로 옮긴다.
// 잡는 것: ① 답 전에 스냅샷을 세어 채우는 것 ② 거절(`{error,code}`) 답에 화면이 바뀌는 것
// ③ 클라가 퍼센트를 스스로 세는 것 — 서버 값을 스냅샷과 **다르게** 줘서, 화면이 서버 수를 그대로 그리는지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexTabsServerTest, "Snarehaven.Codex.Tabs.ProgressFollowsServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexTabsServerTest::RunTest(const FString& Parameters)
{
	// ⓐ 파서 단위 — 정상 / 거절 / 널.
	auto MakeCategory = [](int32 Have, int32 Total, int32 Percent)
	{
		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("have"), Have);
		Entry->SetNumberField(TEXT("total"), Total);
		Entry->SetNumberField(TEXT("percent"), Percent);
		return Entry;
	};
	// 서버 답: 사냥 7/35 20%, 낚시 3/21 14% — 식물·설계도·요리는 뺐다(빠진 갈래는 모르는 채여야 한다).
	auto MakeReply = [&MakeCategory]()
	{
		const TSharedPtr<FJsonObject> Categories = MakeShared<FJsonObject>();
		Categories->SetObjectField(TEXT("hunt"), MakeCategory(7, 35, 20));
		Categories->SetObjectField(TEXT("fishing"), MakeCategory(3, 21, 14));
		const TSharedPtr<FJsonObject> Rewards = MakeShared<FJsonObject>();
		Rewards->SetBoolField(TEXT("farSea"), false);
		Rewards->SetBoolField(TEXT("dangerDock"), true);
		Rewards->SetBoolField(TEXT("specialMerchant"), false);
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetObjectField(TEXT("categories"), Categories);
		Reply->SetObjectField(TEXT("rewards"), Rewards);
		return Reply;
	};
	auto MakeRefusal = []()
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("error"), TEXT("로그인이 필요하다"));
		Reply->SetStringField(TEXT("code"), TEXT("bad_args"));
		return Reply;
	};

	FCodexServerReply Parsed;
	TestTrue(TEXT("정상 답은 읽힌다"), UCodexScreenWidget::ParseCodexRewards(MakeReply(), Parsed));
	const FCodexCategoryProgress& ParsedHunt = Parsed.Categories[static_cast<int32>(ECodexCategory::Hunt)];
	TestTrue(TEXT("사냥 갈래를 안다"), ParsedHunt.bKnown);
	TestEqual(TEXT("사냥 have"), ParsedHunt.Have, 7);
	TestEqual(TEXT("사냥 total"), ParsedHunt.Total, 35);
	TestEqual(TEXT("사냥 percent"), ParsedHunt.Percent, 20);
	TestFalse(TEXT("빠진 식물 갈래는 모른다"), Parsed.Categories[static_cast<int32>(ECodexCategory::Plant)].bKnown);
	TestTrue(TEXT("dangerDock 을 읽는다"), Parsed.bDangerDock);
	FCodexServerReply Untouched;
	TestFalse(TEXT("거절 답은 읽지 않는다"), UCodexScreenWidget::ParseCodexRewards(MakeRefusal(), Untouched));
	TestFalse(TEXT("널 답은 읽지 않는다"), UCodexScreenWidget::ParseCodexRewards(nullptr, Untouched));
	TestFalse(TEXT("거절 답 뒤 출력이 그대로다"), Untouched.Categories[static_cast<int32>(ECodexCategory::Hunt)].bKnown);

	// ⓑ 화면 — 가짜 서버로 답 순서를 손으로 준다.
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	TArray<USnareBackendSubsystem::FServerCallDone> PendingDone;
	USnareBackendSubsystem::TestServerOverride =
		[&PendingDone](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			PendingDone.Add(MoveTemp(OnDone));
		};

	// 스냅샷은 사냥 1칸 — 서버는 7칸이라 답한다. 화면이 스냅샷을 세면 `1/` 이 뜬다.
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA, TestCollectedB}), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	const FString BeforeHunt = Screen->GetTabText(ECodexCategory::Hunt).ToString();
	AddInfo(FString::Printf(TEXT("답 전 사냥 탭 [%s]"), *BeforeHunt));
	TestTrue(TEXT("답 전 사냥 탭은 `-/` 다"), BeforeHunt.Contains(TEXT("\n-/")));
	TestFalse(TEXT("답 전 사냥 탭에 퍼센트가 없다"), BeforeHunt.Contains(TEXT("%")));

	// ① 거절 답 — 화면이 바뀌지 않는다.
	if (TestEqual(TEXT("붙이면 한 번 묻는다"), PendingDone.Num(), 1))
	{
		PendingDone[0](true, MakeRefusal(), FString());
	}
	TestFalse(TEXT("거절 답 뒤에도 보상을 모른다"), Screen->AreRewardsKnown());
	TestEqual(TEXT("거절 답 뒤 사냥 탭이 그대로다"), Screen->GetTabText(ECodexCategory::Hunt).ToString(), BeforeHunt);

	// ② 정상 답 — 서버 수를 그대로 그린다.
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA}), /*bFromBackend=*/true);
	if (TestEqual(TEXT("스냅샷이 바뀌면 다시 묻는다"), PendingDone.Num(), 2))
	{
		PendingDone[1](true, MakeReply(), FString());
	}
	const FString HuntTab = Screen->GetTabText(ECodexCategory::Hunt).ToString();
	const FString FishTab = Screen->GetTabText(ECodexCategory::Fishing).ToString();
	const FString PlantTab = Screen->GetTabText(ECodexCategory::Plant).ToString();
	AddInfo(FString::Printf(TEXT("답 뒤 탭 — 사냥 [%s] 낚시 [%s] 식물 [%s]"), *HuntTab, *FishTab, *PlantTab));
	TestTrue(TEXT("사냥 탭은 서버 7/35 20% 다"), HuntTab.Contains(TEXT("\n7/35 20%")));
	TestTrue(TEXT("낚시 탭은 서버 3/21 14% 다"), FishTab.Contains(TEXT("\n3/21 14%")));
	TestTrue(TEXT("서버가 안 준 식물 탭은 `-/` 다"), PlantTab.Contains(TEXT("\n-/")));
	TestTrue(TEXT("dangerDock 이 화면에 반영된다"), Screen->IsDangerDockOpen());

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

// M5-07e — **사냥 탭 칸 = 서버 `CODEX_CATEGORIES.hunt`** (클라이언트·서버 칸 수 맞춤) + **탈것 칸 수 = 세 표 행 수 합**.
// 한쪽만 고치면(예: 서버에 자리표시 종이 남음) 화면은 `12/35` 인데 서버 보상 판정은 `/38` 로 세어 50%·100% 가 어긋난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexHuntServerTest, "Snarehaven.Codex.Hunt.MatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexHuntServerTest::RunTest(const FString& Parameters)
{
	FString Script;
	UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	UDataTable* const CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	UDataTable* const FishingTable = LoadObject<UDataTable>(nullptr, FishingTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable) || !TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable)
		|| !TestNotNull(TEXT("DT_FishingTable 로드"), FishingTable)
		|| !TestTrue(TEXT("CloudScript 읽기"), FFileHelper::LoadFileToString(Script,
			*(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// ① 사냥 탭 = 서버 HUNT_ANIMALS + HUNT_TROPHIES (순서 무관, 집합 같음).
	const TArray<FName> ServerAnimals = ReadServerArray(Script, TEXT("HUNT_ANIMALS"));
	TSet<FName> ServerHunt(ServerAnimals);
	ServerHunt.Append(ReadServerArray(Script, TEXT("HUNT_TROPHIES")));
	const TSet<FName> ClientHunt = CollectScreenHuntIDs(*Screen);
	AddInfo(FString::Printf(TEXT("사냥 칸 — 화면 %d · 서버 %d (서버 종 %d)"), ClientHunt.Num(), ServerHunt.Num(), ServerAnimals.Num()));
	TestTrue(TEXT("서버 사냥 목록을 읽었다"), ServerAnimals.Num() > 0);
	TestEqual(TEXT("사냥 칸 수 화면 = 서버"), ClientHunt.Num(), ServerHunt.Num());
	TestTrue(TEXT("사냥 칸 ID 화면 = 서버"), ClientHunt.Includes(ServerHunt) && ServerHunt.Includes(ClientHunt));

	// 역검증 — 서버에 칸 하나가 더 있으면 맞대기가 운다(항상 같다고 답하는 비교가 아니다).
	TSet<FName> Tampered = ServerHunt;
	Tampered.Add(FName(TEXT("animal.test_extra")));
	TestFalse(TEXT("서버에 칸 하나가 더 있으면 불일치"), ClientHunt.Includes(Tampered));

	// ② 탈것 칸 수 = 세 표 행 수 합(자리표시 동물 행 뺌) = 설계서 로스터 수. 숫자를 코드에 적지 않고 표에서 센다.
	int32 PlayableAnimalRows = 0;
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		PlayableAnimalRows += (Row != nullptr && !Row->bPlaceholderSpecies && !Row->bStorySpecies) ? 1 : 0;
	}
	const int32 RowSum = PlayableAnimalRows + CropTable->GetRowMap().Num() + FishingTable->GetRowMap().Num();
	const TArray<FName> MountIDs = UCodexScreenWidget::CollectMountCodexIDs(AnimalTable, CropTable, FishingTable);
	AddInfo(FString::Printf(TEXT("탈것 칸 %d = 동물 %d + 작물 %d + 어종 %d (설계서 로스터 %d)"), MountIDs.Num(),
		PlayableAnimalRows, CropTable->GetRowMap().Num(), FishingTable->GetRowMap().Num(), UE_ARRAY_COUNT(DesignRoster)));
	TestEqual(TEXT("탈것 칸 수 = 세 표 행 수 합"), MountIDs.Num(), RowSum);
	TestEqual(TEXT("탈것 칸 수 = DESIGN 8.5 로스터"), MountIDs.Num(), static_cast<int32>(UE_ARRAY_COUNT(DesignRoster)));
	TestEqual(TEXT("서버 사냥 종 수 = 동물 표의 기본 플레이 행 수"), ServerAnimals.Num(), PlayableAnimalRows);

	DestroyTestWorld(World);
	return true;
}

// M21-07c1 — **변종 탭 칸 = 서버 `CODEX_CATEGORIES.variant`**. 서버는 `ANIMAL_PRODUCT_ITEM` 에서 자리표시를 뺀 종마다
// `item.pet_<종>_variant` 를 만든다 — 같은 종 목록인 `HUNT_ANIMALS` 로 그 ID 를 지어 화면 칸과 맞댄다.
// 한쪽만 고치면 화면은 `변종 3/12` 인데 서버는 다른 칸을 찍어 영영 안 찬다. 서버가 보낸 진척은 여섯째 키로 읽힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexVariantServerTest, "Snarehaven.Codex.Variant.MatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexVariantServerTest::RunTest(const FString& Parameters)
{
	FString Script;
	if (!TestTrue(TEXT("CloudScript 읽기"), FFileHelper::LoadFileToString(Script,
		*(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}
	TestTrue(TEXT("서버 도감 표에 variant 갈래가 있다"), Script.Contains(TEXT("variant: VARIANT_PET_ITEMS")));

	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// ① 화면 변종 칸 = 서버 종마다 지은 변종 펫 ID.
	TSet<FName> ServerVariant;
	for (const FName& AnimalID : ReadServerArray(Script, TEXT("HUNT_ANIMALS")))
	{
		FString Species = AnimalID.ToString();
		Species.RemoveFromStart(TEXT("animal."));
		ServerVariant.Add(FName(*FString::Printf(TEXT("item.pet_%s_variant"), *Species)));
	}
	TSet<FName> ClientVariant;
	FString FirstName;
	for (const FCodexSlotView& Slot : Screen->BuildSlots())
	{
		if (Slot.Category == ECodexCategory::Variant)
		{
			ClientVariant.Add(Slot.EntryID);
			if (FirstName.IsEmpty())
			{
				FirstName = Slot.DisplayName.ToString();
			}
		}
	}
	AddInfo(FString::Printf(TEXT("[Variant] 변종 칸 — 화면 %d · 서버 %d · 첫 칸 이름 '%s'"),
		ClientVariant.Num(), ServerVariant.Num(), *FirstName));
	TestEqual(TEXT("변종 칸 수 화면 = 서버"), ClientVariant.Num(), ServerVariant.Num());
	TestEqual(TEXT("변종 칸 12 (DESIGN 10.6)"), ClientVariant.Num(), 12);
	TestTrue(TEXT("변종 칸 ID 화면 = 서버"), ClientVariant.Includes(ServerVariant) && ServerVariant.Includes(ClientVariant));
	TestTrue(TEXT("변종 칸 이름이 `변종 ○○`"), FirstName.StartsWith(TEXT("변종 ")));

	// 역검증 — 서버에 칸 하나가 더 있으면 맞대기가 운다.
	TSet<FName> Tampered = ServerVariant;
	Tampered.Add(FName(TEXT("item.pet_test_variant")));
	TestFalse(TEXT("서버에 변종 칸 하나가 더 있으면 불일치"), ClientVariant.Includes(Tampered));

	// ② 서버 답 `categories.variant` 가 여섯째 탭으로 읽히고 탭 글자가 `변종` 으로 시작한다.
	const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> Rewards = MakeShared<FJsonObject>();
	Rewards->SetBoolField(TEXT("specialMerchant"), false);
	Response->SetObjectField(TEXT("rewards"), Rewards);
	const TSharedPtr<FJsonObject> Categories = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> Variant = MakeShared<FJsonObject>();
	Variant->SetNumberField(TEXT("have"), 3);
	Variant->SetNumberField(TEXT("total"), 12);
	Variant->SetNumberField(TEXT("percent"), 25);
	Categories->SetObjectField(TEXT("variant"), Variant);
	Response->SetObjectField(TEXT("categories"), Categories);
	FCodexServerReply Reply;
	TestTrue(TEXT("답을 읽었다"), UCodexScreenWidget::ParseCodexRewards(Response, Reply));
	const FCodexCategoryProgress& Got = Reply.Categories[static_cast<int32>(ECodexCategory::Variant)];
	TestTrue(TEXT("variant 갈래를 들었다"), Got.bKnown && Got.Have == 3 && Got.Total == 12 && Got.Percent == 25);
	TestFalse(TEXT("안 온 사냥 갈래는 모르는 채"), Reply.Categories[static_cast<int32>(ECodexCategory::Hunt)].bKnown);
	const FString TabText = Screen->GetTabText(ECodexCategory::Variant).ToString().Replace(TEXT("\n"), TEXT(" "));
	AddInfo(FString::Printf(TEXT("[Variant] 탭 글자 '%s'"), *TabText));
	TestTrue(TEXT("탭 글자 `변종 -/12`"), TabText == TEXT("변종 -/12"));

	DestroyTestWorld(World);
	return true;
}

// M10-06 — **탈것 탑승 게이팅**. 스냅샷 `CodexEntries` 에 탈것 칸이 전부 있어야 열린다.
// 하나 모자라면 거부, 다 차면 허용. 배열 길이만 같고 종 하나가 빠진 경우(트로피로 부풀림)도 거부.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexMountGateTest, "Snarehaven.Codex.Mount.RequiresFullCodex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexMountGateTest::RunTest(const FString& Parameters)
{
	UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	UDataTable* const CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	UDataTable* const FishingTable = LoadObject<UDataTable>(nullptr, FishingTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable) || !TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable)
		|| !TestNotNull(TEXT("DT_FishingTable 로드"), FishingTable))
	{
		return false;
	}

	const TArray<FName> Required = UCodexScreenWidget::CollectMountCodexIDs(AnimalTable, CropTable, FishingTable);
	if (!TestTrue(TEXT("탈것 칸이 하나 이상"), Required.Num() > 0))
	{
		return false;
	}
	auto Unlocked = [&](const TArray<FName>& Entries)
	{
		return UCodexScreenWidget::IsMountUnlocked(MakeTestSnapshot(Entries), AnimalTable, CropTable, FishingTable);
	};

	// ① 다 차면 허용 — 트로피 같은 다른 칸이 더 있어도 허용.
	TestTrue(TEXT("탈것 칸이 다 차면 탑승 허용"), Unlocked(Required));
	TArray<FName> WithExtra = Required;
	WithExtra.Add(FName(TEXT("item.fox_tail")));
	TestTrue(TEXT("다른 칸이 더 있어도 허용"), Unlocked(WithExtra));

	// ② 어느 칸이든 하나 모자라면 거부 — 첫 칸만이 아니라 전 칸을 하나씩 빼 본다.
	int32 Refused = 0;
	for (int32 Index = 0; Index < Required.Num(); ++Index)
	{
		TArray<FName> MissingOne = Required;
		MissingOne.RemoveAt(Index);
		Refused += Unlocked(MissingOne) ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("탈것 칸 %d · 하나씩 뺀 %d가지 중 거부 %d"), Required.Num(), Required.Num(), Refused));
	TestEqual(TEXT("하나 모자라면 매번 거부"), Refused, Required.Num());

	// ③ 길이만 같은 스냅샷 — 종 하나를 트로피로 바꿔 채운 것. 길이만 보는 판정이면 여기서 뚫린다.
	TArray<FName> SameLength = Required;
	SameLength[0] = FName(TEXT("item.fox_tail"));
	TestEqual(TEXT("길이는 같다"), SameLength.Num(), Required.Num());
	TestFalse(TEXT("길이만 같고 칸 하나가 빠지면 거부"), Unlocked(SameLength));

	// ④ 빈 도감·표 없음 — 빈 조건으로 열지 않는다.
	TestFalse(TEXT("빈 도감이면 거부"), Unlocked({}));
	TestFalse(TEXT("표가 없으면 거부"),
		UCodexScreenWidget::IsMountUnlocked(MakeTestSnapshot(Required), nullptr, nullptr, nullptr));
	return true;
}

// M13-26 — 스냅샷 `CodexEntries` 는 서버 `GetCodexRewards.entries` 를 **그대로** 옮긴다. 이전엔 아무도 채우지 않아
// 라이브에서 탈것(`IsMountUnlocked`)·설계도(`OwnsBlueprint`)가 영영 닫혔다. 도감이 늘어난 답 뒤에도 다시 받는지,
// 실패한 답이 칸을 비우지 않는지도 본다 (DESIGN 4.8 · CLAUDE.md 8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexSnapshotFromServerTest, "Snarehaven.Codex.Snapshot.EntriesFromServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexSnapshotFromServerTest::RunTest(const FString& Parameters)
{
	TArray<FString> CalledNames;
	TArray<USnareBackendSubsystem::FServerCallDone> PendingDone;
	USnareBackendSubsystem::TestServerOverride =
		[&CalledNames, &PendingDone](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
			USnareBackendSubsystem::FServerCallDone OnDone)
		{
			CalledNames.Add(FunctionName);
			PendingDone.Add(MoveTemp(OnDone));
		};
	auto MakeReply = [](const TArray<FString>& Entries)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FString& Id : Entries)
		{
			List.Add(MakeShared<FJsonValueString>(Id));
		}
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetArrayField(TEXT("entries"), List);
		return Reply;
	};
	const FName Blueprint(TEXT("item.blueprint_grill"));

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({}), /*bFromBackend=*/true);
	Backend->RefreshCodexEntries();
	TestEqual(TEXT("서버 GetCodexRewards 에 묻는다"), FString::Join(CalledNames, TEXT(",")), FString(TEXT("GetCodexRewards")));
	TestEqual(TEXT("답 전엔 칸이 비어 있다"), Backend->GetPlayerSnapshot().CodexEntries.Num(), 0);

	// ① 서버 답 → 스냅샷이 서버 목록과 같다.
	const TArray<FName> First = {TestCollectedA, Blueprint};
	if (PendingDone.Num() == 1)
	{
		PendingDone[0](true, MakeReply({TestCollectedA.ToString(), Blueprint.ToString()}), FString());
	}
	AddInfo(FString::Printf(TEXT("첫 답 뒤 칸 %d개"), Backend->GetPlayerSnapshot().CodexEntries.Num()));
	TestTrue(TEXT("스냅샷 CodexEntries == 서버 entries"), Backend->GetPlayerSnapshot().CodexEntries == First);
	TestTrue(TEXT("설계도가 도감 칸으로 열린다"), SnareCraftRules::OwnsBlueprint(Backend->GetPlayerSnapshot(), Blueprint));

	// ② 도감이 늘어난 뒤 다시 받으면 새 목록으로 바뀐다.
	Backend->RefreshCodexEntries();
	const TArray<FName> Second = {TestCollectedA, Blueprint, TestCollectedB};
	if (PendingDone.Num() == 2)
	{
		PendingDone[1](true, MakeReply({TestCollectedA.ToString(), Blueprint.ToString(), TestCollectedB.ToString()}), FString());
	}
	TestTrue(TEXT("늘어난 서버 목록을 그대로 받는다"), Backend->GetPlayerSnapshot().CodexEntries == Second);

	// ③ 실패한 답은 칸을 비우지 않는다.
	Backend->RefreshCodexEntries();
	if (PendingDone.Num() == 3)
	{
		PendingDone[2](false, nullptr, TEXT("검사용 실패"));
	}
	TestTrue(TEXT("실패 답 뒤에도 지난 서버 목록 그대로"), Backend->GetPlayerSnapshot().CodexEntries == Second);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// M13-28 — 스냅샷 `Achievements` 는 같은 `GetCodexRewards` 답의 `achievements`({counters, done})를 **그대로** 옮긴다.
// 이전엔 아무도 채우지 않아 라이브 업적 화면이 늘 비었다. 실패한 답·업적 칸 없는 답(배포 전 서버)은 지난 값을 둔다
// (DESIGN 8.15 · 4.8 · CLAUDE.md 8 — 클라가 세지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAchievementSnapshotFromServerTest, "Snarehaven.Codex.Snapshot.AchievementsFromServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAchievementSnapshotFromServerTest::RunTest(const FString& Parameters)
{
	TArray<USnareBackendSubsystem::FServerCallDone> PendingDone;
	USnareBackendSubsystem::TestServerOverride =
		[&PendingDone](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
			USnareBackendSubsystem::FServerCallDone OnDone)
		{
			PendingDone.Add(MoveTemp(OnDone));
		};
	auto MakeReply = [](int32 Capture, const TArray<FString>& Done, bool bWithAchievements)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetArrayField(TEXT("entries"), {});
		if (bWithAchievements)
		{
			const TSharedPtr<FJsonObject> Counters = MakeShared<FJsonObject>();
			Counters->SetNumberField(TEXT("capture"), Capture);
			Counters->SetNumberField(TEXT("codexPercent"), 3);
			TArray<TSharedPtr<FJsonValue>> DoneList;
			for (const FString& Id : Done)
			{
				DoneList.Add(MakeShared<FJsonValueString>(Id));
			}
			const TSharedPtr<FJsonObject> Ach = MakeShared<FJsonObject>();
			Ach->SetObjectField(TEXT("counters"), Counters);
			Ach->SetArrayField(TEXT("done"), DoneList);
			Reply->SetObjectField(TEXT("achievements"), Ach);
		}
		return Reply;
	};
	auto Answer = [&PendingDone](bool bOk, const TSharedPtr<FJsonObject>& Reply)
	{
		if (PendingDone.Num() > 0)
		{
			PendingDone.Last()(bOk, Reply, bOk ? FString() : FString(TEXT("검사용 실패")));
		}
	};

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot({}), /*bFromBackend=*/true);
	const FSnareAchievementState& Got = Backend->GetPlayerSnapshot().Achievements;

	// ① 서버 답 → 스냅샷이 서버 상태와 같다.
	Backend->RefreshCodexEntries();
	Answer(true, MakeReply(12, {TEXT("ach.capture_1"), TEXT("ach.capture_10")}, true));
	const TArray<FName> Done = {FName(TEXT("ach.capture_1")), FName(TEXT("ach.capture_10"))};
	AddInfo(FString::Printf(TEXT("첫 답 뒤 달성 %d개 · 카운터 %d개"), Got.Completed.Num(), Got.Counters.Num()));
	TestTrue(TEXT("스냅샷 Completed == 서버 done"), Got.Completed == Done);
	TestEqual(TEXT("capture 카운터 == 서버"), Got.Counters.FindRef(TEXT("capture")), 12);
	TestEqual(TEXT("codexPercent 카운터 == 서버"), Got.Counters.FindRef(TEXT("codexPercent")), 3);

	// ② 실패한 답·업적 칸 없는 답은 지난 값을 둔다.
	Backend->RefreshCodexEntries();
	Answer(false, nullptr);
	TestTrue(TEXT("실패 답 뒤에도 지난 서버 상태 그대로"), Got.Completed == Done && Got.Counters.FindRef(TEXT("capture")) == 12);
	Backend->RefreshCodexEntries();
	Answer(true, MakeReply(0, {}, false));
	TestTrue(TEXT("업적 칸 없는 답 뒤에도 지난 서버 상태 그대로"), Got.Completed == Done);

	// ③ 서버 상태가 바뀌면 새 상태로 바뀐다(카운터만 바뀌어도).
	Backend->RefreshCodexEntries();
	Answer(true, MakeReply(13, {TEXT("ach.capture_1"), TEXT("ach.capture_10")}, true));
	TestEqual(TEXT("카운터만 늘어도 새 값을 받는다"), Got.Counters.FindRef(TEXT("capture")), 13);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// ROADMAP M24-04 — 도감·업적 판 제목이 판 안 가운데 글자가 아니라 판 왼쪽 위 모서리에 걸쳐 위로 나온 진갈색 알약인가
// (시트 `inventory_backpack.md` 「제목 탭」, 배낭·제작·출석 판과 같은 규격). 도감 탭 수·업적 줄 수는 그대로여야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexTitlePillTest, "Snarehaven.Codex.Screen.TitlePillAbovePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexTitlePillTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	UAchievementScreenWidget* const Achievement = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Codex) || !TestNotNull(TEXT("업적 화면 위젯"), Achievement))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Codex->TakeWidget();
	Achievement->TakeWidget();
	const float Rise = SnareUiStyle::MenuTitlePillRise;

	// 판 위 변·왼 변 자리를 화면 뿌리 기준으로 돌려준다 — 알약 자리와 같은 기준으로 비교한다.
	struct FPillView
	{
		FVector2D PillTopLeft = FVector2D(-1.f);
		FVector2D PanelTopLeft = FVector2D(-1.f);
		FString Title;
		bool bTitleInPill = false;
		bool bPillColor = false;
		bool bDrawnOverPanel = false;
		float PillHeight = -1.f;
	};
	auto Inspect = [](UUserWidget& Screen, const TCHAR* PillName, const TCHAR* BoxName, const TCHAR* PanelName)
	{
		FPillView View;
		UWidgetTree* const Tree = Screen.WidgetTree;
		const UTextBlock* const Title = Tree != nullptr ? Tree->FindWidget<UTextBlock>(TEXT("TitleText")) : nullptr;
		const UBorder* const Pill = Tree != nullptr ? Tree->FindWidget<UBorder>(PillName) : nullptr;
		USizeBox* const Box = Tree != nullptr ? Tree->FindWidget<USizeBox>(BoxName) : nullptr;
		UBorder* const Panel = Tree != nullptr ? Tree->FindWidget<UBorder>(PanelName) : nullptr;
		if (Title == nullptr || Pill == nullptr || Box == nullptr || Panel == nullptr)
		{
			return View;
		}
		View.Title = Title->GetText().ToString();
		View.bTitleInPill = Title->GetParent() == Pill && Pill->GetParent() == Box;
		View.bPillColor = Pill->Background.TintColor.GetSpecifiedColor() == SnareUiStyle::UiTitleTab();
		View.PillHeight = Box->GetHeightOverride();
		const UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot);
		const UCanvasPanelSlot* const BoxSlot = Cast<UCanvasPanelSlot>(Box->Slot);
		if (PanelSlot == nullptr || BoxSlot == nullptr)
		{
			return View;
		}
		View.PanelTopLeft = FVector2D(PanelSlot->GetOffsets().Left, PanelSlot->GetOffsets().Top);
		// 알약이 판 안 캔버스(판 안쪽 여백 0)에 있으면 판 자리를 더해 뿌리 기준으로 바꾼다.
		const bool bInsidePanel = Box->GetParent() != nullptr && Box->GetParent()->GetParent() == Panel
			&& Panel->GetPadding() == FMargin(0.f);
		View.PillTopLeft = BoxSlot->GetPosition() + (bInsidePanel ? View.PanelTopLeft : FVector2D::ZeroVector);
		const bool bAnchoredTopLeft = BoxSlot->GetAnchors().Minimum == FVector2D::ZeroVector && BoxSlot->GetAnchors().Maximum == FVector2D::ZeroVector;
		View.bDrawnOverPanel = bAnchoredTopLeft && (bInsidePanel
			|| (Box->GetParent() == Panel->GetParent() && Box->GetParent()->GetChildIndex(Box) > Box->GetParent()->GetChildIndex(Panel)));
		return View;
	};

	const FPillView CodexView = Inspect(*Codex, TEXT("CodexTitlePill"), TEXT("CodexTitlePillBox"), TEXT("CodexPanel"));
	const FPillView AchievementView = Inspect(*Achievement, TEXT("AchievementTitlePill"), TEXT("AchievementTitlePillBox"), TEXT("AchievementPanel"));
	for (const TPair<const TCHAR*, const FPillView*>& Pair : {TPair<const TCHAR*, const FPillView*>(TEXT("도감"), &CodexView),
		TPair<const TCHAR*, const FPillView*>(TEXT("업적"), &AchievementView)})
	{
		const FPillView& View = *Pair.Value;
		TestEqual(FString::Printf(TEXT("%s 제목 글자"), Pair.Key), View.Title, FString(Pair.Key));
		TestTrue(FString::Printf(TEXT("%s 제목 글자 부모 = 알약"), Pair.Key), View.bTitleInPill);
		TestTrue(FString::Printf(TEXT("%s 알약 색 = UiTitleTab"), Pair.Key), View.bPillColor);
		TestTrue(FString::Printf(TEXT("%s 알약을 판 위에 그린다(왼쪽 위 닻)"), Pair.Key), View.bDrawnOverPanel);
		TestEqual(FString::Printf(TEXT("%s 알약 왼 변 = 판 왼 변"), Pair.Key), View.PillTopLeft.X, View.PanelTopLeft.X);
		TestEqual(FString::Printf(TEXT("%s 알약 위 변 = 판 위 변 - MenuTitlePillRise"), Pair.Key), View.PillTopLeft.Y, View.PanelTopLeft.Y - Rise);
		TestTrue(FString::Printf(TEXT("%s 알약이 판에 걸친다(높이 > 나온 몫)"), Pair.Key), View.PillHeight > Rise);
	}

	// 도감 탭 수·업적 줄 수 그대로.
	int32 Tabs = 0;
	for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
	{
		Tabs += Codex->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CodexTabButton_%d"), Index))) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("도감 탭 수 = 분류 수"), Tabs, static_cast<int32>(ECodexCategory::Count));
	const UVerticalBox* const Lines = Achievement->WidgetTree->FindWidget<UVerticalBox>(TEXT("LineList"));
	const int32 LineCount = Lines != nullptr ? Lines->GetChildrenCount() : -1;
	TestTrue(TEXT("업적 줄이 있다"), LineCount > 0);
	TestEqual(TEXT("업적 줄 수 = 표 줄 수"), LineCount, Achievement->BuildLines().Num());

	AddInfo(FString::Printf(TEXT("[CodexTitle] 도감 알약 (%.0f, %.0f) · 판 (%.0f, %.0f) · 탭 %d / 업적 알약 (%.0f, %.0f) · 판 (%.0f, %.0f) · 줄 %d"),
		CodexView.PillTopLeft.X, CodexView.PillTopLeft.Y, CodexView.PanelTopLeft.X, CodexView.PanelTopLeft.Y, Tabs,
		AchievementView.PillTopLeft.X, AchievementView.PillTopLeft.Y, AchievementView.PanelTopLeft.X, AchievementView.PanelTopLeft.Y, LineCount));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}


// ROADMAP M25-02 — 도감 칸이 `[O] 붕어` 글자 줄이 아니라 원판 그림이다(시트 「아이템은 색 원판 위 그림」).
// 잡는 것: ① 칸 글자에 `[` 가 남는 것 ② 원판 수 ≠ 고른 탭 칸 수 ③ 모은 칸 원판이 등급 색이 아니거나 그림이 아이템 표 것이 아닌 것
// ④ 못 모은 칸이 모은 칸처럼 또렷한 것 ⑤ 사냥 종 칸에 그림이 없는 것(종 행엔 그림이 없어 펫 아이템 그림을 빌린다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCodexSlotDiscsTest, "Snarehaven.Codex.Screen.SlotsShowItemDiscs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCodexSlotDiscsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UCodexScreenWidget* const Screen = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("도감 화면 위젯"), Screen) || !TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->TakeWidget();

	// 그림은 비동기로 붙는다 — 미리 읽어 두면 `SetBrushFromSoftTexture` 가 그 자리에서 붙인다(제작 판 시험과 같은 방식).
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		if (const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value))
		{
			Row->IconRef.LoadSynchronous();
		}
	}

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	TestTrue(TEXT("검사용 스냅샷이 반영된다"),
		Backend->ApplyPlayerSnapshot(MakeTestSnapshot({TestCollectedA, TestCollectedB}), /*bFromBackend=*/true));
	Screen->BindToBackend(Backend);

	auto FindItem = [ItemTable](FName ItemID) -> const FItemRow*
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->ItemID == ItemID)
			{
				return Row;
			}
		}
		return nullptr;
	};
	const FColor Empty = SnareUiStyle::UiEmptySlot().ToFColorSRGB();

	for (const ECodexCategory Category : {ECodexCategory::Fishing, ECodexCategory::Hunt})
	{
		Screen->SetSelectedCategory(Category);
		TArray<FCodexSlotView> TabSlots;
		for (const FCodexSlotView& Slot : Screen->BuildSlots())
		{
			if (Slot.Category == Category)
			{
				TabSlots.Add(Slot);
			}
		}
		const TArray<const UBorder*> Discs = CollectSlotDiscs(*Screen);
		const FString Label = Category == ECodexCategory::Fishing ? TEXT("낚시") : TEXT("사냥");
		TestFalse(FString::Printf(TEXT("%s 탭 글자에 `[` 가 없다"), *Label), CollectScreenText(*Screen).Contains(TEXT("[")));
		TestTrue(FString::Printf(TEXT("%s 탭 칸이 있다"), *Label), TabSlots.Num() > 0);
		TestEqual(FString::Printf(TEXT("%s 탭 원판 수 = 칸 수"), *Label), Discs.Num(), TabSlots.Num());

		int32 Collected = 0;
		int32 DimIcons = 0;
		int32 Icons = 0;
		TArray<FString> NoIcon;
		for (int32 Index = 0; Index < FMath::Min(Discs.Num(), TabSlots.Num()); ++Index)
		{
			const FCodexSlotView& Slot = TabSlots[Index];
			const FItemRow* const Row = FindItem(Slot.IconItemID);
			const FColor DiscColor = Discs[Index]->Background.TintColor.GetSpecifiedColor().ToFColorSRGB();
			const UImage* const Icon = Screen->WidgetTree->FindWidget<UImage>(FName(*FString::Printf(TEXT("CodexSlotIcon_%d"), Index)));
			const UTextBlock* const Name = Screen->WidgetTree->FindWidget<UTextBlock>(FName(*FString::Printf(TEXT("CodexSlotName_%d"), Index)));
			TestTrue(FString::Printf(TEXT("%s 칸 %d 이름 = 칸 이름"), *Label, Index),
				Name != nullptr && Name->GetText().EqualTo(Slot.DisplayName));
			if (Row != nullptr && !Row->IconRef.IsNull())
			{
				++Icons;
				TestTrue(FString::Printf(TEXT("%s 칸 %d 그림 = 아이템 표 %s 그림"), *Label, Index, *Slot.IconItemID.ToString()),
					Icon != nullptr && Icon->GetBrush().GetResourceObject() != nullptr
					&& Icon->GetBrush().GetResourceObject() == static_cast<UObject*>(Row->IconRef.Get()));
			}
			else
			{
				NoIcon.Add(Slot.IconItemID.ToString());
			}
			if (Slot.bCollected)
			{
				++Collected;
				TestTrue(FString::Printf(TEXT("%s 모은 칸 %s 원판 = 등급 색"), *Label, *Slot.EntryID.ToString()),
					DiscColor == SnareUiStyle::ItemGradeColor(Row != nullptr ? Row->Grade : EItemGrade::Common).ToFColorSRGB());
				TestTrue(FString::Printf(TEXT("%s 모은 칸 그림 또렷"), *Label), Icon == nullptr || Icon->GetRenderOpacity() >= 1.f);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s 못 모은 칸 %d 원판 = 빈 칸 색"), *Label, Index), DiscColor == Empty);
				if (Icon != nullptr)
				{
					DimIcons += Icon->GetRenderOpacity() < 1.f ? 1 : 0;
					TestTrue(FString::Printf(TEXT("%s 못 모은 칸 %d 그림 불투명 < 1"), *Label, Index), Icon->GetRenderOpacity() < 1.f);
				}
			}
		}
		TestEqual(FString::Printf(TEXT("%s 모은 칸 1"), *Label), Collected, 1);
		TestTrue(FString::Printf(TEXT("%s 칸 그림이 하나 이상"), *Label), Icons > 0);
		AddInfo(FString::Printf(TEXT("[CodexDiscs] %s 칸 %d · 원판 %d · 그림 %d · 모은 칸 %d · 흐린 그림 %d · 그림 없음 [%s]"),
			*Label, TabSlots.Num(), Discs.Num(), Icons, Collected, DimIcons, *FString::Join(NoIcon, TEXT(","))));
	}

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M25-01 — 전체 화면 판 셋(도감·업적·지도)이 배낭·상점 판과 같은 크림 판·진갈색 테두리이고, 판 위 글자에 크림이
// 하나도 없는가(시트 「패널 크림 #F5EBD2 · 글자 진갈색 #4A3826」). 버튼 안 글자·제목 알약 글자는 제 바탕이 따로라 뺀다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenScreenPanelCreamTest, "Snarehaven.Codex.Screen.ScreenPanelsCream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenScreenPanelCreamTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	UAchievementScreenWidget* const Achievement = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Codex) || !TestNotNull(TEXT("업적 화면 위젯"), Achievement) || !TestNotNull(TEXT("지도 화면 위젯"), Map))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Codex->TakeWidget();
	Achievement->TakeWidget();
	Map->TakeWidget();

	const FLinearColor Cream = SnareUiStyle::UiPanelCream();
	const FLinearColor Brown = SnareUiStyle::UiTextOnButton();
	auto SameRgb = [](const FLinearColor& A, const FLinearColor& B) { return A.ToFColorSRGB().ToPackedRGBA() >> 8 == B.ToFColorSRGB().ToPackedRGBA() >> 8; };
	auto HasAncestor = [](const UWidget* Widget, TFunctionRef<bool(const UWidget*)> Pred)
	{
		for (const UWidget* Parent = Widget->GetParent(); Parent != nullptr; Parent = Parent->GetParent())
		{
			if (Pred(Parent))
			{
				return true;
			}
		}
		return false;
	};

	// 지도 판 위 글자는 제목 하나뿐이었고 M25-04 에 알약으로 옮겨 판 위 글자가 0 일 수 있다.
	struct FScreen { const TCHAR* Label; UUserWidget* Widget; const TCHAR* PanelName; const TCHAR* PillName; const TCHAR* LayerName; bool bHasPanelText; };
	const FScreen Screens[] = {
		{TEXT("도감"), Codex, TEXT("CodexPanel"), TEXT("CodexTitlePill"), nullptr, true},
		{TEXT("업적"), Achievement, TEXT("AchievementPanel"), TEXT("AchievementTitlePill"), nullptr, true},
		{TEXT("지도"), Map, TEXT("WorldMapPanel"), TEXT("WorldMapTitlePill"), TEXT("WorldMapLayer"), false},
	};
	for (const FScreen& Screen : Screens)
	{
		UWidgetTree* const Tree = Screen.Widget->WidgetTree;
		const UBorder* const Panel = Tree != nullptr ? Tree->FindWidget<UBorder>(Screen.PanelName) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s 판"), Screen.Label), Panel))
		{
			continue;
		}
		const FLinearColor PanelColor = Panel->Background.TintColor.GetSpecifiedColor();
		TestTrue(FString::Printf(TEXT("%s 판 색 = UiPanelCream"), Screen.Label), SameRgb(PanelColor, Cream));
		TestTrue(FString::Printf(TEXT("%s 판 불투명도 = ScreenPanelOpacity"), Screen.Label), FMath::IsNearlyEqual(PanelColor.A, SnareUiStyle::ScreenPanelOpacity));
		TestTrue(FString::Printf(TEXT("%s 판 테두리 = 진한 나무색 (DESIGN 7.1)"), Screen.Label),
			SameRgb(Panel->Background.OutlineSettings.Color.GetSpecifiedColor(), SnareUiStyle::UiWoodButtonPressed()));
		if (Screen.PillName != nullptr)
		{
			const UBorder* const Pill = Tree->FindWidget<UBorder>(Screen.PillName);
			TestTrue(FString::Printf(TEXT("%s 알약 그대로 UiTitleTab"), Screen.Label),
				Pill != nullptr && Pill->Background.TintColor.GetSpecifiedColor() == SnareUiStyle::UiTitleTab());
		}

		int32 Checked = 0;
		int32 CreamTexts = 0;
		TArray<FString> Bad;
		Tree->ForEachWidget([&](UWidget* Widget)
		{
			const UTextBlock* const Text = Cast<UTextBlock>(Widget);
			if (Text == nullptr || Text->GetVisibility() == ESlateVisibility::Collapsed)
			{
				return;
			}
			if (HasAncestor(Text, [](const UWidget* P) { return P->IsA<UButton>() || P->GetName().Contains(TEXT("TitlePill")); }))
			{
				return;
			}
			if (Screen.LayerName != nullptr && !HasAncestor(Text, [&Screen](const UWidget* P) { return P->GetFName() == FName(Screen.LayerName); }))
			{
				return;
			}
			++Checked;
			const FLinearColor Color = Text->GetColorAndOpacity().GetSpecifiedColor();
			if (SameRgb(Color, Cream) || SameRgb(Color, SnareUiStyle::UiButtonBase()) || SameRgb(Color, SnareUiStyle::UiButtonHighlight()))
			{
				++CreamTexts;
			}
			// 못 모은 칸 글자는 접근성 대비 4.5 를 맞춘 UiTextDim(e3fd7ed2 — 옛 방식은 진갈색에 알파)이라 둘 다 진갈색 계열로 본다.
			if (!SameRgb(Color, Brown) && !SameRgb(Color, SnareUiStyle::UiTextDim()))
			{
				Bad.Add(Text->GetName());
			}
		});
		if (Screen.bHasPanelText)
		{
			TestTrue(FString::Printf(TEXT("%s 판 위 글자가 있다"), Screen.Label), Checked > 0);
		}
		TestEqual(FString::Printf(TEXT("%s 판 위 크림 글자 수"), Screen.Label), CreamTexts, 0);
		TestEqual(FString::Printf(TEXT("%s 진갈색 아닌 판 위 글자 (%s)"), Screen.Label, *FString::Join(Bad, TEXT(","))), Bad.Num(), 0);
		AddInfo(FString::Printf(TEXT("[ScreenCream] %s 판 (%.3f, %.3f, %.3f, %.2f) · 판 위 글자 %d · 크림 글자 %d"), Screen.Label,
			PanelColor.R, PanelColor.G, PanelColor.B, PanelColor.A, Checked, CreamTexts));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
