#include "UI/ChestScreenWidget.h"

#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/ChestLootRules.h"
#include "UI/SnareUiStyle.h"
#include "Ranch/RanchSyncComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡 — 화면 어디에 얼마나 크게). 상점 화면(`UShopScreenWidget`)과 같은 틀을 쓴다.
	constexpr float PanelWidth = 760.f;
	constexpr float PanelHeight = 940.f;
	constexpr float PanelTop = 80.f;
	constexpr float PanelPadding = 24.f;
	/** 판 모서리 — 배낭·상점·가챠 판과 같은 둥글기(M25-05, 시트 `inventory_backpack` 판 규격). */
	constexpr float PanelRadius = 16.f;
	constexpr float LineGap = 8.f;
	/** 10-16 80 → 121 (애플 44pt, DESIGN 2). 단추 둘이 82 커진 만큼 목록을 줄여 판 높이 940 을 지킨다. */
	constexpr float ButtonHeight = SnareUiStyle::MenuTouchMin;
	constexpr float ListHeight = 538.f;
	constexpr float CardWidth = 280.f;
	constexpr float CardHeight = 112.f;
	/** 카드는 화면 가운데(내 캐릭터) 옆 — 상인과 같은 자리. 둘째 카드는 그 아래 한 칸. */
	constexpr float CardOffsetX = 260.f;
}

UChestScreenWidget::UChestScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UDataTable> FailureTableAsset(URanchSyncComponent::GetFailureTablePath());
	if (FailureTableAsset.Succeeded())
	{
		FailureTable = FailureTableAsset.Object;
	}
}

void UChestScreenWidget::BindToChest(UBackpackComponent* InBackpack, int32 InChestIndex, const FString& InTreasureID)
{
	Backpack = InBackpack;
	ChestIndex = InChestIndex;
	TreasureID = InTreasureID;
	RefreshDisplay();
}

void UChestScreenWidget::BindToVault(UBackpackComponent* InBackpack, int32 InRuinIndex)
{
	Backpack = InBackpack;
	VaultRuinIndex = InRuinIndex;
	ChestIndex = 0;
	TreasureID.Empty();
	RefreshDisplay();
}

FText UChestScreenWidget::FormatVaultWait(int32 Seconds)
{
	const int32 Minutes = FMath::DivideAndRoundUp(FMath::Max(Seconds, 0), 60);
	return FText::Format(LOCTEXT("VaultWait", "{0}시간 {1}분"), FText::AsNumber(Minutes / 60), FText::AsNumber(Minutes % 60));
}

FText UChestScreenWidget::FindItemDisplayName(FName ItemID) const
{
	// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7).
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, ItemID);
	return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromName(ItemID);
}

TArray<FText> UChestScreenWidget::BuildDisclosureTexts() const
{
	TArray<FText> Lines;
	const TConstArrayView<SnareChestLoot::FDisclosureLine> Table =
		IsVault() ? SnareChestLoot::GetVaultDisclosureLines() : SnareChestLoot::GetDisclosureLines();
	for (const SnareChestLoot::FDisclosureLine& Line : Table)
	{
		Lines.Add(FText::Format(LOCTEXT("ChestDisclosureLine", "{0}  {1}%"),
			FindItemDisplayName(Line.ItemID), FText::AsNumber(FMath::RoundToInt(Line.Chance * 100.f))));
	}
	const SnareChestLoot::FDisclosureLine Blueprint = IsVault() ? SnareChestLoot::GetVaultBlueprintLine() : SnareChestLoot::GetBlueprintLine();
	Lines.Add(FText::Format(LOCTEXT("ChestDisclosureBlueprint", "설계도 (아직 없는 것 중 하나)  {0}%"),
		FText::AsNumber(FMath::RoundToInt(Blueprint.Chance * 100.f))));
	return Lines;
}

