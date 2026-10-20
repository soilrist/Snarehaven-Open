#include "UI/BackpackScreenWidget.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/OverlaySlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/GachaPoolRow.h"
#include "Data/ItemRowLookup.h"
#include "Data/RecipeRow.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Equipment/EquipmentComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/BackpackComponent.h"
#include "Misc/CommandLine.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/GachaScreenWidget.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/SnareGuideBookWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnareBalanceHud.h"
#include "UI/CrateScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareStoreSettings.h"
#include "UI/WardrobeShopWidget.h"
#include "UI/WardrobeWidget.h"
#include "World/WaterTravelComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 화면 배치 — 시트 `docs/reference/inventory_backpack.md`(1920×1080) 그대로. 게임 규칙 숫자가 아니다.
	constexpr float PanelRightOffset = 325.f;   // 패널 오른쪽 끝 x 1595
	constexpr float PanelTop = 320.f;
	constexpr float PanelWidth = 312.f;
	constexpr float PanelRadius = 16.f;
	constexpr float PillWidth = 130.f;
	constexpr float PillHeight = 50.f;
	constexpr float PillRise = 20.f;           // 패널보다 위로 튀어나온 만큼
	// 10-16 탭 52 → 82: 테(3) 안 버튼 76 × 판 배율 1.6 = 121.6 — 애플 44pt(121, DESIGN 2). 시트 52 보다 크다.
	constexpr float TabSize = 82.f;
	constexpr float TabGap = 6.f;              // 탭 사이 — 다섯 칸이 판 옆(320~760)에 들게 칸 틈(12)보다 좁게
	constexpr float TabOutset = TabSize + 8.f; // 패널 왼쪽 밖
	constexpr float CellSize = 52.f;
	constexpr float CellGap = 12.f;
	constexpr float CellRadius = 8.f;
	constexpr float WellRadius = 4.f;   // 칸 홈 모서리 — 각진 나무 (M101-02)
	constexpr int32 GridColumns = 4;
	constexpr float GridTopPad = 40.f;
	// 머리 탭 오른쪽 끝 코인 칩 (판 아래 줄에서 옮김 — 레퍼런스 게임와 다르게). 알약 130 + 틈 12 + 칩 170 = 판 312 —
	// 190 이면 칩이 「배낭」 알약 오른쪽을 8 덮었다(10-16 캡처).
	constexpr float BarWidth = 170.f;
	constexpr float BarHeight = 36.f;
	constexpr float BalanceIconSize = 28.f;   // 시트 「하단 바」 재화 아이콘
	constexpr float PanelBottomPad = 28.f;     // 5줄이면 패널 440
	// M15-09 카드 높이 150 → 180: 꺼진 버튼의 까닭 줄(13pt, 최대 두 줄 ≈ 36 + 위 여백 4)이 버튼 위에 들어간다.
	constexpr float CardHeight = 180.f;
	constexpr int32 CardHintFontSize = 13;     // 이름 20 · 개수 14 보다 작게 — 까닭은 읽히되 이름을 안 가린다
	// M15-02 배낭 여는 둥근 버튼 — 사냥 HUD 오른쪽 아래 포획 버튼(130, 여백 40, 번개 줄 30 + 틈 20) **위**.
	// 1920 기준 (1767,746)~(1863,842): 패널(x ~1595)보다 오른쪽이라 열린 판과도 안 겹친다.
	constexpr float OpenButtonRadius = 12.f;   // 자리·크기는 기둥 공용값(`SnareUiStyle::PillarButtonSize`)
	const TCHAR* const BalanceIconPath = TEXT("/Game/Game/UI/Icons/Items/T_Icon_CashBundle.T_Icon_CashBundle");
	const TCHAR* const OpenButtonIconPath = TEXT("/Game/Game/UI/Icons/Items/T_Icon_HikingBackpack.T_Icon_HikingBackpack");
	// M24-01 세로 탭 그림 — 시트 「배낭 · 장비/도구(망치+톱) · 도감(파란 책)」. 책·작업대 전용 그림이 없어
	// 도감은 보물 지도, 제작은 고급 작업대 아이템 그림으로 대신한다(DESIGN 8.12 🟡).
	const TCHAR* const TabIconPaths[] = {
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_HikingBackpack.T_Icon_HikingBackpack"),
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_HandAxe.T_Icon_HandAxe"),
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_TreasureMap.T_Icon_TreasureMap"),
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_WorkbenchAdv.T_Icon_WorkbenchAdv"),
		// M28-06b2b 안내서 — 원본 Guide Book 책 그림이 없어 파란 두루마리(설계도) 그림으로 대신한다(🟡).
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_BlueprintGrill.T_Icon_BlueprintGrill") };
	constexpr int32 TabCount = UE_ARRAY_COUNT(TabIconPaths);
	constexpr float TabFrameWidth = 3.f;       // 고른 탭 노란 테 두께 — 시트 테두리(52px 칸의 약 6%)
	constexpr float TabIconPad = 4.f;          // 여는 버튼 그림 여백과 같게
	constexpr float CardGap = 12.f;
	constexpr float CardTailSize = 18.f;       // M100-03 표찰 꼬리(눕힌 네모) 한 변 — 카드 오른쪽 변에 걸친다
	constexpr float CardTailTop = 28.f;        // 카드 위에서 꼬리까지 (이름 줄 높이쯤)
	// M13-23 (DESIGN 8.12 🟡) 칸 카드는 판 **왼쪽**, 세로 탭 기둥 아래 — 판 밑(1920 폭 y 784~934)에 두면
	// 사냥 HUD 체력·스태미나 막대(y 810~882)와 공격 버튼(y 906~)을 덮었다. 1920 폭 (959,576)~(1271,726).
	// 10-16 탭이 82 로 커져 기둥 아래(760~)에 두면 1080 화면 밑으로 나간다 → 카드는 탭 기둥 **왼쪽**, 판 위 변과 같은 높이.
	constexpr float CardTop = PanelTop;
	constexpr float CardRightInset = TabOutset + CardGap;   // 판 왼쪽 변에서 카드 오른쪽 변까지
	// M13-22 (DESIGN 8.12 🟡) 칸 스크롤 바닥 여백 — 스크롤 아래 그림자(엔진 기본 8px)가 마지막 줄 칸에 닿았다.
	// 칸 여백(CellGap/2 = 6) + 12 → 마지막 칸 바닥이 그림자 위보다 10px 위.
	constexpr float ScrollBottomPad = 12.f;
	// 보이는 줄 수 (#0203 · DESIGN 8.12 🟡) — 시트의 4×5 판(440px) 그대로. 더 많은 줄(하이킹 배낭 7줄 등)은
	// 스크롤로 본다. 5줄이면 판·카드 바닥이 934 로 세로 1080 안에 든다.
	constexpr int32 MaxVisibleRows = 5;
	// 감독 2026-09-25 (DESIGN 8.12 🟡) 폰에서 "아이템 창 너무 작다" — 시트 배치(1920×1080 판) 그대로 두고 판 전체를
	// 1.6배로 키운다. 묶음(상태 줄 ~ 판 바닥, 카드 왼쪽 ~ 판 오른쪽) 가운데가 (1277, 514) — 세로만 이 가운데를 쓴다.
	// M15-17: 가로는 **판 오른쪽 변**을 축으로 키운다(아래 `BuildTree`) — 시트가 판을 오른쪽 가장자리에 두기 때문.
	constexpr float PhoneScale = 1.6f;
	const FVector2D SheetSize(1920.f, 1080.f);
	const FVector2D GroupCenter(1277.f, 514.f);

	// 펫 줄(18px 두 줄 + 윗 여백 8)·`풀어놓기` 버튼(+ 윗 여백 8)이 보이는 동안 판을 그만큼 늘린다 — 안 늘리면 재화 바가 판 밖으로 나온다.
	constexpr float PetLineExtraHeight = 64.f;
	constexpr float ReleaseButtonExtraHeight = 56.f;

	float PanelHeightFor(int32 Rows, float ExtraHeight = 0.f)
	{
		return ExtraHeight + GridTopPad + Rows * CellSize + (Rows - 1) * CellGap + ScrollBottomPad + PanelBottomPad;
	}

	bool IsBackpackItem(FName ItemID)
	{
		// 서버 `isBackpackItem` 거울 — 펫·가챠는 칸을 안 차지한다 (DESIGN 8.12).
		const FString Id = ItemID.ToString();
		return !Id.StartsWith(TEXT("gacha.")) && !Id.StartsWith(TEXT("item.pet_"));
	}

	FText GradeName(EItemGrade Grade)
	{
		switch (Grade)
		{
		case EItemGrade::Rare:      return LOCTEXT("BackpackGradeRare", "레어");
		case EItemGrade::Epic:      return LOCTEXT("BackpackGradeEpic", "에픽");
		case EItemGrade::Legendary: return LOCTEXT("BackpackGradeLegendary", "레전더리");
		default:                    return LOCTEXT("BackpackGradeCommon", "일반");
		}
	}

	void SetCanvas(UWidget* Widget, const FVector2D& Position, const FVector2D& Size, const FVector2D& Alignment)
	{
		if (UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
			CanvasSlot->SetAlignment(Alignment);
			CanvasSlot->SetPosition(Position);
			CanvasSlot->SetSize(Size);
		}
	}
}

UBackpackScreenWidget::UBackpackScreenWidget(const FObjectInitializer& ObjectInitializer)
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
	static ConstructorHelpers::FObjectFinder<UDataTable> GachaPoolTableAsset(
		TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool"));
	if (GachaPoolTableAsset.Succeeded())
	{
		GachaPoolTable = GachaPoolTableAsset.Object;
	}
}

UBackpackScreenWidget* UBackpackScreenWidget::AttachTo(APawn* Pawn)
{
	APlayerController* const Controller = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!IsValid(Controller) || !Controller->IsLocalController())
	{
		return nullptr;
	}
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Controller, StaticClass());
	if (!IsValid(Screen))
	{
		return nullptr;
	}
	UGameInstance* const GameInstance = Pawn->GetGameInstance();
	Screen->BindTargets(GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr,
		Pawn->FindComponentByClass<UBackpackComponent>(), Pawn->FindComponentByClass<UEquipmentComponent>());
	Screen->AddedViewportZOrder = Screen->GetViewportZOrder();
	Screen->AddToViewport(Screen->AddedViewportZOrder);
	// 3층 검사용 — 패널을 연 채로 띄운다 (`-SnareGachaScreen` 과 같은 검사 전용 스위치).
	Screen->SetPanelOpen(SnareCheckSwitch::Param(TEXT("SnareBackpackScreen")));
	// `-SnareBackpackTab=Equip` — 「장비」 탭을 연 채로 띄운다(올가미 기계 입구 캡처용).
	FString CheckTab;
	if (SnareCheckSwitch::Value(TEXT("SnareBackpackTab="), CheckTab) && (CheckTab == TEXT("Equip") || CheckTab == TEXT("Gacha")))
	{
		Screen->PressGearTab();
	}
	// `-SnareBackpackTab=Gacha` — 장비 탭에서 3초 뒤 「올가미 기계」(PressGacha), 6초 뒤 뽑기 판 「닫기」(Close) 를 누른다(M107-05 ⑥ 재검사).
	if (CheckTab == TEXT("Gacha") && Pawn->GetWorld() != nullptr)
	{
		TWeakObjectPtr<UBackpackScreenWidget> WeakScreen(Screen);
		FTimerHandle OpenHandle, CloseHandle;
		Pawn->GetWorld()->GetTimerManager().SetTimer(OpenHandle, [WeakScreen]()
		{
			if (WeakScreen.IsValid()) { WeakScreen->PressGacha(); }
		}, 3.f, false);
		Pawn->GetWorld()->GetTimerManager().SetTimer(CloseHandle, [WeakScreen]()
		{
			UGachaScreenWidget* const Gacha = WeakScreen.IsValid() ? WeakScreen->GetGachaScreen() : nullptr;
			const bool bWasOpen = IsValid(Gacha) && Gacha->IsInViewport();
			if (bWasOpen) { Gacha->Close(); }
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 뽑기 닫기 (-SnareBackpackTab=Gacha) — 열려 있었나 %s · 닫은 뒤 화면에 %s · 배낭 장비 탭 %s"),
				bWasOpen ? TEXT("예") : TEXT("아니오"), IsValid(Gacha) && Gacha->IsInViewport() ? TEXT("남음") : TEXT("없음"),
				WeakScreen.IsValid() && WeakScreen->IsInViewport() ? TEXT("보임") : TEXT("없음"));
		}, 6.f, false);
	}
	return Screen;
}

