#include "UI/SnareNoticeCards.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/SnareUiStyle.h"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟢 — 게임 규칙이 아니라 "화면 어디에 얼마나 크게") — 시트 `hud_ingame.md` 5행
	// 「크림 카드 300×70, 왼쪽 아이콘 48px, 제목 굵게 18 + 설명 14, 카드 오른쪽 위 작은 색 점」. 가로 여백은 `MakeCreamCard` 의 16,
	// 세로 여백은 아이콘 48 이 카드 70 가운데에 오게 (70 − 48) / 2 — 목장 수확 카드(M23-03)와 같은 배치.
	constexpr float CardWidth = 300.f;
	constexpr float CardHeight = 70.f;
	constexpr float CardPaddingX = 16.f;
	constexpr float CardPaddingY = (CardHeight - USnareNoticeCards::IconSize) * 0.5f;
	// 아이콘과 글 사이 — 목장 수확 카드 `NoticeCardGap` 과 같은 값.
	constexpr float IconGap = 12.f;
	constexpr float TitleFontSize = 18.f;
	constexpr float BodyFontSize = 14.f;
	// 「!」 꼬리표 — 원본은 카드 높이의 약 1/3 굵은 「!」. 글자 22 는 획이 가늘고 카드 안에 머물러(루프#673 첫 캡처)
	// 글자 32 + 진갈색 외곽선 3, 15° 기울임. 카드 안쪽 여백만큼 밖으로 밀어 오른쪽 위 모서리에 반쯤 걸치게 한다(렌더 이동이라 배치는 안 바뀐다).
	constexpr float TagFontSize = 32.f;
	constexpr int32 TagOutlinePx = 3;
	constexpr float TagAngle = 15.f;
	constexpr float TagInsetX = 10.f;
	constexpr float TagRiseY = 20.f;
	// 글이 꼬리표 밑으로 안 들어가게 비워 두는 오른쪽 폭.
	constexpr float TagRoom = 20.f;
	// 카드끼리 세로 틈 — 목장 알림 줄 `NoticeStackGap` 과 같은 값.
	constexpr float StackGap = 12.f;
}

FLinearColor USnareNoticeCards::DotColor(bool bAchievement)
{
	// 업적 = 코인 금색(#E5A82E), 도감 = 시트 「빨강 = 새 발견」 → HUD 빨강과 같은 값.
	return bAchievement ? FLinearColor::FromSRGBColor(FColor(0xE5, 0xA8, 0x2E)) : SnareUiStyle::UiAccentRed();
}

void USnareNoticeCards::SetCardColor(UBorder& Card, const FLinearColor& Color)
{
	// 크림 바탕은 붓 안에 넣고 색조는 흰색 — 색조가 테두리까지 물들이지 않게.
	Card.SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), SnareUiStyle::HudCornerRadius, Color, OutlineWidth));
	Card.SetBrushColor(FLinearColor::White);
}

