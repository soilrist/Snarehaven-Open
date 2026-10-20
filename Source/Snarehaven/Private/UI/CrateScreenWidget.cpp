#include "UI/CrateScreenWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
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
#include "Data/CrateRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/BackpackComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "TimerManager.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareStoreSettings.h"
#include "World/SnareSound.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡) — 상자 화면(`UChestScreenWidget`)과 같은 판 틀.
	constexpr float PanelWidth = 760.f;
	constexpr float PanelHeight = 940.f;
	constexpr float PanelTop = 80.f;
	constexpr float PanelPadding = 24.f;
	constexpr float PanelRadius = 16.f;
	constexpr float LineGap = 8.f;
	/** 10-16 80 → 121 (애플 44pt, DESIGN 2). 단추 넷이 164 커진 만큼 목록을 줄여 판 높이 940 을 지킨다. */
	constexpr float ButtonHeight = SnareUiStyle::MenuTouchMin;
	constexpr float ListHeight = 376.f;
	/** 결과 이름 글자 — 판에서 가장 크게 (레퍼런스: 상자 연 뒤 결과를 크게 보인다). */
	constexpr float ResultFontSize = 44.f;
	/** 답이 온 뒤 결과를 보이기 전 두근거림 (🟡 연출 길이, 숫자 판정 아님). */
	constexpr float RevealSeconds = 0.8f;

	const TCHAR* const CrateTablePath = TEXT("/Game/Game/Data/Shop/DT_CrateTable.DT_CrateTable");

	FText CrateGradeName(EItemGrade Grade)
	{
		switch (Grade)
		{
		case EItemGrade::Rare:      return LOCTEXT("CrateGradeRare", "레어");
		case EItemGrade::Epic:      return LOCTEXT("CrateGradeEpic", "에픽");
		case EItemGrade::Legendary: return LOCTEXT("CrateGradeLegendary", "레전더리");
		default:                    return LOCTEXT("CrateGradeCommon", "일반");
		}
	}
}

UCrateScreenWidget::UCrateScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> CrateTableAsset(CrateTablePath);
	if (CrateTableAsset.Succeeded())
	{
		CrateTable = CrateTableAsset.Object;
	}
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

UCrateScreenWidget* UCrateScreenWidget::OpenFor(APlayerController* Controller, USnareBackendSubsystem* InBackend,
	UBackpackComponent* InBackpack, FName InCrateID)
{
	if (!IsValid(Controller))
	{
		return nullptr;
	}
	UCrateScreenWidget* const Screen = CreateWidget<UCrateScreenWidget>(Controller, StaticClass());
	if (Screen != nullptr)
	{
		Screen->BindToCrate(InBackend, InBackpack, InCrateID);
		Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	}
	return Screen;
}

void UCrateScreenWidget::BindToCrate(USnareBackendSubsystem* InBackend, UBackpackComponent* InBackpack, FName InCrateID)
{
	Backend = InBackend;
	Backpack = InBackpack;
	CrateID = InCrateID;
	RefreshDisplay();
}

bool UCrateScreenWidget::IsCrateItem(const UDataTable* InCrateTable, FName ItemID)
{
	if (InCrateTable == nullptr || ItemID.IsNone())
	{
		return false;
	}
	bool bFound = false;
	InCrateTable->ForeachRow<FCrateRow>(TEXT("IsCrateItem"), [ItemID, &bFound](const FName&, const FCrateRow& Row)
	{
		bFound |= Row.CrateItemID == ItemID;
	});
	return bFound;
}

int32 UCrateScreenWidget::CountOwned(const TArray<FInventorySlot>& Owned, FName ItemID)
{
	int32 Count = 0;
	for (const FInventorySlot& Entry : Owned)
	{
		Count += Entry.ItemID == ItemID ? Entry.Count : 0;
	}
	return Count;
}

int32 UCrateScreenWidget::GetKeyCount() const
{
	return IsValid(Backend) ? CountOwned(Backend->GetPlayerSnapshot().InventorySlots, UBackpackComponent::GetCrateKeyID()) : 0;
}

TArray<const FCrateRow*> UCrateScreenWidget::FindCrateRows() const
{
	TArray<const FCrateRow*> Rows;
	if (CrateTable != nullptr)
	{
		CrateTable->ForeachRow<FCrateRow>(TEXT("FindCrateRows"), [this, &Rows](const FName&, const FCrateRow& Row)
		{
			if (Row.CrateItemID == CrateID)
			{
				Rows.Add(&Row);
			}
		});
	}
	return Rows;
}

