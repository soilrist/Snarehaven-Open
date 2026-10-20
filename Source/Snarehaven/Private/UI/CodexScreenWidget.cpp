#include "UI/CodexScreenWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Data/RecipeRow.h"
#include "Dom/JsonObject.h"
#include "UI/SnareClickProxy.h"
#include "UI/AchievementScreenWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Data/AnimalRow.h"
#include "Data/CropProductRow.h"
#include "Data/FishingRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/SnareUiStyle.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 자리표시 UI 배치 (Autonomy.md 🟡 — 게임 규칙 숫자가 아니라 "화면 어디에 얼마나 크게" 다).
	// 뽑기 화면과 같은 가로 한가운데 세로 한 줄 배치다 — 화면마다 다른 곳에 두면 눈이 매번 자리를 다시 찾는다.
	constexpr float PlaceholderTitleTop = 100.f;
	constexpr float PlaceholderRowHeight = 56.f;
	constexpr float PlaceholderPanelWidth = 640.f;
	constexpr float PlaceholderProgressTop = 164.f;
	// 10-16 접근성: 탭·제목 줄 단추 높이 88·72 → `MenuTouchMin`(121 = 아이폰 15 Pro 44pt, 1334×616 캡처 69px) — 그만큼 아래 줄을 민다.
	constexpr float PlaceholderRewardTop = 224.f;
	constexpr float PlaceholderTabTop = 288.f;
	constexpr float PlaceholderTabHeight = SnareUiStyle::MenuTouchMin;
	constexpr float PlaceholderSlotListTop = 416.f;
	// 칸 목록 높이 — 3층 캡처가 1117px 이고 목록이 392px 아래에서 시작하니 세로로 쓸 수 있는 것이 약 720px 다.
	// 옛 글자 줄 11줄(56px) 높이를 그대로 둔다 — 원판 칸 줄(92px)로 6줄, 1440 폭에서 한 줄 9칸이면 54칸이 든다.
	constexpr int32 PlaceholderRowsPerColumn = 11;
	constexpr float PlaceholderSlotListHeight = PlaceholderRowsPerColumn * PlaceholderRowHeight;
	constexpr float PlaceholderTabGap = 12.f;

	// M25-02 도감 원판 칸 (자리표시 UI 배치 🟡 — DESIGN 8.12 「도감 칸 원판」).
	/** 원판 지름 — 제작 판 레시피 원판·상점 원판과 같은 52 (같은 아이템이 화면마다 다른 크기로 보이지 않게). */
	constexpr float SlotDiscSize = 52.f;
	/** 원판 안 그림 여백 — 제작 판 `DiscIconPadding` 과 같은 6. */
	constexpr float SlotDiscIconPadding = 6.f;
	/** 칸 폭 — 원판 아래 이름(`북극 곤들매기` 일곱 자, 18pt)이 한 줄에 들어가는 폭. 넘치면 글자만 줄인다. */
	constexpr float SlotCellWidth = 128.f;
	/** 칸 사이 틈 (가로·세로 같음). */
	constexpr float SlotCellGap = 12.f;
	/** 원판과 이름 사이. */
	constexpr float SlotNameGap = 4.f;
	/** 칸 이름 글자 크기 — 판 글자(24)보다 작게, 원판이 칸의 주인공이 되게. */
	constexpr float SlotNameFontSize = 18.f;
	/** 못 모은 칸 그림 불투명도 — 무엇이 남았는지는 보이되 모은 칸과 한눈에 갈리게 (ROADMAP M25-02 🟡 0.25). */
	constexpr float UncollectedIconOpacity = 0.25f;
	// 전체 화면 판(#0174) 값은 업적 화면과 같이 쓰려고 `SnareUiStyle::AddScreenPanel` 로 옮겼다 (M13-31).
	/** 제목 줄 버튼(업적·닫기) 한 개 너비. 둘이 같은 너비라야 한 줄의 짝으로 읽힌다. */
	constexpr float CloseButtonWidth = 160.f;
	/**
	 * 판 안쪽 가로 여백 — 제목 줄 버튼·보상 줄·탭 막대·칸 목록이 전부 이 안에 선다(#0179).
	 * 너비를 숫자로 박지 않고 판 안쪽을 채운다 — 가로 슬레이트 폭은 기기 비율마다 1440(4:3)~2340(19.5:9)이라
	 * 1440 고정 막대는 4:3 에서 판 테두리를 넘었다.
	 */
	constexpr float PanelContentInset = SnareUiStyle::ScreenPanelInset + 32.f;

	FText CategoryLabel(ECodexCategory Category)
	{
		switch (Category)
		{
		case ECodexCategory::Fishing: return LOCTEXT("CodexCategoryFishing", "낚시");
		case ECodexCategory::Hunt: return LOCTEXT("CodexCategoryHunt", "사냥");
		case ECodexCategory::Plant: return LOCTEXT("CodexCategoryPlant", "식물");
		case ECodexCategory::Blueprint: return LOCTEXT("CodexCategoryBlueprint", "설계도");
		case ECodexCategory::Cooking: return LOCTEXT("CodexCategoryCooking", "요리");
		case ECodexCategory::Variant: return LOCTEXT("CodexCategoryVariant", "변종");
		default: return FText::GetEmpty();
		}
	}

	/** 선택된 탭 강조 — `ApplyUiStyle` 이 모든 버튼을 기본색으로 칠한 **뒤에** 부른다. */
	void MarkSelected(UButton* Button, bool bSelected)
	{
		if (!IsValid(Button))
		{
			return;
		}
		SnareUiStyle::MarkSelectedTab(Button, bSelected);
	}
}

