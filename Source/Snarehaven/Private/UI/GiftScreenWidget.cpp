#include "UI/GiftScreenWidget.h"

#include "Snarehaven.h"
#include "SnareCheckSwitch.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareBlockListSubsystem.h"
#include "Backend/SnareServerReply.h"
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
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Ranch/CoopReportComponent.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡). 판 폭·줄 높이는 의뢰 창과 같은 값 — 손 닿는 크기(줄 96).
	constexpr float PanelWidth = 760.f;
	constexpr float PanelTop = 100.f;
	constexpr float PanelPadding = 24.f;
	constexpr float BodyHeight = 580.f;   // 10-16 아래 두 줄이 121 로 커진 만큼(2×41) 줄여 판 높이를 그대로 둔다
	constexpr float RowHeight = 88.f;
	constexpr float RowGap = 8.f;
	constexpr float CloseHeight = SnareUiStyle::MenuTouchMin;   // 10-16 애플 44pt (옛 80)
	// 열기 버튼 — 오른쪽 아래 기둥 맨 위(배낭 0 · 지도 1 · 메뉴 2 · 이모트 3 · 출석 4 · 건네기 5).
	constexpr float OpenButtonRadius = 12.f;
	constexpr float CountButtonWidth = SnareUiStyle::MenuTouchMin;   // 10-16 −/+ 도 누르는 칸 121 (옛 96)
}

UGiftScreenWidget::UGiftScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

UGiftScreenWidget* UGiftScreenWidget::AttachTo(APawn* Pawn)
{
	APlayerController* const Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller) || !Controller->IsLocalController())
	{
		return nullptr;
	}
	UGiftScreenWidget* const Screen = CreateWidget<UGiftScreenWidget>(Controller, StaticClass());
	if (!IsValid(Screen))
	{
		return nullptr;
	}
	UGameInstance* const GameInstance = Pawn->GetGameInstance();
	Screen->BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);
	const TWeakObjectPtr<const APawn> WeakMe(Pawn);
	Screen->PeerSource = [WeakMe]() { return UGiftScreenWidget::GatherPeers(WeakMe.Get()); };
	Screen->AddToViewport(/*ZOrder=*/5);
	// 3층 검사용 — 펼친 채로, 근처 이웃 1명·받을 선물 1줄을 가짜로 채워 띄운다(서버 안 부름, 출시 빌드에선 닫힘).
	if (SnareCheckSwitch::Param(TEXT("SnareGiftScreen")))
	{
		Screen->PeerSource = []() { FGiftPeer Peer; Peer.PlayFabId = TEXT("DEMO"); Peer.Name = LOCTEXT("GiftDemoPeer", "솔방울"); Peer.DistanceCm = 400.f; return TArray<FGiftPeer>{Peer}; };
		FGiftView Demo;
		Demo.GiftId = TEXT("demo");
		Demo.ItemID = TEXT("item.carrot");
		Demo.Count = 3;
		Screen->Gifts.Add(Demo);
		Screen->SetOpen(true);
	}
	return Screen;
}

// ---------------------------------------------------------------------------------------------
// 순수 함수

TArray<FGiftPeer> UGiftScreenWidget::PickNearby(const TArray<FGiftPeer>& All, float MaxCm)
{
	TArray<FGiftPeer> Near;
	for (const FGiftPeer& Peer : All)
	{
		if (!Peer.PlayFabId.IsEmpty() && Peer.DistanceCm <= MaxCm
			&& !Near.ContainsByPredicate([&Peer](const FGiftPeer& Seen) { return Seen.PlayFabId == Peer.PlayFabId; }))
		{
			Near.Add(Peer);
		}
	}
	Near.Sort([](const FGiftPeer& A, const FGiftPeer& B) { return A.DistanceCm < B.DistanceCm; });
	if (Near.Num() > MaxPeerRows)
	{
		Near.SetNum(MaxPeerRows);
	}
	return Near;
}

