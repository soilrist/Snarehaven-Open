#include "UI/SnareNextGoalWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Backend/SnareQuestChain.h"
#include "Data/ItemRow.h"
#include "Data/QuestChainRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리 배치 (🟡 화면 배치 — 게임 규칙 숫자가 아니다). 위 가운데, 동물 카드(위 끝 ~100px) 아래 —
	// 첫 실행 안내 카드가 있던 줄이라 안내가 끝난 뒤에만 뜬다.
	constexpr float CardTop = 120.f;
	constexpr float FontSize = 22.f;
}

USnareNextGoalWidget::USnareNextGoalWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

FText USnareNextGoalWidget::FormatGoal(bool bInHasReply, const TArray<FQuestLineView>& InLines,
	TFunctionRef<int32(FName)> CountOwned, TFunctionRef<FText(FName)> DisplayName, int32 InChainStep)
{
	const FText Giver = LOCTEXT("QuestGiverName", "의뢰인 솔방울");
	FText Goal;
	if (!bInHasReply)
	{
		Goal = FText::Format(LOCTEXT("NextGoalFindGiver", "{0}에게 오늘 의뢰를 받아요"), Giver);
	}
	else if (const FQuestLineView* const Line = InLines.FindByPredicate([](const FQuestLineView& L) { return !L.bDone; }))
	{
		const int32 Owned = CountOwned(Line->ItemID);
		const FText Name = DisplayName(Line->ItemID);
		Goal = Owned >= Line->Count
			? FText::Format(LOCTEXT("NextGoalHandIn", "{0}에게 {1} {2}개 건네기 > {3}코인"), Giver, Name,
				FText::AsNumber(Line->Count), FText::AsNumber(Line->Reward))
			: FText::Format(LOCTEXT("NextGoalCollect", "{0} {1}개 모으기 ({2}/{1})"), Name,
				FText::AsNumber(Line->Count), FText::AsNumber(Owned));
	}
	else if (const UDataTable* const ChainTable = InChainStep >= 0 ? SnareQuestChain::LoadTable() : nullptr;
		ChainTable != nullptr && SnareQuestChain::IsFinished(ChainTable, InChainStep + 1))
	{
		return FText::GetEmpty();	// 하루 의뢰도 줄기도 끝 — 줄이 접힌다
	}
	else if (const FQuestChainRow* const Step = InChainStep >= 0 ? SnareQuestChain::FindStep(SnareQuestChain::LoadTable(), InChainStep + 1) : nullptr)
	{
		const FQuestChainNeed* Short = Step->Needs.FindByPredicate([&CountOwned](const FQuestChainNeed& N) { return CountOwned(N.ItemId) < N.Count; });
		Goal = Short != nullptr
			? FText::Format(LOCTEXT("NextGoalChainCollect", "{0}: {1} {2}개 모으기 ({3}/{2})"), Step->Title, DisplayName(Short->ItemId),
				FText::AsNumber(Short->Count), FText::AsNumber(CountOwned(Short->ItemId)))
			: FText::Format(LOCTEXT("NextGoalChainHandIn", "{0}에게 {1} 건네기 > {2}코인"), Giver, Step->Title, FText::AsNumber(Step->RewardCoins));
	}
	else
	{
		Goal = LOCTEXT("NextGoalAllDone", "오늘 의뢰 끝! 내일 새 의뢰가 와요");
	}
	return FText::Format(LOCTEXT("NextGoalFormat", "다음 목표: {0}"), Goal);
}

FText USnareNextGoalWidget::GetGoalText() const
{
	return FormatGoal(bHasReply, Lines, [this](FName ItemID) { return CountOwned(ItemID); },
		[this](FName ItemID) { return FindItemDisplayName(ItemID); }, ChainStep);
}

FText USnareNextGoalWidget::FindItemDisplayName(FName ItemID) const
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
	return FText::FromName(ItemID);
}

int32 USnareNextGoalWidget::CountOwned(FName ItemID) const
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

void USnareNextGoalWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &USnareNextGoalWidget::HandleBackendSnapshotChanged);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &USnareNextGoalWidget::HandleBackendSnapshotChanged);
	}
	RequestQuests();
	Refresh();
}