UCodexScreenWidget::UCodexScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 로스터(지금 27칸)는 코드가 아니라 표가 안다 (DataDriven.md 1 · engines/unreal.md 코드 규칙 4).
	static ConstructorHelpers::FObjectFinder<UDataTable> AnimalTableAsset(
		TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	if (AnimalTableAsset.Succeeded())
	{
		AnimalTable = AnimalTableAsset.Object;
	}

	// 작물 표는 목장 표와 같은 폴더다 — 둘 다 거점 슬롯 하나가 읽는 표다 (결정 005-2 · DESIGN 8.2).
	static ConstructorHelpers::FObjectFinder<UDataTable> CropTableAsset(
		TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable"));
	if (CropTableAsset.Succeeded())
	{
		CropTable = CropTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> FishingTableAsset(
		TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable"));
	if (FishingTableAsset.Succeeded())
	{
		FishingTable = FishingTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> RecipeTableAsset(
		TEXT("/Game/Game/Data/Crafting/DT_RecipeTable.DT_RecipeTable"));
	if (RecipeTableAsset.Succeeded())
	{
		RecipeTable = RecipeTableAsset.Object;
	}
}

bool UCodexScreenWidget::IsCodexKnown() const
{
	// 주인(`PlayFabId`)이 없으면 응답도 캐시도 아직 없다는 뜻이다 — 그때는 0 칸을 보여 주지 않는다
	// (`SnareBalanceHud::FormatBalanceText` 와 같은 판정).
	// 자리표시 모드(3층 캡처)는 "안다" 로 친다 — 아니면 찬 칸을 그려 놓고 진행률만 `-` 로 뜬다.
	return bDemoMode || (IsValid(WatchedBackend) && WatchedBackend->GetPlayerSnapshot().HasOwner());
}

const FItemRow* UCodexScreenWidget::FindItemRow(FName ItemID) const
{
	if (!IsValid(ItemTable) || ItemID.IsNone())
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

FText UCodexScreenWidget::FindItemDisplayName(FName ItemID) const
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

	// 이름을 모르면 ID 를 그대로 보여 준다 — 빈 줄로 두면 그 칸이 도감에 없는 것처럼 보이고,
	// 그럴싸한 이름을 지어내면 표를 고쳐도 화면이 안 따라간다 (Verification.md 7).
	return FText::FromName(ItemID);
}

TArray<FCodexSlotView> UCodexScreenWidget::BuildSlots() const
{
	TArray<FCodexSlotView> Slots;

	const bool bKnown = IsCodexKnown();
	// 백엔드가 없으면 빈 목록을 본다. 임시 배열을 삼항으로 만들면 참조 수명이 헷갈리니 하나 놓고 가리킨다.
	static const TArray<FName> NoEntries;
	const TArray<FName>& BackendEntries = IsValid(WatchedBackend)
		? WatchedBackend->GetPlayerSnapshot().CodexEntries : NoEntries;
	const TArray<FName>& Collected = bDemoMode ? DemoEntries : BackendEntries;

	// 칸 하나를 만들어 넣는다. 찬 칸 판정은 **백엔드가 준 배열에 있는가** 하나뿐이다 (CLAUDE.md 8).
	// 같은 ID 가 두 번 오면(깃털은 새 네 종이 다 떨군다) 한 칸만 — 서버 `CODEX_CATEGORIES` 도 한 번씩 센다.
	TSet<FName> Seen;
	auto AddSlot = [&Slots, &Collected, &Seen, bKnown](FName EntryID, ECodexCategory Category, const FText& DisplayName,
		FName IconItemID = NAME_None)
	{
		bool bAlreadySeen = false;
		Seen.Add(EntryID, &bAlreadySeen);
		if (EntryID.IsNone() || bAlreadySeen)
		{
			return;
		}

		FCodexSlotView CodexSlot;
		CodexSlot.EntryID = EntryID;
		CodexSlot.Category = Category;
		CodexSlot.CategoryName = CategoryLabel(Category);
		CodexSlot.DisplayName = DisplayName;
		CodexSlot.IconItemID = IconItemID.IsNone() ? EntryID : IconItemID;
		CodexSlot.bCollected = bKnown && Collected.Contains(EntryID);
		Slots.Add(CodexSlot);
	};

	// 아이템 표 한 번 훑기 — 태그로 가르는 칸(식물의 나무·산딸기, 사냥의 깃털·뿔)을 찾을 때 쓴다.
	TMap<FName, const FItemRow*> ItemRows;
	if (IsValid(ItemTable))
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr)
			{
				ItemRows.Add(Row->ItemID, Row);
			}
		}
	}

	// 낚시 — 칸 ID 는 행 이름이 아니라 **생산물 아이템 ID** 다 (CODEX.md 표 A 끝줄 · M8-04 사고).
	if (IsValid(FishingTable))
	{
		for (const TPair<FName, uint8*>& Pair : FishingTable->GetRowMap())
		{
			if (const FFishingRow* const Row = reinterpret_cast<const FFishingRow*>(Pair.Value))
			{
				AddSlot(Row->ProducedItemID, ECodexCategory::Fishing, FindItemDisplayName(Row->ProducedItemID));
			}
		}
	}

	// 사냥 — 종(`AnimalID`) 먼저, 그다음 그 종이 떨구는 깃털·뿔(고기·가죽은 도감 칸이 아니다 — 원본 "깃털·트로피").
	if (IsValid(AnimalTable))
	{
		TArray<FName> TrophyIDs;
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			// 자리표시 종(돼지·소·양)은 기본 플레이에 안 서므로 칸이 아니다 — 서버 `HUNT_ANIMALS` 도 같다 (M5-07e).
			if (Row == nullptr || Row->bPlaceholderSpecies || Row->bStorySpecies)
			{
				continue;
			}
			// 종 행에는 그림이 없다 — 그 종 펫 아이템 행의 그림을 빌린다 (M25-02).
			AddSlot(Row->AnimalID, ECodexCategory::Hunt, Row->DisplayName, Row->PetItemID);
			for (const FName& DropID : Row->DeathDropItems)
			{
				const FItemRow* const* DropRow = ItemRows.Find(DropID);
				const ECraftTag Tag = DropRow != nullptr ? (*DropRow)->CraftTag : ECraftTag::None;
				if (Tag != ECraftTag::Meat && Tag != ECraftTag::Hide)
				{
					TrophyIDs.AddUnique(DropID);
				}
			}
		}
		for (const FName& TrophyID : TrophyIDs)
		{
			AddSlot(TrophyID, ECodexCategory::Hunt, FindItemDisplayName(TrophyID));
		}
	}

	// 식물 — 작물 생산물 + 채집물(나무·향나무·산딸기 태그). 채집물 목록을 여기 적지 않고 태그로 찾는다.
	if (IsValid(CropTable))
	{
		for (const TPair<FName, uint8*>& Pair : CropTable->GetRowMap())
		{
			if (const FCropProductRow* const Row = reinterpret_cast<const FCropProductRow*>(Pair.Value))
			{
				AddSlot(Row->ProducedItemID, ECodexCategory::Plant, FindItemDisplayName(Row->ProducedItemID));
			}
		}
	}
	for (const TPair<FName, const FItemRow*>& Pair : ItemRows)
	{
		const ECraftTag Tag = Pair.Value->CraftTag;
		if (Tag == ECraftTag::Wood || Tag == ECraftTag::Cedar || Tag == ECraftTag::Berry)
		{
			AddSlot(Pair.Key, ECodexCategory::Plant, FindItemDisplayName(Pair.Key));
		}
	}

	// 설계도 · 요리 — 레시피 표 하나에서. 설계도는 레시피가 요구하는 설계도 아이템, 요리는 불 레시피의 결과물.
	if (IsValid(RecipeTable))
	{
		for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
		{
			if (const FRecipeRow* const Row = reinterpret_cast<const FRecipeRow*>(Pair.Value))
			{
				AddSlot(Row->RequiredBlueprintID, ECodexCategory::Blueprint, FindItemDisplayName(Row->RequiredBlueprintID));
			}
		}
		for (const TPair<FName, uint8*>& Pair : RecipeTable->GetRowMap())
		{
			const FRecipeRow* const Row = reinterpret_cast<const FRecipeRow*>(Pair.Value);
			if (Row != nullptr && Row->Station == ERecipeStation::Fire)
			{
				AddSlot(Row->ResultItemID, ECodexCategory::Cooking, FindItemDisplayName(Row->ResultItemID));
			}
		}
	}

	// 변종 — 기본 플레이 종마다 변종 펫 하나(M21-07c1, DESIGN 10.6). 칸 ID 는 서버가 받는 순간 찍는 변종 펫 아이템 ID 그대로고,
	// 이름은 사냥 맵 이름 카드(`UAnimalStateComponent::GetCardName`)와 같은 `변종 ○○` 이다.
	if (IsValid(AnimalTable))
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row != nullptr && !Row->bPlaceholderSpecies && !Row->bStorySpecies)
			{
				AddSlot(Row->VariantPetItemID, ECodexCategory::Variant,
					FText::Format(LOCTEXT("CodexVariantSlotName", "변종 {0}"), Row->DisplayName));
			}
		}
	}

	return Slots;
}

