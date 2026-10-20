#include "UI/SnareGuideBookWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRowLookup.h"
#include "Data/UsageHintRow.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareUsageHints.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡 — 화면 어디에 얼마나 크게). 판 자체는 도감과 같은 `AddScreenPanel`.
	constexpr float PanelPadding = 32.f;
	constexpr float ListWidth = 300.f;          // 가장 긴 쪽 제목(「사냥·포획」 류 5~6자)이 한 줄에 들어갈 폭
	constexpr float ListButtonHeight = SnareUiStyle::MenuTouchMin;    // 10-16 72 → 121 — 애플 44pt (DESIGN 2, 넘치는 쪽은 스크롤)
	constexpr float ListGap = 8.f;
	constexpr float ColumnGap = 40.f;
	constexpr float IconSize = 160.f;
	constexpr float PageTitleFontSize = 34.f;
	constexpr float StepFontSize = 26.f;
	constexpr float StepGap = 14.f;
	constexpr float CloseWidth = 220.f;
	constexpr float CloseHeight = SnareUiStyle::MenuTouchMin;   // 10-16 72 → 121

	/** 선택된 쪽 강조 — `ApplyUiStyle` 이 모든 버튼을 기본색으로 칠한 **뒤에** 부른다(도감 탭과 같은 꼴). */
	void MarkSelected(UButton* Button, bool bSelected)
	{
		if (!IsValid(Button))
		{
			return;
		}
		SnareUiStyle::MarkSelectedTab(Button, bSelected);
	}
}

USnareGuideBookWidget::USnareGuideBookWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

USnareGuideBookWidget* USnareGuideBookWidget::OpenOnViewport(APlayerController* Owner, UWorld* World)
{
	USnareGuideBookWidget* const Screen = IsValid(Owner) ? CreateWidget<USnareGuideBookWidget>(Owner, StaticClass())
		: IsValid(World) ? CreateWidget<USnareGuideBookWidget>(World, StaticClass()) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	// 전체 화면 판이라 HUD(0)·제작 판(1)보다 위 — 도감과 같은 Z.
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	return Screen;
}

void USnareGuideBookWidget::SelectPage(int32 Index)
{
	if (!PageKeys.IsValidIndex(Index))
	{
		return;
	}
	SelectedPage = Index;
	RefreshPage();
}

FText USnareGuideBookWidget::GetShownTitle() const
{
	return IsValid(PageTitleText) ? PageTitleText->GetText() : FText::GetEmpty();
}

TArray<FText> USnareGuideBookWidget::GetShownSteps() const
{
	TArray<FText> Steps;
	if (IsValid(StepList))
	{
		for (int32 Index = 0; Index < StepList->GetChildrenCount(); ++Index)
		{
			if (const UTextBlock* const Text = Cast<UTextBlock>(StepList->GetChildAt(Index)))
			{
				Steps.Add(Text->GetText());
			}
		}
	}
	return Steps;
}

bool USnareGuideBookWidget::HasShownIcon() const
{
	return IsValid(PageIcon) && PageIcon->GetVisibility() != ESlateVisibility::Collapsed;
}

void USnareGuideBookWidget::Close()
{
	RemoveFromParent();
}

void USnareGuideBookWidget::HandleCloseClicked()
{
	Close();
}

void USnareGuideBookWidget::RefreshPage()
{
	const UDataTable* const Table = SnareUsageHints::LoadTable();
	const FUsageHintRow* const Row = PageKeys.IsValidIndex(SelectedPage)
		? SnareUsageHints::FindRow(Table, PageKeys[SelectedPage]) : nullptr;

	if (IsValid(PageTitleText))
	{
		PageTitleText->SetText(Row != nullptr ? Row->GuideTitle : FText::GetEmpty());
	}
	if (IsValid(PageIcon))
	{
		const FItemRow* const Item = Row != nullptr ? SnareItemRows::FindByItemID(ItemTable, Row->GuideIconItemID) : nullptr;
		const bool bHasIcon = Item != nullptr && !Item->IconRef.IsNull();
		if (bHasIcon)
		{
			PageIcon->SetBrushFromSoftTexture(Item->IconRef);
		}
		PageIcon->SetVisibility(bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(StepList) && WidgetTree != nullptr)
	{
		StepList->ClearChildren();
		if (Row != nullptr)
		{
			for (int32 Index = 0; Index < Row->GuideSteps.Num(); ++Index)
			{
				UTextBlock* const Step = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
					FName(*FString::Printf(TEXT("GuideStepText_%d"), Index)));
				Step->SetText(FText::Format(LOCTEXT("GuideStep", "{0}. {1}"), FText::AsNumber(Index + 1), Row->GuideSteps[Index]));
				Step->SetAutoWrapText(true);
				Step->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor()));
				SnareUiStyle::SetFontSize(*Step, StepFontSize);
				if (UVerticalBoxSlot* const StepSlot = Cast<UVerticalBoxSlot>(StepList->AddChild(Step)))
				{
					StepSlot->SetPadding(FMargin(0.f, 0.f, 0.f, StepGap));
				}
			}
		}
	}

	// 순서 줄은 화면이 뜬 **뒤에** 새로 생긴다 — 글꼴을 다시 입히고 고른 쪽만 강조(도감 `RefreshDisplay` 와 같은 순서).
	SnareUiStyle::ApplyUiStyle(*this);
	for (int32 Index = 0; Index < PageButtons.Num(); ++Index)
	{
		MarkSelected(PageButtons[Index], Index == SelectedPage);
	}
}

TSharedRef<SWidget> USnareGuideBookWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	// `Super` **뒤**라야 Slate 위젯까지 글꼴·색이 간다.
	RefreshPage();
	return Built;
}

