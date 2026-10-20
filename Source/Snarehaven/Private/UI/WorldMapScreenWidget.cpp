#include "UI/WorldMapScreenWidget.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SizeBox.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Data/OutpostRow.h"
#include "Engine/DataTable.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "World/ZonePortalActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Player/SnareEmoteBubble.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareUiStyle.h"
#include "World/QuestGiverActor.h"
#include "World/SeaActor.h"
#include "World/SnareHookNpcActor.h"
#include "World/ShopVendorActor.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 화면 배치 (1080 높이 기준, DESIGN 8.12 🟡). 게임 규칙 숫자가 아니다.
	// 지도·메뉴·이모트 버튼은 오른쪽 아래 기둥(`SnareUiStyle::PlacePillarButton`) — 지도 (1,0) · 메뉴 (0,1) · 이모트 (1,1).
	constexpr float ButtonRadius = 12.f;
	const TCHAR* const ButtonIconPath = TEXT("/Game/Game/UI/Icons/Items/T_Icon_TreasureMap.T_Icon_TreasureMap");
	const TCHAR* const MenuIconPath = TEXT("/Game/Game/UI/Icons/T_UI_Gear.T_UI_Gear");
	// 이모트 버튼을 누르면 네 알약이 기둥 왼쪽으로 한 줄 펼쳐진다 (DESIGN 2.2 「이모트」).
	const TCHAR* const EmoteIconPath = TEXT("/Game/Game/UI/Icons/T_UI_Smile.T_UI_Smile");
	constexpr float EmoteButtonBottom = SnareUiStyle::PillarBottom + SnareUiStyle::PillarButtonSize + SnareUiStyle::PillarGap;
	constexpr float EmotePillWidth = 116.f;
	constexpr float EmotePillHeight = 96.f;
	constexpr float EmotePillGap = 10.f;
	constexpr float EmotePillRight = SnareUiStyle::PillarClearRight;
	// 감정표현 말풍선 알약 여섯은 몸 동작 줄 바로 위 한 줄 (DESIGN 2.2 「감정표현 말풍선」 🟡).
	constexpr float BubblePillWidth = 96.f;
	constexpr float BubbleRowBottom = EmoteButtonBottom + EmotePillHeight + EmotePillGap;
	constexpr float DotSize = 26.f;
	constexpr float CloseWidth = 150.f;
	constexpr float CloseHeight = SnareUiStyle::ScreenCloseButtonHeight;   // M38-04 — 64 → 72, 다른 전체 화면 판 닫기와 같은 값
	constexpr float ClosePad = SnareUiStyle::ScreenPanelInset + 20.f;
	constexpr int32 TextureSize = 1024;
	constexpr float SeaMargin = 1.3f;             // 섬 폭의 1.3배를 찍는다 — 둘레 바다가 조금 보이게
	constexpr float CaptureHeightCm = 30000.f;
	constexpr float NoSeaWidthCm = 10000.f;
	// ponytail: 열려 있는 동안 3초마다 다시 찍는다(동물·시간 변화). 폰에서 끊기면 열 때 한 번만으로 줄인다.
	constexpr float RecaptureSeconds = 3.f;
	// 오른쪽 위 좌표 알약 — 레퍼런스 "19°N / 19°W" (steam_02·08·13·16). 누르면 지도가 열린다.
	constexpr float CoordWidth = 190.f;
	constexpr float CoordHeight = 48.f;
	constexpr float CoordMargin = 24.f;
	constexpr float CmPerDegree = 250.f;
	// 상인 표식 알약 (INBOX 18:10 ④) — 글자 30(폰 1334×616 에서 17px, 18 은 10px 라 안 읽힘), 둥근 반지름 20.
	constexpr float VendorPillFont = 30.f;
	constexpr float VendorPillRadius = 20.f;
	constexpr float VendorPillPadX = 14.f;
	constexpr float VendorPillPadY = 3.f;
	// 마을 이름 글자 (INBOX 마을 이름 ②) — 흰 글자 + 진갈색 테, 폰에서 17px 이상(30).
	constexpr float OutpostLabelFont = 30.f;
	constexpr int32 OutpostLabelOutline = 4;
	// 섬 밖 마을 표지 카드 — 지도 틀 좌우 바깥에 한 줄씩, 진갈색 카드 + 밧줄 테 + 바깥 쪽 뾰족 끝(그 방향 바다 건너).
	constexpr float CardGap = 34.f;          // 틀과 카드 사이 — 뾰족 끝(대각선 ≈ 25)이 들어갈 틈
	constexpr float CardPadX = 16.f;
	constexpr float CardPadY = 8.f;
	constexpr float CardRowGap = 12.f;
	constexpr float CardRadius = 12.f;
	constexpr float CardTip = 22.f;          // 45° 돌린 정사각 한 변
	// 부두 덮개 둘레 여백(cm) — 위에서 찍은 콘크리트 가장자리까지 덮는다.
		// 부두·잔교 메시 이름 머리 (`tools/unreal/setup_harbor.py` B 경로) — 위에서 찍으면 흰 콘크리트 띠로만 보인다.
	const TCHAR* const HarborMeshPrefix = TEXT("SM_Building_");
// 항구 배(`SM_Vehicle_Ship_01` 등)·부두 소품 폴더 — 지도 캡처에서 숨긴다.
const TCHAR* const HarborVehicleFolder = TEXT("/Environment/Vehicles/Harbor/");
const TCHAR* const HarborPropFolder = TEXT("/Environment/Props/Harbor/");
// 항구 알약 가운데가 틀 가장자리에서 들어오는 거리 — 알약(「항구」 글자 30 + 여백) 반 폭보다 조금 크게.
constexpr float HarborPillInset = 50.f;
// 지도 머리표 — 공용 판 제목(글자 20·높이 50)보다 크게.
constexpr float TitlePillFont = 30.f;
constexpr float TitlePillHeight = 64.f;
constexpr float TitlePillMinWidth = 160.f;
	const TCHAR* const OutpostTablePath = TEXT("/Game/Game/Data/World/DT_OutpostTable.DT_OutpostTable");

	UCanvasPanelSlot* Place(UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment,
		const FVector2D& Position, const FVector2D& Size)
	{
		UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
		if (CanvasSlot != nullptr)
		{
			CanvasSlot->SetAnchors(Anchors);
			CanvasSlot->SetAlignment(Alignment);
			CanvasSlot->SetPosition(Position);
			CanvasSlot->SetSize(Size);
		}
		return CanvasSlot;
	}
}

