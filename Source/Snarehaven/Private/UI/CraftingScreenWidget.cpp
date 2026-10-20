#include "UI/CraftingScreenWidget.h"

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Building/SnarePowerSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Data/BaseRuleRow.h"
#include "Data/ItemRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "Styling/SlateBrush.h"
#include "TimerManager.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnareUiStyle.h"
#include "World/SnareSound.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 UI 배치 (Autonomy 🟡 — 게임 규칙이 아니라 "화면 어디에 얼마나 크게"). 캡처 1728x1117 가운데 한 장.
	constexpr float PanelWidth = 1000.f;
	constexpr float PanelHeight = 960.f;
	constexpr float PanelPadding = 24.f;
	/** 손가락 한 마디(약 9mm) 이상 — 모바일 터치 버튼 최소 높이. */
	constexpr float TouchRowHeight = SnareUiStyle::MenuTouchMin;
	constexpr float RowGap = 8.f;
	constexpr float TabGap = 12.f;
	/** 쪽 탭 줄 높이 (M38-04b) — DESIGN 2 「🟡 누르는 칸 최소 크기」 72. 글 높이만 받으면 48 이었다. */
	constexpr float TabRowHeight = SnareUiStyle::MenuTouchMin;
	/** 레시피 줄 원판 (M23-04) — 상점 거래 줄 원판(시트 52px)과 같은 크기. */
	constexpr float RecipeDiscSize = 52.f;
	/** 재료 원판 — 글자 줄 높이에 맞춘 작은 원판. */
	constexpr float IngredientDiscSize = 32.f;
	/** 원판 안 그림 여백 — 배낭 칸·상점 원판과 같은 값. */
	constexpr float DiscIconPadding = 6.f;
	/** 줄 안 여백 — 옛 글자 버튼의 좌우 여백과 같은 값. */
	constexpr float RowInnerPadding = 16.f;
	// 제목 탭 (M24-02) — 배낭·상점 판과 같은 규격(시트 `inventory_backpack.md` 「제목 탭: 진갈색 알약, 패널 왼쪽 위
	// 모서리에 걸쳐 위로 튀어나옴」). 폭은 최소값 — `칠하기 - 나무 의자 (안 칠함)` 처럼 긴 제목은 글자만큼 늘어난다.
	constexpr float TitlePillMinWidth = SnareUiStyle::PanelTitlePillMinWidth;
	constexpr float TitlePillHeight = SnareUiStyle::PanelTitlePillHeight;
	constexpr float TitlePillFontSize = SnareUiStyle::PanelTitlePillFontSize;
	constexpr float TitlePillPaddingX = SnareUiStyle::PanelTitlePillPaddingX;
	/** 레시피 줄 오른쪽 「만들기」 단추 폭 — 글 4자(28pt)와 좌우 여백. */
	constexpr float CraftMakeButtonWidth = 200.f;
	// 배 보관함 칸 짜임 (INBOX 지시 10-07 01:25, 🟡 배치) — 칸은 DESIGN 2 「누르는 칸 최소 크기」 72.
	// 판 안 폭 952 = 가방 7열(7×72 + 6×10 = 564) + 틈 24 + 배 4열(4×72 + 3×10 = 318) + 여유. 배 8칸 = 4열 2줄.
	constexpr float BoatCellSize = SnareUiStyle::ScreenCloseButtonHeight;
	constexpr float BoatCellGap = 10.f;
	constexpr float BoatCellRadius = 8.f;
	/** 칸 안 원판 — 칸 72 에서 위아래 8 여백. */
	constexpr float BoatCellDiscSize = 56.f;
	constexpr int32 BoatCellCountFont = 14;
	constexpr int32 BoatBagColumns = 7;
	constexpr int32 BoatStoreColumns = 4;
	constexpr float BoatSectionGap = 24.f;
	/** 위 띠 큰 원판 — 레시피 원판(52)보다 크게, 띠 높이가 버튼 72 와 비슷하게. */
	constexpr float BoatDetailDiscSize = 88.f;
	constexpr int32 BoatDetailNameFont = 22;
	constexpr int32 BoatDetailInfoFont = 16;
	constexpr float BoatMoveButtonWidth = 160.f;
	/** 보트 컨테이너 칸 — 못 넣는 칸은 흐리게. */
	constexpr float BoatLockedCellOpacity = 0.45f;

	/** 집 색 이름. 순서는 `ASnarePlacedStructureActor::HouseColorAt` 과 같다 (서버 `color` 0..4). */
	FText HouseColorName(int32 Index)
	{
		switch (Index)
		{
		case 0: return LOCTEXT("HouseColorRed", "빨강");
		case 1: return LOCTEXT("HouseColorBlue", "파랑");
		case 2: return LOCTEXT("HouseColorGreen", "초록");
		case 3: return LOCTEXT("HouseColorYellow", "노랑");
		default: return LOCTEXT("HouseColorWhite", "하양");
		}
	}
}

void UCraftingScreenWidget::Setup(USnareBaseBuildComponent* InOwner, USnareBaseSubsystem* InBase)
{
	BuildOwner = InOwner;

	if (IsValid(Base))
	{
		Base->OnBaseChanged.Remove(BaseChangedHandle);
		Base->OnBaseMessage.Remove(BaseMessageHandle);
	}
	Base = InBase;
	if (IsValid(Base))
	{
		BaseChangedHandle = Base->OnBaseChanged.AddUObject(this, &UCraftingScreenWidget::HandleBaseChanged);
		BaseMessageHandle = Base->OnBaseMessage.AddUObject(this, &UCraftingScreenWidget::HandleBaseMessage);
	}

	USnareBackendSubsystem* const Backend = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	if (WatchedBackend != Backend)
	{
		if (IsValid(WatchedBackend))
		{
			WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UCraftingScreenWidget::HandleBackendSnapshotChanged);
		}
		WatchedBackend = Backend;
		if (IsValid(WatchedBackend))
		{
			WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &UCraftingScreenWidget::HandleBackendSnapshotChanged);
		}
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

USnareBackendSubsystem* UCraftingScreenWidget::GetBackend() const
{
	return WatchedBackend;
}

void UCraftingScreenWidget::AddToScreenPanelLayer()
{
	// 예전 Z 1 은 「다음 목표」 띠(5) 아래라 띠가 판 위에 그려졌다(INBOX 결함 10-07 01:18 ③).
	AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
}

void UCraftingScreenWidget::Open(ESnareBaseScreenPage FirstPage)
{
	bOpen = true;
	Page = FirstPage;
	SetVisibility(ESlateVisibility::Visible);
	if (IsValid(ToastText))
	{
		ToastText->SetText(FText::GetEmpty());
	}

	// 요리 남은 초만 1초마다 고친다 — 줄 버튼은 다시 세우지 않아 누르는 중인 손가락을 뺏지 않는다.
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(CookTimerHandle, this, &UCraftingScreenWidget::HandleCookTick, 1.f, true);
	}
	RefreshDisplay();
}

void UCraftingScreenWidget::OpenBackpack()
{
	Target.Reset();
	CraftPlace = ESnareCraftPlace::Backpack;
	Open(ESnareBaseScreenPage::Craft);
}

void UCraftingScreenWidget::OpenBoatStorage()
{
	Target.Reset();
	CraftPlace = ESnareCraftPlace::Backpack;
	Open(ESnareBaseScreenPage::BoatStorage);
}

void UCraftingScreenWidget::OpenWorkbenchForTest()
{
	Target.Reset();
	CraftPlace = ESnareCraftPlace::Workbench;
	Open(ESnareBaseScreenPage::Craft);
}

void UCraftingScreenWidget::SetBackendForTest(USnareBackendSubsystem* InBackend)
{
	WatchedBackend = InBackend;
}

void UCraftingScreenWidget::OpenFor(ASnarePlacedStructureActor* InTarget)
{
	Target = InTarget;
	if (!IsValid(InTarget))
	{
		OpenBackpack();
		return;
	}

	switch (InTarget->GetPlaceableFunction())
	{
	case EPlaceableFunction::Tent:
	case EPlaceableFunction::Workbench:
		CraftPlace = ESnareCraftPlace::Workbench;
		Open(ESnareBaseScreenPage::Craft);
		break;
	case EPlaceableFunction::Campfire:
	case EPlaceableFunction::Grill:
		CraftPlace = ESnareCraftPlace::Fire;
		Open(ESnareBaseScreenPage::Craft);
		break;
	case EPlaceableFunction::Smelter:
		CraftPlace = ESnareCraftPlace::Smelter;
		Open(ESnareBaseScreenPage::Craft);
		break;
	case EPlaceableFunction::Storage:
		CraftPlace = ESnareCraftPlace::Backpack;
		Open(ESnareBaseScreenPage::Storage);
		break;
	case EPlaceableFunction::Generator:
		// 내 발전기면 칠하기 쪽 단추도 같이 뜬다(`GetPaintTarget`) — 먼저 보이는 건 연료 쪽.
		CraftPlace = ESnareCraftPlace::Backpack;
		Open(ESnareBaseScreenPage::Generator);
		break;
	default:
		// 페인트를 가진 채 내 가구를 누르면 그 가구가 칠할 대상이다 (DESIGN 13.8 🟡).
		if (TapPaintTarget(InTarget))
		{
			break;
		}
		// 까닭 알림이 떠 있는 동안 같은 전기 가구를 또 「쓰기」 → 이름·까닭·「가방에 넣기」 만 있는 작은 판
		// (INBOX 지시 10-20 15:40 — 페인트가 없어도 전기 가구를 치울 수 있어야 한다).
		if (InTarget->GetPowerUse() > 0 && IsElectricNoticeShown(InTarget))
		{
			CraftPlace = ESnareCraftPlace::Backpack;
			Open(ESnareBaseScreenPage::Furniture);
			break;
		}
		// 전기 가구 — 제작 판 대신 켜짐/꺼짐 까닭 알림 한 줄만 (냉장고를 눌렀는데 제작 목록이 뜨면 뜻을 모른다).
		if (PostElectricStatus(InTarget))
		{
			Target.Reset();
			break;
		}
		// 앉거나 보기만 하는 가구 — 배낭 제작 + 가방에 넣기.
		CraftPlace = ESnareCraftPlace::Backpack;
		Open(ESnareBaseScreenPage::Craft);
		break;
	}
}

