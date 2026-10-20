#include "UI/ShopScreenWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Blueprint/WidgetTree.h"
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
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/AnimalRow.h"
#include "Data/GachaPoolRow.h"
#include "Data/ItemRow.h"
#include "Data/ShopRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/SnareBalanceHud.h"
#include "SnareCheckSwitch.h"
#include "UI/SnareClickProxy.h"
#include "UI/SnarePromptCardWidget.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"
#include "World/SnareSweetPotato.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// M15-18 화면 배치 — 레퍼런스 시트 `docs/reference/shop_vendor.md`(1920×1080) 값을 폰 배율로 키운 것.
	// 게임 규칙 숫자가 아니다(밸런스 표에 안 적는다) — "화면 어디에 얼마나 크게" 다.
	// 판을 배낭과 **같은 배율**로 키운다 (감독 2026-09-25 "아이템 창이 작다" — 한 창만 키우면 짝이 안 맞는다).
	constexpr float PhoneScale = 1.6f;
	/** 시트 px(1920 기준) → 화면 px. 배낭은 판 전체에 렌더 배율을 걸지만, 상점 판은 **폭이 늘었다 줄었다**
	 *  해야 해서(아래 `BuildPlaceholderTree`) 렌더 배율 대신 값마다 배율을 먹인다. */
	constexpr float Px(float Sheet) { return Sheet * PhoneScale; }

	const FVector2D SheetSize(1920.f, 1080.f);
	constexpr float PanelWidthSheet = 600.f;   // 시트 x 630~1230
	/** 시트는 600(y 240~840)이지만 540 으로 줄인다 — 판 아래 끝이 왼쪽 아래 체력 알약(시트 y 990~1040)을 덮어
	 *  반쯤 가렸다(M82-04). 세로는 계속 한가운데라 위아래가 30씩 줄고, 거래 목록은 스크롤이라 잃는 것이 없다. */
	constexpr float PanelHeight = Px(540.f);
	constexpr float PanelRadius = Px(16.f);
	constexpr float PanelPadding = Px(24.f);
	constexpr float DividerWidth = Px(4.f);    // 목록 선반과 명함을 가르는 가로 밧줄선 (M100-01)
	constexpr float ColumnGap = Px(8.f);       // 16 → 8: 목록이 한눈에 3줄 반 보이게 (M100-01 보정)
	constexpr float LineHeight = SnareUiStyle::MenuTouchMin;   // 10-16 애플 44pt (옛 Px(60) = 96)
	constexpr float LineGap = Px(8.f);
	constexpr float DiscSize = Px(52.f);       // 아이템 원판
	// 10-16 「이름은 작고 가격만 크다 — 위계가 거꾸로」: 이름이 가격보다 크다. 가장 작은 글도 `MenuFontMin`.
	constexpr float PriceFontSize = SnareUiStyle::MenuFontMin + 2.f;
	constexpr float UnitFontSize = SnareUiStyle::MenuFontMin;
	constexpr float RowNameFontSize = Px(20.f);
	/** 위 거래 목록 선반이 남는 높이를 다 쓰고, 아래 명함은 인사 두 문장만큼만 잡는다 (M100-01 보정 — 비율 4:3 은
	 *  목록이 한눈에 2줄뿐이라 불합격). 목록은 판 폭을 다 써서 이름이 잘릴 일이 없다. */
	constexpr float VendorNameFontSize = Px(20.f);   // 시트 "빨간 알약에 NPC 이름 20px"
	constexpr float VendorZoneFontSize = Px(16.f);
	constexpr float VendorLetterFontSize = SnareUiStyle::MenuFontMin + 4.f;   // 10-16 「상인 대사 15px」 → 폰 약 21px
	constexpr float PillWidth = Px(130.f);     // 제목 탭 — 배낭 시트와 같은 규격, 색만 초록
	constexpr float PillHeight = Px(50.f);
	constexpr float PillRise = Px(20.f);       // 판 위로 튀어나온 만큼
	constexpr float NamePillHeight = Px(40.f);
	constexpr float TabHeight = SnareUiStyle::MenuTouchMin;
	/** 시트에서 상점 판 오른쪽 끝(1230)과 배낭 판 왼쪽 변(1283) 사이 틈. */
	constexpr float GapToBackpack = 53.f;

	/** 판 오른쪽 끝이 화면 오른쪽 끝에서 몇 px 안쪽인가 — 열린 배낭 판 **왼쪽**에 이 틈만큼 떨어져 선다. */
	float PanelRightInset()
	{
		return UBackpackScreenWidget::GetPanelLeftInsetFromRight() + Px(GapToBackpack);
	}

	/**
	 * 판 왼쪽 끝이 화면 왼쪽에서 몇 px 안쪽인가. **1920 에서 시트 판 폭 그대로**(600×1.6 = 960)가 되게 잡는다 —
	 * 그보다 좁은 화면(4:3 창)에서는 판이 화면 밖으로 나가느니 좁아지는 편이 낫고, 넓은 폰에서는 배낭 옆
	 * 남는 자리를 채운다. 고정 폭으로 두었더니 1728 폭 캡처에서 판 왼쪽(가격·제목 탭)이 화면 밖으로 잘렸다.
	 */
	float PanelLeftInset()
	{
		return SheetSize.X - PanelRightInset() - Px(PanelWidthSheet);
	}

	UTextBlock* MakeLabel(UWidgetTree& Tree, FName Name, const FText& Label, int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* const Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetText(Label);
		// 크림 바탕 위라 기본 흰 글자는 안 보인다 — 색은 부르는 쪽이 팔레트에서 고른다.
		Text->SetColorAndOpacity(FSlateColor(Color));
		SnareUiStyle::SetFontSize(*Text, static_cast<float>(FontSize));
		return Text;
	}

	// 탈것 줄을 고르는 이름 규칙 — 목줄 3·안장 3 목록은 서버가 갖고, 화면은 배낭에서 이 앞머리만 본다.
	const TCHAR* const CollarPrefix = TEXT("item.collar_");
	/** 고구마 장수가 파는 아이템 (DESIGN 8.17 · 서버 `SWEET_POTATO`). */
	const FName SweetPotatoItemID(TEXT("item.sweet_potato"));

	/** 3층 캡처 `-SnarePotatoShot=<요일 0~6>` 이 고른 요일. 없으면 -1. */
	int32 PotatoShotWeekday()
	{
		int32 Weekday = -1;
		return SnareCheckSwitch::Value(TEXT("SnarePotatoShot="), Weekday) && Weekday >= 0 && Weekday <= 6 ? Weekday : -1;
	}

	/** 캡처용 가짜 `GetSweetPotato` 답 — 서버 답과 같은 필드 모양. 값은 검사 전용(Verification.md 6 — 설계 숫자가 아니다). */
	TSharedPtr<FJsonObject> MakePotatoShotReply(int32 Weekday)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetNumberField(TEXT("weekday"), Weekday);
		Reply->SetNumberField(TEXT("cap"), 100);
		if (Weekday == 0)
		{
			Reply->SetNumberField(TEXT("buyPrice"), 10);
			Reply->SetNumberField(TEXT("boughtThisWeek"), 20);
		}
		else
		{
			Reply->SetNumberField(TEXT("price"), 14);
			Reply->SetNumberField(TEXT("prevPrice"), 11);
			Reply->SetNumberField(TEXT("paid"), 300);
			Reply->SetNumberField(TEXT("boughtThisWeek"), 30);
		}
		return Reply;
	}
	const TCHAR* const SaddlePrefix = TEXT("item.saddle_");
	const TCHAR* const AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	FText FindSpeciesName(const UDataTable* AnimalTable, FName SpeciesID)
	{
		if (AnimalTable)
		{
			for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
			{
				const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
				if (Row->AnimalID == SpeciesID && !Row->DisplayName.IsEmpty())
				{
					return Row->DisplayName;
				}
			}
		}
		return FText::FromName(SpeciesID);
	}

	/** 고른 탭은 강조색으로 — 팔레트 `UI_ButtonHighlight` 가 "선택됨" 이다 (DESIGN 2.1). */
	void MarkSelected(UButton* Button, bool bSelected)
	{
		if (!IsValid(Button))
		{
			return;
		}
		SnareUiStyle::MarkSelectedTab(Button, bSelected);
	}
}