bool UGiftScreenWidget::ParseGifts(const TSharedPtr<FJsonObject>& Response, TArray<FGiftView>& OutGifts)
{
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("gifts"), Array))
	{
		return false;
	}
	OutGifts.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object))
		{
			continue;
		}
		FGiftView Gift;
		Gift.GiftId = (*Object)->GetStringField(TEXT("id"));
		Gift.FromId = (*Object)->GetStringField(TEXT("from"));
		Gift.ItemID = FName((*Object)->GetStringField(TEXT("itemId")));
		Gift.Count = static_cast<int32>((*Object)->GetNumberField(TEXT("count")));
		if (!Gift.GiftId.IsEmpty() && !Gift.ItemID.IsNone() && Gift.Count > 0)
		{
			OutGifts.Add(Gift);
		}
	}
	return true;
}

TSharedRef<FJsonObject> UGiftScreenWidget::MakeSendArgs(const FString& TargetId, FName ItemID, int32 Count)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("targetPlayerId"), TargetId);
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	Args->SetNumberField(TEXT("count"), Count);
	return Args;
}

bool UGiftScreenWidget::IsOfferableItem(FName ItemID)
{
	const FString Id = ItemID.ToString();
	return Id.StartsWith(TEXT("item.")) && !Id.StartsWith(TEXT("item.pet_"));
}

TArray<FGiftPeer> UGiftScreenWidget::GatherPeers(const APawn* Me)
{
	TArray<FGiftPeer> Out;
	UWorld* const World = IsValid(Me) ? Me->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return Out;
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* const Other = *It;
		const UCoopReportComponent* const Report = Other->FindComponentByClass<UCoopReportComponent>();
		if (Other == Me || Report == nullptr || Report->GetReportPlayFabId().IsEmpty())
		{
			continue;
		}
		FGiftPeer Peer;
		Peer.PlayFabId = Report->GetReportPlayFabId();
		const APlayerState* const State = Other->GetPlayerState();
		// 차단한 사람은 선물 목록에서도 이름을 가린다(이름표·신고 판과 같은 말).
		Peer.Name = USnareBlockListSubsystem::IsPawnBlocked(Other) ? LOCTEXT("SafetyBlockedName", "차단한 플레이어")
			: State != nullptr && !State->GetPlayerName().IsEmpty()
			? FText::FromString(State->GetPlayerName()) : LOCTEXT("GiftPeerUnnamed", "이웃");
		Peer.DistanceCm = static_cast<float>(FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation()));
		Out.Add(Peer);
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------
// 읽기

FText UGiftScreenWidget::FindItemDisplayName(FName ItemID) const
{
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, ItemID);
	if (Row != nullptr && !Row->DisplayName.IsEmpty())
	{
		return Row->DisplayName;
	}
	// 원 ID 를 화면에 흘리지 않는다(의뢰 창과 같은 꼴) — 표가 서버와 어긋난 것이니 로그로만 알린다.
	if (!WarnedUnknownIDs.Contains(ItemID))
	{
		WarnedUnknownIDs.Add(ItemID);
		UE_LOG(LogSnarehaven, Warning, TEXT("[Gift] 아이템 표에 없는 ID '%s' — 화면엔 '물건' 으로 보인다"), *ItemID.ToString());
	}
	return LOCTEXT("GiftUnknownItem", "물건");
}

int32 UGiftScreenWidget::CountOwned(FName ItemID) const
{
	int32 Count = 0;
	if (IsValid(WatchedBackend))
	{
		for (const FInventorySlot& InvSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
		{
			Count += InvSlot.ItemID == ItemID ? InvSlot.Count : 0;
		}
	}
	return Count;
}

bool UGiftScreenWidget::CanSend() const
{
	return !bPending && !SelectedPeer.IsEmpty() && !SelectedItem.IsNone()
		&& SendCount >= 1 && SendCount <= FMath::Min(MaxSendCount, CountOwned(SelectedItem));
}

// ---------------------------------------------------------------------------------------------
// 고르기

void UGiftScreenWidget::SelectPeer(const FString& PlayFabId)
{
	SelectedPeer = PlayFabId;
	RefreshDisplay();
}

void UGiftScreenWidget::SelectItem(FName ItemID)
{
	SelectedItem = ItemID;
	SendCount = FMath::Clamp(SendCount, 1, FMath::Max(1, FMath::Min(MaxSendCount, CountOwned(ItemID))));
	RefreshDisplay();
}

void UGiftScreenWidget::ChangeCount(int32 Delta)
{
	const int32 Limit = FMath::Max(1, FMath::Min(MaxSendCount, SelectedItem.IsNone() ? MaxSendCount : CountOwned(SelectedItem)));
	SendCount = FMath::Clamp(SendCount + Delta, 1, Limit);
	RefreshDisplay();
}

// ---------------------------------------------------------------------------------------------
// 서버 호출

void UGiftScreenWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UGiftScreenWidget::HandleBackendSnapshotChanged);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &UGiftScreenWidget::HandleBackendSnapshotChanged);
	}
	RefreshDisplay();
}