TArray<FName> UCodexScreenWidget::CollectMountCodexIDs(const UDataTable* InAnimalTable, const UDataTable* InCropTable,
	const UDataTable* InFishingTable)
{
	TArray<FName> IDs;
	if (IsValid(InAnimalTable))
	{
		for (const TPair<FName, uint8*>& Pair : InAnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row != nullptr && !Row->bPlaceholderSpecies && !Row->bStorySpecies && !Row->AnimalID.IsNone())
			{
				IDs.AddUnique(Row->AnimalID);
			}
		}
	}
	if (IsValid(InCropTable))
	{
		for (const TPair<FName, uint8*>& Pair : InCropTable->GetRowMap())
		{
			const FCropProductRow* const Row = reinterpret_cast<const FCropProductRow*>(Pair.Value);
			if (Row != nullptr && !Row->ProducedItemID.IsNone())
			{
				IDs.AddUnique(Row->ProducedItemID);
			}
		}
	}
	if (IsValid(InFishingTable))
	{
		for (const TPair<FName, uint8*>& Pair : InFishingTable->GetRowMap())
		{
			const FFishingRow* const Row = reinterpret_cast<const FFishingRow*>(Pair.Value);
			if (Row != nullptr && !Row->ProducedItemID.IsNone())
			{
				IDs.AddUnique(Row->ProducedItemID);
			}
		}
	}
	return IDs;
}

bool UCodexScreenWidget::IsMountUnlocked(const FSnarePlayerSnapshot& Snapshot, const UDataTable* InAnimalTable,
	const UDataTable* InCropTable, const UDataTable* InFishingTable)
{
	const TArray<FName> Required = CollectMountCodexIDs(InAnimalTable, InCropTable, InFishingTable);
	if (Required.Num() == 0)
	{
		return false;
	}
	for (const FName& ID : Required)
	{
		if (!Snapshot.CodexEntries.Contains(ID))
		{
			return false;
		}
	}
	return true;
}

FText UCodexScreenWidget::GetProgressText() const
{
	const TArray<FCodexSlotView> Slots = BuildSlots();

	int32 CollectedCount = 0;
	for (const FCodexSlotView& CodexSlot : Slots)
	{
		CollectedCount += CodexSlot.bCollected ? 1 : 0;
	}

	// 분모는 표에서 읽어 낸 칸 수다 — 여기에 27 을 적으면 종이 늘었을 때 화면이 거짓말을 한다.
	// 분자는 백엔드에게 못 들었으면 `-` 다: 0 으로 적으면 "하나도 못 모았다" 로 읽힌다 (Verification.md 7).
	return FText::Format(LOCTEXT("CodexProgress", "도감 {0} / {1}"),
		IsCodexKnown() ? FText::AsNumber(CollectedCount) : LOCTEXT("CodexValueUnknown", "-"),
		FText::AsNumber(Slots.Num()));
}