FText UCrateScreenWidget::FindItemDisplayName(FName ItemID) const
{
	// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7).
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, ItemID);
	return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromName(ItemID);
}

int32 UCrateScreenWidget::FindKeyGemPrice() const
{
	const TArray<const FCrateRow*> Rows = FindCrateRows();
	return Rows.Num() > 0 ? Rows[0]->KeyGemPrice : 0;
}

TArray<FText> UCrateScreenWidget::BuildDisclosureTexts() const
{
	// 등급 줄 먼저(10.4 "등급별 %"), 그다음 품목 줄. 값은 표 그대로 — 고지 = 데이터.
	const TArray<const FCrateRow*> Rows = FindCrateRows();
	TArray<FText> Lines;
	for (const EItemGrade Grade : {EItemGrade::Common, EItemGrade::Rare, EItemGrade::Epic, EItemGrade::Legendary})
	{
		float Sum = 0.f;
		bool bAny = false;
		for (const FCrateRow* const Row : Rows)
		{
			if (Row->Grade == Grade)
			{
				Sum += Row->ChancePct;
				bAny = true;
			}
		}
		if (bAny)
		{
			Lines.Add(FText::Format(LOCTEXT("CrateDisclosureGrade", "{0}  {1}%"), CrateGradeName(Grade), FText::AsNumber(Sum)));
		}
	}
	for (const FCrateRow* const Row : Rows)
	{
		Lines.Add(FText::Format(LOCTEXT("CrateDisclosureItem", "{0} ({1})  {2}%"), FindItemDisplayName(Row->ItemID),
			CrateGradeName(Row->Grade), FText::AsNumber(Row->ChancePct)));
	}
	return Lines;
}

bool UCrateScreenWidget::PressOpen()
{
	SetDisclosureShown(false);
	if (bPending || bRevealing || !IsValid(Backpack))
	{
		return false;
	}
	if (GetKeyCount() <= 0)
	{
		// 열쇠 없이는 묻지도 않는다 — 서버도 `no_key` 로 거절한다.
		StatusText = LOCTEXT("CrateNoKey", "열쇠가 없다 - 잡화 상인에게 산다");
		RefreshDisplay();
		return false;
	}
	// 대기는 부르기 전에 세운다 — 서버가 같은 호출 안에서 답하면 그 답이 대기를 푼다.
	bPending = true;
	ResultText = FText::GetEmpty();
	StatusText = FText::GetEmpty();
	const TWeakObjectPtr<UCrateScreenWidget> WeakThis(this);
	const bool bSent = Backpack->RequestOpenCrate(CrateID,
		[WeakThis](bool bAccepted, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Response)
		{
			if (UCrateScreenWidget* const Self = WeakThis.Get())
			{
				Self->ApplyReply(bAccepted, Code, Message, Response);
			}
		});
	if (!bSent)
	{
		bPending = false;
	}
	RefreshDisplay();
	return bSent;
}

bool UCrateScreenWidget::PressBuyKey()
{
	if (bPending || !IsValid(Backpack))
	{
		return false;
	}
	bPending = true;
	StatusText = FText::GetEmpty();
	const TWeakObjectPtr<UCrateScreenWidget> WeakThis(this);
	const bool bSent = Backpack->RequestBuyCrateKeyWithGems(
		[WeakThis](bool bAccepted, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Response)
		{
			UCrateScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			// 열쇠 수는 이어서 오는 스냅샷이 바꾼다 — 여기서 더하지 않는다 (CLAUDE.md 8).
			Self->StatusText = bAccepted ? LOCTEXT("CrateKeyBought", "열쇠를 샀다")
				: URanchSyncComponent::DescribeFailureCode(Self->FailureTable, TEXT("BuyItem"), Code, Message, Response, false);
			Self->RefreshDisplay();
		});
	if (!bSent)
	{
		bPending = false;
	}
	RefreshDisplay();
	return bSent;
}

