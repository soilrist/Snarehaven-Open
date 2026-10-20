#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoatActor.generated.h"

class APawn;
class UStaticMeshComponent;

// DESIGN 8.9 구명보트. 호스트가 띄우고(복제), 좌석 0 = 운전자, 1 이상 = 승객.
//   · 운전자는 붙이지 않는다 — 해저를 그대로 걷고, 배가 운전자 XY 를 따라 수면에 뜬다(호스트 Tick).
//     운전석이 선미라 배 가운데는 운전자보다 운전석 오프셋만큼 앞이다(레퍼런스 구명보트 — 모터 옆에서 몬다).
//     속도는 `UWaterTravelComponent::GetMoveSpeedMultiplier` 가 배 배율로 바꾼다.
//   · 승객은 좌석에 붙이고 이동을 끈다(`MOVE_None`) — 운전자가 끌고 간다.
// 좌석 배열은 복제되고, 각 기계가 `OnRep_Riders` 에서 스스로 붙이고 뗀다.
// 배낭 차감·반환은 이 액터가 아니라 서버 `LaunchBoat`/`StowBoat` 가 한다 (CLAUDE.md 8).
// 메시는 `SM_Vehicle_Boat_01`(바르코 구명보트). 루트는 빈 씬 컴포넌트이고 Hull 은 그 자식이라,
// 물 위 흔들림(출렁임·롤·피치·뱃머리 들기)은 **Hull 의 상대 트랜스폼만** 바꾼다 — 기계마다 각자 도는 보이는
// 효과이고 루트 이동·복제 상태는 건드리지 않는다. 값은 DESIGN 8.9 "구명보트 크기·흔들림" 행.
UCLASS()
class SNAREHAVEN_API ABoatActor : public AActor
{
	GENERATED_BODY()

public:
	ABoatActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 호스트: 띄운 직후 한 번. 좌석 수는 표 `BoatSeats`. */
	void InitBoat(APawn* InOwnerPawn, int32 InMotorTier, int32 SeatCount, float InSurfaceZ);

	/** 호스트: 빈 자리 중 가장 앞(0 = 운전석)에 앉힌다. 이미 앉았거나 자리가 없으면 INDEX_NONE. */
	int32 SeatPawn(APawn* Pawn);

	/** 호스트: 내린다. 앉아 있지 않았으면 false. */
	bool UnseatPawn(APawn* Pawn);

	/** 호스트: 모두 내린다 (넣기·파괴 직전). */
	void UnseatAll();

	int32 FindSeat(const APawn* Pawn) const;
	bool HasFreeSeat() const;
	APawn* GetDriver() const { return Riders.IsValidIndex(0) ? Riders[0].Get() : nullptr; }
	APawn* GetOwnerPawn() const { return OwnerPawn; }
	int32 GetMotorTier() const { return MotorTier; }
	int32 GetSeatCount() const { return Riders.Num(); }

	/** 탈 수 있는 거리 = 근접 사거리 + 이 반경 (배 몸통 절반 길이). */
	float GetBoardRadiusCm() const { return HullLengthCm * 0.5f; }

	/** 좌석 바닥의 월드 트랜스폼(흔들리는 Hull 기준, 배율 제외, 회전 = 액터 축 + 흔들림 기울기). 0 = 운전석(선미). */
	FTransform GetSeatWorldTransform(int32 Seat) const;

	/** 월드에서 이 폰이 앉은 배. 없으면 nullptr. */
	static ABoatActor* FindBoatOf(const APawn* Pawn);

protected:
	/** 몸통 길이(cm). 메시를 이 길이로 균등 배율한다(폭·높이는 메시 비율). DESIGN 8.9 = 280. */
	UPROPERTY(EditAnywhere, Category = "Boat")
	float HullLengthCm = 280.f;

	/** 수면 아래로 잠기는 깊이(cm). DESIGN 8.9 = 15. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float DraftCm = 15.f;

	/** 상하 출렁임 진폭(cm)·주기(초). DESIGN 8.9 = ±4 · 2.6. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float HeaveAmplitudeCm = 4.f;
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float HeavePeriodSeconds = 2.6f;

	/** 좌우 기울기(롤) 진폭(도)·주기(초). DESIGN 8.9 = ±3 · 3.4. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float RollAmplitudeDeg = 3.f;
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float RollPeriodSeconds = 3.4f;

	/** 앞뒤 기울기(피치) 진폭(도)·주기(초). DESIGN 8.9 = ±2 · 2.9. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float PitchAmplitudeDeg = 2.f;
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float PitchPeriodSeconds = 2.9f;

	/** 이 속도(cm/s)에서 뱃머리 들기·흔들림 키우기가 가득 찬다. DESIGN 8.9 = 200. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float BobFullSpeedCmPerSecond = 680.f;

	/** 가득 찬 속도에서 뱃머리를 드는 각도(도). DESIGN 8.9 = 6. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float BowLiftMaxDeg = 6.f;

	/** 가득 찬 속도에서 흔들림 진폭 배율. DESIGN 8.9 = 2. */
	UPROPERTY(EditAnywhere, Category = "Boat|Bob")
	float BobSpeedAmplitudeMul = 2.f;

	/** 운전석(좌석 0)의 몸통 기준 위치(cm, 액터 축 XY, +X = 진행 방향). DESIGN 8.9 "배 좌석 자리" = 선미 −70. */
	UPROPERTY(EditAnywhere, Category = "Boat")
	FVector DriverSeatOffsetCm = FVector(-70.f, 0.f, 0.f);

	/** 승객 좌석(1번부터)의 몸통 기준 위치(cm, 액터 축 XY). DESIGN 8.9 "배 좌석 자리" = 0 · +70. 높이는 `SeatFloorCm`. */
	UPROPERTY(EditAnywhere, Category = "Boat")
	TArray<FVector> PassengerSeatOffsetsCm = { FVector(0.f, 0.f, 0.f), FVector(70.f, 0.f, 0.f) };

	/** 몸통 바닥(피벗)에서 안쪽 바닥까지(cm). BoatSit 몸 원점이 여기 앉는다. DESIGN 8.9 = 32. */
	UPROPERTY(EditAnywhere, Category = "Boat")
	float SeatFloorCm = 32.f;

private:
	UFUNCTION()
	void OnRep_Riders();

	/** 복제된 좌석 목록대로 붙이고 뗀다 (호스트·손님 같은 길). */
	void ApplyRiders();

	/** 기계마다 각자: 이번 틱 속도로 Hull 상대 트랜스폼(출렁임·기울기·뱃머리)을 정한다. 복제 안 함. */
	void UpdateBob(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> Hull;

	UPROPERTY(ReplicatedUsing = OnRep_Riders)
	TArray<TObjectPtr<APawn>> Riders;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> OwnerPawn;

	UPROPERTY(Replicated)
	int32 MotorTier = 0;

	UPROPERTY(Replicated)
	float SurfaceZ = 0.f;

	/** 마지막으로 붙여 둔 승객 — 목록에서 빠진 사람을 떼려고 든다. */
	TArray<TWeakObjectPtr<APawn>> AttachedPassengers;

	// ── 흔들림 (비복제, 보이는 값만) ──
	float HullBaseYaw = 0.f;       // 메시 긴 축을 액터 +X 에 맞추는 회전
	float BobTime = 0.f;
	float BobSpeedAlpha = 0.f;     // 0~1, 속도를 부드럽게 따라간다
	FVector LastBobLocation = FVector::ZeroVector;
	bool bHasLastBobLocation = false;
};
