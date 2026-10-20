#include "Player/SnareLowHealth.h"

float SnareLowHealth::StateFor(float Health, float MaxHealth, bool bDowned)
{
	if (bDowned || MaxHealth <= 0.f || Health <= 0.f)
	{
		return 0.f;
	}
	const float Ratio = Health / MaxHealth;
	return FMath::Clamp((Threshold - Ratio) / Threshold, 0.f, 1.f);
}

float SnareLowHealth::EdgeOpacity(float Strength)
{
	if (Strength <= 0.f)
	{
		return 0.f;
	}
	return FMath::Lerp(EdgeOpacityMin, EdgeOpacityMax, FMath::Clamp(Strength, 0.f, 1.f));
}

float SnareLowHealth::EdgeLayerScale(int32 Layer)
{
	static constexpr float Scales[EdgeBandLayers] = { 1.f, 0.8f, 0.6f, 0.4f, 0.2f, 0.05f };
	return (Layer >= 0 && Layer < EdgeBandLayers) ? Scales[Layer] : 0.f;
}

float SnareLowHealth::PulsedEdgeOpacity(float Strength, float Wave)
{
	if (Strength <= 0.f)
	{
		return 0.f;
	}
	const float Base = EdgeOpacity(Strength);
	const float Dim = FMath::Max(Base * (1.f - EdgePulseDepth), EdgeOpacityMin);
	return FMath::Lerp(FMath::Min(Dim, Base), Base, FMath::Clamp(Wave, 0.f, 1.f));
}

float SnareLowHealth::BeatInterval(float Strength)
{
	return FMath::Lerp(BeatSlowSeconds, BeatFastSeconds, FMath::Clamp(Strength, 0.f, 1.f));
}
