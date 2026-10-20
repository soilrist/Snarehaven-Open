#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CropProductRow.generated.h"

// ROADMAP M6-05a: 거점 **작물 슬롯**에 심는 작물 한 종을 적은 데이터 한 줄
// (DT_CropProductTable 의 행 구조체). 근거는 **DESIGN 8.2 확정 표**(감독 결정 2026-09-22 —
// 당근 15분 / 감자 30분 / 밀 60분). ⑩ 부터 작물은 한 번 거두면 칸이 비어(원본 레퍼런스) **미수확 상한이 없다**.
//
// `FRanchProductRow`(동물 생산) 와 **일부러 같은 짜임**이다 — 거점 슬롯은 컴포넌트가 하나고
// (`URanchComponent`, 결정 005-2 · DESIGN 8-2) 슬롯 타입만 갈린다. 칸 이름이 같아야 슬롯
// 코드(M6-05b)가 두 표를 같은 방식으로 읽는다. 새 컴포넌트·새 타이머를 만들지 않는다.
//
// **작물 ID 칸이 없다**(🟡 — `FRanchProductRow::AnimalID` 에 대응하는 칸). 동물은 "넣는 것(펫)"과
// "나오는 것(송로버섯)"이 달라 ID 가 둘 필요했지만, 작물은 심는 것과 거두는 것이 같은 종이다.
// 도감(DESIGN 8.5)도 농사 칸을 아이템 ID(`item.carrot`)로 세고 있어 `crop.*` 라는 새 이름 무리를
// 만들 이유가 없다 — 작물의 정체성은 `ProducedItemID` 다.
//
// **이 표의 숫자는 DESIGN 8.2 의 사본이다.** 한쪽만 고치면 2층
// `Snarehaven.Crop.Table.MatchesDesign` 이 운다 (M6-04a 위험구역 표 대조와 같은 짜임).
//
// **사이클당 코인(5·10·20)은 여기 없다.** 동물 쪽 코인 10 이 `DT_RanchProductTable` 에 없는 것과
// 같은 이유다 — 지급액은 백엔드가 판정한다 (DESIGN 4.1 권위 2단 표 · CLAUDE.md 8). 클라이언트가
// 코인 숫자를 들고 있으면 그게 곧 치트 경로다. 값은 DESIGN 8.2 표에 있고 CloudScript 가 읽는다.
USTRUCT(BlueprintType)
struct FCropProductRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 다 자라면 나오는 아이템 ID (`DT_ItemTable` 의 `ItemID`, 예: `item.carrot`). 이 행의 정체성이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crop")
	FName ProducedItemID;

	/**
	 * 심을 때 드는 씨앗 아이템 ID (예: `item.seed_carrot`). 대개 `item.seed_<작물>` 이지만 DESIGN 13.3 의
	 * 차(`item.seed_tea` → `item.tea_leaves`)·커피·참나무(`item.seed_oak` → `item.wood` 4개)처럼 이름이 달라
	 * 지어내지 않고 표에 적는다. 서버 `seedOf`(cloudscript_main.js)와 같은 짝이어야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crop")
	FName SeedItemID;

	/** 한 사이클(재배) 길이(분). DESIGN 8.2·13.3 = 15 / 30 / 60 / 120. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crop")
	int32 CycleMinutes = -1;
};
