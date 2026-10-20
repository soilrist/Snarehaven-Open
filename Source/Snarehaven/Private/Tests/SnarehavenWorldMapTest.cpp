// 지도 화면 2층 검사 — 월드 자리가 지도 그림의 맞는 곳에 찍히는가(위 = +X, 오른쪽 = +Y, DESIGN 8.12 🟡 지도 화면).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/SnareUiStyle.h"
#include "UI/WorldMapScreenWidget.h"
#include "Data/OutpostRow.h"
#include "Engine/DataTable.h"
#include "World/QuestGiverActor.h"
#include "World/ShopVendorActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldMapUvTest, "Snarehaven.UI.WorldMap.WorldToUv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldMapUvTest::RunTest(const FString& Parameters)
{
	const FVector2D Center(2500.0, 2500.0);
	constexpr float Width = 13000.f;
	auto Uv = [&](double X, double Y) { return UWorldMapScreenWidget::WorldToMapUv(Center, Width, FVector(X, Y, 0.0)); };
	TestTrue(TEXT("가운데는 그림 가운데"), Uv(2500.0, 2500.0).Equals(FVector2D(0.5, 0.5), 1e-4));
	TestTrue(TEXT("+X 끝은 그림 위"), Uv(2500.0 + Width * 0.5, 2500.0).Equals(FVector2D(0.5, 0.0), 1e-4));
	TestTrue(TEXT("+Y 끝은 그림 오른쪽"), Uv(2500.0, 2500.0 + Width * 0.5).Equals(FVector2D(1.0, 0.5), 1e-4));
	TestTrue(TEXT("-X·-Y 모서리는 왼쪽 아래"), Uv(2500.0 - Width * 0.5, 2500.0 - Width * 0.5).Equals(FVector2D(0.0, 1.0), 1e-4));
	TestEqual(TEXT("좌표 — 위·오른쪽"), UWorldMapScreenWidget::FormatCoordinates(Center, FVector(3500.0, 5500.0, 0.0)), FString(TEXT("4°N / 12°E")));
	TestEqual(TEXT("좌표 — 아래·왼쪽"), UWorldMapScreenWidget::FormatCoordinates(Center, FVector(0.0, 0.0, 0.0)), FString(TEXT("10°S / 10°W")));
	return true;
}

