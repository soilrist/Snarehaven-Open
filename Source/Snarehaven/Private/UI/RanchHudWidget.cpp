#include "UI/RanchHudWidget.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "UI/SnareNoticeCards.h"
#include "UI/SnareNoticeText.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/ItemRowLookup.h"
#include "Data/RanchProductRow.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/CoopReportComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "Ranch/RanchSyncComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/SnareBalanceHud.h"
#include "Data/UsageHintRow.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareUsageHints.h"
#include "UI/SnareZoneTravelHud.h"
#include "World/SnareSound.h"
#include "World/ZoneTravelComponent.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 UI 배치 (Autonomy.md 🟡 — 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게" 다).
	// 진짜 디자인은 WBP_HUD_Ranch 에 얹히고, 그러면 아래 트리는 아예 만들어지지 않는다.
	// M15-03: 레퍼런스 시트 `docs/reference/hud_ingame.md` — 가장자리 40, 카드 제목 18, 모서리 12~16.
	constexpr float PlaceholderPanelWidth = 360.f;
	constexpr float PlaceholderScreenMargin = 40.f;
	constexpr float PlaceholderNoticeHeight = 36.f;
	// 카드 안 버튼 사이 틈.
	constexpr float PlaceholderButtonGap = 12.f;
	constexpr float PlaceholderCardTitleFontSize = 18.f;
	// 10-16 HUD 검수: 수확 준비 칩은 폰에서 17px 이상(30단위 ≈ 17px @616)
	constexpr float PlaceholderReadyNoticeFontSize = 30.f;
	constexpr float PlaceholderCardFontSize = 16.f;
	constexpr float PlaceholderCardSmallFontSize = 14.f;
	constexpr float PlaceholderCardButtonRadius = 12.f;
	// 카드 버튼 최소 크기 — 누르는 칸 최소 72(DESIGN 2 「누르는 칸 최소 크기」). 글자 크기만 하면 1334×616 캡처에서 18px 높이였다(루프#1221).
	constexpr float PlaceholderCardButtonMinHeight = SnareUiStyle::HudTouchMin;
	constexpr float PlaceholderCardButtonMinWidth = 144.f;
	// 포탈 버튼 = 사냥 HUD 탈것 버튼과 같은 작은 둥근 상황 버튼 — 반지름 = 높이 바닥의 반 (M38-02).
	constexpr float PlaceholderContextButtonRadius = SnareZoneTravelHud::MinButtonHeight * 0.5f;
	// 실패 문구가 떠 있는 시간(초) — 🟡 읽을 만큼만 떴다 접힌다 (DESIGN 3.2 🟡 실패 문구 줄).
	constexpr float FailureNoticeSeconds = 3.f;
	// 수확 팝업이 떠 있는 시간(초) — DESIGN 3.2 「수확 팝업」 (ROADMAP M21-03).
	constexpr float HarvestPopupSeconds = 1.2f;
	// M23-03 알림 카드 — 시트 `hud_ingame.md` 「크림 카드 300×70, 왼쪽 아이콘 48px, 제목 18 + 설명 14」.
	constexpr float NoticeCardWidth = 300.f;
	constexpr float NoticeCardHeight = 70.f;
	constexpr float NoticeCardIconSize = 48.f;
	constexpr float NoticeCardPaddingX = 16.f;
	constexpr float NoticeCardGap = 12.f;
	// 알림 카드끼리 세로 틈 (시트 「세로로 쌓인다」, 카드 안 버튼 틈과 같은 값).
	constexpr float NoticeStackGap = 12.f;

	// 아이콘 아트가 들어오기 전 Ready 를 나타내는 글자표 (`AGatherableActor` 의 자리표시 큐브와 같은 취지).
	// ASCII 로 쓴다 — UI 글꼴 Jua 에 `●` 가 없어 네모로 나온다(루프#87, `SnareUiStyle.h`).
	const TCHAR* PlaceholderReadyIcon = TEXT("*");
}

URanchHudWidget::URanchHudWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 미수확 상한은 코드가 아니라 표가 안다 (DataDriven.md 1 · engines/unreal.md 코드 규칙 4).
	static ConstructorHelpers::FObjectFinder<UDataTable> RanchProductTableAsset(
		TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable"));
	if (RanchProductTableAsset.Succeeded())
	{
		RanchProductTable = RanchProductTableAsset.Object;
	}
}

void URanchHudWidget::BindToCharacter(ASnarehavenCharacter* Character)
{
	// 듣던 쪽을 먼저 끊는다 — 옛 목장의 알림이 계속 들어오면 HUD 가 남의 칸을 보여준다.
	if (IsValid(WatchedRanch))
	{
		WatchedRanch->OnRanchCacheChanged.RemoveDynamic(this, &URanchHudWidget::HandleRanchCacheChanged);
	}
	if (IsValid(WatchedInventory))
	{
		WatchedInventory->OnInventoryCacheChanged.RemoveDynamic(
			this, &URanchHudWidget::HandleInventoryCacheChanged);
	}
	if (IsValid(WatchedInteract))
	{
		WatchedInteract->OnSelectedPlacementChanged.RemoveDynamic(this, &URanchHudWidget::HandleSelectedPetChanged);
		WatchedInteract->OnRanchSlotTargetChanged.RemoveDynamic(this, &URanchHudWidget::HandleSlotTargetChanged);
		WatchedInteract->OnNothingToPlace.RemoveDynamic(this, &URanchHudWidget::HandleNothingToPlace);
	}
	if (IsValid(WatchedZoneTravel))
	{
		WatchedZoneTravel->OnZonePortalTargetChanged.RemoveDynamic(
			this, &URanchHudWidget::HandleZonePortalTargetChanged);
	}
	WatchedRanch = nullptr;
	WatchedInventory = nullptr;
	WatchedInteract = nullptr;
	WatchedZoneTravel = nullptr;
	WatchedCharacter = IsValid(Character) ? Character : nullptr;
	UsageHintLastApproach = NAME_None;

	if (IsValid(Character))
	{
		// M10-02: 낚시터가 같은 클래스로 하나 더 붙어 있어 `FindComponentByClass` 는 쓸 수 없다.
		WatchedRanch = URanchComponent::FindStation(Character, ERanchStationKind::Ranch);
		if (IsValid(WatchedRanch))
		{
			WatchedRanch->OnRanchCacheChanged.AddDynamic(this, &URanchHudWidget::HandleRanchCacheChanged);
		}

		// 가진 펫은 인벤토리 **캐시**가 정한다 — 백엔드 스냅샷이 오는 순간 고를 수 있는 종이 달라지므로
		// 목장 알림만 듣고 있으면 고르기 줄이 옛 목록을 보여 준다 (DESIGN 4.5).
		WatchedInventory = Character->FindComponentByClass<UInventoryComponent>();
		if (IsValid(WatchedInventory))
		{
			WatchedInventory->OnInventoryCacheChanged.AddDynamic(
				this, &URanchHudWidget::HandleInventoryCacheChanged);
		}

		WatchedInteract = Character->FindComponentByClass<URanchInteractComponent>();
		if (IsValid(WatchedInteract))
		{
			WatchedInteract->OnSelectedPlacementChanged.AddDynamic(this, &URanchHudWidget::HandleSelectedPetChanged);
			WatchedInteract->OnRanchSlotTargetChanged.AddDynamic(this, &URanchHudWidget::HandleSlotTargetChanged);
			WatchedInteract->OnNothingToPlace.AddDynamic(this, &URanchHudWidget::HandleNothingToPlace);
		}

		// 목장에서 사냥터로 **돌아가는 길**이다 (M6-03b). 사냥 HUD 와 같은 컴포넌트를 듣는다 —
		// 포탈을 고르는 판정이 화면마다 갈리면 한쪽에서만 버튼이 뜬다.
		WatchedZoneTravel = Character->GetZoneTravel();
		if (IsValid(WatchedZoneTravel))
		{
			WatchedZoneTravel->OnZonePortalTargetChanged.AddDynamic(
				this, &URanchHudWidget::HandleZonePortalTargetChanged);
		}
	}

	// 잔액의 출처도 같이 갈아 끼운다 (M7-03). 폰이 아니라 게임 인스턴스에 붙어 있는 값이다.
	UGameInstance* const GameInstance = IsValid(Character) ? Character->GetGameInstance() : nullptr;
	BindToBackend(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr);

	// 나무 넣기·칸 확장(M12-05)은 폰의 요청 컴포넌트가 서버에 묻는다 — HUD 는 누르기·보여 주기만.
	BindToRanchSync(IsValid(Character) ? Character->FindComponentByClass<URanchSyncComponent>() : nullptr);

	// 같이 하기 줄(M21-06b2) — 보고·판정은 폰의 보고 컴포넌트와 서버 몫, HUD 는 답만 보여 준다.
	BindToCoopReport(IsValid(Character) ? Character->FindComponentByClass<UCoopReportComponent>() : nullptr);

	RefreshDisplay();
}

void URanchHudWidget::BindToCoopReport(UCoopReportComponent* Coop)
{
	if (IsValid(WatchedCoop))
	{
		WatchedCoop->OnCoopStateChanged.RemoveDynamic(this, &URanchHudWidget::HandleCoopStateChanged);
	}
	WatchedCoop = Coop;
	if (IsValid(WatchedCoop))
	{
		WatchedCoop->OnCoopStateChanged.AddDynamic(this, &URanchHudWidget::HandleCoopStateChanged);
	}
	RefreshDisplay();
}

