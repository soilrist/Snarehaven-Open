#include "UI/PlayerSafetyWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBlockListSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡) — 선물 판과 같은 틀·같은 줄 높이.
	constexpr float PanelWidth = 760.f;
	constexpr float PanelTop = 100.f;
	constexpr float PanelPadding = 24.f;
	constexpr float BodyHeight = 460.f;
	// 줄 = 메뉴 판 누르는 칸 최소(DESIGN 2 「메뉴 판 누르는 칸 최소」 121, 애플 44pt).
	constexpr float RowHeight = SnareUiStyle::MenuTouchMin;
	constexpr float RowGap = 8.f;
}

UPlayerSafetyWidget* UPlayerSafetyWidget::OpenOnViewport(APlayerController* Owner, UWorld* World)
{
	UPlayerSafetyWidget* const Screen = IsValid(Owner) ? CreateWidget<UPlayerSafetyWidget>(Owner, StaticClass())
		: IsValid(World) ? CreateWidget<UPlayerSafetyWidget>(World, StaticClass()) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	UGameInstance* const GameInstance = Screen->GetGameInstance();
	Screen->BindBlockList(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBlockListSubsystem>() : nullptr);
	const TWeakObjectPtr<APlayerController> WeakOwner(Owner);
	Screen->PeerSource = [WeakOwner]()
	{
		return UGiftScreenWidget::GatherPeers(WeakOwner.IsValid() ? WeakOwner->GetPawn() : nullptr);
	};
	Screen->Reload();
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	return Screen;
}

void UPlayerSafetyWidget::BindBlockList(USnareBlockListSubsystem* InBlockList)
{
	BlockList = InBlockList;
	RefreshDisplay();
}

void UPlayerSafetyWidget::Reload()
{
	Peers = PeerSource ? PeerSource() : TArray<FGiftPeer>();
	Peers.Sort([](const FGiftPeer& A, const FGiftPeer& B) { return A.DistanceCm < B.DistanceCm; });
	SelectedPeer.Empty();
	SelectedReason = NAME_None;
	Message = FText::GetEmpty();
	RefreshDisplay();
}

void UPlayerSafetyWidget::SelectPeer(const FString& PlayFabId)
{
	SelectedPeer = PlayFabId;
	SelectedReason = NAME_None;
	Message = FText::GetEmpty();
	RefreshDisplay();
}

void UPlayerSafetyWidget::SelectReason(FName Reason)
{
	SelectedReason = Reason;
	RefreshDisplay();
}

const FGiftPeer* UPlayerSafetyWidget::FindSelected() const
{
	return Peers.FindByPredicate([this](const FGiftPeer& Peer) { return Peer.PlayFabId == SelectedPeer; });
}

bool UPlayerSafetyWidget::IsPending() const
{
	return IsValid(BlockList) && BlockList->IsPending();
}

bool UPlayerSafetyWidget::CanReport() const
{
	return !IsPending() && FindSelected() != nullptr && !SelectedReason.IsNone();
}

FText UPlayerSafetyWidget::GetShownName(const FGiftPeer& Peer) const
{
	return IsValid(BlockList) && BlockList->IsBlocked(Peer.PlayFabId) ? LOCTEXT("SafetyBlockedName", "차단한 플레이어") : Peer.Name;
}

FText UPlayerSafetyWidget::DescribeReason(FName Reason)
{
	if (Reason == TEXT("name")) { return LOCTEXT("SafetyReasonName", "나쁜 이름"); }
	if (Reason == TEXT("harass")) { return LOCTEXT("SafetyReasonHarass", "괴롭힘·욕설"); }
	if (Reason == TEXT("cheat")) { return LOCTEXT("SafetyReasonCheat", "부정 행위"); }
	return LOCTEXT("SafetyReasonOther", "기타");
}