namespace
{
TWeakObjectPtr<UWorldMapScreenWidget> LocalMapScreen;   // `AttachTo` 가 붙인 내 화면 지도 — 미니맵 누름이 이 길로 연다
}

bool UWorldMapScreenWidget::OpenLocalMap()
{
	if (!LocalMapScreen.IsValid())
	{
		return false;
	}
	LocalMapScreen->SetMapOpen(true);
	return true;
}

UWorldMapScreenWidget* UWorldMapScreenWidget::AttachTo(APawn* Pawn)
{
	APlayerController* const Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller) || !Controller->IsLocalController())
	{
		return nullptr;
	}
	UWorldMapScreenWidget* const Screen = CreateWidget<UWorldMapScreenWidget>(Controller, StaticClass());
	if (!IsValid(Screen))
	{
		return nullptr;
	}
	LocalMapScreen = Screen;
	// 3층 검사용 — 지도를 연 채로 띄운다 (`-SnareBackpackScreen` 과 같은 검사 전용 스위치).
	Screen->SetMapOpen(SnareCheckSwitch::Param(TEXT("SnareMapScreen")));
	Screen->AddedViewportZOrder = Screen->GetViewportZOrder();
	Screen->AddToViewport(Screen->AddedViewportZOrder);
	// `-SnareEmotes` 면 이모트 줄을 편 채, `-SnareEmote=<번호>` 면 그 이모트를 사람이 누른 것과 같은 길로 튼다.
	Screen->SetEmotesOpen(SnareCheckSwitch::Param(TEXT("SnareEmotes")));
	int32 EmoteIndex = INDEX_NONE;
	if (SnareCheckSwitch::Value(TEXT("SnareEmote="), EmoteIndex))
	{
		const bool bKeepOpen = Screen->AreEmotesOpen();
		Screen->PlayEmote(EmoteIndex);
		Screen->SetEmotesOpen(bKeepOpen);
	}
	// `-SnareEmoteBubble=<번호>` 면 그 말풍선을 사람이 누른 것과 같은 길로 띄운다 (M20-07 3층 캡처용).
	int32 BubbleIndex = INDEX_NONE;
	if (SnareCheckSwitch::Value(TEXT("SnareEmoteBubble="), BubbleIndex))
	{
		const bool bKeepOpen = Screen->AreEmotesOpen();
		Screen->PlayBubble(BubbleIndex);
		Screen->SetEmotesOpen(bKeepOpen);
	}
	return Screen;
}

FString UWorldMapScreenWidget::FormatCoordinates(const FVector2D& CenterXY, const FVector& World)
{
	const int32 North = FMath::RoundToInt((World.X - CenterXY.X) / CmPerDegree);
	const int32 East = FMath::RoundToInt((World.Y - CenterXY.Y) / CmPerDegree);
	return FString::Printf(TEXT("%d°%s / %d°%s"), FMath::Abs(North), North < 0 ? TEXT("S") : TEXT("N"),
		FMath::Abs(East), East < 0 ? TEXT("W") : TEXT("E"));
}

FVector2D UWorldMapScreenWidget::WorldToMapUv(const FVector2D& CenterXY, float WidthCm, const FVector& World)
{
	return FVector2D((World.Y - CenterXY.Y) / WidthCm + 0.5, (CenterXY.X - World.X) / WidthCm + 0.5);
}

const TArray<UWorldMapScreenWidget::FEmoteEntry>& UWorldMapScreenWidget::GetEmotes()
{
	static const TArray<FEmoteEntry> Emotes = {
		{ EItemAction::Wave, LOCTEXT("EmoteWave", "인사") },
		{ EItemAction::Cheer, LOCTEXT("EmoteCheer", "만세") },
		{ EItemAction::Dance, LOCTEXT("EmoteDance", "춤") },
		{ EItemAction::Sit, LOCTEXT("EmoteSit", "앉기") },
	};
	return Emotes;
}

