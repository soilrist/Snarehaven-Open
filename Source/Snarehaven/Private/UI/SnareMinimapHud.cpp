#include "UI/SnareMinimapHud.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/ItemRow.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "UI/SnareUiStyle.h"
#include "World/DangerZoneActor.h"
#include "World/SeaActor.h"
#include "World/SnareZoneRules.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace SnareMinimapHud
{
namespace
{
	// 레퍼런스 시트 `hud_ingame.md` — 패널 모서리 12~16px 둥글게, 테두리·그림자 없음. 배낭 패널(16)과 맞춘다.
	constexpr float FrameRadius = 16.f;
	constexpr float DotOutline = 3.f;

	// 이벤트 표식 색 — 팔레트(2.1) 밖 **예외**(DESIGN 8.1b): 네 칸 색 어디에도 없는 빨강이라야 "여기 뭔가 있다" 가 읽힌다.
	FLinearColor EventDotColor()
	{
		return FLinearColor(FColor::FromHex(TEXT("D2452F")));
	}

	template <typename WidgetT>
	void SetShown(WidgetT* Widget, bool bShown)
	{
		const ESlateVisibility Want = bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (Widget != nullptr && Widget->GetVisibility() != Want)
		{
			Widget->SetVisibility(Want);
		}
	}

	float GetScale(const FBox2D& Bounds, float InMapSize)
	{
		const FVector2D Size = Bounds.GetSize();
		const double LongSide = FMath::Max(Size.X, Size.Y);
		return Bounds.bIsValid && LongSide > 0.0 ? static_cast<float>(InMapSize / LongSide) : 0.f;
	}
}

TArray<const FZoneRow*> GetRegionRows(const UDataTable* Table, FName LevelName)
{
	TArray<const FZoneRow*> Rows;
	if (Table != nullptr)
	{
		Table->ForeachRow<FZoneRow>(TEXT("SnareMinimapHud::GetRegionRows"),
			[&Rows, LevelName](const FName&, const FZoneRow& Row)
			{
				if (Row.LevelName == LevelName && SnareZones::IsRegionRow(Row))
				{
					Rows.Add(&Row);
				}
			});
	}
	return Rows;
}

FBox2D GetMapBounds(const TArray<const FZoneRow*>& Rows)
{
	FBox2D Bounds(ForceInit);
	for (const FZoneRow* Row : Rows)
	{
		const FVector2D Half(Row->RegionHalfExtentCm, Row->RegionHalfExtentCm);
		Bounds += Row->RegionCenterCm - Half;
		Bounds += Row->RegionCenterCm + Half;
	}
	return Bounds;
}

FVector2D GetMapExtent(const FBox2D& Bounds, float InMapSize)
{
	const float Scale = GetScale(Bounds, InMapSize);
	const FVector2D Size = Bounds.GetSize();
	// 화면 가로 = 월드 Y, 화면 세로 = 월드 X.
	return Bounds.bIsValid ? FVector2D(Size.Y * Scale, Size.X * Scale) : FVector2D::ZeroVector;
}

FVector2D WorldToMap(const FBox2D& Bounds, const FVector& Location, float InMapSize)
{
	if (!Bounds.bIsValid)
	{
		return FVector2D::ZeroVector;
	}
	const float Scale = GetScale(Bounds, InMapSize);
	const double X = FMath::Clamp(Location.X, Bounds.Min.X, Bounds.Max.X);
	const double Y = FMath::Clamp(Location.Y, Bounds.Min.Y, Bounds.Max.Y);
	// 위가 +X 라 세로는 **Max 에서** 잰다. 가로는 +Y 가 오른쪽이라 Min 에서 잰다.
	return FVector2D((Y - Bounds.Min.Y) * Scale, (Bounds.Max.X - X) * Scale);
}

FText FormatZoneName(const FZoneRow* Row)
{
	return Row != nullptr ? Row->DisplayName : LOCTEXT("MinimapZoneUnknown", "-");
}

FText FormatEventRemaining(float Seconds)
{
	const int32 Whole = FMath::Max(FMath::CeilToInt(Seconds), 0);
	return FText::Format(LOCTEXT("MinimapEventRemaining", "추락 지점 {0}"),
		FText::FromString(FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60)));
}

