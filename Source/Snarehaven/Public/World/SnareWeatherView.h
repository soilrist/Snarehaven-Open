#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnareWeatherView.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

// 날씨 화면 (DESIGN 3.3 「날씨」 표, ROADMAP M104-03). 비·눈 입자를 내 캐릭터 둘레에만 띄운다 — 파티클 에셋 없이
// 엔진 기본 도형을 한 덩어리(InstancedStaticMesh)로 그린다. 하늘 색조는 낮밤 서브시스템이 `GetAmounts` 를 읽어 곱한다.
// 날씨 계산은 `SnareWeather::Amounts`(서버 시계) — 복제할 값이 없다.
namespace SnareWeatherView
{
	constexpr int32 MaxRain = 600;            // 표 「입자 상한」
	constexpr int32 MaxSnow = 800;
	constexpr float SnowSizeCm = 22.f;        // 눈송이 지름 (엔진 기본 공 100cm × 배율)
	constexpr float BoxHalfCm = 1800.f;       // 캐릭터 둘레 가로세로 ±18m — 게임 카메라가 보는 땅(약 24m × 14m)을 덮는다
	constexpr float BoxHeightCm = 1400.f;     // 캐릭터 위 14m 에서 발밑까지 떨어진다
	constexpr float RainFallCmPerSec = 1800.f;
	constexpr float SnowFallCmPerSec = 180.f;
	constexpr float SnowSwayCm = 60.f;        // 눈은 좌우로 흔들리며 내린다
	constexpr float RoofCheckSeconds = 0.25f; // 지붕 아래 판정 주기
	// 색조 곱 (표 「화면 색조」): 비 = 해·하늘 ×0.8 + 푸른 회색 · 눈 = ×0.9 + 밝은 흰푸름
	constexpr float RainLightScale = 0.8f;
	constexpr float SnowLightScale = 0.9f;
	inline const FLinearColor RainTint(0.62f, 0.72f, 0.85f);
	inline const FLinearColor SnowTint(0.88f, 0.94f, 1.0f);

	/** 입자 i 의 0~1 고정 난수 — 씨앗 i, 갈래 Channel */
	SNAREHAVEN_API float Hash01(int32 Index, int32 Channel);

	/** 켜진 입자 수: 정도 0~1 × 상한 */
	SNAREHAVEN_API int32 ActiveCount(float Amount, int32 Max);
}

UCLASS()
class SNAREHAVEN_API USnareWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 지금 비·눈 정도 (0~1). 낮밤이 색조에 쓴다. */
	void GetAmounts(float& OutRain, float& OutSnow) const { OutRain = Rain; OutSnow = Snow; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Build();
	void UpdateParticles(UInstancedStaticMeshComponent* Comp, int32 Max, float Amount, const FVector& Center, double Seconds, bool bSnow);

	UPROPERTY(Transient) TObjectPtr<AActor> Holder;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> RainComp;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SnowComp;
	double OffsetSeconds = 0.0;
	float Rain = 0.f;
	float Snow = 0.f;
	float RoofTimer = 0.f;
	bool bUnderRoof = false;
	bool bBuilt = false;
};
