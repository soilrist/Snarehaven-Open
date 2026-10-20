#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BaseRuleRow.generated.h"

// DESIGN 8.10 (불 옆 회복) · 8.11 (텐트·땅·집·유지비·보관함) 규칙 숫자 한 벌 (`DT_BaseRuleTable` 의 행 구조체).
// **이 표의 숫자는 DESIGN 8.10·8.11 표의 사본이다** — 판정은 CloudScript 가 하고(CLAUDE.md 8),
// 클라이언트는 이 표로 미리보기 색·버튼 켜기·불 옆 회복만 한다. 코드에 숫자를 두지 않는다 (DataDriven.md 3).
//
// 행이 하나뿐인 표다(`Base`). 음수는 **미정**이다 (Verification.md 7) — 쓰는 쪽은 미정 행이면 아무것도 켜지 않는다.
USTRUCT(BlueprintType)
struct FBaseRuleRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 모닥불·정원 화로에서 이 거리(cm) 안이면 체력이 찬다. DESIGN 8.10 = 300. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	float FireHealRadiusCm = -1.f;

	/** 불 옆 초당 회복. DESIGN 8.10 = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	float FireHealPerSecond = -1.f;

	/** 텐트 중심 땅 한 변 칸 수. DESIGN 8.11 = 16. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 LandCells = -1;

	/** 한 칸 크기(cm). DESIGN 8.11 = 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	float CellSizeCm = -1.f;

	/** 한 사람 텐트 최대. DESIGN 8.11 = 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 TentMax = -1;

	/** 땅 하나에 놓을 수 있는 가구 최대. DESIGN 8.11 = 40. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 FurnitureMax = -1;

	/** 텐트를 집으로 올려야 하는 시간(시). DESIGN 8.11 = 24. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 UpgradeDeadlineHours = -1;

	/** 2층 집 땅 하나 가구 최대. DESIGN 8.11 「집 2층·전력」 = 60 (서버 `FURNITURE_MAX_FLOOR2`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 FurnitureMaxFloor2 = -1;

	/**
	 * 2층 올리기 재료 (아이템 ID → 개수). DESIGN 8.11 「집 2층·전력」 = 계단 건축 부품 1 + 나무판 40 + 향나무판 20 + 철판 4
	 * (서버 `FLOOR2_COST`). 화면 글·버튼 켜기만 — 확인·차감은 서버 `UpgradeHouse {level: 2}`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	TMap<FName, int32> Floor2Cost;

	// ── 전력 (DESIGN 8.11 「집 2층·전력」) — 땅 하나마다 공급 합 ≥ 소모 합이면 밤에 그 땅의 전등이 전부 켜진다. ──
	// 불 켜짐은 재화가 아니라 놓인 가구 목록에서 나오는 결과라 각 기기가 스스로 센다 (8.11 「전력 판정」).

	/** 태양광 키트 아이템. DESIGN 8.11 = `item.solar_kit`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	FName SolarKitItemID;

	/** 키트 하나 공급. DESIGN 8.11 = 10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	int32 SolarKitPower = -1;

	/** 배터리 아이템. DESIGN 8.11 = `item.battery`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	FName BatteryItemID;

	/** 배터리 하나 공급 — 같은 땅에 키트가 1개 이상일 때만. DESIGN 8.11 = 10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	int32 BatteryPower = -1;

	/** 전등 아이템. DESIGN 8.11 = `item.lamp_electric`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	FName LampItemID;

	/** 전등 하나 소모. DESIGN 8.11 = 5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	int32 LampPowerUse = -1;

	/** 전등 불빛 밝기(cd). DESIGN 3.3 「밤 불빛」 전구 = 80. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float LampLightCandela = -1.f;

	/** 전등 불빛 반경(cm). DESIGN 3.3 「밤 불빛」 = 528. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float LampLightRadiusCm = -1.f;

	// ── 비전기 조명 (DESIGN 8.11 「비전기 조명」, M108-05e) — 행 기능 `FlameLight` 은 밤이면 전력 없이 켜진다. ──

	/** 불꽃 불빛 밝기(cd). DESIGN 8.11 = 60. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float FlameLightCandela = -1.f;

	/** 불꽃 불빛 반경(cm). DESIGN 8.11 = 352. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	float FlameLightRadiusCm = -1.f;

	/** 땅 하나에서 함께 켜지는 점광 상한(전등 + 불꽃) — 모바일 동적 빛 예산. DESIGN 8.11 = 8. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	int32 LandLightMax = -1;

	// ── 연료 발전기 (DESIGN 8.11 「연료 발전기 숫자」, M108-07) — 연료 시각은 서버 `FeedGenerator` 가 정한다. 값은 서버 상수와 같다. ──

	/** 발전기 아이템. DESIGN 8.11 = `item.generator`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	FName GeneratorItemID;

	/** 연료가 남은 발전기 하나 공급. DESIGN 8.11 = 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	int32 GeneratorPower = -1;

	/** 연료 아이템 → 1개가 타는 분. DESIGN 8.11 = 나무 10 · 석탄 덩어리 30 (서버 `GENERATOR_FUEL_MINUTES`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	TMap<FName, int32> GeneratorFuelMinutes;

	/** 연료 최대 시간. DESIGN 8.11 = 12 (서버 `GENERATOR_FUEL_MAX_HOURS`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Power")
	int32 GeneratorFuelMaxHours = -1;

	/** 발전기 숫자가 채워졌나. 비면 발전기는 공급 0 인 가구로 둔다. */
	bool HasGeneratorRules() const
	{
		return !GeneratorItemID.IsNone() && GeneratorPower > 0 && GeneratorFuelMinutes.Num() > 0 && GeneratorFuelMaxHours > 0;
	}

	/** 비전기 조명 숫자가 채워졌나. 비면 불꽃 가구는 꺼진 채로, 전등은 상한 없이 옛 길 그대로. */
	bool HasFlameRules() const
	{
		return FlameLightCandela > 0.f && FlameLightRadiusCm > 0.f && LandLightMax > 0;
	}

	/** 전력(M20-04) 숫자가 채워졌나. `IsComplete` 에 넣지 않는다 — 비면 전등은 늘 꺼진 채로 둔다. */
	bool HasPowerRules() const
	{
		return !SolarKitItemID.IsNone() && SolarKitPower > 0 && !BatteryItemID.IsNone() && BatteryPower > 0
			&& !LampItemID.IsNone() && LampPowerUse > 0 && LampLightCandela > 0.f && LampLightRadiusCm > 0.f;
	}

	/** 집 올리기 나무 개수. DESIGN 8.11 = 150. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 UpgradeWoodCount = -1;

	/** 집 올리기 향나무 개수. DESIGN 8.11 = 50. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 UpgradeCedarCount = -1;

	/** 집 색 가짓수. DESIGN 8.11 = 5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 HouseColorCount = -1;

	/** 유지비 나무 1개가 늘려 주는 시간(시). DESIGN 8.11 = 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 UpkeepHoursPerWood = -1;

	/** 유지비 최대(시). DESIGN 8.11 = 200. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 UpkeepMaxHours = -1;

	/** 보관함 PIN 자릿수. DESIGN 8.11 = 5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Base")
	int32 PinLength = -1;

	/** 유지비로 넣는 아이템. DESIGN 3.2·8.11 = `item.wood` (서버 `UPKEEP_ITEM`). 목장 HUD 가 가진 개수를 셀 때만 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	FName UpkeepItemID;

	/** 목장 칸 k번째 확장 명성 = 이 값 × k. DESIGN 3.2 = 50. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	int32 RanchExpandFameStep = -1;

	/** 목장 칸 k번째 확장 코인 = 이 값 × k. DESIGN 3.2 = 500. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	int32 RanchExpandCoinStep = -1;

	/** 목장 칸 상한. DESIGN 3.2 = 10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	int32 RanchSlotMax = -1;

	/**
	 * 목장 칸(M12-05) 숫자가 채워졌나. `IsComplete` 에 넣지 않는다 — 목장 숫자가 비었다고 텐트 미리보기까지
	 * 꺼지면 안 된다. false 면 목장 HUD 는 확장 비용 대신 `칸 확장` 만 적고 두 버튼을 끈다.
	 */
	bool HasRanchRules() const
	{
		return !UpkeepItemID.IsNone() && RanchExpandFameStep > 0 && RanchExpandCoinStep > 0 && RanchSlotMax > 0;
	}

	/**
	 * 2층(M20-03) 숫자가 채워졌나. `IsComplete` 에 넣지 않는다 — 2층 줄이 비었다고 1층 미리보기까지 꺼지면 안 된다.
	 * false 면 집 화면은 「2층 올리기」 를 안 띄우고 2층 집도 1층 한도로 본다.
	 */
	bool HasFloor2Rules() const
	{
		if (FurnitureMaxFloor2 <= 0 || Floor2Cost.IsEmpty())
		{
			return false;
		}
		for (const TPair<FName, int32>& Pair : Floor2Cost)
		{
			if (Pair.Key.IsNone() || Pair.Value <= 0)
			{
				return false;
			}
		}
		return true;
	}

	/** 채워진 행인지 (미정 칸이 하나라도 있으면 false). */
	bool IsComplete() const
	{
		return FireHealRadiusCm > 0.f && FireHealPerSecond > 0.f && LandCells > 0 && CellSizeCm > 0.f
			&& TentMax > 0 && FurnitureMax > 0 && UpgradeDeadlineHours > 0 && UpgradeWoodCount > 0
			&& UpgradeCedarCount > 0 && HouseColorCount > 0 && UpkeepHoursPerWood > 0 && UpkeepMaxHours > 0
			&& PinLength > 0;
	}
};
