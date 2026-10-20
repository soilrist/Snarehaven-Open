#include "UI/QuestScreenWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
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
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Backend/SnareQuestChain.h"
#include "Data/ItemRow.h"
#include "Data/QuestChainRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "UI/SnareBalanceHud.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnarePromptCardWidget.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 배치 (Autonomy.md 🟡 — 화면 어디에 얼마나 크게). 상점 창(`UShopScreenWidget`)과 같은 크림 패널·줄 높이.
	constexpr float PanelWidth = 760.f;
	constexpr float PanelTop = 160.f;
	// 화면 오른쪽 위 HUD 코인 알약(`SnareBalanceHud`)을 판이 덮지 않게 판을 왼쪽으로 비킨다 — 알약은 숫자가 길어지면 왼쪽으로 늘어난다(M106-03).
	constexpr float PanelShiftLeft = 140.f;
	constexpr float PanelPadding = 24.f;
	constexpr float LineHeight = 96.f;
	constexpr float LineGap = 8.f;
	constexpr float CloseHeight = SnareUiStyle::MenuTouchMin;   // 10-16 애플 44pt (옛 80)
}

UQuestScreenWidget::UQuestScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

FText UQuestScreenWidget::FindItemDisplayName(FName ItemID) const
{
	if (IsValid(ItemTable))
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->ItemID == ItemID && !Row->DisplayName.IsEmpty())
			{
				return Row->DisplayName;
			}
		}
	}
	// 원 ID(`item.carrot`)를 화면에 흘리지 않는다 — 출석 판(M23-05)과 같은 꼴. 서버 표와 아이템 표가 어긋난 것이니
	// 로그로 알리고 화면엔 `물건` 만.
	if (!WarnedUnknownIDs.Contains(ItemID))
	{
		WarnedUnknownIDs.Add(ItemID);
		UE_LOG(LogSnarehaven, Warning, TEXT("[Quest] 아이템 표에 없는 의뢰 ID '%s' — 화면엔 '물건' 으로 보인다"), *ItemID.ToString());
	}
	return LOCTEXT("QuestUnknownItem", "물건");
}

int32 UQuestScreenWidget::CountOwned(FName ItemID) const
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

FText UQuestScreenWidget::GetLineText(const FQuestLineView& Line) const
{
	const FText Name = FindItemDisplayName(Line.ItemID);
	if (Line.bDone)
	{
		return FText::Format(LOCTEXT("QuestLineDone", "{0} x{1}  —  완료"), Name, FText::AsNumber(Line.Count));
	}
	return FText::Format(LOCTEXT("QuestLine", "{0} x{1} (가진 {2})  >  {3}코인"), Name, FText::AsNumber(Line.Count),
		FText::AsNumber(CountOwned(Line.ItemID)), FText::AsNumber(Line.Reward));
}

void UQuestScreenWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UQuestScreenWidget::HandleBackendSnapshotChanged);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &UQuestScreenWidget::HandleBackendSnapshotChanged);
	}
	RefreshDisplay();
}

void UQuestScreenWidget::HandleBackendSnapshotChanged() { RefreshDisplay(); }
void UQuestScreenWidget::HandleOpenClicked() { SetOpen(true); }
void UQuestScreenWidget::HandleRetryClicked()
{
	Message = FText::GetEmpty();
	bListFailed = false;
	RequestQuests();
	RefreshDisplay();
}

void UQuestScreenWidget::HandleCloseClicked() { SetOpen(false); }
void UQuestScreenWidget::HandleTabDailyClicked() { SetTab(EQuestTab::Daily); }
void UQuestScreenWidget::HandleTabChainClicked() { SetTab(EQuestTab::Chain); }
void UQuestScreenWidget::HandleChainClaimClicked() { RequestChainComplete(); }

void UQuestScreenWidget::SetTab(EQuestTab InTab)
{
	ActiveTab = InTab;
	Message = FText::GetEmpty();
	if (ActiveTab == EQuestTab::Chain)
	{
		RequestChain();
	}
	RefreshDisplay();
}