void UWorldMapScreenWidget::SetEmotesOpen(bool bOpen)
{
	bEmotesOpen = bOpen;
	for (UButton* const Button : EmoteButtons)
	{
		Button->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	ApplyViewportZOrder();
}

void UWorldMapScreenWidget::ToggleEmotes()
{
	SetEmotesOpen(!bEmotesOpen);
}

void UWorldMapScreenWidget::PlayEmote0() { PlayEmote(0); }
void UWorldMapScreenWidget::PlayEmote1() { PlayEmote(1); }
void UWorldMapScreenWidget::PlayEmote2() { PlayEmote(2); }
void UWorldMapScreenWidget::PlayEmote3() { PlayEmote(3); }

void UWorldMapScreenWidget::PlayEmote(int32 Index)
{
	SetEmotesOpen(false);
	ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(GetOwningPlayerPawn());
	if (IsValid(Character) && GetEmotes().IsValidIndex(Index))
	{
		Character->PlayItemAction(GetEmotes()[Index].Action);
	}
}

void UWorldMapScreenWidget::PlayBubble0() { PlayBubble(0); }
void UWorldMapScreenWidget::PlayBubble1() { PlayBubble(1); }
void UWorldMapScreenWidget::PlayBubble2() { PlayBubble(2); }
void UWorldMapScreenWidget::PlayBubble3() { PlayBubble(3); }
void UWorldMapScreenWidget::PlayBubble4() { PlayBubble(4); }
void UWorldMapScreenWidget::PlayBubble5() { PlayBubble(5); }

void UWorldMapScreenWidget::PlayBubble(int32 Index)
{
	SetEmotesOpen(false);
	if (ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(GetOwningPlayerPawn()))
	{
		Character->ShowEmoteBubble(Index);
	}
}

void UWorldMapScreenWidget::SetMapOpen(bool bOpen)
{
	bMapOpen = bOpen;
	if (IsValid(MapLayer))
	{
		MapLayer->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	NextCaptureSeconds = 0.f;   // 열자마자 찍는다
	ApplyViewportZOrder();
}

void UWorldMapScreenWidget::ApplyViewportZOrder()
{
	// 연 판은 HUD·출석 버튼 위로, 닫힌 버튼은 HUD 겹으로 (M26-03). Z 는 붙일 때만 정해져 떼었다 다시 붙인다 —
	// 늘 판 Z 면 닫힌 지도·메뉴·이모트 버튼이 배낭·상자 판 위로 뚫고 나온다. 버튼 자리·누름은 같은 위젯이라 그대로.
	// 트리를 만드는 중(`RebuildWidget`)엔 떼지 않는다 — 붙이기는 `AttachTo` 가 정한 Z 로 한다.
	const int32 Wanted = GetViewportZOrder();
	if (Wanted != AddedViewportZOrder && IsInViewport() && !bBuildingTree)
	{
		RemoveFromParent();
		AddedViewportZOrder = Wanted;
		AddToViewport(Wanted);
	}
}

int32 UWorldMapScreenWidget::GetViewportZOrder() const
{
	if (bMapOpen)
	{
		return SnareUiStyle::ScreenPanelViewportZOrder;
	}
	// 편 이모트 줄은 상인 「상점 열기」 카드(HUD Z, 나중에 붙음) 위로 — 몸 동작 줄이 카드에 덮였다 (INBOX 보정 10-06 02:45).
	return bEmotesOpen ? SnareUiStyle::HudPopupViewportZOrder : SnareUiStyle::HudViewportZOrder;
}

void UWorldMapScreenWidget::ToggleMap()
{
	SetMapOpen(!bMapOpen);
}

void UWorldMapScreenWidget::CloseMap()
{
	SetMapOpen(false);
}

void UWorldMapScreenWidget::OpenPauseMenu()
{
	SetMapOpen(false);
	if (ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(GetOwningPlayerPawn()))
	{
		Character->TogglePauseMenu();
	}
}

void UWorldMapScreenWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapRoot"));
	WidgetTree->RootWidget = Root;
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 지도·메뉴 버튼 — 배낭 버튼과 같은 크림 네모 + 아이콘.
	auto AddIconButton = [this, Root](const TCHAR* Name, const TCHAR* IconPath, int32 Column, int32 Row, const FText& Label) {
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		SnareUiStyle::MakeRoundButton(*Button, ButtonRadius);
		Button->SetBackgroundColor(SnareUiStyle::UiPanelCream());
		Button->SetToolTipText(Label);
		SnareUiStyle::PlacePillarButton(*WidgetTree, *Root, *Button, LoadObject<UTexture2D>(nullptr, IconPath),
			Label, FName(*(FString(Name) + TEXT("Label"))), Column, Row);
		return Button;
	};
	AddIconButton(TEXT("WorldMapOpenButton"), ButtonIconPath, 1, 0, LOCTEXT("WorldMapOpen", "지도"))
		->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::ToggleMap);
	AddIconButton(TEXT("PauseMenuOpenButton"), MenuIconPath, 0, 1, LOCTEXT("PauseMenuOpen", "메뉴"))
		->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::OpenPauseMenu);
	AddIconButton(TEXT("EmoteOpenButton"), EmoteIconPath, 1, 1, LOCTEXT("EmoteOpen", "이모트"))
		->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::ToggleEmotes);

	// 이모트 알약 넷 — 이모트 버튼 왼쪽으로 한 줄. 처음엔 접혀 있다.
	EmoteButtons.Reset();
	for (int32 Index = 0; Index < GetEmotes().Num() && Index < 4; ++Index)
	{
		UButton* const Pill = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			*FString::Printf(TEXT("EmoteButton%d"), Index));
		SnareUiStyle::MakeRoundButton(*Pill, EmotePillHeight * 0.5f);
		Pill->SetBackgroundColor(SnareUiStyle::UiPanelCream());
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(GetEmotes()[Index].Label);
		Label->SetJustification(ETextJustify::Center);
		SnareUiStyle::SetFontSize(*Label, 26.f);
		Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		Pill->SetContent(Label);
		Root->AddChild(Pill);
		const float Right = EmotePillRight + Index * (EmotePillWidth + EmotePillGap);
		Place(Pill, FAnchors(1.f, 1.f), FVector2D(1.f, 1.f), FVector2D(-Right, -EmoteButtonBottom), FVector2D(EmotePillWidth, EmotePillHeight));
		switch (Index)
		{
		case 0: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayEmote0); break;
		case 1: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayEmote1); break;
		case 2: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayEmote2); break;
		default: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayEmote3); break;
		}
		EmoteButtons.Add(Pill);
	}
	// 말풍선 알약 여섯 — 몸 동작 줄 위 한 줄, 같은 이모트 버튼으로 같이 펼치고 접는다.
	for (int32 Index = 0; Index < SnareEmoteBubble::Num() && Index < 6; ++Index)
	{
		UButton* const Pill = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			*FString::Printf(TEXT("EmoteBubbleButton%d"), Index));
		SnareUiStyle::MakeRoundButton(*Pill, EmotePillHeight * 0.5f);
		Pill->SetBackgroundColor(SnareUiStyle::UiPanelCream());
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(SnareEmoteBubble::GetLabel(Index));
		Label->SetJustification(ETextJustify::Center);
		SnareUiStyle::SetFontSize(*Label, 28.f);
		Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		Pill->SetContent(Label);
		Root->AddChild(Pill);
		const float Right = EmotePillRight + Index * (BubblePillWidth + EmotePillGap);
		Place(Pill, FAnchors(1.f, 1.f), FVector2D(1.f, 1.f), FVector2D(-Right, -BubbleRowBottom), FVector2D(BubblePillWidth, EmotePillHeight));
		switch (Index)
		{
		case 0: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayBubble0); break;
		case 1: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayBubble1); break;
		case 2: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayBubble2); break;
		case 3: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayBubble3); break;
		case 4: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayBubble4); break;
		default: Pill->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::PlayBubble5); break;
		}
		EmoteButtons.Add(Pill);
	}
	SetEmotesOpen(bEmotesOpen);

	UButton* const Coord = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("WorldMapCoordButton"));
	SnareUiStyle::MakeRoundButton(*Coord, CoordHeight * 0.5f);
	Coord->SetBackgroundColor(SnareUiStyle::UiPanelCream());
	CoordText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WorldMapCoordText"));
	CoordText->SetJustification(ETextJustify::Center);
	SnareUiStyle::SetFontSize(*CoordText, 22.f);
	CoordText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	Coord->SetContent(CoordText);
	Coord->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::ToggleMap);
	Root->AddChild(Coord);
	Place(Coord, FAnchors(1.f, 0.f), FVector2D(1.f, 0.f), FVector2D(-CoordMargin, CoordMargin), FVector2D(CoordWidth, CoordHeight));
	// 오른쪽 위는 비운다 (M15-02, 시트 `hud_ingame.md` · DESIGN 3.3 「화면 비우기」) — 영상 화면엔 좌표 표시가 없고,
	// 좌표를 늘 띄우면 위성 통신기의 「좌표 보임」 효과(미니맵 줄)가 뜻을 잃는다. 지도는 오른쪽 아래 지도 버튼으로 연다.
	// 잔액 알약과 같은 처리 — 글자는 계속 채우고 알약만 접는다(검사·다른 화면이 같은 한 줄을 읽는다).
	Coord->SetVisibility(ESlateVisibility::Collapsed);

	// 지도 판 — 어둡게 + 크림 판(눌림을 먹어 뒤 HUD 가 안 눌린다) 위에 그림·점·닫기.
	MapLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapLayer"));
	Root->AddChild(MapLayer);
	if (UCanvasPanelSlot* const LayerSlot = Cast<UCanvasPanelSlot>(MapLayer->Slot))
	{
		LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		LayerSlot->SetOffsets(FMargin(0.f));
	}
	SnareUiStyle::AddScreenPanel(*WidgetTree, *MapLayer, TEXT("WorldMapBackdrop"), TEXT("WorldMapPanel"));

	UCanvasPanel* const Frame = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapFrame"));
	MapLayer->AddChild(Frame);
	Place(Frame, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, FVector2D(MapSize));
	MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("WorldMapImage"));
	Frame->AddChild(MapImage);
	if (UCanvasPanelSlot* const ImageSlot = Cast<UCanvasPanelSlot>(MapImage->Slot))
	{
		ImageSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		ImageSlot->SetOffsets(FMargin(0.f));
	}
	// 항구 알약 — 크림 판 위 글은 진갈색(크림 판 규칙) — 공용 크림 카드(`MakeCreamCard`, 처음엔 접힘)에 알약 여백만. 자리는 `RefreshHarborCover`.
	HarborCover = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("WorldMapHarborPill"));
	HarborCover->SetPadding(FMargin(VendorPillPadX, VendorPillPadY));
	UTextBlock* const HarborText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WorldMapHarborText"));
	HarborText->SetText(LOCTEXT("WorldMapHarbor", "항구"));
	SnareUiStyle::SetFontSize(*HarborText, VendorPillFont);
	HarborText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	HarborCover->SetContent(HarborText);
	Frame->AddChild(HarborCover);
	if (UCanvasPanelSlot* const HarborSlot = Cast<UCanvasPanelSlot>(HarborCover->Slot))
	{
		HarborSlot->SetAnchors(FAnchors(0.f, 0.f));
		HarborSlot->SetAlignment(FVector2D(0.5f, 1.f));   // 상인 알약처럼 자리 바로 위에 선다 — 잔교 그림을 덮지 않게
		HarborSlot->SetAutoSize(true);
	}
	// 상인 표식 겹 — 그림 위, 내 점 아래(내 점이 표식에 안 가려지게 먼저 붙인다).
	VendorLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapVendorLayer"));
	Frame->AddChild(VendorLayer);
	if (UCanvasPanelSlot* const VendorSlot = Cast<UCanvasPanelSlot>(VendorLayer->Slot))
	{
		VendorSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		VendorSlot->SetOffsets(FMargin(0.f));
	}
	VendorLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 섬 밖 마을 표지 카드 바탕 겹 — 이름 글자 겹 아래(글자가 카드 위에 선다).
	OutpostCardLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapOutpostCardLayer"));
	Frame->AddChild(OutpostCardLayer);
	if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(OutpostCardLayer->Slot))
	{
		CardSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CardSlot->SetOffsets(FMargin(0.f));
	}
	OutpostCardLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 마을 이름 겹 — 그림 위, 상인 표식 위(이름이 알약에 안 가려지게).
	OutpostLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapOutpostLayer"));
	Frame->AddChild(OutpostLayer);
	if (UCanvasPanelSlot* const OutpostSlot = Cast<UCanvasPanelSlot>(OutpostLayer->Slot))
	{
		OutpostSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		OutpostSlot->SetOffsets(FMargin(0.f));
	}
	OutpostLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	StoryDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WorldMapStoryDot"));
	StoryDot->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.1f, 0.75f, 0.85f), DotSize * 0.5f, FLinearColor::White, 4.f));
	StoryDot->SetVisibility(ESlateVisibility::Collapsed);
	Frame->AddChild(StoryDot);
	Place(StoryDot, FAnchors(0.f, 0.f), FVector2D(0.5f, 0.5f), FVector2D(MapSize * 0.5f), FVector2D(DotSize));
	PlayerDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WorldMapPlayerDot"));
	PlayerDot->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.9f, 0.15f, 0.1f), DotSize * 0.5f, FLinearColor::White, 4.f));
	Frame->AddChild(PlayerDot);
	Place(PlayerDot, FAnchors(0.f, 0.f), FVector2D(0.5f, 0.5f), FVector2D(MapSize * 0.5f), FVector2D(DotSize));

	// 제목 탭(M25-04) — 판 안 글자 대신 도감·업적 판과 같은 진갈색 알약을 판 왼쪽 위 모서리에 걸친다.
	UTextBlock* const Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WorldMapTitle"));
	Title->SetText(LOCTEXT("WorldMapTitle", "지도"));
	USizeBox* const TitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("WorldMapTitlePillBox"), TEXT("WorldMapTitlePill"), *Title);
	// 지도 판은 가장 큰 판이라 머리표도 키운다 — 글자 30(폰 약 17px) · 높이 64.
	SnareUiStyle::SetFontSize(*Title, TitlePillFont);
	TitlePillBox->SetHeightOverride(TitlePillHeight);
	TitlePillBox->SetMinDesiredWidth(TitlePillMinWidth);
	MapLayer->AddChild(TitlePillBox);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(TitlePillBox->Slot))
	{
		PillSlot->SetAnchors(FAnchors(0.f, 0.f));
		PillSlot->SetPosition(FVector2D(SnareUiStyle::ScreenPanelInset, SnareUiStyle::ScreenPanelInset - SnareUiStyle::MenuTitlePillRise));
		PillSlot->SetAutoSize(true);
	}

	UButton* const Close = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("WorldMapCloseButton"));
	SnareUiStyle::MakeRoundButton(*Close, CloseHeight * 0.5f);
	UTextBlock* const CloseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WorldMapCloseText"));
	CloseText->SetText(LOCTEXT("WorldMapClose", "닫기"));
	CloseText->SetJustification(ETextJustify::Center);
	SnareUiStyle::SetFontSize(*CloseText, 26.f);
	Close->SetContent(CloseText);
	Close->OnClicked.AddDynamic(this, &UWorldMapScreenWidget::CloseMap);
	MapLayer->AddChild(Close);
	Place(Close, FAnchors(1.f, 0.f), FVector2D(1.f, 0.f), FVector2D(-ClosePad, ClosePad), FVector2D(CloseWidth, CloseHeight));
}

