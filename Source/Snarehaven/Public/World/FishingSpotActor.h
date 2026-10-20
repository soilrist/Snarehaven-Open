#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/WaterActivityRow.h"
#include "FishingSpotActor.generated.h"

class UMaterialInterface;
class URotatingMovementComponent;
class UStaticMeshComponent;

// DESIGN 8.13 낚시 포인트 — 물 위에 물고기 그림자 셋이 원을 그리는 곳. 여기서만 던질 수 있다.
// `SpotId` 는 서버 `FISHING_SPOTS` 의 키(pond_1 · river_2 · coast_3 · far_1 …)와 **글자 그대로** 같아야 한다 —
// 서버가 이 문자열로 서식지를 고른다. 머리(pond/river/coast/far)가 종류다.
// 남은 마리 수는 호스트가 들고 복제한다: 표 `SpotCatchesBeforeGone` 마리를 잡으면 숨고 `SpotRespawnSeconds` 뒤 다시.
// 그림자는 물고기 메시 `SM_Prop_FishShadow_01` 셋을 납작하게 눌러 머리가 도는 방향 앞으로 (M13-40, DESIGN 8.13 「물고기 그림자 진짜 모습」).
UCLASS()
class SNAREHAVEN_API AFishingSpotActor : public AActor
{
	GENERATED_BODY()

public:
	AFishingSpotActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버 spotId. 레벨 배치 스크립트가 넣는다. */
	UPROPERTY(EditAnywhere, Category = "Fishing")
	FString SpotId;

	UPROPERTY(EditAnywhere, Category = "Fishing")
	TObjectPtr<UMaterialInterface> ShadowMaterial;

	bool IsAvailable() const { return RemainingCatches > 0; }
	int32 GetRemainingCatches() const { return RemainingCatches; }
	bool GetKind(EFishingSpotKind& OutKind) const;

	/** 호스트: 한 마리 잡혔다. 0 이 되면 숨고 다시 생길 타이머를 건다. */
	void ConsumeCatch();

	/** 호스트: 표의 마리 수로 다시 채운다 (타이머 끝·시작). */
	void Refill();

	/** 다시 생기기까지 남은 초. 타이머가 없으면 음수. 2층 검사용. */
	float GetRespawnRemainingSeconds() const;

	/** 이 점에서 RangeCm 안에 있는, 잡을 수 있는 가장 가까운 포인트. */
	static AFishingSpotActor* FindNearestAvailable(const UObject* WorldContext, const FVector& From, float RangeCm);

protected:
	virtual void BeginPlay() override;

	/** 그림자가 도는 반지름(cm). 레벨 모양 값. */
	UPROPERTY(EditAnywhere, Category = "Fishing")
	float ShadowCircleRadiusCm = 60.f;

	/** 그림자 몸 길이(cm) — DESIGN 8.13. 폭은 이 배율 그대로(원본 비율). */
	UPROPERTY(EditAnywhere, Category = "Fishing")
	float ShadowLengthCm = 50.f;

	/** 그림자 높이(cm) — DESIGN 8.13, 물 표면에 붙은 두께로 눌러 납작하게. */
	UPROPERTY(EditAnywhere, Category = "Fishing")
	float ShadowHeightCm = 2.f;

private:
	UFUNCTION()
	void OnRep_RemainingCatches();

	void ApplyVisibility();

	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TObjectPtr<USceneComponent> ShadowPivot;

	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TArray<TObjectPtr<UStaticMeshComponent>> Shadows;

	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TObjectPtr<URotatingMovementComponent> Circling;

	/** 표가 없거나 아직 복제 전이면 -1 (= 못 잡는다). */
	UPROPERTY(ReplicatedUsing = OnRep_RemainingCatches)
	int32 RemainingCatches = -1;

	FTimerHandle RespawnTimer;
};