TArray<FInventorySlot> UBackpackScreenWidget::BuildVisibleSlots(const TArray<FInventorySlot>& Owned,
	const UDataTable* Table, int32 InStackMax)
{
	struct FKeyed
	{
		FInventorySlot InvSlot;
		uint8 Type = 0;
		uint8 Grade = 0;
	};
	TArray<FKeyed> Keyed;
	const int32 Stack = FMath::Max(1, InStackMax);
	for (const FInventorySlot& Entry : Owned)
	{
		if (Entry.ItemID.IsNone() || Entry.Count <= 0 || !IsBackpackItem(Entry.ItemID))
		{
			continue;
		}
		const FItemRow* const Row = SnareItemRows::FindByItemID(Table, Entry.ItemID);
		// 서버 `slotsFor` 와 같게 — 99개마다 한 칸 (DESIGN 8.12 스택 99).
		for (int32 Left = Entry.Count; Left > 0; Left -= Stack)
		{
			FKeyed& Item = Keyed.AddDefaulted_GetRef();
			Item.InvSlot.ItemID = Entry.ItemID;
			Item.InvSlot.Count = FMath::Min(Left, Stack);
			Item.Type = Row != nullptr ? static_cast<uint8>(Row->ItemType) : MAX_uint8;
			Item.Grade = Row != nullptr ? static_cast<uint8>(Row->Grade) : 0;
		}
	}
	// 보이는 순서만 — 종류 → 등급(높은 것 먼저) → ID → 가득 찬 칸 먼저 (DESIGN 8.12 "정렬").
	Keyed.StableSort([](const FKeyed& A, const FKeyed& B)
	{
		if (A.Type != B.Type) return A.Type < B.Type;
		if (A.Grade != B.Grade) return A.Grade > B.Grade;
		if (A.InvSlot.ItemID != B.InvSlot.ItemID) return A.InvSlot.ItemID.LexicalLess(B.InvSlot.ItemID);
		return A.InvSlot.Count > B.InvSlot.Count;
	});
	TArray<FInventorySlot> Out;
	for (const FKeyed& Item : Keyed)
	{
		Out.Add(Item.InvSlot);
	}
	return Out;
}

bool UBackpackScreenWidget::IsShownGachaPrize(FName ItemID)
{
	// 코인 환급은 서버가 코인으로 준다(M35-02) — 옛 계정에 아이템으로 남은 것도 칸을 안 그린다.
	const FString Id = ItemID.ToString();
	return Id.StartsWith(TEXT("gacha.")) && !Id.StartsWith(TEXT("gacha.refund_coin_"));
}

const FGachaPoolRow* UBackpackScreenWidget::FindGachaPoolRow(const UDataTable* InPoolTable, FName ItemID)
{
	if (InPoolTable == nullptr || ItemID.IsNone())
	{
		return nullptr;
	}
	for (const TPair<FName, uint8*>& Pair : InPoolTable->GetRowMap())
	{
		const FGachaPoolRow* const Row = reinterpret_cast<const FGachaPoolRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemID == ItemID)
		{
			return Row;
		}
	}
	return nullptr;
}

TArray<FInventorySlot> UBackpackScreenWidget::BuildGachaSlots(const TArray<FInventorySlot>& Owned,
	const UDataTable* InPoolTable, int32 InStackMax)
{
	struct FKeyed
	{
		FInventorySlot InvSlot;
		uint8 Grade = 0;
	};
	TArray<FKeyed> Keyed;
	const int32 Stack = FMath::Max(1, InStackMax);
	for (const FInventorySlot& Entry : Owned)
	{
		if (Entry.Count <= 0 || !IsShownGachaPrize(Entry.ItemID))
		{
			continue;
		}
		const FGachaPoolRow* const PoolRow = FindGachaPoolRow(InPoolTable, Entry.ItemID);
		for (int32 Left = Entry.Count; Left > 0; Left -= Stack)
		{
			FKeyed& Item = Keyed.AddDefaulted_GetRef();
			Item.InvSlot.ItemID = Entry.ItemID;
			Item.InvSlot.Count = FMath::Min(Left, Stack);
			Item.Grade = PoolRow != nullptr ? static_cast<uint8>(PoolRow->Grade) : 0;
		}
	}
	Keyed.StableSort([](const FKeyed& A, const FKeyed& B)
	{
		if (A.Grade != B.Grade) return A.Grade > B.Grade;
		if (A.InvSlot.ItemID != B.InvSlot.ItemID) return A.InvSlot.ItemID.LexicalLess(B.InvSlot.ItemID);
		return A.InvSlot.Count > B.InvSlot.Count;
	});
	TArray<FInventorySlot> Out;
	for (const FKeyed& Item : Keyed)
	{
		Out.Add(Item.InvSlot);
	}
	return Out;
}

float UBackpackScreenWidget::GetPanelLeftInsetFromRight()
{
	// 판은 화면 오른쪽 변에 맞춰 놓고(`BuildTree`) 오른쪽 변을 축으로 키우므로, 오른쪽 여백은 그대로
	// `PanelRightOffset` 이고 판 폭만 배율을 탄다.
	return PanelRightOffset + PanelWidth * PhoneScale;
}

float UBackpackScreenWidget::GetTabsLeftInsetFromRight()
{
	// 탭 열은 판 왼쪽 변에서 `TabOutset` 밖에 서고(`BuildTree`) 같은 1.6배 묶음 안에 있다.
	return (PanelRightOffset + PanelWidth + TabOutset) * PhoneScale;
}

int32 UBackpackScreenWidget::ComputeCapacity(const TArray<FInventorySlot>& Owned, const UDataTable* Table,
	int32 BaseSlots)
{
	int32 Bonus = 0;
	for (const FInventorySlot& Entry : Owned)
	{
		const FItemRow* const Row = Entry.Count > 0 ? SnareItemRows::FindByItemID(Table, Entry.ItemID) : nullptr;
		if (Row != nullptr && Row->EquipSlot == EEquipSlot::Backpack)
		{
			Bonus = FMath::Max(Bonus, Row->BonusSlots);
		}
	}
	return BaseSlots + Bonus;
}

void UBackpackScreenWidget::BindTargets(USnareBackendSubsystem* InBackend, UBackpackComponent* InBackpack,
	UEquipmentComponent* InEquipment)
{
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UBackpackScreenWidget::HandleSnapshotChanged);
	}
	if (IsValid(Backpack))
	{
		Backpack->OnReply.RemoveDynamic(this, &UBackpackScreenWidget::HandleReply);
	}
	if (IsValid(Equipment))
	{
		Equipment->OnEquipmentChanged.RemoveDynamic(this, &UBackpackScreenWidget::HandleEquipmentChanged);
	}
	if (IsValid(PendingSource))
	{
		PendingSource->OnPendingGrantQueued.RemoveDynamic(this, &UBackpackScreenWidget::HandlePendingGrantQueued);
	}
	Backend = InBackend;
	Backpack = InBackpack;
	Equipment = InEquipment;
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.AddDynamic(this, &UBackpackScreenWidget::HandleSnapshotChanged);
	}
	if (IsValid(Backpack))
	{
		Backpack->OnReply.AddDynamic(this, &UBackpackScreenWidget::HandleReply);
	}
	if (IsValid(Equipment))
	{
		Equipment->OnEquipmentChanged.AddDynamic(this, &UBackpackScreenWidget::HandleEquipmentChanged);
	}
	AActor* const PendingOwner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
	PendingSource = PendingOwner != nullptr ? PendingOwner->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (IsValid(PendingSource))
	{
		PendingSource->OnPendingGrantQueued.AddDynamic(this, &UBackpackScreenWidget::HandlePendingGrantQueued);
	}
	if (!IsValid(RanchSync) && PendingOwner != nullptr)
	{
		SetRanchSync(PendingOwner->FindComponentByClass<URanchSyncComponent>());
	}
	Redraw();
}

void UBackpackScreenWidget::SetRanchSync(URanchSyncComponent* Sync)
{
	if (IsValid(RanchSync))
	{
		RanchSync->OnRanchRequestAnswered.RemoveDynamic(this, &UBackpackScreenWidget::HandleRanchAnswered);
	}
	RanchSync = Sync;
	if (IsValid(RanchSync))
	{
		RanchSync->OnRanchRequestAnswered.AddUniqueDynamic(this, &UBackpackScreenWidget::HandleRanchAnswered);
	}
	WatchRanchSync();
	RedrawCard();
}

void UBackpackScreenWidget::WatchRanchSync()
{
	URanchComponent* const Ranch = IsValid(RanchSync) ? RanchSync->GetRanch() : nullptr;
	if (Ranch == WatchedRanch)
	{
		return;
	}
	if (IsValid(WatchedRanch))
	{
		WatchedRanch->OnRanchCacheChanged.RemoveDynamic(this, &UBackpackScreenWidget::HandleRanchCacheChanged);
	}
	WatchedRanch = Ranch;
	if (IsValid(WatchedRanch))
	{
		WatchedRanch->OnRanchCacheChanged.AddUniqueDynamic(this, &UBackpackScreenWidget::HandleRanchCacheChanged);
	}
}

void UBackpackScreenWidget::HandleRanchAnswered(const FString& FunctionName, bool bAccepted, const FString& FailureCode,
	const FText& Message)
{
	// 유지 넣기 등 다른 목장 답도 「한 번에 하나」 자리를 비우니 카드 버튼을 다시 그린다. 글은 데코 답만.
	if (FunctionName == URanchSyncComponent::GetSetRanchDecoFunctionName())
	{
		// 실패 글은 목장 HUD 도 같은 줄에 띄운다 — 목장 HUD 가 없는 사냥터에서도 보이게 여기서도 올린다.
		PostStatus(bAccepted ? PendingDecoNotice : Message, !bAccepted);
		PendingDecoNotice = FText::GetEmpty();
	}
	RedrawCard();
}

void UBackpackScreenWidget::HandleRanchCacheChanged()
{
	RedrawCard(); // 다시 받은 데코가 들어오면 「목장 적용」 ↔ 「적용 해제」 가 바뀐다
}

void UBackpackScreenWidget::SetNoticeRanchHud(URanchHudWidget* Hud)
{
	NoticeRanchHud = Hud;
}

void UBackpackScreenWidget::SetNoticeHud(UHuntHudWidget* Hud)
{
	NoticeHud = Hud;
}

void UBackpackScreenWidget::SetItemTable(UDataTable* Table)
{
	ItemTable = Table;
	Redraw();
}

const FItemRow* UBackpackScreenWidget::FindRow(FName ItemID) const
{
	return SnareItemRows::FindByItemID(ItemTable, ItemID);
}

int32 UBackpackScreenWidget::GetCellCount() const
{
	return Cells.Num();
}

FText UBackpackScreenWidget::FormatPetSummary(const TArray<FInventorySlot>& Owned, int32 PendingPets)
{
	int32 Pets = 0;
	for (const FInventorySlot& Entry : Owned)
	{
		if (Entry.ItemID.ToString().StartsWith(TEXT("item.pet_")))
		{
			Pets += Entry.Count;
		}
	}
	if (Pets <= 0 && PendingPets <= 0)
	{
		return FText::GetEmpty();
	}
	if (PendingPets <= 0)
	{
		// 어디서 풀어 놓는지도 같이 적는다 — 목장 칸 앞 HUD 의 넣기 길이 유일하다(폰 확인 09-29 22:50).
		return FText::Format(LOCTEXT("BackpackPetCount", "펫 {0}마리 · 목장 칸 앞에서 넣기"), FText::AsNumber(Pets));
	}
	return FText::Format(LOCTEXT("BackpackPetCountPending", "펫 {0}마리 · 등록 대기 {1} · 목장 칸 앞에서 넣기"),
		FText::AsNumber(Pets), FText::AsNumber(PendingPets));
}

FText UBackpackScreenWidget::GetStatusText() const
{
	return Status;
}

FText UBackpackScreenWidget::GetCardHintText() const
{
	return IsValid(CardHintText) ? CardHintText->GetText() : FText::GetEmpty();
}

FText UBackpackScreenWidget::MakeDisabledHint(const FItemRow* Row, const UDataTable* RecipeTable)
{
	if (Row == nullptr)
	{
		return LOCTEXT("BackpackHintNoRow", "표에 없는 물건입니다");
	}
	if (Row->bPassiveWhileCarried)
	{
		return LOCTEXT("BackpackHintCarried", "배낭에 가지고 있는 동안 효과가 켜집니다");
	}
	switch (Row->ItemType)
	{
	case EItemType::Pet:
		// 펫은 배낭 목록에서 걸러져 카드가 뜰 일이 거의 없지만(서버 `isBackpackItem`), 떠도 말은 맞게 한다.
		return LOCTEXT("BackpackHintPet", "포획한 동물 - 목장 칸에 올립니다");
	case EItemType::Placeable:
		// 「놓기」 가 꺼지는 유일한 까닭 = 칸 수가 없고 배도 아니다 (표가 덜 채워진 행).
		return LOCTEXT("BackpackHintNoFootprint", "놓을 자리 칸 수가 없어 아직 못 놓습니다");
	case EItemType::Equipment:
		return LOCTEXT("BackpackHintNoSlot", "입을 자리가 정해지지 않았습니다");
	case EItemType::Food:
		return LOCTEXT("BackpackHintNotEdible", "지금은 먹을 수 없습니다");
	case EItemType::PetCosmetic:
		return LOCTEXT("BackpackHintPetCosmetic", "목장 동물 꾸미기 - 목장 칸 카드에서 씌웁니다");
	case EItemType::RanchDeco:
		return LOCTEXT("BackpackHintRanchDeco", "목장 데코 - 목장 겉모습을 바꿉니다");
	default:
		break;
	}
	// 목줄·안장은 배낭에서 누를 것이 없다 — 동물에 붙이는 곳이 **잡화상 줄**뿐이라 그 사실을 말해 준다
	// (M15-11 · INBOX 2026-09-25 ⑤ "안장은 잡화상에서만 붙는다는 안내를 카드에"). 어느 것이 목줄·안장인지는
	// 상점 화면과 **같은 함수**가 가른다 (DESIGN 8.3 결정 009).
	if (UShopScreenWidget::IsMountCollar(Row->ItemID))
	{
		return LOCTEXT("BackpackHintCollar", "탈것 목줄 - 잡화상에서 동물에 채웁니다");
	}
	if (UShopScreenWidget::IsMountSaddle(Row->ItemID))
	{
		return LOCTEXT("BackpackHintSaddle", "탈것 안장 - 잡화상에서 탈것에 붙입니다");
	}
	// 재료 — 아는 것만 말한다: 레시피 표에 재료로 들면 제작, `SellPrice` 가 있으면 판매 (DESIGN 8.10·8.14).
	const bool bIngredient = IsCraftIngredient(RecipeTable, *Row);
	const bool bSellable = Row->SellPrice > 0;
	if (bIngredient && bSellable)
	{
		return LOCTEXT("BackpackHintCraftSell", "재료 - 제작에 쓰거나 잡화상에 팝니다");
	}
	if (bIngredient)
	{
		return LOCTEXT("BackpackHintCraft", "재료 - 제작에 씁니다");
	}
	if (bSellable)
	{
		return LOCTEXT("BackpackHintSell", "재료 - 잡화상에 팝니다");
	}
	return LOCTEXT("BackpackHintNoUse", "재료 - 아직 쓸 데가 없습니다");
}

