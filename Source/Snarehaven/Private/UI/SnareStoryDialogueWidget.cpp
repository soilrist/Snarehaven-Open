#include "UI/SnareStoryDialogueWidget.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const TCHAR* StorySection = TEXT("Snarehaven.Story");
	const TCHAR* SeenStepKey = TEXT("DialogueSeenStep");

	// 화면 배치 (🟡 게임 규칙 숫자가 아니다). 아래 가운데 넓은 판.
	constexpr float PanelWidth = 900.f;
	constexpr float PanelBottom = 40.f;
	constexpr float StaminaReserve = 500.f;	// 기력 막대 판 왼쪽 끝까지 화면 오른쪽에서 잰 길이(여백 40 + 판 450) + 숨 쉴 틈
	constexpr float PanelMinWidth = 360.f;
	constexpr float SpeakerFontSize = 26.f;
	constexpr float BodyFontSize = 30.f;
	constexpr float HintFontSize = 20.f;
}

int32 USnareStoryDialogueWidget::GetSeenStep()
{
	int32 Seen = 0;
	if (GConfig != nullptr)
	{
		GConfig->GetInt(StorySection, SeenStepKey, Seen, GGameUserSettingsIni);
	}
	return Seen;
}

void USnareStoryDialogueWidget::SetSeenStep(int32 InStep)
{
	if (GConfig != nullptr)
	{
		GConfig->SetInt(StorySection, SeenStepKey, InStep, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool USnareStoryDialogueWidget::ShouldShow(int32 CurrentStep)
{
	return CurrentStep >= 1 && CurrentStep <= SnareStory::TotalSteps && CurrentStep > GetSeenStep()
		&& !SnareCheckSwitch::Param(TEXT("SnareNoGuide"));
}

bool USnareStoryDialogueWidget::ShowGoStep(int32 InStep)
{
	bPersistSeen = false;
	return ShowStep(InStep, false);
}

bool USnareStoryDialogueWidget::ShowStep(int32 InStep, bool bArrival)
{
	Lines = bArrival ? SnareStory::ArrivalDialogue(InStep) : SnareStory::StepDialogue(InStep);
	if (Lines.IsEmpty())
	{
		return false;
	}
	Step = InStep;
	LineIndex = 0;
	bFinished = false;
	Refresh();
	return true;
}

void USnareStoryDialogueWidget::Advance()
{
	if (bFinished || Lines.IsEmpty())
	{
		return;
	}
	++LineIndex;
	if (LineIndex >= Lines.Num())
	{
		Finish();
		return;
	}
	Refresh();
}

void USnareStoryDialogueWidget::Finish()
{
	bFinished = true;
	if (bPersistSeen)
	{
		SetSeenStep(FMath::Max(GetSeenStep(), Step));
	}
	RemoveFromParent();
	UE_LOG(LogSnarehaven, Log, TEXT("[Story] %d단계 대사를 끝냈다 (저장=%s)"), Step, bPersistSeen ? TEXT("예") : TEXT("아니오"));
	OnFinished.Broadcast();
}

void USnareStoryDialogueWidget::Refresh()
{
	if (!Lines.IsValidIndex(LineIndex))
	{
		return;
	}
	if (SpeakerText != nullptr)
	{
		SpeakerText->SetText(Lines[LineIndex].Speaker);
	}
	if (BodyText != nullptr)
	{
		BodyText->SetText(Lines[LineIndex].Text);
	}
	if (HintText != nullptr)
	{
		HintText->SetText(LineIndex + 1 >= Lines.Num() ? LOCTEXT("DialogueClose", "눌러서 닫기") : LOCTEXT("DialogueNext", "눌러서 계속"));
	}
}

FReply USnareStoryDialogueWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Advance();
	return FReply::Handled();
}

FReply USnareStoryDialogueWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	Advance();
	return FReply::Handled();
}

float USnareStoryDialogueWidget::FitPanelWidth(float ScreenWidth)
{
	// 가운데 정렬이라 양쪽을 같이 줄인다 — 오른쪽 끝이 화면 오른쪽에서 StaminaReserve 안쪽에 선다.
	return FMath::Clamp(ScreenWidth - 2.f * StaminaReserve, PanelMinWidth, PanelWidth);
}

void USnareStoryDialogueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	FitToScreen(MyGeometry.GetLocalSize().X);
	float ReadAt = 0.f;
	if (!bFinished && SnareCheckSwitch::Value(TEXT("SnareStoryReadAt="), ReadAt) && ReadAt > 0.f && (ShownSeconds += InDeltaTime) >= ReadAt)
	{
		// 검사 스위치: 판이 뜨고 그 초가 지나면 끝까지 넘긴다 — 사람이 눌러 닫은 것처럼 (장 끝 차례 캡처용, 루프#1428)
		UE_LOG(LogSnarehaven, Log, TEXT("[Story] 검사 스위치로 %d단계 대사를 끝까지 넘긴다 (-SnareStoryReadAt=%.0f)"), Step, ReadAt);
		while (!bFinished && !Lines.IsEmpty())
		{
			Advance();
		}
	}
}

void USnareStoryDialogueWidget::FitToScreen(float ScreenWidth)
{
	if (IsValid(PanelBox))
	{
		const float Want = FitPanelWidth(ScreenWidth);
		if (!FMath::IsNearlyEqual(PanelBox->GetWidthOverride(), Want))
		{
			PanelBox->SetWidthOverride(Want);
		}
	}
}

TSharedRef<SWidget> USnareStoryDialogueWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DialogueRoot"));
		WidgetTree->RootWidget = Root;

		UBorder* const Panel = SnareUiStyle::MakeWoodSignCard(*WidgetTree, TEXT("DialoguePanel"));
		Panel->SetPadding(FMargin(28.f, 20.f));
		Panel->SetVisibility(ESlateVisibility::HitTestInvisible);  // 표찰 카드는 기본이 접힘 — 펴되 눌림은 위젯 전체가 받는다
		PanelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DialoguePanelBox"));
		PanelBox->SetWidthOverride(PanelWidth);
		PanelBox->SetContent(Panel);
		if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(PanelBox)))
		{
			PanelSlotRef = PanelSlot;
			PanelSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
			PanelSlot->SetAlignment(FVector2D(0.5f, 1.f));
			PanelSlot->SetPosition(FVector2D(0.f, -PanelBottom));
			PanelSlot->SetSize(FVector2D(PanelWidth, 0.f));
			PanelSlot->SetAutoSize(true);
		}

		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogueColumn"));
		Panel->SetContent(Column);

		SpeakerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogueSpeaker"));
		SpeakerText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		if (UVerticalBoxSlot* const SpeakerSlot = Column->AddChildToVerticalBox(SpeakerText))
		{
			SpeakerSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		}

		BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogueBody"));
		BodyText->SetAutoWrapText(true);
		BodyText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		Column->AddChildToVerticalBox(BodyText);

		HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogueHint"));
		HintText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));
		if (UVerticalBoxSlot* const HintSlot = Column->AddChildToVerticalBox(HintText))
		{
			HintSlot->SetHorizontalAlignment(HAlign_Right);
			HintSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		}
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (SpeakerText != nullptr)
	{
		SnareUiStyle::SetFontSize(*SpeakerText, SpeakerFontSize);
	}
	if (BodyText != nullptr)
	{
		SnareUiStyle::SetFontSize(*BodyText, BodyFontSize);
	}
	if (HintText != nullptr)
	{
		SnareUiStyle::SetFontSize(*HintText, HintFontSize);
	}
	Refresh();
	return Result;
}

#undef LOCTEXT_NAMESPACE
