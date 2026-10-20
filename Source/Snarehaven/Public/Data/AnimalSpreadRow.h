#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AnimalSpreadRow.generated.h"

// INBOX 지시(감독 2026-09-23 10:45) ★4 — **동물을 맵 전역에 흩어 두는** 한 존의 수치 한 줄
// (DT_AnimalSpreadTable 의 행 구조체). 숫자 원본은 DESIGN 3.1 "동물 맵 전역 분산 스폰" 표다.
//
// **위험구역(8.1)과 반대 방향의 표다.** 위험구역은 한 점 둘레에 동물을 **모아** 밀도를 높이고
// (`FDangerZoneRow`), 이 표는 맵 전역에 **떼어** 놓는다 — 그래서 위험구역에 없는 칸이 둘 있다:
// 동물끼리 떨어져야 할 최소 간격과, 플레이어 시작점 둘레의 금지 반경이다.
// 두 스포너는 서로를 안 본다 (위험구역은 밀도만 높인다는 ★4 의 단서 그대로).
//
// **이 표의 숫자는 DESIGN 3.1 의 사본이다.** 한쪽만 고치면 2층
// `Snarehaven.AnimalSpread.Table.MatchesDesign` 이 운다 (M6-04a 위험구역 표와 같은 방식).
USTRUCT(BlueprintType)
struct FAnimalSpreadRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 어느 존에 흩는가. `DT_ZoneTable` 의 `ZoneID` (예: `zone.hunt`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	FName ZoneID;

	/** 흩을 영역의 반변(cm) — 정사각형 한 변의 절반. DESIGN 3.1 = 2500(`L_Hunt` 바닥 실측). 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	float AreaHalfExtentCm = -1.0f;

	/** 이 존에 동시에 서 있는 동물 수. DESIGN 3.1 = 16. 음수면 미정 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	int32 AnimalCount = -1;

	/** 동물끼리 떨어져야 할 최소 거리(cm). DESIGN 3.1 = 1200. 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	float MinSpacingCm = -1.0f;

	/** 플레이어 시작점 둘레에 동물을 두지 않는 반경(cm). DESIGN 3.1 = 1500. 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	float PlayerStartClearCm = -1.0f;

	/** 한 마리가 죽거나 잡힌 뒤 **다른 자리에** 다시 설 때까지(초). DESIGN 3.1 = 90. 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	int32 RespawnSeconds = -1;

	/**
	 * 시작점 둘레에 늘 세워 두는 **순한(도주만 하는) 동물** 수 (M21-01). DESIGN 3.1 = 시작점이 있는 존 2 · 그 밖 0.
	 * 흩는 정원(`AnimalCount`)에 안 들고 금지 반경도 안 받는다. 음수면 미정.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	int32 StarterAnimalCount = -1;

	/** 그 동물을 세우는 시작점에서의 거리(cm). DESIGN 3.1 = 800. 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimalSpread")
	float StarterRingCm = -1.0f;
};
