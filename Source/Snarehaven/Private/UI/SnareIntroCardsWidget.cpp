#include "UI/SnareIntroCardsWidget.h"
#include "SnareCheckSwitch.h"
#include "Backend/SnareStory.h"

#include "Snarehaven.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const TCHAR* GuideSection = TEXT("Snarehaven.Guide");
	const TCHAR* IntroSeenKey = TEXT("IntroSeen");

	// 화면 배치 (🟡 게임 규칙 숫자가 아니다). 가운데 큰 카드 하나.
	// 카드는 화면 높이(설계 기준 1080)의 85% 남짓, 그림은 카드 폭 거의 전부. 단추는 1334x616 에서 손가락 크기(≥88x44px).
	constexpr float CardWidth = 1100.f;
	// 그림은 tools/blender/render_intro_cards.py 의 640x360 (16:9) 렌더.
	constexpr float PictureWidth = 1036.f;
	constexpr float PictureHeight = 583.f;
	constexpr float NoteFontSize = 24.f;
	constexpr float ButtonMinWidth = 180.f;
	constexpr float ButtonMinHeight = 84.f;
	constexpr float CaptionFontSize = 32.f;
	constexpr float ButtonFontSize = 28.f;
	constexpr float ButtonGap = 20.f;
	constexpr float ScrimOpacity = 0.7f;
}

int32 USnareIntroCardsWidget::GetCardCount()
{
	return 3;
}

FText USnareIntroCardsWidget::GetCardText(int32 Index)
{
	switch (Index)
	{
	case 0: return LOCTEXT("IntroCard0", "폭풍에 배가 부서져, 안개 낀 섬 스네어헤이븐 해안에서 눈을 떠요.");
	case 1: return LOCTEXT("IntroCard1", "올가미로 동물을 죽이지 않고 사로잡아요.");
	case 2:
		// 카드가 끝나면 바로 이어지는 스토리 1장 첫 단계를 이름으로 보여 준다 (정체성 ③). 글은 스토리 표 그대로 빌려 온다.
		return LOCTEXT("IntroCard2", "목장에서 동물을 돌보면 온기가 모여요.\n그 온기로 꺼진 등대에 다시 불을 켜요.");
	default: return FText::GetEmpty();
	}
}

FText USnareIntroCardsWidget::GetCardNote(int32 Index)
{
	if (Index != GetCardCount() - 1)
	{
		return FText::GetEmpty();
	}
	// 카드가 끝나면 바로 이어지는 스토리 1장 첫 단계를 이름으로 보여 준다 (정체성 ③). 글은 스토리 표 그대로 빌려 온다.
	return FText::Format(LOCTEXT("IntroNote2", "첫 할 일: {0} — {1}\n왼쪽 위 '지금 할 일'을 따라가 보세요."),
		SnareStory::ChapterTitle(1), SnareStory::StepGoal(1));
}

FString USnareIntroCardsWidget::GetCardIconPath(int32 Index)
{
	switch (Index)
	{
	case 0: return TEXT("/Game/Game/UI/Intro/T_IntroCard_01.T_IntroCard_01");
	case 1: return TEXT("/Game/Game/UI/Intro/T_IntroCard_02.T_IntroCard_02");
	case 2: return TEXT("/Game/Game/UI/Intro/T_IntroCard_03.T_IntroCard_03");
	default: return FString();
	}
}

bool USnareIntroCardsWidget::IsIntroSeen()
{
	bool bSeen = false;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(GuideSection, IntroSeenKey, bSeen, GGameUserSettingsIni);
	}
	return bSeen;
}