bool UQuestScreenWidget::ApplyChainReply(const TSharedPtr<FJsonObject>& Response)
{
	double Step = 0.0;
	if (!Response.IsValid() || !Response->TryGetNumberField(TEXT("step"), Step))
	{
		return false;
	}
	ChainStep = static_cast<int32>(Step);
	RefreshDisplay();
	return true;
}

bool UQuestScreenWidget::CanClaimChain() const
{
	const UDataTable* const Table = SnareQuestChain::LoadTable();
	const FQuestChainRow* const Row = ChainStep >= 0 ? SnareQuestChain::FindStep(Table, ChainStep + 1) : nullptr;
	if (Row == nullptr)
	{
		return false;
	}
	TMap<FName, int32> Have;
	for (const FQuestChainNeed& Need : Row->Needs)
	{
		Have.Add(Need.ItemId, CountOwned(Need.ItemId));
	}
	return SnareQuestChain::IsGoalMet(*Row, Have);
}

FText UQuestScreenWidget::GetChainTitleText() const
{
	if (ChainStep < 0)
	{
		return LOCTEXT("QuestChainUnknown", "확인 중...");
	}
	const UDataTable* const Table = SnareQuestChain::LoadTable();
	if (SnareQuestChain::IsFinished(Table, ChainStep + 1))
	{
		return LOCTEXT("QuestChainFinished", "솔방울의 부탁을 모두 들어줬다. 고마워!");
	}
	const FQuestChainRow* const Row = SnareQuestChain::FindStep(Table, ChainStep + 1);
	return FText::Format(LOCTEXT("QuestChainTitle", "{0}단계  {1}"), FText::AsNumber(ChainStep + 1),
		Row != nullptr ? Row->Title : FText::GetEmpty());
}

void UQuestScreenWidget::SetChainPreview(int32 StepOneBased)
{
	bChainPreview = true;
	ChainStep = FMath::Max(StepOneBased - 1, 0);
	ActiveTab = EQuestTab::Chain;
	SetOpen(true);
}

void UQuestScreenWidget::OpenOnChainTab()
{
	SetOpen(true);   // 목록 요청이 떠 있으면 줄기 요청은 그 답이 올 때 이어서 나간다 (RequestQuests 답 처리)
	SetTab(EQuestTab::Chain);
}

bool UQuestScreenWidget::RequestChain()
{
	if (bChainPreview)
	{
		return false;   // 검사용 — 단계는 표에서 채웠고 서버를 안 부른다
	}
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	const TWeakObjectPtr<UQuestScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetQuestChain"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UQuestScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Quest] 줄기 단계 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
			}
			Self->ApplyChainReply(Response);
			Self->RefreshDisplay();
		});
	RefreshDisplay();
	return true;
}

bool UQuestScreenWidget::RequestChainComplete()
{
	if (bChainPreview)
	{
		Message = LOCTEXT("QuestChainPreviewPending", "확인 중...");   // 서버 호출도 지급 반영도 없다 (CLAUDE.md 8)
		RefreshDisplay();
		return false;
	}
	if (bPending || ChainStep < 0 || !IsValid(WatchedBackend))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("step"), ChainStep + 1);
	bPending = true;
	Message = FText::GetEmpty();
	RefreshDisplay();

	const TWeakObjectPtr<UQuestScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("CompleteQuestChainStep"), Args,
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UQuestScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (Failure.IsEmpty())
			{
				const int32 Coins = Response.IsValid() ? static_cast<int32>(Response->GetNumberField(TEXT("coins"))) : 0;
				USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);   // 서버가 보상을 확정한 뒤
				Self->Message = FText::Format(LOCTEXT("QuestChainDone", "고마워! {0}코인을 받았다"), FText::AsNumber(Coins));
				if (IsValid(Self->WatchedBackend))
				{
					Self->WatchedBackend->FetchPlayerState();
				}
			}
			else
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Quest] 줄기 받기 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
			}
			// 단계는 서버가 바꿨다(또는 안 바꿨다) — 손으로 올리지 않고 다시 받는다 (CLAUDE.md 8).
			Self->RequestChain();
		});
	return true;
}