FText UCraftingScreenWidget::MeasureElectricStatus(const ASnarePlacedStructureActor* Structure, bool& bOutOn) const
{
	bOutOn = false;
	const FBaseRuleRow* const Rules = IsValid(Base) ? Base->GetRules() : nullptr;
	if (!IsValid(Structure) || Structure->GetPowerUse() <= 0 || Rules == nullptr)
	{
		return FText::GetEmpty();
	}
	// 놓인 액터 켜짐(`ApplyPower`, 0.5초마다)과 같은 셈을 지금 다시 — 막 놓거나 연료가 끝난 순간에도 숫자와 판정이 맞는다.
	int32 Supply = 0;
	int32 Use = 0;
	USnarePowerSubsystem::MeasureLand(GetWorld(), *Rules, Structure->GetOwnerPlayFabId(), Structure->GetTentId(), Supply, Use);
	bOutOn = SnareBuildRules::IsLandPowered(*Rules, Supply, Use);
	return FormatElectricStatusText(bOutOn, Supply, Use);
}

UHuntHudWidget* UCraftingScreenWidget::ResolveNoticeHuntHud() const
{
	const ASnarehavenCharacter* const Character = BuildOwner.IsValid() ? Cast<ASnarehavenCharacter>(BuildOwner->GetOwner()) : nullptr;
	return TestHuntHud.IsValid() ? TestHuntHud.Get() : (IsValid(Character) ? Character->GetHuntHud() : nullptr);
}

URanchHudWidget* UCraftingScreenWidget::ResolveNoticeRanchHud() const
{
	const ASnarehavenCharacter* const Character = BuildOwner.IsValid() ? Cast<ASnarehavenCharacter>(BuildOwner->GetOwner()) : nullptr;
	return TestRanchHud.IsValid() ? TestRanchHud.Get() : (IsValid(Character) ? Character->GetRanchHud() : nullptr);
}

bool UCraftingScreenWidget::IsElectricNoticeShown(const ASnarePlacedStructureActor* Structure) const
{
	if (!IsValid(Structure) || ElectricNoticeTarget.Get() != Structure || ElectricNoticeText.IsEmpty())
	{
		return false;
	}
	// 알림 길(`RouteBaseMessage`)과 같은 순서 — 사냥 HUD 가 있으면 거기, 없으면 목장 HUD. 시계를 따로 세지 않고 HUD 가 지금 보이는 것으로.
	if (const UHuntHudWidget* const Hunt = ResolveNoticeHuntHud())
	{
		return Hunt->GetCaptureBannerText().EqualTo(ElectricNoticeText) || Hunt->GetErrorBannerText().EqualTo(ElectricNoticeText);
	}
	const URanchHudWidget* const Ranch = ResolveNoticeRanchHud();
	return IsValid(Ranch) && Ranch->IsFailureNoticeShown() && Ranch->GetFailureNoticeText().EqualTo(ElectricNoticeText);
}

bool UCraftingScreenWidget::CanBuildNow() const
{
	return bCanBuildForTest || (BuildOwner.IsValid() && BuildOwner->CanBuildHere());
}

bool UCraftingScreenWidget::PressPickUp()
{
	// 텐트는 서버가 못 치우게 한다 — 줄도 안 뜬다(`GetStructureId` 빈 것은 아직 서버 답이 없는 것).
	if (!Target.IsValid() || Target->GetStructureId().IsEmpty() || !IsValid(Base)
		|| !Base->RequestRemoveStructure(Target->GetTentId(), Target->GetStructureId()))
	{
		return false;
	}
	Close();
	return true;
}

bool UCraftingScreenWidget::PostElectricStatus(const ASnarePlacedStructureActor* Structure)
{
	bool bOn = false;
	const FText Line = MeasureElectricStatus(Structure, bOn);
	if (Line.IsEmpty())
	{
		return false;
	}
	ElectricNoticeText = Line;
	ElectricNoticeTarget = Structure;
	if (bOpen)
	{
		Close(); // 다른 판이 떠 있으면 알림이 판 뒤로 안 가게 — 알림 길은 판이 닫혀 있을 때만 HUD 에 띄운다.
	}
	const bool bShown = ASnarehavenCharacter::RouteBaseMessage(ElectricNoticeText, !bOn, this, ResolveNoticeHuntHud(), ResolveNoticeRanchHud());
	UE_LOG(LogSnarehaven, Log, TEXT("전기 가구 까닭 (M108-08e) — %s · 「%s」 · 놓인 몸 켜짐 %s · HUD %s"), *Structure->GetItemID().ToString(),
		*ElectricNoticeText.ToString(), Structure->IsElectricOn() ? TEXT("예") : TEXT("아니오"), bShown ? TEXT("예") : TEXT("아니오"));
	return true;
}

void UCraftingScreenWidget::SetNoticeHudsForTest(UHuntHudWidget* InHunt, URanchHudWidget* InRanch)
{
	TestHuntHud = InHunt;
	TestRanchHud = InRanch;
}

void UCraftingScreenWidget::ShowPage(ESnareBaseScreenPage InPage)
{
	Page = InPage;
	RefreshDisplay();
}

void UCraftingScreenWidget::Close()
{
	bOpen = false;
	Target.Reset();
	SyncOutline();
	SetVisibility(ESlateVisibility::Collapsed);
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CookTimerHandle);
	}
}

void UCraftingScreenWidget::HandleCloseClicked()
{
	Close();
}

void UCraftingScreenWidget::HandleBackendSnapshotChanged()
{
	if (bOpen)
	{
		RefreshDisplay();
	}
}

void UCraftingScreenWidget::HandleBaseChanged()
{
	if (bOpen)
	{
		RefreshDisplay();
	}
}

void UCraftingScreenWidget::HandleBaseMessage(const FText& Message, bool bError)
{
	// 전수 표 실측(M28-09) — 판이 열려 있으면 결과 글은 HUD 알약이 아니라 이 토스트로만 간다.
	if (SnareSound::IsActionLogOn() && bOpen)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 제작 판 글 「%s」 (%s)"), *Message.ToString(), bError ? TEXT("오류") : TEXT("알림"));
	}
	if (IsValid(ToastText))
	{
		ToastText->SetText(Message);
		ToastText->SetColorAndOpacity(FSlateColor(bError ? SnareUiStyle::UiWarnOnWood() : SnareUiStyle::UiTextOnWood()));
	}
}

void UCraftingScreenWidget::HandleCookTick()
{
	// 발전기 남은 시간도 같은 1초 타이머로 — 글만 고치고 단추 줄은 안 건드린다.
	if (bOpen && Page == ESnareBaseScreenPage::Generator && IsValid(GeneratorFuelLine) && Target.IsValid())
	{
		GeneratorFuelLine->SetText(FormatGeneratorFuelText(Target->GetGeneratorFuelSecondsLeft()));
		return;
	}
	if (!bOpen || !IsTimedPlace() || !IsValid(StatusText) || !IsValid(Base))
	{
		return;
	}

	const float Remaining = Base->GetCookRemainingSeconds(FireKey());
	const FSnareCookJob* const Job = Base->FindCookJob(FireKey());
	const bool bSmelt = CraftPlace == ESnareCraftPlace::Smelter;
	StatusText->SetText(Job != nullptr && Remaining >= 0.f
		? FText::Format(bSmelt ? LOCTEXT("CraftSmelting", "{0} 녹이는 중 - {1}초 남음") : LOCTEXT("CraftCooking", "{0} 굽는 중 - {1}초 남음"),
			ItemName(Job->ResultItemID), FMath::CeilToInt32(Remaining))
		: FText::Format(bSmelt ? LOCTEXT("CraftSmeltIdle", "제련 한 번에 {0}초") : LOCTEXT("CraftFireIdle", "요리 한 번에 {0}초"),
			FMath::RoundToInt32(Target.IsValid() ? Base->GetCookSeconds(Target->GetItemID()) : -1.f)));
}

FString UCraftingScreenWidget::FireKey() const
{
	// 불 하나에 요리 하나 — 서버 가구 ID 가 불의 이름이다.
	return Target.IsValid() ? Target->GetStructureId() : FString();
}

FText UCraftingScreenWidget::ItemName(FName ItemID) const
{
	const FItemRow* const Row = IsValid(Base) ? Base->FindItemRow(ItemID) : nullptr;
	return Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromName(ItemID);
}

TArray<FSnareRecipeView> UCraftingScreenWidget::BuildRecipes() const
{
	TArray<FSnareRecipeView> Views;
	if (IsValid(Base))
	{
		static const FSnarePlayerSnapshot EmptySnapshot;
		SnareCraftRules::BuildRecipeList(Base->GetRecipeTable(), Base->GetItemTable(),
			IsValid(WatchedBackend) ? WatchedBackend->GetPlayerSnapshot() : EmptySnapshot, CraftPlace, Views);
		// 내 텐트 = 서버가 확정한(`PlacedAtUtc` 있는) 사본. 판정은 서버 `CraftItem` — 이건 단추 색·안내만 (CLAUDE.md 8).
		SnareCraftRules::MarkTentNeeded(Views, Base->GetTents().ContainsByPredicate(
			[](const FSnareTentMirror& Tent) { return !Tent.PlacedAtUtc.IsEmpty(); }));
	}
	return Views;
}

bool UCraftingScreenWidget::PressRecipe(FName ResultItemID)
{
	if (!IsValid(Base))
	{
		return false;
	}
	if (IsTimedPlace())
	{
		// 요리·제련은 불 위에서 시간이 끝난 뒤에 서버가 굽는다 — 지금은 타이머만 건다 (DESIGN 8.10).
		return Target.IsValid() && Base->StartCooking(FireKey(), Target->GetItemID(), ResultItemID);
	}
	return Base->RequestCraft(ResultItemID, 1, true, CraftPlace);
}

bool UCraftingScreenWidget::PickPaintTarget(ASnarePlacedStructureActor* Structure)
{
	// 서버도 남의 가구는 `not_found` 로 막는다 — 여기선 고르지도 않는다.
	if (!IsValid(Structure) || !IsValid(Base) || Structure->IsPreview() || Structure->IsTent()
		|| !Base->IsOwnFurniture(Structure->GetTentId(), Structure->GetStructureId()))
	{
		return false;
	}
	Target = Structure;
	CraftPlace = ESnareCraftPlace::Backpack;
	Open(ESnareBaseScreenPage::Paint);
	return true;
}

ASnarePlacedStructureActor* UCraftingScreenWidget::GetPaintTarget() const
{
	// 작업대(텐트 포함) 쪽은 목록으로 고른다 — 한 가구 대상은 그 밖의 내 가구뿐.
	return Target.IsValid() && CraftPlace != ESnareCraftPlace::Workbench && !Target->IsTent() && IsValid(Base)
		&& Base->IsOwnFurniture(Target->GetTentId(), Target->GetStructureId()) ? Target.Get() : nullptr;
}

bool UCraftingScreenWidget::PressPaint(FName PaintItemID)
{
	const ASnarePlacedStructureActor* const Picked = GetPaintTarget();
	return Picked != nullptr && !PaintItemID.IsNone()
		&& Base->RequestPaintStructure(Picked->GetTentId(), Picked->GetStructureId(), PaintItemID);
}