FText UWorldMapScreenWidget::GetVendorMapLabel(EShopKind Kind)
{
	switch (Kind)
	{
	case EShopKind::Fishing:
		return LOCTEXT("MapVendorFishing", "낚시");
	case EShopKind::Farming:
		return LOCTEXT("MapVendorFarming", "농사");
	case EShopKind::Clothes:
		return LOCTEXT("MapVendorClothes", "옷");
	case EShopKind::SweetPotato:
		return LOCTEXT("MapVendorSweetPotato", "고구마");
	default:
		return LOCTEXT("MapVendorGeneral", "잡화점");
	}
}

int32 UWorldMapScreenWidget::RefreshVendorMarkers()
{
	UWorld* const World = GetWorld();
	if (!IsValid(VendorLayer) || World == nullptr)
	{
		return 0;
	}
	VendorLayer->ClearChildren();
	int32 Count = 0;
	// 피할 자리 = 마을 이름 글자 + 먼저 선 알약(알약끼리도 안 겹침).
	TArray<FBox2D> Avoid = OutpostLabelRects;
	VendorPillRects.Reset();
	for (TActorIterator<AShopVendorActor> It(World); It; ++It)
	{
		const AShopVendorActor* const Vendor = *It;
		// 의뢰인·갈고리는 같은 부모를 쓰지만 가게가 아니다 — 지도엔 물건 파는 곳만.
		if (!IsValid(Vendor) || Vendor->IsA<AQuestGiverActor>() || Vendor->IsA<ASnareHookNpcActor>())
		{
			continue;
		}
		const bool bGeneral = Vendor->GetShopKind() == EShopKind::General;
		UBorder* const Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			*FString::Printf(TEXT("WorldMapVendorMarker%d"), Count));
		// 잡화점(손도끼·곡괭이 파는 곳)은 기력 노랑으로 눈에 띄게, 나머지 가게는 남색.
		Pill->SetBrush(FSlateRoundedBoxBrush(bGeneral ? SnareUiStyle::UiBoltYellow() : SnareUiStyle::UiNavy(), VendorPillRadius,
			SnareUiStyle::UiTextOnButton(), 2.f));
		Pill->SetPadding(FMargin(VendorPillPadX, VendorPillPadY));
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			*FString::Printf(TEXT("WorldMapVendorLabel%d"), Count));
		Label->SetText(GetVendorMapLabel(Vendor->GetShopKind()));
		SnareUiStyle::SetFontSize(*Label, VendorPillFont);
		Label->SetColorAndOpacity(FSlateColor(bGeneral ? SnareUiStyle::UiTextOnButton() : FLinearColor::White));
		Pill->SetContent(Label);
		VendorLayer->AddChild(Pill);
		const FVector2D Uv = WorldToMapUv(MapCenterXY, MapWidthCm, Vendor->GetActorLocation());
		if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Pill->Slot))
		{
			PillSlot->SetAnchors(FAnchors(0.f, 0.f));
			// 알약은 상인 자리 바로 위에 선다(아래 가운데 기준) — 가게 앞에 선 내 점이 글자를 덮지 않게.
			PillSlot->SetAlignment(FVector2D(0.5f, 1.f));
			PillSlot->SetAutoSize(true);
			FVector2D PillPos = FVector2D(FMath::Clamp(Uv.X, 0.0, 1.0), FMath::Clamp(Uv.Y, 0.0, 1.0)) * MapSize
				- FVector2D(0.f, VendorPillLift);
			// 이름 글자와 겹치면 알약이 위로 비킨다(이름이 우선).
			const FVector2D PillSize = EstimateTextSize(GetVendorMapLabel(Vendor->GetShopKind()).ToString().Len(), VendorPillFont, 0)
				+ FVector2D(2.f * VendorPillPadX, 2.f * VendorPillPadY + 4.f);
			PillPos.Y -= DodgeRects(FBox2D(PillPos - FVector2D(PillSize.X * 0.5f, PillSize.Y), PillPos + FVector2D(PillSize.X * 0.5f, 0.f)), Avoid, VendorPillGap);
			const FBox2D PillRect(PillPos - FVector2D(PillSize.X * 0.5f, PillSize.Y), PillPos + FVector2D(PillSize.X * 0.5f, 0.f));
			Avoid.Add(PillRect);
			VendorPillRects.Add(PillRect);
			PillSlot->SetPosition(PillPos);
		}
		++Count;
	}
	return Count;
}