void USnareNextGoalWidget::HandleBackendSnapshotChanged()
{
	// 가진 개수는 스냅샷이 바뀌자마자 다시 그린다. 의뢰 완료·날 바뀜은 서버만 아니까 목록은 다시 묻는다.
	Refresh();
	if (bPending)
	{
		bRefreshQueued = true;
		return;
	}
	const double Wait = MinRefreshSeconds - (FPlatformTime::Seconds() - LastRequestSeconds);
	UWorld* const World = GetWorld();
	if (Wait <= 0.0 || World == nullptr)
	{
		RequestQuests();
		return;
	}
	if (!World->GetTimerManager().IsTimerActive(RefreshTimer))
	{
		const TWeakObjectPtr<USnareNextGoalWidget> WeakThis(this);
		World->GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (USnareNextGoalWidget* const Self = WeakThis.Get())
			{
				Self->RequestQuests();
			}
		}), static_cast<float>(Wait), false);
	}
}

bool USnareNextGoalWidget::RequestQuests()
{
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	bRefreshQueued = false;
	LastRequestSeconds = FPlatformTime::Seconds();
	const TWeakObjectPtr<USnareNextGoalWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetQuests"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareNextGoalWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Quest] 다음 목표 줄 — 의뢰 목록 거절: %s"), *Failure.ToString());
			}
			else
			{
				Self->ApplyQuestsReply(Response);
			}
			if (Self->bHasReply && !Self->Lines.ContainsByPredicate([](const FQuestLineView& L) { return !L.bDone; }))
			{
				Self->RequestChain();
			}
			if (Self->bRefreshQueued)
			{
				Self->HandleBackendSnapshotChanged();
			}
		});
	return true;
}

bool USnareNextGoalWidget::RequestChain()
{
	if (bChainPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bChainPending = true;
	const TWeakObjectPtr<USnareNextGoalWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetQuestChain"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareNextGoalWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bChainPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Quest] 다음 목표 줄 — 줄기 단계 거절: %s"), *Failure.ToString());
				return;
			}
			Self->ApplyChainReply(Response);
		});
	return true;
}

bool USnareNextGoalWidget::ApplyChainReply(const TSharedPtr<FJsonObject>& Response)
{
	double Step = 0.0;
	if (!Response.IsValid() || !Response->TryGetNumberField(TEXT("step"), Step))
	{
		return false;
	}
	ChainStep = static_cast<int32>(Step);
	Refresh();
	return true;
}

bool USnareNextGoalWidget::ApplyQuestsReply(const TSharedPtr<FJsonObject>& Response)
{
	if (!UQuestScreenWidget::ParseQuestLines(Response, Lines))
	{
		return false;
	}
	bHasReply = true;
	Refresh();
	return true;
}

void USnareNextGoalWidget::SetSuppressed(bool bInSuppressed)
{
	bSuppressed = bInSuppressed;
	Refresh();
}

void USnareNextGoalWidget::Refresh()
{
	if (GoalText != nullptr)
	{
		const FText Text = GetGoalText();
		GoalText->SetText(Text);
		if (Card != nullptr)
		{
			Card->SetVisibility(Text.IsEmpty() || bSuppressed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	}
}

void USnareNextGoalWidget::NativeDestruct()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

TSharedRef<SWidget> USnareNextGoalWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("NextGoalRoot"));
		WidgetTree->RootWidget = Root;

		Card = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("NextGoalCard"));
		Card->SetVisibility(ESlateVisibility::HitTestInvisible);
		Card->SetPadding(FMargin(20.f, 8.f));
		if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Card)))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.f));
			CardSlot->SetPosition(FVector2D(0.f, CardTop));
			CardSlot->SetAutoSize(true);
		}

		GoalText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NextGoalText"));
		GoalText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		Card->SetContent(GoalText);
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (GoalText != nullptr)
	{
		SnareUiStyle::SetFontSize(*GoalText, FontSize);
	}
	Refresh();
	return Result;
}

#undef LOCTEXT_NAMESPACE