void UCrateScreenWidget::ApplyReply(bool bAccepted, const FString& Code, const FString& Message,
	const TSharedPtr<FJsonObject>& Response)
{
	bPending = false;
	if (!bAccepted)
	{
		StatusText = URanchSyncComponent::DescribeFailureCode(FailureTable, TEXT("OpenCrate"), Code, Message, Response, false);
		RefreshDisplay();
		return;
	}
	FString ItemId;
	Response->TryGetStringField(TEXT("itemId"), ItemId);
	const FName Won(*ItemId);
	EItemGrade Grade = EItemGrade::Common;
	for (const FCrateRow* const Row : FindCrateRows())
	{
		Grade = Row->ItemID == Won ? Row->Grade : Grade;
	}
	ResultText = FText::Format(LOCTEXT("CrateResult", "{0}\n{1}"), FindItemDisplayName(Won), CrateGradeName(Grade));
	// 결과 칸을 채우는 이 자리에서만 — 답 전·거절엔 소리가 없다 (CLAUDE.md 8 · DESIGN 2.2 「거점·상자 소리」 권위 줄).
	++ConfirmSoundCount;
	LastConfirmSoundCue = GetConfirmCue();
	USnareSoundSubsystem::Play2D(this, LastConfirmSoundCue, GetConfirmVolume());
	if (ResultBody != nullptr)
	{
		ResultBody->SetColorAndOpacity(FSlateColor(SnareUiStyle::ItemGradeColor(Grade)));
	}
	// 짧은 연출 뒤 크게 — 월드가 없으면(검사) 바로 보인다.
	bRevealing = true;
	UWorld* const World = GetWorld();
	if (World != nullptr)
	{
		World->GetTimerManager().SetTimer(RevealTimer, FTimerDelegate::CreateUObject(this, &UCrateScreenWidget::FinishReveal),
			RevealSeconds, false);
	}
	else
	{
		FinishReveal();
	}
	RefreshDisplay();
}

FName UCrateScreenWidget::GetConfirmCue()
{
	return FName(TEXT("CrateOpen"));
}

float UCrateScreenWidget::GetConfirmVolume()
{
	return 0.8f;
}

void UCrateScreenWidget::FinishReveal()
{
	bRevealing = false;
	RefreshDisplay();
}

void UCrateScreenWidget::SetDisclosureShown(bool bInShown)
{
	bDisclosureShown = bInShown;
	RefreshDisplay();
}

void UCrateScreenWidget::HandleOpenClicked()
{
	PressOpen();
}

void UCrateScreenWidget::HandleBuyKeyClicked()
{
	PressBuyKey();
}

void UCrateScreenWidget::HandleDisclosureClicked()
{
	SetDisclosureShown(!bDisclosureShown);
}

void UCrateScreenWidget::HandleCloseClicked()
{
	RemoveFromParent();
}

