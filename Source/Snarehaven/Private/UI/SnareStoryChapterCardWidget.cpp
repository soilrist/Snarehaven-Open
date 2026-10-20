#include "UI/SnareStoryChapterCardWidget.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Backend/SnareStory.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const TCHAR* StorySection = TEXT("Snarehaven.Story");
	const TCHAR* SeenChapterKey = TEXT("ChapterCardSeen");

	// 화면 배치 (🟡 게임 규칙 숫자가 아니다). 한가운데 카드.
	constexpr float PanelWidth = 720.f;
	constexpr float HeadFontSize = 24.f;
	constexpr float TitleFontSize = 44.f;
	constexpr float RewardFontSize = 30.f;
	constexpr float HintFontSize = 20.f;
}

int32 USnareStoryChapterCardWidget::GetSeenChapter()
{
	int32 Seen = 0;
	if (GConfig != nullptr)
	{
		GConfig->GetInt(StorySection, SeenChapterKey, Seen, GGameUserSettingsIni);
	}
	return Seen;
}

void USnareStoryChapterCardWidget::SetSeenChapter(int32 InChapter)
{
	if (GConfig != nullptr)
	{
		GConfig->SetInt(StorySection, SeenChapterKey, InChapter, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool USnareStoryChapterCardWidget::ShouldShow(int32 CompletedChapter)
{
	return CompletedChapter >= 1 && CompletedChapter <= SnareStory::TotalSteps / SnareStory::StepsPerChapter
		&& CompletedChapter > GetSeenChapter() && !SnareCheckSwitch::Param(TEXT("SnareNoGuide"));
}

bool USnareStoryChapterCardWidget::ShowChapter(int32 InChapter)
{
	const FText ChapterName = SnareStory::ChapterTitle(InChapter);
	if (ChapterName.IsEmpty())
	{
		return false;
	}
	Chapter = InChapter;
	bFinished = false;
	TitleString = ChapterName;
	RewardString = FText::Format(LOCTEXT("ChapterReward", "코인 +{0} · {1}"),
		FText::AsNumber(SnareStory::ChapterCoins(InChapter)), SnareStory::ChapterRewardItemName(InChapter));
	Refresh();
	return true;
}

void USnareStoryChapterCardWidget::Close()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (bPersistSeen)
	{
		SetSeenChapter(FMath::Max(GetSeenChapter(), Chapter));
	}
	RemoveFromParent();
	UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d장 끝 카드를 닫았다 (저장=%s)"), Chapter, bPersistSeen ? TEXT("예") : TEXT("아니오"));
	OnClosed.Broadcast();
}

void USnareStoryChapterCardWidget::Refresh()
{
	if (HeadText != nullptr)
	{
		HeadText->SetText(FText::Format(LOCTEXT("ChapterHead", "{0}장을 끝냈다"), FText::AsNumber(Chapter)));
	}
	if (TitleText != nullptr)
	{
		TitleText->SetText(TitleString);
	}
	if (RewardText != nullptr)
	{
		RewardText->SetText(RewardString);
	}
	if (HintText != nullptr)
	{
		HintText->SetText(LOCTEXT("ChapterClose", "눌러서 닫기"));
	}
}

FReply USnareStoryChapterCardWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Close();
	return FReply::Handled();
}

FReply USnareStoryChapterCardWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	Close();
	return FReply::Handled();
}

TSharedRef<SWidget> USnareStoryChapterCardWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ChapterRoot"));
		WidgetTree->RootWidget = Root;

		UBorder* const Panel = SnareUiStyle::MakeWoodSignCard(*WidgetTree, TEXT("ChapterPanel"));
		Panel->SetPadding(FMargin(32.f, 26.f));
		Panel->SetVisibility(ESlateVisibility::HitTestInvisible);  // 표찰 카드는 기본이 접힘 — 펴되 눌림은 위젯 전체가 받는다
		if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Panel)))
		{
			PanelSlot->SetAnchors(FAnchors(0.5f, 0.45f, 0.5f, 0.45f));
			PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			PanelSlot->SetPosition(FVector2D::ZeroVector);
			PanelSlot->SetSize(FVector2D(PanelWidth, 0.f));
			PanelSlot->SetAutoSize(true);
		}

		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChapterColumn"));
		Panel->SetContent(Column);

		const auto AddLine = [&](TObjectPtr<UTextBlock>& Out, const TCHAR* Name, float Bottom, EHorizontalAlignment Align)
		{
			Out = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Out->SetAutoWrapText(true);
			Out->SetJustification(Align == HAlign_Right ? ETextJustify::Right : ETextJustify::Center);
			Out->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
			if (UVerticalBoxSlot* const LineSlot = Column->AddChildToVerticalBox(Out))
			{
				LineSlot->SetHorizontalAlignment(Align);
				LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
			}
		};
		AddLine(HeadText, TEXT("ChapterHead"), 8.f, HAlign_Center);
		AddLine(TitleText, TEXT("ChapterTitle"), 14.f, HAlign_Center);
		AddLine(RewardText, TEXT("ChapterRewardLine"), 14.f, HAlign_Center);
		AddLine(HintText, TEXT("ChapterHint"), 0.f, HAlign_Right);
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (HeadText != nullptr) { SnareUiStyle::SetFontSize(*HeadText, HeadFontSize); }
	if (TitleText != nullptr) { SnareUiStyle::SetFontSize(*TitleText, TitleFontSize); }
	if (RewardText != nullptr) { SnareUiStyle::SetFontSize(*RewardText, RewardFontSize); }
	if (HintText != nullptr) { SnareUiStyle::SetFontSize(*HintText, HintFontSize); }
	Refresh();
	return Result;
}

#undef LOCTEXT_NAMESPACE