void UGiftScreenWidget::HandleBackendSnapshotChanged() { RefreshDisplay(); }
void UGiftScreenWidget::HandleOpenClicked() { SetOpen(true); }
void UGiftScreenWidget::HandleCloseClicked() { SetOpen(false); }
void UGiftScreenWidget::HandleSendClicked() { RequestSend(); }
void UGiftScreenWidget::HandleCountMinus() { ChangeCount(-1); }
void UGiftScreenWidget::HandleCountPlus() { ChangeCount(+1); }

void UGiftScreenWidget::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	if (bOpen)
	{
		Message = FText::GetEmpty();
		SelectedPeer.Empty();
		SelectedItem = NAME_None;
		SendCount = 1;
		Peers = PickNearby(PeerSource ? PeerSource() : TArray<FGiftPeer>());
		RequestGifts();
	}
	RefreshDisplay();
}

bool UGiftScreenWidget::RequestGifts()
{
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	RetryRequest = nullptr;
	const TWeakObjectPtr<UGiftScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetGifts"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UGiftScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Gift] 선물 목록 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				Self->RetryRequest = [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->RequestGifts(); } };
			}
			ParseGifts(Response, Self->Gifts);
			Self->RefreshDisplay();
		});
	RefreshDisplay();
	return true;
}

bool UGiftScreenWidget::CallGiftFunction(const TCHAR* Function, const TSharedRef<FJsonObject>& Args,
	const FText& SuccessText, bool bClearSelection)
{
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	Message = FText::GetEmpty();
	RetryRequest = nullptr;
	const FString LogName(Function);
	const TWeakObjectPtr<UGiftScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(Function, Args,
		[WeakThis, LogName, Args, SuccessText, bClearSelection](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UGiftScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Gift] %s 거절 — %s"), *LogName, *Failure.ToString());
				Self->Message = Failure;
				// 서버에 닿지 못했을 때만 같은 요청을 다시 — 「물건 모자람」 같은 거절은 다시 보내도 같다.
				if (!bOk || !Response.IsValid())
				{
					Self->RetryRequest = [WeakThis, LogName, Args, SuccessText, bClearSelection]()
					{
						if (WeakThis.IsValid())
						{
							WeakThis->CallGiftFunction(*LogName, Args, SuccessText, bClearSelection);
						}
					};
				}
				Self->RefreshDisplay();
				return;
			}
			USnareSoundSubsystem::Play2D(Self, TEXT("Pickup"), 0.8f);   // 서버가 옮긴 뒤에만 (CLAUDE.md 8)
			Self->Message = SuccessText;
			if (bClearSelection)
			{
				Self->SelectedPeer.Empty();
				Self->SelectedItem = NAME_None;
				Self->SendCount = 1;
			}
			// 배낭·목록은 서버가 바꿨다 — 손으로 고치지 않고 다시 받는다.
			if (IsValid(Self->WatchedBackend))
			{
				Self->WatchedBackend->FetchPlayerState();
			}
			Self->RefreshDisplay();
			Self->RequestGifts();
		});
	RefreshDisplay();
	return true;
}

bool UGiftScreenWidget::RequestSend()
{
	if (!CanSend())
	{
		return false;
	}
	return CallGiftFunction(TEXT("GiftItem"), MakeSendArgs(SelectedPeer, SelectedItem, SendCount),
		LOCTEXT("GiftSent", "보냈어요! 상대가 받으면 옮겨져요"), /*bClearSelection=*/true);
}