bool UCraftingScreenWidget::TapPaintTarget(ASnarePlacedStructureActor* Structure)
{
	return CollectPaints().Num() > 0 && PickPaintTarget(Structure);
}

TMap<FName, int32> UCraftingScreenWidget::CollectPaints() const
{
	TMap<FName, int32> Paints;
	if (IsValid(Base) && IsValid(WatchedBackend))
	{
		for (const FInventorySlot& InvSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
		{
			const FItemRow* const Row = Base->FindItemRow(InvSlot.ItemID);
			if (Row != nullptr && !Row->PaintColorHex.IsEmpty() && InvSlot.Count > 0)
			{
				Paints.FindOrAdd(InvSlot.ItemID) += InvSlot.Count;
			}
		}
	}
	return Paints;
}

FText UCraftingScreenWidget::FormatDeadlineText(const FSnareTentMirror& Tent)
{
	return Tent.bUpgraded || Tent.UpgradeDeadlineUtc.IsEmpty() ? FText::GetEmpty()
		: FText::Format(LOCTEXT("HouseDeadline", "집 올리기 마감 {0}"), FText::FromString(Tent.UpgradeDeadlineUtc));
}

FText UCraftingScreenWidget::FormatUpkeepText(const FSnareTentMirror& Tent)
{
	return !Tent.bUpgraded || Tent.UpkeepUntilUtc.IsEmpty() ? FText::GetEmpty()
		: FText::Format(LOCTEXT("HouseUpkeep", "유지 {0} 까지"), FText::FromString(Tent.UpkeepUntilUtc));
}

FText UCraftingScreenWidget::FormatFloorText(const FSnareTentMirror& Tent, const FBaseRuleRow& Rules)
{
	return !Tent.bUpgraded ? FText::GetEmpty()
		: FText::Format(LOCTEXT("HouseFloor", "{0}층 집 · 가구 {1}/{2}"), FMath::Max(1, Tent.Floors),
			FText::AsNumber(Tent.GetFurnitureLoad(), &FNumberFormattingOptions().SetMaximumFractionalDigits(1)),
			SnareBuildRules::FurnitureMaxFor(Rules, Tent.Floors));
}

FText UCraftingScreenWidget::FormatGeneratorFuelText(double SecondsLeft)
{
	if (SecondsLeft <= 0.0)
	{
		return LOCTEXT("GeneratorStopped", "멈춤 - 연료 없음");
	}
	// 분은 올림 — 30초 남았을 때 「0분 남음」 이 아니라 「1분 남음」.
	const int32 TotalMinutes = FMath::CeilToInt32(SecondsLeft / 60.0);
	const int32 Hours = TotalMinutes / 60;
	const int32 Minutes = TotalMinutes % 60;
	if (Hours <= 0)
	{
		return FText::Format(LOCTEXT("GeneratorRunMinutes", "돌아감 · 연료 {0}분 남음"), Minutes);
	}
	return Minutes == 0
		? FText::Format(LOCTEXT("GeneratorRunHours", "돌아감 · 연료 {0}시간 남음"), Hours)
		: FText::Format(LOCTEXT("GeneratorRunHoursMinutes", "돌아감 · 연료 {0}시간 {1}분 남음"), Hours, Minutes);
}

FText UCraftingScreenWidget::FormatLandPowerText(int32 Supply, int32 Use)
{
	return FText::Format(LOCTEXT("GeneratorLandPower", "이 땅 전력 공급 {0} / 소모 {1}"), Supply, Use);
}

FText UCraftingScreenWidget::FormatElectricStatusText(bool bOn, int32 Supply, int32 Use)
{
	return bOn ? FText::Format(LOCTEXT("ElectricOn", "켜짐 — 이 땅 전력 공급 {0} / 소모 {1}"), Supply, Use)
		: FText::Format(LOCTEXT("ElectricOff", "꺼짐 — 전력이 모자랍니다 (공급 {0} / 소모 {1})"), Supply, Use);
}

bool UCraftingScreenWidget::CanFeedGenerator(bool bOwn, double SecondsLeft, int32 FuelMinutes, int32 MaxHours)
{
	return bOwn && FuelMinutes > 0 && MaxHours > 0
		&& static_cast<double>(MaxHours) * 3600.0 - FMath::Max(SecondsLeft, 0.0) >= static_cast<double>(FuelMinutes) * 60.0;
}

FText UCraftingScreenWidget::FormatIngredientText(const FSnareIngredientView& Ingredient)
{
	return Ingredient.IsEnough()
		? FText::Format(LOCTEXT("CraftIngredientOk", "{0} {1}/{2}"), Ingredient.Label, Ingredient.Have, Ingredient.Need)
		: FText::Format(LOCTEXT("CraftIngredientShort", "× {0} {1}/{2} (모자람)"), Ingredient.Label, Ingredient.Have,
			Ingredient.Need);
}

UButton* UCraftingScreenWidget::AddButtonRow(const FText& Label, bool bEnabled, TFunction<void()> OnClick)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRowButton_%d"), WidgetCounter++)));
	// M27-01 시트 「모든 패널 모서리 둥글게」 — 모양만 바꾸고 색은 `ApplyUiStyle` 몫.
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRowText_%d"), WidgetCounter++)));
	Text->SetText(Label);
	Text->SetAutoWrapText(true);
	// 버튼 칸 가운데 정렬이면 글이 자기 크기만 받아 좁게 접힌다 — 줄 폭을 다 쓰고 왼쪽부터 읽게 한다.
	if (UButtonSlot* const TextSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	Button->SetIsEnabled(bEnabled);

	USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
	Proxy->OnClicked = [OnClick = MoveTemp(OnClick)](FName) { OnClick(); };
	Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
	ClickProxies.Add(Proxy);

	// 손가락이 닿게 위아래 여백으로 줄 높이를 터치 크기에 맞춘다.
	FButtonStyle Style = Button->GetStyle();
	Style.NormalPadding = FMargin(16.f, (TouchRowHeight - 40.f) * 0.5f);
	Style.PressedPadding = Style.NormalPadding;
	Button->SetStyle(Style);

	if (UScrollBoxSlot* const ScrollSlot = Cast<UScrollBoxSlot>(RowList->AddChild(Button)))
	{
		ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
	}
	return Button;
}

UWidget* UCraftingScreenWidget::MakeItemDisc(FName ItemID, float Size, const FString& NamePrefix, const FString& NameSuffix)
{
	// 배낭 칸·상점 줄과 같은 짜임 — 등급 색 원판 위에 아이템 표 그림.
	const FItemRow* const Row = IsValid(Base) && !ItemID.IsNone() ? Base->FindItemRow(ItemID) : nullptr;
	UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
		FName(*FString::Printf(TEXT("%sDiscStack%s"), *NamePrefix, *NameSuffix)));
	UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("%sDisc%s"), *NamePrefix, *NameSuffix)));
	// 어두운 나무 홈 + 아래 1/5 등급 띠 — 색 동그라미는 레퍼런스 게임와 같아 버렸다 (M101-04, 배낭 칸과 같은 짜임).
	Disc->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiSlotWell(), 4.f, SnareUiStyle::UiWoodButton(), 2.f));
	if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(Disc))
	{
		// 빈 Border 는 원하는 크기가 0 이다 — 칸을 꽉 채워야 홈이 보인다 (배낭 칸과 같은 이유).
		DiscSlot->SetHorizontalAlignment(HAlign_Fill);
		DiscSlot->SetVerticalAlignment(VAlign_Fill);
	}
	SnareUiStyle::AddGradeBand(*WidgetTree, *Stack, FName(*FString::Printf(TEXT("%sGrade%s"), *NamePrefix, *NameSuffix)),
		SnareUiStyle::ItemGradeBandColor(Row != nullptr ? Row->Grade : EItemGrade::Common), Size);
	if (Row != nullptr && !Row->IconRef.IsNull())
	{
		UImage* const Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*FString::Printf(TEXT("%sIcon%s"), *NamePrefix, *NameSuffix)));
		Icon->SetBrushFromSoftTexture(Row->IconRef);
		if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Icon))
		{
			IconSlot->SetPadding(FMargin(DiscIconPadding * Size / RecipeDiscSize));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("%sDiscBox%s"), *NamePrefix, *NameSuffix)));
	Box->SetWidthOverride(Size);
	Box->SetHeightOverride(Size);
	Box->AddChild(Stack);
	return Box;
}