FText FormatTreasureRemaining(double Seconds)
{
	const int32 Whole = FMath::Max(FMath::CeilToInt(Seconds), 0);
	return FText::Format(LOCTEXT("MinimapTreasureRemaining", "보물 {0}"),
		FText::FromString(FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60)));
}

FText FormatCoordinates(const FVector& Location)
{
	return FText::Format(LOCTEXT("MinimapCoordinates", "북 {0} · 동 {1}"),
		FText::AsNumber(FMath::RoundToInt(Location.X / 100.0)), FText::AsNumber(FMath::RoundToInt(Location.Y / 100.0)));
}

FName GetCellWidgetName(const FZoneRow& Row)
{
	return FName(*(TEXT("MinimapCell_") + Row.ZoneID.ToString()));
}

FPlaceholder AddPlaceholder(UWidgetTree& Tree, UCanvasPanel& Root, const TArray<const FZoneRow*>& Rows,
	const FVector2D& TopLeft, const ASeaActor* Sea)
{
	FPlaceholder Out;
	const FBox2D Drawn = Sea != nullptr ? Sea->GetMapBounds() : FBox2D(ForceInit);
	Out.Bounds = Drawn.bIsValid ? Drawn : GetMapBounds(Rows);
	if (!Out.Bounds.bIsValid)
	{
		return Out;
	}

	const FVector2D Extent = GetMapExtent(Out.Bounds);
	const FVector2D PanelSize = Extent + FVector2D(FrameBorder * 2.f);

	// 지도는 캔버스 하나에 모은다 — 안쪽 좌표가 전부 지도 왼쪽 위 기준이라 점을 옮길 때 화면 여백을 몰라도 된다.
	UCanvasPanel* const Panel = Tree.ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MinimapPanel"));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root.AddChild(Panel)))
	{
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		PanelSlot->SetAlignment(FVector2D::ZeroVector);
		PanelSlot->SetPosition(TopLeft);
		PanelSlot->SetSize(PanelSize);
	}

	auto Place = [Panel](UWidget* Widget, const FVector2D& Position, const FVector2D& Size)
	{
		if (UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(Panel->AddChild(Widget)))
		{
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
		}
	};

	// 바탕 = 테두리·칸 경계선 색. 팔레트(DESIGN 2.1)의 진갈색 — 새 색을 만들지 않는다.
	UImage* const Frame = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapFrame"));
	Frame->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiTextOnButton(), FrameRadius));
	Place(Frame, FVector2D::ZeroVector, PanelSize);

	const float Scale = GetScale(Out.Bounds, MapSize);
	if (Drawn.bIsValid)
	{
		// 새 지도 — 지도 그림 한 장이 칸 색을 대신한다(존 경계가 그림에 이미 있다)
		UImage* const Picture = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapPicture"));
		Picture->SetBrushResourceObject(Sea->MapTexture);
		Place(Picture, FVector2D(FrameBorder), Extent);
	}
	for (const FZoneRow* Row : Drawn.bIsValid ? TArray<const FZoneRow*>() : Rows)
	{
		// 칸마다 사방을 선 두께 절반씩 줄인다 — 붙은 칸 사이에 바탕색 선이 한 줄 생겨 경계가 보인다.
		const FVector2D CellSize(Row->RegionHalfExtentCm * 2.f * Scale - CellGap);
		const FVector2D Center = WorldToMap(Out.Bounds, FVector(Row->RegionCenterCm, 0.0));
		UImage* const Cell = Tree.ConstructWidget<UImage>(UImage::StaticClass(), GetCellWidgetName(*Row));
		Cell->SetBrush(FSlateColorBrush(FLinearColor(Row->MinimapColor)));
		Place(Cell, FVector2D(FrameBorder) + Center - CellSize * 0.5f, CellSize);
		++Out.CellCount;
	}

	// 이벤트 표식은 칸 위, 플레이어 점 아래 — 내가 그 자리에 도착하면 내 점이 표식을 덮는다.
	Out.EventDot = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapEventDot"));
	Out.EventDot->SetBrush(FSlateRoundedBoxBrush(
		EventDotColor(), EventDotSize * 0.5f, SnareUiStyle::UiTextOnButton(), DotOutline));
	Place(Out.EventDot, FVector2D::ZeroVector, FVector2D(EventDotSize));
	SetShown(Out.EventDot, false);

	// 스토리 목표 표식 — 청록 점. 이벤트·보물 점 아래, 내 점 위가 아니라 아래(도착하면 내 점이 덮는다).
	Out.StoryDot = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapStoryDot"));
	Out.StoryDot->SetBrush(FSlateRoundedBoxBrush(
		FLinearColor(0.1f, 0.75f, 0.85f), EventDotSize * 0.5f, SnareUiStyle::UiTextOnButton(), DotOutline));
	Place(Out.StoryDot, FVector2D::ZeroVector, FVector2D(EventDotSize));
	SetShown(Out.StoryDot, false);

	// 보물 표식 — 팔레트의 전설 등급 금색(2.1). 플레이어 점 아래라 도착하면 내 점이 덮는다.
	Out.TreasureDot = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapTreasureDot"));
	Out.TreasureDot->SetBrush(FSlateRoundedBoxBrush(
		SnareUiStyle::ItemGradeColor(EItemGrade::Legendary), EventDotSize * 0.5f, SnareUiStyle::UiTextOnButton(), DotOutline));
	Place(Out.TreasureDot, FVector2D::ZeroVector, FVector2D(EventDotSize));
	SetShown(Out.TreasureDot, false);

	// 점은 칸 위에 마지막으로 얹는다 (캔버스는 나중에 넣은 것이 위에 그려진다).
	Out.PlayerDot = Tree.ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapPlayerDot"));
	Out.PlayerDot->SetBrush(FSlateRoundedBoxBrush(
		SnareUiStyle::UiButtonHighlight(), DotSize * 0.5f, SnareUiStyle::UiTextOnButton(), DotOutline));
	Place(Out.PlayerDot, FVector2D::ZeroVector, FVector2D(DotSize));
	MoveDot(*Out.PlayerDot, Out.Bounds, FVector(Out.Bounds.GetCenter(), 0.0));

	// 점·칸 위에 얹는 투명 누름 칸 — 지도 틀(테두리 포함)만 덮고 존 이름·좌표 줄은 안 덮는다.
	Out.TouchButton = Tree.ConstructWidget<UButton>(UButton::StaticClass(), TEXT("MinimapTouchButton"));
	Out.TouchButton->SetBackgroundColor(FLinearColor::Transparent);
	if (UCanvasPanelSlot* const TouchSlot = Cast<UCanvasPanelSlot>(Root.AddChild(Out.TouchButton)))
	{
		TouchSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		TouchSlot->SetAlignment(FVector2D::ZeroVector);
		TouchSlot->SetPosition(TopLeft);
		TouchSlot->SetSize(PanelSize);
	}

	Out.ZoneNameText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MinimapZoneText"));
	Out.ZoneNameText->SetText(FormatZoneName(nullptr));
	Out.ZoneNameText->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* const TextSlot = Cast<UCanvasPanelSlot>(Root.AddChild(Out.ZoneNameText)))
	{
		TextSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		TextSlot->SetAlignment(FVector2D::ZeroVector);
		TextSlot->SetPosition(TopLeft + FVector2D(0.f, PanelSize.Y + ZoneNameGap));
		TextSlot->SetSize(FVector2D(PanelSize.X, ZoneNameHeight));
	}

	// 남은 시간 줄은 존 이름 바로 아래 같은 높이 — 자리는 늘 비워 둔다(이벤트가 떴다 사라져도 아래 기둥이 안 움직인다).
	Out.EventText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MinimapEventText"));
	Out.EventText->SetJustification(ETextJustify::Center);
	Out.EventText->SetColorAndOpacity(FSlateColor(EventDotColor()));
	SetShown(Out.EventText, false);
	if (UCanvasPanelSlot* const EventSlot = Cast<UCanvasPanelSlot>(Root.AddChild(Out.EventText)))
	{
		EventSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		EventSlot->SetAlignment(FVector2D::ZeroVector);
		EventSlot->SetPosition(TopLeft + FVector2D(0.f, PanelSize.Y + ZoneNameGap + ZoneNameHeight));
		EventSlot->SetSize(FVector2D(PanelSize.X, ZoneNameHeight));
	}

	Out.CoordText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MinimapCoordText"));
	Out.CoordText->SetJustification(ETextJustify::Center);
	SetShown(Out.CoordText, false);
	if (UCanvasPanelSlot* const CoordSlot = Cast<UCanvasPanelSlot>(Root.AddChild(Out.CoordText)))
	{
		CoordSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		CoordSlot->SetAlignment(FVector2D::ZeroVector);
		CoordSlot->SetPosition(TopLeft + FVector2D(0.f, PanelSize.Y + ZoneNameGap + ZoneNameHeight * 2.f));
		CoordSlot->SetSize(FVector2D(PanelSize.X, ZoneNameHeight));
	}

	Out.TreasureText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MinimapTreasureText"));
	Out.TreasureText->SetJustification(ETextJustify::Center);
	Out.TreasureText->SetColorAndOpacity(FSlateColor(SnareUiStyle::ItemGradeColor(EItemGrade::Legendary)));
	SetShown(Out.TreasureText, false);
	if (UCanvasPanelSlot* const TreasureSlot = Cast<UCanvasPanelSlot>(Root.AddChild(Out.TreasureText)))
	{
		TreasureSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		TreasureSlot->SetAlignment(FVector2D::ZeroVector);
		TreasureSlot->SetPosition(TopLeft + FVector2D(0.f, PanelSize.Y + ZoneNameGap + ZoneNameHeight * 3.f));
		TreasureSlot->SetSize(FVector2D(PanelSize.X, ZoneNameHeight));
	}

	Out.Height = PanelSize.Y + ZoneNameGap + ZoneNameHeight * 4.f;
	return Out;
}