void UCrateScreenWidget::RefreshDisplay()
{
	const int32 Keys = GetKeyCount();
	if (IsValid(TitleText))
	{
		TitleText->SetText(bDisclosureShown ? LOCTEXT("CrateDisclosureTitle", "확률 고지 (한 번 열 때)")
			: FindItemDisplayName(CrateID));
	}
	if (IsValid(ResultBody))
	{
		ResultBody->SetText(bPending ? LOCTEXT("CratePending", "상자 여는 중...")
			: bRevealing ? LOCTEXT("CrateRevealing", "두근두근...") : ResultText);
		ResultBody->SetVisibility(bDisclosureShown ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(StatusBody))
	{
		StatusBody->SetText(StatusText);
	}
	if (IsValid(OpenButton))
	{
		// 답을 기다리거나 연출 중엔 다시 못 누른다 — 두 번 물어 열쇠 두 개를 쓰는 일이 없게.
		OpenButton->SetIsEnabled(!bPending && !bRevealing);
	}
	if (IsValid(OpenButtonText))
	{
		OpenButtonText->SetText(FText::Format(LOCTEXT("CrateOpenWithKey", "열쇠로 열기 (열쇠 {0}개)"), FText::AsNumber(Keys)));
	}
	if (IsValid(BuyKeyButton))
	{
		BuyKeyButton->SetIsEnabled(!bPending);
		// 젬이 꺼진 빌드(1.0)는 젬으로 열쇠 사기 칸을 접는다 (INBOX M94 ④).
		BuyKeyButton->SetVisibility(USnareStoreSettings::AreGemsEnabled() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(BuyKeyButtonText))
	{
		BuyKeyButtonText->SetText(FText::Format(LOCTEXT("CrateBuyKey", "젬 {0}개로 열쇠 사기"), FText::AsNumber(FindKeyGemPrice())));
	}
	if (IsValid(DisclosureButtonText))
	{
		DisclosureButtonText->SetText(bDisclosureShown ? LOCTEXT("CrateShowResult", "결과 보기") : LOCTEXT("CrateShowDisclosure", "확률 보기"));
	}
	if (IsValid(DisclosureList))
	{
		DisclosureList->SetVisibility(bDisclosureShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (DisclosureList->GetChildrenCount() == 0)
		{
			RebuildDisclosureList();
		}
	}
}

void UCrateScreenWidget::RebuildDisclosureList()
{
	if (WidgetTree == nullptr)
	{
		return;
	}
	int32 Index = 0;
	for (const FText& Line : BuildDisclosureTexts())
	{
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("CrateDisclosureText_%d"), Index++)));
		Text->SetText(Line);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		if (UVerticalBoxSlot* const LineSlot = Cast<UVerticalBoxSlot>(DisclosureList->AddChild(Text)))
		{
			LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
		}
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

TSharedRef<SWidget> UCrateScreenWidget::RebuildWidget()
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

bool UCrateScreenWidget::NeedsPlaceholderTree() const
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

void UCrateScreenWidget::BuildPlaceholderTree()
{
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CrateScreenRoot"));
		WidgetTree->RootWidget = Root;
	}

	auto MakeButton = [this](FName Name, UTextBlock** OutText) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Text"))));
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
		*OutText = Text;
		return Button;
	};

	// 크림 둥근 판 — 결과(크게) 또는 고지 표 / 알림 / 버튼 넷. 제목은 판 위로 걸친 알약.
	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CratePanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), PanelRadius));
	Panel->SetPadding(FMargin(PanelPadding));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Panel)))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.f));
		PanelSlot->SetPosition(FVector2D(0.f, PanelTop));
		PanelSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
	}
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CrateColumn"));
	Panel->SetContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	USizeBox* const Pill = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("CrateTitlePillBox"), TEXT("CrateTitlePill"), *TitleText);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Pill)))
	{
		PillSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		PillSlot->SetAlignment(FVector2D::ZeroVector);
		PillSlot->SetPosition(FVector2D(-PanelWidth * 0.5f, PanelTop - SnareUiStyle::MenuTitlePillRise));
		PillSlot->SetAutoSize(true);
	}

	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CrateScroll"));
	UVerticalBox* const Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CrateBody"));
	ResultBody = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultBody"));
	ResultBody->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	ResultBody->SetJustification(ETextJustify::Center);
	ResultBody->SetAutoWrapText(true);
	SnareUiStyle::SetFontSize(*ResultBody, ResultFontSize);
	Body->AddChild(ResultBody);
	DisclosureList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DisclosureList"));
	Body->AddChild(DisclosureList);
	Scroll->AddChild(Body);
	USizeBox* const ListBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CrateListBox"));
	ListBox->SetHeightOverride(ListHeight);
	ListBox->AddChild(Scroll);
	if (UVerticalBoxSlot* const ListSlot = Cast<UVerticalBoxSlot>(Column->AddChild(ListBox)))
	{
		ListSlot->SetPadding(FMargin(0.f, LineGap, 0.f, LineGap));
	}
	StatusBody = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusBody"));
	StatusBody->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiAccentRed()));
	StatusBody->SetJustification(ETextJustify::Center);
	Column->AddChild(StatusBody);

	UTextBlock* Label = nullptr;
	OpenButton = MakeButton(TEXT("OpenButton"), &Label);
	OpenButtonText = Label;
	OpenButton->OnClicked.AddDynamic(this, &UCrateScreenWidget::HandleOpenClicked);
	BuyKeyButton = MakeButton(TEXT("BuyKeyButton"), &Label);
	BuyKeyButtonText = Label;
	BuyKeyButton->OnClicked.AddDynamic(this, &UCrateScreenWidget::HandleBuyKeyClicked);
	DisclosureButton = MakeButton(TEXT("DisclosureButton"), &Label);
	DisclosureButtonText = Label;
	DisclosureButton->OnClicked.AddDynamic(this, &UCrateScreenWidget::HandleDisclosureClicked);
	CloseButton = MakeButton(TEXT("CloseButton"), &Label);
	Label->SetText(LOCTEXT("CrateClose", "닫기"));
	CloseButton->OnClicked.AddDynamic(this, &UCrateScreenWidget::HandleCloseClicked);
	for (UButton* const Button : {OpenButton.Get(), BuyKeyButton.Get(), DisclosureButton.Get(), CloseButton.Get()})
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*(Button->GetName() + TEXT("Box"))));
		Box->SetHeightOverride(ButtonHeight);
		Box->AddChild(Button);
		if (UVerticalBoxSlot* const ButtonSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Box)))
		{
			ButtonSlot->SetPadding(FMargin(0.f, LineGap, 0.f, 0.f));
		}
	}
}

#undef LOCTEXT_NAMESPACE