void URanchHudWidget::HandleCoopStateChanged()
{
	RefreshDisplay();
}

FText URanchHudWidget::GetCoopText() const
{
	if (!IsValid(WatchedCoop) || !WatchedCoop->IsCoopActive())
	{
		return FText::GetEmpty();
	}
	return LOCTEXT("RanchHudCoop", "같이 하기 - 생산 10% 빨라짐");
}

void URanchHudWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}

	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(
			this, &URanchHudWidget::HandleBackendSnapshotChanged);
		WatchedBackend->OnAchievementNotice.RemoveAll(this);
	}

	WatchedBackend = Backend;

	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(
			this, &URanchHudWidget::HandleBackendSnapshotChanged);
		// 업적 달성·도감 새 칸 카드 (M48-03, DESIGN 8.15) — 서버 답의 차이로만 온다.
		WatchedBackend->OnAchievementNotice.AddUObject(this, &URanchHudWidget::HandleAchievementNotice);
	}

	RefreshDisplay();
}

void URanchHudWidget::BindToRanchSync(URanchSyncComponent* Sync)
{
	if (WatchedSync == Sync)
	{
		return;
	}
	if (IsValid(WatchedSync))
	{
		WatchedSync->OnRanchRequestAnswered.RemoveDynamic(this, &URanchHudWidget::HandleRanchRequestAnswered);
	}
	WatchedSync = Sync;
	if (IsValid(WatchedSync))
	{
		WatchedSync->OnRanchRequestAnswered.AddDynamic(this, &URanchHudWidget::HandleRanchRequestAnswered);
	}
	RefreshDisplay();
}

FText URanchHudWidget::GetFeedUpkeepButtonText() const
{
	const int32 Count = IsValid(WatchedSync) ? WatchedSync->GetUpkeepItemCount() : -1;
	if (Count < 0)
	{
		return LOCTEXT("RanchHudFeedUpkeepUnknown", "나무 넣기");
	}
	return FText::Format(LOCTEXT("RanchHudFeedUpkeep", "나무 넣기 ({0}개)"), FText::AsNumber(Count));
}

bool URanchHudWidget::IsFeedUpkeepEnabled() const
{
	return IsValid(WatchedSync) && !WatchedSync->IsUpkeepActionInFlight() && WatchedSync->GetUpkeepItemCount() > 0;
}

bool URanchHudWidget::TryFeedUpkeep()
{
	if (!IsFeedUpkeepEnabled())
	{
		return false;
	}
	// 가진 나무 전부를 싣는다 — 여유를 여기서 계산하지 않는다(남은 시간은 서버 값, CLAUDE.md 8).
	const bool bSent = WatchedSync->RequestFeedUpkeep(WatchedSync->GetUpkeepItemCount());
	RefreshDisplay();
	return bSent;
}

FText URanchHudWidget::GetExpandSlotButtonText() const
{
	if (IsValid(WatchedSync) && WatchedSync->IsSlotCapAtMax())
	{
		return LOCTEXT("RanchHudExpandSlotMax", "칸 최대");
	}
	int32 Fame = 0;
	int32 Coins = 0;
	if (!IsValid(WatchedSync) || !WatchedSync->GetNextExpandCost(Fame, Coins))
	{
		return LOCTEXT("RanchHudExpandSlotUnknown", "칸 확장");
	}
	return FText::Format(LOCTEXT("RanchHudExpandSlot", "칸 확장 - 명성 {0}, 코인 {1}"),
		FText::AsNumber(Fame), FText::AsNumber(Coins));
}

bool URanchHudWidget::IsExpandSlotEnabled() const
{
	int32 Fame = 0;
	int32 Coins = 0;
	return IsValid(WatchedSync) && !WatchedSync->IsUpkeepActionInFlight() && WatchedSync->GetNextExpandCost(Fame, Coins);
}

bool URanchHudWidget::TryExpandSlot()
{
	if (!IsExpandSlotEnabled())
	{
		return false;
	}
	const bool bSent = WatchedSync->RequestExpandSlot();
	RefreshDisplay();
	return bSent;
}

float URanchHudWidget::GetHarvestPopupSeconds()
{
	return HarvestPopupSeconds;
}

void URanchHudWidget::HandleRanchRequestAnswered(const FString& FunctionName, bool bAccepted,
	const FString& FailureCode, const FText& Message)
{
	const bool bBaseState = FunctionName == URanchSyncComponent::GetBaseStateFunctionName();
	if (!bAccepted && !Message.IsEmpty())
	{
		FailureNotice = Message;
		bBaseStateFailed |= bBaseState;
		if (SnareSound::IsActionLogOn())
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[실측] 화면 알림 「%s」 (목장 HUD · %s 거절)"), *FailureNotice.ToString(), *FunctionName);
		}
		if (IsValid(FailureNoticeText))
		{
			// 거점 상태를 못 받았으면 이유를 접지 않는다 (0초 = 타이머 없음) — 「다시 시도」 와 함께 남는다.
			FailureNoticeText->ShowFor(FailureNotice, bBaseState ? 0.f : FailureNoticeSeconds);
		}
	}
	else if (bAccepted)
	{
		bBaseStateFailed = false;
		FailureNotice = FText::GetEmpty();
		if (IsValid(FailureNoticeText))
		{
			FailureNoticeText->HideNotice();
		}
	}
	// 수확 팝업은 서버가 확정한 답의 글만 띄운다 (CLAUDE.md 8 — 누르는 순간이 아니라 응답 뒤).
	const FText Popup = bAccepted && IsValid(WatchedSync) ? WatchedSync->GetLastHarvestPopup() : FText::GetEmpty();
	if (!Popup.IsEmpty())
	{
		HarvestPopup = Popup;
		ShowHarvestCard(WatchedSync->GetLastHarvestTitle(), WatchedSync->GetLastHarvestCoinLine(),
			WatchedSync->GetLastHarvestIcon(), HarvestPopupSeconds);
	}
	// 성공이면 숫자는 아직 옛 값이다 — 다시 받은 스냅샷이 `OnRanchCacheChanged` 로 바꾼다. 버튼만 다시 켠다.
	RefreshDisplay();
}

