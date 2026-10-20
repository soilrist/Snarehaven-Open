#include "UI/SnareFirstRunGuideWidget.h"
#include "SnareCheckSwitch.h"

#include "Snarehaven.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Building/SnareBaseSubsystem.h"
#include "Combat/CaptureComponent.h"
#include "Data/ItemRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Combat/MeleeAttackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareUiStyle.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const TCHAR* GuideSection = TEXT("Snarehaven.Guide");
	const TCHAR* GuideSeenKey = TEXT("Seen");

	// 자리 배치 (🟡 화면 배치 — 게임 규칙 숫자가 아니다). 위쪽 가운데, 알림 카드(왼쪽 위)와 안 겹친다.
	constexpr float CardTop = 90.f;
	constexpr float CardWidth = 760.f;
	constexpr float TextFontSize = 30.f;
	constexpr float ButtonFontSize = 26.f;
	constexpr float ButtonGap = 16.f;

	// 장 번호 — GetSteps() 순서와 같다.
	constexpr int32 StepPickUp = 1;
	constexpr int32 StepBuyHatchet = 2;
	constexpr int32 StepChop = 3;
	constexpr int32 StepAttack = 4;
	constexpr int32 StepCapture = 5;
	constexpr int32 StepRanch = 6;
	constexpr int32 StepHarvest = 7;
	constexpr int32 StepTent = 8;
	constexpr int32 StepWorkbench = 9;
	constexpr int32 StepFire = 10;

	// 마른 가지 더미·나무가 주는 것과 나무 베는 도구 (DESIGN 3.3 채집물 · 8.10 나무 베기 · 8.14 잡화점).
	const FName WoodItemID = TEXT("item.wood");
	const FName HatchetItemID = TEXT("item.hatchet");
	// 텐트 작업대의 첫 레시피 (DESIGN 8.10 나무판 = 나무 4).
	const FName PlankItemID = TEXT("item.plank");

	/** 서버가 준 내 텐트인가 — 남의 사본은 시각이 없다 (`USnareBaseSubsystem::IsOwnFurniture` 와 같은 줄). */
	bool IsOwnTent(const FSnareTentMirror& Tent)
	{
		return !Tent.PlacedAtUtc.IsEmpty();
	}

	/** 요리·제련을 하는 자리인가 — 아이템 표의 `PlaceableFunction` 으로 본다 (모닥불·정원 화로·그릴·제련소). */
	bool IsFireOrSmelter(const USnareBaseSubsystem& Base, FName ItemID)
	{
		const FItemRow* const Row = Base.FindItemRow(ItemID);
		if (Row == nullptr)
		{
			return false;
		}
		switch (Row->PlaceableFunction)
		{
		case EPlaceableFunction::Campfire:
		case EPlaceableFunction::Grill:
		case EPlaceableFunction::Smelter:
			return true;
		default:
			return false;
		}
	}
}

const TArray<FText>& USnareFirstRunGuideWidget::GetSteps()
{
	static const TArray<FText> Steps = {
		LOCTEXT("GuideStepMove", "왼쪽 아래 조이스틱으로 걸어 다녀요."),
		LOCTEXT("GuideStepPickUp", "맨손으로 마른 가지 더미 앞에 서서 줍기 버튼을 눌러요."),
		LOCTEXT("GuideStepBuyHatchet", "나무는 손도끼로 베요. 가진 코인 10으로 잡화점에서 손도끼를 사요."),
		LOCTEXT("GuideStepChop", "도구 버튼으로 손도끼를 들고 나무 앞에서 베기 버튼을 눌러요."),
		LOCTEXT("GuideStepAttack", "동물 가까이 가서 공격 버튼으로 체력을 깎아요."),
		LOCTEXT("GuideStepCapture", "포획 버튼을 누르고 원이 초록일 때 한 번 더!"),
		LOCTEXT("GuideStepRanch", "잡은 동물은 목장 칸에 넣어 두세요. 시간이 지나면 아이템을 줘요."),
		LOCTEXT("GuideStepHarvest", "칸에 아이템이 쌓이면 수확 버튼으로 받아요."),
		LOCTEXT("GuideStepTent", "코인을 모아 잡화점에서 텐트를 사고, 가방에서 놓기로 빈 땅에 세워요."),
		LOCTEXT("GuideStepWorkbench", "텐트 앞에서 열기를 누르면 작업대예요. 나무 4개로 나무판을 만들어요."),
		LOCTEXT("GuideStepFire", "배낭 제작에서 모닥불을 만들어 텐트 땅에 놓아요. 불에선 요리, 제련소에선 광석을 녹여요."),
	};
	return Steps;
}