FVector2D UWorldMapScreenWidget::EstimateTextSize(int32 CharCount, float FontSize, int32 Outline)
{
	return FVector2D(CharCount * FontSize * 1.2f + 2.f * Outline, FontSize * 1.3f + 2.f * Outline);
}

float UWorldMapScreenWidget::DodgeRects(const FBox2D& Pill, const TArray<FBox2D>& Avoid, float Gap)
{
	FBox2D Moved = Pill;
	float Total = 0.f;
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		const FBox2D* Hit = Avoid.FindByPredicate([&Moved, Gap](const FBox2D& Other) { return Other.Intersect(Moved.ExpandBy(Gap)); });
		if (Hit == nullptr)
		{
			break;
		}
		const float Up = Moved.Max.Y + Gap - Hit->Min.Y + 2.f;
		Moved.Min.Y -= Up;
		Moved.Max.Y -= Up;
		Total += Up;
	}
	return Total;
}

int32 UWorldMapScreenWidget::RefreshOutpostLabels()
{
	if (!IsValid(OutpostLayer))
	{
		return 0;
	}
	OutpostLayer->ClearChildren();
	if (IsValid(OutpostCardLayer))
	{
		OutpostCardLayer->ClearChildren();
	}
	OutpostLabelRects.Reset();
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, OutpostTablePath);
	if (Table == nullptr)
	{
		return 0;
	}
	// 섬 밖 거점은 틀 가장자리에 붙이면 잘리고 겹친다 — 좌우 바깥 표지 카드 열로 모은다(쪽 = 거점이 지도 가운데의 왼쪽/오른쪽).
	struct FSideCard
	{
		UTextBlock* Label;
		FVector2D Half;
		float WantY;
	};
	TArray<FSideCard> Sides[2];
	int32 Count = 0;
	TArray<FName> RowNames = Table->GetRowNames();
	RowNames.Sort(FNameLexicalLess());
	for (const FName& RowName : RowNames)
	{
		const FOutpostRow* const Row = Table->FindRow<FOutpostRow>(RowName, TEXT("RefreshOutpostLabels"));
		if (Row == nullptr)
		{
			continue;
		}
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			*FString::Printf(TEXT("WorldMapOutpostLabel_%s"), *RowName.ToString()));
		Label->SetText(Row->DisplayName);
		SnareUiStyle::SetFontSize(*Label, OutpostLabelFont);
		OutpostLayer->AddChild(Label);
		++Count;
		const FVector2D Uv = WorldToMapUv(MapCenterXY, MapWidthCm, FVector(Row->XCm, Row->YCm, 0.f));
		UCanvasPanelSlot* const LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot);
		if (LabelSlot == nullptr)
		{
			continue;
		}
		LabelSlot->SetAnchors(FAnchors(0.f, 0.f));
		LabelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		LabelSlot->SetAutoSize(true);
		const bool bInside = Uv.X >= 0.0 && Uv.X <= 1.0 && Uv.Y >= 0.0 && Uv.Y <= 1.0;
		if (!bInside)
		{
			// 카드 위 글자 — 테 없이 크림(진갈색 카드 대비).
			Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
			Sides[Uv.X < 0.5 ? 0 : 1].Add({ Label, EstimateTextSize(Row->DisplayName.ToString().Len(), OutpostLabelFont, 0) * 0.5f,
				static_cast<float>(FMath::Clamp(Uv.Y, 0.0, 1.0)) * MapSize });
			continue;
		}
		FSlateFontInfo Font = Label->GetFont();
		Font.OutlineSettings.OutlineSize = OutpostLabelOutline;
		Font.OutlineSettings.OutlineColor = SnareUiStyle::UiTextOnButton();
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		// 글자 상자 가운데 기준. 제 점 아래 → 위 → 오른쪽 → 왼쪽 중 먼저 빈 자리(먼저 선 글자와 안 겹침).
		const FVector2D Dot = Uv * MapSize;
		const FVector2D Half = EstimateTextSize(Row->DisplayName.ToString().Len(), OutpostLabelFont, OutpostLabelOutline) * 0.5f;
		// 여덟 자리 × 여섯 겹(반 줄씩 멀어짐) 중 안 겹치는 것 가운데 제 점에 가장 가까운 자리.
		FVector2D Center = Dot + FVector2D(0.f, OutpostLabelDrop + Half.Y);
		bool bFound = false;
		float BestDist = TNumericLimits<float>::Max();
		for (int32 Ring = 0; Ring < 6; ++Ring)
		{
			const float Far = Ring * Half.Y;
			const float Dx = OutpostLabelDrop + Half.X + Far;
			const float Dy = OutpostLabelDrop + Half.Y + Far;
			const FVector2D Candidates[8] = {
				Dot + FVector2D(0.f, Dy), Dot - FVector2D(0.f, Dy),
				Dot + FVector2D(Dx, 0.f), Dot - FVector2D(Dx, 0.f),
				Dot + FVector2D(Dx * 0.6f, Dy), Dot + FVector2D(-Dx * 0.6f, Dy),
				Dot + FVector2D(Dx * 0.6f, -Dy), Dot + FVector2D(-Dx * 0.6f, -Dy) };
			for (const FVector2D& Candidate : Candidates)
			{
				const FBox2D Rect = FBox2D(Candidate - Half, Candidate + Half).ExpandBy(OutpostLabelGap);
				const float Dist = FVector2D::Distance(Candidate, Dot);
				if (Dist < BestDist && !OutpostLabelRects.ContainsByPredicate([&Rect](const FBox2D& Other) { return Other.Intersect(Rect); }))
				{
					Center = Candidate;
					BestDist = Dist;
					bFound = true;
				}
			}
		}
		UE_CLOG(!bFound, LogSnarehaven, Warning, TEXT("마을 이름 %s — 빈 자리 못 찾음(겹침)"), *RowName.ToString());
		OutpostLabelRects.Add(FBox2D(Center - Half, Center + Half));
		LabelSlot->SetPosition(Center);
	}

	// 표지 카드 열 — 한 쪽 카드는 폭을 맞추고 세로는 제 거점 높이 순, 겹치면 아래로 밀고 넘치면 위로 당긴다.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		TArray<FSideCard>& Cards = Sides[Side];
		if (Cards.Num() == 0)
		{
			continue;
		}
		Cards.StableSort([](const FSideCard& A, const FSideCard& B) { return A.WantY < B.WantY; });
		float HalfWidth = 0.f;
		for (const FSideCard& Card : Cards)
		{
			HalfWidth = FMath::Max(HalfWidth, Card.Half.X + CardPadX);
		}
		const float HalfHeight = Cards[0].Half.Y + CardPadY;
		const float Step = 2.f * HalfHeight + CardRowGap;
		TArray<float> Ys;
		for (const FSideCard& Card : Cards)
		{
			Ys.Add(FMath::Max(Card.WantY, Ys.Num() > 0 ? Ys.Last() + Step : HalfHeight));
		}
		for (int32 Index = Ys.Num() - 1; Index >= 0; --Index)
		{
			Ys[Index] = FMath::Min(Ys[Index], Index + 1 < Ys.Num() ? Ys[Index + 1] - Step : MapSize - HalfHeight);
		}
		// ponytail: 한 쪽 카드가 (950 / 68) ≈ 13장을 넘으면 위로 넘친다 — 거점이 그만큼 늘면 두 줄로.
		const float Sign = Side == 0 ? -1.f : 1.f;
		const float CenterX = Side == 0 ? -CardGap - HalfWidth : MapSize + CardGap + HalfWidth;
		for (int32 Index = 0; Index < Cards.Num(); ++Index)
		{
			const FVector2D Center(CenterX, Ys[Index]);
			if (IsValid(OutpostCardLayer))
			{
				// 뾰족 끝 먼저(카드 밑에 깔려 바깥 반만 보인다).
				UBorder* const Tip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
					*FString::Printf(TEXT("WorldMapOutpostCardTip%d_%d"), Side, Index));
				Tip->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiTitleTab(), 3.f, SnareUiStyle::UiRope(), 3.f));
				Tip->SetRenderTransformAngle(45.f);
				OutpostCardLayer->AddChild(Tip);
				Place(Tip, FAnchors(0.f, 0.f), FVector2D(0.5f, 0.5f), Center + FVector2D(Sign * HalfWidth, 0.f), FVector2D(CardTip));
				UBorder* const Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
					*FString::Printf(TEXT("WorldMapOutpostCard%d_%d"), Side, Index));
				Card->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiTitleTab(), CardRadius, SnareUiStyle::UiRope(), 3.f));
				OutpostCardLayer->AddChild(Card);
				Place(Card, FAnchors(0.f, 0.f), FVector2D(0.5f, 0.5f), Center, FVector2D(2.f * HalfWidth, 2.f * HalfHeight));
			}
			if (UCanvasPanelSlot* const LabelSlot = Cast<UCanvasPanelSlot>(Cards[Index].Label->Slot))
			{
				LabelSlot->SetPosition(Center);
			}
			OutpostLabelRects.Add(FBox2D(Center - Cards[Index].Half, Center + Cards[Index].Half));
		}
	}
	UE_LOG(LogSnarehaven, Verbose, TEXT("마을 이름 글자 %d개(카드 왼 %d · 오른 %d) — 지도 가운데 (%.0f, %.0f) · 폭 %.0fcm"), Count,
		Sides[0].Num(), Sides[1].Num(), MapCenterXY.X, MapCenterXY.Y, MapWidthCm);
	return Count;
}