bool UGiftScreenWidget::RequestAccept(const FString& GiftId)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("giftId"), GiftId);
	return CallGiftFunction(TEXT("AcceptGift"), Args, LOCTEXT("GiftAccepted", "선물을 받았어요"), false);
}

bool UGiftScreenWidget::RequestDecline(const FString& GiftId)
{
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("giftId"), GiftId);
	return CallGiftFunction(TEXT("DeclineGift"), Args, LOCTEXT("GiftDeclined", "돌려보냈어요"), false);
}

// ---------------------------------------------------------------------------------------------
// 그리기

UButton* UGiftScreenWidget::AddRowButton(UPanelWidget& Parent, const FText& Label, bool bEnabled, bool bSelected, int32& Index)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
		FName(*FString::Printf(TEXT("GiftRowButton_%d"), Index)));
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("GiftRowText_%d"), Index)));
	Text->SetText(bSelected ? FText::Format(LOCTEXT("GiftRowSelected", "✔ {0}"), Label) : Label);
	Text->SetJustification(ETextJustify::Center);
	Button->AddChild(Text);
	Button->SetIsEnabled(bEnabled && !bPending);
	++Index;
	Parent.AddChild(Button);
	return Button;
}

void UGiftScreenWidget::BindRow(UButton& Button, FName Key, TFunction<void(FName)> OnClicked)
{
	USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
	Proxy->Key = Key;
	Proxy->OnClicked = MoveTemp(OnClicked);
	Button.OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
	RowProxies.Add(Proxy);
}

void UGiftScreenWidget::AddSectionTitle(const FText& Title, int32& Index)
{
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("GiftSection_%d"), Index++)));
	Text->SetText(Title);
	Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	if (UVerticalBoxSlot* const TitleSlot = Cast<UVerticalBoxSlot>(BodyList->AddChild(Text)))
	{
		TitleSlot->SetPadding(FMargin(0.f, RowGap * 2.f, 0.f, RowGap));
	}
}