bool UBackpackScreenWidget::IsCraftIngredient(const UDataTable* RecipeTable, const FItemRow& Row)
{
	if (RecipeTable == nullptr)
	{
		return false;    // 표를 못 받았으면 제작 얘기를 지어내지 않는다
	}
	bool bUsed = false;
	RecipeTable->ForeachRow<FRecipeRow>(TEXT("BackpackHint"), [&bUsed, &Row](const FName&, const FRecipeRow& Recipe)
	{
		for (const FRecipeIngredient& Ingredient : Recipe.Ingredients)
		{
			// 재료 칸은 ID 아니면 태그 하나로 받는다 (`FRecipeIngredient` 주석) — 둘 다 본다.
			if (Ingredient.ItemID == Row.ItemID
				|| (Ingredient.Tag != ECraftTag::None && Ingredient.Tag == Row.CraftTag))
			{
				bUsed = true;
				return;
			}
		}
	});
	return bUsed;
}

const UDataTable* UBackpackScreenWidget::FindRecipeTable() const
{
	const UGameInstance* const GameInstance = GetGameInstance();
	const USnareBaseSubsystem* const Base = GameInstance != nullptr
		? GameInstance->GetSubsystem<USnareBaseSubsystem>() : nullptr;
	return Base != nullptr ? Base->GetRecipeTable() : nullptr;
}

void UBackpackScreenWidget::HandleSnapshotChanged()
{
	Redraw();
}

void UBackpackScreenWidget::HandlePendingGrantQueued(FName ItemID, int32 Count)
{
	Redraw();
}

void UBackpackScreenWidget::HandleReply(bool bAccepted, const FString& Code, const FString& Message)
{
	// 서버 사유를 그대로 옮긴다(`가방이 가득 찼다` 등) — 서버에 못 닿았을 때(`network`)만 표 문구로. 받아들였으면 칸은 곧 스냅샷이 바꾼다.
	// 답에 함수 이름이 없어 `<함수>_<code>` 행은 못 본다.
	Status = bAccepted ? FText::GetEmpty()
		: URanchSyncComponent::DescribeFailureCode(FailureTable, FString(), Code, Message, nullptr, false);
	// 서버에 닿지 못했을 때만 카드에 「다시 시도」 — 「가방이 가득 찼다」 같은 거절은 다시 보내도 같다.
	RetryRequest = !bAccepted && Code == TEXT("network") ? MoveTemp(SentRequest) : nullptr;
	SentRequest = nullptr;
	PostStatus(Status, !bAccepted);
	Redraw(); // 모자를 쓰거나 벗은 것이 받아들여지면 칸의 착용 딱지와 카드 버튼이 같이 바뀐다 (#0198)
}

void UBackpackScreenWidget::HandleEquipmentChanged()
{
	Redraw(); // 도구·옷·배낭을 들거나 내리면 칸의 착용 딱지가 따라온다
}

void UBackpackScreenWidget::PostStatus(const FText& Message, bool bError)
{
	Status = Message;
	if (Message.IsEmpty())
	{
		return;
	}
	const ASnarehavenCharacter* const Character = IsValid(Backpack) ? Cast<ASnarehavenCharacter>(Backpack->GetOwner()) : nullptr;
	UHuntHudWidget* const Hud = IsValid(NoticeHud) ? NoticeHud.Get() : (IsValid(Character) ? Character->GetHuntHud() : nullptr);
	if (IsValid(Hud))
	{
		if (bError)
		{
			Hud->ShowErrorBanner(Message);
		}
		else
		{
			Hud->ShowNoticeBanner(Message);
		}
		return;
	}
	// 거점은 사냥 HUD 를 안 띄운다 — 결과 글은 목장 HUD 거절 줄로 (#0517 빈 칸: 거절에 화면 글 0).
	URanchHudWidget* const RanchHud = IsValid(NoticeRanchHud) ? NoticeRanchHud.Get() : (IsValid(Character) ? Character->GetRanchHud() : nullptr);
	if (IsValid(RanchHud))
	{
		RanchHud->ShowNotice(Message);
	}
}

bool UBackpackScreenWidget::IsItemWorn(FName ItemID) const
{
	const FItemRow* const Row = FindRow(ItemID);
	if (Row == nullptr || Row->EquipSlot == EEquipSlot::None)
	{
		return false;
	}
	if (Row->EquipSlot == EEquipSlot::Head)
	{
		return IsValid(Backpack) && !Backpack->GetWornHatID().IsNone() && Backpack->GetWornHatID() == ItemID;
	}
	if (!IsValid(Equipment))
	{
		return false;
	}
	const FName EquippedRow = Equipment->GetEquippedItem(Row->EquipSlot);
	return !EquippedRow.IsNone() && EquippedRow == Equipment->FindEquipmentRowByItemID(ItemID);
}

void UBackpackScreenWidget::ToggleBackpackTab()
{
	ShowTab(false);
}

void UBackpackScreenWidget::PressGearTab()
{
	ShowTab(true);
}

void UBackpackScreenWidget::ShowTab(bool bGear)
{
	if (bPanelOpen && bGearOnly == bGear)
	{
		SetPanelOpen(false);
		return;
	}
	bGearOnly = bGear;
	SelectedIndex = INDEX_NONE; // 보기가 바뀌면 칸 번호가 달라진다
	if (UTextBlock* const Title = WidgetTree != nullptr ? Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TitleText"))) : nullptr)
	{
		Title->SetText(bGear ? LOCTEXT("BackpackTitleGear", "장비") : LOCTEXT("BackpackTitle", "배낭"));
	}
	Redraw();
	SetPanelOpen(true);
}

void UBackpackScreenWidget::PressCodexTab()
{
	// 떠 있는 동안(업적으로 접힌 때 포함) 다시 만들지 않는다. 닫으면 뒤의 배낭(ZOrder 0)으로 돌아온다.
	if (IsValid(CodexScreen) && CodexScreen->IsInViewport())
	{
		return;
	}
	// Backend = AttachTo 가 붙인 게임 인스턴스의 USnareBackendSubsystem.
	CodexScreen = UCodexScreenWidget::OpenOnViewport(GetOwningPlayer(), GetWorld(), Backend);
}

void UBackpackScreenWidget::PressCraftTab()
{
	// 배낭 제작 목록 = 어디서나 맨손 제작(DESIGN 8.10). 작업대 목록은 작업대 앞 상호작용이 연다.
	AActor* const Owner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
	if (USnareBaseBuildComponent* const Build = Owner != nullptr ? Owner->FindComponentByClass<USnareBaseBuildComponent>() : nullptr)
	{
		Build->OpenBackpackCrafting();
		// 제작 판은 반투명이라 뒤에 배낭 판이 비쳐 겹친다 (M22 ③) — 실제로 떴을 때만 배낭 판을 접는다.
		const UCraftingScreenWidget* const Craft = Build->GetCraftingScreen();
		if (Craft != nullptr && Craft->IsOpen())
		{
			SetPanelOpen(false);
		}
	}
}

void UBackpackScreenWidget::PressGuideTab()
{
	// 도감 탭과 같은 꼴 — 안내서는 전체 화면 판 Z 로 배낭 위에 뜨고, 닫기·ESC 는 안내서만 닫아 배낭으로 돌아온다.
	if (IsValid(GuideBook) && GuideBook->IsInViewport())
	{
		return;
	}
	GuideBook = USnareGuideBookWidget::OpenOnViewport(GetOwningPlayer(), GetWorld());
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 배낭 안내서 탭 — 안내서를 열었다 (쪽 %d)"),
		GuideBook != nullptr ? GuideBook->GetPageKeys().Num() : 0);
}

void UBackpackScreenWidget::PressWardrobeShop()
{
	if (!USnareStoreSettings::AreGemsEnabled())
	{
		return;
	}
	if (IsValid(WardrobeShop) && WardrobeShop->IsInViewport())
	{
		WardrobeShop->Close();
		UE_LOG(LogSnarehaven, Log, TEXT("[UI] 배낭 장비 탭 「상점 열기」 — 옷장 상점을 닫았다"));
		return;
	}
	WardrobeShop = UWardrobeShopWidget::OpenOnViewport(GetOwningPlayer(), GetWorld(), Backend);
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 배낭 장비 탭 「상점 열기」 — 옷장 상점을 %s (장비 보기 %s)"),
		WardrobeShop != nullptr ? TEXT("열었다") : TEXT("못 열었다"), bGearOnly ? TEXT("예") : TEXT("아니오"));
}

void UBackpackScreenWidget::PressGacha()
{
	if (IsValid(GachaScreen) && GachaScreen->IsInViewport())
	{
		return;
	}
	// 뽑기 판은 전체 화면 판 Z 로 배낭 위에 뜬다 — 닫기·ESC 는 뽑기 판만 떼어 내 배낭으로 돌아온다(도감 탭과 같은 꼴).
	GachaScreen = UGachaScreenWidget::OpenOnViewport(GetOwningPlayer(), GetWorld(), Backend);
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 배낭 장비 탭 「올가미 기계」 — 뽑기 판을 %s"),
		GachaScreen != nullptr ? TEXT("열었다") : TEXT("못 열었다"));
}

void UBackpackScreenWidget::PressWardrobe()
{
	if (IsValid(Wardrobe) && Wardrobe->IsInViewport())
	{
		Wardrobe->Close();
		UE_LOG(LogSnarehaven, Log, TEXT("[UI] 배낭 장비 탭 「옷장」 — 옷장 화면을 닫았다"));
		return;
	}
	Wardrobe = UWardrobeWidget::OpenOnViewport(GetOwningPlayer(), GetWorld(), Backend, Backpack, Equipment);
	if (IsValid(Wardrobe))
	{
		// 옷장이 열려 있는 동안 뒤 배낭 판(장비 칸·연결 줄)은 숨긴다 — 닫히면 되살린다.
		Wardrobe->OnClosed.AddUObject(this, &UBackpackScreenWidget::HandleWardrobeClosed);
		bWardrobeCovers = true;
		SetPanelOpen(bPanelOpen);
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[UI] 배낭 장비 탭 「옷장」 — 옷장 화면을 %s (장비 보기 %s)"),
		Wardrobe != nullptr ? TEXT("열었다") : TEXT("못 열었다"), bGearOnly ? TEXT("예") : TEXT("아니오"));
}