// ROADMAP M25-04 — 지도 판 제목이 판 안 글자가 아니라 판 왼쪽 위 모서리에 걸쳐 위로 나온 진갈색 알약인가
// (시트 `inventory_backpack.md` 「제목 탭」, 도감·업적 판과 같은 `MakePanelTitlePill`). 지도 그림 틀 크기는 그대로여야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldMapTitlePillTest, "Snarehaven.UI.WorldMap.TitlePillAbovePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldMapTitlePillTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("지도 화면 위젯"), Map))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Map->TakeWidget();
	UWidgetTree* const Tree = Map->WidgetTree;
	const UTextBlock* const Title = Tree->FindWidget<UTextBlock>(TEXT("WorldMapTitle"));
	const UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("WorldMapTitlePill"));
	USizeBox* const Box = Tree->FindWidget<USizeBox>(TEXT("WorldMapTitlePillBox"));
	UBorder* const Panel = Tree->FindWidget<UBorder>(TEXT("WorldMapPanel"));
	UWidget* const Frame = Tree->FindWidget(TEXT("WorldMapFrame"));
	const UCanvasPanelSlot* const BoxSlot = Box != nullptr ? Cast<UCanvasPanelSlot>(Box->Slot) : nullptr;
	const UCanvasPanelSlot* const PanelSlot = Panel != nullptr ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
	const UCanvasPanelSlot* const FrameSlot = Frame != nullptr ? Cast<UCanvasPanelSlot>(Frame->Slot) : nullptr;
	if (!TestNotNull(TEXT("제목 글자"), Title) || !TestNotNull(TEXT("제목 알약"), Pill) || !TestNotNull(TEXT("알약 상자 칸"), BoxSlot)
		|| !TestNotNull(TEXT("판 칸"), PanelSlot) || !TestNotNull(TEXT("지도 그림 틀 칸"), FrameSlot))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	const FVector2D PanelTopLeft(PanelSlot->GetOffsets().Left, PanelSlot->GetOffsets().Top);
	const FVector2D PillTopLeft = BoxSlot->GetPosition();
	const UPanelWidget* const Layer = Box->GetParent();
	TestEqual(TEXT("제목 글자"), Title->GetText().ToString(), FString(TEXT("지도")));
	TestTrue(TEXT("제목 글자 부모 = 알약 · 알약 부모 = 상자"), Title->GetParent() == Pill && Pill->GetParent() == Box);
	TestTrue(TEXT("알약 색 = UiTitleTab"), Pill->Background.TintColor.GetSpecifiedColor() == SnareUiStyle::UiTitleTab());
	TestTrue(TEXT("알약은 판과 같은 겹에서 판 뒤(위에 그림)"), Layer != nullptr && Layer == Panel->GetParent()
		&& Layer->GetChildIndex(Box) > Layer->GetChildIndex(Panel));
	TestTrue(TEXT("알약 닻 = 왼쪽 위"), BoxSlot->GetAnchors().Minimum == FVector2D::ZeroVector && BoxSlot->GetAnchors().Maximum == FVector2D::ZeroVector);
	TestEqual(TEXT("알약 왼 변 = 판 왼 변"), PillTopLeft.X, PanelTopLeft.X);
	TestEqual(TEXT("알약 위 변 = 판 위 변 - MenuTitlePillRise"), PillTopLeft.Y, PanelTopLeft.Y - SnareUiStyle::MenuTitlePillRise);
	TestTrue(TEXT("알약이 판에 걸친다(높이 > 나온 몫)"), Box->GetHeightOverride() > SnareUiStyle::MenuTitlePillRise);

	// 판 겹에 알약 밖 제목 글자가 남지 않는다 — 겹에 바로 붙은 글자 중 `지도` 는 0.
	int32 LooseTitles = 0;
	for (int32 Index = 0; Index < Layer->GetChildrenCount(); ++Index)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(Layer->GetChildAt(Index));
		LooseTitles += Text != nullptr && Text->GetText().ToString() == TEXT("지도") ? 1 : 0;
	}
	TestEqual(TEXT("판 안 제목 글자 0"), LooseTitles, 0);
	TestEqual(TEXT("지도 그림 틀 크기 = MapSize"), FrameSlot->GetSize(), FVector2D(UWorldMapScreenWidget::MapSize));

	AddInfo(FString::Printf(TEXT("[WorldMapTitle] 알약 (%.0f, %.0f) · 판 (%.0f, %.0f) · 알약 높이 %.0f · 판 안 제목 %d · 그림 틀 %.0f"),
		PillTopLeft.X, PillTopLeft.Y, PanelTopLeft.X, PanelTopLeft.Y, Box->GetHeightOverride(), LooseTitles, FrameSlot->GetSize().X));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ROADMAP M38-04 — 지도 닫기는 손가락 한 마디(72) 이상(DESIGN 8.12 「지도 화면」 닫기 150×72)이고,