void UGiftScreenWidget::RefreshDisplay()
{
	if (IsValid(OpenButton))
	{
		OpenButton->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(Panel))
	{
		Panel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TitlePillBox))
	{
		TitlePillBox->SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(MessageText))
	{
		MessageText->SetText(bPending ? LOCTEXT("GiftPending", "확인 중...")
			: !Message.IsEmpty() ? Message : LOCTEXT("GiftHint", "가까이 있는 이웃에게 물건을 선물해요. 상대가 받아야 옮겨져요."));
	}
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(RetryRequest && !bPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(CountText))
	{
		CountText->SetText(FText::Format(LOCTEXT("GiftCount", "{0}개"), FText::AsNumber(SendCount)));
	}
	if (IsValid(SendButton))
	{
		SendButton->SetIsEnabled(CanSend());
	}
	if (!IsValid(BodyList) || WidgetTree == nullptr)
	{
		return;
	}

	BodyList->ClearChildren();
	RowProxies.Reset();
	int32 Index = 0;
	const TWeakObjectPtr<UGiftScreenWidget> WeakThis(this);

	// ① 받을 선물 — 수락/거절 두 단추.
	if (Gifts.Num() > 0)
	{
		AddSectionTitle(LOCTEXT("GiftIncomingTitle", "받을 선물"), Index);
	}
	for (const FGiftView& Gift : Gifts)
	{
		UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("GiftIncomingRow_%d"), Index++)));
		UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("GiftIncomingLabel_%d"), Index++)));
		Label->SetText(FText::Format(LOCTEXT("GiftIncomingLine", "{0} x{1}"), FindItemDisplayName(Gift.ItemID), FText::AsNumber(Gift.Count)));
		Label->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		if (UHorizontalBoxSlot* const LabelSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
		const FName GiftKey(*Gift.GiftId);
		for (const bool bAccept : {true, false})
		{
			USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
				FName(*FString::Printf(TEXT("GiftIncomingBox_%d"), Index++)));
			Box->SetWidthOverride(CountButtonWidth + 24.f);
			Box->SetHeightOverride(RowHeight);
			UButton* const Button = AddRowButton(*Box, bAccept ? LOCTEXT("GiftAccept", "받기") : LOCTEXT("GiftDecline", "거절"),
				true, false, Index);
			BindRow(*Button, GiftKey, [WeakThis, bAccept](FName Key)
			{
				if (UGiftScreenWidget* const Self = WeakThis.Get())
				{
					if (bAccept)
					{
						Self->RequestAccept(Key.ToString());
					}
					else
					{
						Self->RequestDecline(Key.ToString());
					}
				}
			});
			if (UHorizontalBoxSlot* const BoxSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Box)))
			{
				BoxSlot->SetPadding(FMargin(RowGap, 0.f, 0.f, 0.f));
			}
		}
		BodyList->AddChild(Row);
	}

	// ② 보낼 사람 — 가까운 순.
	AddSectionTitle(LOCTEXT("GiftPeerTitle", "누구에게?"), Index);
	if (Peers.Num() == 0)
	{
		UTextBlock* const Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("GiftPeerEmpty_%d"), Index++)));
		Empty->SetText(LOCTEXT("GiftNoPeer", "가까이에 이웃이 없어요"));
		Empty->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		BodyList->AddChild(Empty);
	}
	for (const FGiftPeer& Peer : Peers)
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("GiftPeerBox_%d"), Index++)));
		Box->SetHeightOverride(RowHeight);
		UButton* const Button = AddRowButton(*Box, Peer.Name, true, Peer.PlayFabId == SelectedPeer, Index);
		BindRow(*Button, FName(*Peer.PlayFabId), [WeakThis](FName Key)
		{
			if (UGiftScreenWidget* const Self = WeakThis.Get())
			{
				Self->SelectPeer(Key.ToString());
			}
		});
		if (UVerticalBoxSlot* const BoxSlot = Cast<UVerticalBoxSlot>(BodyList->AddChild(Box)))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
	}

	// ③ 보낼 물건 — 배낭에 있는 것(칸이 나뉜 같은 아이템은 한 줄로 합침).
	AddSectionTitle(LOCTEXT("GiftItemTitle", "무엇을?"), Index);
	TMap<FName, int32> Owned;
	TArray<FName> Order;
	if (IsValid(WatchedBackend))
	{
		for (const FInventorySlot& InvSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
		{
			if (InvSlot.Count > 0 && IsOfferableItem(InvSlot.ItemID))
			{
				if (!Owned.Contains(InvSlot.ItemID))
				{
					Order.Add(InvSlot.ItemID);
				}
				Owned.FindOrAdd(InvSlot.ItemID) += InvSlot.Count;
			}
		}
	}
	if (Order.Num() == 0)
	{
		UTextBlock* const Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("GiftItemEmpty_%d"), Index++)));
		Empty->SetText(LOCTEXT("GiftNoItem", "보낼 물건이 없어요"));
		Empty->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		BodyList->AddChild(Empty);
	}
	for (const FName ItemID : Order)
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("GiftItemBox_%d"), Index++)));
		Box->SetHeightOverride(RowHeight);
		UButton* const Button = AddRowButton(*Box,
			FText::Format(LOCTEXT("GiftItemLine", "{0}  (가진 {1})"), FindItemDisplayName(ItemID), FText::AsNumber(Owned[ItemID])),
			true, ItemID == SelectedItem, Index);
		BindRow(*Button, ItemID, [WeakThis](FName Key)
		{
			if (UGiftScreenWidget* const Self = WeakThis.Get())
			{
				Self->SelectItem(Key);
			}
		});
		if (UVerticalBoxSlot* const BoxSlot = Cast<UVerticalBoxSlot>(BodyList->AddChild(Box)))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

TSharedRef<SWidget> UGiftScreenWidget::RebuildWidget()
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

void UGiftScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (USnareBackendSubsystem* const Backend = BackendToRebind.Get())
	{
		BindToBackend(Backend);
	}
}

