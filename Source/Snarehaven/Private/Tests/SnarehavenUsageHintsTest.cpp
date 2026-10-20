// 처음 들기·처음 다가가기 안내 표·저장 2층 검사 (M28-05a, `docs/specs/onboarding-usage-hints.md`).
// 잡는 것: ① 표가 비거나 줄이 빠지는 것(15줄) ② 낚싯대·곡괭이 변종이 대표 줄로 안 모여 같은 안내가 여러 번 뜨는 것
// ③ 표에 없는 ItemID(오타)로 줄이 영영 안 뜨는 것 ④ 본 키가 디스크에 안 써져 앱을 다시 켜면 또 뜨는 것
// ⑤ 시험이 이 기기의 진짜 기록(`Snarehaven.Hints`)을 건드리는 것.
// M28-05b — 처음 들기: ⑥ 처음 든 도구 → HUD 안내 한 줄 1번, 다시 들면 0 ⑦ 첫 실행 안내가 떠 있는 동안 든 것은 안 뜨고
// 본 것도 아님 ⑧ 상황 버튼이 가방 도구를 자동으로 들 때(`BeginAttack`)도 뜸 ⑨ 떠 있는 중 다른 도구는 버림(본 것 아님)
// ⑩ 표의 `ShowSeconds` 가 지나면 내려감.
// M28-05c — 처음 다가가기: ⑪ 잡화점 상인 대화 거리에 처음 들면 한 줄 1번, 서 있는 동안·다시 와도 0
// ⑫ 첫 실행 안내 중 다가간 것은 안 뜨고 본 것 아님, 안내가 끝나도 서 있기만 하면 안 뜨고 다시 다가가야 뜸
// ⑬ 낚시 상인(잡화점 아님)은 계기 아님 ⑭ 목장 칸 동사(수확·넣기)는 상인보다 먼저 `ranch.slot`.
// M28-05c2 — 목장 HUD(`L_Ranch` 엔 사냥 HUD 가 없다): ⑮ 잡화점 앞 처음 → 목장 HUD 알약 1번(보임·글 = 표 줄), 다시 와도 0
// ⑯ 목장 HUD 에서 본 키는 사냥 HUD 에서도 안 뜸(본 목록 하나) ⑰ 첫 실행 안내 중엔 안 뜨고 본 것 아님 ⑱ 표 시간이 지나면 접힘.
// M28-06a — 안내서 쪽: ⑲ 쪽 수 = 활동 11 · 순서 = ROADMAP 목록 ⑳ 쪽마다 제목·순서 3줄·아이콘(아이템 표에 있는 ID)
// ㉑ 안내서 전용 `guide.*` 줄은 한 줄 알약으로 안 뜬다(본 것으로도 안 적음).
// M42-01·05 — 손 도구·기능 시설 중 안내 줄 없는 것 0 단정(`NoToolWithoutHint`, 메모리 사본 역검증).
// M42-04 — 텐트·배: 「열기」 대상 텐트·물가 배 띄우기 자리 → `place.Tent`·`place.Boat`, 목장 칸 > 놓인 것·배 > 상인 순.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.UI.UsageHints; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/MeleeAttackComponent.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Data/UsageHintRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Gathering/TapFishingComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SnarehavenCharacter.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareFirstRunGuideWidget.h"
#include "UI/SnareUsageHints.h"
#include "World/FishingSpotActor.h"
#include "World/ShopVendorActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsTableTest, "Snarehaven.UI.UsageHints.TableGroupsToolsAndPlaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsTableTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = SnareUsageHints::LoadTable();
	if (!TestNotNull(TEXT("DT_UsageHints 로드"), Table))
	{
		return false;
	}
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	// ① 줄 수 · 빈 칸.
	// 줄 수는 박지 않는다 — 새 도구·시설 누락은 `NoToolWithoutHint` 가 잡는다(M42-05).
	TestTrue(TEXT("표에 줄이 있다"), Table->GetRowMap().Num() > 0);
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FUsageHintRow* const Row = reinterpret_cast<const FUsageHintRow*>(Pair.Value);
		if (Pair.Key.ToString().StartsWith(TEXT("guide.")))
		{
			continue; // 안내서 전용 — 한 줄 칸은 비어 있어야 한다(아래 GuidePages 시험).
		}
		TestFalse(FString::Printf(TEXT("%s 글이 있다"), *Pair.Key.ToString()), Row->Line.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s 떠 있는 시간 > 0"), *Pair.Key.ToString()), Row->ShowSeconds > 0.f);

		// ③ 도구 줄의 ItemID 가 아이템 표에 실제로 있다(오타면 영영 안 뜬다).
		const FString KeyText = Pair.Key.ToString();
		if (KeyText.StartsWith(TEXT("tool.")))
		{
			const FName OwnItem(*KeyText.RightChop(5));
			TestNotNull(FString::Printf(TEXT("%s 대표 ItemID 가 아이템 표에 있다"), *KeyText),
				SnareItemRows::FindByItemID(ItemTable, OwnItem));
		}
		for (const FName& Also : Row->AlsoItemIDs)
		{
			TestNotNull(FString::Printf(TEXT("%s 의 %s 가 아이템 표에 있다"), *KeyText, *Also.ToString()),
				SnareItemRows::FindByItemID(ItemTable, Also));
		}
	}

	// ② 대표 줄로 모으기.
	const FName RodKey(TEXT("tool.item.rod_basic"));
	const FName PickKey(TEXT("tool.item.pickaxe_basic"));
	TestEqual(TEXT("기본 낚싯대 = 자기 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.rod_basic")), RodKey);
	TestEqual(TEXT("행운 낚싯대 → 기본 낚싯대 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.rod_lucky")), RodKey);
	TestEqual(TEXT("미끼 낚싯대 → 기본 낚싯대 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.rod_bait")), RodKey);
	TestEqual(TEXT("전기 곡괭이 → 기본 곡괭이 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.pickaxe_electric")), PickKey);
	TestEqual(TEXT("손도끼 = 자기 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.hatchet")), FName(TEXT("tool.item.hatchet")));
	TestEqual(TEXT("마체테 = 자기 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.machete")), FName(TEXT("tool.item.machete")));
	const FName PistolKey(TEXT("tool.item.pistol_semi"));
	TestEqual(TEXT("반자동 권총 = 자기 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.pistol_semi")), PistolKey);
	TestEqual(TEXT("현대 소총 → 권총 줄"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.rifle_modern")), PistolKey);
	TestEqual(TEXT("도구 아닌 것(원목)은 줄 없음"), SnareUsageHints::FindKeyForItem(Table, TEXT("item.wood")), FName(NAME_None));
	TestEqual(TEXT("맨손은 줄 없음"), SnareUsageHints::FindKeyForItem(Table, NAME_None), FName(NAME_None));

	TestEqual(TEXT("작업대 → place.Workbench"),
		SnareUsageHints::FindKeyForPlaceable(Table, EPlaceableFunction::Workbench), FName(TEXT("place.Workbench")));
	TestEqual(TEXT("화로 → place.Grill"),
		SnareUsageHints::FindKeyForPlaceable(Table, EPlaceableFunction::Grill), FName(TEXT("place.Grill")));
	TestEqual(TEXT("제련소 → place.Smelter"),
		SnareUsageHints::FindKeyForPlaceable(Table, EPlaceableFunction::Smelter), FName(TEXT("place.Smelter")));
	TestEqual(TEXT("텐트 → place.Tent"),
		SnareUsageHints::FindKeyForPlaceable(Table, EPlaceableFunction::Tent), FName(TEXT("place.Tent")));
	TestEqual(TEXT("구명보트 → place.Boat"),
		SnareUsageHints::FindKeyForPlaceable(Table, EPlaceableFunction::Boat), FName(TEXT("place.Boat")));
	TestEqual(TEXT("가구(None)는 줄 없음"), SnareUsageHints::FindKeyForPlaceable(Table, EPlaceableFunction::None), FName(NAME_None));
	TestNotNull(TEXT("목장 칸 줄"), SnareUsageHints::FindRow(Table, TEXT("ranch.slot")));
	TestNotNull(TEXT("상인 줄"), SnareUsageHints::FindRow(Table, TEXT("vendor.shop")));

	const FUsageHintRow* const RodRow = SnareUsageHints::FindRow(Table, RodKey);
	AddInfo(FString::Printf(TEXT("[UsageHints] 줄 %d · 낚싯대 [%s] %.1f초"), Table->GetRowMap().Num(),
		RodRow != nullptr ? *RodRow->Line.ToString() : TEXT("-"), RodRow != nullptr ? RodRow->ShowSeconds : 0.f));
	return true;
}

// M42-01·05 — 안내 빈 칸: 아이템 표에서 손에 드는 행(Tool·Weapon)·기능 있는 놓는 행마다 표 찾기를 불러
// 줄이 없는 것을 모은다. 줄 수 박기로는 새 도구 누락이 안 걸리니, 빈 칸 0 을 이 단정이 맡는다(예외 없음).
namespace
{
	struct FHintCoverage
	{
		int32 HeldRows = 0;
		int32 PlaceFunctions = 0;
		TArray<FString> MissingHeld;
		TArray<FString> MissingPlaces;
	};

	FHintCoverage SurveyHintCoverage(const UDataTable* HintTable, const UDataTable* ItemTable)
	{
		FHintCoverage Result;
		TSet<EPlaceableFunction> Functions;
		TSet<EPlaceableFunction> MissingFunctions;
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Item = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Item->EquipSlot == EEquipSlot::Tool || Item->EquipSlot == EEquipSlot::Weapon)
			{
				++Result.HeldRows;
				if (SnareUsageHints::FindKeyForItem(HintTable, Item->ItemID).IsNone())
				{
					Result.MissingHeld.Add(Item->ItemID.ToString());
				}
			}
			if (Item->PlaceableFunction != EPlaceableFunction::None)
			{
				Functions.Add(Item->PlaceableFunction);
				if (SnareUsageHints::FindKeyForPlaceable(HintTable, Item->PlaceableFunction).IsNone())
				{
					MissingFunctions.Add(Item->PlaceableFunction);
				}
			}
		}
		Result.PlaceFunctions = Functions.Num();
		for (const EPlaceableFunction Function : MissingFunctions)
		{
			Result.MissingPlaces.Add(StaticEnum<EPlaceableFunction>()->GetNameStringByValue(static_cast<int64>(Function)));
		}
		Result.MissingHeld.Sort();
		Result.MissingPlaces.Sort();
		return Result;
	}

	// 역검증용 — 진짜 표를 건드리지 않고 메모리 사본에서 한 줄만 뺀다.
	UDataTable* CopyHintTableWithout(const UDataTable* Source, FName DropKey)
	{
		UDataTable* const Copy = NewObject<UDataTable>(GetTransientPackage());
		Copy->RowStruct = FUsageHintRow::StaticStruct();
		for (const TPair<FName, uint8*>& Pair : Source->GetRowMap())
		{
			if (Pair.Key != DropKey)
			{
				Copy->AddRow(Pair.Key, *reinterpret_cast<const FUsageHintRow*>(Pair.Value));
			}
		}
		return Copy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsNoToolWithoutHintTest, "Snarehaven.UI.UsageHints.NoToolWithoutHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsNoToolWithoutHintTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = SnareUsageHints::LoadTable();
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_UsageHints 로드"), Table) || !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	const FHintCoverage Coverage = SurveyHintCoverage(Table, ItemTable);
	TestTrue(TEXT("손에 드는 행이 있다(표를 못 읽으면 0 이 거짓 통과)"), Coverage.HeldRows > 0);
	TestTrue(TEXT("기능 있는 놓는 행이 있다"), Coverage.PlaceFunctions > 0);
	AddInfo(FString::Printf(TEXT("[HintCoverage] 손 도구 %d 중 안내 없음 %d: %s · 기능 시설 %d종 중 키 없음 %d: %s"),
		Coverage.HeldRows, Coverage.MissingHeld.Num(), *FString::Join(Coverage.MissingHeld, TEXT(", ")),
		Coverage.PlaceFunctions, Coverage.MissingPlaces.Num(), *FString::Join(Coverage.MissingPlaces, TEXT(", "))));
	for (const FString& ItemID : Coverage.MissingHeld)
	{
		AddError(FString::Printf(TEXT("손 도구 %s 에 처음 들기 안내 줄이 없다 — create_usage_hint_table.py 에 tool.* 줄 또는 AlsoItemIDs"), *ItemID));
	}
	for (const FString& Function : Coverage.MissingPlaces)
	{
		AddError(FString::Printf(TEXT("기능 시설 %s 에 다가가기 안내 줄이 없다 — place.%s 줄"), *Function, *Function));
	}

	// 역검증: 권총 줄을 뺀 사본 → 권총·소총(AlsoItemIDs) 둘이 빈 칸 · 배 줄을 뺀 사본 → Boat 빈 칸.
	const FHintCoverage NoPistol = SurveyHintCoverage(CopyHintTableWithout(Table, TEXT("tool.item.pistol_semi")), ItemTable);
	TestEqual(TEXT("역검증: 권총 줄 없으면 빈 칸 = 권총·소총"), NoPistol.MissingHeld,
		TArray<FString>{TEXT("item.bazooka"), TEXT("item.crossbow"), TEXT("item.mg"), TEXT("item.pistol_auto"), TEXT("item.pistol_semi"), TEXT("item.rifle_burst"), TEXT("item.rifle_modern"), TEXT("item.rifle_semi"), TEXT("item.shotgun"), TEXT("item.shotgun_auto"), TEXT("item.shotgun_short"), TEXT("item.smg")});
	const FHintCoverage NoBoat = SurveyHintCoverage(CopyHintTableWithout(Table, TEXT("place.Boat")), ItemTable);
	TestEqual(TEXT("역검증: 배 줄 없으면 빈 칸 = Boat"), NoBoat.MissingPlaces, TArray<FString>{TEXT("Boat")});
	AddInfo(FString::Printf(TEXT("[HintCoverage] 역검증 권총 줄 뺌 → %s · 배 줄 뺌 → %s"),
		*FString::Join(NoPistol.MissingHeld, TEXT(", ")), *FString::Join(NoBoat.MissingPlaces, TEXT(", "))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsGuidePagesTest, "Snarehaven.UI.UsageHints.GuidePages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsGuidePagesTest::RunTest(const FString& Parameters)
{
	using namespace SnareUsageHints;
	const UDataTable* const Table = LoadTable();
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_UsageHints 로드"), Table) || !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}

	// ⑲ ROADMAP M28-06 활동 목록 순서 그대로.
	const TArray<FString> ExpectedTitles = { TEXT("낚시"), TEXT("채집"), TEXT("사냥·포획"), TEXT("목장 수확"), TEXT("제작"),
		TEXT("집 짓기"), TEXT("배치"), TEXT("요리"), TEXT("용광로"), TEXT("상점"), TEXT("옷장") };
	const TArray<FName> Keys = GetGuidePageKeys(Table);
	TestEqual(TEXT("쪽 수 = 활동 수"), Keys.Num(), ExpectedTitles.Num());

	TArray<FString> Lines;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FUsageHintRow* const Row = FindRow(Table, Keys[Index]);
		const FString Title = Row->GuideTitle.ToString();
		if (ExpectedTitles.IsValidIndex(Index))
		{
			TestEqual(FString::Printf(TEXT("%d쪽 제목"), Index + 1), Title, ExpectedTitles[Index]);
		}
		// ⑳ 쪽마다 순서 3줄 · 빈 줄 없음 · 아이콘 ID 가 아이템 표에 있음(없으면 그림이 빈 원판).
		TestEqual(FString::Printf(TEXT("%s 순서 줄 수"), *Title), Row->GuideSteps.Num(), 3);
		for (const FText& Step : Row->GuideSteps)
		{
			TestFalse(FString::Printf(TEXT("%s 빈 줄 없음"), *Title), Step.IsEmpty());
		}
		TestNotNull(FString::Printf(TEXT("%s 아이콘 %s 가 아이템 표에 있다"), *Title, *Row->GuideIconItemID.ToString()),
			SnareItemRows::FindByItemID(ItemTable, Row->GuideIconItemID));
		Lines.Add(FString::Printf(TEXT("%s(%s) %d줄"), *Title, *Keys[Index].ToString(), Row->GuideSteps.Num()));
	}
	TestTrue(TEXT("빈 표는 쪽 0"), GetGuidePageKeys(nullptr).IsEmpty());

	// ㉑ 안내서 전용 줄은 알약으로 안 뜨고, 본 것으로 적히지도 않는다.
	ResetForTest(TestSection);
	const FName Gather(TEXT("guide.gather"));
	TestNotNull(TEXT("guide.gather 줄 있음"), FindRow(Table, Gather));
	TestNull(TEXT("guide.gather 는 한 줄 알약으로 안 뜸"), ClaimRow(Table, Gather, TestSection));
	TestFalse(TEXT("guide.gather 는 본 것으로 안 적힘"), IsHintSeen(Gather, TestSection));
	ResetForTest(TestSection);

	AddInfo(FString::Printf(TEXT("[UsageHints] 안내서 %d쪽: %s"), Keys.Num(), *FString::Join(Lines, TEXT(" · "))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsSeenTest, "Snarehaven.UI.UsageHints.SeenKeySurvivesReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsSeenTest::RunTest(const FString& Parameters)
{
	using namespace SnareUsageHints;
	const FName Hatchet(TEXT("tool.item.hatchet"));
	const FName Workbench(TEXT("place.Workbench"));

	// ⑤ 진짜 기록은 시험 전후로 같아야 한다.
	FString RealBefore;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealBefore, GGameUserSettingsIni);

	ResetForTest(TestSection);
	TestFalse(TEXT("지운 뒤엔 안 봤다"), IsHintSeen(Hatchet, TestSection));

	MarkHintSeen(Hatchet, TestSection);
	MarkHintSeen(Hatchet, TestSection);
	TestTrue(TEXT("적은 뒤엔 봤다"), IsHintSeen(Hatchet, TestSection));
	TestFalse(TEXT("다른 키는 그대로 안 봤다"), IsHintSeen(Workbench, TestSection));
	MarkHintSeen(Workbench, TestSection);
	TestFalse(TEXT("빈 키는 본 것이 아니다"), IsHintSeen(NAME_None, TestSection));

	// ④ 디스크의 ini 파일을 직접 다시 읽는다 — 메모리 캐시만 바뀐 게 아닌지.
	// `GGameUserSettingsIni` 는 UE5 에서 짧은 이름이라 디스크 경로(`Saved/Config/<플랫폼>/GameUserSettings.ini`)를 따로 만든다.
	const FString IniPath = FConfigCacheIni::GetDestIniFilename(*GGameUserSettingsIni, nullptr, *FPaths::GeneratedConfigDir());
	FString IniText;
	const bool bRead = FFileHelper::LoadFileToString(IniText, *IniPath);
	TestTrue(TEXT("GameUserSettings.ini 를 읽었다"), bRead);
	const FString Expected = FString::Printf(TEXT("[%s]"), TestSection);
	const int32 SectionAt = IniText.Find(Expected);
	TestTrue(TEXT("디스크에 시험 섹션이 있다"), SectionAt != INDEX_NONE);
	const FString AfterSection = SectionAt != INDEX_NONE ? IniText.RightChop(SectionAt) : FString();
	TestTrue(TEXT("디스크에 두 키가 한 번씩 (같은 키 두 번 적어도 하나)"),
		AfterSection.Contains(TEXT("Seen=tool.item.hatchet,place.Workbench")));
	AddInfo(FString::Printf(TEXT("[UsageHints] ini %s · 시험 섹션 %s"), *IniPath,
		SectionAt != INDEX_NONE ? TEXT("있음") : TEXT("없음")));

	ResetForTest(TestSection);
	TestFalse(TEXT("시험 뒤 지우면 다시 안 봤다"), IsHintSeen(Hatchet, TestSection));

	FString RealAfter;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealAfter, GGameUserSettingsIni);
	TestEqual(TEXT("진짜 기록(Snarehaven.Hints)은 그대로"), RealAfter, RealBefore);
	return true;
}

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다). 낚싯대를 자동으로 들고 던지는 길만 열면 된다.
	constexpr float HoldTestAttackDamage = 10.f;
	constexpr float HoldTestRangeCm = 300.f;
	constexpr float HoldTestIntervalSeconds = 1.f;
	constexpr float ApproachTestTalkRangeCm = 250.f;
	const FVector ApproachTestVendorAt(1000.f, 0.f, 0.f);
	const FVector ApproachTestNearAt(900.f, 0.f, 0.f);   // 상인에서 100cm — 대화 거리 안

	UWorld* CreateHintTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyHintTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// 도구 행은 메시 대기라 `EquipItem` 대신 칸만 채운다 (SnarehavenAttackButtonVerbTest 의 HoldTool 과 같은 길 — 같은 알림이 난다).
	bool HoldOnly(ASnarehavenCharacter* Player, FName ToolItemID)
	{
		TArray<FEquippedSlot> Slots;
		if (!ToolItemID.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = EEquipSlot::Tool;
			Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(ToolItemID);
			if (Slot.ItemRowName.IsNone())
			{
				return false;
			}
		}
		Player->GetEquipment()->ApplyEquippedSlots(Slots);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsFirstHoldTest, "Snarehaven.UI.UsageHints.FirstHoldShowsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsFirstHoldTest::RunTest(const FString& Parameters)
{
	using namespace SnareUsageHints;
	const FName HatchetID(TEXT("item.hatchet"));
	const FName RodID(TEXT("item.rod_basic"));
	const FName PickID(TEXT("item.pickaxe_basic"));
	const FName HatchetKey(TEXT("tool.item.hatchet"));
	const FName RodKey(TEXT("tool.item.rod_basic"));
	const FName PickKey(TEXT("tool.item.pickaxe_basic"));
	const UDataTable* const Table = LoadTable();
	const FUsageHintRow* const HatchetRow = FindRow(Table, HatchetKey);
	if (!TestNotNull(TEXT("손도끼 안내 줄"), HatchetRow))
	{
		return false;
	}

	UWorld* World = CreateHintTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("장비"), Player->GetEquipment()))
	{
		DestroyHintTestWorld(World);
		return false;
	}
	Player->DispatchBeginPlay();
	Player->GetMeleeAttack()->SetAttackSettings(HoldTestAttackDamage, HoldTestRangeCm, HoldTestIntervalSeconds);
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyHintTestWorld(World);
		return false;
	}
	FString RealBefore;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealBefore, GGameUserSettingsIni);
	ResetForTest(TestSection);
	Hud->SetUsageHintSection(TestSection);
	Hud->BindToCharacter(Player);

	int32 Shown = 0;
	FName LastKey;
	// 키가 새로 서는 순간을 센다 — 같은 줄이 그대로 남아 있는 것은 새 안내가 아니다.
	const auto Observe = [&Shown, &LastKey, Hud]()
	{
		const FName Now = Hud->GetUsageHintKey();
		if (!Now.IsNone() && Now != LastKey)
		{
			++Shown;
		}
		LastKey = Now;
	};
	const auto Expire = [&Observe, Hud, World]()
	{
		// 표의 가장 긴 줄보다 넉넉히 — 몇 초인지는 표가 정한다.
		for (int32 Step = 0; Step < 200 && !Hud->GetUsageHintKey().IsNone(); ++Step)
		{
			Hud->TickUsageHint(0.1f);
		}
		Observe();
	};

	// ⑥ 처음 든 손도끼 → 한 줄 1번, 글은 표 그대로.
	TestTrue(TEXT("손도끼 들기"), HoldOnly(Player, HatchetID));
	Observe();
	AddInfo(FString::Printf(TEXT("[UsageHint] 처음 손도끼 → 키 %s · 「%s」 · 본 것 %d"), *Hud->GetUsageHintKey().ToString(),
		*Hud->GetUsageHintText().ToString(), IsHintSeen(HatchetKey, TestSection) ? 1 : 0));
	TestEqual(TEXT("처음 손도끼 → 안내 1번"), Shown, 1);
	TestEqual(TEXT("손도끼 줄 키"), Hud->GetUsageHintKey(), HatchetKey);
	TestEqual(TEXT("글 = 표 줄"), Hud->GetUsageHintText().ToString(), HatchetRow->Line.ToString());
	TestTrue(TEXT("본 것으로 적힘"), IsHintSeen(HatchetKey, TestSection));

	// ⑩ 표 시간 바로 전엔 남아 있고, 지나면 내려간다.
	Hud->TickUsageHint(HatchetRow->ShowSeconds - 0.5f);
	TestEqual(TEXT("표 시간 전엔 그대로"), Hud->GetUsageHintKey(), HatchetKey);
	Hud->TickUsageHint(1.f);
	TestTrue(TEXT("표 시간 뒤 내려감"), Hud->GetUsageHintKey().IsNone());
	Observe();

	// ⑥ 내렸다 다시 들면 0.
	TestTrue(TEXT("빈손"), HoldOnly(Player, NAME_None));
	TestTrue(TEXT("손도끼 다시 들기"), HoldOnly(Player, HatchetID));
	Observe();
	AddInfo(FString::Printf(TEXT("[UsageHint] 다시 손도끼 → 지금까지 %d번"), Shown));
	TestEqual(TEXT("두 번째 들기 → 안내 0"), Shown, 1);

	// ⑦ 첫 실행 안내가 떠 있는 동안 낚싯대를 들면 안 뜨고 본 것도 아님.
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	TestNotNull(TEXT("첫 실행 안내"), Guide);
	if (Guide != nullptr)
	{
		Guide->bPersistSeen = false;
	}
	TestTrue(TEXT("안내 중 낚싯대 들기"), HoldOnly(Player, RodID));
	Observe();
	AddInfo(FString::Printf(TEXT("[UsageHint] 첫 실행 안내 중 낚싯대 → 지금까지 %d번 · 본 것 %d"), Shown,
		IsHintSeen(RodKey, TestSection) ? 1 : 0));
	TestEqual(TEXT("첫 실행 안내 중엔 안 뜸"), Shown, 1);
	TestFalse(TEXT("첫 실행 안내 중 든 것은 본 것 아님"), IsHintSeen(RodKey, TestSection));
	if (Guide != nullptr)
	{
		Guide->Skip();
	}

	// ⑧ 안내가 끝난 뒤 — 낚싯대를 가방에만 두고 포인트 앞에서 상황 버튼 → 자동으로 들고, 그 들기로 안내가 뜬다.
	TestTrue(TEXT("빈손"), HoldOnly(Player, NAME_None));
	TArray<FInventorySlot> Bag;
	FInventorySlot& RodSlot = Bag.AddDefaulted_GetRef();
	RodSlot.ItemID = RodID;
	RodSlot.Count = 1;
	TestTrue(TEXT("낚싯대를 가방에"), Player->GetInventory()->ApplyBackendSnapshot(Bag));
	AFishingSpotActor* const Spot = World->SpawnActor<AFishingSpotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Spot != nullptr && !Spot->HasActorBegunPlay())
	{
		Spot->DispatchBeginPlay();
	}
	Player->GetMeleeAttack()->RefreshTarget();
	Player->BeginAttack();
	Player->EndAttack();
	Observe();
	AddInfo(FString::Printf(TEXT("[UsageHint] 상황 버튼 자동 들기 → 든 도구 %s · 키 %s · 지금까지 %d번"),
		*Player->GetEquipment()->GetEquippedItem(EEquipSlot::Tool).ToString(), *Hud->GetUsageHintKey().ToString(), Shown));
	TestEqual(TEXT("자동 들기도 안내 1번"), Shown, 2);
	TestEqual(TEXT("낚싯대 줄 키"), Hud->GetUsageHintKey(), RodKey);

	// ⑨ 떠 있는 동안 곡괭이를 들면 버린다 — 본 것 아님, 내려간 뒤 다시 들면 그때 뜬다.
	TestTrue(TEXT("곡괭이 들기"), HoldOnly(Player, PickID));
	Observe();
	TestEqual(TEXT("떠 있는 중 다른 도구 → 그대로 낚싯대 줄"), Hud->GetUsageHintKey(), RodKey);
	TestFalse(TEXT("버린 곡괭이는 본 것 아님"), IsHintSeen(PickKey, TestSection));
	Expire();
	TestTrue(TEXT("빈손"), HoldOnly(Player, NAME_None));
	TestTrue(TEXT("곡괭이 다시 들기"), HoldOnly(Player, PickID));
	Observe();
	AddInfo(FString::Printf(TEXT("[UsageHint] 버린 곡괭이 다시 → 키 %s · 지금까지 %d번"), *Hud->GetUsageHintKey().ToString(), Shown));
	TestEqual(TEXT("버린 줄은 다음 들기에 뜸"), Hud->GetUsageHintKey(), PickKey);
	TestEqual(TEXT("모두 세 번"), Shown, 3);

	// ⑪ HUD 가 붙기 전에 이미 든 권총 → 붙는 순간 안내(M42-06 — 검사 스위치·존 이동처럼 장비가 먼저 서는 길).
	const FName PistolKey(TEXT("tool.item.pistol_semi"));
	Expire();
	Hud->BindToCharacter(nullptr);
	TestTrue(TEXT("HUD 없이 권총 들기"), HoldOnly(Player, FName(TEXT("item.pistol_semi"))));
	Hud->BindToCharacter(Player);
	AddInfo(FString::Printf(TEXT("[UsageHint] 붙기 전 든 권총 → 붙는 순간 키 %s"), *Hud->GetUsageHintKey().ToString()));
	TestEqual(TEXT("붙기 전 든 것도 붙는 순간 안내"), Hud->GetUsageHintKey(), PistolKey);
	Expire();

	// ⑤ 다른 HUD 시험처럼 섹션을 안 넘긴 HUD 도 시험 중엔 진짜 기록을 안 건드린다.
	UHuntHudWidget* const PlainHud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (PlainHud != nullptr)
	{
		Hud->BindToCharacter(nullptr);
		PlainHud->BindToCharacter(Player);
		TestTrue(TEXT("빈손"), HoldOnly(Player, NAME_None));
		TestTrue(TEXT("손도끼 다시"), HoldOnly(Player, HatchetID));
		PlainHud->BindToCharacter(nullptr);
	}
	FString RealAfter;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealAfter, GGameUserSettingsIni);
	TestEqual(TEXT("진짜 기록(Snarehaven.Hints)은 그대로"), RealAfter, RealBefore);

	ResetForTest(TestSection);
	Hud->BindToCharacter(nullptr);
	DestroyHintTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsFirstApproachTest, "Snarehaven.UI.UsageHints.FirstApproachShowsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsFirstApproachTest::RunTest(const FString& Parameters)
{
	using namespace SnareUsageHints;
	const FName ShopKey(TEXT("vendor.shop"));
	const FName RanchKey(TEXT("ranch.slot"));
	const UDataTable* const Table = LoadTable();
	const FUsageHintRow* const ShopRow = FindRow(Table, ShopKey);
	if (!TestNotNull(TEXT("상인 안내 줄"), ShopRow) || !TestNotNull(TEXT("목장 칸 안내 줄"), FindRow(Table, RanchKey)))
	{
		return false;
	}

	// ⑭ 동사 → 키 (목장 칸은 월드 없이 동사만으로 정해진다).
	TestEqual(TEXT("수확 → 목장 칸"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Harvest, false, EPlaceableFunction::None), RanchKey);
	TestEqual(TEXT("넣기 + 상인 곁 → 목장 칸이 먼저"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Place, true, EPlaceableFunction::None), RanchKey);
	TestEqual(TEXT("공격 + 상인 곁 → 상인"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Attack, true, EPlaceableFunction::None), ShopKey);
	TestEqual(TEXT("공격만 → 없음"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Attack, false, EPlaceableFunction::None), FName(NAME_None));
	// M42-04 — 「열기」 대상 놓인 것·물가 배 띄우기 자리 → `place.<기능>`, 그 키가 표에 실제로 있다(없으면 영영 안 뜸).
	const FName TentKey(TEXT("place.Tent"));
	const FName BoatKey(TEXT("place.Boat"));
	TestEqual(TEXT("열기 + 텐트 → place.Tent"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Open, false, EPlaceableFunction::Tent), TentKey);
	TestEqual(TEXT("낚시 + 배 띄우기 자리 → place.Boat"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Fish, false, EPlaceableFunction::Boat), BoatKey);
	TestEqual(TEXT("열기 + 작업대 + 상인 곁 → 작업대(버튼이 가리키는 쪽)"),
		UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Open, true, EPlaceableFunction::Workbench), FName(TEXT("place.Workbench")));
	TestEqual(TEXT("넣기 + 텐트 땅 → 목장 칸이 먼저"), UHuntHudWidget::PickApproachHintKey(ESnareInteractVerb::Place, false, EPlaceableFunction::Tent), RanchKey);
	TestNotNull(TEXT("텐트 줄이 표에 있다"), FindRow(Table, TentKey));
	TestNotNull(TEXT("배 줄이 표에 있다"), FindRow(Table, BoatKey));

	UWorld* World = CreateHintTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(ApproachTestVendorAt, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("상인 스폰"), Vendor))
	{
		DestroyHintTestWorld(World);
		return false;
	}
	Player->DispatchBeginPlay();
	Player->GetMeleeAttack()->SetAttackSettings(HoldTestAttackDamage, HoldTestRangeCm, HoldTestIntervalSeconds);
	Player->GetMeleeAttack()->SetNpcTalkRangeCm(ApproachTestTalkRangeCm);
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		DestroyHintTestWorld(World);
		return false;
	}
	FString RealBefore;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealBefore, GGameUserSettingsIni);
	ResetForTest(TestSection);
	Hud->SetUsageHintSection(TestSection);
	Hud->BindToCharacter(Player);

	int32 Shown = 0;
	FName LastKey;
	const auto Observe = [&Shown, &LastKey, Hud]()
	{
		const FName Now = Hud->GetUsageHintKey();
		if (!Now.IsNone() && Now != LastKey)
		{
			++Shown;
		}
		LastKey = Now;
	};
	const auto StandAt = [Player, Hud, &Observe](const FVector& At)
	{
		Player->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		Hud->CheckUsageHintForApproach();
		Observe();
	};
	const auto Expire = [&Observe, Hud]()
	{
		for (int32 Step = 0; Step < 200 && !Hud->GetUsageHintKey().IsNone(); ++Step)
		{
			Hud->TickUsageHint(0.1f);
		}
		Observe();
	};

	// ⑪ 멀리 → 0, 처음 다가가면 1, 서 있는 동안 0, 떠났다 다시 와도 0.
	StandAt(FVector::ZeroVector);
	TestEqual(TEXT("멀리선 안 뜸"), Shown, 0);
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 처음 잡화점 앞 → 키 %s · 「%s」 · 본 것 %d"), *Hud->GetUsageHintKey().ToString(),
		*Hud->GetUsageHintText().ToString(), IsHintSeen(ShopKey, TestSection) ? 1 : 0));
	TestEqual(TEXT("처음 잡화점 앞 → 안내 1번"), Shown, 1);
	TestEqual(TEXT("상인 줄 키"), Hud->GetUsageHintKey(), ShopKey);
	TestEqual(TEXT("글 = 표 줄"), Hud->GetUsageHintText().ToString(), ShopRow->Line.ToString());
	TestTrue(TEXT("본 것으로 적힘"), IsHintSeen(ShopKey, TestSection));
	Expire();
	StandAt(ApproachTestNearAt);
	StandAt(FVector::ZeroVector);
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 다시 잡화점 앞 → 지금까지 %d번"), Shown));
	TestEqual(TEXT("두 번째 다가가기 → 안내 0"), Shown, 1);

	// ⑫ 첫 실행 안내 중 다가가면 안 뜨고 본 것 아님 → 안내가 끝나도 서 있기만 하면 0 → 다시 다가가면 1.
	StandAt(FVector::ZeroVector);
	ResetForTest(TestSection);
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	TestNotNull(TEXT("첫 실행 안내"), Guide);
	if (Guide != nullptr)
	{
		Guide->bPersistSeen = false;
	}
	StandAt(ApproachTestNearAt);
	TestEqual(TEXT("첫 실행 안내 중엔 안 뜸"), Shown, 1);
	TestFalse(TEXT("첫 실행 안내 중 다가간 것은 본 것 아님"), IsHintSeen(ShopKey, TestSection));
	if (Guide != nullptr)
	{
		Guide->Skip();
	}
	StandAt(ApproachTestNearAt);
	TestEqual(TEXT("안내 끝나도 서 있기만 하면 안 뜸"), Shown, 1);
	StandAt(FVector::ZeroVector);
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 첫 실행 안내 뒤 다시 다가가기 → 키 %s · 지금까지 %d번"),
		*Hud->GetUsageHintKey().ToString(), Shown));
	TestEqual(TEXT("다음 다가가기에 뜸"), Shown, 2);
	Expire();

	// ⑬ 낚시 상인은 잡화점 줄의 계기가 아니다.
	StandAt(FVector::ZeroVector);
	ResetForTest(TestSection);
	Vendor->SetShopKind(EShopKind::Fishing);
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 낚시 상인 앞 → 키 %s · 지금까지 %d번"), *Hud->GetUsageHintKey().ToString(), Shown));
	TestEqual(TEXT("낚시 상인 앞 → 안 뜸"), Shown, 2);
	TestFalse(TEXT("낚시 상인은 본 것으로 안 적음"), IsHintSeen(ShopKey, TestSection));

	ResetForTest(TestSection);
	FString RealAfter;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealAfter, GGameUserSettingsIni);
	TestEqual(TEXT("진짜 기록(Snarehaven.Hints)은 그대로"), RealAfter, RealBefore);
	Hud->BindToCharacter(nullptr);
	DestroyHintTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenUsageHintsRanchHudTest, "Snarehaven.UI.UsageHints.RanchHudFirstApproach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenUsageHintsRanchHudTest::RunTest(const FString& Parameters)
{
	using namespace SnareUsageHints;
	const FName ShopKey(TEXT("vendor.shop"));
	const FUsageHintRow* const ShopRow = FindRow(LoadTable(), ShopKey);
	if (!TestNotNull(TEXT("상인 안내 줄"), ShopRow))
	{
		return false;
	}

	UWorld* World = CreateHintTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(ApproachTestVendorAt, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("상인 스폰"), Vendor))
	{
		DestroyHintTestWorld(World);
		return false;
	}
	Player->DispatchBeginPlay();
	Player->GetMeleeAttack()->SetAttackSettings(HoldTestAttackDamage, HoldTestRangeCm, HoldTestIntervalSeconds);
	Player->GetMeleeAttack()->SetNpcTalkRangeCm(ApproachTestTalkRangeCm);
	URanchHudWidget* const Ranch = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	UHuntHudWidget* const Hunt = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("목장 HUD 생성"), Ranch) || !TestNotNull(TEXT("사냥 HUD 생성"), Hunt))
	{
		DestroyHintTestWorld(World);
		return false;
	}
	Ranch->TakeWidget();   // 자리표시 트리(알약)를 세운다 — 보임 여부를 읽으려고
	FString RealBefore;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealBefore, GGameUserSettingsIni);
	ResetForTest(TestSection);
	Ranch->SetUsageHintSection(TestSection);
	Hunt->SetUsageHintSection(TestSection);
	Ranch->BindToCharacter(Player);

	int32 Shown = 0;
	FName LastKey;
	const auto StandAt = [Player, Ranch, &Shown, &LastKey](const FVector& At)
	{
		Player->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		Ranch->CheckUsageHintForApproach();
		const FName Now = Ranch->GetUsageHintKey();
		if (!Now.IsNone() && Now != LastKey)
		{
			++Shown;
		}
		LastKey = Now;
	};
	const auto Expire = [Ranch, &LastKey]()
	{
		for (int32 Step = 0; Step < 200 && !Ranch->GetUsageHintKey().IsNone(); ++Step)
		{
			Ranch->TickUsageHint(0.1f);
		}
		LastKey = Ranch->GetUsageHintKey();
	};

	// ⑮ 멀리 0 → 잡화점 앞 1(보임·글) → 떠났다 다시 와도 0.
	StandAt(FVector::ZeroVector);
	TestEqual(TEXT("목장 HUD 멀리선 안 뜸"), Shown, 0);
	TestFalse(TEXT("멀리선 알약 접힘"), Ranch->IsUsageHintVisible());
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 목장 HUD 처음 잡화점 앞 → 키 %s · 「%s」 · 알약 보임 %d · 본 것 %d"),
		*Ranch->GetUsageHintKey().ToString(), *Ranch->GetUsageHintText().ToString(), Ranch->IsUsageHintVisible() ? 1 : 0,
		IsHintSeen(ShopKey, TestSection) ? 1 : 0));
	TestEqual(TEXT("목장 HUD 처음 잡화점 앞 → 안내 1번"), Shown, 1);
	TestEqual(TEXT("목장 HUD 글 = 표 줄"), Ranch->GetUsageHintText().ToString(), ShopRow->Line.ToString());
	TestTrue(TEXT("목장 HUD 알약 보임"), Ranch->IsUsageHintVisible());
	TestTrue(TEXT("본 것으로 적힘"), IsHintSeen(ShopKey, TestSection));

	// ⑱ 표 시간이 지나면 접힌다.
	Expire();
	TestTrue(TEXT("표 시간 뒤 키 비움"), Ranch->GetUsageHintKey().IsNone());
	TestFalse(TEXT("표 시간 뒤 알약 접힘"), Ranch->IsUsageHintVisible());
	StandAt(FVector::ZeroVector);
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 목장 HUD 다시 잡화점 앞 → 지금까지 %d번"), Shown));
	TestEqual(TEXT("목장 HUD 두 번째 다가가기 → 0"), Shown, 1);

	// ⑯ 본 목록은 하나 — 목장 HUD 에서 본 잡화점 줄은 사냥 HUD 에서도 안 뜬다.
	Ranch->BindToCharacter(nullptr);
	Player->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
	Hunt->BindToCharacter(Player);
	Hunt->CheckUsageHintForApproach();
	Player->SetActorLocation(ApproachTestNearAt, false, nullptr, ETeleportType::TeleportPhysics);
	Hunt->CheckUsageHintForApproach();
	AddInfo(FString::Printf(TEXT("[UsageHint] 목장에서 본 뒤 사냥 HUD 잡화점 앞 → 키 %s"), *Hunt->GetUsageHintKey().ToString()));
	TestTrue(TEXT("목장 HUD 에서 본 키는 사냥 HUD 에서 안 뜸"), Hunt->GetUsageHintKey().IsNone());
	Hunt->BindToCharacter(nullptr);

	// ⑰ 첫 실행 안내 중엔 안 뜨고 본 것 아님 → 안내가 끝난 뒤 다시 다가가면 뜸.
	ResetForTest(TestSection);
	Player->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
	Ranch->BindToCharacter(Player);
	StandAt(FVector::ZeroVector);
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	TestNotNull(TEXT("첫 실행 안내"), Guide);
	if (Guide != nullptr)
	{
		Guide->bPersistSeen = false;
	}
	StandAt(ApproachTestNearAt);
	TestEqual(TEXT("첫 실행 안내 중엔 목장 HUD 안 뜸"), Shown, 1);
	TestFalse(TEXT("첫 실행 안내 중 다가간 것은 본 것 아님"), IsHintSeen(ShopKey, TestSection));
	if (Guide != nullptr)
	{
		Guide->Skip();
	}
	StandAt(FVector::ZeroVector);
	StandAt(ApproachTestNearAt);
	AddInfo(FString::Printf(TEXT("[UsageHint] 목장 HUD 첫 실행 안내 뒤 다시 다가가기 → 키 %s · 지금까지 %d번"),
		*Ranch->GetUsageHintKey().ToString(), Shown));
	TestEqual(TEXT("안내 끝난 뒤 다시 다가가면 뜸"), Shown, 2);

	ResetForTest(TestSection);
	FString RealAfter;
	GConfig->GetString(DefaultSection, TEXT("Seen"), RealAfter, GGameUserSettingsIni);
	TestEqual(TEXT("진짜 기록(Snarehaven.Hints)은 그대로"), RealAfter, RealBefore);
	Ranch->BindToCharacter(nullptr);
	DestroyHintTestWorld(World);
	return true;
}

#endif