void URanchHudWidget::ShowHarvestCard(const FText& Title, const FText& CoinLine,
	const TSoftObjectPtr<UTexture2D>& Icon, float Seconds)
{
	if (IsValid(HarvestIcon))
	{
		// 아이콘이 없는 아이템이면 자리만 남긴다 — 글자가 옆으로 뛰지 않게.
		UTexture2D* const IconTexture = Icon.IsNull() ? nullptr : Icon.LoadSynchronous();
		FSlateBrush IconBrush;
		IconBrush.SetResourceObject(IconTexture);
		IconBrush.ImageSize = FVector2D(NoticeCardIconSize, NoticeCardIconSize);
		HarvestIcon->SetBrush(IconBrush);
		HarvestIcon->SetColorAndOpacity(IconTexture != nullptr ? FLinearColor::White : FLinearColor::Transparent);
	}
	if (IsValid(HarvestCoinText))
	{
		HarvestCoinText->SetText(CoinLine);
		HarvestCoinText->SetVisibility(CoinLine.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(HarvestPopupText))
	{
		// 제목 글자가 카드(짝)를 같이 펴고 접는다 — 이 파일엔 시간 세는 코드를 두지 않는다(`NoClientSideTimer`).
		HarvestPopupText->ShowFor(Title, Seconds);
	}
}

int32 URanchHudWidget::GetTargetSlotIndex() const
{
	const ARanchSlotActor* const Target = IsValid(WatchedInteract) ? WatchedInteract->GetSlotTarget() : nullptr;
	return IsValid(Target) ? Target->GetSlotIndex() : INDEX_NONE;
}

FText URanchHudWidget::GetRemovePetButtonText() const
{
	const int32 SlotIndex = GetTargetSlotIndex();
	if (SlotIndex == INDEX_NONE)
	{
		return LOCTEXT("RanchHudRemovePetNoTarget", "펫 빼기");
	}
	return FText::Format(LOCTEXT("RanchHudRemovePet", "펫 빼기 ({0}번 칸)"), FText::AsNumber(SlotIndex + 1));
}

bool URanchHudWidget::IsRemovePetEnabled() const
{
	const int32 SlotIndex = GetTargetSlotIndex();
	const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	return RanchSlot != nullptr && !RanchSlot->IsEmpty() && RanchSlot->Kind == ERanchSlotKind::AnimalProduct
		&& !WatchedRanch->HasPendingRequest(SlotIndex);
}

void URanchHudWidget::PlayHandActionIf(bool bSent) const
{
	// 요청이 나가면 펫에게 손을 뻗는 몸짓 (INBOX 2026-10-15 감사 (e)) — 겉모습뿐, 결과는 서버 답 뒤.
	if (ASnarehavenCharacter* const Body = bSent ? Cast<ASnarehavenCharacter>(GetOwningPlayerPawn()) : nullptr)
	{
		Body->PlayItemAction(EItemAction::Gather);
	}
}

bool URanchHudWidget::TryRemovePet()
{
	// 요청만 쌓는다 — 칸이 비고 펫이 가방에 돌아오는 것은 서버 답 뒤 다시 받은 스냅샷이다 (CLAUDE.md 8).
	const bool bQueued = IsRemovePetEnabled() && WatchedRanch->QueueRemovePet(GetTargetSlotIndex());
	PlayHandActionIf(bQueued);
	RefreshDisplay();
	return bQueued;
}

void URanchHudWidget::HandleRemovePetClicked()
{
	// #0199 한 번에 빼지 않는다 — 확인 줄만 편다. 요청은 「빼기」에서.
	bRemovePetConfirmOpen = IsRemovePetEnabled();
	RefreshDisplay();
}

void URanchHudWidget::HandleRemovePetConfirmClicked()
{
	if (bRemovePetConfirmOpen)
	{
		bRemovePetConfirmOpen = false;
		TryRemovePet();
	}
}

void URanchHudWidget::HandleRemovePetCancelClicked()
{
	bRemovePetConfirmOpen = false;
	RefreshDisplay();
}

TArray<FName> URanchHudWidget::GetPetCosmeticOptions() const
{
	TArray<FName> Options;
	const int32 SlotIndex = GetTargetSlotIndex();
	const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	if (RanchSlot == nullptr || RanchSlot->IsEmpty() || RanchSlot->Kind != ERanchSlotKind::AnimalProduct
		|| !IsValid(WatchedInteract))
	{
		return Options;
	}
	Options = WatchedInteract->GetOwnedPetCosmeticIDs();
	if (!RanchSlot->CosmeticItemID.IsNone())
	{
		Options.Add(NAME_None);
	}
	return Options;
}

FName URanchHudWidget::GetSelectedPetCosmetic() const
{
	const TArray<FName> Options = GetPetCosmeticOptions();
	return Options.Num() > 0 ? Options[PetCosmeticPickIndex % Options.Num()] : NAME_None;
}

FText URanchHudWidget::GetPetCosmeticPickerText() const
{
	const TArray<FName> Options = GetPetCosmeticOptions();
	if (Options.Num() == 0)
	{
		return LOCTEXT("RanchHudCosmeticNone", "꾸미기: 없음");
	}
	const int32 Index = PetCosmeticPickIndex % Options.Num();
	const FText Name = Options[Index].IsNone() ? LOCTEXT("RanchHudCosmeticTakeOff", "벗기기")
		: WatchedInteract->GetPlacementDisplayName(Options[Index]);
	return FText::Format(LOCTEXT("RanchHudCosmeticPicker", "꾸미기: {0} ({1}/{2})"),
		Name, FText::AsNumber(Index + 1), FText::AsNumber(Options.Num()));
}

void URanchHudWidget::CyclePetCosmetic()
{
	const int32 Count = GetPetCosmeticOptions().Num();
	PetCosmeticPickIndex = Count > 0 ? (PetCosmeticPickIndex + 1) % Count : 0;
	RefreshDisplay();
}

bool URanchHudWidget::IsApplyPetCosmeticEnabled() const
{
	const int32 SlotIndex = GetTargetSlotIndex();
	const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	return RanchSlot != nullptr && GetPetCosmeticOptions().Num() > 0
		&& GetSelectedPetCosmetic() != RanchSlot->CosmeticItemID && !WatchedRanch->HasPendingRequest(SlotIndex);
}

bool URanchHudWidget::TryApplyPetCosmetic()
{
	// 요청만 쌓는다 — 몸의 꾸미기·배낭 수는 서버 답 뒤 다시 받은 스냅샷이 바꾼다 (CLAUDE.md 8).
	const bool bQueued = IsApplyPetCosmeticEnabled()
		&& WatchedRanch->QueueSetCosmetic(GetTargetSlotIndex(), GetSelectedPetCosmetic());
	PlayHandActionIf(bQueued);
	if (bQueued)
	{
		PetCosmeticPickIndex = 0;
	}
	RefreshDisplay();
	return bQueued;
}

void URanchHudWidget::HandlePetCosmeticPickerClicked()
{
	CyclePetCosmetic();
}

void URanchHudWidget::HandlePetCosmeticApplyClicked()
{
	TryApplyPetCosmetic();
}

void URanchHudWidget::HandleSlotTargetChanged(ARanchSlotActor* NewTarget)
{
	// 다른 칸을 가리키면 앞 칸에 대한 확인은 무효다.
	bRemovePetConfirmOpen = false;
	PetCosmeticPickIndex = 0;
	RefreshDisplay();
}

void URanchHudWidget::HandleFeedUpkeepClicked()
{
	TryFeedUpkeep();
}

void URanchHudWidget::HandleExpandSlotClicked()
{
	TryExpandSlot();
}

FText URanchHudWidget::GetUpkeepText() const
{
	if (!IsValid(WatchedRanch) || !WatchedRanch->GetUpkeep().bReceived)
	{
		return FText::GetEmpty();
	}

	// 판정(잠김·남은 시간·명성·칸 상한)은 전부 서버 값이다 — 여기서는 글자로 옮기기만 한다 (CLAUDE.md 8).
	const FRanchUpkeepState& Upkeep = WatchedRanch->GetUpkeep();
	const FText Tail = FText::Format(LOCTEXT("RanchHudUpkeepTail", "명성 {0}, 칸 {1}/{2}"),
		FText::AsNumber(Upkeep.Fame), FText::AsNumber(WatchedRanch->GetSlotCount()), FText::AsNumber(Upkeep.SlotCap));
	if (Upkeep.bLocked)
	{
		return FText::Format(LOCTEXT("RanchHudUpkeepLocked", "목장 잠김 - 나무를 넣어야 풀린다, {0}"), Tail);
	}
	if (Upkeep.LeftHours < 0)
	{
		return FText::Format(LOCTEXT("RanchHudUpkeepNotStarted", "목장 유지: 펫을 넣으면 시작, {0}"), Tail);
	}
	return FText::Format(LOCTEXT("RanchHudUpkeepLeft", "목장 유지 {0}시간 남음, {1}"),
		FText::AsNumber(Upkeep.LeftHours), Tail);
}

FText URanchHudWidget::GetBalanceText() const
{
	// 문구도 미정 표시도 두 화면이 한 곳에서 가져온다 (`SnareBalanceHud`).
	return SnareBalanceHud::FormatBalanceText(WatchedBackend);
}

bool URanchHudWidget::IsSlotReady(int32 SlotIndex) const
{
	if (!IsValid(WatchedRanch))
	{
		return false;
	}

	// Ready 판단은 백엔드가 보내 준 개수 하나뿐이다 — 시각 계산은 여기 없다 (CLAUDE.md 8).
	const FRanchSlotState* RanchSlot = WatchedRanch->GetSlot(SlotIndex);
	return RanchSlot != nullptr && !RanchSlot->IsEmpty() && RanchSlot->Stock > 0;
}

int32 URanchHudWidget::GetReadySlotCount() const
{
	if (!IsValid(WatchedRanch))
	{
		return 0;
	}

	int32 ReadyCount = 0;
	for (int32 SlotIndex = 0; SlotIndex < WatchedRanch->GetSlotCount(); ++SlotIndex)
	{
		if (IsSlotReady(SlotIndex))
		{
			++ReadyCount;
		}
	}

	return ReadyCount;
}

int32 URanchHudWidget::GetSlotMaxStack(int32 SlotIndex) const
{
	const FRanchSlotState* RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	if (RanchSlot == nullptr || RanchSlot->IsEmpty() || !IsValid(RanchProductTable))
	{
		return -1;
	}

	// 상한은 표에만 있다 (DESIGN 3.2 = 5). 행 이름이 아니라 `AnimalID` 열로 찾는다 —
	// `URanchInteractComponent` 가 `DT_AnimalTable` 을 훑는 것과 같은 길이고, 종이 한 화면에
	// 들어갈 만큼 적다 (DataDriven.md 4).
	for (const TPair<FName, uint8*>& Pair : RanchProductTable->GetRowMap())
	{
		const FRanchProductRow* Row = reinterpret_cast<const FRanchProductRow*>(Pair.Value);
		if (Row != nullptr && Row->AnimalID == RanchSlot->AnimalID)
		{
			return Row->MaxStack;
		}
	}

	return -1;
}

FText URanchHudWidget::GetSlotText(int32 SlotIndex) const
{
	const FRanchSlotState* RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	if (RanchSlot == nullptr)
	{
		return FText::GetEmpty();
	}

	// 칸 번호는 사람이 세는 대로 1부터 보여 준다 (코드 안의 0~4 와 다른 건 화면 표기뿐이다).
	const FText SlotLabel = FText::AsNumber(SlotIndex + 1);

	if (RanchSlot->IsEmpty())
	{
		return FText::Format(LOCTEXT("RanchHudEmptySlot", "{0}번 칸 - 비어 있음"), SlotLabel);
	}

	const FText ReadyIcon = IsSlotReady(SlotIndex)
		? FText::FromString(PlaceholderReadyIcon)
		: FText::GetEmpty();

	// ⑩ 작물은 쌓이지 않는다(원본대로 한 번 거두면 칸이 빈다) — 개수/상한 대신 자람 상태만 보인다.
	// "다 자람" 도 서버가 `GetBaseState` 에 실어 준 `stock`(1/0) 하나로만 정한다 — 시각 계산은 여기 없다.
	if (RanchSlot->Kind == ERanchSlotKind::Crop)
	{
		return IsSlotReady(SlotIndex)
			? FText::Format(LOCTEXT("RanchHudCropRipe", "{0}번 칸 {1} 다 자람"), SlotLabel, ReadyIcon)
			: FText::Format(LOCTEXT("RanchHudCropGrowing", "{0}번 칸 - 자라는 중"), SlotLabel);
	}

	const int32 MaxStack = GetSlotMaxStack(SlotIndex);
	// 상한이 미정(-1)이면 숫자를 지어내지 않고 `-` 로 둔다 (Verification.md 7).
	const FText MaxStackText = MaxStack >= 0
		? FText::AsNumber(MaxStack)
		: LOCTEXT("RanchHudUnsetMaxStack", "-");

	return FText::Format(LOCTEXT("RanchHudFilledSlot", "{0}번 칸 {1} {2}/{3}"),
		SlotLabel, ReadyIcon, FText::AsNumber(RanchSlot->Stock), MaxStackText);
}

FText URanchHudWidget::GetSlotCardTitle(int32 SlotIndex) const
{
	const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	if (RanchSlot == nullptr)
	{
		return FText::GetEmpty();
	}

	// 이름은 표가 안다 — 동물이면 동물 표, 작물이면 아이템 표 (`GetPlacementDisplayName`).
	const FName OccupantID = RanchSlot->AnimalID.IsNone() ? RanchSlot->CropItemID : RanchSlot->AnimalID;
	FText Name = (!RanchSlot->IsEmpty() && IsValid(WatchedInteract))
		? WatchedInteract->GetPlacementDisplayName(OccupantID) : FText::GetEmpty();
	// M21-07c2 변종 칸 딱지 — 이름 앞에 `변종`(사냥 맵 이름 카드·도감과 같은 꼴).
	if (RanchSlot->bVariant && !Name.IsEmpty())
	{
		Name = FText::Format(LOCTEXT("RanchHudCardVariantTitle", "변종 {0}"), Name);
	}
	return Name.IsEmpty()
		? FText::Format(LOCTEXT("RanchHudCardSlotTitle", "{0}번 칸"), FText::AsNumber(SlotIndex + 1))
		: Name;
}

FText URanchHudWidget::GetPetPickerText() const
{
	if (!IsValid(WatchedInteract))
	{
		return FText::GetEmpty();
	}

	// ⑩ 펫과 가진 씨앗을 한 줄에서 고른다 — 빈 칸에 펫을 넣으면 동물 칸, 씨앗을 심으면 작물 칸이다.
	// 둘 다 가졌으면 고른 탭 안에서만 센다 (M28-08) — 버튼도 그 안에서만 돈다.
	const TArray<FName> Options = WatchedInteract->GetPlacementTabOptions(WatchedInteract->IsSeedTabSelected());
	if (Options.Num() == 0)
	{
		// 아직 포획한 펫도 씨앗도 없다. 이름을 지어내지 않고 없다고만 적는다.
		return LOCTEXT("RanchHudNoPlacement", "배치할 것: 없음");
	}

	const FName SelectedID = WatchedInteract->GetSelectedPlacementID();
	// 이름도 표에서만 온다 — 표를 여기서 또 읽지 않고 상호작용 컴포넌트에 물어본다 (DataDriven.md 1).
	const FText DisplayName = WatchedInteract->GetPlacementDisplayName(SelectedID);
	const int32 SelectedIndex = Options.IndexOfByKey(SelectedID);
	const FText Picker = FText::Format(LOCTEXT("RanchHudPlacementPicker", "배치할 것: {0} ({1}/{2})"),
		DisplayName, FText::AsNumber(SelectedIndex + 1), FText::AsNumber(Options.Num()));

	// 라이브에 `PlantCrop` 이 아직 없으면 씨앗을 골라도 심지 못한다 — 왜 안 되는지 줄에서 보인다.
	if (IsValid(WatchedSync) && WatchedSync->IsFunctionUnavailable(TEXT("PlantCrop"))
		&& WatchedInteract->GetOwnedSeedItemIDs().Contains(SelectedID))
	{
		return FText::Format(LOCTEXT("RanchHudPlacementNotReady", "{0} (서버 준비 중)"), Picker);
	}
	return Picker;
}

bool URanchHudWidget::IsPetPickerEnabled() const
{
	return IsValid(WatchedInteract) && WatchedInteract->GetPlacementOptions().Num() > 0;
}

bool URanchHudWidget::IsSlotActionEnabled(int32 SlotIndex) const
{
	const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	if (RanchSlot == nullptr || WatchedRanch->HasPendingRequest(SlotIndex))
	{
		return false;
	}
	return !RanchSlot->IsEmpty() || IsPetPickerEnabled();
}

bool URanchHudWidget::ArePlacementTabsShown() const
{
	return IsValid(WatchedInteract) && WatchedInteract->HasPlacementTabs();
}

void URanchHudWidget::SelectPlacementTab(bool bSeedTab)
{
	if (!IsValid(WatchedInteract))
	{
		return;
	}
	// 고르기만 바꾼다 — 칸이 차는 것은 넣기 뒤 서버 스냅샷이다 (CLAUDE.md 8).
	WatchedInteract->SelectPlacementTab(bSeedTab);
	RefreshDisplay();
}

void URanchHudWidget::RefreshPlacementTabs()
{
	if (IsValid(PlacementTabRow))
	{
		// 찬 칸이면 넣을 일이 없다. 칸 카드가 접혀 있으면 이 줄도 어차피 안 보인다.
		const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(GetTargetSlotIndex()) : nullptr;
		const bool bEmptyTarget = RanchSlot == nullptr || RanchSlot->IsEmpty();
		PlacementTabRow->SetVisibility(bEmptyTarget && ArePlacementTabsShown()
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	const bool bSeedTab = IsValid(WatchedInteract) && WatchedInteract->IsSeedTabSelected();
	auto MarkSelected = [](UButton* Button, bool bSelected)
	{
		if (!IsValid(Button))
		{
			return;
		}
		SnareUiStyle::MarkSelectedTab(Button, bSelected);
	};
	MarkSelected(PlacementAnimalTabButton, !bSeedTab);
	MarkSelected(PlacementSeedTabButton, bSeedTab);
}

bool URanchHudWidget::IsZoneTravelButtonVisible() const
{
	// 판정도 글자도 사냥 HUD 와 같아야 해서 한 곳이 정한다 (M6-03b — 잔액 줄과 같은 규칙).
	return SnareZoneTravelHud::IsButtonVisible(WatchedZoneTravel);
}

FText URanchHudWidget::GetZoneTravelButtonText() const
{
	return SnareZoneTravelHud::FormatButtonText(WatchedZoneTravel);
}

bool URanchHudWidget::TryZoneTravel()
{
	if (!IsValid(WatchedZoneTravel))
	{
		return false;
	}

	// **HUD 는 맵을 열지 않는다.** 호스트면 컴포넌트가 포탈에게 옮기라 하고, 게스트면 서버에 부탁만 한다
	// (사냥 HUD 와 같은 길 — 클라이언트가 스스로 여는 줄은 두 화면 어디에도 없다).
	// 선착장 요금(서버 `PayFerry`)을 먼저 내고, 성공 답이 온 뒤에만 그 길을 탄다 (DESIGN 8.14).
	return WatchedZoneTravel->TryFerryToNearest();
}

void URanchHudWidget::RefreshDisplay()
{
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(bBaseStateFailed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	const int32 SlotCount = IsValid(WatchedRanch) ? WatchedRanch->GetSlotCount() : 0;
	EnsureSlotRows(SlotCount);

	if (IsValid(SlotList))
	{
		for (int32 SlotIndex = 0; SlotIndex < SlotList->GetChildrenCount(); ++SlotIndex)
		{
			if (UTextBlock* RowText = Cast<UTextBlock>(SlotList->GetChildAt(SlotIndex)))
			{
				RowText->SetText(GetSlotText(SlotIndex));
			}
		}
	}

	if (IsValid(UpkeepText))
	{
		const FText Coop = GetCoopText();
		UpkeepText->SetText(Coop.IsEmpty() ? GetUpkeepText()
			: FText::Format(LOCTEXT("RanchHudUpkeepWithCoop", "{0}\n{1}"), GetUpkeepText(), Coop));
	}

	if (IsValid(FeedUpkeepButton))
	{
		// 요청 중엔 끈다 — 답이 오기 전에 두 번 누르면 같은 나무를 두 번 싣는다.
		FeedUpkeepButton->SetIsEnabled(IsFeedUpkeepEnabled());
	}
	if (IsValid(FeedUpkeepButtonText))
	{
		FeedUpkeepButtonText->SetText(GetFeedUpkeepButtonText());
	}
	if (IsValid(ExpandSlotButton))
	{
		ExpandSlotButton->SetIsEnabled(IsExpandSlotEnabled());
	}
	if (IsValid(ExpandSlotButtonText))
	{
		ExpandSlotButtonText->SetText(GetExpandSlotButtonText());
	}

	if (IsValid(RemovePetButton))
	{
		RemovePetButton->SetIsEnabled(IsRemovePetEnabled());
	}
	if (IsValid(RemovePetButtonText))
	{
		RemovePetButtonText->SetText(GetRemovePetButtonText());
	}
	bRemovePetConfirmOpen = bRemovePetConfirmOpen && IsRemovePetEnabled();
	if (IsValid(RemovePetConfirmRow))
	{
		RemovePetConfirmRow->SetVisibility(bRemovePetConfirmOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (IsValid(ReadyNoticeText))
	{
		const int32 ReadyCount = GetReadySlotCount();
		ReadyNoticeText->SetText(FText::Format(
			LOCTEXT("RanchHudReadyNotice", "수확 준비 {0}칸"),
			FText::AsNumber(ReadyCount)));
		// 수확할 게 없으면 알림을 접는다 — 알림 아이콘이 늘 떠 있으면 알림이 아니다 (DESIGN 3.2).
		ReadyNoticeText->SetVisibility(
			ReadyCount > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (IsValid(ReadyNoticeCard))
		{
			ReadyNoticeCard->SetVisibility(ReadyNoticeText->GetVisibility());
		}
	}

	RefreshSlotCard();
	RefreshPlacementTabs();

	if (IsValid(PetPickerText))
	{
		PetPickerText->SetText(GetPetPickerText());
	}

	if (IsValid(BalanceText))
	{
		// 잔액 줄은 접지 않는다 — 있다가 없어지면 "재화를 잃었다" 로 읽힌다. 모를 때는 `-` 가 뜬다.
		BalanceText->SetText(GetBalanceText());
	}

	if (IsValid(PetPickerButton))
	{
		// 알림줄과 달리 접지 않고 꺼 둔다 — 버튼이 나타났다 사라지면 손가락 위치를 잃는다
		// (사냥 HUD 포획 버튼과 같은 규칙).
		PetPickerButton->SetIsEnabled(IsPetPickerEnabled());
	}

	if (IsValid(ZoneTravelButton))
	{
		// 포탈 버튼만은 **접는다** — 포탈에서 멀어지면 눌러서 될 일이 없다 (사냥 HUD 와 같은 판단).
		ZoneTravelButton->SetVisibility(
			IsZoneTravelButtonVisible() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (IsValid(ZoneTravelButtonText))
		{
			ZoneTravelButtonText->SetText(GetZoneTravelButtonText());
		}
	}
}

void URanchHudWidget::RefreshSlotCard()
{
	if (!IsValid(SlotCard))
	{
		return;
	}

	// 칸 곁에 섰을 때만 뜬다 — 평소 화면에 칸 목록을 늘어놓지 않는다 (M15-03).
	const int32 SlotIndex = GetTargetSlotIndex();
	const FRanchSlotState* const RanchSlot = IsValid(WatchedRanch) ? WatchedRanch->GetSlot(SlotIndex) : nullptr;
	SlotCard->SetVisibility(RanchSlot != nullptr ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (RanchSlot == nullptr)
	{
		return;
	}

	const bool bEmpty = RanchSlot->IsEmpty();
	if (IsValid(SlotCardTitle))
	{
		// 빈 칸은 제목 한 줄에 「N번 칸 - 비어 있음」 — 제목과 상태 줄이 같은 말을 두 번 하지 않는다(M106-03).
		SlotCardTitle->SetText(bEmpty ? GetSlotText(SlotIndex) : GetSlotCardTitle(SlotIndex));
	}
	if (IsValid(SlotCardStatus))
	{
		SlotCardStatus->SetVisibility(bEmpty ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		// 10.9 씌운 꾸미기 이름 — 서버 칸 `cosmetic` 사본이 있을 때만 한 줄 더.
		const FText Status = GetSlotText(SlotIndex);
		SlotCardStatus->SetText(RanchSlot->CosmeticItemID.IsNone() || !IsValid(WatchedInteract) ? Status
			: FText::Format(LOCTEXT("RanchHudCardCosmetic", "{0}\n꾸미기: {1}"), Status,
				WatchedInteract->GetPlacementDisplayName(RanchSlot->CosmeticItemID)));
	}
	if (IsValid(SlotHarvestButton))
	{
		// 요청 중엔 끈다 — 답이 오기 전에 두 번 누르면 같은 칸을 두 번 부탁한다.
		// 빈 칸인데 넣을 동물·씨앗이 없으면 끈다 — 켜 두면 눌러도 아무 일 없는 단추가 된다(M106-03).
		SlotHarvestButton->SetIsEnabled(IsSlotActionEnabled(SlotIndex));
	}
	if (IsValid(SlotHarvestButtonText))
	{
		SlotHarvestButtonText->SetText(bEmpty
			? LOCTEXT("RanchHudCardPlace", "넣기")
			: LOCTEXT("RanchHudCardHarvest", "수확"));
	}
	// 빈 칸에선 무엇을 넣을지 고르고, 찬 칸에선 뺀다 — 쓸 수 없는 버튼은 카드에서 접는다.
	if (IsValid(PetPickerButton))
	{
		PetPickerButton->SetVisibility(bEmpty ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(RemovePetButton))
	{
		RemovePetButton->SetVisibility(bEmpty ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	// 꾸미기 줄은 펫이 선 동물 칸에서만 — 작물·낚시 칸엔 씌울 몸이 없다.
	const bool bPetSlot = !bEmpty && RanchSlot->Kind == ERanchSlotKind::AnimalProduct;
	if (IsValid(PetCosmeticRow))
	{
		PetCosmeticRow->SetVisibility(bPetSlot ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (IsValid(PetCosmeticPickerButton))
	{
		PetCosmeticPickerButton->SetIsEnabled(GetPetCosmeticOptions().Num() > 1);
	}
	if (IsValid(PetCosmeticPickerText))
	{
		PetCosmeticPickerText->SetText(GetPetCosmeticPickerText());
	}
	if (IsValid(PetCosmeticApplyButton))
	{
		PetCosmeticApplyButton->SetIsEnabled(IsApplyPetCosmeticEnabled());
	}
	if (IsValid(PetCosmeticApplyText))
	{
		PetCosmeticApplyText->SetText(GetSelectedPetCosmetic().IsNone() && !RanchSlot->CosmeticItemID.IsNone()
			? LOCTEXT("RanchHudCosmeticTakeOffButton", "벗기기")
			: LOCTEXT("RanchHudCosmeticPutOnButton", "씌우기"));
	}
}

void URanchHudWidget::HandleSlotHarvestClicked()
{
	// 상호작용 키와 같은 길 — 찬 칸은 수확 요청, 빈 칸은 넣기 요청만 쌓는다 (CLAUDE.md 8).
	if (IsValid(WatchedInteract))
	{
		WatchedInteract->TryInteractNearest();
	}
	RefreshDisplay();
}

void URanchHudWidget::HandleRanchCacheChanged()
{
	RefreshDisplay();
}

void URanchHudWidget::HandleRetryClicked()
{
	bBaseStateFailed = false;
	FailureNotice = FText::GetEmpty();
	if (IsValid(FailureNoticeText))
	{
		FailureNoticeText->HideNotice();
	}
	if (IsValid(WatchedSync))
	{
		WatchedSync->RequestBaseState();
	}
	RefreshDisplay();
}

void URanchHudWidget::HandleInventoryCacheChanged()
{
	RefreshDisplay();
}

void URanchHudWidget::HandleAchievementNotice(const TArray<SnareAchievementNotice::FCard>& Cards)
{
	if (!IsValid(AchievementCards))
	{
		ShowNotice(SnareAchievementNotice::JoinCards(Cards)); // WBP 디자인 — 목장 알림 칸은 3초 = `CardSeconds`
		return;
	}
	AchievementCards->ShowCards(Cards, SnareAchievementNotice::CardSeconds);
}

void URanchHudWidget::HandleBackendSnapshotChanged()
{
	RefreshDisplay();
}

void URanchHudWidget::HandleNothingToPlace()
{
	// 「넣기」 를 눌렀는데 아무 일도 없던 빈 칸 (전수 표 실측 #0512).
	ShowNotice(LOCTEXT("RanchHudNothingToPlace", "넣을 동물이나 씨앗이 없다"));
}

void URanchHudWidget::ShowNotice(const FText& Message)
{
	if (Message.IsEmpty())
	{
		return;
	}
	FailureNotice = Message;
	if (SnareSound::IsActionLogOn())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 화면 알림 「%s」 (목장 HUD)"), *FailureNotice.ToString());
	}
	if (IsValid(FailureNoticeText))
	{
		FailureNoticeText->ShowFor(FailureNotice, FailureNoticeSeconds);
	}
}

bool URanchHudWidget::IsFailureNoticeShown() const
{
	return IsValid(FailureNoticeText) && FailureNoticeText->GetVisibility() != ESlateVisibility::Collapsed
		&& !FailureNoticeText->GetText().IsEmpty();
}

void URanchHudWidget::HandleSelectedPetChanged(FName NewPetAnimalID)
{
	RefreshDisplay();
}

void URanchHudWidget::HandlePetPickerClicked()
{
	if (!IsValid(WatchedInteract))
	{
		return;
	}

	// 고르기만 바꾼다. 배치는 여전히 슬롯 앞에서 상호작용 버튼을 눌러야 하고, 칸이 차는 것은
	// 백엔드 스냅샷이 왔을 때뿐이다 (CLAUDE.md 8 · DESIGN 4.1 권위 표).
	WatchedInteract->SelectNextPlacement();
	RefreshDisplay();
}

void URanchHudWidget::HandlePlacementAnimalTabClicked()
{
	SelectPlacementTab(false);
}

void URanchHudWidget::HandlePlacementSeedTabClicked()
{
	SelectPlacementTab(true);
}

void URanchHudWidget::HandleZonePortalTargetChanged(AZonePortalActor* NewTarget)
{
	RefreshDisplay();
}

void URanchHudWidget::HandleZoneTravelClicked()
{
	TryZoneTravel();
}

void URanchHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 대상은 다가가기만 해도 바뀌어 알림이 없다 — 사냥 HUD 상황 버튼 글자와 같은 0.2초 간격으로 본다.
	constexpr double UsageHintCheckSeconds = 0.2;
	const double Now = FPlatformTime::Seconds();
	if (Now >= UsageHintNextCheckSeconds)
	{
		UsageHintNextCheckSeconds = Now + UsageHintCheckSeconds;
		CheckUsageHintForApproach();
	}
	TickUsageHint(InDeltaTime);
}

bool URanchHudWidget::IsUsageHintVisible() const
{
	return IsValid(UsageHint) && UsageHint->GetVisibility() != ESlateVisibility::Collapsed;
}

void URanchHudWidget::CheckUsageHintForApproach()
{
	if (!IsValid(WatchedCharacter))
	{
		return;
	}
	const FName Key = SnareUsageHints::FindApproachKey(WatchedCharacter);
	if (Key == UsageHintLastApproach)
	{
		return;
	}
	UsageHintLastApproach = Key;
	if (!Key.IsNone() && UsageHintTable == nullptr)
	{
		UsageHintTable = SnareUsageHints::LoadTable();
	}
	TryShowUsageHint(Key);
}

bool URanchHudWidget::TryShowUsageHint(FName Key)
{
	// 한 번에 하나 — 떠 있는 동안 온 키는 줄 세우지 않고 버린다(본 것 아님, 다음 다가가기에 뜬다). 사냥 HUD 와 같은 규칙.
	if (Key.IsNone() || !UsageHintKey.IsNone() || SnareUsageHints::IsBlocked(GetWorld()))
	{
		return false;
	}
	const FUsageHintRow* const Row =
		SnareUsageHints::ClaimRow(UsageHintTable, Key, SnareUsageHints::ResolveSection(UsageHintSection));
	if (Row == nullptr)
	{
		return false;
	}
	UsageHintKey = Key;
	UsageHintLine = Row->Line;
	UsageHintSecondsLeft = Row->ShowSeconds;
	UE_LOG(LogSnarehaven, Log, TEXT("[UsageHint] 처음 안내 %s — %s (%.1f초, 목장 HUD)"), *Key.ToString(),
		*UsageHintLine.ToString(), UsageHintSecondsLeft);
	UpdateUsageHintPill();
	return true;
}

void URanchHudWidget::TickUsageHint(float DeltaSeconds)
{
	if (UsageHintKey.IsNone())
	{
		return;
	}
	// 판·첫 실행 안내가 덮고 있는 동안은 접혀 기다리고 시간도 안 간다 — 남은 시간만큼 다시 뜬다.
	if (!SnareUsageHints::IsBlocked(GetWorld()))
	{
		UsageHintSecondsLeft -= DeltaSeconds;
	}
	if (UsageHintSecondsLeft <= 0.f)
	{
		UsageHintKey = NAME_None;
		UsageHintLine = FText::GetEmpty();
		UsageHintSecondsLeft = 0.f;
	}
	UpdateUsageHintPill();
}

void URanchHudWidget::UpdateUsageHintPill()
{
	if (!IsValid(UsageHint) || !IsValid(UsageHintText))
	{
		return;
	}
	if (UsageHintKey.IsNone() || SnareUsageHints::IsBlocked(GetWorld()))
	{
		UsageHint->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (!UsageHintText->GetText().EqualTo(UsageHintLine))
	{
		UsageHintText->SetText(UsageHintLine);
	}
	// 목장엔 상황 버튼이 없고 칸 버튼이 칸 카드 안에 있다 — 카드가 떠 있으면 그 바로 위, 아니면 카드 자리.
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(UsageHint->Slot))
	{
		float Lift = PlaceholderScreenMargin;
		if (IsValid(SlotCard) && SlotCard->GetVisibility() != ESlateVisibility::Collapsed)
		{
			SlotCard->ForceLayoutPrepass();
			Lift += SlotCard->GetDesiredSize().Y + PlaceholderButtonGap;
		}
		PillSlot->SetPosition(FVector2D(0.f, -Lift));
	}
	UsageHint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void URanchHudWidget::NativeDestruct()
{
	// 위젯이 내려간 뒤에도 알림이 오면 죽은 위젯을 부른다 (engines/unreal.md 코드 규칙 7).
	BindToCharacter(nullptr);

	Super::NativeDestruct();
}

TSharedRef<SWidget> URanchHudWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}

	if (IsValid(PetPickerButton)
		&& !PetPickerButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandlePetPickerClicked))
	{
		PetPickerButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandlePetPickerClicked);
	}

	if (IsValid(ZoneTravelButton)
		&& !ZoneTravelButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleZoneTravelClicked))
	{
		// 포탈도 한 번 누르면 끝이라 고르기 버튼과 같은 `OnClicked` 다.
		ZoneTravelButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleZoneTravelClicked);
	}

	if (IsValid(RemovePetButton)
		&& !RemovePetButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleRemovePetClicked))
	{
		RemovePetButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleRemovePetClicked);
	}
	if (IsValid(RemovePetConfirmButton)
		&& !RemovePetConfirmButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleRemovePetConfirmClicked))
	{
		RemovePetConfirmButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleRemovePetConfirmClicked);
	}
	if (IsValid(RemovePetCancelButton)
		&& !RemovePetCancelButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleRemovePetCancelClicked))
	{
		RemovePetCancelButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleRemovePetCancelClicked);
	}

	if (IsValid(PlacementAnimalTabButton)
		&& !PlacementAnimalTabButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandlePlacementAnimalTabClicked))
	{
		PlacementAnimalTabButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandlePlacementAnimalTabClicked);
	}
	if (IsValid(PlacementSeedTabButton)
		&& !PlacementSeedTabButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandlePlacementSeedTabClicked))
	{
		PlacementSeedTabButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandlePlacementSeedTabClicked);
	}
	if (IsValid(PetCosmeticPickerButton)
		&& !PetCosmeticPickerButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandlePetCosmeticPickerClicked))
	{
		PetCosmeticPickerButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandlePetCosmeticPickerClicked);
	}
	if (IsValid(PetCosmeticApplyButton)
		&& !PetCosmeticApplyButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandlePetCosmeticApplyClicked))
	{
		PetCosmeticApplyButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandlePetCosmeticApplyClicked);
	}

	if (IsValid(FeedUpkeepButton)
		&& !FeedUpkeepButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleFeedUpkeepClicked))
	{
		FeedUpkeepButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleFeedUpkeepClicked);
	}

	if (IsValid(SlotHarvestButton)
		&& !SlotHarvestButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleSlotHarvestClicked))
	{
		SlotHarvestButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleSlotHarvestClicked);
	}

	if (IsValid(ExpandSlotButton)
		&& !ExpandSlotButton->OnClicked.IsAlreadyBound(this, &URanchHudWidget::HandleExpandSlotClicked))
	{
		ExpandSlotButton->OnClicked.AddDynamic(this, &URanchHudWidget::HandleExpandSlotClicked);
	}

	RefreshDisplay();

	// 글자·버튼 모양은 화면마다 따로 정하지 않는다 — 네 화면이 같은 글꼴·색을 쓴다 (M5-05a·b).
	// `Super` **뒤**라야 한다 — Slate 위젯이 생긴 뒤에 칠해야 화면까지 바뀐다.
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