bool UBackpackScreenWidget::IsPanelShown() const
{
	return IsValid(Panel) && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UBackpackScreenWidget::HandleWardrobeClosed()
{
	bWardrobeCovers = false;
	if (IsValid(this) && bPanelOpen)
	{
		SetPanelOpen(true);
	}
}

void UBackpackScreenWidget::SetBalanceChipHidden(bool bHidden)
{
	if (IsValid(BalanceChip))
	{
		BalanceChip->SetVisibility(bHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

bool UBackpackScreenWidget::IsBalanceChipHidden() const
{
	return IsValid(BalanceChip) && BalanceChip->GetVisibility() == ESlateVisibility::Collapsed;
}

void UBackpackScreenWidget::SetCloseHidden(bool bHidden)
{
	bCloseHidden = bHidden;
	if (IsValid(CloseBox))
	{
		CloseBox->SetVisibility(bPanelOpen && !bCloseHidden ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

bool UBackpackScreenWidget::IsCloseHidden() const
{
	return IsValid(CloseBox) && CloseBox->GetVisibility() == ESlateVisibility::Collapsed;
}

void UBackpackScreenWidget::PressClose()
{
	SetPanelOpen(false);
}

void UBackpackScreenWidget::SetPanelOpen(bool bOpen)
{
	bPanelOpen = bOpen;
	if (!bOpen)
	{
		SelectedIndex = INDEX_NONE;
		if (IsValid(WardrobeShop) && WardrobeShop->IsInViewport())
		{
			WardrobeShop->Close();
		}
		if (IsValid(Wardrobe) && Wardrobe->IsInViewport())
		{
			Wardrobe->Close();
		}
	}
	const ESlateVisibility Shown = bOpen && !bWardrobeCovers ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (IsValid(Panel))
	{
		Panel->SetVisibility(Shown);
	}
	if (IsValid(TitlePill))
	{
		TitlePill->SetVisibility(Shown);
	}
	if (IsValid(TabColumn))
	{
		TabColumn->SetVisibility(Shown);
	}
	if (IsValid(CloseBox))
	{
		CloseBox->SetVisibility(Shown == ESlateVisibility::Collapsed || bCloseHidden ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	// 코인 칩 — 판이 열리면 머리 탭 줄 오른쪽 끝, 닫히면 화면 오른쪽 위 구석 (10-16 HUD 3차 사람 지적 「우측 위쪽에 붙여두던가」).
	// 닫힌 칩은 키운 시트 판(Root) 밖, 화면 판(RootWidget)에 붙인다 — 시트 좌표의 구석은 변환 때문에 화면 밖으로 밀린다.
	UCanvasPanel* const ChipParent = Shown != ESlateVisibility::Collapsed ? Root.Get() : Cast<UCanvasPanel>(WidgetTree ? WidgetTree->RootWidget : nullptr);
	if (IsValid(BalanceChip) && IsValid(ChipParent))
	{
		if (BalanceChip->GetParent() != ChipParent)
		{
			ChipParent->AddChild(BalanceChip);
		}
		SetCanvas(BalanceChip, Shown != ESlateVisibility::Collapsed
				? FVector2D(-PanelRightOffset, PanelTop - PillRise + (PillHeight - BarHeight) * 0.5f)
				: FVector2D(-SnareUiStyle::PillarRight, SnareUiStyle::PillarRight),
			FVector2D(BarWidth, BarHeight), FVector2D(1.f, 0.f));
		// 닫힌 칩(화면 오른쪽 위 구석)은 글 길이만큼 늘어난다 — 170 고정이면 「서버 연결 안 됨」 끝 글자가 잘렸다(출시 전 점검 10-16).
		// 연 판 머리 줄은 「배낭」 알약 옆 자리가 170 뿐이라 고정 폭 + 글 줄임(`BalanceTextFit`) 그대로.
		if (UCanvasPanelSlot* const ChipSlot = Cast<UCanvasPanelSlot>(BalanceChip->Slot))
		{
			ChipSlot->SetAutoSize(Shown == ESlateVisibility::Collapsed);
		}
	}
	RedrawTabs();
	RedrawCard();
	// 연 판은 상인 「상점 열기」 카드(HUD Z) 위로 — 같은 Z 면 나중에 뜬 카드가 세로 탭 위에 겹친다(#0503 3층 캡처).
	// Z 는 붙일 때만 정해져 지도와 같이 떼었다 다시 붙인다. 닫힌 판은 HUD Z 로 돌아가 다른 판 위로 뚫고 나오지 않는다.
	const int32 Wanted = GetViewportZOrder();
	if (Wanted != AddedViewportZOrder && IsInViewport())
	{
		RemoveFromParent();
		AddedViewportZOrder = Wanted;
		AddToViewport(Wanted);
	}
}

int32 UBackpackScreenWidget::GetViewportZOrder() const
{
	return bPanelOpen ? SnareUiStyle::ScreenPanelViewportZOrder : SnareUiStyle::HudViewportZOrder;
}

int32 UBackpackScreenWidget::GetCurrentTabIndex() const
{
	// 도감·제작은 다른 판을 띄우는 버튼이라 이 판의 「지금 탭」 이 될 수 없다 — 배낭(0)·장비(1) 보기만.
	if (!bPanelOpen)
	{
		return INDEX_NONE;
	}
	return bGearOnly ? 1 : 0;
}

void UBackpackScreenWidget::RedrawTabs()
{
	const int32 Current = GetCurrentTabIndex();
	for (int32 Index = 0; Index < TabFrames.Num(); ++Index)
	{
		if (UBorder* const Frame = TabFrames[Index])
		{
			Frame->SetBrushColor(Index == Current ? SnareUiStyle::UiBoltYellow() : SnareUiStyle::UiPanelCream());
		}
	}
}

void UBackpackScreenWidget::SelectSlot(int32 Index)
{
	if (Index >= ShownSlots.Num() && PendingPetSlots.IsValidIndex(Index - ShownSlots.Num()))
	{
		// 등록 대기 펫 칸 — 백엔드가 받기 전엔 목장에 못 넣는다(규칙 8). 이유를 알려 준다.
		PostStatus(LOCTEXT("BackpackPendingPetTap", "등록 대기 중 - 서버에 접속되면 목장에 넣을 수 있습니다"));
		return;
	}
	SelectedIndex = (Index == SelectedIndex || !ShownSlots.IsValidIndex(Index)) ? INDEX_NONE : Index;
	RetryRequest = nullptr;   // 다른 칸을 골랐다 — 앞 칸 요청을 다시 보내지 않는다
	RedrawCard();
}

void UBackpackScreenWidget::PressUse()
{
	const FItemRow* const DecoRow = ShownSlots.IsValidIndex(SelectedIndex) ? FindRow(ShownSlots[SelectedIndex].ItemID) : nullptr;
	if (DecoRow != nullptr && DecoRow->ItemType == EItemType::RanchDeco)
	{
		PressRanchDeco();   // 가챠 경품이라 배낭 컴포넌트가 아니라 목장 전송기가 묻는다
		return;
	}
	if (!ShownSlots.IsValidIndex(SelectedIndex) || !IsValid(Backpack))
	{
		return;
	}
	const FName ItemID = ShownSlots[SelectedIndex].ItemID;
	const FItemRow* const Row = FindRow(ItemID);
	if (UCrateScreenWidget::IsCrateItem(GetDefault<UCrateScreenWidget>()->GetCrateTable(), ItemID))
	{
		// 꾸미기 상자(10.8) — 열쇠가 있을 때만 상자 화면을 연다. 뽑기는 그 화면이 서버(`OpenCrate`)에 묻는다.
		const TArray<FInventorySlot> Empty;
		if (UCrateScreenWidget::CountOwned(IsValid(Backend) ? Backend->GetPlayerSnapshot().InventorySlots : Empty,
			UBackpackComponent::GetCrateKeyID()) <= 0)
		{
			PostStatus(LOCTEXT("BackpackCrateNoKey", "열쇠가 없다 - 잡화 상인에게 산다"));
			return;
		}
		UCrateScreenWidget::OpenFor(GetOwningPlayer(), Backend, Backpack, ItemID);
	}
	else if (ItemID == SnareChestLoot::GetTreasureMapItemID())
	{
		// 보물 지도(13.8) — 서버가 자리를 정하고 지도를 뺀다. 표식은 배낭 컴포넌트가 답을 받은 뒤 세운다.
		SendToBackpack([](UBackpackComponent& Target) { Target.RequestUseTreasureMap(); });
	}
	else if (Row == nullptr || Row->FilledItemID.IsNone())
	{
		SendToBackpack([ItemID](UBackpackComponent& Target) { Target.RequestConsume(ItemID); });
	}
	else
	{
		// 빈 병은 물가(바다·낚시 포인트 400cm, DESIGN 13.6)에서만 — 판정은 호스트, 거절 사유는 `HandleReply` 로 온다 (#0203).
		SendToBackpack([ItemID](UBackpackComponent& Target) { Target.RequestFill(ItemID); });
	}
}

void UBackpackScreenWidget::SendToBackpack(TFunction<void(UBackpackComponent&)> Send)
{
	// 답(`HandleReply`)이 같은 틀에서 바로 올 수도 있어 보내기 **전에** 기억한다.
	RetryRequest = nullptr;
	SentRequest = Send;
	Send(*Backpack);
}

void UBackpackScreenWidget::PressRetry()
{
	TFunction<void(UBackpackComponent&)> Send = MoveTemp(RetryRequest);
	RetryRequest = nullptr;
	Status = FText::GetEmpty();
	if (Send && IsValid(Backpack))
	{
		SendToBackpack(MoveTemp(Send));
	}
	RedrawCard();
}

void UBackpackScreenWidget::PressRanchDeco()
{
	const FName ItemID = ShownSlots.IsValidIndex(SelectedIndex) ? ShownSlots[SelectedIndex].ItemID : NAME_None;
	const FItemRow* const Row = FindRow(ItemID);
	if (Row == nullptr || Row->ItemType != EItemType::RanchDeco)
	{
		return;
	}
	if (!IsValid(RanchSync))
	{
		PostStatus(LOCTEXT("BackpackDecoNoRanch", "여기서는 목장 데코를 적용할 수 없습니다"), true);
		return;
	}
	// 같은 종류 칸에 적혀 있으면 푼다(빈 itemId + kind), 아니면 적용한다 — 같은 종류의 옛 것은 서버가 바꿔 끼운다.
	const bool bClear = IsSelectedDecoApplied();
	if (!RanchSync->RequestSetRanchDeco(bClear ? NAME_None : ItemID, Row->RanchDecoKind))
	{
		PostStatus(LOCTEXT("BackpackDecoBusy", "목장 요청을 기다리는 중 - 잠시 뒤 다시 누르세요"), true);
		return;
	}
	PendingDecoNotice = bClear
		? FText::Format(LOCTEXT("BackpackDecoCleared", "{0} 적용을 풀었다"), Row->DisplayName)
		: FText::Format(LOCTEXT("BackpackDecoApplied", "{0}을(를) 목장에 적용했다"), Row->DisplayName);
	PostStatus(bClear ? LOCTEXT("BackpackDecoClearing", "푸는 중") : LOCTEXT("BackpackDecoApplying", "적용하는 중"));
	RedrawCard();   // 답이 올 때까지 버튼이 꺼진다
}

bool UBackpackScreenWidget::IsSelectedDecoApplied() const
{
	const FName ItemID = ShownSlots.IsValidIndex(SelectedIndex) ? ShownSlots[SelectedIndex].ItemID : NAME_None;
	const FItemRow* const Row = FindRow(ItemID);
	const URanchComponent* const Ranch = IsValid(RanchSync) ? RanchSync->GetRanch() : nullptr;
	return Row != nullptr && Row->ItemType == EItemType::RanchDeco && Ranch != nullptr
		&& Ranch->GetDeco().FindRef(Row->RanchDecoKind) == ItemID;
}

void UBackpackScreenWidget::PressEquip()
{
	if (!ShownSlots.IsValidIndex(SelectedIndex))
	{
		return;
	}
	const FName ItemID = ShownSlots[SelectedIndex].ItemID;
	// #0198 모자는 서버 기록이 본체다(13.5 효과) — 겉모습(메시)이 없어도 서버에 적는다. 전에는 겉모습이 붙어야만
	// 서버에 적어서, 메시 없는 모자 7종은 써도 효과가 없었다. 쓴 모자를 다시 누르면 벗는다.
	const FItemRow* const ItemRow = FindRow(ItemID);
	if (ItemRow != nullptr && ItemRow->EquipSlot == EEquipSlot::Head && IsValid(Backpack))
	{
		const bool bTakeOff = IsSelectedWornHat();
		RetryRequest = nullptr;
		SentRequest = [bTakeOff, ItemID](UBackpackComponent& Target) { bTakeOff ? Target.RequestUnequipHat() : Target.RequestEquipHat(ItemID); };
		if (bTakeOff ? Backpack->RequestUnequipHat() : Backpack->RequestEquipHat(ItemID))
		{
			if (!bTakeOff && IsValid(Equipment))
			{
				Equipment->EquipItemByItemID(ItemID); // 겉모습만 — 메시가 없으면 조용히 안 붙는다
			}
			Status = bTakeOff ? LOCTEXT("BackpackUnequipping", "벗는 중") : LOCTEXT("BackpackHatEquipping", "쓰는 중");
			PostStatus(Status);
		}
		return;
	}
	if (!IsValid(Equipment))
	{
		return;
	}
	// 게스트(멀티 손님)는 자기 화면을 혼자 고치지 않는다 — 호스트에게 요청만 보내고 결과는 복제로 내려온다
	// (DESIGN 4.1 권위 표). 그래서 돌아온 false 는 "못 한다" 가 아니라 **"보냈다"** 다. 전에는 게스트가 장착할
	// 때마다 "장착할 수 없다" 가 떴다 (M15-11 · INBOX 2026-09-25 ⑤).
	// 장비 행인지는 장착하는 쪽과 **같은 함수**로 본다 — 재료를 눌렀을 때까지 "보냈다" 라고 말하지 않는다.
	bool bFailed = false;
	const bool bSentToHost = !Equipment->HasEquipAuthority()
		&& !Equipment->FindEquipmentRowByItemID(ItemID).IsNone();
	// **든 것을 다시 누르면 내린다** — 모자와 같은 규칙이다. 전에는 같은 카드를 눌러도 또 장착이라
	// 한 번 든 도구를 가방에서 내려놓을 길이 없었다 (M15-08 · INBOX ②).
	if (ItemRow != nullptr && IsSelectedEquippedGear())
	{
		const bool bTookOff = Equipment->UnequipSlot(ItemRow->EquipSlot);
		bFailed = !bSentToHost && !bTookOff;
		Status = bSentToHost ? LOCTEXT("BackpackGearOffSent", "내리는 중")
			: bTookOff ? LOCTEXT("BackpackGearOff", "내렸다") : LOCTEXT("BackpackGearOffFail", "내릴 수 없다");
	}
	else
	{
		// 겉모습은 바로 입힌다(DESIGN 3.5).
		const bool bWorn = Equipment->EquipItemByItemID(ItemID);
		bFailed = !bSentToHost && !bWorn;
		Status = bSentToHost ? LOCTEXT("BackpackEquipSent", "장착하는 중")
			: bWorn ? LOCTEXT("BackpackEquipped", "장착했다") : LOCTEXT("BackpackEquipFail", "장착할 수 없다");
	}
	PostStatus(Status, bFailed);
	RedrawCard(); // 버튼 글자가 `장착` ↔ `내리기` 로 바뀐다
}

bool UBackpackScreenWidget::IsSelectedEquippedGear() const
{
	if (!IsValid(Equipment) || !ShownSlots.IsValidIndex(SelectedIndex))
	{
		return false;
	}
	const FName ItemID = ShownSlots[SelectedIndex].ItemID;
	const FItemRow* const Row = FindRow(ItemID);
	if (Row == nullptr || Row->EquipSlot == EEquipSlot::None || Row->EquipSlot == EEquipSlot::Head)
	{
		return false;   // 모자는 서버 기록이 본체라 `IsSelectedWornHat` 이 따로 본다 (#0198)
	}
	// 무엇이 붙어 있는지는 장비 컴포넌트 하나가 안다 — 여기서 따로 세지 않는다 (HUD 버튼과 같은 규칙).
	const FName EquippedRow = Equipment->GetEquippedItem(Row->EquipSlot);
	return !EquippedRow.IsNone() && EquippedRow == Equipment->FindEquipmentRowByItemID(ItemID);
}

bool UBackpackScreenWidget::IsSelectedWornHat() const
{
	// 쓴 모자는 서버가 받아들인 것만 — 겉모습 칸(메시)은 모자 7종에 아직 없어 기준이 못 된다.
	return IsValid(Backpack) && ShownSlots.IsValidIndex(SelectedIndex) && !Backpack->GetWornHatID().IsNone()
		&& Backpack->GetWornHatID() == ShownSlots[SelectedIndex].ItemID;
}

void UBackpackScreenWidget::PressDrop()
{
	// ponytail: 칸 하나를 통째로 버린다 — 개수 고르기는 원본에 없고 모바일 입력이 늘어서 뺐다.
	if (ShownSlots.IsValidIndex(SelectedIndex) && IsValid(Backpack))
	{
		const FInventorySlot Dropped = ShownSlots[SelectedIndex];
		SendToBackpack([Dropped](UBackpackComponent& Target) { Target.RequestDrop(Dropped.ItemID, Dropped.Count); });
	}
}

USnareBaseBuildComponent* UBackpackScreenWidget::FindBuild() const
{
	AActor* const Owner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
	return Owner != nullptr ? Owner->FindComponentByClass<USnareBaseBuildComponent>() : nullptr;
}

UWaterTravelComponent* UBackpackScreenWidget::FindWater() const
{
	AActor* const Owner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
	return Owner != nullptr ? Owner->FindComponentByClass<UWaterTravelComponent>() : nullptr;
}

FName UBackpackScreenWidget::PickReleasePet(const TArray<FName>& OwnedPets, FName CurrentSelection)
{
	if (OwnedPets.Contains(CurrentSelection))
	{
		return CurrentSelection;
	}
	return OwnedPets.Num() > 0 ? OwnedPets[0] : NAME_None;
}

void UBackpackScreenWidget::PressReleasePet()
{
	AActor* const Owner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
	URanchInteractComponent* const Ranch = Owner != nullptr ? Owner->FindComponentByClass<URanchInteractComponent>() : nullptr;
	if (Ranch == nullptr)
	{
		PostStatus(LOCTEXT("BackpackReleaseNoRanch", "여기서는 펫을 풀어놓을 수 없습니다"), true);
		return;
	}
	const FName Pet = PickReleasePet(Ranch->GetOwnedPetAnimalIDs(), Ranch->GetSelectedPlacementID());
	if (Pet.IsNone() || !Ranch->SelectPlacement(Pet))
	{
		PostStatus(LOCTEXT("BackpackReleaseNoPet", "풀어놓을 펫이 없습니다"), true);
		return;
	}
	SetPanelOpen(false);
	PostStatus(FText::Format(LOCTEXT("BackpackReleaseReady", "{0} — 목장 칸 앞에서 넣으세요"), Ranch->GetPlacementDisplayName(Pet)));
}

void UBackpackScreenWidget::PressPlace()
{
	// 설치물 카드의 「놓기」 — 레퍼런스처럼 가방에서 바로 놓기 모드로 간다(제작 → 놓기 탭 우회로였다, INBOX ①).
	// 배(구명보트)는 땅 격자가 아니라 물 위에 띄운다 (DESIGN 8.9).
	if (!ShownSlots.IsValidIndex(SelectedIndex))
	{
		return;
	}
	const FName ItemID = ShownSlots[SelectedIndex].ItemID;
	const FItemRow* const Row = FindRow(ItemID);
	if (Row != nullptr && Row->PlaceableFunction == EPlaceableFunction::Boat)
	{
		UWaterTravelComponent* const Water = FindWater();
		const bool bLaunch = Water != nullptr && Water->CanLaunchBoat();
		if (bLaunch)
		{
			Water->LaunchBoat();       // 배낭 차감은 서버 `LaunchBoat` — 배는 응답 뒤에 선다 (CLAUDE.md 8)
			SetPanelOpen(false);
		}
		Status = bLaunch ? LOCTEXT("BackpackBoatLaunching", "배를 띄우는 중")
			: LOCTEXT("BackpackBoatNeedsWater", "물가에서만 배를 띄울 수 있습니다");
		PostStatus(Status, !bLaunch);
		return;
	}

	USnareBaseBuildComponent* const Build = FindBuild();
	const FText Reason = Build != nullptr ? Build->GetPlaceBlockReason(ItemID)
		: LOCTEXT("BackpackPlaceNoBuild", "놓기를 아직 쓸 수 없습니다");
	const bool bPlacing = Reason.IsEmpty() && Build->BeginPlacing(ItemID);
	if (bPlacing)
	{
		SetPanelOpen(false);       // 미리보기를 봐야 놓을 자리를 고른다
		Status = FText::GetEmpty();
	}
	else
	{
		Status = Reason.IsEmpty() ? LOCTEXT("BackpackPlaceFailed", "지금은 놓을 수 없습니다") : Reason;
	}
	PostStatus(Status, !bPlacing);
}

UTextBlock* UBackpackScreenWidget::MakeText(FName Name, const FText& Text, int32 Size, const FLinearColor& Color)
{
	UTextBlock* const Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Block->SetText(Text);
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = Size;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	return Block;
}

UButton* UBackpackScreenWidget::MakeButton(FName Name, const FText& Label, int32 Size)
{
	UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	UTextBlock* const Text = MakeText(FName(Name.ToString() + TEXT("Text")), Label, Size, SnareUiStyle::UiTextOnButton());
	Text->SetJustification(ETextJustify::Center);
	// #0199 좁은 폭(카드 버튼 4개 ≈ 66px)에서 「버리기」 같은 글이 버튼 밖으로 넘치지 않게 — 넘칠 때만 줄인다.
	UScaleBox* const Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), FName(Name.ToString() + TEXT("Fit")));
	Fit->SetStretch(EStretch::ScaleToFit);
	Fit->SetStretchDirection(EStretchDirection::DownOnly);
	Fit->AddChild(Text);
	Button->AddChild(Fit);
	return Button;
}

void UBackpackScreenWidget::BuildTree()
{
	UCanvasPanel* const Screen = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BackpackScreen"));
	WidgetTree->RootWidget = Screen;
	// 판·탭·카드는 1920×1080 시트 판(Root) 안에 두고 그 판을 화면에 놓은 뒤 키운다.
	// 렌더 변환은 누름 판정에도 들어간다(`IsUnderLocation` 이 변환된 모양으로 잰다).
	// M15-17 시트(`docs/reference/inventory_backpack.md`)는 판을 **오른쪽 가장자리**(x 1283~1595 = 폭의 66.8~83.1%)에
	// 둔다. 1920 시트 판을 화면 가운데에 놓고 묶음 가운데를 축으로 키우면 판이 폭의 50.5~76.5% 로 밀려 앉고,
	// 폰(2340 폭)에서는 판 오른쪽에 500px 빈 자리까지 남았다 → 시트 판의 오른쪽 변을 화면 오른쪽 변에 맞춘다.
	Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BackpackRoot"));
	if (UCanvasPanelSlot* const SheetSlot = Cast<UCanvasPanelSlot>(Screen->AddChild(Root)))
	{
		SheetSlot->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));
		SheetSlot->SetAlignment(FVector2D(1.f, 0.5f));
		SheetSlot->SetSize(SheetSize);
	}
	// 키우는 축: 가로는 판 오른쪽 변(키워도 가장자리에서 안 밀린다) · 세로는 묶음 가운데(화면 세로 가운데로 옮긴다).
	const FVector2D ScalePivot(SheetSize.X - PanelRightOffset, GroupCenter.Y);
	Root->SetRenderTransformPivot(ScalePivot / SheetSize);
	Root->SetRenderTransform(FWidgetTransform(FVector2D(0.f, SheetSize.Y * 0.5f - GroupCenter.Y),
		FVector2D(PhoneScale), FVector2D::ZeroVector, 0.f));

	// 왼쪽 세로 탭 — 배낭·장비·도감·제작·안내서 (DESIGN 8.12 · M28-06b2b). 새 화면 없이 있는 화면에 잇는다(M13-29).
	UVerticalBox* const Tabs = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TabColumn"));
	TabColumn = Tabs;
	Root->AddChild(Tabs);
	const FText TabNames[] = { LOCTEXT("BackpackTabBag", "배낭"), LOCTEXT("BackpackTabGear", "장비"),
		LOCTEXT("BackpackTabCodex", "도감"), LOCTEXT("BackpackTabCraft", "제작"), LOCTEXT("BackpackTabGuide", "안내서") };
	static_assert(UE_ARRAY_COUNT(TabNames) == UE_ARRAY_COUNT(TabIconPaths), "탭 이름·그림 수가 같아야 한다");
	TabFrames.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TabNames); ++Index)
	{
		// M24-01 시트 「각 52px 크림 네모 · 선택 시 노란 테두리」 — 네모(10-16 82) > 테(지금 탭만 노랑) > 크림 버튼 > 그림.
		// 글자는 툴팁으로만 남긴다(폰엔 안 보이지만 PC·시험이 읽는다).
		USizeBox* const TabBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*FString::Printf(TEXT("TabBox_%d"), Index)));
		TabBox->SetWidthOverride(TabSize);
		TabBox->SetHeightOverride(TabSize);
		UBorder* const Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*FString::Printf(TEXT("TabFrame_%d"), Index)));
		Frame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, CellRadius));
		Frame->SetBrushColor(SnareUiStyle::UiPanelCream());
		Frame->SetPadding(FMargin(TabFrameWidth));
		TabBox->AddChild(Frame);
		TabFrames.Add(Frame);
		UButton* const Tab = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*FString::Printf(TEXT("Tab_%d"), Index)));
		SnareUiStyle::MakeRoundButton(*Tab, CellRadius - TabFrameWidth);
		Tab->SetBackgroundColor(SnareUiStyle::UiPanelCream());
		Tab->SetToolTipText(TabNames[Index]);
		Frame->SetContent(Tab);
		UImage* const Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*FString::Printf(TEXT("TabIcon_%d"), Index)));
		Icon->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, TabIconPaths[Index]));
		if (UButtonSlot* const IconSlot = Cast<UButtonSlot>(Tab->SetContent(Icon)))
		{
			IconSlot->SetPadding(FMargin(TabIconPad - TabFrameWidth));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}
		if (UVerticalBoxSlot* const TabSlot = Tabs->AddChildToVerticalBox(TabBox))
		{
			TabSlot->SetPadding(FMargin(0.f, 0.f, 0.f, TabGap));
		}
		switch (Index)
		{
		case 0: Tab->OnClicked.AddDynamic(this, &UBackpackScreenWidget::ToggleBackpackTab); break;
		case 1: Tab->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressGearTab); break;
		case 2: Tab->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressCodexTab); break;
		case 3: Tab->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressCraftTab); break;
		default: Tab->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressGuideTab); break;
		}
	}
	RedrawTabs();
	SetCanvas(Tabs, FVector2D(-(PanelRightOffset + PanelWidth + TabOutset), PanelTop),
		FVector2D(TabSize, TabCount * (TabSize + TabGap)), FVector2D(0.f, 0.f));

	// 닫힌 배낭을 여는 길은 이 둥근 버튼 하나 — 탭은 열린 판 옆에만 뜬다 (M15-02, 레퍼런스 배낭 탭).
	// 감독 2026-09-25: 레퍼런스 오른쪽 아래 작은 크림 네모 아이콘(영상 Ov49JXK9pgY) — 글자 대신 배낭 아이콘.
	UButton* const OpenButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BackpackOpenButton"));
	SnareUiStyle::MakeRoundButton(*OpenButton, OpenButtonRadius);
	OpenButton->SetBackgroundColor(SnareUiStyle::UiPanelCream());
	OpenButton->SetToolTipText(LOCTEXT("BackpackOpen", "배낭"));
	OpenButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::ToggleBackpackTab);
	// 기둥 (열 0, 행 0) — 포획 버튼 바로 위, 아이콘 아래 라벨 「배낭」.
	SnareUiStyle::PlacePillarButton(*WidgetTree, *Screen, *OpenButton, LoadObject<UTexture2D>(nullptr, OpenButtonIconPath),
		LOCTEXT("BackpackOpen", "배낭"), TEXT("BackpackOpenLabel"), 0, 0);

	// 패널 — 격자 + 재화 바.
	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackpackPanel"));
	Panel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodPanel(), PanelRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));   // 나무 표찰 (M99-06)
	// 칸마다 위아래로 CellGap/2 여백이 붙으니 패널 위·바 위 여백에서 그만큼 뺀다 — 판 높이가 `PanelHeightFor` 와 맞는다.
	Panel->SetPadding(FMargin(0.f, GridTopPad - CellGap * 0.5f, 0.f, PanelBottomPad));
	Root->AddChild(Panel);
	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelColumn"));
	Panel->SetContent(Column);
	Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("SlotGrid"));
	Grid->SetSlotPadding(FMargin(CellGap * 0.5f));
	// 최소 크기는 칸 + 여백(`SlotPadding`)을 합친 값에 걸린다 — 칸만 넣으면 칸이 52 가 아니라 40~44 로 줄었다(#0203 시험에서 잼).
	Grid->SetMinDesiredSlotWidth(CellSize + CellGap);
	Grid->SetMinDesiredSlotHeight(CellSize + CellGap);
	// #0203 칸 격자는 세로 스크롤 안 — 줄이 많아도 판은 화면 안에 머문다(높이는 `Redraw` 가 보이는 줄로 맞춘다).
	SlotScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SlotScroll"));
	SlotScroll->SetOrientation(Orient_Vertical);
	// 터치 누름을 스크롤이 삼키면 칸을 못 고른다 — 아래 `NativeOnMouseButtonDown` 까지 올려 보낸다.
	SlotScroll->SetConsumePointerInput(false);
	SlotScroll->AddChild(Grid);
	SlotView = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SlotView"));
	SlotView->SetContent(SlotScroll);
	if (UVerticalBoxSlot* const GridSlot = Column->AddChildToVerticalBox(SlotView))
	{
		GridSlot->SetHorizontalAlignment(HAlign_Center);
	}
	PetText = MakeText(TEXT("PetText"), FText::GetEmpty(), 18, SnareUiStyle::UiTextOnWood());
	PetText->SetAutoWrapText(true);
	PetText->SetJustification(ETextJustify::Center);
	PetText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* const PetTextSlot = Column->AddChildToVerticalBox(PetText))
	{
		PetTextSlot->SetHorizontalAlignment(HAlign_Fill);
		PetTextSlot->SetPadding(FMargin(12.f, 8.f, 12.f, 0.f));
	}
	ReleasePetButton = MakeButton(TEXT("ReleasePetButton"), LOCTEXT("BackpackReleasePet", "펫 풀어놓기"), 18);
	ReleasePetButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressReleasePet);
	ReleasePetButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* const ReleaseSlot = Column->AddChildToVerticalBox(ReleasePetButton))
	{
		ReleaseSlot->SetHorizontalAlignment(HAlign_Center);
		ReleaseSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}
	// 레퍼런스 옷장 화면(`steam_06.jpg`)으로 가는 문 — 상점은 옷장 화면 안 「상점 열기」 에서 (`skin-parts.md` 5장 5).
	WardrobeButton = MakeButton(TEXT("WardrobeButton"), LOCTEXT("BackpackWardrobe", "옷장"), 18);
	WardrobeButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressWardrobe);
	WardrobeButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* const ShopSlot = Column->AddChildToVerticalBox(WardrobeButton))
	{
		ShopSlot->SetHorizontalAlignment(HAlign_Center);
		ShopSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}
	// 뽑기(올가미 기계) 입구 — 경품이 꾸미기라 옷장 바로 아래. 젬이 꺼진 빌드도 코인 1회 뽑기가 있어 늘 보인다.
	GachaButton = MakeButton(TEXT("GachaButton"), LOCTEXT("BackpackGacha", "올가미 기계"), 18);
	GachaButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressGacha);
	GachaButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* const GachaSlot = Column->AddChildToVerticalBox(GachaButton))
	{
		GachaSlot->SetHorizontalAlignment(HAlign_Center);
		GachaSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}
	UBorder* const Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BalanceBar"));
	Bar->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonBase(), BarHeight * 0.5f));
	Bar->SetVerticalAlignment(VAlign_Center);
	Bar->SetPadding(FMargin(16.f, 0.f));
	BalanceText = MakeText(TEXT("BalanceText"), FText::GetEmpty(), 20, SnareUiStyle::UiTextOnButton());
	// 시트 「하단 바」 — 왼쪽 재화 아이콘(초록 지폐 28px) · 숫자 · 오른쪽 끝 `>` (M15-25).
	UHorizontalBox* const BarRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BalanceBarRow"));
	UImage* const CurrencyIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BalanceIcon"));
	CurrencyIcon->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, BalanceIconPath));
	USizeBox* const IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BalanceIconBox"));
	IconBox->SetWidthOverride(BalanceIconSize);
	IconBox->SetHeightOverride(BalanceIconSize);
	// 오류(`서버 연결 안 됨`)일 땐 돈 그림 대신 빨간 동그라미 `!` — 재화 숫자처럼 읽히지 않게 (10-16 HUD 검수 ③).
	UBorder* const WarnBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BalanceWarnBadge"));
	WarnBadge->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiAccentRed(), BalanceIconSize * 0.5f));
	WarnBadge->SetHorizontalAlignment(HAlign_Center);
	WarnBadge->SetVerticalAlignment(VAlign_Center);
	WarnBadge->SetPadding(FMargin(0.f));
	WarnBadge->SetContent(MakeText(TEXT("BalanceWarnText"), FText::FromString(TEXT("!")), 20, SnareUiStyle::UiPanelCream()));
	WarnBadge->SetVisibility(ESlateVisibility::Collapsed);
	UOverlay* const IconStack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("BalanceIconStack"));
	for (UWidget* const Layer : { static_cast<UWidget*>(CurrencyIcon), static_cast<UWidget*>(WarnBadge) })
	{
		if (UOverlaySlot* const LayerSlot = IconStack->AddChildToOverlay(Layer))
		{
			LayerSlot->SetHorizontalAlignment(HAlign_Fill);
			LayerSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	IconBox->SetContent(IconStack);
	if (UHorizontalBoxSlot* const IconSlot = BarRow->AddChildToHorizontalBox(IconBox))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	}
	// 「서버 연결 안 됨」 은 칩 안 자리(≈122px)보다 길다(글 156px, 10-13 캡처에서 끝 글자가 칩 밖으로 넘침) — 넘칠 때만 줄인다.
	// 칩을 넓히면 왼쪽 「배낭」 제목 탭을 가린다(판 폭 한계).
	UScaleBox* const TextFit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("BalanceTextFit"));
	TextFit->SetStretch(EStretch::ScaleToFit);
	TextFit->SetStretchDirection(EStretchDirection::DownOnly);
	TextFit->AddChild(BalanceText);
	if (UHorizontalBoxSlot* const TextSlot = BarRow->AddChildToHorizontalBox(TextFit))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetHorizontalAlignment(HAlign_Left);
	}
	Bar->SetContent(BarRow);
	USizeBox* const BarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BalanceBarBox"));
	BarBox->SetMinDesiredWidth(BarWidth);   // 최소 폭 — 닫힌 칩은 글 길이만큼 늘어난다
	BarBox->SetHeightOverride(BarHeight);
	BarBox->SetContent(Bar);
	BalanceChip = BarBox;
	// 스토어 스크린샷용 — `-SnareHideBalanceChip` 이면 코인 칩을 접는다(오프라인 캡처의 「서버 연결 안 됨」 가리기, 잔액 값은 안 건드림).
	if (SnareCheckSwitch::Param(TEXT("SnareHideBalanceChip")))
	{
		BalanceChip->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 제목 탭 — 패널 왼쪽 위에 걸쳐 위로 튀어나온다(패널 **뒤에** 넣어야 위에 그려진다).
	TitlePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitlePill"));
	TitlePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodButton(), PillHeight * 0.5f, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	TitlePill->SetHorizontalAlignment(HAlign_Center);
	TitlePill->SetVerticalAlignment(VAlign_Center);
	TitlePill->SetContent(MakeText(TEXT("TitleText"), LOCTEXT("BackpackTitle", "배낭"), 20, FLinearColor::White));
	Root->AddChild(TitlePill);
	SetCanvas(TitlePill, FVector2D(-(PanelRightOffset + PanelWidth), PanelTop - PillRise),
		FVector2D(PillWidth, PillHeight), FVector2D(0.f, 0.f));
	// 코인 칩 — 머리 탭과 같은 줄, 판 오른쪽 끝에 맞춘다.
	Root->AddChild(BarBox);
	SetCanvas(BarBox, FVector2D(-PanelRightOffset, PanelTop - PillRise + (PillHeight - BarHeight) * 0.5f),
		FVector2D(BarWidth, BarHeight), FVector2D(1.f, 0.f));

	// 「닫기」 — 다른 메뉴 판과 같은 단추(10-16 「가방만 닫기가 없다」). 판 오른쪽 위, 코인 칩 위에 붙는다 — 판 아래는
	// 펫 줄이 늘면 화면 밖으로 나간다. 뿌리가 1.6배라 크기는 1080 기준 값 / PhoneScale.
	UButton* Close = nullptr;
	CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("BackpackCloseBox"), TEXT("BackpackCloseButton"),
		LOCTEXT("BackpackClose", "닫기"), Close);
	const FVector2D CloseSize(SnareUiStyle::MenuTouchMin * 2.f / PhoneScale, SnareUiStyle::MenuTouchMin / PhoneScale);
	CloseBox->SetHeightOverride(CloseSize.Y);
	CloseBox->SetMinDesiredWidth(CloseSize.X);
	if (UTextBlock* const CloseText = Cast<UTextBlock>(Close->GetContent()))
	{
		SnareUiStyle::SetFontSize(*CloseText, (SnareUiStyle::MenuFontMin + 4.f) / PhoneScale);
	}
	Close->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressClose);
	Root->AddChild(CloseBox);
	SetCanvas(CloseBox, FVector2D(-PanelRightOffset, PanelTop - PillRise - CellGap), CloseSize, FVector2D(1.f, 1.f));
	CloseBox->SetVisibility(bPanelOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// 칸 카드 — 이름·등급·개수 + 사용/장착/놓기/버리기 (DESIGN 8.12 "칸 누르기").
	Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotCard"));
	// M100-03 판 쌍 대신 말풍선형 나무 표찰 — 모서리를 각지게(표찰) 하고 오른쪽 꼬리(눕힌 네모)가 칸 판을 가리킨다.
	Card->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodPanel(), SnareUiStyle::WoodPlateRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	Card->SetPadding(FMargin(16.f, 12.f));
	Root->AddChild(Card);
	CardTail = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardTail"));
	CardTail->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodPanel(), 2.f, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	CardTail->SetRenderTransformAngle(45.f);   // 네모를 눕혀 마름모 — 말풍선 꼬리
	CardTail->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	CardTail->SetVisibility(ESlateVisibility::Collapsed);
	Root->AddChild(CardTail);
	UVerticalBox* const CardColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardColumn"));
	Card->SetContent(CardColumn);
	CardNameText = MakeText(TEXT("CardNameText"), FText::GetEmpty(), 20, SnareUiStyle::UiTextOnWood());
	CardInfoText = MakeText(TEXT("CardInfoText"), FText::GetEmpty(), 14, SnareUiStyle::UiTextOnWood());
	// M15-09 (INBOX ③) 꺼진 버튼의 까닭 — 폰에는 툴팁이 안 보여 카드 안 글로 말한다. 누를 것이 있으면 접힌다.
	CardHintText = MakeText(TEXT("CardHintText"), FText::GetEmpty(), CardHintFontSize, SnareUiStyle::UiTextOnWood());
	CardHintText->SetAutoWrapText(true);      // 카드 폭(312 − 여백 32)을 넘는 까닭은 둘째 줄로 내린다
	CardColumn->AddChildToVerticalBox(CardNameText);
	CardColumn->AddChildToVerticalBox(CardInfoText);
	if (UVerticalBoxSlot* const HintSlot = CardColumn->AddChildToVerticalBox(CardHintText))
	{
		HintSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}
	UHorizontalBox* const Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardButtons"));
	if (UVerticalBoxSlot* const ButtonsSlot = CardColumn->AddChildToVerticalBox(Buttons))
	{
		ButtonsSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}
	UseButton = MakeButton(TEXT("UseButton"), LOCTEXT("BackpackUse", "사용"), 16);
	EquipButton = MakeButton(TEXT("EquipButton"), LOCTEXT("BackpackEquip", "장착"), 16);
	PlaceButton = MakeButton(TEXT("PlaceButton"), LOCTEXT("BackpackPlace", "놓기"), 16);
	DropButton = MakeButton(TEXT("DropButton"), LOCTEXT("BackpackDrop", "버리기"), 16);
	for (UButton* const Button : { UseButton.Get(), EquipButton.Get(), PlaceButton.Get(), DropButton.Get() })
	{
		if (UHorizontalBoxSlot* const ButtonSlot = Buttons->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ButtonSlot->SetPadding(FMargin(2.f, 0.f));
		}
	}
	UseButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressUse);
	EquipButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressEquip);
	DropButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressDrop);
	PlaceButton->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressPlace);

	// 「다시 시도」 — 버튼 줄 아래, 서버에 닿지 못한 요청이 있을 때만. 크기는 닫기와 같이 1080 기준 / PhoneScale.
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("BackpackRetryBox"), TEXT("BackpackRetryButton"),
		LOCTEXT("BackpackRetry", "다시 시도"), Retry);
	RetryBox->SetHeightOverride(SnareUiStyle::MenuTouchMin / PhoneScale);
	RetryBox->SetMinDesiredWidth(SnareUiStyle::MenuTouchMin * 2.f / PhoneScale);
	if (UTextBlock* const RetryText = Cast<UTextBlock>(Retry->GetContent()))
	{
		SnareUiStyle::SetFontSize(*RetryText, (SnareUiStyle::MenuFontMin + 4.f) / PhoneScale);
	}
	Retry->OnClicked.AddDynamic(this, &UBackpackScreenWidget::PressRetry);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* const RetrySlot = CardColumn->AddChildToVerticalBox(RetryBox))
	{
		RetrySlot->SetHorizontalAlignment(HAlign_Left);
		RetrySlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	}
}

