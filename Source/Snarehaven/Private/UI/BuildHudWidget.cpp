#include "UI/BuildHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Data/ItemRow.h"
#include "Engine/GameInstance.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy 🟡 — 화면 자리·크기).
	// 제작 버튼은 없다 — 제작은 배낭 창 왼쪽 탭에서 연다(M15-02, `UBackpackScreenWidget::PressCraftTab`).
	constexpr float BarBottomOffset = -48.f;
	constexpr float ButtonPaddingX = 32.f;
	/** 모바일 터치 버튼 최소 높이 (CraftingScreenWidget 과 같은 값). */
	constexpr float TouchButtonHeight = 88.f;
	constexpr float BarGap = 16.f;
}

void UBuildHudWidget::BindToBuild(USnareBaseBuildComponent* InBuild)
{
	if (Build.IsValid())
	{
		Build->OnPlacementChanged.Remove(PlacementChangedHandle);
	}
	Build = InBuild;
	if (Build.IsValid())
	{
		PlacementChangedHandle = Build->OnPlacementChanged.AddUObject(this, &UBuildHudWidget::RefreshPlacementBar);
	}
	RefreshPlacementBar();
}

void UBuildHudWidget::RefreshPlacementBar()
{
	if (!IsValid(PlacementBar) || !IsValid(VerdictText) || !IsValid(PinBox))
	{
		return;
	}

	const bool bPlacing = Build.IsValid() && Build->IsPlacing();
	PlacementBar->SetVisibility(bPlacing ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bPlacing)
	{
		return;
	}

	const bool bWall = Build->IsPlacingWallDecor();
	const bool bTop = Build->IsPlacingTopDecor();
	switch (Build->GetPlacementVerdict())
	{
	case ESnarePlacementVerdict::Green:
		VerdictText->SetText(LOCTEXT("PlaceGreen", "놓을 수 있음"));
		VerdictText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		break;
	case ESnarePlacementVerdict::Red:
		VerdictText->SetText(bWall ? LOCTEXT("PlaceRedWall", "이미 걸려 있음 - 높이를 바꾸세요")
			: bTop ? LOCTEXT("PlaceRedTop", "이미 올려져 있음 - 자리를 바꾸세요")
			: LOCTEXT("PlaceRed", "다른 것과 겹침"));
		VerdictText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiWarnRed()));
		break;
	default:
		VerdictText->SetText(bWall ? LOCTEXT("PlaceBlueWall", "집 벽에만 걸 수 있음")
			: bTop ? LOCTEXT("PlaceBlueTop", "탁자·보관함 위에만 올릴 수 있음") : LOCTEXT("PlaceBlue", "내 땅 밖"));
		VerdictText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		break;
	}

	if (IsValid(RotateText))
	{
		// 소품은 돌림이 받침 것이라 「돌리기」 자리가 슬롯 고르기 — 받침이 없으면 「자리 -」.
		RotateText->SetText(bWall ? FText::Format(LOCTEXT("HudWallHeight", "높이 {0}"), Build->GetPlacementWallHeight())
			: bTop ? (Build->GetPlacementTopSlot() >= 0
				? FText::Format(LOCTEXT("HudTopSlot", "자리 {0}/{1}"), Build->GetPlacementTopSlot() + 1, Build->GetPlacementTopSlotCount())
				: LOCTEXT("HudTopSlotNone", "자리 -"))
			: LOCTEXT("HudRotate", "돌리기"));
	}
	if (IsValid(FineButton))
	{
		FineButton->SetVisibility(bWall || bTop ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(FineText))
	{
		FineText->SetText(Build->IsFinePlacement() ? LOCTEXT("HudFineOn", "세밀 켜짐 15°") : LOCTEXT("HudFineOff", "세밀 꺼짐"));
	}

	// 보관함일 때만 PIN 칸을 보인다 (서버 `PlaceStructure {pin}`).
	const UGameInstance* const GameInstance = GetGameInstance();
	const USnareBaseSubsystem* const Base = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBaseSubsystem>() : nullptr;
	const FItemRow* const Row = Base != nullptr ? Base->FindItemRow(Build->GetPlacingItemID()) : nullptr;
	PinBox->SetVisibility(Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Storage
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UBuildHudWidget::HandleRotateClicked()
{
	if (Build.IsValid())
	{
		Build->RotatePlacement();
	}
}

void UBuildHudWidget::HandleFineClicked()
{
	if (Build.IsValid())
	{
		Build->ToggleFinePlacement();
	}
}

void UBuildHudWidget::HandleConfirmClicked()
{
	if (Build.IsValid())
	{
		Build->ConfirmPlacement(PinBox->GetVisibility() == ESlateVisibility::Visible ? PinBox->GetText().ToString() : FString());
	}
}

void UBuildHudWidget::HandleCancelClicked()
{
	if (Build.IsValid())
	{
		Build->CancelPlacement();
	}
}

TSharedRef<SWidget> UBuildHudWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	RefreshPlacementBar();
	return Built;
}

void UBuildHudWidget::NativeDestruct()
{
	if (Build.IsValid())
	{
		Build->OnPlacementChanged.Remove(PlacementChangedHandle);
	}
	Super::NativeDestruct();
}

bool UBuildHudWidget::NeedsPlaceholderTree() const
{
	return WidgetTree != nullptr && WidgetTree->RootWidget == nullptr;
}

UButton* UBuildHudWidget::MakeButton(const FName& Name, const FText& Label)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*(Name.ToString() + TEXT("Text"))));
	Text->SetText(Label);
	Button->AddChild(Text);
	FButtonStyle Style = Button->GetStyle();
	Style.NormalPadding = FMargin(ButtonPaddingX, (TouchButtonHeight - 40.f) * 0.5f);
	Style.PressedPadding = Style.NormalPadding;
	Button->SetStyle(Style);
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
	return Button;
}