UButton* UCraftingScreenWidget::AddRecipeRow(const FSnareRecipeView& View, int32 RowIndex, bool bEnabled,
	TFunction<void()> OnClick)
{
	// 줄 바탕은 나무 판(눌리지 않음), 누름은 오른쪽 「만들기」 단추만 (10-16 지시) — 재료가 모자라면 단추만 회색으로 꺼지고
	// 그림·재료 글은 그대로 읽힌다. 단추 글은 위 겹이라 꺼져도 흐려지지 않는다(`MakeClearLabelButton`).
	UBorder* const Row = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRecipeRow_%d"), RowIndex)));
	Row->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodButton(), SnareUiStyle::HudCornerRadius));
	Row->SetPadding(FMargin(RowInnerPadding, RowGap));
	USizeBox* const MinHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRecipeMin_%d"), RowIndex)));
	MinHeight->SetMinDesiredHeight(SnareUiStyle::MenuTouchMin);
	UHorizontalBox* const Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRecipeContent_%d"), RowIndex)));
	MinHeight->AddChild(Content);
	Row->SetContent(MinHeight);

	UButton* Button = nullptr;
	UTextBlock* ButtonLabel = nullptr;
	UOverlay* const ButtonStack = SnareUiStyle::MakeClearLabelButton(*WidgetTree,
		FName(*FString::Printf(TEXT("CraftRecipeButton_%d"), RowIndex)), LOCTEXT("CraftMakeButton", "만들기"),
		SnareUiStyle::MenuFontMin + 4.f, Button, ButtonLabel);
	Button->SetIsEnabled(bEnabled);
	USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
	Proxy->OnClicked = [OnClick = MoveTemp(OnClick)](FName) { OnClick(); };
	Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
	ClickProxies.Add(Proxy);
	USizeBox* const ButtonBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRecipeButtonBox_%d"), RowIndex)));
	ButtonBox->SetWidthOverride(CraftMakeButtonWidth);
	ButtonBox->SetHeightOverride(SnareUiStyle::MenuTouchMin);
	ButtonBox->AddChild(ButtonStack);

	const FString RowSuffix = FString::Printf(TEXT("_%d"), RowIndex);
	UWidget* const ResultDisc = MakeItemDisc(View.ResultItemID, RecipeDiscSize, TEXT("CraftRecipe"), RowSuffix);
	if (UHorizontalBoxSlot* const DiscSlot = Cast<UHorizontalBoxSlot>(Content->AddChild(ResultDisc)))
	{
		DiscSlot->SetVerticalAlignment(VAlign_Center);
		DiscSlot->SetPadding(FMargin(0.f, 0.f, RowInnerPadding, 0.f));
	}

	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRecipeColumn_%d"), RowIndex)));
	if (UHorizontalBoxSlot* const ColumnSlot = Cast<UHorizontalBoxSlot>(Content->AddChild(Column)))
	{
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* const Head = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRecipeName_%d"), RowIndex)));
	Head->SetText(View.bLocked
		? FText::Format(LOCTEXT("CraftRowLocked", "잠김: {0} x{1} - 설계도 필요"), View.ResultName, View.ResultCount)
		: View.bNeedsTent
		? FText::Format(LOCTEXT("CraftRowNeedTent", "잠김: {0} x{1} - 텐트 필요"), View.ResultName, View.ResultCount)
		: FText::Format(LOCTEXT("CraftRow", "{0} x{1}"), View.ResultName, View.ResultCount));
	Head->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));   // 나무 줄 위 글 (M99-06)
	Column->AddChild(Head);

	// 재료 — 작은 원판 + `나뭇가지 0/5`, 줄이 넘치면 다음 줄로.
	UWrapBox* const Ingredients = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftIngredients_%d"), RowIndex)));
	Column->AddChild(Ingredients);
	for (int32 Index = 0; Index < View.Ingredients.Num(); ++Index)
	{
		const FSnareIngredientView& Ingredient = View.Ingredients[Index];
		const FString Suffix = FString::Printf(TEXT("_%d_%d"), RowIndex, Index);
		UHorizontalBox* const Chip = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
			FName(*(TEXT("CraftIngredientChip") + Suffix)));
		if (UHorizontalBoxSlot* const ChipDisc = Cast<UHorizontalBoxSlot>(Chip->AddChild(
			MakeItemDisc(Ingredient.ItemID, IngredientDiscSize, TEXT("CraftIngredient"), Suffix))))
		{
			ChipDisc->SetVerticalAlignment(VAlign_Center);
			ChipDisc->SetPadding(FMargin(0.f, 0.f, RowGap, 0.f));
		}
		UTextBlock* const Amount = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(TEXT("CraftIngredientText") + Suffix)));
		Amount->SetText(FormatIngredientText(Ingredient));
		Amount->SetColorAndOpacity(FSlateColor(Ingredient.IsEnough() ? SnareUiStyle::UiTextOnWood() : SnareUiStyle::UiWarnOnWood()));
		if (Ingredient.IsEnough())
		{
			if (UHorizontalBoxSlot* const AmountSlot = Cast<UHorizontalBoxSlot>(Chip->AddChild(Amount)))
			{
				AmountSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		else
		{
			// 모자란 재료 — 글자색만이 아니라 진한 붉은 갈색 칩 + 「×」 로 모양도 다르게 (M99-06 보정).
			UBorder* const ShortChip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				FName(*(TEXT("CraftIngredientShortChip") + Suffix)));
			ShortChip->SetBrushColor(SnareUiStyle::UiWarnChip());
			ShortChip->SetPadding(FMargin(6.f, 1.f));
			ShortChip->SetContent(Amount);
			if (UHorizontalBoxSlot* const AmountSlot = Cast<UHorizontalBoxSlot>(Chip->AddChild(ShortChip)))
			{
				AmountSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		if (UWrapBoxSlot* const ChipSlot = Cast<UWrapBoxSlot>(Ingredients->AddChild(Chip)))
		{
			ChipSlot->SetPadding(FMargin(0.f, RowGap * 0.5f, RowInnerPadding, 0.f));
		}
	}

	if (UHorizontalBoxSlot* const ButtonSlot = Cast<UHorizontalBoxSlot>(Content->AddChild(ButtonBox)))
	{
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
		ButtonSlot->SetPadding(FMargin(RowInnerPadding, 0.f, 0.f, 0.f));
	}

	if (UScrollBoxSlot* const ScrollSlot = Cast<UScrollBoxSlot>(RowList->AddChild(Row)))
	{
		ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
	}
	return Button;
}

UTextBlock* UCraftingScreenWidget::AddTextRow(const FText& Label)
{
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("CraftRowLine_%d"), WidgetCounter++)));
	Text->SetText(Label);
	Text->SetAutoWrapText(true);
	Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	RowList->AddChild(Text);
	return Text;
}

void UCraftingScreenWidget::AddTab(const FText& Label, ESnareBaseScreenPage TabPage)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
		FName(*FString::Printf(TEXT("CraftTab_%d"), WidgetCounter++)));
	SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::WoodTabRadius);
	UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("CraftTabText_%d"), WidgetCounter++)));
	Text->SetText(Label);
	Button->AddChild(Text);
	// 지금 쪽은 진갈색 바탕 + 밧줄 밑줄 (M101-01) — 색은 `MarkSelectedButtons` 가 입힌다.
	TabButtons.Add(Button);
	const bool bCurrentTab = TabPage == Page;
	if (bCurrentTab)
	{
		CurrentTabButton = Button;
	}
	// 밑줄은 단추 위에 겹친다 — 단추 크기(누름 72)는 그대로.
	constexpr float TabUnderlineHeight = 4.f;
	constexpr float TabUnderlineInset = 10.f;
	constexpr float TabUnderlineLift = 6.f;
	UBorder* const Underline = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("CraftTabUnderline_%d"), WidgetCounter++)));
	Underline->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiRope(), 0.f));
	USizeBox* const UnderlineBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftTabUnderlineBox_%d"), WidgetCounter++)));
	UnderlineBox->SetHeightOverride(TabUnderlineHeight);
	UnderlineBox->SetVisibility(bCurrentTab ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	UnderlineBox->AddChild(Underline);
	UOverlay* const TabOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
		FName(*FString::Printf(TEXT("CraftTabOverlay_%d"), WidgetCounter++)));
	if (UOverlaySlot* const ButtonSlot = Cast<UOverlaySlot>(TabOverlay->AddChild(Button)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UOverlaySlot* const LineSlot = Cast<UOverlaySlot>(TabOverlay->AddChild(UnderlineBox)))
	{
		LineSlot->SetHorizontalAlignment(HAlign_Fill);
		LineSlot->SetVerticalAlignment(VAlign_Bottom);
		LineSlot->SetPadding(FMargin(TabUnderlineInset, 0.f, TabUnderlineInset, TabUnderlineLift));
	}

	USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
	TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
	Proxy->OnClicked = [WeakThis, TabPage](FName)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->ShowPage(TabPage);
		}
	};
	Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
	ClickProxies.Add(Proxy);

	// 쪽 단추는 글자 폭에 줄어들지 않게 가로 최소 242 (10-16 캡처: 50 폭 세로 상자).
	USizeBox* const TabSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("CraftTabSize_%d"), WidgetCounter++)));
	TabSizeBox->SetMinDesiredWidth(SnareUiStyle::MenuTouchMin * 2.f);
	TabSizeBox->AddChild(TabOverlay);
	if (UHorizontalBoxSlot* const BoxSlot = Cast<UHorizontalBoxSlot>(TabRow->AddChild(TabSizeBox)))
	{
		BoxSlot->SetPadding(FMargin(0.f, 0.f, TabGap, 0.f));
	}
}

void UCraftingScreenWidget::SyncOutline()
{
	ASnarePlacedStructureActor* const Wanted = bOpen ? GetPaintTarget() : nullptr;
	if (OutlinedTarget.Get() == Wanted)
	{
		return;
	}
	if (OutlinedTarget.IsValid())
	{
		OutlinedTarget->SetSelectedOutline(false);
	}
	OutlinedTarget = Wanted;
	if (Wanted != nullptr)
	{
		Wanted->SetSelectedOutline(true);
	}
}