void UBackpackScreenWidget::Redraw()
{
	const TArray<FInventorySlot> Empty;
	const TArray<FInventorySlot>& Owned = IsValid(Backend) ? Backend->GetPlayerSnapshot().InventorySlots : Empty;
	const int32 PrevIndex = SelectedIndex;
	const FName SelectedItem = ShownSlots.IsValidIndex(SelectedIndex) ? ShownSlots[SelectedIndex].ItemID : NAME_None;
	ShownSlots = BuildVisibleSlots(Owned, ItemTable, StackMax);
	if (bGearOnly)
	{
		// 장비 보기 — 장착할 수 있는 것만 (DESIGN 8.12 🟡 장비 탭). 칸 수는 배낭 그대로.
		ShownSlots.RemoveAll([this](const FInventorySlot& InvSlot)
		{
			const FItemRow* const Row = FindRow(InvSlot.ItemID);
			return Row == nullptr || Row->EquipSlot == EEquipSlot::None;
		});
	}
	// M35-06 가챠 경품 — 배낭 칸 뒤에 이어 붙인다. 칸 수(`Capacity`)에는 안 들어가 그만큼 칸을 더 그린다(서버 `isBackpackItem`).
	GachaSlotStart = ShownSlots.Num();
	GachaSlotCount = 0;
	if (!bGearOnly)
	{
		const TArray<FInventorySlot> GachaSlots = BuildGachaSlots(Owned, GachaPoolTable, StackMax);
		GachaSlotCount = GachaSlots.Num();
		ShownSlots.Append(GachaSlots);
	}
	Capacity = ComputeCapacity(Owned, ItemTable, GetDefault<UInventoryComponent>()->GetSlotCount());
	// 고른 칸이 아직 같은 아이템이면 그 칸 그대로(M13-22 — 같은 아이템 두 칸의 뒤 칸이 앞 칸으로 옮겨 갔다).
	// 아니면 같은 아이템을 따라가고, 다 버렸으면 카드를 닫는다.
	SelectedIndex = SelectedItem.IsNone() ? INDEX_NONE
		: (ShownSlots.IsValidIndex(PrevIndex) && ShownSlots[PrevIndex].ItemID == SelectedItem) ? PrevIndex
		: ShownSlots.IndexOfByPredicate([SelectedItem](const FInventorySlot& InvSlot) { return InvSlot.ItemID == SelectedItem; });

	if (!IsValid(Grid) || WidgetTree == nullptr)
	{
		return;
	}

	Grid->ClearChildren();
	Cells.Reset();
	// 등록 대기 펫 — 아직 백엔드가 못 받은 포획(오프라인·거절)도 격자 안 칸으로 보인다 (M17-09, 폰 확인 09-29).
	PendingPetSlots.Reset();
	if (!bGearOnly)
	{
		AActor* const PendingOwner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
		if (const UInventoryComponent* const Pending = PendingOwner != nullptr ? PendingOwner->FindComponentByClass<UInventoryComponent>() : nullptr)
		{
			for (const FInventorySlot& Entry : Pending->GetPendingGrants())
			{
				if (Entry.ItemID.ToString().StartsWith(TEXT("item.pet_")))
				{
					PendingPetSlots.Add(Entry);
				}
			}
		}
	}
	// 서버보다 많이 가졌으면(넘친 배낭) 칸을 늘려 전부 보여 준다 — 숨기면 가진 것이 사라진 것처럼 보인다.
	const int32 CellCount = FMath::Max(Capacity + GachaSlotCount, ShownSlots.Num() + PendingPetSlots.Num());
	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		UBorder* const Cell = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*FString::Printf(TEXT("Cell_%d"), Index)));
		Cell->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiSlotWell(), WellRadius, SnareUiStyle::UiWoodButton(), 2.f));   // 어두운 나무 홈 + 나무 테두리 (M101-02)
		Cell->SetPadding(FMargin(0.f));
		Cell->SetVisibility(ESlateVisibility::Visible);
		if (ShownSlots.IsValidIndex(Index))
		{
			const FInventorySlot& Item = ShownSlots[Index];
			const FItemRow* const Row = FindRow(Item.ItemID);
			const bool bGacha = Index >= GachaSlotStart && Index < GachaSlotStart + GachaSlotCount;
			const FGachaPoolRow* const PoolRow = bGacha ? FindGachaPoolRow(GachaPoolTable, Item.ItemID) : nullptr;
			UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
			// 색 원판 = 등급 (시트 · DESIGN 8.12 등급 색). 고른 칸은 강조 테두리.
			// 가챠 소재 5종은 아이템 표 행이 없어 공개 문서 표(`DT_GachaPool`) 등급으로 칠한다 — 두 등급 차례가 같다.
			UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				FName(*FString::Printf(TEXT("CellDisc_%d"), Index)));
			const FLinearColor GradeColor = SnareUiStyle::ItemGradeBandColor(Row != nullptr ? Row->Grade
				: PoolRow != nullptr ? static_cast<EItemGrade>(PoolRow->Grade) : EItemGrade::Common);
			const bool bWorn = IsItemWorn(Item.ItemID);
			// 칸 틀 — 속이 빈 테두리(고른 칸 노랑·착용 칸 갈색). 등급은 동그라미가 아니라 아래 띠로 보인다 (M101-04).
			Disc->SetBrush(Index == SelectedIndex
				? FSlateRoundedBoxBrush(FLinearColor::Transparent, WellRadius, SnareUiStyle::UiButtonHighlight(), 3.f)
				: bWorn ? FSlateRoundedBoxBrush(FLinearColor::Transparent, WellRadius, SnareUiStyle::UiButtonBase(), 3.f)
				: FSlateRoundedBoxBrush(FLinearColor::Transparent, WellRadius));
			// 빈 Border 는 원하는 크기가 0 이다 — 칸을 꽉 채우게 펴야 틀이 보인다(오버레이 기본 정렬은 왼쪽 위).
			if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(Disc))
			{
				DiscSlot->SetHorizontalAlignment(HAlign_Fill);
				DiscSlot->SetVerticalAlignment(VAlign_Fill);
			}
			SnareUiStyle::AddGradeBand(*WidgetTree, *Stack, FName(*FString::Printf(TEXT("CellGrade_%d"), Index)), GradeColor, CellSize);
			if (Row != nullptr && !Row->IconRef.IsNull())
			{
				UImage* const Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
					FName(*FString::Printf(TEXT("CellIcon_%d"), Index)));
				Icon->SetBrushFromSoftTexture(Row->IconRef);
				if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Icon))
				{
					IconSlot->SetPadding(FMargin(6.f));
					IconSlot->SetHorizontalAlignment(HAlign_Fill);
					IconSlot->SetVerticalAlignment(VAlign_Fill);
				}
			}
			if (bGacha)
			{
				// 그림이 없는 경품(소재)은 무엇인지 이름 한 줄(아래) — 대기 펫 칸과 같은 모양.
				if (Row == nullptr || Row->IconRef.IsNull())
				{
					UTextBlock* const Name = MakeText(FName(*FString::Printf(TEXT("CellGachaName_%d"), Index)),
						PoolRow != nullptr ? PoolRow->DisplayName : FText::FromName(Item.ItemID), 12, SnareUiStyle::UiTextOnButton());
					Name->SetAutoWrapText(true);
					Name->SetJustification(ETextJustify::Center);
					if (UOverlaySlot* const NameSlot = Stack->AddChildToOverlay(Name))
					{
						NameSlot->SetHorizontalAlignment(HAlign_Center);
						NameSlot->SetVerticalAlignment(VAlign_Center);
						NameSlot->SetPadding(FMargin(2.f));
					}
				}
				// 칸 수에 안 드는 칸이라는 표시 — 왼쪽 위 `가챠` 딱지(`착용`·`대기` 딱지와 같은 모양).
				UBorder* const Tag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
					FName(*FString::Printf(TEXT("CellGachaTag_%d"), Index)));
				Tag->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonBase(), 4.f));
				Tag->SetPadding(FMargin(4.f, 0.f));
				Tag->SetContent(MakeText(FName(*FString::Printf(TEXT("CellGachaTagText_%d"), Index)),
					LOCTEXT("BackpackGachaTag", "가챠"), 12, SnareUiStyle::UiTextOnButton()));
				if (UOverlaySlot* const TagSlot = Stack->AddChildToOverlay(Tag))
				{
					TagSlot->SetHorizontalAlignment(HAlign_Left);
					TagSlot->SetVerticalAlignment(VAlign_Top);
				}
			}
			if (Item.Count > 1)
			{
				UTextBlock* const Count = MakeText(FName(*FString::Printf(TEXT("CellCount_%d"), Index)),
					FText::AsNumber(Item.Count), 14, SnareUiStyle::UiTextOnButton());
				if (UOverlaySlot* const CountSlot = Stack->AddChildToOverlay(Count))
				{
					CountSlot->SetHorizontalAlignment(HAlign_Right);
					CountSlot->SetVerticalAlignment(VAlign_Bottom);
					CountSlot->SetPadding(FMargin(0.f, 0.f, 2.f, 0.f));
				}
			}
			if (bWorn)
			{
				// 지금 든 것 — 칸 왼쪽 위에 `착용` 딱지 (INBOX 2026-09-29 ②-b).
				UBorder* const Tag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
					FName(*FString::Printf(TEXT("CellWornTag_%d"), Index)));
				Tag->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonBase(), 4.f));
				Tag->SetPadding(FMargin(4.f, 0.f));
				Tag->SetContent(MakeText(FName(*FString::Printf(TEXT("CellWornText_%d"), Index)),
					LOCTEXT("BackpackWornTag", "착용"), 12, SnareUiStyle::UiTextOnButton()));
				if (UOverlaySlot* const TagSlot = Stack->AddChildToOverlay(Tag))
				{
					TagSlot->SetHorizontalAlignment(HAlign_Left);
					TagSlot->SetVerticalAlignment(VAlign_Top);
				}
			}
			Cell->SetContent(Stack);
		}
		else if (PendingPetSlots.IsValidIndex(Index - ShownSlots.Num()))
		{
			const FInventorySlot& Item = PendingPetSlots[Index - ShownSlots.Num()];
			const FItemRow* const Row = FindRow(Item.ItemID);
			UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
			UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				FName(*FString::Printf(TEXT("CellDisc_%d"), Index)));
			Disc->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, WellRadius));
			if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(Disc))
			{
				DiscSlot->SetHorizontalAlignment(HAlign_Fill);
				DiscSlot->SetVerticalAlignment(VAlign_Fill);
			}
			SnareUiStyle::AddGradeBand(*WidgetTree, *Stack, FName(*FString::Printf(TEXT("CellGrade_%d"), Index)),
				SnareUiStyle::ItemGradeBandColor(Row != nullptr ? Row->Grade : EItemGrade::Common), CellSize);
			if (Row != nullptr && !Row->IconRef.IsNull())
			{
				UImage* const Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
					FName(*FString::Printf(TEXT("CellIcon_%d"), Index)));
				Icon->SetBrushFromSoftTexture(Row->IconRef);
				if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Icon))
				{
					IconSlot->SetPadding(FMargin(6.f));
					IconSlot->SetHorizontalAlignment(HAlign_Fill);
					IconSlot->SetVerticalAlignment(VAlign_Fill);
				}
			}
			// 이름 한 줄(아래) + `대기` 딱지(왼쪽 위) — 이름이 없으면 무슨 펫인지 모른다.
			UTextBlock* const Name = MakeText(FName(*FString::Printf(TEXT("CellPendingName_%d"), Index)),
				Row != nullptr ? Row->DisplayName : FText::FromName(Item.ItemID), 12, SnareUiStyle::UiTextOnButton());
			if (UOverlaySlot* const NameSlot = Stack->AddChildToOverlay(Name))
			{
				NameSlot->SetHorizontalAlignment(HAlign_Center);
				NameSlot->SetVerticalAlignment(VAlign_Bottom);
			}
			UBorder* const Tag = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
				FName(*FString::Printf(TEXT("CellPendingTag_%d"), Index)));
			Tag->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiButtonBase(), 4.f));
			Tag->SetPadding(FMargin(4.f, 0.f));
			Tag->SetContent(MakeText(FName(*FString::Printf(TEXT("CellPendingTagText_%d"), Index)),
				LOCTEXT("BackpackPendingTag", "대기"), 12, SnareUiStyle::UiTextOnButton()));
			if (UOverlaySlot* const TagSlot = Stack->AddChildToOverlay(Tag))
			{
				TagSlot->SetHorizontalAlignment(HAlign_Left);
				TagSlot->SetVerticalAlignment(VAlign_Top);
			}
			Cell->SetContent(Stack);
		}
		// 격자 칸도 기본 정렬이 왼쪽 위라 빈 칸이 0x0 으로 사라진다(3층 캡처에서 칸이 하나도 안 보였다).
		if (UUniformGridSlot* const CellSlot = Grid->AddChildToUniformGrid(Cell, Index / GridColumns, Index % GridColumns))
		{
			CellSlot->SetHorizontalAlignment(HAlign_Fill);
			CellSlot->SetVerticalAlignment(VAlign_Fill);
		}
		Cells.Add(Cell);
	}

	// 판은 보이는 줄까지만 자란다 — 나머지 줄은 스크롤 (#0203).
	const int32 Rows = FMath::Min(FMath::DivideAndRoundUp(CellCount, GridColumns), MaxVisibleRows);
	SlotView->SetHeightOverride(Rows * (CellSize + CellGap) + ScrollBottomPad);
	SetCanvas(Card, FVector2D(-(PanelRightOffset + PanelWidth + CardRightInset), CardTop),
		FVector2D(PanelWidth, CardHeight), FVector2D(1.f, 0.f));
	SetCanvas(CardTail, FVector2D(-(PanelRightOffset + PanelWidth + CardRightInset - CardTailSize / 2.f), CardTop + CardTailTop),
		FVector2D(CardTailSize, CardTailSize), FVector2D(1.f, 0.f));

	// 재화 바 — 한 줄로 (`SnareBalanceHud` 와 같은 글, 못 들었으면 `-`).
	// 포획한 펫 — 격자에는 안 보이니 격자 아래 한 줄로 센다. 대기 펫은 오프라인·백엔드 거절로 아직 등록 안 된 것.
	int32 PendingPets = 0;
	AActor* const PetOwner = IsValid(Backpack) ? Backpack->GetOwner() : nullptr;
	if (const UInventoryComponent* const Pending = PetOwner != nullptr ? PetOwner->FindComponentByClass<UInventoryComponent>() : nullptr)
	{
		for (const FInventorySlot& Entry : Pending->GetPendingGrants())
		{
			if (Entry.ItemID.ToString().StartsWith(TEXT("item.pet_")))
			{
				PendingPets += Entry.Count;
			}
		}
	}
	const FText PetLine = FormatPetSummary(Owned, PendingPets);
	BalanceText->SetText(FText::FromString(SnareBalanceHud::FormatBalanceText(Backend).ToString().Replace(TEXT("\n"), TEXT("   "))));
	const bool bBalanceOffline = SnareBalanceHud::IsBalanceOffline(Backend);
	if (UWidget* const CoinIcon = WidgetTree->FindWidget(TEXT("BalanceIcon")))
	{
		CoinIcon->SetVisibility(bBalanceOffline ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (UWidget* const WarnBadge = WidgetTree->FindWidget(TEXT("BalanceWarnBadge")))
	{
		WarnBadge->SetVisibility(bBalanceOffline ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	PetText->SetText(PetLine);
	PetText->SetVisibility(PetLine.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	float PetExtra = PetLine.IsEmpty() ? 0.f : PetLineExtraHeight;
	// 가진 펫이 있을 때만 `풀어놓기` — 없으면 접어 자리를 안 먹는다.
	if (IsValid(ReleasePetButton))
	{
		const URanchInteractComponent* const RanchInteract = PetOwner != nullptr ? PetOwner->FindComponentByClass<URanchInteractComponent>() : nullptr;
		const bool bHasPet = RanchInteract != nullptr && RanchInteract->GetOwnedPetAnimalIDs().Num() > 0;
		ReleasePetButton->SetVisibility(bHasPet ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		PetExtra += bHasPet ? ReleaseButtonExtraHeight : 0.f;
	}
	if (IsValid(WardrobeButton))
	{
		WardrobeButton->SetVisibility(bGearOnly ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		PetExtra += bGearOnly ? ReleaseButtonExtraHeight : 0.f;   // 버튼 높이가 `풀어놓기` 와 같다
	}
	if (IsValid(GachaButton))
	{
		GachaButton->SetVisibility(bGearOnly ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		PetExtra += bGearOnly ? ReleaseButtonExtraHeight : 0.f;
	}
	SetCanvas(Panel, FVector2D(-PanelRightOffset, PanelTop), FVector2D(PanelWidth, PanelHeightFor(Rows, PetExtra)), FVector2D(1.f, 0.f));
	RedrawCard();
	SnareUiStyle::ApplyUiStyle(*this);
}

void UBackpackScreenWidget::RedrawCard()
{
	if (!IsValid(Card))
	{
		return;
	}
	WatchRanchSync();
	const bool bShow = bPanelOpen && ShownSlots.IsValidIndex(SelectedIndex);
	Card->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (IsValid(CardTail))
	{
		CardTail->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bShow)
	{
		return;
	}
	const FInventorySlot& Item = ShownSlots[SelectedIndex];
	const FItemRow* const Row = FindRow(Item.ItemID);
	const bool bGacha = SelectedIndex >= GachaSlotStart && SelectedIndex < GachaSlotStart + GachaSlotCount;
	const FGachaPoolRow* const PoolRow = bGacha ? FindGachaPoolRow(GachaPoolTable, Item.ItemID) : nullptr;
	const FText CardName = Row != nullptr ? Row->DisplayName
		: PoolRow != nullptr ? PoolRow->DisplayName : FText::FromName(Item.ItemID);
	CardNameText->SetText(IsItemWorn(Item.ItemID)
		? FText::Format(LOCTEXT("BackpackCardNameWorn", "{0} (착용중)"), CardName) : CardName);
	CardInfoText->SetText(FText::Format(LOCTEXT("BackpackCardInfo", "{0} - {1}개"),
		GradeName(Row != nullptr ? Row->Grade : PoolRow != nullptr ? static_cast<EItemGrade>(PoolRow->Grade) : EItemGrade::Common),
		FText::AsNumber(Item.Count)));
	// 쓸 수 있는 버튼만 켠다 — 먹기는 회복량(음수 = 독, 날로 먹는 작물 13.3·광대버섯 13.4)이 있거나 채울 수 있는 것(빈 병 13.6),
	// 장착은 장비, 놓기는 설치물(거점 조각 전까지 꺼 둔다).
	// 회복 없이 버프만 있는 것(핫팩·순한맛 13.6)도 먹는다.
	// 보물 지도(13.8)·꾸미기 상자(10.8)도 "사용" 이다.
	// 목장 데코(M35-05d)는 사용 칸이 「목장 적용/적용 해제」 — 목장 전송기가 있고 다른 목장 요청이 안 돌 때만.
	const bool bDeco = Row != nullptr && Row->ItemType == EItemType::RanchDeco;
	UseButton->SetIsEnabled(bDeco
		? IsValid(RanchSync) && !RanchSync->IsUpkeepActionInFlight()
			&& !RanchSync->IsFunctionUnavailable(URanchSyncComponent::GetSetRanchDecoFunctionName())
		: Item.ItemID == SnareChestLoot::GetTreasureMapItemID()
		|| UCrateScreenWidget::IsCrateItem(GetDefault<UCrateScreenWidget>()->GetCrateTable(), Item.ItemID) || (Row != nullptr
		&& (Row->HealAmount != 0 || Row->BuffKind != EBuffKind::None || !Row->FilledItemID.IsNone())));
	if (UTextBlock* const UseText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("UseButtonText"))))
	{
		UseText->SetText(!bDeco ? LOCTEXT("BackpackUse", "사용")
			: IsSelectedDecoApplied() ? LOCTEXT("BackpackDecoClear", "적용 해제") : LOCTEXT("BackpackDecoApply", "목장 적용"));
	}
	// 소지만으로 효과가 나는 도구(위성 통신기)는 장착할 것이 없다 — 버튼 자리에 `소지 중` 만 보인다.
	const bool bCarriedOnly = Row != nullptr && Row->bPassiveWhileCarried;
	EquipButton->SetIsEnabled(Row != nullptr && Row->ItemType == EItemType::Equipment && !bCarriedOnly);
	if (UTextBlock* const EquipText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("EquipButtonText"))))
	{
		EquipText->SetText(bCarriedOnly ? LOCTEXT("BackpackCarried", "소지 중")
			: IsSelectedWornHat() ? LOCTEXT("BackpackUnequip", "벗기")
			: IsSelectedEquippedGear() ? LOCTEXT("BackpackGearUnequip", "내리기")
			: LOCTEXT("BackpackEquip", "장착"));
	}
	// 놓기 = 설치형(가구·불·보관함·텐트·배). 못 놓는 까닭(텐트 먼저·거점존 아님·물 위)은 선 자리에 따라
	// 바뀌어 카드를 다시 그릴 때가 아니라 **누른 때** 판단해 한 줄로 말한다 (INBOX 2026-09-25 ①).
	// 배(구명보트)는 땅 격자 칸이 0 이다 — 물 위에 띄우므로 발자국을 안 본다 (DESIGN 8.9).
	PlaceButton->SetIsEnabled(Row != nullptr && Row->ItemType == EItemType::Placeable
		&& (Row->FootprintX > 0 || Row->PlaceableFunction == EPlaceableFunction::Boat));
	// 가챠 경품은 서버가 버리기를 거절한다(`DropItem` → `protected`) — 누를 수 없게 끈다.
	DropButton->SetIsEnabled(!bGacha);
	// M15-09 (INBOX ③) 버릴 수밖에 없는 칸이면 왜 그런지 한 줄로 — 폰에는 꺼진 버튼의 툴팁이 안 보인다.
	// 버튼 상태를 그대로 읽어 판단한다(종류를 다시 따지면 위 조건과 조용히 어긋난다).
	if (IsValid(CardHintText))
	{
		const bool bAnyAction = UseButton->GetIsEnabled() || EquipButton->GetIsEnabled() || PlaceButton->GetIsEnabled();
		// 가챠 소재 5종은 아이템 표 행이 없다 — 서버 제작이 대체 재료로 받는다(M35-03 `GACHA_SUBSTITUTES`).
		const FText Hint = RetryRequest ? Status   // 서버에 닿지 못한 까닭 — 「다시 시도」 바로 위
			: bAnyAction ? FText::GetEmpty()
			: (bGacha && Row == nullptr) ? LOCTEXT("BackpackHintGachaMaterial", "가챠 소재 - 제작에 씁니다 (버릴 수 없음)")
			: MakeDisabledHint(Row, FindRecipeTable());
		CardHintText->SetText(Hint);
		CardHintText->SetVisibility(Hint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(RetryRequest ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

TSharedRef<SWidget> UBackpackScreenWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	Redraw();
	SetPanelOpen(bPanelOpen);
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}

FReply UBackpackScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 칸은 버튼이 아니라 테두리라 누른 자리를 여기서 찾는다(터치도 이 길로 온다).
	// 스크롤 밖(재화 바 등)을 누르면 가려진 칸이 잡히지 않게 스크롤 영역 안일 때만 찾는다 (#0203).
	const bool bInScroll = IsValid(SlotScroll)
		&& SlotScroll->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition());
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (bInScroll && IsValid(Cells[Index]) && Cells[Index]->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
		{
			// 누름에서 고르면 터치로 스크롤을 시작한 칸이 먼저 골라진다 — 뗄 때 고른다 (M13-20).
			BeginSlotPress(Index, InMouseEvent.GetScreenSpacePosition());
			return FReply::Handled();
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UBackpackScreenWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 스크롤이 문턱을 넘겨 포인터를 잡았으면 뗌은 여기 오지 않는다 — 다음 누름이 기억을 덮는다.
	return EndSlotPress(InMouseEvent.GetScreenSpacePosition())
		? FReply::Handled()
		: Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UBackpackScreenWidget::BeginSlotPress(int32 Index, const FVector2D& ScreenPos)
{
	PressedIndex = Index;
	PressedAt = ScreenPos;
}

bool UBackpackScreenWidget::EndSlotPress(const FVector2D& ScreenPos)
{
	const int32 Index = PressedIndex;
	PressedIndex = INDEX_NONE;
	if (Index == INDEX_NONE || FVector2D::Distance(PressedAt, ScreenPos) > GetTapSlopPx())
	{
		return false;
	}
	SelectSlot(Index);
	Redraw();
	return true;
}

float UBackpackScreenWidget::GetTapSlopPx()
{
	// 엔진 기본(1mm, 최소 5px · 모바일 10px)이라 Slate 가 없을 땐 그 최소값 (DESIGN 8.12).
	return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetDragTriggerDistance() : 5.f;
}

#undef LOCTEXT_NAMESPACE