void UQuestScreenWidget::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	if (bOpen)
	{
		RequestQuests();
	}
	RefreshDisplay();
}

void UQuestScreenWidget::SetGiverName(const FText& InName)
{
	GiverName = InName;
	RefreshDisplay();
}

bool UQuestScreenWidget::ParseQuestLines(const TSharedPtr<FJsonObject>& Response, TArray<FQuestLineView>& OutLines)
{
	const TArray<TSharedPtr<FJsonValue>>* Quests = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("quests"), Quests))
	{
		return false;
	}
	OutLines.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Quests)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object))
		{
			continue;
		}
		FQuestLineView Line;
		Line.QuestID = FName((*Object)->GetStringField(TEXT("id")));
		Line.ItemID = FName((*Object)->GetStringField(TEXT("itemId")));
		Line.Count = (*Object)->GetIntegerField(TEXT("count"));
		Line.Reward = (*Object)->GetIntegerField(TEXT("reward"));
		(*Object)->TryGetBoolField(TEXT("done"), Line.bDone);
		OutLines.Add(Line);
	}
	return true;
}

bool UQuestScreenWidget::ApplyQuestsReply(const TSharedPtr<FJsonObject>& Response)
{
	if (!ParseQuestLines(Response, Lines))
	{
		return false;
	}
	RefreshDisplay();
	return true;
}

bool UQuestScreenWidget::RequestQuests()
{
	if (bChainPreview)
	{
		return false;   // 검사용 — 서버를 안 부른다
	}
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	const TWeakObjectPtr<UQuestScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetQuests"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UQuestScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			Self->bListFailed = !Failure.IsEmpty();
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Quest] 의뢰 목록 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
			}
			Self->ApplyQuestsReply(Response);
			Self->RefreshDisplay();
			if (Self->ActiveTab == EQuestTab::Chain && Self->ChainStep < 0)
			{
				Self->RequestChain();   // 줄기 탭을 먼저 눌러 둔 채 목록 답이 늦은 경우
			}
		});
	RefreshDisplay();
	return true;
}

bool UQuestScreenWidget::RequestComplete(FName QuestID)
{
	if (bPending || QuestID.IsNone() || !IsValid(WatchedBackend))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("questId"), QuestID.ToString());
	bPending = true;
	Message = FText::GetEmpty();
	RefreshDisplay();

	const TWeakObjectPtr<UQuestScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("CompleteQuest"), Args,
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UQuestScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Quest] 건네기 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				Self->RefreshDisplay();
				return;
			}
			const int32 Coins = Response.IsValid() ? static_cast<int32>(Response->GetNumberField(TEXT("coins"))) : 0;
			USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);   // 서버가 보상을 확정한 뒤 (DESIGN 2.2 소리)
			Self->Message = FText::Format(LOCTEXT("QuestDone", "고마워! {0}코인을 받았다"), FText::AsNumber(Coins));
			// 코인·배낭·완료 표시는 서버가 바꿨다 — 손으로 고치지 않고 다시 받는다 (CLAUDE.md 8).
			if (IsValid(Self->WatchedBackend))
			{
				Self->WatchedBackend->FetchPlayerState();
			}
			Self->RequestQuests();
		});
	return true;
}