void UCraftingScreenWidget::RefreshDisplay()
{
	// 목록 틀이 없어도(시험 월드) 테두리는 고른 가구를 따라간다.
	SyncOutline();
	if (!IsValid(RowList) || !IsValid(TabRow) || WidgetTree == nullptr)
	{
		return;
	}

	RowList->ClearChildren();
	TabRow->ClearChildren();
	bPickUpShown = false;
	FurnitureStatusText = FText::GetEmpty();
	ClickProxies.Reset();
	SelectedButtons.Reset();
	TabButtons.Reset();
	CurrentTabButton = nullptr;
	GeneratorFuelLine = nullptr;

	const bool bCanBuild = CanBuildNow();
	const bool bTentTarget = Target.IsValid() && Target->IsTent();
	const bool bStorageTarget = Target.IsValid() && Target->GetPlaceableFunction() == EPlaceableFunction::Storage;
	const bool bBoatStorage = Page == ESnareBaseScreenPage::BoatStorage;
	const bool bGeneratorTarget = Target.IsValid() && Target->GetPlaceableFunction() == EPlaceableFunction::Generator;
	// 전기 가구는 제작 쪽 대신 작은 가구 쪽 — 페인트를 들고 눌러 칠하기 쪽으로 열려도 돌아갈 곳이 그 판이다.
	const bool bElectricTarget = Target.IsValid() && Target->GetPowerUse() > 0 && !bGeneratorTarget;

	// 쪽 단추 — 놓인 것마다 보이는 쪽이 다르다. 놓기·집은 지을 수 있는 땅에 선 사람만 (게스트도 된다, M15-14).
	// 배 보관함은 배 위에서 여는 판이라 그 쪽 하나뿐이다.
	if (bBoatStorage)
	{
		AddTab(LOCTEXT("CraftTabBoatStorage", "배 보관함"), ESnareBaseScreenPage::BoatStorage);
	}
	else if (bElectricTarget)
	{
		AddTab(LOCTEXT("CraftTabFurniture", "가구"), ESnareBaseScreenPage::Furniture);
	}
	else if (!bStorageTarget && !bGeneratorTarget)
	{
		AddTab(CraftPlace == ESnareCraftPlace::Fire ? LOCTEXT("CraftTabCook", "요리")
			: CraftPlace == ESnareCraftPlace::Smelter ? LOCTEXT("CraftTabSmelt", "제련") : LOCTEXT("CraftTabCraft", "제작"),
			ESnareBaseScreenPage::Craft);
	}
	if (bCanBuild && !Target.IsValid() && !bBoatStorage)
	{
		AddTab(LOCTEXT("CraftTabPlace", "놓기"), ESnareBaseScreenPage::Place);
	}
	if (bCanBuild && bTentTarget)
	{
		AddTab(LOCTEXT("CraftTabPlace", "놓기"), ESnareBaseScreenPage::Place);
		AddTab(LOCTEXT("CraftTabHouse", "집"), ESnareBaseScreenPage::House);
	}
	if (bStorageTarget)
	{
		AddTab(LOCTEXT("CraftTabStorage", "보관함"), ESnareBaseScreenPage::Storage);
	}
	if (bGeneratorTarget)
	{
		AddTab(LOCTEXT("CraftTabGenerator", "발전기"), ESnareBaseScreenPage::Generator);
	}
	if ((bCanBuild && Target.IsValid() && CraftPlace == ESnareCraftPlace::Workbench) || GetPaintTarget() != nullptr)
	{
		AddTab(LOCTEXT("CraftTabPaint", "칠하기"), ESnareBaseScreenPage::Paint);
	}

	// 배 보관함은 쪽이 하나뿐이라 쪽 단추 줄을 뺀다 — 판 위 제목 탭과 같은 「배 보관함」 이 두 번 보였다
	// (INBOX 결함 10-07 01:18 ④). 다른 판의 쪽 단추는 그대로(M38-04b 탭 72 시험).
	if (UWidget* const TabRowBox = TabRow->GetParent())
	{
		// 작은 가구 판도 같다 — 쪽이 「가구」 하나면 제목 탭(가구 이름)만 남긴다.
		TabRowBox->SetVisibility((bBoatStorage || Page == ESnareBaseScreenPage::Furniture) && TabRow->GetChildrenCount() <= 1
			? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	// 배 보관함 판은 불투명 — 배 위라 뒤 바다·배·「다음 목표」 띠가 0.95 판 너머로 비쳐 칸이 흐려 보였다
	// (INBOX 결함 10-07 02:00 ①). 다른 쪽은 공용 `ScreenPanelOpacity` 그대로.
	if (IsValid(PanelBorder))
	{
		FLinearColor PanelColor = SnareUiStyle::UiWoodPanel();
		PanelColor.A = 1.f;   // 10-16 모든 쪽 불투명
		PanelBorder->SetBrush(FSlateRoundedBoxBrush(PanelColor, SnareUiStyle::ScreenPanelRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	}

	if (IsValid(PinBox))
	{
		PinBox->SetVisibility(Page ==ESnareBaseScreenPage::Storage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(StatusText))
	{
		StatusText->SetText(FText::GetEmpty());
	}

	switch (Page)
	{
	case ESnareBaseScreenPage::Place: BuildPlaceRows(); break;
	case ESnareBaseScreenPage::House: BuildHouseRows(); break;
	case ESnareBaseScreenPage::Storage: BuildStorageRows(); break;
	case ESnareBaseScreenPage::BoatStorage: BuildBoatStorageRows(); break;
	case ESnareBaseScreenPage::Paint: BuildPaintRows(); break;
	case ESnareBaseScreenPage::Generator: BuildGeneratorRows(); break;
	case ESnareBaseScreenPage::Furniture: BuildFurnitureRows(); break;
	default: BuildCraftRows(); break;
	}

	// 가방에 넣기 — 호스트의 가구만 (100% 회수, 서버 `RemoveStructure`). 텐트는 서버가 못 치우게 한다.
	if (bCanBuild && Target.IsValid() && !Target->GetStructureId().IsEmpty())
	{
		TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
		AddButtonRow(LOCTEXT("CraftPickUp", "가방에 넣기"), true, [WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->PressPickUp();
			}
		});
		bPickUpShown = true;
	}
	else if (Page == ESnareBaseScreenPage::Furniture)
	{
		// 지을 수 없는 자리(거점존 밖)에서 연 판 — 왜 치우는 줄이 없는지 한 줄로.
		AddTextRow(LOCTEXT("FurnitureNeedLand", "집 땅에 서 있어야 가방에 넣을 수 있어요"));
	}

	// 줄은 화면이 뜬 뒤에 생긴다 — 글꼴·버튼 색을 다시 입힌다 (CodexScreenWidget 과 같은 이유).
	SnareUiStyle::ApplyUiStyle(*this);
	MarkSelectedButtons();
}

void UCraftingScreenWidget::MarkSelectedButtons()
{
	for (UButton* const Tab : TabButtons)
	{
		SnareUiStyle::StyleWoodTab(Tab, Tab == CurrentTabButton);
	}
	for (UButton* const Button : SelectedButtons)
	{
		SnareUiStyle::MarkSelectedTab(Button, true);   // 노란 바탕 + 진갈 글 + 테두리 (M99-06)
	}
}

void UCraftingScreenWidget::BuildCraftRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(CraftPlace == ESnareCraftPlace::Fire ? LOCTEXT("CraftTitleFire", "요리")
			: CraftPlace == ESnareCraftPlace::Smelter ? LOCTEXT("CraftTitleSmelter", "제련소")
			: CraftPlace == ESnareCraftPlace::Workbench ? LOCTEXT("CraftTitleBench", "작업대")
			: LOCTEXT("CraftTitleBackpack", "배낭 제작"));
	}
	HandleCookTick();

	const TArray<FSnareRecipeView> Views = BuildRecipes();
	if (Views.Num() == 0)
	{
		AddTextRow(LOCTEXT("CraftNoRecipe", "여기서 만들 수 있는 것이 없습니다"));
		return;
	}

	// 서버가 거절할 줄을 눌러 보기 전에 이유를 판 안 한 줄로 (M107-05).
	if (IsValid(StatusText) && Views.ContainsByPredicate([](const FSnareRecipeView& View) { return View.bNeedsTent; }))
	{
		StatusText->SetText(LOCTEXT("CraftNeedTentHint", "텐트가 있어야 만들 수 있어요"));
	}

	const bool bCookBusy = IsTimedPlace() && IsValid(Base) && Base->FindCookJob(FireKey()) != nullptr;
	int32 RowIndex = 0;
	for (const FSnareRecipeView& View : Views)
	{
		TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
		const FName ResultItemID = View.ResultItemID;
		AddRecipeRow(View, RowIndex++, View.CanCraft() && !bCookBusy, [WeakThis, ResultItemID]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->PressRecipe(ResultItemID);
			}
		});
	}
}

void UCraftingScreenWidget::BuildPlaceRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("CraftTitlePlace", "놓기"));
	}

	const FSnarePlayerSnapshot* const Snapshot = IsValid(WatchedBackend) ? &WatchedBackend->GetPlayerSnapshot() : nullptr;
	int32 Rows = 0;
	if (Snapshot != nullptr && IsValid(Base))
	{
		for (const FInventorySlot& InvSlot : Snapshot->InventorySlots)
		{
			const FItemRow* const Row = Base->FindItemRow(InvSlot.ItemID);
			// 배는 물 위에 띄운다 (DESIGN 8.9) — 땅 격자에 안 놓인다.
			if (Row == nullptr || Row->ItemType != EItemType::Placeable || Row->FootprintX <= 0
				|| Row->PlaceableFunction == EPlaceableFunction::Boat || InvSlot.Count <= 0)
			{
				continue;
			}

			TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
			const FName ItemID = InvSlot.ItemID;
			AddButtonRow(FText::Format(LOCTEXT("PlaceRow", "{0} x{1} - {2}x{3}칸"), Row->DisplayName, InvSlot.Count,
				Row->FootprintX, Row->FootprintY), true, [WeakThis, ItemID]()
			{
				if (WeakThis.IsValid() && WeakThis->BuildOwner.IsValid() && WeakThis->BuildOwner->BeginPlacing(ItemID))
				{
					WeakThis->Close();
				}
			});
			++Rows;
		}
	}
	if (Rows == 0)
	{
		AddTextRow(LOCTEXT("PlaceNone", "가방에 놓을 것이 없습니다"));
	}
}

void UCraftingScreenWidget::BuildHouseRows()
{
	const FSnareTentMirror* const Tent = IsValid(Base) && Target.IsValid() ? Base->FindTent(Target->GetTentId()) : nullptr;
	const FBaseRuleRow* const Rules = IsValid(Base) ? Base->GetRules() : nullptr;
	if (IsValid(TitleText))
	{
		TitleText->SetText(Tent != nullptr && Tent->bUpgraded ? LOCTEXT("HouseTitleHouse", "집") : LOCTEXT("HouseTitleTent", "텐트"));
	}
	if (Tent == nullptr || Rules == nullptr)
	{
		AddTextRow(LOCTEXT("HouseUnknown", "이 텐트의 기록을 아직 받지 못했습니다"));
		return;
	}

	TWeakObjectPtr<USnareBaseSubsystem> WeakBase(Base);
	TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
	const FString TentId = Tent->TentId;

	if (!Tent->bUpgraded)
	{
		AddTextRow(FormatDeadlineText(*Tent));
		// 색 고르기 — 고른 색은 강조 바탕 (M24-02).
		const int32 ColorCount = FMath::Min(Rules->HouseColorCount, 5);
		for (int32 Index = 0; Index < ColorCount; ++Index)
		{
			UButton* const ColorButton = AddButtonRow(HouseColorName(Index), true, [WeakThis, Index]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->ChosenColor = Index;
					WeakThis->RefreshDisplay();
				}
			});
			if (Index == ChosenColor)
			{
				SelectedButtons.Add(ColorButton);
			}
		}
		const int32 Color = ChosenColor;
		AddButtonRow(FText::Format(LOCTEXT("HouseUpgrade", "집 올리기 - 나무 {0}, 향나무 {1}"),
			Rules->UpgradeWoodCount, Rules->UpgradeCedarCount), true, [WeakBase, TentId, Color]()
		{
			if (WeakBase.IsValid())
			{
				WeakBase->RequestUpgradeHouse(TentId, Color);
			}
		});
		return;
	}

	AddTextRow(FormatFloorText(*Tent, *Rules));
	if (Tent->Floors < 2 && Rules->HasFloor2Rules())
	{
		// 재료 확인·차감은 서버 몫 — 모자라면 거절 글이 뜬다 (1층 「집 올리기」 와 같은 길).
		TArray<FText> CostParts;
		for (const TPair<FName, int32>& Pair : Rules->Floor2Cost)
		{
			CostParts.Add(FText::Format(LOCTEXT("HouseFloor2Part", "{0} {1}"), ItemName(Pair.Key), Pair.Value));
		}
		AddButtonRow(FText::Format(LOCTEXT("HouseFloor2", "2층 올리기 - {0}"), FText::Join(FText::FromString(TEXT(", ")), CostParts)),
			true, [WeakBase, TentId]()
		{
			if (WeakBase.IsValid())
			{
				WeakBase->RequestUpgradeHouse(TentId, 0, 2);
			}
		});
	}
	AddTextRow(FormatUpkeepText(*Tent));
	for (const int32 Count : { 1, 10 })
	{
		AddButtonRow(FText::Format(LOCTEXT("HouseFeed", "유지비 나무 {0}개 (+{1}시간)"), Count,
			Count * Rules->UpkeepHoursPerWood), true, [WeakBase, TentId, Count]()
		{
			if (WeakBase.IsValid())
			{
				WeakBase->RequestFeedUpkeep(TentId, Count);
			}
		});
	}
	AddTextRow(FText::Format(LOCTEXT("HouseFeedMax", "유지비는 최대 {0}시간"), Rules->UpkeepMaxHours));
}

