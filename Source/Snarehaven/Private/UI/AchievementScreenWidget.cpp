#include "UI/AchievementScreenWidget.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/AchievementRow.h"
#include "Engine/DataTable.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡 — 화면 자리 숫자). 도감 화면과 같은 가로 한가운데 한 줄 배치다.
	constexpr float PlaceholderTitleTop = 100.f;
	constexpr float PlaceholderProgressTop = 164.f;
	constexpr float PlaceholderListTop = 240.f;
	constexpr float PlaceholderRowHeight = 56.f;
	constexpr float PlaceholderPanelWidth = 640.f;
	/** 13줄 × 56px ≈ 730px — 1117px 캡처 안에 든다. */
	constexpr float PlaceholderListHeight = 14.f * PlaceholderRowHeight;
	constexpr float PlaceholderCloseWidth = 160.f;

	/** 줄 왼쪽 달성 원판 지름 (M25-03, DESIGN 8.12 「🟡 업적 줄 원판」) — 제작 판 재료 원판 32(M23-04)와 같은 값. */
	constexpr float LineDiscSize = 32.f;
	/** 원판과 글 사이 틈 (DESIGN 8.12 같은 줄). */
	constexpr float LineDiscGap = 12.f;
}

UAchievementScreenWidget::UAchievementScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> AchievementTableAsset(
		TEXT("/Game/Game/Data/Achievements/DT_AchievementTable.DT_AchievementTable"));
	if (AchievementTableAsset.Succeeded())
	{
		AchievementTable = AchievementTableAsset.Object;
	}
}

bool UAchievementScreenWidget::IsKnown() const
{
	return IsValid(WatchedBackend) && WatchedBackend->GetPlayerSnapshot().HasOwner();
}

FText UAchievementScreenWidget::GetProgressText() const
{
	const int32 Total = IsValid(AchievementTable) ? AchievementTable->GetRowMap().Num() : 0;
	int32 Done = 0;
	if (IsKnown() && IsValid(AchievementTable))
	{
		const TArray<FName>& Completed = WatchedBackend->GetPlayerSnapshot().Achievements.Completed;
		for (const TPair<FName, uint8*>& Pair : AchievementTable->GetRowMap())
		{
			const FAchievementRow* const Row = reinterpret_cast<const FAchievementRow*>(Pair.Value);
			Done += (Row != nullptr && Completed.Contains(Row->AchievementID)) ? 1 : 0;
		}
	}
	return FText::Format(LOCTEXT("AchievementProgress", "업적 {0} / {1}"),
		IsKnown() ? FText::AsNumber(Done) : LOCTEXT("AchievementUnknown", "-"), FText::AsNumber(Total));
}

TArray<FAchievementLineView> UAchievementScreenWidget::BuildLines() const
{
	TArray<FAchievementLineView> Lines;
	if (!IsValid(AchievementTable))
	{
		return Lines;
	}

	const bool bKnown = IsKnown();
	for (const TPair<FName, uint8*>& Pair : AchievementTable->GetRowMap())
	{
		const FAchievementRow* const Row = reinterpret_cast<const FAchievementRow*>(Pair.Value);
		if (Row == nullptr)
		{
			continue;
		}
		// 달성 = 서버 `done` 에 있는가. 진행 수로 추측하지 않는다 (CLAUDE.md 8).
		const FSnareAchievementState* const State = bKnown ? &WatchedBackend->GetPlayerSnapshot().Achievements : nullptr;
		const bool bDone = State != nullptr && State->Completed.Contains(Row->AchievementID);
		const int32* const Current = State != nullptr ? State->Counters.Find(Row->Counter) : nullptr;
		const FText CurrentText = !bKnown ? LOCTEXT("AchievementCounterUnknown", "-")
			: FText::AsNumber(FMath::Min(Current != nullptr ? *Current : 0, Row->Target));
		FAchievementLineView& Line = Lines.AddDefaulted_GetRef();
		Line.Text = FText::Format(LOCTEXT("AchievementLine", "{0}  {1}/{2}"),
			Row->DisplayName, CurrentText, FText::AsNumber(Row->Target));
		Line.bDone = bDone;
	}
	return Lines;
}

void UAchievementScreenWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UAchievementScreenWidget::RefreshDisplay);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &UAchievementScreenWidget::RefreshDisplay);
	}
	RefreshDisplay();
}

void UAchievementScreenWidget::SetSourceTable(UDataTable* InAchievementTable)
{
	AchievementTable = InAchievementTable;
	RefreshDisplay();
}