bool URanchHudWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}

	if (WidgetTree->RootWidget == nullptr)
	{
		return true;
	}

	// 새로 만든 위젯 블루프린트의 빈 CanvasPanel 은 아직 디자인이 아니라 빈 도화지다
	// (`UHuntHudWidget::NeedsPlaceholderTree` 와 같은 판단).
	const UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return RootPanel != nullptr && RootPanel->GetChildrenCount() == 0;
}

void URanchHudWidget::BuildPlaceholderTree()
{
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RanchHudRoot"));
		WidgetTree->RootWidget = Root;
	}

	// M15-03 목장 화면: 평소엔 빈 월드 + 오른쪽 위 잔액 알약뿐이다. 칸 정보는 칸 곁에 설 때만 뜨는
	// 아래 가운데 크림 카드 하나에 모은다(레퍼런스 시트 `docs/reference/hud_ingame.md`).

	// 왼쪽 위 알림 줄 — 크림 카드가 세로로 쌓인다(시트 「최대 2장」: 수확 준비 + 수확 팝업).
	UVerticalBox* const NoticeStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NoticeStack"));
	NoticeStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UCanvasPanelSlot* StackSlot = Cast<UCanvasPanelSlot>(Root->AddChild(NoticeStack)))
	{
		StackSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		StackSlot->SetAlignment(FVector2D(0.f, 0.f));
		StackSlot->SetPosition(FVector2D(PlaceholderScreenMargin, PlaceholderScreenMargin));
		StackSlot->SetAutoSize(true);
	}

	// 수확 알림 — 왼쪽 위 크림 카드 (사냥 HUD 알림 카드와 같은 자리·모양).
	ReadyNoticeCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("ReadyNoticeCard"));
	if (UVerticalBoxSlot* NoticeSlot = NoticeStack->AddChildToVerticalBox(ReadyNoticeCard))
	{
		NoticeSlot->SetHorizontalAlignment(HAlign_Left);
	}
	ReadyNoticeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReadyNoticeText"));
	ReadyNoticeText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*ReadyNoticeText, PlaceholderReadyNoticeFontSize);
	ReadyNoticeCard->SetContent(ReadyNoticeText);

	// 거절 문구 (M12-05) — 위 가운데, 잠깐 떴다 접힌다.
	FailureNoticeText = WidgetTree->ConstructWidget<USnareNoticeText>(USnareNoticeText::StaticClass(), TEXT("FailureNoticeText"));
	if (UCanvasPanelSlot* FailureSlot = Cast<UCanvasPanelSlot>(Root->AddChild(FailureNoticeText)))
	{
		FailureSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		FailureSlot->SetAlignment(FVector2D(0.5f, 0.f));
		FailureSlot->SetPosition(FVector2D(0.f, PlaceholderScreenMargin));
		FailureSlot->SetSize(FVector2D(PlaceholderPanelWidth * 2.f, PlaceholderNoticeHeight));
	}
	FailureNoticeText->SetJustification(ETextJustify::Center);
	FailureNoticeText->HideNotice();

	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("RanchRetryBox"), TEXT("RanchRetryButton"),
		LOCTEXT("RanchRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &URanchHudWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* RetrySlot = Cast<UCanvasPanelSlot>(Root->AddChild(RetryBox)))
	{
		RetrySlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		RetrySlot->SetAlignment(FVector2D(0.5f, 0.f));
		RetrySlot->SetPosition(FVector2D(0.f, PlaceholderScreenMargin + PlaceholderNoticeHeight));
		RetrySlot->SetAutoSize(true);
	}

	// 수확 팝업 (M21-03 → M23-03) — 수확 준비 카드 아래 크림 알림 카드: 아이콘 48 + 제목 18 + 설명 14.
	HarvestCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("HarvestCard"));
	HarvestCard->SetPadding(FMargin(NoticeCardPaddingX, (NoticeCardHeight - NoticeCardIconSize) * 0.5f));
	if (UVerticalBoxSlot* CardSlot = NoticeStack->AddChildToVerticalBox(HarvestCard))
	{
		CardSlot->SetHorizontalAlignment(HAlign_Left);
		CardSlot->SetPadding(FMargin(0.f, NoticeStackGap, 0.f, 0.f));
	}
	USizeBox* const CardBody = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HarvestCardBody"));
	CardBody->SetWidthOverride(NoticeCardWidth - NoticeCardPaddingX * 2.f);
	CardBody->SetHeightOverride(NoticeCardIconSize);
	HarvestCard->SetContent(CardBody);
	UHorizontalBox* const CardRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HarvestCardRow"));
	CardBody->SetContent(CardRow);
	HarvestIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HarvestIcon"));
	HarvestIcon->SetDesiredSizeOverride(FVector2D(NoticeCardIconSize, NoticeCardIconSize));
	if (UHorizontalBoxSlot* IconSlot = CardRow->AddChildToHorizontalBox(HarvestIcon))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, NoticeCardGap, 0.f));
	}
	UVerticalBox* const CardLines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HarvestCardLines"));
	if (UHorizontalBoxSlot* LinesSlot = CardRow->AddChildToHorizontalBox(CardLines))
	{
		LinesSlot->SetVerticalAlignment(VAlign_Center);
		LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	HarvestPopupText = WidgetTree->ConstructWidget<USnareNoticeText>(USnareNoticeText::StaticClass(), TEXT("HarvestPopupText"));
	HarvestPopupText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*HarvestPopupText, PlaceholderCardTitleFontSize);
	CardLines->AddChildToVerticalBox(HarvestPopupText);
	HarvestCoinText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HarvestCoinText"));
	HarvestCoinText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*HarvestCoinText, PlaceholderCardSmallFontSize);
	CardLines->AddChildToVerticalBox(HarvestCoinText);
	HarvestPopupText->SetCompanion(HarvestCard);
	HarvestPopupText->HideNotice();

	// 업적·도감 알림 카드 두 장 (M48-03c) — 같은 알림 줄 셋째 칸, 사냥 HUD 와 같은 모양.
	AchievementCards = WidgetTree->ConstructWidget<USnareNoticeCards>(USnareNoticeCards::StaticClass(), TEXT("AchievementCards"));
	AchievementCards->BuildCards(*WidgetTree);
	NoticeStack->AddChildToVerticalBox(AchievementCards);
