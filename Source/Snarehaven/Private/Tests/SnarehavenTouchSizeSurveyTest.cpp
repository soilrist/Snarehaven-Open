// ROADMAP M38-01 조사 → M38-05 단정 — 누르는 칸 크기 (DESIGN 2 「🟡 누르는 칸 최소 크기」 72).
// HUD 넷과 ESC 목록 판들을 시험 월드에서 만들어 트리를 세운 뒤, 보이는 `UButton` 마다 짧은 변 × 판 렌더 배율을 재서
// 72 미만·크기 0·판을 못 만듦을 판·버튼 이름과 함께 `AddError` 로 낸다. 예외 없음(배율 판 안 칸은 배율 곱한 값으로 잰다).
// 역검증: `-SnareTouchFloor=200` 을 붙여 돌리면 기준이 200 으로 올라가 실패해야 한다(소스·파일은 그대로).
// 크기 재는 길(M38-04b): ① 판을 1920×1080 뿌리 칸에 배치(`SlatePrepass` → `ArrangeChildren` 를 끝까지)해
//   버튼이 실제로 받은 칸 × 쌓인 렌더 배율 ② 배치에서 못 찾으면(M38-01 길) 캔버스 칸 → 감싼 `USizeBox` →
//   Slate 원하는 크기 × 조상 렌더 배율. 원하는 크기는 가로·세로 상자가 칸을 채우기 전 값이라 실제보다 작다.
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.UI.TouchSize; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SWidget.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/SnareStoreSettings.h"
#include "UI/AttendanceScreenWidget.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/BuildHudWidget.h"
#include "UI/ChestScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/CrateScreenWidget.h"
#include "UI/GachaScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/QuestScreenWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareGuideBookWidget.h"
#include "UI/WardrobeShopWidget.h"
#include "UI/WardrobeWidget.h"
#include "UI/WaterActionWidget.h"
#include "UI/WorldMapScreenWidget.h"

namespace
{
	/** DESIGN 2 「🟡 누르는 칸 최소 크기」 — 1920×1080 기준 짧은 변. */
	constexpr float TouchSizeFloor = 72.f;

	/** 단정 기준 — HUD 72, 메뉴 판 121(애플 44pt, DESIGN 2 10-16). 역검증 때만 명령줄 `-SnareTouchFloor=` 로 올린다. */
	float EveryButtonFloor(float Base = TouchSizeFloor)
	{
		float Floor = Base;
		FParse::Value(FCommandLine::Get(), TEXT("SnareTouchFloor="), Floor);
		return Floor;
	}

	UWorld* CreateTouchSurveyWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTouchSurveyWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	bool IsStretched(const FAnchors& Anchors)
	{
		return !FMath::IsNearlyEqual(Anchors.Minimum.X, Anchors.Maximum.X)
			|| !FMath::IsNearlyEqual(Anchors.Minimum.Y, Anchors.Maximum.Y);
	}

	/** 버튼과 조상 중 하나라도 접혀 있거나 숨었으면 거짓. */
	bool IsShownInTree(const UWidget& Button)
	{
		for (const UWidget* Walk = &Button; Walk != nullptr; Walk = Walk->GetParent())
		{
			const ESlateVisibility Vis = Walk->GetVisibility();
			if (Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden)
			{
				return false;
			}
		}
		return true;
	}

	/** 버튼과 조상의 렌더 변환 배율 곱(가로·세로 중 작은 쪽). */
	float AccumulatedRenderScale(const UWidget& Button)
	{
		float Scale = 1.f;
		for (const UWidget* Walk = &Button; Walk != nullptr; Walk = Walk->GetParent())
		{
			const FVector2D Local = Walk->GetRenderTransform().Scale;
			Scale *= FMath::Min(FMath::Abs(Local.X), FMath::Abs(Local.Y));
		}
		return Scale;
	}