UShopScreenWidget::UShopScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ShopTableAsset(
		TEXT("/Game/Game/Data/Shop/DT_ShopTable.DT_ShopTable"));
	if (ShopTableAsset.Succeeded())
	{
		ShopTable = ShopTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> GachaPoolTableAsset(
		TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool"));
	if (GachaPoolTableAsset.Succeeded())
	{
		GachaPoolTable = GachaPoolTableAsset.Object;
	}
}

const FItemRow* UShopScreenWidget::FindItemRow(FName ItemID) const
{
	if (!IsValid(ItemTable))
	{
		return nullptr;
	}
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemID == ItemID)
		{
			return Row;
		}
	}
	return nullptr;
}

FText UShopScreenWidget::FindItemDisplayName(FName ItemID) const
{
	// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7).
	const FItemRow* const Row = FindItemRow(ItemID);
	if (Row != nullptr && !Row->DisplayName.IsEmpty())
	{
		return Row->DisplayName;
	}
	// 가챠 소재는 공개 문서 이름 — 끝의 묶음 수(`가죽 조각 x5`)는 뽑을 때 개수라 뗀다(팔기 줄의 `x{개수}` 는 가진 개수).
	if (const FGachaPoolRow* const GachaRow = FindSellableGachaRow(ItemID))
	{
		FString Name = GachaRow->DisplayName.ToString();
		int32 Cut = INDEX_NONE;
		if (Name.FindLastChar(TEXT(' '), Cut) && Name.Len() > Cut + 2 && Name[Cut + 1] == TEXT('x')
			&& Name.Mid(Cut + 2).IsNumeric())
		{
			Name.LeftInline(Cut);
		}
		return FText::FromString(Name);
	}
	return FText::FromName(ItemID);
}

const FGachaPoolRow* UShopScreenWidget::FindSellableGachaRow(FName ItemID) const
{
	const FGachaPoolRow* const Row = UBackpackScreenWidget::FindGachaPoolRow(GachaPoolTable, ItemID);
	return Row != nullptr && Row->SellPrice > 0 ? Row : nullptr;
}

TArray<FShopLineView> UShopScreenWidget::BuildBuyLines() const
{
	TArray<FShopLineView> Lines;
	if (ShopKind == EShopKind::SweetPotato)
	{
		// 고구마 장수는 표가 없다 — 서버가 준 개당 값으로 묶음 줄을 세운다. 남은 상한을 넘는 묶음은 안 보인다.
		for (const int32 Bundles : GetPotatoBundleChoices())
		{
			const int32 Count = Bundles * SnareSweetPotato::BundleSize;
			if (PotatoBuyPrice <= 0 || PotatoBought + Count > PotatoCap)
			{
				continue;
			}
			FShopLineView Line;
			Line.ItemID = SweetPotatoItemID;
			Line.DisplayName = FindItemDisplayName(SweetPotatoItemID);
			Line.Price = Count * PotatoBuyPrice;
			Line.Count = Count;
			Line.Bundles = Bundles;
			Lines.Add(Line);
		}
		return Lines;
	}
	if (!IsValid(ShopTable))
	{
		return Lines;
	}
	for (const TPair<FName, uint8*>& Pair : ShopTable->GetRowMap())
	{
		const FShopRow* const Row = reinterpret_cast<const FShopRow*>(Pair.Value);
		if (Row == nullptr || Row->ItemID.IsNone() || GetSellerKind(Row->ItemID) != ShopKind)
		{
			continue;
		}
		FShopLineView Line;
		Line.ItemID = Row->ItemID;
		Line.DisplayName = FindItemDisplayName(Row->ItemID);
		Line.Price = Row->Price;
		Line.Count = Row->Count;
		Lines.Add(Line);
	}
	if (ShopKind == EShopKind::General)
	{
		AppendMountLines(Lines);   // 목줄·안장은 잡화상만
	}
	return Lines;
}

const TArray<int32>& UShopScreenWidget::GetPotatoBundleChoices()
{
	static const TArray<int32> Choices = {1, 5, 10};
	return Choices;
}

EShopKind UShopScreenWidget::GetSellerKind(FName ItemID)
{
	const FString ID = ItemID.ToString();
	if (ID.StartsWith(TEXT("item.rod_")) || ID.EndsWith(TEXT("_bait")) || ID == TEXT("item.fisher_hat"))
	{
		return EShopKind::Fishing;
	}
	if (ID.StartsWith(TEXT("item.seed_")) || ID == TEXT("item.farmer_hat"))
	{
		return EShopKind::Farming;
	}
	if (ID.EndsWith(TEXT("_hat")) || ID.EndsWith(TEXT("_shirt")) || ID.EndsWith(TEXT("_pants")) || ID.StartsWith(TEXT("item.paint_")))
	{
		return EShopKind::Clothes;
	}
	return EShopKind::General;
}

bool UShopScreenWidget::IsMountCollar(FName ItemID)
{
	return ItemID.ToString().StartsWith(CollarPrefix);
}

bool UShopScreenWidget::IsMountSaddle(FName ItemID)
{
	return ItemID.ToString().StartsWith(SaddlePrefix);
}

bool UShopScreenWidget::DoesBuy(EShopKind Kind, const FItemRow& Row) const
{
	switch (Kind)
	{
	case EShopKind::Fishing:
		return Row.CraftTag == ECraftTag::Fish;
	case EShopKind::Farming:
	{
		// 작물 = 씨앗이 있는 것 (`item.<작물>` ↔ `item.seed_<작물>`). 이름이 다른 둘은 서버 `CROP_SEED_EXCEPTIONS` 그대로.
		const FString ID = Row.ItemID.ToString();
		return ID == TEXT("item.tea_leaves") || ID == TEXT("item.coffee_beans")
			|| (ID.StartsWith(TEXT("item.")) && FindItemRow(FName(TEXT("item.seed_") + ID.Mid(5))) != nullptr);
	}
	case EShopKind::Clothes:
	case EShopKind::SweetPotato:
		return false;
	default:
		return true;
	}
}

void UShopScreenWidget::AppendMountLines(TArray<FShopLineView>& Lines) const
{
	if (!IsValid(WatchedBackend) || !WatchedBackend->GetPlayerSnapshot().HasOwner())
	{
		return;
	}
	const FSnarePlayerSnapshot& Snapshot = WatchedBackend->GetPlayerSnapshot();
	FName Collar;
	TArray<FName> Saddles;
	for (const FInventorySlot& InvSlot : Snapshot.InventorySlots)
	{
		if (InvSlot.Count > 0 && IsMountCollar(InvSlot.ItemID) && Collar.IsNone())
		{
			Collar = InvSlot.ItemID; // 어느 목줄이든 종 하나 — 가진 첫 목줄을 쓴다(결정 009)
		}
		if (InvSlot.Count > 0 && IsMountSaddle(InvSlot.ItemID))
		{
			Saddles.AddUnique(InvSlot.ItemID);
		}
	}
	if (Collar.IsNone() && Saddles.Num() == 0)
	{
		return;
	}
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	auto Add = [this, &Lines, AnimalTable](FName ItemID, FName Species)
	{
		FShopLineView Line;
		Line.ItemID = ItemID;
		Line.DisplayName = FindItemDisplayName(ItemID);
		Line.Count = 1;
		Line.MountSpecies = Species;
		Line.MountSpeciesName = FindSpeciesName(AnimalTable, Species);
		Lines.Add(Line);
	};
	for (const FName& Species : WatchedBackend->GetMountSpeciesChoices())
	{
		if (!Collar.IsNone() && !Snapshot.Mounts.Contains(Species))
		{
			Add(Collar, Species);
		}
	}
	for (const FName& Saddle : Saddles)
	{
		for (const FName& Species : Snapshot.Mounts)
		{
			if (Snapshot.MountSaddles.FindRef(Species) != Saddle)
			{
				Add(Saddle, Species);
			}
		}
	}
}

