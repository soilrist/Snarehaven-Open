#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RanchProductRow.generated.h"

// ROADMAP M2-02: 목장에 배치한 펫 한 종이 무엇을 얼마 만에 주는지 적은 데이터 한 줄
// (DT_RanchProductTable 의 행 구조체, 스키마는 DESIGN 4.3 "RanchProductTable" 그대로).
// 동물이 늘어도 코드가 아니라 행이 는다 (DataDriven.md 1).
//
// **이 행의 숫자는 판정이 아니다.** 목장 타이머 만료·수확 개수·지급은 전부 백엔드가 판정하고
// (DESIGN 3.2 "소유/권위"·4.1 권위 2단 표·CLAUDE.md 8), 클라이언트는 이 표를 화면 표시와
// 요청에만 쓴다. 클라이언트가 이 표를 보고 스스로 지급하면 그건 치트 경로다.
USTRUCT(BlueprintType)
struct FRanchProductRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 어떤 펫이 생산하는가. `DT_AnimalTable` 의 `AnimalID` (행 이름 `Ranch_Pig` ↔ `animal.pig`, DataDriven.md 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	FName AnimalID;

	/** 한 사이클에 주는 아이템 ID (`DT_ItemTable` 의 `ItemID`). 비어 있으면 아직 안 정한 행이다 — 그때는 생산이 켜지지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	FName ProducedItemID;

	/** 한 사이클 길이(분). DESIGN 3.2 = 30. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	int32 CycleMinutes = -1;

	/**
	 * 수확하지 않고 쌓아 둘 수 있는 최대 개수. DESIGN 3.2 "미수확 아이템 누적 상한" = 5
	 * (그 이상이면 생산 일시정지). **인벤토리 한 칸 스택 상한 99 와 다른 숫자다** —
	 * 그쪽은 `DT_ItemTable` 의 열로 M2-05 에서 들어간다 (DESIGN 3.3). 음수면 미정 취급.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	int32 MaxStack = -1;
};