void UQuestScreenWidget::RefreshDisplay()
{
	if (IsValid(OpenButton))
	{
		OpenButton->SetVisibility(!bOpen && bPromptOnScreen && !bPromptSuppressed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Panel))
	{
		Panel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TitlePillBox))
	{
		// 판 밖에 걸친 알약도 같이 접는다 — 닫힌 판의 제목만 HUD 위에 남지 않게(출석 판과 같은 꼴).
		TitlePillBox->SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(GiverNameText))
	{
		GiverNameText->SetText(GiverName);
	}
	if (IsValid(MessageText))
	{
		MessageText->SetText(bPending ? LOCTEXT("QuestPending", "확인 중...")
			: !Message.IsEmpty() ? Message
			: ActiveTab == EQuestTab::Chain ? LOCTEXT("QuestChainHint", "내가 한 번만 거는 부탁이야. 순서대로 하나씩.")
			: LOCTEXT("QuestHint", "오늘 부탁할 것들이야. 가져오면 코인을 줄게."));
	}
	if (IsValid(RetryBox))
	{
		// 목록을 못 받았으면 막다른 길이 아니게 — 「다시 시도」 하나 (10-16 검수).
		RetryBox->SetVisibility(bListFailed && !bPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// 탭 단추: 지금 탭은 또렷하게, 다른 탭은 흐리게(눌리긴 한다). 꺼진 회색은 「못 누름」으로 읽혀 지금 탭이 헷갈렸다.
	if (IsValid(TabDailyButton))
	{
		TabDailyButton->SetRenderOpacity(ActiveTab == EQuestTab::Daily ? 1.f : 0.6f);
	}
	if (IsValid(TabChainButton))
	{
		TabChainButton->SetRenderOpacity(ActiveTab == EQuestTab::Chain ? 1.f : 0.6f);
	}
	if (!IsValid(LineList) || WidgetTree == nullptr)
	{
		return;
	}

	LineList->ClearChildren();
	LineProxies.Reset();
	if (ActiveTab == EQuestTab::Chain)
	{
		auto AddChainText = [this](const FText& Value, const FName Name)
		{
			UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Text->SetText(Value);
			Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));   // 크림 판 위 글자 — 기본 흰색이면 안 읽힌다
			Text->SetAutoWrapText(true);
			if (UVerticalBoxSlot* const TextSlot = Cast<UVerticalBoxSlot>(LineList->AddChild(Text)))
			{
				TextSlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
			}
		};
		AddChainText(GetChainTitleText(), TEXT("QuestChainTitleText"));
		const UDataTable* const Table = SnareQuestChain::LoadTable();
		const FQuestChainRow* const Row = ChainStep >= 0 ? SnareQuestChain::FindStep(Table, ChainStep + 1) : nullptr;
		if (Row != nullptr)
		{
			int32 NeedIndex = 0;
			for (const FQuestChainNeed& Need : Row->Needs)
			{
				AddChainText(FText::Format(LOCTEXT("QuestChainNeed", "{0} x{1} (가진 {2})"), FindItemDisplayName(Need.ItemId),
					FText::AsNumber(Need.Count), FText::AsNumber(CountOwned(Need.ItemId))),
					FName(*FString::Printf(TEXT("QuestChainNeed_%d"), NeedIndex++)));
			}
			FText Reward = FText::Format(LOCTEXT("QuestChainReward", "보상  {0}코인"), FText::AsNumber(Row->RewardCoins));
			if (!Row->RewardItemId.IsNone() && Row->RewardItemCount > 0)
			{
				Reward = FText::Format(LOCTEXT("QuestChainRewardItem", "{0} + {1} x{2}"), Reward,
					FindItemDisplayName(Row->RewardItemId), FText::AsNumber(Row->RewardItemCount));
			}
			AddChainText(Reward, TEXT("QuestChainRewardText"));

			USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("QuestChainClaimBox"));
			Box->SetHeightOverride(LineHeight);
			UButton* const Claim = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuestChainClaimButton"));
			SnareUiStyle::MakeRoundButton(*Claim, SnareUiStyle::HudCornerRadius);
			UTextBlock* const ClaimText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuestChainClaimText"));
			ClaimText->SetText(LOCTEXT("QuestChainClaim", "받기"));
			ClaimText->SetJustification(ETextJustify::Center);
			Claim->AddChild(ClaimText);
			// 재료가 모자라거나 답을 기다리는 동안은 못 누른다 — 서버도 거절하지만 누를 수 있어 보이면 헷갈린다.
			const bool bCanClaim = !bPending && CanClaimChain();
			Claim->SetIsEnabled(bCanClaim);
			// 꺼진 단추는 켜진 「닫기」와 같은 진갈색이라 왜 안 눌리는지 몰랐다 — 꺼진 탭과 같은 옅은 판 + 이유 글.
			if (!bCanClaim)
			{
				Claim->SetRenderOpacity(0.6f);
				if (!bPending)
				{
					ClaimText->SetText(LOCTEXT("QuestChainClaimShort", "재료가 모자라요"));
				}
			}
			Claim->OnClicked.AddDynamic(this, &UQuestScreenWidget::HandleChainClaimClicked);
			Box->AddChild(Claim);
			LineList->AddChild(Box);
		}
		SnareUiStyle::ApplyUiStyle(*this);
		return;
	}
	int32 Index = 0;
	for (const FQuestLineView& Line : Lines)
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("QuestLineBox_%d"), Index)));
		Box->SetHeightOverride(LineHeight);
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("QuestLineButton_%d"), Index)));
		// M27-02 시트 「모든 패널 모서리 둥글게」 — 모양만 바꾸고 색은 `ApplyUiStyle` 몫.
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("QuestLineText_%d"), Index)));
		Text->SetText(GetLineText(Line));
		Button->AddChild(Text);
		// 끝냈거나 모자라면 못 누른다 — 서버도 거절하지만 누를 수 있어 보이면 헷갈린다.
		// 단추를 끄면 슬레이트가 글자를 반쯤 투명하게 깎아 크림 글자가 안 읽힌다(M106-03 감독 캡처) —
		// 켠 채로 배경만 어둡게 하고, 못 내는 줄은 눌러도 아무 일 없게 아래 클릭 길에서 막는다.
		const bool bCanHandIn = !Line.bDone && CountOwned(Line.ItemID) >= Line.Count;
		Button->SetIsEnabled(!bPending);
		if (!bCanHandIn)
		{
			Button->SetBackgroundColor(FLinearColor(0.78f, 0.78f, 0.78f, 1.f));
		}
		Box->AddChild(Button);

		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->Key = Line.QuestID;
		const TWeakObjectPtr<UQuestScreenWidget> WeakThis(this);
		Proxy->OnClicked = [WeakThis, bCanHandIn](FName QuestID)
		{
			if (UQuestScreenWidget* const Self = WeakThis.Get(); Self != nullptr && bCanHandIn)
			{
				Self->RequestComplete(QuestID);
			}
		};
		Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		LineProxies.Add(Proxy);
		if (UVerticalBoxSlot* const LineSlot = Cast<UVerticalBoxSlot>(LineList->AddChild(Box)))
		{
			LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
		}
		++Index;
	}
	SnareUiStyle::ApplyUiStyle(*this);
}