void UAchievementScreenWidget::RefreshDisplay()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("AchievementTitle", "업적"));
	}
	if (IsValid(ProgressText))
	{
		ProgressText->SetText(GetProgressText());
	}
	if (!IsValid(LineList) || WidgetTree == nullptr)
	{
		return;
	}

	// 매번 다시 세운다 — 13줄이라 싸고, 표가 줄었을 때 옛 줄이 남지 않는다 (도감 화면과 같은 방식).
	LineList->ClearChildren();
	int32 Index = 0;
	for (const FAchievementLineView& Line : BuildLines())
	{
		LineList->AddChild(MakeLineRow(Line, Index++));
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

UWidget* UAchievementScreenWidget::MakeLineRow(const FAchievementLineView& Line, int32 Index)
{
	// 시트 「`[ ]` 대신 원판」(M25-03) — 도감 칸(M25-02)과 같은 원판 짜임, 달성만 민트.
	UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("AchievementDisc_%d"), Index)));
	Disc->SetBrush(FSlateRoundedBoxBrush(Line.bDone ? SnareUiStyle::UiIconMint() : SnareUiStyle::UiEmptySlot(),
		LineDiscSize * 0.5f));
	USizeBox* const DiscBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("AchievementDiscBox_%d"), Index)));
	DiscBox->SetWidthOverride(LineDiscSize);
	DiscBox->SetHeightOverride(LineDiscSize);
	DiscBox->AddChild(Disc);

	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("AchievementLine_%d"), Index)));
	Text->SetText(Line.Text);
	// 판 위 글자 색(M25-01: 달성 진갈색 · 미달 흐린 진갈색).
	Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor(!Line.bDone)));

	UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("AchievementRow_%d"), Index)));
	if (UHorizontalBoxSlot* const DiscSlot = Row->AddChildToHorizontalBox(DiscBox))
	{
		DiscSlot->SetVerticalAlignment(VAlign_Center);
		DiscSlot->SetPadding(FMargin(0.f, 0.f, LineDiscGap, 0.f));
	}
	if (UHorizontalBoxSlot* const TextSlot = Row->AddChildToHorizontalBox(Text))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Row;
}

TSharedRef<SWidget> UAchievementScreenWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildPlaceholderTree();
	}
	RefreshDisplay();
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	RefreshDisplay();
	return Built;
}

void UAchievementScreenWidget::NativeDestruct()
{
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

void UAchievementScreenWidget::BuildPlaceholderTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AchievementRoot"));
	WidgetTree->RootWidget = Root;

	// 도감과 같은 전체 화면 판(M13-31) — 글자는 전부 판 **안** 캔버스에 선다. 판 없이 월드·HUD 위에 바로 뜨면
	// 흐린 미달 줄이 풀밭 위에서 안 읽히고, 뒤 HUD 버튼이 눌린다.
	UCanvasPanel* const Content = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AchievementContent"));
	UBorder* const Panel = SnareUiStyle::AddScreenPanel(*WidgetTree, *Root, TEXT("AchievementBackdrop"), TEXT("AchievementPanel"));
	Panel->SetPadding(FMargin(0.f));
	Panel->SetContent(Content);

	// 자리 숫자는 화면 위 기준이다 — 판 안 캔버스는 판 가장자리(`ScreenPanelInset`)만큼 내려와 있으니 빼서 같은 자리에 둔다.
	auto AddCentered = [Content](UWidget* Child, float X, float Top, float Width, float Height)
	{
		if (UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Content->AddChild(Child)))
		{
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.f));
			CanvasSlot->SetPosition(FVector2D(X, Top - SnareUiStyle::ScreenPanelInset));
			CanvasSlot->SetSize(FVector2D(Width, Height));
		}
	};

	// 제목 탭(M24-04) — 도감 판과 같은 알약을 판 왼쪽 위 모서리에 걸친다. 판 안 캔버스에 두어 글자는 여전히 판의 자손이다.
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	USizeBox* const TitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("AchievementTitlePillBox"),
		TEXT("AchievementTitlePill"), *TitleText);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Content->AddChild(TitlePillBox)))
	{
		PillSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		PillSlot->SetPosition(FVector2D(0.f, -SnareUiStyle::MenuTitlePillRise));
		PillSlot->SetAutoSize(true);
	}

	ProgressText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProgressText"));
	ProgressText->SetJustification(ETextJustify::Center);
	ProgressText->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor()));
	AddCentered(ProgressText, 0.f, PlaceholderProgressTop, PlaceholderPanelWidth, PlaceholderRowHeight);

	LineList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LineList"));
	AddCentered(LineList, 0.f, PlaceholderListTop, PlaceholderPanelWidth, PlaceholderListHeight);

	// 닫기 — 제목 오른쪽. 이 화면을 걷고 연 화면(도감)을 되살린다.
	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	SnareUiStyle::MakeRoundButton(*CloseButton, SnareUiStyle::HudCornerRadius);
	UTextBlock* const CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseLabel"));
	CloseLabel->SetText(LOCTEXT("AchievementClose", "닫기"));
	CloseLabel->SetJustification(ETextJustify::Center);
	CloseButton->AddChild(CloseLabel);
	AddCentered(CloseButton, PlaceholderPanelWidth * 0.5f + PlaceholderCloseWidth, PlaceholderTitleTop,
		PlaceholderCloseWidth, SnareUiStyle::ScreenCloseButtonHeight);

	CloseProxy = NewObject<USnareClickProxy>(this);
	const TWeakObjectPtr<UAchievementScreenWidget> WeakThis(this);
	CloseProxy->OnClicked = [WeakThis](FName)
	{
		if (UAchievementScreenWidget* const Self = WeakThis.Get())
		{
			Self->Close();
		}
	};
	CloseButton->OnClicked.AddDynamic(CloseProxy, &USnareClickProxy::HandleClicked);
}

void UAchievementScreenWidget::Close()
{
	if (UUserWidget* const Back = ReturnScreen.Get())
	{
		Back->SetVisibility(ESlateVisibility::Visible);
	}
	RemoveFromParent();
}

#undef LOCTEXT_NAMESPACE