void UWorldMapScreenWidget::FindHarbor()
{
	UWorld* const World = GetWorld();
	if (bHarborSearched || World == nullptr)
	{
		return;
	}
	bHarborSearched = true;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const UStaticMeshComponent* const Mesh = It->GetStaticMeshComponent();
		if (Mesh == nullptr || Mesh->GetStaticMesh() == nullptr)
		{
			continue;
		}
		const FString MeshName = Mesh->GetStaticMesh()->GetName();
		const FString MeshPath = Mesh->GetStaticMesh()->GetPathName();
		const bool bQuay = MeshName.StartsWith(HarborMeshPrefix) && (MeshName.Contains(TEXT("Quay")) || MeshName.Contains(TEXT("Pier")));
		if (bQuay)
		{
			HarborBounds += It->GetComponentsBoundingBox();
		}
		if (bQuay || MeshPath.Contains(HarborVehicleFolder) || MeshPath.Contains(HarborPropFolder))
		{
			HarborActors.Add(*It);
		}
	}
	// 배 타는 잔교(존 포털, 지도 그림의 나무 잔교)가 있으면 알약은 그 자리 — 부두 메시 가운데는 잔교보다 14m 북쪽이라
	// 알약이 잔교 그림보다 위에 떠 있었다(10-16 감독 캡처). 잔교가 없는 레벨만 부두 메시 가운데를 쓴다.
	for (TActorIterator<AZonePortalActor> It(World); It; ++It)
	{
		if (It->CanTravel())
		{
			HarborBounds = FBox(It->GetActorLocation(), It->GetActorLocation());
			break;
		}
	}
}

