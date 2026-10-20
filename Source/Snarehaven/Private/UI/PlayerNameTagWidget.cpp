#include "UI/PlayerNameTagWidget.h"

#include "Backend/SnareBlockListSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Player/SnareEmoteBubble.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareUiStyle.h"

namespace
{
	constexpr int32 MaxNameChars = 12;
	constexpr float FontSize = 20.f;          // 시트 14px@1080 을 폰 화면에서 읽히게 키운 값 (DESIGN 2.2)
	constexpr float PadX = 12.f;
	constexpr float PadY = 3.f;
	// 말풍선 (DESIGN 2.2 「감정표현 말풍선」 🟡) — 이름보다 크게, 흰 바탕 진갈색 글자로 이름 알약과 갈라 보이게.
	constexpr float BubbleFontSize = 30.f;
	constexpr float BubblePadX = 16.f;
	constexpr float BubblePadY = 4.f;
	constexpr float BubbleGap = 6.f;
	// 먹기 「+N」 (DESIGN 8.8 「먹기」 🟡) — 말풍선만큼 크게, 진갈색 테로 풀밭 위에서도 읽히게.
	constexpr float HealPopFontSize = 30.f;
	constexpr float HealPopOutlinePx = 2.f;
	constexpr float HealPopRisePx = 40.f;
	constexpr float HealPopFadeStart = 0.5f;
}

FString UPlayerNameTagWidget::MakeHealPopLabel(int32 Amount)
{
	if (Amount == 0)
	{
		return FString();
	}
	return Amount > 0 ? FString::Printf(TEXT("+%d"), Amount) : FString::Printf(TEXT("-%d"), -Amount);
}

float UPlayerNameTagWidget::GetHealPopRisePx(float Progress)
{
	return HealPopRisePx * FMath::Clamp(Progress, 0.f, 1.f);
}

float UPlayerNameTagWidget::GetHealPopOpacity(float Progress)
{
	const float T = FMath::Clamp(Progress, 0.f, 1.f);
	return T <= HealPopFadeStart ? 1.f : 1.f - (T - HealPopFadeStart) / (1.f - HealPopFadeStart);
}

int32 UPlayerNameTagWidget::GetColorSlot(const FString& PlayerName, int32 PlayerId)
{
	// 접두어는 `ASnarehavenGameMode::MakeRoomPlayerName` 과 같은 글자.
	static const FString RoomPrefix = TEXT("플레이어 ");
	const FString Number = PlayerName.StartsWith(RoomPrefix) ? PlayerName.RightChop(RoomPrefix.Len()) : FString();
	const int32 RoomNumber = Number.IsNumeric() ? FCString::Atoi(*Number) : 0;
	return RoomNumber >= 1 ? RoomNumber - 1 : PlayerId;
}

FString UPlayerNameTagWidget::MakeLabel(const FString& PlayerName)
{
	const FString Trimmed = PlayerName.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return TEXT("플레이어");
	}
	return Trimmed.Len() <= MaxNameChars ? Trimmed : Trimmed.Left(MaxNameChars - 1) + TEXT("…");
}

