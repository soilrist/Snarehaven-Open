#include "Player/SnareShiver.h"

namespace
{
	// 추위 떨림 소리 (DESIGN 2.2 M57-00) — 크기 0.6 · 되풀이 간격 3.0초.
	constexpr float ShiverVolume = 0.6f;
	constexpr float ShiverMinGapSeconds = 3.f;
}

SnareSound::FChangeCue SnareShiver::CueFor(float ZoneDamagePerSecond, float ColdResist, float SecondsSinceLast)
{
	SnareSound::FChangeCue Out;
	const float Effective = ZoneDamagePerSecond * (1.f - FMath::Clamp(ColdResist, 0.f, 1.f));
	if (Effective <= NoDamage || SecondsSinceLast < ShiverMinGapSeconds)
	{
		return Out;
	}
	Out.Cue = TEXT("Shiver");
	Out.Volume = ShiverVolume;
	Out.MinGapSeconds = ShiverMinGapSeconds;
	Out.bWorld = true;
	return Out;
}