bool UPlayerSafetyWidget::RequestReport()
{
	if (!CanReport() || !IsValid(BlockList))
	{
		return false;
	}
	Message = FText::GetEmpty();
	const TWeakObjectPtr<UPlayerSafetyWidget> WeakThis(this);
	const bool bSent = BlockList->RequestReport(SelectedPeer, SelectedReason, [WeakThis](const FText& Failure)
	{
		if (UPlayerSafetyWidget* const Self = WeakThis.Get())
		{
			Self->Message = !Failure.IsEmpty() ? Failure : LOCTEXT("SafetyReported", "신고했어요. 운영팀이 확인해요.");
			if (Failure.IsEmpty())
			{
				Self->SelectedReason = NAME_None;
			}
			Self->RefreshDisplay();
		}
	});
	RefreshDisplay();
	return bSent;
}

bool UPlayerSafetyWidget::RequestToggleBlock()
{
	const FGiftPeer* const Peer = FindSelected();
	if (Peer == nullptr || IsPending() || !IsValid(BlockList))
	{
		return false;
	}
	const bool bBlock = !BlockList->IsBlocked(Peer->PlayFabId);
	Message = FText::GetEmpty();
	const TWeakObjectPtr<UPlayerSafetyWidget> WeakThis(this);
	const bool bSent = BlockList->RequestSetBlocked(Peer->PlayFabId, Peer->Name.ToString(), bBlock, [WeakThis, bBlock](const FText& Failure)
	{
		if (UPlayerSafetyWidget* const Self = WeakThis.Get())
		{
			Self->Message = !Failure.IsEmpty() ? Failure
				: bBlock ? LOCTEXT("SafetyBlocked", "차단했어요. 이름과 이모트가 내 화면에서 가려져요.")
				: LOCTEXT("SafetyUnblocked", "차단을 풀었어요.");
			Self->RefreshDisplay();
		}
	});
	RefreshDisplay();
	return bSent;
}

void UPlayerSafetyWidget::Close()
{
	RemoveFromParent();
}

void UPlayerSafetyWidget::HandleReportClicked() { RequestReport(); }
void UPlayerSafetyWidget::HandleBlockClicked() { RequestToggleBlock(); }
void UPlayerSafetyWidget::HandleCloseClicked() { Close(); }

// ---------------------------------------------------------------------------------------------
// 그리기

void UPlayerSafetyWidget::AddSectionTitle(const FText& Title, int32& Index)
{
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("SafetySection_%d"), Index++)));
	Text->SetText(Title);
	Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	if (UVerticalBoxSlot* const TitleSlot = Cast<UVerticalBoxSlot>(BodyList->AddChild(Text)))
	{
		TitleSlot->SetPadding(FMargin(0.f, RowGap * 2.f, 0.f, RowGap));
	}
}

USizeBox* UPlayerSafetyWidget::AddRow(const FText& Label, bool bSelected, FName Key, TFunction<void(FName)> OnClicked, int32& Index)
{
	USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("SafetyRowBox_%d"), Index)));
	Box->SetHeightOverride(RowHeight);
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
		FName(*FString::Printf(TEXT("SafetyRowButton_%d"), Index)));
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("SafetyRowText_%d"), Index)));
	// 고른 표시는 Jua 에 있는 ★ — ✔(U+2714)는 Jua·대체 Roboto 둘 다 없어 네모로 나왔다(출시 전 점검 10-16).
	Text->SetText(bSelected ? FText::Format(LOCTEXT("SafetyRowSelected", "★ {0}"), Label) : Label);
	Text->SetJustification(ETextJustify::Center);
	Button->AddChild(Text);
	Button->SetIsEnabled(!IsPending());
	Box->AddChild(Button);
	++Index;

	USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
	Proxy->Key = Key;
	Proxy->OnClicked = MoveTemp(OnClicked);
	Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
	RowProxies.Add(Proxy);

	if (UVerticalBoxSlot* const BoxSlot = Cast<UVerticalBoxSlot>(BodyList->AddChild(Box)))
	{
		BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
	}
	return Box;
}