TSharedRef<SWidget> UPlayerNameTagWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		Pill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("NameTagPill"));
		Pill->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, (FontSize + 2.f * PadY) * 0.5f + 2.f));
		Pill->SetBrushColor(SnareUiStyle::UiNameTagColor(0));
		Pill->SetPadding(FMargin(PadX, PadY));
		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NameTagText"));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetText(FText::FromString(MakeLabel(FString())));
		Pill->SetContent(Label);

		Bubble = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EmoteBubble"));
		Bubble->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, (BubbleFontSize + 2.f * BubblePadY) * 0.5f + 2.f));
		Bubble->SetBrushColor(FLinearColor::White);
		Bubble->SetPadding(FMargin(BubblePadX, BubblePadY));
		BubbleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EmoteBubbleText"));
		BubbleText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		BubbleText->SetJustification(ETextJustify::Center);
		Bubble->SetContent(BubbleText);
		Bubble->SetVisibility(ESlateVisibility::Collapsed);

		HealPop = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealPopText"));
		HealPop->SetJustification(ETextJustify::Center);
		HealPop->SetVisibility(ESlateVisibility::Collapsed);

		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NameTagColumn"));
		if (UVerticalBoxSlot* const HealSlot = Column->AddChildToVerticalBox(HealPop))
		{
			HealSlot->SetHorizontalAlignment(HAlign_Center);
		}
		if (UVerticalBoxSlot* const BubbleSlot = Column->AddChildToVerticalBox(Bubble))
		{
			BubbleSlot->SetHorizontalAlignment(HAlign_Center);
			BubbleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BubbleGap));
		}
		if (UVerticalBoxSlot* const PillSlot = Column->AddChildToVerticalBox(Pill))
		{
			PillSlot->SetHorizontalAlignment(HAlign_Center);
		}
		WidgetTree->RootWidget = Column;
	}
	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (Label != nullptr)
	{
		SnareUiStyle::SetFontSize(*Label, FontSize);
	}
	if (BubbleText != nullptr)
	{
		SnareUiStyle::SetFontSize(*BubbleText, BubbleFontSize);
	}
	if (HealPop != nullptr)
	{
		SnareUiStyle::SetFontSize(*HealPop, HealPopFontSize);
		FSlateFontInfo Font = HealPop->GetFont();
		Font.OutlineSettings.OutlineSize = HealPopOutlinePx;
		Font.OutlineSettings.OutlineColor = SnareUiStyle::UiTextOnButton();
		HealPop->SetFont(Font);
	}
	return Result;
}

void UPlayerNameTagWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(OwnerPawn.Get());
	// 내가 차단한 사람(10-16 신고·차단)은 내 화면에서만 이름을 가리고 말풍선을 숨긴다.
	const bool bBlocked = USnareBlockListSubsystem::IsPawnBlocked(OwnerPawn.Get());
	const int32 BubbleIndex = Character != nullptr && !bBlocked ? Character->GetEmoteBubble() : INDEX_NONE;
	if (BubbleIndex != ShownBubble && Bubble != nullptr && BubbleText != nullptr)
	{
		ShownBubble = BubbleIndex;
		BubbleText->SetText(SnareEmoteBubble::GetGlyph(BubbleIndex));
		Bubble->SetVisibility(BubbleIndex == INDEX_NONE ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (HealPop != nullptr)
	{
		const float Progress = Character != nullptr ? Character->GetHealPopProgress() : -1.f;
		if (Progress < 0.f)
		{
			HealPop->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			const int32 Amount = Character->GetHealPopAmount();
			if (Character->GetHealPopCount() != ShownHealPopCount)
			{
				ShownHealPopCount = Character->GetHealPopCount();
				HealPop->SetText(FText::FromString(MakeHealPopLabel(Amount)));
				HealPop->SetColorAndOpacity(FSlateColor(Amount > 0 ? SnareUiStyle::UiIconMint() : SnareUiStyle::UiAccentRed()));
			}
			HealPop->SetRenderTranslation(FVector2D(0.f, -GetHealPopRisePx(Progress)));
			HealPop->SetRenderOpacity(GetHealPopOpacity(Progress));
			HealPop->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	const APlayerState* const State = OwnerPawn.IsValid() ? OwnerPawn->GetPlayerState() : nullptr;
	if (State == nullptr || Label == nullptr || Pill == nullptr)
	{
		return;
	}
	const FString Name = bBlocked ? FString(TEXT("차단한 플레이어")) : MakeLabel(State->GetPlayerName());
	if (Name != ShownName)
	{
		ShownName = Name;
		Label->SetText(FText::FromString(Name));
	}
	const int32 Color = GetColorSlot(State->GetPlayerName(), State->GetPlayerId());
	if (Color != ShownColor)
	{
		ShownColor = Color;
		Pill->SetBrushColor(SnareUiStyle::UiNameTagColor(Color));
	}
}