TSharedRef<SWidget> UQuestScreenWidget::RebuildWidget()
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

void UQuestScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 상인은 멀어지면 화면을 내리고(`RemoveFromParent` → `NativeDestruct`) 다시 오면 **같은 화면**을 올린다 —
	// 여기서 다시 묶지 않으면 "연결 안 됨"·빈 팔기 목록으로 굳는다(감독 PIE 재현, M22).
	if (USnareBackendSubsystem* const Backend = BackendToRebind.Get())
	{
		BindToBackend(Backend);
	}
}

void UQuestScreenWidget::SetPromptTarget(const FVector& WorldLocation)
{
	PromptTarget = WorldLocation;
	bPromptOnScreen = false;
	RefreshDisplay();
}

void UQuestScreenWidget::SetPromptSuppressed(bool bSuppressed)
{
	if (bPromptSuppressed != bSuppressed)
	{
		bPromptSuppressed = bSuppressed;
		RefreshDisplay();
	}
}

bool UQuestScreenWidget::PlaceOpenButton(const FVector2D& TargetPx, const FVector2D& ViewportPx)
{
	FVector2D TopLeft = FVector2D::ZeroVector;
	const bool bOnScreen = USnarePromptCardWidget::ComputeTapCardPosition(TargetPx, ViewportPx, TopLeft);
	UCanvasPanelSlot* const OpenSlot = IsValid(OpenButton) ? Cast<UCanvasPanelSlot>(OpenButton->Slot) : nullptr;
	if (bOnScreen && OpenSlot != nullptr)
	{
		OpenSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		OpenSlot->SetAlignment(FVector2D::ZeroVector);
		OpenSlot->SetPosition(TopLeft);
	}
	if (bOnScreen != bPromptOnScreen)
	{
		bPromptOnScreen = bOnScreen;
		RefreshDisplay();
	}
	return bOnScreen;
}

void UQuestScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen || !PromptTarget.IsSet())
	{
		return;
	}
	FVector2D TargetPx = FVector2D::ZeroVector;
	FVector2D ViewportPx = FVector2D::ZeroVector;
	const bool bProjected = USnarePromptCardWidget::ProjectTarget(GetOwningPlayer(), PromptTarget.GetValue(), TargetPx, ViewportPx);
	PlaceOpenButton(bProjected ? TargetPx : FVector2D(-1.f, -1.f), ViewportPx);
}

void UQuestScreenWidget::NativeDestruct()
{
	BackendToRebind = WatchedBackend;
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

void UQuestScreenWidget::BuildTree()
{
	UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("QuestScreenRoot"));
	WidgetTree->RootWidget = Root;

	auto MakeText = [this](FName Name) -> UTextBlock*
	{
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		return Text;
	};
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

	// 의뢰인 머리 옆 크림 카드 — 상점 열기 카드와 같은 한 벌 (M23-02).
	OpenButton = USnarePromptCardWidget::MakeTapCard(*WidgetTree, TEXT("QuestOpenButton"), LOCTEXT("QuestOpen", "의뢰 보기"),
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_TreasureMap.T_Icon_TreasureMap"));
	OpenButton->OnClicked.AddDynamic(this, &UQuestScreenWidget::HandleOpenClicked);
	if (UCanvasPanelSlot* const OpenSlot = Cast<UCanvasPanelSlot>(Root->AddChild(OpenButton)))
	{
		OpenSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		OpenSlot->SetAlignment(FVector2D(0.f, 0.5f));
		OpenSlot->SetPosition(FVector2D(USnarePromptCardWidget::TapCardGapPx, 0.f));
		OpenSlot->SetSize(FVector2D(USnarePromptCardWidget::TapCardWidth, USnarePromptCardWidget::TapCardHeight));
	}

	// 판은 자동 크기라 판과 제목 알약을 한 겹(Overlay)에 두어 알약이 늘 판 왼쪽 위에 붙게 한다(출석 판 M24-03 과 같은 길).
	// 겹은 알약이 위로 나온 만큼 올려 판 위쪽 변은 `PanelTop` 그대로.
	const float PillRise = SnareUiStyle::MenuTitlePillRise;
	const float PillOverhang = SnareUiStyle::PanelTitlePillHeight - PillRise;   // 알약이 판 안으로 내려온 몫
	UOverlay* const Frame = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("QuestFrame"));
	Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* const FrameSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Frame)))
	{
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.f));
		FrameSlot->SetPosition(FVector2D(-PanelShiftLeft, PanelTop - PillRise));
		FrameSlot->SetAutoSize(true);
	}

	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("QuestPanel"));
	// 판 = 상점 판과 같은 크림 둥근 판(M27-02, 시트 `inventory_backpack.md` 「패널 크림 #F5EBD2 … 모서리 16px」).
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiPanelCream(), SnareUiStyle::ScreenPanelRadius));
	// 위 여백에 알약이 판 안으로 내려온 몫을 더한다 — 의뢰인 이름 줄이 알약에 안 가리게.
	Panel->SetPadding(FMargin(PanelPadding, PanelPadding + PillOverhang, PanelPadding, PanelPadding));
	if (UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Frame->AddChild(Panel)))
	{
		PanelSlot->SetPadding(FMargin(0.f, PillRise, 0.f, 0.f));
	}

	// 제목 탭 — 판 **뒤에** 넣어야 판 위에 그려진다. 배낭·제작·출석·도감 판과 같은 진갈색 알약.
	UTextBlock* const Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuestTitle"));
	Title->SetText(LOCTEXT("QuestTitle", "의뢰"));
	TitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("QuestTitlePillBox"), TEXT("QuestTitlePill"), *Title);
	if (UOverlaySlot* const PillSlot = Cast<UOverlaySlot>(Frame->AddChild(TitlePillBox)))
	{
		PillSlot->SetHorizontalAlignment(HAlign_Left);
		PillSlot->SetVerticalAlignment(VAlign_Top);
	}
	USizeBox* const Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("QuestWidth"));
	Width->SetWidthOverride(PanelWidth);
	Panel->SetContent(Width);
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("QuestColumn"));
	Width->AddChild(Column);

	UHorizontalBox* const Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("QuestHeader"));
	GiverNameText = MakeText(TEXT("GiverNameText"));
	// 코인 글자는 판 안에 두지 않는다 — HUD 코인 알약이 이미 보여 주고 판 오른쪽 위와 겹쳤다(M106-03).
	if (UHorizontalBoxSlot* const HeaderSlot = Cast<UHorizontalBoxSlot>(Header->AddChild(GiverNameText)))
	{
		HeaderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	Column->AddChild(Header);

	// 탭 두 개 — 오늘 의뢰 / 이어지는 의뢰 (M105-04, 10-16 「줄기」 는 뜻이 안 읽혀 바꿈). 지금 탭은 또렷하고 다른 탭은 흐리다.
	UHorizontalBox* const TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("QuestTabRow"));
	TabDailyButton = MakeButton(TEXT("QuestTabDaily"), LOCTEXT("QuestTabDaily", "오늘 의뢰"));
	TabChainButton = MakeButton(TEXT("QuestTabChain"), LOCTEXT("QuestTabChain", "이어지는 의뢰"));
	TabDailyButton->OnClicked.AddDynamic(this, &UQuestScreenWidget::HandleTabDailyClicked);
	TabChainButton->OnClicked.AddDynamic(this, &UQuestScreenWidget::HandleTabChainClicked);
	for (UButton* const Tab : {TabDailyButton.Get(), TabChainButton.Get()})
	{
		USizeBox* const TabBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		TabBox->SetHeightOverride(CloseHeight);
		TabBox->AddChild(Tab);
		if (UHorizontalBoxSlot* const TabSlot = Cast<UHorizontalBoxSlot>(TabRow->AddChild(TabBox)))
		{
			TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TabSlot->SetPadding(FMargin(0.f, LineGap, LineGap, 0.f));
		}
	}
	Column->AddChild(TabRow);

	MessageText = MakeText(TEXT("QuestMessageText"));
	MessageText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* const MessageSlot = Cast<UVerticalBoxSlot>(Column->AddChild(MessageText)))
	{
		MessageSlot->SetPadding(FMargin(0.f, LineGap, 0.f, LineGap * 2.f));
	}

	UButton* const Retry = MakeButton(TEXT("QuestRetryButton"), LOCTEXT("QuestRetry", "다시 시도"));
	Retry->OnClicked.AddDynamic(this, &UQuestScreenWidget::HandleRetryClicked);
	RetryBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("QuestRetryBox"));
	RetryBox->SetHeightOverride(CloseHeight);
	RetryBox->SetMinDesiredWidth(CloseHeight * 2.f);
	RetryBox->AddChild(Retry);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* const RetrySlot = Cast<UVerticalBoxSlot>(Column->AddChild(RetryBox)))
	{
		RetrySlot->SetHorizontalAlignment(HAlign_Left);
		RetrySlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
	}

	LineList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("QuestLineList"));
	Column->AddChild(LineList);

	// 닫기는 모든 메뉴 판과 같은 한 모양 (10-16).
	UButton* Close = nullptr;
	USizeBox* const CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("QuestCloseBox"), TEXT("QuestCloseButton"),
		LOCTEXT("QuestClose", "닫기"), Close);
	CloseButton = Close;
	CloseButton->OnClicked.AddDynamic(this, &UQuestScreenWidget::HandleCloseClicked);
	if (UVerticalBoxSlot* const CloseSlot = Cast<UVerticalBoxSlot>(Column->AddChild(CloseBox)))
	{
		CloseSlot->SetHorizontalAlignment(HAlign_Right);
		CloseSlot->SetPadding(FMargin(0.f, LineGap, 0.f, 0.f));
	}
}

#undef LOCTEXT_NAMESPACE