bool UChestScreenWidget::PressOpen()
{
	SetOpen(true);
	SetDisclosureShown(false);
	if (bPending || !IsValid(Backpack))
	{
		return false;
	}
	// 대기는 부르기 전에 세운다 — 서버가 같은 호출 안에서 답하면(로그인 전 등) 그 답이 대기를 푼다.
	bPending = true;
	ResultText = FText::GetEmpty();
	const TWeakObjectPtr<UChestScreenWidget> WeakThis(this);
	const UBackpackComponent::FChestDone OnDone =
		[WeakThis](bool bAccepted, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Response)
		{
			if (UChestScreenWidget* const Self = WeakThis.Get())
			{
				Self->ApplyReply(bAccepted, Code, Message, Response);
			}
		};
	const bool bSent = IsVault() ? Backpack->RequestOpenVault(VaultRuinIndex, OnDone)
		: Backpack->RequestOpenChest(ChestIndex, TreasureID, OnDone);
	if (!bSent)
	{
		bPending = false;
	}
	else if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwningPlayerPawn()))
	{
		// 뚜껑을 여는 손 몸짓 (INBOX 2026-10-15 감사 (e)) — 겉모습뿐, 상자 안 물건은 서버 답 뒤.
		Body->PlayItemAction(EItemAction::Gather);
	}
	RefreshDisplay();
	return bSent;
}

void UChestScreenWidget::ApplyReply(bool bAccepted, const FString& Code, const FString& Message,
	const TSharedPtr<FJsonObject>& Response)
{
	bPending = false;
	if (!bAccepted)
	{
		const int32 WaitSeconds = SnareChestLoot::ParseWaitSeconds(Response);
		if (IsVault() && Code == TEXT("no_key"))
		{
			ResultText = LOCTEXT("VaultNoKey", "금고 열쇠패가 있어야 연다");
		}
		else if (IsVault() && Code == TEXT("recharging") && WaitSeconds >= 0)
		{
			ResultText = FText::Format(LOCTEXT("VaultRecharging", "다시 차기까지 {0}"), FormatVaultWait(WaitSeconds));
		}
		else
		{
			ResultText = Code == TEXT("recharging") && WaitSeconds >= 0
				? FText::Format(LOCTEXT("ChestRecharging", "다시 차기까지 {0}"), FText::FromString(SnareChestLoot::FormatClock(WaitSeconds)))
				: URanchSyncComponent::DescribeFailureCode(FailureTable, TEXT("OpenChest"), Code, Message, Response, false);
		}
		RefreshDisplay();
		return;
	}

	const SnareChestLoot::FOpenResult Result = SnareChestLoot::ParseOpenChest(Response);
	TArray<FString> Lines;
	Lines.Add(FText::Format(LOCTEXT("ChestRolls", "꺼낸 것 (굴림 {0}번)"), FText::AsNumber(Result.Rolls)).ToString());
	for (const FInventorySlot& InvSlot : Result.Items)
	{
		Lines.Add(FText::Format(LOCTEXT("ChestItemLine", "{0} x{1}"), FindItemDisplayName(InvSlot.ItemID),
			FText::AsNumber(InvSlot.Count)).ToString());
	}
	for (const FName Blueprint : Result.Blueprints)
	{
		Lines.Add(FText::Format(LOCTEXT("ChestBlueprintLine", "설계도 - {0}"), FindItemDisplayName(Blueprint)).ToString());
	}
	if (Result.bDropped)
	{
		Lines.Add(LOCTEXT("ChestDropped", "가방이 가득 차 발밑에 두었다").ToString());
	}
	ResultText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	RefreshDisplay();
}

void UChestScreenWidget::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	RefreshDisplay();
}

void UChestScreenWidget::SetDisclosureShown(bool bInShown)
{
	bDisclosureShown = bInShown;
	RefreshDisplay();
}

void UChestScreenWidget::HandleOpenClicked()
{
	PressOpen();
}

