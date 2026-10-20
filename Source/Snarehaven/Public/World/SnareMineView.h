#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnareMineView.generated.h"

// 광산 굴 안 보기 (M96 보정 — 곡괭이 캡처: 앞쪽 바위벽이 플레이어를 가리고 굴이 검다). 로컬 플레이어가
// 굴 안에 있을 때만 (1) 카메라와 플레이어 사이를 지나는 벽 바위를 이 기기에서 끄고 (2) 플레이어 둘레에
// 약한 불빛을 켠다. 숨김은 보이기만 바꾼다 — 막는 상자(숨긴 충돌)는 그대로다. 숫자 근거는 DESIGN 8.16 「굴 안 보기」.
namespace SnareMineView
{
	/** 굴 방 전체를 덮는 월드 상자(X) — 굴 원점 (70000, 0), 방은 원점 기준 X -400~3500 · Y ±1000 (`setup_mine.py`). */
	constexpr float MineMinX = 69000.f;
	constexpr float MineMaxX = 74500.f;
	constexpr float MineHalfY = 1600.f;

	/** 가림 판정 때 바위 바운드를 부풀리는 폭(cm) — 선이 바위 모서리를 스치는 것도 가린 것으로 친다. */
	constexpr float OccluderMarginCm = 40.f;
	/** 플레이어에서 이 거리(cm) 안의 바위만 가림을 본다 — 먼 벽은 선이 닿아도 플레이어를 가리지 않는다. */
	constexpr float OccluderRangeCm = 1500.f;

	/** 플레이어 둘레 불빛 — 굴 등불(120cd·700)보다 약하게, 채굴 모자(200cd·900)보다 약하게. */
	constexpr float GlowIntensityCd = 80.f;
	constexpr float GlowRadiusCm = 600.f;
	constexpr float GlowHeightCm = 120.f;

	/** 발 위치가 굴 안인가. */
	SNAREHAVEN_API bool IsInMine(const FVector& Feet);

	/** 카메라→플레이어 선이 바위 바운드(부풀림 포함)를 지나는가. 플레이어가 바운드 안이어도 가린 것이다. */
	SNAREHAVEN_API bool OccludesView(const FBox& RockBounds, const FVector& CameraLocation, const FVector& Target);
}

UCLASS()
class SNAREHAVEN_API USnareMineViewSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void CollectRocks();
	void SetGlow(APawn* Pawn, bool bOn);
	void ShowAllRocks();

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Rocks;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenRocks;
	TWeakObjectPtr<class UPointLightComponent> Glow;
	bool bCollected = false;
};
