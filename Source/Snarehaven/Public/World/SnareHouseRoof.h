#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnareHouseRoof.generated.h"

// 레퍼런스 실내 보기 — 집에 들어가면 지붕·앞벽(덮개)이 사라져 방이 위에서 보인다. 덮개 액터는
// `setup_ranch_buildings.py` 가 `RoofTag` 를 붙여 세운다. 숨김은 보이기만 바꾸고 충돌은 남긴다(앞벽이 계속 막는다).
namespace SnareHouseRoof
{
	/** 덮개 바운드를 안쪽으로 줄이는 폭 — 처마 내밈(55)·밑돌 띠를 빼고 벽 안쪽만 "안"으로 친다. */
	constexpr float EaveInsetCm = 60.f;

	/** 발 위치가 덮개 밑(처마를 뺀 XY 안, 지붕 꼭대기 아래)인가. */
	SNAREHAVEN_API bool IsUnderRoof(const FBox& RoofBounds, const FVector& Feet);

	/** 집 안에서 검은 바탕으로 바깥을 가릴 때 남길 액터인가 — 원점이 덮개 바운드 안이고 집보다 크지 않은 것만(땅·바다 판은 원점이 안에 있어도 크기로 걸러진다). */
	SNAREHAVEN_API bool KeepsVisibleInInterior(const FBox& RoofBounds, const FVector& ActorOrigin, const FVector& ActorExtent);

	/** 문턱을 넘을 때 검은 화면에서 걷히는 시간(초) — 0.3 이하여야 딱 끊김도 굼뜸도 아니다. */
	constexpr float InteriorFadeSeconds = 0.25f;
}

UCLASS()
class SNAREHAVEN_API USnareHouseRoofSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static const FName RoofTag;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** 집 안 진입 — 바깥 액터의 그리기 구성요소를 이 기기에서만 끈다(액터 hidden 은 복제되니 구성요소 단위). */
	void EnterInterior(UWorld* World, const AActor* Roof, const APawn* Pawn);
	void LeaveInterior();

	TArray<TWeakObjectPtr<AActor>> Roofs;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenOutside;
	TWeakObjectPtr<AActor> InsideRoof;
	bool bSearched = false;
};
