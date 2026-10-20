#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DangerZoneRow.generated.h"

// ROADMAP M6-04: 사냥존 안의 **위험구역(던전)** 한 곳을 적은 데이터 한 줄
// (DT_DangerZoneTable 의 행 구조체). 근거는 `docs/decisions/005`-3 · DESIGN 1.3 과
// **숫자를 확정한 DESIGN 8.1 표**(감독 결정 2026-09-22 — 반지름 500cm · 동물 5마리 · 재생성 300초).
//
// 새 맵·인스턴스를 만들지 않는다 — `L_Hunt` 안 한 구역의 **동물 밀도만** 높인다(결정 005-3).
// 구역이 늘어도 코드가 아니라 행이 는다 (DataDriven.md 1).
//
// **이 표의 숫자는 DESIGN 8.1 의 사본이다.** 한쪽만 고치면 2층
// `Snarehaven.DangerZone.Table.MatchesDesign` 이 운다 (M7-02 확률 공개 대조와 같은 방식).
//
// **동물을 실제로 세우는 코드는 아직 0줄이다** — 스포너는 M6-04b, 맵 배치는 M6-04c 다.
USTRUCT(BlueprintType)
struct FDangerZoneRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 이 위험구역이 **어느 존 안**에 있는가. `DT_ZoneTable` 의 `ZoneID` (예: `zone.hunt`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone")
	FName ZoneID;

	/** 여기 세울 동물의 `DT_AnimalTable` 행 이름 (예: `Animal_Pig`). 종이 늘면 행이 는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone")
	FName AnimalRowName;

	/** 구역 반지름(cm). DESIGN 8.1 = 500. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone")
	float RadiusCm = -1.0f;

	/** 구역 안에 동시에 서 있는 동물 수. DESIGN 8.1 = 5 (목장 슬롯 수와 같은 수). 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone")
	int32 AnimalCount = -1;

	/** 한 마리가 사라진 뒤 다시 서기까지(초). DESIGN 8.1 = 300. 음수면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone")
	int32 RespawnSeconds = -1;

	/**
	 * M12-07 랜덤 월드 이벤트: 구역이 선 뒤 사라지기까지(초). **0 = 상시 구역**(8.1 그대로).
	 * 0 보다 크면 한정시간 구역이다 — 같은 구역 코드에 수명만 붙인다. DESIGN 8.1b 표의 사본.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone|WorldEvent")
	int32 LifetimeSeconds = 0;

	/** M12-07: 서버가 이 행을 몇 초마다 맵 어딘가에 새로 세우는가. **0 = 맵에 놓인 상시 구역**(자동 등장 없음). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone|WorldEvent")
	int32 AppearIntervalSeconds = 0;

	/**
	 * M21-07f 주간 우두머리: 구역 종 `MaxHP` 에 곱하는 배수. DESIGN 10.6 「우두머리 HP」 = 5.
	 * **0 = 우두머리 없는 구역**(이벤트 구역). 0 보다 크면 채울 때마다 우두머리가 없으면 한 마리 더 세운다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone|Boss")
	float BossHPMultiplier = 0.f;

	/** M21-07f: 우두머리 몸 배율. DESIGN 10.6 「우두머리 자리·종」 = 1.5. `BossHPMultiplier` 가 0 이면 안 본다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone|Boss")
	float BossBodyScale = 0.f;

	/**
	 * M21-07g: 우두머리 보상 모자 확률 — **고지용 사본**이다. 굴림은 서버 `ClaimBoss`(`BOSS_HAT_CHANCE`)가 한다.
	 * DESIGN 10.6 「우두머리 모자」 = 0.1. 서버 = 이 칸 = DESIGN 을 2층 `Snarehaven.Disclosure.VariantBoss.*` 가 맞댄다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DangerZone|Boss")
	float BossHatChance = 0.f;
};