TArray<FShopLineView> UShopScreenWidget::BuildSellLines() const
{
	TArray<FShopLineView> Lines;
	if (!IsValid(WatchedBackend) || !WatchedBackend->GetPlayerSnapshot().HasOwner())
	{
		return Lines;
	}
	for (const FInventorySlot& InvSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
	{
		// 파는 값이 0 인 것(나무·향나무·산딸기·장비 …)은 목록에 없다 — 눌러 봐야 서버가 `not_sellable` 로 거절한다.
		const FItemRow* const Row = FindItemRow(InvSlot.ItemID);
		// 가챠 희귀 소재 5종은 아이템 표 행이 없다 — 잡화점만 공개 문서 풀 표 값으로 산다(DESIGN 10.2, 서버 `FIXED_SELL_PRICE`).
		const FGachaPoolRow* const GachaRow = Row == nullptr && ShopKind == EShopKind::General
			? FindSellableGachaRow(InvSlot.ItemID) : nullptr;
		if (GachaRow != nullptr && InvSlot.Count > 0)
		{
			FShopLineView Line;
			Line.ItemID = InvSlot.ItemID;
			Line.DisplayName = FindItemDisplayName(InvSlot.ItemID);
			Line.Price = GachaRow->SellPrice;
			Line.Count = InvSlot.Count;
			Lines.Add(Line);
			continue;
		}
		if (Row == nullptr || Row->SellPrice <= 0 || InvSlot.Count <= 0 || !DoesBuy(ShopKind, *Row))
		{
			continue;
		}
		FShopLineView Line;
		Line.ItemID = InvSlot.ItemID;
		Line.DisplayName = Row->DisplayName.IsEmpty() ? FText::FromName(InvSlot.ItemID) : Row->DisplayName;
		Line.Price = Row->SellPrice;
		Line.Count = InvSlot.Count;
		Lines.Add(Line);
	}
	// 고구마는 일반 `SellItem` 이 거절하는 아이템(`SellPrice` 0)이라 표 줄이 아니다 — 잡화상만, 서버가 알려 준 지금 칸 값으로 줄을 세운다.
	const int32 HeldPotatoes = CountHeldPotatoes();
	if (ShopKind == EShopKind::General && PotatoPrice > 0 && HeldPotatoes > 0)
	{
		FShopLineView Line;
		Line.ItemID = SweetPotatoItemID;
		Line.DisplayName = FindItemDisplayName(SweetPotatoItemID);
		Line.Price = PotatoPrice;
		Line.Count = HeldPotatoes;
		Line.bPotatoSell = true;
		Line.PotatoTrend = PotatoPrevPrice > 0 ? FMath::Sign(PotatoPrice - PotatoPrevPrice) : 0;
		Line.PotatoPaidEach = PotatoBought > 0 ? FMath::RoundToInt(static_cast<float>(PotatoPaid) / PotatoBought) : 0;
		Lines.Add(Line);
	}
	return Lines;
}

int32 UShopScreenWidget::CountHeldPotatoes() const
{
	int32 Total = 0;
	if (IsValid(WatchedBackend))
	{
		for (const FInventorySlot& InvSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
		{
			if (InvSlot.ItemID == SweetPotatoItemID && InvSlot.Count > 0)
			{
				Total += InvSlot.Count;
			}
		}
	}
	// 캡처 평일(`-SnarePotatoShot=1~6`)이고 가방에 없으면 산 30개를 가졌다고 보고 팔기 줄을 세운다(누르면 서버가 판정).
	return Total == 0 && PotatoShotWeekday() > 0 ? 30 : Total;
}

FText UShopScreenWidget::GetCoinText() const
{
	// 스토어 스크린샷용 — `-SnareShotCoinText=코인 128` 이면 알약 글자만 바꾼다(잔액·지급 값은 안 건드림, 출시 빌드 무시).
	FString ShotText;
	if (SnareCheckSwitch::Value(TEXT("SnareShotCoinText="), ShotText, false) && !ShotText.IsEmpty())
	{
		return FText::FromString(ShotText);
	}
	return SnareBalanceHud::FormatBalanceText(WatchedBackend);
}

FText UShopScreenWidget::GetLineText(const FShopLineView& Line, bool bSellLine) const
{
	// 시트 순서: 사기는 "가격 > 아이템", 팔기는 "아이템 > 가격". 탈것 줄은 "쓸 것 > 종".
	if (!Line.MountSpecies.IsNone())
	{
		return FText::Format(LOCTEXT("ShopMountLine", "{0}  >  {1}"), Line.DisplayName, Line.MountSpeciesName);
	}
	if (Line.bPotatoSell)
	{
		const FText Trend = Line.PotatoTrend > 0 ? LOCTEXT("PotatoUp", " 오름") : Line.PotatoTrend < 0 ? LOCTEXT("PotatoDown", " 내림") : FText::GetEmpty();
		return FText::Format(LOCTEXT("PotatoSellLine", "{0} x{1}  >  {2}코인{3} (산 값 {4})"), Line.DisplayName,
			FText::AsNumber(Line.Count), FText::AsNumber(Line.Price), Trend, FText::AsNumber(Line.PotatoPaidEach));
	}
	return bSellLine
		? FText::Format(LOCTEXT("ShopSellLine", "{0} x{1}  >  {2}코인"), Line.DisplayName,
			FText::AsNumber(Line.Count), FText::AsNumber(Line.Price))
		: FText::Format(LOCTEXT("ShopBuyLine", "{0}코인  >  {1} x{2}"), FText::AsNumber(Line.Price),
			Line.DisplayName, FText::AsNumber(Line.Count));
}

void UShopScreenWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &UShopScreenWidget::HandleBackendSnapshotChanged);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &UShopScreenWidget::HandleBackendSnapshotChanged);
	}
	RefreshDisplay();
}

void UShopScreenWidget::HandleBackendSnapshotChanged()
{
	RefreshDisplay();
}

void UShopScreenWidget::SetOpen(bool bInOpen)
{
	const bool bJustOpened = bInOpen && !bOpen;
	bOpen = bInOpen;
	if (bJustOpened)
	{
		if (ShopKind == EShopKind::General && PotatoShotWeekday() > 0)
		{
			bSellTab = true;   // 캡처 평일: 잡화상 팔기 탭을 펴서 고구마 팔기 줄을 찍는다
		}
		FetchSweetPotato();  // 고구마 장수만 — 열 때마다 이번 주 값·상한을 서버에서 새로 받는다
	}
	// 시트 「구성」 — 상점 판과 배낭이 **같이** 열린다. 사고팔 때 내 칸이 안 보이면 무엇을 팔지 고를 수가 없다.
	if (IsValid(BackpackScreen))
	{
		BackpackScreen->SetPanelOpen(bOpen);
		BackpackScreen->SetCloseHidden(bOpen);   // 상점 탭 줄 「닫기」 가 둘 다 닫는다 — 배낭 쪽 「닫기」 는 접는다
		BackpackScreen->SetBalanceChipHidden(bOpen);   // 상점 판 머리 코인 알약이 있어 배낭 쪽은 접는다
	}
	RefreshDisplay();
}

void UShopScreenWidget::SetSellTab(bool bInSellTab)
{
	bSellTab = bInSellTab;
	if (bSellTab)
	{
		FetchSweetPotato();   // 잡화상: 고구마를 가졌으면 지금 칸 값을 새로 받는다
	}
	RefreshDisplay();
}

void UShopScreenWidget::SetVendorName(const FText& InName)
{
	VendorName = InName;
	RefreshDisplay();
}

void UShopScreenWidget::SetVendorZone(const FText& InZone)
{
	VendorZone = InZone;
	RefreshDisplay();
}

void UShopScreenWidget::BindBackpackScreen(UBackpackScreenWidget* InBackpack)
{
	BackpackScreen = InBackpack;
	RefreshDisplay();
}

float UShopScreenWidget::GetPhoneScale()
{
	return PhoneScale;
}

FText UShopScreenWidget::GetVendorLetter(EShopKind Kind)
{
	// 편지체 인사 두 문장 (시트 「구성」 오른쪽 칸). **원본 문구를 옮기지 않고 새로 썼다**(Reference.md 5).
	// 가게마다 다른 글이라 어느 가게 앞에 섰는지가 글로도 읽힌다.
	switch (Kind)
	{
	case EShopKind::Fishing:
		return LOCTEXT("VendorLetterFishing",
			"물때는 내가 봐 줄 테니 자네는 던지기만 하게. 잡은 것은 상하기 전에 내게 가져오게.");
	case EShopKind::Farming:
		return LOCTEXT("VendorLetterFarming",
			"씨앗은 한 번 심으면 되돌릴 수 없으니 밭부터 고르게. 거둔 것은 내가 값을 쳐 주지.");
	case EShopKind::SweetPotato:
		return LOCTEXT("VendorLetterSweetPotato",
			"일요일에만 오는 장수라네. 다음 일요일이 오면 썩으니 그 전에 잡화상에 팔게.");
	case EShopKind::Clothes:
		return LOCTEXT("VendorLetterClothes",
			"섬에서는 차림이 곧 인사일세. 여기서는 팔기만 하니, 입던 것은 잡화상에 맡기게.");
	default:
		return LOCTEXT("VendorLetterGeneral",
			"섬에 온 것을 환영하네. 고구마는 일요일에 달순이한테 사서 평일에 내게 팔게.");
	}
}

void UShopScreenWidget::SetShopKind(EShopKind InKind)
{
	ShopKind = InKind;
	RefreshDisplay();
}