void UCraftingScreenWidget::BuildGeneratorRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("GeneratorTitle", "발전기"));
	}
	const FBaseRuleRow* const Rules = IsValid(Base) ? Base->GetRules() : nullptr;
	if (!Target.IsValid() || Rules == nullptr || !Rules->HasGeneratorRules())
	{
		AddTextRow(LOCTEXT("GeneratorUnknown", "발전기 규칙을 읽지 못했습니다"));
		return;
	}

	// 남은 초는 놓인 액터의 복제 끝 시각 — 서버 `fuelUntilUtc` 를 서버 시계로 옮긴 값이라 남의 발전기도 같은 답 (M108-07c).
	const double SecondsLeft = Target->GetGeneratorFuelSecondsLeft();
	GeneratorFuelLine = AddTextRow(FormatGeneratorFuelText(SecondsLeft));

	int32 Supply = 0;
	int32 Use = 0;
	USnarePowerSubsystem::MeasureLand(GetWorld(), *Rules, Target->GetOwnerPlayFabId(), Target->GetTentId(), Supply, Use);
	AddTextRow(FormatLandPowerText(Supply, Use));
	if (Use > Supply)
	{
		// 8.11 한 줄 판정 — 모자라면 하나씩이 아니라 전부 꺼진다.
		UTextBlock* const Short = AddTextRow(LOCTEXT("GeneratorShort", "전력이 모자라 전부 꺼집니다"));
		Short->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiWarnOnWood()));
	}

	// 넣기 단추는 내 발전기만 — 서버도 자기 `base` 만 읽어 남의 것은 `not_found` 다.
	const FString TentId = Target->GetTentId();
	const FString StructureId = Target->GetStructureId();
	const bool bOwn = Base->IsOwnFurniture(TentId, StructureId);
	if (!bOwn)
	{
		AddTextRow(LOCTEXT("GeneratorNotOwn", "남의 발전기에는 연료를 넣을 수 없습니다"));
	}
	else
	{
		// 짧게 타는 연료부터 (나무 → 석탄 덩어리) — 표 맵 순서에 기대지 않는다.
		TArray<TPair<FName, int32>> Fuels = Rules->GeneratorFuelMinutes.Array();
		Fuels.Sort([](const TPair<FName, int32>& A, const TPair<FName, int32>& B)
		{
			return A.Value != B.Value ? A.Value < B.Value : A.Key.LexicalLess(B.Key);
		});
		TWeakObjectPtr<USnareBaseSubsystem> WeakBase(Base);
		for (const TPair<FName, int32>& Fuel : Fuels)
		{
			const FName FuelID = Fuel.Key;
			const bool bEnabled = CanFeedGenerator(bOwn, SecondsLeft, Fuel.Value, Rules->GeneratorFuelMaxHours);
			for (const int32 Count : { 1, 10 })
			{
				AddButtonRow(FText::Format(LOCTEXT("GeneratorFeed", "{0} {1}개 (+{2}분)"), ItemName(FuelID), Count, Count * Fuel.Value),
					bEnabled, [WeakBase, TentId, StructureId, FuelID, Count]()
				{
					if (WeakBase.IsValid())
					{
						WeakBase->RequestFeedGenerator(TentId, StructureId, FuelID, Count);
					}
				});
			}
		}
	}
	AddTextRow(FText::Format(LOCTEXT("GeneratorMax", "연료는 최대 {0}시간"), Rules->GeneratorFuelMaxHours));
	AddTextRow(LOCTEXT("GeneratorRemoveLoses", "치우면 남은 연료는 사라집니다"));
}

void UCraftingScreenWidget::BuildFurnitureRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(Target.IsValid() ? ItemName(Target->GetItemID()) : LOCTEXT("FurnitureTitle", "가구"));
	}
	bool bOn = false;
	FurnitureStatusText = MeasureElectricStatus(Target.Get(), bOn);
	if (!FurnitureStatusText.IsEmpty())
	{
		// 알림과 같은 글·같은 숫자 — 꺼짐은 판 위 경고 색(알림의 붉은 알약 자리).
		UTextBlock* const Line = AddTextRow(FurnitureStatusText);
		Line->SetColorAndOpacity(FSlateColor(bOn ? SnareUiStyle::UiTextOnWood() : SnareUiStyle::UiWarnOnWood()));
	}
}

void UCraftingScreenWidget::BuildPaintRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("PaintTitle", "칠하기"));
	}
	const FSnareTentMirror* const Tent = IsValid(Base) && Target.IsValid() ? Base->FindTent(Target->GetTentId()) : nullptr;
	if (Tent == nullptr)
	{
		AddTextRow(LOCTEXT("PaintUnknown", "이 땅의 기록을 아직 받지 못했습니다"));
		return;
	}

	// 가방 속 페인트 (서버 스냅샷 그대로). 고른 것은 강조 바탕 (M24-02).
	const TMap<FName, int32> Paints = CollectPaints();
	if (Paints.Num() == 0)
	{
		AddTextRow(LOCTEXT("PaintNone", "페인트가 없습니다 (잡화점)"));
		return;
	}
	if (!Paints.Contains(ChosenPaint))
	{
		ChosenPaint = Paints.CreateConstIterator()->Key;
	}
	TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
	for (const TPair<FName, int32>& Pair : Paints)
	{
		const FName PaintID = Pair.Key;
		const FText Label = FText::Format(LOCTEXT("PaintCan", "{0} x{1}"), ItemName(PaintID), Pair.Value);
		UButton* const PaintButton = AddButtonRow(Label, true, [WeakThis, PaintID]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ChosenPaint = PaintID;
				WeakThis->RefreshDisplay();
			}
		});
		if (PaintID == ChosenPaint)
		{
			SelectedButtons.Add(PaintButton);
		}
	}

	// 고른 가구 하나 — 제목에 이름·지금 색을 띄우고 「칠하기」 한 번 (DESIGN 13.8 🟡).
	const FName Paint = ChosenPaint;
	if (const ASnarePlacedStructureActor* const Picked = GetPaintTarget())
	{
		const FSnareStructureMirror* const Structure = Base->FindStructure(Picked->GetStructureId());
		const FName NowColor = Structure != nullptr ? Structure->Color : NAME_None;
		if (IsValid(TitleText))
		{
			TitleText->SetText(FText::Format(LOCTEXT("PaintPickedTitle", "칠하기 - {0} ({1})"), ItemName(Picked->GetItemID()),
				NowColor.IsNone() ? LOCTEXT("PaintBare", "안 칠함") : ItemName(NowColor)));
		}
		AddButtonRow(LOCTEXT("PaintPress", "칠하기"), NowColor != Paint, [WeakThis, Paint]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->PressPaint(Paint);
			}
		});
		return;
	}

	// 이 땅의 가구 — 누르면 서버 `PaintStructure`. 색은 응답 뒤 사본·액터에만 입는다.
	TWeakObjectPtr<USnareBaseSubsystem> WeakBase(Base);
	const FString TentId = Tent->TentId;
	for (const FSnareStructureMirror& Structure : Tent->Structures)
	{
		const FString StructureId = Structure.StructureId;
		const FText Now = Structure.Color.IsNone() ? LOCTEXT("PaintBare", "안 칠함") : ItemName(Structure.Color);
		AddButtonRow(FText::Format(LOCTEXT("PaintRow", "칠하기 - {0} ({1})"), ItemName(Structure.ItemID), Now),
			Structure.Color != Paint, [WeakBase, TentId, StructureId, Paint]()
		{
			if (WeakBase.IsValid())
			{
				WeakBase->RequestPaintStructure(TentId, StructureId, Paint);
			}
		});
	}
	if (Tent->Structures.Num() == 0)
	{
		AddTextRow(LOCTEXT("PaintNoFurniture", "칠할 가구가 없습니다"));
	}
}

void UCraftingScreenWidget::BuildStorageRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("StorageTitle", "보관함"));
	}
	if (!Target.IsValid() || !IsValid(Base))
	{
		return;
	}

	// 남의 보관함이면 주인 ID 를 싣는다 (서버 `ownerId`). 내 것이면 비운다.
	const FString MyId = IsValid(WatchedBackend) ? WatchedBackend->GetPlayFabId() : FString();
	const FString OwnerId = Target->GetOwnerPlayFabId() == MyId ? FString() : Target->GetOwnerPlayFabId();
	const FString TentId = Target->GetTentId();
	const FString StructureId = Target->GetStructureId();
	TWeakObjectPtr<USnareBaseSubsystem> WeakBase(Base);
	TWeakObjectPtr<UEditableTextBox> WeakPin(PinBox);

	AddTextRow(LOCTEXT("StorageInside", "보관함 안"));
	const FSnareStructureMirror* const Mirror = Base->FindStructure(StructureId);
	if (Mirror == nullptr)
	{
		AddTextRow(LOCTEXT("StorageUnknown", "넣거나 꺼내면 내용이 보입니다"));
	}
	else if (Mirror->Storage.Num() == 0)
	{
		AddTextRow(LOCTEXT("StorageEmpty", "비어 있습니다"));
	}
	else
	{
		for (const TPair<FName, int32>& Pair : Mirror->Storage)
		{
			const FName ItemID = Pair.Key;
			const int32 Count = Pair.Value;
			AddButtonRow(FText::Format(LOCTEXT("StorageTake", "꺼내기 - {0} x{1}"), ItemName(ItemID), Count), true,
				[WeakBase, WeakPin, TentId, StructureId, ItemID, Count, OwnerId]()
			{
				if (WeakBase.IsValid())
				{
					WeakBase->RequestMoveFromStorage(TentId, StructureId, ItemID, Count, OwnerId,
						WeakPin.IsValid() ? WeakPin->GetText().ToString() : FString());
				}
			});
		}
	}

	AddTextRow(LOCTEXT("StorageBackpack", "내 가방"));
	if (IsValid(WatchedBackend))
	{
		for (const FInventorySlot& InvSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
		{
			if (InvSlot.Count <= 0)
			{
				continue;
			}
			const FName ItemID = InvSlot.ItemID;
			const int32 Count = InvSlot.Count;
			AddButtonRow(FText::Format(LOCTEXT("StoragePut", "넣기 - {0} x{1}"), ItemName(ItemID), Count), true,
				[WeakBase, WeakPin, TentId, StructureId, ItemID, Count, OwnerId]()
			{
				if (WeakBase.IsValid())
				{
					WeakBase->RequestMoveToStorage(TentId, StructureId, ItemID, Count, OwnerId,
						WeakPin.IsValid() ? WeakPin->GetText().ToString() : FString());
				}
			});
		}
	}
}