bool USnareFirstRunGuideWidget::IsGuideSeen()
{
	bool bSeen = false;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(GuideSection, GuideSeenKey, bSeen, GGameUserSettingsIni);
	}
	return bSeen;
}

void USnareFirstRunGuideWidget::SetGuideSeen(bool bSeen)
{
	if (GConfig != nullptr)
	{
		GConfig->SetBool(GuideSection, GuideSeenKey, bSeen, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool USnareFirstRunGuideWidget::ShouldShowGuide()
{
	return !SnareCheckSwitch::Param(TEXT("SnareNoGuide")) && !IsGuideSeen();
}

void USnareFirstRunGuideWidget::Advance()
{
	if (IsFinished())
	{
		return;
	}
	++StepIndex;
	if (IsFinished())
	{
		Finish();
		return;
	}
	Refresh();
	// 텐트·불을 이미 세워 둔 사람은 그 장에 들어서자마자 넘긴다.
	HandleBaseChanged();
}

void USnareFirstRunGuideWidget::Skip()
{
	StepIndex = GetSteps().Num();
	Finish();
}

void USnareFirstRunGuideWidget::BindToCharacter(ASnarehavenCharacter* Character)
{
	if (!IsValid(Character))
	{
		return;
	}
	if (UMeleeAttackComponent* const Melee = Character->GetMeleeAttack())
	{
		Melee->OnMeleeHit.AddUniqueDynamic(this, &USnareFirstRunGuideWidget::HandleMeleeHit);
	}
	if (UCaptureComponent* const Capture = Character->GetCapture())
	{
		Capture->OnAnimalCaptured.AddUniqueDynamic(this, &USnareFirstRunGuideWidget::HandleAnimalCaptured);
	}
	BoundInventory = Character->GetInventory();
	if (BoundInventory != nullptr)
	{
		BoundInventory->OnInventoryCacheChanged.AddUniqueDynamic(this, &USnareFirstRunGuideWidget::HandleInventoryChanged);
		LastWoodCount = BoundInventory->GetItemCount(WoodItemID);
		LastPlankCount = BoundInventory->GetItemCount(PlankItemID);
	}
	if (URanchSyncComponent* const Sync = Character->FindComponentByClass<URanchSyncComponent>())
	{
		Sync->OnRanchRequestAnswered.AddUniqueDynamic(this, &USnareFirstRunGuideWidget::HandleRanchAnswered);
	}
	const UWorld* const World = Character->GetWorld();
	if (UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr)
	{
		BindToBase(GameInstance->GetSubsystem<USnareBaseSubsystem>());
	}
}

void USnareFirstRunGuideWidget::BindToBase(USnareBaseSubsystem* Base)
{
	if (USnareBaseSubsystem* const Old = BoundBase.Get())
	{
		Old->OnBaseChanged.Remove(BaseChangedHandle);
	}
	BoundBase = Base;
	BaseChangedHandle.Reset();
	if (Base != nullptr)
	{
		BaseChangedHandle = Base->OnBaseChanged.AddUObject(this, &USnareFirstRunGuideWidget::HandleBaseChanged);
	}
}

void USnareFirstRunGuideWidget::HandleBaseChanged()
{
	const USnareBaseSubsystem* const Base = BoundBase.Get();
	if (Base == nullptr || (StepIndex != StepTent && StepIndex != StepFire))
	{
		return;
	}
	// 그 장에 와 있을 때만 본다 — 미리 세워 둔 텐트가 공격·포획 장을 건너뛰게 하지 않는다.
	for (const FSnareTentMirror& Tent : Base->GetTents())
	{
		if (!IsOwnTent(Tent))
		{
			continue;
		}
		if (StepIndex == StepTent)
		{
			CompleteStep(StepTent);
			return;
		}
		for (const FSnareStructureMirror& Structure : Tent.Structures)
		{
			if (IsFireOrSmelter(*Base, Structure.ItemID))
			{
				CompleteStep(StepFire);
				return;
			}
		}
	}
}

void USnareFirstRunGuideWidget::HandleMeleeHit(AAnimalCharacter* Target, float Damage)
{
	CompleteStep(StepAttack);
}

void USnareFirstRunGuideWidget::HandleAnimalCaptured(AAnimalCharacter* Target, FName PetItemID)
{
	// 포획 판정만으로는 안 넘긴다 — 펫은 아직 지급 대기열에 있다. 서버가 넣어 준 스냅샷을 기다린다.
	PendingPetID = PetItemID;
}

void USnareFirstRunGuideWidget::HandleInventoryChanged()
{
	if (BoundInventory == nullptr)
	{
		return;
	}
	// 나뭇가지가 늘면 지금 장이 줍기·베기일 때만 넘긴다 — 손도끼 장에서 가지를 더 주워도 그 장은 남는다.
	const int32 WoodCount = BoundInventory->GetItemCount(WoodItemID);
	if (WoodCount > LastWoodCount && (StepIndex == StepPickUp || StepIndex == StepChop))
	{
		CompleteStep(StepIndex);
	}
	LastWoodCount = WoodCount;
	// 나무판이 늘면 작업대 장만 넘긴다 (서버 `CraftItem` 뒤 스냅샷).
	const int32 PlankCount = BoundInventory->GetItemCount(PlankItemID);
	if (PlankCount > LastPlankCount && StepIndex == StepWorkbench)
	{
		CompleteStep(StepWorkbench);
	}
	LastPlankCount = PlankCount;
	if (StepIndex == StepBuyHatchet && BoundInventory->GetItemCount(HatchetItemID) > 0)
	{
		CompleteStep(StepBuyHatchet);
	}
	if (!PendingPetID.IsNone() && BoundInventory->GetItemCount(PendingPetID) > 0)
	{
		PendingPetID = NAME_None;
		CompleteStep(StepCapture);
	}
}

void USnareFirstRunGuideWidget::HandleRanchAnswered(const FString& FunctionName, bool bAccepted,
	const FString& FailureCode, const FText& Message)
{
	if (!bAccepted)
	{
		return;
	}
	if (FunctionName == TEXT("RegisterPet"))
	{
		CompleteStep(StepRanch);
	}
	else if (FunctionName == TEXT("HarvestRanch"))
	{
		CompleteStep(StepHarvest);
	}
}

void USnareFirstRunGuideWidget::CompleteStep(int32 Step)
{
	if (IsFinished() || StepIndex > Step)
	{
		return;
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 첫 실행 안내 %d장 행동 완료 - 다음 장으로"), Step + 1);
	StepIndex = Step;
	Advance();
}

void USnareFirstRunGuideWidget::Finish()
{
	if (bPersistSeen)
	{
		SetGuideSeen(true);
	}
	if (Card != nullptr)
	{
		Card->SetVisibility(ESlateVisibility::Collapsed);
	}
	RemoveFromParent();
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 첫 실행 안내를 끝냈다 (저장=%s)"), bPersistSeen ? TEXT("예") : TEXT("아니오"));
	OnFinished.Broadcast();
}

void USnareFirstRunGuideWidget::HandleNextClicked()
{
	Advance();
}

void USnareFirstRunGuideWidget::HandleSkipClicked()
{
	Skip();
}

void USnareFirstRunGuideWidget::Refresh()
{
	const TArray<FText>& Steps = GetSteps();
	if (StepText != nullptr && Steps.IsValidIndex(StepIndex))
	{
		StepText->SetText(FText::Format(LOCTEXT("GuideStepFormat", "{0}/{1}  {2}"),
			FText::AsNumber(StepIndex + 1), FText::AsNumber(Steps.Num()), Steps[StepIndex]));
		if (SnareSound::IsActionLogOn())
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[실측] 첫 실행 안내 %d/%d 「%s」"),
				StepIndex + 1, Steps.Num(), *Steps[StepIndex].ToString());
		}
	}
	if (NextLabel != nullptr)
	{
		NextLabel->SetText(StepIndex + 1 >= Steps.Num() ? LOCTEXT("GuideStart", "시작!") : LOCTEXT("GuideNext", "다음"));
	}
}

TSharedRef<SWidget> USnareFirstRunGuideWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GuideRoot"));
		WidgetTree->RootWidget = Root;

		Card = SnareUiStyle::MakeWoodSignCard(*WidgetTree, TEXT("GuideCard"));
		Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Card->SetPadding(FMargin(24.f, 16.f));
		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Card)))
		{
			CardSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
			CardSlot->SetAlignment(FVector2D(0.5f, 0.f));
			CardSlot->SetPosition(FVector2D(0.f, CardTop));
			CardSlot->SetSize(FVector2D(CardWidth, 0.f));
			CardSlot->SetAutoSize(true);
		}

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GuideColumn"));
		Card->SetContent(Column);

		StepText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GuideStepText"));
		StepText->SetAutoWrapText(true);
		StepText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiPanelCream()));   // 나무 표찰 위 글은 크림 (DESIGN 7.1)
		Column->AddChildToVerticalBox(StepText);

		UHorizontalBox* Buttons =
			WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GuideButtons"));
		if (UVerticalBoxSlot* ButtonsSlot = Column->AddChildToVerticalBox(Buttons))
		{
			ButtonsSlot->SetHorizontalAlignment(HAlign_Right);
			ButtonsSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		}

		auto AddButton = [&](FName Name, const FText& Label, UTextBlock** OutLabel, bool bFirst) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			SnareUiStyle::MakeRoundButton(*Button, 14.f);
			UTextBlock* Text =
				WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name.ToString() + TEXT("Text")));
			Text->SetText(Label);
			Text->SetJustification(ETextJustify::Center);
			Button->AddChild(Text);
			if (UHorizontalBoxSlot* BoxSlot = Buttons->AddChildToHorizontalBox(Button))
			{
				BoxSlot->SetPadding(FMargin(bFirst ? 0.f : ButtonGap, 0.f, 0.f, 0.f));
			}
			if (OutLabel != nullptr)
			{
				*OutLabel = Text;
			}
			return Button;
		};
		SkipButton = AddButton(TEXT("GuideSkipButton"), LOCTEXT("GuideSkip", "건너뛰기"), nullptr, true);
		UTextBlock* NextLabelRaw = nullptr;
		NextButton = AddButton(TEXT("GuideNextButton"), LOCTEXT("GuideNext", "다음"), &NextLabelRaw, false);
		NextLabel = NextLabelRaw;
		SkipButton->OnClicked.AddDynamic(this, &USnareFirstRunGuideWidget::HandleSkipClicked);
		NextButton->OnClicked.AddDynamic(this, &USnareFirstRunGuideWidget::HandleNextClicked);
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (StepText != nullptr)
	{
		SnareUiStyle::SetFontSize(*StepText, TextFontSize);
	}
	if (NextLabel != nullptr)
	{
		SnareUiStyle::SetFontSize(*NextLabel, ButtonFontSize);
	}
	// 나무 표찰 위 단추는 표찰보다 한 단 진한 갈색 — 같은 나무색이면 단추가 묻힌다.
	for (UButton* const Button : {SkipButton.Get(), NextButton.Get()})
	{
		if (Button != nullptr)
		{
			FButtonStyle Style = Button->GetStyle();
			Style.Normal.TintColor = FSlateColor(SnareUiStyle::UiTitleTab());
			Style.Hovered.TintColor = FSlateColor(SnareUiStyle::UiTitleTab());
			Button->SetStyle(Style);
		}
	}
	Refresh();
	return Result;
}

#undef LOCTEXT_NAMESPACE