	/** 배율 곱하기 전 크기와 잰 길(로그용). */
	FVector2D MeasureButton(UButton& Button, FString& OutHow)
	{
		if (const UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Button.Slot))
		{
			if (!CanvasSlot->GetAutoSize() && !IsStretched(CanvasSlot->GetAnchors()))
			{
				OutHow = TEXT("캔버스 칸");
				return CanvasSlot->GetSize();
			}
		}
		UWidget* Measured = &Button;
		OutHow = TEXT("원하는 크기");
		if (USizeBox* const Box = Cast<USizeBox>(Button.GetParent()))
		{
			Measured = Box;
			OutHow = TEXT("크기 상자");
			if (const UCanvasPanelSlot* const BoxSlot = Cast<UCanvasPanelSlot>(Box->Slot))
			{
				if (!BoxSlot->GetAutoSize() && !IsStretched(BoxSlot->GetAnchors()))
				{
					OutHow = TEXT("크기 상자 캔버스 칸");
					return BoxSlot->GetSize();
				}
			}
		}
		// 판이 열린 뒤 만든 칸(배낭 칸·옷 상점 진열)은 아직 Slate 가 없을 수 있다 — 여기서 세운다.
		const TSharedRef<SWidget> Slate = Measured->TakeWidget();
		Slate->SlatePrepass(1.f);
		return Slate->GetDesiredSize();
	}

	/** 1920×1080 뿌리에서 끝까지 배치한 칸 — 렌더 배율까지 쌓인 짧은 변. */
	void ArrangeAll(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, TMap<const SWidget*, float>& OutShortSide)
	{
		const FVector2D Local = Geometry.GetLocalSize();
		const FSlateRenderTransform& Render = Geometry.GetAccumulatedRenderTransform();
		const float Width = Render.TransformVector(FVector2f(static_cast<float>(Local.X), 0.f)).Size();
		const float Height = Render.TransformVector(FVector2f(0.f, static_cast<float>(Local.Y))).Size();
		OutShortSide.Add(&Widget.Get(), FMath::Min(Width, Height));
		// 「보이되 누를 수 없음」 상자(탭 줄 등)도 지나가야 그 안 버튼까지 닿는다.
		FArrangedChildren Arranged(EVisibility::All);
		Widget->ArrangeChildren(Geometry, Arranged);
		for (int32 Index = 0; Index < Arranged.Num(); ++Index)
		{
			ArrangeAll(Arranged[Index].Widget, Arranged[Index].Geometry, OutShortSide);
		}
	}

	TMap<const SWidget*, float> ArrangePanel(const TSharedRef<SWidget>& Root)
	{
		TMap<const SWidget*, float> ShortSides;
		Root->SlatePrepass(1.f);
		ArrangeAll(Root, FGeometry::MakeRoot(FVector2D(1920.f, 1080.f), FSlateLayoutTransform()), ShortSides);
		return ShortSides;
	}

	struct FTouchSurveyCount
	{
		int32 Panels = 0;
		int32 Buttons = 0;
		int32 Hidden = 0;
		int32 ZeroSize = 0;
		int32 Small = 0;
	};

	void SurveyPanel(FAutomationTestBase& Test, const TCHAR* PanelName, UUserWidget* Panel, FTouchSurveyCount& Count, float Floor)
	{
		if (Panel == nullptr || Panel->WidgetTree == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("[TouchSize] %s: 판을 못 만듦"), PanelName));
			return;
		}
		++Count.Panels;
		TArray<UWidget*> All;
		Panel->WidgetTree->GetAllWidgets(All);
		int32 PanelButtons = 0;
		int32 PanelSmall = 0;
		// 뿌리를 재는 동안 붙잡는다 — 아무도 안 잡으면 Slate 트리가 풀려 버튼 칸을 배치에서 못 찾는다.
		const TSharedRef<SWidget> Root = Panel->TakeWidget();
		// 판이 열린 뒤 만든 칸(배낭 칸·옷 상점 진열)은 아직 Slate 가 없을 수 있다 — 배치 전에 세운다.
		for (UWidget* const Widget : All)
		{
			if (Cast<UButton>(Widget) != nullptr && Widget->GetCachedWidget() == nullptr)
			{
				Widget->TakeWidget();
			}
		}
		const TMap<const SWidget*, float> Arranged = ArrangePanel(Root);
		for (UWidget* const Widget : All)
		{
			UButton* const Button = Cast<UButton>(Widget);
			if (Button == nullptr)
			{
				continue;
			}
			++Count.Buttons;
			++PanelButtons;
			if (!IsShownInTree(*Button))
			{
				++Count.Hidden;
				continue;
			}
			FString How;
			FVector2D Raw;
			float Scale = 1.f;
			float ShortSide = 0.f;
			const TSharedPtr<SWidget> Slate = Button->GetCachedWidget();
			if (const float* const ArrangedShort = Slate.IsValid() ? Arranged.Find(Slate.Get()) : nullptr)
			{
				How = TEXT("1920×1080 배치");
				ShortSide = *ArrangedShort;
				Raw = FVector2D(ShortSide, ShortSide);
			}
			else
			{
				Raw = MeasureButton(*Button, How);
				Scale = AccumulatedRenderScale(*Button);
				ShortSide = FMath::Min(Raw.X, Raw.Y) * Scale;
			}
			if (ShortSide <= KINDA_SMALL_NUMBER)
			{
				++Count.ZeroSize;
				Test.AddError(FString::Printf(TEXT("[TouchSize] 크기 0: %s / %s (%s)"), PanelName, *Button->GetName(), *How));
				continue;
			}
			if (ShortSide < Floor)
			{
				++Count.Small;
				++PanelSmall;
				Test.AddError(FString::Printf(TEXT("[TouchSize] %.0f 미만: %s / %s — %.0f×%.0f × 배율 %.2f = 짧은 변 %.0f (%s)"),
					Floor, PanelName, *Button->GetName(), Raw.X, Raw.Y, Scale, ShortSide, *How));
			}
		}
		Test.AddInfo(FString::Printf(TEXT("[TouchSize] %s: 버튼 %d · 기준 미만 %d"), PanelName, PanelButtons, PanelSmall));
	}

	template <typename TWidget>
	TWidget* BuildPanel(UWorld* World)
	{
		TWidget* const Panel = CreateWidget<TWidget>(World, TWidget::StaticClass());
		if (Panel != nullptr)
		{
			Panel->TakeWidget();
		}
		return Panel;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTouchSizeEveryButtonTest, "Snarehaven.UI.TouchSize.EveryButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M38-05 — 조사한 판의 보이는 버튼 전부 짧은 변이 기준(HUD 72 · 메뉴 판 121) 이상. */
bool FSnarehavenTouchSizeEveryButtonTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTouchSurveyWorld();
	FTouchSurveyCount Count;
	const float HudFloor = EveryButtonFloor();
	const float Floor = FMath::Max(EveryButtonFloor(SnareUiStyle::MenuTouchMin), HudFloor);

	SurveyPanel(*this, TEXT("사냥 HUD"), BuildPanel<UHuntHudWidget>(World), Count, HudFloor);
	SurveyPanel(*this, TEXT("목장 HUD"), BuildPanel<URanchHudWidget>(World), Count, HudFloor);
	SurveyPanel(*this, TEXT("물 버튼 줄"), BuildPanel<UWaterActionWidget>(World), Count, HudFloor);
	SurveyPanel(*this, TEXT("짓기 HUD"), BuildPanel<UBuildHudWidget>(World), Count, HudFloor);

	UBackpackScreenWidget* const Backpack = BuildPanel<UBackpackScreenWidget>(World);
	Backpack->SetPanelOpen(true);
	SurveyPanel(*this, TEXT("배낭"), Backpack, Count, Floor);

	UShopScreenWidget* const Shop = BuildPanel<UShopScreenWidget>(World);
	Shop->SetOpen(true);
	SurveyPanel(*this, TEXT("상점"), Shop, Count, Floor);

	UChestScreenWidget* const Chest = BuildPanel<UChestScreenWidget>(World);
	Chest->SetOpen(true);
	SurveyPanel(*this, TEXT("보관함"), Chest, Count, Floor);

	UQuestScreenWidget* const Quest = BuildPanel<UQuestScreenWidget>(World);
	Quest->SetOpen(true);
	SurveyPanel(*this, TEXT("퀘스트"), Quest, Count, Floor);

	UAttendanceScreenWidget* const Attendance = BuildPanel<UAttendanceScreenWidget>(World);
	Attendance->SetOpen(true);
	SurveyPanel(*this, TEXT("출석"), Attendance, Count, Floor);

	UCraftingScreenWidget* const Crafting = BuildPanel<UCraftingScreenWidget>(World);
	Crafting->OpenBackpack();
	SurveyPanel(*this, TEXT("제작"), Crafting, Count, Floor);

	UWorldMapScreenWidget* const Map = BuildPanel<UWorldMapScreenWidget>(World);
	Map->SetMapOpen(true);
	SurveyPanel(*this, TEXT("지도"), Map, Count, Floor);

	UWardrobeShopWidget* const WardrobeShop = BuildPanel<UWardrobeShopWidget>(World);
	WardrobeShop->SetOpen(true);
	SurveyPanel(*this, TEXT("옷 상점"), WardrobeShop, Count, Floor);

	SurveyPanel(*this, TEXT("뽑기"), BuildPanel<UGachaScreenWidget>(World), Count, Floor);
	SurveyPanel(*this, TEXT("일시정지 메뉴"), BuildPanel<UPauseMenuWidget>(World), Count, Floor);
	SurveyPanel(*this, TEXT("도감"), BuildPanel<UCodexScreenWidget>(World), Count, Floor);
	SurveyPanel(*this, TEXT("업적"), BuildPanel<UAchievementScreenWidget>(World), Count, Floor);
	SurveyPanel(*this, TEXT("옷장"), BuildPanel<UWardrobeWidget>(World), Count, Floor);
	SurveyPanel(*this, TEXT("상자 열기"), BuildPanel<UCrateScreenWidget>(World), Count, Floor);
	SurveyPanel(*this, TEXT("안내서"), BuildPanel<USnareGuideBookWidget>(World), Count, Floor);

	AddInfo(FString::Printf(TEXT("[TouchSize] 합계(HUD %.0f · 메뉴 %.0f): 판 %d · 버튼 %d · 숨김 %d · 크기 0 %d · 기준 미만 %d"),
		HudFloor, Floor, Count.Panels, Count.Buttons, Count.Hidden, Count.ZeroSize, Count.Small));
	TestTrue(TEXT("조사한 판이 19개"), Count.Panels == 19);
	TestTrue(TEXT("잰 버튼이 있다"), Count.Buttons - Count.Hidden > 0);

	DestroyTouchSurveyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTouchSizeCraftTabsTest, "Snarehaven.UI.TouchSize.CraftTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M38-04b — 제작 판 쪽 탭이 1920×1080 배치에서 짧은 변 72 이상(전엔 글 높이 48). */
bool FSnarehavenTouchSizeCraftTabsTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTouchSurveyWorld();
	UCraftingScreenWidget* const Crafting = BuildPanel<UCraftingScreenWidget>(World);
	Crafting->OpenBackpack();
	const TSharedRef<SWidget> Root = Crafting->TakeWidget();
	const TMap<const SWidget*, float> Arranged = ArrangePanel(Root);

	TArray<UWidget*> All;
	Crafting->WidgetTree->GetAllWidgets(All);
	int32 Tabs = 0;
	float Lowest = TNumericLimits<float>::Max();
	for (UWidget* const Widget : All)
	{
		if (Cast<UButton>(Widget) == nullptr || !Widget->GetName().StartsWith(TEXT("CraftTab_")) || !IsShownInTree(*Widget))
		{
			continue;
		}
		const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
		const float* const ShortSide = Slate.IsValid() ? Arranged.Find(Slate.Get()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 배치됐다"), *Widget->GetName()), ShortSide))
		{
			continue;
		}
		++Tabs;
		Lowest = FMath::Min(Lowest, *ShortSide);
		TestTrue(*FString::Printf(TEXT("%s 짧은 변 %.0f ≥ 72"), *Widget->GetName(), *ShortSide), *ShortSide >= TouchSizeFloor);
	}
	TestTrue(TEXT("보이는 쪽 탭이 있다"), Tabs > 0);
	AddInfo(FString::Printf(TEXT("[CraftTabsTouch] 탭 %d · 가장 짧은 변 %.0f"), Tabs, Tabs > 0 ? Lowest : 0.f));

	DestroyTouchSurveyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTouchSizeWardrobeButtonsTest, "Snarehaven.UI.TouchSize.WardrobeButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M38-04c — 옷장 소유/전체 탭·상점 열기·나가기가 1920×1080 배치에서 짧은 변 72 이상(전엔 46), 판은 화면 안. */
bool FSnarehavenTouchSizeWardrobeButtonsTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 상점 열기는 젬이 꺼진 1.0 에선 접힌다 — 크기는 켠 채로 잰다
	UWorld* const World = CreateTouchSurveyWorld();
	UWardrobeWidget* const Wardrobe = BuildPanel<UWardrobeWidget>(World);
	const TSharedRef<SWidget> Root = Wardrobe->TakeWidget();
	const TMap<const SWidget*, float> Arranged = ArrangePanel(Root);

	const TCHAR* const Names[] = { TEXT("WardrobeOwnedTab"), TEXT("WardrobeAllTab"), TEXT("WardrobeOpenShop"), TEXT("WardrobeQuit") };
	float Lowest = TNumericLimits<float>::Max();
	for (const TCHAR* const Name : Names)
	{
		UWidget* const Button = Wardrobe->WidgetTree->FindWidget(FName(Name));
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 있다"), Name), Button))
		{
			continue;
		}
		const TSharedPtr<SWidget> Slate = Button->GetCachedWidget();
		const float* const ShortSide = Slate.IsValid() ? Arranged.Find(Slate.Get()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 배치됐다"), Name), ShortSide))
		{
			continue;
		}
		Lowest = FMath::Min(Lowest, *ShortSide);
		TestTrue(*FString::Printf(TEXT("%s 짧은 변 %.0f ≥ 72"), Name, *ShortSide), *ShortSide >= TouchSizeFloor);
	}

	// 판 바닥 안 — 오른쪽 판이 키가 커져도 1080 안에 든다.
	const UWidget* const Panel = Wardrobe->WidgetTree->FindWidget(FName(TEXT("WardrobePanel")));
	if (TestNotNull(TEXT("옷장 판이 있다"), Panel) && Panel->GetCachedWidget().IsValid())
	{
		const float PanelHeight = Panel->GetCachedWidget()->GetDesiredSize().Y;
		TestTrue(*FString::Printf(TEXT("판 높이 %.0f ≤ 1080"), PanelHeight), PanelHeight <= 1080.f);
		AddInfo(FString::Printf(TEXT("[WardrobeTouch] 버튼 %d · 가장 짧은 변 %.0f · 판 높이 %.0f"),
			UE_ARRAY_COUNT(Names), Lowest, PanelHeight));
	}

	DestroyTouchSurveyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTouchSizeWardrobeShopTest, "Snarehaven.UI.TouchSize.WardrobeShop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** M38-04d — 옷 상점 타일 8개(전엔 격자 칸 왼쪽 위 정렬로 33)·닫기(46)가 1920×1080 배치에서 짧은 변 72 이상, 판은 화면 안. */
bool FSnarehavenTouchSizeWardrobeShopTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTouchSurveyWorld();
	UWardrobeShopWidget* const Shop = BuildPanel<UWardrobeShopWidget>(World);
	Shop->SetOpen(true);
	const TSharedRef<SWidget> Root = Shop->TakeWidget();
	const TMap<const SWidget*, float> Arranged = ArrangePanel(Root);

	TArray<FString> Names;
	for (int32 Index = 0; Index < UWardrobeShopWidget::TileCount; ++Index)
	{
		Names.Add(FString::Printf(TEXT("WardrobeTile_%d"), Index));
	}
	Names.Add(TEXT("WardrobeCloseButton"));
	float Lowest = TNumericLimits<float>::Max();
	for (const FString& Name : Names)
	{
		UWidget* const Button = Shop->WidgetTree->FindWidget(FName(*Name));
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 있다"), *Name), Button))
		{
			continue;
		}
		const TSharedPtr<SWidget> Slate = Button->GetCachedWidget();
		const float* const ShortSide = Slate.IsValid() ? Arranged.Find(Slate.Get()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 가 배치됐다"), *Name), ShortSide))
		{
			continue;
		}
		Lowest = FMath::Min(Lowest, *ShortSide);
		TestTrue(*FString::Printf(TEXT("%s 짧은 변 %.0f ≥ 72"), *Name, *ShortSide), *ShortSide >= TouchSizeFloor);
	}

	const UWidget* const Panel = Shop->WidgetTree->FindWidget(FName(TEXT("WardrobePanel")));
	if (TestNotNull(TEXT("옷 상점 판이 있다"), Panel) && Panel->GetCachedWidget().IsValid())
	{
		const float PanelHeight = Panel->GetCachedWidget()->GetDesiredSize().Y;
		TestTrue(*FString::Printf(TEXT("판 높이 %.0f ≤ 1080"), PanelHeight), PanelHeight <= 1080.f);
		AddInfo(FString::Printf(TEXT("[WardrobeShopTouch] 버튼 %d · 가장 짧은 변 %.0f · 판 높이 %.0f"),
			Names.Num(), Lowest, PanelHeight));
	}

	DestroyTouchSurveyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenTouchSizeWardrobeTilesTest, "Snarehaven.UI.TouchSize.WardrobeTiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * INBOX 10-15 「옷장 글자 목록뿐」 — 꾸미기 원판이 1920×1080 배치에서 짧은 변 212 이상(1334×616 폰에서 120px 이상)이고
 * 원판마다 아이콘이 붙는다. 가진 것이 없으면 `전체` 탭으로 연다는 판정도 함께 본다. 판은 화면 왼쪽 절반 안.
 */
bool FSnarehavenTouchSizeWardrobeTilesTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTouchSurveyWorld();
	UWardrobeWidget* const Wardrobe = BuildPanel<UWardrobeWidget>(World);
	TestTrue(TEXT("백엔드 없는 새 계정은 가진 꾸미기가 없다 → 전체 탭으로 연다"), Wardrobe->HasNoOwnedCosmetic());
	Wardrobe->SelectOwnedTab(false);
	const TSharedRef<SWidget> Root = Wardrobe->TakeWidget();
	const TMap<const SWidget*, float> Arranged = ArrangePanel(Root);

	const int32 TileCount = Wardrobe->GetShownTiles().Num();
	TestTrue(*FString::Printf(TEXT("전체 탭 원판 %d 개 > 0"), TileCount), TileCount > 0);
	float Lowest = TNumericLimits<float>::Max();
	int32 MissingIcons = 0;
	for (int32 Index = 0; Index < TileCount; ++Index)
	{
		MissingIcons += Wardrobe->GetTileIconTexture(Index) == nullptr ? 1 : 0;
		const UButton* const Button = Wardrobe->GetTileButton(Index);
		const TSharedPtr<SWidget> Slate = Button != nullptr ? Button->GetCachedWidget() : nullptr;
		const float* const ShortSide = Slate.IsValid() ? Arranged.Find(Slate.Get()) : nullptr;
		// 밀대 아래로 가려진 줄은 배치에 안 잡힐 수 있다 — 잡힌 원판만 잰다.
		if (ShortSide != nullptr)
		{
			Lowest = FMath::Min(Lowest, *ShortSide);
		}
	}
	TestEqual(TEXT("아이콘 없는 원판 0개"), MissingIcons, 0);
	TestTrue(*FString::Printf(TEXT("원판 짧은 변 %.0f ≥ 212"), Lowest), Lowest >= 211.5f && Lowest < TNumericLimits<float>::Max());

	const UWidget* const Panel = Wardrobe->WidgetTree->FindWidget(FName(TEXT("WardrobePanel")));
	if (TestNotNull(TEXT("옷장 판이 있다"), Panel) && Panel->GetCachedWidget().IsValid())
	{
		const FVector2D PanelSize = Panel->GetCachedWidget()->GetDesiredSize();
		TestTrue(*FString::Printf(TEXT("판 폭 %.0f ≤ 960 (캐릭터 자리 남김)"), PanelSize.X), PanelSize.X <= 960.f);
		AddInfo(FString::Printf(TEXT("[WardrobeTiles] 원판 %d · 가장 짧은 변 %.0f · 아이콘 없음 %d · 판 %.0f×%.0f"),
			TileCount, Lowest, MissingIcons, PanelSize.X, PanelSize.Y));
	}

	// 감독 대조 10-16 ③ — 아래 부위 표찰(모자…배낭 · 회전)이 손가락 크기 88×44px＠1334(DPI 0.57 → 154×77) 이상.
	TArray<FString> TabNames;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		TabNames.Add(FString::Printf(TEXT("WardrobeSlot_%d"), Index));
	}
	TabNames.Add(TEXT("WardrobeRotate"));
	float NarrowestTab = TNumericLimits<float>::Max();
	float ShortestTab = TNumericLimits<float>::Max();
	for (const FString& TabName : TabNames)
	{
		const UWidget* const Tab = Wardrobe->WidgetTree->FindWidget(FName(*TabName));
		const TSharedPtr<SWidget> Slate = Tab != nullptr ? Tab->GetCachedWidget() : nullptr;
		const float* const ShortSide = Slate.IsValid() ? Arranged.Find(Slate.Get()) : nullptr;
		if (TestNotNull(*FString::Printf(TEXT("%s 가 배치됐다"), *TabName), ShortSide))
		{
			ShortestTab = FMath::Min(ShortestTab, *ShortSide);
			// 버튼은 감싼 상자 폭만큼 늘어난다 — 폭은 상자(`…Box`) 바람 크기로 본다.
			const UWidget* const TabBox = Wardrobe->WidgetTree->FindWidget(FName(TabName + TEXT("Box")));
			NarrowestTab = FMath::Min(NarrowestTab, TabBox != nullptr && TabBox->GetCachedWidget().IsValid()
				? static_cast<float>(TabBox->GetCachedWidget()->GetDesiredSize().X) : 0.f);
		}
	}
	TestTrue(*FString::Printf(TEXT("부위 표찰 짧은 변 %.0f ≥ 77"), ShortestTab), ShortestTab >= 77.f);
	TestTrue(*FString::Printf(TEXT("부위 표찰 폭 %.0f ≥ 154"), NarrowestTab), NarrowestTab >= 154.f);
	AddInfo(FString::Printf(TEXT("[WardrobeSlotTabs] 표찰 %d · 가장 짧은 변 %.0f · 가장 좁은 폭 %.0f"), TabNames.Num(), ShortestTab, NarrowestTab));

	DestroyTouchSurveyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