void UBuildHudWidget::BuildPlaceholderTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BuildHudRoot"));
	WidgetTree->RootWidget = Root;

	PlacementBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BuildHudBar"));
	if (UCanvasPanelSlot* const CanvasSlot = Root->AddChildToCanvas(PlacementBar))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CanvasSlot->SetPosition(FVector2D(0.f, BarBottomOffset));
		CanvasSlot->SetAutoSize(true);
	}

	VerdictText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BuildHudVerdict"));
	PinBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("BuildHudPin"));
	PinBox->SetHintText(LOCTEXT("HudPinHint", "PIN 5자리"));
	PinBox->KeyboardType = EVirtualKeyboardType::Number;

	UButton* const Rotate = MakeButton(TEXT("BuildHudRotate"), LOCTEXT("HudRotate", "돌리기"));
	Rotate->OnClicked.AddDynamic(this, &UBuildHudWidget::HandleRotateClicked);
	UButton* const Fine = MakeButton(TEXT("BuildHudFine"), LOCTEXT("HudFineOff", "세밀 꺼짐"));
	Fine->OnClicked.AddDynamic(this, &UBuildHudWidget::HandleFineClicked);
	FineText = Cast<UTextBlock>(Fine->GetChildAt(0));
	FineButton = Fine;
	RotateText = Cast<UTextBlock>(Rotate->GetChildAt(0));
	UButton* const Confirm = MakeButton(TEXT("BuildHudConfirm"), LOCTEXT("HudConfirm", "놓기"));
	Confirm->OnClicked.AddDynamic(this, &UBuildHudWidget::HandleConfirmClicked);
	UButton* const Cancel = MakeButton(TEXT("BuildHudCancel"), LOCTEXT("HudCancel", "취소"));
	Cancel->OnClicked.AddDynamic(this, &UBuildHudWidget::HandleCancelClicked);

	// M97-c-5: 판정 글은 땅 위 맨글자였다 — 밧줄 카드 안에 넣는다(`MakeCreamCard` 는 접힌 채 시작이라 보이게 돌린다).
	UBorder* const VerdictCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("BuildHudVerdictCard"));
	VerdictCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	VerdictCard->SetContent(VerdictText);

	for (UWidget* const Child : TArray<UWidget*>{ VerdictCard, PinBox, Rotate, Fine, Confirm, Cancel })
	{
		if (UHorizontalBoxSlot* const BoxSlot = PlacementBar->AddChildToHorizontalBox(Child))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, BarGap, 0.f));
			BoxSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
}

#undef LOCTEXT_NAMESPACE
