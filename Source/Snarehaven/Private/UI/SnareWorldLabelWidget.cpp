#include "UI/SnareWorldLabelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "UI/SnareUiStyle.h"

namespace
{
	constexpr float FontSize = 22.f;   // 이름표(20)보다 한 치수 크게 — 나무 이름은 멀리서도 읽혀야 한다 (🟡)
	constexpr float PadX = 14.f;
	constexpr float PadY = 4.f;
}

void USnareWorldLabelWidget::SetLabel(const FText& InLabel)
{
	LabelText = InLabel;
	if (Label != nullptr)
	{
		Label->SetText(LabelText);
	}
}

TSharedRef<SWidget> USnareWorldLabelWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WorldLabelPill"));
		Pill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, (FontSize + 2.f * PadY) * 0.5f + 2.f));
		Pill->SetBrushColor(FLinearColor::White);
		Pill->SetPadding(FMargin(PadX, PadY));
		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WorldLabelText"));
		Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		Label->SetText(LabelText);
		Pill->SetContent(Label);
		WidgetTree->RootWidget = Pill;
	}
	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (Label != nullptr)
	{
		SnareUiStyle::SetFontSize(*Label, FontSize);
	}
	return Result;
}

bool USnareWorldLabelWidget::IsFullyInside(const FSlateRect& LabelRect, const FSlateRect& Viewport)
{
	return LabelRect.Left >= Viewport.Left && LabelRect.Top >= Viewport.Top
		&& LabelRect.Right <= Viewport.Right && LabelRect.Bottom <= Viewport.Bottom;
}

void USnareWorldLabelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 화면 공간 알약은 물건이 화면 밖이어도 가장자리에 걸쳐 그려져 글 반쪽이 남는다(10-16 목장 왼쪽 아래 「라요」 =
	// 고구마 장수 팻말). 다 들어올 때만 보인다. 접지(Collapsed) 않고 투명도만 — 접으면 배치가 멈춰 다시 못 돌아온다.
	const FGeometry Viewport = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
	const FVector2D ViewportSize = Viewport.GetAbsoluteSize();
	const bool bShow = ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f
		|| IsFullyInside(MyGeometry.GetLayoutBoundingRect(), Viewport.GetLayoutBoundingRect());
	SetRenderOpacity(bShow ? 1.f : 0.f);
}