void UShopScreenWidget::ApplySweetPotatoReply(const TSharedPtr<FJsonObject>& Response)
{
	if (!Response.IsValid())
	{
		return;
	}
	// 일요일 `GetSweetPotato` 만 `buyPrice` 가 온다. 그 밖의 요일·`BuySweetPotato` 답에는 없다 — 있을 때만 바꾼다.
	double Value = 0.0;
	if (Response->TryGetNumberField(TEXT("buyPrice"), Value))
	{
		PotatoBuyPrice = static_cast<int32>(Value);
	}
	else if (Response->TryGetNumberField(TEXT("weekday"), Value))
	{
		PotatoBuyPrice = 0;
	}
	// 월~토 `GetSweetPotato` 답만 `price`·`prevPrice`·`paid` 를 싣는다 (`weekday` 가 있는 답) — 잡화상 팔기 줄이 읽는다.
	if (Response->TryGetNumberField(TEXT("weekday"), Value))
	{
		PotatoPrice = Response->TryGetNumberField(TEXT("price"), Value) ? static_cast<int32>(Value) : 0;
		PotatoPrevPrice = Response->TryGetNumberField(TEXT("prevPrice"), Value) ? static_cast<int32>(Value) : 0;
		PotatoPaid = Response->TryGetNumberField(TEXT("paid"), Value) ? static_cast<int32>(Value) : 0;
	}
	if (Response->TryGetNumberField(TEXT("boughtThisWeek"), Value))
	{
		PotatoBought = static_cast<int32>(Value);
	}
	if (Response->TryGetNumberField(TEXT("cap"), Value))
	{
		PotatoCap = static_cast<int32>(Value);
	}
}

void UShopScreenWidget::FetchSweetPotato()
{
	// 고구마 장수는 늘, 잡화상은 고구마를 가졌을 때만 묻는다 (안 가졌으면 팔기 줄이 없으니 서버를 부르지 않는다).
	const bool bVendor = ShopKind == EShopKind::SweetPotato;
	if (!IsValid(WatchedBackend) || !(bVendor || (ShopKind == EShopKind::General && CountHeldPotatoes() > 0)))
	{
		return;
	}
	RetryRequest = nullptr;
	const TWeakObjectPtr<UShopScreenWidget> WeakThis(this);
	const auto OnDone =
		[WeakThis, bVendor](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UShopScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!bVendor)
			{
				if (Failure.IsEmpty())
				{
					Self->ApplySweetPotatoReply(Response);   // 잡화상: 인사말 없이 팔기 줄 값만 맞춘다
				}
			}
			else if (!Failure.IsEmpty())
			{
				Self->Message = Failure;
				Self->RetryRequest = [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->FetchSweetPotato(); } };
			}
			else
			{
				Self->ApplySweetPotatoReply(Response);
				Self->Message = Self->PotatoBuyPrice > 0
					? FText::Format(LOCTEXT("PotatoGreeting", "고구마 사세요~ 이번 주는 개당 {0}코인! (이번 주 남은 {1}개)"),
						FText::AsNumber(Self->PotatoBuyPrice), FText::AsNumber(FMath::Max(0, Self->PotatoCap - Self->PotatoBought)))
					: LOCTEXT("PotatoClosed", "고구마는 일요일에만 판다");
			}
			Self->RefreshDisplay();
		};
	// 3층 캡처는 실제 요일을 기다리지 않는다 — 서버 답 자리에 같은 모양 가짜 답을 넣어 같은 길로 보인다.
	if (const int32 ShotWeekday = PotatoShotWeekday(); ShotWeekday >= 0)
	{
		OnDone(true, MakePotatoShotReply(ShotWeekday), FString());
		return;
	}
	WatchedBackend->CallServerFunction(TEXT("GetSweetPotato"), MakeShared<FJsonObject>(), OnDone);
}

bool UShopScreenWidget::RequestBuySweetPotato(int32 Bundles)
{
	if (bPending || Bundles < 1 || !IsValid(WatchedBackend))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("bundles"), Bundles);
	bPending = true;
	Message = FText::GetEmpty();
	RetryRequest = nullptr;
	RefreshDisplay();

	const TWeakObjectPtr<UShopScreenWidget> WeakThis(this);
	const FText ItemName = FText::Format(LOCTEXT("PotatoTradeName", "{0} x{1}"),
		FindItemDisplayName(SweetPotatoItemID), FText::AsNumber(Bundles * SnareSweetPotato::BundleSize));
	WatchedBackend->CallServerFunction(TEXT("BuySweetPotato"), Args,
		[WeakThis, ItemName, Bundles](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UShopScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Shop] 고구마 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				if (!bOk || !Response.IsValid())
				{
					Self->RetryRequest = [WeakThis, Bundles]() { if (WeakThis.IsValid()) { WeakThis->RequestBuySweetPotato(Bundles); } };
				}
				Self->RefreshDisplay();
				return;
			}
			USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);
			Self->ApplySweetPotatoReply(Response);   // 산 개수·상한만 — 코인·배낭은 아래에서 다시 받는다 (CLAUDE.md 8)
			Self->Message = FText::Format(LOCTEXT("ShopBought", "{0} 샀다"), ItemName);
			Self->RefreshDisplay();
			if (IsValid(Self->WatchedBackend))
			{
				Self->WatchedBackend->FetchPlayerState();
			}
		});
	return true;
}

bool UShopScreenWidget::RequestSellSweetPotato(int32 Count)
{
	if (bPending || Count < 1 || !IsValid(WatchedBackend))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("count"), Count);
	bPending = true;
	Message = FText::GetEmpty();
	RetryRequest = nullptr;
	RefreshDisplay();

	const TWeakObjectPtr<UShopScreenWidget> WeakThis(this);
	const FText ItemName = FText::Format(LOCTEXT("PotatoTradeName", "{0} x{1}"), FindItemDisplayName(SweetPotatoItemID), FText::AsNumber(Count));
	WatchedBackend->CallServerFunction(TEXT("SellSweetPotato"), Args,
		[WeakThis, ItemName, Count](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UShopScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Shop] 고구마 팔기 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				if (!bOk || !Response.IsValid())
				{
					Self->RetryRequest = [WeakThis, Count]() { if (WeakThis.IsValid()) { WeakThis->RequestSellSweetPotato(Count); } };
				}
				Self->RefreshDisplay();
				return;
			}
			USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);
			Self->Message = FText::Format(LOCTEXT("ShopSold", "{0} 팔았다"), ItemName);
			Self->RefreshDisplay();
			if (IsValid(Self->WatchedBackend))
			{
				Self->WatchedBackend->FetchPlayerState();   // 코인·배낭은 서버가 바꿨다 — 다시 받는다 (CLAUDE.md 8)
			}
		});
	return true;
}

void UShopScreenWidget::SetSourceTables(UDataTable* InShopTable, UDataTable* InItemTable)
{
	ShopTable = InShopTable;
	ItemTable = InItemTable;
	RefreshDisplay();
}

void UShopScreenWidget::HandleOpenClicked() { SetOpen(true); }
void UShopScreenWidget::HandleCloseClicked() { SetOpen(false); }
void UShopScreenWidget::HandleBuyTabClicked() { SetSellTab(false); }
void UShopScreenWidget::HandleSellTabClicked() { SetSellTab(true); }
void UShopScreenWidget::HandleSellAllClicked() { SetSellAllMode(!bSellAllMode); }

void UShopScreenWidget::SetSellAllMode(bool bInSellAll)
{
	bSellAllMode = bInSellAll;
	RefreshDisplay();
}

bool UShopScreenWidget::RequestBuy(FName ItemID)
{
	return SendTrade(TEXT("BuyItem"), ItemID);
}

bool UShopScreenWidget::RequestSell(FName ItemID)
{
	return SendTrade(TEXT("SellItem"), ItemID);
}

bool UShopScreenWidget::RequestSellAll(FName ItemID)
{
	for (const FShopLineView& Line : BuildSellLines())
	{
		if (Line.ItemID == ItemID && Line.Count > 0)
		{
			return SendTrade(TEXT("SellItem"), ItemID, NAME_None, Line.Count);
		}
	}
	return false;
}

bool UShopScreenWidget::RequestMountTrade(FName ItemID, FName SpeciesID)
{
	const bool bCollar = ItemID.ToString().StartsWith(CollarPrefix);
	return !SpeciesID.IsNone() && SendTrade(bCollar ? TEXT("ExchangeCollar") : TEXT("AttachSaddle"), ItemID, SpeciesID);
}