// 1920×1080·4:3(1440×1080) 두 화면에서 지도 그림 틀(MapSize 950, 가운데)과 겹치지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldMapCloseTouchSizeTest, "Snarehaven.UI.TouchSize.WorldMapClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldMapCloseTouchSizeTest::RunTest(const FString& Parameters)
{
	constexpr float DesignMinTouch = 72.f;
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	if (TestNotNull(TEXT("지도 화면 위젯"), Map))
	{
		Map->TakeWidget();
		const UWidget* const Close = Map->WidgetTree->FindWidget(TEXT("WorldMapCloseButton"));
		const UWidget* const Frame = Map->WidgetTree->FindWidget(TEXT("WorldMapFrame"));
		const UCanvasPanelSlot* const CloseSlot = Close != nullptr ? Cast<UCanvasPanelSlot>(Close->Slot) : nullptr;
		const UCanvasPanelSlot* const FrameSlot = Frame != nullptr ? Cast<UCanvasPanelSlot>(Frame->Slot) : nullptr;
		if (TestNotNull(TEXT("닫기 칸"), CloseSlot) && TestNotNull(TEXT("지도 그림 틀 칸"), FrameSlot))
		{
			// 캔버스 칸 사각형 = 닻 자리 + 위치 − 크기 × 정렬 (닻은 점 닻만 쓴다).
			auto RectIn = [](const UCanvasPanelSlot* CanvasSlot, const FVector2D& Screen)
			{
				const FVector2D Size = CanvasSlot->GetSize();
				const FVector2D Min = CanvasSlot->GetAnchors().Minimum * Screen + CanvasSlot->GetPosition() - Size * CanvasSlot->GetAlignment();
				return FBox2D(Min, Min + Size);
			};
			const FVector2D CloseSize = CloseSlot->GetSize();
			TestTrue(TEXT("닫기 짧은 변 ≥ 72"), FMath::Min(CloseSize.X, CloseSize.Y) >= DesignMinTouch);
			TestTrue(TEXT("닫기 높이 = 전체 화면 판 닫기 공용 값"), FMath::IsNearlyEqual(CloseSize.Y, SnareUiStyle::ScreenCloseButtonHeight));
			for (const FVector2D& Screen : { FVector2D(1920.0, 1080.0), FVector2D(1440.0, 1080.0) })
			{
				const FBox2D CloseRect = RectIn(CloseSlot, Screen);
				const FBox2D FrameRect = RectIn(FrameSlot, Screen);
				const bool bOverlap = CloseRect.Min.X < FrameRect.Max.X && FrameRect.Min.X < CloseRect.Max.X
					&& CloseRect.Min.Y < FrameRect.Max.Y && FrameRect.Min.Y < CloseRect.Max.Y;
				const bool bInside = CloseRect.Min.X >= 0.0 && CloseRect.Min.Y >= 0.0 && CloseRect.Max.X <= Screen.X && CloseRect.Max.Y <= Screen.Y;
				TestFalse(*FString::Printf(TEXT("%.0f×%.0f: 닫기가 지도 그림과 안 겹친다"), Screen.X, Screen.Y), bOverlap);
				TestTrue(*FString::Printf(TEXT("%.0f×%.0f: 닫기가 화면 안"), Screen.X, Screen.Y), bInside);
				AddInfo(FString::Printf(TEXT("[WorldMapCloseTouch] %.0f×%.0f 닫기 (%.0f,%.0f)~(%.0f,%.0f) · 그림 (%.0f,%.0f)~(%.0f,%.0f) · 겹침 %d"),
					Screen.X, Screen.Y, CloseRect.Min.X, CloseRect.Min.Y, CloseRect.Max.X, CloseRect.Max.Y,
					FrameRect.Min.X, FrameRect.Min.Y, FrameRect.Max.X, FrameRect.Max.Y, bOverlap ? 1 : 0));
			}
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ROADMAP M26-03 — 연 지도 판은 HUD·출석 버튼보다 위 Z(도감·업적과 같은 전체 화면 판 Z), 닫힌 지도 버튼은 HUD Z 라
// 배낭·상자 판(0) 위로 뚫고 나오지 않는다. 뷰포트가 없는 시험 월드라 붙이기는 못 하고, `AttachTo`·`SetMapOpen` 이
// 붙일 때 쓰는 Z 값을 연·닫힌 두 상태로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldMapPanelAboveHudTest, "Snarehaven.UI.WorldMap.PanelAboveHud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldMapPanelAboveHudTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("지도 화면 위젯"), Map))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Map->TakeWidget();
	const int32 ClosedZ = Map->GetViewportZOrder();
	Map->SetMapOpen(true);
	const int32 OpenZ = Map->GetViewportZOrder();
	const bool bOpenLayerShown = Map->WidgetTree->FindWidget(TEXT("WorldMapLayer")) != nullptr
		&& Map->WidgetTree->FindWidget(TEXT("WorldMapLayer"))->GetVisibility() != ESlateVisibility::Collapsed;
	Map->SetMapOpen(false);
	const int32 ReclosedZ = Map->GetViewportZOrder();
	// INBOX 보정 10-06 02:45 — 편 이모트 줄은 같은 HUD Z 로 나중에 붙는 상인 「상점 열기」 카드 위, 접으면 HUD Z.
	Map->SetEmotesOpen(true);
	const int32 EmotesZ = Map->GetViewportZOrder();
	Map->SetEmotesOpen(false);
	const int32 EmotesClosedZ = Map->GetViewportZOrder();

	TestEqual(TEXT("닫힌 지도 = HUD Z"), ClosedZ, SnareUiStyle::HudViewportZOrder);
	TestEqual(TEXT("연 지도 = 전체 화면 판 Z(도감·업적과 같은 값)"), OpenZ, SnareUiStyle::ScreenPanelViewportZOrder);
	TestTrue(TEXT("전체 화면 판 Z ≥ 10"), SnareUiStyle::ScreenPanelViewportZOrder >= 10);
	TestTrue(TEXT("연 지도 Z > HUD Z"), OpenZ > SnareUiStyle::HudViewportZOrder);
	TestTrue(TEXT("연 지도 판이 보인다"), bOpenLayerShown);
	TestEqual(TEXT("다시 닫으면 HUD Z"), ReclosedZ, SnareUiStyle::HudViewportZOrder);
	TestTrue(TEXT("편 이모트 줄 Z > HUD Z(상인 「상점 열기」 카드)"), EmotesZ > SnareUiStyle::HudViewportZOrder);
	TestTrue(TEXT("편 이모트 줄 Z < 전체 화면 판 Z(배낭·상자 판 아래)"), EmotesZ < SnareUiStyle::ScreenPanelViewportZOrder);
	TestEqual(TEXT("이모트 줄 접으면 HUD Z"), EmotesClosedZ, SnareUiStyle::HudViewportZOrder);

	AddInfo(FString::Printf(TEXT("[WorldMapZ] 닫힘 %d · 열림 %d · 다시 닫힘 %d · 이모트 폄 %d · 이모트 접음 %d · 판 Z %d · HUD Z %d"),
		ClosedZ, OpenZ, ReclosedZ, EmotesZ, EmotesClosedZ, SnareUiStyle::ScreenPanelViewportZOrder, SnareUiStyle::HudViewportZOrder));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// INBOX 18:10 ④ — 지도 판에서 잡화점을 찾을 수 있는가. 시험 월드에 잡화·낚시 상인과 의뢰인을 세우면
// 표식은 상인 둘만(의뢰인 뺌), 글자는 「잡화점」·「낚시」, 자리는 `WorldToMapUv` × 그림 틀 MapSize(950) 이다.
// 바다 액터가 없는 시험 월드라 지도 가운데·폭은 위젯 기본값((0,0) · 10000cm)이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldMapVendorMarkersTest, "Snarehaven.UI.WorldMap.VendorMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldMapVendorMarkersTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 시험 전용 자리 — 밸런스 값이 아니다.
	const FVector GeneralSpot(1000.f, -2000.f, 88.f);
	const FVector FishingSpot(-3000.f, 1500.f, 88.f);
	AShopVendorActor* const General = World->SpawnActor<AShopVendorActor>(GeneralSpot, FRotator::ZeroRotator);
	AShopVendorActor* const Fishing = World->SpawnActor<AShopVendorActor>(FishingSpot, FRotator::ZeroRotator);
	AQuestGiverActor* const Giver = World->SpawnActor<AQuestGiverActor>(FVector(0.f, 0.f, 88.f), FRotator::ZeroRotator);
	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("잡화 상인"), General) || !TestNotNull(TEXT("낚시 상인"), Fishing)
		|| !TestNotNull(TEXT("의뢰인"), Giver) || !TestNotNull(TEXT("지도 화면 위젯"), Map))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	General->SetShopKind(EShopKind::General);
	Fishing->SetShopKind(EShopKind::Fishing);
	Map->TakeWidget();
	Map->SetMapOpen(true);
	const int32 Count = Map->RefreshVendorMarkers();
	const int32 CountAgain = Map->RefreshVendorMarkers();

	UWidgetTree* const Tree = Map->WidgetTree;
	const UCanvasPanel* const Layer = Tree->FindWidget<UCanvasPanel>(TEXT("WorldMapVendorLayer"));
	const UWidget* const Dot = Tree->FindWidget(TEXT("WorldMapPlayerDot"));
	TestEqual(TEXT("표식 = 상인 둘(의뢰인 뺌)"), Count, 2);
	TestEqual(TEXT("다시 세워도 둘(겹쳐 쌓이지 않음)"), CountAgain, 2);
	TestTrue(TEXT("표식 겹 자식 = 2"), Layer != nullptr && Layer->GetChildrenCount() == 2);
	TestTrue(TEXT("내 점이 표식 겹보다 위"), Layer != nullptr && Dot != nullptr && Layer->GetParent() == Dot->GetParent()
		&& Dot->GetParent()->GetChildIndex(Dot) > Dot->GetParent()->GetChildIndex(Layer));

	TMap<FString, FVector2D> Found;
	for (int32 Index = 0; Layer != nullptr && Index < Layer->GetChildrenCount(); ++Index)
	{
		const UBorder* const Pill = Cast<UBorder>(Layer->GetChildAt(Index));
		const UTextBlock* const Label = Pill != nullptr ? Cast<UTextBlock>(Pill->GetContent()) : nullptr;
		const UCanvasPanelSlot* const PillSlot = Pill != nullptr ? Cast<UCanvasPanelSlot>(Pill->Slot) : nullptr;
		if (Label != nullptr && PillSlot != nullptr)
		{
			Found.Add(Label->GetText().ToString(), PillSlot->GetPosition());
			AddInfo(FString::Printf(TEXT("[MapVendor] 「%s」 → (%.1f, %.1f)"), *Label->GetText().ToString(),
				PillSlot->GetPosition().X, PillSlot->GetPosition().Y));
		}
	}
	// 알약 아래 끝 = 상인 자리보다 `VendorPillLift` 위(내 점 반지름 13 + 2) — 가게 앞 내 점이 글자를 안 덮는다.
	auto Expected = [](const FVector& Spot)
	{
		return UWorldMapScreenWidget::WorldToMapUv(FVector2D::ZeroVector, 10000.f, Spot) * UWorldMapScreenWidget::MapSize
			- FVector2D(0.f, UWorldMapScreenWidget::VendorPillLift);
	};
	const FVector2D* const GeneralPos = Found.Find(TEXT("잡화점"));
	const FVector2D* const FishingPos = Found.Find(TEXT("낚시"));
	TestTrue(TEXT("「잡화점」 표식"), GeneralPos != nullptr);
	TestTrue(TEXT("「낚시」 표식"), FishingPos != nullptr);
	TestTrue(TEXT("잡화점 자리 = WorldToMapUv × MapSize"), GeneralPos != nullptr && GeneralPos->Equals(Expected(GeneralSpot), 0.5));
	TestTrue(TEXT("낚시 자리 = WorldToMapUv × MapSize"), FishingPos != nullptr && FishingPos->Equals(Expected(FishingSpot), 0.5));
	bool bAllBottomAnchored = Layer != nullptr && Layer->GetChildrenCount() > 0;
	for (int32 Index = 0; Layer != nullptr && Index < Layer->GetChildrenCount(); ++Index)
	{
		const UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Layer->GetChildAt(Index)->Slot);
		bAllBottomAnchored &= PillSlot != nullptr && PillSlot->GetAlignment().Equals(FVector2D(0.5, 1.0));
	}
	TestTrue(TEXT("알약 기준 = 아래 가운데(상인 자리 위에 선다)"), bAllBottomAnchored);
	TestTrue(TEXT("들어 올림 ≥ 내 점 반지름 13"), UWorldMapScreenWidget::VendorPillLift >= 13.f);
	TestEqual(TEXT("농사 글자"), UWorldMapScreenWidget::GetVendorMapLabel(EShopKind::Farming).ToString(), FString(TEXT("농사")));
	TestEqual(TEXT("옷 글자"), UWorldMapScreenWidget::GetVendorMapLabel(EShopKind::Clothes).ToString(), FString(TEXT("옷")));
	AddInfo(FString::Printf(TEXT("[MapVendor] 표식 %d · 다시 %d · 의뢰인 뺌"), Count, CountAgain));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// 마을 이름 ② — 큰 지도에 거점 14곳 이름 글자가 선다: 개수 = 표 행 수, 자리 = WorldToMapUv × MapSize 가로 같음 · 세로 = +16 이상(글자끼리 겹치면 아래로 비킴),