void UChestScreenWidget::HandleDisclosureClicked()
{
	SetOpen(true);
	SetDisclosureShown(!bDisclosureShown);
}

void UChestScreenWidget::HandleCloseClicked()
{
	SetOpen(false);
}

void UChestScreenWidget::RefreshDisplay()
{
	if (IsValid(OpenButton))
	{
		OpenButton->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		// 답을 기다리는 동안 다시 못 누른다 — 두 번 물어 두 번 굴리는 일이 없게.
		OpenButton->SetIsEnabled(!bPending);
	}
	if (IsValid(DisclosureCardButton))
	{
		DisclosureCardButton->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(ChestPanel))
	{
		ChestPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(ChestTitlePillBox))
	{
		// 알약은 판과 같이 접힌다 — 닫힌 상자 옆 카드 위에 제목만 떠 있지 않게. 눌림은 안 먹는다.
		ChestTitlePillBox->SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TitleText))
	{
		TitleText->SetText(bDisclosureShown ? LOCTEXT("ChestDisclosureTitle", "확률 고지 (한 번 굴릴 때)")
			: IsVault() ? LOCTEXT("VaultTitle", "잠긴 금고")
			: TreasureID.IsEmpty() ? LOCTEXT("ChestTitle", "상자") : LOCTEXT("ChestTreasureTitle", "보물 상자"));
	}
	if (IsValid(ResultBody))
	{
		ResultBody->SetText(bPending ? (IsVault() ? LOCTEXT("VaultPending", "금고 여는 중...") : LOCTEXT("ChestPending", "상자 여는 중...")) : ResultText);
		ResultBody->SetVisibility(bDisclosureShown ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(DisclosureButtonText))
	{
		DisclosureButtonText->SetText(bDisclosureShown ? LOCTEXT("ChestShowResult", "결과 보기") : LOCTEXT("ChestShowDisclosure", "확률 보기"));
	}
	if (!IsValid(DisclosureList) || WidgetTree == nullptr)
	{
		return;
	}
	DisclosureList->SetVisibility(bDisclosureShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (DisclosureList->GetChildrenCount() > 0)
	{
		return;
	}
	// 고지 줄은 코드 값이라 바뀌지 않는다 — 한 번만 세운다.
	int32 Index = 0;
	for (const FText& Line : BuildDisclosureTexts())
	{
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ChestDisclosureText_%d"), Index++)));
		Text->SetText(Line);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		if (UVerticalBoxSlot* const LineSlot = Cast<UVerticalBoxSlot>(DisclosureList->AddChild(Text)))
		{
			LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
		}
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

TSharedRef<SWidget> UChestScreenWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}
	RefreshDisplay();

	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

bool UChestScreenWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}
	if (WidgetTree->RootWidget == nullptr)
	{
		return true;
	}
	const UPanelWidget* const RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return RootPanel != nullptr && RootPanel->GetChildrenCount() == 0;
}