void UWorldMapScreenWidget::RefreshHarborCover()
{
	if (!IsValid(HarborCover))
	{
		return;
	}
	FindHarbor();
	if (!HarborBounds.IsValid)
	{
		HarborCover->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// 잔교(또는 부두 가운데) — 틀 안으로 알약 반쯤 들여 잘리지 않게.
	const FVector2D Uv = WorldToMapUv(MapCenterXY, MapWidthCm, HarborBounds.GetCenter());
	const FVector2D Inset(HarborPillInset);
	if (UCanvasPanelSlot* const HarborSlot = Cast<UCanvasPanelSlot>(HarborCover->Slot))
	{
		HarborSlot->SetPosition(FVector2D::Clamp(Uv * MapSize, Inset, FVector2D(MapSize) - Inset));
	}
	HarborCover->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UWorldMapScreenWidget::CaptureMap()
{
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const APawn* const Pawn = GetOwningPlayerPawn();
	const ASeaActor* const MapSea = ASeaActor::FindSea(World);
	const FBox2D Drawn = MapSea != nullptr ? MapSea->GetMapBounds() : FBox2D(ForceInit);
	if (Drawn.bIsValid)
	{
		// 새 지도 — 그려 둔 지도 그림을 그대로 쓴다(손그림 한 장). 캡처하지 않는다.
		MapCenterXY = Drawn.GetCenter();
		MapWidthCm = Drawn.GetSize().X;
		MapImage->SetBrushResourceObject(MapSea->MapTexture);
		return;
	}
	if (const ASeaActor* const FoundSea = MapSea)
	{
		MapCenterXY = FVector2D(FoundSea->GetActorLocation());
		MapWidthCm = 2.f * FoundSea->IslandHalfExtentCm * SeaMargin;
	}
	else
	{
		MapCenterXY = IsValid(Pawn) ? FVector2D(Pawn->GetActorLocation()) : FVector2D::ZeroVector;
		MapWidthCm = NoSeaWidthCm;
	}
	if (!IsValid(RenderTarget))
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->RenderTargetFormat = RTF_RGBA8;
		RenderTarget->InitAutoFormat(TextureSize, TextureSize);
		RenderTarget->UpdateResourceImmediate(true);
		MapImage->SetBrushResourceObject(RenderTarget);
	}
	if (!Capture.IsValid())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Capture = World->SpawnActor<ASceneCapture2D>(Params);
		if (!Capture.IsValid())
		{
			return;
		}
		USceneCaptureComponent2D* const Component = Capture->GetCaptureComponent2D();
		Component->ProjectionType = ECameraProjectionMode::Orthographic;
		Component->CaptureSource = SCS_FinalColorLDR;
		Component->bCaptureEveryFrame = false;
		Component->bCaptureOnMovement = false;
		Component->TextureTarget = RenderTarget;
	}
	USceneCaptureComponent2D* const Component = Capture->GetCaptureComponent2D();
	// 부두·배는 위에서 흰 띠·흰 덩이로만 보여 지도에서 뺀다 — 그 자리엔 알약 「항구」.
	FindHarbor();
	Component->HiddenActors.Reset();
	for (const TWeakObjectPtr<AActor>& Harbor : HarborActors)
	{
		if (Harbor.IsValid())
		{
			Component->HiddenActors.Add(Harbor.Get());
		}
	}
	Component->OrthoWidth = MapWidthCm;
	Capture->SetActorLocationAndRotation(FVector(MapCenterXY, CaptureHeightCm), FRotator(-90.f, 0.f, 0.f));
	Component->CaptureScene();
	UE_LOG(LogSnarehaven, Verbose, TEXT("지도 찍음 — 가운데 (%.0f, %.0f) · 폭 %.0fcm"), MapCenterXY.X, MapCenterXY.Y, MapWidthCm);
}