void UCraftingScreenWidget::BuildBoatStorageRows()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("BoatStorageTitle", "배 보관함"));
	}
	if (!IsValid(Base))
	{
		return;
	}

	// 열쇠는 보트 컨테이너 — 없으면 서버가 `locked` 로 막는다. 누르기 전에 까닭부터 보인다.
	const FName Container = USnareBaseSubsystem::GetBoatContainerItemID();
	const TArray<FInventorySlot>* const Bag = IsValid(WatchedBackend) ? &WatchedBackend->GetPlayerSnapshot().InventorySlots : nullptr;
	if (Bag == nullptr || SnareCraftRules::CountItem(*Bag, Container) < 1)
	{
		BoatPickedItem = NAME_None;
		AddTextRow(LOCTEXT("BoatStorageNoContainer", "배 짐칸이 있어야 배 보관함을 씁니다"));
		return;
	}

	// 칸 짜임 (INBOX 지시 10-07 01:25) — 위 = 고른 아이템 설명 띠, 아래 왼쪽 = 내 가방 칸, 아래 오른쪽 = 배 보관함 칸.
	// 칸 한 번 = 고르기, 띠의 버튼 = 옮기기. 칸은 서버 스냅샷·사본 그대로라 응답 뒤에만 바뀐다 (CLAUDE.md 8).
	TArray<FInventorySlot> BagItems;
	for (const FInventorySlot& InvSlot : *Bag)
	{
		if (InvSlot.Count > 0)
		{
			BagItems.Add(InvSlot);
		}
	}
	TArray<FInventorySlot> StoreItems;
	for (const TPair<FName, int32>& Pair : Base->GetBoatStorage())
	{
		FInventorySlot Entry;
		Entry.ItemID = Pair.Key;
		Entry.Count = Pair.Value;
		StoreItems.Add(Entry);
	}

	// 고른 것이 응답 뒤 그쪽에서 사라졌으면(다 옮김) 고르기를 푼다. 보트 컨테이너는 고를 수 없다.
	const TArray<FInventorySlot>& PickedSide = bBoatPickedFromStore ? StoreItems : BagItems;
	const FInventorySlot* const Picked = BoatPickedItem.IsNone() || (!bBoatPickedFromStore && BoatPickedItem == Container) ? nullptr
		: PickedSide.FindByPredicate([this](const FInventorySlot& Entry) { return Entry.ItemID == BoatPickedItem; });
	if (Picked == nullptr)
	{
		BoatPickedItem = NAME_None;
	}

	// ── ⓐ 위 띠: 큰 아이콘 · 이름 · 설명 · 「넣기」/「꺼내기」 ──
	UBorder* const Detail = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BoatDetailBand"));
	Detail->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonBase(), SnareUiStyle::HudCornerRadius));
	Detail->SetPadding(FMargin(RowInnerPadding));
	UHorizontalBox* const DetailRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BoatDetailRow"));
	Detail->SetContent(DetailRow);
	if (UScrollBoxSlot* const DetailSlot = Cast<UScrollBoxSlot>(RowList->AddChild(Detail)))
	{
		DetailSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BoatSectionGap));
	}
	if (Picked != nullptr)
	{
		if (UHorizontalBoxSlot* const DiscSlot = Cast<UHorizontalBoxSlot>(
			DetailRow->AddChild(MakeItemDisc(Picked->ItemID, BoatDetailDiscSize, TEXT("BoatDetail"), FString()))))
		{
			DiscSlot->SetVerticalAlignment(VAlign_Center);
			DiscSlot->SetPadding(FMargin(0.f, 0.f, RowInnerPadding, 0.f));
		}
	}
	UVerticalBox* const DetailColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BoatDetailColumn"));
	if (UHorizontalBoxSlot* const ColumnSlot = Cast<UHorizontalBoxSlot>(DetailRow->AddChild(DetailColumn)))
	{
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}
	auto AddDetailLine = [this, DetailColumn](const TCHAR* Name, const FText& Label, int32 FontSize)
	{
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetText(Label);
		Text->SetAutoWrapText(true);
		Text->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		DetailColumn->AddChildToVerticalBox(Text);
	};
	if (Picked == nullptr)
	{
		AddDetailLine(TEXT("BoatDetailName"), LOCTEXT("BoatPickHint", "칸을 눌러 아이템을 고르세요"), BoatDetailNameFont);
	}
	else
	{
		AddDetailLine(TEXT("BoatDetailName"), ItemName(Picked->ItemID), BoatDetailNameFont);
		AddDetailLine(TEXT("BoatDetailInfo"), FText::Format(bBoatPickedFromStore
			? LOCTEXT("BoatDetailInfoStore", "배 보관함 - {0}개") : LOCTEXT("BoatDetailInfoBag", "내 가방 - {0}개"),
			FText::AsNumber(Picked->Count)), BoatDetailInfoFont);
		AddDetailLine(TEXT("BoatDetailDesc"), bBoatPickedFromStore
			? LOCTEXT("BoatDetailDescStore", "꺼내기를 누르면 전부 가방으로 옮깁니다")
			: LOCTEXT("BoatDetailDescBag", "넣기를 누르면 전부 배 보관함으로 옮깁니다"), BoatDetailInfoFont);

		UButton* const MoveButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BoatMoveButton"));
		SnareUiStyle::MakeRoundButton(*MoveButton, SnareUiStyle::HudCornerRadius);
		UTextBlock* const MoveText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BoatMoveText"));
		MoveText->SetText(bBoatPickedFromStore ? LOCTEXT("BoatTake", "꺼내기") : LOCTEXT("BoatPut", "넣기"));
		MoveButton->AddChild(MoveText);
		// 넣기는 칸이 다 찼어도 누를 수 있다 — 같은 아이템 더하기·거절 글은 서버 몫(`full`).
		TWeakObjectPtr<USnareBaseSubsystem> WeakBase(Base);
		const FName ItemID = Picked->ItemID;
		const int32 Count = Picked->Count;
		const bool bFromStore = bBoatPickedFromStore;
		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->OnClicked = [WeakBase, ItemID, Count, bFromStore](FName)
		{
			if (!WeakBase.IsValid())
			{
				return;
			}
			if (bFromStore)
			{
				WeakBase->RequestMoveFromBoatStorage(ItemID, Count);
			}
			else
			{
				WeakBase->RequestMoveToBoatStorage(ItemID, Count);
			}
		};
		MoveButton->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		ClickProxies.Add(Proxy);
		USizeBox* const MoveBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BoatMoveButtonBox"));
		MoveBox->SetMinDesiredWidth(BoatMoveButtonWidth);
		MoveBox->SetMinDesiredHeight(SnareUiStyle::ScreenCloseButtonHeight);
		MoveBox->AddChild(MoveButton);
		if (UHorizontalBoxSlot* const MoveSlot = Cast<UHorizontalBoxSlot>(DetailRow->AddChild(MoveBox)))
		{
			MoveSlot->SetVerticalAlignment(VAlign_Center);
			MoveSlot->SetPadding(FMargin(RowInnerPadding, 0.f, 0.f, 0.f));
		}
	}

	// ── ⓑⓒ 아래: 왼쪽 내 가방 칸 · 오른쪽 배 보관함 칸 ──
	UHorizontalBox* const Grids = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BoatGridRow"));
	RowList->AddChild(Grids);
	auto AddSide = [this, Grids](const TCHAR* Name, const FText& Head, bool bFill) -> UUniformGridPanel*
	{
		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sColumn"), Name)));
		if (UHorizontalBoxSlot* const ColumnSlot = Cast<UHorizontalBoxSlot>(Grids->AddChild(Column)))
		{
			ColumnSlot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
			ColumnSlot->SetPadding(FMargin(bFill ? 0.f : BoatSectionGap, 0.f, 0.f, 0.f));
		}
		UTextBlock* const HeadText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%sHead"), Name)));
		HeadText->SetText(Head);
		HeadText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
		if (UVerticalBoxSlot* const HeadSlot = Column->AddChildToVerticalBox(HeadText))
		{
			HeadSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
		UUniformGridPanel* const Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(),
			FName(*FString::Printf(TEXT("%sGrid"), Name)));
		Grid->SetSlotPadding(FMargin(BoatCellGap * 0.5f));
		Grid->SetMinDesiredSlotWidth(BoatCellSize);
		Grid->SetMinDesiredSlotHeight(BoatCellSize);
		if (UVerticalBoxSlot* const GridSlot = Column->AddChildToVerticalBox(Grid))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Left);
		}
		return Grid;
	};

	UUniformGridPanel* const BagGrid = AddSide(TEXT("BoatBag"), LOCTEXT("StorageBackpack", "내 가방"), true);
	// 가방도 빈 칸까지 7×n 격자 — 가진 칸만 그리면 한 줄뿐이라 판 아래 절반이 텅 비었다 (INBOX 결함 10-07 02:00 ④).
	// 칸 수 = 배낭 화면과 같은 `ComputeCapacity`(기본 칸 + 멘 배낭), 넘친 가방이면 가진 만큼, 줄 끝까지 채움.
	const int32 BagCapacity = UBackpackScreenWidget::ComputeCapacity(*Bag, Base->GetItemTable(),
		GetDefault<UInventoryComponent>()->GetSlotCount());
	const int32 BagCells = FMath::DivideAndRoundUp(FMath::Max3(BagCapacity, BagItems.Num(), 1), BoatBagColumns) * BoatBagColumns;
	for (int32 Index = 0; Index < BagCells; ++Index)
	{
		// 보트 컨테이너 자신은 흐리게·못 고름 — 넣으면 열쇠가 보관함 안에 갇힌다 (서버도 `self` 로 막는다).
		const bool bHasItem = BagItems.IsValidIndex(Index);
		AddBoatCell(BagGrid, Index, BoatBagColumns, bHasItem ? BagItems[Index] : FInventorySlot(), /*bFromStore=*/false,
			bHasItem && BagItems[Index].ItemID != Container);
	}

	const int32 MaxSlots = Base->GetBoatStorageSlots();
	const FText StoreHead = Base->HasBoatStorageMirror()
		? FText::Format(LOCTEXT("BoatStoreHead", "배 보관함 {0}/{1}"), StoreItems.Num(), MaxSlots)
		: FText::Format(LOCTEXT("BoatStoreHeadUnknown", "배 보관함 ?/{0}"), MaxSlots);
	UUniformGridPanel* const StoreGrid = AddSide(TEXT("BoatStore"), StoreHead, false);
	// 빈 칸도 칸 모양으로 — 칸 수는 표 `BonusSlots`. 서버가 더 많이 줬으면(표가 줄었음) 있는 만큼 다 보인다.
	for (int32 Index = 0; Index < FMath::Max(MaxSlots, StoreItems.Num()); ++Index)
	{
		AddBoatCell(StoreGrid, Index, BoatStoreColumns, StoreItems.IsValidIndex(Index) ? StoreItems[Index] : FInventorySlot(),
			/*bFromStore=*/true, StoreItems.IsValidIndex(Index));
	}
	if (!Base->HasBoatStorageMirror())
	{
		AddTextRow(LOCTEXT("StorageUnknown", "넣거나 꺼내면 내용이 보입니다"));
	}
}