UWidget* UCodexScreenWidget::MakeSlotCell(const FCodexSlotView& CodexSlot, int32 Index)
{
	// 배낭 칸·제작 판과 같은 짜임 — 원판 위 그림, 아래 이름. 모은 칸만 등급 색이고, 못 모은 칸은 빈 칸 색에 그림을 흐리게.
	const FItemRow* const Row = FindItemRow(CodexSlot.IconItemID);
	const FLinearColor DiscColor = CodexSlot.bCollected
		? SnareUiStyle::ItemGradeColor(Row != nullptr ? Row->Grade : EItemGrade::Common)
		: SnareUiStyle::UiEmptySlot();

	UOverlay* const Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotStack_%d"), Index)));
	UBorder* const Disc = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotDisc_%d"), Index)));
	Disc->SetBrush(FSlateRoundedBoxBrush(DiscColor, SlotDiscSize * 0.5f));
	if (UOverlaySlot* const DiscSlot = Stack->AddChildToOverlay(Disc))
	{
		// 빈 Border 는 원하는 크기가 0 이다 — 칸을 꽉 채워야 원판이 보인다 (배낭 칸과 같은 이유).
		DiscSlot->SetHorizontalAlignment(HAlign_Fill);
		DiscSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (Row != nullptr && !Row->IconRef.IsNull())
	{
		UImage* const Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*FString::Printf(TEXT("CodexSlotIcon_%d"), Index)));
		Icon->SetBrushFromSoftTexture(Row->IconRef);
		Icon->SetRenderOpacity(CodexSlot.bCollected ? 1.f : UncollectedIconOpacity);
		if (UOverlaySlot* const IconSlot = Stack->AddChildToOverlay(Icon))
		{
			IconSlot->SetPadding(FMargin(SlotDiscIconPadding));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	USizeBox* const DiscBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotDiscBox_%d"), Index)));
	DiscBox->SetWidthOverride(SlotDiscSize);
	DiscBox->SetHeightOverride(SlotDiscSize);
	DiscBox->AddChild(Stack);

	UTextBlock* const Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotName_%d"), Index)));
	Name->SetText(CodexSlot.DisplayName);
	Name->SetJustification(ETextJustify::Center);
	// 크림 판(M25-01) 위 진갈색 — 못 모은 칸은 같은 진갈색을 흐리게.
	Name->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor(!CodexSlot.bCollected)));
	SnareUiStyle::SetFontSize(*Name, SlotNameFontSize);
	// 긴 이름은 칸 밖으로 넘치지 않게 줄이기만 한다(#0199 탭 글자와 같은 방식).
	UScaleBox* const NameFit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotNameFit_%d"), Index)));
	NameFit->SetStretch(EStretch::ScaleToFit);
	NameFit->SetStretchDirection(EStretchDirection::DownOnly);
	NameFit->AddChild(Name);

	UVerticalBox* const Cell = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotCell_%d"), Index)));
	if (UVerticalBoxSlot* const DiscSlot = Cell->AddChildToVerticalBox(DiscBox))
	{
		DiscSlot->SetHorizontalAlignment(HAlign_Center);
	}
	if (UVerticalBoxSlot* const NameSlot = Cell->AddChildToVerticalBox(NameFit))
	{
		NameSlot->SetHorizontalAlignment(HAlign_Center);
		NameSlot->SetPadding(FMargin(0.f, SlotNameGap, 0.f, 0.f));
	}

	USizeBox* const CellBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
		FName(*FString::Printf(TEXT("CodexSlotCellBox_%d"), Index)));
	CellBox->SetWidthOverride(SlotCellWidth);
	CellBox->AddChild(Cell);
	return CellBox;
}

FText UCodexScreenWidget::GetTabText(ECodexCategory Category) const
{
	// 서버가 센 진척이 있으면 그것만 그린다 — 칸 수·퍼센트 모두 서버 몫이다 (DESIGN 8.3, CLAUDE.md 8).
	const FCodexCategoryProgress& Server = GetServerProgress(Category);
	if (!bDemoMode && bRewardsKnown && Server.bKnown)
	{
		return FText::Format(LOCTEXT("CodexTabServer", "{0}\n{1}/{2} {3}%"), CategoryLabel(Category),
			FText::AsNumber(Server.Have), FText::AsNumber(Server.Total), FText::AsNumber(Server.Percent));
	}

	int32 Total = 0;
	int32 Have = 0;
	for (const FCodexSlotView& CodexSlot : BuildSlots())
	{
		if (CodexSlot.Category == Category)
		{
			++Total;
			Have += CodexSlot.bCollected ? 1 : 0;
		}
	}
	// 서버 답 전에는 분자를 모른다 — 스냅샷을 세어 채우지 않는다(세면 서버와 다른 수가 뜰 수 있다).
	// 분모는 표 칸 수(로스터)다. 3층 캡처 자리표시(`-SnareCodexDemo=`)만 자리표시 칸을 센다 — 화면 글자뿐.
	return FText::Format(LOCTEXT("CodexTab", "{0}\n{1}/{2}"), CategoryLabel(Category),
		bDemoMode ? FText::AsNumber(Have) : LOCTEXT("CodexTabUnknown", "-"), FText::AsNumber(Total));
}