void UGiftScreenWidget::NativeDestruct()
{
	BackendToRebind = WatchedBackend;
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

void UGiftScreenWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GiftScreenRoot"));
	WidgetTree->RootWidget = Root;
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	auto MakeButton = [this](FName Name, const FText& Label) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Text"))));
		Text->SetText(Label);
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
		return Button;
	};

	// 열기 버튼 — 오른쪽 아래 기둥 맨 위, 다른 네모 버튼과 같은 크림 + 글자(아이콘 그림이 아직 없어 글자 「선물」).
	OpenButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("GiftOpenButton"));
	SnareUiStyle::MakeRoundButton(*OpenButton, OpenButtonRadius);
	OpenButton->SetBackgroundColor(SnareUiStyle::UiPanelCream());
	OpenButton->SetToolTipText(LOCTEXT("GiftOpenTip", "이웃에게 선물"));
	OpenButton->OnClicked.AddDynamic(this, &UGiftScreenWidget::HandleOpenClicked);
	// 기둥 (열 1, 행 2) — 선물 그림이 아직 없어 라벨 「선물」 만 가운데.
	// 아이콘 = 민트 선물 상자(`tools/unreal/import_ui_gift.py`). 자리는 오른쪽 열 맨 위 — 출석(목장에만 있다)이 왼쪽 위라
	// 사냥 맵에서 출석이 없어도 기둥 가운데가 비지 않는다.
	SnareUiStyle::PlacePillarButton(*WidgetTree, *Root, *OpenButton,
		LoadObject<UTexture2D>(nullptr, TEXT("/Game/Game/UI/Icons/T_UI_Gift.T_UI_Gift")),
		LOCTEXT("GiftOpen", "선물"), TEXT("GiftOpenLabel"), 0, 2);

	// 판 + 제목 알약 — 의뢰 창과 같은 틀(알약이 판 왼쪽 위에 걸침).
	const float PillRise = SnareUiStyle::MenuTitlePillRise;
	const float PillOverhang = SnareUiStyle::PanelTitlePillHeight - PillRise;
	UOverlay* const Frame = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("GiftFrame"));
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* const FrameSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Frame)))
	{
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.f));
		FrameSlot->SetPosition(FVector2D(0.f, PanelTop - PillRise));
		FrameSlot->SetAutoSize(true);
	}
	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("GiftPanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), SnareUiStyle::ScreenPanelRadius));
	Panel->SetPadding(FMargin(PanelPadding, PanelPadding + PillOverhang, PanelPadding, PanelPadding));
	if (UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Frame->AddChild(Panel)))
	{
		PanelSlot->SetPadding(FMargin(0.f, PillRise, 0.f, 0.f));
	}
	UTextBlock* const Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GiftTitle"));
	Title->SetText(LOCTEXT("GiftTitle", "선물"));
	TitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("GiftTitlePillBox"), TEXT("GiftTitlePill"), *Title);
	if (UOverlaySlot* const PillSlot = Cast<UOverlaySlot>(Frame->AddChild(TitlePillBox)))
	{
		PillSlot->SetHorizontalAlignment(HAlign_Left);
		PillSlot->SetVerticalAlignment(VAlign_Top);
	}

	USizeBox* const Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GiftWidth"));
	Width->SetWidthOverride(PanelWidth);
	Panel->SetContent(Width);
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GiftColumn"));
	Width->AddChild(Column);

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GiftMessageText"));
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	if (UVerticalBoxSlot* const MessageSlot = Cast<UVerticalBoxSlot>(Column->AddChild(MessageText)))
	{
		MessageSlot->SetPadding(FMargin(0.f, RowGap, 0.f, RowGap));
	}

	// 몸통 — 받을 선물·사람·물건 줄이 길어지면 이 안에서만 민다.
	USizeBox* const BodyBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GiftBodyBox"));
	BodyBox->SetHeightOverride(BodyHeight);
	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("GiftScroll"));
	BodyBox->AddChild(Scroll);
	BodyList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GiftBodyList"));
	Scroll->AddChild(BodyList);
	Column->AddChild(BodyBox);

	// 개수 줄: [−] N개 [＋] [보내기]
	UHorizontalBox* const SendRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GiftSendRow"));
	auto AddFixed = [this, SendRow](UWidget* Child, float W)
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(W);
		Box->SetHeightOverride(CloseHeight);
		Box->AddChild(Child);
		if (UHorizontalBoxSlot* const BoxSlot = Cast<UHorizontalBoxSlot>(SendRow->AddChild(Box)))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, RowGap, 0.f));
		}
	};
	UButton* const Minus = MakeButton(TEXT("GiftCountMinus"), LOCTEXT("GiftMinus", "−"));
	Minus->OnClicked.AddDynamic(this, &UGiftScreenWidget::HandleCountMinus);
	AddFixed(Minus, CountButtonWidth);
	CountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GiftCountText"));
	CountText->SetJustification(ETextJustify::Center);
	CountText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	{
		USizeBox* const CountBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GiftCountBox"));
		// 높이를 박지 않는다 — 박으면 글이 상자 위에 붙어 「1개」 가 위로 떴다(10-16). 세로 가운데는 줄 칸이 맞춘다.
		CountBox->SetWidthOverride(CountButtonWidth + 24.f);
		CountBox->AddChild(CountText);
		if (UHorizontalBoxSlot* const BoxSlot = Cast<UHorizontalBoxSlot>(SendRow->AddChild(CountBox)))
		{
			BoxSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	UButton* const Plus = MakeButton(TEXT("GiftCountPlus"), LOCTEXT("GiftPlus", "+"));
	Plus->OnClicked.AddDynamic(this, &UGiftScreenWidget::HandleCountPlus);
	AddFixed(Plus, CountButtonWidth);
	// 「보내기」 는 꺼져도 글이 또렷하게 — 글을 단추 위 겹에 둔다(10-16 「꺼진 보내기가 안 읽힌다」).
	UButton* Send = nullptr;
	UTextBlock* SendLabel = nullptr;
	UOverlay* const SendStack = SnareUiStyle::MakeClearLabelButton(*WidgetTree, TEXT("GiftSendButton"), LOCTEXT("GiftSend", "보내기"),
		SnareUiStyle::MenuFontMin + 4.f, Send, SendLabel);
	SendButton = Send;
	SendButton->OnClicked.AddDynamic(this, &UGiftScreenWidget::HandleSendClicked);
	{
		USizeBox* const SendBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GiftSendBox"));
		SendBox->SetHeightOverride(CloseHeight);
		SendBox->AddChild(SendStack);
		if (UHorizontalBoxSlot* const BoxSlot = Cast<UHorizontalBoxSlot>(SendRow->AddChild(SendBox)))
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
	if (UVerticalBoxSlot* const SendRowSlot = Cast<UVerticalBoxSlot>(Column->AddChild(SendRow)))
	{
		SendRowSlot->SetPadding(FMargin(0.f, RowGap, 0.f, 0.f));
	}

	// 닫기는 모든 메뉴 판과 같은 한 모양 (10-16).
	UButton* Close = nullptr;
	USizeBox* const CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("GiftCloseBox"), TEXT("GiftCloseButton"),
		LOCTEXT("GiftClose", "닫기"), Close);
	CloseButton = Close;
	CloseButton->OnClicked.AddDynamic(this, &UGiftScreenWidget::HandleCloseClicked);
	// 「다시 시도」 는 닫기와 같은 줄 왼쪽 — 판 높이를 늘리지 않는다. 서버에 닿지 못했을 때만 보인다.
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("GiftRetryBox"), TEXT("GiftRetryButton"),
		LOCTEXT("GiftRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &UGiftScreenWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	UHorizontalBox* const BottomRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GiftBottomRow"));
	if (UHorizontalBoxSlot* const RetrySlot = Cast<UHorizontalBoxSlot>(BottomRow->AddChild(RetryBox)))
	{
		RetrySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RetrySlot->SetHorizontalAlignment(HAlign_Left);
	}
	BottomRow->AddChild(CloseBox);
	if (UVerticalBoxSlot* const CloseSlot = Cast<UVerticalBoxSlot>(Column->AddChild(BottomRow)))
	{
		CloseSlot->SetPadding(FMargin(0.f, RowGap, 0.f, 0.f));
	}
}

void UGiftScreenWidget::HandleRetryClicked()
{
	TFunction<void()> Retry = MoveTemp(RetryRequest);
	RetryRequest = nullptr;
	Message = FText::GetEmpty();
	if (Retry)
	{
		Retry();
	}
	RefreshDisplay();
}

#undef LOCTEXT_NAMESPACE