void UCraftingScreenWidget::AddBoatCell(UUniformGridPanel* Grid, int32 Index, int32 Columns, const FInventorySlot& Entry,
	bool bFromStore, bool bEnabled)
{
	const FString Suffix = FString::Printf(TEXT("_%d"), Index);
	const TCHAR* const Prefix = bFromStore ? TEXT("BoatStoreCell") : TEXT("BoatBagCell");
	UWidget* Cell = nullptr;
	if (Entry.ItemID.IsNone())
	{
		// 빈 칸 — 누를 것 없음, 칸 모양만. 색은 판 크림보다 한 단계 어두운 `UiButtonPressed` — `UiEmptySlot`(#E9E0C8)은
		// 판(#F5EBD2)과 거의 같아 칸이 안 보였다 (INBOX 결함 10-07 02:00 ④).
		UBorder* const Empty = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			FName(*FString::Printf(TEXT("%sEmpty%s"), Prefix, *Suffix)));
		Empty->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonPressed(), BoatCellRadius));
		Cell = Empty;
	}
	else
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("%s%s"), Prefix, *Suffix)));
		SnareUiStyle::MakeRoundButton(*Button, BoatCellRadius);
		Button->SetIsEnabled(bEnabled);
		if (!bEnabled)
		{
			Button->SetRenderOpacity(BoatLockedCellOpacity);
		}
		UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
			FName(*FString::Printf(TEXT("%sStack%s"), Prefix, *Suffix)));
		if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(
			MakeItemDisc(Entry.ItemID, BoatCellDiscSize, Prefix, Suffix)))
		{
			DiscSlot->SetHorizontalAlignment(HAlign_Center);
			DiscSlot->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* const CountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%sCount%s"), Prefix, *Suffix)));
		CountText->SetText(FText::AsNumber(Entry.Count));
		CountText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
		FSlateFontInfo Font = CountText->GetFont();
		Font.Size = BoatCellCountFont;
		CountText->SetFont(Font);
		if (UOverlaySlot* const CountSlot = Stack->AddChildToOverlay(CountText))
		{
			CountSlot->SetHorizontalAlignment(HAlign_Right);
			CountSlot->SetVerticalAlignment(VAlign_Bottom);
		}
		if (UButtonSlot* const StackSlot = Cast<UButtonSlot>(Button->SetContent(Stack)))
		{
			StackSlot->SetPadding(FMargin(0.f));
			StackSlot->SetHorizontalAlignment(HAlign_Fill);
			StackSlot->SetVerticalAlignment(VAlign_Fill);
		}

		// 칸 한 번 = 고르기 (테두리 대신 고른 탭과 같은 강조 바탕 — `MarkSelectedButtons`). 서버에는 안 묻는다.
		TWeakObjectPtr<UCraftingScreenWidget> WeakThis(this);
		const FName ItemID = Entry.ItemID;
		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->OnClicked = [WeakThis, ItemID, bFromStore](FName)
		{
			if (WeakThis.IsValid())
			{
				WeakThis->PickBoatCell(ItemID, bFromStore);
			}
		};
		Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
		ClickProxies.Add(Proxy);
		if (ItemID == BoatPickedItem && bFromStore == bBoatPickedFromStore)
		{
			SelectedButtons.Add(Button);
		}
		Cell = Button;
	}

	USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("%sBox%s"), Prefix, *Suffix)));
	Box->SetWidthOverride(BoatCellSize);
	Box->SetHeightOverride(BoatCellSize);
	Box->AddChild(Cell);
	// 격자 칸 기본 정렬이 왼쪽 위라 채우기로 둔다 (배낭 칸과 같은 함정).
	if (UUniformGridSlot* const CellSlot = Grid->AddChildToUniformGrid(Box, Index / Columns, Index % Columns))
	{
		CellSlot->SetHorizontalAlignment(HAlign_Fill);
		CellSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

void UCraftingScreenWidget::PickBoatCell(FName ItemID, bool bFromStore)
{
	BoatPickedItem = ItemID;
	bBoatPickedFromStore = bFromStore;
	RefreshDisplay();
}

TSharedRef<SWidget> UCraftingScreenWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}

	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	MarkSelectedButtons();
	return Built;
}

void UCraftingScreenWidget::NativeDestruct()
{
	if (IsValid(Base))
	{
		Base->OnBaseChanged.Remove(BaseChangedHandle);
		Base->OnBaseMessage.Remove(BaseMessageHandle);
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UCraftingScreenWidget::HandleBackendSnapshotChanged);
	}
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CookTimerHandle);
	}
	if (OutlinedTarget.IsValid())
	{
		OutlinedTarget->SetSelectedOutline(false);
	}
	Super::NativeDestruct();
}

bool UCraftingScreenWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}
	const UPanelWidget* const RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return WidgetTree->RootWidget == nullptr || (RootPanel != nullptr && RootPanel->GetChildrenCount() == 0);
}

void UCraftingScreenWidget::BuildPlaceholderTree()
{
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CraftScreenRoot"));
		WidgetTree->RootWidget = Root;
	}

	// 가운데 판 한 장 — 쪽 단추 / 상태 / 목록 / PIN / 안내 / 닫기 순으로 위에서 아래로 읽힌다. 제목은 판 위로 걸친 탭.
	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CraftPanel"));
	// 판 = 배낭 패널 크림 (M23-04, 시트 `inventory_backpack.md` 「패널 크림 #F5EBD2 … 모서리 16px」). 그래서 판 위 글자는
	// 진갈색이다. 모서리는 M27-01 — 도감·업적 판과 같은 `ScreenPanelRadius`.
	FLinearColor PanelColor = SnareUiStyle::UiWoodPanel();
	PanelColor.A = 1.f;   // 10-16 「반투명 판 너머 풀밭이 글 사이로 비친다」 — 불투명
	Panel->SetBrush(FSlateRoundedBoxBrush(PanelColor, SnareUiStyle::ScreenPanelRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	PanelBorder = Panel;
	// 위 여백 = 판 안으로 내려온 제목 탭 높이만큼 더 — 첫 줄(쪽 단추)이 탭에 가리지 않게.
	Panel->SetPadding(FMargin(PanelPadding, PanelPadding + TitlePillHeight - SnareUiStyle::MenuTitlePillRise,
		PanelPadding, PanelPadding));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Panel)))
	{
		// 위 변을 고정하고 높이는 내용만큼(10-16 「빈 판이 화면 끝까지」) — 최대는 옛 고정 높이, 넘치면 목록이 스크롤.
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.f));
		PanelSlot->SetPosition(FVector2D(0.f, -PanelHeight * 0.5f));
		PanelSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
		PanelSlot->SetAutoSize(true);
	}

	USizeBox* const PanelFit = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CraftPanelFit"));
	PanelFit->SetWidthOverride(PanelWidth - PanelPadding * 2.f);
	PanelFit->SetMaxDesiredHeight(PanelHeight - PanelPadding * 2.f - (TitlePillHeight - SnareUiStyle::MenuTitlePillRise));
	Panel->SetContent(PanelFit);
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CraftColumn"));
	PanelFit->AddChild(Column);

	auto AddToColumn = [Column](UWidget* Child, bool bFill)
	{
		if (UVerticalBoxSlot* const BoxSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Child)))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
			if (bFill)
			{
				BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
	};

	TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CraftTabs"));
	USizeBox* const TabRowBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CraftTabsBox"));
	TabRowBox->SetMinDesiredHeight(TabRowHeight);
	TabRowBox->AddChild(TabRow);
	AddToColumn(TabRowBox, false);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CraftStatus"));
	StatusText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnWood()));
	AddToColumn(StatusText, false);

	RowList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CraftRows"));
	AddToColumn(RowList, true);

	PinBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("CraftPin"));
	PinBox->SetHintText(LOCTEXT("StoragePinHint", "PIN 숫자 5자리 (남의 보관함)"));
	PinBox->KeyboardType = EVirtualKeyboardType::Number;
	PinBox->SetVisibility(ESlateVisibility::Collapsed);
	AddToColumn(PinBox, false);

	ToastText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CraftToast"));
	ToastText->SetAutoWrapText(true);
	AddToColumn(ToastText, false);

	BottomRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CraftBottom"));
	AddToColumn(BottomRow, false);

	UButton* Close = nullptr;
	USizeBox* const CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("CraftCloseBox"), TEXT("CraftClose"),
		LOCTEXT("CraftClose", "닫기"), Close);
	CloseButton = Close;
	CloseButton->OnClicked.AddDynamic(this, &UCraftingScreenWidget::HandleCloseClicked);
	BottomRow->AddChild(CloseBox);

	// 제목 탭 — 판 왼쪽 위 모서리에 걸쳐 위로 튀어나온다(판 **뒤에** 넣어야 위에 그려진다). 배낭 판과 같은 진갈색·흰 글자.
	TitlePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CraftTitlePill"));
	TitlePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodButton(), TitlePillHeight * 0.5f, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	TitlePill->SetHorizontalAlignment(HAlign_Center);
	TitlePill->SetVerticalAlignment(VAlign_Center);
	TitlePill->SetPadding(FMargin(TitlePillPaddingX, 0.f));
	TitlePill->SetVisibility(ESlateVisibility::HitTestInvisible);
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CraftTitle"));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TitleText->SetJustification(ETextJustify::Center);
	SnareUiStyle::SetFontSize(*TitleText, TitlePillFontSize);
	TitlePill->SetContent(TitleText);
	USizeBox* const PillBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CraftTitlePillBox"));
	PillBox->SetMinDesiredWidth(TitlePillMinWidth);
	PillBox->SetHeightOverride(TitlePillHeight);
	PillBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	PillBox->AddChild(TitlePill);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(PillBox)))
	{
		PillSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		PillSlot->SetAlignment(FVector2D(0.f, 0.f));
		PillSlot->SetPosition(FVector2D(-PanelWidth * 0.5f, -PanelHeight * 0.5f - SnareUiStyle::MenuTitlePillRise));
		PillSlot->SetAutoSize(true);
	}
}

#undef LOCTEXT_NAMESPACE