bool UShopScreenWidget::SendTrade(const FString& FunctionName, FName ItemID, FName SpeciesID, int32 Count)
{
	if (bPending || ItemID.IsNone() || !IsValid(WatchedBackend))
	{
		return false;
	}

	// 시트의 한 줄 탭 = 하나 거래. 여러 개는 「모두 팔기」 한 번(`Count` = 가진 개수)이거나 여러 번 누른다.
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	const bool bMount = !SpeciesID.IsNone();
	if (bMount)
	{
		Args->SetStringField(FunctionName == TEXT("ExchangeCollar") ? TEXT("collarItemId") : TEXT("saddleItemId"), ItemID.ToString());
		Args->SetStringField(TEXT("speciesId"), SpeciesID.ToString());
	}
	else
	{
		Args->SetStringField(TEXT("itemId"), ItemID.ToString());
		Args->SetNumberField(TEXT("count"), FMath::Max(1, Count));
	}

	bPending = true;
	Message = FText::GetEmpty();
	RetryRequest = nullptr;
	RefreshDisplay();

	const TWeakObjectPtr<UShopScreenWidget> WeakThis(this);
	const FText ItemName = Count > 1
		? FText::Format(LOCTEXT("ShopTradeNameCount", "{0} x{1}"), FindItemDisplayName(ItemID), FText::AsNumber(Count))
		: FindItemDisplayName(ItemID);
	const bool bSell = FunctionName == TEXT("SellItem");
	WatchedBackend->CallServerFunction(FunctionName, Args,
		[WeakThis, ItemName, bSell, bMount, FunctionName, ItemID, SpeciesID, Count](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UShopScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Shop] 거래 거절 — %s"), *Failure.ToString());
				Self->Message = Failure;
				// 서버에 닿지 못했을 때만 같은 거래를 다시 — 「코인 모자람」 은 다시 보내도 같다.
				if (!bOk || !Response.IsValid())
				{
					Self->RetryRequest = [WeakThis, FunctionName, ItemID, SpeciesID, Count]()
					{
						if (WeakThis.IsValid())
						{
							WeakThis->SendTrade(FunctionName, ItemID, SpeciesID, Count);
						}
					};
				}
				Self->RefreshDisplay();
				return;
			}

			USnareSoundSubsystem::Play2D(Self, TEXT("Coins"), 0.8f);   // 서버가 거래를 확정한 뒤 (DESIGN 2.2 소리)
			Self->Message = FText::Format(bMount ? LOCTEXT("ShopMountDone", "{0} 썼다")
				: bSell ? LOCTEXT("ShopSold", "{0} 팔았다") : LOCTEXT("ShopBought", "{0} 샀다"), ItemName);
			Self->RefreshDisplay();
			// 코인·배낭은 서버가 바꿨다 — 캐시를 손으로 고치지 않고 다시 받는다 (CLAUDE.md 8). 탈것 칸은 답에 온 그대로.
			if (IsValid(Self->WatchedBackend))
			{
				if (bMount)
				{
					Self->WatchedBackend->ApplyMountsReply(Response);
				}
				Self->WatchedBackend->FetchPlayerState();
			}
		});
	return true;
}

void UShopScreenWidget::RefreshDisplay()
{
	if (IsValid(OpenButton))
	{
		OpenButton->SetVisibility(!bOpen && bPromptOnScreen && !bPromptSuppressed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// 제목 탭·코인 줄은 판 밖 루트에 서 있어 판과 같이 접고 편다 — 안 접으면 닫힌 상점이 HUD 위에 남는다 (M23-01).
	const ESlateVisibility PanelVisibility = bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	for (UWidget* const PanelPart : TArray<UWidget*>{ShopPanel, TitlePill, CoinPill})
	{
		if (IsValid(PanelPart))
		{
			PanelPart->SetVisibility(PanelVisibility);
		}
	}
	if (IsValid(VendorNameText))
	{
		VendorNameText->SetText(VendorName.IsEmpty() ? LOCTEXT("ShopTitle", "잡화점") : VendorName);
	}
	if (IsValid(VendorZoneText))
	{
		// 존을 아직 못 받았으면 접는다 — 홀로 남은 `-` 가 뜻 없는 글자로 읽혔다(10-16 검수).
		VendorZoneText->SetText(VendorZone);
		VendorZoneText->SetVisibility(VendorZone.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (IsValid(VendorLetterText))
	{
		VendorLetterText->SetText(GetVendorLetter(ShopKind));
	}
	if (IsValid(CoinText))
	{
		CoinText->SetText(GetCoinText());
	}
	if (IsValid(SellAllButton))
	{
		SellAllButton->SetVisibility(bSellTab ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(MessageText))
	{
		MessageText->SetText(bPending ? LOCTEXT("ShopPending", "거래 확인 중...") : Message);
	}
	if (IsValid(RetryBox))
	{
		RetryBox->SetVisibility(RetryRequest && !bPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (!IsValid(LineList) || WidgetTree == nullptr)
	{
		return;
	}

	// 줄은 매번 다시 세운다 — 남겨 두고 덧쓰면 다 팔아 없어진 줄이 화면에 남는다.
	LineList->ClearChildren();
	LineProxies.Reset();

	const TArray<FShopLineView> Lines = bSellTab ? BuildSellLines() : BuildBuyLines();
	int32 Index = 0;
	for (const FShopLineView& Line : Lines)
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("ShopLineBox_%d"), Index)));
		Box->SetHeightOverride(LineHeight);

		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("ShopLineButton_%d"), Index)));
		// 시트 "버튼 없음, 행 자체를 누른다" — 줄 전체가 버튼이고 그 **안**이 가격·화살표·원판이다.
		// 줄이 단추 폭을 다 받아야 가격이 오른쪽 끝에 선다 — 기본 가운데 맞춤이면 줄이 제 글 폭만큼만 가운데에 뭉쳤다(10-16).
		if (UButtonSlot* const RowSlot = Cast<UButtonSlot>(Button->AddChild(BuildTradeRow(Index, Line, bSellTab))))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
			RowSlot->SetVerticalAlignment(VAlign_Fill);
		}
		// 시트 「모든 패널 모서리 둥글게」 — 색은 `ApplyUiStyle` 이 TintColor 만 덮어 모양이 남는다 (M27-03).
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		// 답을 기다리는 동안 줄 전부를 잠근다 — 같은 줄을 두 번 눌러 두 번 사는 일이 없게.
		Button->SetIsEnabled(!bPending);

		Box->AddChild(Button);

		USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
		Proxy->Key = Line.ItemID;
		const TWeakObjectPtr<UShopScreenWidget> WeakThis(this);
		const bool bSellLine = bSellTab;
		const FName Species = Line.MountSpecies;
		const int32 Bundles = Line.Bundles;
		const bool bPotatoSell = Line.bPotatoSell;
		const int32 HeldCount = Line.Count;
		Proxy->OnClicked = [WeakThis, bSellLine, Species, Bundles, bPotatoSell, HeldCount](FName ItemID)
		{
			if (UShopScreenWidget* const Self = WeakThis.Get())
			{
				Bundles > 0 ? Self->RequestBuySweetPotato(Bundles)
					: bPotatoSell ? Self->RequestSellSweetPotato(Self->bSellAllMode ? HeldCount : 1)
					: !Species.IsNone() ? Self->RequestMountTrade(ItemID, Species)
					: !bSellLine ? Self->RequestBuy(ItemID)
					: Self->bSellAllMode ? Self->RequestSellAll(ItemID) : Self->RequestSell(ItemID);
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

	if (Lines.Num() == 0)
	{
		UTextBlock* const Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShopEmptyText"));
		Empty->SetText(bSellTab ? (ShopKind == EShopKind::SweetPotato ? LOCTEXT("PotatoNoBuy", "고구마는 잡화상에 판다") : ShopKind == EShopKind::Clothes ? LOCTEXT("ShopClothesNoBuy", "옷 가게는 사들이지 않는다") : LOCTEXT("ShopNothingToSell", "팔 수 있는 것이 없다")) : LOCTEXT("ShopNoStock", "진열된 것이 없다"));
		Empty->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		LineList->AddChild(Empty);
	}

	// 새 줄에도 글꼴·버튼 색을 입히고, 고른 탭만 강조색으로 되돌린다.
	SnareUiStyle::ApplyUiStyle(*this);
	MarkSelected(BuyTabButton, !bSellTab);
	MarkSelected(SellTabButton, bSellTab);
	MarkSelected(SellAllButton, bSellAllMode);
}

TSharedRef<SWidget> UShopScreenWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}
	RefreshDisplay();

	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	MarkSelected(BuyTabButton, !bSellTab);
	MarkSelected(SellTabButton, bSellTab);
	MarkSelected(SellAllButton, bSellAllMode);
	return Built;
}

void UShopScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 상인은 멀어지면 화면을 내리고(`RemoveFromParent` → `NativeDestruct`) 다시 오면 **같은 화면**을 올린다 —
	// 여기서 다시 묶지 않으면 "연결 안 됨"·빈 팔기 목록으로 굳는다(감독 PIE 재현, M22).
	if (USnareBackendSubsystem* const Backend = BackendToRebind.Get())
	{
		BindToBackend(Backend);
	}
}

void UShopScreenWidget::SetPromptTarget(const FVector& WorldLocation)
{
	PromptTarget = WorldLocation;
	// 첫 투영 전엔 카드 자리를 모른다 — 옛 가운데 자리에 한 프레임 번쩍이지 않게 접어 둔다.
	bPromptOnScreen = false;
	RefreshDisplay();
}

void UShopScreenWidget::SetPromptSuppressed(bool bSuppressed)
{
	if (bPromptSuppressed != bSuppressed)
	{
		bPromptSuppressed = bSuppressed;
		RefreshDisplay();
	}
}

bool UShopScreenWidget::PlaceOpenButton(const FVector2D& TargetPx, const FVector2D& ViewportPx)
{
	FVector2D TopLeft = FVector2D::ZeroVector;
	const bool bOnScreen = USnarePromptCardWidget::ComputeTapCardPosition(TargetPx, ViewportPx, TopLeft);
	UCanvasPanelSlot* const OpenSlot = IsValid(OpenButton) ? Cast<UCanvasPanelSlot>(OpenButton->Slot) : nullptr;
	if (bOnScreen && OpenSlot != nullptr)
	{
		// 투영 자리는 뷰포트 왼쪽 위 기준이라 앵커·정렬도 왼쪽 위로 바꾼다.
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

void UShopScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen || !PromptTarget.IsSet())
	{
		return;
	}
	FVector2D TargetPx = FVector2D::ZeroVector;
	FVector2D ViewportPx = FVector2D::ZeroVector;
	const bool bProjected = USnarePromptCardWidget::ProjectTarget(GetOwningPlayer(), PromptTarget.GetValue(), TargetPx, ViewportPx);
	// 카메라 뒤는 화면 밖과 같다 — 음수 자리로 넘겨 접게 한다.
	PlaceOpenButton(bProjected ? TargetPx : FVector2D(-1.f, -1.f), ViewportPx);
}

