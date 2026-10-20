#pragma once

#include "CoreMinimal.h"
#include "Data/ItemRow.h"
#include "Data/RecipeRow.h"
#include "SnareCraftRules.generated.h"

class UDataTable;
struct FInventorySlot;
struct FSnarePlayerSnapshot;

/** 제작 화면이 어디서 열렸나 — 열린 자리에 맞는 레시피만 보인다 (DESIGN 8.10 "자리"). */
UENUM(BlueprintType)
enum class ESnareCraftPlace : uint8
{
	/** 배낭 제작 탭 — "어디서나" 레시피만. */
	Backpack,
	/** 내 텐트(작업대)·고급 작업대 앞 — 작업대 + 어디서나. */
	Workbench,
	/** 모닥불·정원 화로·그릴 위 — 요리만. */
	Fire,
	/** 제련소 옆 — 제련만 (DESIGN 8.16). */
	Smelter
};

/** 재료 한 칸이 지금 몇 개 있고 몇 개 드는지. */
USTRUCT(BlueprintType)
struct FSnareIngredientView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FName ItemID;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	ECraftTag Tag = ECraftTag::None;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	int32 Need = 0;

	/** 가방에 있는 수 (앞 재료가 먼저 쓴 몫은 뺀다 — 서버 `pickInputs` 와 같은 차례). */
	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	int32 Have = 0;

	bool IsEnough() const { return Have >= Need; }
};

/** 제작 화면 한 줄. */
USTRUCT(BlueprintType)
struct FSnareRecipeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FName RecipeRowName;

	/** 서버 `CraftItem` 의 `recipeId` — CloudScript `RECIPES` 는 결과 아이템 ID 로 열린다. */
	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FName ResultItemID;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FText ResultName;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	int32 ResultCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	ERecipeStation Station = ERecipeStation::Workbench;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	FName RequiredBlueprintID;

	/** ★ 설계도가 없다. */
	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	bool bLocked = false;

	/** 작업대 레시피인데 내 텐트가 없다 — 서버 `CraftItem` 이 `locked` 로 거절할 줄 (M107-05). 표시용. */
	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	bool bNeedsTent = false;

	UPROPERTY(BlueprintReadOnly, Category = "Craft")
	TArray<FSnareIngredientView> Ingredients;

	bool HasAllIngredients() const
	{
		for (const FSnareIngredientView& Ingredient : Ingredients)
		{
			if (!Ingredient.IsEnough())
			{
				return false;
			}
		}
		return true;
	}

	/** 지금 누르면 서버에 물어볼 만한가 (판정은 여전히 서버). */
	bool CanCraft() const { return !bLocked && !bNeedsTent && HasAllIngredients(); }
};

// 제작 화면이 보여 주는 "있다/모자라다·잠김" 을 표와 스냅샷만으로 계산한다. 판정은 서버 `CraftItem` 이다
// (CLAUDE.md 8) — 이 결과로는 버튼 색만 정한다. 2층 검사가 월드 없이 부른다.
namespace SnareCraftRules
{
	SNAREHAVEN_API int32 CountItem(const TArray<FInventorySlot>& Slots, FName ItemID);

	/** DT_ItemTable 을 ItemID → 행으로 편다 (행 이름과 ItemID 가 달라서). */
	SNAREHAVEN_API void IndexItems(const UDataTable* ItemTable, TMap<FName, const FItemRow*>& OutByID);

	/**
	 * ★ 설계도를 가졌나. 서버 `BuyItem` 이 설계도를 내부 목록과 도감에 같이 적으므로 도감 칸을 본다
	 * (가방에 설계도 아이템이 있어도 인정). 서버 내부 목록은 클라이언트가 못 읽는다 — 어긋나면 서버가 `locked` 로 답한다.
	 */
	SNAREHAVEN_API bool OwnsBlueprint(const FSnarePlayerSnapshot& Snapshot, FName BlueprintID);

	/** 이 자리에서 이 레시피가 보이나. */
	SNAREHAVEN_API bool IsStationOpen(ERecipeStation Station, ESnareCraftPlace Place);

	SNAREHAVEN_API FSnareRecipeView BuildRecipeView(FName RowName, const FRecipeRow& Row,
		const FSnarePlayerSnapshot& Snapshot, const TMap<FName, const FItemRow*>& ItemsByID);

	/** 자리에 맞는 레시피 전부 (표 순서 그대로). */
	SNAREHAVEN_API void BuildRecipeList(const UDataTable* RecipeTable, const UDataTable* ItemTable,
		const FSnarePlayerSnapshot& Snapshot, ESnareCraftPlace Place, TArray<FSnareRecipeView>& OutViews);

	/** 내 텐트가 없으면 작업대 레시피 줄에 `bNeedsTent` 를 켠다 — 서버 `CraftItem` 의 「작업대가 없다(텐트 먼저)」와 같은 조건. */
	SNAREHAVEN_API void MarkTentNeeded(TArray<FSnareRecipeView>& Views, bool bHasTent);

	/** 태그 재료 이름 ("나무류" 같은 묶음 이름). */
	SNAREHAVEN_API FText TagLabel(ECraftTag Tag);

	/**
	 * 서버가 제작을 확정했을 때 그 자리에서 한 번 나는 소리 (M28-02, DESIGN 8.10 「제작 확정 소리」).
	 * 작업대·배낭 = 망치 `CraftHammer`, 제련소 = 쉭 `CraftSmelt`, 불 = 지글 `CraftSizzle` (M52-02, DESIGN 8.10 「제작 소리」 표).
	 */
	SNAREHAVEN_API FName ConfirmCue(ESnareCraftPlace Place);

	/** `ConfirmCue` 소리의 크기 — 망치·제련 0.8, 지글 0.6 (같은 표). */
	SNAREHAVEN_API float ConfirmVolume(ESnareCraftPlace Place);
}
