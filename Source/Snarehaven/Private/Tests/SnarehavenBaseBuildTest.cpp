// DESIGN 8.10 제작·요리 · 8.11 건축·거점 2층 검사.
// 보는 것: ① 규칙 표·요리 시간·나무 베기 숫자가 설계서와 같은가 (숫자는 설계서에서 읽는다 — 사본을 셋으로 만들지 않는다)
// ② 레시피 재료 판정(있다/모자라다·★ 잠김·자리별 목록)이 표 그대로인가 ③ 서버 응답 **전에는** 가방·사본이 안 바뀌는가
// ④ 배치 격자(초록·빨강·파랑)·40개 한도·텐트 2개 ⑤ 마감·유지 글자가 서버 응답 글자 그대로인가.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신하고 검사 끝에 비운다 (실제 PlayFab 호출 없음).
// 이름공간을 따로 둔 이유: 유니티 빌드에서 다른 검사 파일의 익명 이름공간과 부딪히지 않게.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Base; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "Building/SnarePowerSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SizeBox.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Crafting/SnareCraftRules.h"
#include "Data/BaseRuleRow.h"
#include "Data/GatherableRow.h"
#include "Data/ItemRow.h"
#include "Data/RecipeRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gathering/GatherableActor.h"
#include "Internationalization/Regex.h"
#include "Inventory/DropBagActor.h"
#include "Inventory/InventoryComponent.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/BuildHudWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchSlotActor.h"
#include "SnarehavenGameMode.h"
#include "SnarehavenCharacter.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif
#include "World/LootChestActor.h"
#include "World/SeaActor.h"

namespace SnarehavenBaseBuildTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* RecipeTablePath = TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable");
	const TCHAR* BaseRuleTablePath = TEXT("/Game/Game/Data/Balance/DT_BaseRuleTable.DT_BaseRuleTable");
	const TCHAR* GatherableTablePath = TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable");

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다). 서버가 보낸 셈 치는 글자·ID.
	const TCHAR* TestTentId = TEXT("tent_test_1");
	const TCHAR* TestStructureId = TEXT("st_test_1");
	const TCHAR* TestPlacedAt = TEXT("2026-09-24T01:02:03.000Z");
	const TCHAR* TestDeadline = TEXT("2026-09-25T01:02:03.000Z");
	const TCHAR* TestUpkeepAfterUpgrade = TEXT("2026-09-26T01:02:03.000Z");
	const TCHAR* TestUpkeepAfterFeed = TEXT("2026-09-26T21:02:03.000Z");
	constexpr int32 TestHouseColor = 3;
	constexpr int32 TestFeedCount = 10;
	constexpr int32 TestRollTries = 200;

	/** 가짜 서버가 받은 부탁 한 건. 응답은 검사가 원하는 때에 준다 — 그 전이 "응답 전" 이다. */
	struct FPendingCall
	{
		FString FunctionName;
		TSharedPtr<FJsonObject> Args;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	/** 가짜 서버를 켠다. 받은 부탁은 쌓기만 하고 답하지 않는다. */
	TSharedRef<TArray<FPendingCall>> InstallDeferredServer()
	{
		TSharedRef<TArray<FPendingCall>> Calls = MakeShared<TArray<FPendingCall>>();
		USnareBackendSubsystem::TestServerOverride =
			[Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
			{
				Calls->Add(FPendingCall{ FunctionName, Args, MoveTemp(OnDone) });
			};
		return Calls;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	/** 맨 마지막 부탁에 서버가 이 JSON 으로 답한다. */
	void Respond(TArray<FPendingCall>& Calls, const FString& ResponseJson)
	{
		if (Calls.Num() == 0 || !Calls.Last().OnDone)
		{
			return;
		}
		USnareBackendSubsystem::FServerCallDone Done = MoveTemp(Calls.Last().OnDone);
		Done(true, ParseJson(ResponseJson), FString());
	}

	FString ArgString(const FPendingCall& Call, const TCHAR* Field)
	{
		FString Value;
		if (Call.Args.IsValid())
		{
			Call.Args->TryGetStringField(Field, Value);
		}
		return Value;
	}

	int32 ArgInt(const FPendingCall& Call, const TCHAR* Field)
	{
		double Value = -1.0;
		if (Call.Args.IsValid())
		{
			Call.Args->TryGetNumberField(Field, Value);
		}
		return FMath::RoundToInt32(Value);
	}

	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	FSnarePlayerSnapshot MakeSnapshot()
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTBASE");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		return Snapshot;
	}

	/**
	 * 초기화하지 않은 게임 인스턴스에 세운 기지 창구 — 월드가 없어 요리 타이머가 돌지 않으니 검사가 시간을 쥔다.
	 * 표는 실제 표를 꽂는다 (기대값을 코드에 적지 않는다).
	 */
	USnareBaseSubsystem* MakeBase(FAutomationTestBase& Test, USnareBackendSubsystem* Backend)
	{
		UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
		UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
		UDataTable* const BaseRuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
		Test.TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable);
		Test.TestNotNull(TEXT("DT_RecipeTable 로드"), RecipeTable);
		Test.TestNotNull(TEXT("DT_BaseRuleTable 로드"), BaseRuleTable);

		USnareBaseSubsystem* const Base = NewObject<USnareBaseSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
		Base->SetTablesForTest(ItemTable, RecipeTable, BaseRuleTable);
		Base->SetBackendForTest(Backend);
		return Base;
	}

	FSnareTentMirror MakeTent(const FString& TentId, const FIntPoint& Cell)
	{
		FSnareTentMirror Tent;
		Tent.TentId = TentId;
		Tent.Cell = Cell;
		Tent.PlacedAtUtc = TestPlacedAt;
		Tent.UpgradeDeadlineUtc = TestDeadline;
		return Tent;
	}

	// ── 설계서 읽기 (SnarehavenCraftingDataTest 와 같은 짜임) ──

	FString ExtractSection(const FString& Doc, const FString& HeadingPrefix)
	{
		const int32 Start = Doc.Find(HeadingPrefix, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 BodyStart = Start + HeadingPrefix.Len();
		const int32 NextHeading = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyStart);
		return NextHeading == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, NextHeading - BodyStart);
	}

	/** 첫 매치의 캡처 그룹을 정수로. 없으면 -1 (그러면 비교가 실패해 설계서 문구가 바뀐 걸 알린다). */
	int32 CaptureInt(const FString& Text, const TCHAR* Pattern, int32 Group = 1)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(Group)) : -1;
	}

	bool LoadDesignSections(FAutomationTestBase& Test, FString& OutCraft, FString& OutBase)
	{
		FString Doc;
		const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
		if (!Test.TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath), FFileHelper::LoadFileToString(Doc, *DocPath)))
		{
			return false;
		}
		OutCraft = ExtractSection(Doc, TEXT("### 8.10"));
		OutBase = ExtractSection(Doc, TEXT("### 8.11"));
		return Test.TestFalse(TEXT("8.10 절이 있다"), OutCraft.IsEmpty()) && Test.TestFalse(TEXT("8.11 절이 있다"), OutBase.IsEmpty());
	}

	/** 태그 재료를 채울 아이템 하나 (표 순서 첫 번째). */
	FName FirstItemWithTag(const UDataTable* ItemTable, ECraftTag Tag)
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->CraftTag == Tag)
			{
				return Row->ItemID;
			}
		}
		return NAME_None;
	}

	/** 레시피 재료를 딱 맞게 담은 가방. `OutFirstSource` 는 첫 재료를 채운 아이템 (하나 빼 보는 역검사용). */
	FSnarePlayerSnapshot MakeExactSnapshot(const FRecipeRow& Recipe, const UDataTable* ItemTable, FName& OutFirstSource)
	{
		FSnarePlayerSnapshot Snapshot = MakeSnapshot();
		OutFirstSource = NAME_None;
		for (const FRecipeIngredient& Ingredient : Recipe.Ingredients)
		{
			const FName Source = Ingredient.Tag != ECraftTag::None ? FirstItemWithTag(ItemTable, Ingredient.Tag) : Ingredient.ItemID;
			Snapshot.AddItem(Source, Ingredient.Count);
			if (OutFirstSource.IsNone())
			{
				OutFirstSource = Source;
			}
		}
		if (!Recipe.RequiredBlueprintID.IsNone())
		{
			Snapshot.CodexEntries.Add(Recipe.RequiredBlueprintID);
		}
		return Snapshot;
	}

	UWorld* CreateTestWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
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

using namespace SnarehavenBaseBuildTest;

// ① 규칙 행이 설계서 8.10·8.11 과 같다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseRulesDesignTest, "Snarehaven.Base.Rules.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseRulesDesignTest::RunTest(const FString& Parameters)
{
	FString Craft;
	FString BaseText;
	if (!LoadDesignSections(*this, Craft, BaseText))
	{
		return false;
	}
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Row = Table != nullptr ? Table->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("BaseRulesTest")) : nullptr;
	if (!TestNotNull(TEXT("DT_BaseRuleTable 의 Base 행"), Row))
	{
		return false;
	}
	TestTrue(TEXT("미정(-1) 칸이 없다"), Row->IsComplete());

	TestEqual(TEXT("불 옆 회복 반경 cm"), FMath::RoundToInt32(Row->FireHealRadiusCm), CaptureInt(Craft, TEXT(R"(\*\*(\d+)cm\*\* 안 체력)")));
	TestEqual(TEXT("불 옆 회복 /초"), FMath::RoundToInt32(Row->FireHealPerSecond), CaptureInt(Craft, TEXT(R"(체력 \*\*\+(\d+)/초\*\*)")));
	TestEqual(TEXT("땅 칸 수"), Row->LandCells, CaptureInt(BaseText, TEXT(R"(\*\*(\d+) × \d+ 칸\*\*)")));
	TestEqual(TEXT("1칸 cm"), FMath::RoundToInt32(Row->CellSizeCm), CaptureInt(BaseText, TEXT(R"(1칸 (\d+)cm)")));
	TestEqual(TEXT("텐트 최대"), Row->TentMax, CaptureInt(BaseText, TEXT(R"(한 사람 \*\*최대 (\d+)\*\*)")));
	TestEqual(TEXT("땅 하나 가구 수"), Row->FurnitureMax, CaptureInt(BaseText, TEXT(R"(땅 하나에 \*\*(\d+)\*\*)")));
	TestEqual(TEXT("집 올리기 기한 시간"), Row->UpgradeDeadlineHours, CaptureInt(BaseText, TEXT(R"(\*\*(\d+)시간 안에\*\*)")));
	TestEqual(TEXT("집 올리기 나무"), Row->UpgradeWoodCount, CaptureInt(BaseText, TEXT(R"(\*\*나무 (\d+) \+ 향나무 (\d+)\*\*)"), 1));
	TestEqual(TEXT("집 올리기 향나무"), Row->UpgradeCedarCount, CaptureInt(BaseText, TEXT(R"(\*\*나무 (\d+) \+ 향나무 (\d+)\*\*)"), 2));
	TestEqual(TEXT("집 색 수"), Row->HouseColorCount, CaptureInt(BaseText, TEXT(R"((\d+)가지 중 고름)")));
	TestEqual(TEXT("나무 1개당 유지 시간"), Row->UpkeepHoursPerWood, CaptureInt(BaseText, TEXT(R"(1개 = \*\*(\d+)시간\*\*)")));
	TestEqual(TEXT("유지 최대 시간"), Row->UpkeepMaxHours, CaptureInt(BaseText, TEXT(R"(최대 \*\*(\d+)시간\*\*)")));
	TestEqual(TEXT("PIN 자리 수"), Row->PinLength, CaptureInt(BaseText, TEXT(R"(\*\*PIN (\d+)자리\*\*)")));
	return true;
}

// ② 요리 시간은 불 종류별로 아이템 표에서 오고, 설계서 8.10 과 같다. 불이 아니면 -1. 올려도 서버엔 아직 안 묻는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseCookTimeTest, "Snarehaven.Base.Cook.SecondsPerFireFromTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseCookTimeTest::RunTest(const FString& Parameters)
{
	FString Craft;
	FString BaseText;
	if (!LoadDesignSections(*this, Craft, BaseText))
	{
		return false;
	}
	const TCHAR* const CookPattern = TEXT(R"(모닥불 \*\*(\d+)초\*\* · 정원 화로 \*\*(\d+)초\*\* · 그릴 \*\*(\d+)초\*\*)");
	const TArray<TPair<FName, int32>> Fires = {
		{ TEXT("item.campfire"), CaptureInt(Craft, CookPattern, 1) },
		{ TEXT("item.garden_brazier"), CaptureInt(Craft, CookPattern, 2) },
		{ TEXT("item.grill"), CaptureInt(Craft, CookPattern, 3) },
	};

	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);

	for (const TPair<FName, int32>& Fire : Fires)
	{
		TestTrue(*FString::Printf(TEXT("%s 설계서 초를 읽었다"), *Fire.Key.ToString()), Fire.Value > 0);
		TestEqual(*FString::Printf(TEXT("%s 요리 시간"), *Fire.Key.ToString()),
			FMath::RoundToInt32(Base->GetCookSeconds(Fire.Key)), Fire.Value);
	}
	TestEqual(TEXT("불이 아닌 것(나무판)은 -1"), Base->GetCookSeconds(TEXT("item.plank")), -1.f);
	TestFalse(TEXT("불이 아닌 것 위엔 요리를 못 올린다"), Base->StartCooking(TEXT("fire_x"), TEXT("item.plank"), TEXT("item.roast_meat")));

	// 그릴에 올린다 — 타이머는 재화가 아니다. 끝나기 전엔 서버에 아무것도 안 묻는다.
	const FString FireKey = TEXT("fire_grill");
	TestTrue(TEXT("그릴에 구운 고기를 올린다"), Base->StartCooking(FireKey, TEXT("item.grill"), TEXT("item.roast_meat")));
	TestFalse(TEXT("같은 불엔 한 번에 1개"), Base->StartCooking(FireKey, TEXT("item.grill"), TEXT("item.roast_meat")));
	const FSnareCookJob* const Job = Base->FindCookJob(FireKey);
	if (TestNotNull(TEXT("요리 한 건이 선다"), Job))
	{
		TestEqual(TEXT("요리 길이 = 그릴 표 값"), FMath::RoundToInt32(Job->DurationSeconds), Fires[2].Value);
	}
	TestEqual(TEXT("올린 순간엔 서버 호출 0건"), Calls->Num(), 0);

	// 타이머 끝 — 그때 `CraftItem {recipeId = 결과 아이템}` 을 묻는다.
	Base->CompleteCooking(FireKey);
	if (TestEqual(TEXT("끝나면 서버 호출 1건"), Calls->Num(), 1))
	{
		TestEqual(TEXT("함수 이름"), (*Calls)[0].FunctionName, FString(TEXT("CraftItem")));
		TestEqual(TEXT("recipeId = 결과 아이템"), ArgString((*Calls)[0], TEXT("recipeId")), FString(TEXT("item.roast_meat")));
		TestEqual(TEXT("times = 1"), ArgInt((*Calls)[0], TEXT("times")), 1);
	}
	TestNull(TEXT("요리 칸이 비었다"), Base->FindCookJob(FireKey));
	Respond(*Calls, TEXT(R"({"crafted":"item.roast_meat","count":1})"));
	TestFalse(TEXT("응답 뒤 한가하다"), Base->IsBusy());

	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// ③ 레시피 줄의 재료 판정이 표 그대로다 — 빈 가방이면 모두 모자라고 ★ 만 잠김, 딱 맞게 담으면 켜지고 하나 빼면 꺼진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseRecipeViewTest, "Snarehaven.Base.Craft.IngredientsMatchTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseRecipeViewTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const UDataTable* const RecipeTable = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable) || !TestNotNull(TEXT("DT_RecipeTable 로드"), RecipeTable))
	{
		return false;
	}
	TMap<FName, const FItemRow*> ItemsByID;
	SnareCraftRules::IndexItems(ItemTable, ItemsByID);

	int32 LockedRows = 0;
	for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
	{
		const FRecipeRow& Recipe = *reinterpret_cast<const FRecipeRow*>(Pair.Value);
		const FString Name = Pair.Key.ToString();

		// 빈 가방.
		const FSnareRecipeView Empty = SnareCraftRules::BuildRecipeView(Pair.Key, Recipe, MakeSnapshot(), ItemsByID);
		TestEqual(*FString::Printf(TEXT("[%s] 결과 ID"), *Name), Empty.ResultItemID, Recipe.ResultItemID);
		TestEqual(*FString::Printf(TEXT("[%s] 재료 칸 수"), *Name), Empty.Ingredients.Num(), Recipe.Ingredients.Num());
		for (int32 Index = 0; Index < FMath::Min(Empty.Ingredients.Num(), Recipe.Ingredients.Num()); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("[%s] 재료 %d 필요 수 = 표"), *Name, Index), Empty.Ingredients[Index].Need, Recipe.Ingredients[Index].Count);
			TestEqual(*FString::Printf(TEXT("[%s] 재료 %d 있음 0"), *Name, Index), Empty.Ingredients[Index].Have, 0);
			TestFalse(*FString::Printf(TEXT("[%s] 재료 %d 이름이 있다"), *Name, Index), Empty.Ingredients[Index].Label.IsEmpty());
		}
		TestEqual(*FString::Printf(TEXT("[%s] ★ 잠김 = 설계도 칸"), *Name), Empty.bLocked, !Recipe.RequiredBlueprintID.IsNone());
		TestFalse(*FString::Printf(TEXT("[%s] 빈 가방은 못 만든다"), *Name), Empty.CanCraft());
		LockedRows += Empty.bLocked ? 1 : 0;

		// 딱 맞게 + 설계도.
		FName FirstSource;
		FSnarePlayerSnapshot Exact = MakeExactSnapshot(Recipe, ItemTable, FirstSource);
		const FSnareRecipeView Ready = SnareCraftRules::BuildRecipeView(Pair.Key, Recipe, Exact, ItemsByID);
		TestTrue(*FString::Printf(TEXT("[%s] 딱 맞으면 만들 수 있다"), *Name), Ready.CanCraft());

		// 역검사: 첫 재료 하나를 뺀다 → 꺼지고 그 재료에 모자람 표시.
		FSnarePlayerSnapshot Short = Exact;
		for (FInventorySlot& Slot : Short.InventorySlots)
		{
			if (Slot.ItemID == FirstSource)
			{
				--Slot.Count;
			}
		}
		const FSnareRecipeView NotReady = SnareCraftRules::BuildRecipeView(Pair.Key, Recipe, Short, ItemsByID);
		TestFalse(*FString::Printf(TEXT("[%s] 하나 모자라면 못 만든다"), *Name), NotReady.CanCraft());
		if (NotReady.Ingredients.Num() > 0)
		{
			TestTrue(*FString::Printf(TEXT("[%s] 모자람이 글자로 보인다"), *Name),
				UCraftingScreenWidget::FormatIngredientText(NotReady.Ingredients[0]).ToString().Contains(TEXT("모자람")));
		}

		// 역검사: 재료는 다 있어도 설계도가 없으면 ★ 는 잠긴다.
		if (!Recipe.RequiredBlueprintID.IsNone())
		{
			Exact.CodexEntries.Reset();
			const FSnareRecipeView NoBlueprint = SnareCraftRules::BuildRecipeView(Pair.Key, Recipe, Exact, ItemsByID);
			TestTrue(*FString::Printf(TEXT("[%s] 설계도 없으면 잠김"), *Name), NoBlueprint.bLocked);
			TestFalse(*FString::Printf(TEXT("[%s] 설계도 없으면 못 만든다"), *Name), NoBlueprint.CanCraft());
		}
	}
	AddInfo(FString::Printf(TEXT("레시피 %d줄, ★ %d줄"), RecipeTable->GetRowMap().Num(), LockedRows));
	TestTrue(TEXT("★ 레시피가 있다"), LockedRows > 0);

	// 자리별 목록 — 배낭은 어디서나만, 작업대는 작업대+어디서나, 불은 요리만, 제련소는 제련만 (DESIGN 8.16).
	struct FPlaceCase { ESnareCraftPlace Place; const TCHAR* Label; };
	for (const FPlaceCase& Case : { FPlaceCase{ ESnareCraftPlace::Backpack, TEXT("배낭") },
		FPlaceCase{ ESnareCraftPlace::Workbench, TEXT("작업대") }, FPlaceCase{ ESnareCraftPlace::Fire, TEXT("불") },
		FPlaceCase{ ESnareCraftPlace::Smelter, TEXT("제련소") } })
	{
		TArray<FSnareRecipeView> Views;
		SnareCraftRules::BuildRecipeList(RecipeTable, ItemTable, MakeSnapshot(), Case.Place, Views);
		TestTrue(*FString::Printf(TEXT("%s 목록이 비지 않았다"), Case.Label), Views.Num() > 0);
		for (const FSnareRecipeView& View : Views)
		{
			const bool bExpected = Case.Place == ESnareCraftPlace::Fire ? View.Station == ERecipeStation::Fire
				: Case.Place == ESnareCraftPlace::Smelter ? View.Station == ERecipeStation::Smelter
				: Case.Place == ESnareCraftPlace::Backpack ? View.Station == ERecipeStation::Anywhere
				: View.Station == ERecipeStation::Workbench || View.Station == ERecipeStation::Anywhere;
			TestTrue(*FString::Printf(TEXT("%s 목록의 %s 자리가 맞다"), Case.Label, *View.ResultItemID.ToString()), bExpected);
		}
		if (Case.Place == ESnareCraftPlace::Backpack)
		{
			// DESIGN 8.10 「작업대 없이 되는 것」 — 모닥불·미끼 + 가공품 2(수지 접착제·면 원단). 요리 재료 3은 불, 티백은 제작 없음(10-06 보정)
			for (const TCHAR* const ID : { TEXT("item.campfire"), TEXT("item.berry_bait"), TEXT("item.resin_adhesive"), TEXT("item.cotton_fabric") })
			{
				TestTrue(*FString::Printf(TEXT("배낭 목록에 %s"), ID),
					Views.ContainsByPredicate([ID](const FSnareRecipeView& View) { return View.ResultItemID == FName(ID); }));
			}
			for (const TCHAR* const ID : { TEXT("item.flour"), TEXT("item.sugar"), TEXT("item.vegetable_oil"), TEXT("item.tea_bag") })
			{
				TestFalse(*FString::Printf(TEXT("배낭 목록에 %s 가 없다"), ID),
					Views.ContainsByPredicate([ID](const FSnareRecipeView& View) { return View.ResultItemID == FName(ID); }));
			}
			AddInfo(FString::Printf(TEXT("[Craft] 배낭 제작 목록 %d줄"), Views.Num()));
		}
		if (Case.Place == ESnareCraftPlace::Fire)
		{
			// 원본 「요리」 — 밀가루·설탕·식물성 오일은 모닥불에서 (DESIGN 13.6, 10-06 보정)
			for (const TCHAR* const ID : { TEXT("item.flour"), TEXT("item.sugar"), TEXT("item.vegetable_oil") })
			{
				TestTrue(*FString::Printf(TEXT("불 목록에 %s"), ID),
					Views.ContainsByPredicate([ID](const FSnareRecipeView& View) { return View.ResultItemID == FName(ID); }));
			}
			AddInfo(FString::Printf(TEXT("[Craft] 불 요리 목록 %d줄"), Views.Num()));
		}
	}
	return true;
}

// ④ 응답 전엔 아무것도 안 바뀐다 — 가방 스냅샷·사본 그대로, 한 번에 한 요청. 응답이 오면 그때 안내가 바뀐다. 거절도 사본 불변.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseNothingBeforeResponseTest, "Snarehaven.Base.Server.NothingBeforeResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseNothingBeforeResponseTest::RunTest(const FString& Parameters)
{
	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	FSnarePlayerSnapshot Snapshot = MakeSnapshot();
	Snapshot.AddItem(TEXT("item.wood"), 4);
	TestTrue(TEXT("검사 가방이 반영된다"), Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true));
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);

	auto WoodInBag = [Backend]() { return SnareCraftRules::CountItem(Backend->GetPlayerSnapshot().InventorySlots, TEXT("item.wood")); };
	auto PlankInBag = [Backend]() { return SnareCraftRules::CountItem(Backend->GetPlayerSnapshot().InventorySlots, TEXT("item.plank")); };

	TestTrue(TEXT("나무판 제작을 묻는다"), Base->RequestCraft(TEXT("item.plank"), 1));
	TestTrue(TEXT("묻는 중"), Base->IsBusy());
	TestFalse(TEXT("한 번에 한 요청"), Base->RequestCraft(TEXT("item.plank"), 1));
	TestEqual(TEXT("서버 호출은 1건뿐"), Calls->Num(), 1);
	TestEqual(TEXT("응답 전 나무 그대로"), WoodInBag(), 4);
	TestEqual(TEXT("응답 전 나무판 0"), PlankInBag(), 0);
	// M15-16 — 물린 둘째 누름은 이제 말을 한다(전엔 안내가 비어 있었다). 바뀌는 것은 글자뿐,
	// 서버 호출·가방은 위에서 본 대로 그대로다 (CLAUDE.md 8).
	TestEqual(TEXT("응답 전 안내는 「만드는 중」 한 줄"), Base->GetLastMessage().ToString(),
		FString(TEXT("만드는 중입니다")));

	// 서버가 성공이라 답해도 가방 숫자는 응답이 아니라 `FetchPlayerState` 다음 스냅샷에서만 바뀐다.
	Respond(*Calls, TEXT(R"({"crafted":"item.plank","count":1})"));
	TestFalse(TEXT("응답 뒤 한가하다"), Base->IsBusy());
	TestTrue(*FString::Printf(TEXT("완성 안내 (실제 \"%s\")"), *Base->GetLastMessage().ToString()),
		Base->GetLastMessage().ToString().Contains(TEXT("완성")));
	TestEqual(TEXT("응답만으로는 가방이 안 바뀐다 (나무)"), WoodInBag(), 4);
	TestEqual(TEXT("응답만으로는 가방이 안 바뀐다 (나무판)"), PlankInBag(), 0);

	// 역검사: 거절 — 안내는 오류 코드에서, 사본은 그대로.
	TestTrue(TEXT("텐트를 묻는다"), Base->RequestPlaceTent(FIntPoint(0, 0)));
	TestEqual(TEXT("응답 전 텐트 사본 0"), Base->GetTents().Num(), 0);
	Respond(*Calls, TEXT(R"({"error":"x","code":"not_enough_items","need":35,"have":5})"));
	TestEqual(TEXT("거절이면 텐트 사본 0"), Base->GetTents().Num(), 0);
	TestTrue(*FString::Printf(TEXT("모자람 안내에 필요·있음 수 (실제 \"%s\")"), *Base->GetLastMessage().ToString()),
		Base->GetLastMessage().ToString().Contains(TEXT("필요 35")) && Base->GetLastMessage().ToString().Contains(TEXT("있음 5")));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// ⑤ 텐트 → 가구 → 집 → 유지 → 보관함: 보내는 인자와, 사본·화면 글자가 서버 응답 글자 그대로인가.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseServerVerbatimTest, "Snarehaven.Base.Server.MirrorIsServerVerbatim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseServerVerbatimTest::RunTest(const FString& Parameters)
{
	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);

	// 텐트.
	const FIntPoint TentCell(10, 20);
	TestTrue(TEXT("PlaceTent 를 묻는다"), Base->RequestPlaceTent(TentCell));
	TestEqual(TEXT("PlaceTent x"), ArgInt(Calls->Last(), TEXT("x")), TentCell.X);
	TestEqual(TEXT("PlaceTent y"), ArgInt(Calls->Last(), TEXT("y")), TentCell.Y);
	Respond(*Calls, FString::Printf(TEXT(R"({"tentId":"%s","placedAtUtc":"%s","upgradeDeadlineUtc":"%s"})"), TestTentId, TestPlacedAt, TestDeadline));
	const FSnareTentMirror* Tent = Base->FindTent(TestTentId);
	if (!TestNotNull(TEXT("텐트 사본"), Tent))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}
	TestEqual(TEXT("마감 글자 그대로"), Tent->UpgradeDeadlineUtc, FString(TestDeadline));
	TestTrue(TEXT("화면 마감 글에 서버 글자"), UCraftingScreenWidget::FormatDeadlineText(*Tent).ToString().Contains(TestDeadline));
	TestTrue(TEXT("텐트일 땐 유지 글 없음"), UCraftingScreenWidget::FormatUpkeepText(*Tent).IsEmpty());

	// 가구 (보관함 + PIN, 회전은 15° 로 맞춰 보낸다).
	TestTrue(TEXT("PlaceStructure 를 묻는다"), Base->RequestPlaceStructure(TestTentId, TEXT("item.storage_box"), TentCell + FIntPoint(4, 0), 100, TEXT("12345")));
	TestEqual(TEXT("PlaceStructure 함수"), Calls->Last().FunctionName, FString(TEXT("PlaceStructure")));
	TestEqual(TEXT("itemId"), ArgString(Calls->Last(), TEXT("itemId")), FString(TEXT("item.storage_box")));
	TestEqual(TEXT("rot 은 15° 단위"), ArgInt(Calls->Last(), TEXT("rot")), 105);
	TestEqual(TEXT("pin"), ArgString(Calls->Last(), TEXT("pin")), FString(TEXT("12345")));
	TestNull(TEXT("응답 전 가구 사본 없음"), Base->FindStructure(TestStructureId));
	Respond(*Calls, FString::Printf(TEXT(R"({"tentId":"%s","structureId":"%s"})"), TestTentId, TestStructureId));
	TestNotNull(TEXT("응답 뒤 가구 사본"), Base->FindStructure(TestStructureId));

	// 보관함 넣기 — 칸 수는 서버 `storage` 그대로.
	TestTrue(TEXT("MoveToStorage 를 묻는다"), Base->RequestMoveToStorage(TestTentId, TestStructureId, TEXT("item.wood"), 7, FString(), FString()));
	Respond(*Calls, TEXT(R"({"storage":{"item.wood":7}})"));
	const FSnareStructureMirror* const Box = Base->FindStructure(TestStructureId);
	if (TestNotNull(TEXT("보관함 사본"), Box))
	{
		TestEqual(TEXT("보관함 나무 = 서버 값"), Box->Storage.FindRef(TEXT("item.wood")), 7);
	}

	// 벽 장식 — `wall {side, h}` 를 보내고, 응답 뒤 사본에 변·높이가 남는다.
	TestTrue(TEXT("벽 걸기를 묻는다"), Base->RequestPlaceStructure(TestTentId, TEXT("item.picture_frame"), TentCell + FIntPoint(1, 2), 0, FString(),
		FIntPoint::ZeroValue, 1, 150));
	{
		const TSharedPtr<FJsonObject>* WallArg = nullptr;
		if (TestTrue(TEXT("wall 인자"), Calls->Last().Args.IsValid() && Calls->Last().Args->TryGetObjectField(TEXT("wall"), WallArg)))
		{
			TestEqual(TEXT("wall.side"), ArgInt(FPendingCall{Calls->Last().FunctionName, *WallArg}, TEXT("side")), 1);
			TestEqual(TEXT("wall.h"), ArgInt(FPendingCall{Calls->Last().FunctionName, *WallArg}, TEXT("h")), 150);
		}
	}
	Respond(*Calls, FString::Printf(TEXT(R"({"tentId":"%s","structureId":"st_wall_1"})"), TestTentId));
	if (const FSnareStructureMirror* const Frame = Base->FindStructure(TEXT("st_wall_1")))
	{
		TestEqual(TEXT("사본 변"), Frame->WallSide, 1);
		TestEqual(TEXT("사본 높이"), Frame->WallHeightCm, 150);
	}
	else
	{
		AddError(TEXT("벽 장식 사본 없음"));
	}
	TestEqual(TEXT("바닥 가구는 벽 아님"), Base->FindStructure(TestStructureId)->WallSide, -1);

	// 집 올리기.
	TestTrue(TEXT("UpgradeHouse 를 묻는다"), Base->RequestUpgradeHouse(TestTentId, TestHouseColor));
	TestEqual(TEXT("color 인자"), ArgInt(Calls->Last(), TEXT("color")), TestHouseColor);
	TestFalse(TEXT("응답 전엔 아직 텐트"), Base->FindTent(TestTentId)->bUpgraded);
	Respond(*Calls, FString::Printf(TEXT(R"({"tentId":"%s","color":%d,"upkeepUntilUtc":"%s"})"), TestTentId, TestHouseColor, TestUpkeepAfterUpgrade));
	Tent = Base->FindTent(TestTentId);
	TestTrue(TEXT("집이 됐다"), Tent->bUpgraded);
	TestEqual(TEXT("색 = 서버 값"), Tent->Color, TestHouseColor);
	TestEqual(TEXT("유지 글자 그대로"), Tent->UpkeepUntilUtc, FString(TestUpkeepAfterUpgrade));
	TestTrue(TEXT("집이면 마감 글 없음"), UCraftingScreenWidget::FormatDeadlineText(*Tent).IsEmpty());
	TestTrue(TEXT("화면 유지 글에 서버 글자"), UCraftingScreenWidget::FormatUpkeepText(*Tent).ToString().Contains(TestUpkeepAfterUpgrade));

	// 유지 넣기 — 새 시각도 클라이언트가 더하지 않고 서버 글자를 그대로 쓴다.
	TestTrue(TEXT("FeedUpkeep 를 묻는다"), Base->RequestFeedUpkeep(TestTentId, TestFeedCount));
	TestEqual(TEXT("count 인자"), ArgInt(Calls->Last(), TEXT("count")), TestFeedCount);
	TestEqual(TEXT("응답 전 유지 글자 그대로"), Base->FindTent(TestTentId)->UpkeepUntilUtc, FString(TestUpkeepAfterUpgrade));
	Respond(*Calls, FString::Printf(TEXT(R"({"upkeepUntilUtc":"%s","used":%d})"), TestUpkeepAfterFeed, TestFeedCount));
	TestEqual(TEXT("유지 글자 = 새 서버 값"), Base->FindTent(TestTentId)->UpkeepUntilUtc, FString(TestUpkeepAfterFeed));
	TestTrue(TEXT("화면 유지 글도 새 값"), UCraftingScreenWidget::FormatUpkeepText(*Base->FindTent(TestTentId)).ToString().Contains(TestUpkeepAfterFeed));

	// 칠하기 (DESIGN 13.8) — 응답 전엔 안 칠함, 응답 뒤 서버 `color` 그대로.
	const FName TestPaint(TEXT("item.paint_red"));
	TestTrue(TEXT("PaintStructure 를 묻는다"), Base->RequestPaintStructure(TestTentId, TestStructureId, TestPaint));
	TestEqual(TEXT("PaintStructure 함수"), Calls->Last().FunctionName, FString(TEXT("PaintStructure")));
	TestEqual(TEXT("paintId 인자"), ArgString(Calls->Last(), TEXT("paintId")), TestPaint.ToString());
	TestEqual(TEXT("structureId 인자"), ArgString(Calls->Last(), TEXT("structureId")), FString(TestStructureId));
	TestTrue(TEXT("응답 전엔 안 칠함"), Base->FindStructure(TestStructureId)->Color.IsNone());
	Respond(*Calls, FString::Printf(TEXT(R"({"painted":true,"tentId":"%s","structureId":"%s","color":"%s"})"),
		TestTentId, TestStructureId, *TestPaint.ToString()));
	TestEqual(TEXT("가구 색 = 서버 color"), Base->FindStructure(TestStructureId)->Color, TestPaint);

	// 치우기.
	TestTrue(TEXT("RemoveStructure 를 묻는다"), Base->RequestRemoveStructure(TestTentId, TestStructureId));
	TestNotNull(TEXT("응답 전엔 가구가 남아 있다"), Base->FindStructure(TestStructureId));
	Respond(*Calls, TEXT(R"({"ok":true})"));
	TestNull(TEXT("응답 뒤 가구가 사라진다"), Base->FindStructure(TestStructureId));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// ⑥ 배치 격자 — 초록(내 땅)·빨강(겹침)·파랑(땅 밖·한도·거점존 밖). 칸 수는 규칙 표에서 읽는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePlacementTest, "Snarehaven.Base.Place.GridOverlapLandLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePlacementTest::RunTest(const FString& Parameters)
{
	USnareBaseSubsystem* const Base = MakeBase(*this, nullptr);
	const FBaseRuleRow* const Rules = Base->GetRules();
	if (!TestNotNull(TEXT("규칙 행"), Rules))
	{
		return false;
	}
	const int32 Half = Rules->LandCells / 2;
	const FName Box(TEXT("item.storage_box"));
	const FName Bench(TEXT("item.workbench_adv"));
	const FName Table(TEXT("item.table_wood"));
	const FName TentItem(TEXT("item.tent"));
	auto Verdict = [Base](FName Item, FIntPoint Cell, int32 Rot = 0, bool bZone = true)
	{
		return static_cast<int32>(Base->EvaluatePlacement(Item, Cell, Rot, bZone));
	};
	const int32 Green = static_cast<int32>(ESnarePlacementVerdict::Green);
	const int32 Red = static_cast<int32>(ESnarePlacementVerdict::Red);
	const int32 Blue = static_cast<int32>(ESnarePlacementVerdict::Blue);

	TestEqual(TEXT("텐트 없으면 가구는 파랑"), Verdict(Box, FIntPoint(4, 0)), Blue);
	TestEqual(TEXT("첫 텐트는 초록"), Verdict(TentItem, FIntPoint(0, 0)), Green);
	TestEqual(TEXT("거점존 밖은 파랑"), Verdict(TentItem, FIntPoint(0, 0), 0, false), Blue);

	Base->AddTentMirrorForTest(MakeTent(TestTentId, FIntPoint(0, 0)));
	TestEqual(TEXT("땅 안 빈 칸은 초록"), Verdict(Box, FIntPoint(4, 0)), Green);
	TestEqual(TEXT("텐트 몸과 겹치면 빨강"), Verdict(Box, FIntPoint(1, 1)), Red);
	TestEqual(TEXT("땅 끝 안쪽(+Half-1)은 초록"), Verdict(Box, FIntPoint(Half - 1, 0)), Green);
	TestEqual(TEXT("땅 끝 바깥(+Half)은 파랑"), Verdict(Box, FIntPoint(Half, 0)), Blue);
	TestEqual(TEXT("땅 끝 안쪽(-Half)은 초록"), Verdict(Box, FIntPoint(-Half, 0)), Green);
	TestEqual(TEXT("땅 끝 바깥(-Half-1)은 파랑"), Verdict(Box, FIntPoint(-Half - 1, 0)), Blue);
	TestEqual(TEXT("2칸 가구가 땅 끝을 넘으면 파랑"), Verdict(Bench, FIntPoint(Half - 1, 0), 0), Blue);
	TestEqual(TEXT("90° 돌리면 들어가서 초록"), Verdict(Bench, FIntPoint(Half - 1, 0), 90), Green);

	// 놓인 가구 발자국 전체와 겹침.
	FSnareTentMirror Tent = *Base->FindTent(TestTentId);
	FSnareStructureMirror Placed;
	Placed.StructureId = TestStructureId;
	Placed.ItemID = Table;
	Placed.Cell = FIntPoint(4, 4);
	Tent.Structures.Add(Placed);
	USnareBaseSubsystem* const WithTable = MakeBase(*this, nullptr);
	WithTable->AddTentMirrorForTest(Tent);
	TestEqual(TEXT("탁자 둘째 칸과 겹치면 빨강"), static_cast<int32>(WithTable->EvaluatePlacement(Box, FIntPoint(5, 4), 0, true)), Red);
	TestEqual(TEXT("탁자 옆 칸은 초록"), static_cast<int32>(WithTable->EvaluatePlacement(Box, FIntPoint(6, 4), 0, true)), Green);

	// 가구 한도 — 한도-1 개면 초록, 한도면 파랑.
	FSnareTentMirror Full = MakeTent(TestTentId, FIntPoint(0, 0));
	for (int32 Index = 0; Index < Rules->FurnitureMax - 1; ++Index)
	{
		FSnareStructureMirror Fence;
		Fence.StructureId = FString::Printf(TEXT("fence_%d"), Index);
		Fence.ItemID = TEXT("item.fence_wood");
		Fence.Cell = FIntPoint(-Half + Index % Rules->LandCells, -Half + Index / Rules->LandCells);
		Full.Structures.Add(Fence);
	}
	USnareBaseSubsystem* const AlmostFull = MakeBase(*this, nullptr);
	AlmostFull->AddTentMirrorForTest(Full);
	TestEqual(TEXT("한도-1 개면 아직 초록"), static_cast<int32>(AlmostFull->EvaluatePlacement(Box, FIntPoint(4, 0), 0, true)), Green);
	FSnareStructureMirror Last;
	Last.StructureId = TEXT("fence_last");
	Last.ItemID = TEXT("item.fence_wood");
	Last.Cell = FIntPoint(Half - 1, Half - 1);
	Full.Structures.Add(Last);
	USnareBaseSubsystem* const AtLimit = MakeBase(*this, nullptr);
	AtLimit->AddTentMirrorForTest(Full);
	TestEqual(*FString::Printf(TEXT("가구 %d 개면 파랑"), Rules->FurnitureMax),
		static_cast<int32>(AtLimit->EvaluatePlacement(Box, FIntPoint(4, 0), 0, true)), Blue);

	// 텐트 — 내 다른 텐트 땅과 겹치면 빨강, 한도면 파랑.
	TestEqual(TEXT("둘째 텐트가 첫 땅과 겹치면 빨강"), Verdict(TentItem, FIntPoint(Rules->LandCells - 1, 0)), Red);
	TestEqual(TEXT("땅 하나 떨어지면 초록"), Verdict(TentItem, FIntPoint(Rules->LandCells, 0)), Green);
	for (int32 Index = 1; Index < Rules->TentMax; ++Index)
	{
		Base->AddTentMirrorForTest(MakeTent(FString::Printf(TEXT("tent_extra_%d"), Index), FIntPoint(Rules->LandCells * Index * 2, 0)));
	}
	TestEqual(*FString::Printf(TEXT("텐트 %d 개면 파랑"), Rules->TentMax), Verdict(TentItem, FIntPoint(-Rules->LandCells * 4, 0)), Blue);
	return true;
}

// ⑥-1 놓기 자리는 캐릭터 앞 — 3×3 텐트가 어느 쪽을 봐도 몸을 덮지 않고 가운데가 바라보는 줄 위 (INBOX M22 ②).
// 칸·틈은 시험 값 — 규칙 표 칸 크기가 바뀌어도 이 시험은 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePlaceAheadTest, "Snarehaven.Base.Place.AheadClearOfBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePlaceAheadTest::RunTest(const FString& Parameters)
{
	constexpr float Cell = 100.f;
	constexpr float Clearance = 100.f;
	const FVector2D Origins[] = { FVector2D(0.f, 0.f), FVector2D(37.f, -912.f), FVector2D(-1450.f, 260.f) };
	const FIntPoint Sizes[] = { FIntPoint(3, 3), FIntPoint(1, 1), FIntPoint(2, 1) };
	for (const FVector2D& Origin : Origins)
	{
		for (const FIntPoint& Size : Sizes)
		{
			for (int32 Degrees = 0; Degrees < 360; Degrees += 45)
			{
				const FVector2D Forward(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees)));
				const FIntPoint Anchor = SnareBuildRules::AnchorAhead(Origin, Forward, Size, Cell, Clearance);
				const FVector2D Min = FVector2D(Anchor) * Cell;
				const FVector2D Max = Min + FVector2D(Size) * Cell;
				const FVector2D Nearest(FMath::Clamp(Origin.X, Min.X, Max.X), FMath::Clamp(Origin.Y, Min.Y, Max.Y));
				const FVector2D ToCenter = (Min + Max) * 0.5f - Origin;
				const FString What = FString::Printf(TEXT("%s %dx%d %d°"), *Origin.ToString(), Size.X, Size.Y, Degrees);
				TestTrue(*FString::Printf(TEXT("%s 몸과 %.0fcm 이상 떨어짐"), *What, Clearance),
					FVector2D::Distance(Nearest, Origin) >= Clearance);
				TestTrue(*FString::Printf(TEXT("%s 앞쪽"), *What), FVector2D::DotProduct(ToCenter.GetSafeNormal(), Forward) > 0.7f);
				TestTrue(*FString::Printf(TEXT("%s 너무 멀지 않음(두 칸 이내 더)"), *What),
					ToCenter.Size() <= Clearance + FVector2D(Size).Size() * Cell * 0.5f + 2.f * Cell);
			}
		}
	}
	// 옛 계산(앞 두 칸을 앵커로)은 -X 를 보면 3×3 이 몸을 덮었다 — 그 자리는 이제 아니다.
	const FIntPoint Old = SnareBuildRules::WorldToCell(FVector(-200.f, 0.f, 0.f), Cell);
	TestEqual(TEXT("옛 앵커 -X 는 몸을 덮던 자리 (-2,0)"), Old, FIntPoint(-2, 0));
	TestNotEqual(TEXT("새 앵커는 다른 자리"), SnareBuildRules::AnchorAhead(FVector2D::ZeroVector, FVector2D(-1.f, 0.f), FIntPoint(3, 3), Cell, Clearance), Old);
	return true;
}

// ⑥-2 레퍼런스처럼 사냥 섬 빈 땅에도 텐트 (DESIGN 8.11 「놓는 곳」). 존은 게임모드로 가르고, 뭍만(바다·광산 굴 밖),
// 겹침은 같은 존끼리, 한도는 존을 합쳐 본다 — 서버 `PlaceTent` 와 같은 줄.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHuntTentTest, "Snarehaven.Base.Place.HuntIslandTent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHuntTentTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGameStateBase* const GameState = World->SpawnActor<AGameStateBase>(Params);
	World->SetGameState(GameState);

	TestEqual(TEXT("게임모드 없으면 못 짓는다"), USnareBaseBuildComponent::GetTentZone(World), FName(NAME_None));
	GameState->GameModeClass = ARanchGameMode::StaticClass();
	TestEqual(TEXT("거점 게임모드 = ranch"), USnareBaseBuildComponent::GetTentZone(World), SnareBaseZones::Ranch);
	GameState->GameModeClass = ASnarehavenGameMode::StaticClass();
	TestEqual(TEXT("사냥 게임모드 = hunt"), USnareBaseBuildComponent::GetTentZone(World), SnareBaseZones::Hunt);

	TestTrue(TEXT("바다 없는 맵은 막지 않는다"), USnareBaseBuildComponent::IsBuildAllowedAt(World, FVector(9000.f, 0.f, 0.f)));
	ASeaActor* const Sea = World->SpawnActor<ASeaActor>(Params);
	// 3×3 노드 거리장(2000cm 간격) — 가운데 노드만 뭍(-1000), 둘레는 바다
	Sea->FieldOriginCm = FVector2D(-2000.0, -2000.0);
	Sea->CoastCellCm = 2000.f;
	Sea->CoastFieldSize = FIntPoint(3, 3);
	Sea->CoastField = { 1000, 1000, 1000, 1000, -1000, 1000, 1000, 1000, 1000 };
	TestTrue(TEXT("섬 가운데 뭍은 된다"), USnareBaseBuildComponent::IsBuildAllowedAt(World, FVector::ZeroVector));
	TestFalse(TEXT("바다 위는 안 된다"), USnareBaseBuildComponent::IsBuildAllowedAt(World, FVector(3000.f, 0.f, 0.f)));
	// 광산 굴은 같은 맵의 먼 곳 (DESIGN 8.16 굴 안 — 바다 벽 너머 X 70000) — 거리장 밖이라 뭍이 아니다.
	TestFalse(TEXT("광산 굴 안은 안 된다"), USnareBaseBuildComponent::IsBuildAllowedAt(World, FVector(70000.f, 0.f, 0.f)));

	// 나무(손도끼로 캐는 채집물)가 발자국 안에 서 있으면 막고, 맨손 꽃은 안 막는다.
	auto SpawnGatherable = [World, &Params](const FVector& Where, FName Tool)
	{
		AGatherableActor* const Gatherable = World->SpawnActor<AGatherableActor>(AGatherableActor::StaticClass(), FTransform(Where), Params);
		FGatherableRow Row;
		Row.GrantItemID = TEXT("item.wood");
		Row.GrantCount = 1;
		Row.RequiredToolItemID = Tool;
		Gatherable->InitializeFromRow(Row);
	};
	const FVector2D TentHalf(100.0, 100.0);
	SpawnGatherable(FVector(-300.f, 0.f, 0.f), NAME_None);
	TestFalse(TEXT("꽃 위엔 놓는다"), USnareBaseBuildComponent::IsBlockedByGatherable(World, FVector(-300.f, 0.f, 0.f), TentHalf));
	SpawnGatherable(FVector(350.f, 0.f, 0.f), TEXT("item.hatchet"));
	TestTrue(TEXT("나무 위엔 못 놓는다"), USnareBaseBuildComponent::IsBlockedByGatherable(World, FVector(300.f, 0.f, 0.f), TentHalf));
	TestFalse(TEXT("나무 옆 빈 땅은 된다"), USnareBaseBuildComponent::IsBlockedByGatherable(World, FVector(600.f, 0.f, 0.f), TentHalf));
	DestroyTestWorld(World);

	USnareBaseSubsystem* const Base = MakeBase(*this, nullptr);
	const FBaseRuleRow* const Rules = Base->GetRules();
	if (!TestNotNull(TEXT("규칙 행"), Rules))
	{
		return false;
	}
	const FName TentItem(TEXT("item.tent"));
	const FName Box(TEXT("item.storage_box"));
	auto Verdict = [Base](FName Item, FIntPoint Cell, FName Zone)
	{
		return static_cast<int32>(Base->EvaluatePlacement(Item, Cell, 0, true, Zone));
	};
	Base->AddTentMirrorForTest(MakeTent(TestTentId, FIntPoint(0, 0)));
	TestEqual(TEXT("거점 텐트와 같은 칸이라도 섬이면 초록"), Verdict(TentItem, FIntPoint(0, 0), SnareBaseZones::Hunt),
		static_cast<int32>(ESnarePlacementVerdict::Green));
	TestEqual(TEXT("거점에선 여전히 빨강"), Verdict(TentItem, FIntPoint(0, 0), SnareBaseZones::Ranch),
		static_cast<int32>(ESnarePlacementVerdict::Red));
	TestNull(TEXT("거점 텐트 땅은 섬 칸이 아니다"), Base->FindTentOwningCell(FIntPoint(1, 0), SnareBaseZones::Hunt));
	TestEqual(TEXT("섬엔 내 땅이 없어 가구는 파랑"), Verdict(Box, FIntPoint(4, 0), SnareBaseZones::Hunt),
		static_cast<int32>(ESnarePlacementVerdict::Blue));

	FSnareTentMirror HuntTent = MakeTent(TEXT("tent_hunt"), FIntPoint(0, 0));
	HuntTent.Zone = SnareBaseZones::Hunt;
	Base->AddTentMirrorForTest(HuntTent);
	TestEqual(TEXT("섬 텐트 땅 안 가구는 초록"), Verdict(Box, FIntPoint(4, 0), SnareBaseZones::Hunt),
		static_cast<int32>(ESnarePlacementVerdict::Green));
	TestEqual(TEXT("섬 텐트 땅은 섬 텐트 것"), Base->FindTentOwningCell(FIntPoint(1, 0), SnareBaseZones::Hunt)->TentId, FString(TEXT("tent_hunt")));
	for (int32 Index = 2; Index < Rules->TentMax; ++Index)
	{
		Base->AddTentMirrorForTest(MakeTent(FString::Printf(TEXT("tent_more_%d"), Index), FIntPoint(Rules->LandCells * Index * 2, 0)));
	}
	TestEqual(*FString::Printf(TEXT("존을 합쳐 %d 개면 섬에서도 파랑"), Rules->TentMax),
		Verdict(TentItem, FIntPoint(Rules->LandCells * 8, 0), SnareBaseZones::Hunt), static_cast<int32>(ESnarePlacementVerdict::Blue));

	// 서버 저장값 복원 — `zone` 이 없는 옛 텐트는 거점, 있으면 그 글자 그대로.
	USnareBaseSubsystem* const Restored = MakeBase(*this, nullptr);
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TCHAR* Json : { TEXT("{\"tentId\":\"old\",\"x\":0,\"y\":0,\"placedAtUtc\":\"2026-09-25T00:00:00.000Z\"}"),
		TEXT("{\"tentId\":\"isl\",\"zone\":\"hunt\",\"x\":0,\"y\":0,\"placedAtUtc\":\"2026-09-25T00:00:00.000Z\"}") })
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	Restored->ApplyBackendTents(Values);
	TestEqual(TEXT("zone 없는 옛 텐트는 거점"), Restored->FindTent(TEXT("old"))->Zone, SnareBaseZones::Ranch);
	TestEqual(TEXT("섬 텐트는 hunt"), Restored->FindTent(TEXT("isl"))->Zone, SnareBaseZones::Hunt);
	return true;
}

// 루프#1437 흠: 목장 동물 칸 발판(말뚝 사이) 위에 텐트가 초록으로 섰다 — 칸 바닥에 걸치면 막고, 잠긴(숨긴) 칸·맞닿기는 안 막는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseNotOnRanchSlotTest, "Snarehaven.Base.Place.NotOnRanchSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseNotOnRanchSlotTest::RunTest(const FString& Parameters)
{
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("기본 큐브"), Cube))
	{
		return false;
	}
#if WITH_EDITOR
	FStaticMeshCompilingManager::Get().FinishCompilation({ Cube });
#endif
	// 시험 전용 값 — 칸 말뚝 (250, 0), 붙은 발판 200×200cm → 칸 바닥 X 150~350 · Y -100~100. 발자국 반폭 100.
	const FVector SlotAt(250.f, 0.f, 0.f);
	const FVector2D Half(100.0, 100.0);
	UWorld* const World = CreateTestWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(ARanchSlotActor::StaticClass(), FTransform(SlotAt), Params);
	AStaticMeshActor* const Plank = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(SlotAt), Params);
	Plank->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Plank->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Plank->SetActorScale3D(FVector(2.f, 2.f, 0.1f));
	Plank->AttachToActor(Slot, FAttachmentTransformRules::KeepWorldTransform);

	TestTrue(TEXT("말뚝 곁 발판 위는 못 놓는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(World, FVector(250.f, 50.f, 0.f), Half));
	TestTrue(TEXT("말뚝에서 먼 발판 끝(Y 90)에 걸쳐도 못 놓는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(World, FVector(250.f, 190.f, 0.f), Half));
	TestTrue(TEXT("발판 앞 가장자리에 10cm 걸쳐도 못 놓는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(World, FVector(60.f, 0.f, 0.f), Half));
	TestFalse(TEXT("발판 앞에 맞닿기만 하면 놓는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(World, FVector(50.f, 0.f, 0.f), Half));
	TestFalse(TEXT("칸 밖 빈 땅은 놓는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(World, FVector(-500.f, 0.f, 0.f), Half));
	Slot->SetActorHiddenInGame(true);
	TestFalse(TEXT("잠긴(숨긴) 칸은 안 막는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(World, FVector(250.f, 50.f, 0.f), Half));
	DestroyTestWorld(World);
	TestFalse(TEXT("월드 없으면 안 막는다"), USnareBaseBuildComponent::IsBlockedByRanchSlot(nullptr, FVector::ZeroVector, Half));
	return true;
}

// ⑦ 나무 베기 — 손도끼가 있어야 베이고, 설계서 타격 수만큼 쳐야 쓰러지며, 개수는 표 범위(설계서 1~4)에서 굴리며, 벤 나무는 사라지지 않고 다시 자란다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseTreeTest, "Snarehaven.Base.Tree.ToolRollRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseTreeTest::RunTest(const FString& Parameters)
{
	FString Craft;
	FString BaseText;
	if (!LoadDesignSections(*this, Craft, BaseText))
	{
		return false;
	}
	const int32 DesignMin = CaptureInt(Craft, TEXT(R"(한 그루 \*\*(\d+)~(\d+)개\*\*)"), 1);
	const int32 DesignMax = CaptureInt(Craft, TEXT(R"(한 그루 \*\*(\d+)~(\d+)개\*\*)"), 2);
	const int32 DesignRespawn = CaptureInt(Craft, TEXT(R"(\*\*(\d+)초\*\* 뒤 다시 자람)"));
	const int32 DesignResinMax = CaptureInt(Craft, TEXT(R"(덤 수지 \*\*0~(\d+)개\*\*)"));
	// 쓰러지기까지 타격 수 (8.10 「나무 **3번**·향나무 **4번** 쳐야 쓰러지고」).
	const int32 DesignTreeHits = CaptureInt(Craft, TEXT(R"(나무 \*\*(\d+)번\*\*·향나무 \*\*(\d+)번\*\*)"), 1);
	const int32 DesignCedarHits = CaptureInt(Craft, TEXT(R"(나무 \*\*(\d+)번\*\*·향나무 \*\*(\d+)번\*\*)"), 2);
	TestTrue(TEXT("설계서 타격 수를 읽었다"), DesignTreeHits > 1 && DesignCedarHits > 1);

	const UDataTable* const GatherTable = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), GatherTable))
	{
		return false;
	}
	TestNull(TEXT("수지 나무 더미는 없다 (수지는 벤 나무 덤)"), GatherTable->FindRow<FGatherableRow>(TEXT("Gatherable_ResinTree"), TEXT("TreeTest"), false));

	UWorld* const World = CreateTestWorld();
	int32 TreeRows = 0;
	for (const TPair<FName, uint8*>& Pair : GatherTable->GetRowMap())
	{
		const FGatherableRow& Row = *reinterpret_cast<const FGatherableRow*>(Pair.Value);
		if (Row.RequiredToolItemID != FName(TEXT("item.hatchet")))   // 광맥(곡괭이)은 아래 ⑦-2 가 본다
		{
			continue;
		}
		++TreeRows;
		const FString Name = Pair.Key.ToString();
		TestEqual(*FString::Printf(TEXT("[%s] 최소 개수 = 설계서"), *Name), Row.GrantCount, DesignMin);
		TestEqual(*FString::Printf(TEXT("[%s] 최대 개수 = 설계서"), *Name), Row.GrantCountMax, DesignMax);
		TestEqual(*FString::Printf(TEXT("[%s] 다시 자람 초 = 설계서"), *Name), FMath::RoundToInt32(Row.RespawnSeconds), DesignRespawn);
		const bool bCedar = Row.GrantItemID == FName(TEXT("item.cedar"));
		TestEqual(*FString::Printf(TEXT("[%s] 쓰러지기까지 타격 수 = 설계서"), *Name), Row.HitsToFell, bCedar ? DesignCedarHits : DesignTreeHits);
		AddInfo(FString::Printf(TEXT("[TreeHits] %s %d번 · %d~%d개"), *Name, Row.HitsToFell, Row.GrantCount, Row.GrantCountMax));

		AGatherableActor* const Tree = World->SpawnActor<AGatherableActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 스폰"), *Name), Tree) || !TestTrue(TEXT("행 적용"), Tree->InitializeFromRow(Row)))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("[%s] 맨손으로는 못 벤다"), *Name), Tree->CanGatherWithTool(NAME_None));
		TestTrue(*FString::Printf(TEXT("[%s] 손도끼면 벤다"), *Name), Tree->CanGatherWithTool(Row.RequiredToolItemID));
		// DESIGN 13.5 — 전기톱은 손도끼 대신 벨 수 있고(+1), 마체테는 나무를 못 벤다.
		TestTrue(*FString::Printf(TEXT("[%s] 전기톱으로도 벤다"), *Name), Tree->CanGatherWithTool(TEXT("item.chainsaw")));
		TestFalse(*FString::Printf(TEXT("[%s] 마체테로는 못 벤다"), *Name), Tree->CanGatherWithTool(TEXT("item.machete")));

		int32 SeenMin = MAX_int32;
		int32 SeenMax = MIN_int32;
		for (int32 Try = 0; Try < TestRollTries; ++Try)
		{
			Tree->Respawn();
			SeenMin = FMath::Min(SeenMin, Tree->GetGrantCount());
			SeenMax = FMath::Max(SeenMax, Tree->GetGrantCount());
		}
		TestEqual(*FString::Printf(TEXT("[%s] 굴림 최솟값이 범위 끝"), *Name), SeenMin, DesignMin);
		TestEqual(*FString::Printf(TEXT("[%s] 굴림 최댓값이 범위 끝"), *Name), SeenMax, DesignMax);

		// 덤 수지 (8.10) — 0~최대를 굴려 양 끝이 다 나온다. 0 은 「이번엔 수지 없음」.
		TestEqual(*FString::Printf(TEXT("[%s] 덤 = 수지"), *Name), Row.ExtraItemID, FName(TEXT("item.resin")));
		TestEqual(*FString::Printf(TEXT("[%s] 덤 최대 = 설계서"), *Name), Row.ExtraCountMax, DesignResinMax);
		int32 ResinMin = MAX_int32;
		int32 ResinMax = MIN_int32;
		for (int32 Try = 0; Try < TestRollTries; ++Try)
		{
			Tree->Respawn();
			ResinMin = FMath::Min(ResinMin, Tree->GetExtraCount());
			ResinMax = FMath::Max(ResinMax, Tree->GetExtraCount());
		}
		TestEqual(*FString::Printf(TEXT("[%s] 덤 굴림 최솟값 0"), *Name), ResinMin, 0);
		TestEqual(*FString::Printf(TEXT("[%s] 덤 굴림 최댓값이 범위 끝"), *Name), ResinMax, DesignResinMax);

		TestTrue(*FString::Printf(TEXT("[%s] 벤다"), *Name), Tree->Gather());
		TestTrue(*FString::Printf(TEXT("[%s] 벤 나무는 사라지지 않는다 (다시 자람)"), *Name), IsValid(Tree));
		TestFalse(*FString::Printf(TEXT("[%s] 벤 직후엔 다시 못 벤다"), *Name), Tree->CanGather());
		Tree->Respawn();
		TestTrue(*FString::Printf(TEXT("[%s] 다시 자라면 또 벤다"), *Name), Tree->CanGatherWithTool(Row.RequiredToolItemID));
	}
	TestTrue(TEXT("손도끼 나무 행이 있다"), TreeRows > 0);
	DestroyTestWorld(World);
	return true;
}

// ⑦-2 광맥 (DESIGN 8.16) — 곡괭이 등급 표 그대로 캐지고, 개수·다시 나기 초가 설계서와 같다. 제련소 행은 30초 타이머.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseOreTest, "Snarehaven.Base.Mining.OreVeins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseOreTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const FString Mining = ExtractSection(Doc, TEXT("### 8.16"));
	const int32 DesignMin = CaptureInt(Mining, TEXT(R"(한 번에 \*\*(\d+)~(\d+)\*\*개)"), 1);
	const int32 DesignMax = CaptureInt(Mining, TEXT(R"(한 번에 \*\*(\d+)~(\d+)\*\*개)"), 2);
	const int32 DesignRespawn = CaptureInt(Mining, TEXT(R"(다시 나기 \*\*(\d+)초\*\*)"));
	const int32 DesignSmelt = CaptureInt(Mining, TEXT(R"(한 번에 1개 \*\*(\d+)초\*\*)"));

	const UDataTable* const GatherTable = LoadObject<UDataTable>(nullptr, GatherableTablePath);
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_GatherableTable 로드"), GatherTable) || !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	const FItemRow* const Smelter = ItemTable->FindRow<FItemRow>(TEXT("Item_Smelter"), TEXT("OreTest"));
	if (TestNotNull(TEXT("제련소 행"), Smelter))
	{
		TestTrue(TEXT("제련소 = Smelter 가구"), Smelter->PlaceableFunction == EPlaceableFunction::Smelter);
		TestEqual(TEXT("제련 초 = 설계서"), Smelter->CookSeconds, DesignSmelt);
	}

	// 곡괭이 등급 (8.16 「곡괭이 등급」) — 맨손·손도끼는 둘 다 못 캔다.
	struct FToolCase { const TCHAR* Tool; bool bBasic; bool bAdvanced; };
	const FToolCase Cases[] = {
		{ TEXT("item.pickaxe_basic"), true, false }, { TEXT("item.pickaxe_advanced"), true, true },
		{ TEXT("item.pickaxe_electric"), true, true }, { TEXT("item.hatchet"), false, false }, { TEXT(""), false, false } };
	UWorld* const World = CreateTestWorld();
	for (const TCHAR* RowName : { TEXT("Gatherable_OreBasic"), TEXT("Gatherable_OreAdvanced") })
	{
		const FGatherableRow* const Row = GatherTable->FindRow<FGatherableRow>(RowName, TEXT("OreTest"));
		if (!TestNotNull(RowName, Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("[%s] 최소 = 설계서"), RowName), Row->GrantCount, DesignMin);
		TestEqual(*FString::Printf(TEXT("[%s] 최대 = 설계서"), RowName), Row->GrantCountMax, DesignMax);
		TestEqual(*FString::Printf(TEXT("[%s] 다시 나기 = 설계서"), RowName), FMath::RoundToInt32(Row->RespawnSeconds), DesignRespawn);
		TestEqual(*FString::Printf(TEXT("[%s] +1 도구 = 전기 곡괭이"), RowName), Row->BonusToolItemID, FName(TEXT("item.pickaxe_electric")));
		AGatherableActor* const Vein = World->SpawnActor<AGatherableActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!TestTrue(TEXT("행 적용"), Vein != nullptr && Vein->InitializeFromRow(*Row)))
		{
			continue;
		}
		const bool bAdvancedVein = FCString::Strcmp(RowName, TEXT("Gatherable_OreAdvanced")) == 0;
		for (const FToolCase& Case : Cases)
		{
			const FName Tool = *Case.Tool ? FName(Case.Tool) : NAME_None;
			TestEqual(*FString::Printf(TEXT("[%s] %s 로 캐기"), RowName, *Tool.ToString()),
				Vein->CanGatherWithTool(Tool), bAdvancedVein ? Case.bAdvanced : Case.bBasic);
		}
	}
	// 들판 광석 바위 6종 (8.16 「들판 광석 바위」) — 종류마다 한 행, 광맥과 같은 곡괭이 등급 · 세 번 쳐야 무너짐 · 광맥과 같은 다시 나기.
	struct FRockCase { const TCHAR* Row; const TCHAR* Chunk; bool bAdvancedRock; };
	const FRockCase Rocks[] = {
		{ TEXT("Gatherable_OreRockCoal"), TEXT("item.coal_chunk"), false }, { TEXT("Gatherable_OreRockCopper"), TEXT("item.copper_chunk"), false },
		{ TEXT("Gatherable_OreRockIron"), TEXT("item.iron_chunk"), false }, { TEXT("Gatherable_OreRockTitanium"), TEXT("item.titanium_chunk"), true },
		{ TEXT("Gatherable_OreRockSilver"), TEXT("item.silver_chunk"), true }, { TEXT("Gatherable_OreRockGold"), TEXT("item.gold_chunk"), true } };
	for (const FRockCase& Rock : Rocks)
	{
		const FGatherableRow* const Row = GatherTable->FindRow<FGatherableRow>(Rock.Row, TEXT("OreTest"));
		if (!TestNotNull(Rock.Row, Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("[%s] 지급 = 그 종류 덩어리"), Rock.Row), Row->GrantItemID, FName(Rock.Chunk));
		TestEqual(*FString::Printf(TEXT("[%s] 최소 = 설계서"), Rock.Row), Row->GrantCount, DesignMin);
		TestEqual(*FString::Printf(TEXT("[%s] 최대 = 설계서"), Rock.Row), Row->GrantCountMax, DesignMax);
		TestEqual(*FString::Printf(TEXT("[%s] 다시 나기 = 설계서"), Rock.Row), FMath::RoundToInt32(Row->RespawnSeconds), DesignRespawn);
		TestEqual(*FString::Printf(TEXT("[%s] 세 번 쳐야 무너짐"), Rock.Row), Row->HitsToFell, 3);
		TestTrue(*FString::Printf(TEXT("[%s] 몸을 막는 줄기"), Rock.Row), Row->TrunkCollisionCm.X > 0.f && Row->TrunkCollisionCm.Y > 0.f);
		AGatherableActor* const RockActor = World->SpawnActor<AGatherableActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!TestTrue(TEXT("행 적용"), RockActor != nullptr && RockActor->InitializeFromRow(*Row)))
		{
			continue;
		}
		for (const FToolCase& Case : Cases)
		{
			const FName Tool = *Case.Tool ? FName(Case.Tool) : NAME_None;
			TestEqual(*FString::Printf(TEXT("[%s] %s 로 캐기"), Rock.Row, *Tool.ToString()),
				RockActor->CanGatherWithTool(Tool), Rock.bAdvancedRock ? Case.bAdvanced : Case.bBasic);
		}
		const FVector Hitter(500.f, 0.f, 0.f);
		TestFalse(*FString::Printf(TEXT("[%s] 첫 번째 침은 흔들리기만"), Rock.Row), RockActor->TakeHit(Hitter));
		TestFalse(*FString::Printf(TEXT("[%s] 두 번째 침도 흔들리기만"), Rock.Row), RockActor->TakeHit(Hitter));
		TestTrue(*FString::Printf(TEXT("[%s] 세 번째 침이 무너뜨림"), Rock.Row), RockActor->TakeHit(Hitter));
		TestEqual(*FString::Printf(TEXT("[%s] 지급 아이템"), Rock.Row), RockActor->GetGrantItemID(), FName(Rock.Chunk));
	}
	DestroyTestWorld(World);
	return true;
}

// ⑧ 페인트 7 (DESIGN 13.8) — 표의 색이 설계서 「칠한 색」 그대로이고, 칠한 가구 액터가 그 색을 입는다(나무색·집 색은 그대로).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePaintTest, "Snarehaven.Base.Paint.TableAndTint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePaintTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FString IdLine;
	FString HexLine;
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("| 아이템 |")) && Line.Contains(TEXT("item.paint_white"))) { IdLine = Line; }
		if (Line.StartsWith(TEXT("| 칠한 색 |"))) { HexLine = Line; }
	}
	TArray<FString> Ids;
	for (FRegexMatcher M(FRegexPattern(TEXT(R"(`(item\.paint_[a-z]+)`)")), IdLine); M.FindNext();) { Ids.Add(M.GetCaptureGroup(1)); }
	TArray<FString> Hexes;
	for (FRegexMatcher M(FRegexPattern(TEXT(R"(`#([0-9A-F]{6})`)")), HexLine); M.FindNext();) { Hexes.Add(M.GetCaptureGroup(1)); }
	TestEqual(TEXT("설계서 페인트 7"), Ids.Num(), 7);
	if (!TestEqual(TEXT("색도 7"), Hexes.Num(), Ids.Num()))
	{
		return false;
	}

	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}
	int32 PaintRows = 0;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || Row->PaintColorHex.IsEmpty())
		{
			continue;
		}
		++PaintRows;
		const int32 Index = Ids.IndexOfByKey(Row->ItemID.ToString());
		if (TestTrue(*FString::Printf(TEXT("%s 는 설계서 페인트"), *Row->ItemID.ToString()), Index != INDEX_NONE))
		{
			TestEqual(*FString::Printf(TEXT("%s 색"), *Row->ItemID.ToString()), Row->PaintColorHex, Hexes[Index]);
			TestEqual(*FString::Printf(TEXT("%s 판매가 0"), *Row->ItemID.ToString()), Row->SellPrice, 0);
		}
	}
	TestEqual(TEXT("표의 페인트 행 = 설계서 수"), PaintRows, Ids.Num());

	// 액터 — 벤치를 세우고 칠한다. 색 해석은 액터가 표에서 한다.
	UWorld* const World = CreateTestWorld();
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	ASnarePlacedStructureActor* const Bench = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("벤치 액터"), Bench))
	{
		Bench->InitStructure(TEXT("item.bench"), TestTentId, TestStructureId, FString(), 0, 100.f);
		Bench->FinishSpawning(FTransform::Identity);
		const FLinearColor Wood = Bench->GetTint();
		Bench->SetPaint(FName(*Ids[3]));
		TestEqual(TEXT("칠한 색 = 표 색"), Bench->GetTint().ToFColor(true), FColor::FromHex(Hexes[3]));
		TestEqual(TEXT("복제할 페인트 ID"), Bench->GetPaintItemID(), FName(*Ids[3]));
		Bench->SetPaint(NAME_None);
		TestEqual(TEXT("안 칠함이면 나무색"), Bench->GetTint(), Wood);
	}
	DestroyTestWorld(World);
	return true;
}

// ⑨ 가구 집어 칠하기 (DESIGN 13.8 🟡) — 내 가구만 고른 대상이 되고, 「칠하기」 한 번이 그 가구로 `PaintStructure` 를 묻는다.
// 응답 전엔 사본·액터 색이 그대로다 (CLAUDE.md 8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePaintPickTest, "Snarehaven.Base.Paint.PickFurniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePaintPickTest::RunTest(const FString& Parameters)
{
	const TCHAR* const OtherTentId = TEXT("tent_test_other");
	const TCHAR* const OtherStructureId = TEXT("st_test_other");
	const TCHAR* const StrayStructureId = TEXT("st_test_stray");
	const FName TestPaint(TEXT("item.paint_red"));

	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBaseSubsystem* const Base = MakeBase(*this, MakeTestBackend());

	// 내 텐트(시각 있음) + 벤치, 남의 보관함 사본(시각 없음) + 벤치.
	FSnareTentMirror Mine = MakeTent(TestTentId, FIntPoint(0, 0));
	FSnareStructureMirror MyBench;
	MyBench.StructureId = TestStructureId;
	MyBench.ItemID = TEXT("item.bench");
	Mine.Structures.Add(MyBench);
	Base->AddTentMirrorForTest(Mine);
	FSnareTentMirror Other = MakeTent(OtherTentId, FIntPoint(50, 50));
	Other.PlacedAtUtc.Empty();
	FSnareStructureMirror OtherBench = MyBench;
	OtherBench.StructureId = OtherStructureId;
	Other.Structures.Add(OtherBench);
	Base->AddTentMirrorForTest(Other);

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, FString(), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	ASnarePlacedStructureActor* const Bench = Spawn(TEXT("item.bench"), TestTentId, TestStructureId);
	ASnarePlacedStructureActor* const ForeignBench = Spawn(TEXT("item.bench"), OtherTentId, OtherStructureId);
	ASnarePlacedStructureActor* const StrayBench = Spawn(TEXT("item.bench"), TestTentId, StrayStructureId);
	ASnarePlacedStructureActor* const TentBody = Spawn(TEXT("item.tent"), TestTentId, FString());
	ASnarePlacedStructureActor* const Ghost = Spawn(TEXT("item.bench"), TestTentId, TestStructureId);
	Ghost->SetPreviewVerdict(ESnarePlacementVerdict::Green);

	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("화면"), Screen))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}
	Screen->Setup(nullptr, Base);

	// 가구 아닌 것·남의 가구는 안 골라진다 — 대상도 그대로 비어 있다.
	TestFalse(TEXT("빈 것은 안 골라짐"), Screen->PickPaintTarget(nullptr));
	TestFalse(TEXT("남의 가구는 안 골라짐"), Screen->PickPaintTarget(ForeignBench));
	TestFalse(TEXT("사본에 없는 가구는 안 골라짐"), Screen->PickPaintTarget(StrayBench));
	TestFalse(TEXT("텐트 몸은 안 골라짐"), Screen->PickPaintTarget(TentBody));
	TestFalse(TEXT("미리보기는 안 골라짐"), Screen->PickPaintTarget(Ghost));
	TestNull(TEXT("고른 대상 없음"), Screen->GetPaintTarget());
	TestFalse(TEXT("대상 없이는 안 묻는다"), Screen->PressPaint(TestPaint));
	TestEqual(TEXT("서버에 간 부탁 0"), Calls->Num(), 0);

	// 내 가구를 집는다 → 고른 대상 = 그 가구, 칠하기 쪽이 열린다.
	TestTrue(TEXT("내 가구는 골라짐"), Screen->PickPaintTarget(Bench));
	TestEqual(TEXT("고른 대상 = 그 가구"), Screen->GetPaintTarget(), Bench);
	TestEqual(TEXT("칠하기 쪽"), Screen->GetPage(), ESnareBaseScreenPage::Paint);
	TestFalse(TEXT("고른 뒤 남의 가구를 눌러도 안 바뀜"), Screen->PickPaintTarget(ForeignBench));
	TestEqual(TEXT("대상 그대로"), Screen->GetPaintTarget(), Bench);

	// 「칠하기」 한 번 → 인자 확인. 응답 전엔 색 그대로.
	TestTrue(TEXT("칠하기가 서버에 묻는다"), Screen->PressPaint(TestPaint));
	if (TestEqual(TEXT("부탁 1건"), Calls->Num(), 1))
	{
		TestEqual(TEXT("PaintStructure 함수"), Calls->Last().FunctionName, FString(TEXT("PaintStructure")));
		TestEqual(TEXT("tentId 인자"), ArgString(Calls->Last(), TEXT("tentId")), FString(TestTentId));
		TestEqual(TEXT("structureId 인자"), ArgString(Calls->Last(), TEXT("structureId")), FString(TestStructureId));
		TestEqual(TEXT("paintId 인자"), ArgString(Calls->Last(), TEXT("paintId")), TestPaint.ToString());
	}
	TestTrue(TEXT("응답 전 사본 색 그대로"), Base->FindStructure(TestStructureId)->Color.IsNone());
	TestTrue(TEXT("응답 전 액터 색 그대로"), Bench->GetPaintItemID().IsNone());
	Respond(*Calls, FString::Printf(TEXT(R"({"painted":true,"tentId":"%s","structureId":"%s","color":"%s"})"),
		TestTentId, TestStructureId, *TestPaint.ToString()));
	TestEqual(TEXT("응답 뒤 사본 색 = 서버 color"), Base->FindStructure(TestStructureId)->Color, TestPaint);

	Screen->Close();
	TestNull(TEXT("닫으면 대상 없음"), Screen->GetPaintTarget());
	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

// ⑨-2 고른 가구 테두리 (M13-24) — 고르면 그 가구 메시에만 오버레이, 다른 가구를 고르면 앞 것은 빠지고, 닫으면 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePaintOutlineTest, "Snarehaven.Base.Paint.SelectedOutline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePaintOutlineTest::RunTest(const FString& Parameters)
{
	const TCHAR* const SecondStructureId = TEXT("st_test_second");
	const TCHAR* const OtherTentId = TEXT("tent_test_other");
	const TCHAR* const OtherStructureId = TEXT("st_test_other");

	USnareBaseSubsystem* const Base = MakeBase(*this, MakeTestBackend());
	FSnareTentMirror Mine = MakeTent(TestTentId, FIntPoint(0, 0));
	FSnareStructureMirror MyBench;
	MyBench.StructureId = TestStructureId;
	MyBench.ItemID = TEXT("item.bench");
	Mine.Structures.Add(MyBench);
	FSnareStructureMirror MySecond = MyBench;
	MySecond.StructureId = SecondStructureId;
	Mine.Structures.Add(MySecond);
	Base->AddTentMirrorForTest(Mine);
	FSnareTentMirror Other = MakeTent(OtherTentId, FIntPoint(50, 50));
	Other.PlacedAtUtc.Empty();
	FSnareStructureMirror OtherBench = MyBench;
	OtherBench.StructureId = OtherStructureId;
	Other.Structures.Add(OtherBench);
	Base->AddTentMirrorForTest(Other);

	UWorld* const World = CreateTestWorld();
	TArray<ASnarePlacedStructureActor*> All;
	auto Spawn = [World, &All](FName ItemID, const FString& TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, FString(), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		All.Add(Actor);
		return Actor;
	};
	ASnarePlacedStructureActor* const Bench = Spawn(TEXT("item.bench"), TestTentId, TestStructureId);
	ASnarePlacedStructureActor* const Second = Spawn(TEXT("item.bench"), TestTentId, SecondStructureId);
	ASnarePlacedStructureActor* const ForeignBench = Spawn(TEXT("item.bench"), OtherTentId, OtherStructureId);
	Spawn(TEXT("item.tent"), TestTentId, FString());
	auto CountOutlined = [&All]()
	{
		return All.FilterByPredicate([](const ASnarePlacedStructureActor* Actor) { return Actor->HasSelectedOutline(); }).Num();
	};

	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("화면"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->Setup(nullptr, Base);
	TestEqual(TEXT("고르기 전 테두리 0"), CountOutlined(), 0);

	TestTrue(TEXT("내 가구 고름"), Screen->PickPaintTarget(Bench));
	TestTrue(TEXT("고른 가구에 테두리"), Bench->HasSelectedOutline());
	TestEqual(TEXT("테두리는 한 가구뿐"), CountOutlined(), 1);

	TestFalse(TEXT("남의 가구는 안 골라짐"), Screen->PickPaintTarget(ForeignBench));
	TestFalse(TEXT("남의 가구는 테두리 없음"), ForeignBench->HasSelectedOutline());
	TestTrue(TEXT("앞 테두리 그대로"), Bench->HasSelectedOutline());

	TestTrue(TEXT("다른 내 가구 고름"), Screen->PickPaintTarget(Second));
	TestFalse(TEXT("앞 가구 테두리 빠짐"), Bench->HasSelectedOutline());
	TestTrue(TEXT("새 가구에 테두리"), Second->HasSelectedOutline());
	TestEqual(TEXT("여전히 한 가구뿐"), CountOutlined(), 1);

	Screen->Close();
	TestEqual(TEXT("풀리면 테두리 0"), CountOutlined(), 0);
	DestroyTestWorld(World);
	return true;
}

// ⑨-3 칠하기·테두리 3층 스위치 (M13-33) — 앞에 벤치 둘: 하나는 표 색으로 칠해지고 하나는 테두리가 켜진다. 로컬 전용.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePaintDemoTest, "Snarehaven.Base.Paint.DemoSwitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePaintDemoTest::RunTest(const FString& Parameters)
{
	const FName TestPaint(TEXT("item.paint_red"));
	const FVector Forward(1.f, 0.f, 0.f);

	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	const TArray<ASnarePlacedStructureActor*> Demo =
		USnareBaseBuildComponent::SpawnPaintDemo(World, FVector::ZeroVector, Forward, TestPaint, 100.f);
	if (TestEqual(TEXT("벤치 둘"), Demo.Num(), 2))
	{
		const FItemRow* PaintRow = nullptr;
		for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			PaintRow = Row != nullptr && Row->ItemID == TestPaint ? Row : PaintRow;
		}
		for (const ASnarePlacedStructureActor* const Bench : Demo)
		{
			TestEqual(TEXT("벤치 행"), Bench->GetItemID(), FName(TEXT("item.bench")));
			TestFalse(TEXT("로컬 전용"), Bench->GetIsReplicated());
			TestTrue(TEXT("앞에 선다"), FVector::DotProduct(Bench->GetActorLocation(), Forward) > 0.f);
		}
		TestEqual(TEXT("칠한 벤치 페인트 ID"), Demo[0]->GetPaintItemID(), TestPaint);
		if (TestNotNull(TEXT("페인트 행"), PaintRow))
		{
			TestEqual(TEXT("칠한 색 = 표 색"), Demo[0]->GetTint().ToFColor(true), FColor::FromHex(PaintRow->PaintColorHex));
		}
		TestFalse(TEXT("칠한 벤치는 테두리 없음"), Demo[0]->HasSelectedOutline());
		TestTrue(TEXT("다른 벤치는 테두리 켜짐"), Demo[1]->HasSelectedOutline());
		TestTrue(TEXT("테두리 벤치는 안 칠함"), Demo[1]->GetPaintItemID().IsNone());
		TestNotEqual(TEXT("둘은 다른 자리"), Demo[0]->GetActorLocation(), Demo[1]->GetActorLocation());
	}
	DestroyTestWorld(World);
	return true;
}

// ⑨-4 테두리가 위에서 보인다 (M13-34) — 실제 껍질 머티리얼에 띄움 칸이 있고 코드가 0 보다 큰 값을 넣는다.
// 밑면이 바닥 아래로 부풀면 탑다운에서 앞·옆 띠의 시선이 바닥에 먼저 닿아 가려진다(감독 3층 #0220).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseOutlineLiftTest, "Snarehaven.Base.Paint.OutlineAboveGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseOutlineLiftTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	ASnarePlacedStructureActor* const Bench = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
	Bench->InitStructure(TEXT("item.bench"), FString(), FString(), FString(), 0, 100.f);
	Bench->FinishSpawning(FTransform::Identity);
	Bench->SetSelectedOutline(true);

	const UStaticMeshComponent* const Body = Bench->FindComponentByClass<UStaticMeshComponent>();
	const UMaterialInterface* const Overlay = Body != nullptr ? Body->GetOverlayMaterial() : nullptr;
	if (TestNotNull(TEXT("테두리 껍질"), Overlay))
	{
		float LiftCm = 0.f;
		float ThicknessCm = 0.f;
		TestTrue(TEXT("껍질에 띄움 칸"), Overlay->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineGroundLiftCm")), LiftCm));
		TestTrue(TEXT("껍질에 두께 칸"), Overlay->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineThicknessCm")), ThicknessCm));
		TestTrue(TEXT("밑면이 바닥 위로 뜬다"), LiftCm > 0.f);
		TestTrue(TEXT("옆 두께가 띄움보다 커서 밑면 띠가 남는다"), ThicknessCm > LiftCm);
		const UMaterial* const Shell = Overlay->GetMaterial();
		if (TestNotNull(TEXT("껍질 원본 머티리얼"), Shell))
		{
			TestTrue(TEXT("뒷면을 그리려면 양면"), Shell->IsTwoSided());
			TestEqual(TEXT("앞면은 마스크로 자른다"), Shell->GetBlendMode(), EBlendMode::BLEND_Masked);
		}
	}
	DestroyTestWorld(World);
	return true;
}

// ⑨-5 가구·상자 진짜 메시 크기 (M13-37 · DESIGN 8.11 표) — 설치물 행마다 자기 메시가 붙고, 월드 긴 변이 표 폭 ±3cm,
// 긴 변이 발자국 X 방향, 밑면이 바닥, 칸 안에 들어가며, 테두리·칠하기가 새 메시에서도 켜진다. 상자·가방도 표 폭.
// 표 폭은 설계서에서 읽는다(사본을 만들지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePlacedMeshTest, "Snarehaven.Base.PlacedMesh.RealSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePlacedMeshTest::RunTest(const FString& Parameters)
{
	constexpr float WidthToleranceCm = 3.f;
	constexpr float GroundToleranceCm = 0.5f;
	const FName TestPaint(TEXT("item.paint_red"));

	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	struct FSizeRow { FString Id; FString Mesh; float WidthCm = 0.f; };
	TArray<FSizeRow> Design;
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		FRegexMatcher M(FRegexPattern(TEXT(R"(^\| [^`]*`(\w+)`[^`]*`(SM_\w+)` \| (\d+) \|)")), Line);
		if (M.FindNext())
		{
			Design.Add({ M.GetCaptureGroup(1), M.GetCaptureGroup(2), FCString::Atof(*M.GetCaptureGroup(3)) });
		}
	}
	if (!TestEqual(TEXT("설계서 8.11 메시 크기 표 58행 (가구 10 + 제련소 + 전력 3 + 벽 장식 7 + 소품 3 + 집 가구 26 + 계단 + 발전기 + 전기 가구 4 + 상자 + 가방)"), Design.Num(), 58))
	{
		return false;
	}

	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}
	auto FindItem = [Items](FName Id) -> const FItemRow*
	{
		for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->ItemID == Id)
			{
				return Row;
			}
		}
		return nullptr;
	};
	const FItemRow* const PaintRow = FindItem(TestPaint);
	if (!TestNotNull(TEXT("빨강 페인트 행"), PaintRow))
	{
		return false;
	}
	const FLinearColor PaintColor(FColor::FromHex(PaintRow->PaintColorHex));

	UWorld* const World = CreateTestWorld();
	int32 Furniture = 0;
	for (const FSizeRow& Want : Design)
	{
		if (Want.Id.EndsWith(TEXT("Actor")))
		{
			continue;
		}
		++Furniture;
		const FName ItemId(*(TEXT("item.") + Want.Id));
		const FItemRow* const Row = FindItem(ItemId);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 표 행"), *Want.Id), Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("[%s] 표 폭 = 설계서"), *Want.Id), Row->PlacedMeshWidthCm, Want.WidthCm);

		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemId, TestTentId, TestStructureId, FString(), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		UStaticMeshComponent* const Body = Actor->FindComponentByClass<UStaticMeshComponent>();
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 몸"), *Want.Id), Body) || !TestNotNull(TEXT("메시"), Body->GetStaticMesh().Get()))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("[%s] 자기 메시"), *Want.Id), Body->GetStaticMesh()->GetName(), Want.Mesh);

		const FBox Box = Body->CalcBounds(Body->GetComponentTransform()).GetBox();
		const FVector Size = Box.GetSize();
		AddInfo(FString::Printf(TEXT("[%s] %s 월드 크기(cm)=%.1fx%.1fx%.1f 밑면z=%.2f 가운데=(%.1f,%.1f) 발자국=%dx%d 표 폭=%.0f"),
			*Want.Id, *Want.Mesh, Size.X, Size.Y, Size.Z, Box.Min.Z, Box.GetCenter().X, Box.GetCenter().Y,
			Actor->GetFootprint().X, Actor->GetFootprint().Y, Want.WidthCm));
		TestTrue(*FString::Printf(TEXT("[%s] 긴 변 %.1f = 표 %.0f ±%.0f"), *Want.Id, Size.X, Want.WidthCm, WidthToleranceCm),
			FMath::Abs(Size.X - Want.WidthCm) <= WidthToleranceCm);
		TestTrue(*FString::Printf(TEXT("[%s] 긴 변이 발자국 X 방향"), *Want.Id), Size.X + GroundToleranceCm >= Size.Y);
		TestTrue(*FString::Printf(TEXT("[%s] 밑면이 바닥"), *Want.Id), FMath::Abs(Box.Min.Z) <= GroundToleranceCm);
		// 설계서는 폭(긴 변)만 칸 안에 두라고 정한다 — 깊이는 메시 비율이라 표에 없다(로그의 월드 크기로 본다).
		TestTrue(*FString::Printf(TEXT("[%s] 폭이 칸 안에 든다"), *Want.Id), Size.X <= Actor->GetFootprint().X * 100.f);
		// 벽 장식은 뒷면(-Y)이 루트 = 벽 면이다 — 가운데면 두께 절반이 벽에 묻힌다 (감독 결함 10-20 08:10).
		const bool bWallDecor = Row->PlaceableFunction == EPlaceableFunction::WallDecor;
		TestTrue(*FString::Printf(TEXT("[%s] %s"), *Want.Id, bWallDecor ? TEXT("가로 가운데 · 뒷면이 루트") : TEXT("발자국 가운데")),
			FMath::Abs(Box.GetCenter().X) <= 1.f && (bWallDecor ? FMath::Abs(Box.Min.Y) <= 0.1f : FMath::Abs(Box.GetCenter().Y) <= 1.f));

		Actor->SetSelectedOutline(true);
		TestTrue(*FString::Printf(TEXT("[%s] 테두리 켜짐"), *Want.Id), Actor->HasSelectedOutline() && Body->GetOverlayMaterial() != nullptr);

		FLinearColor Applied = FLinearColor::Black;
		const UMaterialInterface* const Tinted = Body->GetMaterial(0);
		TestTrue(*FString::Printf(TEXT("[%s] 안 칠하면 텍스처 그대로(흰색 곱)"), *Want.Id),
			Tinted != nullptr && Tinted->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Applied)
			&& Applied.Equals(FLinearColor::White));
		// 텐트는 집 색, 불(모닥불·화로·그릴·제련소)은 불 색 자리라 칠하기 대상이 아니다 — 큐브 때와 같은 규칙.
		const EPlaceableFunction Function = Row->PlaceableFunction;
		if (Function != EPlaceableFunction::Tent && Function != EPlaceableFunction::Campfire && Function != EPlaceableFunction::Grill
			&& Function != EPlaceableFunction::Smelter)
		{
			Actor->SetPaint(TestPaint);
			const UMaterialInterface* const Painted = Body->GetMaterial(0);
			TestTrue(*FString::Printf(TEXT("[%s] 칠한 색이 메시 재질에"), *Want.Id),
				Painted != nullptr && Painted->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Applied)
				&& Applied.Equals(PaintColor));
			TestTrue(*FString::Printf(TEXT("[%s] 칠해도 테두리 그대로"), *Want.Id), Actor->HasSelectedOutline());
		}
	}
	TestEqual(TEXT("가구 10종 + 제련소(8.16) + 전력 3(8.11 키트·배터리·전등) + 벽 장식 7(M108-02·05c) + 소품 3(M108-03) + 집 가구 26(M108-04a·04b·05a·05b·05d·05e) + 계단(M108-06b) + 발전기(M108-07b) + 전기 가구 4(M108-08b)"), Furniture, 56);
	DestroyTestWorld(World);

	// 상자·가방 — 몸이 루트라 루트 배율이 곧 월드 배율이다.
	auto CheckProp = [this, &Design](const AActor* Default, const TCHAR* Id)
	{
		const FSizeRow* const Want = Design.FindByPredicate([Id](const FSizeRow& Row) { return Row.Id == Id; });
		const UStaticMeshComponent* const Body = Cast<UStaticMeshComponent>(Default->GetRootComponent());
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 설계서 행"), Id), Want) || !TestNotNull(TEXT("몸"), Body)
			|| !TestNotNull(TEXT("메시"), Body->GetStaticMesh().Get()))
		{
			return;
		}
		TestEqual(*FString::Printf(TEXT("[%s] 자기 메시"), Id), Body->GetStaticMesh()->GetName(), Want->Mesh);
		const FBox Box = Body->GetStaticMesh()->GetBoundingBox();
		const FVector Size = Box.GetSize() * Body->GetRelativeScale3D();
		const float Long = FMath::Max(Size.X, Size.Y);
		AddInfo(FString::Printf(TEXT("[%s] %s 월드 크기(cm)=%.1fx%.1fx%.1f 밑면z=%.2f 표 폭=%.0f"),
			Id, *Want->Mesh, Size.X, Size.Y, Size.Z, Box.Min.Z * Body->GetRelativeScale3D().Z, Want->WidthCm));
		TestTrue(*FString::Printf(TEXT("[%s] 긴 변 %.1f = 표 %.0f ±%.0f"), Id, Long, Want->WidthCm, WidthToleranceCm),
			FMath::Abs(Long - Want->WidthCm) <= WidthToleranceCm);
		TestTrue(*FString::Printf(TEXT("[%s] 균등 배율"), Id), Body->GetRelativeScale3D().AllComponentsEqual());
	};
	CheckProp(GetDefault<ALootChestActor>(), TEXT("LootChestActor"));
	CheckProp(GetDefault<ADropBagActor>(), TEXT("DropBagActor"));
	return true;
}

// ⑨-2 땅 하나 최악 드로콜 (M108-03b, DESIGN 8.11 「가구 한도 드로콜 재계산」) — 표의 놓는 가구 메시 섹션(= 드로콜)을 실측해,
// 2층 한도 몫 안에서 드로콜이 가장 많이 나오는 배치(벽 장식 0.5 · 받침 1 + 소품 0.5 × 슬롯 · 바닥 가구 1)가 예산 이하인가.
// 한도·예산·벽 장식 최대는 설계서에서 읽는다. 배치는 반 몫 단위 배낭 셈이라 「벽 먼저」 같은 순서 가정이 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseDrawCallBudgetTest, "Snarehaven.Base.PlacedMesh.DrawCallBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseDrawCallBudgetTest::RunTest(const FString& Parameters)
{
	FString Craft;
	FString BaseText;
	if (!LoadDesignSections(*this, Craft, BaseText))
	{
		return false;
	}
	const int32 Limit = CaptureInt(BaseText, TEXT(R"(가구 한도 \*\*\d+ → (\d+)\*\*)"));
	const int32 Budget = CaptureInt(BaseText, TEXT(R"(땅 하나 최악 드로콜 예산 (\d+))"));
	const int32 WallMax = CaptureInt(BaseText, TEXT(R"(벽 장식 최대 (\d+) =)"));
	if (!TestTrue(*FString::Printf(TEXT("설계서 숫자 셋 (한도 %d · 예산 %d · 벽 장식 최대 %d)"), Limit, Budget, WallMax),
		Limit > 0 && Budget > 0 && WallMax > 0))
	{
		return false;
	}
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}

	// 반 몫 단위 물건: (반 몫, 드로콜). 벽 장식은 개수 상한이 따로라 빼서 센다.
	struct FCost { int32 Halves = 0; int32 Draws = 0; };
	TArray<FCost> Others;
	int32 WallDraws = 0;
	int32 TopDraws = 0;
	struct FHost { int32 Draws = 0; int32 Slots = 0; };
	TArray<FHost> Hosts;
	int32 Rows = 0;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || Row->PlacedMeshRef.IsNull() || Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			continue;
		}
		UStaticMesh* const Mesh = Row->PlacedMeshRef.LoadSynchronous();
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 메시"), *Row->ItemID.ToString()), Mesh))
		{
			continue;
		}
#if WITH_EDITOR
		FStaticMeshCompilingManager::Get().FinishCompilation({ Mesh });
#endif
		const int32 Sections = Mesh->GetNumSections(0);
		++Rows;
		AddInfo(FString::Printf(TEXT("[%s] %s 섹션 %d"), *Row->ItemID.ToString(), *Mesh->GetName(), Sections));
		// 앞으로 새 가구·장식 메시도 재질 1개 (ROADMAP M108-03b) — 섹션이 늘면 이 줄이 먼저 잡는다.
		TestEqual(*FString::Printf(TEXT("[%s] 섹션 1 (재질 1개)"), *Row->ItemID.ToString()), Sections, 1);
		if (Row->PlaceableFunction == EPlaceableFunction::WallDecor)
		{
			WallDraws = FMath::Max(WallDraws, Sections);
		}
		else if (Row->PlaceableFunction == EPlaceableFunction::TopDecor)
		{
			TopDraws = FMath::Max(TopDraws, Sections);
		}
		else
		{
			Others.Add({ 2, Sections });
			if (Row->TopSlots > 0)
			{
				Hosts.Add({ Sections, Row->TopSlots });
			}
		}
	}
	TestTrue(TEXT("벽 장식·소품·받침 행이 있다"), WallDraws > 0 && TopDraws > 0 && Hosts.Num() > 0);
	for (const FHost& Host : Hosts)
	{
		for (int32 Filled = 1; Filled <= Host.Slots; ++Filled)
		{
			Others.Add({ 2 + Filled, Host.Draws + Filled * TopDraws });
		}
	}

	// 벽 장식 말고 물건으로 반 몫 u 를 채울 때 최대 드로콜 (무한 배낭).
	const int32 Cap = Limit * 2;
	TArray<int32> Best;
	Best.Init(0, Cap + 1);
	for (int32 U = 1; U <= Cap; ++U)
	{
		Best[U] = Best[U - 1];
		for (const FCost& Cost : Others)
		{
			if (Cost.Halves <= U)
			{
				Best[U] = FMath::Max(Best[U], Best[U - Cost.Halves] + Cost.Draws);
			}
		}
	}
	int32 Worst = 0;
	int32 WorstWalls = 0;
	for (int32 Walls = 0; Walls <= FMath::Min(WallMax, Cap); ++Walls)
	{
		const int32 Draws = Walls * WallDraws + Best[Cap - Walls];
		if (Draws > Worst)
		{
			Worst = Draws;
			WorstWalls = Walls;
		}
	}
	AddInfo(FString::Printf(TEXT("놓는 가구 %d행 · 벽 장식 섹션 %d · 소품 섹션 %d · 받침 %d종 → 2층 한도 %d 최악 드로콜 %d (벽 장식 %d개) / 예산 %d"),
		Rows, WallDraws, TopDraws, Hosts.Num(), Limit, Worst, WorstWalls, Budget));
	TestTrue(*FString::Printf(TEXT("땅 하나 최악 드로콜 %d <= 예산 %d"), Worst, Budget), Worst <= Budget);
	return true;
}

// ⑩ 가구 직접 탭 (M13-20) — 탭 자리 광선이 처음 지나는 가구(텐트 땅 상자는 건너뜀)를 찾고, 페인트를 가진 채
// 내 가구면 칠하기 쪽이 열린다. 남의 가구·빈 곳·페인트 없음이면 아무것도 안 연다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePaintTapTest, "Snarehaven.Base.Paint.TapFurniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePaintTapTest::RunTest(const FString& Parameters)
{
	const TCHAR* const OtherTentId = TEXT("tent_test_other");
	const TCHAR* const OtherStructureId = TEXT("st_test_other");
	const FName TestPaint(TEXT("item.paint_red"));
	const FVector MinePos(0.f, 0.f, 0.f);
	const FVector OtherPos(1000.f, 0.f, 0.f);
	const FVector EmptyPos(5000.f, 5000.f, 0.f);
	const FVector Down(0.f, 0.f, -1.f);
	const FVector Above(0.f, 0.f, 3000.f);

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	FSnarePlayerSnapshot WithPaint = MakeSnapshot();
	FInventorySlot PaintSlot;
	PaintSlot.ItemID = TestPaint;
	PaintSlot.Count = 1;
	WithPaint.InventorySlots.Add(PaintSlot);
	TestTrue(TEXT("페인트 든 가방"), Backend->ApplyPlayerSnapshot(WithPaint, /*bFromBackend=*/true));
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);

	FSnareTentMirror Mine = MakeTent(TestTentId, FIntPoint(0, 0));
	FSnareStructureMirror MyBench;
	MyBench.StructureId = TestStructureId;
	MyBench.ItemID = TEXT("item.bench");
	Mine.Structures.Add(MyBench);
	Base->AddTentMirrorForTest(Mine);
	FSnareTentMirror Other = MakeTent(OtherTentId, FIntPoint(50, 50));
	Other.PlacedAtUtc.Empty();
	FSnareStructureMirror OtherBench = MyBench;
	OtherBench.StructureId = OtherStructureId;
	Other.Structures.Add(OtherBench);
	Base->AddTentMirrorForTest(Other);

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& TentId, const FString& StructureId, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform(Location), Params);
		Actor->InitStructure(ItemID, TentId, StructureId, FString(), 0, 100.f);
		Actor->FinishSpawning(FTransform(Location));
		return Actor;
	};
	ASnarePlacedStructureActor* const Bench = Spawn(TEXT("item.bench"), TestTentId, TestStructureId, MinePos);
	ASnarePlacedStructureActor* const ForeignBench = Spawn(TEXT("item.bench"), OtherTentId, OtherStructureId, OtherPos);
	Spawn(TEXT("item.tent"), TestTentId, FString(), MinePos); // 벤치를 덮는 텐트 몸

	// 광선 — 위에서 내려다본 탭 자리.
	TestEqual(TEXT("내 벤치 위 탭 → 내 벤치 (텐트 건너뜀)"),
		USnareBaseBuildComponent::FindFurnitureOnRay(World, MinePos + Above, Down), Bench);
	TestEqual(TEXT("남의 벤치 위 탭 → 남의 벤치"),
		USnareBaseBuildComponent::FindFurnitureOnRay(World, OtherPos + Above, Down), ForeignBench);
	TestNull(TEXT("빈 곳 탭 → 없음"), USnareBaseBuildComponent::FindFurnitureOnRay(World, EmptyPos + Above, Down));

	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("화면"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);

	TestFalse(TEXT("빈 곳이면 안 열림"),
		Screen->TapPaintTarget(USnareBaseBuildComponent::FindFurnitureOnRay(World, EmptyPos + Above, Down)));
	TestFalse(TEXT("남의 가구면 안 열림"),
		Screen->TapPaintTarget(USnareBaseBuildComponent::FindFurnitureOnRay(World, OtherPos + Above, Down)));
	TestFalse(TEXT("아직 화면 닫힘"), Screen->IsOpen());
	TestNull(TEXT("고른 대상 없음"), Screen->GetPaintTarget());

	TestTrue(TEXT("내 가구면 열림"),
		Screen->TapPaintTarget(USnareBaseBuildComponent::FindFurnitureOnRay(World, MinePos + Above, Down)));
	TestTrue(TEXT("화면 열림"), Screen->IsOpen());
	TestEqual(TEXT("칠하기 쪽"), Screen->GetPage(), ESnareBaseScreenPage::Paint);
	TestEqual(TEXT("대상 = 탭한 벤치"), Screen->GetPaintTarget(), Bench);

	// 페인트가 없으면 내 가구여도 안 연다.
	Screen->Close();
	Backend->ApplyPlayerSnapshot(MakeSnapshot(), /*bFromBackend=*/true);
	TestFalse(TEXT("페인트 없으면 안 열림"), Screen->TapPaintTarget(Bench));
	TestFalse(TEXT("닫힌 채"), Screen->IsOpen());

	DestroyTestWorld(World);
	return true;
}

// ⑩ 게스트가 놓은 것을 호스트가 받아 복제 액터로 세운다 (M15-26, DESIGN 8.11 멀티) — 호스트 기준 겹침 판정.
// 검사 전용 값: 주인 계정 ID·칸은 서버가 보낸 셈 치는 것이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHostSharesGuestTest, "Snarehaven.Base.Place.HostSharesGuestStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHostSharesGuestTest::RunTest(const FString& Parameters)
{
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString OtherOwner(TEXT("OTHER_TEST_ID"));
	const FName Bench(TEXT("item.bench"));
	const FIntPoint Spot(40, 40);

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	// 표는 실제 표를 꽂는다 (기대값을 코드에 적지 않는다).
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();

	const auto Standing = [World]()
	{
		int32 Count = 0;
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			Count += It->IsPreview() ? 0 : 1;
		}
		return Count;
	};

	TestTrue(TEXT("호스트에는 권위가 있다"), HostPawn->HasAuthority());
	TestTrue(TEXT("컴포넌트가 복제 대상(서버 함수가 타는 길)"), Build->GetIsReplicated());

	// 게스트 가구 하나 — 복제 액터로 선다, 주인은 게스트 계정.
	TestTrue(TEXT("게스트 가구를 받는다"), Build->HostAcceptShared(Bench, TestTentId, TestStructureId, Spot, 0, GuestOwner, NAME_None));
	TestEqual(TEXT("선 것 1"), Standing(), 1);
	ASnarePlacedStructureActor* Standing1 = nullptr;
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		Standing1 = *It;
	}
	if (TestNotNull(TEXT("선 액터"), Standing1))
	{
		TestTrue(TEXT("복제 액터"), Standing1->GetIsReplicated());
		TestEqual(TEXT("주인 = 게스트 계정"), Standing1->GetOwnerPlayFabId(), GuestOwner);
	}

	// 같은 주소를 또 보내도 늘지 않고, 같은 칸을 다른 주소로 보내면 거절.
	TestTrue(TEXT("같은 주소는 그대로 받음"), Build->HostAcceptShared(Bench, TestTentId, TestStructureId, Spot, 0, GuestOwner, NAME_None));
	TestEqual(TEXT("여전히 1"), Standing(), 1);
	TestFalse(TEXT("겹치는 다른 게스트 가구는 거절"),
		Build->HostAcceptShared(Bench, TEXT("tent_test_2"), TEXT("st_test_2"), Spot, 0, OtherOwner, NAME_None));
	TestEqual(TEXT("거절 뒤에도 1"), Standing(), 1);

	// 놓을 수 없는 것(없는 ID)·텐트 주소 없음은 거절.
	TestFalse(TEXT("없는 아이템은 거절"),
		Build->HostAcceptShared(TEXT("item.no_such_thing"), TestTentId, TEXT("st_test_3"), FIntPoint(80, 80), 0, GuestOwner, NAME_None));
	TestFalse(TEXT("텐트 주소 없음은 거절"),
		Build->HostAcceptShared(Bench, FString(), TEXT("st_test_4"), FIntPoint(90, 90), 0, GuestOwner, NAME_None));

	// 안 겹치면 받는다.
	TestTrue(TEXT("안 겹치면 받는다"),
		Build->HostAcceptShared(Bench, TestTentId, TEXT("st_test_5"), FIntPoint(60, 60), 0, GuestOwner, NAME_None));
	TestEqual(TEXT("선 것 2"), Standing(), 2);

	// 치우기: 주인 계정이 다르면 못 치우고, 같으면 치운다.
	TestFalse(TEXT("남의 계정으론 못 치움"), Build->HostRemoveShared(TestStructureId, OtherOwner));
	TestEqual(TEXT("그대로 2"), Standing(), 2);
	TestTrue(TEXT("주인 계정이면 치움"), Build->HostRemoveShared(TestStructureId, GuestOwner));
	TestEqual(TEXT("치운 뒤 1"), Standing(), 1);
	return true;
}

// ⑧ 집 2층 (DESIGN 8.11 「집 2층·전력」, M20-03b) — 표 = 설계서, `{tentId, level: 2}` 만 보내고 응답 뒤에만 2층,
// 화면 단계 글, 가구 한도 40 → 60 이 놓기 미리보기에 든다, 서버 저장값 `floors` 복원.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseFloor2Test, "Snarehaven.Base.Floor2.RequestDisplayLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseFloor2Test::RunTest(const FString& Parameters)
{
	FString Craft;
	FString BaseText;
	if (!LoadDesignSections(*this, Craft, BaseText))
	{
		return false;
	}
	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	const FBaseRuleRow* const Rules = Base->GetRules();
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("2층 줄이 채워졌다"), Rules->HasFloor2Rules()))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}

	// 표 = 설계서 (숫자는 설계서에서 읽는다).
	const TCHAR* const CostPattern = TEXT(R"(\*\*층계 짜맞춤 (\d+) \+ 나무판 (\d+) \+ 향나무판 (\d+) \+ 철판 (\d+)\*\*)");
	TestEqual(TEXT("2층 가구 한도"), Rules->FurnitureMaxFloor2, CaptureInt(BaseText, TEXT(R"(가구 한도 \*\*\d+ → (\d+)\*\*)")));
	TestEqual(TEXT("2층 재료 가짓수"), Rules->Floor2Cost.Num(), 4);
	TestEqual(TEXT("계단 건축 부품"), Rules->Floor2Cost.FindRef(TEXT("item.stair_kit")), CaptureInt(BaseText, CostPattern, 1));
	TestEqual(TEXT("나무판"), Rules->Floor2Cost.FindRef(TEXT("item.plank")), CaptureInt(BaseText, CostPattern, 2));
	TestEqual(TEXT("향나무판"), Rules->Floor2Cost.FindRef(TEXT("item.cedar_plank")), CaptureInt(BaseText, CostPattern, 3));
	TestEqual(TEXT("철판"), Rules->Floor2Cost.FindRef(TEXT("item.sheet_metal")), CaptureInt(BaseText, CostPattern, 4));
	for (const TPair<FName, int32>& Pair : Rules->Floor2Cost)
	{
		TestNotNull(*FString::Printf(TEXT("재료 %s 가 아이템 표에 있다"), *Pair.Key.ToString()), Base->FindItemRow(Pair.Key));
	}

	// 집 하나 (1층).
	FSnareTentMirror House = MakeTent(TestTentId, FIntPoint(0, 0));
	House.bUpgraded = true;
	House.UpkeepUntilUtc = TestUpkeepAfterUpgrade;
	Base->AddTentMirrorForTest(House);
	TestEqual(TEXT("층 기본값 1"), Base->FindTent(TestTentId)->Floors, 1);
	TestEqual(TEXT("1층 단계 글"), UCraftingScreenWidget::FormatFloorText(*Base->FindTent(TestTentId), *Rules).ToString(),
		FString::Printf(TEXT("1층 집 · 가구 0/%d"), Rules->FurnitureMax));
	TestTrue(TEXT("텐트면 단계 글 없음"), UCraftingScreenWidget::FormatFloorText(MakeTent(TEXT("t"), FIntPoint(0, 0)), *Rules).IsEmpty());

	// 거절 — 사본 그대로, 2층 몫 글.
	TestTrue(TEXT("2층을 묻는다"), Base->RequestUpgradeHouse(TestTentId, TestHouseColor, 2));
	TestEqual(TEXT("UpgradeHouse 함수"), Calls->Last().FunctionName, FString(TEXT("UpgradeHouse")));
	TestEqual(TEXT("level 2"), ArgInt(Calls->Last(), TEXT("level")), 2);
	TestEqual(TEXT("tentId"), ArgString(Calls->Last(), TEXT("tentId")), FString(TestTentId));
	TestFalse(TEXT("2층엔 색을 안 보낸다"), Calls->Last().Args->HasField(TEXT("color")));
	TestEqual(TEXT("응답 전엔 1층"), Base->FindTent(TestTentId)->Floors, 1);
	Respond(*Calls, TEXT(R"({"error":"x","code":"not_house"})"));
	TestEqual(TEXT("거절이면 1층"), Base->FindTent(TestTentId)->Floors, 1);
	TestEqual(TEXT("not_house 는 2층 글"), Base->GetLastMessage().ToString(), FString(TEXT("집이 된 뒤에 2층을 올릴 수 있습니다")));

	// 성공 — 응답의 `level` 그대로.
	TestTrue(TEXT("다시 묻는다"), Base->RequestUpgradeHouse(TestTentId, 0, 2));
	Respond(*Calls, FString::Printf(TEXT(R"({"upgraded":true,"tentId":"%s","level":2,"color":%d,"upkeepUntilUtc":"%s","furnitureMax":60})"),
		TestTentId, TestHouseColor, TestUpkeepAfterFeed));
	TestEqual(TEXT("응답 뒤 2층"), Base->FindTent(TestTentId)->Floors, 2);
	TestEqual(TEXT("유지 글자 = 서버 값"), Base->FindTent(TestTentId)->UpkeepUntilUtc, FString(TestUpkeepAfterFeed));
	TestEqual(TEXT("2층 단계 글"), UCraftingScreenWidget::FormatFloorText(*Base->FindTent(TestTentId), *Rules).ToString(),
		FString::Printf(TEXT("2층 집 · 가구 0/%d"), Rules->FurnitureMaxFloor2));
	USnareBackendSubsystem::TestServerOverride = nullptr;

	// 한도 — 1층 한도만큼 차면 1층은 파랑, 2층은 초록. 2층 한도만큼 차면 2층도 파랑.
	const FName Box(TEXT("item.storage_box"));
	const int32 Half = Rules->LandCells / 2;
	// HungOrTop = 벽 장식·소품 수 (짝수 번째는 벽, 홀수 번째는 첫 가구 위 소품) — 한도에서 0.5개씩 (서버 `furnitureLoad`).
	auto MakeFilledTent = [&](int32 Count, int32 Floors, int32 HungOrTop)
	{
		FSnareTentMirror Tent = MakeTent(TestTentId, FIntPoint(0, 0));
		Tent.bUpgraded = true;
		Tent.Floors = Floors;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSnareStructureMirror Fence;
			Fence.StructureId = FString::Printf(TEXT("fence_%d"), Index);
			Fence.ItemID = TEXT("item.fence_wood");
			Fence.Cell = FIntPoint(-Half + Index % Rules->LandCells, -Half + Index / Rules->LandCells);
			Tent.Structures.Add(Fence);
		}
		for (int32 Index = 0; Index < HungOrTop; ++Index)
		{
			FSnareStructureMirror Decor;
			Decor.StructureId = FString::Printf(TEXT("decor_%d"), Index);
			if (Index % 2 == 0)
			{
				Decor.ItemID = TEXT("item.picture_frame");
				Decor.WallSide = 0;
				Decor.WallHeightCm = 150;
			}
			else
			{
				Decor.ItemID = TEXT("item.mug");
				Decor.TopHostId = TEXT("fence_0");
				Decor.TopSlot = Index / 2;
			}
			Tent.Structures.Add(Decor);
		}
		return Tent;
	};
	auto Filled = [&](int32 Count, int32 Floors, int32 HungOrTop = 0)
	{
		const FSnareTentMirror Tent = MakeFilledTent(Count, Floors, HungOrTop);
		USnareBaseSubsystem* const WithTent = MakeBase(*this, nullptr);
		WithTent->AddTentMirrorForTest(Tent);
		return static_cast<int32>(WithTent->EvaluatePlacement(Box, FIntPoint(Half - 1, Half - 1), 0, true));
	};
	const int32 Green = static_cast<int32>(ESnarePlacementVerdict::Green);
	const int32 Blue = static_cast<int32>(ESnarePlacementVerdict::Blue);
	TestEqual(*FString::Printf(TEXT("1층 가구 %d 개면 파랑"), Rules->FurnitureMax), Filled(Rules->FurnitureMax, 1), Blue);
	TestEqual(*FString::Printf(TEXT("2층 가구 %d 개면 아직 초록"), Rules->FurnitureMax), Filled(Rules->FurnitureMax, 2), Green);
	TestEqual(*FString::Printf(TEXT("2층 가구 %d 개면 파랑"), Rules->FurnitureMaxFloor2), Filled(Rules->FurnitureMaxFloor2, 2), Blue);
	TestEqual(TEXT("한도 계산 1층"), SnareBuildRules::FurnitureMaxFor(*Rules, 1), Rules->FurnitureMax);
	TestEqual(TEXT("한도 계산 2층"), SnareBuildRules::FurnitureMaxFor(*Rules, 2), Rules->FurnitureMaxFloor2);

	// 벽 장식·소품은 0.5개 (DESIGN 8.11 「가구 한도」 = 서버 `furnitureLoad`) — 사본 개수가 한도여도 몫이 남으면 초록.
	TestEqual(TEXT("몫: 바닥 3 + 벽·소품 3 = 4.5"), SnareBuildRules::FurnitureLoad(3, 3), 4.5f);
	TestEqual(*FString::Printf(TEXT("1층 바닥 %d + 벽 장식 1 = 한도 - 0.5 면 초록"), Rules->FurnitureMax - 1),
		Filled(Rules->FurnitureMax - 1, 1, 1), Green);
	TestEqual(*FString::Printf(TEXT("1층 바닥 %d + 벽 장식·소품 2 = 한도면 파랑"), Rules->FurnitureMax - 1),
		Filled(Rules->FurnitureMax - 1, 1, 2), Blue);
	TestEqual(TEXT("2층 벽 장식·소품만 한도 × 2 - 1 개면 초록"), Filled(0, 2, Rules->FurnitureMaxFloor2 * 2 - 1), Green);
	const FSnareTentMirror HalfLoad = MakeFilledTent(Rules->FurnitureMax - 1, 1, 1);
	TestEqual(TEXT("사본 수 = 한도"), HalfLoad.Structures.Num(), Rules->FurnitureMax);
	TestEqual(TEXT("단계 글은 몫으로"), UCraftingScreenWidget::FormatFloorText(HalfLoad, *Rules).ToString(),
		FString::Printf(TEXT("1층 집 · 가구 %d.5/%d"), Rules->FurnitureMax - 1, Rules->FurnitureMax));

	// 서버 저장값 복원 — `floors` 없으면 1, 있으면 그 값.
	USnareBaseSubsystem* const Restored = MakeBase(*this, nullptr);
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const TCHAR* Json : { TEXT("{\"tentId\":\"one\",\"x\":0,\"y\":0,\"upgraded\":true,\"placedAtUtc\":\"2026-09-25T00:00:00.000Z\"}"),
		TEXT("{\"tentId\":\"two\",\"x\":40,\"y\":0,\"upgraded\":true,\"floors\":2,\"placedAtUtc\":\"2026-09-25T00:00:00.000Z\"}") })
	{
		Values.Add(MakeShared<FJsonValueObject>(ParseJson(Json)));
	}
	Restored->ApplyBackendTents(Values);
	TestEqual(TEXT("floors 없는 집은 1층"), Restored->FindTent(TEXT("one"))->Floors, 1);
	TestEqual(TEXT("floors 2 는 2층"), Restored->FindTent(TEXT("two"))->Floors, 2);
	return true;
}

// 전력 (M20-04b) — 땅마다 키트·배터리·전등을 세어 밤에만 켠다. 발전 0 → 꺼짐, 키트 놓음 → 켜짐 (DESIGN 8.11 「집 2층·전력」).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePowerTest, "Snarehaven.Base.Power.LampsNeedSupplyAtNight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePowerTest::RunTest(const FString& Parameters)
{
	FString Craft;
	FString BaseText;
	FString Doc;
	if (!LoadDesignSections(*this, Craft, BaseText)
		|| !FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md"))))
	{
		return false;
	}
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("PowerTest")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("전력 줄이 채워졌다"), Rules->HasPowerRules()))
	{
		return false;
	}

	// 표 = 설계서.
	TestEqual(TEXT("키트 공급"), Rules->SolarKitPower, CaptureInt(BaseText, TEXT(R"(`item.solar_kit` 공급 \| 놓으면 \*\*(\d+)\*\*)")));
	TestEqual(TEXT("배터리 공급"), Rules->BatteryPower, CaptureInt(BaseText, TEXT(R"(`item.battery` 공급 \| 놓으면 \*\*\+(\d+)\*\*)")));
	TestEqual(TEXT("전등 소모"), Rules->LampPowerUse, CaptureInt(BaseText, TEXT(R"(`item.lamp_electric` 소모 \| 하나 \*\*(\d+)\*\*)")));
	TestEqual(TEXT("전등 밝기 = 3.3 전구"), FMath::RoundToInt32(Rules->LampLightCandela), CaptureInt(Doc, TEXT(R"(전구 (\d+)cd 주황)")));
	TestEqual(TEXT("전등 반경 = 3.3"), FMath::RoundToInt32(Rules->LampLightRadiusCm), CaptureInt(Doc, TEXT(R"(반경 \*\*(\d+)cm\*\*)")));
	TestEqual(TEXT("키트 ID"), Rules->SolarKitItemID, FName(TEXT("item.solar_kit")));
	TestEqual(TEXT("배터리 ID"), Rules->BatteryItemID, FName(TEXT("item.battery")));
	TestEqual(TEXT("전등 ID"), Rules->LampItemID, FName(TEXT("item.lamp_electric")));

	// 순수 판정 — 몇 개를 켜는지는 표 숫자로 계산한다(숫자가 바뀌어도 안 깨지게).
	const int32 KitLamps = Rules->SolarKitPower / Rules->LampPowerUse;
	const int32 PairLamps = (Rules->SolarKitPower + Rules->BatteryPower) / Rules->LampPowerUse;
	TestFalse(TEXT("발전 0 → 꺼짐"), SnareBuildRules::IsLandPowered(*Rules, 0, 0, 1));
	TestFalse(TEXT("전등 0 이면 켤 것 없음"), SnareBuildRules::IsLandPowered(*Rules, 1, 0, 0));
	TestTrue(TEXT("키트 하나로 켜지는 만큼"), SnareBuildRules::IsLandPowered(*Rules, 1, 0, KitLamps));
	TestFalse(TEXT("키트 하나로 하나 넘치면 전부 꺼짐"), SnareBuildRules::IsLandPowered(*Rules, 1, 0, KitLamps + 1));
	TestEqual(TEXT("키트 없는 배터리는 0"), SnareBuildRules::PowerSupply(*Rules, 0, 3), 0);
	TestFalse(TEXT("배터리만이면 꺼짐"), SnareBuildRules::IsLandPowered(*Rules, 0, 1, 1));
	TestTrue(TEXT("키트 + 배터리 한 쌍"), SnareBuildRules::IsLandPowered(*Rules, 1, 1, PairLamps));
	TestFalse(TEXT("한 쌍에 하나 넘치면 꺼짐"), SnareBuildRules::IsLandPowered(*Rules, 1, 1, PairLamps + 1));

	// 월드 — 놓인 가구 액터를 땅마다 센다.
	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	ASnarePlacedStructureActor* const Lamp = Spawn(Rules->LampItemID, TEXT("land_a"), TEXT("lamp_a"));
	ASnarePlacedStructureActor* const OtherLamp = Spawn(Rules->LampItemID, TEXT("land_b"), TEXT("lamp_b"));
	TestFalse(TEXT("놓자마자는 꺼짐"), Lamp->IsLampLightVisible());

	TestEqual(TEXT("밤 · 발전 0 → 켜진 전등 0"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), 0);
	TestFalse(TEXT("발전 0 → 불빛 안 보임"), Lamp->IsLampLightVisible());

	ASnarePlacedStructureActor* const Kit = Spawn(Rules->SolarKitItemID, TEXT("land_a"), TEXT("kit_a"));
	TestEqual(TEXT("밤 · 키트 놓음 → 그 땅 전등 1"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), 1);
	TestTrue(TEXT("키트 놓음 → 불빛 보임"), Lamp->IsLampLightVisible());
	TestFalse(TEXT("다른 땅 전등은 그대로 꺼짐"), OtherLamp->IsLampLightVisible());
	TestFalse(TEXT("키트 자체는 불빛 없음"), Kit->IsLampLightVisible());
	// 점광이 바닥이면 땅을 스치듯 비춰 발치에 점만 맺힌다(감독 보정 10-20 13:10) — 메시 실측 키의 위쪽 절반(전구 자리)에 있어야 한다.
	const UPointLightComponent* const LampLight = Lamp->FindComponentByClass<UPointLightComponent>();
	const UStaticMeshComponent* const LampBody = Lamp->GetBodyComponent();
	const UStaticMesh* const LampMesh = LampBody != nullptr ? LampBody->GetStaticMesh() : nullptr;
	if (TestNotNull(TEXT("전등 점광"), LampLight) && TestNotNull(TEXT("전등 메시"), LampMesh))
	{
		const FBox MeshBox = LampMesh->GetBoundingBox();
		const float LampHeightCm = (MeshBox.Max.Z - MeshBox.Min.Z) * LampBody->GetRelativeScale3D().Z;
		const float LightZ = LampLight->GetRelativeLocation().Z;
		AddInfo(FString::Printf(TEXT("전등 점광 높이 %.1f cm · 메시 키 %.1f cm"), LightZ, LampHeightCm));
		TestTrue(TEXT("전등 점광이 전구 자리(키의 위쪽 절반, 꼭대기 아래)"), LightZ > LampHeightCm * 0.5f && LightZ < LampHeightCm);
	}

	TestEqual(TEXT("낮 → 0"), USnarePowerSubsystem::ApplyPower(World, *Rules, false), 0);
	TestFalse(TEXT("낮 → 불빛 안 보임"), Lamp->IsLampLightVisible());

	USnarePowerSubsystem::ApplyPower(World, *Rules, true);
	Kit->Destroy();
	TestEqual(TEXT("키트 치움 → 0"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), 0);
	TestFalse(TEXT("키트 치움 → 꺼짐"), Lamp->IsLampLightVisible());

	DestroyTestWorld(World);
	return true;
}

// M108-05e 비전기 조명 — 밤이면 전력 없이 켜지고, 땅 하나 점광은 전등 + 불꽃 합해 상한까지(전등 먼저, 가구 ID 순).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseFlameLightTest, "Snarehaven.Base.Power.FlameLightsAtNightWithinBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseFlameLightTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("FlameTest")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("비전기 조명 줄이 채워졌다"), Rules->HasFlameRules())
		|| !TestTrue(TEXT("전력 줄도 채워졌다"), Rules->HasPowerRules()))
	{
		return false;
	}
	// 표 = 설계서 8.11 「비전기 조명」.
	TestEqual(TEXT("불꽃 밝기"), FMath::RoundToInt32(Rules->FlameLightCandela), CaptureInt(Doc, TEXT(R"(불꽃 점광 \*\*(\d+)cd)")));
	TestEqual(TEXT("불꽃 반경"), FMath::RoundToInt32(Rules->FlameLightRadiusCm), CaptureInt(Doc, TEXT(R"(불꽃 점광 \*\*\d+cd · 반경 (\d+)cm)")));
	TestEqual(TEXT("땅 하나 점광 상한"), Rules->LandLightMax, CaptureInt(Doc, TEXT(R"(땅 하나 점광 상한 (\d+)\*\*)")));

	const TCHAR* const FlameIds[] = { TEXT("item.candle_stand"), TEXT("item.oil_lamp"), TEXT("item.lantern_post"), TEXT("item.garden_torch") };
	USnareBaseSubsystem* const Empty = MakeBase(*this, nullptr);
	for (const TCHAR* const Id : FlameIds)
	{
		const FItemRow* const Row = Empty->FindItemRow(FName(Id));
		TestTrue(*FString::Printf(TEXT("[%s] 행 기능 = 비전기 불빛"), Id), Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::FlameLight);
	}

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	// 상한 + 1 개를 한 땅에 — 가구 ID f00 … 순으로 켜진다.
	const int32 Max = Rules->LandLightMax;
	TArray<ASnarePlacedStructureActor*> Flames;
	for (int32 Index = 0; Index <= Max; ++Index)
	{
		Flames.Add(Spawn(FName(FlameIds[Index % UE_ARRAY_COUNT(FlameIds)]), TEXT("land_a"), FString::Printf(TEXT("f%02d"), Index)));
	}
	ASnarePlacedStructureActor* const OtherFlame = Spawn(FName(FlameIds[0]), TEXT("land_b"), TEXT("f00"));

	TestEqual(TEXT("낮 → 0"), USnarePowerSubsystem::ApplyPower(World, *Rules, false), 0);
	TestFalse(TEXT("낮 → 불꽃 안 보임"), Flames[0]->IsLampLightVisible());

	TestEqual(TEXT("밤 · 전력 없음 → 땅 A 상한만큼 + 땅 B 하나"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), Max + 1);
	TestTrue(TEXT("가구 ID 첫 불꽃 켜짐"), Flames[0]->IsLampLightVisible());
	TestFalse(TEXT("상한 밖 마지막 불꽃은 꺼짐"), Flames[Max]->IsLampLightVisible());
	TestTrue(TEXT("다른 땅은 제 상한"), OtherFlame->IsLampLightVisible());
	const UPointLightComponent* const Light = Flames[0]->FindComponentByClass<UPointLightComponent>();
	if (TestNotNull(TEXT("점광"), Light))
	{
		AddInfo(FString::Printf(TEXT("불꽃 점광 높이 %.1f cm · 밝기 %.0f · 반경 %.0f"), Light->GetRelativeLocation().Z, Light->Intensity, Light->AttenuationRadius));
		TestTrue(TEXT("점광이 불꽃 높이(촛대 꼭대기 근처)"), Light->GetRelativeLocation().Z > 100.f);
		TestEqual(TEXT("점광 반경 = 표"), Light->AttenuationRadius, Rules->FlameLightRadiusCm);
	}

	// 전력 들인 전등이 먼저 자리를 차지한다 — 불꽃 하나가 더 꺼진다.
	ASnarePlacedStructureActor* const Lamp = Spawn(Rules->LampItemID, TEXT("land_a"), TEXT("lamp_a"));
	Spawn(Rules->SolarKitItemID, TEXT("land_a"), TEXT("kit_a"));
	TestEqual(TEXT("밤 · 전등 켜짐 → 땅 A 합은 그대로 상한"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), Max + 1);
	TestTrue(TEXT("전등 켜짐"), Lamp->IsLampLightVisible());
	TestFalse(TEXT("자리를 내준 불꽃 꺼짐"), Flames[Max - 1]->IsLampLightVisible());
	TestTrue(TEXT("앞 불꽃은 그대로"), Flames[0]->IsLampLightVisible());

	DestroyTestWorld(World);
	return true;
}

// ⑪ 배낭 제작 줄 원판 (M23-04, 시트 `inventory_backpack.md` 「아이템은 색 원판 위 그림」) — 줄 수 = 레시피 수 ·
// 줄마다 만들 것 원판(등급 색) 그림 = 아이템 표 `IconRef` · 모자란 재료 글자는 빨강, 채우면 진갈색으로 돌아온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseCraftRowDiscTest, "Snarehaven.Base.Craft.BackpackRowsShowItemDiscs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseCraftRowDiscTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	Backend->ApplyPlayerSnapshot(MakeSnapshot(), /*bFromBackend=*/true);

	UWorld* const World = CreateTestWorld();
	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("화면"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);

	// 그림은 비동기로 읽힌다 — 미리 읽어 두면 `SetBrushFromSoftTexture` 가 그 자리에서 붙인다.
	const TArray<FSnareRecipeView> Views = Screen->BuildRecipes();
	if (!TestTrue(TEXT("배낭 레시피가 있다"), Views.Num() > 0))
	{
		DestroyTestWorld(World);
		return false;
	}
	for (const FSnareRecipeView& View : Views)
	{
		if (const FItemRow* const Row = Base->FindItemRow(View.ResultItemID))
		{
			Row->IconRef.LoadSynchronous();
		}
	}
	Screen->TakeWidget();
	Screen->OpenBackpack();

	const FLinearColor Red = SnareUiStyle::UiWarnOnWood();
	const FLinearColor Brown = SnareUiStyle::UiTextOnWood();
	auto TextColor = [Screen](const FString& Name)
	{
		const UTextBlock* const Text = Screen->WidgetTree->FindWidget<UTextBlock>(FName(*Name));
		return Text != nullptr ? Text->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
	};

	int32 Icons = 0;
	int32 RedTexts = 0;
	for (int32 Index = 0; Index < Views.Num(); ++Index)
	{
		const FSnareRecipeView& View = Views[Index];
		const FItemRow* const Row = Base->FindItemRow(View.ResultItemID);
		const UBorder* const Disc = Screen->WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("CraftRecipeDisc_%d"), Index)));
		const UBorder* const Band = Screen->WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("CraftRecipeGrade_%d"), Index)));
		if (TestNotNull(*FString::Printf(TEXT("[%s] 칸"), *View.ResultItemID.ToString()), Disc)
			&& TestNotNull(*FString::Printf(TEXT("[%s] 등급 띠"), *View.ResultItemID.ToString()), Band) && Row != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("[%s] 띠 색 = 등급 띠 색"), *View.ResultItemID.ToString()),
				Band->Background.TintColor.GetSpecifiedColor(), SnareUiStyle::ItemGradeBandColor(Row->Grade));
			TestEqual(*FString::Printf(TEXT("[%s] 칸은 동그라미가 아니다"), *View.ResultItemID.ToString()),
				Disc->Background.OutlineSettings.CornerRadii.X, 4.0);
		}
		const UImage* const Icon = Screen->WidgetTree->FindWidget<UImage>(FName(*FString::Printf(TEXT("CraftRecipeIcon_%d"), Index)));
		if (Row != nullptr && !Row->IconRef.IsNull())
		{
			if (TestNotNull(*FString::Printf(TEXT("[%s] 그림"), *View.ResultItemID.ToString()), Icon))
			{
				TestEqual(*FString::Printf(TEXT("[%s] 그림 = 아이템 표 IconRef"), *View.ResultItemID.ToString()),
					Icon->GetBrush().GetResourceObject(), static_cast<UObject*>(Row->IconRef.Get()));
				++Icons;
			}
		}
		for (int32 Part = 0; Part < View.Ingredients.Num(); ++Part)
		{
			// 빈 가방 — 재료 전부 모자람 → 빨강.
			const FLinearColor Color = TextColor(FString::Printf(TEXT("CraftIngredientText_%d_%d"), Index, Part));
			TestEqual(*FString::Printf(TEXT("[%s] 재료 %d 빈 가방 = 빨강"), *View.ResultItemID.ToString(), Part), Color, Red);
			RedTexts += Color == Red ? 1 : 0;
		}
	}
	TestNull(TEXT("레시피 수 다음 줄은 없다"),
		Screen->WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("CraftRecipeDisc_%d"), Views.Num()))));
	TestTrue(TEXT("그림 있는 줄이 있다"), Icons > 0);

	// 첫 줄 재료를 딱 맞게 채운다 → 첫 줄 재료 글자는 진갈색, 버튼이 켜진다.
	FSnarePlayerSnapshot Exact = MakeSnapshot();
	for (const FSnareIngredientView& Ingredient : Views[0].Ingredients)
	{
		Exact.AddItem(Ingredient.Tag != ECraftTag::None ? FirstItemWithTag(ItemTable, Ingredient.Tag) : Ingredient.ItemID, Ingredient.Need);
	}
	Backend->ApplyPlayerSnapshot(Exact, /*bFromBackend=*/true);
	Screen->RefreshDisplay();
	int32 BrownTexts = 0;
	for (int32 Part = 0; Part < Views[0].Ingredients.Num(); ++Part)
	{
		const FLinearColor Color = TextColor(FString::Printf(TEXT("CraftIngredientText_0_%d"), Part));
		TestEqual(*FString::Printf(TEXT("첫 줄 재료 %d 채우면 = 진갈색"), Part), Color, Brown);
		BrownTexts += Color == Brown ? 1 : 0;
	}
	const UButton* const FirstButton = Screen->WidgetTree->FindWidget<UButton>(TEXT("CraftRecipeButton_0"));
	TestTrue(TEXT("첫 줄 버튼 켜짐"), FirstButton != nullptr && FirstButton->GetIsEnabled());

	AddInfo(FString::Printf(TEXT("[CraftDisc] 배낭 레시피 %d줄 · 원판 %d · 그림 %d · 빈 가방 빨강 재료 %d · 채운 첫 줄 진갈색 %d/%d"),
		Views.Num(), Views.Num(), Icons, RedTexts, BrownTexts, Views[0].Ingredients.Num()));
	Screen->Close();
	DestroyTestWorld(World);
	return true;
}


// ⑫ 제작 판 제목 알약·고른 탭 색 (M24-02, 시트 `inventory_backpack.md` 「제목 탭: 진갈색 알약, 패널 왼쪽 위 모서리에
// 걸쳐 위로 튀어나옴」) — 알약 위 변 = 판 위 변 − `MenuTitlePillRise` · 알약 왼 변 = 판 왼 변 · 제목 글자는 알약 안 ·
// 판 글자에 `[` 0 · 지금 탭 바탕 = 상점 `사기` 고른 탭 바탕.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseCraftTitlePillTest, "Snarehaven.Base.Craft.TitlePillAndTabHighlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseCraftTitlePillTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	Backend->ApplyPlayerSnapshot(MakeSnapshot(), /*bFromBackend=*/true);

	UWorld* const World = CreateTestWorld();
	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	UShopScreenWidget* const Shop = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("제작 화면"), Screen) || !TestNotNull(TEXT("상점 화면"), Shop))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);
	Screen->TakeWidget();
	Screen->OpenBackpack();
	Shop->TakeWidget();

	const UBorder* const Pill = Screen->WidgetTree->FindWidget<UBorder>(TEXT("CraftTitlePill"));
	const UWidget* const PillBox = Screen->WidgetTree->FindWidget(TEXT("CraftTitlePillBox"));
	const UWidget* const Panel = Screen->WidgetTree->FindWidget(TEXT("CraftPanel"));
	const UTextBlock* const Title = Screen->WidgetTree->FindWidget<UTextBlock>(TEXT("CraftTitle"));
	const UCanvasPanelSlot* const PillSlot = PillBox != nullptr ? Cast<UCanvasPanelSlot>(PillBox->Slot) : nullptr;
	const UCanvasPanelSlot* const PanelSlot = Panel != nullptr ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
	if (!TestNotNull(TEXT("제목 알약"), Pill) || !TestNotNull(TEXT("알약 칸"), PillSlot) || !TestNotNull(TEXT("판 칸"), PanelSlot)
		|| !TestNotNull(TEXT("제목 글자"), Title))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector2D PanelTopLeft = PanelSlot->GetPosition() - PanelSlot->GetSize() * PanelSlot->GetAlignment();
	const FVector2D PillTopLeft = PillSlot->GetPosition() - PillSlot->GetSize() * PillSlot->GetAlignment();
	TestEqual(TEXT("알약 위 변 = 판 위 변 - MenuTitlePillRise"), PillTopLeft.Y, PanelTopLeft.Y - SnareUiStyle::MenuTitlePillRise);
	TestEqual(TEXT("알약 왼 변 = 판 왼 변"), PillTopLeft.X, PanelTopLeft.X);
	TestTrue(TEXT("알약·판 같은 닻"), PillSlot->GetAnchors().Minimum == PanelSlot->GetAnchors().Minimum);
	TestEqual(TEXT("알약 색 = UiWoodButton (M99-06 밝은 나무)"), Pill->Background.TintColor.GetSpecifiedColor(), SnareUiStyle::UiWoodButton());
	TestTrue(TEXT("제목 글자는 알약 안"), Title->GetParent() == Pill);
	TestEqual(TEXT("배낭 제목"), Title->GetText().ToString(), FString(TEXT("배낭 제작")));

	int32 Texts = 0;
	TArray<FString> Bracketed;
	Screen->WidgetTree->ForEachWidget([&Texts, &Bracketed](UWidget* Widget)
	{
		if (const UTextBlock* const Text = Cast<UTextBlock>(Widget))
		{
			++Texts;
			if (Text->GetText().ToString().Contains(TEXT("[")))
			{
				Bracketed.Add(Text->GetText().ToString());
			}
		}
	});
	TestEqual(*FString::Printf(TEXT("판 글자에 [ 0 (%s)"), *FString::Join(Bracketed, TEXT(" | "))), Bracketed.Num(), 0);

	const UButton* const ShopBuyTab = Shop->WidgetTree->FindWidget<UButton>(TEXT("BuyTabButton"));
	const FLinearColor ShopSelected = ShopBuyTab != nullptr ? ShopBuyTab->GetStyle().Normal.TintColor.GetSpecifiedColor() : FLinearColor::Transparent;
	TestEqual(TEXT("상점 고른 탭 = UiButtonHighlight"), ShopSelected, SnareUiStyle::UiButtonHighlight());
	int32 Tabs = 0;
	int32 Highlighted = 0;
	FString TabLabels;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		const UButton* const Button = Cast<UButton>(Widget);
		if (Button == nullptr || !Button->GetName().StartsWith(TEXT("CraftTab_")))
		{
			return;
		}
		++Tabs;
		const UTextBlock* const Label = Button->GetChildrenCount() > 0 ? Cast<UTextBlock>(Button->GetChildAt(0)) : nullptr;
		TabLabels += Label != nullptr ? Label->GetText().ToString() + TEXT(" ") : TEXT("? ");
		const FLinearColor Tint = Button->GetStyle().Normal.TintColor.GetSpecifiedColor();
		if (Tint == SnareUiStyle::UiButtonHighlight())
		{
			++Highlighted;
		}
	});
	TestEqual(TEXT("배낭 제작 탭 1개"), Tabs, 1);
	TestEqual(TEXT("지금 탭 바탕 = 노란 강조 (10-16)"), Highlighted, 1);

	// 다시 그려도(서버 스냅샷 갱신) 강조가 `ApplyUiStyle` 에 덮이지 않는다.
	Screen->RefreshDisplay();
	int32 HighlightedAfter = 0;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		const UButton* const Button = Cast<UButton>(Widget);
		if (Button != nullptr && Button->GetName().StartsWith(TEXT("CraftTab_"))
			&& Button->GetStyle().Normal.TintColor.GetSpecifiedColor() == SnareUiStyle::UiButtonHighlight())
		{
			++HighlightedAfter;
		}
	});
	TestEqual(TEXT("다시 그린 뒤에도 지금 탭 강조"), HighlightedAfter, 1);

	AddInfo(FString::Printf(TEXT("[CraftTitle] 알약 (%.0f, %.0f) · 판 (%.0f, %.0f) · 제목 %s · 글자 %d개 중 [ %d · 탭 %s· 강조 %d/%d"),
		PillTopLeft.X, PillTopLeft.Y, PanelTopLeft.X, PanelTopLeft.Y, *Title->GetText().ToString(), Texts, Bracketed.Num(),
		*TabLabels, Highlighted, Tabs));
	Screen->Close();
	DestroyTestWorld(World);
	return true;
}


// ⑬ 제작 판 둥근 판·둥근 버튼 (M27-01, 시트 `inventory_backpack.md` 「패널 크림 … 모서리 16px」·`hud_ingame.md` 「모든 패널
// 모서리 둥글게 12~16px」) — 판 브러시 = 크림·불투명 `ScreenPanelOpacity`·반지름 `ScreenPanelRadius` 둥근 네모 · 판 안 버튼
// (쪽 단추·레시피 줄·놓기 줄·닫기) 전부 네 상태 둥근 네모·반지름 `HudCornerRadius` · 고른 쪽 단추 = `UiButtonHighlight`.
// 잡는 것: 판을 다시 `SetBrushColor` 네모로 되돌리는 것 · 새 버튼을 엔진 기본 네모로 세우는 것 · 강조가 모양을 덮는 것.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseCraftRoundTest, "Snarehaven.Base.Craft.RoundPanelAndButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseCraftRoundTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	// 놓기 줄(`AddButtonRow`)이 생기게 배치형 하나를 가방에 — 표에서 고른다(이름을 코드에 적지 않는다).
	FName PlaceableID;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row->ItemType == EItemType::Placeable && Row->FootprintX > 0 && Row->PlaceableFunction != EPlaceableFunction::Boat)
		{
			PlaceableID = Row->ItemID; // 행 이름(`Item_…`)이 아니라 가방이 쓰는 ID
			break;
		}
	}
	if (!TestFalse(TEXT("표에 배치형이 있다"), PlaceableID.IsNone()))
	{
		return false;
	}
	FSnarePlayerSnapshot Snapshot = MakeSnapshot();
	Snapshot.AddItem(PlaceableID, 1);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);

	UWorld* const World = CreateTestWorld();
	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("제작 화면"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);
	Screen->TakeWidget();
	Screen->OpenBackpack();

	// 판 — 나무 판 #4A3826·0.95·둥근 16.
	const UBorder* const Panel = Screen->WidgetTree->FindWidget<UBorder>(TEXT("CraftPanel"));
	if (!TestNotNull(TEXT("제작 판"), Panel))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FSlateBrush& PanelBrush = Panel->Background;
	FLinearColor PanelColor = SnareUiStyle::UiWoodPanel();
	PanelColor.A = 1.f;   // 10-16 불투명
	const FVector4 PanelRadius(SnareUiStyle::ScreenPanelRadius, SnareUiStyle::ScreenPanelRadius, SnareUiStyle::ScreenPanelRadius,
		SnareUiStyle::ScreenPanelRadius);
	TestTrue(TEXT("판 브러시 = 둥근 네모"), PanelBrush.DrawAs == ESlateBrushDrawType::RoundedBox);
	TestTrue(TEXT("판 반지름 = ScreenPanelRadius"), PanelBrush.OutlineSettings.CornerRadii == PanelRadius);
	TestEqual(TEXT("판 색 = 나무 판·불투명"), PanelBrush.TintColor.GetSpecifiedColor(), PanelColor);

	const FVector4 ButtonRadius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius,
		SnareUiStyle::HudCornerRadius);
	const FVector4 TabRadius(SnareUiStyle::WoodTabRadius, SnareUiStyle::WoodTabRadius, SnareUiStyle::WoodTabRadius,
		SnareUiStyle::WoodTabRadius);
	auto IsRound = [&ButtonRadius, &TabRadius](const FSlateBrush& Brush, bool bTab)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox
			&& Brush.OutlineSettings.CornerRadii == (bTab ? TabRadius : ButtonRadius);
	};
	// 판 안 버튼 전부 — 이름 앞부분으로 종류를 센다(종류마다 하나 이상이어야 시험이 그 길을 본 것이다).
	auto CheckButtons = [&](const TCHAR* Stage, TMap<FString, int32>& OutKinds, int32& OutRound, int32& OutAll)
	{
		Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			const UButton* const Button = Cast<UButton>(Widget);
			if (Button == nullptr)
			{
				return;
			}
			const FString Name = Button->GetName();
			int32 Cut = INDEX_NONE;
			const FString Kind = Name.FindLastChar(TEXT('_'), Cut) ? Name.Left(Cut) : Name;
			OutKinds.FindOrAdd(Kind) += 1;
			const FButtonStyle& Style = Button->GetStyle();
			const bool bTab = Name.StartsWith(TEXT("CraftTab_"));   // 쪽 탭은 각진 나무 표찰 (M101-01)
			const bool bRound = IsRound(Style.Normal, bTab) && IsRound(Style.Hovered, bTab) && IsRound(Style.Pressed, bTab) && IsRound(Style.Disabled, bTab);
			TestTrue(FString::Printf(TEXT("%s %s 네 상태 둥근 네모 · 반지름 = %s"), Stage, *Name, bTab ? TEXT("WoodTabRadius") : TEXT("HudCornerRadius")), bRound);
			OutRound += bRound ? 1 : 0;
			++OutAll;
		});
	};

	TMap<FString, int32> CraftKinds;
	int32 CraftRound = 0;
	int32 CraftAll = 0;
	CheckButtons(TEXT("제작 쪽"), CraftKinds, CraftRound, CraftAll);
	TestTrue(TEXT("제작 쪽 단추가 있다"), CraftKinds.FindRef(TEXT("CraftTab")) > 0);
	TestTrue(TEXT("레시피 줄 버튼이 있다"), CraftKinds.FindRef(TEXT("CraftRecipeButton")) > 0);
	TestEqual(TEXT("닫기 버튼 1개"), CraftKinds.FindRef(TEXT("CraftClose")), 1);

	int32 Highlighted = 0;
	Screen->WidgetTree->ForEachWidget([&Highlighted](UWidget* Widget)
	{
		const UButton* const Button = Cast<UButton>(Widget);
		if (Button != nullptr && Button->GetName().StartsWith(TEXT("CraftTab_"))
			&& Button->GetStyle().Normal.TintColor.GetSpecifiedColor() == SnareUiStyle::UiButtonHighlight())
		{
			++Highlighted;
		}
	});
	TestEqual(TEXT("고른 쪽 단추 색 = UiButtonHighlight"), Highlighted, 1);

	// 놓기 쪽 — 줄 버튼(`AddButtonRow`)도 둥글다.
	Screen->ShowPage(ESnareBaseScreenPage::Place);
	TMap<FString, int32> PlaceKinds;
	int32 PlaceRound = 0;
	int32 PlaceAll = 0;
	CheckButtons(TEXT("놓기 쪽"), PlaceKinds, PlaceRound, PlaceAll);
	TestTrue(TEXT("놓기 줄 버튼이 있다"), PlaceKinds.FindRef(TEXT("CraftRowButton")) > 0);

	AddInfo(FString::Printf(TEXT("[CraftRound] 판 %s 반지름 %.0f 불투명 %.2f · 제작 쪽 둥근 버튼 %d/%d (쪽 단추 %d·레시피 %d·닫기 %d) · 강조 %d · 놓기 쪽 둥근 버튼 %d/%d (줄 %d, %s)"),
		PanelBrush.DrawAs == ESlateBrushDrawType::RoundedBox ? TEXT("RoundedBox") : TEXT("네모"),
		static_cast<float>(PanelBrush.OutlineSettings.CornerRadii.X), PanelBrush.TintColor.GetSpecifiedColor().A,
		CraftRound, CraftAll, CraftKinds.FindRef(TEXT("CraftTab")), CraftKinds.FindRef(TEXT("CraftRecipeButton")),
		CraftKinds.FindRef(TEXT("CraftClose")), Highlighted, PlaceRound, PlaceAll, PlaceKinds.FindRef(TEXT("CraftRowButton")),
		*PlaceableID.ToString()));
	Screen->Close();
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseBuildHudRoundTest, "Snarehaven.Base.BuildHud.RoundButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M27-04 — 시트 `hud_ingame` 「모든 패널 모서리 둥글게 12~16px」. 놓기 막대 버튼(돌리기·놓기·취소) 네 상태가 둥글고
// 터치 높이 여백(`SetStyle`)도 그대로 남는지 실제 값으로 잰다.
bool FSnarehavenBaseBuildHudRoundTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	UBuildHudWidget* const Screen = CreateWidget<UBuildHudWidget>(World, UBuildHudWidget::StaticClass());
	if (!TestNotNull(TEXT("건축 HUD 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	Screen->TakeWidget();

	const FVector4 Radius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius,
		SnareUiStyle::HudCornerRadius);
	auto IsRound = [&Radius](const FSlateBrush& Brush)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox && Brush.OutlineSettings.CornerRadii == Radius;
	};
	int32 All = 0;
	int32 Round = 0;
	int32 Padded = 0;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Child)
	{
		const UButton* const Button = Cast<UButton>(Child);
		if (Button == nullptr)
		{
			return;
		}
		++All;
		const FButtonStyle& Style = Button->GetStyle();
		const bool bRound = IsRound(Style.Normal) && IsRound(Style.Hovered) && IsRound(Style.Pressed) && IsRound(Style.Disabled);
		Round += bRound ? 1 : 0;
		Padded += Style.NormalPadding.Top > 0.f ? 1 : 0;
		TestTrue(FString::Printf(TEXT("%s 네 상태 둥근 네모 · 반지름 = HudCornerRadius"), *Button->GetName()), bRound);
		TestTrue(FString::Printf(TEXT("%s 터치 높이 여백 남음"), *Button->GetName()), Style.NormalPadding.Top > 0.f);
	});
	AddInfo(FString::Printf(TEXT("[BuildHudRound] 둥근 버튼 %d/%d · 여백 %d"), Round, All, Padded));
	TestEqual(TEXT("건축 HUD 버튼 수"), All, 4); // 돌리기·세밀·놓기·취소 — 시험이 버튼을 다 봤는지

	DestroyTestWorld(World);
	return true;
}

// ⑪ 배 보관함 판 (M32-02c, DESIGN 8.9 보트 컨테이너) — 컨테이너가 없으면 까닭만, 있으면 넣기·꺼내기 줄.
// 줄을 누르면 `MoveToStorage`/`MoveFromStorage {boat: true}` 만 묻고, 사본은 서버 `storage` 뒤에만 바뀐다 (CLAUDE.md 8).
// 컨테이너 자신은 넣는 줄이 없다 (넣으면 열쇠가 갇힘). 칸 수는 표 `BonusSlots`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseBoatStorageScreenTest, "Snarehaven.Base.Storage.BoatStorageScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseBoatStorageScreenTest::RunTest(const FString& Parameters)
{
	const FName Wood(TEXT("item.wood"));
	const FName Container = USnareBaseSubsystem::GetBoatContainerItemID();
	constexpr int32 TestWoodCount = 5;

	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	const FItemRow* const ContainerRow = Base->FindItemRow(Container);
	if (!TestNotNull(TEXT("표에 배 짐칸 행"), ContainerRow))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}
	TestTrue(TEXT("배 보관함 칸 > 0"), Base->GetBoatStorageSlots() > 0);
	TestEqual(TEXT("배 보관함 칸 = 표 BonusSlots"), Base->GetBoatStorageSlots(), ContainerRow->BonusSlots);

	auto BagWith = [](bool bContainer)
	{
		FSnarePlayerSnapshot Snapshot = MakeSnapshot();
		FInventorySlot WoodSlot;
		WoodSlot.ItemID = TEXT("item.wood");
		WoodSlot.Count = TestWoodCount;
		Snapshot.InventorySlots.Add(WoodSlot);
		if (bContainer)
		{
			FInventorySlot ContainerSlot;
			ContainerSlot.ItemID = USnareBaseSubsystem::GetBoatContainerItemID();
			ContainerSlot.Count = 1;
			Snapshot.InventorySlots.Add(ContainerSlot);
		}
		return Snapshot;
	};
	Backend->ApplyPlayerSnapshot(BagWith(false), /*bFromBackend=*/true);

	UWorld* const World = CreateTestWorld();
	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("화면"), Screen))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);

	auto Labels = [Screen]()
	{
		TArray<FString> Out;
		Screen->WidgetTree->ForEachWidget([&Out](UWidget* Widget)
		{
			if (const UTextBlock* const Text = Cast<UTextBlock>(Widget))
			{
				Out.Add(Text->GetText().ToString());
			}
		});
		return Out;
	};
	auto FindNamed = [Screen](const TCHAR* Name) -> UWidget*
	{
		UWidget* Found = nullptr;
		Screen->WidgetTree->ForEachWidget([&Found, Name](UWidget* Widget)
		{
			if (Found == nullptr && Widget->GetFName() == FName(Name))
			{
				Found = Widget;
			}
		});
		return Found;
	};
	auto TextOf = [&FindNamed](const TCHAR* Name)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(FindNamed(Name));
		return Text != nullptr ? Text->GetText().ToString() : FString();
	};
	auto AnyLabel = [&Labels](const FString& Part)
	{
		return Labels().ContainsByPredicate([&Part](const FString& Label) { return Label.Contains(Part); });
	};

	// 컨테이너 없음 — 까닭 한 줄, 칸 0, 서버 부탁 0.
	Screen->OpenBoatStorage();
	TestEqual(TEXT("배 보관함 쪽"), Screen->GetPage(), ESnareBaseScreenPage::BoatStorage);
	TestTrue(TEXT("컨테이너 없으면 까닭 줄"), AnyLabel(TEXT("배 짐칸이 있어야")));
	TestNull(TEXT("컨테이너 없으면 가방 칸 없음"), FindNamed(TEXT("BoatBagCell_0")));
	TestFalse(TEXT("다른 쪽 탭(제작) 없음"), Labels().Contains(TEXT("제작")));
	TestEqual(TEXT("서버 부탁 0"), Calls->Num(), 0);

	// 컨테이너 있음 (INBOX 지시 10-07 01:25 칸 짜임) — 위 설명 띠 · 왼쪽 가방 칸 · 오른쪽 배 칸(빈 칸도 칸 모양).
	Backend->ApplyPlayerSnapshot(BagWith(true), /*bFromBackend=*/true);
	Screen->RefreshDisplay();
	const int32 MaxSlots = Base->GetBoatStorageSlots();
	TestEqual(TEXT("응답 전 배 칸 머리 ?/N"), TextOf(TEXT("BoatStoreHead")), FString::Printf(TEXT("배 보관함 ?/%d"), MaxSlots));
	TestTrue(TEXT("응답 전 「넣거나 꺼내면」"), AnyLabel(TEXT("넣거나 꺼내면")));
	TestEqual(TEXT("가방 칸 머리"), TextOf(TEXT("BoatBagHead")), FString(TEXT("내 가방")));
	TestNotNull(TEXT("배 빈 칸 첫째"), FindNamed(*FString::Printf(TEXT("BoatStoreCellEmpty_%d"), 0)));
	TestNotNull(TEXT("배 빈 칸 마지막"), FindNamed(*FString::Printf(TEXT("BoatStoreCellEmpty_%d"), MaxSlots - 1)));
	TestNull(TEXT("배 빈 칸은 표 칸 수까지"), FindNamed(*FString::Printf(TEXT("BoatStoreCellEmpty_%d"), MaxSlots)));
	TestEqual(TEXT("안 골랐으면 띠에 안내"), TextOf(TEXT("BoatDetailName")), FString(TEXT("칸을 눌러 아이템을 고르세요")));
	TestNull(TEXT("안 골랐으면 옮기기 버튼 없음"), FindNamed(TEXT("BoatMoveButton")));

	// INBOX 결함 10-07 02:00 ①④ — 판 불투명 · 빈 칸이 판 크림보다 또렷이 어둡다 · 가방도 빈 칸까지 7×n 격자.
	const UBorder* const PanelBorder = Cast<UBorder>(FindNamed(TEXT("CraftPanel")));
	TestTrue(TEXT("배 보관함 판 알파 1"), PanelBorder != nullptr && PanelBorder->Background.TintColor.GetSpecifiedColor().A >= 1.f);
	auto Brightness = [](const FLinearColor& Color) { return Color.R + Color.G + Color.B; };
	const UBorder* const StoreEmpty = Cast<UBorder>(FindNamed(TEXT("BoatStoreCellEmpty_0")));
	TestTrue(TEXT("빈 칸이 판 크림보다 밝기 합 0.5 이상 어둡다"), StoreEmpty != nullptr
		&& Brightness(SnareUiStyle::UiPanelCream()) - Brightness(StoreEmpty->Background.TintColor.GetSpecifiedColor()) >= 0.5f);
	int32 BagCellCount = 0;
	Screen->WidgetTree->ForEachWidget([&BagCellCount](UWidget* Widget)
	{
		const FString Name = Widget->GetName();
		if (Name.StartsWith(TEXT("BoatBagCell_")) || Name.StartsWith(TEXT("BoatBagCellEmpty_")))
		{
			++BagCellCount;
		}
	});
	TestNotNull(TEXT("가방 빈 칸(가진 2칸 뒤)"), FindNamed(TEXT("BoatBagCellEmpty_2")));
	TestTrue(TEXT("가방 칸 수 = 7 의 배수"), BagCellCount > 0 && BagCellCount % 7 == 0);
	TestTrue(TEXT("가방 칸 ≥ 기본 칸"), BagCellCount >= GetDefault<UInventoryComponent>()->GetSlotCount());
	AddInfo(FString::Printf(TEXT("[BoatStorage] 판 알파 %.2f · 빈 칸 밝기 %.2f(판 %.2f) · 가방 칸 %d"),
		PanelBorder != nullptr ? PanelBorder->Background.TintColor.GetSpecifiedColor().A : -1.f,
		StoreEmpty != nullptr ? Brightness(StoreEmpty->Background.TintColor.GetSpecifiedColor()) : -1.f, Brightness(SnareUiStyle::UiPanelCream()),
		BagCellCount));

	// 가방 순서 = 스냅샷 순서 (나무 0, 컨테이너 1). 컨테이너 칸은 꺼짐(흐림)이라 못 고른다.
	UButton* const WoodCell = Cast<UButton>(FindNamed(TEXT("BoatBagCell_0")));
	UButton* const ContainerCell = Cast<UButton>(FindNamed(TEXT("BoatBagCell_1")));
	if (!TestNotNull(TEXT("나무 칸"), WoodCell) || !TestNotNull(TEXT("컨테이너 칸"), ContainerCell))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}
	TestFalse(TEXT("컨테이너 칸 꺼짐"), ContainerCell->GetIsEnabled());
	TestTrue(TEXT("나무 칸 켜짐"), WoodCell->GetIsEnabled());
	TestEqual(TEXT("나무 칸 개수 글"), TextOf(TEXT("BoatBagCellCount_0")), FString::FromInt(TestWoodCount));
	TestNotNull(TEXT("나무 칸 원판"), FindNamed(TEXT("BoatBagCellDisc_0")));
	const USizeBox* const CellBox = Cast<USizeBox>(WoodCell->GetParent());
	TestTrue(TEXT("칸 짧은 변 ≥ 72"), CellBox != nullptr
		&& FMath::Min(CellBox->GetWidthOverride(), CellBox->GetHeightOverride()) >= SnareUiStyle::ScreenCloseButtonHeight);
	Screen->PickBoatCell(Container, /*bFromStore=*/false);
	TestTrue(TEXT("컨테이너는 골라도 풀림"), Screen->GetBoatPickedItem().IsNone());

	// 칸 한 번 = 고르기 — 서버 부탁 없음, 띠 갱신, 버튼 「넣기」.
	Cast<UButton>(FindNamed(TEXT("BoatBagCell_0")))->OnClicked.Broadcast();
	TestEqual(TEXT("고르기는 서버 부탁 0"), Calls->Num(), 0);
	TestEqual(TEXT("고른 것 = 나무"), Screen->GetBoatPickedItem(), Wood);
	TestFalse(TEXT("가방 쪽에서 고름"), Screen->IsBoatPickedFromStore());
	const FString WoodName = TextOf(TEXT("BoatDetailName"));
	TestFalse(TEXT("띠 이름 = 나무 (안내 글 아님)"), WoodName.IsEmpty() || WoodName.Contains(TEXT("칸을 눌러")));
	TestTrue(TEXT("띠 개수"), TextOf(TEXT("BoatDetailInfo")).Contains(FString::FromInt(TestWoodCount)));
	TestEqual(TEXT("버튼 글 넣기"), TextOf(TEXT("BoatMoveText")), FString(TEXT("넣기")));
	// 「」 는 판 글꼴에 없어 빈 네모로 나왔다 (INBOX 결함 10-07 02:00 ③).
	const FString DescLine = TextOf(TEXT("BoatDetailDesc"));
	TestTrue(TEXT("설명 줄에 「」 없음"), !DescLine.IsEmpty() && !DescLine.Contains(TEXT("「")) && !DescLine.Contains(TEXT("」")));

	UButton* const PutButton = Cast<UButton>(FindNamed(TEXT("BoatMoveButton")));
	if (!TestNotNull(TEXT("넣기 버튼"), PutButton))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}
	PutButton->OnClicked.Broadcast();
	if (TestEqual(TEXT("넣기 부탁 1건"), Calls->Num(), 1))
	{
		bool bBoat = false;
		Calls->Last().Args->TryGetBoolField(TEXT("boat"), bBoat);
		TestEqual(TEXT("함수 MoveToStorage"), Calls->Last().FunctionName, FString(TEXT("MoveToStorage")));
		TestTrue(TEXT("boat: true"), bBoat);
		TestEqual(TEXT("itemId 나무"), ArgString(Calls->Last(), TEXT("itemId")), Wood.ToString());
		TestEqual(TEXT("count 가진 만큼"), ArgInt(Calls->Last(), TEXT("count")), TestWoodCount);
		TestFalse(TEXT("텐트 ID 안 보냄"), Calls->Last().Args->HasField(TEXT("tentId")));
	}
	TestFalse(TEXT("응답 전 사본 모름"), Base->HasBoatStorageMirror());
	TestEqual(TEXT("응답 전 사본 비어 있음"), Base->GetBoatStorage().Num(), 0);
	TestNull(TEXT("응답 전 배 칸 그대로 비어 있음"), FindNamed(TEXT("BoatStoreCell_0")));

	Respond(*Calls, FString::Printf(TEXT(R"({"stored":"item.wood","count":%d,"storage":{"item.wood":%d}})"), TestWoodCount, TestWoodCount));
	TestEqual(TEXT("응답 뒤 사본 = 서버 storage"), Base->GetBoatStorage().FindRef(Wood), TestWoodCount);
	// 서버가 가방 스냅샷도 새로 준다 — 나무가 빠진 가방.
	FSnarePlayerSnapshot AfterPut = BagWith(true);
	AfterPut.InventorySlots.RemoveAll([&Wood](const FInventorySlot& Entry) { return Entry.ItemID == Wood; });
	Backend->ApplyPlayerSnapshot(AfterPut, /*bFromBackend=*/true);
	Screen->RefreshDisplay();
	TestEqual(TEXT("응답 뒤 배 칸 머리 1/N"), TextOf(TEXT("BoatStoreHead")), FString::Printf(TEXT("배 보관함 1/%d"), MaxSlots));
	TestTrue(TEXT("가방에서 다 옮겼으면 고르기 풀림"), Screen->GetBoatPickedItem().IsNone());
	TestNotNull(TEXT("남은 배 빈 칸"), FindNamed(*FString::Printf(TEXT("BoatStoreCellEmpty_%d"), MaxSlots - 1)));

	UButton* const StoreCell = Cast<UButton>(FindNamed(TEXT("BoatStoreCell_0")));
	if (TestNotNull(TEXT("배 칸 나무"), StoreCell))
	{
		StoreCell->OnClicked.Broadcast();
		TestTrue(TEXT("배 쪽에서 고름"), Screen->IsBoatPickedFromStore());
		const FString TakeLabel = TextOf(TEXT("BoatMoveText"));
		TestEqual(TEXT("버튼 글 꺼내기"), TakeLabel, FString(TEXT("꺼내기")));
		Cast<UButton>(FindNamed(TEXT("BoatMoveButton")))->OnClicked.Broadcast();
		if (TestEqual(TEXT("꺼내기 부탁"), Calls->Num(), 2))
		{
			bool bBoat = false;
			Calls->Last().Args->TryGetBoolField(TEXT("boat"), bBoat);
			TestEqual(TEXT("함수 MoveFromStorage"), Calls->Last().FunctionName, FString(TEXT("MoveFromStorage")));
			TestTrue(TEXT("꺼내기도 boat: true"), bBoat);
			TestEqual(TEXT("꺼내기 count 칸 개수"), ArgInt(Calls->Last(), TEXT("count")), TestWoodCount);
		}
		// 거절 — 사본 그대로, 배 보관함 글(PIN 글 아님), 고르기 유지.
		Respond(*Calls, TEXT(R"({"error":"x","code":"locked"})"));
		TestEqual(TEXT("거절이면 사본 그대로"), Base->GetBoatStorage().FindRef(Wood), TestWoodCount);
		TestTrue(TEXT("거절 글 = 컨테이너 까닭"), Base->GetLastMessage().ToString().Contains(TEXT("배 짐칸")));
		Screen->RefreshDisplay();
		TestEqual(TEXT("거절 뒤 고르기 유지"), Screen->GetBoatPickedItem(), Wood);
		AddInfo(FString::Printf(TEXT("[BoatStorage] 칸 고르기 → 띠 '%s' · 넣기 → MoveToStorage{boat:true} · 응답 뒤 머리 '%s' · 배 칸 고르기 → '%s' · 거절 글 '%s'"),
			*WoodName, *TextOf(TEXT("BoatStoreHead")), *TakeLabel, *Base->GetLastMessage().ToString()));
	}

	// 재접속 — `GetBaseState.boatStorage` 빈 객체면 사본 비움(모름 아님).
	Base->ApplyBackendBoatStorage(ParseJson(TEXT("{}")));
	TestTrue(TEXT("빈 객체도 받은 것"), Base->HasBoatStorageMirror());
	TestEqual(TEXT("빈 객체면 사본 0"), Base->GetBoatStorage().Num(), 0);

	Screen->Close();
	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

// ⑭ M108-01 세밀 회전 — 15° 단위로 맞추고, 칸 발자국은 가까운 90° 로 바꾼다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseFineRotationTest, "Snarehaven.Base.Rotation.FifteenDegreeSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseFineRotationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("15 는 15"), SnareBuildRules::SnapRotation(15), 15);
	TestEqual(TEXT("352 는 345"), SnareBuildRules::SnapRotation(352), 345);
	TestEqual(TEXT("358 은 0 으로 돈다"), SnareBuildRules::SnapRotation(358), 0);
	TestEqual(TEXT("-15 는 345"), SnareBuildRules::SnapRotation(-15), 345);
	TestEqual(TEXT("90 돌리기는 그대로 90"), SnareBuildRules::SnapRotation(90), 90);
	TestEqual(TEXT("24단계"), SnareBuildRules::FineRotationSteps * SnareBuildRules::FineRotationStepDegrees, 360);
	TestEqual(TEXT("30° 는 가로세로 그대로"), SnareBuildRules::RotatedFootprint(2, 1, 30), FIntPoint(2, 1));
	TestEqual(TEXT("60° 는 가로세로 바뀜"), SnareBuildRules::RotatedFootprint(2, 1, 60), FIntPoint(1, 2));
	TestEqual(TEXT("285° 는 가로세로 바뀜"), SnareBuildRules::RotatedFootprint(2, 1, 285), FIntPoint(1, 2));
	return true;
}

// ⑮ M108-01 반 칸 격자 — 반 칸 단위 자리 계산·겹침·땅 판정.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHalfGridTest, "Snarehaven.Base.HalfGrid.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHalfGridTest::RunTest(const FString& Parameters)
{
	FIntPoint Cell, Half;
	SnareBuildRules::SplitHalfUnits(FIntPoint(5, -3), Cell, Half);
	TestEqual(TEXT("5 → 칸 2 + 반 1"), Cell.X, 2);
	TestEqual(TEXT("5 → 반 1"), Half.X, 1);
	TestEqual(TEXT("-3 → 칸 -2"), Cell.Y, -2);
	TestEqual(TEXT("-3 → 반 1"), Half.Y, 1);
	TestEqual(TEXT("되돌리면 같다"), SnareBuildRules::JoinHalfUnits(Cell, Half), FIntPoint(5, -3));

	// 1×1 가구: 칸 (0,0) 반 (1,0) 의 가운데는 x 100cm, y 50cm (칸 크기 100).
	const FVector Center = SnareBuildRules::FootprintCenterHalf(FIntPoint(1, 0), FIntPoint(1, 1), 100.f, 0.f);
	TestTrue(TEXT("가운데 x"), FMath::IsNearlyEqual(Center.X, 100.0));
	TestTrue(TEXT("가운데 y"), FMath::IsNearlyEqual(Center.Y, 50.0));

	// 앞에 놓기: 반 칸 단위 앵커는 가까운 변이 몸에서 clearance 이상.
	const FIntPoint Ahead = SnareBuildRules::AnchorAheadHalf(FVector2D(0.f, 0.f), FVector2D(1.f, 0.f), FIntPoint(1, 1), 100.f, 100.f);
	const float NearEdgeX = Ahead.X * 50.f;
	TestTrue(TEXT("가까운 변이 몸에서 100cm 이상"), NearEdgeX >= 100.f - KINDA_SMALL_NUMBER);
	TestTrue(TEXT("반 칸 낭비 없음(150 미만)"), NearEdgeX < 150.f);

	// 겹침: 같은 칸이어도 반 칸 비켜 놓으면 1×1 둘은 겹치지만, 한 칸 비키면 안 겹친다.
	TSet<FIntPoint> Occupied;
	TArray<FIntPoint> Units;
	SnareBuildRules::FootprintHalfUnits(FIntPoint(4, 4), FIntPoint(1, 1), Units);
	Occupied.Append(Units);
	const FIntPoint Tent(0, 0);
	TestEqual(TEXT("반 칸 비킴은 겹침"), SnareBuildRules::EvaluateStructureHalf(FIntPoint(5, 4), FIntPoint(1, 1), &Tent, 16, Occupied),
		ESnarePlacementVerdict::Red);
	TestEqual(TEXT("한 칸 비키면 안 겹침"), SnareBuildRules::EvaluateStructureHalf(FIntPoint(6, 4), FIntPoint(1, 1), &Tent, 16, Occupied),
		ESnarePlacementVerdict::Green);
	// 땅: 16칸 → 칸 -8~7 = 반 칸 단위 -16~15. 앵커 14 는 14~15 라 안, 15 는 15~16 이라 반 칸 넘쳐 밖.
	TestEqual(TEXT("땅 끝 안"), SnareBuildRules::EvaluateStructureHalf(FIntPoint(14, 0), FIntPoint(1, 1), &Tent, 16, TSet<FIntPoint>()),
		ESnarePlacementVerdict::Green);
	TestEqual(TEXT("땅 끝 밖(반 칸 넘침)"), SnareBuildRules::EvaluateStructureHalf(FIntPoint(15, 0), FIntPoint(1, 1), &Tent, 16, TSet<FIntPoint>()),
		ESnarePlacementVerdict::Blue);
	return true;
}

// ⑯ M108-02 벽 걸기 — 집 발자국 변 고르기·걸릴 위치.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseWallSlotTest, "Snarehaven.Base.WallSlot.Pick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseWallSlotTest::RunTest(const FString& Parameters)
{
	// 집 = 칸 (2,3) 부터 3×3, 칸 크기 100 → x 200~500, y 300~600.
	const FIntPoint Tent(2, 3);
	FIntPoint Cell;
	int32 Side = -1;
	TestTrue(TEXT("고름"), SnareBuildRules::PickWallSlot(FVector2D(350.f, 650.f), Tent, 3, 100.f, Cell, Side));
	TestEqual(TEXT("+Y 변"), Side, 0);
	TestEqual(TEXT("+Y 변 가운데 칸"), Cell, FIntPoint(3, 5));
	SnareBuildRules::PickWallSlot(FVector2D(560.f, 450.f), Tent, 3, 100.f, Cell, Side);
	TestEqual(TEXT("+X 변"), Side, 1);
	TestEqual(TEXT("+X 변 가운데 칸"), Cell, FIntPoint(4, 4));
	SnareBuildRules::PickWallSlot(FVector2D(350.f, 250.f), Tent, 3, 100.f, Cell, Side);
	TestEqual(TEXT("-Y 변"), Side, 2);
	TestEqual(TEXT("-Y 변 가운데 칸"), Cell, FIntPoint(3, 3));
	SnareBuildRules::PickWallSlot(FVector2D(150.f, 500.f), Tent, 3, 100.f, Cell, Side);
	TestEqual(TEXT("-X 변"), Side, 3);
	TestEqual(TEXT("-X 변 위 칸"), Cell, FIntPoint(2, 5));
	SnareBuildRules::PickWallSlot(FVector2D(9000.f, 9000.f), Tent, 3, 100.f, Cell, Side);
	TestTrue(TEXT("먼 점도 집 칸 안으로 눌림"), Cell.X >= 2 && Cell.X <= 4 && Cell.Y >= 3 && Cell.Y <= 5);
	TestFalse(TEXT("칸 크기 0 은 거절"), SnareBuildRules::PickWallSlot(FVector2D::ZeroVector, Tent, 3, 0.f, Cell, Side));

	float Yaw = 0.f;
	const FVector Loc = SnareBuildRules::WallSlotLocation(FIntPoint(3, 5), 0, 100.f, 150.f, Yaw);
	TestTrue(TEXT("+Y 변 위치"), Loc.Equals(FVector(350.f, 600.f, 150.f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("+Y 변 yaw 90"), FMath::IsNearlyEqual(Yaw, 90.f));
	SnareBuildRules::WallSlotLocation(FIntPoint(2, 3), 3, 100.f, 150.f, Yaw);
	TestTrue(TEXT("-X 변 yaw 180"), FMath::IsNearlyEqual(Yaw, 180.f));
	return true;
}

// ⑰ M108-02 벽 걸기 — 미리보기 색(서버 `wallHangFail` 과 같은 줄)·높이 단·방 안쪽 돌림·표 행 기능.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseWallHangTest, "Snarehaven.Base.WallSlot.Hang",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseWallHangTest::RunTest(const FString& Parameters)
{
	// 시험 전용 집 — 칸 (2,3) 부터 3×3. 높이 값은 단 표에서 읽는다(숫자가 바뀌어도 안 깨지게).
	const FIntPoint Tent(2, 3);
	const int32 Low = SnareBuildRules::WallHeightStops[0];
	const int32 High = SnareBuildRules::WallHeightStops[UE_ARRAY_COUNT(SnareBuildRules::WallHeightStops) - 1];
	const TArray<SnareBuildRules::FWallHung> None;
	TestEqual(TEXT("+X 변 칸 = 초록"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 1, Low, Tent, 3, true, None),
		ESnarePlacementVerdict::Green);
	TestEqual(TEXT("집이 아닌 텐트 = 파랑"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 1, Low, Tent, 3, false, None),
		ESnarePlacementVerdict::Blue);
	TestEqual(TEXT("가운데 칸(변 아님) = 파랑"), SnareBuildRules::EvaluateWallHang(FIntPoint(3, 4), 1, Low, Tent, 3, true, None),
		ESnarePlacementVerdict::Blue);
	TestEqual(TEXT("칸은 +X 변인데 변 번호가 -X = 파랑"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 3, Low, Tent, 3, true, None),
		ESnarePlacementVerdict::Blue);
	TestEqual(TEXT("집 밖 칸 = 파랑"), SnareBuildRules::EvaluateWallHang(FIntPoint(5, 4), 1, Low, Tent, 3, true, None),
		ESnarePlacementVerdict::Blue);
	TestEqual(TEXT("높이 범위 아래 = 파랑"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 1, Low - 1, Tent, 3, true, None),
		ESnarePlacementVerdict::Blue);
	TestEqual(TEXT("높이 범위 위 = 파랑"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 1, High + 1, Tent, 3, true, None),
		ESnarePlacementVerdict::Blue);

	const TArray<SnareBuildRules::FWallHung> Hung = { { FIntPoint(4, 4), 1, Low } };
	TestEqual(TEXT("같은 칸·변·간격 미만 = 빨강"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 1,
		Low + SnareBuildRules::WallItemGapCm - 1, Tent, 3, true, Hung), ESnarePlacementVerdict::Red);
	TestEqual(TEXT("같은 칸·변·간격 그대로 = 초록"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 4), 1,
		Low + SnareBuildRules::WallItemGapCm, Tent, 3, true, Hung), ESnarePlacementVerdict::Green);
	TestEqual(TEXT("모서리 칸 다른 변 = 초록"), SnareBuildRules::EvaluateWallHang(FIntPoint(4, 5), 0, Low, Tent, 3, true,
		{ { FIntPoint(4, 5), 1, Low } }), ESnarePlacementVerdict::Green);

	// 높이 단은 낮은 순으로 돌고 끝에서 처음으로. 기본(눈높이)도 단 안이다.
	TestEqual(TEXT("첫 단 다음"), SnareBuildRules::NextWallHeight(Low), SnareBuildRules::WallHeightStops[1]);
	TestEqual(TEXT("끝 단 다음 = 첫 단"), SnareBuildRules::NextWallHeight(High), Low);
	bool bDefaultIsStop = false;
	for (const int32 Stop : SnareBuildRules::WallHeightStops)
	{
		bDefaultIsStop |= Stop == SnareBuildRules::WallHeightDefault;
	}
	TestTrue(TEXT("기본 높이는 단 안"), bDefaultIsStop);

	// 장식 앞(메시 +Y)이 방 안쪽(바깥 yaw 반대)을 본다 — 네 변 모두.
	for (int32 Side = 0; Side < 4; ++Side)
	{
		float OutwardYaw = 0.f;
		SnareBuildRules::WallSlotLocation(FIntPoint::ZeroValue, Side, 100.f, 0.f, OutwardYaw);
		const FVector Outward = FRotator(0.f, OutwardYaw, 0.f).Vector();
		const FVector Front = FRotator(0.f, SnareBuildRules::WallDecorRotation(Side), 0.f).RotateVector(FVector::RightVector);
		TestTrue(FString::Printf(TEXT("변 %d 장식 앞이 방 안쪽"), Side), FVector::DotProduct(Front, Outward) < -0.99f);
	}

	// 표의 벽 장식 7행(M108-02 셋 + 05c 넷)은 기능 `WallDecor` — 놓기가 바닥 대신 벽으로 간다.
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	int32 WallRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::WallDecor)
		{
			++WallRows;
			AddInfo(FString::Printf(TEXT("벽 장식 행 %s"), *Row->ItemID.ToString()));
		}
	}
	TestEqual(TEXT("벽 장식 행 수 (M108-02 셋 + 05c 넷 + 08b 네온 간판)"), WallRows, 8);
	return true;
}

// ⑪ 게스트 벽 장식을 호스트가 그 집 벽에 세운다 (M108-02, DESIGN 8.11 벽 걸기·멀티) — 바닥에 서지 않고, 같은 벽 간격·
// 벽 칸 밖·집 없음은 거절, 벽 장식은 바닥 칸을 막지 않는다. 검사 전용 값: 칸·계정·주소는 서버가 보낸 셈 치는 것이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHostSharesWallTest, "Snarehaven.Base.WallSlot.HostSharesGuestWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHostSharesWallTest::RunTest(const FString& Parameters)
{
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString HouseId(TEXT("tent_wall_test"));
	const FIntPoint HouseCell(40, 40);

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	const FBaseRuleRow* const Rules = Base->GetRules();
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestNotNull(TEXT("아이템 표"), Base->GetItemTable()))
	{
		return false;
	}
	// 행 ID 는 적지 않고 기능으로 — 텐트 하나, 벽 장식 하나, 바닥 가구 하나(1×1).
	FName TentItem;
	FName WallItem;
	FName FloorItem;
	int32 HouseCells = 1;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}
		if (Row->PlaceableFunction == EPlaceableFunction::Tent && TentItem.IsNone())
		{
			TentItem = Row->ItemID;
			HouseCells = FMath::Max(1, Row->FootprintX);
		}
		else if (Row->PlaceableFunction == EPlaceableFunction::WallDecor && WallItem.IsNone())
		{
			WallItem = Row->ItemID;
		}
		else if (Row->PlaceableFunction == EPlaceableFunction::None && Row->FootprintX == 1 && Row->FootprintY == 1
			&& FloorItem.IsNone())
		{
			FloorItem = Row->ItemID;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestFalse(TEXT("벽 장식 행"), WallItem.IsNone())
		|| !TestFalse(TEXT("1×1 바닥 가구 행"), FloorItem.IsNone()))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();
	const auto Find = [World](const FString& StructureId) -> ASnarePlacedStructureActor*
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (!It->IsPreview() && It->GetStructureId() == StructureId)
			{
				return *It;
			}
		}
		return nullptr;
	};

	// 집이 아직 이 화면에 없으면 걸 벽이 없다.
	const int32 Side = 1;
	const FIntPoint WallCell = HouseCell + FIntPoint(HouseCells - 1, 0);
	TestFalse(TEXT("집 없이 온 벽 장식은 거절"),
		Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_0"), WallCell, 0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 150));

	// 게스트 집(텐트)이 먼저 오고, 그 뒷벽(+X, 변 1)에 높이 150 장식.
	TestTrue(TEXT("게스트 집을 받는다"), Build->HostAcceptShared(TentItem, HouseId, FString(), HouseCell, 0, GuestOwner, NAME_None));
	TestTrue(TEXT("벽 장식을 받는다"),
		Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_1"), WallCell, 0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 150));
	const ASnarePlacedStructureActor* const House = Find(FString());
	const ASnarePlacedStructureActor* const Hung = Find(TEXT("st_wall_1"));
	if (TestNotNull(TEXT("집 액터"), House) && TestNotNull(TEXT("걸린 액터"), Hung))
	{
		const FVector Expected = ASnarePlacedStructureActor::WallToWorld(WallCell, Side, Rules->CellSizeCm,
			House->GetActorLocation().Z, 150);
		TestTrue(TEXT("자리 = 그 집 벽 칸 변·높이 150 (바닥 칸 가운데 아님)"), Hung->GetActorLocation().Equals(Expected, 0.5f));
		TestEqual(TEXT("돌림 = 방 안쪽"), Hung->GetRotation(), SnareBuildRules::WallDecorRotation(Side));
		TestTrue(TEXT("복제 액터"), Hung->GetIsReplicated());
		TestEqual(TEXT("벽 변 기억"), Hung->GetWallSide(), Side);
		AddInfo(FString::Printf(TEXT("집 칸 (%d,%d) %d칸 · 벽 칸 (%d,%d) 변 %d · 자리 %s · 돌림 %d"), HouseCell.X, HouseCell.Y,
			HouseCells, WallCell.X, WallCell.Y, Side, *Hung->GetActorLocation().ToCompactString(), Hung->GetRotation()));
	}

	// 같은 칸·변 높이 차 50 미만은 거절, 50 이상은 받는다 (서버 `WALL_ITEM_GAP`).
	TestFalse(TEXT("같은 벽 120(차 30)은 거절"),
		Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_2"), WallCell, 0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 120));
	TestTrue(TEXT("같은 벽 200(차 50)은 받는다"),
		Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_3"), WallCell, 0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 200));

	// 벽 칸 밖(집 가운데 칸의 변·집 밖 칸)·벽 장식 아닌 행을 벽으로·벽 장식을 바닥으로는 거절.
	TestFalse(TEXT("집 안쪽 칸 변은 거절"), Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_4"), HouseCell + FIntPoint(1, 1),
		0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 150));
	TestFalse(TEXT("집 밖 칸은 거절"), Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_5"), HouseCell + FIntPoint(HouseCells, 0),
		0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 150));
	TestFalse(TEXT("바닥 가구를 벽으로는 거절"), Build->HostAcceptShared(FloorItem, HouseId, TEXT("st_wall_6"), WallCell,
		0, GuestOwner, NAME_None, FIntPoint::ZeroValue, Side, 150));
	TestFalse(TEXT("벽 장식을 바닥으로는 거절"), Build->HostAcceptShared(WallItem, HouseId, TEXT("st_wall_7"), HouseCell + FIntPoint(8, 8),
		0, GuestOwner, NAME_None));
	TestNull(TEXT("거절된 것은 안 섬"), Find(TEXT("st_wall_7")));

	// 벽 장식은 바닥 칸을 막지 않는다 — 걸린 벽 바로 바깥(+X) 칸에 바닥 가구.
	TestTrue(TEXT("벽 바깥 바닥 칸은 비어 있음"), Build->HostAcceptShared(FloorItem, TEXT("tent_other"), TEXT("st_floor_1"),
		WallCell + FIntPoint(1, 0), 0, GuestOwner, NAME_None));

	// 치우기는 바닥 가구와 같은 길.
	TestTrue(TEXT("벽 장식도 주인 계정이면 치움"), Build->HostRemoveShared(TEXT("st_wall_1"), GuestOwner));
	TestNull(TEXT("치운 뒤 없음"), Find(TEXT("st_wall_1")));
	DestroyTestWorld(World);
	return true;
}

// ⑫ 게스트가 텐트를 집으로 올리면 호스트 화면의 그 텐트도 집이 된다 (DESIGN 8.11 집 올리기·멀티) — 전엔 이미 선 텐트라
// 공유가 「그대로 받음」으로 끝나 호스트 화면엔 계속 텐트였다. 처음부터 집으로 와도 집으로 선다. 검사 전용 값: 칸·계정·색 번호.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHostSharesHouseTest, "Snarehaven.Base.HostSharesGuestHouse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHostSharesHouseTest::RunTest(const FString& Parameters)
{
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString HouseId(TEXT("tent_house_test"));
	const FString LaterHouseId(TEXT("tent_house_test_2"));
	const int32 TestColor = 2;

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	FName TentItem;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			TentItem = Row->ItemID;
			break;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();
	const auto FindTent = [World](const FString& TentId) -> ASnarePlacedStructureActor*
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (!It->IsPreview() && It->IsTent() && It->GetTentId() == TentId)
			{
				return *It;
			}
		}
		return nullptr;
	};
	const auto CountTents = [World](const FString& TentId)
	{
		int32 Count = 0;
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			Count += (!It->IsPreview() && It->GetTentId() == TentId) ? 1 : 0;
		}
		return Count;
	};

	// 텐트로 먼저 온다 → 나중에 같은 주소가 집·색으로 다시 온다.
	TestTrue(TEXT("게스트 텐트를 받는다"), Build->HostAcceptShared(TentItem, HouseId, FString(), FIntPoint(40, 40), 0, GuestOwner, NAME_None));
	const ASnarePlacedStructureActor* const Tent = FindTent(HouseId);
	if (!TestNotNull(TEXT("텐트 액터"), Tent))
	{
		return false;
	}
	TestFalse(TEXT("처음엔 텐트"), Tent->IsHouse());
	const FLinearColor TentTint = Tent->GetTint();
	TestTrue(TEXT("집으로 올린 공유도 받는다"), Build->HostAcceptShared(TentItem, HouseId, FString(), FIntPoint(40, 40), 0, GuestOwner,
		NAME_None, FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, TestColor));
	TestTrue(TEXT("새로 세우지 않음(같은 액터)"), FindTent(HouseId) == Tent);
	TestEqual(TEXT("그 주소 액터 하나"), CountTents(HouseId), 1);
	TestTrue(TEXT("호스트 화면도 집"), Tent->IsHouse());
	TestEqual(TEXT("집 색 번호 = 공유된 값"), Tent->GetHouseColor(), TestColor);
	TestTrue(TEXT("색 = 그 집 색"), Tent->GetTint().Equals(ASnarePlacedStructureActor::HouseColorAt(TestColor), 0.01f));
	TestFalse(TEXT("텐트 색에서 바뀜"), Tent->GetTint().Equals(TentTint, 0.01f));
	TestTrue(TEXT("복제 액터(다른 게스트에게도 집으로 간다)"), Tent->GetIsReplicated());

	// 처음부터 집으로 온 텐트(호스트가 늦게 들어온 방 등)도 집으로 선다.
	TestTrue(TEXT("집으로 온 텐트를 받는다"), Build->HostAcceptShared(TentItem, LaterHouseId, FString(), FIntPoint(60, 60), 0, GuestOwner,
		NAME_None, FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, TestColor));
	const ASnarePlacedStructureActor* const Later = FindTent(LaterHouseId);
	if (TestNotNull(TEXT("둘째 집 액터"), Later))
	{
		TestTrue(TEXT("처음부터 집"), Later->IsHouse());
		TestEqual(TEXT("둘째 집 색 번호"), Later->GetHouseColor(), TestColor);
		AddInfo(FString::Printf(TEXT("텐트 색 %s → 집 색 %s (번호 %d)"), *TentTint.ToString(), *Tent->GetTint().ToString(), TestColor));
	}
	// 게스트가 2층을 올린 공유 — 같은 액터가 층수만 바뀐다 (M108-06c).
	TestTrue(TEXT("2층으로 올린 공유도 받는다"), Build->HostAcceptShared(TentItem, HouseId, FString(), FIntPoint(40, 40), 0, GuestOwner,
		NAME_None, FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, TestColor, FString(), -1, /*HouseFloors=*/2));
	TestTrue(TEXT("2층도 같은 액터"), FindTent(HouseId) == Tent);
	TestEqual(TEXT("호스트 화면 층수 = 공유된 값"), Tent->GetHouseFloors(), 2);
	DestroyTestWorld(World);
	return true;
}

// M108-02 걸 벽 — 집이 된 텐트는 3×3 오두막 몸통이 되고, 벽 칸 변마다 장식 뒤에 벽이 있다(문 칸은 문 위만).
// 몸통·덮개 메시에 복잡 충돌을 켠 시험 사본을 세워 변 자리에서 바깥으로 쏘면 벽에 맞고, 몸통 벽(앞벽 = 덮개 말고)은
// 안쪽으로 쏘면 비어 있어야 한다(앞벽 안쪽엔 열린 문짝이 붙는다 — 덮개째 안에서 숨는다).
// 높이는 단 표 전부 + 가장 키 큰 벽 장식 윗변(표 행 메시에서 잰다). 덮개는 집일 때만 보이고 안에 서면 숨는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHouseWallsTest, "Snarehaven.Base.WallSlot.HouseWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHouseWallsTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	FName TentItem;
	float DecorTopCm = 0.f;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}
		if (Row->PlaceableFunction == EPlaceableFunction::Tent && TentItem.IsNone())
		{
			TentItem = Row->ItemID;
		}
		const UStaticMesh* const Mesh = Row->PlaceableFunction == EPlaceableFunction::WallDecor ? Row->PlacedMeshRef.LoadSynchronous() : nullptr;
		if (Mesh != nullptr && Row->PlacedMeshWidthCm > 0.f)
		{
			const FVector Size = Mesh->GetBoundingBox().GetSize();
			DecorTopCm = FMath::Max(DecorTopCm, Size.Z * Row->PlacedMeshWidthCm / FMath::Max(Size.X, Size.Y));
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestTrue(TEXT("벽 장식 행 메시 높이"), DecorTopCm > 0.f))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	constexpr float CellCm = 100.f;
	constexpr int32 HouseCells = 3;
	constexpr int32 TestColor = 2;
	const FVector Center = SnareBuildRules::FootprintCenterHalf(FIntPoint::ZeroValue, FIntPoint(HouseCells, HouseCells), CellCm, 0.f);
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	ASnarePlacedStructureActor* const House = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform(Center), Params);
	House->InitStructure(TentItem, TEXT("house_walls_test"), FString(), FString(), 0, CellCm);
	House->FinishSpawning(FTransform(Center));
	const UStaticMesh* const TentMesh = House->GetBodyComponent()->GetStaticMesh();
	TestFalse(TEXT("텐트일 땐 덮개 숨김"), House->GetHouseRoofComponent()->IsVisible());

	House->SetHouse(true, TestColor);
	const UStaticMesh* const Shell = House->GetBodyComponent()->GetStaticMesh();
	if (!TestNotNull(TEXT("집 몸통 메시"), Shell) || !TestTrue(TEXT("텐트 메시에서 바뀜"), Shell != TentMesh)
		|| !TestTrue(TEXT("오두막 몸통"), Shell->GetName().Contains(TEXT("TentHouse_01"))))
	{
		DestroyTestWorld(World);
		return false;
	}
	const UStaticMeshComponent* const Roof = House->GetHouseRoofComponent();
	TestTrue(TEXT("집이면 덮개 보임(폰 없음 = 밖)"), Roof->IsVisible());
	FLinearColor RoofColor;
	const int32 RoofSlot = Roof->GetMaterialIndex(TEXT("Roof"));
	TestTrue(TEXT("덮개 지붕 슬롯 색 칸"), RoofSlot != INDEX_NONE
		&& Roof->GetMaterial(RoofSlot)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DiffuseColor")), RoofColor));
	TestTrue(TEXT("지붕 = 집 색"), RoofColor.Equals(ASnarePlacedStructureActor::HouseColorAt(TestColor), 0.01f));
	TestTrue(TEXT("색 = 집 색"), House->GetTint().Equals(ASnarePlacedStructureActor::HouseColorAt(TestColor), 0.01f));
	House->UpdateHouseRoof(Center + FVector(0.f, 0.f, 90.f));
	TestFalse(TEXT("안에 서면 덮개 숨김"), Roof->IsVisible());
	House->UpdateHouseRoof(Center + FVector(-4.f * CellCm, 0.f, 90.f));
	TestTrue(TEXT("밖으로 나가면 덮개 다시"), Roof->IsVisible());

	// 벽 찾기용 사본 — 같은 몸통·덮개 메시, 같은 자리, 복잡 충돌(삼각형)만 켬.
	const auto MakeProbe = [House](const UStaticMeshComponent* Source)
	{
		UStaticMeshComponent* const Probe = NewObject<UStaticMeshComponent>(House);
		Probe->SetStaticMesh(Source->GetStaticMesh());
		Probe->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Probe->SetWorldTransform(Source->GetComponentTransform());
		Probe->RegisterComponent();
		return Probe;
	};
	UStaticMeshComponent* const ShellProbe = MakeProbe(House->GetBodyComponent());
	UStaticMeshComponent* const CoverProbe = MakeProbe(Roof);
	const FCollisionQueryParams Complex(SCENE_QUERY_STAT(HouseWallsTest), true);
	constexpr float ProbeCm = 30.f;
	TArray<float> Heights;
	for (const int32 Stop : SnareBuildRules::WallHeightStops)
	{
		Heights.Add(Stop);
	}
	Heights.Add(SnareBuildRules::WallHeightStops[UE_ARRAY_COUNT(SnareBuildRules::WallHeightStops) - 1] + DecorTopCm - 1.f);
	int32 Backed = 0;
	int32 Doorway = 0;
	float MaxGap = -1.f;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		for (int32 Along = 0; Along < HouseCells; ++Along)
		{
			const bool bAlongX = Side == 0 || Side == 2;
			const int32 Edge = (Side == 0 || Side == 1) ? HouseCells - 1 : 0;
			const FIntPoint Cell = bAlongX ? FIntPoint(Along, Edge) : FIntPoint(Edge, Along);
			for (const float Height : Heights)
			{
				float OutwardYaw = 0.f;
				const FVector Slot = SnareBuildRules::WallSlotLocation(Cell, Side, CellCm, Height, OutwardYaw);
				const FVector Out = FRotator(0.f, OutwardYaw, 0.f).Vector();
				FHitResult Hit;
				const bool bWall = ShellProbe->LineTraceComponent(Hit, Slot - Out * 2.f, Slot + Out * ProbeCm, Complex)
					|| CoverProbe->LineTraceComponent(Hit, Slot - Out * 2.f, Slot + Out * ProbeCm, Complex);
				// 문(액터 돌림 0 → -X 변 3 가운데 칸)은 문 높이 위에만 벽이 있다 — 문간은 비어 있어야 드나든다.
				const bool bDoorCell = Side == 3 && Along == HouseCells / 2;
				if (bDoorCell && !bWall)
				{
					++Doorway;
					continue;
				}
				Backed += bWall ? 1 : 0;
				TestTrue(*FString::Printf(TEXT("변 %d 칸 (%d,%d) 높이 %.0f 장식 뒤에 벽"), Side, Cell.X, Cell.Y, Height), bWall);
				// 장식 뒷면(= 자리)과 벽 면 틈 0~2cm — 뜨지도 뚫지도 않는다 (감독 결함 10-20 08:10). 쏘기는 자리 2cm 앞에서 시작.
				if (bWall)
				{
					const float Gap = Hit.Distance - 2.f;
					MaxGap = FMath::Max(MaxGap, Gap);
					TestTrue(*FString::Printf(TEXT("변 %d 칸 (%d,%d) 높이 %.0f 벽 면 틈 %.2f cm 가 0~2"), Side, Cell.X, Cell.Y, Height, Gap),
						Gap >= -0.1f && Gap <= 2.f);
				}
				FHitResult Inside;
				TestFalse(*FString::Printf(TEXT("변 %d 칸 (%d,%d) 높이 %.0f 앞(방 안)은 비어 있음"), Side, Cell.X, Cell.Y, Height),
					Side != 3 && ShellProbe->LineTraceComponent(Inside, Slot - Out * 2.f, Slot - Out * ProbeCm, Complex));
			}
		}
	}
	// 문간은 문 높이 아래 단만 비고, 가장 높은 줄(장식 윗변)은 문 위 벽에 걸린다.
	TestTrue(TEXT("문간 빼고 전부 벽"), Backed == 4 * HouseCells * Heights.Num() - Doorway);
	TestTrue(TEXT("문 칸도 맨 윗줄은 벽"), Doorway > 0 && Doorway < Heights.Num());
	AddInfo(FString::Printf(TEXT("벽 뒤 확인 %d · 문간 비어 있음 %d · 장식 윗변 %.1f cm · 벽 면 틈 최대 %.2f cm · 몸통 범위 %s"),
		Backed, Doorway, DecorTopCm, MaxGap, *Shell->GetBoundingBox().ToString()));

	House->SetHouse(false, 0);
	TestTrue(TEXT("집이 아니면 텐트 메시로"), House->GetBodyComponent()->GetStaticMesh() == TentMesh);
	TestFalse(TEXT("집이 아니면 덮개 숨김"), Roof->IsVisible());
	DestroyTestWorld(World);
	return true;
}

// ⑫ 위에 올리기 슬롯 (M108-03, DESIGN 8.11 위에 올리기) — 슬롯 규칙 함수, 표의 받침 슬롯 수·소품 행이 서버 `TOP_HOSTS`·`TOP_ITEMS` 와
// 같은지, 받침 액터의 슬롯 자리가 몸 윗면 위·긴 변 안이고 서로 소품 가로 한도만큼 떨어졌는지, 그 자리에 세운 소품 밑면이 윗면에 닿는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseTopSlotTest, "Snarehaven.Base.TopSlot.Place",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseTopSlotTest::RunTest(const FString& Parameters)
{
	constexpr float ToleranceCm = 0.5f;
	// 시험 전용 슬롯 수 — 표 값이 바뀌어도 규칙 함수 시험은 안 깨진다.
	TestEqual(TEXT("셋 중 첫 슬롯 = -1/3"), SnareBuildRules::TopSlotAlong(0, 3), -1.f / 3.f);
	TestEqual(TEXT("셋 중 가운데 = 0"), SnareBuildRules::TopSlotAlong(1, 3), 0.f);
	TestEqual(TEXT("둘 중 둘째 = 1/4"), SnareBuildRules::TopSlotAlong(1, 2), 0.25f);
	TestEqual(TEXT("범위 밖 슬롯 = 0"), SnareBuildRules::TopSlotAlong(3, 3), 0.f);
	TestEqual(TEXT("끝 슬롯 다음 = 처음"), SnareBuildRules::NextTopSlot(2, 3), 0);
	TestEqual(TEXT("안 고름 다음 = 처음"), SnareBuildRules::NextTopSlot(-1, 3), 0);
	TestEqual(TEXT("슬롯 없는 받침 = -1"), SnareBuildRules::NextTopSlot(0, 0), -1);
	TestEqual(TEXT("빈 슬롯 = 초록"), SnareBuildRules::EvaluateTopPlace(1, 3, { 0, 2 }), ESnarePlacementVerdict::Green);
	TestEqual(TEXT("찬 슬롯 = 빨강"), SnareBuildRules::EvaluateTopPlace(2, 3, { 0, 2 }), ESnarePlacementVerdict::Red);
	TestEqual(TEXT("슬롯 범위 밖 = 파랑"), SnareBuildRules::EvaluateTopPlace(3, 3, {}), ESnarePlacementVerdict::Blue);
	TestEqual(TEXT("받침 아님 = 파랑"), SnareBuildRules::EvaluateTopPlace(0, 0, {}), ESnarePlacementVerdict::Blue);

	// 서버 원본에서 받침·소품 목록을 읽는다 — 클라 표와 한 줄씩 같아야 미리보기 색이 서버 판정과 맞는다.
	FString Server;
	if (!TestTrue(TEXT("서버 원본을 읽는다"), FFileHelper::LoadFileToString(Server,
		*(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}
	TMap<FName, int32> ServerHosts;
	FRegexMatcher HostsLine(FRegexPattern(TEXT(R"(var TOP_HOSTS = \{([^}]*)\})")), Server);
	if (HostsLine.FindNext())
	{
		FRegexMatcher Pair(FRegexPattern(TEXT(R"re("([\w.]+)": (\d+))re")), HostsLine.GetCaptureGroup(1));
		while (Pair.FindNext())
		{
			ServerHosts.Add(FName(*Pair.GetCaptureGroup(1)), FCString::Atoi(*Pair.GetCaptureGroup(2)));
		}
	}
	TSet<FName> ServerTops;
	FRegexMatcher TopsLine(FRegexPattern(TEXT(R"(var TOP_ITEMS = \[([^\]]*)\])")), Server);
	if (TopsLine.FindNext())
	{
		FRegexMatcher Item(FRegexPattern(TEXT(R"re("([\w.]+)")re")), TopsLine.GetCaptureGroup(1));
		while (Item.FindNext())
		{
			ServerTops.Add(FName(*Item.GetCaptureGroup(1)));
		}
	}
	TestTrue(TEXT("서버 받침 목록을 읽었다"), ServerHosts.Num() > 0);
	TestTrue(TEXT("서버 소품 목록을 읽었다"), ServerTops.Num() > 0);

	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}
	TMap<FName, int32> ClientHosts;
	TSet<FName> ClientTops;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->TopSlots > 0)
		{
			ClientHosts.Add(Row->ItemID, Row->TopSlots);
		}
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::TopDecor)
		{
			ClientTops.Add(Row->ItemID);
		}
	}
	TestEqual(TEXT("받침 수 = 서버"), ClientHosts.Num(), ServerHosts.Num());
	for (const TPair<FName, int32>& Host : ServerHosts)
	{
		const int32* const Slots = ClientHosts.Find(Host.Key);
		TestTrue(*FString::Printf(TEXT("[%s] 표 슬롯 %d = 서버 %d"), *Host.Key.ToString(), Slots != nullptr ? *Slots : -1, Host.Value),
			Slots != nullptr && *Slots == Host.Value);
	}
	TestEqual(TEXT("소품 행 수 = 서버"), ClientTops.Num(), ServerTops.Num());
	for (const FName& Top : ServerTops)
	{
		TestTrue(*FString::Printf(TEXT("[%s] 표 기능 TopDecor"), *Top.ToString()), ClientTops.Contains(Top));
	}

	// 받침마다 — 돌림 90(데모와 같다)과 세밀 15°. 슬롯 자리 = 몸 꼭대기 높이, 몸 수평 경계 안, 이웃 슬롯과 소품 가로 한도(40) 이상.
	// 가장 넓은 소품을 가운데 슬롯에 세워 밑면이 윗면에 닿는지도 본다.
	FName WidestTop;
	float WidestCm = 0.f;
	for (const TPair<FName, uint8*>& Pair : Items->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::TopDecor && Row->PlacedMeshWidthCm > WidestCm)
		{
			WidestTop = Row->ItemID;
			WidestCm = Row->PlacedMeshWidthCm;
		}
	}
	constexpr float TopItemMaxWidthCm = 40.f;
	UWorld* const World = CreateTestWorld();
	for (const TPair<FName, int32>& Host : ClientHosts)
	{
		for (const int32 Rotation : { 90, 15 })
		{
			FActorSpawnParameters Params;
			Params.bDeferConstruction = true;
			ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
				ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
			Actor->InitStructure(Host.Key, TestTentId, TestStructureId, FString(), Rotation, 100.f);
			Actor->FinishSpawning(FTransform(FVector(1000.f, 2000.f, 0.f)));
			const UStaticMeshComponent* const Body = Actor->GetBodyComponent();
			TestEqual(*FString::Printf(TEXT("[%s] 액터 슬롯 수 = 표"), *Host.Key.ToString()), Actor->GetTopSlotCount(), Host.Value);
			if (!TestNotNull(TEXT("받침 몸"), Body))
			{
				continue;
			}
			const FBox Box = Body->CalcBounds(Body->GetComponentTransform()).GetBox();
			TArray<FVector> Spots;
			for (int32 Slot = 0; Slot < Actor->GetTopSlotCount(); ++Slot)
			{
				FVector At;
				const bool bHas = Actor->GetTopSlotLocation(Slot, At);
				TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] 슬롯 %d 자리 있음"), *Host.Key.ToString(), Rotation, Slot), bHas);
				TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] 슬롯 %d 높이 %.1f = 몸 꼭대기 %.1f"), *Host.Key.ToString(), Rotation, Slot,
					At.Z, Box.Max.Z), FMath::Abs(At.Z - Box.Max.Z) <= ToleranceCm);
				TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] 슬롯 %d 몸 수평 경계 안"), *Host.Key.ToString(), Rotation, Slot),
					Box.IsInsideXY(At));
				for (const FVector& Other : Spots)
				{
					TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] 슬롯 %d 이웃 간격 %.1f >= %.0f"), *Host.Key.ToString(), Rotation, Slot,
						FVector::Dist2D(At, Other), TopItemMaxWidthCm), FVector::Dist2D(At, Other) + ToleranceCm >= TopItemMaxWidthCm);
				}
				Spots.Add(At);
				AddInfo(FString::Printf(TEXT("[%s 돌림 %d] 슬롯 %d 자리 %s · 몸 꼭대기 z %.1f"), *Host.Key.ToString(), Rotation, Slot,
					*At.ToCompactString(), Box.Max.Z));
			}
			// 소품 크기 보정(루프#1382) — 가장 넓은 소품도 이웃 슬롯 간격(슬롯 칸) 안이라 옆 소품과 겹치지 않는다.
			float NarrowestGap = TNumericLimits<float>::Max();
			for (int32 First = 0; First < Spots.Num(); ++First)
			{
				for (int32 Second = First + 1; Second < Spots.Num(); ++Second)
				{
					NarrowestGap = FMath::Min(NarrowestGap, static_cast<float>(FVector::Dist2D(Spots[First], Spots[Second])));
				}
			}
			if (Spots.Num() > 1)
			{
				TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] 가장 넓은 소품 %s %.0f <= 슬롯 칸 %.1f"), *Host.Key.ToString(), Rotation,
					*WidestTop.ToString(), WidestCm, NarrowestGap), WidestCm <= NarrowestGap);
			}
			FVector Middle;
			TestFalse(*FString::Printf(TEXT("[%s] 범위 밖 슬롯은 자리 없음"), *Host.Key.ToString()),
				Actor->GetTopSlotLocation(Actor->GetTopSlotCount(), Middle));
			if (!WidestTop.IsNone() && Actor->GetTopSlotLocation(Actor->GetTopSlotCount() / 2, Middle))
			{
				ASnarePlacedStructureActor* const Prop = World->SpawnActor<ASnarePlacedStructureActor>(
					ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
				Prop->InitStructure(WidestTop, TestTentId, TEXT("top_test_prop"), FString(), Rotation, 100.f);
				Prop->SetTopSlot(TestStructureId, Actor->GetTopSlotCount() / 2);
				Prop->FinishSpawning(FTransform(Middle));
				const UStaticMeshComponent* const PropBody = Prop->GetBodyComponent();
				const FBox PropBox = PropBody != nullptr ? PropBody->CalcBounds(PropBody->GetComponentTransform()).GetBox() : FBox(ForceInit);
				TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] %s 밑면 %.1f = 윗면 %.1f"), *Host.Key.ToString(), Rotation,
					*WidestTop.ToString(), PropBox.Min.Z, Box.Max.Z), PropBox.IsValid && FMath::Abs(PropBox.Min.Z - Box.Max.Z) <= ToleranceCm);
				TestTrue(*FString::Printf(TEXT("[%s 돌림 %d] 소품은 위에 올림"), *Host.Key.ToString(), Rotation), Prop->IsOnTop());
				Prop->Destroy();
			}
			Actor->Destroy();
		}
	}
	DestroyTestWorld(World);
	return true;
}

// ⑭ 게스트 소품을 호스트가 그 받침 윗면 슬롯에 세운다 (M108-03, DESIGN 8.11 위에 올리기·멀티) — 바닥에 서지 않고,
// 받침 없음·슬롯 밖·찬 슬롯·소품 아닌 행을 받침으로·소품을 바닥으로는 거절, 소품은 바닥 칸을 막지 않는다.
// 검사 전용 값: 칸·계정·주소는 서버가 보낸 셈 치는 것이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHostSharesTopTest, "Snarehaven.Base.TopSlot.HostSharesGuestTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHostSharesTopTest::RunTest(const FString& Parameters)
{
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString GuestTent(TEXT("tent_top_test"));
	const FString HostId(TEXT("st_host"));
	const FIntPoint HostCell(40, 40);

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	if (!TestNotNull(TEXT("규칙 행"), Base->GetRules()) || !TestNotNull(TEXT("아이템 표"), Base->GetItemTable()))
	{
		return false;
	}
	// 행 ID 는 적지 않고 기능·칸으로 — 받침 하나(슬롯 가장 많은 것), 소품 하나, 받침 아닌 1×1 바닥 가구 하나.
	FName HostItem;
	int32 HostSlots = 0;
	FName TopItem;
	FName FloorItem;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}
		if (Row->TopSlots > HostSlots)
		{
			HostItem = Row->ItemID;
			HostSlots = Row->TopSlots;
		}
		if (Row->PlaceableFunction == EPlaceableFunction::TopDecor && TopItem.IsNone())
		{
			TopItem = Row->ItemID;
		}
		else if (Row->PlaceableFunction == EPlaceableFunction::None && Row->TopSlots <= 0 && Row->FootprintX == 1
			&& Row->FootprintY == 1 && FloorItem.IsNone())
		{
			FloorItem = Row->ItemID;
		}
	}
	if (!TestFalse(TEXT("받침 행"), HostItem.IsNone()) || !TestFalse(TEXT("소품 행"), TopItem.IsNone())
		|| !TestFalse(TEXT("1×1 바닥 가구 행"), FloorItem.IsNone()) || !TestTrue(TEXT("받침 슬롯 2 이상"), HostSlots >= 2))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();
	const auto Find = [World](const FString& StructureId) -> ASnarePlacedStructureActor*
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (!It->IsPreview() && It->GetStructureId() == StructureId)
			{
				return *It;
			}
		}
		return nullptr;
	};
	const auto Share = [Build, &TopItem, &GuestTent, &GuestOwner, &HostCell](const TCHAR* StructureId, const FString& OnHost, int32 Slot)
	{
		return Build->HostAcceptShared(TopItem, GuestTent, StructureId, HostCell, 0, GuestOwner, NAME_None,
			FIntPoint::ZeroValue, -1, 0, false, 0, OnHost, Slot);
	};

	// 받침이 아직 이 화면에 없으면 올릴 데가 없다.
	TestFalse(TEXT("받침 없이 온 소품은 거절"), Share(TEXT("st_top_0"), HostId, 0));

	// 게스트 받침(돌림 90)이 먼저 오고, 그 슬롯 1 에 소품.
	TestTrue(TEXT("게스트 받침을 받는다"), Build->HostAcceptShared(HostItem, GuestTent, HostId, HostCell, 90, GuestOwner, NAME_None));
	TestTrue(TEXT("소품을 받는다"), Share(TEXT("st_top_1"), HostId, 1));
	const ASnarePlacedStructureActor* const Host = Find(HostId);
	const ASnarePlacedStructureActor* const Prop = Find(TEXT("st_top_1"));
	if (TestNotNull(TEXT("받침 액터"), Host) && TestNotNull(TEXT("소품 액터"), Prop))
	{
		FVector Expected;
		TestTrue(TEXT("받침 슬롯 1 자리 있음"), Host->GetTopSlotLocation(1, Expected));
		TestTrue(TEXT("자리 = 받침 윗면 슬롯 1 (바닥 칸 가운데 아님)"), Prop->GetActorLocation().Equals(Expected, 0.5f));
		TestEqual(TEXT("돌림 = 받침 돌림"), Prop->GetRotation(), Host->GetRotation());
		TestTrue(TEXT("복제 액터"), Prop->GetIsReplicated());
		TestTrue(TEXT("받침·슬롯 기억"), Prop->IsOnTop() && Prop->GetTopHostId() == HostId && Prop->GetTopSlot() == 1);
		AddInfo(FString::Printf(TEXT("받침 %s 슬롯 %d · 받침 자리 %s · 소품 자리 %s · 돌림 %d"), *HostItem.ToString(), HostSlots,
			*Host->GetActorLocation().ToCompactString(), *Prop->GetActorLocation().ToCompactString(), Prop->GetRotation()));
	}

	// 찬 슬롯·슬롯 밖·없는 받침은 거절, 빈 슬롯은 받는다 (서버 `topPlaceFail`).
	TestFalse(TEXT("찬 슬롯 1 은 거절"), Share(TEXT("st_top_2"), HostId, 1));
	TestFalse(TEXT("슬롯 밖은 거절"), Share(TEXT("st_top_3"), HostId, HostSlots));
	TestFalse(TEXT("음수 슬롯은 거절"), Share(TEXT("st_top_4"), HostId, -1));
	TestFalse(TEXT("없는 받침은 거절"), Share(TEXT("st_top_5"), TEXT("st_nowhere"), 0));
	TestTrue(TEXT("빈 슬롯 0 은 받는다"), Share(TEXT("st_top_6"), HostId, 0));
	TestNull(TEXT("거절된 것은 안 섬"), Find(TEXT("st_top_2")));

	// 소품 아닌 행을 받침으로·소품을 바닥으로·받침 아닌 가구 위로는 거절.
	TestFalse(TEXT("바닥 가구를 받침 위로는 거절"), Build->HostAcceptShared(FloorItem, GuestTent, TEXT("st_top_7"), HostCell, 0,
		GuestOwner, NAME_None, FIntPoint::ZeroValue, -1, 0, false, 0, HostId, 2));
	TestFalse(TEXT("소품을 바닥으로는 거절"), Build->HostAcceptShared(TopItem, GuestTent, TEXT("st_top_8"), HostCell + FIntPoint(8, 8), 0,
		GuestOwner, NAME_None));
	TestTrue(TEXT("받침 아닌 바닥 가구를 받는다"), Build->HostAcceptShared(FloorItem, GuestTent, TEXT("st_floor_1"), HostCell + FIntPoint(6, 0),
		0, GuestOwner, NAME_None));
	TestFalse(TEXT("받침 아닌 가구 위는 거절"), Share(TEXT("st_top_9"), TEXT("st_floor_1"), 0));

	// 소품은 바닥 칸을 막지 않는다 — 받침 발자국 바로 바깥 네 쪽 칸에 바닥 가구(소품 자리를 발자국으로 읽으면 끝 슬롯이 옆 칸을 막는다).
	const FIntPoint HostSize = Host != nullptr
		? SnareBuildRules::RotatedFootprint(Host->GetFootprint().X, Host->GetFootprint().Y, Host->GetRotation()) : FIntPoint(1, 1);
	// 양 끝 슬롯이 닿는 네 쪽 칸 — 발자국 첫 줄·끝 줄의 바깥 (같은 칸이 겹치면 한 번만).
	TSet<FIntPoint> Outside;
	for (int32 Y = 0; Y < HostSize.Y; Y += FMath::Max(1, HostSize.Y - 1))
	{
		Outside.Add(FIntPoint(-1, Y));
		Outside.Add(FIntPoint(HostSize.X, Y));
	}
	for (int32 X = 0; X < HostSize.X; X += FMath::Max(1, HostSize.X - 1))
	{
		Outside.Add(FIntPoint(X, -1));
		Outside.Add(FIntPoint(X, HostSize.Y));
	}
	int32 Index = 0;
	for (const FIntPoint& Offset : Outside)
	{
		const FIntPoint Next = HostCell + Offset;
		TestTrue(*FString::Printf(TEXT("받침 밖 칸 (%d,%d) 은 비어 있음"), Next.X, Next.Y),
			Build->HostAcceptShared(FloorItem, TEXT("tent_other"), FString::Printf(TEXT("st_side_%d"), Index++), Next, 0, GuestOwner, NAME_None));
	}
	AddInfo(FString::Printf(TEXT("받침 발자국 %dx%d · 밖 칸 %d 확인"), HostSize.X, HostSize.Y, Outside.Num()));

	// 치우기는 바닥 가구와 같은 길.
	TestTrue(TEXT("소품도 주인 계정이면 치움"), Build->HostRemoveShared(TEXT("st_top_1"), GuestOwner));
	TestNull(TEXT("치운 뒤 없음"), Find(TEXT("st_top_1")));
	TestTrue(TEXT("치운 슬롯 1 에 다시 받는다"), Share(TEXT("st_top_10"), HostId, 1));
	DestroyTestWorld(World);
	return true;
}

// ⑬ 러그 깔기 (DESIGN 8.11 「러그 깔기」, M108-05b) — 러그와 바닥 가구는 같은 칸을 나눠 쓰고, 러그끼리·텐트 몸과는 겹침.
// 미리보기(창구 판정)와 호스트 복제(게스트 러그·가구 받기) 두 길을 같이 본다. 칸은 시험 값.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseRugTest, "Snarehaven.Base.Place.RugUnderFurniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseRugTest::RunTest(const FString& Parameters)
{
	const FName Rug(TEXT("item.rug_square"));
	const FName Mat(TEXT("item.doormat"));
	const FName Table(TEXT("item.table_round"));
	const int32 Green = static_cast<int32>(ESnarePlacementVerdict::Green);
	const int32 Red = static_cast<int32>(ESnarePlacementVerdict::Red);

	// 표의 러그 넷 + 통나무 디딤 길(M108-05d) = `Rug` 기능(서버 `RUG_ITEMS` 와 같은 목록).
	USnareBaseSubsystem* const Empty = MakeBase(*this, nullptr);
	for (const TCHAR* Id : { TEXT("item.rug_round"), TEXT("item.rug_runner"), TEXT("item.rug_square"), TEXT("item.doormat"), TEXT("item.path_log") })
	{
		const FItemRow* const Row = Empty->FindItemRow(FName(Id));
		if (TestNotNull(*FString::Printf(TEXT("[%s] 표 행"), Id), Row))
		{
			TestEqual(*FString::Printf(TEXT("[%s] 러그 기능"), Id), static_cast<int32>(Row->PlaceableFunction), static_cast<int32>(EPlaceableFunction::Rug));
		}
	}

	// 미리보기 — 러그(3×2) 하나가 (4,0) 에 깔린 땅.
	FSnareTentMirror Tent = MakeTent(TestTentId, FIntPoint(0, 0));
	FSnareStructureMirror Placed;
	Placed.StructureId = TestStructureId;
	Placed.ItemID = Rug;
	Placed.Cell = FIntPoint(4, 0);
	Tent.Structures.Add(Placed);
	USnareBaseSubsystem* const WithRug = MakeBase(*this, nullptr);
	WithRug->AddTentMirrorForTest(Tent);
	auto Verdict = [](USnareBaseSubsystem* Base, FName Item, FIntPoint Cell)
	{
		return static_cast<int32>(Base->EvaluatePlacement(Item, Cell, 0, true));
	};
	TestEqual(TEXT("러그 위에 탁자는 초록"), Verdict(WithRug, Table, FIntPoint(5, 1)), Green);
	TestEqual(TEXT("러그 위에 러그는 빨강"), Verdict(WithRug, Mat, FIntPoint(6, 1)), Red);
	TestEqual(TEXT("러그 옆 빈 칸에 깔개는 초록"), Verdict(WithRug, Mat, FIntPoint(7, 0)), Green);
	TestEqual(TEXT("텐트 몸 위 러그는 빨강"), Verdict(WithRug, Mat, FIntPoint(1, 1)), Red);

	// 놓인 탁자 밑에 깔개 — 순서가 거꾸로여도 초록, 탁자 위에 탁자는 그대로 빨강.
	FSnareTentMirror WithTableTent = MakeTent(TestTentId, FIntPoint(0, 0));
	Placed.ItemID = Table;
	Placed.Cell = FIntPoint(4, 4);
	WithTableTent.Structures.Add(Placed);
	USnareBaseSubsystem* const WithTable = MakeBase(*this, nullptr);
	WithTable->AddTentMirrorForTest(WithTableTent);
	TestEqual(TEXT("탁자 밑에 깔개는 초록"), Verdict(WithTable, Mat, FIntPoint(4, 4)), Green);
	TestEqual(TEXT("탁자 위에 탁자는 빨강"), Verdict(WithTable, Table, FIntPoint(4, 4)), Red);

	// 호스트 복제 — 게스트 러그 위에 다른 게스트 탁자를 받고, 러그 위 러그는 거절.
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();
	const FString Guest(TEXT("GUEST_TEST_ID"));
	const FIntPoint Spot(40, 40);
	TestTrue(TEXT("게스트 러그를 받는다"), Build->HostAcceptShared(Rug, TestTentId, TEXT("st_rug_1"), Spot, 0, Guest, NAME_None));
	TestTrue(TEXT("러그 위 게스트 탁자를 받는다"), Build->HostAcceptShared(Table, TestTentId, TEXT("st_table_1"), Spot + FIntPoint(1, 1), 0, Guest, NAME_None));
	TestFalse(TEXT("러그 위 러그는 거절"), Build->HostAcceptShared(Mat, TestTentId, TEXT("st_mat_1"), Spot + FIntPoint(2, 0), 0, Guest, NAME_None));
	TestFalse(TEXT("탁자 위 탁자는 거절"), Build->HostAcceptShared(Table, TestTentId, TEXT("st_table_2"), Spot + FIntPoint(1, 1), 0, Guest, NAME_None));
	TestTrue(TEXT("러그 옆 빈 땅 깔개는 받는다"), Build->HostAcceptShared(Mat, TestTentId, TEXT("st_mat_2"), Spot + FIntPoint(-1, 0), 0, Guest, NAME_None));
	return true;
}

// M108-06c 위층 몸 (명세 `docs/specs/house-stairs.md` 3·5절) — 2층 집이면 덮개가 1층 앞벽 + 위층 덮개(2층 바닥 높이)로 갈리고
// 위층 벽·2층 바닥 판 3×3 이 선다. 계단(행 기능 `Stairs`)이 덮은 칸만 판이 비고(계단이 먼저 서든 집이 먼저 서든), 계단을 치우면 메운다.
// 위층 벽·판은 폰만 막는다: 폰 채널로 쏘면 판 윗면(+275)·위층 벽에 맞고, 바닥 찾기(움직이지 않는 것만)·카메라는 지나간다.
// 1층 집으로 돌리면 옛 덮개·높이 0 으로 돌아간다. 안에 서면 위층도 덮개와 같이 숨는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseUpperBodyTest, "Snarehaven.Base.Floor2.UpperBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseUpperBodyTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	FName TentItem;
	FName StairsItem;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent && TentItem.IsNone())
		{
			TentItem = Row->ItemID;
		}
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Stairs && StairsItem.IsNone())
		{
			StairsItem = Row->ItemID;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestFalse(TEXT("계단 행"), StairsItem.IsNone()))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	constexpr float CellCm = 100.f;
	constexpr int32 HouseCells = 3;
	constexpr int32 FloorTiles = HouseCells * HouseCells;
	constexpr int32 StairsLength = 3;
	const float UpperZ = SnareBuildRules::UpperFloorZCm;
	const FVector Center = SnareBuildRules::FootprintCenterHalf(FIntPoint::ZeroValue, FIntPoint(HouseCells, HouseCells), CellCm, 0.f);
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarePlacedStructureActor* const House = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform(Center), Params);
	House->InitStructure(TentItem, TEXT("upper_body_test"), FString(), FString(), 0, CellCm);
	House->FinishSpawning(FTransform(Center));
	House->SetHouse(true, 1, 1);
	const UStaticMeshComponent* const Roof = House->GetHouseRoofComponent();
	const UStaticMesh* const GroundRoofMesh = Roof->GetStaticMesh();
	const UInstancedStaticMeshComponent* const Floor = House->GetUpperFloorComponent();
	TestFalse(TEXT("1층 집은 2층 집 아님"), House->IsTwoStoryHouse());
	TestEqual(TEXT("1층 집은 2층 판 0"), Floor->GetInstanceCount(), 0);
	TestFalse(TEXT("1층 집은 위층 벽 숨김"), House->GetHouseUpperComponent()->IsVisible());

	House->SetHouse(true, 1, 2);
	if (!TestTrue(TEXT("2층 집"), House->IsTwoStoryHouse()))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("위층 덮개 메시"), Roof->GetStaticMesh() != nullptr && Roof->GetStaticMesh()->GetName().Contains(TEXT("TentHouseUpperRoof_01")));
	TestEqual(TEXT("지붕이 2층 바닥 높이만큼 오름"), static_cast<float>(Roof->GetRelativeLocation().Z), UpperZ);
	TestTrue(TEXT("1층 앞벽 메시"), House->GetHouseFrontComponent()->GetStaticMesh() != nullptr
		&& House->GetHouseFrontComponent()->GetStaticMesh()->GetName().Contains(TEXT("TentHouseFront_01")));
	TestTrue(TEXT("밖(폰 없음)에선 1층 앞벽·위층 벽·판·덮개 보임"), House->GetHouseFrontComponent()->IsVisible()
		&& House->GetHouseUpperComponent()->IsVisible() && Floor->IsVisible() && Roof->IsVisible());
	TestEqual(TEXT("계단 없으면 판 9칸"), Floor->GetInstanceCount(), FloorTiles);
	// 1층 앞벽 + 위층 덮개 = 옛 덮개 높이 + 2층 — 위층 덮개 꼭대기가 1층 덮개 꼭대기 + 275.
	const float GroundRoofTop = GroundRoofMesh->GetBoundingBox().Max.Z;
	TestTrue(*FString::Printf(TEXT("지붕 꼭대기 %.0f = 1층 지붕 %.0f + %.0f"), Roof->Bounds.GetBox().Max.Z - Center.Z, GroundRoofTop, UpperZ),
		FMath::IsNearlyEqual(Roof->Bounds.GetBox().Max.Z - Center.Z, GroundRoofTop + UpperZ, 1.f));

	// 계단 = 오른 줄(Y 칸 2) 전체, 돌림 0(긴 변 X).
	const auto SpawnStairs = [&]()
	{
		const FVector At = SnareBuildRules::FootprintCenterHalf(SnareBuildRules::JoinHalfUnits(FIntPoint(0, HouseCells - 1), FIntPoint::ZeroValue),
			SnareBuildRules::RotatedFootprint(StairsLength, 1, 0), CellCm, 0.f);
		ASnarePlacedStructureActor* const Stairs = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform(At), Params);
		Stairs->InitStructure(StairsItem, TEXT("upper_body_test"), TEXT("st_stairs"), FString(), 0, CellCm);
		Stairs->FinishSpawning(FTransform(At));
		return Stairs;
	};
	ASnarePlacedStructureActor* Stairs = SpawnStairs();
	TestEqual(TEXT("계단이 서면 계단 칸 3 이 빔"), Floor->GetInstanceCount(), FloorTiles - StairsLength);
	int32 InHole = 0;
	for (int32 Index = 0; Index < Floor->GetInstanceCount(); ++Index)
	{
		FTransform Tile;
		Floor->GetInstanceTransform(Index, Tile, /*bWorldSpace=*/true);
		InHole += Tile.GetLocation().Y > Center.Y + CellCm * 0.5f ? 1 : 0;
		TestTrue(*FString::Printf(TEXT("판 %d 높이 = 2층 바닥"), Index), FMath::IsNearlyEqual(Tile.GetLocation().Z - Center.Z, UpperZ, 0.1f));
	}
	TestEqual(TEXT("계단 줄엔 판 0"), InHole, 0);

	// 폰 채널: 판 윗면·위층 벽에 맞는다. 계단 구멍은 지나간다.
	const auto TracePawn = [World](const FVector& From, const FVector& To, FHitResult& Hit)
	{
		return World->LineTraceSingleByChannel(Hit, From, To, ECC_Pawn, FCollisionQueryParams(SCENE_QUERY_STAT(UpperBodyTest), true));
	};
	const FVector RoomCell = Center + FVector(0.f, -CellCm, 0.f);
	const FVector HoleCell = Center + FVector(0.f, CellCm, 0.f);
	FHitResult Hit;
	const bool bFloorHit = TracePawn(RoomCell + FVector(0.f, 0.f, UpperZ + 100.f), RoomCell + FVector(0.f, 0.f, UpperZ - 100.f), Hit);
	TestTrue(*FString::Printf(TEXT("폰은 2층 판 윗면에 섬 (맞음 %d · 높이 %.1f)"), bFloorHit, Hit.ImpactPoint.Z - Center.Z),
		bFloorHit && FMath::IsNearlyEqual(Hit.ImpactPoint.Z - Center.Z, UpperZ, 1.f));
	TestFalse(TEXT("계단 구멍은 판 없음"),
		TracePawn(HoleCell + FVector(0.f, 0.f, UpperZ + 100.f), HoleCell + FVector(0.f, 0.f, UpperZ - 100.f), Hit));
	const FVector Chest = RoomCell + FVector(0.f, 0.f, UpperZ + 100.f);
	const bool bWallHit = TracePawn(Chest, Chest + FVector(HouseCells * CellCm, 0.f, 0.f), Hit);
	TestTrue(*FString::Printf(TEXT("2층 뒷벽이 폰을 막음 (맞음 %d · 안쪽 면 %.1f)"), bWallHit, Hit.ImpactPoint.X - Center.X),
		bWallHit && FMath::IsNearlyEqual(Hit.ImpactPoint.X - Center.X, HouseCells * CellCm * 0.5f, 1.f));
	FHitResult Static;
	TestFalse(TEXT("바닥 찾기(움직이지 않는 것)는 2층 판에 안 맞음"), World->LineTraceSingleByObjectType(Static,
		RoomCell + FVector(0.f, 0.f, UpperZ + 100.f), RoomCell + FVector(0.f, 0.f, UpperZ - 100.f), FCollisionObjectQueryParams(ECC_WorldStatic)));
	TestFalse(TEXT("카메라는 2층 판을 지나감"), World->LineTraceSingleByChannel(Static,
		RoomCell + FVector(0.f, 0.f, UpperZ + 100.f), RoomCell + FVector(0.f, 0.f, UpperZ - 100.f), ECC_Camera));

	// 안에 서면 위층도 덮개와 같이 숨고, 나가면 다시.
	House->UpdateHouseRoof(Center + FVector(0.f, 0.f, 90.f));
	TestFalse(TEXT("1층 안에 서면 1층 앞벽·위층 벽·판·덮개 숨김"), House->GetHouseFrontComponent()->IsVisible()
		|| House->GetHouseUpperComponent()->IsVisible() || Floor->IsVisible() || Roof->IsVisible());
	House->UpdateHouseRoof(Center + FVector(-4.f * CellCm, 0.f, 90.f));
	TestTrue(TEXT("나가면 다시 보임"), House->GetHouseUpperComponent()->IsVisible() && Floor->IsVisible() && Roof->IsVisible());

	// 계단을 치우면 구멍을 메우고, 계단이 먼저 선 뒤 2층이 돼도 구멍이 난다.
	Stairs->Destroy();
	TestEqual(TEXT("계단을 치우면 판 9칸"), Floor->GetInstanceCount(), FloorTiles);
	Stairs = SpawnStairs();
	House->SetHouse(true, 1, 1);
	TestEqual(TEXT("1층으로 돌리면 판 0"), Floor->GetInstanceCount(), 0);
	TestTrue(TEXT("1층으로 돌리면 옛 덮개"), Roof->GetStaticMesh() == GroundRoofMesh);
	TestEqual(TEXT("1층으로 돌리면 덮개 높이 0"), static_cast<float>(Roof->GetRelativeLocation().Z), 0.f);
	TestFalse(TEXT("1층 집은 위층 판에 안 막힘"),
		TracePawn(RoomCell + FVector(0.f, 0.f, UpperZ + 100.f), RoomCell + FVector(0.f, 0.f, UpperZ - 100.f), Hit));
	House->SetHouse(true, 1, 2);
	TestEqual(TEXT("계단이 먼저 있어도 2층이 되면 구멍"), Floor->GetInstanceCount(), FloorTiles - StairsLength);
	AddInfo(FString::Printf(TEXT("2층 판 %d칸(계단 칸 %d 비움) · 지붕 높이 %.0f · 판 윗면 %.1f · 2층 뒷벽 안쪽 면 %.1f"),
		Floor->GetInstanceCount(), StairsLength, Roof->GetRelativeLocation().Z, UpperZ, HouseCells * CellCm * 0.5f));
	DestroyTestWorld(World);
	return true;
}

// M108-06d 걸어 오르기 — 계단 비탈 판이 걸을 수 있는 비탈보다 낮고, 진짜 캐릭터 이동(조이스틱과 같은 `AddMovementInput`)으로
// 1층 땅에서 계단을 올라 옆 2층 판에 선 뒤 다시 내려온다. 역검증: 계단 없는 줄을 같은 입력으로 걸으면 안 오른다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseWalkStairsTest, "Snarehaven.Base.Floor2.WalkStairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseWalkStairsTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("기본 큐브"), Cube))
	{
		return false;
	}
	FName TentItem;
	FName StairsItem;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent && TentItem.IsNone())
		{
			TentItem = Row->ItemID;
		}
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Stairs && StairsItem.IsNone())
		{
			StairsItem = Row->ItemID;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestFalse(TEXT("계단 행"), StairsItem.IsNone()))
	{
		return false;
	}

	// 검사 전용 값 (Verification.md 6) — 칸 100 · 걷기 속도 400(엔진 기본 600 은 한 걸음이 커 판 모서리를 건너뛸 수 있다) · 1/60초 걸음.
	constexpr float CellCm = 100.f;
	constexpr int32 HouseCells = 3;
	constexpr int32 StairsLength = 3;
	constexpr float WalkSpeedCm = 400.f;
	constexpr float TickSeconds = 1.f / 60.f;
	constexpr float ClimbSeconds = 3.f, StepOutSeconds = 1.f;
	// 2층에 섰다 = 발 높이가 판 윗면(275)에서 이만큼 안. 내려왔다 = 땅에서 이만큼 안.
	constexpr float FloorToleranceCm = 5.f;
	const float UpperZ = SnareBuildRules::UpperFloorZCm;

	UWorld* const World = CreateTestWorld();
	// 땅 — 윗면 z 0 인 넓은 판(움직이지 않는 것).
	AStaticMeshActor* const Ground = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator);
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->SetActorScale3D(FVector(30.f, 30.f, 1.f));

	const FVector Center = SnareBuildRules::FootprintCenterHalf(FIntPoint::ZeroValue, FIntPoint(HouseCells, HouseCells), CellCm, 0.f);
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarePlacedStructureActor* const House = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform(Center), Params);
	House->InitStructure(TentItem, TEXT("walk_stairs_test"), FString(), FString(), 0, CellCm);
	House->FinishSpawning(FTransform(Center));
	House->SetHouse(true, 1, 2);
	// 계단 = 오른 줄(Y 칸 2) 전체, 돌림 0 — 낮은 끝 -X(문 쪽), 높은 끝 +X(뒷벽).
	const FVector StairsAt = SnareBuildRules::FootprintCenterHalf(SnareBuildRules::JoinHalfUnits(FIntPoint(0, HouseCells - 1), FIntPoint::ZeroValue),
		SnareBuildRules::RotatedFootprint(StairsLength, 1, 0), CellCm, 0.f);
	ASnarePlacedStructureActor* const Stairs = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform(StairsAt), Params);
	Stairs->InitStructure(StairsItem, TEXT("walk_stairs_test"), TEXT("st_stairs"), FString(), 0, CellCm);
	Stairs->FinishSpawning(FTransform(StairsAt));
	if (!TestTrue(TEXT("2층 집"), House->IsTwoStoryHouse())
		|| !TestEqual(TEXT("계단 구멍"), House->GetUpperFloorComponent()->GetInstanceCount(), HouseCells * HouseCells - StairsLength))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 비탈 — 폰 채널로 계단 가운데를 위에서 쏴 윗면 높이·기울기를 잰다. 걸을 수 있는 비탈보다 1° 넘게 낮아야 한다.
	const ASnarehavenCharacter* const BodyDefaults = GetDefault<ASnarehavenCharacter>();
	const float WalkableDegrees = BodyDefaults->GetCharacterMovement()->GetWalkableFloorAngle();
	const float DesignDegrees = SnareBuildRules::StairsSlopeDegrees(StairsLength * CellCm);
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(WalkStairsTest), false, House);
	const bool bRampHit = World->LineTraceSingleByChannel(Hit, StairsAt + FVector(0.f, 0.f, UpperZ + 100.f),
		StairsAt - FVector(0.f, 0.f, 10.f), ECC_Pawn, Query);
	const float MeasuredDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(Hit.ImpactNormal.Z), -1.f, 1.f)));
	TestTrue(*FString::Printf(TEXT("계단 가운데 비탈 윗면 = 2층 높이 반 (맞음 %d · 높이 %.1f)"), bRampHit, Hit.ImpactPoint.Z - StairsAt.Z),
		bRampHit && Hit.GetActor() == Stairs && FMath::IsNearlyEqual(Hit.ImpactPoint.Z - StairsAt.Z, UpperZ * 0.5f, 1.f));
	TestTrue(*FString::Printf(TEXT("잰 비탈 %.2f° = 설계 %.2f°"), MeasuredDegrees, DesignDegrees), FMath::IsNearlyEqual(MeasuredDegrees, DesignDegrees, 0.1f));
	TestTrue(*FString::Printf(TEXT("비탈 %.2f° < 걸을 수 있는 비탈 %.2f° − 1°"), MeasuredDegrees, WalkableDegrees), MeasuredDegrees < WalkableDegrees - 1.f);
	FHitResult Other;
	TestFalse(TEXT("바닥 찾기(움직이지 않는 것)는 비탈에 안 맞음"), World->LineTraceSingleByObjectType(Other,
		StairsAt + FVector(0.f, 0.f, UpperZ + 100.f), StairsAt + FVector(0.f, 0.f, 1.f), FCollisionObjectQueryParams(ECC_WorldStatic), Query));
	TestFalse(TEXT("카메라는 비탈을 지나감"), World->LineTraceSingleByChannel(Other,
		StairsAt + FVector(0.f, 0.f, UpperZ + 100.f), StairsAt + FVector(0.f, 0.f, 1.f), ECC_Camera, Query));

	// ② 걷기 — 진짜 캐릭터 이동. 손으로 만든 월드라 이동 컴포넌트 틱을 직접 부르고 조종 없이도 물리를 돌린다(동물 몸 시험과 같은 길).
	const auto SpawnWalker = [&](const FVector& Feet) -> ASnarehavenCharacter*
	{
		FActorSpawnParameters BodyParams;
		BodyParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnarehavenCharacter* const Body = World->SpawnActor<ASnarehavenCharacter>(ASnarehavenCharacter::StaticClass(),
			FTransform(Feet + FVector(0.f, 0.f, BodyDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f)), BodyParams);
		if (Body != nullptr)
		{
			UCharacterMovementComponent* const Movement = Body->GetCharacterMovement();
			Movement->bRunPhysicsWithNoController = true;
			Movement->MaxWalkSpeed = WalkSpeedCm;
			Movement->SetMovementMode(MOVE_Falling);
			for (float T = 0.f; T < 0.5f; T += TickSeconds)
			{
				Movement->TickComponent(TickSeconds, LEVELTICK_All, nullptr);   // 땅에 내려앉기
			}
		}
		return Body;
	};
	const auto WalkFor = [&](ASnarehavenCharacter* Body, const FVector& Direction, float Seconds, float& OutMaxFeet)
	{
		for (float T = 0.f; T < Seconds; T += TickSeconds)
		{
			Body->AddMovementInput(Direction, 1.f);
			Body->GetCharacterMovement()->TickComponent(TickSeconds, LEVELTICK_All, nullptr);
			OutMaxFeet = FMath::Max(OutMaxFeet, static_cast<float>(Body->GetActorLocation().Z
				- Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - StairsAt.Z));
		}
	};
	const auto FeetOf = [&](const ASnarehavenCharacter* Body)
	{
		return static_cast<float>(Body->GetActorLocation().Z - Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - StairsAt.Z);
	};
	const FVector Up = Stairs->GetActorForwardVector();
	const FVector Out = (Center - StairsAt).GetSafeNormal2D();
	const FVector LowEnd = StairsAt - Up * (StairsLength * CellCm * 0.5f + CellCm * 0.5f);   // 첫 디딤 반 칸 앞 땅

	ASnarehavenCharacter* const Climber = SpawnWalker(LowEnd);
	if (!TestNotNull(TEXT("걷는 몸"), Climber))
	{
		DestroyTestWorld(World);
		return false;
	}
	float MaxFeet = 0.f;
	TestTrue(*FString::Printf(TEXT("시작은 땅 (발 높이 %.1f)"), FeetOf(Climber)), FMath::Abs(FeetOf(Climber)) < FloorToleranceCm);
	WalkFor(Climber, Up, ClimbSeconds, MaxFeet);
	const float TopFeet = FeetOf(Climber);
	const float TopAlong = static_cast<float>(FVector::DotProduct(Climber->GetActorLocation() - StairsAt, Up));
	WalkFor(Climber, Out, StepOutSeconds, MaxFeet);
	const float Floor2Feet = FeetOf(Climber);
	const bool bFloor2Ground = Climber->GetCharacterMovement()->IsMovingOnGround();
	TestTrue(*FString::Printf(TEXT("계단 꼭대기 발 높이 %.1f · 계단 위 %.0f — 2층 판까지 디딤 높이 안"), TopFeet, TopAlong),
		TopFeet > UpperZ - Climber->GetCharacterMovement()->MaxStepHeight && TopFeet <= UpperZ + FloorToleranceCm);
	TestTrue(*FString::Printf(TEXT("옆 2층 판에 섬 (발 높이 %.1f ≥ 270 · 땅 위 %d)"), Floor2Feet, bFloor2Ground),
		bFloor2Ground && Floor2Feet >= UpperZ - FloorToleranceCm && FMath::Abs(Floor2Feet - UpperZ) < FloorToleranceCm);

	// ③ 내려오기 — 계단 줄로 돌아가 낮은 끝 쪽으로. 2층에 갇히지 않는다.
	WalkFor(Climber, -Out, StepOutSeconds, MaxFeet);
	WalkFor(Climber, -Up, ClimbSeconds, MaxFeet);
	const float DownFeet = FeetOf(Climber);
	TestTrue(*FString::Printf(TEXT("계단으로 땅까지 내려옴 (발 높이 %.1f · 땅 위 %d)"), DownFeet, Climber->GetCharacterMovement()->IsMovingOnGround()),
		Climber->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(DownFeet) < FloorToleranceCm);

	// ④ 역검증 — 계단 없는 왼 줄(Y 칸 0)을 같은 입력으로 걸으면 오르지 못한다(비탈 판이 몸을 올린 것이다).
	ASnarehavenCharacter* const Control = SpawnWalker(LowEnd + Out * 2.f * CellCm);
	float ControlMax = 0.f;
	if (TestNotNull(TEXT("역검증 몸"), Control))
	{
		WalkFor(Control, Up, ClimbSeconds, ControlMax);
		TestTrue(*FString::Printf(TEXT("역검증: 계단 없는 줄은 안 오름 (최고 발 높이 %.1f)"), ControlMax), ControlMax < FloorToleranceCm);
	}
	AddInfo(FString::Printf(TEXT("비탈 %.2f°(걸을 수 있는 %.2f°) · 꼭대기 발 %.1f · 2층 판 발 %.1f · 최고 %.1f · 내려온 발 %.1f · 역검증 최고 %.1f"),
		MeasuredDegrees, WalkableDegrees, TopFeet, Floor2Feet, MaxFeet, DownFeet, ControlMax));
	DestroyTestWorld(World);
	return true;
}


// M108-06f-3 계단이 치워질 때 — 그 집 2층에 선 몸(판 위·계단 꼭대기)은 문 앞 디딤 자리로 옮겨 땅에 서고, 1층·집 밖 몸은
// 그대로다(갇힘 없음 — 호스트 판정, 손으로 만든 월드는 클라가 아니다). 역검증: 계단 아닌 가구를 치우면 아무도 안 옮긴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseStairsRemovedTest, "Snarehaven.Base.Floor2.StairsRemovedToDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseStairsRemovedTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("기본 큐브"), Cube))
	{
		return false;
	}
	FName TentItem;
	FName StairsItem;
	FName RugItem;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		FName* const Slot = Row == nullptr ? nullptr
			: Row->PlaceableFunction == EPlaceableFunction::Tent ? &TentItem
			: Row->PlaceableFunction == EPlaceableFunction::Stairs ? &StairsItem
			: Row->PlaceableFunction == EPlaceableFunction::Rug ? &RugItem : nullptr;
		if (Slot != nullptr && Slot->IsNone())
		{
			*Slot = Row->ItemID;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestFalse(TEXT("계단 행"), StairsItem.IsNone())
		|| !TestFalse(TEXT("러그 행"), RugItem.IsNone()))
	{
		return false;
	}

	// 검사 전용 값 (Verification.md 6) — 칸 100 · 3×3 집 · 1/60초 걸음 · 앞벽 바깥면 165(`make_house_meshes.py` WALL_X).
	constexpr float CellCm = 100.f;
	constexpr int32 HouseCells = 3;
	constexpr int32 StairsLength = 3;
	constexpr float TickSeconds = 1.f / 60.f;
	constexpr float FrontWallOuterCm = 165.f;
	constexpr float FloorToleranceCm = 5.f;
	const float UpperZ = SnareBuildRules::UpperFloorZCm;

	const ASnarehavenCharacter* const BodyDefaults = GetDefault<ASnarehavenCharacter>();
	const float RadiusCm = BodyDefaults->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float DoorX = SnareBuildRules::HouseDoorStepX(HouseCells, CellCm);
	TestEqual(TEXT("문 앞 디딤 = 앞 변 한 칸 밖"), DoorX, -(HouseCells * 0.5f + 1.f) * CellCm);
	TestTrue(*FString::Printf(TEXT("디딤 자리 몸(반지름 %.0f)이 앞벽 바깥면 -%.0f 에 안 닿음"), RadiusCm, FrontWallOuterCm),
		DoorX + RadiusCm < -FrontWallOuterCm);

	UWorld* const World = CreateTestWorld();
	AStaticMeshActor* const Ground = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator);
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->SetActorScale3D(FVector(30.f, 30.f, 1.f));

	const FVector Center = SnareBuildRules::FootprintCenterHalf(FIntPoint::ZeroValue, FIntPoint(HouseCells, HouseCells), CellCm, 0.f);
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto SpawnPiece = [&](FName Item, const FString& Id, const FVector& At)
	{
		ASnarePlacedStructureActor* const Piece = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform(At), Params);
		Piece->InitStructure(Item, TEXT("stairs_removed_test"), Id, FString(), 0, CellCm);
		Piece->FinishSpawning(FTransform(At));
		return Piece;
	};
	ASnarePlacedStructureActor* const House = SpawnPiece(TentItem, FString(), Center);
	House->SetHouse(true, 1, 2);
	// 계단 = 오른 줄(Y 칸 2) 전체, 돌림 0 — 낮은 끝 -X(문 쪽). 러그 = 1층 앞 왼 칸(역검증으로 치울 것).
	const FVector StairsAt = SnareBuildRules::FootprintCenterHalf(SnareBuildRules::JoinHalfUnits(FIntPoint(0, HouseCells - 1), FIntPoint::ZeroValue),
		SnareBuildRules::RotatedFootprint(StairsLength, 1, 0), CellCm, 0.f);
	ASnarePlacedStructureActor* const Stairs = SpawnPiece(StairsItem, TEXT("st_stairs"), StairsAt);
	ASnarePlacedStructureActor* const Rug = SpawnPiece(RugItem, TEXT("st_rug"),
		SnareBuildRules::FootprintCenterHalf(FIntPoint::ZeroValue, FIntPoint(1, 1), CellCm, 0.f));
	if (!TestTrue(TEXT("2층 집"), House->IsTwoStoryHouse())
		|| !TestEqual(TEXT("계단 구멍"), House->GetUpperFloorComponent()->GetInstanceCount(), HouseCells * HouseCells - StairsLength))
	{
		DestroyTestWorld(World);
		return false;
	}

	const auto CellAt = [&](int32 X, int32 Y, float Z)
	{
		return SnareBuildRules::FootprintCenterHalf(SnareBuildRules::JoinHalfUnits(FIntPoint(X, Y), FIntPoint::ZeroValue), FIntPoint(1, 1), CellCm, Z);
	};
	const auto Settle = [&](ASnarehavenCharacter* Body)
	{
		UCharacterMovementComponent* const Movement = Body->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Falling);
		for (float T = 0.f; T < 0.75f; T += TickSeconds)
		{
			Movement->TickComponent(TickSeconds, LEVELTICK_All, nullptr);   // 땅·판에 내려앉기
		}
	};
	const auto SpawnBody = [&](const FVector& Feet) -> ASnarehavenCharacter*
	{
		FActorSpawnParameters BodyParams;
		BodyParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnarehavenCharacter* const Body = World->SpawnActor<ASnarehavenCharacter>(ASnarehavenCharacter::StaticClass(),
			FTransform(Feet + FVector(0.f, 0.f, BodyDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f)), BodyParams);
		if (Body != nullptr)
		{
			Body->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Settle(Body);
		}
		return Body;
	};
	const auto FeetOf = [&](const ASnarehavenCharacter* Body)
	{
		return static_cast<float>(ASnarePlacedStructureActor::HouseViewFeet(*Body).Z - Center.Z);
	};
	// 2층 판 가운데 줄 · 계단 꼭대기 칸(비탈 위, 발 ≥ 200) · 1층 가운데 칸 · 집 옆 땅.
	ASnarehavenCharacter* const Upper = SpawnBody(CellAt(1, 0, UpperZ));
	ASnarehavenCharacter* const OnStairsTop = SpawnBody(CellAt(HouseCells - 1, HouseCells - 1, UpperZ));
	ASnarehavenCharacter* const Downstairs = SpawnBody(CellAt(1, 1, 0.f));
	ASnarehavenCharacter* const Outside = SpawnBody(Center + FVector(0.f, -4.f * CellCm, 0.f));
	if (!TestNotNull(TEXT("2층 몸"), Upper) || !TestNotNull(TEXT("계단 꼭대기 몸"), OnStairsTop)
		|| !TestNotNull(TEXT("1층 몸"), Downstairs) || !TestNotNull(TEXT("집 밖 몸"), Outside))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float UpperFeet = FeetOf(Upper);
	const float TopFeet = FeetOf(OnStairsTop);
	TestTrue(*FString::Printf(TEXT("시작: 2층 판 위 (발 %.1f)"), UpperFeet), FMath::Abs(UpperFeet - UpperZ) < FloorToleranceCm);
	TestTrue(*FString::Printf(TEXT("시작: 계단 꼭대기는 2층 (발 %.1f ≥ 200)"), TopFeet), SnareBuildRules::IsOnUpperFloor(TopFeet));
	TestTrue(*FString::Printf(TEXT("시작: 1층 몸은 땅 (발 %.1f)"), FeetOf(Downstairs)), FMath::Abs(FeetOf(Downstairs)) < FloorToleranceCm);
	const FVector UpperBefore = Upper->GetActorLocation();
	const FVector DownBefore = Downstairs->GetActorLocation();
	const FVector OutsideBefore = Outside->GetActorLocation();

	// ① 역검증 — 러그(계단 아님)를 치우면 아무도 안 옮긴다.
	Rug->Destroy();
	TestTrue(*FString::Printf(TEXT("역검증: 러그 치움 — 2층 몸 그대로 (움직임 %.1f)"), FVector::Dist(Upper->GetActorLocation(), UpperBefore)),
		FVector::Dist(Upper->GetActorLocation(), UpperBefore) < 1.f);

	// ② 계단 치움 — 진짜 치우기 길(`Destroy` → `Destroyed`)로.
	Stairs->Destroy();
	TestEqual(TEXT("계단 자리 2층 판 구멍 메움"), House->GetUpperFloorComponent()->GetInstanceCount(), HouseCells * HouseCells);
	const FVector Door = House->GetActorTransform().TransformPosition(FVector(DoorX, 0.f, 0.f));
	FString Report;
	for (ASnarehavenCharacter* const Body : { Upper, OnStairsTop })
	{
		Settle(Body);
		const FVector InHouse = House->GetActorTransform().InverseTransformPosition(Body->GetActorLocation());
		const float FromDoor = static_cast<float>(FVector::Dist2D(Body->GetActorLocation(), Door));
		const float Feet = FeetOf(Body);
		const bool bGround = Body->GetCharacterMovement()->IsMovingOnGround();
		const TCHAR* const Name = Body == Upper ? TEXT("2층 판 몸") : TEXT("계단 꼭대기 몸");
		TestTrue(*FString::Printf(TEXT("%s 문 앞으로 (집 공간 X %.0f < 앞벽 -%.0f · 디딤에서 %.0f)"), Name, InHouse.X, FrontWallOuterCm, FromDoor),
			InHouse.X + RadiusCm <= -FrontWallOuterCm + 1.f && FromDoor < 3.f * RadiusCm);
		TestTrue(*FString::Printf(TEXT("%s 땅에 섬 (발 %.1f · 땅 위 %d)"), Name, Feet, bGround), bGround && FMath::Abs(Feet) < FloorToleranceCm);
		Report += FString::Printf(TEXT("%s X %.0f · 디딤 %.0f · 발 %.1f / "), Name, InHouse.X, FromDoor, Feet);
	}
	TestTrue(TEXT("문 밖을 봄"), FMath::Abs(FRotator::NormalizeAxis(Upper->GetActorRotation().Yaw - 180.f)) < 1.f);
	TestTrue(*FString::Printf(TEXT("1층 몸 그대로 (움직임 %.1f)"), FVector::Dist(Downstairs->GetActorLocation(), DownBefore)),
		FVector::Dist(Downstairs->GetActorLocation(), DownBefore) < 1.f);
	TestTrue(*FString::Printf(TEXT("집 밖 몸 그대로 (움직임 %.1f)"), FVector::Dist(Outside->GetActorLocation(), OutsideBefore)),
		FVector::Dist(Outside->GetActorLocation(), OutsideBefore) < 1.f);
	TestEqual(TEXT("계단 없는 2층 집에 다시 불러도 2층에 남은 몸 0"), House->MoveUpperFloorPawnsToDoor(), 0);
	AddInfo(FString::Printf(TEXT("시작 2층 발 %.1f · 꼭대기 발 %.1f / %s디딤 X %.0f"), UpperFeet, TopFeet, *Report, DoorX));
	DestroyTestWorld(World);
	return true;
}

// M108-06e 층별 집 안 보기 — 2층 집 안에서 발 높이로 층을 가른다. 1층(발 < 집 바닥 + 200)이면 위층 덮개·1층 앞벽·위층 벽·
// 2층 판이 다 숨고, 2층(≥ 200)이면 위층 덮개만 숨는다. 2층 쪽은 진짜 캐릭터를 2층 판에 세워 덮개 숨김이 쓰는 발 위치 그대로 잰다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHouseRoofFloorsTest, "Snarehaven.Base.Floor2.HouseRoofFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHouseRoofFloorsTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("기본 큐브"), Cube))
	{
		return false;
	}
	FName TentItem;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent && TentItem.IsNone())
		{
			TentItem = Row->ItemID;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()))
	{
		return false;
	}

	// 검사 전용 값 (Verification.md 6) — 칸 100 · 1/60초 걸음 · 판에 섰다 = 발 높이가 판 윗면에서 5 안.
	constexpr float CellCm = 100.f;
	constexpr int32 HouseCells = 3;
	constexpr float TickSeconds = 1.f / 60.f;
	constexpr float FloorToleranceCm = 5.f;
	const float UpperZ = SnareBuildRules::UpperFloorZCm;
	const float Split = SnareBuildRules::UpperFloorSplitCm;
	TestTrue(*FString::Printf(TEXT("문턱 %.0f 이 계단 중간 %.1f 와 2층 판 %.0f 사이"), Split, UpperZ * 0.5f, UpperZ),
		Split > UpperZ * 0.5f && Split < UpperZ);

	UWorld* const World = CreateTestWorld();
	AStaticMeshActor* const Ground = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator);
	Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Ground->SetActorScale3D(FVector(30.f, 30.f, 1.f));
	const FVector Center = SnareBuildRules::FootprintCenterHalf(FIntPoint::ZeroValue, FIntPoint(HouseCells, HouseCells), CellCm, 0.f);
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarePlacedStructureActor* const House = World->SpawnActor<ASnarePlacedStructureActor>(
		ASnarePlacedStructureActor::StaticClass(), FTransform(Center), Params);
	House->InitStructure(TentItem, TEXT("roof_floors_test"), FString(), FString(), 0, CellCm);
	House->FinishSpawning(FTransform(Center));
	House->SetHouse(true, 1, 2);
	if (!TestTrue(TEXT("2층 집"), House->IsTwoStoryHouse()))
	{
		DestroyTestWorld(World);
		return false;
	}
	const UStaticMeshComponent* const Roof = House->GetHouseRoofComponent();
	const UStaticMeshComponent* const Front = House->GetHouseFrontComponent();
	const UStaticMeshComponent* const Upper = House->GetHouseUpperComponent();
	const UInstancedStaticMeshComponent* const Floor = House->GetUpperFloorComponent();
	const auto Seen = [&]()
	{
		return FString::Printf(TEXT("덮개 %d · 1층 앞벽 %d · 위층 벽 %d · 2층 판 %d"),
			Roof->IsVisible(), Front->IsVisible(), Upper->IsVisible(), Floor->IsVisible());
	};
	const auto ExpectUpstairs = [&](const TCHAR* What)
	{
		TestTrue(*FString::Printf(TEXT("%s — 2층 보기: 덮개만 숨음 (%s)"), What, *Seen()),
			!Roof->IsVisible() && Front->IsVisible() && Upper->IsVisible() && Floor->IsVisible());
	};
	const auto ExpectGround = [&](const TCHAR* What)
	{
		TestTrue(*FString::Printf(TEXT("%s — 1층 보기: 다 숨음 (%s)"), What, *Seen()),
			!Roof->IsVisible() && !Front->IsVisible() && !Upper->IsVisible() && !Floor->IsVisible());
	};
	const FVector RoomCell = Center + FVector(0.f, -CellCm, 0.f);

	// ① 발 높이 경계 — 바닥·계단 중간·문턱 바로 밑은 1층, 문턱·2층 판은 2층, 밖은 높이와 상관없이 다 보임.
	House->UpdateHouseRoof(RoomCell);
	ExpectGround(TEXT("1층 바닥(발 0)"));
	House->UpdateHouseRoof(RoomCell + FVector(0.f, 0.f, UpperZ * 0.5f));
	ExpectGround(TEXT("계단 중간 높이"));
	House->UpdateHouseRoof(RoomCell + FVector(0.f, 0.f, Split - 1.f));
	ExpectGround(TEXT("문턱 − 1"));
	House->UpdateHouseRoof(RoomCell + FVector(0.f, 0.f, Split));
	ExpectUpstairs(TEXT("문턱"));
	House->UpdateHouseRoof(RoomCell + FVector(0.f, 0.f, UpperZ));
	ExpectUpstairs(TEXT("2층 판 높이"));
	House->UpdateHouseRoof(Center + FVector(-4.f * CellCm, 0.f, UpperZ));
	TestTrue(*FString::Printf(TEXT("집 밖(2층 높이)은 다 보임 (%s)"), *Seen()),
		Roof->IsVisible() && Front->IsVisible() && Upper->IsVisible() && Floor->IsVisible());

	// ② 진짜 몸 — 2층 판·1층 바닥에 세운 캐릭터의 덮개 숨김 발 위치. 액터 위치(캡슐 가운데)를 쓰면 1층에서도 반 키만큼 높아
	// 계단 중간에서 2층 보기로 넘어간다 — 발 위치는 캡슐 밑이어야 한다.
	const auto StandAt = [&](const FVector& Feet) -> ASnarehavenCharacter*
	{
		FActorSpawnParameters BodyParams;
		BodyParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const float HalfHeight = GetDefault<ASnarehavenCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		ASnarehavenCharacter* const Body = World->SpawnActor<ASnarehavenCharacter>(ASnarehavenCharacter::StaticClass(),
			FTransform(Feet + FVector(0.f, 0.f, HalfHeight + 5.f)), BodyParams);
		if (Body != nullptr)
		{
			UCharacterMovementComponent* const Movement = Body->GetCharacterMovement();
			Movement->bRunPhysicsWithNoController = true;
			Movement->SetMovementMode(MOVE_Falling);
			for (float T = 0.f; T < 0.5f; T += TickSeconds)
			{
				Movement->TickComponent(TickSeconds, LEVELTICK_All, nullptr);   // 판·땅에 내려앉기
			}
		}
		return Body;
	};
	ASnarehavenCharacter* const Upstairs = StandAt(RoomCell + FVector(0.f, 0.f, UpperZ));
	ASnarehavenCharacter* const Downstairs = StandAt(RoomCell);
	if (!TestNotNull(TEXT("2층 몸"), Upstairs) || !TestNotNull(TEXT("1층 몸"), Downstairs))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector UpFeet = ASnarePlacedStructureActor::HouseViewFeet(*Upstairs);
	const FVector DownFeet = ASnarePlacedStructureActor::HouseViewFeet(*Downstairs);
	TestTrue(*FString::Printf(TEXT("2층 몸이 판에 섬 (땅 위 %d · 발 높이 %.1f)"), Upstairs->GetCharacterMovement()->IsMovingOnGround(), UpFeet.Z - Center.Z),
		Upstairs->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(UpFeet.Z - Center.Z - UpperZ) < FloorToleranceCm);
	TestTrue(*FString::Printf(TEXT("1층 몸이 바닥에 섬 (발 높이 %.1f)"), DownFeet.Z - Center.Z), FMath::Abs(DownFeet.Z - Center.Z) < FloorToleranceCm);
	House->UpdateHouseRoof(UpFeet);
	ExpectUpstairs(TEXT("2층 판에 선 몸"));
	House->UpdateHouseRoof(DownFeet);
	ExpectGround(TEXT("1층에 선 몸"));
	// 역검증 — 캡슐 가운데로 재면 계단 중간(발 137)에 선 몸이 2층 보기가 된다(가운데 = 발 + 반 키 > 200).
	const float HalfHeight = Downstairs->GetSimpleCollisionHalfHeight();
	TestTrue(*FString::Printf(TEXT("역검증: 계단 중간 몸의 캡슐 가운데 %.1f 는 문턱 위 — 발로 재야 1층"), UpperZ * 0.5f + HalfHeight),
		SnareBuildRules::IsOnUpperFloor(UpperZ * 0.5f + HalfHeight) && !SnareBuildRules::IsOnUpperFloor(UpperZ * 0.5f));
	AddInfo(FString::Printf(TEXT("문턱 %.0f · 2층 몸 발 %.1f · 1층 몸 발 %.1f · 반 키 %.1f"),
		Split, UpFeet.Z - Center.Z, DownFeet.Z - Center.Z, HalfHeight));
	DestroyTestWorld(World);
	return true;
}

// M108-06f 놓인 가구 층 칸 — 2층 가구(서버 `floor: 2`)는 집 바닥 + 2층 높이에 서고, 층이 다르면 같은 칸이어도 안 겹치고,
// 2층 받침 위 소품은 받침 층을 따른다. 2층 집 1층에 서면 2층 가구(몸·불빛)만 숨고 1층 가구·2층·밖에선 보인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseUpperFurnitureTest, "Snarehaven.Base.Floor2.UpperFurniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseUpperFurnitureTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6) — 집 앵커 칸 (40,40), 손님 주소·높이 허용 1cm.
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString HouseId(TEXT("tent_floor2_furniture_test"));
	const FIntPoint HouseCell(40, 40);
	constexpr float ToleranceCm = 1.f;

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	FName TentItem;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			TentItem = Row->ItemID;
			break;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();
	const auto Find = [World, &HouseId](const FString& StructureId) -> ASnarePlacedStructureActor*
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (!It->IsPreview() && It->GetTentId() == HouseId && It->GetStructureId() == StructureId)
			{
				return *It;
			}
		}
		return nullptr;
	};
	const auto Share = [&](FName ItemID, const FString& StructureId, const FIntPoint& Offset, int32 Floor,
		const FString& TopHostId = FString(), int32 TopSlot = -1)
	{
		return Build->HostAcceptShared(ItemID, HouseId, StructureId, HouseCell + Offset, 0, GuestOwner, NAME_None,
			FIntPoint::ZeroValue, -1, 0, false, 0, TopHostId, TopSlot, 1, Floor);
	};

	// 2층 집 (게스트가 올린 공유 그대로).
	TestTrue(TEXT("2층 집을 받는다"), Build->HostAcceptShared(TentItem, HouseId, FString(), HouseCell, 0, GuestOwner, NAME_None,
		FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, 1, FString(), -1, /*HouseFloors=*/2));
	ASnarePlacedStructureActor* const House = Find(FString());
	if (!TestNotNull(TEXT("집 액터"), House) || !TestTrue(TEXT("2층 집"), House->IsTwoStoryHouse()))
	{
		DestroyTestWorld(World);
		return false;
	}
	const float HouseZ = House->GetActorLocation().Z;
	const float UpperZ = SnareBuildRules::UpperFloorZCm;

	// ① 2층 바닥 가구 — 높이 = 집 바닥 + 2층, 층 칸 2, 복제(게스트들도 층으로 가른다).
	TestTrue(TEXT("2층 안락의자를 받는다"), Share(TEXT("item.armchair"), TEXT("up_chair"), FIntPoint(2, 1), 2));
	const ASnarePlacedStructureActor* const UpChair = Find(TEXT("up_chair"));
	if (!TestNotNull(TEXT("2층 안락의자"), UpChair))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("2층 의자 층 칸"), UpChair->GetPlacedFloor(), 2);
	TestTrue(*FString::Printf(TEXT("2층 의자 높이 = 집 바닥 + %.0f (잰 값 %.1f)"), UpperZ, UpChair->GetActorLocation().Z - HouseZ),
		FMath::Abs(UpChair->GetActorLocation().Z - HouseZ - UpperZ) < ToleranceCm);
	TestTrue(TEXT("2층 의자 복제"), UpChair->GetIsReplicated());
	// 같은 층 같은 칸은 겹침 — 역검증(층을 안 보면 1층 것과 섞인다).
	TestFalse(TEXT("같은 2층 칸 둘째는 거절"), Share(TEXT("item.armchair"), TEXT("up_chair_2"), FIntPoint(2, 1), 2));
	TestNull(TEXT("거절된 둘째는 안 섬"), Find(TEXT("up_chair_2")));

	// ② 2층 받침 위 소품 — 받침 층을 따라 2, 2층 판보다 위.
	TestTrue(TEXT("2층 둥근 탁자를 받는다"), Share(TEXT("item.table_round"), TEXT("up_table"), FIntPoint(0, 0), 2));
	TestTrue(TEXT("2층 탁자 위 머그잔을 받는다"), Share(TEXT("item.mug"), TEXT("up_mug"), FIntPoint(0, 0), 1, TEXT("up_table"), 0));
	const ASnarePlacedStructureActor* const UpMug = Find(TEXT("up_mug"));
	if (TestNotNull(TEXT("2층 머그잔"), UpMug))
	{
		TestEqual(TEXT("소품 층 = 받침 층"), UpMug->GetPlacedFloor(), 2);
		TestTrue(*FString::Printf(TEXT("소품이 2층 판 위 (잰 값 %.1f)"), UpMug->GetActorLocation().Z - HouseZ),
			UpMug->GetActorLocation().Z - HouseZ > UpperZ);
	}

	// ③ 2층 전등 — 켠 불빛도 층 보기로 숨는다. 1층 가구는 집 밖 텐트 줄 바닥에(집 몸 칸 밖).
	TestTrue(TEXT("2층 전등을 받는다"), Share(TEXT("item.lamp_electric"), TEXT("up_lamp"), FIntPoint(1, 0), 2));
	ASnarePlacedStructureActor* const UpLamp = Find(TEXT("up_lamp"));
	TestTrue(TEXT("1층 의자(집 밖 마당)를 받는다"), Share(TEXT("item.armchair"), TEXT("yard_chair"), FIntPoint(-2, 1), 1));
	const ASnarePlacedStructureActor* const YardChair = Find(TEXT("yard_chair"));
	if (!TestNotNull(TEXT("2층 전등"), UpLamp) || !TestNotNull(TEXT("마당 의자"), YardChair))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("마당 의자 층 칸 1"), YardChair->GetPlacedFloor(), 1);
	TestTrue(*FString::Printf(TEXT("마당 의자는 땅에 (잰 값 %.1f)"), YardChair->GetActorLocation().Z - HouseZ),
		FMath::Abs(YardChair->GetActorLocation().Z - HouseZ) < ToleranceCm);
	UpLamp->SetLampLit(true, 60.f, 352.f);

	const UStaticMeshComponent* const ChairBody = UpChair->GetBodyComponent();
	const auto Seen = [&]()
	{
		return FString::Printf(TEXT("2층 의자 숨김 %d·몸 %d · 머그 숨김 %d · 전등 불빛 %d · 마당 의자 숨김 %d"),
			UpChair->IsFloorViewHidden(), ChairBody != nullptr && ChairBody->IsVisible(), UpMug != nullptr && UpMug->IsFloorViewHidden(),
			UpLamp->IsLampLightVisible(), YardChair->IsFloorViewHidden());
	};
	const auto ExpectShown = [&](const TCHAR* What)
	{
		TestTrue(*FString::Printf(TEXT("%s — 2층 가구 보임 (%s)"), What, *Seen()),
			!UpChair->IsFloorViewHidden() && ChairBody != nullptr && ChairBody->IsVisible() && UpMug != nullptr && !UpMug->IsFloorViewHidden()
			&& UpLamp->IsLampLightVisible() && !YardChair->IsFloorViewHidden());
	};
	const FVector RoomCell = House->GetActorLocation() + FVector(0.f, -Base->GetRules()->CellSizeCm, 0.f);
	House->UpdateHouseRoof(RoomCell);
	TestTrue(*FString::Printf(TEXT("1층 안 — 2층 가구·불빛만 숨음 (%s)"), *Seen()),
		UpChair->IsFloorViewHidden() && ChairBody != nullptr && !ChairBody->IsVisible() && UpMug != nullptr && UpMug->IsFloorViewHidden()
		&& !UpLamp->IsLampLightVisible() && !YardChair->IsFloorViewHidden());
	House->UpdateHouseRoof(RoomCell + FVector(0.f, 0.f, UpperZ));
	ExpectShown(TEXT("2층 안"));
	House->UpdateHouseRoof(RoomCell);
	House->UpdateHouseRoof(House->GetActorLocation() + FVector(-4.f * Base->GetRules()->CellSizeCm, 0.f, 0.f));
	ExpectShown(TEXT("1층 안 → 집 밖"));
	AddInfo(FString::Printf(TEXT("집 바닥 %.1f · 2층 의자 +%.1f · 머그 +%.1f · 마당 의자 +%.1f"), HouseZ,
		UpChair->GetActorLocation().Z - HouseZ, UpMug != nullptr ? UpMug->GetActorLocation().Z - HouseZ : -1.f,
		YardChair->GetActorLocation().Z - HouseZ));
	DestroyTestWorld(World);
	return true;
}

// M108-06f-2 2층 놓기 미리보기 (명세 `docs/specs/house-stairs.md` 2절 「2층 놓기」) — 2층 집 2층(발 ≥ 문턱)에 서면 놓을 층이 2,
// 1층·계단 중간·집 밖·1층 집이면 1. 2층 판정은 서버 `floor2PlaceFail` 과 같은 줄: 계단 구멍·집 밖·삐져나옴은 파랑, 텐트 몸은 안 셈,
// 같은 2층 가구와만 빨강(러그는 러그끼리). 1층 판정은 2층 가구를 안 센다(층이 다르면 안 겹침).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBasePlaceUpstairsTest, "Snarehaven.Base.Floor2.PlacePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBasePlaceUpstairsTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6) — 집 앵커 (0,0), 계단 = 앞 줄 X 로 (0,0)~(2,0), 2층 의자 (2,2).
	const FName Chair(TEXT("item.chair_wood"));
	const FName Bed(TEXT("item.bed_single"));
	const FName RugRound(TEXT("item.rug_round"));
	const FName Stairs(TEXT("item.stairs_wood"));
	const int32 Green = static_cast<int32>(ESnarePlacementVerdict::Green);
	const int32 Blue = static_cast<int32>(ESnarePlacementVerdict::Blue);
	const int32 Red = static_cast<int32>(ESnarePlacementVerdict::Red);

	const auto MakeHouse = [](int32 Floors, bool bWithStairs)
	{
		FSnareTentMirror House = MakeTent(TestTentId, FIntPoint(0, 0));
		House.bUpgraded = true;
		House.Floors = Floors;
		if (bWithStairs)
		{
			FSnareStructureMirror& Step = House.Structures.AddDefaulted_GetRef();
			Step.StructureId = TEXT("st_stairs");
			Step.ItemID = TEXT("item.stairs_wood");
			Step.Cell = FIntPoint(0, 0);
		}
		FSnareStructureMirror& UpChair = House.Structures.AddDefaulted_GetRef();
		UpChair.StructureId = TEXT("st_up_chair");
		UpChair.ItemID = TEXT("item.chair_wood");
		UpChair.Cell = FIntPoint(2, 2);
		UpChair.Floor = 2;
		// 역검증 사본 — 집 밖 마당의 2층 칸(서버엔 없는 자리)과 1층 의자: 1층 판정이 층을 가르는지 본다.
		FSnareStructureMirror& Ghost = House.Structures.AddDefaulted_GetRef();
		Ghost.StructureId = TEXT("st_ghost_up");
		Ghost.ItemID = TEXT("item.chair_wood");
		Ghost.Cell = FIntPoint(5, 5);
		Ghost.Floor = 2;
		FSnareStructureMirror& YardChair = House.Structures.AddDefaulted_GetRef();
		YardChair.StructureId = TEXT("st_yard_chair");
		YardChair.ItemID = TEXT("item.chair_wood");
		YardChair.Cell = FIntPoint(6, 6);
		return House;
	};
	USnareBaseSubsystem* const TwoStory = MakeBase(*this, nullptr);
	TwoStory->AddTentMirrorForTest(MakeHouse(2, true));
	const auto Verdict = [](USnareBaseSubsystem* Base, FName Item, FIntPoint Cell, int32 Floor, FIntPoint Half = FIntPoint::ZeroValue)
	{
		return static_cast<int32>(Base->EvaluatePlacement(Item, Cell, 0, true, SnareBaseZones::Ranch, Half, Floor));
	};

	// ① 2층 — 텐트 몸은 1층이라 집 안 빈 칸은 초록.
	TestEqual(TEXT("2층 빈 칸 의자 초록"), Verdict(TwoStory, Chair, FIntPoint(1, 1), 2), Green);
	TestEqual(TEXT("2층 반 칸 의자 초록"), Verdict(TwoStory, Chair, FIntPoint(0, 1), 2, FIntPoint(1, 1)), Green);
	TestEqual(TEXT("2층 계단 구멍 칸 파랑"), Verdict(TwoStory, Chair, FIntPoint(1, 0), 2), Blue);
	TestEqual(TEXT("2층 반 칸이 구멍에 걸치면 파랑"), Verdict(TwoStory, Chair, FIntPoint(1, 0), 2, FIntPoint(0, 1)), Blue);
	TestEqual(TEXT("2층 집 밖 파랑"), Verdict(TwoStory, Chair, FIntPoint(3, 1), 2), Blue);
	TestEqual(TEXT("2층 침대가 집 밖으로 삐져나오면 파랑"), Verdict(TwoStory, Bed, FIntPoint(2, 1), 2), Blue);
	TestEqual(TEXT("2층 침대가 집 안이면 초록"), Verdict(TwoStory, Bed, FIntPoint(0, 1), 2), Green);
	TestEqual(TEXT("2층 의자 위 의자 빨강"), Verdict(TwoStory, Chair, FIntPoint(2, 2), 2), Red);
	TestEqual(TEXT("2층 의자 밑 러그 초록"), Verdict(TwoStory, RugRound, FIntPoint(1, 1), 2), Green);
	TestEqual(TEXT("계단은 2층으로 안 묻는다 — 파랑"), Verdict(TwoStory, Stairs, FIntPoint(1, 1), 2), Blue);

	// ② 1층 — 집 안 빈 칸은 초록(집 몸은 안 셈, M108-06f-4)·계단 칸은 빨강, 2층 가구는 안 센다(역검증: 같은 자리 1층 의자는 빨강).
	TestEqual(TEXT("1층 집 안 빈 칸 초록"), Verdict(TwoStory, Chair, FIntPoint(1, 1), 1), Green);
	TestEqual(TEXT("1층 계단 칸 빨강"), Verdict(TwoStory, Chair, FIntPoint(1, 0), 1), Red);
	TestEqual(TEXT("1층은 2층 칸을 안 센다 — 초록"), Verdict(TwoStory, Chair, FIntPoint(5, 5), 1), Green);
	TestEqual(TEXT("1층 의자 위 의자 빨강"), Verdict(TwoStory, Chair, FIntPoint(6, 6), 1), Red);

	// ③ 2층이 없는 집 — 계단 없는 2층 집·1층 집이면 2층 칸 전부 파랑.
	USnareBaseSubsystem* const NoStairs = MakeBase(*this, nullptr);
	NoStairs->AddTentMirrorForTest(MakeHouse(2, false));
	TestEqual(TEXT("계단 없는 2층 집 파랑"), Verdict(NoStairs, Chair, FIntPoint(1, 1), 2), Blue);
	USnareBaseSubsystem* const OneStory = MakeBase(*this, nullptr);
	OneStory->AddTentMirrorForTest(MakeHouse(1, true));
	TestEqual(TEXT("1층 집 2층 파랑"), Verdict(OneStory, Chair, FIntPoint(1, 1), 2), Blue);

	// ④ 놓을 층 — 진짜 기지 창구·놓기 부품으로 발 높이·자리마다.
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	const float CellCm = Base->GetRules()->CellSizeCm;
	FSnareTentMirror House = MakeHouse(2, true);
	House.Cell = FIntPoint(40, 40);
	Base->AddTentMirrorForTest(House);
	FSnareTentMirror Flat = MakeHouse(1, true);
	Flat.TentId = TEXT("tent_flat_test");
	Flat.Cell = FIntPoint(200, 200);
	Base->AddTentMirrorForTest(Flat);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const Pawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("폰"), Pawn))
	{
		DestroyTestWorld(World);
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(Pawn, TEXT("PlaceUpstairsBuild"));
	Build->RegisterComponent();
	// 집 바닥 = 사본만 있을 때 집 칸 바닥(`HouseGroundZ` 와 같은 길).
	const FVector Room = ASnarePlacedStructureActor::CellToWorld(World, House.Cell + FIntPoint(1, 1), FIntPoint(1, 1), CellCm);
	const float GroundZ = ASnarePlacedStructureActor::CellToWorld(World, House.Cell, FIntPoint(1, 1), CellCm).Z;
	const float UpperZ = SnareBuildRules::UpperFloorZCm;
	const float Split = SnareBuildRules::UpperFloorSplitCm;
	const auto FloorAt = [&](const FVector& Feet, FName Zone = SnareBaseZones::Ranch)
	{
		return Build->PlacementFloorAt(Feet, Zone);
	};
	const FVector RoomFloor(Room.X, Room.Y, GroundZ);
	const FSnareTentMirror* Found = nullptr;
	TestEqual(TEXT("2층 판에 서면 2층"), Build->PlacementFloorAt(RoomFloor + FVector(0.f, 0.f, UpperZ), SnareBaseZones::Ranch, &Found), 2);
	TestTrue(TEXT("2층 집 사본을 돌려준다"), Found != nullptr && Found->TentId == House.TentId);
	TestEqual(TEXT("문턱이면 2층"), FloorAt(RoomFloor + FVector(0.f, 0.f, Split)), 2);
	TestEqual(TEXT("문턱 − 1 이면 1층"), FloorAt(RoomFloor + FVector(0.f, 0.f, Split - 1.f)), 1);
	TestEqual(TEXT("1층 바닥은 1층"), FloorAt(RoomFloor), 1);
	TestEqual(TEXT("집 밖 2층 높이는 1층"), FloorAt(RoomFloor + FVector(-2.f * CellCm, 0.f, UpperZ)), 1);
	TestEqual(TEXT("다른 존이면 1층"), FloorAt(RoomFloor + FVector(0.f, 0.f, UpperZ), SnareBaseZones::Hunt), 1);
	const FVector FlatRoom = ASnarePlacedStructureActor::CellToWorld(World, Flat.Cell + FIntPoint(1, 1), FIntPoint(1, 1), CellCm);
	TestEqual(TEXT("1층 집 위 높이는 1층"), FloorAt(FVector(FlatRoom.X, FlatRoom.Y, GroundZ + UpperZ)), 1);
	AddInfo(FString::Printf(TEXT("집 바닥 %.1f · 문턱 %.0f · 2층 판 %.0f · 칸 %.0f"), GroundZ, Split, UpperZ, CellCm));
	DestroyTestWorld(World);
	return true;
}

// M108-06f-4 집 1층 안 가구 — 집으로 올린 텐트의 몸(3×3)은 꾸미는 방이라 겹침으로 안 센다(서버 `PlaceStructure` 와 같은 줄).
// 놓기 미리보기(`EvaluatePlacement`)·호스트 공유(`HostAcceptShared`) 둘 다. 텐트(집 아님) 몸은 그대로 막는다(역검증).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseHouseGroundRoomTest, "Snarehaven.Base.Floor2.HouseGroundRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseHouseGroundRoomTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6) — 집 앵커 (60,60)·텐트 앵커 (80,80), 방 가운데 칸 = 앵커 + (1,1).
	const FName Chair(TEXT("item.chair_wood"));
	const FName RugRound(TEXT("item.rug_round"));
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString HouseId(TEXT("tent_ground_room_house"));
	const FString TentId(TEXT("tent_ground_room_tent"));
	const FIntPoint HouseCell(60, 60);
	const FIntPoint TentCell(80, 80);
	const FIntPoint Room(1, 1);
	const int32 Green = static_cast<int32>(ESnarePlacementVerdict::Green);
	const int32 Red = static_cast<int32>(ESnarePlacementVerdict::Red);

	// ① 미리보기 — 집 안 빈 칸 초록(의자·러그), 텐트 몸은 빨강.
	FSnareTentMirror HouseMirror = MakeTent(HouseId, HouseCell);
	HouseMirror.bUpgraded = true;
	FSnareTentMirror TentMirror = MakeTent(TentId, TentCell);
	USnareBaseSubsystem* const Mirrors = MakeBase(*this, nullptr);
	Mirrors->AddTentMirrorForTest(HouseMirror);
	Mirrors->AddTentMirrorForTest(TentMirror);
	const auto Verdict = [Mirrors](FName Item, const FIntPoint& Cell)
	{
		return static_cast<int32>(Mirrors->EvaluatePlacement(Item, Cell, 0, true));
	};
	TestEqual(TEXT("집 1층 방 가운데 의자 초록"), Verdict(Chair, HouseCell + Room), Green);
	TestEqual(TEXT("집 1층 방 앵커 칸 의자 초록"), Verdict(Chair, HouseCell), Green);
	TestEqual(TEXT("집 1층 방 러그 초록"), Verdict(RugRound, HouseCell), Green);
	TestEqual(TEXT("역검증: 텐트 몸 가운데 의자 빨강"), Verdict(Chair, TentCell + Room), Red);
	TestEqual(TEXT("역검증: 텐트 몸 러그 빨강"), Verdict(RugRound, TentCell), Red);

	// ② 호스트 공유 — 진짜 월드에 집·텐트 액터를 세우고 손님 의자를 받는다.
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		DestroyTestWorld(World);
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	FName TentItem;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			TentItem = Row->ItemID;
			break;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		DestroyTestWorld(World);
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("GroundRoomBuild"));
	Build->RegisterComponent();
	const auto ShareBody = [&](const FString& Id, const FIntPoint& Cell, bool bHouse)
	{
		return Build->HostAcceptShared(TentItem, Id, FString(), Cell, 0, GuestOwner, NAME_None,
			FIntPoint::ZeroValue, -1, 0, bHouse, 1);
	};
	const auto ShareFloor = [&](FName Item, const FString& Id, const FString& StructureId, const FIntPoint& Cell)
	{
		return Build->HostAcceptShared(Item, Id, StructureId, Cell, 0, GuestOwner, NAME_None);
	};
	const auto Spawned = [World](const FString& StructureId)
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (!It->IsPreview() && It->GetStructureId() == StructureId)
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("손님 집을 받는다"), ShareBody(HouseId, HouseCell, true));
	TestTrue(TEXT("손님 텐트를 받는다"), ShareBody(TentId, TentCell, false));
	TestTrue(TEXT("집 1층 방 의자를 받는다"), ShareFloor(Chair, HouseId, TEXT("room_chair"), HouseCell + Room));
	TestTrue(TEXT("집 1층 방 의자가 선다"), Spawned(TEXT("room_chair")));
	TestTrue(TEXT("의자 밑 러그를 받는다"), ShareFloor(RugRound, HouseId, TEXT("room_rug"), HouseCell));
	TestFalse(TEXT("같은 칸 둘째 의자는 거절"), ShareFloor(Chair, HouseId, TEXT("room_chair_2"), HouseCell + Room));
	TestFalse(TEXT("역검증: 텐트 몸 가운데 의자는 거절"), ShareFloor(Chair, TentId, TEXT("tent_chair"), TentCell + Room));
	TestFalse(TEXT("역검증: 거절된 텐트 의자는 안 섬"), Spawned(TEXT("tent_chair")));
	DestroyTestWorld(World);
	return true;
}


// 연료 발전기 표 (M108-07b) — 규칙 칸 = DESIGN 8.11 「연료 발전기 숫자」 줄 = 서버 `GENERATOR_*`, 행 기능 `Generator`·1×1·레시피 하나.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseGeneratorRulesTest, "Snarehaven.Base.Power.GeneratorRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseGeneratorRulesTest::RunTest(const FString& Parameters)
{
	FString Doc;
	FString Server;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md"))))
		|| !TestTrue(TEXT("서버 원본을 읽는다"), FFileHelper::LoadFileToString(Server,
			*(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("GeneratorRules")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("발전기 줄이 채워졌다"), Rules->HasGeneratorRules()))
	{
		return false;
	}

	// 설계서 한 줄 — 「연료 발전기 숫자」 칸만 잘라 읽는다(윗줄 「연료 발전기」의 옛 100W·10분과 섞이지 않게).
	FRegexMatcher LineMatcher(FRegexPattern(TEXT(R"((\| 🟡 연료 발전기 숫자 [^\n]*))")), Doc);
	if (!TestTrue(TEXT("설계서 「연료 발전기 숫자」 줄"), LineMatcher.FindNext()))
	{
		return false;
	}
	const FString Line = LineMatcher.GetCaptureGroup(1);
	const FName Wood(TEXT("item.wood"));
	const FName Coal(TEXT("item.coal_chunk"));
	TestEqual(TEXT("출력 = 설계서"), Rules->GeneratorPower, CaptureInt(Line, TEXT(R"(출력 \*\*(\d+)\*\*)")));
	TestEqual(TEXT("나무 분 = 설계서"), Rules->GeneratorFuelMinutes.FindRef(Wood), CaptureInt(Line, TEXT(R"(나무 1 = \*\*(\d+)분\*\*)")));
	TestEqual(TEXT("석탄 분 = 설계서"), Rules->GeneratorFuelMinutes.FindRef(Coal), CaptureInt(Line, TEXT(R"(석탄 덩어리 1 = \*\*(\d+)분\*\*)")));
	TestEqual(TEXT("최대 시간 = 설계서"), Rules->GeneratorFuelMaxHours, CaptureInt(Line, TEXT(R"(최대 \*\*(\d+)시간\*\*)")));
	TestEqual(TEXT("연료 종류 둘"), Rules->GeneratorFuelMinutes.Num(), 2);

	// 서버 상수 — 클라 판 글(「나무 1개 (+10분)」)이 서버가 실제로 더하는 분과 같아야 한다.
	FRegexMatcher ServerItem(FRegexPattern(TEXT(R"re(var GENERATOR_ITEM = "([\w.]+)")re")), Server);
	TestEqual(TEXT("서버 발전기 ID"), ServerItem.FindNext() ? FName(*ServerItem.GetCaptureGroup(1)) : FName(), Rules->GeneratorItemID);
	TestEqual(TEXT("서버 나무 분"), Rules->GeneratorFuelMinutes.FindRef(Wood),
		CaptureInt(Server, TEXT(R"re(var GENERATOR_FUEL_MINUTES = \{ "item.wood": (\d+))re")));
	TestEqual(TEXT("서버 석탄 분"), Rules->GeneratorFuelMinutes.FindRef(Coal),
		CaptureInt(Server, TEXT(R"re(var GENERATOR_FUEL_MINUTES = \{[^}]*"item.coal_chunk": (\d+))re")));
	TestEqual(TEXT("서버 최대 시간"), Rules->GeneratorFuelMaxHours, CaptureInt(Server, TEXT(R"(var GENERATOR_FUEL_MAX_HOURS = (\d+);)")));

	// 아이템 행·레시피.
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const UDataTable* const Recipes = LoadObject<UDataTable>(nullptr, RecipeTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), Items) || !TestNotNull(TEXT("DT_RecipeTable"), Recipes))
	{
		return false;
	}
	const FItemRow* GeneratorRow = nullptr;
	Items->ForeachRow<FItemRow>(TEXT("GeneratorRules"), [&](const FName&, const FItemRow& Row)
	{
		if (Row.ItemID == Rules->GeneratorItemID)
		{
			GeneratorRow = &Row;
		}
	});
	if (!TestNotNull(TEXT("발전기 행"), GeneratorRow))
	{
		return false;
	}
	TestEqual(TEXT("행 기능 Generator"), GeneratorRow->PlaceableFunction, EPlaceableFunction::Generator);
	TestEqual(TEXT("발자국 X 1"), GeneratorRow->FootprintX, 1);
	TestEqual(TEXT("발자국 Y 1"), GeneratorRow->FootprintY, 1);
	TestFalse(TEXT("메시 있음"), GeneratorRow->PlacedMeshRef.IsNull());
	TestFalse(TEXT("아이콘 있음"), GeneratorRow->IconRef.IsNull());
	int32 RecipeCount = 0;
	Recipes->ForeachRow<FRecipeRow>(TEXT("GeneratorRules"), [&](const FName&, const FRecipeRow& Row)
	{
		RecipeCount += Row.ResultItemID == Rules->GeneratorItemID ? 1 : 0;
	});
	TestEqual(TEXT("발전기 레시피 하나"), RecipeCount, 1);
	AddInfo(FString::Printf(TEXT("발전기 %s · 출력 %d · 나무 %d분 · 석탄 %d분 · 최대 %d시간 · 레시피 %d"), *Rules->GeneratorItemID.ToString(),
		Rules->GeneratorPower, Rules->GeneratorFuelMinutes.FindRef(Wood), Rules->GeneratorFuelMinutes.FindRef(Coal),
		Rules->GeneratorFuelMaxHours, RecipeCount));
	return true;
}


// M108-07c 연료 발전기 전력 — 연료가 남은 발전기만 땅 공급에 `GeneratorPower` 를 더하고, 끝 시각은 서버 `fuelUntilUtc` 를
// **서버 시계**(`nowUtc`)로 잰 남은 초를 게임 서버 시계로 옮긴 값이다. 끝 시각이 지나면 그 땅 전등이 꺼진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseGeneratorPowerTest, "Snarehaven.Base.Power.Generator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseGeneratorPowerTest::RunTest(const FString& Parameters)
{
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("GeneratorPower")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("전력 줄"), Rules->HasPowerRules())
		|| !TestTrue(TEXT("발전기 줄"), Rules->HasGeneratorRules()))
	{
		return false;
	}

	// 순수 셈 — 기대값은 표 숫자로(숫자가 바뀌어도 안 깨지게).
	const int32 GeneratorLamps = Rules->GeneratorPower / Rules->LampPowerUse;
	TestEqual(TEXT("키트 없이 도는 발전기 하나 = 발전기 출력"), SnareBuildRules::PowerSupply(*Rules, 0, 0, 1), Rules->GeneratorPower);
	TestEqual(TEXT("키트 + 배터리 + 발전기 = 셋의 합"), SnareBuildRules::PowerSupply(*Rules, 1, 1, 1),
		Rules->SolarKitPower + Rules->BatteryPower + Rules->GeneratorPower);
	TestEqual(TEXT("키트 없는 배터리는 발전기가 있어도 0"), SnareBuildRules::PowerSupply(*Rules, 0, 2, 1), Rules->GeneratorPower);
	TestEqual(TEXT("멈춘 발전기(0개)는 공급 0"), SnareBuildRules::PowerSupply(*Rules, 0, 0, 0), 0);
	TestTrue(TEXT("발전기 하나로 켜지는 만큼"), SnareBuildRules::IsLandPowered(*Rules, 0, 0, GeneratorLamps, 1));
	TestFalse(TEXT("발전기 하나에 하나 넘치면 전부 꺼짐"), SnareBuildRules::IsLandPowered(*Rules, 0, 0, GeneratorLamps + 1, 1));
	TestTrue(TEXT("발전기 둘이면 두 배"), SnareBuildRules::IsLandPowered(*Rules, 0, 0, GeneratorLamps * 2, 2));

	// 사본 — `fuelUntilUtc` 를 그대로 두고, 남은 초는 기기 시계가 아니라 서버 `nowUtc` 로 잰다.
	// 서버 시각을 2020년으로 둔다: 기기 시계로 쟀다면 이미 지나 0 이 나온다.
	USnareBaseSubsystem* const Base = MakeBase(*this, nullptr);
	Base->ApplyBackendNow(TEXT("2020-01-01T00:00:00.000Z"));
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueObject>(ParseJson(TEXT("{\"tentId\":\"gen_land\",\"x\":0,\"y\":0,\"upgraded\":true,"
		"\"placedAtUtc\":\"2019-12-31T00:00:00.000Z\",\"items\":[{\"structureId\":\"gen_a\",\"itemId\":\"item.generator\",\"x\":1,\"y\":1,"
		"\"fuelUntilUtc\":\"2020-01-01T01:20:00.000Z\"},{\"structureId\":\"gen_b\",\"itemId\":\"item.generator\",\"x\":3,\"y\":1}]}"))));
	Base->ApplyBackendTents(Values);
	const FSnareTentMirror* const Land = Base->FindTent(TEXT("gen_land"));
	if (!TestNotNull(TEXT("사본 텐트"), Land) || !TestEqual(TEXT("사본 가구 둘"), Land->Structures.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("사본 fuelUntilUtc 는 서버 글자 그대로"), Land->Structures[0].FuelUntilUtc, FString(TEXT("2020-01-01T01:20:00.000Z")));
	TestTrue(TEXT("연료 안 넣은 발전기는 빈 글자"), Land->Structures[1].FuelUntilUtc.IsEmpty());
	const double Left = Base->FuelSecondsLeft(Land->Structures[0].FuelUntilUtc);
	TestTrue(FString::Printf(TEXT("남은 초 = 서버 시계로 1시간 20분 (%.1f)"), Left), Left > 80.0 * 60.0 - 5.0 && Left <= 80.0 * 60.0);
	TestEqual(TEXT("빈 글자는 0초"), Base->FuelSecondsLeft(FString()), 0.0);
	TestEqual(TEXT("지난 시각은 0초"), Base->FuelSecondsLeft(TEXT("2019-12-31T23:00:00.000Z")), 0.0);

	// 월드 — 발전기 + 전등 넷 땅. 연료 없음 → 꺼짐, 연료 넣음 → 켜짐, 게임 서버 시계가 끝 시각을 지나면 → 꺼짐.
	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("gen_land"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	constexpr int32 DemoLamps = 4;
	const int32 ExpectedLit = Rules->HasFlameRules() ? FMath::Min(DemoLamps, Rules->LandLightMax) : DemoLamps;
	ASnarePlacedStructureActor* const Generator = Spawn(Rules->GeneratorItemID, TEXT("gen_a"));
	TArray<ASnarePlacedStructureActor*> Lamps;
	for (int32 Index = 0; Index < DemoLamps; ++Index)
	{
		Lamps.Add(Spawn(Rules->LampItemID, FString::Printf(TEXT("lamp_%d"), Index)));
	}
	TestTrue(TEXT("행 기능 = 발전기"), Generator->GetPlaceableFunction() == EPlaceableFunction::Generator);
	TestFalse(TEXT("놓자마자는 연료 없음"), Generator->IsGeneratorRunning());
	TestEqual(TEXT("밤 · 연료 없는 발전기 → 켜진 전등 0"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), 0);

	Generator->SetGeneratorFuelSecondsLeft(Left);
	TestTrue(TEXT("연료 넣음 → 돈다"), Generator->IsGeneratorRunning());
	TestTrue(TEXT("복제 끝 시각 − 서버 시계 = 넣은 초"),
		FMath::IsNearlyEqual(Generator->GetGeneratorFuelSecondsLeft(), Left, 0.01));
	TestEqual(TEXT("밤 · 연료 넣음 → 전등 넷 켜짐"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), ExpectedLit);
	TestTrue(TEXT("전등 불빛 보임"), Lamps[0]->IsLampLightVisible());
	TestFalse(TEXT("발전기 자체는 불빛 없음"), Generator->IsLampLightVisible());
	TestEqual(TEXT("낮 → 0 (전등은 밤에만)"), USnarePowerSubsystem::ApplyPower(World, *Rules, false), 0);

	Lamps[0]->SetGeneratorFuelSecondsLeft(Left);
	TestFalse(TEXT("발전기 아닌 가구에 연료를 줘도 안 돈다"), Lamps[0]->IsGeneratorRunning());

	World->TimeSeconds += Left - 1.0;
	TestTrue(TEXT("끝 1초 전 → 아직 돈다"), Generator->IsGeneratorRunning());
	TestEqual(TEXT("끝 1초 전 밤 → 켜짐"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), ExpectedLit);
	World->TimeSeconds += 2.0;
	TestFalse(TEXT("끝 시각 지남 → 멈춤"), Generator->IsGeneratorRunning());
	TestEqual(TEXT("끝 시각 지남 → 남은 초 0"), Generator->GetGeneratorFuelSecondsLeft(), 0.0);
	TestEqual(TEXT("끝 시각 지남 밤 → 전부 꺼짐"), USnarePowerSubsystem::ApplyPower(World, *Rules, true), 0);
	TestFalse(TEXT("끝 시각 지남 → 불빛 안 보임"), Lamps[0]->IsLampLightVisible());

	Generator->SetGeneratorFuelSecondsLeft(0.0);
	TestFalse(TEXT("0초 = 연료 없음"), Generator->IsGeneratorRunning());
	AddInfo(FString::Printf(TEXT("발전기 공급 %d · 전등 소모 %d · 발전기 하나로 전등 %d · 사본 남은 초 %.0f"),
		Rules->GeneratorPower, Rules->LampPowerUse, GeneratorLamps, Left));

	DestroyTestWorld(World);
	return true;
}


// M108-07d 발전기 공회전 소리 — 연료가 남아 도는 동안만 고리가 나고, 끝 시각이 지나면 1초 판에서 멎는다. 미리보기·연료 없는 발전기·
// 다른 가구는 안 난다. 감쇠는 DESIGN 8.11 「연료 발전기 숫자」 300/1500. 판정은 이름이 아니라 실제 오디오 컴포넌트·소리 자산 칸 (PROMPT 12).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseGeneratorSoundTest, "Snarehaven.Base.Power.GeneratorSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseGeneratorSoundTest::RunTest(const FString& Parameters)
{
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("GeneratorSound")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("발전기 줄"), Rules->HasGeneratorRules()))
	{
		return false;
	}
	const USoundWave* const Wave = LoadObject<USoundWave>(nullptr, TEXT("/Game/Game/Audio/SFX/SFX_Generator_Loop_01.SFX_Generator_Loop_01"));
	if (!TestNotNull(TEXT("소리 자산 SFX_Generator_Loop_01"), Wave))
	{
		return false;
	}
	TestTrue(TEXT("공회전 자산은 되풀이(looping)"), Wave->bLooping);

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("gen_sound"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	// 소리 장치가 없는 실행(`-NoSound`)에선 오디오 컴포넌트가 안 생긴다 — 그때는 경고로 알리고 「재생 중」 쪽만 건너뛴다
	// (전기톱 공회전 `SnarehavenHoldLoopTest` 와 같은 방식). 켜야 하는 상태(`IsGeneratorSoundOn`)는 늘 잰다.
	const bool bHasAudioDevice = World->GetAudioDeviceRaw() != nullptr;
	if (!bHasAudioDevice)
	{
		AddWarning(TEXT("소리 장치가 없다(-NoSound?) — 발전기 공회전 재생 중인지는 이번 실행에서 못 잰다"));
	}

	constexpr double TestFuelSeconds = 90.0;   // 시험 전용 — 표 숫자(나무 10분)가 바뀌어도 안 깨지게
	ASnarePlacedStructureActor* const Generator = Spawn(Rules->GeneratorItemID, TEXT("gen_s"));
	ASnarePlacedStructureActor* const Lamp = Spawn(Rules->LampItemID, TEXT("lamp_s"));
	TestFalse(TEXT("놓자마자(연료 없음)는 소리 끔"), Generator->IsGeneratorSoundOn());
	TestFalse(TEXT("놓자마자는 재생 안 함"), Generator->IsGeneratorSoundPlaying());

	Generator->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestTrue(TEXT("연료 넣음 → 소리 켬"), Generator->IsGeneratorSoundOn());
	const UAudioComponent* const Loop = Generator->GetGeneratorLoop();
	if (bHasAudioDevice && TestNotNull(TEXT("공회전 오디오 컴포넌트"), Loop))
	{
		TestTrue(TEXT("연료 넣음 → 재생 중"), Generator->IsGeneratorSoundPlaying());
		const USoundAttenuation* const Attenuation = Loop->AttenuationSettings;
		if (TestNotNull(TEXT("감쇠 설정"), Attenuation))
		{
			TestEqual(TEXT("감쇠 안쪽 300cm"), static_cast<float>(Attenuation->Attenuation.AttenuationShapeExtents.X), 300.f);
			TestEqual(TEXT("감쇠 바깥 1500cm = 안쪽 + 사라지는 거리"),
				static_cast<float>(Attenuation->Attenuation.AttenuationShapeExtents.X + Attenuation->Attenuation.FalloffDistance), 1500.f);
		}
		TestTrue(TEXT("소리는 발전기 몸에 붙음"), Loop->GetAttachParent() == Generator->GetRootComponent());
	}

	Lamp->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestFalse(TEXT("발전기 아닌 가구에 연료를 줘도 소리 없음"), Lamp->IsGeneratorSoundOn());

	World->TimeSeconds += TestFuelSeconds - 1.0;
	Generator->RefreshGeneratorSound();
	TestTrue(TEXT("끝 1초 전 → 아직 소리"), Generator->IsGeneratorSoundOn());
	World->TimeSeconds += 2.0;
	Generator->RefreshGeneratorSound();   // 도는 동안 1초 타이머가 부르는 길 — 시험 월드는 틱이 없어 직접 부른다
	TestFalse(TEXT("끝 시각 지남 → 소리 끔"), Generator->IsGeneratorSoundOn());
	TestFalse(TEXT("끝 시각 지남 → 재생 안 함"), Generator->IsGeneratorSoundPlaying());
	TestNull(TEXT("끝 시각 지남 → 오디오 컴포넌트 없앰"), Generator->GetGeneratorLoop());

	Generator->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestTrue(TEXT("다시 연료 → 다시 소리"), Generator->IsGeneratorSoundOn());
	Generator->SetGeneratorFuelSecondsLeft(0.0);
	TestFalse(TEXT("0초 = 연료 없음 → 바로 끔"), Generator->IsGeneratorSoundOn());
	TestFalse(TEXT("0초 → 재생 안 함"), Generator->IsGeneratorSoundPlaying());

	ASnarePlacedStructureActor* const Preview = Spawn(Rules->GeneratorItemID, TEXT("gen_preview"));
	Preview->SetPreviewVerdict(ESnarePlacementVerdict::Green);
	Preview->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestFalse(TEXT("미리보기 발전기는 소리 없음"), Preview->IsGeneratorSoundOn());
	AddInfo(FString::Printf(TEXT("발전기 공회전 — 소리 장치 %d · 자산 %.1f초 되풀이 %d"), bHasAudioDevice ? 1 : 0, Wave->Duration,
		Wave->bLooping ? 1 : 0));

	DestroyTestWorld(World);
	return true;
}


// M108-07d-2 발전기 도는 모습 — 연료가 남은 동안만 틱이 켜져 몸이 놓인 자리 둘레로 떨고, 끝 시각이 지나면 틱이 꺼지고 몸은 놓인 자리
// 그대로 돌아온다. 미리보기·다른 가구는 안 떤다. 판정은 실제 몸 컴포넌트 자리와 액터 틱 켜짐 (PROMPT 12). 진폭은 표 숫자라 범위만 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseGeneratorShakeTest, "Snarehaven.Base.Power.GeneratorShake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseGeneratorShakeTest::RunTest(const FString& Parameters)
{
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("GeneratorShake")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("발전기 줄"), Rules->HasGeneratorRules()))
	{
		return false;
	}
	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("gen_shake"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	auto BodyOf = [](const ASnarePlacedStructureActor* Actor) -> const UStaticMeshComponent*
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (const UStaticMeshComponent* const Mesh : Meshes)
		{
			if (Mesh->GetFName() == TEXT("Body"))
			{
				return Mesh;
			}
		}
		return nullptr;
	};

	constexpr double TestFuelSeconds = 90.0;   // 시험 전용 — 표 숫자(나무 10분)가 바뀌어도 안 깨지게
	constexpr float FrameSeconds = 1.f / 30.f;   // 모바일 30fps 한 프레임
	constexpr int32 TestFrames = 12;
	constexpr float MaxOffsetCm = 5.f;   // 칸(100cm) 안에 머무는 눈높이 — 몸이 옆 칸·땅으로 새지 않는지만 본다
	ASnarePlacedStructureActor* const Generator = Spawn(Rules->GeneratorItemID, TEXT("gen_k"));
	const UStaticMeshComponent* const Body = BodyOf(Generator);
	if (!TestNotNull(TEXT("발전기 몸 컴포넌트"), Body))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector Rest = Body->GetRelativeLocation();
	TestTrue(TEXT("놓인 몸은 진짜 메시(큐브 자리표시 아님)"), Body->GetStaticMesh() != nullptr
		&& !Body->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/")));
	TestFalse(TEXT("연료 없음 → 안 떪"), Generator->IsGeneratorShaking());
	TestFalse(TEXT("연료 없음 → 틱 꺼짐"), Generator->IsActorTickEnabled());

	Generator->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestTrue(TEXT("연료 넣음 → 떪"), Generator->IsGeneratorShaking());
	TestTrue(TEXT("연료 넣음 → 틱 켜짐"), Generator->IsActorTickEnabled());
	float MaxOffset = 0.f;
	float MaxOffsetZ = 0.f;
	int32 DistinctSteps = 0;
	FVector Previous = Body->GetRelativeLocation();
	for (int32 Frame = 0; Frame < TestFrames; ++Frame)
	{
		Generator->TickGeneratorShake(FrameSeconds);   // 시험 월드는 틱이 없어 직접 부른다
		const FVector Now = Body->GetRelativeLocation();
		MaxOffset = FMath::Max(MaxOffset, static_cast<float>((Now - Rest).Size()));
		MaxOffsetZ = FMath::Max(MaxOffsetZ, static_cast<float>(FMath::Abs(Now.Z - Rest.Z)));
		DistinctSteps += (Now - Previous).Size() > KINDA_SMALL_NUMBER ? 1 : 0;
		Previous = Now;
	}
	TestTrue(TEXT("도는 동안 몸 자리가 흔들림"), MaxOffset > KINDA_SMALL_NUMBER);
	TestEqual(TEXT("프레임마다 자리가 바뀜"), DistinctSteps, TestFrames);
	TestTrue(TEXT("흔들림은 놓인 자리 둘레(칸 밖으로 안 샘)"), MaxOffset <= MaxOffsetCm);
	TestTrue(TEXT("위아래로는 안 떪(땅에 묻히거나 뜨지 않음)"), MaxOffsetZ <= KINDA_SMALL_NUMBER);
	TestTrue(TEXT("검사 값 = 실제 몸 자리 - 놓인 자리"), Generator->GetBodyOffsetFromRest().Equals(Body->GetRelativeLocation() - Rest, 0.01));

	World->TimeSeconds += TestFuelSeconds + 1.0;
	Generator->RefreshGeneratorSound();   // 도는 동안 1초 타이머가 부르는 길
	TestFalse(TEXT("끝 시각 지남 → 안 떪"), Generator->IsGeneratorShaking());
	TestFalse(TEXT("끝 시각 지남 → 틱 꺼짐"), Generator->IsActorTickEnabled());
	TestTrue(TEXT("멈추면 몸은 놓인 자리 그대로"), Body->GetRelativeLocation().Equals(Rest, 0.001));
	Generator->TickGeneratorShake(FrameSeconds);
	TestTrue(TEXT("멈춘 뒤 걸음을 불러도 제자리"), Body->GetRelativeLocation().Equals(Rest, 0.001));

	Generator->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	Generator->TickGeneratorShake(FrameSeconds);
	Generator->SetGeneratorFuelSecondsLeft(0.0);
	TestFalse(TEXT("0초 = 연료 없음 → 바로 멈춤"), Generator->IsGeneratorShaking());
	TestTrue(TEXT("0초 → 놓인 자리 그대로"), Body->GetRelativeLocation().Equals(Rest, 0.001));

	ASnarePlacedStructureActor* const Lamp = Spawn(Rules->LampItemID, TEXT("lamp_k"));
	Lamp->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestFalse(TEXT("발전기 아닌 가구는 연료를 줘도 안 떪"), Lamp->IsGeneratorShaking() || Lamp->IsActorTickEnabled());
	ASnarePlacedStructureActor* const Preview = Spawn(Rules->GeneratorItemID, TEXT("gen_kp"));
	Preview->SetPreviewVerdict(ESnarePlacementVerdict::Green);
	Preview->SetGeneratorFuelSecondsLeft(TestFuelSeconds);
	TestFalse(TEXT("미리보기 발전기는 안 떪"), Preview->IsGeneratorShaking() || Preview->IsActorTickEnabled());
	AddInfo(FString::Printf(TEXT("발전기 떨림 — %d프레임 최대 %.2fcm · 위아래 %.3fcm · 놓인 자리 (%.1f, %.1f, %.1f)"), TestFrames, MaxOffset,
		MaxOffsetZ, Rest.X, Rest.Y, Rest.Z));

	DestroyTestWorld(World);
	return true;
}


// M108-07c-2 발전기 판 — 발전기를 「쓰기」 하면 발전기 쪽이 열리고, 남은 시간·이 땅 공급/소모·연료 단추(내 발전기만)가 뜨며,
// 단추는 서버 `FeedGenerator` 를 묻고 사본·남은 시간은 **응답 뒤에만** 바뀐다(CLAUDE.md 8). 남의 발전기는 단추 없음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseGeneratorPanelTest, "Snarehaven.Base.Power.GeneratorPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseGeneratorPanelTest::RunTest(const FString& Parameters)
{
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("GeneratorPanel")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("발전기 줄"), Rules->HasGeneratorRules())
		|| !TestTrue(TEXT("연료 둘 이상"), Rules->GeneratorFuelMinutes.Num() >= 2))
	{
		return false;
	}
	// 짧게 타는 연료 = 첫째, 오래 타는 연료 = 끝 (판 순서와 같게). 숫자는 표에서 — 바뀌어도 안 깨지게.
	TArray<TPair<FName, int32>> Fuels = Rules->GeneratorFuelMinutes.Array();
	Fuels.Sort([](const TPair<FName, int32>& A, const TPair<FName, int32>& B) { return A.Value < B.Value; });
	const TPair<FName, int32> Short = Fuels[0];
	const TPair<FName, int32> Long = Fuels.Last();
	const double MaxSeconds = Rules->GeneratorFuelMaxHours * 3600.0;

	// 순수 글·단추 켜짐.
	TestEqual(TEXT("1시간 20분"), UCraftingScreenWidget::FormatGeneratorFuelText(80.0 * 60.0).ToString(), FString(TEXT("돌아감 · 연료 1시간 20분 남음")));
	TestEqual(TEXT("딱 2시간"), UCraftingScreenWidget::FormatGeneratorFuelText(7200.0).ToString(), FString(TEXT("돌아감 · 연료 2시간 남음")));
	TestEqual(TEXT("30초 = 1분(올림)"), UCraftingScreenWidget::FormatGeneratorFuelText(30.0).ToString(), FString(TEXT("돌아감 · 연료 1분 남음")));
	TestEqual(TEXT("0초 = 멈춤"), UCraftingScreenWidget::FormatGeneratorFuelText(0.0).ToString(), FString(TEXT("멈춤 - 연료 없음")));
	TestTrue(TEXT("빈 발전기 → 켜짐"), UCraftingScreenWidget::CanFeedGenerator(true, 0.0, Long.Value, Rules->GeneratorFuelMaxHours));
	TestFalse(TEXT("남의 것 → 꺼짐"), UCraftingScreenWidget::CanFeedGenerator(false, 0.0, Short.Value, Rules->GeneratorFuelMaxHours));
	TestTrue(TEXT("남은 칸 = 짧은 연료 하나 → 짧은 것 켜짐"),
		UCraftingScreenWidget::CanFeedGenerator(true, MaxSeconds - Short.Value * 60.0, Short.Value, Rules->GeneratorFuelMaxHours));
	TestFalse(TEXT("남은 칸 = 짧은 연료 하나 → 긴 것 꺼짐(서버 full)"),
		UCraftingScreenWidget::CanFeedGenerator(true, MaxSeconds - Short.Value * 60.0, Long.Value, Rules->GeneratorFuelMaxHours));
	TestFalse(TEXT("가득 → 전부 꺼짐"), UCraftingScreenWidget::CanFeedGenerator(true, MaxSeconds, Short.Value, Rules->GeneratorFuelMaxHours));

	// 사본 — 내 땅에 연료 없는 발전기 하나. 서버 시각은 2020년(기기 시계와 갈라 본다).
	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	Base->ApplyBackendNow(TEXT("2020-01-01T00:00:00.000Z"));
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueObject>(ParseJson(FString::Printf(TEXT("{\"tentId\":\"gen_land\",\"x\":0,\"y\":0,\"upgraded\":true,"
		"\"placedAtUtc\":\"2019-12-31T00:00:00.000Z\",\"items\":[{\"structureId\":\"gen_a\",\"itemId\":\"%s\",\"x\":1,\"y\":1}]}"),
		*Rules->GeneratorItemID.ToString()))));
	Base->ApplyBackendTents(Values);

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const TCHAR* TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	constexpr int32 DemoLamps = 4;
	ASnarePlacedStructureActor* const Generator = Spawn(Rules->GeneratorItemID, TEXT("gen_land"), TEXT("gen_a"));
	for (int32 Index = 0; Index < DemoLamps; ++Index)
	{
		Spawn(Rules->LampItemID, TEXT("gen_land"), FString::Printf(TEXT("lamp_%d"), Index));
	}
	ASnarePlacedStructureActor* const Foreign = Spawn(Rules->GeneratorItemID, TEXT("other_land"), TEXT("gen_other"));

	int32 Supply = -1;
	int32 Use = -1;
	USnarePowerSubsystem::MeasureLand(World, *Rules, TEXT("PFTESTBASE"), TEXT("gen_land"), Supply, Use);
	TestEqual(TEXT("연료 없음 → 공급 0"), Supply, 0);
	TestEqual(TEXT("소모 = 전등 넷"), Use, DemoLamps * Rules->LampPowerUse);
	Generator->SetGeneratorFuelSecondsLeft(80.0 * 60.0);
	USnarePowerSubsystem::MeasureLand(World, *Rules, TEXT("PFTESTBASE"), TEXT("gen_land"), Supply, Use);
	TestEqual(TEXT("연료 있음 → 공급 = 발전기 출력"), Supply, Rules->GeneratorPower);
	USnarePowerSubsystem::MeasureLand(World, *Rules, TEXT("PFTESTBASE"), TEXT("other_land"), Supply, Use);
	TestEqual(TEXT("남의 땅은 따로 — 그 땅 연료 없는 발전기 공급 0"), Supply, 0);

	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("제작 화면"), Screen))
	{
		DestroyTestWorld(World);
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);
	Screen->TakeWidget();

	// 판 속 글 전부·연료 단추(글 → 켜짐).
	auto Texts = [Screen]()
	{
		TArray<FString> Out;
		Screen->WidgetTree->ForEachWidget([&Out](UWidget* Widget)
		{
			if (const UTextBlock* const Text = Cast<UTextBlock>(Widget))
			{
				Out.Add(Text->GetText().ToString());
			}
		});
		return Out;
	};
	auto FuelButtons = [Screen]()
	{
		TMap<FString, UButton*> Out;
		Screen->WidgetTree->ForEachWidget([&Out](UWidget* Widget)
		{
			UButton* const Button = Cast<UButton>(Widget);
			const UTextBlock* const Label = Button != nullptr ? Cast<UTextBlock>(Button->GetContent()) : nullptr;
			if (Label != nullptr && Label->GetText().ToString().Contains(TEXT("분)")))
			{
				Out.Add(Label->GetText().ToString(), Button);
			}
		});
		return Out;
	};
	const FString ShortName = Base->FindItemRow(Short.Key) != nullptr ? Base->FindItemRow(Short.Key)->DisplayName.ToString() : Short.Key.ToString();
	const FString LongName = Base->FindItemRow(Long.Key) != nullptr ? Base->FindItemRow(Long.Key)->DisplayName.ToString() : Long.Key.ToString();
	const FString ShortOne = FString::Printf(TEXT("%s 1개 (+%d분)"), *ShortName, Short.Value);
	const FString LongTen = FString::Printf(TEXT("%s 10개 (+%d분)"), *LongName, Long.Value * 10);

	Screen->OpenFor(Generator);
	TestTrue(TEXT("열림"), Screen->IsOpen());
	TestEqual(TEXT("발전기 쪽"), Screen->GetPage(), ESnareBaseScreenPage::Generator);
	TArray<FString> Shown = Texts();
	TestTrue(TEXT("쪽 단추 「발전기」"), Shown.Contains(TEXT("발전기")));
	TestFalse(TEXT("제작 쪽 단추 없음"), Shown.Contains(TEXT("제작")));
	TestTrue(TEXT("남은 시간 줄"), Shown.Contains(TEXT("돌아감 · 연료 1시간 20분 남음")));
	TestTrue(TEXT("이 땅 공급/소모 줄"), Shown.Contains(UCraftingScreenWidget::FormatLandPowerText(Rules->GeneratorPower,
		DemoLamps * Rules->LampPowerUse).ToString()));
	TestEqual(TEXT("공급 넉넉 → 모자람 줄 없음"), Shown.Contains(TEXT("전력이 모자라 전부 꺼집니다")),
		Rules->GeneratorPower < DemoLamps * Rules->LampPowerUse);
	TestTrue(TEXT("최대 줄"), Shown.Contains(FString::Printf(TEXT("연료는 최대 %d시간"), Rules->GeneratorFuelMaxHours)));
	TestTrue(TEXT("치우면 사라짐 줄"), Shown.Contains(TEXT("치우면 남은 연료는 사라집니다")));
	TMap<FString, UButton*> Buttons = FuelButtons();
	TestEqual(TEXT("연료 단추 = 연료 수 × 2"), Buttons.Num(), Fuels.Num() * 2);
	TestTrue(*FString::Printf(TEXT("「%s」 켜짐"), *ShortOne), Buttons.Contains(ShortOne) && Buttons[ShortOne]->GetIsEnabled());
	TestTrue(*FString::Printf(TEXT("「%s」 켜짐"), *LongTen), Buttons.Contains(LongTen) && Buttons[LongTen]->GetIsEnabled());

	// 거의 가득 — 짧은 연료 하나 칸만 남으면 긴 연료 단추는 꺼진다.
	Generator->SetGeneratorFuelSecondsLeft(MaxSeconds - Short.Value * 60.0);
	Screen->RefreshDisplay();
	Buttons = FuelButtons();
	TestTrue(TEXT("거의 가득 · 짧은 연료 켜짐"), Buttons.Contains(ShortOne) && Buttons[ShortOne]->GetIsEnabled());
	TestTrue(TEXT("거의 가득 · 긴 연료 꺼짐"), Buttons.Contains(LongTen) && !Buttons[LongTen]->GetIsEnabled());

	// 다 탐 — 멈춤, 공급 0 < 소모라 모자람 줄.
	Generator->SetGeneratorFuelSecondsLeft(0.0);
	Screen->RefreshDisplay();
	Shown = Texts();
	TestTrue(TEXT("멈춤 줄"), Shown.Contains(TEXT("멈춤 - 연료 없음")));
	TestTrue(TEXT("공급 0 줄"), Shown.Contains(UCraftingScreenWidget::FormatLandPowerText(0, DemoLamps * Rules->LampPowerUse).ToString()));
	TestTrue(TEXT("모자람 줄"), Shown.Contains(TEXT("전력이 모자라 전부 꺼집니다")));

	// 단추 → 서버에 묻는다. 응답 전엔 사본 그대로, 응답 뒤 서버 `fuelUntilUtc` 글자 그대로 + 남은 초는 서버 `nowUtc` 로.
	Buttons = FuelButtons();
	if (TestTrue(TEXT("짧은 연료 1개 단추"), Buttons.Contains(ShortOne)))
	{
		Buttons[ShortOne]->OnClicked.Broadcast();
	}
	if (TestTrue(TEXT("FeedGenerator 를 물었다"), Calls->Num() > 0 && Calls->Last().FunctionName == TEXT("FeedGenerator")))
	{
		TestEqual(TEXT("tentId 인자"), ArgString(Calls->Last(), TEXT("tentId")), FString(TEXT("gen_land")));
		TestEqual(TEXT("structureId 인자"), ArgString(Calls->Last(), TEXT("structureId")), FString(TEXT("gen_a")));
		TestEqual(TEXT("fuel 인자"), ArgString(Calls->Last(), TEXT("fuel")), Short.Key.ToString());
		TestEqual(TEXT("count 인자"), ArgInt(Calls->Last(), TEXT("count")), 1);
		TestTrue(TEXT("응답 전 사본 연료 빈 글자"), Base->FindStructure(TEXT("gen_a"))->FuelUntilUtc.IsEmpty());
		const FString Until = FString::Printf(TEXT("2030-06-01T00:%02d:00.000Z"), Short.Value);
		Respond(*Calls, FString::Printf(TEXT(R"({"used":1,"fuelUntilUtc":"%s","nowUtc":"2030-06-01T00:00:00.000Z"})"), *Until));
		TestEqual(TEXT("응답 뒤 사본 = 서버 글자"), Base->FindStructure(TEXT("gen_a"))->FuelUntilUtc, Until);
		const double Left = Base->FuelSecondsLeft(Until);
		TestTrue(FString::Printf(TEXT("남은 초 = 서버 시계로 연료 하나 (%.1f)"), Left), Left > Short.Value * 60.0 - 5.0 && Left <= Short.Value * 60.0);
	}
	Buttons = FuelButtons();
	if (Buttons.Contains(LongTen))
	{
		Buttons[LongTen]->OnClicked.Broadcast();
		Respond(*Calls, TEXT(R"({"error":"x","code":"full"})"));
		TestEqual(TEXT("가득 거절 글"), Base->GetLastMessage().ToString(), FString(TEXT("연료가 가득입니다")));
	}

	// 남의 발전기 — 글은 보이고 넣기 단추는 없다.
	Screen->OpenFor(Foreign);
	TestEqual(TEXT("남의 발전기도 발전기 쪽"), Screen->GetPage(), ESnareBaseScreenPage::Generator);
	TestTrue(TEXT("남의 것 안내"), Texts().Contains(TEXT("남의 발전기에는 연료를 넣을 수 없습니다")));
	TestEqual(TEXT("남의 것 연료 단추 0"), FuelButtons().Num(), 0);
	AddInfo(FString::Printf(TEXT("연료 단추 %d · 짧은 %s %d분 · 긴 %s %d분 · 최대 %d시간 · 전등 %d 소모 %d"), Fuels.Num() * 2,
		*Short.Key.ToString(), Short.Value, *Long.Key.ToString(), Long.Value, Rules->GeneratorFuelMaxHours, DemoLamps,
		DemoLamps * Rules->LampPowerUse));

	Screen->Close();
	DestroyTestWorld(World);
	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// M108-08b-2 전기 가구 행 칸 — 소모 `PowerUse` 는 설계서 8.11 「🟡 전기 가구」 줄 값이고, 켜짐 짝 `PlacedOnMeshRef` 는 꺼진 메시와
// 같은 크기·섹션 1·같은 팔레트 재질(바꿔 끼워도 자리·드로콜 그대로)이다. 놓인 액터는 켜지면 짝으로, 꺼지거나 미리보기면 꺼진 메시로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseElectricRowsTest, "Snarehaven.Base.Power.ElectricRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseElectricRowsTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FRegexMatcher LineMatcher(FRegexPattern(TEXT(R"((\| 🟡 전기 가구 \(M108-08[^\n]*))")), Doc);
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Rules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("ElectricRows")) : nullptr;
	if (!TestTrue(TEXT("설계서 「🟡 전기 가구」 줄"), LineMatcher.FindNext()) || !TestNotNull(TEXT("DT_ItemTable"), Items)
		|| !TestNotNull(TEXT("규칙 행"), Rules))
	{
		return false;
	}
	const FString Line = LineMatcher.GetCaptureGroup(1);
	const TCHAR* const ElectricIds[] = { TEXT("item.fridge"), TEXT("item.fan_stand"), TEXT("item.radio"), TEXT("item.neon_sign") };

	// 설계서 값 — 「`item.fridge`(…) 소모 **40**」처럼 ID 뒤 첫 굵은 숫자.
	TMap<FName, int32> Expected;
	for (const TCHAR* const Id : ElectricIds)
	{
		const FString Pattern = FString::Printf(TEXT(R"(`%s`[^*]*\*\*(\d+)\*\*)"), Id);
		Expected.Add(FName(Id), CaptureInt(Line, *Pattern));
	}
	int32 ExpectedSum = 0;
	for (const TPair<FName, int32>& Pair : Expected)
	{
		TestTrue(*FString::Printf(TEXT("설계서 소모 읽힘 %s"), *Pair.Key.ToString()), Pair.Value > 0);
		ExpectedSum += Pair.Value;
	}

	TMap<FName, const FItemRow*> ElectricRows;
	int32 OtherWithPower = 0;
	Items->ForeachRow<FItemRow>(TEXT("ElectricRows"), [&](const FName&, const FItemRow& Row)
	{
		if (Expected.Contains(Row.ItemID))
		{
			ElectricRows.Add(Row.ItemID, &Row);
		}
		else if (Row.PowerUse != 0 || !Row.PlacedOnMeshRef.IsNull())
		{
			++OtherWithPower;
			AddError(FString::Printf(TEXT("전기 아닌 행에 소모·켜짐 짝: %s (%d)"), *Row.ItemID.ToString(), Row.PowerUse));
		}
	});
	TestEqual(TEXT("전기 가구 행 넷"), ElectricRows.Num(), 4);
	TestEqual(TEXT("전기 아닌 행은 소모 0·짝 없음"), OtherWithPower, 0);
	TestEqual(TEXT("넷 합 = 발전기 하나 출력(설계서 근거)"), ExpectedSum, Rules->GeneratorPower);

	int32 RowSum = 0;
	for (const TPair<FName, const FItemRow*>& Pair : ElectricRows)
	{
		const FItemRow& Row = *Pair.Value;
		const FString Id = Pair.Key.ToString();
		TestEqual(*FString::Printf(TEXT("소모 = 설계서 %s"), *Id), Row.PowerUse, Expected.FindRef(Pair.Key));
		RowSum += Row.PowerUse;
		UStaticMesh* const Off = Row.PlacedMeshRef.LoadSynchronous();
		UStaticMesh* const On = Row.PlacedOnMeshRef.LoadSynchronous();
		if (!TestNotNull(*FString::Printf(TEXT("꺼진 메시 %s"), *Id), Off) || !TestNotNull(*FString::Printf(TEXT("켜짐 짝 %s"), *Id), On))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("짝은 다른 메시 %s"), *Id), Off != On);
		TestEqual(*FString::Printf(TEXT("짝 섹션 1 %s"), *Id), On->GetNumSections(0), 1);
		TestEqual(*FString::Printf(TEXT("꺼진 섹션 1 %s"), *Id), Off->GetNumSections(0), 1);
		TestTrue(*FString::Printf(TEXT("짝 재질 = 꺼진 재질(같은 팔레트) %s"), *Id), On->GetMaterial(0) == Off->GetMaterial(0));
		const FBox OffBox = Off->GetBoundingBox();
		const FBox OnBox = On->GetBoundingBox();
		TestTrue(*FString::Printf(TEXT("짝 크기·자리 = 꺼진 메시 %s"), *Id),
			OffBox.Min.Equals(OnBox.Min, 0.1) && OffBox.Max.Equals(OnBox.Max, 0.1));
		AddInfo(FString::Printf(TEXT("[%s] 소모 %d · %s ↔ %s · 크기 %.1fx%.1fx%.1f / %.1fx%.1fx%.1f"), *Id, Row.PowerUse, *Off->GetName(),
			*On->GetName(), OffBox.GetSize().X, OffBox.GetSize().Y, OffBox.GetSize().Z, OnBox.GetSize().X, OnBox.GetSize().Y,
			OnBox.GetSize().Z));
	}
	TestEqual(TEXT("행 소모 합 = 설계서 합"), RowSum, ExpectedSum);

	// 놓인 액터 — 켜짐/꺼짐/미리보기/전기 아님.
	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("elec_rows"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	auto BodyOf = [](const ASnarePlacedStructureActor* Actor) -> const UStaticMeshComponent*
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (const UStaticMeshComponent* const Mesh : Meshes)
		{
			if (Mesh->GetFName() == TEXT("Body"))
			{
				return Mesh;
			}
		}
		return nullptr;
	};
	const FName FridgeId(TEXT("item.fridge"));
	const FItemRow* const FridgeRow = ElectricRows.FindRef(FridgeId);
	ASnarePlacedStructureActor* const Fridge = Spawn(FridgeId, TEXT("fridge_k"));
	const UStaticMeshComponent* const Body = BodyOf(Fridge);
	if (FridgeRow != nullptr && TestNotNull(TEXT("냉장고 몸"), Body))
	{
		const UStaticMesh* const Off = FridgeRow->PlacedMeshRef.Get();
		const UStaticMesh* const On = FridgeRow->PlacedOnMeshRef.Get();
		const FTransform Rest = Body->GetRelativeTransform();
		TestTrue(TEXT("처음엔 꺼진 메시"), Body->GetStaticMesh() == Off && !Fridge->IsShowingElectricOnMesh());
		Fridge->SetElectricOn(true);
		TestTrue(TEXT("켜면 켜짐 짝"), Body->GetStaticMesh() == On && Fridge->IsShowingElectricOnMesh());
		TestTrue(TEXT("켜도 자리·배율 그대로"), Body->GetRelativeTransform().Equals(Rest, 0.01));
		Fridge->SetElectricOn(false);
		TestTrue(TEXT("끄면 꺼진 메시"), Body->GetStaticMesh() == Off && !Fridge->IsShowingElectricOnMesh());

		ASnarePlacedStructureActor* const Preview = Spawn(FridgeId, TEXT("fridge_kp"));
		Preview->SetPreviewVerdict(ESnarePlacementVerdict::Green);
		Preview->SetElectricOn(true);
		TestFalse(TEXT("미리보기는 켜도 꺼진 모습"), Preview->IsShowingElectricOnMesh());
	}
	ASnarePlacedStructureActor* const Chair = Spawn(TEXT("item.chair_wood"), TEXT("chair_k"));
	const UStaticMeshComponent* const ChairBody = BodyOf(Chair);
	const UStaticMesh* const ChairMesh = ChairBody != nullptr ? ChairBody->GetStaticMesh() : nullptr;
	Chair->SetElectricOn(true);
	TestTrue(TEXT("전기 아닌 가구는 켜도 같은 메시"), ChairBody != nullptr && ChairBody->GetStaticMesh() == ChairMesh
		&& !Chair->IsShowingElectricOnMesh());
	AddInfo(FString::Printf(TEXT("전기 가구 넷 소모 합 %d (설계서 %d · 발전기 %d)"), RowSum, ExpectedSum, Rules->GeneratorPower));
	DestroyTestWorld(World);
	return true;
}


// M108-08c 전기 가구 전력 셈 — 땅 소모 = 전등 × `LampPowerUse` + 전기 가구 `PowerUse` 합(8.11 「🟡 전기 가구」). 공급 ≥ 소모면 전기 가구는
// 낮밤 없이 켜지고, 하나 넘치거나 발전기 연료가 다 타면 전등까지 전부 꺼진다. 경계는 시험 전용 발전기 출력(= 넷 소모 합)으로 —
// 표의 소모·출력이 바뀌어도 「딱 맞으면 켜짐 · 전등 하나 넘치면 꺼짐」 꼴은 그대로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseElectricFurnitureTest, "Snarehaven.Base.Power.ElectricFurniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseElectricFurnitureTest::RunTest(const FString& Parameters)
{
	const UDataTable* const RuleTable = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const TableRules = RuleTable != nullptr ? RuleTable->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("ElectricFurniture")) : nullptr;
	if (!TestNotNull(TEXT("규칙 행"), TableRules) || !TestTrue(TEXT("발전기 줄"), TableRules->HasGeneratorRules()))
	{
		return false;
	}

	// 순수 셈 — 소모 합·한 줄 판정.
	TestEqual(TEXT("소모 = 전등 + 전기 가구"), SnareBuildRules::LandPowerUse(*TableRules, 2, 40), 2 * TableRules->LampPowerUse + 40);
	TestTrue(TEXT("공급 = 소모 → 켜짐"), SnareBuildRules::IsLandPowered(*TableRules, 100, 100));
	TestFalse(TEXT("소모가 하나 넘침 → 꺼짐"), SnareBuildRules::IsLandPowered(*TableRules, 100, 101));
	TestFalse(TEXT("켤 것 없음 → 꺼짐"), SnareBuildRules::IsLandPowered(*TableRules, 100, 0));

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const TCHAR* TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	const TCHAR* const ElectricIds[] = { TEXT("item.fridge"), TEXT("item.fan_stand"), TEXT("item.radio"), TEXT("item.neon_sign") };
	TArray<ASnarePlacedStructureActor*> Electrics;
	int32 ElectricSum = 0;
	for (const TCHAR* const Id : ElectricIds)
	{
		ASnarePlacedStructureActor* const Actor = Spawn(FName(Id), TEXT("elec_land"), FString(Id) + TEXT("_a"));
		TestTrue(*FString::Printf(TEXT("소모 칸이 액터에 %s"), Id), Actor->GetPowerUse() > 0);
		ElectricSum += Actor->GetPowerUse();
		Electrics.Add(Actor);
	}
	ASnarePlacedStructureActor* const Chair = Spawn(TEXT("item.chair_wood"), TEXT("elec_land"), TEXT("chair_a"));
	TestEqual(TEXT("전기 아닌 가구는 소모 0"), Chair->GetPowerUse(), 0);

	// 시험 전용 출력 — 발전기 하나 = 넷 소모 합 딱.
	FBaseRuleRow Rules = *TableRules;
	Rules.GeneratorPower = ElectricSum;
	ASnarePlacedStructureActor* const Generator = Spawn(Rules.GeneratorItemID, TEXT("elec_land"), TEXT("gen_a"));
	auto AllOn = [&Electrics](bool bOn)
	{
		for (const ASnarePlacedStructureActor* const Actor : Electrics)
		{
			if (Actor->IsElectricOn() != bOn || Actor->IsShowingElectricOnMesh() != bOn)
			{
				return false;
			}
		}
		return true;
	};

	USnarePowerSubsystem::ApplyPower(World, Rules, false);
	TestTrue(TEXT("연료 없음 → 전부 꺼짐"), AllOn(false));

	constexpr double FuelSeconds = 600.0;
	Generator->SetGeneratorFuelSecondsLeft(FuelSeconds);
	USnarePowerSubsystem::ApplyPower(World, Rules, false);
	TestTrue(TEXT("낮 · 공급 = 소모 → 전부 켜짐(낮밤 없이)"), AllOn(true));
	USnarePowerSubsystem::ApplyPower(World, Rules, true);
	TestTrue(TEXT("밤에도 켜짐"), AllOn(true));
	TestFalse(TEXT("의자는 켜짐 모습 없음"), Chair->IsShowingElectricOnMesh());
	int32 Supply = -1;
	int32 Use = -1;
	USnarePowerSubsystem::MeasureLand(World, Rules, TEXT("PFTESTBASE"), TEXT("elec_land"), Supply, Use);
	TestEqual(TEXT("판 소모 = 넷 합"), Use, ElectricSum);
	TestEqual(TEXT("판 공급 = 발전기 하나"), Supply, ElectricSum);

	// 미리보기·남의 땅은 이 땅 소모에 안 든다.
	ASnarePlacedStructureActor* const Preview = Spawn(TEXT("item.fridge"), TEXT("elec_land"), TEXT("fridge_p"));
	Preview->SetPreviewVerdict(ESnarePlacementVerdict::Green);
	ASnarePlacedStructureActor* const Foreign = Spawn(TEXT("item.fridge"), TEXT("other_land"), TEXT("fridge_o"));
	USnarePowerSubsystem::ApplyPower(World, Rules, false);
	TestTrue(TEXT("미리보기·남의 땅 냉장고 → 이 땅 그대로 켜짐"), AllOn(true));
	TestFalse(TEXT("미리보기는 켜짐 모습 없음"), Preview->IsShowingElectricOnMesh());
	TestFalse(TEXT("공급 없는 남의 땅 냉장고 꺼짐"), Foreign->IsElectricOn());
	Preview->Destroy();
	Foreign->Destroy();

	// 전등 하나 더 → 소모가 공급을 넘어 전등까지 전부 꺼짐.
	ASnarePlacedStructureActor* const Lamp = Spawn(Rules.LampItemID, TEXT("elec_land"), TEXT("lamp_a"));
	TestEqual(TEXT("밤 · 전등 하나 넘침 → 켜진 전등 0"), USnarePowerSubsystem::ApplyPower(World, Rules, true), 0);
	TestTrue(TEXT("넘치면 전기 가구 전부 꺼짐"), AllOn(false));
	TestFalse(TEXT("넘치면 전등도 꺼짐"), Lamp->IsLampLit());
	USnarePowerSubsystem::MeasureLand(World, Rules, TEXT("PFTESTBASE"), TEXT("elec_land"), Supply, Use);
	TestEqual(TEXT("판 소모 = 넷 + 전등"), Use, ElectricSum + Rules.LampPowerUse);
	Lamp->Destroy();
	USnarePowerSubsystem::ApplyPower(World, Rules, false);
	TestTrue(TEXT("전등 치우면 다시 켜짐"), AllOn(true));

	// 연료 끝 → 꺼짐.
	World->TimeSeconds += FuelSeconds + 1.0;
	TestFalse(TEXT("연료 다 탐"), Generator->IsGeneratorRunning());
	USnarePowerSubsystem::ApplyPower(World, Rules, false);
	TestTrue(TEXT("연료 끝 → 전부 꺼짐"), AllOn(false));
	AddInfo(FString::Printf(TEXT("전기 가구 넷 소모 합 %d · 시험 발전기 출력 %d · 전등 %d"), ElectricSum, Rules.GeneratorPower, Rules.LampPowerUse));

	DestroyTestWorld(World);
	return true;
}

// M108-08d-1 선풍기 날개 따로 돌리기 — 날개는 행 `PlacedSpinMeshRef` 따로 메시(섹션 1·몸과 같은 팔레트)로 몸에 붙어 머리 가운데에 젖혀
// 서고, 켜짐 모습인 동안만 초당 `PlacedSpinTurnsPerSecond`(설계서 8.11 「🟡 전기 가구」 「선풍기 날개 초당 n바퀴」) 바퀴로 돈다.
// 꺼지면 그 각에 멈추고 틱도 꺼진다. 미리보기는 날개가 보이되 안 돈다. 날개는 꺼진 몸 메시 경계(망 테) 안에 든다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseElectricSpinTest, "Snarehaven.Base.Power.ElectricSpin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseElectricSpinTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FRegexMatcher LineMatcher(FRegexPattern(TEXT(R"((\| 🟡 전기 가구 \(M108-08[^\n]*))")), Doc);
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestTrue(TEXT("설계서 「🟡 전기 가구」 줄"), LineMatcher.FindNext()) || !TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}
	const int32 DesignTurns = CaptureInt(LineMatcher.GetCaptureGroup(1), TEXT(R"(선풍기 날개 \*\*초당 (\d+)바퀴\*\*)"));
	TestTrue(TEXT("설계서 날개 바퀴 읽힘"), DesignTurns > 0);

	const FName FanId(TEXT("item.fan_stand"));
	const FItemRow* FanRow = nullptr;
	int32 OtherSpin = 0;
	Items->ForeachRow<FItemRow>(TEXT("ElectricSpin"), [&](const FName&, const FItemRow& Row)
	{
		if (Row.ItemID == FanId)
		{
			FanRow = &Row;
		}
		else if (!Row.PlacedSpinMeshRef.IsNull() || Row.PlacedSpinTurnsPerSecond != 0.f)
		{
			++OtherSpin;
			AddError(FString::Printf(TEXT("선풍기 아닌 행에 도는 메시: %s"), *Row.ItemID.ToString()));
		}
	});
	TestEqual(TEXT("도는 메시는 선풍기만"), OtherSpin, 0);
	if (!TestNotNull(TEXT("선풍기 행"), FanRow))
	{
		return false;
	}
	TestEqual(TEXT("날개 바퀴 = 설계서"), FanRow->PlacedSpinTurnsPerSecond, static_cast<float>(DesignTurns));
	UStaticMesh* const Off = FanRow->PlacedMeshRef.LoadSynchronous();
	UStaticMesh* const Blade = FanRow->PlacedSpinMeshRef.LoadSynchronous();
	if (!TestNotNull(TEXT("꺼진 몸"), Off) || !TestNotNull(TEXT("날개 메시"), Blade))
	{
		return false;
	}
	TestEqual(TEXT("날개 섹션 1"), Blade->GetNumSections(0), 1);
	TestTrue(TEXT("날개 재질 = 몸 재질(같은 팔레트)"), Blade->GetMaterial(0) == Off->GetMaterial(0));

	// 날개 경계 상자 면 가운데 여섯 점을 몸 좌표로(젖힘·자리) — 어느 각에서도 꺼진 몸 경계(망 테) 안. 상자 귀는 잎 끝이 아니라
	// (세 잎 끝 반지름 23.5 인데 귀는 29) 면 가운데로 본다.
	const FBox OffBox = Off->GetBoundingBox().ExpandBy(0.5);
	const FBox BladeBox = Blade->GetBoundingBox();
	const FVector Mid = BladeBox.GetCenter();
	const FQuat Tilt(FVector::XAxisVector, FMath::DegreesToRadians(FanRow->PlacedSpinTiltDeg));
	int32 Outside = 0;
	for (const float Angle : { 0.f, 45.f, 90.f, 135.f })
	{
		const FQuat Rot = Tilt * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Angle));
		for (const FVector& Local : { FVector(BladeBox.Min.X, Mid.Y, Mid.Z), FVector(BladeBox.Max.X, Mid.Y, Mid.Z),
			FVector(Mid.X, BladeBox.Min.Y, Mid.Z), FVector(Mid.X, BladeBox.Max.Y, Mid.Z),
			FVector(Mid.X, Mid.Y, BladeBox.Min.Z), FVector(Mid.X, Mid.Y, BladeBox.Max.Z) })
		{
			Outside += OffBox.IsInsideOrOn(FanRow->PlacedSpinPivotCm + Rot.RotateVector(Local)) ? 0 : 1;
		}
	}
	TestEqual(TEXT("날개 끝이 몸 경계 밖 0"), Outside, 0);

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("spin_land"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	ASnarePlacedStructureActor* const Fan = Spawn(FanId, TEXT("fan_s"));
	const UStaticMeshComponent* const Spin = Fan->GetSpinComponent();
	if (!TestNotNull(TEXT("날개 컴포넌트"), Spin))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("꺼져도 날개가 보임"), Spin->GetStaticMesh() == Blade && Spin->IsVisible());
	TestTrue(TEXT("날개는 몸에 붙음"), Spin->GetAttachParent() == Fan->GetBodyComponent());
	TestTrue(TEXT("앞(+Y)이 위를 봄(머리 젖힘)"), Spin->GetComponentQuat().GetAxisY().Z > 0.1);
	constexpr float Step = 0.1f;
	const float Turned = FMath::Fmod(360.f * FanRow->PlacedSpinTurnsPerSecond * Step, 360.f);
	Fan->TickElectricSpin(Step);
	TestFalse(TEXT("꺼짐 → 안 돎"), Fan->IsSpinning() || Fan->IsActorTickEnabled());
	TestEqual(TEXT("꺼짐 → 각 그대로"), Fan->GetSpinAngleDeg(), 0.f);

	Fan->SetElectricOn(true);
	TestTrue(TEXT("켜짐 → 돎·틱 켜짐"), Fan->IsSpinning() && Fan->IsActorTickEnabled());
	const FQuat Before = Spin->GetRelativeRotation().Quaternion();
	Fan->TickElectricSpin(Step);   // 시험 월드는 틱이 없어 직접 부른다
	TestEqual(TEXT("한 걸음 각 = 바퀴 × 360 × 초"), Fan->GetSpinAngleDeg(), Turned, 0.01f);
	TestFalse(TEXT("날개 돌림이 바뀜"), Spin->GetRelativeRotation().Quaternion().Equals(Before, 1e-4));
	TestTrue(TEXT("돌아도 축 자리 그대로"), Spin->GetRelativeLocation().Equals(FanRow->PlacedSpinPivotCm, 0.01));
	TestTrue(TEXT("돌아도 앞이 위를 봄"), Spin->GetComponentQuat().GetAxisY().Z > 0.1);

	Fan->SetElectricOn(false);
	const float Stopped = Fan->GetSpinAngleDeg();
	Fan->TickElectricSpin(Step);
	TestFalse(TEXT("끄면 멈춤·틱 꺼짐"), Fan->IsSpinning() || Fan->IsActorTickEnabled());
	TestEqual(TEXT("끄면 그 각에 멈춤"), Fan->GetSpinAngleDeg(), Stopped);
	TestTrue(TEXT("꺼져도 날개 보임"), Spin->IsVisible());

	Fan->SetFloorViewHidden(true);
	TestFalse(TEXT("층 보기 숨김 → 날개도 숨김"), Spin->IsVisible());
	Fan->SetFloorViewHidden(false);
	TestTrue(TEXT("숨김 풀면 날개 보임"), Spin->IsVisible());

	ASnarePlacedStructureActor* const Preview = Spawn(FanId, TEXT("fan_p"));
	Preview->SetPreviewVerdict(ESnarePlacementVerdict::Green);
	Preview->SetElectricOn(true);
	TestTrue(TEXT("미리보기 날개 보임·안 돎"), Preview->GetSpinComponent()->IsVisible() && !Preview->IsSpinning());

	ASnarePlacedStructureActor* const Fridge = Spawn(TEXT("item.fridge"), TEXT("fridge_s"));
	Fridge->SetElectricOn(true);
	TestTrue(TEXT("냉장고는 날개 없음"), !Fridge->GetSpinComponent()->IsVisible() && !Fridge->IsSpinning());
	AddInfo(FString::Printf(TEXT("선풍기 날개 초당 %.1f바퀴 · 0.1초 %.1f° · 자리 %s · 젖힘 %.0f° · 날개 크기 %s"), FanRow->PlacedSpinTurnsPerSecond, Turned,
		*FanRow->PlacedSpinPivotCm.ToString(), FanRow->PlacedSpinTiltDeg, *BladeBox.GetSize().ToString()));

	DestroyTestWorld(World);
	return true;
}

// M108-08d-2 라디오 가락 — 행 `PlacedOnLoopCue` 고리(`SFX_Radio_Loop_01`, 되풀이)는 켜짐 모습인 동안만 몸에 붙어 돌고 꺼지면 없어진다.
// 감쇠·음량 = 설계서 8.11 「🟡 전기 가구」 「라디오 가락 고리 감쇠 n / mcm · 음량 v」 = 행 값. 미리보기·냉장고는 안 튼다.
// 판정은 이름이 아니라 실제 오디오 컴포넌트·감쇠 칸·소리 자산 칸 (PROMPT 12).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseElectricLoopTest, "Snarehaven.Base.Power.ElectricLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseElectricLoopTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FRegexMatcher LineMatcher(FRegexPattern(TEXT(R"((\| 🟡 전기 가구 \(M108-08[^\n]*))")), Doc);
	const UDataTable* const Items = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestTrue(TEXT("설계서 「🟡 전기 가구」 줄"), LineMatcher.FindNext()) || !TestNotNull(TEXT("DT_ItemTable"), Items))
	{
		return false;
	}
	const FString Line = LineMatcher.GetCaptureGroup(1);
	const TCHAR* const LoopPattern = TEXT(R"(라디오 가락 고리 감쇠 \*\*(\d+) / (\d+)cm\*\* · 음량 \*\*([0-9.]+)\*\*)");
	const int32 DesignInner = CaptureInt(Line, LoopPattern, 1);
	const int32 DesignOuter = CaptureInt(Line, LoopPattern, 2);
	FRegexMatcher VolumeMatcher(FRegexPattern(LoopPattern), Line);
	const float DesignVolume = VolumeMatcher.FindNext() ? FCString::Atof(*VolumeMatcher.GetCaptureGroup(3)) : -1.f;
	TestTrue(TEXT("설계서 라디오 고리 숫자 읽힘"), DesignInner > 0 && DesignOuter > DesignInner && DesignVolume > 0.f);

	const FName RadioId(TEXT("item.radio"));
	const FItemRow* RadioRow = nullptr;
	int32 OtherLoop = 0;
	Items->ForeachRow<FItemRow>(TEXT("ElectricLoop"), [&](const FName&, const FItemRow& Row)
	{
		if (Row.ItemID == RadioId)
		{
			RadioRow = &Row;
		}
		else if (!Row.PlacedOnLoopCue.IsNone())
		{
			++OtherLoop;
			AddError(FString::Printf(TEXT("라디오 아닌 행에 켜짐 고리: %s"), *Row.ItemID.ToString()));
		}
	});
	TestEqual(TEXT("켜짐 고리는 라디오만"), OtherLoop, 0);
	if (!TestNotNull(TEXT("라디오 행"), RadioRow))
	{
		return false;
	}
	TestEqual(TEXT("고리 안쪽 = 설계서"), RadioRow->PlacedOnLoopInnerCm, static_cast<float>(DesignInner));
	TestEqual(TEXT("고리 바깥 = 설계서"), RadioRow->PlacedOnLoopOuterCm, static_cast<float>(DesignOuter));
	TestEqual(TEXT("고리 음량 = 설계서"), RadioRow->PlacedOnLoopVolume, DesignVolume, 0.001f);
	TestTrue(TEXT("라디오는 켜짐 메시 짝이 있음(켜짐 모습이 고리 조건)"), !RadioRow->PlacedOnMeshRef.IsNull());
	const FString WavePath = FString::Printf(TEXT("/Game/Game/Audio/SFX/SFX_%s_01.SFX_%s_01"), *RadioRow->PlacedOnLoopCue.ToString(),
		*RadioRow->PlacedOnLoopCue.ToString());
	const USoundWave* const Wave = LoadObject<USoundWave>(nullptr, *WavePath);
	if (!TestNotNull(*FString::Printf(TEXT("소리 자산 %s"), *WavePath), Wave))
	{
		return false;
	}
	TestTrue(TEXT("가락 자산은 되풀이(looping)"), Wave->bLooping);

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("loop_land"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	// 소리 장치가 없는 실행(`-NoSound`)에선 오디오 컴포넌트가 안 생긴다 — 발전기 소리 시험과 같이 경고로 알리고 「재생 중」만 건너뛴다.
	const bool bHasAudioDevice = World->GetAudioDeviceRaw() != nullptr;
	if (!bHasAudioDevice)
	{
		AddWarning(TEXT("소리 장치가 없다(-NoSound?) — 라디오 가락 재생 중인지는 이번 실행에서 못 잰다"));
	}

	ASnarePlacedStructureActor* const Radio = Spawn(RadioId, TEXT("radio_s"));
	TestFalse(TEXT("놓자마자(꺼짐)는 고리 끔"), Radio->IsElectricLoopOn() || Radio->IsElectricLoopPlaying());
	TestNull(TEXT("꺼짐 → 오디오 컴포넌트 없음"), Radio->GetElectricLoop());

	Radio->SetElectricOn(true);
	TestTrue(TEXT("켜짐 → 고리 켬"), Radio->IsElectricLoopOn());
	const UAudioComponent* const Loop = Radio->GetElectricLoop();
	if (bHasAudioDevice && TestNotNull(TEXT("가락 오디오 컴포넌트"), Loop))
	{
		TestTrue(TEXT("켜짐 → 재생 중"), Radio->IsElectricLoopPlaying());
		TestTrue(TEXT("가락 자산 = 행 큐"), Loop->Sound == Wave);
		const USoundAttenuation* const Attenuation = Loop->AttenuationSettings;
		if (TestNotNull(TEXT("감쇠 설정"), Attenuation))
		{
			TestEqual(TEXT("감쇠 안쪽 = 행"), static_cast<float>(Attenuation->Attenuation.AttenuationShapeExtents.X), RadioRow->PlacedOnLoopInnerCm);
			TestEqual(TEXT("감쇠 바깥 = 안쪽 + 사라지는 거리 = 행"),
				static_cast<float>(Attenuation->Attenuation.AttenuationShapeExtents.X + Attenuation->Attenuation.FalloffDistance),
				RadioRow->PlacedOnLoopOuterCm);
		}
		TestTrue(TEXT("소리는 라디오 몸에 붙음"), Loop->GetAttachParent() == Radio->GetRootComponent());
	}
	Radio->SetElectricOn(true);   // 같은 상태로 다시 불러도 고리가 둘이 되지 않는다
	TestTrue(TEXT("다시 켜도 같은 컴포넌트"), Radio->GetElectricLoop() == Loop);

	Radio->SetElectricOn(false);
	TestFalse(TEXT("끔 → 고리 끔"), Radio->IsElectricLoopOn() || Radio->IsElectricLoopPlaying());
	TestNull(TEXT("끔 → 오디오 컴포넌트 없앰"), Radio->GetElectricLoop());
	Radio->SetElectricOn(true);
	TestTrue(TEXT("다시 켬 → 다시 고리"), Radio->IsElectricLoopOn());

	ASnarePlacedStructureActor* const Preview = Spawn(RadioId, TEXT("radio_p"));
	Preview->SetPreviewVerdict(ESnarePlacementVerdict::Green);
	Preview->SetElectricOn(true);
	TestFalse(TEXT("미리보기 라디오는 고리 없음"), Preview->IsElectricLoopOn() || Preview->GetElectricLoop() != nullptr);

	ASnarePlacedStructureActor* const Fridge = Spawn(TEXT("item.fridge"), TEXT("fridge_l"));
	Fridge->SetElectricOn(true);
	TestFalse(TEXT("냉장고는 켜져도 고리 없음"), Fridge->IsElectricLoopOn() || Fridge->GetElectricLoop() != nullptr);
	AddInfo(FString::Printf(TEXT("라디오 가락 %s — 소리 장치 %d · 자산 %.1f초 되풀이 %d · 감쇠 %.0f / %.0fcm · 음량 %.2f"),
		*RadioRow->PlacedOnLoopCue.ToString(), bHasAudioDevice ? 1 : 0, Wave->Duration, Wave->bLooping ? 1 : 0,
		RadioRow->PlacedOnLoopInnerCm, RadioRow->PlacedOnLoopOuterCm, RadioRow->PlacedOnLoopVolume));

	DestroyTestWorld(World);
	return true;
}

// M108-08e 전기 가구 까닭 알림 — 전기 가구에 「쓰기」 하면 판은 안 열리고(INBOX 보정 10-20 14:40) HUD 짧은 알림 한 줄이
// 그 땅 켜짐/꺼짐과 공급·소모를 말한다 — 켜짐은 알림 카드, 꺼짐은 붉은 오류 알약. 숫자는 발전기 판과 같은 `MeasureLand`,
// 판정은 놓인 몸과 같은 `IsLandPowered`. 전기 아닌 가구는 알림 없이 배낭 제작 판 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseElectricStatusTest, "Snarehaven.Base.Power.ElectricStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseElectricStatusTest::RunTest(const FString& Parameters)
{
	// 순수 글.
	TestEqual(TEXT("켜짐 글"), UCraftingScreenWidget::FormatElectricStatusText(true, 105, 100).ToString(),
		FString(TEXT("켜짐 — 이 땅 전력 공급 105 / 소모 100")));
	TestEqual(TEXT("꺼짐 글"), UCraftingScreenWidget::FormatElectricStatusText(false, 10, 45).ToString(),
		FString(TEXT("꺼짐 — 전력이 모자랍니다 (공급 10 / 소모 45)")));

	InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	const FBaseRuleRow* const Rules = Base->GetRules();
	if (!TestNotNull(TEXT("규칙 행"), Rules) || !TestTrue(TEXT("발전기 줄"), Rules->HasGeneratorRules()))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const TCHAR* TentId, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TentId, StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	ASnarePlacedStructureActor* const Generator = Spawn(Rules->GeneratorItemID, TEXT("elec_land"), TEXT("gen_a"));
	ASnarePlacedStructureActor* const Radio = Spawn(TEXT("item.radio"), TEXT("elec_land"), TEXT("radio_a"));
	ASnarePlacedStructureActor* const Chair = Spawn(TEXT("item.chair_wood"), TEXT("elec_land"), TEXT("chair_a"));
	ASnarePlacedStructureActor* const Foreign = Spawn(TEXT("item.fridge"), TEXT("other_land"), TEXT("fridge_o"));
	const int32 RadioUse = Radio->GetPowerUse();
	if (!TestTrue(TEXT("라디오는 전기 가구"), RadioUse > 0) || !TestTrue(TEXT("발전기 하나가 라디오 하나를 켬"), Rules->GeneratorPower >= RadioUse))
	{
		DestroyTestWorld(World);
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}

	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("제작 화면"), Screen))
	{
		DestroyTestWorld(World);
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);
	Screen->TakeWidget();
	UHuntHudWidget* const HuntHud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("사냥 HUD"), HuntHud))
	{
		DestroyTestWorld(World);
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}
	HuntHud->TakeWidget();
	Screen->SetNoticeHudsForTest(HuntHud, nullptr);

	// 연료 없음 → 판 안 열림 + 붉은 오류 알약에 꺼짐 까닭(공급 0).
	const FString OffLine = UCraftingScreenWidget::FormatElectricStatusText(false, 0, RadioUse).ToString();
	Screen->OpenFor(Radio);
	TestFalse(TEXT("전기 가구 → 판 안 열림"), Screen->IsOpen());
	TestEqual(TEXT("연료 없음 → 꺼짐 글"), Screen->GetElectricNoticeText().ToString(), OffLine);
	TestEqual(TEXT("꺼짐 → HUD 오류 알약"), HuntHud->GetErrorBannerText().ToString(), OffLine);
	TestTrue(TEXT("꺼짐 → 알림 카드는 비어 있음"), HuntHud->GetCaptureBannerText().IsEmpty());

	// 연료 넣음 → 켜짐 까닭은 알림 카드. 놓인 몸 판정과 같다. 앞 알림이 내려간 뒤 누른다 — 떠 있을 때 또 누르면 작은 가구 판
	// (`Snarehaven.Base.Power.ElectricFurniturePanel`).
	Generator->SetGeneratorFuelSecondsLeft(600.0);
	USnarePowerSubsystem::ApplyPower(World, *Rules, false);
	TestTrue(TEXT("놓인 라디오 켜짐"), Radio->IsElectricOn());
	HuntHud->ClearErrorBanner();
	Screen->OpenFor(Radio);
	TestFalse(TEXT("켜짐도 판 안 열림"), Screen->IsOpen());
	const FString OnLine = UCraftingScreenWidget::FormatElectricStatusText(true, Rules->GeneratorPower, RadioUse).ToString();
	TestEqual(TEXT("연료 있음 → 켜짐 글"), Screen->GetElectricNoticeText().ToString(), OnLine);
	TestEqual(TEXT("켜짐 → HUD 알림 카드"), HuntHud->GetCaptureBannerText().ToString(), OnLine);
	TestTrue(TEXT("켜짐 → 오류 알약 내려감"), HuntHud->GetErrorBannerText().IsEmpty());

	// 발전기 판 「공급 / 소모」 줄과 같은 숫자.
	int32 Supply = -1;
	int32 Use = -1;
	USnarePowerSubsystem::MeasureLand(World, *Rules, TEXT("PFTESTBASE"), TEXT("elec_land"), Supply, Use);
	TestEqual(TEXT("알림 숫자 = 발전기 판 숫자"), OnLine,
		UCraftingScreenWidget::FormatElectricStatusText(true, Supply, Use).ToString());

	// 전기 아닌 가구 → 알림 없이 배낭 제작 판 그대로.
	HuntHud->ClearCaptureBanner();
	Screen->OpenFor(Chair);
	TestTrue(TEXT("의자 → 판 열림"), Screen->IsOpen());
	TestEqual(TEXT("의자 → 배낭 제작 쪽 그대로"), Screen->GetPage(), ESnareBaseScreenPage::Craft);
	TestTrue(TEXT("의자 → 알림 없음"), HuntHud->GetCaptureBannerText().IsEmpty() && HuntHud->GetErrorBannerText().IsEmpty());

	// 판이 떠 있을 때 남의 땅(공급 없음) 냉장고 → 판은 닫히고 그 땅 숫자로 꺼짐 알약.
	Screen->OpenFor(Foreign);
	const FString ForeignLine = UCraftingScreenWidget::FormatElectricStatusText(false, 0, Foreign->GetPowerUse()).ToString();
	TestFalse(TEXT("떠 있던 판 닫힘"), Screen->IsOpen());
	TestEqual(TEXT("남의 땅 냉장고 → 그 땅 꺼짐 글"), Screen->GetElectricNoticeText().ToString(), ForeignLine);
	TestEqual(TEXT("남의 땅 냉장고 → 오류 알약"), HuntHud->GetErrorBannerText().ToString(), ForeignLine);
	AddInfo(FString::Printf(TEXT("까닭 알림 켜짐 「%s」 · 라디오 소모 %d · 발전기 출력 %d"), *OnLine, RadioUse, Rules->GeneratorPower));

	Screen->Close();
	DestroyTestWorld(World);
	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}


// 전기 가구 작은 판 (INBOX 지시 10-20 15:40) — 까닭 알림이 떠 있는 동안 같은 전기 가구를 한 번 더 「쓰기」 하면 제작 목록 없는
// 작은 판(이름·까닭 줄·「가방에 넣기」)이 열려 페인트 없이도 치울 수 있다. 알림이 내려갔거나 다른 가구면 다시 알림부터.
// 「가방에 넣기」 는 서버 `RemoveStructure` 를 묻기만 하고(CLAUDE.md 8), 지을 수 없는 자리에선 줄 대신 까닭 한 줄.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseElectricFurniturePanelTest, "Snarehaven.Base.Power.ElectricFurniturePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseElectricFurniturePanelTest::RunTest(const FString& Parameters)
{
	const TSharedRef<TArray<FPendingCall>> Calls = InstallDeferredServer();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	USnareBaseSubsystem* const Base = MakeBase(*this, Backend);
	const FBaseRuleRow* const Rules = Base->GetRules();
	if (!TestNotNull(TEXT("규칙 행"), Rules))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}

	UWorld* const World = CreateTestWorld();
	auto Spawn = [World](FName ItemID, const FString& StructureId)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		ASnarePlacedStructureActor* const Actor = World->SpawnActor<ASnarePlacedStructureActor>(
			ASnarePlacedStructureActor::StaticClass(), FTransform::Identity, Params);
		Actor->InitStructure(ItemID, TEXT("panel_land"), StructureId, TEXT("PFTESTBASE"), 0, 100.f);
		Actor->FinishSpawning(FTransform::Identity);
		return Actor;
	};
	ASnarePlacedStructureActor* const Radio = Spawn(TEXT("item.radio"), TEXT("radio_p"));
	ASnarePlacedStructureActor* const Fridge = Spawn(TEXT("item.fridge"), TEXT("fridge_p"));
	UCraftingScreenWidget* const Screen = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	UHuntHudWidget* const HuntHud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestTrue(TEXT("라디오·냉장고는 전기 가구"), Radio->GetPowerUse() > 0 && Fridge->GetPowerUse() > 0)
		|| !TestNotNull(TEXT("제작 화면"), Screen) || !TestNotNull(TEXT("사냥 HUD"), HuntHud))
	{
		DestroyTestWorld(World);
		USnareBackendSubsystem::TestServerOverride = nullptr;
		return false;
	}
	Screen->Setup(nullptr, Base);
	Screen->SetBackendForTest(Backend);
	Screen->TakeWidget();
	HuntHud->TakeWidget();
	Screen->SetNoticeHudsForTest(HuntHud, nullptr);
	Screen->SetCanBuildForTest(true);

	// 첫 「쓰기」 — 판 없이 알림(발전기 없어 꺼짐 알약). 소모는 같은 땅 둘의 합.
	const FString LandOff = UCraftingScreenWidget::FormatElectricStatusText(false, 0, Radio->GetPowerUse() + Fridge->GetPowerUse()).ToString();
	Screen->OpenFor(Radio);
	TestFalse(TEXT("첫 쓰기 → 판 안 열림"), Screen->IsOpen());
	TestEqual(TEXT("첫 쓰기 → 꺼짐 알약"), HuntHud->GetErrorBannerText().ToString(), LandOff);

	// 알림이 떠 있는 동안 두 번째 「쓰기」 → 작은 가구 판: 까닭 줄 + 가방에 넣기, 제작 쪽 아님.
	Screen->OpenFor(Radio);
	TestTrue(TEXT("두 번째 쓰기 → 판 열림"), Screen->IsOpen());
	TestEqual(TEXT("두 번째 쓰기 → 가구 쪽"), Screen->GetPage(), ESnareBaseScreenPage::Furniture);
	TestEqual(TEXT("판 까닭 줄 = 알림 글"), Screen->GetFurnitureStatusText().ToString(), LandOff);
	TestTrue(TEXT("판에 가방에 넣기"), Screen->IsPickUpShown());

	// 가방에 넣기 → 서버에 묻기만, 판 닫힘.
	const int32 CallsBefore = Calls->Num();
	TestTrue(TEXT("가방에 넣기 → 물음"), Screen->PressPickUp());
	TestEqual(TEXT("서버 호출 하나"), Calls->Num(), CallsBefore + 1);
	if (Calls->Num() > CallsBefore)
	{
		TestEqual(TEXT("RemoveStructure 함수"), Calls->Last().FunctionName, FString(TEXT("RemoveStructure")));
		TestEqual(TEXT("structureId 인자"), ArgString(Calls->Last(), TEXT("structureId")), FString(TEXT("radio_p")));
		TestEqual(TEXT("tentId 인자"), ArgString(Calls->Last(), TEXT("tentId")), FString(TEXT("panel_land")));
	}
	TestFalse(TEXT("물은 뒤 판 닫힘"), Screen->IsOpen());

	// 알림이 내려간 뒤 「쓰기」 → 판이 아니라 알림부터 다시.
	HuntHud->ClearErrorBanner();
	Screen->OpenFor(Radio);
	TestFalse(TEXT("알림 내려간 뒤 → 판 안 열림"), Screen->IsOpen());
	TestEqual(TEXT("알림 내려간 뒤 → 알림 다시"), HuntHud->GetErrorBannerText().ToString(), LandOff);

	// 라디오 알림이 떠 있을 때 다른 전기 가구(냉장고) → 그 가구 알림부터.
	Screen->OpenFor(Fridge);
	TestFalse(TEXT("다른 가구 → 판 안 열림"), Screen->IsOpen());
	TestEqual(TEXT("다른 가구 → 그 가구 알림"), HuntHud->GetErrorBannerText().ToString(), LandOff);

	// 지을 수 없는 자리에서 두 번째 「쓰기」 → 판은 열리되 가방에 넣기 대신 까닭 줄(빈 판·갇힘 없음).
	Screen->SetCanBuildForTest(false);
	Screen->OpenFor(Fridge);
	TestTrue(TEXT("땅 밖 → 판 열림"), Screen->IsOpen() && Screen->GetPage() == ESnareBaseScreenPage::Furniture);
	TestFalse(TEXT("땅 밖 → 가방에 넣기 없음"), Screen->IsPickUpShown());
	AddInfo(FString::Printf(TEXT("작은 가구 판 까닭 「%s」 · 서버 호출 %d"), *Screen->GetFurnitureStatusText().ToString(), Calls->Num()));

	Screen->Close();
	DestroyTestWorld(World);
	USnareBackendSubsystem::TestServerOverride = nullptr;
	return true;
}

// M108-09 집 안 꾸미기 보기 (DESIGN 8.11 「집 안 보기 — 지붕·앞벽 투명」) — 집 안에 서면 덮개와 같이 **선 층 앞벽(-X)에 건 장식**도
// 숨는다(앞벽이 없는데 장식만 남으면 허공에 뒷면이 떠 방을 가린다). 뒷벽·옆벽 장식과 앞 줄 바닥 가구는 그대로, 밖에 나오면 되돌림.
// 2층 집 2층에 서면 1층 앞벽이 보이므로 1층 앞벽 장식도 보인다. 가름은 벽 변(게스트 화면엔 없다)이 아니라 액터 자리.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseFrontWallDecorTest, "Snarehaven.Base.HouseView.FrontWallDecor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseFrontWallDecorTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6) — 1층 집 앵커 (40,40) · 2층 집 앵커 (60,40) · 손님 주소 · 높이 150/200.
	const FString GuestOwner(TEXT("GUEST_TEST_ID"));
	const FString HouseId(TEXT("tent_front_decor_test"));
	const FString TallId(TEXT("tent_front_decor_tall_test"));
	const FIntPoint HouseCell(40, 40);
	const FIntPoint TallCell(60, 40);
	constexpr int32 FrontSide = 3;
	constexpr int32 BackSide = 1;
	constexpr int32 SideWall = 0;

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	Base->SetTablesForTest(LoadObject<UDataTable>(nullptr, ItemTablePath), LoadObject<UDataTable>(nullptr, RecipeTablePath),
		LoadObject<UDataTable>(nullptr, BaseRuleTablePath));
	FName TentItem;
	for (const TPair<FName, uint8*>& Pair : Base->GetItemTable()->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Tent)
		{
			TentItem = Row->ItemID;
			break;
		}
	}
	if (!TestFalse(TEXT("텐트 행"), TentItem.IsNone()) || !TestNotNull(TEXT("규칙 표"), Base->GetRules()))
	{
		return false;
	}
	const float CellCm = Base->GetRules()->CellSizeCm;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* const HostPawn = World->SpawnActor<APawn>(Params);
	if (!TestNotNull(TEXT("호스트 폰"), HostPawn))
	{
		return false;
	}
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(HostPawn, TEXT("HostBuild"));
	Build->RegisterComponent();
	const auto Find = [World](const FString& TentId, const FString& StructureId) -> ASnarePlacedStructureActor*
	{
		for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
		{
			if (!It->IsPreview() && It->GetTentId() == TentId && It->GetStructureId() == StructureId)
			{
				return *It;
			}
		}
		return nullptr;
	};
	const auto Hang = [&](const FString& TentId, const FIntPoint& Anchor, FName ItemID, const FString& StructureId,
		const FIntPoint& Offset, int32 Side, int32 HeightCm) -> ASnarePlacedStructureActor*
	{
		const bool bTaken = Build->HostAcceptShared(ItemID, TentId, StructureId, Anchor + Offset, 0, GuestOwner, NAME_None,
			FIntPoint::ZeroValue, Side, HeightCm);
		TestTrue(*FString::Printf(TEXT("%s 걸기를 받는다"), *StructureId), bTaken);
		return Find(TentId, StructureId);
	};
	const auto Shown = [](const ASnarePlacedStructureActor* Piece)
	{
		return Piece != nullptr && !Piece->IsFloorViewHidden() && Piece->GetBodyComponent() != nullptr && Piece->GetBodyComponent()->IsVisible();
	};

	// ① 1층 집 — 앞벽 둘(문 양옆 화환 150·네온 간판 200) · 뒷벽 지도 · 옆벽 액자 · 앞 줄 바닥 안락의자.
	TestTrue(TEXT("1층 집을 받는다"), Build->HostAcceptShared(TentItem, HouseId, FString(), HouseCell, 0, GuestOwner, NAME_None,
		FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, 1));
	ASnarePlacedStructureActor* const House = Find(HouseId, FString());
	if (!TestNotNull(TEXT("집 액터"), House) || !TestTrue(TEXT("집"), House->IsHouse()))
	{
		DestroyTestWorld(World);
		return false;
	}
	ASnarePlacedStructureActor* const FrontWreath = Hang(HouseId, HouseCell, TEXT("item.wall_wreath"), TEXT("front_wreath"), FIntPoint(0, 0), FrontSide, 150);
	ASnarePlacedStructureActor* const FrontNeon = Hang(HouseId, HouseCell, TEXT("item.neon_sign"), TEXT("front_neon"), FIntPoint(0, 2), FrontSide, 200);
	ASnarePlacedStructureActor* const BackMap = Hang(HouseId, HouseCell, TEXT("item.wall_map"), TEXT("back_map"), FIntPoint(2, 1), BackSide, 150);
	ASnarePlacedStructureActor* const SideFrame = Hang(HouseId, HouseCell, TEXT("item.picture_frame"), TEXT("side_frame"), FIntPoint(1, 2), SideWall, 150);
	TestTrue(TEXT("앞 줄 안락의자를 받는다"), Build->HostAcceptShared(TEXT("item.armchair"), HouseId, TEXT("front_chair"), HouseCell,
		0, GuestOwner, NAME_None));
	ASnarePlacedStructureActor* const FrontChair = Find(HouseId, TEXT("front_chair"));
	if (!TestNotNull(TEXT("앞벽 화환"), FrontWreath) || !TestNotNull(TEXT("앞벽 네온"), FrontNeon) || !TestNotNull(TEXT("뒷벽 지도"), BackMap)
		|| !TestNotNull(TEXT("옆벽 액자"), SideFrame) || !TestNotNull(TEXT("앞 줄 의자"), FrontChair))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 자리 가름 — 앞벽 둘만 앞벽 면 위, 뒷벽·옆벽·앞 줄 바닥 가구는 아님 (역검증: 의자는 앞 줄이어도 면에서 반 칸 안쪽).
	TestTrue(TEXT("앞벽 화환·네온은 앞벽 면"), House->IsOnFrontWall(FrontWreath->GetActorLocation()) && House->IsOnFrontWall(FrontNeon->GetActorLocation()));
	TestFalse(TEXT("뒷벽 지도는 앞벽 아님"), House->IsOnFrontWall(BackMap->GetActorLocation()));
	TestFalse(TEXT("옆벽 액자는 앞벽 아님"), House->IsOnFrontWall(SideFrame->GetActorLocation()));
	TestFalse(TEXT("앞 줄 의자는 앞벽 아님"), House->IsOnFrontWall(FrontChair->GetActorLocation()));
	TestFalse(TEXT("문 앞 디딤 자리(집 밖)는 앞벽 아님"), House->IsOnFrontWall(House->GetActorTransform().TransformPosition(
		FVector(SnareBuildRules::HouseDoorStepX(House->GetFootprint().X, CellCm), 0.f, 0.f))));

	const auto Seen = [&]()
	{
		return FString::Printf(TEXT("덮개 %d · 앞 화환 %d · 앞 네온 %d · 뒤 지도 %d · 옆 액자 %d · 의자 %d"),
			House->GetHouseRoofComponent()->IsVisible(), Shown(FrontWreath), Shown(FrontNeon), Shown(BackMap), Shown(SideFrame), Shown(FrontChair));
	};
	const FVector RoomCell = House->GetActorLocation() + FVector(0.f, -CellCm, 0.f);
	const FVector Outside = House->GetActorLocation() + FVector(-4.f * CellCm, 0.f, 0.f);
	House->UpdateHouseRoof(Outside);
	TestTrue(*FString::Printf(TEXT("집 밖 — 덮개·장식 다 보임 (%s)"), *Seen()), House->GetHouseRoofComponent()->IsVisible()
		&& Shown(FrontWreath) && Shown(FrontNeon) && Shown(BackMap) && Shown(SideFrame) && Shown(FrontChair));
	House->UpdateHouseRoof(RoomCell);
	TestTrue(*FString::Printf(TEXT("집 안 — 덮개와 앞벽 장식만 숨음 (%s)"), *Seen()), !House->GetHouseRoofComponent()->IsVisible()
		&& !Shown(FrontWreath) && !Shown(FrontNeon) && Shown(BackMap) && Shown(SideFrame) && Shown(FrontChair));
	House->UpdateHouseRoof(Outside);
	TestTrue(*FString::Printf(TEXT("다시 밖 — 되돌림 (%s)"), *Seen()), House->GetHouseRoofComponent()->IsVisible()
		&& Shown(FrontWreath) && Shown(FrontNeon) && Shown(BackMap) && Shown(SideFrame) && Shown(FrontChair));

	// ② 2층 집 — 1층 앞벽 장식은 1층 안에서 숨고, 2층에 서면(1층 앞벽 보임) 보인다.
	TestTrue(TEXT("2층 집을 받는다"), Build->HostAcceptShared(TentItem, TallId, FString(), TallCell, 0, GuestOwner, NAME_None,
		FIntPoint::ZeroValue, -1, 0, /*bHouse=*/true, 2, FString(), -1, /*HouseFloors=*/2));
	ASnarePlacedStructureActor* const Tall = Find(TallId, FString());
	ASnarePlacedStructureActor* const TallWreath = Hang(TallId, TallCell, TEXT("item.wall_wreath"), TEXT("tall_front_wreath"), FIntPoint(0, 2), FrontSide, 150);
	if (!TestNotNull(TEXT("2층 집 액터"), Tall) || !TestTrue(TEXT("2층 집"), Tall->IsTwoStoryHouse()) || !TestNotNull(TEXT("2층 집 앞벽 화환"), TallWreath))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FVector TallRoom = Tall->GetActorLocation() + FVector(0.f, -CellCm, 0.f);
	Tall->UpdateHouseRoof(TallRoom);
	TestTrue(*FString::Printf(TEXT("2층 집 1층 안 — 1층 앞벽·화환 숨음 (앞벽 %d · 화환 %d)"), Tall->GetHouseFrontComponent()->IsVisible(), Shown(TallWreath)),
		!Tall->GetHouseFrontComponent()->IsVisible() && !Shown(TallWreath));
	Tall->UpdateHouseRoof(TallRoom + FVector(0.f, 0.f, SnareBuildRules::UpperFloorZCm));
	TestTrue(*FString::Printf(TEXT("2층 집 2층 안 — 1층 앞벽·화환 보임 (앞벽 %d · 화환 %d)"), Tall->GetHouseFrontComponent()->IsVisible(), Shown(TallWreath)),
		Tall->GetHouseFrontComponent()->IsVisible() && Shown(TallWreath));
	// 다른 집 안에 서도 이 집 장식은 안 건드림 — 집마다 자기 텐트 주소만.
	Tall->UpdateHouseRoof(TallRoom);
	TestTrue(*FString::Printf(TEXT("2층 집 안에 서도 1층 집 앞벽 장식 그대로 (%s)"), *Seen()), Shown(FrontWreath) && Shown(FrontNeon));
	const FVector WreathIn = House->GetActorTransform().InverseTransformPosition(FrontWreath->GetActorLocation());
	AddInfo(FString::Printf(TEXT("앞벽 장식 집 공간 X %.1f (앞벽 면 %.1f) · 의자 X %.1f · 칸 %.0f"), WreathIn.X,
		-House->GetFootprint().X * CellCm * 0.5f, House->GetActorTransform().InverseTransformPosition(FrontChair->GetActorLocation()).X, CellCm));
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