void UChestScreenWidget::BuildPlaceholderTree()
{
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ChestScreenRoot"));
		WidgetTree->RootWidget = Root;
	}

	auto MakeButton = [this](FName Name, const FText& Label, UTextBlock** OutText = nullptr) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		// M26-05 시트 「모든 패널 모서리 둥글게」 — 색은 뒤의 `ApplyUiStyle` 몫이라 모양만.
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Text"))));
		Text->SetText(Label);
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
		if (OutText != nullptr)
		{
			*OutText = Text;
		}
		return Button;
	};
	auto MakeText = [this](FName Name) -> UTextBlock*
	{
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		// 크림 바탕 위라 기본 흰 글자는 안 보인다 — 팔레트의 진한 갈색.
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		return Text;
	};
	auto PlaceCard = [Root](UButton* Card, float Y)
	{
		if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Card)))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CardSlot->SetAlignment(FVector2D(0.f, 0.5f));
			CardSlot->SetPosition(FVector2D(CardOffsetX, Y));
			CardSlot->SetSize(FVector2D(CardWidth, CardHeight));
		}
	};

	// 닫힌 상태: 상자 옆 카드 둘 — 열기, 확률 보기(열기 전에도 고지를 본다).
	OpenButton = MakeButton(TEXT("OpenButton"), LOCTEXT("ChestOpen", "상자 열기"));
	OpenButton->OnClicked.AddDynamic(this, &UChestScreenWidget::HandleOpenClicked);
	PlaceCard(OpenButton, 0.f);
	DisclosureCardButton = MakeButton(TEXT("DisclosureCardButton"), LOCTEXT("ChestDisclosureCard", "확률 보기"));
	DisclosureCardButton->OnClicked.AddDynamic(this, &UChestScreenWidget::HandleDisclosureClicked);
	PlaceCard(DisclosureCardButton, CardHeight + LineGap);

	// 열린 상태: 크림 둥근 판 — 결과 또는 고지 표 / 고지 토글 / 닫기. 제목은 판 위로 걸친 알약(M25-05).
	ChestPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChestPanel"));
	ChestPanel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), PanelRadius));
	ChestPanel->SetPadding(FMargin(PanelPadding));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(ChestPanel)))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.f));
		PanelSlot->SetPosition(FVector2D(0.f, PanelTop));
		PanelSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
	}

	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChestColumn"));
	ChestPanel->SetContent(Column);

	// 제목 탭 — 배낭·도감·지도 판과 같은 진갈색 알약을 판 왼쪽 위 모서리에 걸친다. 판 뒤에 넣어야 판 위에 그려진다.
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	ChestTitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("ChestTitlePillBox"), TEXT("ChestTitlePill"), *TitleText);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(ChestTitlePillBox)))
	{
		PillSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		PillSlot->SetAlignment(FVector2D::ZeroVector);
		PillSlot->SetPosition(FVector2D(-PanelWidth * 0.5f, PanelTop - SnareUiStyle::MenuTitlePillRise));
		PillSlot->SetAutoSize(true);
	}

	// 결과와 고지 표는 같은 자리를 번갈아 쓴다 — 고지 16줄이 패널 밖으로 안 넘치게 끌어 내린다.
	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ChestScroll"));
	UVerticalBox* const Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChestBody"));
	ResultBody = MakeText(TEXT("ResultBody"));
	ResultBody->SetAutoWrapText(true);
	Body->AddChild(ResultBody);
	DisclosureList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DisclosureList"));
	Body->AddChild(DisclosureList);
	Scroll->AddChild(Body);
	USizeBox* const ListBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ChestListBox"));
	ListBox->SetHeightOverride(ListHeight);
	ListBox->AddChild(Scroll);
	if (UVerticalBoxSlot* const ListSlot = Cast<UVerticalBoxSlot>(Column->AddChild(ListBox)))
	{
		ListSlot->SetPadding(FMargin(0.f, LineGap, 0.f, LineGap));
	}

	UTextBlock* ToggleText = nullptr;
	DisclosureButton = MakeButton(TEXT("DisclosureButton"), LOCTEXT("ChestShowDisclosure", "확률 보기"), &ToggleText);
	DisclosureButtonText = ToggleText;
	DisclosureButton->OnClicked.AddDynamic(this, &UChestScreenWidget::HandleDisclosureClicked);
	CloseButton = MakeButton(TEXT("CloseButton"), LOCTEXT("ChestClose", "닫기"));
	CloseButton->OnClicked.AddDynamic(this, &UChestScreenWidget::HandleCloseClicked);
	for (UButton* const Button : {DisclosureButton.Get(), CloseButton.Get()})
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*(Button->GetName() + TEXT("Box"))));
		Box->SetHeightOverride(ButtonHeight);
		Box->AddChild(Button);
		if (UVerticalBoxSlot* const ButtonSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Box)))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
		}
	}
}

#undef LOCTEXT_NAMESPACE