void UPlayerSafetyWidget::RefreshDisplay()
{
	const FGiftPeer* const Selected = FindSelected();
	const bool bSelectedBlocked = Selected != nullptr && IsValid(BlockList) && BlockList->IsBlocked(Selected->PlayFabId);
	if (IsValid(MessageText))
	{
		MessageText->SetText(IsPending() ? LOCTEXT("SafetyPending", "확인 중...")
			: !Message.IsEmpty() ? Message
			: LOCTEXT("SafetyHint", "같은 방 사람을 골라 신고하거나 차단해요. 차단하면 내 화면에서만 이름·이모트가 가려져요."));
	}
	if (IsValid(ReportButton))
	{
		ReportButton->SetIsEnabled(CanReport());
	}
	if (IsValid(BlockButton))
	{
		BlockButton->SetIsEnabled(Selected != nullptr && !IsPending());
	}
	if (IsValid(BlockLabel))
	{
		BlockLabel->SetText(bSelectedBlocked ? LOCTEXT("SafetyUnblock", "차단 해제") : LOCTEXT("SafetyBlock", "차단"));
	}
	if (!IsValid(BodyList) || WidgetTree == nullptr)
	{
		return;
	}

	BodyList->ClearChildren();
	RowProxies.Reset();
	int32 Index = 0;
	const TWeakObjectPtr<UPlayerSafetyWidget> WeakThis(this);
	USizeBox* LastSelectedRow = nullptr;   // 고른 줄(이유가 있으면 이유, 없으면 사람) — 스크롤 밖에 반쯤 가려지던 것

	AddSectionTitle(LOCTEXT("SafetyPeerTitle", "누구?"), Index);
	if (Peers.Num() == 0)
	{
		UTextBlock* const Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SafetyPeerEmpty"));
		Empty->SetText(LOCTEXT("SafetyNoPeer", "같은 방에 다른 사람이 없어요"));
		Empty->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		BodyList->AddChild(Empty);
	}
	for (const FGiftPeer& Peer : Peers)
	{
		const bool bPicked = Peer.PlayFabId == SelectedPeer;
		USizeBox* const Row = AddRow(GetShownName(Peer), bPicked, FName(*Peer.PlayFabId), [WeakThis](FName Key)
		{
			if (UPlayerSafetyWidget* const Self = WeakThis.Get())
			{
				Self->SelectPeer(Key.ToString());
			}
		}, Index);
		LastSelectedRow = bPicked ? Row : LastSelectedRow;
	}

	if (Selected != nullptr)
	{
		AddSectionTitle(LOCTEXT("SafetyReasonTitle", "신고 이유"), Index);
		for (const FName Reason : USnareBlockListSubsystem::GetReportReasons())
		{
			USizeBox* const Row = AddRow(DescribeReason(Reason), Reason == SelectedReason, Reason, [WeakThis](FName Key)
			{
				if (UPlayerSafetyWidget* const Self = WeakThis.Get())
				{
					Self->SelectReason(Key);
				}
			}, Index);
			LastSelectedRow = Reason == SelectedReason ? Row : LastSelectedRow;
		}
	}
	// 고른 줄이 판 아래로 반쯤 잘리지 않게 스크롤을 그 줄까지 내린다(다음 틱 배치 뒤 적용).
	if (UScrollBox* const Scroll = Cast<UScrollBox>(BodyList->GetParent()); Scroll != nullptr && LastSelectedRow != nullptr)
	{
		Scroll->ScrollWidgetIntoView(LastSelectedRow, false, EDescendantScrollDestination::IntoView);
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

TSharedRef<SWidget> UPlayerSafetyWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	RefreshDisplay();
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

void UPlayerSafetyWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SafetyRoot"));
	WidgetTree->RootWidget = Root;
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 판 + 제목 알약 — 선물 판과 같은 틀.
	const float PillRise = SnareUiStyle::MenuTitlePillRise;
	const float PillOverhang = SnareUiStyle::PanelTitlePillHeight - PillRise;
	UOverlay* const Frame = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SafetyFrame"));
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* const FrameSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Frame)))
	{
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.f));
		FrameSlot->SetPosition(FVector2D(0.f, PanelTop - PillRise));
		FrameSlot->SetAutoSize(true);
	}
	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SafetyPanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), SnareUiStyle::ScreenPanelRadius));
	Panel->SetPadding(FMargin(PanelPadding, PanelPadding + PillOverhang, PanelPadding, PanelPadding));
	if (UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Frame->AddChild(Panel)))
	{
		PanelSlot->SetPadding(FMargin(0.f, PillRise, 0.f, 0.f));
	}
	UTextBlock* const Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SafetyTitle"));
	Title->SetText(LOCTEXT("SafetyTitle", "플레이어"));
	USizeBox* const TitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("SafetyTitlePillBox"), TEXT("SafetyTitlePill"), *Title);
	if (UOverlaySlot* const PillSlot = Cast<UOverlaySlot>(Frame->AddChild(TitlePillBox)))
	{
		PillSlot->SetHorizontalAlignment(HAlign_Left);
		PillSlot->SetVerticalAlignment(VAlign_Top);
	}

	USizeBox* const Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SafetyWidth"));
	Width->SetWidthOverride(PanelWidth);
	Panel->SetContent(Width);
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SafetyColumn"));
	Width->AddChild(Column);

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SafetyMessageText"));
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	if (UVerticalBoxSlot* const MessageSlot = Cast<UVerticalBoxSlot>(Column->AddChild(MessageText)))
	{
		MessageSlot->SetPadding(FMargin(0.f, RowGap, 0.f, RowGap));
	}

	USizeBox* const BodyBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SafetyBodyBox"));
	BodyBox->SetHeightOverride(BodyHeight);
	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SafetyScroll"));
	BodyBox->AddChild(Scroll);
	BodyList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SafetyBodyList"));
	Scroll->AddChild(BodyList);
	Column->AddChild(BodyBox);

	// 아래 줄: [신고 보내기] [차단 / 차단 해제] — 꺼져도 글이 읽히는 단추(선물 「보내기」 와 같은 꼴).
	UHorizontalBox* const ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SafetyActionRow"));
	auto AddAction = [this, ActionRow](FName Name, const FText& Label, UButton*& OutButton, UTextBlock*& OutLabel, bool bGap)
	{
		UOverlay* const Stack = SnareUiStyle::MakeClearLabelButton(*WidgetTree, Name, Label, SnareUiStyle::MenuFontMin + 4.f, OutButton, OutLabel);
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*(Name.ToString() + TEXT("Box"))));
		Box->SetHeightOverride(SnareUiStyle::MenuTouchMin);
		Box->AddChild(Stack);
		if (UHorizontalBoxSlot* const BoxSlot = Cast<UHorizontalBoxSlot>(ActionRow->AddChild(Box)))
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BoxSlot->SetPadding(FMargin(0.f, 0.f, bGap ? RowGap : 0.f, 0.f));
		}
	};
	UButton* Report = nullptr;
	UTextBlock* ReportLabel = nullptr;
	AddAction(TEXT("SafetyReportButton"), LOCTEXT("SafetyReport", "신고 보내기"), Report, ReportLabel, true);
	ReportButton = Report;
	ReportButton->OnClicked.AddDynamic(this, &UPlayerSafetyWidget::HandleReportClicked);
	UButton* Block = nullptr;
	UTextBlock* BlockText = nullptr;
	AddAction(TEXT("SafetyBlockButton"), LOCTEXT("SafetyBlock", "차단"), Block, BlockText, false);
	BlockButton = Block;
	BlockLabel = BlockText;
	BlockButton->OnClicked.AddDynamic(this, &UPlayerSafetyWidget::HandleBlockClicked);
	if (UVerticalBoxSlot* const ActionSlot = Cast<UVerticalBoxSlot>(Column->AddChild(ActionRow)))
	{
		ActionSlot->SetPadding(FMargin(0.f, RowGap, 0.f, 0.f));
	}

	UButton* Close = nullptr;
	USizeBox* const CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("SafetyCloseBox"), TEXT("SafetyCloseButton"),
		LOCTEXT("SafetyClose", "닫기"), Close);
	Close->OnClicked.AddDynamic(this, &UPlayerSafetyWidget::HandleCloseClicked);
	if (UVerticalBoxSlot* const CloseSlot = Cast<UVerticalBoxSlot>(Column->AddChild(CloseBox)))
	{
		CloseSlot->SetHorizontalAlignment(HAlign_Right);
		CloseSlot->SetPadding(FMargin(0.f, RowGap, 0.f, 0.f));
	}
}

#undef LOCTEXT_NAMESPACE