void MoveDot(UImage& Dot, const FBox2D& Bounds, const FVector& Location)
{
	if (UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(Dot.Slot))
	{
		// 점 크기는 슬롯에서 읽는다 — 플레이어 점(18)·이벤트 표식(14)이 같은 함수를 쓴다.
		Slot->SetPosition(FVector2D(FrameBorder) + WorldToMap(Bounds, Location) - Slot->GetSize() * 0.5f);
	}
}

void UpdateCoordinates(UTextBlock* Text, bool bShown, const FVector& Location)
{
	SetShown(Text, bShown);
	if (bShown && Text != nullptr)
	{
		const FText Line = FormatCoordinates(Location);
		if (!Text->GetText().EqualTo(Line))
		{
			Text->SetText(Line);
		}
	}
}

void UpdateEventMarker(UImage* Dot, UTextBlock* Text, const FBox2D& Bounds, const ADangerZoneActor* Event)
{
	const bool bShown = Event != nullptr && Bounds.bIsValid;
	SetShown(Dot, bShown);
	SetShown(Text, bShown);
	if (!bShown)
	{
		return;
	}

	if (Dot != nullptr)
	{
		MoveDot(*Dot, Bounds, Event->GetActorLocation());
	}
	if (Text != nullptr)
	{
		const FText Line = FormatEventRemaining(Event->GetLifetimeRemaining());
		if (!Text->GetText().EqualTo(Line))
		{
			Text->SetText(Line);
		}
	}
}

void UpdateStoryMarker(UImage* Dot, const FBox2D& Bounds, const FVector* Location)
{
	const bool bShown = Location != nullptr && Bounds.bIsValid;
	SetShown(Dot, bShown);
	if (bShown && Dot != nullptr)
	{
		MoveDot(*Dot, Bounds, *Location);
	}
}

void UpdateTreasureMarker(UImage* Dot, UTextBlock* Text, const FBox2D& Bounds, const FVector* Location, double RemainingSeconds)
{
	const bool bActive = RemainingSeconds > 0.0;
	SetShown(Dot, bActive && Location != nullptr && Bounds.bIsValid);
	SetShown(Text, bActive);
	if (!bActive)
	{
		return;
	}
	if (Dot != nullptr && Location != nullptr && Bounds.bIsValid)
	{
		MoveDot(*Dot, Bounds, *Location);
	}
	if (Text != nullptr)
	{
		const FText Line = FormatTreasureRemaining(RemainingSeconds);
		if (!Text->GetText().EqualTo(Line))
		{
			Text->SetText(Line);
		}
	}
}
}

#undef LOCTEXT_NAMESPACE
