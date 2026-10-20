#include "Player/SnareRevive.h"

namespace
{
	// 부활 소리 (DESIGN 2.2 M59-00) — 크기 0.9 · 간격 없음.
	constexpr float ReviveVolume = 0.9f;
}

SnareSound::FChangeCue SnareRevive::CueFor(bool bWasDowned)
{
	SnareSound::FChangeCue Out;
	if (!bWasDowned)
	{
		return Out;
	}
	Out.Cue = TEXT("Revive");
	Out.Volume = ReviveVolume;
	Out.MinGapSeconds = 0.f;
	Out.bWorld = true;
	return Out;
}

FVector SnareRevive::HomeFrontLocation(const FVector& HomeLocation, float YawDegrees, const FIntPoint& Footprint, float CellCm)
{
	const float HalfReachCm = 0.5f * CellCm * static_cast<float>(FMath::Max(1, FMath::Max(Footprint.X, Footprint.Y)));
	const FVector Forward = FRotator(0.f, YawDegrees, 0.f).Vector();
	return HomeLocation + Forward * (HalfReachCm + HomeFrontMarginCm);
}
