#pragma once

#include "CoreMinimal.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "RecipeRow.generated.h"

// 레시피를 만들 수 있는 자리 (DESIGN 8.10 레시피 표 "자리" 열).
UENUM(BlueprintType)
enum class ERecipeStation : uint8
{
	/** 내 텐트에 붙은 작업대(또는 고급 작업대) 앞. */
	Workbench,
	/** 배낭 제작 탭, 어디서나 (모닥불·미끼 팩). */
	Anywhere,
	/** 모닥불·정원 화로·그릴 위 — 걸리는 시간은 불 행의 `FItemRow::CookSeconds`. */
	Fire,
	/** 제련소 옆 — 걸리는 시간은 제련소 행의 `FItemRow::CookSeconds` (DESIGN 8.16). */
	Smelter
};

// 재료 한 칸. `ItemID` 와 `Tag` 중 **하나만** 채운다 — "나무판 4" 는 ID, "고기 1" 은 태그.
USTRUCT(BlueprintType)
struct FRecipeIngredient
{
	GENERATED_BODY()

	/** 정확히 이 아이템 (`item.plank`). 태그로 받을 때는 비운다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName ItemID;

	/** 이 묶음에 드는 아무 아이템 (`FItemRow::CraftTag`). ID 로 받을 때는 `None`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	ECraftTag Tag = ECraftTag::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	int32 Count = 0;
};

// DESIGN 8.10 제작·요리: 레시피 한 줄 (DT_RecipeTable 의 행 구조체).
// **이 표의 숫자는 DESIGN 8.10 의 사본이다.** 재료 확인·차감·지급은 CloudScript `CraftItem(recipeId, times)`
// 한 호출이 판정한다 (CLAUDE.md 8) — 클라이언트는 이 표로 버튼을 켜고 끌 뿐이다.
// 제작 시간 칸은 없다 — 8.10 이 "0초(즉시)" 로 정했고, 요리 시간은 불마다 달라 불 행(`CookSeconds`)에 있다.
USTRUCT(BlueprintType)
struct FRecipeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** CloudScript `CraftItem` 이 받는 ID. 행 이름 `Recipe_Plank` ↔ `recipe.plank`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName RecipeID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TArray<FRecipeIngredient> Ingredients;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	ERecipeStation Station = ERecipeStation::Workbench;

	/** 이 설계도 아이템이 있어야 열린다 (8.10 ★). 비면 설계도가 필요 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName RequiredBlueprintID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName ResultItemID;

	/** 한 번 만들 때 나오는 개수 (산딸기 미끼 팩 = 3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	int32 ResultCount = 1;
};