#if !UE_BUILD_SHIPPING
	// 3층 캡처용 자리표시 (`-SnareHarvestPopupDemo`) — 오프라인 캡처엔 서버 답이 없어 팝업이 한 번도 안 뜬다.
	// 화면 글자만 바뀐다(가방·코인·칸은 그대로, CLAUDE.md 8). 글은 ROADMAP M21-03 예시 그대로.
	// `-SnareHarvestCapDemo` 는 하루 상한에 닿은 답(코인 0 · `coinsCapped`)의 카드 (M109-04c).
	const bool bCapDemo = SnareCheckSwitch::Param(TEXT("SnareHarvestCapDemo"));
	if (bCapDemo || SnareCheckSwitch::Param(TEXT("SnareHarvestPopupDemo")))
	{
		const FText DemoName = LOCTEXT("RanchHudHarvestDemoItem", "송로버섯");
		const int32 DemoCoins = bCapDemo ? 0 : 10;
		HarvestPopup = URanchSyncComponent::FormatHarvestPopup(DemoName, 1, DemoCoins);
		const UDataTable* const DemoItems = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
		const FItemRow* const DemoRow = SnareItemRows::FindByItemID(DemoItems, TEXT("item.truffle"));
		ShowHarvestCard(URanchSyncComponent::FormatHarvestPopup(DemoName, 1, 0),
			URanchSyncComponent::FormatHarvestCoinLine(DemoCoins, bCapDemo),
			DemoRow != nullptr ? DemoRow->IconRef : TSoftObjectPtr<UTexture2D>(), 600.f);
		UE_LOG(LogSnarehaven, Log, TEXT("검사용 수확 팝업 '%s' · 코인 줄 '%s' (%s)"), *HarvestPopup.ToString(),
			*URanchSyncComponent::FormatHarvestCoinLine(DemoCoins, bCapDemo).ToString(),
			bCapDemo ? TEXT("-SnareHarvestCapDemo") : TEXT("-SnareHarvestPopupDemo"));
	}