TSharedRef<SWidget> UWorldMapScreenWidget::RebuildWidget()
{
	TGuardValue<bool> BuildingGuard(bBuildingTree, true);
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	SetMapOpen(bMapOpen);
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

void UWorldMapScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const APawn* const Pawn = GetOwningPlayerPawn();
	if (IsValid(Pawn) && IsValid(CoordText))
	{
		if (!Sea.IsValid())
		{
			Sea = ASeaActor::FindSea(this);
		}
		const FString Coordinates = FormatCoordinates(Sea.IsValid() ? FVector2D(Sea->GetActorLocation()) : FVector2D::ZeroVector,
			Pawn->GetActorLocation());
		if (!CoordText->GetText().ToString().Equals(Coordinates))
		{
			CoordText->SetText(FText::FromString(Coordinates));
		}
	}
	if (!bMapOpen || !IsValid(MapImage))
	{
		return;
	}
	NextCaptureSeconds -= InDeltaTime;
	if (NextCaptureSeconds <= 0.f)
	{
		NextCaptureSeconds = RecaptureSeconds;
		CaptureMap();
		RefreshHarborCover();
		RefreshOutpostLabels();
		RefreshVendorMarkers();
	}
	if (IsValid(StoryDot))
	{
		const AShopVendorActor* const StoryNpc = AShopVendorActor::FindStoryNpc(GetWorld(), SnareStory::GetCurrentTargetNpc());
		StoryDot->SetVisibility(StoryNpc != nullptr ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (StoryNpc != nullptr)
		{
			const FVector2D StoryUv = WorldToMapUv(MapCenterXY, MapWidthCm, StoryNpc->GetActorLocation());
			if (UCanvasPanelSlot* const StorySlot = Cast<UCanvasPanelSlot>(StoryDot->Slot))
			{
				StorySlot->SetPosition(FVector2D(FMath::Clamp(StoryUv.X, 0.0, 1.0), FMath::Clamp(StoryUv.Y, 0.0, 1.0)) * MapSize);
			}
		}
	}
	if (IsValid(Pawn))
	{
		const FVector2D Uv = WorldToMapUv(MapCenterXY, MapWidthCm, Pawn->GetActorLocation());
		if (UCanvasPanelSlot* const DotSlot = Cast<UCanvasPanelSlot>(PlayerDot->Slot))
		{
			DotSlot->SetPosition(FVector2D(FMath::Clamp(Uv.X, 0.0, 1.0), FMath::Clamp(Uv.Y, 0.0, 1.0)) * MapSize);
		}
	}
}

void UWorldMapScreenWidget::NativeDestruct()
{
	if (Capture.IsValid())
	{
		Capture->Destroy();
	}
	Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
