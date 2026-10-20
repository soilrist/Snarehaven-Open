#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SnareCampCageActor.generated.h"

class UStaticMeshComponent;

// 메인 스토리 4장 3단계(18단계) 「캠프의 덫과 우리 3개 부수기」 (`docs/STORY.md` 4장). 작살단 캠프(`ASnareHarpoonCampActor`)가 셋 세운다.
// 호스트·게스트 각자 자기 화면에 세우는 로컬 액터다 — 부순 수는 내 이야기 진행(내 기기)에만 쓰이고 재화·보상이 없어
// 권위 2단과 무관하다. 단계 보고는 `SnarehavenCharacter` 가 `AllBroken` 을 보고 한다(판정은 서버 `storyStep`).
// 때리는 길 = 근접 휘두르기 타격 순간에 맞힐 동물이 없으면 `StrikeNearest` (MeleeAttackComponent::HandleDelayedHit).
UCLASS()
class SNAREHAVEN_API ASnareCampCageActor : public AActor
{
	GENERATED_BODY()

public:
	ASnareCampCageActor();

	/** 우리 하나를 부수는 데 필요한 타격 수 (DESIGN 의뢰 줄기 표 「우리 부수기」). */
	static constexpr int32 HitsToBreak = 3;

	/** 우리 중심에서 이 거리(cm) 안이면 맞는다 — 우리 가로 220cm 의 반 + 팔 닿는 거리. */
	static constexpr float StrikeRangeCm = 260.f;

	/** 캠프 중심 기준 월드 우리 자리(cm) — 천막(`TentSpots`)의 반대편(−Y). 겹침 0 은 시험 `Snarehaven.Story.CampLayout`. */
	static const TArray<FVector2D>& Offsets();

	/** `From` 에서 닿는 가장 가까운 안 부서진 우리를 한 번 친다. 쳤으면 true. */
	static bool StrikeNearest(UWorld* World, const FVector& From);

	/** 한 번 친다. 이번 타격으로 부서졌으면 true. */
	bool TakeStrike();

	bool IsBroken() const { return HitsTaken >= HitsToBreak; }
	int32 GetHitsTaken() const { return HitsTaken; }

	/** 월드의 우리 수 / 부서진 수. */
	static int32 CountAll(UWorld* World);
	static int32 CountBroken(UWorld* World);

	/** 우리가 하나 이상 서 있고 전부 부서졌나. 우리가 없으면 false. */
	static bool AllBroken(UWorld* World);

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CageMesh;

	int32 HitsTaken = 0;
};
