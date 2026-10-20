#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SnareSound.h"
#include "MineDoorActor.generated.h"

class APawn;
class UBoxComponent;
class UPrimitiveComponent;
struct FHitResult;

// DESIGN 8.16 「광산」 — 레퍼런스 설원 광산의 문. 원본 광산은 "벙커처럼 안으로 들어가는" 곳이라, 입구(설원)와
// 출구(광산 안) 문 두 개가 서로의 앞자리를 `ExitLocation` 으로 가리킨다. 광산 안은 **같은 맵의 먼 곳**이다.
//
// 발을 들이면 **서버가 그 폰만** 옮긴다 — 방 전체를 다른 맵으로 옮기는 `AZonePortalActor` 와 다르다
// (한 사람이 광산에 들어가도 나머지는 설원에 남는다). 자리는 `tools/unreal/setup_mine.py` 가 채운다.
UCLASS()
class SNAREHAVEN_API AMineDoorActor : public AActor
{
	GENERATED_BODY()

public:
	AMineDoorActor();

	/**
	 * 이 폰을 반대편 문 앞으로 옮긴다. 발판이 부르고, 2층 검사도 직접 부른다.
	 * 서버에서, 사람이 조종하는 폰만 — 동물은 광산에 안 들어간다.
	 * @return 옮겼으면 true
	 */
	bool SendThrough(APawn* Pawn);

	/** 돌문 소리 한 번 (DESIGN 8.10, M53-03) — 건넌 자리에서 모든 기기(`Multicast_`). 확정이 아니면 무음. */
	void PlayCrossingCue(bool bConfirmed, const FVector& Where);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCrossingCue(FName Cue, float Volume, FVector Where);

	/** 2층 시험이 읽는다 — 지금까지 낸 소리 수·마지막 이름·멀티캐스트로 보낸 수. */
	int32 GetCrossingCueCount() const { return CrossingCueCount; }
	FName GetLastCrossingCue() const { return LastCrossingCue; }
	int32 GetCrossingMulticastCount() const { return CrossingMulticastCount; }

	/** 반대편 문 앞 바닥(월드). 비어 있으면(0) 문이 꺼진 것. */
	const FVector& GetExitLocation() const { return ExitLocation; }

	UBoxComponent* GetTrigger() const { return Trigger; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 발판 — 문 구멍 앞. 크기는 맵에 놓인 문마다 액터 배율로 준다. */
	UPROPERTY(VisibleAnywhere, Category = "Mine")
	TObjectPtr<UBoxComponent> Trigger;

	/** 반대편 문 앞 바닥(월드 좌표). 반대편 발판 밖이어야 되돌아 튕기지 않는다(2층 검사). */
	UPROPERTY(EditAnywhere, Category = "Mine")
	FVector ExitLocation = FVector::ZeroVector;

	/** 도착해서 바라볼 방향(도) — 문에서 멀어지는 쪽. */
	UPROPERTY(EditAnywhere, Category = "Mine")
	float ExitYawDegrees = 0.f;

private:
	int32 CrossingCueCount = 0;
	FName LastCrossingCue;
	int32 CrossingMulticastCount = 0;
};