#endif

	// 처음 다가가기 안내 알약 (M28-05c2) — 아래 가운데, 자리는 `UpdateUsageHintPill` 이 칸 카드에 맞춰 잡는다.
	UsageHint = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("UsageHint"));
	UsageHint->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* const UsageSlot = Cast<UCanvasPanelSlot>(Root->AddChild(UsageHint)))
	{
		UsageSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
		UsageSlot->SetAlignment(FVector2D(0.5f, 1.f));
		UsageSlot->SetPosition(FVector2D(0.f, -PlaceholderScreenMargin));
		UsageSlot->SetAutoSize(true);
	}
	UsageHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("UsageHintText"));
	UsageHintText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*UsageHintText, PlaceholderCardFontSize);
	UsageHint->SetContent(UsageHintText);

	// 칸 목록은 화면에 늘어놓지 않는다(M15-03 디버그 글자 목록 제거) — 줄은 칸 수를 따라 계속 만들어
	// 두되 접어 둔다. 카드가 같은 `GetSlotText` 를 쓰므로 문구가 한 곳에서 정해진다.
	SlotList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SlotList"));
	Root->AddChild(SlotList);
	SlotList->SetVisibility(ESlateVisibility::Collapsed);

	// 칸 카드 — 아래 가운데. 칸 곁에 섰을 때만 편다(`RefreshSlotCard`).
	SlotCard = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("SlotCard"));
	if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(SlotCard)))
	{
		CardSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
		CardSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CardSlot->SetPosition(FVector2D(0.f, -PlaceholderScreenMargin));
		CardSlot->SetAutoSize(true);
	}
	UVerticalBox* const CardColumn =
		WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SlotCardColumn"));
	SlotCard->SetContent(CardColumn);

	auto AddCardText = [this](UPanelWidget& Parent, const TCHAR* Name, float FontSize) -> UTextBlock*
	{
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		SnareUiStyle::SetFontSize(*Text, FontSize);
		Parent.AddChild(Text);
		return Text;
	};
	// 카드 버튼 한 벌 — 작은 둥근 네모, 글자 16.
	// 글자 이름은 옛 트리 그대로 둔다(`PetPickerText` 처럼) — 검사·WBP 가 그 이름으로 찾는다.
	auto AddCardButton = [this](UPanelWidget& Parent, const TCHAR* Name, UTextBlock*& OutText,
		const TCHAR* TextName = nullptr) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		SnareUiStyle::MakeRoundButton(*Button, PlaceholderCardButtonRadius);
		if (UHorizontalBoxSlot* ButtonSlot = Cast<UHorizontalBoxSlot>(Parent.AddChild(Button)))
		{
			ButtonSlot->SetPadding(FMargin(PlaceholderButtonGap * 0.5f, 0.f));
		}
		OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			TextName != nullptr ? FName(TextName) : FName(*FString::Printf(TEXT("%sText"), Name)));
		OutText->SetJustification(ETextJustify::Center);
		SnareUiStyle::SetFontSize(*OutText, PlaceholderCardFontSize);
		// 버튼 자체가 아니라 안쪽 글자 상자에 최소 크기를 준다 — 버튼을 접으면 자리도 같이 접힌다.
		USizeBox* const MinBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sMin"), Name)));
		MinBox->SetMinDesiredWidth(PlaceholderCardButtonMinWidth);
		MinBox->SetMinDesiredHeight(PlaceholderCardButtonMinHeight);
		if (USizeBoxSlot* const TextSlot = Cast<USizeBoxSlot>(MinBox->AddChild(OutText)))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
		Button->AddChild(MinBox);
		return Button;
	};

	SlotCardTitle = AddCardText(*CardColumn, TEXT("SlotCardTitle"), PlaceholderCardTitleFontSize);
	SlotCardStatus = AddCardText(*CardColumn, TEXT("SlotCardStatus"), PlaceholderCardFontSize);

	// 넣기 판 「동물 / 씨앗」 탭 (M28-08) — 고르기 줄 위. 둘 다 가졌을 때만 편다(`RefreshPlacementTabs`).
	PlacementTabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlacementTabRow"));
	if (UVerticalBoxSlot* TabRowSlot = Cast<UVerticalBoxSlot>(CardColumn->AddChild(PlacementTabRow)))
	{
		TabRowSlot->SetPadding(FMargin(0.f, PlaceholderButtonGap, 0.f, 0.f));
		TabRowSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UTextBlock* AnimalTabText = nullptr;
	PlacementAnimalTabButton = AddCardButton(*PlacementTabRow, TEXT("PlacementAnimalTabButton"), AnimalTabText);
	AnimalTabText->SetText(LOCTEXT("RanchHudPlacementTabAnimal", "동물"));
	UTextBlock* SeedTabText = nullptr;
	PlacementSeedTabButton = AddCardButton(*PlacementTabRow, TEXT("PlacementSeedTabButton"), SeedTabText);
	SeedTabText->SetText(LOCTEXT("RanchHudPlacementTabSeed", "씨앗"));
	PlacementTabRow->SetVisibility(ESlateVisibility::Collapsed);

	// 수확·고르기·빼기 — 한 줄.
	UHorizontalBox* const CardButtonRow =
		WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotCardButtonRow"));
	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(CardColumn->AddChild(CardButtonRow)))
	{
		RowSlot->SetPadding(FMargin(0.f, PlaceholderButtonGap, 0.f, 0.f));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UTextBlock* HarvestText = nullptr;
	SlotHarvestButton = AddCardButton(*CardButtonRow, TEXT("SlotHarvestButton"), HarvestText);
	SlotHarvestButtonText = HarvestText;
	// 접근성 이름(10-16) — 글이 빈 동안(칸을 고르기 전) VoiceOver 가 읽을 이름. `ButtonAccessibleName` 이 툴팁으로 떨어진다.
	SlotHarvestButton->SetToolTipText(LOCTEXT("RanchHudHarvestA11y", "수확"));
	UTextBlock* PickerText = nullptr;
	PetPickerButton = AddCardButton(*CardButtonRow, TEXT("PetPickerButton"), PickerText, TEXT("PetPickerText"));
	PetPickerText = PickerText;
	UTextBlock* RemoveText = nullptr;
	RemovePetButton = AddCardButton(*CardButtonRow, TEXT("RemovePetButton"), RemoveText);
	RemovePetButtonText = RemoveText;

	// 펫 빼기 확인 줄 (#0199) — 「정말 뺄까요? [빼기] [취소]」. 평소엔 접힘.
	RemovePetConfirmRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RemovePetConfirmRow"));
	if (UVerticalBoxSlot* ConfirmSlot = Cast<UVerticalBoxSlot>(CardColumn->AddChild(RemovePetConfirmRow)))
	{
		ConfirmSlot->SetPadding(FMargin(0.f, PlaceholderButtonGap, 0.f, 0.f));
		ConfirmSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UTextBlock* const ConfirmQuestion = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RemovePetConfirmText"));
	ConfirmQuestion->SetText(LOCTEXT("RanchHudRemovePetConfirm", "정말 뺄까요?"));
	ConfirmQuestion->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
	SnareUiStyle::SetFontSize(*ConfirmQuestion, PlaceholderCardFontSize);
	if (UHorizontalBoxSlot* QuestionSlot = Cast<UHorizontalBoxSlot>(RemovePetConfirmRow->AddChild(ConfirmQuestion)))
	{
		QuestionSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* YesText = nullptr;
	RemovePetConfirmButton = AddCardButton(*RemovePetConfirmRow, TEXT("RemovePetConfirmButton"), YesText);
	YesText->SetText(LOCTEXT("RanchHudRemovePetYes", "빼기"));
	UTextBlock* NoText = nullptr;
	RemovePetCancelButton = AddCardButton(*RemovePetConfirmRow, TEXT("RemovePetCancelButton"), NoText);
	NoText->SetText(LOCTEXT("RanchHudRemovePetNo", "취소"));
	RemovePetConfirmRow->SetVisibility(ESlateVisibility::Collapsed);

	// 동물 꾸미기 줄 (DESIGN 10.9) — 「꾸미기: ○○ (1/3)」 [씌우기]. 펫이 선 동물 칸에서만 편다.
	PetCosmeticRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PetCosmeticRow"));
	if (UVerticalBoxSlot* CosmeticSlot = Cast<UVerticalBoxSlot>(CardColumn->AddChild(PetCosmeticRow)))
	{
		CosmeticSlot->SetPadding(FMargin(0.f, PlaceholderButtonGap, 0.f, 0.f));
		CosmeticSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UTextBlock* CosmeticPickerText = nullptr;
	PetCosmeticPickerButton = AddCardButton(*PetCosmeticRow, TEXT("PetCosmeticPickerButton"), CosmeticPickerText);
	PetCosmeticPickerText = CosmeticPickerText;
	PetCosmeticPickerButton->SetToolTipText(LOCTEXT("RanchHudCosmeticPickerA11y", "꾸미기 고르기"));
	UTextBlock* CosmeticApplyText = nullptr;
	PetCosmeticApplyButton = AddCardButton(*PetCosmeticRow, TEXT("PetCosmeticApplyButton"), CosmeticApplyText);
	PetCosmeticApplyText = CosmeticApplyText;
	PetCosmeticApplyButton->SetToolTipText(LOCTEXT("RanchHudCosmeticApplyA11y", "꾸미기 씌우기"));
	PetCosmeticRow->SetVisibility(ESlateVisibility::Collapsed);

	// 유지 줄 + 나무 넣기·칸 확장 (M12-05) — 카드 맨 아래 작은 글자.
	UpkeepText = AddCardText(*CardColumn, TEXT("UpkeepText"), PlaceholderCardSmallFontSize);
	if (UVerticalBoxSlot* UpkeepSlot = Cast<UVerticalBoxSlot>(UpkeepText->Slot))
	{
		UpkeepSlot->SetPadding(FMargin(0.f, PlaceholderButtonGap, 0.f, 0.f));
	}
	UHorizontalBox* const UpkeepButtonRow =
		WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("UpkeepButtonRow"));
	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(CardColumn->AddChild(UpkeepButtonRow)))
	{
		RowSlot->SetPadding(FMargin(0.f, PlaceholderButtonGap * 0.5f, 0.f, 0.f));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UTextBlock* FeedText = nullptr;
	FeedUpkeepButton = AddCardButton(*UpkeepButtonRow, TEXT("FeedUpkeepButton"), FeedText);
	FeedUpkeepButtonText = FeedText;
	UTextBlock* ExpandText = nullptr;
	ExpandSlotButton = AddCardButton(*UpkeepButtonRow, TEXT("ExpandSlotButton"), ExpandText);
	ExpandSlotButtonText = ExpandText;

	// 포탈 버튼 (M6-03b) — 오른쪽 아래 작은 둥근 상황 버튼. 포탈 곁에서만 뜬다(`RefreshDisplay`).
	UTextBlock* TravelText = nullptr;
	ZoneTravelButton = SnareZoneTravelHud::CreateButton(*WidgetTree, TravelText);
	ZoneTravelButtonText = TravelText;
	SnareUiStyle::MakeRoundButton(*ZoneTravelButton, PlaceholderContextButtonRadius);
	SnareUiStyle::SetFontSize(*TravelText, PlaceholderCardTitleFontSize);
	if (UCanvasPanelSlot* TravelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(ZoneTravelButton)))
	{
		TravelSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		TravelSlot->SetAlignment(FVector2D(1.f, 1.f));
		TravelSlot->SetPosition(FVector2D(-PlaceholderScreenMargin, -PlaceholderScreenMargin));
		TravelSlot->SetAutoSize(true);
	}

	// 잔액 줄만은 자리·문구를 여기서 적지 않는다 — 사냥 HUD 와 같아야 해서 한 곳이 정한다 (M7-03).
	BalanceText = SnareBalanceHud::AddPlaceholderRow(*WidgetTree, *Root);
}

void URanchHudWidget::EnsureSlotRows(int32 SlotCount)
{
	if (!IsValid(SlotList))
	{
		return;
	}

	// 칸 수는 `URanchComponent` 가 정한다 — 여기에 5 를 적으면 설계가 두 군데에 적힌다 (DESIGN 3.2).
	while (SlotList->GetChildrenCount() < SlotCount)
	{
		const FName RowName(*FString::Printf(TEXT("SlotRowText_%d"), SlotList->GetChildrenCount()));
		UTextBlock* RowText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), RowName);
		SlotList->AddChild(RowText);
	}

	// 칸이 줄면 남은 줄을 떼어 낸다 (옛 칸이 화면에 남아 있으면 없는 펫을 보여 준다).
	while (SlotList->GetChildrenCount() > SlotCount)
	{
		SlotList->RemoveChildAt(SlotList->GetChildrenCount() - 1);
	}

	// 이 줄들은 화면이 뜬 **뒤에** 생긴다 — `RebuildWidget` 때 한 번 칠한 글꼴·색이 안 닿는다 (M5-05a·b).
	SnareUiStyle::ApplyUiStyle(*this);
}

#undef LOCTEXT_NAMESPACE