bool UCodexScreenWidget::ParseCodexRewards(const TSharedPtr<FJsonObject>& Response, FCodexServerReply& OutReply)
{
	const TSharedPtr<FJsonObject>* Rewards = nullptr;
	if (!Response.IsValid() || Response->HasField(TEXT("error"))
		|| !Response->TryGetObjectField(TEXT("rewards"), Rewards) || Rewards == nullptr || !Rewards->IsValid())
	{
		return false;
	}

	FCodexServerReply Reply;
	(*Rewards)->TryGetBoolField(TEXT("farSea"), Reply.bFarSea);
	(*Rewards)->TryGetBoolField(TEXT("dangerDock"), Reply.bDangerDock);
	(*Rewards)->TryGetBoolField(TEXT("specialMerchant"), Reply.bSpecialMerchant);

	// 서버 `CODEX_CATEGORIES` 키 — `ECodexCategory` 순서와 같다.
	static const TCHAR* const CategoryKeys[] = {
		TEXT("fishing"), TEXT("hunt"), TEXT("plant"), TEXT("blueprint"), TEXT("cooking"), TEXT("variant")};
	static_assert(UE_ARRAY_COUNT(CategoryKeys) == static_cast<int32>(ECodexCategory::Count), "갈래 키 수가 탭 수와 같아야 한다");

	const TSharedPtr<FJsonObject>* Categories = nullptr;
	if (Response->TryGetObjectField(TEXT("categories"), Categories) && Categories != nullptr && Categories->IsValid())
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(CategoryKeys); ++Index)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FCodexCategoryProgress& Out = Reply.Categories[Index];
			// 한 칸이라도 빠진 갈래는 모르는 채로 둔다 — 0 으로 채우면 "하나도 없다" 로 읽힌다.
			Out.bKnown = (*Categories)->TryGetObjectField(CategoryKeys[Index], Entry) && Entry != nullptr && Entry->IsValid()
				&& (*Entry)->TryGetNumberField(TEXT("have"), Out.Have)
				&& (*Entry)->TryGetNumberField(TEXT("total"), Out.Total)
				&& (*Entry)->TryGetNumberField(TEXT("percent"), Out.Percent);
		}
	}

	OutReply = Reply;
	return true;
}

FText UCodexScreenWidget::GetRewardText() const
{
	// 열림·잠김은 서버 답을 옮겨 적을 뿐이다 — 화면이 비율을 세서 판정하면 그게 곧 공짜 선착장이다.
	auto State = [this](bool bOpen)
	{
		return !bRewardsKnown ? LOCTEXT("CodexRewardUnknown", "-")
			: bOpen ? LOCTEXT("CodexRewardOpen", "열림") : LOCTEXT("CodexRewardLocked", "잠김");
	};
	return FText::Format(LOCTEXT("CodexRewards", "먼바다 {0}  |  위험구역 선착장 {1}  |  특별 상인 {2}"),
		State(ServerReply.bFarSea), State(ServerReply.bDangerDock), State(ServerReply.bSpecialMerchant));
}

FText UCodexScreenWidget::GetVariantDisclosureText() const
{
	return FormatVariantDisclosure(GetVariantChance());
}

float UCodexScreenWidget::GetVariantChance() const
{
	if (!IsValid(AnimalTable))
	{
		return 0.f;
	}
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row != nullptr && !Row->bPlaceholderSpecies && !Row->bStorySpecies)
		{
			return Row->VariantChance;
		}
	}
	return 0.f;
}

FText UCodexScreenWidget::FormatVariantDisclosure(float Chance)
{
	FNumberFormattingOptions Percent;
	Percent.SetMaximumFractionalDigits(1).SetUseGrouping(false);
	return FText::Format(LOCTEXT("CodexVariantDisclosure", "확률 고지: 변종 {0}% (동물이 설 때마다)"),
		FText::AsNumber(Chance * 100.f, &Percent));
}

void UCodexScreenWidget::RequestRewards()
{
	if (bRewardsPending || !IsValid(WatchedBackend))
	{
		return;
	}
	// 칸이 지난번 물은 것과 같으면 답도 같다 — 스냅샷 알림(재화·시작 장비 왕복)마다 다시 묻지 않는다.
	const TArray<FName>& Entries = WatchedBackend->GetPlayerSnapshot().CodexEntries;
	if (bRewardsAsked && AskedCodexEntries == Entries)
	{
		return;
	}
	bRewardsAsked = true;
	AskedCodexEntries = Entries;
	bRewardsPending = true;

	const TWeakObjectPtr<UCodexScreenWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetCodexRewards"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UCodexScreenWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bRewardsPending = false;
			FCodexServerReply Reply;
			if (!bOk || !ParseCodexRewards(Response, Reply))
			{
				// 못 들었으면 모르는 채로 둔다 — 잠김으로 적으면 "아직 모른다" 와 "못 열었다" 가 같은 화면이 된다.
				// 다음 스냅샷(로그인 뒤 첫 조회 등)에 다시 묻게 "물었다" 표시를 푼다.
				Self->bRewardsAsked = false;
				UE_LOG(LogSnarehaven, Log, TEXT("[Codex] 보상 판정을 못 받았다 — %s"), *Error);
				return;
			}
			Self->bRewardsKnown = true;
			Self->ServerReply = Reply;
			const FCodexCategoryProgress& Hunt = Reply.Categories[static_cast<int32>(ECodexCategory::Hunt)];
			UE_LOG(LogSnarehaven, Log, TEXT("[Codex] 서버 진척 — 사냥 %d/%d %d%%, 선착장 %s"),
				Hunt.Have, Hunt.Total, Hunt.Percent, Reply.bDangerDock ? TEXT("열림") : TEXT("잠김"));
			Self->RefreshDisplay();
			// 기다리는 동안 칸이 바뀌었으면 이 답은 옛 칸 것이다 — 한 번 더 묻는다(같으면 안 나간다).
			Self->RequestRewards();
		});
}

void UCodexScreenWidget::SetSelectedCategory(ECodexCategory Category)
{
	SelectedCategory = Category;
	RefreshDisplay();
}

void UCodexScreenWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}

	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(
			this, &UCodexScreenWidget::HandleBackendSnapshotChanged);
	}

	WatchedBackend = Backend;
	bRewardsKnown = false;
	bRewardsAsked = false;
	ServerReply = FCodexServerReply();

	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(
			this, &UCodexScreenWidget::HandleBackendSnapshotChanged);
	}

	RefreshDisplay();
	RequestRewards();
}

void UCodexScreenWidget::HandleBackendSnapshotChanged()
{
	RefreshDisplay();
	// 칸이 늘었으면 보상도 바뀌었을 수 있다 — 다시 묻는다(판정은 서버).
	RequestRewards();
}

void UCodexScreenWidget::SetSourceTables(UDataTable* InAnimalTable, UDataTable* InCropTable,
	UDataTable* InFishingTable, UDataTable* InItemTable, UDataTable* InRecipeTable)
{
	AnimalTable = InAnimalTable;
	CropTable = InCropTable;
	FishingTable = InFishingTable;
	ItemTable = InItemTable;
	RecipeTable = InRecipeTable;
	RefreshDisplay();
}

void UCodexScreenWidget::SetDemoFilledCount(int32 Count)
{
	// 로스터는 여기서도 표에서만 온다 — 자리표시라고 ID 를 손으로 적으면 표가 바뀔 때 조용히 어긋난다.
	bDemoMode = true;
	DemoEntries.Reset();
	for (const FCodexSlotView& CodexSlot : BuildSlots())
	{
		if (DemoEntries.Num() >= Count)
		{
			break;
		}
		DemoEntries.Add(CodexSlot.EntryID);
	}

	RefreshDisplay();
}

UCodexScreenWidget* UCodexScreenWidget::OpenOnViewport(APlayerController* Owner, UWorld* World, USnareBackendSubsystem* Backend)
{
	UCodexScreenWidget* const Screen = IsValid(Owner) ? CreateWidget<UCodexScreenWidget>(Owner, StaticClass())
		: IsValid(World) ? CreateWidget<UCodexScreenWidget>(World, StaticClass()) : nullptr;
	if (Screen == nullptr)
	{
		return nullptr;
	}
	Screen->BindToBackend(Backend);
	// 전체 화면 판이라 HUD(0)·제작 판(1)보다 위라야 한다 — HUD 가 나중에 붙어도 판을 뚫고 올라오지 않게.
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	return Screen;
}

UAchievementScreenWidget* UCodexScreenWidget::OpenAchievementScreen()
{
	UAchievementScreenWidget* const Screen = GetOwningPlayer() != nullptr
		? CreateWidget<UAchievementScreenWidget>(GetOwningPlayer(), UAchievementScreenWidget::StaticClass())
		: CreateWidget<UAchievementScreenWidget>(GetWorld(), UAchievementScreenWidget::StaticClass());
	if (Screen == nullptr)
	{
		return nullptr;
	}
	// 같은 백엔드를 본다 — 업적 값도 스냅샷에서만 온다 (CLAUDE.md 8).
	Screen->BindToBackend(WatchedBackend);
	Screen->SetReturnScreen(this);
	// 도감과 같은 판 Z — 기본 0 이면 출석(5) 버튼이 업적 판 위로 그려졌다 (M26-03).
	Screen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
	SetVisibility(ESlateVisibility::Collapsed);
	return Screen;
}

