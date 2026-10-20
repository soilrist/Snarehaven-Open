#include "World/SnareDayNight.h"
#include "SnareCheckSwitch.h"
#include "World/SnareWeatherView.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Snarehaven.h"

namespace SnareDayNight
{
	float NightAmount(double Seconds)
	{
		const double Phase = FMath::Fmod(FMath::Fmod(Seconds, CycleSeconds) + CycleSeconds, CycleSeconds);
		const double DayEnd = CycleSeconds - NightSeconds;
		double Night;
		if (Phase < DayEnd - TwilightSeconds) Night = 0.0;
		else if (Phase < DayEnd) Night = (Phase - (DayEnd - TwilightSeconds)) / TwilightSeconds;
		else if (Phase < CycleSeconds - TwilightSeconds) Night = 1.0;
		else Night = (CycleSeconds - Phase) / TwilightSeconds;
		return FMath::SmoothStep(0.f, 1.f, static_cast<float>(Night));
	}
}

const FName USnareDayNightSubsystem::NightLightTag(TEXT("SnareNightLight"));

bool USnareDayNightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnareDayNightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnareDayNightSubsystem, STATGROUP_Tickables);
}

void USnareDayNightSubsystem::FindLights()
{
	bSearched = true;
	// 캡처·점검용: 주기 안의 몇 초에서 시작할지 (`-SnareTimeOfDay=900` = 밤)
	SnareCheckSwitch::Value(TEXT("SnareTimeOfDay="), TimeOffsetSeconds);
	UWorld* const World = GetWorld();
	for (TActorIterator<ADirectionalLight> It(World); It && !Sun.IsValid(); ++It)
	{
		Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
	}
	for (TActorIterator<ASkyLight> It(World); It && !Sky.IsValid(); ++It)
	{
		Sky = It->GetLightComponent();
	}
	if (Sun.IsValid())
	{
		SunDayLux = Sun->Intensity;
		SunDayColor = Sun->GetLightColor();
	}
	if (Sky.IsValid())
	{
		SkyDayIntensity = Sky->Intensity;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const bool bActorTagged = It->ActorHasTag(NightLightTag);
		It->ForEachComponent<ULightComponent>(false, [&](ULightComponent* Light)
		{
			if (bActorTagged || Light->ComponentHasTag(NightLightTag))
			{
				NightLights.Add(Light);
			}
		});
	}
	UE_LOG(LogSnarehaven, Log, TEXT("낮밤 — 해 %s · 하늘빛 %s · 밤 불빛 %d · 시작 %.0f초"),
		Sun.IsValid() ? TEXT("있음") : TEXT("없음"), Sky.IsValid() ? TEXT("있음") : TEXT("없음"),
		NightLights.Num(), TimeOffsetSeconds);
}

void USnareDayNightSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* const World = GetWorld();
	const AGameStateBase* const GameState = World ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return;
	}
	if (!bSearched)
	{
		FindLights();
	}
	const float Night = SnareDayNight::NightAmount(GameState->GetServerWorldTimeSeconds() + TimeOffsetSeconds);
	float Rain = 0.f;
	float Snow = 0.f;
	if (const USnareWeatherSubsystem* const Weather = World->GetSubsystem<USnareWeatherSubsystem>())
	{
		Weather->GetAmounts(Rain, Snow);
	}
	if (FMath::Abs(Night - LastNight) > 0.002f || FMath::Abs(Rain - LastRain) > 0.002f || FMath::Abs(Snow - LastSnow) > 0.002f)
	{
		LastRain = Rain;
		LastSnow = Snow;
		Apply(Night);
	}
}

void USnareDayNightSubsystem::Apply(float Night)
{
	const bool bLightsFlip = LastNight < 0.f || (LastNight >= 0.5f) != (Night >= 0.5f);   // 처음 한 번 + 넘어갈 때만
	LastNight = Night;
	// 날씨 색조(표 「화면 색조」): 비·눈 정도만큼 해·하늘을 눌러 푸른 회색·흰푸름으로 — 밤 톤 위에 곱한다.
	const float Scale = FMath::Lerp(1.f, SnareWeatherView::RainLightScale, LastRain) * FMath::Lerp(1.f, SnareWeatherView::SnowLightScale, LastSnow);
	const FLinearColor Tint = FMath::Lerp(FMath::Lerp(FLinearColor::White, SnareWeatherView::RainTint, LastRain), SnareWeatherView::SnowTint, LastSnow);
	if (Sun.IsValid())
	{
		Sun->SetIntensity(SunDayLux * FMath::Lerp(1.f, SnareDayNight::NightSunScale, Night) * Scale);
		Sun->SetLightColor(FMath::Lerp(SunDayColor, SnareDayNight::MoonColor, Night) * Tint);
	}
	if (Sky.IsValid())
	{
		Sky->SetIntensity(SkyDayIntensity * FMath::Lerp(1.f, SnareDayNight::NightSkyScale, Night) * Scale);
		Sky->SetLightColor(FMath::Lerp(FLinearColor::White, SnareDayNight::NightSkyColor, Night) * Tint);
	}
	if (bLightsFlip)
	{
		for (const TWeakObjectPtr<ULightComponent>& Light : NightLights)
		{
			if (Light.IsValid())
			{
				Light->SetVisibility(Night >= 0.5f);
			}
		}
	}
}