// 다시 세워도 쌓이지 않고, 헤이븐 마을 글자가 있다. 바다 액터 없는 시험 월드라 가운데·폭은 위젯 기본값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldMapOutpostLabelsTest, "Snarehaven.UI.WorldMap.OutpostLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldMapOutpostLabelsTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/World/DT_OutpostTable.DT_OutpostTable"));
	if (!TestNotNull(TEXT("지도 화면 위젯"), Map) || !TestNotNull(TEXT("DT_OutpostTable"), Table))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Map->TakeWidget();
	Map->SetMapOpen(true);
	const int32 Count = Map->RefreshOutpostLabels();
	const int32 CountAgain = Map->RefreshOutpostLabels();
	const UCanvasPanel* const Layer = Map->WidgetTree->FindWidget<UCanvasPanel>(TEXT("WorldMapOutpostLayer"));
	TestTrue(TEXT("표 행이 있다"), Table->GetRowMap().Num() > 0);
	TestEqual(TEXT("글자 수 = 표 행 수"), Count, Table->GetRowMap().Num());
	TestEqual(TEXT("다시 세워도 같다(쌓이지 않음)"), CountAgain, Count);
	TestTrue(TEXT("이름 겹 자식 = 글자 수"), Layer != nullptr && Layer->GetChildrenCount() == Count);
	int32 Matched = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FOutpostRow* const Row = reinterpret_cast<const FOutpostRow*>(Pair.Value);
		const UTextBlock* const Label = Map->WidgetTree->FindWidget<UTextBlock>(
			*FString::Printf(TEXT("WorldMapOutpostLabel_%s"), *Pair.Key.ToString()));
		const UCanvasPanelSlot* const LabelSlot = Label != nullptr ? Cast<UCanvasPanelSlot>(Label->Slot) : nullptr;
		const FVector2D Want = UWorldMapScreenWidget::WorldToMapUv(FVector2D::ZeroVector, 10000.f, FVector(Row->XCm, Row->YCm, 0.f)) * UWorldMapScreenWidget::MapSize;
		// 지도 밖 자리는 가장자리에 붙으니 자리 대조는 지도 안 거점만.
		const FVector2D Half = UWorldMapScreenWidget::EstimateTextSize(Row->DisplayName.ToString().Len(), 30.f, 4) * 0.5f;
		const bool bInside = Want.X >= 0.f && Want.X <= UWorldMapScreenWidget::MapSize && Want.Y >= 0.f && Want.Y <= UWorldMapScreenWidget::MapSize;
		if (Label != nullptr && LabelSlot != nullptr && Label->GetText().EqualTo(Row->DisplayName)
			&& (!bInside || FBox2D(LabelSlot->GetPosition() - Half, LabelSlot->GetPosition() + Half).ComputeSquaredDistanceToPoint(Want) <= 160.0 * 160.0))
		{
			++Matched;
		}
	}
	TestEqual(TEXT("글자 = 이름·자리가 표와 같음"), Matched, Table->GetRowMap().Num());
	const UTextBlock* const Haven = Map->WidgetTree->FindWidget<UTextBlock>(TEXT("WorldMapOutpostLabel_haven_outpost"));
	TestTrue(TEXT("「헤이븐 마을」 글자"), Haven != nullptr && Haven->GetText().ToString() == TEXT("헤이븐 마을"));
	// 보정: 이름 상자끼리 겹침 0, 알약은 이름 상자를 피한다.
	const TArray<FBox2D>& Rects = Map->GetOutpostLabelRects();
	int32 Overlaps = 0;
	for (int32 A = 0; A < Rects.Num(); ++A)
	{
		for (int32 B = A + 1; B < Rects.Num(); ++B)
		{
			Overlaps += Rects[A].ExpandBy(UWorldMapScreenWidget::OutpostLabelGap).Intersect(Rects[B]) ? 1 : 0;
		}
	}
	TestEqual(TEXT("이름 상자 수 = 글자 수"), Rects.Num(), Count);
	TestEqual(TEXT("이름 상자끼리 겹침 0·간격 8px 이상"), Overlaps, 0);
	if (Rects.Num() > 0)
	{
		const FBox2D PillOnName(Rects[0].Min + FVector2D(2.f, 2.f), Rects[0].Min + FVector2D(60.f, 30.f));
		const float Up = UWorldMapScreenWidget::DodgeRects(PillOnName, Rects);
		FBox2D PillMoved = PillOnName;
		PillMoved.Min.Y -= Up;
		PillMoved.Max.Y -= Up;
		TestTrue(TEXT("이름 위에 얹힌 알약은 위로 비킨다"), Up > 0.f);
		TestTrue(TEXT("비킨 알약은 어떤 이름과도 안 겹침"), !Rects.ContainsByPredicate([&PillMoved](const FBox2D& R) { return R.Intersect(PillMoved); }));
	}
	// 보정(#1078): 알약끼리도 겹침 0 — 같은 자리 알약은 먼저 선 알약 위로 비킨다.
	{
		const TArray<FBox2D> First = { FBox2D(FVector2D(100.f, 100.f), FVector2D(160.f, 128.f)) };
		const FBox2D Second(FVector2D(110.f, 104.f), FVector2D(170.f, 132.f));
		const float Up = UWorldMapScreenWidget::DodgeRects(Second, First);
		FBox2D Moved = Second;
		Moved.Min.Y -= Up;
		Moved.Max.Y -= Up;
		TestTrue(TEXT("알약끼리 겹치면 위로 비킨다"), Up > 0.f && !First[0].Intersect(Moved));
	}
	const TArray<FBox2D>& Pills = Map->GetVendorPillRects();
	int32 PillOverlaps = 0;
	for (int32 A = 0; A < Pills.Num(); ++A)
	{
		for (int32 B = A + 1; B < Pills.Num(); ++B)
		{
			PillOverlaps += Pills[A].ExpandBy(UWorldMapScreenWidget::VendorPillGap).Intersect(Pills[B]) ? 1 : 0;
		}
		for (const FBox2D& Name : Rects)
		{
			PillOverlaps += Pills[A].ExpandBy(UWorldMapScreenWidget::VendorPillGap).Intersect(Name) ? 1 : 0;
		}
	}
	TestEqual(TEXT("상인 알약끼리·이름 상자와 겹침 0·간격 4px 이상"), PillOverlaps, 0);
	TestTrue(TEXT("글자 아래 내려앉음 ≥ 내 점 반지름 13"), UWorldMapScreenWidget::OutpostLabelDrop >= 13.f);
	AddInfo(FString::Printf(TEXT("[MapOutpost] 글자 %d · 다시 %d · 맞음 %d"), Count, CountAgain, Matched));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