void UCodexScreenWidget::RefreshDisplay()
{
	if (IsValid(TitleText))
	{
		TitleText->SetText(LOCTEXT("CodexTitle", "도감"));
	}

	if (IsValid(ProgressText))
	{
		ProgressText->SetText(GetProgressText());
	}

	if (IsValid(RewardText))
	{
		// M21-07g — 변종 탭은 보상 줄 대신 확률 고지 줄(DESIGN 10.6, 상자 「확률 보기」 와 같은 10.4 표시 원칙).
		const bool bVariantTab = SelectedCategory == ECodexCategory::Variant;
		RewardText->SetText(bVariantTab ? GetVariantDisclosureText() : GetRewardText());
		// M27-05 — 서버 답 전엔 보상 줄을 접는다. `먼바다 -  |  …` 는 뜻 없는 줄이고, 열림·잠김은 서버만 안다.
		// 변종 탭 고지는 표 값이라 답과 상관없이 보인다.
		RewardText->SetVisibility(bVariantTab || bRewardsKnown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// 탭 다섯 — 매번 다시 세운다(칸 수 글자가 바뀌므로). 30개 남짓이라 싸다.
	TArray<UButton*> TabButtons;
	if (IsValid(TabBar) && WidgetTree != nullptr)
	{
		TabBar->ClearChildren();
		TabProxies.Reset();
		for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
		{
			const ECodexCategory Category = static_cast<ECodexCategory>(Index);
			UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
				FName(*FString::Printf(TEXT("CodexTabButton_%d"), Index)));
			// M26-05 시트 「모든 패널 모서리 둥글게」 — 모양만 바꾸고 색은 아래 `ApplyUiStyle`·`MarkSelected` 몫.
			SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
			UTextBlock* const Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
				FName(*FString::Printf(TEXT("CodexTabText_%d"), Index)));
			Label->SetText(GetTabText(Category));
			Label->SetJustification(ETextJustify::Center);
			// #0199 좁은 폭·두 줄 글에서 탭 글이 버튼 밖으로 넘치거나 잘리지 않게 — 넘칠 때만 줄인다.
			UScaleBox* const Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
			Fit->SetStretch(EStretch::ScaleToFit);
			Fit->SetStretchDirection(EStretchDirection::DownOnly);
			Fit->AddChild(Label);
			Button->AddChild(Fit);

			USnareClickProxy* const Proxy = NewObject<USnareClickProxy>(this);
			Proxy->Key = FName(*FString::FromInt(Index));
			const TWeakObjectPtr<UCodexScreenWidget> WeakThis(this);
			Proxy->OnClicked = [WeakThis, Category](FName)
			{
				if (UCodexScreenWidget* const Self = WeakThis.Get())
				{
					Self->SetSelectedCategory(Category);
				}
			};
			Button->OnClicked.AddDynamic(Proxy, &USnareClickProxy::HandleClicked);
			TabProxies.Add(Proxy);
			TabButtons.Add(Button);

			if (UHorizontalBoxSlot* const TabSlot = Cast<UHorizontalBoxSlot>(TabBar->AddChild(Button)))
			{
				TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				// 탭 사이에만 틈 — 마지막 탭 뒤 틈은 줄 끝을 안쪽으로 밀기만 한다.
				const bool bLast = Index == static_cast<int32>(ECodexCategory::Count) - 1;
				TabSlot->SetPadding(FMargin(0.f, 0.f, bLast ? 0.f : PlaceholderTabGap, 0.f));
			}
		}
	}

	if (!IsValid(SlotList) || WidgetTree == nullptr)
	{
		return;
	}

	// 칸을 남겨 두고 덧쓰면 표가 줄었을 때 옛 칸이 화면에 남는다 — 매번 다시 세운다.
	// 30칸쯤이라 지우고 만드는 값이 싸다 (`UGachaScreenWidget::ShowPullResults` 와 같은 방식).
	SlotList->ClearChildren();

	// M25-02 — 원판 칸을 판 폭만큼 옆으로 놓고 넘치면 다음 줄로(시트 「원판 위 그림」). 묶음 머리글 줄은 없다 —
	// 고른 탭 하나만 그리고 그 탭이 이미 강조돼 있다. 칸 수는 표가 정하므로 여기서 세지 않는다.
	UWrapBox* const Grid = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("CodexSlotGrid"));
	Grid->SetInnerSlotPadding(FVector2D(SlotCellGap, SlotCellGap));
	if (UHorizontalBoxSlot* const GridSlot = Cast<UHorizontalBoxSlot>(SlotList->AddChild(Grid)))
	{
		GridSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	int32 CellIndex = 0;
	for (const FCodexSlotView& CodexSlot : BuildSlots())
	{
		// 고른 탭 칸만 — 나머지 탭은 머리글의 `찬 칸/칸 수` 로만 보인다.
		if (CodexSlot.Category == SelectedCategory)
		{
			Grid->AddChildToWrapBox(MakeSlotCell(CodexSlot, CellIndex++));
		}
	}

	// 이 줄들은 화면이 뜬 **뒤에** 생긴다 — `RebuildWidget` 때 한 번 칠한 글꼴이 안 닿는다 (M5-05a·b).
	// 글꼴만 다시 입히고 위에서 고른 글자 색은 그대로 남는다 (`ApplyUiStyle` 은 버튼 밖 글자 색을 안 건드린다).
	SnareUiStyle::ApplyUiStyle(*this);
	for (int32 Index = 0; Index < TabButtons.Num(); ++Index)
	{
		MarkSelected(TabButtons[Index], static_cast<ECodexCategory>(Index) == SelectedCategory);
	}
}

TSharedRef<SWidget> UCodexScreenWidget::RebuildWidget()
{
	if (NeedsPlaceholderTree())
	{
		BuildPlaceholderTree();
	}

	RefreshDisplay();

	// `Super` **뒤**라야 한다 — Slate 위젯이 생긴 뒤에 칠해야 화면까지 바뀐다 (M5-05a·b).
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	// 탭 강조까지 다시 칠하려고 한 번 더 그린다 (`RefreshDisplay` 끝이 `ApplyUiStyle` → 강조 순서다).
	RefreshDisplay();
	return Built;
}

void UCodexScreenWidget::NativeDestruct()
{
	// 듣기 시작한 곳이 풀어 준다 (engines/unreal.md 코드 규칙 5) — 안 풀면 화면이 사라진 뒤에도
	// 백엔드가 죽은 위젯을 부른다.
	BindToBackend(nullptr);
	Super::NativeDestruct();
}

bool UCodexScreenWidget::NeedsPlaceholderTree() const
{
	if (WidgetTree == nullptr)
	{
		return false;
	}

	if (WidgetTree->RootWidget == nullptr)
	{
		return true;
	}

	// 새로 만든 위젯 블루프린트의 빈 CanvasPanel 은 아직 디자인이 아니라 빈 도화지다.
	const UPanelWidget* const RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	return RootPanel != nullptr && RootPanel->GetChildrenCount() == 0;
}