void USnareNoticeCards::BuildCards(UWidgetTree& Tree)
{
	if (Cards.Num() > 0)
	{
		return;
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	for (int32 Index = 0; Index < SnareAchievementNotice::MaxCards; ++Index)
	{
		UBorder* const Card = SnareUiStyle::MakeCreamCard(Tree, *FString::Printf(TEXT("AchievementCard_%d"), Index));
		Card->SetPadding(FMargin(CardPaddingX, CardPaddingY));
		SetCardColor(*Card, DotColor(false));
		if (UVerticalBoxSlot* const CardSlot = AddChildToVerticalBox(Card))
		{
			CardSlot->SetHorizontalAlignment(HAlign_Left);
			CardSlot->SetPadding(FMargin(0.f, StackGap, 0.f, 0.f));
		}
		USizeBox* const Body = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			*FString::Printf(TEXT("AchievementCardBody_%d"), Index));
		Body->SetWidthOverride(CardWidth - CardPaddingX * 2.f);
		Body->SetHeightOverride(CardHeight - CardPaddingY * 2.f);
		Card->SetContent(Body);
		UOverlay* const Layer = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(),
			*FString::Printf(TEXT("AchievementCardLayer_%d"), Index));
		Body->SetContent(Layer);

		// 가로 줄 = 왼쪽 아이콘 48 + 글 두 줄. 아이콘이 비면 접혀서(가로 줄은 접힌 칸의 틈도 안 센다) 글이 왼쪽으로 붙는다.
		UHorizontalBox* const Row = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			*FString::Printf(TEXT("AchievementCardRow_%d"), Index));
		if (UOverlaySlot* const RowSlot = Layer->AddChildToOverlay(Row))
		{
			RowSlot->SetVerticalAlignment(VAlign_Fill);
			RowSlot->SetPadding(FMargin(0.f, 0.f, TagRoom, 0.f));
		}
		UImage* const Icon = Tree.ConstructWidget<UImage>(UImage::StaticClass(),
			*FString::Printf(TEXT("AchievementCardIcon_%d"), Index));
		Icon->SetDesiredSizeOverride(FVector2D(IconSize, IconSize));
		Icon->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* const IconSlot = Row->AddChildToHorizontalBox(Icon))
		{
			IconSlot->SetVerticalAlignment(VAlign_Center);
			IconSlot->SetPadding(FMargin(0.f, 0.f, IconGap, 0.f));
		}
		UVerticalBox* const Lines = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
			*FString::Printf(TEXT("AchievementCardLines_%d"), Index));
		if (UHorizontalBoxSlot* const LinesSlot = Row->AddChildToHorizontalBox(Lines))
		{
			LinesSlot->SetVerticalAlignment(VAlign_Center);
			LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		UTextBlock* const Title = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			*FString::Printf(TEXT("AchievementCardTitle_%d"), Index));
		Title->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		SnareUiStyle::SetFontSize(*Title, TitleFontSize);
		Lines->AddChildToVerticalBox(Title);
		UTextBlock* const BodyText = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			*FString::Printf(TEXT("AchievementCardText_%d"), Index));
		BodyText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		SnareUiStyle::SetFontSize(*BodyText, BodyFontSize);
		Lines->AddChildToVerticalBox(BodyText);

		UTextBlock* const Tag = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			*FString::Printf(TEXT("AchievementCardTag_%d"), Index));
		Tag->SetText(FText::FromString(TEXT("!")));
		Tag->SetColorAndOpacity(FSlateColor(DotColor(false)));
		SnareUiStyle::SetFontSize(*Tag, TagFontSize);
		FSlateFontInfo TagFont = Tag->GetFont();
		TagFont.OutlineSettings.OutlineSize = TagOutlinePx;
		TagFont.OutlineSettings.OutlineColor = SnareUiStyle::UiTextOnButton();
		Tag->SetFont(TagFont);
		Tag->SetRenderTransformAngle(TagAngle);
		Tag->SetRenderTranslation(FVector2D(CardPaddingX - TagInsetX, -CardPaddingY - TagRiseY));
		if (UOverlaySlot* const TagSlot = Layer->AddChildToOverlay(Tag))
		{
			TagSlot->SetHorizontalAlignment(HAlign_Right);
			TagSlot->SetVerticalAlignment(VAlign_Top);
		}

		Cards.Add(Card);
		Titles.Add(Title);
		Bodies.Add(BodyText);
		Tags.Add(Tag);
		Icons.Add(Icon);
	}
}

void USnareNoticeCards::ShowCards(const TArray<SnareAchievementNotice::FCard>& InCards, float Seconds)
{
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const bool bShown = InCards.IsValidIndex(Index);
		if (bShown)
		{
			const SnareAchievementNotice::FCard& Card = InCards[Index];
			Titles[Index]->SetText(Card.Title);
			Bodies[Index]->SetText(Card.Body);
			const FLinearColor Color = DotColor(Card.bAchievement);
			SetCardColor(*Cards[Index], Color);
			Tags[Index]->SetColorAndOpacity(FSlateColor(Color));
			// 그림을 못 찾으면(빈 칸·깨진 경로) 칸을 접는다 — 빈 네모 0.
			UTexture2D* const Texture = Card.Icon.IsNull() ? nullptr : Card.Icon.LoadSynchronous();
			if (Texture != nullptr)
			{
				FSlateBrush IconBrush;
				IconBrush.SetResourceObject(Texture);
				IconBrush.ImageSize = FVector2D(IconSize, IconSize);
				Icons[Index]->SetBrush(IconBrush);
			}
			Icons[Index]->SetVisibility(Texture != nullptr ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		Cards[Index]->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HideTimer, this, &USnareNoticeCards::HideCards, Seconds, false);
	}
}

void USnareNoticeCards::HideCards()
{
	for (UBorder* const Card : Cards)
	{
		if (IsValid(Card))
		{
			Card->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

int32 USnareNoticeCards::GetShownCardCount() const
{
	int32 Shown = 0;
	for (const UBorder* const Card : Cards)
	{
		Shown += IsValid(Card) && Card->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0;
	}
	return Shown;
}

void USnareNoticeCards::ReleaseSlateResources(bool bReleaseChildren)
{
	// 위젯이 내려간 뒤 타이머가 죽은 위젯을 부르지 않게 (engines/unreal.md 코드 규칙 7).
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	Super::ReleaseSlateResources(bReleaseChildren);
}