void USnareIntroCardsWidget::SetIntroSeen(bool bSeen)
{
	if (GConfig != nullptr)
	{
		GConfig->SetBool(GuideSection, IntroSeenKey, bSeen, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool USnareIntroCardsWidget::ShouldShowIntro()
{
	return !SnareCheckSwitch::Param(TEXT("SnareNoGuide")) && !IsIntroSeen();
}

void USnareIntroCardsWidget::Advance()
{
	if (IsFinished())
	{
		return;
	}
	++CardIndex;
	if (IsFinished())
	{
		Finish();
		return;
	}
	Refresh();
}

void USnareIntroCardsWidget::Skip()
{
	CardIndex = GetCardCount();
	Finish();
}

void USnareIntroCardsWidget::Finish()
{
	if (bPersistSeen)
	{
		SetIntroSeen(true);
	}
	RemoveFromParent();
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 첫 실행 그림 카드를 끝냈다 (저장=%s)"), bPersistSeen ? TEXT("예") : TEXT("아니오"));
	OnFinished.Broadcast();
}

void USnareIntroCardsWidget::HandleNextClicked()
{
	Advance();
}

void USnareIntroCardsWidget::HandleSkipClicked()
{
	Skip();
}

void USnareIntroCardsWidget::Refresh()
{
	if (CardTextBlock != nullptr)
	{
		CardTextBlock->SetText(GetCardText(CardIndex));
	}
	if (CardNoteBlock != nullptr)
	{
		const FText Note = GetCardNote(CardIndex);
		CardNoteBlock->SetText(Note);
		CardNoteBlock->SetVisibility(Note.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (CardImage != nullptr)
	{
		UTexture2D* const Texture = LoadObject<UTexture2D>(nullptr, *GetCardIconPath(CardIndex));
		if (Texture != nullptr)
		{
			CardImage->SetBrushFromTexture(Texture);
		}
		CardImage->SetDesiredSizeOverride(FVector2D(PictureWidth, PictureHeight));
	}
	if (NextLabel != nullptr)
	{
		NextLabel->SetText(CardIndex + 1 >= GetCardCount() ? LOCTEXT("IntroStart", "시작!") : LOCTEXT("IntroNext", "다음"));
	}
}

TSharedRef<SWidget> USnareIntroCardsWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("IntroRoot"));
		WidgetTree->RootWidget = Root;

		// 뒤 화면을 어둡게 덮고 눌림을 막는다.
		UBorder* const Scrim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("IntroScrim"));
		Scrim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, ScrimOpacity));
		if (UCanvasPanelSlot* const ScrimSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Scrim)))
		{
			ScrimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			ScrimSlot->SetOffsets(FMargin(0.f));
		}

		UBorder* const Card = SnareUiStyle::MakeWoodSignCard(*WidgetTree, TEXT("IntroCard"));
		Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Card->SetPadding(FMargin(32.f, 28.f));
		if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Card)))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CardSlot->SetSize(FVector2D(CardWidth, 0.f));
			CardSlot->SetAutoSize(true);
		}

		// 카드 폭을 글 길이와 상관없이 고정한다 (장마다 폭이 들쭉날쭉하지 않게).
		USizeBox* const Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IntroWidth"));
		Width->SetWidthOverride(CardWidth - 64.f);
		Card->SetContent(Width);
		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("IntroColumn"));
		Width->SetContent(Column);

		CardImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IntroPicture"));
		if (UVerticalBoxSlot* const PicSlot = Column->AddChildToVerticalBox(CardImage))
		{
			PicSlot->SetHorizontalAlignment(HAlign_Center);
			PicSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 20.f));
		}

		CardTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("IntroCaption"));
		CardTextBlock->SetAutoWrapText(true);
		CardTextBlock->SetJustification(ETextJustify::Center);
		CardTextBlock->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		Column->AddChildToVerticalBox(CardTextBlock);

		CardNoteBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("IntroNote"));
		CardNoteBlock->SetAutoWrapText(true);
		CardNoteBlock->SetJustification(ETextJustify::Center);
		CardNoteBlock->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		if (UVerticalBoxSlot* const NoteSlot = Column->AddChildToVerticalBox(CardNoteBlock))
		{
			NoteSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		}

		UHorizontalBox* const Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("IntroButtons"));
		if (UVerticalBoxSlot* const ButtonsSlot = Column->AddChildToVerticalBox(Buttons))
		{
			ButtonsSlot->SetHorizontalAlignment(HAlign_Right);
			ButtonsSlot->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
		}

		auto AddButton = [&](FName Name, const FText& Label, UTextBlock** OutLabel, bool bFirst) -> UButton*
		{
			UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			SnareUiStyle::MakeRoundButton(*Button, 14.f);
			UTextBlock* const Text =
				WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name.ToString() + TEXT("Text")));
			Text->SetText(Label);
			Text->SetJustification(ETextJustify::Center);
			Button->AddChild(Text);
			// 손가락 크기 — 글이 짧아도 단추가 작아지지 않게.
			USizeBox* const Finger = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(Name.ToString() + TEXT("Size")));
			Finger->SetMinDesiredWidth(ButtonMinWidth);
			Finger->SetMinDesiredHeight(ButtonMinHeight);
			Finger->SetContent(Button);
			if (UHorizontalBoxSlot* const BoxSlot = Buttons->AddChildToHorizontalBox(Finger))
			{
				BoxSlot->SetPadding(FMargin(bFirst ? 0.f : ButtonGap, 0.f, 0.f, 0.f));
			}
			if (OutLabel != nullptr)
			{
				*OutLabel = Text;
			}
			return Button;
		};
		UButton* const SkipButton = AddButton(TEXT("IntroSkipButton"), LOCTEXT("IntroSkip", "건너뛰기"), nullptr, true);
		UTextBlock* NextLabelRaw = nullptr;
		UButton* const NextButton = AddButton(TEXT("IntroNextButton"), LOCTEXT("IntroNext", "다음"), &NextLabelRaw, false);
		NextLabel = NextLabelRaw;
		SkipButton->OnClicked.AddDynamic(this, &USnareIntroCardsWidget::HandleSkipClicked);
		NextButton->OnClicked.AddDynamic(this, &USnareIntroCardsWidget::HandleNextClicked);
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	// 나무 표찰 위 단추는 한 단 진한 갈색 — 공통 스타일(표찰과 같은 나무색)을 덮어써야 단추 테두리가 보인다.
	for (const TCHAR* const ButtonName : {TEXT("IntroSkipButton"), TEXT("IntroNextButton")})
	{
		if (UButton* const Button = WidgetTree->FindWidget<UButton>(FName(ButtonName)))
		{
			FButtonStyle Style = Button->GetStyle();
			Style.Normal.TintColor = FSlateColor(SnareUiStyle::UiTitleTab());
			Style.Hovered.TintColor = FSlateColor(SnareUiStyle::UiTitleTab());
			Button->SetStyle(Style);
		}
	}
	if (CardTextBlock != nullptr)
	{
		SnareUiStyle::SetFontSize(*CardTextBlock, CaptionFontSize);
	}
	if (CardNoteBlock != nullptr)
	{
		SnareUiStyle::SetFontSize(*CardNoteBlock, NoteFontSize);
	}
	if (NextLabel != nullptr)
	{
		SnareUiStyle::SetFontSize(*NextLabel, ButtonFontSize);
	}
	Refresh();
	return Result;
}

#undef LOCTEXT_NAMESPACE
