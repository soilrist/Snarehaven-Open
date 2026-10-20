#pragma once

#include "CoreMinimal.h"
#include "Data/GachaRateRow.h"
#include "Engine/DataTable.h"
#include "GachaPoolRow.generated.h"

// ROADMAP M7-07: 올가미 기계 뽑기 풀 한 줄 (DT_GachaPool 의 행 구조체).
// **원본은 `docs/GACHA_DISCLOSURE.md` "아이템별 확률" 표다** — `tools/unreal/create_data_tables.py`(`read_gacha_pool_rows`) 가
// 그 표를 그대로 읽어 채운다. 서버 풀(`cloudscript_main.js` `GACHA_POOL`)과 ID 가 같은지는 2층 검사가 본다.
//
// **이 표도 판정이 아니다.** 어느 아이템이 나오는지는 CloudScript `RollGacha` 가 굴린다(DESIGN 10.5).
// 클라이언트는 이 표로 ① 아이템별 확률을 화면에 적고 ② 응답의 `itemId` 를 이름으로 바꾸기만 한다.
USTRUCT(BlueprintType)
struct FGachaPoolRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 서버 `GACHA_POOL[].id` · PlayFab 카탈로그 ID 와 같은 문자열 (`gacha.pet_ribbon`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	FName ItemID;

	/** 공개 문서의 이름 그대로 (`펫 리본 목줄`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	EGachaGrade Grade = EGachaGrade::Common;

	/** 공개 문서의 카테고리 (`펫 의상`·`목장 데코`·`희귀 소재`·`재화 환급`, DESIGN 10.3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	FText Category;

	/** 개별 확률(백분율). 등급 확률을 등급 안에서 균등 배분한 값 — 문서 표기 그대로. 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	float ProbabilityPct = -1.0f;

	/** 잡화점에 하나 팔 때 코인 — 희귀 소재만 > 0 (DESIGN 10.2 「희귀 소재 판매가」, 서버 `FIXED_SELL_PRICE`). 0 = 못 판다.
	 *  소재는 아이템 표 행이 없어 상점 팔기 탭이 이 값을 보여 준다. 실제 지급은 서버 `SellItem` 이 정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	int32 SellPrice = 0;
};