void UShopScreenWidget::NativeDestruct()
{
	BackendToRebind = WatchedBackend;
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

bool UShopScreenWidget::NeedsPlaceholderTree() const
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

void UShopScreenWidget::BuildPlaceholderTree()
{
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ShopScreenRoot"));
		WidgetTree->RootWidget = Root;
	}

	auto MakeButton = [this](FName Name, const FText& Label) -> UButton*
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Text"))));
		Text->SetText(Label);
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
		// 탭·닫기도 둥글게 — 고른 탭 색은 `MarkSelected` 가 TintColor 만 바꿔 모양이 남는다 (M27-03).
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		return Button;
	};

	// 닫힌 상태: 상인 머리 옆 크림 카드 하나 (M23-02). 판과 달리 **키우지 않는다** — 자리는 매 틱 투영으로
	// 옮기고(`PlaceOpenButton`), 투영 전(대상을 모를 때)엔 화면 가운데 오른쪽에 둔다.
	OpenButton = USnarePromptCardWidget::MakeTapCard(*WidgetTree, TEXT("OpenButton"), LOCTEXT("ShopOpen", "상점 열기"),
		TEXT("/Game/Game/UI/Icons/Items/T_Icon_MysteryBag.T_Icon_MysteryBag"));
	OpenButton->OnClicked.AddDynamic(this, &UShopScreenWidget::HandleOpenClicked);
	if (UCanvasPanelSlot* const OpenSlot = Cast<UCanvasPanelSlot>(Root->AddChild(OpenButton)))
	{
		OpenSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		OpenSlot->SetAlignment(FVector2D(0.f, 0.5f));
		OpenSlot->SetPosition(FVector2D(USnarePromptCardWidget::TapCardGapPx, 0.f));
		OpenSlot->SetSize(FVector2D(USnarePromptCardWidget::TapCardWidth, USnarePromptCardWidget::TapCardHeight));
	}

	// 열린 상태: 크림 판 — 두 칸(왼쪽 거래 목록 / 오른쪽 NPC 명함) + 아래 결과 한 줄·닫기.
	// 시트가 상점 판을 배낭 **왼쪽**에 두므로(사고팔 때 내 칸이 보여야 한다) 오른쪽 끝을 열린 배낭 판에서
	// 시트 틈(53px)만큼 떼고, **왼쪽 끝은 화면 왼쪽에서 재** 가로로 늘었다 줄었다 하게 건다
	// (앵커 가로 0~1). 배낭처럼 판을 통째로 키우면 좁은 창에서 판 왼쪽이 화면 밖으로 잘린다 — 실제로
	// 1728 폭 캡처에서 가격 글과 제목 탭이 잘려 나갔다. 세로는 시트대로 화면 한가운데다.
	ShopPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ShopPanel"));
	ShopPanel->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodPanel(), PanelRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));   // 나무 표찰 (M99-06)
	ShopPanel->SetPadding(FMargin(PanelPadding));
	if (UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Root->AddChild(ShopPanel)))
	{
		PanelSlot->SetAnchors(FAnchors(0.f, 0.5f, 1.f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.f, 0.5f));
		// 가로가 펼친 앵커라 Left·Right 는 **여백**이고, 세로는 Top 이 자리·Bottom 이 높이다.
		PanelSlot->SetOffsets(FMargin(PanelLeftInset(), 0.f, PanelRightInset(), PanelHeight));
	}

	UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ShopColumn"));
	ShopPanel->SetContent(Column);

	// 위 = 거래 목록 선반, 아래 = NPC 명함 (M100-01 — 레퍼런스 게임의 「목록 왼쪽 + 오른쪽 판」 쌍과 구도를 달리한다).
	UVerticalBox* const Columns = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ShopColumns"));
	if (UVerticalBoxSlot* const ColumnsSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Columns)))
	{
		ColumnsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// ── 왼쪽 칸: 사기·팔기 탭 + 거래 목록 ──
	UVerticalBox* const Left = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ShopTradeColumn"));
	if (UVerticalBoxSlot* const LeftSlot = Cast<UVerticalBoxSlot>(Columns->AddChild(Left)))
	{
		LeftSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LeftSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ColumnGap));
	}

	UHorizontalBox* const Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ShopTabs"));
	BuyTabButton = MakeButton(TEXT("BuyTabButton"), LOCTEXT("ShopBuyTab", "사기"));
	SellTabButton = MakeButton(TEXT("SellTabButton"), LOCTEXT("ShopSellTab", "팔기"));
	BuyTabButton->OnClicked.AddDynamic(this, &UShopScreenWidget::HandleBuyTabClicked);
	SellTabButton->OnClicked.AddDynamic(this, &UShopScreenWidget::HandleSellTabClicked);
	// 팔기 탭에서만 보이는 켬/끔 — 켜면 줄 탭 한 번이 그 종류 전부다(INBOX M22 ④). 줄마다 버튼을 붙이면
	// 이름 자리가 모자라 잘린다(시트 대조 시험이 1920 폭에서 -112px 를 잡았다) — 그래서 탭 줄에 하나만 둔다.
	SellAllButton = MakeButton(TEXT("SellAllButton"), LOCTEXT("ShopSellAll", "모두 팔기"));
	SellAllButton->OnClicked.AddDynamic(this, &UShopScreenWidget::HandleSellAllClicked);
	for (UButton* const Tab : {BuyTabButton.Get(), SellTabButton.Get(), SellAllButton.Get()})
	{
		if (UHorizontalBoxSlot* const TabSlot = Cast<UHorizontalBoxSlot>(Tabs->AddChild(Tab)))
		{
			// 「모두 팔기」 는 제 글 폭만 받고 사기·팔기가 나머지를 나눈다 — 셋을 똑같이 나누면 1728 폭에서 글이 잘렸다(2층 82/116px).
			TabSlot->SetSize(FSlateChildSize(Tab == SellAllButton ? ESlateSizeRule::Automatic : ESlateSizeRule::Fill));
			TabSlot->SetPadding(FMargin(0.f, 0.f, LineGap, LineGap));
		}
	}
	// 「닫기」 는 탭 줄 오른쪽 끝 (10-16) — 판 아래 따로 두면 누르는 칸 121 두 줄이 목록을 3줄 아래로 줄였다.
	UButton* Close = nullptr;
	USizeBox* const CloseBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("ShopCloseBox"), TEXT("CloseButton"),
		LOCTEXT("ShopClose", "닫기"), Close);
	CloseButton = Close;
	CloseButton->OnClicked.AddDynamic(this, &UShopScreenWidget::HandleCloseClicked);
	if (UHorizontalBoxSlot* const CloseSlot = Cast<UHorizontalBoxSlot>(Tabs->AddChild(CloseBox)))
	{
		CloseSlot->SetPadding(FMargin(0.f, 0.f, 0.f, LineGap));
	}
	USizeBox* const TabBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ShopTabBox"));
	TabBox->SetHeightOverride(TabHeight + LineGap);
	TabBox->AddChild(Tabs);
	Left->AddChild(TabBox);

	// 줄이 많아도 판 밖으로 안 넘치게 — 손가락으로 끌어 내린다 (시트 "스크롤바 오른쪽 세로").
	UScrollBox* const Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ShopLineScroll"));
	LineList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LineList"));
	Scroll->AddChild(LineList);
	if (UVerticalBoxSlot* const ScrollSlot = Cast<UVerticalBoxSlot>(Left->AddChild(Scroll)))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// ── 가로 밧줄선 — 선반 아래 모서리 (M100-01, 예전 세로 구분선 자리). ──
	ColumnDivider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ColumnDivider"));
	ColumnDivider->SetBrushColor(SnareUiStyle::UiRope());
	USizeBox* const DividerBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ColumnDividerBox"));
	DividerBox->SetHeightOverride(DividerWidth);
	DividerBox->AddChild(ColumnDivider);
	Columns->AddChild(DividerBox);

	// ── 오른쪽 칸: NPC 명함 (이름 알약 → 존 → 편지체 본문) ──
	VendorCard = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VendorCard"));
	if (UVerticalBoxSlot* const RightSlot = Cast<UVerticalBoxSlot>(Columns->AddChild(VendorCard)))
	{
		RightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));   // 명함은 제 글만큼만 — 나머지는 목록 (M100-01 보정)
		RightSlot->SetPadding(FMargin(0.f, ColumnGap, 0.f, 0.f));
	}
	VendorNamePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("VendorNamePill"));
	VendorNamePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodButton(), SnareUiStyle::PanelTitlePlateRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	VendorNamePill->SetHorizontalAlignment(HAlign_Center);
	VendorNamePill->SetVerticalAlignment(VAlign_Center);
	VendorNameText = MakeLabel(*WidgetTree, TEXT("VendorNameText"), FText::GetEmpty(), VendorNameFontSize, SnareUiStyle::UiPanelCream());
	// 좁은 판에서 「잡화상 도토리」 가 알약 오른쪽 밖으로 잘렸다(M81-03 캡처) — 긴 이름은 알약 안에 들어가게
	// 줄이기만 한다(Codex 칸 이름·#0199 탭 글자와 같은 방식). 알약 양옆에 숨 쉴 자리도 둔다.
	UScaleBox* const NameFit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("VendorNameFit"));
	NameFit->SetStretch(EStretch::ScaleToFit);
	NameFit->SetStretchDirection(EStretchDirection::DownOnly);
	NameFit->AddChild(VendorNameText);
	VendorNamePill->SetPadding(FMargin(NamePillHeight * 0.5f, 0.f));
	VendorNamePill->SetContent(NameFit);
	USizeBox* const NamePillBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("VendorNamePillBox"));
	NamePillBox->SetHeightOverride(NamePillHeight);
	NamePillBox->AddChild(VendorNamePill);

	// 낮아진 아래 칸이라 이름 알약 + 존 이름을 한 줄에 둔다 (M100-01) — 따로 두면 편지 본문이 판 밖으로 넘쳤다(캡처).
	// 시트의 좌표 줄 자리 — 우리는 존 이름을 적는다(시트 「우리 것과 다르게 할 점」).
	UHorizontalBox* const NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("VendorNameRow"));
	VendorCard->AddChild(NameRow);
	if (UHorizontalBoxSlot* const PillSlotInRow = Cast<UHorizontalBoxSlot>(NameRow->AddChild(NamePillBox)))
	{
		PillSlotInRow->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	VendorZoneText = MakeLabel(*WidgetTree, TEXT("VendorZoneText"), FText::GetEmpty(), VendorZoneFontSize,
		SnareUiStyle::UiButtonPressed());
	VendorZoneText->SetJustification(ETextJustify::Center);
	if (UHorizontalBoxSlot* const ZoneSlot = Cast<UHorizontalBoxSlot>(NameRow->AddChild(VendorZoneText)))
	{
		ZoneSlot->SetPadding(FMargin(LineGap * 2.f, 0.f, 0.f, 0.f));
		ZoneSlot->SetVerticalAlignment(VAlign_Center);
	}

	VendorLetterText = MakeLabel(*WidgetTree, TEXT("VendorLetterText"), FText::GetEmpty(), VendorLetterFontSize,
		SnareUiStyle::UiTextOnWood());
	VendorLetterText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* const LetterSlot = Cast<UVerticalBoxSlot>(VendorCard->AddChild(VendorLetterText)))
	{
		LetterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		LetterSlot->SetPadding(FMargin(0.f, LineGap, 0.f, 0.f));
	}

	// ── 판 아래: 결과 한 줄 + 닫기 (시트에는 없다 — 폰에는 "밖을 눌러 닫기" 가 안 통해 버튼이 하나 필요하다) ──
	MessageText = MakeLabel(*WidgetTree, TEXT("MessageText"), FText::GetEmpty(), RowNameFontSize,
		SnareUiStyle::UiTextOnWood());
	MessageText->SetJustification(ETextJustify::Center);
	MessageText->SetAutoWrapText(true);
	// 「다시 시도」 는 결과 줄 오른쪽 — 서버에 닿지 못했을 때만 보인다 (의뢰 화면과 같은 꼴).
	UHorizontalBox* const MessageRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ShopMessageRow"));
	if (UHorizontalBoxSlot* const TextSlot = Cast<UHorizontalBoxSlot>(MessageRow->AddChild(MessageText)))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	UButton* Retry = nullptr;
	RetryBox = SnareUiStyle::MakeCloseButton(*WidgetTree, TEXT("ShopRetryBox"), TEXT("ShopRetryButton"),
		LOCTEXT("ShopRetry", "다시 시도"), Retry);
	Retry->OnClicked.AddDynamic(this, &UShopScreenWidget::HandleRetryClicked);
	RetryBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* const RetrySlot = Cast<UHorizontalBoxSlot>(MessageRow->AddChild(RetryBox)))
	{
		RetrySlot->SetPadding(FMargin(LineGap, LineGap, 0.f, 0.f));
	}
	Column->AddChild(MessageRow);


	// 제목 탭 — 판 왼쪽 위에 걸쳐 위로 튀어나온다(판 **뒤에** 넣어야 위에 그려진다). 상점은 초록(시트).
	TitlePill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitlePill"));
	TitlePill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiWoodButton(), SnareUiStyle::PanelTitlePlateRadius, SnareUiStyle::UiRope(), SnareUiStyle::HudRopeWidth));
	TitlePill->SetHorizontalAlignment(HAlign_Center);
	TitlePill->SetVerticalAlignment(VAlign_Center);
	TitlePill->SetContent(MakeLabel(*WidgetTree, TEXT("TitleText"), LOCTEXT("ShopTitle2", "상점"),
		VendorNameFontSize, SnareUiStyle::UiPanelCream()));
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(TitlePill)))
	{
		PillSlot->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));
		PillSlot->SetAlignment(FVector2D(0.f, 0.f));
		PillSlot->SetPosition(FVector2D(PanelLeftInset(), -PanelHeight * 0.5f - PillRise));
		PillSlot->SetSize(FVector2D(PillWidth, PillHeight));
	}

	// 코인 줄은 제목 탭과 같은 높이 오른쪽 — 판 안에 넣으면 거래 목록 한 줄을 먹는다. 맨 글자로 두면 크림 판 위
	// 테두리에 걸쳐 어두운 글씨가 판과 겹쳐 읽혔다(M82-04) — 남색 알약에 크림 글씨로 판 밖에 세운다.
	CoinPill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CoinPill"));
	CoinPill->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiNavy(), PillHeight * 0.5f));
	CoinPill->SetPadding(FMargin(PillHeight * 0.5f, 0.f));
	CoinPill->SetVerticalAlignment(VAlign_Center);
	CoinText = MakeLabel(*WidgetTree, TEXT("CoinText"), FText::GetEmpty(), VendorNameFontSize,
		SnareUiStyle::UiPanelCream());
	CoinPill->SetContent(CoinText);
	if (UCanvasPanelSlot* const CoinSlot = Cast<UCanvasPanelSlot>(Root->AddChild(CoinPill)))
	{
		CoinSlot->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));
		// 판 위 변보다 **위**에 띄운다 — 걸쳐 두면 「서버 연결 안 됨」 띠가 판 오른쪽 위 모서리를 덮었다(10-16 검수).
		CoinSlot->SetAlignment(FVector2D(1.f, 1.f));
		CoinSlot->SetPosition(FVector2D(-PanelRightInset(), -PanelHeight * 0.5f - LineGap));
		CoinSlot->SetAutoSize(true);
	}
}