void USnareGuideBookWidget::BuildTree()
{
	PageKeys = SnareUsageHints::GetGuidePageKeys(SnareUsageHints::LoadTable());
	SelectedPage = 0;

	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GuideBookRoot"));
	WidgetTree->RootWidget = Root;

	// 판 = 도감·업적과 같은 전체 화면 크림 둥근 판(바깥 어둡게 + 눌림을 먹어 뒤 HUD 가 안 눌림).
	UBorder* const Panel = SnareUiStyle::AddScreenPanel(*WidgetTree, *Root, TEXT("GuideBookBackdrop"), TEXT("GuideBookPanel"));
	Panel->SetPadding(FMargin(PanelPadding));

	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuideBookColumn"));
	Panel->SetContent(Column);

	// 맨 위 줄: 제목 알약 「안내서」 + 오른쪽 닫기 — 배낭·제작 판과 같은 진갈색 알약.
	UHorizontalBox* const Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GuideBookHeader"));
	UTextBlock* const Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GuideBookTitle"));
	Title->SetText(LOCTEXT("GuideBookTitle", "안내서"));
	if (UHorizontalBoxSlot* const PillSlot = Cast<UHorizontalBoxSlot>(Header->AddChild(
		SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("GuideBookTitlePillBox"), TEXT("GuideBookTitlePill"), *Title))))
	{
		PillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PillSlot->SetHorizontalAlignment(HAlign_Left);
	}
	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("GuideBookCloseButton"));
	SnareUiStyle::MakeRoundButton(*CloseButton, SnareUiStyle::HudCornerRadius);
	UTextBlock* const CloseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GuideBookCloseText"));
	CloseText->SetText(LOCTEXT("GuideBookClose", "닫기"));
	CloseText->SetJustification(ETextJustify::Center);
	CloseButton->AddChild(CloseText);
	CloseButton->OnClicked.AddDynamic(this, &USnareGuideBookWidget::HandleCloseClicked);
	USizeBox* const CloseBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GuideBookCloseBox"));
	CloseBox->SetWidthOverride(CloseWidth);
	CloseBox->SetHeightOverride(CloseHeight);
	CloseBox->AddChild(CloseButton);
	Header->AddChild(CloseBox);
	if (UVerticalBoxSlot* const HeaderSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Header)))
	{
		HeaderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ListGap * 2.f));
	}

	UHorizontalBox* const Body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GuideBookBody"));
	if (UVerticalBoxSlot* const BodySlot = Cast<UVerticalBoxSlot>(Column->AddChild(Body)))
	{
		BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// 왼쪽: 활동 목록 — 쪽 11개가 작은 화면에서 넘치면 끌어 내린다(스크롤 안 버튼은 `ApplyUiStyle` 이 정확한 탭으로).
	USizeBox* const ListBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GuideBookListBox"));
	ListBox->SetWidthOverride(ListWidth);
	UScrollBox* const ListScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("GuideBookListScroll"));
	ListBox->AddChild(ListScroll);
	Body->AddChild(ListBox);

	const UDataTable* const Table = SnareUsageHints::LoadTable();
	PageButtons.Reset();
	PageProxies.Reset();
	for (int32 Index = 0; Index < PageKeys.Num(); ++Index)
	{
		const FUsageHintRow* const Row = SnareUsageHints::FindRow(Table, PageKeys[Index]);
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("GuidePageButton_%d"), Index)));
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("GuidePageButtonText_%d"), Index)));
		Label->SetText(Row != nullptr ? Row->GuideTitle : FText::FromName(PageKeys[Index]));
		Label->SetJustification(ETextJustify::Center);
		Button->AddChild(Label);

		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->Key = PageKeys[Index];
		const TWeakObjectPtr<USnareGuideBookWidget> WeakThis(this);
		Proxy->OnClicked = [WeakThis](FName Key)
		{
			if (USnareGuideBookWidget* const Self = WeakThis.Get())
			{
				Self->SelectPage(Self->PageKeys.IndexOfByKey(Key));
			}
		};
		Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		PageButtons.Add(Button);
		PageProxies.Add(Proxy);

		USizeBox* const ButtonBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("GuidePageButtonBox_%d"), Index)));
		ButtonBox->SetHeightOverride(ListButtonHeight);
		ButtonBox->AddChild(Button);
		if (UScrollBoxSlot* const ButtonSlot = Cast<UScrollBoxSlot>(ListScroll->AddChild(ButtonBox)))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ListGap));
		}
	}

	// 오른쪽: 그림 1 + 쪽 제목 + 순서 줄.
	UVerticalBox* const PageColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuidePageColumn"));
	if (UHorizontalBoxSlot* const PageSlot = Cast<UHorizontalBoxSlot>(Body->AddChild(PageColumn)))
	{
		PageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PageSlot->SetPadding(FMargin(ColumnGap, 0.f, 0.f, 0.f));
	}
	USizeBox* const IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GuidePageIconBox"));
	IconBox->SetWidthOverride(IconSize);
	IconBox->SetHeightOverride(IconSize);
	PageIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GuidePageIcon"));
	IconBox->AddChild(PageIcon);
	if (UVerticalBoxSlot* const IconSlot = Cast<UVerticalBoxSlot>(PageColumn->AddChild(IconBox)))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Left);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 0.f, StepGap));
	}
	PageTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GuidePageTitleText"));
	PageTitleText->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor()));
	SnareUiStyle::SetFontSize(*PageTitleText, PageTitleFontSize);
	if (UVerticalBoxSlot* const TitleSlot = Cast<UVerticalBoxSlot>(PageColumn->AddChild(PageTitleText)))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, StepGap * 2.f));
	}
	StepList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuideStepList"));
	PageColumn->AddChild(StepList);
}

#undef LOCTEXT_NAMESPACE
