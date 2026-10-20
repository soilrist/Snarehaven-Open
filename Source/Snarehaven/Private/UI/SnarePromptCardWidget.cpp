#include "UI/SnarePromptCardWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 시트 카드 36px 을 아이콘+여백으로 맞춘다: 24 + 2×6 = 36.
	constexpr float CardIconSize = 24.f;
	constexpr float PadX = 12.f;
	constexpr float PadY = 6.f;

	/** 누르는 카드 글자 — 상점 판 가격 글(시트 26px)과 같은 크기. 72 높이에 한 줄. */
	constexpr float TapCardFontSize = 26.f;

	/** 누르는 카드 왼쪽 행동 아이콘 — 72 높이 카드 안에서 여백 합쳐 56. */
	constexpr float TapCardIconSize = 44.f;
	constexpr float TapCardIconGap = 9.f; // 1280×720 에서 약 6px.

	/** 아이콘 그림 크기를 브러시에 직접 박는다 — `SetDesiredSizeOverride` 만으론 브러시 기본 32 로 그려져
	 *  1280×720 에서 약 14px 로 작게 찍혔다 (감독 캡처 10-17). */
	void SetIconSize(UImage& Icon, float Size)
	{
		FSlateBrush Brush = Icon.GetBrush();
		Brush.ImageSize = FVector2D(Size, Size);
		Icon.SetBrush(Brush);
		Icon.SetDesiredSizeOverride(FVector2D(Size, Size));
	}
}

TSharedRef<SWidget> USnarePromptCardWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		Card = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("PromptCard"));
		// `MakeCreamCard` 는 접힌 채로 돌려준다(HUD 카드는 펼 거리가 생길 때 편다) — 이 카드는 위젯째로
		// 켜고 끄므로(`UWidgetComponent::SetVisibility`) 안쪽은 늘 보이게 둔다. 접힌 채 두면 컴포넌트를
		// 켜도 빈 자리만 뜬다.
		Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Card->SetPadding(FMargin(PadX, PadY));

		// 「!」 대신 행동 아이콘 (결함 10-15 16:50 「!」 이상함) — 목장 칸은 넣기·수확 둘 다 줍기라 바구니.
		// 아이콘을 못 찾으면 빈 카드만 — 「!」 로 되돌리지 않는다.
		Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PromptIcon"));
		if (UTexture2D* const IconTexture = LoadObject<UTexture2D>(nullptr, RanchSlotIconPath))
		{
			Icon->SetBrushFromTexture(IconTexture);
		}
		SetIconSize(*Icon, CardIconSize);
		Card->SetContent(Icon);
		WidgetTree->RootWidget = Card;
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	// 글꼴·색은 한 벌에서 온다 (M5-05a).
	SnareUiStyle::ApplyUiStyle(*this);
	return Result;
}

UButton* USnarePromptCardWidget::MakeTapCard(UWidgetTree& Tree, FName Name, const FText& Label, const TCHAR* IconPath)
{
	UButton* const Button = Tree.ConstructWidget<UButton>(UButton::StaticClass(), Name);
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
	UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*(Name.ToString() + TEXT("Text"))));
	// 「!」 대신 행동 아이콘 — 아이콘 + 짧은 동사. 아이콘을 못 찾으면 글자만 둔다.
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	SnareUiStyle::SetFontSize(*Text, TapCardFontSize);

	UTexture2D* const IconTexture = IconPath != nullptr ? LoadObject<UTexture2D>(nullptr, IconPath) : nullptr;
	if (IconTexture == nullptr)
	{
		Button->AddChild(Text);
		return Button;
	}
	UHorizontalBox* const Row = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(*(Name.ToString() + TEXT("Row"))));
	UImage* const Icon = Tree.ConstructWidget<UImage>(UImage::StaticClass(), FName(*(Name.ToString() + TEXT("Icon"))));
	Icon->SetBrushFromTexture(IconTexture);
	SetIconSize(*Icon, TapCardIconSize);
	if (UHorizontalBoxSlot* const IconSlot = Row->AddChildToHorizontalBox(Icon))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, TapCardIconGap, 0.f));
	}
	if (UHorizontalBoxSlot* const TextSlot = Row->AddChildToHorizontalBox(Text))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	Button->AddChild(Row);
	return Button;
}

bool USnarePromptCardWidget::ComputeTapCardPosition(const FVector2D& TargetPx, const FVector2D& ViewportPx, FVector2D& OutTopLeft)
{
	const bool bOnScreen = TargetPx.X >= 0.f && TargetPx.Y >= 0.f && TargetPx.X <= ViewportPx.X && TargetPx.Y <= ViewportPx.Y;
	if (!bOnScreen)
	{
		return false;
	}
	OutTopLeft.X = TargetPx.X + TapCardGapPx;
	if (OutTopLeft.X + TapCardWidth > ViewportPx.X)
	{
		OutTopLeft.X = TargetPx.X - TapCardGapPx - TapCardWidth;
	}
	// 세로는 화면 안으로만 당긴다 — 대상이 위·아래 끝에 걸려도 카드가 반쯤 잘리지 않게.
	OutTopLeft.Y = FMath::Clamp(TargetPx.Y - TapCardHeight * 0.5f, 0.f, FMath::Max(0.f, ViewportPx.Y - TapCardHeight));
	return true;
}

bool USnarePromptCardWidget::ProjectTarget(const APlayerController* Controller, const FVector& WorldLocation,
	FVector2D& OutTargetPx, FVector2D& OutViewportPx)
{
	if (!IsValid(Controller))
	{
		return false;
	}
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(Controller);
	if (ViewportScale <= 0.f)
	{
		return false;
	}
	// 위젯 px = 뷰포트 px ÷ DPI 배율 — 캔버스 슬롯 자리와 같은 단위.
	OutViewportPx = UWidgetLayoutLibrary::GetViewportSize(Controller) / ViewportScale;
	return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, WorldLocation, OutTargetPx, false);
}

#undef LOCTEXT_NAMESPACE
