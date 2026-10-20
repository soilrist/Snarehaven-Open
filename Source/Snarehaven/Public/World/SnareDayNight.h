#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnareDayNight.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class ULightComponent;

// 레퍼런스 낮밤 (DESIGN 3.3 「낮밤」 표). 시각은 서버 시계(`GetServerWorldTimeSeconds`)에서만 계산한다 —
// 복제할 값이 없고 모든 사람이 같은 하늘을 본다. 게임 월드마다 저절로 붙어 레벨에 배치할 것이 없다.
namespace SnareDayNight
{
	constexpr double CycleSeconds = 1200.0;   // 하루 20분
	constexpr double NightSeconds = 480.0;    // 그중 밤 8분
	constexpr double TwilightSeconds = 60.0;  // 해질녘·새벽 1분씩 섞는다

	// 밤 색 — 레퍼런스 밤 화면(`steam_02`·`steam_08`, 시트 world_environment.md): 전체가 남보라 `#5B4FA8` 로 눌리고
	// 불빛만 따뜻하다. 값과 근거는 DESIGN 3.3 「낮밤」 표.
	inline const FLinearColor MoonColor(0.62f, 0.48f, 1.0f);
	inline const FLinearColor NightSkyColor(0.58f, 0.42f, 1.0f);
	constexpr float NightSunScale = 0.35f;
	constexpr float NightSkyScale = 0.65f;

	/** 0 = 낮, 1 = 한밤. 주기 0초 = 아침. 해질녘·새벽은 부드럽게 오간다. */
	SNAREHAVEN_API float NightAmount(double Seconds);
}

/** 해·하늘빛을 밤으로 누르고, `SnareNightLight` 태그가 붙은 불빛을 밤에만 켠다. */
UCLASS()
class SNAREHAVEN_API USnareDayNightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 이 태그가 붙은 빛 컴포넌트(또는 그 액터)는 밤에만 켜진다 — 가로등·창문·등대. */
	static const FName NightLightTag;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	float GetNightAmount() const { return LastNight; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void FindLights();
	void Apply(float Night);

	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<USkyLightComponent> Sky;
	TArray<TWeakObjectPtr<ULightComponent>> NightLights;
	float SunDayLux = 0.f;
	float SkyDayIntensity = 0.f;
	FLinearColor SunDayColor = FLinearColor::White;
	double TimeOffsetSeconds = 0.0;
	float LastNight = -1.f;
	float LastRain = 0.f;   // 날씨 색조 (SnareWeatherView)
	float LastSnow = 0.f;
	bool bSearched = false;
};
