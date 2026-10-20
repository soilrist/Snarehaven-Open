#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WaterActivityRow.generated.h"

/** 낚시 포인트 종류 (DESIGN 8.13 "자리" 줄). 서버 `FISHING_SPOTS` 의 fresh / sea / farSea 와 맞물린다. */
UENUM(BlueprintType)
enum class EFishingSpotKind : uint8
{
	Pond,
	River,
	Coast,
	FarSea,
	Swamp
};

// DESIGN 8.9(바다·수영·배) · 8.13(두드리기 낚시) 표의 숫자 한 벌 (`DT_WaterActivityTable` 의 행 구조체, 행 `Default`).
// **이 표의 숫자는 두 절 표의 사본이다** — 한쪽만 고치면 2층 `Snarehaven.Water.Table.MatchesDesign` 이 운다.
// 행을 만드는 곳은 `tools/unreal/create_water_tables.py` 하나다.
// 음수는 **미정**이다 (Verification.md 7) — 미정이 하나라도 있으면 `SnareWater::FindRow` 가 행을 안 준다.
// 가격(구명보트 50·지렁이 1)·보트 컨테이너 8칸은 여기 없다 — 가격은 상점(백엔드) 몫, 칸 수는 `DT_ItemTable.BonusSlots`.
USTRUCT(BlueprintType)
struct FWaterActivityRow : public FTableRowBase
{
	GENERATED_BODY()

	// ── 8.9 바다·수영 ──

	/** 해안에서 바깥 끝(보이지 않는 벽)까지. DESIGN 8.9 = 3000cm. 레벨 배치 스크립트가 읽는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sea")
	float SeaWidthCm = -1.f;

	/** 수영 속도 = 걷기 × 이 값. DESIGN 8.9 = 0.6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swim")
	float SwimSpeedMul = -1.f;

	/** 물속에서 초당 스태미나 소모. DESIGN 8.9 = 8. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swim")
	float SwimStaminaPerSecond = -1.f;

	/** 스태미나 0 인 채 물속이면 초당 체력 감소. DESIGN 8.9 = 5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swim")
	float DrownHealthPerSecond = -1.f;

	// ── 8.9 배 ──

	/** 배 속도 = 걷기 × 이 값. DESIGN 8.9 = 1.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boat")
	float BoatSpeedMul = -1.f;

	/** 보트 모터 배율. DESIGN 8.9 = 2.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boat")
	float BoatMotorMul = -1.f;

	/** 전기 보트 모터 배율. DESIGN 8.9 = 2.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boat")
	float BoatElectricMotorMul = -1.f;

	/** 구명보트 좌석 수. DESIGN 8.9 = 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boat")
	int32 BoatSeats = -1;

	// ── 8.3 탈것 (M6-06) — 걷기 기준 배율이라 수영·배와 한 행에 둔다 ──

	/** 탈것 속도 = 걷기 × 이 값. DESIGN 8.3 = 5.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount")
	float MountSpeedMul = -1.f;

	/** 탄 모습: 탈것 몸 길이 = 탄 사람 몸 너비 × 이 값. 탑다운에서 앞뒤로 보이는 몫. DESIGN 8.3 = 3.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount")
	float MountLengthPerRiderWidth = -1.f;

	/**
	 * 탄 사람·안장을 탈것 몸 가운데에서 꼬리 쪽으로 물리는 몫(몸 길이 비). DESIGN 8.3 = 0.2.
	 * 60° 카메라에서 앉은 사람 머리가 앞으로 투영돼 늑대 머리·어깨를 덮던 것을 걷어 낸다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount")
	float MountSeatBackOfLength = -1.f;

	/**
	 * 탈것 몸 걷기 클립이 **클립 공간에서** 밟는 속도(cm/s) — 몸 배율을 곱해 화면 속도로 쓴다.
	 * `tools/unreal/measure_player_anim_speed.py` 실측(늑대 `A_Animal_Wolf_Walk`) = 53.2. DESIGN 8.3.
	 * ponytail: 탈것 한 종(늑대 자리표시) 값. 종 고르기가 생기면 `FAnimalRow` 로 옮긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount")
	float MountWalkClipSpeedCmPerSecond = -1.f;

	/** 탈것 몸 달리기(도주) 클립의 클립 공간 속도(cm/s). 같은 도구 실측(`A_Animal_Wolf_Run`) = 173.6. DESIGN 8.3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount")
	float MountRunClipSpeedCmPerSecond = -1.f;

	// ── 8.13 두드리기 낚시 ──

	/** 포인트 몇 cm 안에서 던질 수 있나. DESIGN 8.13 = 400. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float CastRangeCm = -1.f;

	/** 입질 대기 최소(초, 장비 배율 전). DESIGN 8.13 = 6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float BiteMinSeconds = -1.f;

	/** 입질 대기 최대(초, 장비 배율 전). DESIGN 8.13 = 12. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float BiteMaxSeconds = -1.f;

	/** 지렁이 미끼를 쓰면 입질 대기 × 이 값. DESIGN 8.13 = 0.9. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float WormBiteMul = -1.f;

	/** 연타 목표. DESIGN 8.13 = 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float TapGoal = -1.f;

	/** 한 번 누를 때 진행 = 목표 ÷ (이 값 + 낚시 시간(분) ÷ `TapMinutesDivisor`). DESIGN 8.13 = 10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float TapBasePresses = -1.f;

	/** 위 식의 분 나누개. DESIGN 8.13 = 10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float TapMinutesDivisor = -1.f;

	/** 안 누르면 초당 진행 감소. DESIGN 8.13 = 15. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float TapDecayPerSecond = -1.f;

	/** 진행 0 에서 이만큼 머물면 놓친다(초). DESIGN 8.13 = 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float TapFailSeconds = -1.f;

	/** 한 포인트에서 이만큼 잡으면 사라진다. DESIGN 8.13 = 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	int32 SpotCatchesBeforeGone = -1;

	/** 사라진 포인트가 다시 생기기까지(초). DESIGN 8.13 = 300. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	float SpotRespawnSeconds = -1.f;

	/** 먼바다 포인트는 바다 어종 중 낚시 시간이 이 분 이상인 것만. DESIGN 8.13 = 120. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	int32 FarSeaMinCycleMinutes = -1;
};