void UCodexScreenWidget::BuildPlaceholderTree()
{
	// WBP 가 만들어 둔 빈 캔버스가 있으면 그걸 쓴다 (루트를 갈아 끼우면 WBP 쪽 설정이 날아간다).
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (Root == nullptr)
	{
		Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CodexScreenRoot"));
		WidgetTree->RootWidget = Root;
	}

	// 판은 **맨 먼저** 넣는다 — 캔버스는 나중에 넣은 것이 위에 그려지니 글자가 판 위에 온다.
	// 둘 다 `Visible` 이라 눌림을 먹는다 — 도감이 열린 동안 뒤 HUD 버튼·조이스틱이 안 눌린다.
	SnareUiStyle::AddScreenPanel(*WidgetTree, *Root, TEXT("CodexBackdrop"), TEXT("CodexPanel"));

	// 가로 한가운데 한 줄씩 쌓는다 — 제목 → 진행률 → 칸 목록 순으로 읽힌다.
	auto AddCenteredChild = [this, Root](UWidget* Child, float Top, float Height, float Width = PlaceholderPanelWidth)
	{
		if (UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Child)))
		{
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.f));
			CanvasSlot->SetPosition(FVector2D(0.f, Top));
			CanvasSlot->SetSize(FVector2D(Width, Height));
		}
	};

	// 넓은 줄(보상·탭·칸 목록)은 판 안쪽 가로를 꽉 채운다 — 좌우 앵커를 벌려 두면 화면 폭이 바뀌어도
	// 양쪽 여백만 지키고 줄 너비가 따라 준다. 탭·칸은 가로 상자의 Fill 이라 같이 줄고 는다.
	auto AddContentWideChild = [Root](UWidget* Child, float Top, float Height)
	{
		if (UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Child)))
		{
			CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
			CanvasSlot->SetOffsets(FMargin(PanelContentInset, Top, PanelContentInset, Height));
		}
	};

	// 제목 탭(M24-04) — 판 안 가운데 글자 대신 배낭·제작·출석 판과 같은 진갈색 알약을 판 왼쪽 위 모서리에 걸친다.
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	USizeBox* const TitlePillBox = SnareUiStyle::MakePanelTitlePill(*WidgetTree, TEXT("CodexTitlePillBox"), TEXT("CodexTitlePill"), *TitleText);
	if (UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Root->AddChild(TitlePillBox)))
	{
		PillSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		PillSlot->SetPosition(FVector2D(SnareUiStyle::ScreenPanelInset, SnareUiStyle::ScreenPanelInset - SnareUiStyle::MenuTitlePillRise));
		PillSlot->SetAutoSize(true);
	}

	// 제목 줄 오른쪽 버튼 묶음 (#0176) — 업적·닫기를 **한 가로 상자**에 넣는다. 전엔 캔버스에 따로 꽂아
	// 업적은 제목 높이(100), 닫기는 판 모서리 높이(72)라 약 30px 어긋나 보였다. 한 상자면 세로 자리는
	// 상자 하나가 정하니 둘이 다시 갈라질 수 없다.
	UHorizontalBox* const TitleButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TitleButtonRow"));
	if (UCanvasPanelSlot* const CanvasSlot = Cast<UCanvasPanelSlot>(Root->AddChild(TitleButtonRow)))
	{
		CanvasSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		CanvasSlot->SetAlignment(FVector2D(1.f, 0.f));
		CanvasSlot->SetPosition(FVector2D(-PanelContentInset, PlaceholderTitleTop));
		CanvasSlot->SetSize(FVector2D(2.f * CloseButtonWidth + PlaceholderTabGap, SnareUiStyle::MenuTouchMin));
	}
	auto AddTitleButton = [this, TitleButtonRow](FName ButtonName, FName LabelName, const FText& Label, float LeftGap)
	{
		UButton* const Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
		SnareUiStyle::MakeRoundButton(*Button, SnareUiStyle::HudCornerRadius);
		UTextBlock* const Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
		Text->SetText(Label);
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
		if (UHorizontalBoxSlot* const BoxSlot = TitleButtonRow->AddChildToHorizontalBox(Button))
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BoxSlot->SetVerticalAlignment(VAlign_Fill);
			BoxSlot->SetPadding(FMargin(LeftGap, 0.f, 0.f, 0.f));
		}
		return Button;
	};

	// "업적" (M12-06) — 도감과 업적은 둘 다 "모은 기록" 이라 같은 자리에서 연다.
	UButton* const AchievementButton = AddTitleButton(TEXT("AchievementButton"), TEXT("AchievementLabel"),
		LOCTEXT("CodexAchievementButton", "업적"), 0.f);
	AchievementProxy = NewObject<USnareClickProxy>(this);
	const TWeakObjectPtr<UCodexScreenWidget> WeakThis(this);
	AchievementProxy->OnClicked = [WeakThis](FName)
	{
		if (UCodexScreenWidget* const Self = WeakThis.Get())
		{
			Self->OpenAchievementScreen();
		}
	};
	AchievementButton->OnClicked.AddDynamic(AchievementProxy, &USnareClickProxy::HandleClicked);

	// "닫기" — 업적 화면 닫기와 같은 방식(화면을 떼어 낸다 → NativeDestruct 가 백엔드 구독을 푼다).
	UButton* const CloseButton = AddTitleButton(TEXT("CodexCloseButton"), TEXT("CodexCloseLabel"),
		LOCTEXT("CodexClose", "닫기"), PlaceholderTabGap);
	CloseProxy = NewObject<USnareClickProxy>(this);
	CloseProxy->OnClicked = [WeakThis](FName)
	{
		if (UCodexScreenWidget* const Self = WeakThis.Get())
		{
			Self->RemoveFromParent();
		}
	};
	CloseButton->OnClicked.AddDynamic(CloseProxy, &USnareClickProxy::HandleClicked);

	// 진행률은 칸 목록보다 **위**다 — 스물일곱 줄을 세지 않고도 얼마나 모았는지 한눈에 보여야 한다.
	ProgressText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProgressText"));
	ProgressText->SetJustification(ETextJustify::Center);
	ProgressText->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor()));
	AddCenteredChild(ProgressText, PlaceholderProgressTop, PlaceholderRowHeight);

	// 보상 줄은 진행률 바로 아래 — 50%·100% 에 무엇이 열리는지가 모으는 이유다.
	RewardText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RewardText"));
	RewardText->SetJustification(ETextJustify::Center);
	RewardText->SetColorAndOpacity(FSlateColor(SnareUiStyle::ScreenTextColor()));
	AddContentWideChild(RewardText, PlaceholderRewardTop, PlaceholderRowHeight);

	TabBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabBar"));
	AddContentWideChild(TabBar, PlaceholderTabTop, PlaceholderTabHeight);

	// 칸 줄은 **비운 채로** 세운다 — 줄도 세로 칸도 `RefreshDisplay` 가 표를 읽었을 때만 생긴다.
	SlotList = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotList"));
	AddContentWideChild(SlotList, PlaceholderSlotListTop, PlaceholderSlotListHeight);
}

#undef LOCTEXT_NAMESPACE
