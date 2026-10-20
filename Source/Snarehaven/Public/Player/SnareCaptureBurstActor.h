#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SnareCaptureBurstActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 포획 순간 반짝 조각 + 펫이 배낭 쪽으로 날아가는 연출 (ROADMAP M21-03 · DESIGN 3.1 「포획 순간」).
 * 각 기기가 `ASnarehavenCharacter::Multicast_PlayCaptureFeedback` 에서 제 손으로 띄우는 **복제 안 하는 연출 액터**다 —
 * 지급과 상관없다(펫 등록은 그대로 `ClaimLoot` 뒤, CLAUDE.md 8). 피격 파편(`AAnimalCharacter`)처럼 파티클 에셋 없이
 * 인스턴스 메시로 그린다. 반짝·비행이 다 끝나면 스스로 사라진다.
 */
UCLASS(NotBlueprintable)
class SNAREHAVEN_API ASnareCaptureBurstActor : public AActor
{
	GENERATED_BODY()

public:
	ASnareCaptureBurstActor();

	/** 반짝 조각 수. */
	static constexpr int32 SparkleCount = 12;
	/** 반짝 조각 수명(초). */
	static constexpr float SparkleLifeSeconds = 0.5f;
	/** 펫이 배낭까지 날아가는 시간(초) — ROADMAP M21-03 「0.4초」. */
	static constexpr float FlightSeconds = 0.4f;
	/** 도착할 때 펫 크기(처음 크기에 곱함). */
	static constexpr float FlightEndScale = 0.2f;
	/** 날아가는 길 가운데가 곧은 선보다 위로 뜨는 높이(cm). */
	static constexpr float FlightArcCm = 80.f;

	/**
	 * 동물 자리에서 연출을 시작한다. 날아갈 몸은 동물 메시(없으면 자리표시 공)를 같은 크기로 복사한다.
	 * @param Catcher 잡은 사람 — 매 틱 그 배낭(`Socket_Back`) 자리를 다시 읽어 따라간다
	 */
	void Start(const FVector& AnimalLocation, USkeletalMesh* PetMesh, const FVector& PetScale, AActor* Catcher);

	virtual void Tick(float DeltaSeconds) override;

	/** 지금 펫이 가야 할 배낭 자리. 잡은 사람이 없거나 소켓이 없으면 몸 가운데 위. */
	FVector GetFlightTargetLocation() const;

	/** 2층 검사용 — 남은 반짝 수, 펫 몸 위치·보이는지, 날아간 비율(0~1). */
	int32 GetSparkleInstanceCount() const;
	FVector GetPetLocation() const;
	bool IsPetVisible() const;
	float GetFlightAlpha() const { return FMath::Clamp(ElapsedSeconds / FlightSeconds, 0.f, 1.f); }

private:
	USceneComponent* GetPetBody() const;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Sparkles;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> PetSkeletal;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PetPlaceholder;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SparkleMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	TWeakObjectPtr<AActor> CatcherActor;
	TArray<FVector> SparkleVelocities;
	FVector StartLocation = FVector::ZeroVector;
	FVector StartScale = FVector::OneVector;
	float ElapsedSeconds = 0.f;
	bool bStarted = false;
};
