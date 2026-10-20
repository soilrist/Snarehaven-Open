#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GachaCostRow.generated.h"

// ROADMAP M7-01: 올가미 기계의 비용과 천장 (DT_GachaConfig 의 행 구조체, 행 하나짜리 표다).
// 등급표(`DT_GachaRates`)와 표를 가른 이유는 **값의 단위가 다르기 때문**이다 — 비용·천장은 등급마다
// 다른 값이 아니라 기계 전체에 하나씩이라, 등급 행에 끼워 넣으면 같은 숫자가 네 번 적히고
// 넷 중 하나만 고쳐도 아무도 모르는 사고가 난다 (M7-02 가 막으려는 바로 그 사고).
//
// 숫자 원본은 `docs/GACHA_DISCLOSURE.md`(DESIGN 10.2) 다. **판정은 백엔드 몫이다** —
// 차감도 천장 카운트도 CloudScript 가 한다 (DESIGN 10.5 · CLAUDE.md 8). 클라이언트는 버튼에
// "젬 100" 이라고 적는 데만 쓴다.
USTRUCT(BlueprintType)
struct FGachaCostRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 1회 뽑기 — 젬. DESIGN 10.2 = 100. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	int32 SinglePullGems = -1;

	/** 1회 뽑기 — 스네어 코인. DESIGN 10.2 = 500 (유료 쪽이 5배 효율적이도록 일부러 비싸다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	int32 SinglePullCoins = -1;

	/** 10연차 — 젬. DESIGN 10.2 = 900 (1회씩 살 때보다 10% 할인). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	int32 TenPullGems = -1;

	/** 천장. DESIGN 10.2 = 50회 누적 레전더리 미획득 시 50회째 확정 지급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	int32 PityPullCount = -1;
};