UWidget* UShopScreenWidget::BuildTradeRow(int32 Index, const FShopLineView& Line, bool bSellLine)
{
	// 시트: 사는 것 `가격 > 아이템 원판` · 파는 것 `아이템 원판 > 가격`. **화살표 방향이 곧 돈의 방향**이라
	// 두 줄의 차이는 순서 하나뿐이다 — 글자를 다르게 적어 가르지 않는다.
	UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("ShopRow_%d"), Index)));

	// 값 덩어리 — 초록 굵은 숫자 + 작은 단위(시트 "위첨자 단위"). 탈것 줄은 값 대신 붙일 종 이름이다.
	const bool bMountLine = !Line.MountSpecies.IsNone();
	UHorizontalBox* const Money = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("ShopRowMoney_%d"), Index)));
	if (bMountLine)
	{
		Money->AddChild(MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("ShopRowPrice_%d"), Index)),
			Line.MountSpeciesName, RowNameFontSize, SnareUiStyle::UiTextOnButton()));
	}
	else
	{
		UTextBlock* const PriceLabel = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("ShopRowPrice_%d"), Index)),
			FText::AsNumber(Line.Price), PriceFontSize, SnareUiStyle::UiShopPriceGreen());
		SnareUiStyle::StylePriceOnWood(*PriceLabel);
		Money->AddChild(PriceLabel);
		UTextBlock* const Unit = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("ShopRowUnit_%d"), Index)),
			LOCTEXT("ShopPriceUnit", "코인"), UnitFontSize, SnareUiStyle::UiShopPriceGreen());
		SnareUiStyle::StylePriceOnWood(*Unit);
		if (UHorizontalBoxSlot* const UnitSlot = Cast<UHorizontalBoxSlot>(Money->AddChild(Unit)))
		{
			UnitSlot->SetVerticalAlignment(VAlign_Top);   // 위첨자
		}
		if (Line.bPotatoSell && Line.PotatoTrend != 0)
		{
			// 지난 칸보다 올랐으면 「오름」(초록), 내렸으면 「내림」(글자색). ▲▼ 는 게임 글꼴에 없어 네모로 찍혀(10-19 캡처) 글자로 적는다.
			UTextBlock* const Trend = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("ShopRowTrend_%d"), Index)),
				Line.PotatoTrend > 0 ? LOCTEXT("PotatoUpArrow", " 오름") : LOCTEXT("PotatoDownArrow", " 내림"), UnitFontSize,
				Line.PotatoTrend > 0 ? SnareUiStyle::UiShopPriceGreen() : SnareUiStyle::UiTextOnButton());
			SnareUiStyle::StylePriceOnWood(*Trend);   // 나무 줄 위라 초록·진갈색 모두 안 읽힌다(M97-c-5) — 글자(오름·내림)가 가른다
			Money->AddChild(Trend);
		}
	}

	// 아이템 덩어리 — 색 원판(등급) 위에 그림, 그 옆에 이름·개수.
	UHorizontalBox* const Item = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("ShopRowItem_%d"), Index)));
	UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
		FName(*FString::Printf(TEXT("ShopRowStack_%d"), Index)));
	UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("ShopRowDisc_%d"), Index)));
	const FItemRow* const Row0 = FindItemRow(Line.ItemID);
	// 가챠 소재는 아이템 표 행이 없다 — 원판 색은 공개 문서 등급(배낭 칸과 같은 색, 두 등급 차례가 같다).
	const FGachaPoolRow* const GachaRow0 = Row0 == nullptr ? FindSellableGachaRow(Line.ItemID) : nullptr;
	// 어두운 나무 홈 + 아래 1/5 등급 띠 — 색 동그라미는 레퍼런스 게임와 같아 버렸다 (M101-04, 배낭 칸과 같은 짜임).
	Disc->SetBrush(FSlateRoundedBoxBrush(SnareUiStyle::UiSlotWell(), 4.f, SnareUiStyle::UiWoodButton(), 2.f));
	if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(Disc))
	{
		// 빈 Border 는 원하는 크기가 0 이다 — 칸을 꽉 채워야 홈이 보인다 (배낭 칸과 같은 이유).
		DiscSlot->SetHorizontalAlignment(HAlign_Fill);
		DiscSlot->SetVerticalAlignment(VAlign_Fill);
	}
	SnareUiStyle::AddGradeBand(*WidgetTree, *Stack, FName(*FString::Printf(TEXT("ShopRowGrade_%d"), Index)),
		SnareUiStyle::ItemGradeBandColor(Row0 != nullptr ? Row0->Grade : GachaRow0 != nullptr ? static_cast<EItemGrade>(GachaRow0->Grade)
			: EItemGrade::Common), DiscSize);
	if (Row0 != nullptr && !Row0->IconRef.IsNull())
	{
		UImage* const Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*FString::Printf(TEXT("ShopRowIcon_%d"), Index)));
		Icon->SetBrushFromSoftTexture(Row0->IconRef);
		if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Icon))
		{
			IconSlot->SetPadding(FMargin(6.f));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	USizeBox* const DiscBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("ShopRowDiscBox_%d"), Index)));
	DiscBox->SetWidthOverride(DiscSize);
	DiscBox->SetHeightOverride(DiscSize);
	DiscBox->AddChild(Stack);
	if (UHorizontalBoxSlot* const DiscSlot = Cast<UHorizontalBoxSlot>(Item->AddChild(DiscBox)))
	{
		DiscSlot->SetVerticalAlignment(VAlign_Center);
	}
	FText NameLabel = Line.Count > 1
		? FText::Format(LOCTEXT("ShopRowNameCount", "{0} x{1}"), Line.DisplayName, FText::AsNumber(Line.Count))
		: Line.DisplayName;
	if (Line.bPotatoSell && Line.PotatoPaidEach > 0)
	{
		NameLabel = FText::Format(LOCTEXT("PotatoPaidNote", "{0} (산 값 {1})"), NameLabel, FText::AsNumber(Line.PotatoPaidEach));
	}
	UTextBlock* const Name = MakeLabel(*WidgetTree, FName(*FString::Printf(TEXT("ShopRowName_%d"), Index)),
		NameLabel, RowNameFontSize, SnareUiStyle::UiTextOnButton());
	// 시트의 줄은 `가격 > 원판` 뿐이고 **이름 글자는 우리가 보탠 것**이다 — 자리가 모자라면 글자가 반쯤
	// 잘려 `손도ㄲ` 처럼 보였다(3층 캡처). 잘릴 때는 말줄임으로 끝내 "잘렸다" 는 것이 읽히게 한다.
	Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	if (UHorizontalBoxSlot* const NameSlot = Cast<UHorizontalBoxSlot>(Item->AddChild(Name)))
	{
		// 이름은 **남는 폭 전부**를 받는다 (M22 폰 결함 `손…`). 자동 폭이면 제 글 폭과 딱 같은 자리를 받는데,
		// 폰은 DPI 배율로 글을 다시 재서 반 픽셀만 모자라도 말줄임이 걸려 첫 글자만 남았다.
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		NameSlot->SetPadding(FMargin(LineGap, 0.f, 0.f, 0.f));
	}

	// 10-16: 사기·팔기 모두 「원판·이름 왼쪽 | 가격 오른쪽 끝」 — 목록을 훑을 때 이름 줄과 가격 줄이 각각 한 세로선에 선다.
	// 사는지 파는지는 탭이 말한다. 옛 `가격 > 아이템` 화살표 줄은 행 왼쪽 절반을 비워 가격이 가운데에 몰렸다.
	if (UHorizontalBoxSlot* const ItemSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Item)))
	{
		ItemSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ItemSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* const MoneySlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Money)))
	{
		MoneySlot->SetVerticalAlignment(VAlign_Center);
		MoneySlot->SetPadding(FMargin(LineGap, 0.f, LineGap, 0.f));
	}
	return Row;
}

void UShopScreenWidget::HandleRetryClicked()
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
