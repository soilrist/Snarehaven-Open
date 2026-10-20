#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PlayerVitalsRow.generated.h"

// ROADMAP M11-01: 플레이어의 체력·스태미나 숫자 한 벌 (`DT_PlayerVitalsTable` 의 행 구조체).
// **이 표의 숫자는 DESIGN 3.1 "플레이어 체력·스태미나" 표(루프#142 확정)의 사본이다** —
// `UPlayerVitalsComponent` 는 숫자를 하나도 들고 있지 않고 이 행을 받아 채운다 (DataDriven.md 3).
//
// 행이 하나뿐인 표다(`Player`). 종족·직업이 생기면 행이 늘고 코드는 그대로다 —
// 동물이 `DT_AnimalTable` 행으로 갈리는 것과 같은 짜임.
//
// 음수는 **미정**이다 (Verification.md 7) — 컴포넌트는 미정 행으로 초기화되지 않는다.
USTRUCT(BlueprintType)
struct FPlayerVitalsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 최대 체력. DESIGN 3.1 = 100 (동물 기본 HP 와 같은 눈금). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float MaxHealth = -1.0f;

	/** 최대 스태미나. DESIGN 3.1 = 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float MaxStamina = -1.0f;

	/** 달리는 동안 초당 스태미나 소모. DESIGN 3.1 = 10 (최대 연속 달리기 10초). 걷기·정지는 0 이라 칸이 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float RunStaminaPerSecond = -1.0f;

	/** 공격 1타가 먹는 스태미나. DESIGN 3.1 = 10 (동물 한 마리 5타 = 50). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float AttackStaminaCost = -1.0f;

	/** 스태미나 회복 속도(초당). DESIGN 3.1 = 20 (소모의 2배). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float StaminaRegenPerSecond = -1.0f;

	/** 달리기·공격을 멈춘 뒤 회복이 시작되기까지(초). DESIGN 3.1 = 0.6 (공격 간격과 같은 값). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float StaminaRegenDelaySeconds = -1.0f;

	/** 탈진(스태미나 0 인 채로 계속 이동) 중 초당 체력 감소. DESIGN 3.1 = 2 (100 -> 50초). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float ExhaustionHealthPerSecond = -1.0f;

	/** 체력 자연 회복(초당). DESIGN 3.1 = 1 (탈진 사망의 2배 느리게). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float HealthRegenPerSecond = -1.0f;

	/** 체력이 자연 회복되기 시작하는 스태미나 하한. DESIGN 3.1 = 50. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float HealthRegenStaminaThreshold = -1.0f;

	// 아래 둘은 DESIGN 3.1 「사망 시 잃는 것」 표의 "부활 시 체력·스태미나 | 50 / 50" 한 줄이다 (M11-03).
	// **비율(0.5)이 아니라 절대값으로 둔다** — 설계서가 적어 둔 값이 50 이고, 2층이 그 줄의 숫자를
	// 그대로 읽어 맞대기 때문이다. 최대치를 바꾸면 이 둘도 같이 손봐야 한다는 뜻이고, 그게 표를
	// 읽는 사람에게도 정직하다(전회복은 "최대치" 라고 따로 적혀 있다).

	/** 부활 직후 체력. DESIGN 3.1 = 50 (최대의 절반 — 전회복이면 죽는 것이 회복 수단이 된다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float RespawnHealth = -1.0f;

	/** 부활 직후 스태미나. DESIGN 3.1 = 50 (체력과 같은 줄). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float RespawnStamina = -1.0f;

	/**
	 * 쓰러짐 클립이 끝난 뒤 **누운 채로 있는 시간**(초). DESIGN 3.1 「쓰러짐 → 부활」 = 1.0.
	 * 부활까지 걸리는 시간은 이 값 + **클립 길이**이고, 클립 길이는 표가 아니라 클립에서 읽는다
	 * (`A_Player_Death`, DataDriven.md 3 — 감독이 클립을 갈아 끼우면 저절로 따라온다).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals")
	float DeathLingerSeconds = -1.0f;
};
