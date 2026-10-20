#pragma once

#include "CoreMinimal.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "CrateRow.generated.h"

// DESIGN 10.8 꾸미기 상자 한 줄 = 그 상자에서 나올 수 있는 물건 하나 (DT_CrateTable 의 행 구조체).
// **이 표는 판정이 아니다** — 뽑기는 CloudScript `OpenCrate` 가 서버 `CRATE_POOLS` 로 한다(CLAUDE.md 8).
// 클라이언트는 ① 확률 고지(10.4) ② 응답 `itemId` 를 이름·등급으로 바꾸기 ③ 열쇠 젬 값 표시에만 쓴다.
// 계절이 바뀌면 새 상자 ID 와 `Season` 값으로 행을 더한다 (레퍼런스 = 계절마다 DropTable 이 따로).
USTRUCT(BlueprintType)
struct FCrateRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 이 줄이 속한 상자 (`item.crate_autumn26`). 배낭에서 이 ID 를 누르면 상자 화면이 열린다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	FName CrateItemID;

	/** 계절 열 (`autumn26`). 같은 상자의 줄은 다 같은 값이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	FName Season;

	/** 나오는 꾸미기 (`DT_ItemTable` 의 `ItemID`) — 효과 없는 것만 (DESIGN 10.3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	FName ItemID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	EItemGrade Grade = EItemGrade::Common;

	/** 이 물건이 나올 확률(백분율). 한 상자의 줄을 더하면 100. 서버 `chance × 100` 과 같다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	float ChancePct = 0.0f;

	/** 열쇠 1개 젬 값 (DESIGN 10.8 「열쇠 젬 값」 사본 — 차감은 서버 `BuyItem` 이 한다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	int32 KeyGemPrice = 0;
};
